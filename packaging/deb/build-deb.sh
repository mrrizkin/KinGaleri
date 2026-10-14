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

cmake -S "${root}" -B "${build_dir}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DKG_BUILD_TESTS=OFF
cmake --build "${build_dir}"

cd "${build_dir}"
cpack -G DEB

shopt -s nullglob
debs=(kin-galeri_*.deb)
if (( ${#debs[@]} )); then
    echo "==> built ${build_dir}/${debs[0]}"
fi
