# Fetches and bootstraps vcpkg into ./vcpkg if it isn't already there.
# Run once after cloning the repo, before configuring with CMake.
$root = Split-Path -Parent $PSScriptRoot
$vcpkgDir = Join-Path $root "vcpkg"

if (-not (Test-Path $vcpkgDir)) {
    git clone --depth 1 https://github.com/microsoft/vcpkg.git $vcpkgDir
}

& "$vcpkgDir\bootstrap-vcpkg.bat" -disableMetrics
