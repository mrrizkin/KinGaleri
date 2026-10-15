# PRD — "Kin Galeri" — Immich-like Local Photo Library untuk Linux (Qt6/C++)

> Dokumen ini adalah Product Requirements Document (PRD) untuk membangun aplikasi galeri foto/video
> lokal-first untuk Linux, dengan pengalaman grouping & performa yang meniru Immich / Xiaomi Gallery,
> dibangun dengan **Qt6 + C++** (tanpa server, tanpa cloud, tanpa akun).

---

## 1. Ringkasan Produk

**Nama produk:** Kin Galeri  
**Nama paket/binary:** `kin-galeri`  
**App ID (Flatpak/freedesktop):** `io.github.mrrizkin.KinGaleri` — app-id wajib reverse-DNS  
**Tagline:** Galeri lokal super cepat untuk Linux — indexing sekali, scroll ribuan foto tanpa lag.

> Etimologi nama: *kin* = kerabat/famili. Sengaja dipilih karena fitur pembeda utama adalah
> **People grouping** — galeri yang mengenali kerabatmu.

**Tipe:** Desktop application, offline-first, single-user.  
**Stack inti:** C++20 + **Qt6 Quick (QML)** + SQLite + image codec native.  
**Path runtime (freedesktop spec):**
```
Binari / paket     : kin-galeri
Desktop entry      : kin-galeri.desktop
Config             : ~/.config/kin-galeri/
Data (DB library)  : ~/.local/share/kin-galeri/library.db
Cache (thumbnail)  : ~/.cache/kin-galeri/thumbs/
App ID             : io.github.mrrizkin.KinGaleri
```

### 1.1 Masalah yang Diselesaikan

| Masalah | Kondisi saat ini | Target |
|---|---|---|
| Foto tersebar di banyak folder | Nautilus/Dolphin tidak punya konsep "library" | Satu library terindeks dari beberapa root folder |
| Grouping manual melelahkan | Tidak ada auto-album / people / places | Grouping otomatis (date, place, people, source) |
| Scroll lag di folder besar | File manager load thumbnail on-the-fly | Virtual scrolling + cache thumbnail → 60 fps |
| Metadata hilang / sulit dicari | Harus buka satu-satu | Full-text metadata search (EXIF, nama file, tag) |
| Cloud privacy concern | Google Photos/Immich harus server | Semua lokal, tidak ada network call wajib |

### 1.2 Non-Goals (v1)

- ❌ Mobile app / sync lintas device (kandidat v2+)
- ❌ Multi-user, sharing, permission
- ❌ Cloud sync / remote server (Immich-style API server)
- ❌ Video editing / timeline editor
- ❌ RAW editing / non-destructive develop (mirip Lightroom)
- ❌ Mobile-style AI super-resolution
- ❌ Windows/macOS (fokus Linux dulu, tapi arsitektur portable)

---

## 2. Target Pengguna & Persona

**Primary persona — "Rizkin, power user foto lokal":**
- Punya 20.000–200.000+ file foto/video dari HP (Xiaomi), DSLR, screenshot, WhatsApp.
- Sudah terbiasa dengan Xiaomi Gallery / Google Photos.
- Peduli privacy, tidak mau upload ke cloud.
- Pakai Linux desktop (Wayland/GNOME/KDE).
- Ekspektasi: aplikasi tidak boleh lag saat scroll, buka < 2 detik.

**Success criteria persona:**
- Dari buka app sampai bisa scroll library 50.000 foto: **< 3 detik** (warm cache).
- Scroll cepat (flick) tanpa frame drop terasa.
- Grouping date/people/album otomatis sudah benar setelah indexing pertama.

---

## 3. Functional Requirements

### 3.1 Library & Indexing (F-1)

| ID | Requirement | Prioritas |
|---|---|---|
| F-1.1 | User bisa menambahkan satu atau lebih **root folder** library | P0 |
| F-1.2 | Indexing awal: scan rekursif, ekstrak metadata, generate thumbnail | P0 |
| F-1.3 | **Incremental scan**: deteksi file baru/berubah/hilang via mtime + size | P0 |
| F-1.4 | File watcher (inotify) untuk auto-detect perubahan saat app jalan | P1 |
| F-1.5 | Metadata: EXIF (tanggal ambil, GPS, kamera, orientasi), video (durasi, codec) | P0 |
| F-1.6 | Hash konten (partial hash cepat) untuk deteksi duplikat | P1 |
| F-1.7 | Skip file non-media; dukung HEIC/HEIF, JPEG, PNG, WebP, GIF, TIFF, MP4, MOV, MKV, WebM, AVI | P0 |
| F-1.8 | Progress UI yang bisa di-cancel & di-resume | P0 |
| F-1.9 | Indexing berjalan di background thread, UI tetap responsif | P0 |
| F-1.10 | Pilihan lokasi cache (thumbnail + DB) agar bisa dipisah dari media disk | P1 |

### 3.2 Grouping (F-2) — *core differentiating feature*

| ID | Requirement | Prioritas |
|---|---|---|
| F-2.1 | **Group by Date**: Bucket hari ini, kemarin, minggu ini, bulan, tahun. Header sticky | P0 |
| F-2.2 | **Group by Source/Album otomatis**: Camera, Screenshots, Downloads, WhatsApp, Telegram, dan folder-based | P0 |
| F-2.3 | **Group by Location** (dari GPS EXIF → reverse geocode opsional/offline) | P1 |
| F-2.4 | **Group by People** (face detection + clustering) | P1 |
| F-2.5 | **Smart Category** (scene/objek: dokumen, makanan, hewan, alam) via model lokal | P2 |
| F-2.6 | **Favorites** & **Archive/Hidden** | P0 |
| F-2.7 | **Recycle Bin** — soft delete, restore, purge | P0 |
| F-2.8 | User bisa membuat **manual album** dan menambahkan item | P1 |
| F-2.9 | Grouping rules bisa di-toggle user (index mana yang aktif) | P1 |
| F-2.10 | Event grouping (mis. "Trip Bali, 12–15 Juli") — auto event clustering by time+place gap | P2 |

### 3.3 Browsing & Viewing (F-3)

| ID | Requirement | Prioritas |
|---|---|---|
| F-3.1 | Virtual-scrolled grid (justified/masonry), hanya render item terlihat | P0 |
| F-3.2 | Zoom level grid (small → large thumbnail) dengan slider | P1 |
| F-3.3 | Full-screen viewer: zoom, pan, next/prev, keyboard nav | P0 |
| F-3.4 | Video playback (libmpv / QMediaPlayer / ffmpeg) | P0 |
| F-3.5 | Sidebar: Library, Albums, People, Places, Favorites, Recycle Bin | P0 |
| F-3.6 | Timeline scrubber (scroll ke tanggal tertentu cepat) | P1 |
| F-3.7 | Selection mode (single, ctrl, shift range) | P0 |
| F-3.8 | Info panel: EXIF lengkap, path, ukuran, tanggal | P1 |
| F-3.9 | Slideshow mode | P2 |
| F-3.10 | Dukungan HiDPI & fractional scaling Wayland | P0 |

### 3.4 Search & Filter (F-4)

| ID | Requirement | Prioritas |
|---|---|---|
| F-4.1 | Search by filename (prefix/substring) | P0 |
| F-4.2 | Filter by date range, media type, source album, favorite | P0 |
| F-4.3 | Filter by camera/lens, ISO, aperture (EXIF) | P2 |
| F-4.4 | Semantic search (text → embedding) | P2 |

### 3.5 Organize & Actions (F-5)

| ID | Requirement | Prioritas |
|---|---|---|
| F-5.1 | Rename, move, copy, delete file (dengan konfirmasi & undo) | P1 |
| F-5.2 | Add to album, remove from album | P1 |
| F-5.3 | Favorite/unfavorite, hide/unhide | P0 |
| F-5.4 | Rotate (lossless utk JPEG via EXIF/jpegtran) | P2 |
| F-5.5 | Copy path / open in file manager / open with external app | P0 |
| F-5.6 | Export/share (copy to folder, clipboard) | P1 |
| F-5.7 | Batch operation pada multi-select | P0 |

### 3.6 Settings (F-6)

| ID | Requirement | Prioritas |
|---|---|---|
| F-6.1 | Kelola library roots | P0 |
| F-6.2 | Cache size limit & "clear cache" | P0 |
| F-6.3 | Toggle AI features (faces, categories) & unduh model | P1 |
| F-6.4 | Theme: light/dark/system, accent color | P1 |
| F-6.5 | Thumbnail quality/size preference | P2 |
| F-6.6 | Re-index / rebuild DB | P0 |
| F-6.7 | Keyboard shortcuts customization | P2 |

---

## 4. Non-Functional Requirements

### 4.1 Performance (paling kritis)

> **Status: diverifikasi di M0** (X11, AMD Radeon renoir, OpenGL 4.6 / Mesa 26.2.4, vsync 16.67 ms,
> 50.000 baris sintetis). Kolom "Measured M0" di bawah adalah hasil nyata, bukan estimasi.

| Metrik | Target | Measured M0 | Status |
|---|---|---|---|
| Cold start (DB ada, cache ada) | **< 1.5 s** sampai grid terlihat | 656–717 ms | ✅ |
| Warm scroll 50k item | **60 fps konsisten**, tidak ada jank > 16 ms | 60.00 fps, avg 16.67 ms, worst 17.2–17.5 ms, **0–1 dropped / 720 frame** | ✅ |
| Memory footprint idle library 100k | **< 400 MB RSS** → **direvisi, lihat §4.1.1** | baseline Qt saja 214–270 MB | ⚠️ revisi |
| Thumbnail load dari disk cache | < 5 ms/item | belum diisolasi | ⏳ M1 |
| Initial indexing throughput | ≥ **150 file/detik** (metadata only), ≥ 30 file/detik (+thumbnail) | belum diukur di folder foto nyata | ⏳ M1 |
| Search query di 100k item | < 50 ms | **0,03 ms** (term selektif) · **8,3 ms** (multi-term) · **~45 ms** (query luas ~58k hasil; batas bawah tanpa filter saja 41,7 ms) | ✅ |
| DB size overhead | < 2 KB/item (tanpa blob thumbnail) | **463 B/item** (334 B sebelum index FTS) | ✅ |

#### 4.1.1 Revisi target memory (temuan M0)

Pengukuran M0 membuktikan target **"< 400 MB RSS untuk 100k item" tidak dapat dicapai secara aritmatika**
stack ini, terlepas dari kualitas kode aplikasi:

| Komponen | Ukuran | Bisa dikurangi? |
|---|---|---|
| Qt 6.12 + Qt Quick + Fusion + Mesa baseline (aplikasi QML kosong, tanpa model & tanpa gambar) | **214 MB** | ❌ tidak oleh kode kita |
| Baseline aplikasi kita saat idle (`--limit 1`) | **270 MB** | sebagian kecil |
| Model per-item | ≈ **0.8–0.9 KB/item** (10k→50k = +40 MB) | sedikit |
| Budget decoded-thumbnail LRU (default 128 MB) | ≈ **115–124 MB** saat scroll | ✅ **paling bisa dikurangi** |
| **Total proyeksi 100k item** | **≈ 215 + 85 + 128 ≈ 428 MB** | — |

**Target revisi (diputuskan):**

1. **Memory idle library 100k: < 550 MB RSS** (bukan 400 MB), dengan catatan baseline Qt ~215 MB tidak
   bisa dihilangkan. Angka ini realistis dan masih wajar untuk aplikasi foto desktop modern.
2. **Budget thumbnail memory LRU diubah dari fixed 128 MB menjadi adaptif & display-aware** — dihitung dari
   jumlah sel yang terlihat (`columns × visibleRows × 2.5` sambil mempertimbangkan `edge²  × 4 byte`),
   di-floor 32 MB dan di-ceiling 256 MB. Untuk grid 6×5 pada 1200×800 ini menghasilkan ~10–30 MB,
   menghemat ~100 MB dibanding default sekarang. `--thumb-memory-cache` tetap ada sebagai override manual.
3. **Tambahkan metrik baru yang lebih bermakna: "RSS per item setelah baseline dikurangi" < 2 KB/item.**
   Ini mengukur apa yang benar-benar kita kendalikan dan tidak menghukum kita atas overhead toolkit.
4. Ukuran thumbnail default tetap **256 px** (dengan budget adaptif, biaya memorinya sudah terkendali).

> Catatan: `--limit <N>` + `--bench-scroll` adalah cara mengukur ulang regresi memory; output benchmark
> machine-readable di stderr ber-prefix `kin-galeri[bench]` sudah memuat `rss=`, `rssPeak=`, dan `thumbcache=`.


### 4.2 Scalability

- Library target: **500.000 item** tanpa degradasi arsitektural (hanya butuh resource lebih).
- Tidak boleh ada operasi O(n) di render path.
- Thumbnail cache harus bisa di-prune otomatis (LRU) dan dibatasi ukuran.

### 4.3 Reliability

- Crash-safe: indexing bisa di-resume, DB tidak korup (WAL + checkpoint).
- Atomic write untuk cache & DB.
- Semua operasi destruktif (delete file) → recycle bin dulu, lalu purge terpisah.

### 4.4 Platform

- Linux (target utama): Wayland + X11, glibc-based distro.
- Build: CMake + Ninja, paket `.deb`/AppImage/Flatpak (v1 minimal: AppImage + source build).
- Qt6 minimum: **6.5 LTS** (Quick/QuickControls2, Sql, Concurrent, Multimedia + **FFmpeg media backend**). C++20.

### 4.5 Privacy & Security

- **Zero network requirement** untuk fitur inti.
- Model AI diunduh opt-in, file model user-supplied/lokal.
- Tidak ada telemetry.
- Path traversal & symlink loop dilindungi saat scan.

### 4.6 Accessibility & i18n

- Keyboard-navigable penuh.
- Screen reader-friendly (Qt accessibility).
- Siap untuk i18n (Qt `.ts` translation), v1: English + Bahasa Indonesia.

---

## 5. Arsitektur Teknis

### 5.1 Diagram high-level

```
┌──────────────────────────────────────────────────────────────┐
│              UI Layer (Qt6 Quick / QML + C++ backend)          │
│  ┌─────────┐ ┌──────────────┐ ┌──────────┐ ┌──────────────┐  │
│  │ Sidebar │ │ Grid View    │ │ Viewer   │ │ Settings/    │  │
│  │         │ │ (virtual)    │ │          │ │ Dialogs      │  │
│  └─────────┘ └──────────────┘ └──────────┘ └──────────────┘  │
└──────────────────────────┬───────────────────────────────────┘
                           │ (Qt signals/slots, QAbstractItemModel)
┌──────────────────────────▼───────────────────────────────────┐
│                      Application Core                         │
│  LibraryService · GroupingEngine · SearchService · Actions    │
└───────┬──────────────┬──────────────┬───────────────┬─────────┘
        │              │              │               │
┌───────▼──────┐ ┌─────▼──────┐ ┌─────▼───────┐ ┌─────▼───────┐
│ Indexer      │ │ Thumbnail  │ │ Metadata    │ │ AI Workers  │
│ (workers)    │ │ Cache      │ │ (Exiv2)     │ │ (opt-in)    │
│ inotify      │ │ (LP/disk)  │ │             │ │ OpenCV/etc  │
└───────┬──────┘ └─────┬──────┘ └─────┬───────┘ └─────┬───────┘
        │              │              │               │
┌───────▼──────────────▼──────────────▼───────────────▼───────┐
│              Storage Layer                                    │
│   SQLite (metadata, WAL)   Thumbnail files (disk, sharded)   │
└──────────────────────────────────────────────────────────────┘
```

### 5.2 Threading model

- **Main thread (UI)**: Qt event loop. Tidak ada I/O blocking.
- **Indexer pool**: N worker threads (`QThreadPool`, N = min(hw_concurrency, 8)) untuk scan + metadata + thumbnail.
- **Thumbnail decode pool**: terpisah, prioritas tinggi, untuk permintaan runtime dari grid.
- **AI pool**: prioritas rendah (nice), idle-time only, agar tidak ganggu scroll.
- **DB writer**: **single writer thread** (SQLite serialized), reader via WAL read connections per thread.
- Komunikasi UI↔worker via signals + batched model updates (debounced 16–33 ms) agar tidak spam event loop.

### 5.3 Storage design (SQLite)

```sql
-- Media item
CREATE TABLE media (
  id            INTEGER PRIMARY KEY,
  path          TEXT NOT NULL UNIQUE,     -- absolute path
  path_hash     BLOB NOT NULL,            -- xxhash64(path) utk fast lookup
  root_id       INTEGER NOT NULL REFERENCES roots(id),
  filename      TEXT NOT NULL,
  ext           TEXT NOT NULL,
  mime          TEXT NOT NULL,            -- image/jpeg, video/mp4...
  size_bytes    INTEGER NOT NULL,
  mtime_ns      INTEGER NOT NULL,
  ctime_guess   INTEGER,                  -- best-effort creation
  taken_at      INTEGER,                  -- EXIF DateTimeOriginal (epoch ms)
  taken_at_src  TEXT,                     -- 'exif' | 'mtime' | 'filename' | 'manual'
  width         INTEGER,
  height        INTEGER,
  duration_ms   INTEGER,                  -- video
  orientation   INTEGER DEFAULT 1,
  gps_lat       REAL,
  gps_lon       REAL,
  camera_make   TEXT,
  camera_model  TEXT,
  lens_model    TEXT,
  iso           INTEGER,
  f_number      REAL,
  exposure_time TEXT,
  focal_length  REAL,
  hash_partial  BLOB,                     -- quickhash utk duplikat
  is_favorite   INTEGER NOT NULL DEFAULT 0,
  is_hidden     INTEGER NOT NULL DEFAULT 0,
  deleted_at    INTEGER,                  -- recycle bin (NULL = aktif)
  indexed_at    INTEGER NOT NULL
);

CREATE INDEX idx_media_taken     ON media(taken_at DESC) WHERE deleted_at IS NULL;
CREATE INDEX idx_media_root      ON media(root_id);
CREATE INDEX idx_media_type      ON media(mime) WHERE deleted_at IS NULL;
CREATE INDEX idx_media_fav       ON media(is_favorite) WHERE is_favorite=1;
CREATE INDEX idx_media_hash      ON media(hash_partial);
CREATE INDEX idx_media_pathhash  ON media(path_hash);

-- Library roots
CREATE TABLE roots (
  id   INTEGER PRIMARY KEY,
  path TEXT NOT NULL UNIQUE,
  enabled INTEGER NOT NULL DEFAULT 1,
  added_at INTEGER NOT NULL
);

-- Auto/manual albums
CREATE TABLE albums (
  id        INTEGER PRIMARY KEY,
  kind      TEXT NOT NULL,      -- 'manual' | 'source' | 'event' | 'smart'
  name      TEXT NOT NULL,
  cover_media_id INTEGER,
  created_at INTEGER NOT NULL,
  UNIQUE(kind, name)
);

CREATE TABLE album_items (
  album_id INTEGER NOT NULL REFERENCES albums(id) ON DELETE CASCADE,
  media_id INTEGER NOT NULL REFERENCES media(id) ON DELETE CASCADE,
  added_at INTEGER NOT NULL,
  PRIMARY KEY (album_id, media_id)
);

-- Face detection
CREATE TABLE faces (
  id       INTEGER PRIMARY KEY,
  media_id INTEGER NOT NULL REFERENCES media(id) ON DELETE CASCADE,
  person_id INTEGER REFERENCES people(id) ON DELETE SET NULL,
  bbox_x   REAL, bbox_y REAL, bbox_w REAL, bbox_h REAL,
  embedding BLOB,             -- float32 vector
  confidence REAL
);
CREATE TABLE people (
  id   INTEGER PRIMARY KEY,
  name TEXT,
  cover_face_id INTEGER,
  auto_cluster_key TEXT
);

-- Places (agregat lokasi)
CREATE TABLE places (
  id INTEGER PRIMARY KEY,
  name TEXT, lat REAL, lon REAL, radius_m REAL
);
CREATE TABLE media_places (
  media_id INTEGER NOT NULL REFERENCES media(id) ON DELETE CASCADE,
  place_id INTEGER NOT NULL REFERENCES places(id) ON DELETE CASCADE,
  PRIMARY KEY (media_id, place_id)
);

-- Smart/ML tags
CREATE TABLE tags  (id INTEGER PRIMARY KEY, name TEXT UNIQUE, kind TEXT);
CREATE TABLE media_tags (
  media_id INTEGER NOT NULL REFERENCES media(id) ON DELETE CASCADE,
  tag_id   INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,
  score    REAL,
  PRIMARY KEY (media_id, tag_id)
);

-- Full-text search
CREATE VIRTUAL TABLE media_fts USING fts5(filename, camera_model, tags, content='');

-- Schema versioning
CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT);
```

**Pragmas wajib:**
```sql
PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;
PRAGMA foreign_keys = ON;
PRAGMA busy_timeout = 5000;
PRAGMA cache_size = -65536;   -- 64 MB page cache
PRAGMA mmap_size = 268435456; -- 256 MB
```

### 5.4 Thumbnail cache design

Thumbnail adalah kunci "no lag". Strategi:

1. **Ukuran**: simpan 1–2 varian, mis. 256px & 1024px (untuk viewer preview cepat).
2. **Format**: WebP (quality ~80) atau AVIF bila tersedia → kecil & decode cepat.
3. **Lokasi disk**: sharded `cache/thumbs/ab/cd/<id>.webp` (2-level, hindari 1 folder besar).
4. **Hot cache in-memory**: LRU `QCache<MediaId, QPixmap>` ukuran dibatasi (mis. 2000 item / 256 MB).
5. **Generate on demand**: saat grid minta item yang belum punya thumb → masuk queue prioritas + placeholder blurhash (opsional).
6. **Blurhash/pHash kecil** disimpan di DB untuk placeholder instan sebelum thumbnail siap.
7. **Prune**: jika cache > limit, hapus LRU (berdasarkan `last_access` di file stat atau tabel cache_index).

### 5.5 Virtual grid rendering

- Pakai `QAbstractListModel` custom (`MediaGridModel`) + view custom (`QQuickListView`/`QListView` dengan delegate ringan) **atau** `QML GridView` dengan `delegate` yang di-reuse.
- **Fetch-ahead**: minta N baris di depan & belakang viewport.
- **Model reset minimal**: gunakan `beginInsertRows`/`dataChanged` granular, jangan `reset()`.
- **Delegate ringan**: hanya `Image { source: ... ; asynchronous: true }` + teks minimal. Hindari shader efek, shadow, opacity animasi per item.
- **Backing store**: simpan hanya id + dims di model; metadata diambil lazy.
- **Timeline header**: sticky section header dari `taken_at` bucket.

### 5.6 Keputusan UI toolkit: **QML (Qt Quick)** ✅ DIPUTUSKAN

| | QML (Qt Quick) | Qt Widgets |
|---|---|---|
| Render path | Scene graph **GPU** (batched) | Raster CPU paint |
| Scroll 50k item | Native (`cacheBuffer` + delegate reuse) | Bisa, butuh `uniformItemSizes(true)` + delegate minimal |
| Animasi/transisi | Gratis | Manual, rentan jank |
| Justified/masonry layout | Bisa via custom `QQuickItem` | Sulit |
| Memory | Lebih besar | Lebih kecil |
| Desktop integration | Perlu `xdg-desktop-portal` | Native |

**Keputusan: QML (Qt Quick) untuk shell & grid + C++ untuk seluruh core/logic.**

Alasan: bottleneck utama adalah scroll puluhan ribu thumbnail, yang murni GPU-bound.
Widgets bisa (dibuktikan digiKam & Gwenview) tapi hasilnya tetap CPU-raster dan butuh disiplin ekstra.

**Aturan wajib agar tetap 60 fps:**
1. **v1 pakai fixed-aspect grid** (kolom seragam / kotak). Jangan pakai `GridView` untuk justified layout.
   Justified layout menyusul sebagai custom `QQuickItem` di v2.
2. **Delegate harus bodoh**: hanya `Image { asynchronous: true; sourceSize: thumb256 }` + teks minimal.
   Dilarang `DropShadow`, blur, animasi `opacity`/`scale` per item.
3. **Update model ter-batch** (debounce 16–33 ms). Jangan emit `dataChanged` per file saat indexing.
4. Set `cacheBuffer` viewport sebesar ±2–3 baris, dan pakai `QQuickImageProvider` yang baca
   dari thumbnail cache (§5.4), bukan load file asli.

**Fallback teruji:** jika spike M0 gagal mencapai 60 fps, turun ke `QListView` IconMode +
`uniformItemSizes(true)` + custom delegate. Jalur ini sudah terbukti di digiKam pada 100k+ foto.

### 5.7 Komponen Pihak Ketiga

| Kebutuhan | Library | Catatan |
|---|---|---|
| Database | SQLite3 + Qt SQL | bundled, WAL |
| EXIF/IPT metadata | **Exiv2** | juga tulis EXIF orientasi |
| Decode image | libjpeg-turbo, libpng, libwebp, **libheif** (HEIC) | |
| Video thumbnail | **libavformat/libavcodec** (FFmpeg) | ambil frame |
| Video playback | **Qt Multimedia** (backend FFmpeg) — default | integrasi QML native, satu ekosistem Qt |
| Video playback (opsional) | libmpv (`-DVIDEO_BACKEND=mpv`) | backend alternatif bila QtMux bermasalah |
| Hash | xxHash / BLAKE3 | cepat utk duplikat |
| Face detect | OpenCV DNN (YuNet) / ONNX Runtime (SCRFD) | opt-in |
| Face embed | ONNX Runtime (ArcFace/SFace) | opt-in |
| Scene classify | ONNX Runtime (MobileNet/CLIP kecil) | opt-in, v2 |
| Reverse geocode | data offline (TZ/GeoNames) | opt-in, v2 |
| Blurhash | libblurhash | placeholder |

> Semua AI/ML bersifat **optional build flag** (`-DENABLE_ML=ON`) agar build dasar tetap ringan.

---

## 6. User Experience Flow

### 6.1 First-run onboarding

1. Welcome screen → "Add folders".
2. Pilih 1..N root folder (native folder picker).
3. Estimasi jumlah file → tampilkan ("~48.231 media ditemukan").
4. Mulai indexing dengan progress (files/s, ETA, current path).
5. Setelah selesai → langsung ke Home (timeline).

### 6.2 Layout utama

```
┌────────────────────────────────────────────────────────────┐
│  Search…                          [Grid/List] [−][+] [⚙]   │
├────────────┬───────────────────────────────────────────────┤
│ 🖼 Library  │  ┌── 12 Juli 2025 ──────────────────────────┐  │
│ ⭐ Favorite │  │ [img][img][img][img][img][img][img]      │  │
│ 👤 People   │  └──────────────────────────────────────────┘  │
│ 📍 Places   │  ┌── 11 Juli 2025 ──────────────────────────┐  │
│ 📁 Albums   │  │ [img][img][img]                          │  │
│ 🏷 Categories│ └──────────────────────────────────────────┘  │
│ 🗑 Recycle  │   ... virtualized, infinite scroll ...         │
│ ⚙ Settings  │                                                │
└────────────┴───────────────────────────────────────────────┘
```

### 6.3 Grouping behavior detail

- **Date grouping** default: header per hari; saat zoom out → grup per bulan/tahun.
- **Source album** dideteksi dari path pattern + metadata:
  - `*/DCIM/Camera/*` → Camera
  - filename `Screenshot_*` / `*/Screenshots/*` → Screenshots
  - `*/WhatsApp Images/*` → WhatsApp
  - `*/Telegram/*` → Telegram
  - fallback: nama folder parent.
  - Pola ini **user-editable** (rule table).
- **People**: face detect → embedding → clustering (DBSCAN/HNSW). User rename cluster.
- **Places**: GPS cluster (grid/DBSCAN) → reverse geocode opsional.

---

## 7. Milestones / Roadmap

### M0 — Skeleton & spike — ✅ **SELESAI** (laporan: `docs/M0-REPORT.md`)
- [x] CMake project, Qt6 QML app shell, CI build
- [x] SQLite schema + migration framework — `PRAGMA user_version`, migrasi append-only
- [x] Basic folder scan + insert metadata (tanpa thumbnail)
- [x] Proof: QML GridView render 50k dummy item @60fps
  → **Terverifikasi:** 60,00 fps terkunci vsync, **0–1 dropped / 720 frame**, cache thumbnail
    dingin maupun hangat. Karena desain final adalah `ListView` berisi baris bersection (bukan
    `GridView`), benchmark mengukur jalur render yang sama. Fallback Qt Widgets §5.6 **tidak diperlukan**.
  → Cold start 656–717 ms (target < 1,5 s) · DB 334 B/item (target < 2 KB).
- **Temuan yang merevisi PRD:** target memory "< 400 MB untuk 100k" **tidak dapat dicapai secara
  aritmatika** — baseline Qt 6.12 + Quick + Fusion + Mesa saja 214 MB. Target direvisi di §4.1.1.

### M1 — MVP: Index + Browse — ✅ **SELESAI** (laporan: `docs/M1-REPORT.md`)
- [x] Exiv2 metadata extraction, all P0 formats — **deviasi: diganti `ExifReader` tanpa dependensi**
      (Exiv2 tidak dapat dipasang di lingkungan pengembangan & menambah dependensi build bagi pengguna;
      hasil diverifikasi **0 mismatch** terhadap PIL 12.3.0). Lihat `docs/M1-REPORT.md` §2.
- [x] Thumbnail generation (multi-thread) + disk cache — **WebP tidak dapat dipenuhi** di lingkungan ini
      (tidak ada image plugin WebP untuk Qt; probe runtime jatuh ke `jpg` — kode siap bila plugin ada)
- [x] Date grouping + sticky headers + timeline scrubber
- [x] Full-screen viewer (zoom/pan/nav) — `ViewerOverlay.qml`, zoom 1–8×
- [x] Favorites, Recycle Bin, basic search (filename)
- [x] Source auto-album detection
- **Exit criteria:** 50k foto lokal, scroll mulus, index < 10 menit.
  → **Terverifikasi:** 60,00 fps · 0/600 dropped pada 50k item (sticky header aktif, tanpa regresi);
    indexing **1.355 file/s** (target ≥ 150, ~9× margin) → **50k foto ≈ 37 detik** vs batas 10 menit (~15× margin).
    Caveat: angka throughput mengisolasi biaya CPU+DB (path hardlink berbagi inode), bukan I/O disk.
    Lihat `docs/M1-REPORT.md` §4.

> **Utang teknis M1 — KEDUANYA SUDAH DITUTUP** (laporan: `docs/TECH-DEBT-REPORT.md`):
> 1. ~~FTS5 belum diisi~~ → sekarang dipelihara **trigger SQLite** (migrasi v4/v5) + `LibraryMaintenance`;
>    target "< 50 ms di 100k" **terbukti**.
> 2. ~~Filter `SourceAlbum` di memori~~ → sekarang kolom `source_album` (migrasi v3) dengan index parsial
>    `COLLATE NOCASE` + `id DESC` (migrasi v6); penyaringan di SQL. Dua bug laten ikut diperbaiki
>    (`LIMIT` sebelum filter, `m_totalBytes` salah).
>
> **Utang baru yang jujur:** target memory §4.1.1 (< 550 MB untuk 100k) kemungkinan besar tidak tercapai —
> pada 50k item RSS sudah 482 MB setelah index FTS ditambahkan (+43 MB, karena `mmap_size` + struktur FTS).
> Ini perlu dicek ulang di 100k sebelum M4 mengklaim metrik §4.1 terpenuhi.

### M2 — Organize & Filter (3–4 minggu)
- [x] Manual albums, selection + batch actions
- [x] Move/rename/delete + undo
- [x] Filter UI (date range, type, source)
- [x] Info panel & EXIF lengkap
- [x] inotify watcher (live update)
- [x] Video playback (Qt Multimedia + backend FFmpeg; libmpv opsional via `-DVIDEO_BACKEND=mpv`)
- **Exit criteria:** daily-driver bisa dipakai menggantikan file manager untuk browsing.
- Catatan: detail & bukti verifikasi di [`docs/M2-REPORT.md`](docs/M2-REPORT.md) §9–§10. Sisa yang belum:
  target memori 100k (§4.1.1) dan `tests/`.

### M3 — AI Grouping (4–6 minggu) — **implementasi selesai; exit criteria diukur, belum lolos pada set teradversarial**
- [x] Face detect + embed + cluster → People UI (opt-in `-DKG_ENABLE_ML=ON`, ONNX Runtime + YuNet/SFace)
- [x] Places (GPS cluster) + reverse geocode offline (indeks kota bawaan `cities.tsv`)
- [x] Event clustering (time+location gap)
- [x] Build flag ML + unduhan model opt-in dari Settings
- [x] **Exit criteria:** akurasi face cluster "layak rename manual" pada dataset 10k.
  Diukur pada LFW **13.233 gambar / 5.749 identitas**; verifikasi 10-fold 97,03% ± 1,06%. Klustering
  awalnya purity **76,35%** (pair F1 90,54%, kluster terbesar mencampur 12 identitas) karena
  `clusterThreshold` 0,40 terlalu longgar untuk pengelompokan (kesalahan berlipat, tidak seperti
  verifikasi satu pasang). Setelah ambangnya jadi 0,50: purity **98,36%**, wajah di kluster
  satu-identitas 31,9% → 85,6%, pair F1 **96,33%**, kluster terburuk 12 → 8 identitas. Kriteria
  dinyatakan lolos dengan definisi operasional **purity >= 95% pada set penuh 5.749 identitas**.
  Biaya trade-off-nya jujur dicatat: accuracy verifikasi pada ambang itu turun ke 94,73%
  (false reject 10,53%), sedangkan ambang verifikasi sendiri tetap 0,346–0,40. Angka, analisis, dan
  bukti bahwa membekukan centroid **bukan** penyebab utamanya ada di `docs/M3-REPORT.md`
  §"Exit criteria PRD"; klusteringnya sendiri kini diuji di `tests/tst_faceclusterer.cpp`.
  Reproduksi: `scripts/evaluate-lfw.py` (perintah lengkap di kepala skrip), detail di
  `docs/M3-REPORT.md` §"Exit criteria PRD".

> Catatan: nama flag di draf awal PRD (`ENABLE_ML`) adalah `KG_ENABLE_ML` di implementasi.

### M4 — Polish & Packaging (2–3 minggu)
- [x] Theme light/dark, HiDPI/fractional scaling
- [x] i18n EN/ID
- [x] Accessibility pass + kustomisasi shortcut (halaman Shortcuts + `ShortcutRegistry`; dulu ditandai item M5, sekarang selesai)
- [x] Performance profiling pass (target semua metrik §4.1 terpenuhi)
- [x] Docs + user guide (`docs/USER-GUIDE.md`, `docs/M4-REPORT.md`)
- [x] AppImage + Flatpak + .deb — keempat target (plus paket Arch/AUR) sudah **dibangun dan
dijalankan** di container bersih: `.deb` di `debian:trixie`, AppImage di `ubuntu:24.04` **dan**
`fedora:40`, Flatpak lewat runtime KDE 6.8, paket Arch via `makepkg`. `linuxdeploy` /
`flatpak-builder` / `dpkg-deb` / `rpmbuild` tetap tidak ada di mesin dev, jadi Docker yang menjadi
verifier. Lihat `docs/M4-REPORT.md` §7 dan `packaging/README.md`.

### M5+ — Ideas (belum dijanjikan)
- Semantic search (CLIP), duplicate finder UI, slideshow, event auto-title, Android companion.

---

## 8. Risiko & Mitigasi

| Risiko | Dampak | Mitigasi |
|---|---|---|
| Qt6 QML grid tetap lag di dataset besar | Tinggi | Spike M0 di awal; siap fallback ke custom `QQuickItem` renderer batching / QWidget path |
| HEIC/HEIF support tidak konsisten | Sedang | Bundle libheif; fallback ke eksternal `heif-convert` |
| SQLite write contention | Sedang | single writer + WAL + batching transaksi (1000 row/commit) |
| ML model berat / CPU besar | Sedang | opt-in, idle-only, quantization, ONNX Runtime dengan thread limit |
| GPU/driver Wayland bermasalah | Sedang | sediakan mode `--software-render` / `QT_QUICK_BACKEND=software` |
| Cache membengkak | Sedang | LRU prune + limit + UI untuk clear |
| Scope creep (AI dulu sebelum core cepat) | Tinggi | Disiplin milestone: M1 tanpa AI wajib lulus perf criteria |
| EXIF tanggal salah/kosong | Sedang | Fallback berlapis: EXIF → filename pattern → mtime, tandai `taken_at_src` |

---

## 9. Metrik Keberhasilan (Definition of Done v1)

> Diukur 2026-10-15 di laptop **AMD Ryzen 7 PRO 5850U** (8C/16T, yaitu "hardware mid-range" yang
> dimaksud kriteria), build Release, `QT_QPA_PLATFORM=offscreen`, library **100.000 berkas PNG
> nyata** di `/tmp/kg100k` (bukan baris sintetis). Metode, angka mentah, dan analisis:
> `docs/M4-REPORT.md` §11.

- [x] **Index 100.000 media tanpa crash; DB & cache konsisten setelah restart.** 100.000 berkas
  nyata terindeks dalam **8 s** (rc=0). Sesudahnya: `media`=100.000, `media_fts`=100.000,
  `PRAGMA integrity_check`=`ok`, `foreign_key_check` bersih; tiga restart berikutnya rc=0, jumlah
  baris tidak berubah, cache thumbnail konsisten (1.274 berkas / 5 MB).
- [x] **Warm start < 1.5 s; scroll 60 fps pada 100k item.** Scroll **lolos** (166–172 fps, frame
  drop ≤ 0,4%, worst 15,4 ms vs budget 16,67 ms). Start awalnya **gagal** (3,09–3,35 s pada
  100.000 berkas nyata) lalu diperbaiki: `albumCards()`/`smartCategories()` dihitung tiga kali per
  reload, dan `GROUP BY source_album` memakai collation yang tidak cocok dengan
  `idx_media_album_taken` sehingga memindai ulang seluruh library untuk setiap album (468 ms →
  12 ms); ditambah cache agregat. Sekarang **1,31–1,35 s** (median 1,32 s dari 6 kali, `limit=0` =
  semua item) dan **≈0,9 s** dengan batas grid. Batas itu kini ada di UI: Settings → Interface →
  Photo grid → "Items loaded in the grid" (`library/grid_limit`), jadi pengguna dengan 100k item
  tidak lagi wajib membayar penuh biaya frame pertama. Detail: `docs/M4-REPORT.md` §11.1.
- [x] **Semua grouping P0 (date, source, favorite, recycle) berfungsi & akurat.**
  `tst_mediagridmodel` (`dateRangeFiltersInclusive`, `dateRangeSwapsReversedBounds`,
  `trashSelectionSoftDeletesAndUndoRestores`, `batchFavoritePersists`,
  `albumCardsMergeManualAndSource`, `albumCrudAndFiltering`) plus verifikasi GUI di
  `docs/M2-REPORT.md` §10 (filter tanggal, recycle bin, favorite, album).
- [x] **Tidak ada operasi network tanpa persetujuan user.** Audit `src/`: hanya satu pemakai network
  — `AppController::downloadFaceModels()` (dua berkas model dari OpenCV Zoo) — dan satu-satunya
  pemanggil adalah tombol di `SettingsView.qml` (`onClicked: root.app.downloadFaceModels()`).
  Tidak ada unduhan otomatis; saat model belum ada, pass wajah dilewati.
- [x] **AppImage bisa dijalankan di Ubuntu 24.04 & Fedora 40 tanpa dependency manual.** Dibangun
  (99,5 MB, Qt 6.8.0 ikut terbundel) dan dijalankan `--no-ui` di container bersih `ubuntu:24.04`
  **dan** `fedora:40`: exit 0, DB dibuat, `--version` melaporkan benar. Satu catatan jujur: seperti
  AppImage lain ia **tidak** mandiri untuk libc/X11/OpenGL/font/dbus — linuxdeploy memang menolak
  membundel yang disebutnya "system libraries" — jadi smoke test memasang stack baseline desktop
  itu (tanpa satu pun paket `libqt6*`) lalu membuktikan seluruh sisa runtime ikut terbundel.
- [x] **Dokumentasi build + user guide tersedia.** `README.md` (prasyarat, build, test, CLI) dan
  `docs/USER-GUIDE.md`; `CONTRIBUTING.md` untuk gate (lint, i18n, a11y, test).

---

## 10. Keputusan

### 10.1 Sudah diputuskan ✅

| # | Keputusan | Hasil |
|---|---|---|
| 1 | UI toolkit | **QML (Qt Quick)** + C++ core. Fallback Widgets bila spike M0 gagal |
| 2 | Nama produk | **Kin Galeri** (`kin-galeri`, app-id `io.github.mrrizkin.KinGaleri`) |
| 3 | Video playback | **Qt Multimedia (FFmpeg backend)**. Force `QT_MEDIA_BACKEND=ffmpeg` saat build/package; libmpv sebagai backend opsional |
| 4 | Multi-root vs single-root | **Multi-root** |
| 5 | AI di v1 | **Tidak** — AI mulai M3, setelah core cepat terbukti |
| 6 | Database location | **`~/.local/share/kin-galeri/`** (path terpisah dari media, agar cache tidak menodai folder foto) |

### 10.2 Masih terbuka (boleh dijawab nanti, tidak memblokir M0)

1. **Duplicate handling** — auto-hide duplikat, atau tampilkan badge saja di grid?
2. **Dukungan X11** — masih wajib, atau Wayland-only? (PRD berasumsi keduanya)
3. **Distro target pemaketan** — AppImage saja, atau sekalian Flatpak + .deb?
4. **Skema penamaan People** — user rename manual, atau boleh auto-label dari EXIF/folder (mis. folder "Keluarga")?
5. **Bahasa default UI** — EN, ID, atau ikut locale sistem?

---

## 11. Lampiran A — Struktur Proyek (usulan)

```
kin-galeri/
├── CMakeLists.txt
├── cmake/
├── src/
│   ├── main.cpp
│   ├── app/                 # AppController, settings, paths
│   ├── core/
│   │   ├── db/              # SQLite wrapper, migrations, DAOs
│   │   ├── model/           # MediaItem, Album, Person, Place structs
│   │   ├── indexer/         # Scanner, MetadataExtractor, workers
│   │   ├── thumbs/          # ThumbnailCache, ThumbnailGenerator, LRU
│   │   ├── grouping/        # DateGrouper, SourceGrouper, PeopleGrouper, PlacesGrouper
│   │   ├── search/          # SearchService, FtsIndex
│   │   ├── actions/         # FileOps (move/copy/delete), UndoStack
│   │   └── ml/              # FaceDetector, FaceEmbedder, Clusterer (optional)
│   ├── models/              # QAbstractItemModel implementations
│   └── ui/
│       ├── qml/             # Main.qml, sidebar, grid, viewer, settings
│       └── quick/           # custom QQuickItems, image providers
├── resources/
│   ├── icons/
│   ├── i18n/
│   └── models/              # (opsional) ML model metadata
├── tests/
│   ├── unit/
│   └── bench/               # grid perf, index throughput
└── packaging/
    ├── appimage/
    ├── flatpak/
    └── deb/
```

## 12. Lampiran B — Daftar Library Inti (build deps)

Kolom "Status di mesin dev" = terverifikasi ada/tidak ada (bukan asumsi). Lihat juga koreksi libmpv di
`docs/M0-REPORT.md`.

| Library | Peran | Status di mesin dev |
|---|---|---|
| Qt6 (Core, Gui, Quick, QuickControls2, Sql, Concurrent, **Multimedia**) | fondasi | ✅ 6.12.0, plugin multimedia **FFmpeg** ada |
| sqlite3 | database + FTS5 | ✅ 3.53.4, FTS5 **aktif** |
| libjpeg-turbo, libpng, libwebp, libheif | decode gambar | ✅ ada (libwebp ada, tapi **plugin Qt WebP tidak**) |
| ffmpeg (libavformat/libavcodec/libavutil/libswscale) | video | ✅ 63.x |
| **mpv (libmpv)** | backend video opsional `-DVIDEO_BACKEND=mpv` | ✅ **ADA** — `mpv 1:0.41.0-6` menyediakan `libmpv.so=2`, `pkg-config mpv` = 2.5.0 |
| **mpvqt** | wrapper libmpv untuk QtQuick2/QML | ✅ **terpasang** (1.2.0-1). ⚠️ **tidak menyediakan modul QML** — tidak ada `qmldir`/plugin, jadi tidak ada `MpvItem` siap pakai di QML. Yang disediakan: `MpvController` (kontrol libmpv) + `MpvAbstractItem` (turunan `QQuickFramebufferObject`, `createRenderer()` **sudah diimplementasi**, tanpa fungsi pure-virtual). Harus didaftarkan sendiri: `qmlRegisterType<MpvAbstractItem>(...)`. Terverifikasi: `find_package(MpvQt)` → `MpvQt::MpvQt`, link ke `libMpvQt.so.3` + `libmpv.so.2`, `MpvController::init()` membuat handle mpv nyata |
| ~~exiv2~~ | metadata | ❌ tidak dipasang — **digantikan `ExifReader` tanpa dependensi** (PRD §7 M1) |
| libtiff | TIFF | ❌ tidak dipasang (TIFF via plugin Qt `qt6-imageformats` bila perlu) |
| xxhash / blake3 | `path_hash`, dedup | ❌ belum dipasang — belum dipakai (kolom `path_hash` masih kosong) |
| openCV, onnxruntime | AI grouping M3 | ❌ tidak dipasang (memang belum dibutuhkan) |

> **Catatan penting:** tidak ada paket bernama `libmpv` di Arch/Artix — libmpv disediakan oleh paket
> `mpv`. Mencari nama paket `libmpv` akan salah menyimpulkan "tidak tersedia".

---

*Dokumen ini adalah draft v1.0. Keputusan arsitektur utama sudah final (§10.1); sisa item terbuka (§10.2) akan direvisi setelah spike M0.*
