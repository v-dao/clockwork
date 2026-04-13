# Generate libvulkan-1.dll.a from third_party/vulkan-win32-mingw/vulkan-1.def (MinGW dlltool).
# Does not commit binaries; run once per clone (or use scripts/build.ps1, which invokes this if missing).
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Dir = Join-Path $Root "third_party/vulkan-win32-mingw"
$Def = Join-Path $Dir "vulkan-1.def"
$Out = Join-Path $Dir "libvulkan-1.dll.a"

if (-not (Test-Path $Def)) {
  Write-Error "bootstrap_vulkan_mingw: missing $Def"
  exit 1
}

$dlltool = $env:MINGW_DLLTOOL
if (-not $dlltool -or -not (Test-Path $dlltool)) {
  $cmd = Get-Command dlltool -ErrorAction SilentlyContinue
  if ($cmd) {
    $dlltool = $cmd.Path
  }
}
if (-not $dlltool) {
  Write-Error @"
bootstrap_vulkan_mingw: dlltool not found. Install MinGW-w64 and add its bin to PATH,
or set MINGW_DLLTOOL to the full path of dlltool.exe (e.g. C:\msys64\ucrt64\bin\dlltool.exe).
"@
  exit 1
}

New-Item -ItemType Directory -Force -Path $Dir | Out-Null
Push-Location $Dir
try {
  & $dlltool -d "vulkan-1.def" -l "libvulkan-1.dll.a" -D vulkan-1.dll
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
} finally {
  Pop-Location
}

if (-not (Test-Path $Out)) {
  Write-Error "bootstrap_vulkan_mingw: expected output missing: $Out"
  exit 1
}
Write-Host "bootstrap_vulkan_mingw: wrote $Out"
