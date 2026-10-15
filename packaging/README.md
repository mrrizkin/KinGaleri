# Packaging

Four targets, all **built and run** for real. The workflow in
[`.github/workflows/package.yml`](../.github/workflows/package.yml) encodes the same
steps; the results below come from running them both in Docker containers on the dev
machine and in CI. Building is only half of it — the install and smoke jobs are what
prove an artifact works, and they have already earned their keep (see "Notes that cost
time to learn").

The version line reports the build's optional features, so any artifact can be asked
what it actually is:

```
$ kin-galeri --version
Kin Galeri 0.1.0 (Qt 6.8.2, video: qtmultimedia, ml: on)
```

| Target | Built on | How it was verified |
|---|---|---|
| AppImage | Ubuntu 24.04 + Qt 6.8 (aqtinstall) — 99.5 MB | Ran in `ubuntu:24.04` **and** `fedora:40` containers with `--appimage-extract-and-run`: QML modules, multimedia plugin and ONNX Runtime present in the bundle, GUI runs offscreen, `--version` = `… video: qtmultimedia, ml: on` |
| `.deb` | Debian 13 (`debian:trixie`, Qt 6.8.2) — 914 KB | Installed into a **clean** `debian:trixie` (only its declared dependencies), then plugins/QML modules asserted, GUI run offscreen, `desktop-file-validate` + `appstreamcli validate --no-net` |
| Flatpak | KDE 6.8 runtime — 638 KB bundle | `flatpak-builder` → `flatpak build-bundle`; ONNX Runtime asserted inside `/app` |
| Arch | `archlinux:base-devel` — 874 KB | `makepkg -sf` as a non-root user, against the real GitHub URL (see `arch/README.md` for AUR) |

## Optional features per target

| Target | Video backend | Face grouping | ONNX Runtime from |
|---|---|---|---|
| AppImage | Qt Multimedia | **on** | the pinned 1.20.1 archive, bundled |
| `.deb` | Qt Multimedia | **on** | Debian's `libonnxruntime1.21`, declared as a dependency |
| Flatpak | Qt Multimedia | **on** | the pinned 1.20.1 archive, built as a module into `/app` |
| Arch (AUR) | **mpv** (mpvqt is in `extra`) | **on** | the pinned 1.20.1 archive, installed to `/usr/lib` |

Face grouping is ONNX Runtime, and it is on in every target by request. A build that
cannot configure it now fails outright instead of quietly dropping the feature, and
CI asserts `--version | grep 'ml: on'` on the artifacts — the version line is the
cheapest place to prove a package has what it claims.

Debian is the one target that does not bundle ONNX Runtime: Debian 13 packages
`libonnxruntime1.21`, so the `.deb` links it and lets `dpkg-shlibdeps` derive the
dependency. Its SONAME is `libonnxruntime.so.1.21`, so the upstream 1.20.1 archive
could not satisfy it anyway — which is why the other three carry their own copy.

mpv is only built where mpvqt is packaged. Debian, Ubuntu and Flathub do not ship
mpvqt, so those targets use the Qt Multimedia backend, which PRD §10.1 makes the
default regardless; Arch does have it, so the AUR package gets mpv. Asking for
`-DKG_VIDEO_BACKEND=mpv` on a machine without mpvqt is now an error rather than a
silent fallback, so a request for mpv is never quietly downgraded to something else.

The face models themselves (about 37 MB) are still a user action: the Settings page
downloads them on request rather than shipping them, which keeps the packages small.

## What the AppImage expects from the host

An AppImage bundles Qt, the QML modules, the image-format and multimedia plugins,
mpv/mpvqt, ONNX Runtime and SQLite — but it deliberately does **not** bundle what
linuxdeploy calls system libraries: libc, the X11/xcb stack, OpenGL (libglvnd), the
font stack (fontconfig/freetype/harfbuzz/fribidi), libdbus, and the **audio
stack**. ALSA, JACK and PipeWire are in that last category for a reason: a sound
client library has to match the sound server actually running on the machine, so
shipping our own would be the wrong fix for a missing one. Ubuntu's libmpv links
all three directly, which is why they only became a requirement when mpv was added.
Every Ubuntu or Fedora desktop already has them; a bare container has none, which is
why a naked `ubuntu:24.04` image is the wrong smoke test. The CI job installs exactly
that baseline and not a single `libqt6*` package, so the test still fails if a Qt
library ever stops being bundled.

If "no manual dependencies" ever needs to be true without that asterisk, the answer
is not to bundle ALSA but to build a **minimal libmpv** from source with the audio
backends reduced to what a desktop certainly provides. That is a larger change than
it sounds (mpv pulls in libass, libplacebo and friends), and it is the same work the
Flatpak target would need.

## Why the AppImage is built on Ubuntu 24.04

An AppImage inherits the glibc of the machine that built it, so it has to be built
on the *oldest* supported distro. Ubuntu 24.04 is that baseline, but its distro Qt
is 6.4.2 — below this project's minimum of 6.5. The build therefore installs Qt
6.8 with `aqtinstall` and lets linuxdeploy bundle it, instead of linking the
distro's Qt.

The `.deb` goes the other way: it links the distro's Qt on purpose, so it is built
on Debian 13 (Qt 6.8.2) and declares the matching runtime dependencies. A `.deb`
from a Qt 6.8 host will not install on a Qt 6.4 distro — that is what the AppImage
and Flatpak are for.

## Local builds

```bash
packaging/appimage/build.sh      # needs Qt 6.5+ on PATH + network on first run
packaging/deb/build-deb.sh       # needs dpkg-deb, dpkg-shlibdeps and file
CMAKE_ARGS=-DKG_ENABLE_MPV=OFF packaging/deb/build-deb.sh   # on a distro without mpvqt

flatpak-builder --user --force-clean --repo=repo \
    --install-deps-from=flathub build-flatpak packaging/flatpak/io.github.mrrizkin.KinGaleri.yml

cd packaging/arch && makepkg -si
```

`build.sh` and `build-deb.sh` both honour `BUILD_DIR=` to keep the artifacts out
of the source tree.

## Notes that cost time to learn

* The AppImage has to bundle the `offscreen` platform plugin itself:
  linuxdeploy's Qt plugin ships `xcb` only, which makes the documented `--no-ui`
  mode impossible without an X server.
* `PRUNE_QT_SQLDRIVERS=1` keeps only `libqsqlite.so`. Without it the Qt plugin
  tries to deploy every SQL driver and dies on the first missing client library
  (`libpq.so.5`, `libmimerapi.so`, ...). It has to run *after* configure, because
  `Qt6SqlConfig.cmake` references files that would have been deleted.
* Qt must be on `LD_LIBRARY_PATH` when it lives outside the system prefix,
  otherwise linuxdeploy reports a missing `libQt6QuickControls2.so.6`.
* `flatpak-builder` needs its state dir on the same filesystem as the build and
  repo dirs (`--state-dir`), and `appstreamcli compose` needs an SVG rasterizer
  (`librsvg2-common` on Debian/Ubuntu) — without it, compose fails on a perfectly
  valid SVG icon while `appstreamcli validate` still passes.
* CPack's `SHLIBDEPS` needs the `file` utility, otherwise CPack fails with a
  misleading `CPackDeb.cmake` message — `build-deb.sh` checks for it up front.
* `makepkg` needs `-s` to pull dependencies, and `archlinux:base-devel` has no
  `git`, so the Arch job installs it and relaxes `safe.directory` for the
  bind-mounted source.
* `mpvqt` is an Arch package; Debian and the KDE runtime do not ship it. Those
  builds pass `-DKG_ENABLE_MPV=OFF` and play video through `QtVideoSurface.qml`
  (`MediaPlayer` + `VideoOutput` + `AudioOutput`), which is the PRD 10.1 default.
  Keep it in step with `MpvVideoSurface.qml`: `ViewerOverlay` drives both through
  the same property and function names and picks between them by
  `app.videoBackend`.
* Qt Multimedia loads its QML module and its FFmpeg plugin with `dlopen`, so
  `dpkg-shlibdeps` cannot see them and linking `Qt6::Multimedia` is not enough
  either (the linker drops it under `--as-needed` when no C++ symbol uses it).
  `CPACK_DEBIAN_PACKAGE_DEPENDS` therefore names `libqt6multimedia6` and
  `qml6-module-qtmultimedia` by hand - the same reason it names the QSQLITE
  driver, the SVG plugin and the `qml6-module-qtquick-*` modules.
* An AppImage is only useful if it carries the QML modules: `usr/qml/QtQuick`,
  `usr/qml/QtMultimedia`. linuxdeploy's Qt plugin finds those by scanning the
  app's QML imports - but it does **not** find the plugins that module loads back
  into C++. Measured on the first bundle that had the new video surface: the QML
  module was there, while `usr/plugins/multimedia/`, `usr/plugins/iconengines/`
  and `libQt6Multimedia.so.6` were all absent, because the app never references a
  Multimedia symbol itself so the linker drops `Qt6::Multimedia` from
  `DT_NEEDED`, leaving linuxdeploy no dependency graph to walk. That bundle could
  compile the video surface, load it, and then play nothing.
  `packaging/appimage/build.sh` therefore copies those two plugin categories into
  the AppDir before linuxdeploy runs (the same trick it already uses for the
  offscreen platform plugin), and the smoke test asserts they and the library are
  present rather than trusting the scanner.
* linuxdeploy and its Qt plugin are themselves AppImages. On a runner without
  FUSE, `APPIMAGE_EXTRACT_AND_RUN=1` makes the nested invocations work; the
  script already passes `--appimage-extract-and-run` to linuxdeploy itself.
* `makepkg` refuses to run as root, which is why the Arch job creates a
  `builder` user rather than building in the container's default root shell.
