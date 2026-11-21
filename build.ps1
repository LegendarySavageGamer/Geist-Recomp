# GeistNative Windows build. Run from an "x64 Native Tools Command Prompt for VS 2022" PowerShell.
param(
  [string]$Iso = "$PSScriptRoot\..\Geist (USA).iso",
  [string]$Config = "Release"
)
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
$base = (Get-Content patches\gcn-recomp.BASE_COMMIT).Split(" ")[0]
if (-not (Test-Path gcn-recomp\CMakeLists.txt)) {
  git clone https://github.com/ethannicholas/gcn-recomp gcn-recomp
  if ($LASTEXITCODE) { throw 'Could not obtain the runtime source.' }
  git -C gcn-recomp checkout $base
  if ($LASTEXITCODE) { throw 'Could not select the runtime revision.' }
  Get-ChildItem patches\*.patch | Sort-Object Name | ForEach-Object {
    git -C gcn-recomp apply --whitespace=nowarn $_.FullName
    if ($LASTEXITCODE) { throw "Could not apply $($_.Name)." }
  }
}
# Release source archives include the patched runtime. Use it directly; resetting
# it here discarded release fixes and local work before every rebuild.
if (-not (Test-Path $Iso)) { throw "Disc image not found: $Iso (pass -Iso <path>)" }
cmake -S . -B build\win -G Ninja -DCMAKE_BUILD_TYPE=$Config -DGAME_ISO="$Iso"
ninja -C build\win
Write-Host "Built: build\win\geist.exe  (run: build\win\geist.exe `"$Iso`")"
