[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$bash = 'C:\msys64\usr\bin\bash.exe'

if (-not (Test-Path -LiteralPath $bash)) {
    winget install --id MSYS2.MSYS2 --exact --accept-package-agreements --accept-source-agreements
}

if (-not (Test-Path -LiteralPath $bash)) {
    throw "MSYS2 installation did not provide $bash"
}

& $bash -lc @'
pacman -Syu --noconfirm &&
pacman -S --needed --noconfirm \
  git \
  mingw-w64-ucrt-x86_64-toolchain \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-qt6-base \
  mingw-w64-ucrt-x86_64-qt6-declarative \
  mingw-w64-ucrt-x86_64-qt6-shadertools \
  mingw-w64-ucrt-x86_64-qt6-tools \
  mingw-w64-ucrt-x86_64-sdl3
'@

if ($LASTEXITCODE -ne 0) {
    throw "MSYS2 dependency installation failed with exit code $LASTEXITCODE"
}
