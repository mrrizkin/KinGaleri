# M3 — Smart grouping (Places, Events, Smart, People)

Status: **implementasi selesai dan terverifikasi ulang** (lihat §Verifikasi). Build default tetap tanpa
dependensi baru; backend wajah opsional di balik `-DKG_ENABLE_ML=ON`. Exit criteria PRD soal akurasi
kluster wajah pada dataset 10k **sudah diukur** pada LFW 13.233 gambar, dan hasilnya belum lolos pada
set teradversarial itu - angka lengkapnya di §"Exit criteria PRD".

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

### Exit criteria PRD: diukur pada LFW 13.233 gambar (2026-10-14)

Kriteria "akurasi face cluster 'layak rename manual' pada dataset 10k" akhirnya dijalankan pada
**LFW** (13.233 gambar, 5.749 identitas, arsip resmi + `pairs.txt`) lewat `scripts/evaluate-lfw.py`.
Perintah reproduksinya ada di kepala skrip itu; model YuNet/SFace yang dipakai adalah yang diunduh
Settings, bukan model lain.

Dua pertanyaan dijawab terpisah, karena LFW bukan bentuk pustaka pribadi: distribusinya ekstrem
(median 1 gambar per identitas), sedangkan pustaka pribadi punya sedikit orang dengan ratusan foto.

**1. Verifikasi (protokol resmi `pairs.txt`, 6.000 pasang, threshold dilatih pada 9 fold dan diuji
pada fold ke-10):**

| Metrik | Hasil |
|---|---|
| 10-fold cross-validated accuracy | **97,03% ± 1,06%** |
| Accuracy pada threshold pipeline (`cos >= 0,40`) | 96,77% (false reject 6,47%, false accept 0,00%) |
| Threshold terbaik (tuned on test, jadi batas atas optimis) | 97,08% (`cos >= 0,346`) |

Semua 6.000 pasang terpakai: deteksi wajah 100% (13.233/13.233 gambar LFW).

**2. Klustering (yang sebenarnya ditanya kriteria):**

| Metrik | Hasil |
|---|---|
| Identitas sebenarnya | 5.749 |
| Kluster yang dibuat | 3.530 |
| Purity (mayoritas identitas per kluster) | **76,35%** |
| Wajah di kluster satu-identitas | 31,9% |
| Pairwise precision / recall / F1 | 86,69% / 94,76% / **90,54%** |
| Singleton (tidak ada gunanya di-rename) | 51,3% kluster |

Kluster terbesar = **527 wajah berisi 12 identitas** (George W. Bush 515 + Michael Piuze, dll), lalu
236 wajah / 3 identitas (Colin Powell + Joe Lieberman + George W. Bush) dan 143 wajah / 3 identitas
(Tony Blair + ...). Pola kegagalannya spesifik dan berulang: wajah "rata-rata" laki-laki berjas
mulai menarik identitas lain ke centroid-nya. Itu **pergeseran centroid** (running mean pada
`addToCluster`), bukan masalah mutu embedding.

**3. Efek bentuk pustaka.** `scripts/evaluate-lfw.py sweep` menjalankan ulang algoritma yang sama
pada subset identitas dengan >= K gambar, sehingga terlihat kapan algoritma ini memadai:

| K (min gambar/identitas) | identitas | gambar | kluster | purity | pair F1 | wajah di kluster murni | singleton |
|---|---|---|---|---|---|---|---|
| 1 (seluruh LFW) | 5.749 | 13.233 | 3.530 | 76,4% | 90,5% | 31,9% | 51,3% |
| 2 | 1.680 | 9.164 | 1.509 | 88,3% | 93,5% | 49,5% | 14,7% |
| 3 | 901 | 7.606 | 938 | 93,2% | 95,0% | 62,4% | 14,5% |
| 5 | 423 | 5.985 | 515 | 96,4% | 95,7% | 68,7% | 20,4% |
| 10 | 158 | 4.324 | 234 | 98,4% | 96,2% | 69,4% | 31,2% |
| 20 | 62 | 3.023 | 119 | 99,7% | 96,8% | 68,4% | 47,1% |

**Putusan (jujur):** kotak exit criteria di `PRD.md` **tetap kosong**. Pada set teradversarial
jawabannya *tidak layak*: purity 76,35% berarti sekitar seperempat wajah duduk di kluster yang
mayoritasnya benar tetapi bercampur identitas lain, dan tidak ada cara manual yang masuk akal untuk
membetulkan satu kluster berisi 527 wajah dari 12 orang. Mutu embedding-nya sendiri tidak jadi
masalah (97,03% verifikasi) - yang perlu diperbaiki klusteringnya, bukan modelnya.

Definisi operasional yang dipakai untuk menyatakan kriteria ini lolos: **purity >= 95% pada set
5.749 identitas**. Dengan algoritma sekarang itu baru tercapai pada pustaka dengan <= ~400
identitas, yang justru kasus nyata aplikasi ini - tetapi bukan yang diminta kriteria. Utang teknis
dan rencana perbaikannya ada di `docs/TECH-DEBT-REPORT.md`.

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
sekali. Test: `tst_mediagridmodel::taggingFiltersAndIndexesFts`, `tst_imageloader` (11/11 lulus).
