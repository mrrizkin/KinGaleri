#!/usr/bin/env bash
# Prints the version tag this commit would be released under.
#
# The scheme is the one used by this author's other repositories:
#
#     v<year>.<ISO week>.<patch>
#
# The patch counts releases *inside that single week*, so it resets to 0 when the
# week rolls over - sapasora went v2026.40.8 straight to v2026.42.0, which is what
# makes the year and the week meaningful rather than decorative.
#
#     scripts/next-version.sh            # the next tag, e.g. v2026.42.0
#     scripts/next-version.sh --current  # the version compiled into the binary
#
# Note the `v`: the tag carries it, the values written into CMakeLists.txt, the
# AppStream metadata and the PKGBUILD do not.
#
# The tag is the source of truth - this script only computes it. Releasing stays a
# deliberate act (push a tag), rather than something every push to main does.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ "${1:-}" == "--current" ]]; then
    # The version lives on its own line in the project() call.
    sed -n 's/^[[:space:]]*VERSION \([0-9][^ )]*\).*/\1/p' "${root}/CMakeLists.txt" | head -n1
    exit 0
fi

# Tags may be missing in a shallow clone or a fresh checkout; that only affects the
# patch number, and the fetch is best-effort.
git -C "${root}" fetch --tags --quiet 2>/dev/null || true

year_week="$(date -u +'%Y.%V')"
last_patch="$(git -C "${root}" tag -l "v${year_week}.*" 2>/dev/null \
    | sed -E "s/^v${year_week}\.([0-9]+)$/\1/" \
    | sort -n \
    | tail -n1)"

if [[ -z "${last_patch}" ]]; then
    patch=0
else
    patch=$((last_patch + 1))
fi

printf 'v%s.%s\n' "${year_week}" "${patch}"
