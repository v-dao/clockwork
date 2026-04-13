# Run Ninja from repository root (requires ninja on PATH).
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

$vkLib = Join-Path $Root "third_party/vulkan-win32-mingw/libvulkan-1.dll.a"
if (-not (Test-Path $vkLib)) {
  Write-Host "MinGW Vulkan import library missing; running scripts/bootstrap_vulkan_mingw.ps1 ..."
  & "$PSScriptRoot/bootstrap_vulkan_mingw.ps1"
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
}

$ninja = if ($env:NINJA) { $env:NINJA } else { "ninja" }
& $ninja
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (Test-Path "$Root/build/compile_commands.json") {
  Write-Host "compile_commands.json -> build/compile_commands.json"
}
Write-Host "Ninja OK. Binaries under build/cmd/"

# 回归门禁与 A12：`engine_tests` 为单一事实来源（需在仓库根目录运行以解析 `scenarios/`）。
if ($env:CLOCKWORK_SKIP_ENGINE_TESTS -eq "1") {
  Write-Host "CLOCKWORK_SKIP_ENGINE_TESTS=1 — skipped engine_tests"
  exit 0
}
$engineTests = "$Root/build/cmd/engine_tests/engine_tests.exe"
if (-not (Test-Path $engineTests)) {
  $engineTests = "$Root/build/cmd/engine_tests/engine_tests"
}
if (Test-Path $engineTests) {
  Write-Host "Running engine_tests..."
  Push-Location $Root
  try {
    & $engineTests
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  } finally {
    Pop-Location
  }
  Write-Host "engine_tests OK."
} else {
  Write-Warning "engine_tests binary not found under build/cmd/engine_tests/ (build default target to generate it)."
}

