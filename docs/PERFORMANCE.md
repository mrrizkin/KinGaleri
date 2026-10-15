# Performa

Semua angka di sini diukur di mesin yang sama — **AMD Ryzen 7 PRO 5850U**,
16 thread, build Release, `QT_QPA_PLATFORM=offscreen` — dan bisa diulang dengan
perintah yang tertulis di tiap bagian. Angka dari mesin lain tentu berbeda; yang
penting bukan angka absolutnya, melainkan metode dan ambang batasnya.

## Cara mengukur

```bash
# Pustaka 100k berkas nyata (bukan baris sintetis)
XDG_CONFIG_HOME=/tmp/kg/cfg XDG_DATA_HOME=/tmp/kg/data \
  ./build/kin-galeri --db /tmp/kg/lib.db --cache /tmp/kg/cache \
  --no-home-scan --index /tmp/kg100k

# Pemanasan + gulir, 2 detik
XDG_CONFIG_HOME=/tmp/kg/cfg XDG_DATA_HOME=/tmp/kg/data QT_QPA_PLATFORM=offscreen \
  ./build/kin-galeri --db /tmp/kg/lib.db --cache /tmp/kg/cache \
  --no-home-scan --bench-scroll 2
```

`--bench-scroll` mencetak tiga baris: `startup` (dari proses lahir sampai frame
pertama benar-benar tampil, diukur lewat `PerfMonitor::firstFramePresented`),
`resting` (RSS setelah tenang), dan `scroll` (frame, drop, fps, RSS puncak).

`--seed-benchmark N` menyisipkan baris sintetis dan berguna untuk menguji beban
gulir tanpa menyiapkan berkas, tetapi **tidak** menguji pipeline indeks — untuk
itu pakai berkas sungguhan seperti di atas.

## Hasil

| Skenario | Startup | Frame | Drop | fps | RSS | Puncak |
|---|---|---|---|---|---|---|
| 100k berkas, tanpa batas grid | **1322 ms** (median 6 run: 1310–1346) | — | — | — | 328 MB | 351 MB |
| 100k berkas, batas grid 20.000 | **894–899 ms** | — | — | — | ~300 MB | ~350 MB |
| Gulir 100k, 12 detik | — | 1781 | 2 (0,11%) | **172,0** | 335 MB | 357 MB |
| Gulir 100k (run lain) | — | — | ≤0,4% | 166–172 | 316–339 MB | — |

Frame terburuk ~15 ms terhadap anggaran 16,67 ms, jadi 60 fps punya ruang; rss
jauh di bawah target PRD < 550 MB.

Indeks 100.000 berkas PNG: **~8 detik**, basis data 47 MB. Setelah tiga kali
restart: `media` = `media_fts` = 100.000, `PRAGMA integrity_check` = `ok`,
`PRAGMA foreign_key_check` bersih, cache thumbnail 1.274 berkas / 5 MB dan tidak
membengkak antar-restart.

### Kenapa startup turun

Pengukuran pertama pada 100k adalah ~2,7–3,4 detik. Instrumentasi menunjukkan
penyebabnya bukan thumbnail (frame pertama hanya ~20 ms setelah startup selesai),
melainkan dua hal di `MediaGridModel`:

1. `albumCards()` melakukan `GROUP BY source_album` dengan collation BINARY
   sementara indeksnya `COLLATE NOCASE` (`idx_media_album_taken`), sehingga
   subquery cover men-scan ulang seluruh pustaka untuk tiap album: **468 ms**
   untuk 100 album. Dengan collation yang konsisten: **12 ms**. Ini sekaligus
   memperbaiki bug laten — kartu album dihitung case-sensitive padahal filternya
   tidak.
2. Agregat itu dihitung **tiga kali** per reload (dari binding `albumsChanged`,
   `collectionsChanged`, dan `registerThumbnailSources`). Sekarang hasilnya
   di-cache per reload.

Loop materialisasi 100.000 baris masih memakan ~776 ms (~8 µs/baris, 18
`query.value()` per baris). Itu sisa yang masih bisa dikejar, tapi tidak lagi
menentukan: batas grid sudah cukup untuk membawa startup di bawah 1,5 detik.

## Akurasi pengelompokan wajah

Data: LFW, 13.233 gambar / 5.749 identitas. 13.233 gambar menghasilkan 16.998
wajah dan 4.976 orang. Diukur dengan `scripts/evaluate-lfw.py`, memakai `pairs.txt`
resmi untuk verifikasi dan nama folder sebagai label kebenaran untuk klasterisasi.

| Ambang klasterisasi | Klaster | Purity | Wajah di klaster satu-identitas | Pairwise P / R / F1 | Singleton |
|---|---|---|---|---|---|
| 0,40 (lama) | 3.530 | 76,35% | 31,9% | 86,7 / 94,8 / 90,5% | 51,3% |
| **0,50 (sekarang)** | 5.845 | **98,36%** | **85,6%** | 97,8 / 94,9 / **96,3%** | 70,9% |

Verifikasi pasangan (tugas berbeda, ambang berbeda): **97,03% ± 1,06%** dengan
10-fold cross-validation, pada ambang terbaik 0,346. Ambang 0,50 hanya dipakai
untuk mengelompokkan; menaikkannya menurunkan akurasi verifikasi pada ambang itu
(94,73%), tapi aplikasi tidak pernah memutuskan pasangan — ia hanya mengelompokkan,
dan di situ keputusan yang salah bersifat menular (satu wajah salah masuk akan
menggeser centroid dan menarik wajah salah berikutnya).

Hipotesis yang sempat populer — bahwa penyebabnya adalah running-mean centroid —
**tidak terbukti** saat diuji: membekukan centroid setelah N anggota hanya
menggeser angka pada ambang longgar dan tidak mengubah apa pun pada ambang kerja.
Angka di tabel di atas adalah bukti pengganti hipotesis itu.

## Definition of Done (PRD §9)

| Kriteria | Status | Bukti |
|---|---|---|
| Indeks 100.000 media tanpa crash; DB & cache konsisten setelah restart | ✅ | 100k berkas nyata, `integrity_check` ok, 3× restart konsisten |
| Warm start < 1,5 s; gulir 60 fps pada 100k item | ✅ | 1322 ms; 166–172 fps, drop ≤0,4% |
| Semua grouping P0 (date, source, favorite, recycle) berfungsi & akurat | ✅ | `tst_mediagridmodel`, `tst_grouping`, verifikasi GUI |
| Tidak ada operasi jaringan tanpa persetujuan user | ✅ | satu-satunya pemanggil jaringan adalah tombol unduh model di Pengaturan |
| AppImage jalan di Ubuntu 24.04 & Fedora 40 tanpa dependensi manual | ✅ | smoke test CI di kedua container, lihat `PACKAGING.md` |
| Dokumentasi build + panduan pengguna | ✅ | `README.md`, `CONTRIBUTING.md`, `USER-GUIDE.md`, wiki |

## Batas kejujuran

- Semua angka di atas dari **satu mesin** dan satu build (`-DCMAKE_BUILD_TYPE=Release`).
  Startup adalah ukuran yang paling sensitif terhadap keadaan mesin; angka
  offscreen bervariasi ±40 ms antar-run, jadi yang dilaporkan adalah median, bukan
  satu run terbaik.
- 100k di sini berarti 100.000 berkas PNG yang benar-benar ada di disk, bukan
  baris basis data. Pustaka dengan video besar akan lebih lambat di sisi dekode,
  dan itu bukan bagian dari angka ini.
- Angka LFW memakai potret yang sudah terpotong rapi. Wajah di foto grup dan
  sudut ekstrem tidak diwakili oleh dataset itu.
