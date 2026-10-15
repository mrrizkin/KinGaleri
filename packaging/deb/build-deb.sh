#!/usr/bin/env bash
#
# Builds the .deb through CPack (which delegates to dpkg-deb).
#
# Requires: dpkg-deb + dpkg-shlibdeps (the `dpkg` and `dpkg-dev` packages) on a
# Debian-derived host. On Arch/Artix this script will stop with a clear message
# rather than produce a broken package.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../.." && pwd)"
build_dir="${BUILD_DIR:-${root}/build}"

if ! command -v dpkg-deb >/dev/null 2>&1 || ! command -v dpkg-shlibdeps >/dev/null 2>&1; then
    echo "error: dpkg-deb/dpkg-shlibdeps are missing." >&2
    echo "       Install 'dpkg' and 'dpkg-dev' (Debian/Ubuntu) to build the .deb." >&2
    exit 1
fi
# CPACK_DEBIAN_PACKAGE_SHLIBDEPS is ON, and CPackDeb derives the shared-library
# dependencies by shelling out to `file`. Without it CPack dies deep inside
# CPackDeb.cmake with an unhelpful message, so say what is actually wrong here.
if ! command -v file >/dev/null 2>&1; then
    echo "error: the 'file' utility is missing." >&2
    echo "       CPack needs it to compute the .deb dependencies (install 'file')." >&2
    exit 1
fi

# Extra CMake arguments, e.g. CMAKE_ARGS="-DKG_ENABLE_MPV=OFF" on a distro without
# mpvqt (Debian). Word-split on purpose so several options can be passed.
# shellcheck disable=SC2086
cmake -S "${root}" -B "${build_dir}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DKG_BUILD_TESTS=OFF \
    ${CMAKE_ARGS:-}
cmake --build "${build_dir}"

cd "${build_dir}"
cpack -G DEB

shopt -s nullglob
debs=(kin-galeri_*.deb)
if (( ${#debs[@]} )); then
    echo "==> built ${build_dir}/${debs[0]}"
fi
