# Kin Galeri

[![License: GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Linux-informational.svg)](#build)

Galeri foto & video **lokal-first** untuk Linux desktop — dibangun dengan **Qt 6 / Qt Quick (QML) + C++20**.

Tanpa server, tanpa akun, tanpa network call wajib. Library Anda tetap di disk Anda.

> **Status: M4 selesai** — versi `0.1.0`, diuji pada Qt 6.12.0 (+ libmpv/mpvqt).
> Yang sudah jalan: schema DB + migrasi, indexing folder dengan **EXIF**, thumbnail cache,
> grid bertimeline dengan **sticky header + scrollbar tanggal**, **viewer layar penuh**, pencarian **FTS5**,
> **recycle bin**, filter album **di SQL**, **poster frame video** (FFmpeg), **pemutaran video
> (libmpv)**, album manual + seleksi batch, move/rename/delete + **undo**, inotify watcher, filter
> rentang tanggal, **tema terang/gelap**, **i18n EN/ID**, HiDPI/fractional scaling, unit test, dan
> skrip pemaketan AppImage/Flatpak/.deb.
> Belum ada: People/Places/Smart (M3) dan kustomisasi shortcut.
> Laporan: [`docs/M4-REPORT.md`](docs/M4-REPORT.md) · [`docs/M2-REPORT.md`](docs/M2-REPORT.md) · [`docs/M1-REPORT.md`](docs/M1-REPORT.md) · [`docs/M0-REPORT.md`](docs/M0-REPORT.md) · [`docs/TECH-DEBT-REPORT.md`](docs/TECH-DEBT-REPORT.md) · [`docs/USER-GUIDE.md`](docs/USER-GUIDE.md) · [`PRD.md`](PRD.md).

---

## Mengapa

Galeri foto cloud itu nyaman, tapi menyimpan seluruh riwayat hidup di server orang lain bukan pilihan
yang bisa diterima semua orang. Alternatif self-hosted (Immich dan sejenisnya) benar secara privasi,
tapi tetap menuntut Docker, server yang menyala 24/7, dan perawatan.

Kin Galeri mengambil posisi berbeda: **satu aplikasi desktop, satu file SQLite, zero infrastruktur.**
Yang diambil dari galeri modern adalah *pengalamannya* — timeline yang rapi, grouping otomatis
(Camera/Screenshots/WhatsApp), dan scroll yang tetap mulus di ratusan ribu foto.

Nama *kin* berarti keluarga/kerabat — mengacu ke fitur **People grouping** yang jadi pembeda inti.

## Fitur yang sudah ada

- **Indexing folder rekursif** — multi-root, incremental (upsert berdasarkan `mtime`/`size`),
  cancelable, commit berbatch. **~1.355 file/detik** pada pengukuran M1.
- **Ekstraksi EXIF tanpa dependensi** — dimensi, kamera, lensa, ISO, aperture, exposure, focal length,
  orientasi, GPS, dan waktu capture. Tanpa Exiv2; diverifikasi **0 mismatch** terhadap PIL.
- **Rantai fallback waktu capture** — EXIF → nama file → `mtime`, dengan `taken_at_src` mencatat
  asal nilainya sehingga baris lama bisa di-reindex tanpa kehilangan informasi.
- **Timeline grid dengan pemisah hari** — "Today" / "Yesterday" / tanggal ber-locale,
  plus **sticky header** dan **scrollbar** di kanan yang menampilkan date bubble saat di-drag.
- **Halaman Album** — semua album (manual + folder/kamera) tampil sebagai grid cover dengan
  jumlah item; klik satu kartu untuk membuka grid foto album itu, tombol **← Albums** untuk kembali.
  Buat/ganti nama/hapus album langsung dari halaman ini (**+ New album…**, klik-kanan kartu).
- **Viewer layar penuh** — zoom 1–8×, pan, navigasi keyboard & klik, orientasi EXIF selalu benar
  (termasuk varian mirrored).
- **Poster frame video** — frame diekstrak langsung dengan FFmpeg di dalam thread pool thumbnail,
  tanpa konteks OpenGL, sehingga juga bekerja saat indexing `--no-ui`. Rotasi dari display matrix
  container dihormati, jadi video portrait tidak jadi miring.
- **Pemutaran video (libmpv)** — viewer memutar video lewat libmpv/mpvqt dengan transport lengkap:
  play/pause, seek, volume, mute, kecepatan 0,5–2×. Qt Multimedia + FFmpeg tetap tersedia sebagai
  backend alternatif yang bisa dipilih saat build.
- **Thumbnail cache 3 tingkat** — LRU memori → disk (sharded) → generator on-demand ber-thread-pool.
  Thumbnail muncul secara asinkron tanpa memblokir UI.
- **Pencarian full-text (FTS5)** atas nama file dan identitas kamera (`make` + `model`), dengan
  prefix matching dan debounce 220 ms. Input pengguna selalu diperlakukan sebagai data literal —
  operator FTS5 seperti `AND`/`OR`/`NEAR` tidak bisa disuntikkan. Fallback ke `LIKE` otomatis bila
  SQLite dibangun tanpa FTS5. Pada 100k item: term selektif **0,03 ms**, query luas **~45 ms**.
- **Recycle bin** — soft delete di database dengan restore dan empty. **File di disk tidak pernah disentuh.**
- **Panel detail/EXIF** per item, diambil on-demand dari DB.
- **Filter** — All / Images / Videos / Favorites, plus **filter album asal** yang dideteksi dari path
  dan diterapkan **di SQL** (bukan di memori). Daftar album di sidebar dibaca dari database, jadi album
  apa pun yang benar-benar ada di library Anda ikut muncul.
- **Favorit** — toggle dari grid, viewer, atau menu konteks.
- **Benchmark bawaan** — seeding data sintetis deterministik + scroll benchmark terukur,
  dengan output machine-readable di stderr.
- **`--no-ui`** untuk smoke test headless dan pekerjaan batch.

## Roadmap singkat

| Milestone | Isi |
|---|---|
| **M0** ✅ | Skeleton, schema + migrasi, indexing, thumbnail, grid timeline, benchmark |
| **M1** ✅ | EXIF (tanpa dependensi), sticky header + scrubber, viewer layar penuh, pencarian, recycle bin, panel detail |
| **M2** ✅ | Poster frame video (FFmpeg), pemutaran video (libmpv), album manual + seleksi batch, move/rename/delete + undo, filter rentang tanggal, inotify watcher, panel EXIF, memory budget adaptif |
| **M3** | People/Places/Smart grouping, opsional AI (`-DENABLE_ML=ON`) |
| **M4** ✅ | Tema terang/gelap + i18n EN/ID, HiDPI/fractional scaling, `--software-render`, aksesibilitas, skrip AppImage/Flatpak/.deb, unit test, user guide |
| **M5+** | Ide, belum dijanjikan |

Detail lengkap: [`PRD.md`](PRD.md) §7.

## Build

**Prasyarat:** Qt **6.5 LTS** atau lebih baru (Quick, QuickControls2, Sql, Concurrent, Multimedia),
CMake ≥ 3.21, compiler C++20, SQLite3.

**Opsional:**

- **qt6-imageformats** — plugin gambar tambahan: **WebP**, TIFF, TGA, WBMP, ICNS. Tanpa ini Qt6
  tidak bisa membaca `.webp`, sehingga file WebP tetap terindeks tapi thumbnail & viewer-nya gagal
  dimuat (muncul placeholder). Di Arch/Artix: `sudo pacman -S qt6-imageformats`.
- **mpv** + **mpvqt** — untuk pemutaran video via libmpv. Di Arch/Artix: `sudo pacman -S mpv mpvqt`
  (perhatikan: **libmpv disediakan oleh paket `mpv`**, tidak ada paket terpisah bernama `libmpv`).
  Tanpa ini, build otomatis memakai Qt Multimedia sebagai gantinya.
- **libavformat/libavcodec/libswscale** (paket `ffmpeg`) — untuk poster frame video.

Di Arch/Artix:

```bash
sudo pacman -S --needed qt6-base qt6-declarative qt6-multimedia qt6-imageformats cmake ninja gcc sqlite \
    ffmpeg mpv mpvqt
```

Build:

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

Binary: `build/kin-galeri`.

### Opsi CMake

| Opsi | Default | Efek |
|---|---|---|
| `KG_VIDEO_BACKEND` | `auto` | `auto` / `mpv` / `qtmultimedia`. `auto` memilih mpv bila tersedia, lalu Qt Multimedia. |
| `KG_ENABLE_MPV` | `ON` | Pemutaran video via libmpv (butuh `mpvqt`). Matikan untuk memaksa Qt Multimedia. |
| `KG_ENABLE_VIDEO_THUMBS` | `ON` | Poster frame video via FFmpeg. Otomatis nonaktif bila dev lib FFmpeg tidak ada. |
| `KG_ENABLE_MULTIMEDIA` | `ON` | Pemutaran video via Qt Multimedia. |
| `KG_BUILD_TESTS` | `OFF` | Build unit test (`tests/`, Qt Test + CTest). |

Contoh memaksa fallback Qt Multimedia:

```bash
cmake -S . -B build -G Ninja -DKG_VIDEO_BACKEND=qtmultimedia
```

Unit test (tidak menyentuh library pengguna — semuanya memakai `QTemporaryDir`):

```bash
cmake -S . -B build-tests -G Ninja -DKG_BUILD_TESTS=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

> Pemutaran video default memakai **libmpv** (via `mpvqt`). `QQuickFramebufferObject`, yang dipakai
> mpvqt, tidak deprecated di Qt 6.12.
>
> Backend Qt Multimedia memakai **FFmpeg**; plugin `libffmpegmediaplugin.so` harus terpasang
> (`qt6-multimedia-ffmpeg` di Arch). Aplikasi memaksa `QT_MEDIA_BACKEND=ffmpeg` saat startup.

## Menjalankan

```bash
./build/kin-galeri
```

Saat pertama kali dijalankan pada library kosong, aplikasi otomatis memakai **folder home** sebagai
root dan langsung mengindeksnya (rekursif, hanya file media). Direktori tersembunyi dan sampah
teknis (`.cache`, `.git`, `node_modules`, `__pycache__`, virtualenv, dll.) dilewati, dan metadata
dibaca paralel supaya scan pertama tetap cepat. Root ini bisa dihapus seperti root biasa; kalau
library sudah punya root, home tidak ditambahkan lagi. Matikan default tersebut dengan
`--no-home-scan` atau `KG_NO_HOME_SCAN=1`.

Verifikasi cepat tanpa perlu menyentuh foto asli — generate 50.000 baris sintetis lalu scroll otomatis:

```bash
./build/kin-galeri --db /tmp/kg-bench.db --cache /tmp/kg-cache --seed-benchmark 50000 --no-ui
./build/kin-galeri --db /tmp/kg-bench.db --cache /tmp/kg-cache --bench-scroll 12
```

Smoke test tanpa display (butuh `--no-ui`, kalau tidak aplikasi memang tetap berjalan sebagai GUI):

```bash
QT_QPA_PLATFORM=offscreen ./build/kin-galeri --db /tmp/kg-bench.db --cache /tmp/kg-cache --no-ui
```

Index folder nyata secara headless:

```bash
./build/kin-galeri --db ~/.local/share/kin-galeri/library.db --index ~/Pictures --no-ui
```

### CLI

```
Usage: Kin Galeri [options]

Options:
  -h, --help                 Show this help.
  -v, --version              Show the version.
      --db <file>            Override the library database file.
      --cache <dir>          Override the cache directory.
      --reset-db             Delete the library database first.
  -i, --index <folder>       Index a folder (repeatable).
      --seed-benchmark <N>   Insert N synthetic rows for the scroll benchmark.
      --thumbnail-size <px>  Thumbnail edge length (default 256).
      --thumb-memory-cache <MB>  Decoded thumbnail budget in MB (default: adaptive).
                                 A value >= 0 pins the budget; adaptive sizes it from the
                                 visible cell count.
      --limit <N>            Surface only the newest N items (0 = all).
      --perf-overlay         Show the frame-time overlay on start.
      --bench-scroll <s>     Automated scroll benchmark for <s> seconds, then exit.
      --no-ui                Headless mode.
      --rebuild-search-index Rebuild the full-text search index and exit.
      --software-render      Force the Qt Quick software renderer.
      --no-home-scan         Do not seed a fresh library with the home folder.
```

### Pengaturan

Halaman **Settings** (ikon **⚙** di kanan header, atau entri **Pengaturan** di sidebar) memakai
rel kategori di kiri dan kartu bertajuk di kanan. Semua preferensi disimpan lewat `QSettings` dan
berlaku langsung tanpa restart:

- **General** — halaman awal (All photos / Albums / lanjutkan terakhir), konfirmasi sebelum
  memindahkan ke recycle bin, overlay frame-time saat mulai, dan **Developer mode** (menampilkan
  alat benchmark, tombol overlay, serta detail penyimpanan di bilah status).
- **Interface** — tema (System / Light / Dark), warna aksen, bahasa (System / English / Bahasa
  Indonesia), ikon sistem dari tema desktop (dengan fallback glif teks), tampil/sembunyikan sidebar + lebarnya, ukuran thumbnail dan jarak antar tile, header
  tanggal, animasi transisi, sorotan hover.
- **Library** — kelola folder pustaka (tambah / hapus / **Rescan now**), pantau perubahan otomatis,
  pindai ulang saat mulai, ikuti symlink, sertakan folder tersembunyi, lewati folder sampah (daftar
  bawaan `node_modules`/cache/toolchain/`cmake-build-*` dll. bisa dilihat & diedit lewat **Customize…**),
  serta daftar **folder yang diabaikan** (path, beserta isinya) dan **pola nama** (glob, mis.
  `Screenshots`, `backup*`) yang dilewati saat pemindaian.
- **Thumbnails** — budget cache memori (adaptif atau 32–2048 MB) dan info/aksi cache disk.
- **Playback** — putar otomatis, ulang, mulai tanpa suara, volume default, backend.
- **Storage** — info cache/database, bersihkan cache, tambah folder.
- **About** — versi dan backend video.

Preferensi disimpan di `~/.config/kin/Kin Galeri.conf`.
Panduan lengkap: [`docs/USER-GUIDE.md`](docs/USER-GUIDE.md).

### Pemaketan

```bash
packaging/appimage/build.sh                 # AppImage (unduh linuxdeploy otomatis)
packaging/deb/build-deb.sh                  # .deb via CPack (butuh dpkg-deb)
flatpak-builder --user --install --force-clean build-flatpak \
    packaging/flatpak/io.github.mrrizkin.KinGaleri.yml
```

`cmake --install` memasang binary, `.desktop`, dan ikon secara normal; pembundelan Qt diserahkan ke
alat packaging. Lihat [`docs/M4-REPORT.md`](docs/M4-REPORT.md) §7.


Jalankan `./build/kin-galeri --help` untuk teks otoritatif — blok di atas adalah salinannya.

Variabel environment:

| Variabel | Efek |
|---|---|
| `KG_DB_FILE` | Override path database |
| `KG_CACHE_DIR` | Override direktori cache |
| `KG_NO_HOME_SCAN=1` | Jangan jadikan folder home sebagai root default |
| `KG_DEBUG=1` | Cetak nilai CLI hasil parsing |

### Lokasi data (default)

| Isi | Lokasi |
|---|---|
| Config | `~/.config/kin-galeri/` |
| Database | `~/.local/share/kin-galeri/library.db` |
| Cache thumbnail | `~/.cache/kin-galeri/thumbs/` |

Database sengaja **dipisah** dari folder media, supaya library bisa dipindah tanpa merusak indeks.

### Keyboard

| Tombol | Aksi |
|---|---|
| `Ctrl+F` | Fokus ke kolom pencarian |
| `Ctrl+R` | Muat ulang grid |
| `Ctrl+Z` | Undo aksi terakhir (rename / move / recycle bin) |
| `F12` | Toggle overlay performa |
| `Esc` | Tutup viewer → tutup panel detail → lepas fokus search |
| Klik sel | Buka viewer layar penuh |
| Double-click sel | Toggle favorit |
| Klik kanan sel | Menu konteks (favorit / detail / Select / Add to album / Rename / Move / recycle bin / copy path / show in file manager) |
| Tombol **Select** di header | Mode seleksi: klik = pilih, Shift+klik = rentang, klik kanan = menu konteks |
| Tombol **Date ▾** di header | Filter rentang tanggal (Today / Last 7 days / Last 30 days / This year / Any time) |
| `←` `→` (di viewer, gambar) | Item sebelumnya / berikutnya |
| `←` `→` (di viewer, video) | Seek ±5 detik |
| `Space` (di viewer) | Pause/play video, atau reset zoom pada gambar |
| `M` (di viewer, video) | Toggle mute |
| `F` (di viewer) | Fit ke layar |

## Catatan teknis

**Format thumbnail diprobe saat runtime.** Qt tidak menjamin plugin WebP/AVIF tersedia — di banyak distro
keduanya tidak ada. `ThumbnailService::pickWriteFormat()` memilih yang pertama tersedia dari
`webp, avif, jpg, png` dan ekstensi file di disk mengikuti hasil probe, jadi cache tetap konsisten
antar mesin. Format aktif ditampilkan di status bar.

> **WebP butuh paket `qt6-imageformats`.** Probe di atas hanya soal format *tulis* cache; untuk bisa
> *membaca* `.webp` (thumbnail & viewer) Qt6 harus punya plugin `libqwebp.so` yang disediakan
> `qt6-imageformats`. Tanpa paket itu, `QImageReader::supportedImageFormats()` tidak memuat `webp`
> dan file WebP tampil sebagai placeholder — walaupun tetap terindeks. Cek cepat:
> `QT_PLUGIN_PATH=/usr/lib/qt6/plugins` lalu cari `webp` di daftar format yang didukung.

> **Tapi thumbnail video TIDAK lewat jalur ini.** Kalau ada plugin webp, video poster frame tetap
> ditulis sebagai JPEG. Dua jalur terpisah: tanpa FFmpeg, thumbnail video = none.

**Revision thumbnail bersifat monotonik dan wajib begitu.** Revision tertanam di URL gambar
(`image://thumbs/<id>?v=<n>`), dan `QQuickPixmapCache` Qt di-key berdasarkan URL serta **mengingat
placeholder selamanya**. Kalau revision pernah mundur — misalnya di-reset ke 0 saat `reload()` —
URL lama yang sudah di-cache placeholder-nya tidak akan pernah diminta ulang, dan sel itu macet di
placeholder permanen. Karena itu `MediaGridModel::m_thumbnailRevisions` sengaja hidup lebih lama
dari `reload()`. **Jangan pernah memundurkan revision.**

**Poster frame video diekstrak dengan FFmpeg, bukan lewat render API mpv.** Alasannya: ekstraksi frame
harus juga berjalan saat indexing `--no-ui` dan di dalam thread pool thumbnail, di mana **tidak ada
konteks OpenGL**. `VideoFrameExtractor` membaca display matrix container untuk rotasi, jadi video
portrait ponsel tidak menghasilkan thumbnail miring. Frame diambil pada ~10 % durasi (dibatasi 1–10 s),
bukan pada 0 ms, karena frame pertama sering hitam atau fade-in.

**Waktu capture diambil dari EXIF lebih dulu**, baru jatuh ke nama file lalu `mtime`. Kolom
`taken_at_src` mencatat asal nilainya (`exif` / `filename` / `mtime`), jadi baris lama bisa di-reindex
setelah perbaikan parser tanpa kehilangan informasi tentang seberapa dapat dipercaya tanggalnya.

**EXIF dibaca tanpa Exiv2.** PRD awalnya menyebut Exiv2, tapi itu berarti satu dependensi C++ eksternal
untuk setiap pengguna yang membangun sendiri. `src/core/metadata/ExifReader` adalah parser TIFF/EXIF
read-only tanpa dependensi yang mencakup seluruh tag P0, dan sudah diverifikasi **tanpa satu pun
selisih** terhadap PIL 12.3.0 pada library nyata. Batasannya: hanya container JPEG dan TIFF, read-only,
tanpa maker-note/XMP. Kalau nanti butuh menulis EXIF atau format RAW, Exiv2 perlu ditambahkan kembali.

**Viewer memakai `image://full/<base64url(path)>?s=<edge>`, bukan `file://` langsung.** Dua alasan:
`QImageReader::setAutoTransform(true)` menerapkan orientasi EXIF termasuk varian mirrored (2/4/5/7)
sehingga viewer tidak pernah bisa berbeda dari grid, dan `setScaledSize()` membatasi decode sehingga
foto 50 MP tidak mengalokasikan ratusan MB. Path di-encode base64url untuk menghindari tabrakan sintaks URL.

**Recycle bin adalah soft delete murni.** `deleted_at` di-set di database; **tidak ada file di disk yang
pernah disentuh**. "Empty recycle bin" menghapus baris DB, bukan file.

**File yang tidak bisa dibuat thumbnail-nya tidak akan dicoba berulang kali.** Video, HEIC/DNG tanpa plugin,
dan file korup menghasilkan `QImage` null. `ThumbnailService` mencatatnya di kumpulan `m_failed` sehingga
permintaan berikutnya langsung dilayani placeholder tanpa menjadwalkan ulang pekerjaan yang pasti gagal.
Jumlahnya tampil di status bar sebagai `failed N`. Tombol **Clear thumbnail cache** juga mengosongkan
catatan ini, jadi kalau Anda baru memasang plugin format gambar, klik itu untuk mencoba lagi.

> Sebelumnya `thumbnailReady()` dikirim bahkan saat generasi gagal. Karena sinyal itu menaikkan `revision`
> dan mengubah URL gambar, Qt terus meminta ulang sel tersebut → jadwalkan generasi → gagal → minta ulang,
> **loop tak terbatas**. Itulah yang membuat thumbnail video berkedip. Detail lengkap dan verifikasinya ada
> di [`docs/M1-REPORT.md`](docs/M1-REPORT.md) §7.1.

**Pencarian memakai FTS5, dan index-nya dijaga oleh trigger SQLite.** Bukan oleh kode aplikasi —
sehingga tidak ada jalur kode mana pun (indexer, recycle bin, empty trash) yang bisa lupa memperbaruinya.
Satu detail yang mudah salah: tokenizer FTS5 memperlakukan tanda baca sebagai pemisah, jadi
`IMG_20250808.jpg` di-index sebagai `img` + `20250808` + `jpg`. Input pengguna **harus dipecah dengan
cara yang sama — tanda baca diganti spasi, bukan dihapus**. Menghapusnya akan menyatukan `IMG_20250808`
menjadi satu term yang tidak ada di index, dan pencarian akan **diam-diam mengembalikan nol hasil**.

**Album asal disimpan sebagai kolom `source_album`, bukan dihitung saat muat.** Ini yang membuat filter
album bisa memakai index dan menghilangkan jalur O(n) di jalur muat. Konsekuensi yang berguna: kalau
aturan deteksi album diubah, cukup naikkan `kSourceAlbumRulesVersion` di `LibraryMaintenance` dan
seluruh library dihitung ulang otomatis tanpa reindex manual.

**Model grid dua tingkat.** Qt `GridView` tidak mendukung `ListView.section`, dan model datar tidak bisa
menghasilkan pemisah hari. Jadi model luar adalah daftar *baris*, dan tiap baris adalah model bersarang —
sehingga satu thumbnail yang siap hanya meng-invalidate satu sel, bukan satu baris penuh.

## Performa terukur

Terverifikasi pada X11, AMD Radeon renoir, OpenGL 4.6, Mesa 26.2.4, vsync 16,67 ms.

### Scroll & startup (50.000 baris sintetis, M1)

| Metrik | Hasil | Target PRD |
|---|---|---|
| Cold start → frame pertama | 708–761 ms | < 1,5 s ✅ |
| Scroll 50k (sticky header + scrubber aktif) | **60,00 fps, 0/600 dropped**, worst 16,9–17,0 ms | 60 fps ✅ |
| Ukuran DB | 463 B/item (334 B sebelum FTS) | < 2 KB ✅ |
| Memory idle 100k | **tidak bisa < 400 MB** — baseline Qt saja 214 MB; pada 50k RSS sudah 482 MB | ⚠️ target direvisi, lihat PRD §4.1.1 |

### Throughput indexing (5.000 file foto nyata, M1)

| Skenario | Throughput |
|---|---|
| Index pertama (DB kosong) | **1.355 file/s** |
| Rescan inkremental (semua tidak berubah) | **1.356 file/s** |

Target PRD ≥ 150 file/s → **terlampaui ~9×**. Untuk 50.000 foto: **≈ 37 detik**, versus batas exit
criterion 10 menit (~15× margin).

> Angka ini mengisolasi biaya **CPU + DB** (parse EXIF + upsert SQLite) — pada pengukuran M1 path file
> dibuat hardlink sehingga page cache membuat pembacaan disk gratis setelah beberapa file pertama.
> Pada library nyata dengan 50k file unik, throughput akan dibatasi kecepatan disk dan lebih rendah.
> Yang terbukti: jalur CPU/DB bukan bottleneck.

Angka lengkap, metode pengukuran, dan daftar bug nyata:
[`docs/M1-REPORT.md`](docs/M1-REPORT.md) · [`docs/M0-REPORT.md`](docs/M0-REPORT.md).

## Struktur proyek

```
src/
  main.cpp            CLI, environment, bootstrap engine
  app/                Paths, CliOptions, AppController (facade QML `app`), PerfMonitor
  core/
    db/               Database (koneksi per-thread), Migrations (user_version)
    metadata/         ExifReader (parser EXIF/TIFF tanpa dependensi)
    model/            MediaItem, SourceAlbum
    grouping/         DateGrouper
    thumbs/           ThumbnailService (LRU memori + disk + generator)
    indexer/          FolderIndexer
    benchmark/        BenchmarkSeeder
  models/             MediaGridModel (model dua tingkat baris → item)
  ui/
    quick/            ThumbnailProvider (image://thumbs/<id>), FullImageProvider (image://full/…),
                      ThemeIconProvider (image://themeicon/<name>)
    qml/              Main, Theme, AppIcon, Sidebar, MediaGrid, MediaRow, MediaCell,
                      TimelineHeader, PerfOverlay, ViewerOverlay, AlbumsView,
                      AlbumCard, SettingsView
```

## Lisensi

Kin Galeri adalah **perangkat lunak bebas** dan dirilis di bawah
**[GNU General Public License v3.0 atau lebih baru (GPL-3.0-or-later)](LICENSE)**.

```
Copyright (C) 2026 Kin Galeri contributors (mrrizkin)

This program is free software: you can redistribute it and/or modify it under
 the terms of the GNU General Public License as published by the Free Software
 Foundation, either version 3 of the License, or (at your option) any later
 version.
```

Ini adalah program bebas: Anda boleh memakai, mempelajari, mengubah, dan
mendistribusikannya. Konsekuensinya, **turunan/redistribusi juga harus tetap
open source di bawah GPL** — kamu tidak boleh mengambil kode ini lalu menutup
sumbernya.

Lisensi ini dipilih karena aplikasi ini menautkan **Qt 6**, **libmpv/mpv**, dan
**FFmpeg**, yang kompatibel penuh dengan GPL-3.0-or-later. Teks lisensi lengkap
ada di file [`LICENSE`](LICENSE).
