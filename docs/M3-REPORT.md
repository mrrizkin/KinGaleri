# M3 — Smart grouping (Places, Events, Smart, People)

Status: **implementasi selesai dan terverifikasi ulang** (lihat §Verifikasi). Build default tetap tanpa
dependensi baru; backend wajah opsional di balik `-DKG_ENABLE_ML=ON`. Satu hal yang **belum**
dibuktikan: exit criteria PRD soal akurasi kluster wajah pada dataset 10k.

## Ruang lingkup

| Fitur | Butuh model? | Status |
|---|---|---|
| **Places** — cluster GPS + nama kota offline | tidak | selesai |
| **Events** — celah waktu + lompatan lokasi | tidak | selesai |
| **Smart** — koleksi heuristik | tidak | selesai |
| **People** — deteksi + embedding + clustering wajah | ya (YuNet + SFace) | selesai, opt-in |

Semua dihitung di perangkat dan disimpan di `library.db`; tidak ada gambar,
embedding, atau koordinat yang dikirim keluar kecuali unduhan model yang
dipicu pengguna.

## Arsitektur

### Skema (migrasi v7 `smart_grouping`)

- `places` (sudah ada sejak v1) ditambah `country`, `media_count`, `cover_media_id`.
- `events` + `media_events` baru; nama event dibiarkan kosong dan diformat UI dari
  `start_at`/`end_at`, agar tidak menyimpan teks ber-locale di DB.
- `faces` ditambah `model_marker`; `people` ditambah `media_count`, `cover_media_id`,
  `is_hidden`.
- `face_scans(media_id, model_marker, face_count, analyzed_at)` mencatat bahwa sebuah
  gambar **sudah diperiksa** walau nol wajah ditemukan, sehingga pass berikutnya tidak
  mengulang foto yang sama.
- Index parsial `idx_media_gps` supaya rebuild Places hanya memindai baris ber-GPS.

### Inti (tanpa model)

- `core/grouping/GeoClusterer` — union-find pada grid derajat, keputusan jarak tetap
  haversine (`mergeMeters` default 500 m), lalu diberi nama dari
  `resources/data/cities.tsv` (GeoNames `cities15000`, populasi ≥ 100k + kota ID ≥ 40k,
  6.431 baris, 240 KB, CC BY 4.0). Tanpa kota dalam 50 km → label koordinat.
- `core/grouping/EventClusterer` — sortir waktu, potong saat celah > 6 jam atau lompatan
  GPS > 20 km. Titik tanpa GPS tidak menghapus anchor lokasi.
- `core/grouping/GroupingService` — rebuild penuh `places`+`media_places` lalu
  `events`+`media_events`, keduanya transaksional dan idempoten.

### Backend wajah (opsional)

- `core/ml/FaceEngine` — ONNX Runtime C++ API, model dimuat malas.
  - **YuNet** 640×640: letterbox (bukan stretch), input **BGR** [0,255], decode persis
    seperti `cv::FaceDetectorYN` (cls/obj sudah [0,1] tanpa sigmoid, `score=sqrt(cls*obj)`,
    `box=(cell+delta)*stride`, `size=exp(delta)*stride`), lalu NMS.
  - **SFace** 112×112: alignment similarity 5 titik ke referensi ArcFace (bentuk tertutup
    via bilangan kompleks, tanpa SVD), sampling bilinear manual, input **RGB** [0,255],
    output 128-d dinormalisasi L2. (Detektor BGR dan embedder RGB memang berbeda; ini
    mengikuti implementasi referensi OpenCV.)
- `core/ml/FacePipeline` — inkremental per `media` (gambar, non-sintetis, non-terhapus),
  deteksi+embed, commit per gambar, lalu clustering global greedy berbasis centroid.
  Threshold cosine default 0,40. Nama orang yang sudah diubah pengguna dibawa lintas
  rebuild dengan mencocokkan embedding penutup orang lama ke cluster baru terdekat.

### UI

- Sidebar **COLLECTIONS**: People / Places / Events / Smart (selalu tampil).
- `CollectionsView.qml` — satu komponen untuk empat halaman: grid kartu cover + jumlah,
  tombol Rebuild untuk Places/Events, empty state, dan menu klik-kanan (rename/merge)
  untuk People.
- `SettingsView` halaman **Smart**: master switch, pilihan Places/Events/Faces, unduh
  model, auto-analyze, idle-only, pause-on-battery, slider thread (1–8), status, Rebuild.
- `MediaGridModel` menambah filter `placeId`/`personId`/`eventId`/`smartCategory` dan
  properti koleksi `places`/`events`/`people`/`smartCategories`.

## Verifikasi

### Verifikasi ulang (mandiri, reproduksi penuh)

Klaim di bawah dijalankan ulang dari nol lewat build ML, bukan sekadar membaca artefak lama:

```bash
# 4 potret di /tmp/faces/{merkel,michelle,obama1,obama2}.jpg + model di /tmp/kgmldata/kin-galeri/models
XDG_DATA_HOME=/tmp/kgmldata ./build-ml/kin-galeri --no-ui --no-home-scan \
    --db /tmp/kgv/verify.db --cache /tmp/kgv/verifycache --index /tmp/faces
```

Hasil di DB segar: **4 wajah → 3 orang**, 1 tempat, 4 event.

| Pemeriksaan | Hasil |
|---|---|
| `people` | #1 `media_count=2` (obama1 2009 + obama2 2012 — kluster lintas tanggal), #2 michelle, #3 merkel |
| `places` | `Köln, DE` (`country=DE`) — GPS EXIF di-resolve indeks kota offline, bukan dari tag teks |
| `events` | 4 event, `start_at`/`end_at` milidetik dari EXIF, satu media per event pada set ini |
| UI People | 3 kartu cover; kartu Obama berlabel **2 item(s)** |
| UI Places | 1 kartu "Köln, DE" |
| UI Events | 4 kartu berlabel tanggal (`4 Jun 2019`, `12 Feb 2018`, `6 Dec 2012`, `13 Jan 2009`) |
| UI Smart | 5 koleksi heuristik tampil (Screenshots/Videos/Panoramas/Large files/Recently taken) |

### Catatan sebelumnya (tetap berlaku)

- `tests/tst_grouping.cpp` (8 test): merge/pisah cluster, haversine, lookup kota Jakarta, pemisahan
  event karena waktu dan lokasi, urutan event, dan penulisan Places/Events ke DB termasuk idempotensi
  rebuild.
- Halaman Places menampilkan "Soreang · 2 item", Events menampilkan 5 event dengan label tanggal +
  subjudul tempat, People menampilkan kartu cover yang benar (dataset berbeda dari reproduksi di atas).

### Yang belum dibuktikan

- **Exit criteria PRD**: "akurasi face cluster layak rename manual pada dataset 10k". Semua pengujian
  wajah di atas memakai 4 potret. Belum ada run pada dataset 10k, jadi akurasi pada skala itu belum
  bisa diklaim. Ini tercatat sebagai kotak yang sengaja tetap kosong di `PRD.md` bagian M3.

## Build dengan backend wajah

```bash
curl -L -o ort.tgz \
  https://github.com/microsoft/onnxruntime/releases/download/v1.20.1/onnxruntime-linux-x64-1.20.1.tgz
tar xzf ort.tgz
cmake -S . -B build -G Ninja -DKG_ENABLE_ML=ON \
  -DKG_ORT_ROOT="$PWD/onnxruntime-linux-x64-1.20.1"
cmake --build build
```

Model (~40 MB) diunduh dari **Settings → Smart → Download model** ke
`<data dir>/models/`.

## Utang / batas

- Clustering greedy; belum ada split per-wajah (merge sudah ada).
- Rebuild wajah penuh hanya saat model/marker berubah atau dipicu manual; foto baru
  diproses inkremental.
- Deteksi hanya pada gambar, bukan frame video.
- Model int8 belum disediakan; fp32 dipakai karena totalnya sudah kecil (~40 MB).

## Addendum: tag + fallback HEIC (penutup utang)

Dua item yang sebelumnya tercatat sebagai utang ditutup setelah M3:

- **Tag** — migrasi v8 (`fts_tags`) menambah index `media_tags` dan 8 trigger sehingga kolom
  `tags` di `media_fts` mengikuti `media_tags` (dan diperbarui saat tag dibuat, di-rename, atau
  dihapus). `MediaGridModel` mendapat filter `tag`, daftar `tags`, serta operasi
  `addTagToMedia` / `removeTagFromMedia` / `renameTag` / `deleteTag`; sidebar punya halaman
  **Tag**. Nama tag kini ikut `MATCH` pencarian.
- **HEIC/HEIF** — `ImageLoader::decodeHeif` menjalankan `heif-convert` on-demand dan dipakai
  `ThumbnailService` + `FullImageProvider` bila Qt tidak bisa membaca file. Tetap opsional:
  tanpa tool itu perilakunya placeholder seperti sebelumnya.

`LibraryMaintenance::kFtsIndexVersion` naik 2 → 3 agar library lama membangun ulang indeks
sekali. Test: `tst_mediagridmodel::taggingFiltersAndIndexesFts`, `tst_imageloader` (10/10 lulus).
