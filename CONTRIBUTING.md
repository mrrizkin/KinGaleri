# Contributing to Kin Galeri

Thanks for taking the time to contribute! Kin Galeri is free software under the
[GPL-3.0-or-later](LICENSE), and contributions of every size are welcome:
bug reports, translations, documentation, and code.

By participating you agree that your contributions are licensed under the same
license as the project (inbound = outbound).

## Reporting bugs

Open an issue at <https://github.com/mrrizkin/KinGaleri/issues> and include:

- what you did, what you expected, and what actually happened;
- your distro and Qt version (`qmake6 -query QT_VERSION` or the About panel);
- the video backend in use (libmpv or Qt Multimedia);
- the relevant log output. Start the app from a terminal to see it.

If the bug involves a specific file, mention its type (not the private photo
itself). Please never attach personal media you do not want public.

## Building and running

The full build instructions live in [README.md](README.md#build). In short:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
./build/kin-galeri
```

Optional features (for example the libmpv backend) are controlled by the
`KG_*` CMake options documented in the README.

## Tests

Every change is expected to keep the test suite green:

```bash
cmake -S . -B build-tests -G Ninja -DKG_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-tests
cd build-tests && QT_QPA_PLATFORM=offscreen ctest
```

Add a test next to the existing ones in `tests/` when you fix a bug or add
behaviour that can be checked without a display. New settings must be covered in
`tst_appsettings.cpp`.

## QML lint

Run the gate from the repo root:

```bash
cmake -S . -B build -G Ninja        # once, to generate the lint response file
./scripts/qml-lint.sh               # exit 0 == zero warnings
```

`scripts/qml-lint.sh` must finish with **zero warnings**. Two traps it works
around, so prefer it over calling `qmllint` yourself:

* `qt5-declarative` ships its own `qmllint` at `/usr/bin/qmllint`, which shadows
the Qt 6 binary (`/usr/lib/qt6/bin/qmllint`) and produces nothing useful.
* `cmake --build build --target all_qmllint` runs qmllint through CMake, does
**not** fail on warnings, and is skipped when its stamp file is up to date — its
empty output is easy to mistake for "clean". The script reuses the response file
CMake generates (`<build>/.rcc/qmllint/kin-galeri.rsp`) and adds `-W 0`, which
turns any warning into a non-zero exit code. It also parses warnings that plain
text output hides, e.g. `block-scope-var-declaration` for a `var` in a block.

`.qmllint.ini` switches off exactly one category, `UnqualifiedAccess`, because
its 241 hits all come from nested components reading an outer `root` id, and a
proper fix means moving inline delegates into their own files. Everything else
stays enabled; if a new warning appears, fix it, or silence it narrowly with a
`// qmllint disable <category>` comment that states the reason (`MpvVideoSurface.qml`
and `ViewerOverlay.qml` have examples).

## Translations and accessibility

Two more checks that are not part of `ctest`, because both need a configured
build and neither is a unit test:

```bash
python3 scripts/i18n-audit.py       # exit 0 == every qsTr() has a catalog entry

# accessibility: what a screen reader actually sees
QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 ./build/kin-galeri --no-home-scan &
python3 scripts/a11y-walk.py Kin    # walks the live AT-SPI tree over D-Bus
```

`scripts/i18n-audit.py` exists because the local `lupdate` cannot read QML and
Qt silently falls back to the source string when a catalog entry is missing, so
an untranslated screen fails nothing. It compares every `qsTr()` in
`src/ui/qml/` against `resources/i18n/kin-galeri_id.ts`, using the QML file's
base name as the context (that is what Qt uses). The C++ half is covered by
`tests/tst_i18n.cpp`.

`scripts/a11y-walk.py` prints the roles, names and descriptions the app exposes.
There is no `accerciser` or `pyatspi` on the dev machine, but the AT-SPI bus can
be read directly, so this is the real tree rather than a guess from the source.
Run it after touching anything interactive: an unnamed or `role=label` control
is a bug, not a cosmetic detail.

## Video surfaces

There are two implementations of the same surface, chosen at runtime by
`AppSettings::videoBackend`:

| File | Backend | Built when |
|---|---|---|
| `src/ui/qml/MpvVideoSurface.qml` | libmpv via mpvqt | `-DKG_ENABLE_MPV=ON` |
| `src/ui/qml/QtVideoSurface.qml` | Qt Multimedia (the PRD default) | `-DKG_ENABLE_MULTIMEDIA=ON` |

They are separate files because QML imports are static: importing `QtMultimedia`
from the mpv surface would break the whole file in a build without that module,
and vice versa. `ViewerOverlay.qml` loads one by **file name**, not by type, for
the same reason.

Because both are driven by the same code in `ViewerOverlay.qml`, they must expose
an identical interface: `filePath`, `posterUrl`, `autoplay`, `loop`, `muted`,
`volume`, `hasVideo`, `loaded`, `ended`, `paused`, `position`, `duration`, and the
functions `togglePause()`, `replay()`, `seekTo()`, `seekRelative()`, `stop()`,
`adjustVolume()`, `toggleMute()`, `formatTime()`. Change one, change the other, and
check both configurations build - a missing property or a renamed function shows
up only at runtime, as a surface that silently fails to load.

Two Qt 6 traps worth remembering, both of which cost a debugging round: the player
object has no `muted`/`volume` (they live on a separate `AudioOutput`), and the
video connection is `MediaPlayer.videoOutput: output` rather than Qt 5's
`VideoOutput.source: player`.

## Versions and releases

Versions follow `v<year>.<ISO week>.<patch>`, the scheme this author's other
repositories use. The tag is the source of truth:

```bash
scripts/next-version.sh            # v2026.42.2 - the tag to create
scripts/next-version.sh --current  # 2026.42.2 - what the binary reports
```

The patch counts releases inside a single week, so it resets when the week rolls
over - one of those repositories went `v2026.40.8` straight to `v2026.42.0`. The
value written into `CMakeLists.txt` (`project(... VERSION ...)`) is what `--version`
and the packages report, so bump it when you tag, and keep the AppStream `<release>`
entry and the PKGBUILD's `pkgver` in step with it. The `v` belongs to the tag only.

Releasing is deliberately manual: pushing a `v*` tag runs
`.github/workflows/package.yml`, which builds and smoke-tests the AppImage, `.deb`,
Flatpak and Arch packages and attaches them, with `SHA256SUMS`, to a GitHub release.
A plain push to `main` publishes nothing.

## Code style

- C++20, Qt 6 conventions. Match the style of the file you are editing.
- 4-space indentation, braces on their own line for functions/classes.
- Prefer `QStringLiteral`/`QLatin1String` and keep hot loops allocation-light.
- User-visible strings must be wrapped in `qsTr()`/`tr()` and added to
  `resources/i18n/kin-galeri_id.ts` (edit the `.ts` **by hand** or with
  `lconvert`; do not run the `update_translations` target, the local `lupdate`
  lacks QML support). For a C++ class inside `namespace kg` the translation
  context is the metaobject name (`kg::MyClass`), never the bare class name - a
  mismatch silently drops every string back to English, so `tests/tst_i18n.cpp`
  pins the two together.
- Keep commits focused and explain the *why* in the message.

## Pull requests

1. Fork the repository and create a branch from `main`.
2. Make your change with tests where practical.
3. Run the test suite.
4. Open a pull request describing the problem and the approach.

Small, self-contained PRs are reviewed fastest. If you plan a large
refactor, open an issue first so we can agree on the direction.
