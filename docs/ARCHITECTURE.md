# Arsitektur

Kin Galeri adalah satu proses Qt 6: C++20 untuk data dan I/O, QML untuk tampilan.
Tidak ada server, tidak ada akun, tidak ada basis data jaringan — pustaka foto
adalah direktori di disk, dan seluruh keadaan aplikasi ada di SQLite lokal.

## Peta direktori

| Path | Isi |
|---|---|
| `src/main.cpp` | Parsing CLI, `QGuiApplication`, memasang controller ke QML |
| `src/app/` | `AppController` (orkestrasi + properti untuk QML), `AppSettings`, `Paths`, `CliOptions`, `ShortcutRegistry` |
| `src/core/db/` | `Database` (koneksi per-thread), `Migrations`, `LibraryMaintenance` |
| `src/core/indexer/` | `FolderIndexer` (scan + rekonsiliasi), `LibraryWatcher` (inotify) |
| `src/core/metadata/` | `ExifReader` (EXIF + GPS + tanggal pengambilan) |
| `src/core/thumbs/` | `ImageLoader` (dekode + orientasi), `ThumbnailService` (cache LRU) |
| `src/core/grouping/` | `DateGrouper`, `GeoClusterer`, `EventClusterer`, `GroupingService` |
| `src/core/ml/` | `FaceEngine` (ONNX Runtime), `FacePipeline`, `FaceClusterer`, `VectorMath` — opsional |
| `src/models/` | `MediaGridModel` (satu `QAbstractListModel` untuk seluruh tampilan pustaka) |
| `src/ui/qml/` | Modul QML `KinGaleri` |
| `src/ui/quick/` | `KinMpvItem` — surface video mpv |
| `src/ui/` | `ThumbnailProvider` (image provider `image://thumb/…`) |
| `resources/` | `cities.tsv` (indeks kota offline), `.ts` terjemahan |
| `tests/` | 12 target Qt Test |

## Model data

SQLite dengan WAL, satu basis data per pustaka. Migrasi bernomor (`v1`…`v8`) dan
idempoten: `Migrations` menjalankan yang belum tercatat di tabel `meta`, jadi
basis data lama ikut naik sendiri. Migrasi yang perlu, bukan yang mudah: v2/v4/v8
membangun FTS5 (`media_fts`) beserta trigger sinkronisasinya, v3 menambah
`source_album`, v6 indeks recency `idx_media_album_taken`, v7 tabel
`places`/`events`/`faces`/`people`/`face_scans`.

Tabel inti: `media` (satu baris per berkas, termasuk `taken_at`, `taken_at_src`,
`is_favorite`, `deleted_at` untuk Tong sampah, `is_synthetic`), `roots`,
`albums`/`album_items`, `tags`/`media_tags`, `media_fts`, plus tabel pengelompokan.

Koneksi SQLite **dinamai per-thread**, bukan per-berkas: `Database` menyimpan
koneksi di `QThreadStorage`-nya sendiri karena SQLite tidak boleh dipakai
lintas-thread. `~Database()` menutup koneksi thread-nya, yang penting untuk
perpindahan pustaka dan untuk test.

## Threading

Thread UI tidak pernah menyentuh berkas. `FolderIndexer` berjalan di thread
pekerja lewat `QtConcurrent`, melaporkan progres, dan dapat dibatalkan; tulisannya
dibatching dalam transaksi sehingga 100.000 berkas masuk dalam hitungan detik,
bukan menit. `LibraryWatcher` (inotify) memberi tahu perubahan dan menjadwalkan
scan ulang, dengan debounce agar menyimpan banyak berkas sekaligus tidak memicu
puluhan scan. Analisis ML berjalan terpisah dan hanya saat idle secara default
(`smart/idle_only`), supaya tidak berebut CPU dengan gulir galeri.

## Pipeline thumbnail

`ImageLoader` men-dekode gambar, menerapkan orientasi EXIF, dan menyerahkan
pikselnya ke `ThumbnailService` yang menyimpannya ke cache di disk
(`thumbs/`) dengan indeks LRU. Anggaran memori cache menyesuaikan jumlah kolom
yang terlihat dan ukuran thumb, sehingga jendela kecil tidak menahan ratusan MB.
HEIC/HEIF ditangani lewat decoder Qt bila ada; kalau tidak, aplikasi memanggil
`heif-convert` sebagai fallback. Dekode yang gagal menghasilkan placeholder,
bukan crash, dan URL thumbnail memakai parameter revisi supaya gambar yang
di-cache tidak pernah menyegel placeholder selamanya.

## Pengelompokan pintar

`GroupingService` menjalankan tiga pengelompokan yang bisa hidup sendiri:

- `DateGrouper` membuat kunci hari dari tanggal pengambilan (EXIF → pola nama
  berkas → mtime, dengan `taken_at_src` mencatat sumbernya supaya tanggal
  tebakan tidak menyamar sebagai tanggal EXIF).
- `GeoClusterer` mengelompokkan berdasarkan GPS EXIF dan menamai tempat dari
  `resources/cities.tsv` — indeks kota yang dibundel, jadi tidak perlu jaringan.
- `EventClusterer` menggabungkan kedekatan waktu dan lokasi menjadi event.

Wajah bersifat opt-in: `FaceEngine` memakai YuNet (deteksi) dan SFace (embedding)
lewat ONNX Runtime, `FacePipeline` menyimpannya ke tabel `faces`, dan
`FaceClusterer` mengelompokkannya dengan cosine similarity. Ambangnya **0,50**,
lebih ketat daripada yang dipakai verifikasi pasangan, karena klasterisasi
majemuk kesalahannya: satu wajah yang salah masuk akan menggeser centroid dan
menarik wajah salah berikutnya. (Pada LFW, 0,40 memberi purity 76% dan 0,50
memberi 98% — lihat `PERFORMANCE.md`.)

## Lapisan QML

QML dibangun sebagai modul `KinGaleri` dengan `qt_add_qml_module`, jadi tipe
dikenal compiler dan bukan sekadar dimuat saat runtime. Dua hal yang mudah
terjebak:

- Singleton **harus** ditandai sebelum masuk ke modul:
  `set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)`.
  Tanpa itu `qmldir` mendaftarkannya sebagai tipe biasa dan setiap `Theme.<warna>`
  menjadi undefined.
- Impor QML bersifat statis. Satu berkas yang mengimpor modul yang tidak ada akan
  gagal dimuat **seluruhnya**, bukan hanya barisnya. Karena itu video tidak
  diimpor di `ViewerOverlay.qml` (yang selalu ada), melainkan dipilih lewat
  `Loader` dengan **nama berkas**:

| Berkas | Backend | Dibangun bila |
|---|---|---|
| `MpvVideoSurface.qml` | libmpv lewat mpvqt | `KG_ENABLE_MPV=ON` |
| `QtVideoSurface.qml` | Qt Multimedia (default PRD) | `KG_ENABLE_MULTIMEDIA=ON` |

Keduanya mengekspos nama properti dan fungsi yang identik, karena
`ViewerOverlay.qml` menggerakkan keduanya dengan kode yang sama. Opsi CMake yang
diminta secara eksplisit dan tidak terpenuhi **menggagalkan configure**, bukan
diam-diam jatuh ke default — kelas bug ini pernah lolos ke `.deb` yang tanpa
pemutaran video sama sekali.

`KinGaleri.Mpv` didaftarkan saat runtime (`qmlRegisterType`), karena mpvqt tidak
mengirim `qmldir`; itu sebabnya `MpvVideoSurface.qml` mematikan peringatan
`import`/`unresolved-*` qmllint untuk dirinya sendiri, dan sebabnya `MpvItem`
tidak bisa divalidasi lint sekeras berkas lain.

## Pengaturan dan lokasi data

`AppSettings` membungkus `QSettings` (format INI) dan mengekspos semuanya sebagai
`Q_PROPERTY`, jadi QML mengikat langsung dan tidak ada lapisan sinkronisasi.
Nilai yang dinormalkan (mis. batas grid dibulatkan ke ribuan) ditulis lewat
setter yang sama, sehingga berkas pengaturan tidak pernah berisi nilai mustahil.
Lokasi mengikuti `XDG_CONFIG_HOME`/`XDG_DATA_HOME`; `--db`/`--cache` menimpanya,
dan itulah cara test mengisolasi diri.

## Build

CMake ≥ 3.21, C++20, Qt ≥ 6.5 (batas bawah itu nyata: `QTP0004` baru ada di 6.8
dan dipasang berguard, sementara Ubuntu 24.04 hanya punya 6.4 yang terlalu tua
untuk proyek ini). Opsi: `KG_ENABLE_MPV` (default ON), `KG_ENABLE_MULTIMEDIA`
(default ON), `KG_ENABLE_ML` (default OFF, butuh ONNX Runtime), `KG_BUILD_TESTS`
(default OFF). Peringatan compiler `-Wall -Wextra -Wpedantic` dinyalakan dan
diperlakukan serius.

## Lingkungan uji

`qml-lint.sh` memakai qmllint **Qt 6** secara eksplisit (di banyak distro,
`/usr/bin/qmllint` adalah milik Qt 5 dan membayangi yang benar),
`i18n-audit.py` menolak `qsTr()` yang tidak punya entri katalog, dan `ctest`
menjalankan 12 target di atas platform `offscreen` sehingga tidak butuh layar.
