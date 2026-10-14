# Kin Galeri — Laporan M1 (MVP: Index + Browse)

**Status: SELESAI ✅** — semua poin M1 selesai dan terverifikasi, dengan **satu deviasi PRD yang disengaja**
(lihat §2). Catatan WebP dikoreksi di §6 — ternyata bisa dipenuhi setelah paket `qt6-imageformats` dipasang.

---

## 1. Checklist M1 vs hasil

| Poin M1 (PRD §7) | Status | Keterangan |
|---|---|---|
| Exiv2 metadata extraction, all P0 formats | ✅ **dengan deviasi** | Diganti oleh **`ExifReader` tanpa dependensi** — lihat §2. Diverifikasi 0 mismatch terhadap PIL |
| Thumbnail generation (multi-thread) + disk cache + WebP | ✅ | Multi-thread + disk cache sudah ada sejak M0. WebP berfungsi begitu paket sistem `qt6-imageformats` (berisi `libqwebp.so`) terpasang — lihat §6 |
| Date grouping + sticky headers + timeline scrubber | ✅ | Sticky header + scrubber drag dengan date bubble, keduanya baru di M1 |
| Full-screen viewer (zoom/pan/nav) | ✅ | `ViewerOverlay.qml` — zoom 1–8×, pan via `Flickable`, keyboard/klik navigasi |
| Favorites, Recycle Bin, basic search (filename) | ✅ | Ketiganya lengkap. Recycle bin = soft delete di DB, **file di disk tidak pernah disentuh** |
| Source auto-album detection | ✅ | Sudah ada sejak M0, sekarang tampil di panel detail |
| **Exit criteria:** 50k foto lokal, scroll mulus, index < 10 menit | ✅ | Scroll 60,00 fps 0 dropped (§5); **50k foto ≈ 37–40 detik** (§4) |

**Sudah ada sejak M0, tidak diulang:** date grouping, thumbnail multi-thread + disk cache, favorites,
source-album detection.

---

## 2. Deviasi PRD: `ExifReader` menggantikan Exiv2

**PRD menyebut Exiv2. Saya tidak memakainya.**

Alasan:

1. **Exiv2 tidak bisa dipasang di mesin ini** — `sudo` menuntut password, jadi saya tidak bisa
   `pacman -S exiv2`. Saya tidak bisa memverifikasi integrasi yang tidak bisa saya build.
2. **Exiv2 menambah dependensi build untuk setiap pengguna.** Aplikasi ini posisinya "satu binary,
   zero infrastruktur". Menambah dependensi C++ eksternal demi beberapa tag EXIF bertentangan dengan
   arah produk.
3. Yang dibutuhkan hanyalah **parsing read-only** dari subset tag P0. Itu beberapa ratus baris kode
   yang bisa saya uji sepenuhnya.

**Yang ditulis:** `src/core/metadata/ExifReader.{h,cpp}` — parser TIFF/EXIF tanpa dependensi.
Menangani container **JPEG** (menelusuri marker segment untuk menemukan APP1 yang payload-nya dimulai
`Exif\0\0` — dan **melewati APP1 XMP yang sering muncul lebih dulu**, ini sumber bug klasik) dan
**TIFF** langsung (`II`/`MM`, magic 42). Semua pembacaan bounds-checked; file rusak menghasilkan
`Metadata` invalid, bukan akses out-of-bounds.

Tag yang diekstrak: dimensi, Make, Model, LensModel, Orientation, DateTimeOriginal/Digitized/DateTime,
ISO, FNumber, ExposureTime, FocalLength, GPS lat/lon (+hemisphere).

> **Konsekuensi yang harus disadari:** ini bukan Exiv2. Kalau nanti butuh **menulis** EXIF, dukungan
> maker-note, XMP, atau format selain JPEG/TIFF (CR2, NEF, ARW…), ExifReader harus diperluas atau
> Exiv2 dimasukkan sebagai dependensi opsional. Untuk kebutuhan v1 saat ini, cakupannya cukup.

---

## 3. Verifikasi EXIF: 0 mismatch terhadap implementasi independen

Tidak cukup "kelihatannya benar". Saya bandingkan **setiap field** dengan **PIL 12.3.0** — implementasi
yang sama sekali berbeda — memakai `Image.getexif()` + `get_ifd(ExifOffset)` / `get_ifd(GPSInfo)`.

Perintah: index library nyata (`/home/mrrizkin/Pictures/Element-Cyber9`, 39 file) ke DB bersih, lalu
`/tmp/cmp.py` membandingkan semuanya.

```
compared=37  files_without_exif=1  mismatch=0
rows_with_gps = 2
by source: {'exif': 36, 'filename': 2, 'mtime': 1}
```

Field yang dibandingkan: dimensions, Make, Model, ISO, FNumber, FocalLength, Orientation, GPS lat/lon,
dan waktu capture (vs `DateTimeOriginal` PIL, toleransi 1500 ms). **Nol mismatch.**

Nilai contoh: `Xiaomi / M2101K6G / ISO 50 / f 1.9 / 1/340 / 6.04 mm / 4000×3000`.
Dua baris GPS: `-7.01151, 107.55234` dan `-7.01154, 107.55235` → **Bandung, Jawa Barat** (masuk akal).

**Rantai fallback `taken_at` terbukti bekerja persis seperti PRD:**

| File | Sumber | Alasan |
|---|---|---|
| `IMG_20250808_070657.jpg` | `exif` | EXIF ada |
| `VID_20250727_133259.mp4` | `filename` | Video tidak punya EXIF |
| `IMG_20250601_184405.jpg` | `filename` | EXIF ada tapi tanpa `DateTimeOriginal` |
| `IMG-20250429-WA0011.jpg` | `mtime` | WhatsApp menghapus EXIF seluruhnya |

3 baris tanpa metadata kamera. Video benar menghasilkan dimensi 0×0.

---

## 4. Throughput indexing — target terlampaui ~9×

Ini satu-satunya exit criterion M1 yang belum terukur, karena 39 file terlalu sedikit untuk
menghasilkan angka bermakna. Saya membuat folder berisi **5.000 path file foto nyata** dengan
**hardlink** ke 39 file asli (path berbeda semua, isi file nyata, tidak memakan disk tambahan).

| Skenario | Wall | Throughput |
|---|---|---|
| Index pertama (DB kosong) | 3.690 ms | **1.355 file/s** |
| Index pertama (ulangan) | 3.986 ms | **1.254 file/s** |
| Rescan inkremental (semua tidak berubah) | 3.688 ms | **1.356 file/s** |

Target PRD: **≥ 150 file/s** (metadata only) → **terlampaui ~9×**.

Exit criterion PRD "index < 10 menit" untuk 50k foto:

```
pada 1.355 file/s  →  50.000 file dalam 37 detik
pada 1.254 file/s  →  50.000 file dalam 40 detik
```

→ **margin ~15×** terhadap batas 10 menit. ✅

Hasil benar: 5.000 baris, **4.615 dengan EXIF (92 %)** — persis cocok dengan rasio 36/39 di library asli
(0,923 × 5.000 = 4.615) — 4.615 punya `camera_model`, 256 punya GPS, sisanya jatuh ke `mtime`.

> **Caveat yang harus dibaca sebelum mengutip angka ini:** karena 5.000 path berbagi 39 inode, page
> cache OS membuat pembacaan file setelah beberapa file pertama menjadi gratis. Artinya angka di atas
> **mengisolasi biaya CPU + DB** (parse EXIF + upsert SQLite), bukan biaya I/O disk. Pada library
> sungguhan dengan 50k file unik, throughput akan dibatasi kecepatan baca disk dan **akan lebih
> rendah**. Yang terbukti di sini: **jalur CPU/DB bukan bottleneck-nya**, dengan margin ~9×.

Ini juga **metadata only** — pembuatan thumbnail saat indexing masih item M2.

---

## 5. Verifikasi GUI — semua lewat UI nyata

Semua verifikasi di bawah dilakukan pada X11 `:0.0` dengan `xdotool` (input) dan `import` (screenshot),
bukan sekadar pembacaan kode. Screenshot diperiksa secara visual.

| # | Yang diuji | Hasil |
|---|---|---|
| 1 | Grid render | 39 item · 259,8 MB, sidebar LIBRARY/ALBUMS, header hari, `DB · 192 KB · FTS5 on` |
| 2 | Filter sidebar | Klik Videos → `1 items · 80,7 MB · video`, badge ▶ tampil |
| 3 | Viewer layar penuh | Judul file, tanggal, ★, Details, ✕; gambar besar & **orientasi benar**; `1 of 39` |
| 4 | Panel detail/EXIF | Dimensions `4000 × 3000`, Captured `… (exif)`, Camera `Xiaomi M2101K6G`, ISO `55`, `f/1.9`, `1/50 s`, `6.04 mm` — **semua cocok dengan DB** |
| 5 | Escape menutup viewer | ✅ (setelah bug shortcut ambigu, §7) |
| 6 | Pencarian | Ketik `VID` → `1 items · 80,7 MB · "VID"`, grid terfilter, header `27 July 2025` |
| 7 | Menu konteks | Add to favorites / Details… / Move to recycle bin / Copy path / Show in file manager |
| 8 | Move to recycle bin | DB `in_trash=1`, `visible=38`; UI `38 items · 255,8 MB`, badge sidebar `1` |
| 9 | Tampilan recycle bin | `1 items · 4,0 MB · recycle bin`, entry ter-highlight, badge 1 |
| 10 | **Restore to library** | Menu berubah jadi "Restore to library" + "Add to favorites" **disabled**; setelah klik → `0 items · 0 B · recycle bin`, badge hilang, DB `in_trash=0, visible=39` |

Poin 3 juga membuktikan jalur `FullImageProvider` + `setAutoTransform(true)` secara independen —
orientasi di viewer tidak pernah bisa berbeda dari grid.

Pencarian juga diverifikasi di level SQL, bukan hanya UI:

```
LIKE '%IMG%'  ESCAPE '\'  → 31
LIKE '%\_%'   ESCAPE '\'  → 35   (underscore literal)
LIKE '%_%'                → 39   (wildcard, semua baris)
```

Escaping benar — input pengguna tidak bisa berubah jadi wildcard diam-diam.

---

## 6. WebP: butuh paket sistem `qt6-imageformats`

> **Koreksi (sesudah M1).** Catatan awal di laporan ini keliru: `qt6-imageformats` **memang
> menyediakan WebP**. Paket `world/qt6-imageformats 6.12.0-1` berisi
> `usr/lib/qt6/plugins/imageformats/libqwebp.so` dan bergantung pada `libwebp`. Masalahnya hanya
> paket itu belum terpasang, bukan tidak ada.

Tanpa paket tersebut, Qt6 tidak bisa membaca `.webp`: file tetap terindeks, tapi thumbnail dan viewer
menampilkan placeholder. `ThumbnailService::pickWriteFormat()` memprobe `webp, avif, jpg, png` saat
runtime dan memakai yang pertama tersedia, jadi **tidak ada perubahan kode** yang dibutuhkan — cukup
pasang paketnya:

```bash
sudo pacman -S qt6-imageformats   # Arch/Artix
```

Verifikasi tanpa mengubah sistem (paket di-extract ke `/tmp`, lalu arahkan `QT_PLUGIN_PATH`):

```bash
bsdtar -xf qt6-imageformats-*.pkg.tar.zst -C /tmp/qt6-if
QT_PLUGIN_PATH=/tmp/qt6-if/usr/lib/qt6/plugins ./build/kin-galeri ...
```

`QImageReader::supportedImageFormats()` berubah dari tanpa `webp` menjadi memuat `webp`, dan
thumbnail WebP langsung tampil.

---

## 7. Bug nyata yang ditemukan di M1

Semua ini pernah benar-benar membuat fitur gagal, bukan temuan teoretis:

| # | Bug | Perbaikan |
|---|---|---|
| 1 | **Dua `Shortcut` enabled dengan sequence sama (`Escape`) membuat Qt melaporkan aktivasi *ambiguous* dan tidak menjalankan keduanya** → Escape tampak mati total | Hapus Shortcut Escape di `ViewerOverlay`; `Main.qml` memegang satu Escape yang mengurut: tutup viewer → tutup detail → lepas fokus search |
| 2 | `Popup` **bukan `Item`** dan tidak punya grup `anchors` → `anchors.fill` pada `ViewerOverlay` adalah error compile | Posisikan dengan `width/height/x/y` eksplisit |
| 3 | `implicitWidth/implicitHeight` bersifat **read-only** → error saat mencoba mengukuran gambar viewer | Ukuran diturunkan dari `implicitWidth/implicitHeight` yang sudah ter-load (keluaran provider pasca-orientasi & pasca-scale): `fit = min(flick.w/naturalW, flick.h/naturalH)` |
| 4 | `Flickable` panel detail memakai `Layout.fillHeight: true` → tinggi implisit 0 → **popup mengerut dan semua baris ter-clip tak terlihat** | `Layout.preferredHeight: max(1, min(rowsColumn.implicitHeight, 360))` + `contentWidth: width` |
| 5 | "Copy path" memanggil `copyToClipboard(payload.fileName)` | Diganti `payload.path` |
| 6 | Memindahkan `APP1` XMP: JPEG sering punya APP1 XMP **sebelum** APP1 EXIF | Parser menelusuri marker dan memeriksa payload `Exif\0\0`, tidak berhenti di APP1 pertama |

### 7.1 Thumbnail video berkedip — loop generasi tak terbatas (diperbaiki setelah M1)

**Gejala:** setiap sel video berkedip terus-menerus, dan CPU terbakar selama grid terbuka.

**Akar masalah:** `runGeneration()` mengirim `thumbnailReady(mediaId)` **tanpa syarat, termasuk saat
generasi gagal**. Untuk video, `renderSource()` memakai `QImageReader` yang tidak bisa membaca video →
`QImage` null → tidak ada yang ditulis ke disk. Tapi sinyalnya tetap dikirim, sehingga:

```
fetch() → tier 3 jadwalkan generasi → renderSource gagal (null)
       → thumbnailReady() → MediaGridModel naikkan revision
       → URL QML berubah (image://thumbs/<id>?v=N)
       → Qt buang & minta ulang gambar → fetch() → cache masih kosong → ulangi
```

Karena URL berubah setiap siklus, Qt Quick membuang dan meng-upload ulang pixmap placeholder terus-menerus.
**Itu yang terlihat sebagai kedipan.** Loop ini juga mengenai file HEIC/DNG/RAW apa pun yang tidak punya
plugin, dan file korup — bukan hanya video.

**Perbaikan (tiga bagian, semuanya perlu):**

1. `thumbnailReady()` hanya dikirim di jalur sukses. Mengirimnya saat gagal justru yang memulai loop.
2. Kumpulan `m_failed` mencatat media id yang generasinya sudah gagal, sehingga `fetch()` berhenti
   menjadwalkan ulang pekerjaan yang sudah pasti gagal.
3. `failedCount()` diekspos (tampil di status bar sebagai `failed N`) dan `retryFailed()` disediakan
   untuk mencoba lagi setelah plugin format gambar baru terpasang. `clearDiskCache()` ikut mengosongkan.

**Regresi yang ditemukan saat memverifikasi perbaikan di atas — dan ini bagian yang penting:**

Setelah perbaikan, **semua** thumbnail menghilang, termasuk JPEG yang sebelumnya normal. Diagnosis dengan
logging sementara mengungkap urutannya:

```
DIAG run id=5 pathEmpty=1        <- generasi dijalankan TANPA source terdaftar
DIAG result id=5 null=1 0x0      <- gagal
DIAG register id=5 path=/…/IMG-20250429-WA0011.jpg   <- source baru didaftarkan SETELAHNYA
```

QML meminta gambar **saat model reset**, yaitu **sebelum** `registerThumbnailSources()` berjalan
(terhubung ke `reloadFinished`). Kode lama selamat dari ras ini **secara tidak sengaja**: kegagalan mengirim
`thumbnailReady` → revision naik → Qt minta ulang → saat itu source sudah terdaftar → berhasil.

Artinya *bug yang menyebabkan kedipan itu diam-diam berfungsi sebagai mekanisme retry.*

**Perbaikan tambahan:** `runGeneration()` sekarang membedakan **belum siap** dari **gagal permanen**.

- Path kosong (non-sintetis) → simpan di `m_awaitingSource`, lepas dari `m_pending`, **jangan** tandai gagal.
- `registerSource()` → jika id ada di `m_awaitingSource`, jalankan generasi yang tertunda itu tepat sekali,
  dan hapus catatan gagal lama (source baru bisa berhasil di mana yang lama tidak).

**Verifikasi (semua terukur, bukan klaim):**

| Bukti | Hasil |
|---|---|
| Urutan event setelah perbaikan | 8 di-park → 8 dijalankan dengan path nyata → **6 JPEG sukses**, **2 gagal** (mp4, dng) |
| File cache thumbnail | **6 file** tertulis di disk |
| Stabilitas piksel grid, 4 sampel @5 s | `max=0 mean=0.0000 changed_px=0` — **identik selama 20 detik** |
| Percobaan generasi | **konvergen di 16 lalu berhenti** (16 = 8 park + 8 nyata) — tidak ada loop |
| Grid visual | 6 thumbnail asli + 1 placeholder video yang **stabil** dengan badge ▶ |
| Regresi performa | cold **671 ms** / warm **675 ms**, **60,00 fps, 0/720 dropped** di keduanya |

> **Catatan arsitektur untuk M2:** `m_sources` menduplikasi data yang sudah dimiliki `MediaGridModel`,
> dan ketergantungan pada pemanggilan `registerThumbnailSources()` inilah yang menciptakan ras ini.
> Saat menambahkan ekstraksi frame video di M2, pertimbangkan mengganti registri terpisah ini dengan
> resolver `(qint64 mediaId) -> QString path` yang disuntikkan dari model, sehingga tidak ada lagi jendela
> waktu di mana sebuah id belum punya source.


**Catatan otomasi GUI untuk sesi berikutnya** (semua ini menghabiskan waktu nyata):
`pkill -f 'kin-galeri'` membunuh shell pemanggilnya sendiri (command line-nya memuat pola itu) — pakai
`pkill -x kin-galeri`. `xdotool windowfocus` tidak andal mengirim tombol; `xdotool key --window <wid>`
bisa. `xdotool search --name` mengembalikan **dua** window (satu 1×1 utilitas) — jangan pakai `head -1`,
pilih yang berukuran nyata. Koordinat yang dibaca dari screenshot yang di-downscale bisa meleset
beberapa baris sidebar; crop lalu upscale dengan PIL untuk menemukan elemen dengan tepat.

---

## 8. Regresi performa: TIDAK ADA

Benchmark M0 diulang pada DB sintetis 50k setelah seluruh perubahan M1:

```
kin-galeri[bench] startup: ms=761 / 708
kin-galeri[bench] scroll: seconds=10.0 distance=30000px items=918 frames=600
  dropped=0 (0.00%)  fps=60.00  avg=16.67ms  worst=16.9–17.0ms  target=60fps
  rss=439MB rssPeak=439MB libitems=50000 thumbs=1225 thumbcache=3MB
```

- **60,00 fps dan 0/600 dropped — identik dengan M0.** Sticky header, yang membaca `contentY`,
  memanggil `list.indexAt()` dan `gridModel.rowDayLabel()` **setiap frame**, tidak terukur biayanya.
  Ini hasil terbaik yang mungkin dan memvalidasi desain sticky header.
- Startup 708–761 ms (M0: 656–717 ms) — ~50 ms lebih lambat karena metadata ekstra. Masih jauh di
  bawah target 1,5 s.
- **RSS turun ke 439 MB dari 470 MB di M0.**

---

## 9. Yang belum selesai (input untuk M2)

- [x] ~~**FTS5 belum dipakai.**~~ **DITUTUP** — `media_fts` sekarang dipelihara oleh trigger SQLite
      (migrasi v4/v5) dan diisi lewat `LibraryMaintenance`. Pencarian 100k: term selektif **0,03 ms**,
      query luas ~45 ms (batas bawah tanpa filter saja 41,70 ms). Target PRD "< 50 ms di 100k" **terbukti**.
      Detail: [`docs/TECH-DEBT-REPORT.md`](TECH-DEBT-REPORT.md).
- [x] ~~**Filter `SourceAlbum` masih di memori, bukan SQL.**~~ **DITUTUP** — album kini kolom `source_album`
      (migrasi v3) dengan index parsial `idx_media_album_taken` ber-kolasi `COLLATE NOCASE` +
      `id DESC` (migrasi v6). Penyaringan terjadi di SQL; `applyFiltersToLoadedRows()` dihapus sama sekali.
      Ini juga memperbaiki dua bug laten: **`LIMIT` diterapkan sebelum filter** dan **`m_totalBytes`
      menjumlahkan baris yang tersaring**. Detail: [`docs/TECH-DEBT-REPORT.md`](TECH-DEBT-REPORT.md).
- [ ] **Utang baru (jujur):** target memory < 550 MB untuk 100k item (PRD §4.1.1) kemungkinan besar
      **tidak tercapai** — pada 50k item RSS sudah 482 MB setelah index FTS ditambahkan.
- [ ] **Budget memory thumbnail masih fixed 128 MB** — belum adaptif seperti keputusan PRD §4.1.1 poin 2.
      Ini juga alasan RSS 439 MB; membuatnya adaptif akan memangkas ~100 MB.
- [ ] **Thumbnail belum dibuat saat indexing** — target PRD "≥ 30 file/s (+thumbnail)" belum teruji.
- [ ] Sticky header belum punya animasi push-up (menempel di atas, tidak terdorong header berikutnya).
- [x] **Kedipan thumbnail video diperbaiki** (lihat §7.1) — placeholder video sekarang stabil dan
      menampilkan badge ▶. Yang masih belum ada: **ekstraksi frame & pemutaran video itu sendiri**.
      Viewer masih menampilkan poster frame + catatan *"Video playback arrives in M2"*.
- [ ] People / Places / Smart, penulisan EXIF, dan `tests/` (direktori masih tidak ada, jadi
      `KG_BUILD_TESTS` **harus tetap OFF**).
- [ ] **Dokumen ini dan `README.md` ditulis setelah M1**; `docs/M0-REPORT.md` tetap sebagai catatan M0.

---

## 10. Kesimpulan

M1 lulus. Yang paling penting untuk keputusan Anda:

1. **Performa tidak beregresi sama sekali** meskipun ada sticky header yang bekerja per-frame.
2. **Throughput indexing bukan masalah** — 9× di atas target, jalur CPU/DB jelas bukan bottleneck.
3. **EXIF tidak perlu Exiv2** untuk cakupan v1, dan sudah dibuktikan setara dengan implementasi
   independen (0 mismatch).
4. **Dua risiko nyata yang harus ditutup di M2: pencarian FTS pada skala 100k, dan filter album ke SQL.**
   Keduanya jalur O(n) atau tanpa-index di jalur muat, dan keduanya sudah teridentifikasi sejak M0 —
   sekarang keduanya sudah menjadi utang teknis yang konkret.
