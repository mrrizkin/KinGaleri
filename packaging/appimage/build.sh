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

# linuxdeploy resolves the binary's dependencies with the dynamic loader, so the
# Qt we built against has to be on the library search path. That is already true
# for a distro Qt; it is not true for an aqtinstall prefix such as
# /opt/Qt/6.8.0/gcc_64, where linuxdeploy otherwise stops with "Could not find
# dependency: libQt6QuickControls2.so.6".
qmake_bin="$(command -v qmake6 || command -v qmake || true)"
if [[ -n "${qmake_bin}" ]]; then
    qt_lib_dir="$(dirname "$(readlink -f "${qmake_bin}")")/../lib"
    if [[ -d "${qt_lib_dir}" ]]; then
        qt_lib_dir="$(cd "${qt_lib_dir}" && pwd)"
        export LD_LIBRARY_PATH="${qt_lib_dir}:${LD_LIBRARY_PATH:-}"
        echo "    Qt libs: ${qt_lib_dir}"
    fi
fi
qt_plugins_dir="$("${qmake_bin}" -query QT_INSTALL_PLUGINS 2>/dev/null || true)"

# linuxdeploy-plugin-qt only ships the xcb platform plugin, which makes the
# documented headless mode (`--no-ui`, used for batch runs and for the CI smoke
# test) impossible without an X server. Copy the offscreen plugin into the
# AppDir first so linuxdeploy resolves its dependencies like any other file.
if [[ -n "${qt_plugins_dir}" && -f "${qt_plugins_dir}/platforms/libqoffscreen.so" ]]; then
    install -Dm755 "${qt_plugins_dir}/platforms/libqoffscreen.so" \
        "${appdir}/usr/plugins/platforms/libqoffscreen.so"
    echo "    bundled the offscreen platform plugin"
fi

# linuxdeploy-plugin-qt finds QML modules by scanning imports, so it does bring
# QtMultimedia's QML files and libquickmultimediaplugin.so. What it does not bring
# is the piece that module loads back into C++: plugins/multimedia (the FFmpeg
# media plugin). And libQt6Multimedia is missing too, because the application never
# references a Multimedia symbol itself - the import is QML-only, so the linker
# drops Qt6::Multimedia from DT_NEEDED under --as-needed and linuxdeploy has no
# dependency graph to follow. The result is a bundle whose video surface compiles,
# loads, and then plays nothing at all.
#
# So copy both plugin categories in before linuxdeploy runs, exactly like the
# offscreen platform plugin above: once they are files in the AppDir, linuxdeploy
# resolves their dependencies (which is how libQt6Multimedia gets bundled).
# iconengines comes too, because without the SVG icon engine the themed icons in
# AppIcon.qml have no way to render.
if [[ -n "${qt_plugins_dir}" ]]; then
    for plugin_category in multimedia iconengines; do
        [[ -d "${qt_plugins_dir}/${plugin_category}" ]] || continue
        mkdir -p "${appdir}/usr/plugins/${plugin_category}"
        cp -a "${qt_plugins_dir}/${plugin_category}/." "${appdir}/usr/plugins/${plugin_category}/"
        echo "    bundled plugins/${plugin_category}"
    done
fi

# linuxdeploy-plugin-qt deploys *every* plugin in a category it decides the binary
# needs, so merely linking QtSql drags in every SQL driver in the Qt install:
# PostgreSQL, MySQL, ODBC, Mimer, ... Each one needs a client library that is
# normally absent, and deployment dies on the first missing one with e.g.
# "ERROR: Could not find dependency: libpq.so.5" or "...: libmimerapi.so".
# Setting PRUNE_QT_SQLDRIVERS=1 keeps only the SQLite driver. It is opt-in
# because it edits the Qt installation, which is fine for a throwaway CI or
# aqtinstall prefix but not for a distro Qt owned by the package manager. The app
# only ever opens SQLite.
if [[ "${PRUNE_QT_SQLDRIVERS:-0}" == "1" ]]; then
    if [[ -n "${qt_plugins_dir}" && -d "${qt_plugins_dir}/sqldrivers" ]]; then
        for driver in "${qt_plugins_dir}"/sqldrivers/*.so; do
            [[ -e "${driver}" ]] || continue
            [[ "$(basename "${driver}")" == "libqsqlite.so" ]] && continue
            rm -f "${driver}"
        done
        echo "    kept only libqsqlite.so in ${qt_plugins_dir}/sqldrivers"
    fi
fi

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
