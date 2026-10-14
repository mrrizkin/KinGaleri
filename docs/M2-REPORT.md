# Kin Galeri — Laporan M2 (Organize & Filter)

**Status: SELESAI kecuali butir yang dicatat di §8.** Yang sudah dikerjakan: **video poster frame
(FFmpeg)**, **video playback (libmpv via mpvqt)**, **album manual + seleksi & aksi batch**, **move/
rename/delete + undo**, **filter rentang tanggal**, **inotify watcher + indeks inkremental**, **panel
exif lebih lengkap**, dan **budget memori thumbnail adaptif**. Sisa yang belum: target memori 100k
(§8) dan `tests/`.

Laporan terkait: [`M1-REPORT.md`](M1-REPORT.md) · [`M0-REPORT.md`](M0-REPORT.md) · [`TECH-DEBT-REPORT.md`](TECH-DEBT-REPORT.md)

> Bagian 1–6 di bawah adalah catatan sesi sebelumnya (video). Bagian §9–§10 mencatat pekerjaan
> *Organize & Filter* yang menyelesaikan M2.

---

## 1. Keputusan backend video: **libmpv** (opsi A)

PRD §10.1 menetapkan **Qt Multimedia + FFmpeg** sebagai default dan **libmpv opsional**
(`-DVIDEO_BACKEND=mpv`). Anda memilih **opsi A** — libmpv sebagai backend yang benar-benar dipakai —
dengan alasan yang bagus: jalur mpv sebelumnya **tidak pernah bisa diuji**, dan jalur kode yang tidak
pernah dijalankan sebenarnya bukan fitur, hanya niat.

Hasilnya sekarang **keduanya jalan**:

| `KG_VIDEO_BACKEND` | Efek |
|---|---|
| `auto` (default) | Pakai mpv kalau tersedia, kalau tidak Qt Multimedia, kalau tidak `none` |
| `mpv` | Paksa mpv; kalau mpvqt tidak ada → warning lalu fallback |
| `qtmultimedia` | Pakai Qt Multimedia/FFmpeg |

Qt Multimedia tetap dipertahankan sebagai fallback, jadi keputusan PRD §10.1 tidak dibatalkan —
hanya urutan preferensinya yang berubah.

---

## 2. KOREKSI: libmpv ternyata sudah terpasang sejak awal

**Klaim saya di `docs/M0-REPORT.md` — "MISSING: libmpv" — salah.** Saya mencari paket bernama
`libmpv`, tidak menemukannya, lalu menyimpulkan tidak tersedia.

Kenyataannya di Arch/Artix **tidak ada paket bernama `libmpv`** — libmpv **disediakan oleh paket
`mpv`**, yang sudah terpasang:

```
pacman -Qo /usr/lib/libmpv.so*   → owned by mpv 1:0.41.0-6  [installed]
pkg-config --modversion mpv      → 2.5.0
mpv_create() + link -lmpv        → berjalan, exit 0
```

**Pelajaran yang saya catat:** verifikasi ketersediaan library lewat **header, `pkg-config`, dan
apakah `-l` benar-benar bisa di-link** — bukan lewat nama paket. Nama paket berbeda antar distro
(`libmpv-dev` di Debian, `mpv-libs-devel` di Fedora, `mpv-devel` di openSUSE).

`docs/M0-REPORT.md` sudah dikoreksi dengan blockquote eksplisit, dan **`PRD.md` §12 Lampiran B diubah
dari daftar biasa menjadi tabel status** dengan kolom "Status di mesin dev".

### mpvqt juga tidak seperti yang saya klaim

Saya sempat bilang mpvqt "menyediakan `MpvItem` sehingga video bisa langsung ditanam di QML".
**Itu juga salah.** Isi `mpvqt` hanya:

```
/usr/include/MpvQt/{MpvAbstractItem, MpvController, …}.h
/usr/lib/cmake/MpvQt/{MpvQtConfig, FindLibmpv, …}.cmake
/usr/lib/libMpvQt.so.3
```

**Tidak ada `qmldir`, tidak ada plugin, tidak ada tipe QML terdaftar.** Jadi tetap harus menulis
sendiri `qmlRegisterType` dan pembungkus C++-nya.

Yang **memang** diberikan mpvqt dan itu bagian yang sulit: `MpvAbstractItem` sudah mengimplementasikan
`createRenderer()` (render API mpv di atas `QQuickFramebufferObject`). Itu menyelamatkan pekerjaan
signifikan, dan `QQuickFramebufferObject` **tidak deprecated di Qt 6.12**, jadi pendekatan ini tidak
berada di API yang akan segera hilang.

---

## 3. Video poster frame — `VideoFrameExtractor` (FFmpeg, headless)

`src/core/thumbs/VideoFrameExtractor.{h,cpp}` — dibangun langsung di atas
libavformat/libavcodec/libswscale. **Sengaja BUKAN lewat render API mpv**, karena ekstraksi frame
harus juga jalan saat indexing `--no-ui` dan di dalam thread pool `ThumbnailService`, di mana
**tidak ada konteks OpenGL**. Semua entry point statis dan stateless, jadi aman dari thread mana pun.

```cpp
struct Info { bool valid; int width, height; qint64 durationMs; int rotationDegrees; };
static Info   probe(const QString &path);
static QImage extractFrame(const QString &path, qint64 positionMs, int edge);
static QImage extractPoster(const QString &path, int edge);
static bool   available();
```

Keputusan implementasi yang penting:

- **Semua handle libav dibungkus `struct AvContext`** yang membebaskan `sws → packet → frame → dec → fmt`
  dalam urutan benar dari destruktornya, sehingga setiap early return bebas leak. Handle C libav bukan
  RAII; lupa satu berarti kehilangan satu fd plus ratusan MB per thumbnail gagal.
- **Rotasi dibaca dari `AV_PKT_DATA_DISPLAYMATRIX`** (`av_display_rotation_get` mengembalikan derajat
  **berlawanan arah jarum jam**, jadi dinegasi lalu dinormalkan ke [0,360) dan di-snap ke kelipatan 90°).
  Video ponsel rutin membawa 90/270 di sini; mengabaikannya menghasilkan thumbnail miring walaupun
  decode-nya sempurna.
- **`probe()` menukar width/height untuk 90/270**, jadi yang dilaporkan adalah geometri *tampilan*,
  bukan geometri coded.
- **`extractPoster()` memilih `clamp(duration/10, 1000, 10000)` ms** — frame 0 sering hitam atau fade-in,
  jadi 0 ms adalah pilihan poster yang buruk.
- **Seek**: `av_seek_frame(..., AVSEEK_FLAG_BACKWARD)` + `avcodec_flush_buffers()`. Seek yang **gagal**
  sengaja tidak fatal (jatuh ke decode dari awal).
- **Melewati 2 frame pertama setelah seek** — sering sisa keyframe yang belum utuh.
- **`thread_count = min(2, max(1, av_cpu_count()/2))`** — sengaja rendah, karena ini jalan di pool bersama
  `ThumbnailService` di mana banyak video bisa mengantre; decoder yang over-subscribe akan memblokir
  seluruh pool.
- **Tidak pernah upscale**: `scale = min(1.0, min(edge/srcW, edge/srcH))`.
- Seluruh isi file dibungkus `#ifdef KG_HAS_VIDEO_THUMBS`, dengan stub `#else` yang mengembalikan
  `available()==false` — aplikasi tetap build dan jalan tanpa dev lib FFmpeg.

Perubahan pendukung: `ThumbnailService::SourceEntry` mendapat `bool video`, signature menjadi
`registerSource(mediaId, sourcePath, synthetic, video)`, dan `renderSource()` men-dispatch video ke
`extractPoster()` sebelum menyentuh `QImageReader` (container video bukan gambar — inilah yang dulu
membuat setiap sel video menampilkan placeholder). `AppController::registerThumbnailSources()` mengirim
`entry->item.kind == MediaKind::Video`.

### Bug di kode saya sendiri: rotasi dobel

Run pertama menghasilkan poster **landscape** untuk video **portrait**:

```
probe: valid=1 720x1280 duration=47567ms rotation=90
poster: 256x144     <-- SALAH, seharusnya portrait
```

Penyebabnya: saya menghitung ukuran target swscale dari geometri **tampilan** (sebelum swap untuk
90/270), **lalu juga** menerapkan rotasi pada hasilnya — rotasinya kena dua kali. Perbaikan: scale
hanya terhadap `srcW`/`srcH` **coded**, baru rotasi; rotasi itu sendiri yang mengubah dimensi.

Setelah diperbaiki: `probe 720x1280 rotation=90` → `poster 144x256 PORTRAIT`. Frame 30 detik
diekstrak dan **diperiksa secara visual** — orang dengan sepeda, tegak, portrait benar.

---

## 4. Video playback — `KinMpvItem` + `MpvVideoSurface.qml`

- **`src/ui/quick/KinMpvItem.{h,cpp}`** — subclass `MpvAbstractItem`. Q_PROPERTY: `source`, `position`,
  `duration`, `paused`, `volume`, `muted`, `speed`, `hasVideo`, `loaded`, `errorString`.
  Q_INVOKABLE: `play`, `pause`, `togglePause`, `seek`, `seekRelative`, `stop`.
  - Mengamati `time-pos`, `duration`, `pause`, `volume`, `mute`, `speed`, dan **`width` sebagai
    `MPV_FORMAT_INT64` — `width > 0` adalah sinyal andal bahwa track video benar-benar sedang di-decode**,
    tepat kondisi untuk menampilkan permukaan video.
  - Load pakai `commandAsync({"loadfile", path, "replace"})` supaya ganti klip tidak mengantre.
  - Setter memperbarui state lokal dulu lalu mendorong ke mpv (*optimistic*) supaya UI tidak tertinggal
    satu round-trip di belakang klik.
  - **Semua observasi dan loading di-gate pada `ready()`** — mengamati sebelum itu adalah no-op atau error.
- **`src/ui/qml/MpvVideoSurface.qml`** — komponen mandiri, **sengaja dipisah**: import QML bersifat
  statis, jadi `import KinGaleri.Mpv` di dalam `ViewerOverlay.qml` akan membuat seluruh viewer gagal
  dimuat pada build tanpa mpvqt. Berisi permukaan `MpvItem`, poster selama belum ada frame ter-decode,
  `BusyIndicator`, notice "Audio only", label error, dan transport bar (play/pause, posisi `0:06`,
  slider seek, durasi, mute, slider volume, kecepatan 0.5/1/1.5/2).
  - **Tidak mendaftarkan Shortcut apa pun dengan sengaja** — ViewerOverlay sudah memiliki Left/Right/Space/F,
    dan dua Shortcut enabled dengan sequence sama membuat Qt melaporkan aktivasi *ambiguous* dan
    **tidak menjalankan keduanya** (kelas bug yang sama dengan Escape sebelumnya).
  - Slider seek memakai `Binding on value { when: !pressed; value: mpv.position }` — penugasan `value:`
    biasa akan berkelahi dengan `onMoved` dan menghasilkan binding loop.
- **`src/main.cpp`** — `qmlRegisterType<KinMpvItem>("KinGaleri.Mpv", 1, 0, "MpvItem")` (URI berbeda,
  karena `"KinGaleri"` sudah punya qmldir hasil generate dan registrasi runtime ke sana menimbulkan
  definisi yang bentrok).
- **`AppController::videoBackend`** — properti CONSTANT berisi nilai `KG_VIDEO_BACKEND`, supaya QML punya
  cara runtime untuk memutuskan boleh tidaknya memuat komponen mpv.
- **`ViewerOverlay.qml`** — notice lama "Video playback arrives in M2" **diganti** `Loader`
  (`active: … && app.videoBackend === "mpv"`). Left/Right jadi seek ±5 s saat video aktif, Space jadi
  toggle pause, dan `M` baru untuk mute.

**Bug API mpvqt yang ketemu:** `MpvAbstractItem::commandAsync` menerima **`const QStringList&`**, bukan
`QVariant` — hanya `MpvController::commandAsync` yang menerima QVariant. Mengirim QVariant adalah error
compile; saya sempat mengalaminya.

### Detail CMake

- `KG_VIDEO_BACKEND` = `auto|mpv|qtmultimedia`; `KG_ENABLE_MPV` (default ON) + `find_package(MpvQt QUIET)`.
- **File QML dipindah ke variabel `KG_QML_FILES`**, dan `MpvVideoSurface.qml` hanya ditambahkan bila
  `KG_ENABLE_MPV`. Ini wajib: percobaan pertama saya memakai `target_sources(... QT_RESOURCE_ALIAS)`
  yang **diam-diam tidak memasukkan file ke qmldir hasil generate**.

---

## 5. Bug kedua: thumbnail video macet permanen di placeholder

Anda melaporkan sel video menampilkan placeholder gradien permanen, padahal poster `144x256` yang
sempurna **sudah ada di disk**. Saya sempat salah mendiagnosis ini sebagai masalah generasi thumbnail.

Diagnostik sementara (`KG_DIAG` + `console.log`, sudah dihapus) membuktikan urutan sebenarnya:

```
fetch id=1 edge=193 exists=0 failed=0 awaiting=0 video=-1   <- disk miss, placeholder disajikan
ready id=1 found=1                                         <- generasi BERHASIL, revision naik
thumbUrlChanged id=1 url=image://thumbs/1?v=1              <- QML MEMANG menerima URL baru
thumbUrlChanged id=0 url=image://thumbs/1?v=0              <- tapi delegate BARU muncul di v=0
cell completed  id=1 url=image://thumbs/1?v=0
fetch calls for id=1 in the entire session: 1
```

**Akar masalahnya:** generasi thumbnail, sinyal `thumbnailReady`, koneksi, kenaikan revision, dan
pembaruan QML **semuanya bekerja benar** — `?v=1` benar-benar sampai ke QML. Masalahnya adalah
`reload()` berikutnya (sidebar mengubah kind/album/favorites, masing-masing memicu reload)
**membangun ulang `m_entries` dari nol dengan `revision = 0`**.

URL kembali ke `image://thumbs/1?v=0`, dan **`QQuickPixmapCache` Qt di-key berdasarkan URL dan sudah
memegang placeholder untuk URL persis itu** (karena `Image` di `MediaCell` memakai `cache: true`).
Provider tidak pernah ditanya lagi, dan placeholder menjadi permanen.

**Generalisasinya:** *revision adalah state per-load, tapi image cache bersifat global dan di-key
berdasarkan URL. URL apa pun yang pernah mengembalikan placeholder tidak akan pernah bisa diminta lagi.*

**Perbaikan** di `MediaGridModel`: peta monotonik yang bertahan melewati `reload()`:

```cpp
/// Monotonic thumbnail revision per media id. Outlives reload() on purpose: the
/// revision is embedded in the image URL, and Qt's pixmap cache remembers a
/// placeholder forever if the same URL is requested twice. See reload().
QHash<qint64, int> m_thumbnailRevisions;
```

`reload()` menyemai entry dengan `m_thumbnailRevisions.value(item.id, 0)` (bukan literal `0`), dan
`onThumbnailReady()` memakai `entry.revision = ++m_thumbnailRevisions[mediaId]`. Revision hanya bergerak
maju, jadi URL yang pernah mengembalikan placeholder tidak pernah diminta lagi, dan setelah generasi
pertama URL-nya stabil lintas reload.

> **Catatan untuk M2 lanjutan:** akar masalah strukturalnya adalah revision disimpan sebagai state model.
> Kalau nanti ada sumber invalidasi lain (misalnya file diganti di disk), pola yang sama harus dipakai —
> **jangan pernah memundurkan revision**.

---

## 6. Bug ketiga: video selesai → spinner abadi, poster lama, durasi 0:00

**Gejala yang dilaporkan:** *"ketika video sudah selesai dan tidak ada video lagi... playback jadi
0:00 - 0:00 dengan gambar video sebelumnya dan icon loading terus terusan"*.

**Akar masalah:** tiga state berbeda dicampur menjadi satu — *sedang memuat*, *dipause*, dan
*sudah selesai*. `endFile` pada dasarnya berarti "tidak ada file", sehingga handler lama men-set
`loaded=false`, `hasVideo=false`, `position=0`. Akibatnya:

- `MpvVideoSurface.qml` menampilkan `BusyIndicator` selama `!loaded` → spinner berputar selamanya;
- `Image` poster muncul kembali karena `!hasVideo` → "gambar video sebelumnya";
- durasi jatuh ke `0:00` karena `onPropertyChanged` tidak menjaga nilai `QVariant` yang *invalid*.

**Temuan probe libmpv** (`/tmp/eof2`, observasi `eof-reached`/`duration`/`time-pos`/`width`):

| Konfigurasi | Perilaku saat akhir file |
|---|---|
| `keep-open=no` | `END_FILE reason=EOF`, lalu `duration`/`time-pos`/`width` menjadi **UNAVAILABLE** (event `MPV_FORMAT_NONE`) → `toDouble()` = `0.0` |
| `keep-open=yes` | **tidak ada `END_FILE`**; `eof-reached=1`, `pause=1`, dan `duration=47.567` / `time-pos=47.567` / `width=1280` **tetap valid** |

Jadi dengan `keep-open=yes`, sinyal "selesai" yang benar adalah **`eof-reached`**, bukan `endFile`.

**Perbaikan** (`KinMpvItem`):

1. `keep-open=yes` diset saat `ready()` sehingga frame terakhir dan durasi dipertahankan di akhir file.
2. Properti baru `ended`; `observeProperty("eof-reached", MPV_FORMAT_FLAG)` menjadi pemicunya.
   Saat `eof-reached` menjadi true: `ended=true`, `position=duration`, `paused=true` — tanpa meng-unload apa pun.
3. `endFile` hanya menandai *unload* untuk alasan non-EOF (`stop`/`quit`/`error`/`redirect`); cabang `eof`
   tetap ada sebagai fallback.
4. `onPropertyChanged` mengabaikan `QVariant` yang invalid, dan menolak nilai `duration <= 0` menimpa
   durasi yang sudah diketahui. Setelah `ended`, update `time-pos` diabaikan agar timeline tetap di ujung.
5. `play()`/`togglePause()` saat `ended` memanggil `replay()`: seek ke 0, bersihkan `ended` (yang juga
   membersihkan `eof-reached` milik mpv), lalu unpause.
6. QML: spinner `running: !mpv.loaded && !mpv.ended && …`, poster `visible: !mpv.hasVideo && !mpv.ended`,
   dan tombol **replay (↻)** muncul saat `mpv.ended`.

**Verifikasi GUI** (X11, klip `VID_20250727_133259.mp4`, durasi 0:47):

| Pemeriksaan | Hasil |
|---|---|
| Seek sampai akhir (10× tombol →) | timeline berhenti di **0:47 / 0:47**, tombol pause berubah menjadi play (paused) |
| Spinner | **tidak muncul** |
| Poster lama | **tidak muncul** — frame terakhir tetap tampil |
| Tombol replay | **muncul** di tengah surface |
| Tekan Space saat `ended` | playback **restart dari 0:01** dan berjalan lagi, tombol replay hilang |

---

## 7. Verifikasi

| Pemeriksaan | Hasil |
|---|---|
| Poster video di grid | **Frame sepeda nyata tampil** (portrait, orientasi benar), bukan placeholder |
| Bertahan melewati reload yang dulu merusaknya | Klik **All photos** lalu **Videos** — thumbnail tetap benar |
| Bukti disk | `144x256 RGB` di `<cache>/thumbs/00/00/1.jpg` |
| **Playback libmpv** | Viewer menampilkan frame ter-decode + transport: `0:06` / `0:47`, tombol pause tampil (artinya sedang playing), `1 of 1` |
| Playback benar-benar berjalan | Screenshot berikutnya menunjukkan **frame yang sama sekali berbeda** dari klip yang sama — bukan still beku |
| Log aplikasi saat playback | tanpa error (hanya catatan benign libswscale `deprecated pixel format`) |
| Headless QML load (build mpv) | exit 0, tanpa error QML |
| **Regresi performa** | `startup: ms=639`, **60.00 fps**, `worst 16.9ms`, **0/600 dropped**, `rss=467MB` (turun dari 491 MB) |
| **Build fallback** `-DKG_VIDEO_BACKEND=qtmultimedia` | terkonfigurasi `qtmultimedia (multimedia=ON, mpv=OFF)`, build sukses, **jumlah `MpvVideoSurface` di qmldir = 0**, headless exit 0 |
| Exit code | `--no-ui` → 0; `--seed-benchmark` → 0 |

---

## 8. Sisa M2

- [x] Album manual + seleksi & aksi batch (§9.1)
- [x] Move/rename/delete + undo (§9.2)
- [x] UI filter (rentang tanggal, tipe, sumber) (§9.3)
- [x] Panel info & EXIF lebih lengkap (§9.5)
- [x] inotify watcher (live update) (§9.4)
- [x] **Budget memory thumbnail adaptif** (PRD §4.1.1 poin 2) (§9.6)
- [x] **Periksa ulang target memory 100k** — diukur ulang pada 100k baris sintetis (build mpv,
      offscreen): startup 1003 ms, `rss=330MB` saat scroll, `rssPeak=351MB`, 1/811 frame drop.
      Ini **memenuhi** target §4.1.1 (< 550 MB), bahkan target asli < 400 MB. Pada 50k: rss=263MB.
      (Angka headless tanpa decode thumbnail nyata; angka 467 MB lama berasal dari sesi video.)
- [x] Trigger FTS untuk `media_tags` saat tagging masuk — migrasi v8 menambah 8 trigger; nama tag
      kini ikut `MATCH` dan diperbarui saat tag dibuat/di-rename/dihapus.
- [ ] `tests/` masih tidak ada → `KG_BUILD_TESTS` **harus tetap OFF**
- [ ] Belum ada yang di-commit (bukan repo git)

---

## 9. Organize & Filter (lanjutan M2)

### 9.1 Album manual + seleksi & aksi batch (F-2.8 / F-3.7 / F-5.2 / F-5.7)

Model (`MediaGridModel`) sekarang punya konsep **album manual** yang terpisah dari `source_album`
(album yang dideteksi dari struktur folder):

- Tabel `albums` + `album_items` dipakai apa adanya; `Q_PROPERTY(QVariantList albums)` memetakan
  `{id, name, count}`.
- Filter album memakai subquery `AND id IN (SELECT media_id FROM album_items WHERE album_id = :albumId)`,
  sehingga tetap bisa digabung dengan pencarian/FTS dan filter lain.
- `createAlbum` / `renameAlbum` / `deleteAlbum` / `addToAlbum` / `removeFromAlbum`, plus varian
  `*Selection*` untuk aksi batch.
- **Seleksi bersifat per-model, bukan per-delegate**: `selectionMode`, `selectionCount`, `isSelected`,
  `toggleSelected`, `selectRange(from,to,additive)`, `selectedIds`. Delegate hanya melaporkan klik;
  source of truth-nya satu, jadi "Select all" dan Shift+klik konsisten.
- `MediaRowModel` mendapat role `selected`; `notifyEntryChanged`/`notifySelectionChanged` mendorong
  perubahan ke delegate tanpa reload seluruh model.

Di QML: `MediaGrid` menyimpan `lastSelectionIndex` supaya **Shift+klik memperluas rentang**. Klik kanan
selalu membuka menu konteks — juga saat mode seleksi, supaya aksi batch tetap dapat dijangkau.

### 9.2 Rename / move / delete + undo (F-5.1)

- `renameMedia(id, newName)` mengubah nama di disk **dan** baris `media` (path/filename/ext/source_album);
  trigger FTS ikut jalan karena kolom `filename`/`source_album` diperbarui.
- `moveMedia(ids, targetDir)` bersifat **all-or-nothing**: kalau satu file gagal dipindah atau sudah ada
  di tujuan, seluruh batch di-rollback supaya tidak ada state setengah jadi.
- **Undo stack** (maks 32 aksi) menyimpan `{tipe, label, ids, oldPaths, newPaths}` untuk Rename / Move /
  Trash / Restore. `undo()` mengembalikan path/`deleted_at` dan memuat ulang. UI: tombol **Undo** di
  header (enabled saat `canUndo`, tooltip berisi label aksi) + `Ctrl+Z`.
- `moveOnDisk` memakai `QFile::rename` lalu **fallback copy+remove** untuk kasus `EXDEV` (pindah
  filesystem), jadi move antar-mount tetap bekerja.

### 9.3 Filter rentang tanggal (F-4.2)

`MediaGridModel` menambah `dateFrom`/`dateTo` (epoch ms, 0 = unbounded) + `dateRangeLabel`, dengan SQL
`AND taken_at >= :dateFrom` / `<= :dateTo`. UI sengaja memakai **preset** (Today / Last 7 days /
Last 30 days / This year / Any time) alih-alih kalender penuh — pertanyaan yang umum adalah "apa yang
baru saya ambil", dan preset menjawabnya satu klik. Chip di header menampilkan rentang aktif dan
tombol ✕ untuk menghapusnya.

### 9.4 inotify watcher + indeks inkremental (F-1.3 / F-1.4)

- `LibraryWatcher` (`QFileSystemWatcher`) mengawasi setiap direktori di bawah root (cap 4096 direktori,
  flag `truncated` kalau lebih), debounce 900 ms, lalu memanggil `AppController::onLibraryChanged()`.
- **Scan inkremental**: file dengan `mtime_ns` + `size` identik dilewati (tidak decode EXIF/header),
  jadi rescan setelah satu file berubah tidak menyentuh seluruh library.
- **Rekonsiliasi hapus**: setelah scan yang tuntas, baris di bawah root yang path-nya tidak terlihat
  di scan dihapus (PRD F-1.3 "hilang"). Kalau scan dibatalkan, rekonsiliasi tidak dijalankan.
- Footer menampilkan `watching N folders`.

**Batasan yang diketahui:** memindahkan file ke folder **di luar** semua root yang diawasi membuat
baris itu dihapus oleh rekonsiliasi (root_id masih menunjuk root lama, jadi path-nya dianggap hilang).
Ini konsisten dengan "file keluar dari library", tetapi bisa mengejutkan; move **di dalam** library
(dan antar subfolder) tetap aman dan bisa di-undo.

### 9.5 Panel detail / EXIF lebih lengkap (F-3.8)

`mediaDetail` menambah `megapixels` ("X.X MP") dan `durationText` (H:MM:SS / M:SS); panel detail
menampilkan baris **Resolution** dan **Duration**.

### 9.6 Budget memori thumbnail adaptif (PRD §4.1.1 poin 2)

- `updateThumbnailBudget(columns, visibleRows)` menghitung `cells = columns * visibleRows * 2.5`, lalu
  `bytes = cells * edge * edge * 4`, di-clamp ke `[32, 256]` MB. Dipanggil dari `MediaGrid` saat jumlah
  kolom/tinggi berubah (`syncThumbBudget`).
- `--thumb-memory-cache` default berubah dari `128` menjadi **adaptive**; nilai non-negatif mengunci
  budget (override), nilai negatif = auto.
- `ThumbnailService::setMemoryCacheLimitMb()` sekarang **langsung mengevict** saat limit diturunkan,
  bukan menunggu insert berikutnya.

### Bug yang ketemu saat verifikasi GUI

1. **`albumId` dideklarasikan READ-only di Q_PROPERTY**, padahal `Sidebar.qml` menugaskannya
   (`model.albumId = …`). QML melempar `TypeError: Cannot assign to read-only property "albumId"` dan
   filter album diam-diam tidak pernah aktif. Perbaikan: tambah `WRITE setAlbumId` (setter-nya sudah ada).
2. **`FolderDialog.selectedFolder` adalah URL (`file://…`)**, sedangkan `moveMedia`/`indexFolder`
   mengharapkan path filesystem → dialog Move gagal dengan "Target folder does not exist". Perbaikan:
   helper `localPath()` di C++ (`AppController.cpp`, `MediaGridModel.cpp`) yang menormalkan URL `file:`
   menjadi path lokal, karena `toLocalFile()` bukan fungsi di QML (nilainya sudah berupa string).
3. **Binding loop `implicitWidth`** pada Dialog "Empty recycle bin?" (varian Fusion) disenyapkan dengan
   memberi `width` eksplisit pada Dialog.

---

## 10. Verifikasi Organize & Filter (GUI X11 + sqlite)

Semua diperiksa pada library uji `/tmp/kgtest` (5 PNG; satu file digeser mtime-nya ke 2020 untuk menguji
filter tanggal), DB `/tmp/kgidx.db`.

| Pemeriksaan | Hasil |
|---|---|
| Mode Select + bar batch | Bar muncul: `0 selected`, Select all / Clear / Add to album… / Favorite / Unfavorite / Move to recycle bin / Done |
| Multi-seleksi | Klik 2 sel → `2 selected`; kotak centang muncul di sel |
| Buat album + tambah item | `Add to album…` → buat **Trip** → SQL: `albums=1`, `album_items=2`; sidebar menampilkan `Trip 2`; seleksi bersih setelahnya |
| Rename album via sidebar | Klik kanan → Rename… → **Renamed2026** tersimpan di DB |
| Delete album via sidebar | Klik kanan → Delete album → `albums=0` |
| Filter tanggal | `This year` → header `4 items · … · 1/1/26 – 10/14/26` (file 2020 hilang); ✕ → kembali `5 items` |
| Rename file | Menu konteks → Rename… → `b.png` → `renamed_b.png` (disk + DB) |
| Undo rename | `Ctrl+Z` → file & DB kembali `b.png` |
| Move (FolderDialog, `file://`) | Pindah ke `/tmp/kgtest/sub` → file di tujuan, DB `path` diperbarui, `root_id` tetap 1 |
| Undo move | `Ctrl+Z` → file & DB kembali ke root |
| Batch recycle bin + undo | 2 item di-trash (`deleted_at` terisi), `Ctrl+Z` mengembalikan keduanya |
| Batch Favorite/Unfavorite | 2 item → `is_favorite=1`, Unfavorite → `0` |
| inotify watcher | `touch` file → baris otomatis di-reindex (taken_at berubah) tanpa restart; file yang dihapus dari disk hilang dari DB |
| QML runtime | Tanpa warning setelah perbaikan `albumId`/`localPath`/binding loop |
| Build | `cmake --build build` bersih |
| Regresi 50k (offscreen) | startup 563 ms, **0/807 frame drop**, 170 fps, rss=263MB |
| Regresi 100k (offscreen) | startup 1003 ms, **1/811 frame drop** (0.12%), 171 fps, rss=330MB (peak 351MB) |
| `--no-ui` | exit 0 |
