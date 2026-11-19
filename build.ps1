param([switch]$SkipRust, [string]$OutputDirectory = 'dist/VoltChanger')
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$output = if ([System.IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory } else { Join-Path $root $OutputDirectory }
Push-Location $root
try {
    $cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmakeCommand) { $cmake = $cmakeCommand.Source }
    else {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio C++ Build Tools with CMake.' }
        $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        $cmake = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
        if (-not (Test-Path -LiteralPath $cmake)) { throw 'CMake component is missing from Visual Studio.' }
    }
    & "$root/scripts/fetch-protos.ps1"
    & $cmake -S $root -B "$root/build/native" -A x64
    if ($LASTEXITCODE) { throw 'CMake configure failed.' }
    & $cmake --build "$root/build/native" --config Release --parallel 6
    if ($LASTEXITCODE) { throw 'DLL build failed.' }
    if (-not $SkipRust) {
        & cargo build --release --locked
        if ($LASTEXITCODE) { throw 'Rust build failed.' }
    }
    New-Item -ItemType Directory -Force $output | Out-Null
    $builtDll = (Get-Content -LiteralPath "$root/build/native/ovr-dll-path.txt" -Raw).Trim()
    Copy-Item -LiteralPath $builtDll -Destination (Join-Path $output 'ovr_skins.dll')
    foreach ($name in @('ovr_skins.cfg', 'ovr_plus.cfg', 'ovr_hud.cfg')) {
        $source = Join-Path (Split-Path $builtDll -Parent) $name
        $destinationConfig = Join-Path $output $name
        if ((Test-Path -LiteralPath $source) -and
            (-not (Test-Path -LiteralPath $destinationConfig) -or (Get-Item -LiteralPath $source).LastWriteTime -gt (Get-Item -LiteralPath $destinationConfig).LastWriteTime)) {
            if (Test-Path -LiteralPath $destinationConfig) { Copy-Item -LiteralPath $destinationConfig -Destination ($destinationConfig + '.previous') }
            Copy-Item -LiteralPath $source -Destination $destinationConfig
        }
    }
    if (-not $SkipRust) {
        Copy-Item -LiteralPath "$root/target/release/VoltChanger.exe" -Destination (Join-Path $output 'VoltChanger.exe')
    }
    Copy-Item -LiteralPath "$root/upstream/LICENSE" -Destination (Join-Path $output 'LICENSE')
    Copy-Item -LiteralPath "$root/README.md" -Destination (Join-Path $output 'README.md')
    Copy-Item -LiteralPath "$root/FUNCTIONS.md" -Destination (Join-Path $output 'FUNCTIONS.md')
    Copy-Item -LiteralPath "$root/NOTICE.md" -Destination (Join-Path $output 'NOTICE.md')
    New-Item -ItemType Directory -Force (Join-Path $output 'licenses') | Out-Null
    Copy-Item -LiteralPath "$root/upstream/src/Resource/Minhook/LICENSE.txt" -Destination (Join-Path $output 'licenses/MinHook.txt')
    Copy-Item -LiteralPath "$root/start.ps1" -Destination (Join-Path $output 'start.ps1')
    Copy-Item -LiteralPath "$root/start.cmd" -Destination (Join-Path $output 'start.cmd')
    New-Item -ItemType Directory -Force (Join-Path $output 'assistant') | Out-Null
    Get-ChildItem -LiteralPath "$root/resources/assistant" -File | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $output 'assistant')
    }
    Write-Host "Built: $output"
} finally { Pop-Location }
