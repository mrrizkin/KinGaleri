# Pemaketan

Empat target rilis, semuanya dibangun dan diuji di CI (`.github/workflows/package.yml`).
Alatnya tidak perlu ada di mesin developer: kalau `linuxdeploy`, `flatpak-builder`,
`dpkg-deb`, atau `rpmbuild` tidak terpasang, Docker dan CI yang menjadi verifier.

| Target | Dibangun di | Video | Face grouping |
|---|---|---|---|
| `.deb` | `debian:trixie` | mpv — `libmpvqt1` + `libmpv2` dari Debian | ON — `libonnxruntime1.21` dari Debian |
| AppImage | `ubuntu-24.04` + Qt 6.8 (aqtinstall) | mpv — mpvqt 1.2.0 + ECM 6.15.0 dari sumber | ON — ONNX Runtime 1.20.1 dibundel |
| Flatpak | runtime `org.kde.Platform` 6.8 | Qt Multimedia | ON — ONNX Runtime 1.20.1 sebagai modul |
| Arch | `archlinux:base-devel` | mpv — `mpvqt` dari `extra` | ON — ONNX Runtime 1.20.1 dibundel |

## Kenapa begini

**AppImage harus dibangun di Ubuntu 24.04, bukan Fedora atau Debian 13.** glibc
adalah batas bawah kompatibilitas AppImage, dan Ubuntu 24.04 (glibc 2.39) adalah
distro tertua yang ingin didukung. Masalahnya Qt di sana 6.4.2 — di bawah minimum
proyek — jadi Qt 6.8 dipasang di sampingnya lewat `aqtinstall` dan ikut dibundel.
Membangunnya di Debian 13 (glibc 2.41) akan menghasilkan biner yang tidak jalan
di Ubuntu 24.04.

**`.deb` menargetkan Debian 13** karena di situlah Qt 6.8.2, `libmpvqt1`, dan
`libonnxruntime1.21` semuanya tersedia sebagai paket distro — tidak ada yang
di-vendor.

**Flatpak sengaja memakai Qt Multimedia.** Runtime KDE 6.8 tidak membawa libmpv
maupun mpvqt, dan Flatpak memang tidak boleh memakai pustaka milik host:
aplikasi membawa salinannya sendiri. Menambahkan mpv berarti membangun libmpv
beserta ketergantungannya (libplacebo, libass, dan seterusnya) sebagai modul,
di samping mpvqt yang tetap harus dibangun sendiri. Qt Multimedia adalah backend
default PRD dan sudah berfungsi penuh, jadi pilihannya sadar: satu target memakai
backend berbeda, bukan satu target tanpa pemutaran video.

**PKGBUILD Arch memakai paket distro.** `PKGBUILD` (`kin-galeri-git`) bergantung pada
`libmpvqt1`-nya Arch (`mpvqt`) dan hanya membundel ONNX Runtime, karena Arch
tidak punya paket onnxruntime sama sekali. `pkgver()` memakai `git describe`,
jadi build dari tag tepat `2026.42.0` dan build di antaranya mendapat sufiks
`.r<n>.g<sha>`.

**Paket ini belum diterbitkan ke AUR, dan sekarang memang belum bisa:** registrasi
akun AUR sedang ditutup sementara (pihak AUR membatasi pendaftaran karena banjir akun
otomatis), jadi paketnya tidak bisa dibuat di sana sama sekali — bukan karena ada yang
perlu diperbaiki di sini. Saat pendaftaran dibuka kembali, satu-satunya prasyarat adalah
akun, dan seluruh pekerjaannya hanya `git push` sekali (`packaging/arch/README.md`
berisi langkahnya, belum pernah dijalankan). Sampai itu terjadi, paket Arch dipasang dari
`.pkg.tar.zst` di halaman rilis, atau dibangun sendiri dengan
`cd packaging/arch && makepkg -si`.

## Pelajaran yang mahal: dependensi yang tidak terlihat

`dpkg-shlibdeps` hanya melihat `DT_NEEDED`. Apa pun yang dimuat lewat `dlopen`
tidak muncul di sana, dan itu membuat paket pertama **terbangun, terpasang, bahkan
`--version` jalan** — tapi tidak bisa membuka basis data dan tidak bisa memuat
satu pun berkas QML. Yang harus dinamai sendiri di `CPACK_DEBIAN_PACKAGE_DEPENDS`:

| Hal | Paket Debian | Kenapa tidak terlihat |
|---|---|---|
| Driver SQLite | `libqt6sql6-sqlite` | plugin `QSQLITE` di-`dlopen` |
| Plugin gambar SVG | `qt6-svg-plugins` | bukan `libqt6svg6`; itu hanya pustakanya |
| Modul QML | `qml6-module-qtqml*`, `qml6-module-qtquick*` | inti QML: tanpa ini aplikasi tidak memuat QML sama sekali |
| Qt Multimedia | `libqt6multimedia6`, `qml6-module-qtmultimedia` | tidak ada simbol C++ yang dipakai, jadi `--as-needed` membuangnya dari `DT_NEEDED` |
| ONNX Runtime | `libonnxruntime1.21` | diturunkan otomatis karena di-link langsung |

`CPack` **menggabungkan** daftar itu dengan hasil `dpkg-shlibdeps`, bukan
menggantinya, jadi SHLIBDEPS tetap dipakai untuk yang bisa dilihatnya.

Konsekuensi yang sama muncul di AppImage dan Flatpak: linuxdeploy men-deploy
dependensi biner utama dan apa yang dikenali plugin Qt, bukan dependensi berkas
yang kita taruh sendiri di AppDir. Karena itu `build.sh` menyalin eksplisit
`libQt6Multimedia`, `libMpvQt`, `libmpv`, dan `libonnxruntime*` — asumsi
sebaliknya pernah menghasilkan AppImage yang video-nya tidak bisa diputar.

## Kontrak host AppImage

AppImage membundel Qt, plugin, modul QML, dan aplikasinya — bukan sistem
operasinya. Yang **tetap** disediakan desktop:

- libc, X11/xcb, OpenGL/EGL, fontconfig/freetype — linuxdeploy menyebutnya
  "system libraries" dan menolaknya dengan sengaja;
- **stack audio**: ALSA, JACK, dan PipeWire. libmpv meng-link ketiganya secara
  langsung, jadi bundle tidak boleh membawanya sendiri (harus cocok dengan sound
  server host). Smoke test memasangnya eksplisit, dan di mesin tanpa audio stack
  yang lengkap AppImage akan gagal sebelum jendela muncul.

Kalau Anda menginginkan AppImage yang lebih ramping, membangun libmpv dengan
backend audio yang lebih sedikit adalah cara yang benar — bukan membundelnya.

## Yang diuji CI

| Job | Yang dibuktikan |
|---|---|
| `appimage` | AppImage terbentuk; `--version` menyebut `ml: on` **dan** `video: mpv` |
| `smoke-appimage` (ubuntu:24.04, fedora:40) | `usr/qml/QtMultimedia/qmldir`, `usr/plugins/multimedia/`, `usr/plugins/iconengines/`, `usr/lib/libQt6Multimedia.so.6*`, `usr/lib/libonnxruntime.so.1*`, `usr/lib/libMpvQt.so.1*`, `usr/lib/libmpv.so.2*`; GUI offscreen berjalan dan membuat basis data; tidak ada error QML di log |
| `deb` | paket terbentuk, unggahan artefak |
| `deb-install` | dipasang di `debian:trixie` **bersih** (0 paket `libqt6*` di awal) hanya dengan dependensinya, lalu: seluruh plugin/modul di tabel di atas ada, `Depends` memuat `libonnxruntime1.21` + `libmpvqt1` + `libmpv2`, `--version` jalan tanpa display, GUI offscreen, `desktop-file-validate`, `appstreamcli validate` |
| `flatpak` | `flatpak-builder` + `build-bundle`, lalu `flatpak run … --version` |
| `arch` | `makepkg -sf` sebagai pengguna non-root |

Dua pelajaran operasional yang sudah dikodekan: `--no-ui` **tidak** memuat QML,
jadi smoke test yang hanya menjalankan `--no-ui` tidak membuktikan apa pun soal
QML; dan `kin-galeri --version`/`--help` harus jalan tanpa display, yang dulu
tidak (argumen CLI diperiksa setelah `QGuiApplication` dibuat).

## Rilis

Versi mengikuti `v<tahun>.<minggu ISO>.<patch>`, patch direset tiap minggu
(`./scripts/next-version.sh` menghitung berikutnya; `--current` mencetak yang
sekarang). Versi di `CMakeLists.txt` mengalir ke `--version`, `CPACK_PACKAGE_VERSION`,
dan `pkgver`.

```bash
./scripts/next-version.sh            # v2026.42.1
git tag v2026.42.1 && git push origin v2026.42.1
```

Tag `v*` memicu `package.yml`: keempat artefak dibangun, smoke test dijalankan,
lalu `gh release create` menerbitkan AppImage + `.deb` + Flatpak + paket Arch +
`SHA256SUMS`. Push ke `main` **tidak** menerbitkan apa pun. Checksum ditulis
setelah artefak diratakan ke satu direktori, supaya `sha256sum -c SHA256SUMS`
bekerja di folder unduhan — versi pertama menulis path berprefiks subdirektori
dan gagal.

Flatpak yang diterbitkan **mereferensi** runtime, tidak membenamkannya, jadi
pengguna baru perlu sekali `flatpak install flathub org.kde.Platform//6.8`.
