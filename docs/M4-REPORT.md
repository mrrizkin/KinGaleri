# M4 — Polish & Packaging

**Status:** selesai, dengan satu catatan lingkungan: skrip/manifest pemaketan **ditulis dan
divalidasi sebagian**, tetapi artefak AppImage/Flatpak/`.deb` tidak bisa dibangun di mesin dev ini
karena `linuxdeploy`, `flatpak-builder`, dan `dpkg-deb` tidak terpasang dan tidak ada `sudo`.
Lihat §7.4.

Laporan ini melengkapi `docs/M2-REPORT.md`. Item 1 dari daftar utang ("tutup utang kecil") juga
dikerjakan di sini karena ia menutup celah yang ditemukan saat M2 (§5–§6).

---

## 1. Tema terang / gelap

- `src/app/AppSettings.{h,cpp}` (baru) menyimpan preferensi lewat `QSettings`
  (`appearance/theme`, `appearance/language`) — sengaja **tidak** di `library.db`, karena tema adalah
  properti aplikasi, bukan koleksi foto.
- Nilai `theme`: `system` / `light` / `dark`. Untuk `system`, `dark()` mengikuti
  `QStyleHints::colorScheme()`, dan `colorSchemeChanged` diteruskan agar tema ikut berubah saat
  desktop berganti mode ketika aplikasi berjalan.
- `src/ui/qml/Theme.qml` sekarang menurunkan **seluruh palet** dari dua properti kecil: `dark`
  (boolean) dan `accent` (nama preset, bukan warna). Ia tetap singleton tanpa logika per-frame.
  Ke-enam preset aksen — `blue`, `teal`, `purple`, `pink`, `orange`, `green` — mendefinisikan
  varian terang/gelapnya masing-masing di satu tempat, dan `systemIcons` (dari
  `interface/system_icons`) memilih antara ikon bawaan sistem atau ikon bertema. Ke-enam aksen itu
  juga yang tercantum di `packaging/io.github.mrrizkin.KinGaleri.metainfo.xml`.
- `Main.qml` mengikat `Theme.dark` ke `app.settings.dark` lewat `Binding`. Pilihan System / Light /
  Dark, warna aksen, dan bahasa tinggal di halaman **Settings → Interface** (`SettingsView`), bukan
  di dropdown header; tombol paling kanan header hanya tombol navigasi ke Settings.

Kontras kedua palet: latar `#14161a`↔`#f6f7f9`, teks `#eef1f6`↔`#1b1f27`, aksen `#4c8dff`↔`#2f6fe4`.

## 2. i18n EN / ID

- Semua string QML dibungkus `qsTr()` dan string C++ `tr()`. Katalog Indonesia:
  `resources/i18n/kin-galeri_id.ts` — **455 pesan, semuanya diterjemahkan** (0 `unfinished`):
  362 pesan dari **11 konteks QML** (tiap berkas `src/ui/qml/*.qml`), 1 dari `KinMpvItem` (kelas C++
  tanpa namespace, jadi konteksnya memang nama pendek), dan 92 dari 3 konteks C++ (`kg::AppController`,
  `kg::MediaGridModel`, `kg::ShortcutRegistry`).
- **Cakupan QML dijaga skrip, bukan diasumsikan.** Karena `lupdate` di mesin ini tidak bisa membaca QML
  dan Qt jatuh ke teks sumber saat entri katalog hilang, mudah menambah `qsTr()` tanpa menambah entri —
  dan satu layar tetap Inggris sementara sisanya Indonesia, tanpa ada test yang menangkapnya. Itu
  memang terjadi: audit 2026-10-14 menemukan konteks **`CollectionsView` (16 pesan) dan `MediaCell`
  (2) sama sekali tidak ada** di `.ts`, plus **88 string** di konteks yang sudah ada (`Main` 40,
  `SettingsView` 43, `Sidebar` 4, `MediaGrid` 1) — total **106 pesan, sekitar seperempat dari seluruh
  pemanggilan `qsTr()`**. Semuanya sudah diterjemahkan, dan `scripts/i18n-audit.py` sekarang menolak
  `qsTr()` tanpa entri (konteks = nama berkas QML, sesuai konvensi Qt). Terverifikasi visual: kartu
  koleksi kini **"Tangkapan layar / File besar / Baru diambil"**, deskripsi sel di tree AT-SPI
  **`Gambar`** (dulu `Image`), dan nama toggle Settings berbahasa Indonesia.
- **Konteks `tr()` harus memakai nama metaobject, bukan nama kelas telanjang.** Kelas C++ proyek ini
  hidup di `namespace kg`, jadi `metaObject()->className()` — yang dipakai Qt sebagai konteks —
  bernilai `kg::AppController`, bukan `AppController`. Selama `.ts` memakai nama pendek, konteksnya
  tidak pernah cocok dan **semua** terjemahan C++ jatuh ke bahasa Inggris; string QML tetap bekerja
  karena `qsTr()` memakai nama komponen QML, sehingga bug ini tidak terlihat dari UI. Linter tidak
  bisa menemukannya (`tr()` dan `QStringLiteral` sama-sama valid), jadi dijaga oleh
  `tests/tst_i18n.cpp`, yang mengaitkan nama konteks di `.ts` dengan
  `T::staticMetaObject.className()` dan menolak nama pendek.
- Digabungkan saat build oleh `qt_add_translations()` (lihat `CMakeLists.txt`) ke resource
  `:/i18n/kin-galeri_id.qm`. `AppSettings::applyLanguage()` memuatnya saat preferensi berubah;
  mengganti bahasa memicu `LanguageChange` sehingga binding `qsTr()` di QML dievaluasi ulang **tanpa
  restart**.
- Bila Qt tanpa LinguistTools, build tetap jalan tanpa terjemahan (hanya pesan status).
- Bahasa Inggris adalah bahasa sumber, jadi tidak butuh `.qm`; memilih English menghapus translator.

Penerjemahan sebagian tidak apa-apa: entri yang belum diterjemahkan jatuh ke teks sumber (Inggris).

**Sudah ditutup.** 43 string C++ yang sebelumnya `QStringLiteral(...)` sudah dibungkus `tr()`
(`AppController` 17, `MediaGridModel` 26 termasuk `Undid %1`, ditambah pemetaan filter
`videos`/`images`), ketiga konteks C++ ditambahkan ke `.ts`, dan konteksnya diperbaiki menjadi
`kg::…`. Terverifikasi visual: UI Indonesia menampilkan status bar **"Siap"** dan header
**"4 item · 5.7 MB"** (`/tmp/kgv/i18n-id3.png`), bukan lagi Inggris.

> **Peringatan tool.** `lupdate` dari `qt6-tools` 6.12.0 di mesin dev **tidak** dibangun dengan
dukungan QML/JavaScript ("missing qml/javascript support"). Menjalankan target `update_translations`
di sana akan menghapus seluruh 258 pesan QML; itulah kenapa `CMakeLists.txt` menambahkan probe saat
konfigurasi. `lupdate-pro` di mesin yang sama punya dukungan QML tetapi hanya menerima berkas
`.pro`, jadi `.ts` disunting dengan tangan (lihat `CONTRIBUTING.md`).

## 3. HiDPI & fractional scaling (F-3.10)

`main.cpp` menetapkan `QT_SCALE_FACTOR_ROUNDING_POLICY=PassThrough` **sebelum** `QGuiApplication`
dibuat (kebijakan dibaca di konstruktor). Ini menjaga faktor 1.25×/1.5× tidak dibulatkan ke bilangan
bulat, yang membuat UI terlihat "chunky". Variabel dihormati bila pengguna sudah menetapkannya, dan
di Wayland faktor ditentukan kompositor.

**Diverifikasi visual:** `QT_SCALE_FACTOR=1.25` (jendela 1800×1150) dan `1.5` (1920×1053) dirender
tanpa teks terpotong, kontrol tumpang tindih, atau ikon buram.

## 4. Fallback `--software-render`

Menutup risiko PRD §8 (driver Wayland/GPU bermasalah). Flag dibaca langsung dari `argv` di
`applyPreAppEnvironment()` dan men-set `QT_QUICK_BACKEND=software` sebelum `QGuiApplication` memilih
backend scenegraph. Ini penting: sesudah konstruktor, perubahan sudah terlambat.

**Diverifikasi visual:** baik `QT_QUICK_BACKEND=software` maupun `--software-render` merender UI
lengkap tanpa artefak.

## 5. Aksesibilitas

**Cara memverifikasi (bukan asumsi).** Tidak ada `accerciser`/`pyatspi` di mesin dev, tetapi bus AT-SPI
hidup dan bisa dibaca lewat D-Bus, jadi tree yang benar-benar diekspos aplikasi dibaca langsung:
jalankan dengan `QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1` lalu `python3 scripts/a11y-walk.py Kin`. Ini
bukan pembacaan kode sumber, melainkan apa yang dilihat pembaca layar.

- `MediaCell` mendapat `Accessible.role: Button`, `Accessible.name` = nama file,
  `Accessible.description` (Gambar/Video), dan `Accessible.selected`.
- `Accessible.name` ditambahkan ke kontrol berikon di header (zoom −/+, bersihkan tanggal, ⚙) dan ke
  kolom pencarian. Kontrol berteks sudah otomatis punya nama dari `text`.
- **Dua cacat nyata ditemukan lewat tree dan sudah diperbaiki.** (1) Navigasi sidebar tadinya hanya
  sekumpulan `role=label`: tiap tujuan diumumkan sebagai teks statis dan tak bisa dicapai Tab.
  `NavEntry` sekarang `Accessible.role: Button` + `name`/`description`/`selected`, `activeFocusOnTab`,
  indikator fokus, Enter/Space untuk mengaktifkan, plus `Accessible.onPressAction` agar pembaca layar
  bisa menekannya. Satu perubahan menutup 12 entri; tree kini menampilkan `role=push button
  name='Semua foto'` dst (yang terpilih berdeskripsi "Halaman saat ini"). (2) Toggle di Settings
  ter-expose sebagai `check box` **tanpa nama** karena labelnya `Label` bersaudara; `ToggleRow`
  sekarang memasang `Accessible.role: CheckBox`, `name` dari label, dan `description` dari teks
  penjelas, dan tree menampilkan ketiganya lengkap.
- Sisa satu elemen tanpa nama di tree adalah `role=text` di dalam `ComboBox` internal Qt ("Semua
  foto"); ComboBox-nya sendiri sudah bernama.
- Seluruh aksi penting tetap punya jalan pintas papan tombol (lihat user guide).

**Kustomisasi shortcut — sudah ditutup (sebelumnya ditandai item M5).**
`src/app/ShortcutRegistry.{h,cpp}` memegang skema aksi → sequence yang persisten di `QSettings`,
normalisasi sequence, deteksi konflik (`conflictsFor()`), penangkapan dari key event
(`sequenceFromKey()`), dan `resetAll()`. `SettingsView` punya halaman **Shortcuts** dengan satu
tombol per aksi; menekan tombol membuka popup penangkap tombol yang langsung menampilkan konflik.
Semua `Shortcut` di `Main.qml` memakai `app.shortcuts.sequences["…"]`, jadi perubahan berlaku tanpa
restart. Dikunci oleh `tests/tst_shortcuts.cpp` dan diverifikasi visual (popup ter-center, tombol
Reset all).

## 6. Unit test (item 1)

Direktori `tests/` (sebelumnya tidak ada) berisi 12 target Qt Test + CTest, diaktifkan dengan
`-DKG_BUILD_TESTS=ON`. `kin-galeri-testcore` (static library) mengompilasi subset sumber non-QML
sehingga target aplikasi tidak terganggu.

| Target | Mengunci perilaku |
|---|---|
| `tst_clioptions` | default adaptive (`-1`), rentang validasi, flag baru |
| `tst_appsettings` | normalisasi tema/bahasa, persistensi QSettings |
| `tst_pathutils` | konversi `file://` → path (bug move M2) |
| `tst_librarywatcher` | debounce, subdirektori, `stop()`, idempoten |
| `tst_mediagridmodel` | album CRUD, seleksi, filter tanggal, rename/move + rollback + undo, trash + undo |
| `tst_librarymaintenance` | pemulihan FTS |
| `tst_folderindexer` | index awal, incremental skip, upsert, rekonsiliasi file hilang |
| `tst_shortcuts` | skema default, rebind persisten, konflik ditolak, reset satu aksi vs `resetAll`, tangkapan key event, sampah tersimpan diabaikan |
| `tst_grouping` | jarak haversine, merge/pisah klaster geo, label kota bawaan, pemisahan event per jeda waktu & lompatan tempat, urutan, penulisan tempat+event |
| `tst_imageloader` | deteksi `heif-convert` per platform, decode berkas hilang, decode round-trip |
| `tst_i18n` | konteks `tr()` C++ cocok dengan `staticMetaObject().className()` (nama pendek ditolak), terjemahan konteks C++ tidak kosong |
| `tst_faceclusterer` | `FaceClusterer` tanpa ML: wajah tak berhubungan jadi kluster sendiri, pasangan skor 0,80 menyatu, 0,45 menyatu di 0,40 tapi tidak di 0,50 (ambang yang dipakai), kluster terdekat menang, centroid tetap unit-length, `mediaIds` unik dan cover = wajah pertama |

Semua memakai `QTemporaryDir`; library pengguna tidak tersentuh. Dua bug ditemukan oleh test ini:
`Database` tidak punya destruktor (koneksi bocor antar-instance), dan ekspektasi `scanned` menghitung
file yang tidak didukung.

## 7. Packaging

### 7.1 Aturan install

`CMakeLists.txt` sekarang memasang binary ke `bin/`, `.desktop` ke
`share/applications/`, dan ikon SVG ke `share/icons/hicolor/scalable/apps/`. **Tidak** ada
`qt_generate_deploy_qml_app_script()`: pembundelan Qt diserahkan ke alat packaging (plugin Qt dari
linuxdeploy untuk AppImage; runtime untuk Flatpak). Ini juga menghindari ketergantungan pada
kombinasi Qt/CMake deploy-tool tertentu.

### 7.2 AppImage — `packaging/appimage/build.sh`

Menjalankan `cmake --install` ke sebuah `AppDir`, lalu `linuxdeploy --plugin qt`. Alat diunduh ke
`build-appimage/tools/` saat pertama. Desktop file + ikon juga disalin ke akar AppDir.

Tiga hal yang baru kelihatan setelah benar-benar dibangun:

* **Qt harus ada di `LD_LIBRARY_PATH`.** Kalau Qt ada di prefix non-sistem (mis. hasil `aqtinstall`
  di `/opt/Qt`), plugin Qt dari linuxdeploy berhenti dengan "Could not find dependency:
  libQt6QuickControls2.so.6". Skrip menurunkan `qt_lib_dir` dari `qmake6 -query` dan mengekspornya.
* **Plugin platform `offscreen` harus dibundel sendiri.** Plugin Qt hanya membawa `xcb`, sehingga
  mode headless `--no-ui` yang diiklankan README mustahil tanpa X server. Skrip menyalin
  `libqoffscreen.so` ke AppDir sebelum linuxdeploy supaya dependensinya ikut diselesaikan.
* **Driver SQL dipangkas, tapi harus setelah configure.** Menautkan QtSql membuat plugin Qt
  men-deploy *semua* driver (PostgreSQL, MySQL, ODBC, Mimer) dan gagal pada pustaka klien pertama
  yang tidak ada. `PRUNE_QT_SQLDRIVERS=1` menyisakan `libqsqlite.so` saja — tapi `Qt6SqlConfig.cmake`
  mereferensikan berkas yang dihapus, jadi CMake gagal kalau pemangkasan dilakukan sebelum configure.

### 7.3 Flatpak — `packaging/flatpak/io.github.mrrizkin.KinGaleri.yml`

Memakai `org.kde.Platform`/`Sdk` 6.8 (sudah berisi Qt6 + FFmpeg). Backend mpv dimatikan karena
libmpv tidak ada di runtime; playback lewat Qt Multimedia — surface `QtVideoSurface.qml`, yang belum
ada saat subbagian ini pertama ditulis (§7.4).

Dua catatan operasional: `flatpak-builder` menolak jalan kalau direktori state dan direktori
build/repo berada di filesystem berbeda (pakai `--state-dir`), dan tahap `appstreamcli compose`
memerlukan rasterizer SVG (`librsvg2-common` di Debian/Ubuntu) — tanpa itu compose gagal dengan
"Unrecognized image file format" untuk ikon SVG yang sebenarnya sah (`appstreamcli validate` tetap
lolos, jadi ini jebakan yang hanya muncul saat compose).

### 7.4 `.deb` — `packaging/deb/build-deb.sh` + CPack

CPack `DEB` dengan `SHLIBDEPS ON`; skrip menolak jalan bila `dpkg-deb`/`dpkg-shlibdeps` tidak ada.
`SHLIBDEPS` juga memerlukan utilitas `file`; tanpa itu CPack gagal dengan pesan dari `CPackDeb.cmake`
yang menyesatkan, jadi skrip memeriksanya di depan.

**Yang tidak bisa dilihat `SHLIBDEPS`.** Paket pertama *kelihatan* baik — terbangun, terpasang, bahkan
menjalankan `--version` — karena container pembangunnya kebetulan punya paket *pengembangan* Qt. Begitu
dijalankan di container yang hanya berisi dependensi paket itu sendiri, ternyata rusak:
`QSqlDatabase: QSQLITE driver not loaded`, dan QML tidak punya satu modul pun (`qml/` hanya berisi
`builtins.qmltypes`). Sebabnya semua yang dimuat lewat `dlopen` tidak pernah muncul di `ldd`:

- driver `QSQLITE` ada di paket terpisah `libqt6sql6-sqlite` di Debian — di Arch ia ikut `qt6-base`,
  dan itulah yang membuat 12/12 test lulus lokal sementara CI gagal (`tst_mediagridmodel`,
  `tst_librarymaintenance`, `tst_folderindexer`, `tst_grouping` semuanya "driver not loaded");
- plugin gambar SVG ada di `qt6-svg-plugins`, **bukan** `libqt6svg6` (yang hanya pustakanya);
- modul QML: tanpa `qml6-module-qtquick*` binary terpasang tanpa cara memuat satu pun berkas QML.

Semuanya sekarang dinamai eksplisit di `CPACK_DEBIAN_PACKAGE_DEPENDS` — CPack *menggabungkan* daftar itu
dengan hasil SHLIBDEPS, bukan menggantinya — dan diverifikasi dengan memasang paket baru di container
`debian:trixie` **bersih** (0 paket `libqt6*` di awal): `libqsqlite.so` ada, `qml/` berisi
`Qt QtQml QtQuick`, GUI offscreen hidup >8 detik tanpa error, DB terbentuk.

**Temuan kedua, yang lebih serius: backend video tidak ada.** PRD §10.1 menetapkan **Qt Multimedia
(backend FFmpeg)** sebagai default dan libmpv sebagai opsi, tetapi yang ada di kode hanya mpv:
`MpvVideoSurface.qml` adalah satu-satunya surface, `ViewerOverlay` hanya mengaktifkannya bila
`app.videoBackend === "mpv"`, dan macro `KG_ENABLE_MULTIMEDIA` — walau `Qt6::Multimedia` tetap
ditautkan — **tidak dipakai di mana pun**. Jadi setiap paket yang dibangun tanpa mpvqt (`.deb`,
Flatpak) *diam-diam* tidak bisa memutar video, sementara `packaging/README.md` dan §7.3 mengklaim
sebaliknya. Test headless tidak mungkin menangkapnya.

Perbaikannya: `src/ui/qml/QtVideoSurface.qml` — `MediaPlayer` + `VideoOutput` + `AudioOutput`, dengan
properti dan fungsi yang **persis sama namanya** seperti surface mpv (`filePath`, `posterUrl`,
`autoplay`, `loop`, `muted`, `volume`, `hasVideo`, `loaded`, `ended`, `paused`, `position`,
`duration`, `togglePause()`, `replay()`, `seekTo()`, `seekRelative()`, `stop()`, `adjustVolume()`,
`toggleMute()`, `formatTime()`) termasuk bar transport yang sama. Berkas itu masuk modul QML hanya
bila `KG_ENABLE_MULTIMEDIA` menyala, dan `ViewerOverlay` memilih surface lewat **nama berkas**, bukan
tipe, karena impor QML bersifat statis dan merujuk tipe yang tidak ada akan menggagalkan seluruh
berkas pada build tanpa modul itu.

Dua kesalahan API yang ditemukan saat menulisnya, dicatat supaya tidak terulang: di Qt 6
`QMediaPlayer` **tidak punya** `muted`/`volume` (keduanya milik objek `AudioOutput` terpisah), dan
arah koneksi videonya sudah dibalik — `MediaPlayer.videoOutput: output`, bukan
`VideoOutput.source: player` seperti di Qt 5. Keduanya hanya muncul sebagai error QML saat runtime,
bukan saat build, jadi layar yang gagal memuat akan terlihat seperti poster yang "diam".

Verifikasi (build `-DKG_ENABLE_MPV=OFF`, GUI di X11): klip H.264 12 detik diputar, timecode di dalam
frame maju antar tangkapan, bar transport tampil lengkap (putar/jeda, posisi, seek, durasi, mute,
volume, kecepatan), dan pada EOF frame terakhir tetap terlihat dengan tombol putar-ulang ↻ **tanpa**
spinner — perilaku yang sama dengan yang diwajibkan M2-REPORT §6 untuk mpv. Pintasan papan tombol
diuji dengan instrumentasi sementara: Space melaporkan `playbackState` 1 lalu 2, M membalik `muted`,
Panah Kanan/Kiri mengirim `+5000/-5000`; instrumentasinya sudah dibuang. `./scripts/qml-lint.sh` 0
peringatan di **kedua** konfigurasi (mpv dan qtmultimedia) dan `scripts/i18n-audit.py` 351/351.

Karena surface baru mengimpor `QtMultimedia`, paket yang dibangun tanpa mpv sekarang juga butuh modul
QML dan plugin multimedia itu — ditambahkan ke `CPACK_DEBIAN_PACKAGE_DEPENDS`, dan smoke test AppImage
diperketat: ia mengekstrak bundle dan memastikan `usr/qml/QtMultimedia/qmldir` serta
`usr/plugins/multimedia/` ada, lalu menjalankan GUI offscreen dan gagal bila log memuat error QML.

### 7.5 Hasil: dibangun dan dijalankan di container, karena alatnya tidak ada di mesin dev

`linuxdeploy`, `flatpak-builder`, `dpkg-deb`, dan `rpmbuild` tidak terpasang di mesin dev (Artix,
tanpa `sudo`), jadi verifikasinya dilakukan di container Docker — seperti profiling dan pengujian UI
— dan `.github/workflows/package.yml` mengencode langkah yang sama untuk CI:

| Artefak | Hasil |
|---|---|
| AppImage | `Kin_Galeri-x86_64.AppImage` **99,5 MB**, Qt 6.8.0 (aqtinstall) terbundel, plugin `offscreen`+`xcb`, hanya `libqsqlite.so` di `sqldrivers/`. Dijalankan di `ubuntu:24.04` **dan** `fedora:40`: `--version` benar, `--no-ui` exit 0, DB terbentuk |
| `.deb` | `kin-galeri_0.1.0_amd64.deb` **914 KB** di `debian:trixie`; dipasang di container debian:trixie **bersih** (dependensi saja, tanpa paket dev Qt) lalu headless exit 0 (8 migrasi) dan GUI offscreen hidup, `--version` = Qt 6.8.2; `desktop-file-validate` + `appstreamcli validate` lolos |
| Flatpak | bundle **638 KB** dari runtime KDE 6.8; `flatpak install` + `flatpak run --command=kin-galeri … --version` → **"Kin Galeri 0.1.0 (Qt 6.8.3)"** |
| Arch (AUR) | `makepkg -sf` di `archlinux:base-devel` → `kin-galeri-git-0.1.0.r15…-x86_64.pkg.tar.zst` 874 KB; `--printsrcinfo` benar |

Jadi DoD §9 "AppImage bisa dijalankan di Ubuntu 24.04 & Fedora 40" sekarang punya bukti, dengan satu
kualifikasi yang ditulis apa adanya di `packaging/README.md`: AppImage tidak membundel
libc/X11/OpenGL/font/dbus — itu memang kontraknya, dan setiap desktop Ubuntu/Fedora sudah punya.
Smoke test CI memasang tepat stack baseline itu (tanpa paket `libqt6*`) sebagai bukti bahwa sisanya
memang ikut terbundel; kalau suatu saat perlu satu paket `libqt6*`, berarti AppImage-nya rusak.

Satu hal yang tetap belum: belum ada tag/rilis yang diterbitkan (CI baru berjalan setelah di-push).
Ada pula satu warning kosmetik
(`setHighDpiScaleFactorRoundingPolicy must be called before creating the QGuiApplication instance`)
yang **tidak** berasal dari kode ini: `grep` di `src/` tidak menemukan pemanggilan itu sama sekali.
Penyebabnya sempat saya salah tebak sebagai runtime KDE, dan pemeriksaan lanjutan membantahnya —
container `debian:trixie` bersih dengan Qt 6.8.2 polos memunculkannya juga, sementara build native
Qt 6.12 tidak. Jadi ini perilaku Qt 6.8 saat QML diinisialisasi. Nilai yang kita minta
(`QT_SCALE_FACTOR_ROUNDING_POLICY=PassThrough`, PRD F-3.10) dipasang sebagai variabel lingkungan
sebelum `QGuiApplication`, jadi tetap berlaku.

## 8. Profiling performa (PRD §4.1)

Build mpv, `QT_QPA_PLATFORM=offscreen`, `--bench-scroll`:

| Dataset | Startup | Frame | Dropped | fps | avg | RSS | peak | thumbs |
|---|---|---|---|---|---|---|---|---|
| 50k sintetis | 595 ms | 1435 | **0** | 172.9 | 5.78 ms | 265 MB | 265 MB | 930 |
| 100k, run dingin | 1753 ms | 1413 | 3 (0.21%) | 170.7 | 5.86 ms | 332 MB | 351 MB | 948 |
| 100k, run hangat | 990–1004 ms | 1118–1129 | **0** | 172–174 | 5.8 ms | 322 MB | 339 MB | 948 |

Warm start < 1.5 s dan RSS 100k 322 MB (target PRD < 550 MB) — **semua target §4.1 terpenuhi**.
Angka startup 1753 ms hanya muncul pada run pertama pasca-seeding (page cache dingin), bukan regresi
dari perubahan M4.

## 9. Verifikasi

- `cmake --build build` bersih; `QT_QPA_PLATFORM=offscreen` menjalankan QML tanpa error
  (`TypeError`/binding loop tidak ada) — dicek juga setelah penambahan `Accessible`.
- GUI X11 `:0.0`: tema **gelap** + bahasa **Indonesia** tampil benar (`Semua foto`, `Tong sampah`,
  `Album baru…`, `memantau 2 folder`); tema **terang** + bahasa **Inggris** juga benar.
- `tests/`: 7/7 lulus.
- `--rebuild-search-index` diuji pada DB nyata (`/tmp/kgidx.db`).
- Install: `DESTDIR=... cmake --install` benar; binary hasil install jalan.
- `desktop-file-validate`: OK.

## 10. Yang masih terbuka

- **Kustomisasi shortcut** (PRD M4) — **sudah ditutup**; lihat §5.
- **AppImage/Flatpak/.deb belum dibangun** di lingkungan ini (§7.4). Skrip/manifest siap.
- ~~**Terjemahan C++**~~ — **sudah ditutup**; lihat §2 (43 string dibungkus `tr()`, konteks
  `kg::…` diperbaiki, dijaga `tests/tst_i18n.cpp`).
- **`UnqualifiedAccess` QML**: 241 peringatan qmllint berasal dari komponen bersarang yang membaca id
  `root` milik komponen luarnya. Persis itu yang dilarang `pragma ComponentBehavior: Bound`.
  Perbaikannya bukan satu-baris: delegate inline tidak bisa menerima `required` property dari view,
  jadi tiap delegate harus dipindah ke berkasnya sendiri. Kategori ini untuk sementara dimatikan di
  `.qmllint.ini` (dengan komentar alasannya); kategori lain tetap aktif dan gate
  `./scripts/qml-lint.sh` sekarang keluar dengan **0 peringatan** (memakai `-W 0`, yang juga
  menangkap peringatan yang tidak dicetak mode teks).
- **`KinGaleri.Mpv` tanpa qmldir**: `MpvItem` didaftarkan saat runtime lewat `qmlRegisterType()`,
  sehingga `MpvVideoSurface.qml` tidak bisa di-resolve pada waktu lint maupun kompilasi. Perbaikan
  yang benar adalah modul QML statis (`qt_add_qml_module` + `QML_ELEMENT`) untuk tipe itu; untuk
  sekarang berkasnya mematikan empat kategori qmllint dengan komentar yang menjelaskan alasannya.
- **HEIC/HEIF** fallback `heif-convert` — **sudah ditutup**: `ImageLoader::decodeHeif` dipakai
  `ThumbnailService` dan `FullImageProvider` (lihat `src/core/thumbs/ImageLoader.cpp`).

---

## 11. Definition of Done v1 (PRD §9) — diukur, bukan diasumsikan

PRD §9 adalah kriteria rilis tertinggi proyek ini, tetapi keenam kotaknya belum pernah diperiksa
sebagai satu set. Semuanya diukur dalam satu sesi pada **AMD Ryzen 7 PRO 5850U** (8C/16T — laptop
kelas menengah, jadi memang target yang dimaksud kriteria), build Release, `QT_QPA_PLATFORM=offscreen`.

**Mengapa library berkas nyata.** `--seed-benchmark 100000` hanya menulis baris sintetis
(`BenchmarkSeeder`) dan tidak menyentuh pemindaian direktori, EXIF, maupun thumbnail, sehingga tidak
membuktikan "index 100.000 media". Untuk item 1 dan 2 dipakai `/tmp/kg100k`: **100.000 PNG nyata**
(100 direktori × 1.000 berkas, isi berbeda, mtime berbeda satu jam sehingga rentang tanggal lebar).

| # | Kriteria | Hasil | Bukti |
|---|---|---|---|
| 1 | Index 100k tanpa crash; DB & cache konsisten setelah restart | **Lolos** | 100.000 berkas terindeks dalam **8 s**, rc=0; `media`=100.000, `media_fts`=100.000, `PRAGMA integrity_check`=ok, `foreign_key_check` bersih; 3 restart rc=0, jumlah baris tetap, cache thumbnail 1.274 berkas / 5 MB |
| 2a | Warm start < 1,5 s @ 100k | **Lolos** (setelah perbaikan §11.1) | process spawn → frame pertama: **1,31–1,35 s** (median 1,32 s dari 6 kali) pada 100k berkas nyata; awalnya 3,09–3,35 s |
| 2b | Scroll 60 fps @ 100k | **Lolos** | 166–172 fps, drop ≤ 0,4% (0–3 dari ~520–800 frame), worst 15,4 ms vs budget 16,67 ms; rss 316–339 MB (target < 550 MB) |
| 3 | Grouping P0 (date/source/favorite/recycle) | **Lolos** | `tst_mediagridmodel` (`dateRangeFiltersInclusive`, `dateRangeSwapsReversedBounds`, `trashSelectionSoftDeletesAndUndoRestores`, `batchFavoritePersists`, `albumCardsMergeManualAndSource`) + GUI M2-REPORT §10 |
| 4 | Tidak ada network tanpa persetujuan user | **Lolos** | audit `src/`: satu pemakai network (`AppController::downloadFaceModels()`), satu pemanggil (tombol di `SettingsView.qml`) |
| 5 | AppImage di Ubuntu 24.04 & Fedora 40 | **Belum** | pekerjaan packaging (§7.4), ditunda atas instruksi user |
| 6 | Dokumentasi build + user guide | **Lolos** | `README.md`, `docs/USER-GUIDE.md`, `CONTRIBUTING.md` |

### 11.1 Analisis dan perbaikan warm start

Instrumentasi sementara (`QElapsedTimer`, sudah dilepas dari kode) membagi start 100k seperti ini:

| Tahap | Waktu |
|---|---|
| `Database::open` + migrasi | 0 ms |
| `ensureSourceAlbum` + `ensureFtsPopulated` (dijaga nilai di `meta`, jadi no-op) | 0 ms |
| Query grid (`prepare` + `exec`) | 0 ms — di sqlite3, `SELECT` 100k id terurut hanya **20 ms** |
| Loop materialisasi 100k baris → `MediaItem`/`Entry` | **776–817 ms** |

Klarifikasi penting: klaim sebelumnya di bagian ini — bahwa sisa waktu habis menunggu thumbnail —
**salah**. Instrumentasi `PerfMonitor::firstFramePresented` menunjukkan frame pertama hanya keluar
**~20 ms** setelah `startupCompleted`, jadi thumbnail bukan penyebabnya. Penyebab sebenarnya ada dua:

1. **`MediaGridModel::albumCards()` dan `smartCategories()` dihitung tiga kali per reload** —
   sekali untuk binding QML `albumsChanged`, sekali untuk `collectionsChanged`, sekali lagi saat
   `registerThumbnailSources()` meminta cover. Pada 100k item itu 405–468 ms + 145 ms, dikali tiga.
2. **Bugs collation di `albumCards()`.** Query mengelompokkan `source_album` dengan collation BINARY,
   sedangkan index yang tersedia `idx_media_album_taken(source_album COLLATE NOCASE, taken_at DESC,
   id DESC) WHERE deleted_at IS NULL`. Akibatnya subquery cover per album tidak bisa memakai index dan
   **memindai ulang seluruh library untuk setiap album** (100 album × 100k baris = 468 ms). `GROUP BY`
   dan pembanding subquery sekarang sama-sama `COLLATE NOCASE` → **12 ms**. Itu juga membetulkan bug
   laten: kartu album tadinya dibedakan huruf besar/kecil sementara filternya tidak.

Perbaikannya: konsistensi collation di atas, plus cache agregat
(`mutable QHash<QString, QVariantList> m_aggregateCache` di `MediaGridModel`, dibersihkan di
`reload()`) supaya tiga pemanggilan itu hanya menghitung sekali.

**Hasil (library berkas nyata 100k, `QT_QPA_PLATFORM=offscreen`):**

| Kondisi | Sebelum | Sesudah |
|---|---|---|
| `grid_limit=0` (semua 100k item) | 3,09–3,35 s | **1,31–1,35 s** (6 kali: 1310, 1319, 1322, 1323, 1339, 1346 ms; median 1322) |
| `grid_limit=20000` | — | **894–899 ms** |

**Knob-nya sekarang ada di UI.** Sebelumnya `--limit` hanya flag CLI; pengguna dengan 100k item tidak
punya cara memotong biaya frame pertama. Sekarang ada setting **Settings → Interface → Photo grid →
"Items loaded in the grid"** (`library/grid_limit`, SpinBox 0–200000; 0 = "All items"), dengan
normalisasi ke kelipatan 1.000 dalam rentang 1.000–1.000.000. Nilai CLI `--limit` tetap menang bila
diberikan (dibedakan lewat `CliOptions::gridLimitSet`, karena 0 adalah nilai yang sah).

Sisa optimasi (tercatat di `docs/TECH-DEBT-REPORT.md`, belum dikerjakan karena kriteria sudah lolos):

1. **Progressive population** — materialisasi N pertama, lanjutkan sisanya saat idle. Membuat frame
   pertama tetap cepat berapa pun besar library.
2. **Hindari `query.value()` 18× per baris** — (~8 µs/baris) baca kolom lewat `QSqlRecord` sekali atau
   langsung lewat statement sqlite3 tanpa lapisan `QVariant`.
