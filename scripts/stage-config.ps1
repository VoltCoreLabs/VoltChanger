param([Parameter(Mandatory=$true)][string]$Workspace, [Parameter(Mandatory=$true)][string]$Destination)
$ErrorActionPreference = 'Stop'
$folders = @((Join-Path $Workspace 'build/native/Release'), (Join-Path $Workspace 'build/native/package/Release'), (Join-Path $Workspace 'dist/VoltChanger'))
$packages = Join-Path $Workspace 'build/native/package'
if (Test-Path -LiteralPath $packages) {
    $folders += @(Get-ChildItem -LiteralPath $packages -Directory | Where-Object Name -Match '^[0-9a-f]{12}$' | ForEach-Object { Join-Path $_.FullName 'Release' })
}
$folders += @(Get-ChildItem -LiteralPath $Workspace -Directory -Filter 'dist*' | ForEach-Object FullName)
foreach ($name in @('ovr_skins.cfg', 'ovr_plus.cfg', 'ovr_hud.cfg')) {
    $target = Join-Path $Destination $name
    if (Test-Path -LiteralPath $target) { continue }
    $sources = @($folders | ForEach-Object { $candidate = Join-Path $_ $name; if (Test-Path -LiteralPath $candidate) { Get-Item -LiteralPath $candidate } })
    $source = $sources | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($source) { Copy-Item -LiteralPath $source.FullName -Destination $target }
}
