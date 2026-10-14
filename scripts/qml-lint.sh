#!/usr/bin/env bash
#
# QML lint gate. Exits non-zero if qmllint reports a single warning.
#
# Why a wrapper instead of calling qmllint directly:
#
#   * `qt5-declarative` installs a Qt 5 `qmllint` at /usr/bin/qmllint that
#     shadows the Qt 6 binary (/usr/lib/qt6/bin/qmllint) on PATH and produces
#     nothing useful. This script always resolves the Qt 6 one, or honours an
#     explicit $QMLLINT.
#   * qmllint needs the include paths and the embedded .qrc resources generated
#     by a configured build. CMake writes exactly those into
#     <build>/.rcc/qmllint/kin-galeri.rsp, so we reuse that file verbatim.
#   * `-W 0` turns "more than zero warnings" into a failure exit code, which the
#     raw CMake target does not do.
#
# Usage:
#   ./scripts/qml-lint.sh [build-dir]     # default: ./build
#
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-$root/build}"
rsp="$build_dir/.rcc/qmllint/kin-galeri.rsp"

if [[ ! -f "$rsp" ]]; then
    echo "qml-lint: $rsp not found - configure the build first:" >&2
    echo "  cmake -S '$root' -B '$build_dir' -G Ninja" >&2
    exit 2
fi

qmllint_bin="${QMLLINT:-}"
if [[ -z "$qmllint_bin" ]]; then
    qt_bins="$(qtpaths6 --query QT_INSTALL_BINS 2>/dev/null || true)"
    for candidate in "${qt_bins:+$qt_bins/qmllint}" /usr/lib/qt6/bin/qmllint; do
        [[ -x "$candidate" ]] && { qmllint_bin="$candidate"; break; }
    done
fi

if [[ -z "$qmllint_bin" ]]; then
    echo "qml-lint: no Qt 6 qmllint found; set QMLLINT=/path/to/qmllint" >&2
    exit 2
fi

# Run from the repo root so .qmllint.ini (documented suppressions) applies.
cd "$root"
echo "qml-lint: $qmllint_bin"
# shellcheck disable=SC2086  # $rsp is intentionally a single @response-file token
"$qmllint_bin" "@$rsp" -W 0
