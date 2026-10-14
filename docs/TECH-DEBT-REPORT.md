# Kin Galeri — Laporan Pelunasan Utang Teknis (FTS5 + filter album di SQL)

**Status: SELESAI ✅** — kedua utang teknis yang dibawa dari M1 sudah ditutup, terverifikasi dengan
pengukuran dan GUI nyata, tanpa regresi performa.

Konteks: `docs/M1-REPORT.md` §9 menandai dua utang ini sebagai risiko yang "harus ditutup lebih awal".
Keduanya ditutup **sebelum** M2 dimulai, sesuai keputusan pengguna.

---

## 1. Apa yang diperbaiki

**Utang 1 — FTS5 tidak diisi.** Tabel `media_fts` (`filename, camera_model, album, tags`) sudah dibuat
sejak migrasi v2 tapi **kosong**. Pencarian memakai `filename LIKE '%x%'` yang **tidak bisa memakai
index** — di 100k item ini 59 ms dan skalanya buruk. Target PRD "< 50 ms di 100k" belum terbukti.

**Utang 2 — filter `SourceAlbum` di memori.** `MediaGridModel::reload()` menghitung ulang
`SourceAlbum::detect()` untuk setiap baris dan menyaring di `applyFiltersToLoadedRows()` — jalur O(n).
Dua bug laten ikut ketemu: **`LIMIT` diterapkan *sebelum* filter** (artinya filter album bisa
menghasilkan lebih sedikit item dari yang seharusnya, karena baris di luar N terbaru tidak pernah
dipertimbangkan), dan **`m_totalBytes` menjumlahkan baris yang seharusnya tersaring**.

---

## 2. Skema: sekarang di v8

`struct Migration` mendapat field baru **`bool requiresFts5 = false`**, menggantikan pengecekan string
`migration.name == "full_text_search"` yang di-hardcode di `Database::migrate()`. Field ini di-set pada
v2, v4, v5.

| Ver | Nama | Isi | requiresFts5 |
|---|---|---|---|
| v1 | initial_schema | (tidak berubah) | tidak |
| v2 | full_text_search | `CREATE VIRTUAL TABLE media_fts USING fts5(...)` | **ya** |
| v3 | media_source_album | `ALTER TABLE media ADD COLUMN source_album TEXT` + index | tidak |
| v4 | fts_sync_triggers | 3 trigger sinkronisasi FTS | **ya** |
| v5 | fts_camera_identity | recreate trigger, index `camera_make || ' ' || camera_model` | **ya** |
| v6 | media_album_recency_index | `CREATE INDEX idx_media_album_taken ON media(source_album COLLATE NOCASE, taken_at DESC, id DESC) WHERE deleted_at IS NULL` | tidak |
| v7 | smart_grouping | kolom `places.country/media_count/cover_media_id`, tabel `events` + `media_events`, kolom `faces.model_marker`, tabel `face_scans`, index `idx_media_gps` | tidak |
| v8 | fts_tags | index `idx_media_tags_tag` + 8 trigger (media, media_tags, tags) agar kolom `tags` di FTS mengikuti `media_tags` | **ya** |

Tiga keputusan di sini yang layak dicatat:

**v4 adalah alasan `source_album` harus menjadi kolom.** Trigger hanya bisa mereferensikan kolom pada
`media`, jadi album harus disimpan sebelum FTS bisa meng-index-nya.

**v5 ada karena v4 hanya meng-index `camera_model`.** Akibatnya mencari merek ("Xiaomi", "Canon")
mengembalikan nol meskipun `camera_make` sudah ada di tabel. Tabel FTS5 tidak bisa `ALTER`, tapi tidak
perlu — make dan model digabung ke kolom `camera_model` yang sudah ada (sekarang lebih tepat dibaca
sebagai "identitas kamera"), jadi hanya trigger yang dibuat ulang.

**v6 butuh `COLLATE NOCASE`, dan butuh `id DESC`.** Keduanya bukan hiasan:
- Query membandingkan dengan `source_album = ? COLLATE NOCASE`. Dengan index ber-kolasi BINARY,
  **SQLite menolak memakainya** dan jatuh ke `SCAN media` + temp b-tree. Terkonfirmasi lewat
  `EXPLAIN QUERY PLAN`.
- Tanpa `id DESC` di index, SQLite masih mengeluarkan `USE TEMP B-TREE FOR LAST TERM OF ORDER BY`.

---

## 3. FTS dijaga oleh trigger, bukan kode aplikasi

Index disinkronkan oleh **trigger SQLite**, sehingga **tidak ada jalur kode mana pun** — indexer,
recycle bin, empty trash, atau wrapper di masa depan — yang bisa lupa memperbaruinya:

```sql
CREATE TRIGGER media_fts_ai AFTER INSERT ON media BEGIN
    INSERT INTO media_fts(rowid, filename, camera_model, album, tags)
    VALUES (new.id, new.filename,
            trim(COALESCE(new.camera_make,'') || ' ' || COALESCE(new.camera_model,'')),
            COALESCE(new.source_album,''), '');
END;

CREATE TRIGGER media_fts_ad AFTER DELETE ON media BEGIN
    DELETE FROM media_fts WHERE rowid = old.id;
END;

CREATE TRIGGER media_fts_au AFTER UPDATE OF filename, camera_make, camera_model, source_album ON media BEGIN
    DELETE FROM media_fts WHERE rowid = old.id;
    INSERT INTO media_fts(...) VALUES (...);
END;
```

`rowid = media.id` adalah kunci join-nya.

**Siklus hidup trigger diverifikasi** pada DB 2000 baris:

| Aksi | Ekspektasi | Hasil |
|---|---|---|
| `UPDATE media SET deleted_at=999` (soft delete) | baris **tetap** (item ada di trash, filenya masih ada) | baris FTS masih ada ✓ |
| `DELETE FROM media` (purge) | baris **hilang** | fts=0 ✓ |
| `UPDATE media SET source_album='ZzAlbum'` | baris di-refresh & bisa dicari lewat album baru | cocok ✓ |
| `DELETE FROM media` (semua) | index kosong | fts=0 ✓ |

Poin pertama disengaja: **item di recycle bin tetap bisa dicari** selama masih ada.

---

## 4. File baru: `src/core/db/LibraryMaintenance.{h,cpp}`

Pass data sekali-jalan yang tidak bisa dinyatakan lewat DDL (mereka memanggil `SourceAlbum::detect()`
atau menelusuri baris yang sudah ada). Idempoten, dijaga nilai di tabel `meta`, dipanggil dari
`AppController::initialise()` tepat setelah `migrate()`.

```cpp
namespace LibraryMaintenance {
inline constexpr int kSourceAlbumRulesVersion = 1;  // bump bila SourceAlbum::rules() berubah
inline constexpr int kFtsIndexVersion = 2;          // v2 = camera_make digabung (migrasi v5)
bool ensureSourceAlbum(Database&, QString *error = nullptr);
bool ensureFtsPopulated(Database&, QString *error = nullptr);
}
```

**`ensureSourceAlbum`** — bila versi aturan di `meta` berbeda: `SELECT id, path, filename`, hitung
`SourceAlbum::detect()` per baris, lalu dalam **satu transaksi** UPDATE semua dan catat versinya.
**Baca dulu, tulis kemudian** — cursor yang terbuka di koneksi yang sama sementara UPDATE dijalankan
adalah risiko self-deadlock. **UPDATE inilah yang menyalakan trigger v4/v5**, jadi satu pass ini
sekaligus mengisi index untuk library yang sudah ada.

Konsekuensi desain yang penting: **mengubah aturan deteksi album nanti adalah operasi yang didukung**,
bukan berarti user harus reindex manual — cukup naikkan `kSourceAlbumRulesVersion`.

**`ensureFtsPopulated`** — bila versi index berbeda: **rebuild penuh** (`DELETE FROM media_fts` lalu
`INSERT ... SELECT` dari media), **bukan** mengisi yang kosong saja. Alasannya: *isi* yang di-index
berubah antar versi, jadi baris yang di-index dengan definisi lama akan **selamanya** tetap cocok
dengan teks lama kalau tidak dibangun ulang. Konsistensi rutin tidak bergantung pada fungsi ini —
trigger yang menanganinya. No-op bila `!ftsAvailable()`.

**Urutan di `initialise()` penting:** `ensureSourceAlbum` dulu (UPDATE-nya menyalakan trigger), baru
`ensureFtsPopulated`.

---

## 5. Bug yang ditemukan: `--no-ui` tanpa pekerjaan startup menggantung selamanya

Ketemu saat menguji jalur upgrade. Cabang "tidak ada pekerjaan" di `runStartupRequests()` adalah:

```cpp
reload();
finishStartupChain();   // guard di awal: if (!m_startupChainActive) return;
```

`m_startupChainActive` **tidak pernah di-set true** di cabang itu, jadi `startupCompleted()` tidak
pernah dipancarkan dan `--no-ui` tidak pernah keluar. Dua cabang lain (seed, index) men-set flag
sebelum kerja asinkronnya — itulah sebabnya `--seed-benchmark` dan `--index` selalu jalan dan bug ini
tidak pernah terlihat.

**Dampak nyata ke dokumentasi:** `docs/M0-REPORT.md` mendokumentasikan "smoke test tanpa display"
sebagai `QT_QPA_PLATFORM=offscreen ./build/kin-galeri ...` **tanpa `--no-ui`**. Perintah itu memang tidak
akan pernah selesai, dan itu **memang benar** — tanpa `--no-ui` aplikasi adalah GUI yang tetap berjalan
menunggu event loop. Dua hal berbeda yang mudah tertukar:

| Perintah | Perilaku | Status |
|---|---|---|
| `--no-ui` tanpa pekerjaan startup | dulu menggantung selamanya | **BUG — diperbaiki, sekarang exit 0** |
| offscreen tanpa `--no-ui` | tetap berjalan | **BENAR** — itu memang aplikasi GUI |
| offscreen + `--no-ui` | bekerja lalu keluar | sudah benar sejak awal |

Dokumen M0 sudah diperbaiki agar memakai `--no-ui`, dengan catatan mengapa flag itu wajib.
Diverifikasi: `QT_QPA_PLATFORM=offscreen ... --no-ui` → **exit 0**.

Perbaikan:

```cpp
// Flag harus dinaikkan SEBELUM reload(), karena reload() berjalan sinkron dan
// memanggil finishStartupChain() sebelum fungsi ini kembali. Tanpa itu guard di
// finishStartupChain() melihat chain tidak aktif dan return lebih awal, sehingga
// startupCompleted() tidak pernah dipancarkan - yang diam-diam menggantung --no-ui
// pada library yang tidak butuh indexing.
m_startupChainActive = true;
reload();
finishStartupChain();
```

**Jalur startup terverifikasi (semua diperiksa lewat exit code, bukan hanya log):**
`--no-ui` tanpa kerja → **0**; offscreen + `--no-ui` → **0**; `--index` → **0**; `--seed-benchmark` → **0**.

> **Pelajaran yang dicatat:** ini **kedua kali** bug di area ini hanya ketemu karena kombinasi CLI
> tertentu belum pernah dijalankan. Pengujian harus selalu memeriksa **exit code proses**, bukan hanya
> isi log.

---

## 6. Perubahan `MediaGridModel`

- **`reload()` SELECT** mendapat `source_album` sebagai kolom **terakhir** (index 17), dengan fallback
  `SourceAlbum::detect()` bila kosong (baris dari sebelum v3) — tidak lagi dihitung tanpa syarat.
- **Filter album sekarang SQL:** `AND source_album = :album COLLATE NOCASE`.
  `applyFiltersToLoadedRows()` **dihapus seluruhnya.** Ini sekaligus memperbaiki bug LIMIT-sebelum-filter
  dan `m_totalBytes` yang salah.
- **Pencarian sekarang FTS5** bila tersedia, kalau tidak jatuh ke LIKE:
  ```cpp
  AND id IN (SELECT rowid FROM media_fts WHERE media_fts MATCH :fts)
  ```
  Ekspresi dibangun **sebelum** SQL sehingga bentuk statement ditentukan sekali. Query tanpa token yang
  bisa di-index menghasilkan `AND 0` — jujur: user mengetik sesuatu yang tidak mungkin cocok — alih-alih
  diam-diam mencocokkan semua baris.
- **`buildFtsExpression()`** — fungsi baru, dengan tiga properti:
  - **Aman:** setiap term dibungkus tanda kutip ganda (kutip di dalamnya digandakan), sehingga operator
    FTS5 (`AND`, `OR`, `NOT`, `NEAR`, `^`, `-`, filter kolom seperti `filename:`) di input pengguna
    menjadi **data literal, bukan sintaks**.
  - **Prefix matching:** tiap term diberi `*` di belakangnya, jadi mengetik "vac" cocok dengan
    "vacation.jpg".
  - **Keselarasan tokenizer dengan FTS5 (bug yang saya temukan lewat penalaran, lalu diverifikasi):**
    tokenizer `unicode61` memperlakukan **tanda baca dan simbol sebagai pemisah**, jadi
    `IMG_20250808_070657.jpg` di-index sebagai token `img`, `20250808`, `070657`, `jpg`. Input pengguna
    **harus dipecah dengan cara yang sama — tanda baca diganti SPASI, bukan dihapus.** Menghapusnya akan
    menyatukan `IMG_20250808` menjadi satu term `img20250808` yang tidak ada di mana pun di index, dan
    query akan **diam-diam mengembalikan nol hasil**. Implementasi:
    `for (const QChar &ch : input) normalized.append(ch.isPunct() || ch.isSymbol() ? QLatin1Char(' ') : ch);`
    lalu split pada `\s+`.
- **`mediaDetail()`** mendapat `source_album` (index 26) dan memakai nilai tersimpan dengan fallback
  `detect()`.
- **Properti baru `albumNames`** (`Q_PROPERTY(QStringList albumNames READ albumNames NOTIFY albumsChanged)`):
  ```sql
  SELECT DISTINCT source_album FROM media
  WHERE deleted_at IS NULL AND source_album IS NOT NULL AND source_album <> ''
  ORDER BY source_album COLLATE NOCASE
  ```
  Dibuat sebagai **properti, bukan invokable**, supaya QML otomatis mengevaluasi ulang saat library
  berubah.

---

## 7. Perubahan file lain

- **`FolderIndexer.cpp`** — `source_album` masuk ke kolom INSERT, VALUES (`:album`), dan blok
  `ON CONFLICT ... DO UPDATE SET`. Karena blok UPDATE hanya menyala saat mtime/size berubah, perubahan
  **aturan** album ditangani `LibraryMaintenance`, bukan reindex — pemisahan yang disengaja.
- **`BenchmarkSeeder.cpp`** — `:album` ditambahkan, di-bind lewat `SourceAlbum::detect(path, filename)`
  (bukan `profile.album`) supaya benchmark benar-benar menguji aturan deteksi asli.
- **`Sidebar.qml`** — daftar album hardcoded `["Camera", "Screenshots", …]` diganti
  `model: root.app.model.albumNames`. Ditambah empty-state ("No albums yet — index a folder to populate
  this list"). **Ini juga memperbaiki celah lama:** folder yang tidak cocok aturan bawaan (jatuh ke nama
  direktori induk, misalnya `Element-Cyber9`) sebelumnya **tidak pernah bisa dipilih** di sidebar.
- **`AppController.cpp`** — pemanggilan kedua fungsi maintenance setelah `migrate()`, dengan propagasi error.
- **`CMakeLists.txt`** — `LibraryMaintenance.{cpp,h}` masuk `KG_SOURCES`.

---

## 8. Hasil pengukuran

### 8.1 Pencarian di 100k baris (target PRD < 50 ms) — SEMUA LULUS

100k baris sintetis di-seed dalam 6,3 s; fts=100000 (trigger mengikuti).

| Query | Sebelum | Sesudah |
|---|---|---|
| FTS `"IMG"*` (~58k baris cocok — kasus terburuk) | 58,93 ms (LIKE) | **44,99–48,12 ms** |
| FTS `"IMG"* "2025"*` (multi-term AND) | — | **8,28 ms** |
| FTS `"M2101K6G"*` (selektif) | 24,85 ms (LIKE) | **0,03 ms** |
| FTS term langka | — | **0,40 ms** |
| FTS + filter album | — | **12,87–15,16 ms** |
| album `Camera` (~58k baris) | 51,36 ms | **14,70 ms** |
| album `WhatsApp` | 30,09 ms | **2,45 ms** |
| album `Instagram` | — | **0,59 ms** |
| `DISTINCT source_album` (sidebar) | — | **4,66 ms** |
| tanpa filter, seluruh library (100k) | — | **41,70 ms** |

**Pembacaan yang jujur:** query agregat tanpa filter pada 100k baris saja sudah 41,70 ms. Jadi FTS
luas pada ~45 ms hanya menambah **~3 ms** di atas batas bawah yang tidak bisa dihindari — yaitu
mematerialisasi 100k baris terurut. **Pencarian FTS sendiri nyaris gratis.** Yang dominan adalah
`ORDER BY taken_at DESC` atas seluruh library, dan itu masalah berbeda (paginasi/windowing), bukan
masalah index pencarian.

### 8.2 Overhead DB

| | Sebelum | Sesudah |
|---|---|---|
| Ukuran DB, 50k baris | 16,69 MB (334 B/item) | 22,07 MB (**463 B/item**) |
| — FTS (content 2,45 + data 1,39 + docsize 0,61) | 0 | 4,45 MB (~91 B/item) |
| — `idx_media_album_taken` | 0 | 1,39 MB (~28 B/item) |
| tabel `media` | 9,06 MB | 9,06 MB |
| `sqlite_autoindex_media_1` (path UNIQUE) | 4,32 MB | 4,32 MB |

Masih **4× di bawah budget PRD 2 KB/item.** Full-text search berbiaya ~91 B/item — murah.

### 8.3 Cek regresi scroll (50k sintetis) — TIDAK ADA REGRESI

```
startup: ms=643
resting: rss=362MB thumbs=48
scroll: seconds=10.0 distance=30000px items=918 frames=600
  dropped=0 (0.00%) fps=60.00 avg=16.67ms worst=16.9ms target=60fps
  rss=482MB rssPeak=482MB libitems=50000
```

Cache thumbnail dingin dan hangat: **60,00 fps, 0/600 dropped.** Startup 643 ms — **lebih cepat** dari
M1 (708–761 ms).

**RSS naik 439 MB → 482 MB (+43 MB).** Penyebabnya index FTS yang di-mmap (`mmap_size=268435456`)
ditambah struktur FTS dan string `sourceAlbum` ekstra per baris.

> **Peringatan yang harus dibawa ke M2:** PRD §4.1.1 merevisi target memory ke < 550 MB untuk **100k**
> item. Pada **50k** kita sudah di 482 MB. **Artinya target itu perlu dicek ulang di 100k — kemungkinan
> besar tidak tercapai.** Ini utang baru yang jujur, bukan sesuatu yang bisa dibiarkan.

---

## 9. Verifikasi

- **DB baru:** keenam migrasi diterapkan berurutan; 3 trigger ada; `fts=39 = media=39`;
  `album_notnull=39`; album yang terdeteksi = `Element-Cyber9` (fallback direktori induk) + `WhatsApp`.
- **Jalur upgrade dari DB era-M1 (v2) yang disimulasikan** — trigger di-drop, index album di-drop,
  `ALTER TABLE media DROP COLUMN source_album`, `media_fts` dan `meta` dikosongkan, `PRAGMA user_version=2`:
  membuka ulang menghasilkan migrasi v2→v6, `triggers=3`, `fts=39`, `album_notnull=39`, meta key benar,
  dan index yang dipakai. **Jalur upgrade bekerja.**
- **Kebenaran query FTS** (SQL mentah, identik dengan yang dihasilkan `buildFtsExpression`):
  `Xiaomi`→35, `M2101K6G`→36, `WhatsApp`→1, `IMG`+`2025`→31, `IMG`+`20250808`→3, `Canon`→0,
  `nonexistentzz`→0.
- **GUI, library nyata (39 file), X11 :0.0:** sidebar menampilkan **album asli dari DB**
  (`Element-Cyber9`, `WhatsApp`) alih-alih enam yang di-hardcode. Klik album menyaring dengan benar
  (`38 items · 259,7 MB · Element-Cyber9`). Mengetik `IMG_2025` di kolom pencarian menghasilkan
  `31 items · 139,6 MB · "IMG_2025"` — **persis sama dengan hasil SQL mentah (31)**, membuktikan jalur
  FTS multi-token bekerja utuh dari UI sampai query.

---

## 10. Yang masih terbuka

> **Status per akhir M3.** Daftar ini disegarkan setelah M3 selesai; entri lama yang sudah
> beres dicatat sebagai *Selesai* agar sejarahnya tidak hilang.

### Selesai setelah laporan ini pertama ditulis

- **Jalur perbaikan FTS (dulu: hanya rebuild saat versi berubah).** Sekarang ada
  `LibraryMaintenance::rebuildFts()` yang tanpa syarat membangun ulang `media_fts` dari `media`, dan
  **membuat ulang virtual table + trigger** bila keduanya hilang. Ia dipakai oleh
  `ensureFtsPopulated()` (rebuild sekali saat versi naik) dan tersedia sebagai jalur pemulihan manual
  lewat CLI `--rebuild-search-index` serta `AppController::rebuildSearchIndex()`. DDL-nya diambil dari
  `Migrations::ftsSchemaStatements()` supaya definisi tabel dan trigger tidak terduplikasi. Diuji di
  `tests/tst_librarymaintenance.cpp`.
- **`Database` tidak punya destruktor** sehingga koneksi (yang dinamai per-thread, bukan per-file)
  bocor dan `Database` kedua untuk file lain di thread yang sama diam-diam memakai file lama. Ditemukan
  oleh test; sekarang `~Database()` memanggil `closeThreadConnection()`. Ini tidak terlihat di aplikasi
  karena hanya ada satu `Database` seumur proses, tetapi berbahaya untuk library switching dan test.
- **M2 sudah selesai** (album manual, seleksi + batch action, move/rename/delete + undo, inotify
  watcher, pemutaran video + ekstraksi frame, budget memory thumbnail adaptif, filter rentang tanggal).
  Lihat `docs/M2-REPORT.md` §9–§10.
- **Target memory 100k** terukur 330 MB (peak 351 MB), jauh di bawah target PRD < 550 MB. Lihat
  `docs/M2-REPORT.md` §8.
- **Tidak ada test sama sekali.** Sekarang ada `tests/` (11 target, `-DKG_BUILD_TESTS=ON`) yang mengunci
  perilaku M2–M4: album CRUD, seleksi, filter tanggal, rename/move termasuk rollback, trash + undo,
  incremental scan + rekonsiliasi, debounce watcher, parsing CLI, jalur pemulihan FTS, skema shortcut
  pengguna, pengelompokan tanggal/geo/event, dan deteksi decoder HEIF.

### Masih terbuka

- ~~**Terjemahan C++ (M4)**~~ — **sudah ditutup.** 43 string dibungkus `tr()`, ketiga konteks C++
  ada di `.ts` (349 pesan: 258 QML + 92 C++). Konteksnya adalah nama metaobject (`kg::AppController`),
  bukan nama kelas — kesalahan itu sempat mematikan seluruh terjemahan C++ tanpa terlihat, dan
  sekarang dijaga `tests/tst_i18n.cpp`. Lihat `docs/M4-REPORT.md` §2.
- **Peringatan `UnqualifiedAccess` qmllint (241 buah)**: komponen bersarang membaca id `root` pihak
  luar, yang dilarang `pragma ComponentBehavior: Bound`. Kategori ini dimatikan di `.qmllint.ini`
  dengan komentar alasan; kategori lain aktif dan gate `./scripts/qml-lint.sh` sekarang 0 peringatan.
- **`KinGaleri.Mpv` belum punya qmldir statis**: tipe `MpvItem` didaftarkan runtime lewat
  `qmlRegisterType()`, sehingga `MpvVideoSurface.qml` tidak bisa di-resolve saat lint/kompilasi.
  Kandidat perbaikan: `qt_add_qml_module` + `QML_ELEMENT` untuk modul tersebut.
- **Tampilan trash** (`deleted_at IS NOT NULL`) tidak memakai partial index — dapat diterima karena
  recycle bin secara alami kecil.
- **`Database` menamai koneksi per-thread, bukan per-file/instance.** Aman untuk aplikasi saat ini,
  tetapi kalau suatu saat dua library dibuka bersamaan dalam satu thread, `connection()` akan diam-diam
  mengembalikan koneksi pertama. Destruktor menutup celah untuk pemakaian berurutan.
- **HEIC/HEIF**: sudah ditutup, `heif-convert` dipakai sebagai fallback on-demand (migration tidak
  terlibat; lihat `src/core/thumbs/ImageLoader.cpp`).
- **Smart grouping (M3)** selesai, tetapi menyisakan utang kecil: rename/merge orang memakai model
  `MediaGridModel` yang sama dengan grid, jadi keduanya membaca `people` secara live (properti
  `people`) yang dapat memicu query ketika halaman People sedang tidak terlihat. Belum menjadi masalah
  pada ukuran library yang diuji, tetapi kandidat untuk model khusus People bila jumlah cluster besar.
- **Klaster wajah bersifat greedy by-confidence**: satu wajah yang salah cluster tidak bisa dibatalkan
  per-wajah. Merge/split manual per orang baru menyediakan merge; split belum ada (M5).
