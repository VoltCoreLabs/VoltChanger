$ErrorActionPreference = 'Stop'
$exe = Join-Path $PSScriptRoot 'VoltChanger.exe'
if (-not (Test-Path -LiteralPath $exe)) { $exe = Join-Path $PSScriptRoot 'dist/VoltChanger/VoltChanger.exe' }
if (-not (Test-Path -LiteralPath $exe)) { throw 'Build the application first: powershell -ExecutionPolicy Bypass -File ./build.ps1' }
Start-Process -FilePath $exe -ArgumentList '--play' -WindowStyle Hidden
