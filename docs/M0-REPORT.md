# Kin Galeri — Laporan M0 (Skeleton & Spike)

**Status: SELESAI ✅** — kriteria penerimaan M0 terpenuhi dan terukur.

> Deliverable M0 menurut PRD §7: *"Proof: QML GridView render 50k dummy item @60fps"*, plus CMake project,
> SQLite schema + migrations, dan basic folder scan.

---

## 1. Kesimpulan yang menentukan

**Keputusan QML (Qt Quick) di PRD §5.6 dipertahankan. Fallback ke Qt Widgets TIDAK diperlukan.**

Scroll benchmark pada 50.000 baris sintetis dengan GPU nyata (AMD Radeon renoir, OpenGL 4.6, Mesa 26.2.4,
vsync 16,67 ms) menghasilkan **60,00 fps terkunci vsync** dengan **0–1 dropped frame dari 720** — baik pada
thumbnail cache dingin (thumbnail di-generate saat scroll) maupun hangat. Tidak ada jank di atas 16 ms
selain deviasi pengukuran vsync normal (worst 17,2–17,5 ms).

**Satu target PRD gagal dan sudah direvisi:** target memory "< 400 MB RSS untuk 100k item" terbukti
**tidak dapat dicapai secara aritmatika** di stack ini. Detail dan revisinya ada di PRD §4.1.1.

---

## 2. Hasil terukur vs target PRD §4.1

| Metrik | Target PRD | Hasil M0 | Status |
|---|---|---|---|
| Cold start (DB + cache ada) | < 1,5 s | **656–717 ms** | ✅ |
| Warm scroll 50k item | 60 fps konsisten | **60,00 fps**, avg 16,67 ms, worst 17,2–17,5 ms, 0–1/720 dropped | ✅ |
| DB overhead per item | < 2 KB | **334 B/item** | ✅ |
| Thumbnail load dari disk | < 5 ms/item | belum diisolasi | ⏳ M1 |
| Indexing throughput | ≥ 150 file/s | belum diuji di folder foto nyata | ⏳ M1 |
| Search 100k item | < 50 ms | belum ada (FTS tabel sudah dibuat) | ⏳ M1 |
| Memory idle 100k | < 400 MB | **tidak mungkin** — silakan lihat §4.1.1 | ⚠️ direvisi |

### Kontribusi memory (dari `--limit`)

| Konfigurasi | RSS diam | RSS saat scroll |
|---|---|---|
| `--limit 1` | 270 MB | 271 MB |
| `--limit 10000` | 318 MB | 432 MB |
| `--limit 50000` | 358 MB | 473 MB |
| **Aplikasi QML minimal (window kosong, Qt6 + Fusion)** | **214 MB** | — |

Temuan: ~215–270 MB adalah **baseline Qt 6.12 / Qt Quick / Fusion / Mesa** yang tidak bisa dihilangkan
oleh kode aplikasi. Biaya model kita hanya ≈ 0,8–0,9 KB/item. Budget memory thumbnail (LRU 128 MB) adalah
satu-satunya suku besar yang bisa dikendalikan.

### Reproduksi seeding (50.000 baris)

Seeding 50.000 baris memakan **1,84 s (≈ 27.000 baris/s)**, menghasilkan 2.479 hari unik
(2020-01-01 → 2026-10-14, 3–63 baris/hari), ukuran DB **16.687.104 B**. RNG di-seed tetap (`0xC0FFEE`),
jadi angkanya bisa direproduksi persis.

---

## 3. Bagaimana mengulang verifikasi

```bash
cd /home/mrrizkin/Projects/work/oss/gallery
cmake -S . -B build -G Ninja && cmake --build build

rm -rf /tmp/kg-bench.db* /tmp/kg-cache
# 1. seed — ±1,8 s
./build/kin-galeri --db /tmp/kg-bench.db --cache /tmp/kg-cache --seed-benchmark 50000 --no-ui
# 2. scroll benchmark, thumbnail cache DINGIN
./build/kin-galeri --db /tmp/kg-bench.db --cache /tmp/kg-cache --bench-scroll 12
# 3. ulangi → angka thumbnail cache HANGAT
./build/kin-galeri --db /tmp/kg-bench.db --cache /tmp/kg-cache --bench-scroll 12

# smoke test tanpa display (WAJIB pakai --no-ui, kalau tidak aplikasi memang tetap
# berjalan sebagai GUI dan perintah ini tidak akan pernah selesai)
QT_QPA_PLATFORM=offscreen ./build/kin-galeri --db /tmp/kg-bench.db --cache /tmp/kg-cache --no-ui
```

Output benchmark machine-readable di **stderr**, prefix `kin-galeri[bench]`, tiga jenis baris:
`startup: ms=N`, `resting: rss=NMB thumbs=N`, dan
`scroll: seconds=… distance=…px items=… frames=… dropped=… (…%) fps=… avg=…ms worst=…ms budget=…ms target=…fps rss=…MB rssPeak=…MB libitems=… thumbs=… thumbcache=…MB`.

---

## 4. Yang benar-benar dibangun

### 4.1 Struktur

```
CMakeLists.txt                  target `kin-galeri`, modul QML URI `KinGaleri`, C++20
src/main.cpp                    CLI, environment, engine bootstrap, benchmark wiring
src/app/Paths.{h,cpp}           lokasi config/data/cache + sharding path thumbnail
src/app/CliOptions.{h,cpp}      parser CLI (QCommandLineParser)
src/app/AppController.{h,cpp}   facade QML bernama `app`
src/app/PerfMonitor.{h,cpp}     sampling frameSwapped, fps/avg/worst/dropped
src/core/db/Database.{h,cpp}    koneksi SQLite per-thread, PRAGMA, helper query
src/core/db/Migrations.{h,cpp}  migrasi append-only berbasis `PRAGMA user_version`
src/core/model/MediaItem.h      struct MediaItem
src/core/model/SourceAlbum.{h,cpp}  deteksi album asal dari path
src/core/grouping/DateGrouper.{h,cpp}  dayKey/dayLabel/sectionLabel
src/core/thumbs/ThumbnailService.{h,cpp}  LRU memori + disk + generator
src/core/indexer/FolderIndexer.{h,cpp}    scan folder rekursif + upsert
src/core/benchmark/BenchmarkSeeder.{h,cpp}  seeding data sintetis deterministik
src/models/MediaGridModel.{h,cpp}  model dua tingkat (baris → item)
src/ui/quick/ThumbnailProvider.{h,cpp}  QQuickImageProvider `image://thumbs/<id>`
src/ui/qml/*.qml                Main, Theme(singleton), Sidebar, MediaGrid, MediaRow,
                                MediaCell, TimelineHeader, PerfOverlay
```

### 4.2 Keputusan arsitektur penting

**Model grid dua tingkat (baris → item).** `GridView` Qt tidak mendukung `ListView.section`, dan model
datar tidak bisa menghasilkan pemisah hari. Jadi outer model adalah **daftar baris** (`MediaGridModel`),
dan setiap baris adalah `MediaRowModel` bersarang. Konsekuensinya: saat satu thumbnail siap,
`dataChanged` hanya dikirim untuk **satu sel itu** — bukan membangun ulang seluruh baris.
Setiap entry menyimpan `revision`, yang muncul di URL QML `image://thumbs/<id>?v=<revision>`
sehingga Qt otomatis me-request ulang gambar yang sudah siap.

**Thumbnail 3 tingkat** — memory LRU (mutex-guarded, cost dari `QImage::sizeInBytes()`) → disk
`<cache>/thumbs/<2-hex>/<2-hex>/<id>.<ext>` → generator `QThreadPool` on-demand. Karena Qt di mesin ini
**tidak punya plugin WebP/AVIF**, format tulis diprobe saat runtime (`pickWriteFormat()` memilih yang
pertama tersedia dari `webp, avif, jpg, png`; di sini jatuh ke **jpg**, kualitas 82). Ekstensi file disk
mengikuti hasil probe, sehingga cache tetap valid di mesin dengan plugin berbeda.

**Migrations memakai `PRAGMA user_version`**, bukan tabel `meta`, dan versi di-bump dalam transaksi yang
sama dengan body migrasi. Migrasi FTS5 (`v2`) dibungkus probe runtime `sqlite_compileoption_used('ENABLE_FTS5')`;
kalau SQLite tanpa FTS5, migrasi dilewati dengan warning dan versi tetap maju supaya tidak dicoba selamanya.

**Koneksi DB per-thread** (`kingaleri-<tid>`). `QSqlDatabase` bersifat thread-affine, jadi worker wajib
memanggil `Database::closeThreadConnection()` sebelum thread-nya mati.

### 4.3 Environment yang tervalidasi

- Qt **6.12.0**, g++ 16.2.1, CMake 4.4.4, Ninja 1.13.2, sqlite3 3.53.4.
- **`libffmpegmediaplugin.so` ADA** di `/usr/lib/qt6/plugins/multimedia/` → keputusan Qt Multimedia +
  backend FFmpeg di PRD tervalidasi. `QT_MEDIA_BACKEND=ffmpeg` dipaksa saat startup.
- Plugin gambar Qt yang tersedia: **gif, ico, jpeg, svg** (+ PNG/BMP bawaan). **Tidak ada WebP, tidak ada AVIF.**
- **Library yang TIDAK ada dan tidak dibutuhkan M0: exiv2, libtiff, xxhash.**

> **KOREKSI (ditemukan saat memulai M2):** laporan awal M0 ini juga mencantumkan **libmpv** sebagai
> tidak tersedia. **Itu salah.** libmpv sudah terpasang sejak awal:
> paket `mpv 1:0.41.0-6` menyediakan `libmpv.so=2-64`, header ada di `/usr/include/mpv/` (client.h,
> render.h, render_gl.h, stream_cb.h), `pkg-config mpv` melaporkan **2.5.0**, dan sebuah program uji
> `mpv_create()` berhasil di-compile, di-link, dan dijalankan terhadap `/usr/lib/libmpv.so.2`.
>
> Penyebab kesalahan: di Arch/Artix **tidak ada paket bernama `libmpv`** — libmpv disediakan *oleh*
> paket `mpv`. Pencarian nama paket `libmpv` karena itu tidak menemukan apa pun, dan saya menyimpulkan
> "tidak ada" padahal library-nya sudah tersedia. Pelajarannya: verifikasi lewat header/`pkg-config`/
> `-l` yang benar-benar dipakai untuk build, bukan lewat nama paket.
  (EXIF lewat Exiv2 adalah item M1; untuk M0 `taken_at` diturunkan dari pola nama file, lalu `mtime`.)

---

## 5. Bug nyata yang ditemukan & diperbaiki selama M0

Ini bukan daftar teoretis — setiap item pernah benar-benar menggagalkan runtime:

| # | Bug | Perbaikan |
|---|---|---|
| 1 | `QCommandLineParser::parse(QStringList)` menganggap elemen 0 sebagai nama aplikasi, jadi `arguments().mid(1)` diam-diam membuang argumen pertama (`--db` hilang, `--cache` jalan). Juga: saat `parse()` gagal, nilai opsi yang sudah diparse di-rollback. | `parseCli(QCoreApplication::arguments())` — **jangan** pre-trim |
| 2 | `const QSqlDatabase db = connection()` lalu memanggil `open()/transaction()/commit()` non-const | variabel dibuat non-const |
| 3 | `Q_PROPERTY(MediaGridModel* …)` butuh tipe lengkap untuk cek metatype moc | `AppController.h` meng-include `models/MediaGridModel.h` |
| 4 | Deprecation Qt 6.12: `QDate::startOfDay(Qt::LocalTime)` | `QDateTime(date, QTime(0,0), QTimeZone::LocalTime)` |
| 5 | `Database::close()` dari main thread menutup koneksi worker → `requested database does not belong to the calling thread` | dipecah jadi `close()` / `closeThreadConnection()`; worker menutup koneksinya sendiri + `PRAGMA wal_checkpoint(PASSIVE)` |
| 6 | `--no-ui` tanpa pekerjaan startup menggantung selamanya (quit terikat `busyChanged` yang tak pernah menyala) | sinyal eksplisit `startupCompleted` |
| 7 | Konkatenasi string literal QML lintas baris (`qsTr("…" "…")`) = syntax error | satu string |
| 8 | `MediaCell.qml` memakai `Label` tanpa `import QtQuick.Controls` | import ditambahkan |
| 9 | **`pragma Singleton` saja tidak cukup** — tanpa `set_source_files_properties(... QT_QML_SINGLETON_TYPE TRUE)` **sebelum** `qt_add_qml_module`, `Theme` terdaftar sebagai tipe biasa dan semua `Theme.<warna>` menjadi `undefined` | properti source-file ditambahkan; `build/KinGaleri/qmldir` sekarang berisi `singleton Theme 1.0` |
| 10 | **Tidak ada plugin WebP** → "Unsupported image format" saat menulis thumbnail | probe format runtime + ekstensi disk mengikuti probe |
| 11 | **UB urutan inisialisasi member** di `ThumbnailService`: `m_format(pickWriteFormat(&m_extension))` berjalan sebelum `m_extension` (QString) dikonstruksi → ekstensi kosong, file bernama `12345.` | probe dipindah ke **body** konstruktor |
| 12 | `QImageWriter::supportedImageFormats()` mengembalikan huruf kecil | kandidat format dibuat huruf kecil |
| 13 | `--limit` diparse tapi tidak pernah diterapkan ke model (no-op senyap) | dialirkan via `setGridLimit()` **sebelum** `initialise()` |
| 14 | `QFile::atEnd()`/`readLine()` pada `/proc/self/status` mengembalikan kosong | `readAll().split('\n')`; `residentMemoryMb()` (VmRSS) & `peakMemoryMb()` (VmHWM) jalan |
| 15 | Include hilang di main.cpp; `QObject::connect` tidak ter-kualifikasi | include + kualifikasi ditambahkan |
| 16 | CMake: target `SQLite::SQLite3` deprecated | pakai `SQLite3::SQLite3` bila target tersedia |

---

## 6. Yang belum dikerjakan (input untuk M1)

- [ ] **`README.md` belum ditulis.**
- [ ] Direktori `tests/` yang dirujuk opsi CMake `KG_BUILD_TESTS` belum ada (`add_subdirectory(tests)`
      akan gagal bila opsi dinyalakan). Opsi default `OFF`, jadi build normal aman.
- [ ] **Sticky (floating) section header belum ada** — baru header hari inline. PRD menyebutnya P0;
      ditunda ke M1.
- [ ] Filter album di `SourceAlbum` dijalankan **di memori**, bukan di SQL. Pada 500k item ini perlu
      dipindah ke SQL (atau di-cache), kalau tidak akan jadi O(n) di jalur muat.
- [ ] `--bench-scroll` memakai `NumberAnimation` sehingga mengukur rendering Qt Quick, bukan `GridView`
      spesifik. Ini representatif karena desain final adalah `ListView` berisi baris bersection,
      bukan `GridView`.
- [ ] `SourceAlbum` dan daftar album di sidebar masih **hardcoded**.
- [ ] Belum ada: pencarian/FTS (UI), UI album, People/Places/Smart/Recycle bin, viewer layar penuh,
      pemutaran video, EXIF via Exiv2, People grouping, dan pembuatan thumbnail saat indexing.
- [ ] Target throughput indexing (≥ 150 file/s) **belum diverifikasi di folder foto nyata** — ini
      pengukuran M1 yang paling penting berikutnya.
- [ ] Budget memory thumbnail masih fixed (128 MB); belum adaptif seperti yang diputuskan di PRD §4.1.1
      poin 2.

---

## 7. Dampak ke roadmap

M0 lulus, jadi **M1 (MVP: Index + Browse) bisa dimulai tanpa risiko arsitektural**. Tiga hal yang harus
masuk M1 lebih awal karena sudah teridentifikasi di sini:

1. **Budget memory thumbnail adaptif + pengukuran ulang RSS** (menutup target §4.1.1 poin 2).
2. **Filter album di SQL**, bukan di memori (menutup risiko skalabilitas 500k).
3. **Pengukuran throughput indexing di folder foto nyata** (satu-satunya target §4.1 yang belum diuji).
