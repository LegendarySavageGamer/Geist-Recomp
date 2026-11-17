# GeistNative Windows build. Run from an "x64 Native Tools Command Prompt for VS 2022" PowerShell.
param(
  [string]$Iso = "$PSScriptRoot\..\Geist (USA).iso",
  [string]$Config = "Release"
)
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
$base = (Get-Content patches\gcn-recomp.BASE_COMMIT).Split(" ")[0]
if (-not (Test-Path gcn-recomp\.git)) {
  git clone https://github.com/ethannicholas/gcn-recomp gcn-recomp
}
git -C gcn-recomp checkout -f $base
Get-ChildItem patches\*.patch | ForEach-Object { git -C gcn-recomp apply --whitespace=nowarn $_.FullName }
if (-not (Test-Path $Iso)) { throw "Disc image not found: $Iso (pass -Iso <path>)" }
cmake -S . -B build\win -G Ninja -DCMAKE_BUILD_TYPE=$Config -DGAME_ISO="$Iso"
ninja -C build\win
Write-Host "Built: build\win\geist.exe  (run: build\win\geist.exe `"$Iso`")"
