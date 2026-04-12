# Run Ninja from repository root (requires ninja on PATH).
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

$ninja = if ($env:NINJA) { $env:NINJA } else { "ninja" }
& $ninja
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (Test-Path "$Root/build/compile_commands.json") {
  Write-Host "compile_commands.json -> build/compile_commands.json"
}
Write-Host "Ninja OK. Binaries under build/cmd/"
