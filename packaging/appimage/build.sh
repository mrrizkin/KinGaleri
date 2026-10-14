#!/usr/bin/env bash
#
# Builds a Kin Galeri AppImage (PRD 9: runs on Ubuntu 24.04 / Fedora 40 with no
# manual dependencies).
#
# Requires: cmake, ninja, a Qt 6.5+ development install, and network access the
# first time so the two Debian-built helper tools can be downloaded:
#   - linuxdeploy           (assembles the AppDir, bundles the Qt runtime)
#   - linuxdeploy-plugin-qt (resolves QML imports and plugins)
# appimagetool is fetched by linuxdeploy itself unless already on PATH.
#
# Usage:  packaging/appimage/build.sh
#         BUILD_DIR=/tmp/kg-build AppDir=/tmp/KinGaleri.AppDir packaging/appimage/build.sh
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../.." && pwd)"

build_dir="${BUILD_DIR:-${root}/build-appimage}"
appdir="${APPDIR:-${root}/build-appimage/AppDir}"
tools_dir="${root}/build-appimage/tools"
arch="$(uname -m)"

echo "==> configure & build"
cmake -S "${root}" -B "${build_dir}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DKG_BUILD_TESTS=OFF
cmake --build "${build_dir}"

echo "==> install into ${appdir}"
rm -rf "${appdir}"
DESTDIR="${appdir}" cmake --install "${build_dir}"

# The desktop file and icon have to sit at the AppDir root as well, which is
# where appimagetool looks for them.
install -Dm644 "${root}/packaging/io.github.mrrizkin.KinGaleri.desktop" \
    "${appdir}/io.github.mrrizkin.KinGaleri.desktop"
install -Dm644 "${root}/resources/icons/io.github.mrrizkin.KinGaleri.svg" \
    "${appdir}/io.github.mrrizkin.KinGaleri.svg"

echo "==> fetch helper tools (once)"
mkdir -p "${tools_dir}"
export PATH="${tools_dir}:${PATH}"
export ARCH="${arch}"

fetch() {
    local url="$1" dest="$2"
    if [[ ! -x "$dest" ]]; then
        echo "    $url"
        curl -fL --retry 3 -o "$dest" "$url"
        chmod +x "$dest"
    fi
}

if ! command -v linuxdeploy >/dev/null 2>&1; then
    fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${arch}.AppImage" \
        "${tools_dir}/linuxdeploy"
fi
if ! command -v linuxdeploy-plugin-qt >/dev/null 2>&1; then
    fetch "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${arch}.AppImage" \
        "${tools_dir}/linuxdeploy-plugin-qt"
fi

echo "==> assemble AppImage"
# --appimage-extract-and-run keeps this working on hosts without FUSE.
export QML_SOURCES_PATHS="${root}/src/ui/qml"
linuxdeploy --appimage-extract-and-run \
    --appdir "${appdir}" \
    --plugin qt \
    --desktop-file "${root}/packaging/io.github.mrrizkin.KinGaleri.desktop" \
    --icon-file "${root}/resources/icons/io.github.mrrizkin.KinGaleri.svg" \
    --output appimage

shopt -s nullglob
built=(Kin_Galeri*.AppImage)
if (( ${#built[@]} )); then
    echo "==> built ${built[0]}"
else
    echo "==> AppImage assembled (output is in the current directory)"
fi
