# Panduan Pengguna — Kin Galeri

Panduan ini ditujukan untuk pengguna akhir. Untuk arsitektur dan keputusan teknis, lihat
[`PRD.md`](PRD.md) dan laporan per milestone di `docs/`.

Kin Galeri adalah penjelajah foto & video **lokal**: semua data disimpan di komputer Anda, tidak ada
jaringan, tidak ada telemetry.

---

## 1. Menjalankan

### Dari source

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/kin-galeri
```

### Dari paket

- **AppImage:** `chmod +x Kin_Galeri-*.AppImage && ./Kin_Galeri-*.AppImage`
- **Flatpak:** `flatpak run io.github.mrrizkin.KinGaleri`
- **Debian/Ubuntu:** `sudo dpkg -i kin-galeri_*.deb`

Cara membangun paket ada di `packaging/`, `PACKAGING.md` dan `../packaging/README.md`.

## 2. Menambahkan foto

Klik **Tambah folder…** di bilah kiri bawah dan pilih folder berisi foto. Pemindaian berjalan di latar
belakang dan bisa dibatalkan lewat tombol **Cancel** di header.

Folder yang sudah ditambahkan akan dipantau (`inotify`): menambah, mengubah, atau menghapus file di
dalamnya otomatis menyegarkan library tanpa pemindaian ulang manual. Status pemantauan terlihat di
footer, mis. `memantau 2 folder`.

Untuk indexing headless (mis. lewat cron):

```bash
kin-galeri --db ~/.local/share/kin-galeri/library.db --index ~/Pictures --no-ui
```

## 3. Antarmuka

```
┌── Header: pencarian · ringkasan · Tanggal ▾ · Pilih · Batalkan · zoom · Perf · ⚙ ──┐
│ Sidebar          │ Grid (timeline, header tanggal lengket)                        │
│  LIBRARY         │                                                                │
│   Semua foto     │                                                                │
│   Album          │                                                                │
│   Gambar         │                                                                │
│   Video          │                                                                │
│   Favorit        │                                                                │
│  KOLEKSI         │                                                                │
│   Orang/Tempat/… │                                                                │
│   Tong sampah    │                                                                │
└── Footer: ringkasan · progres · status pemantauan ────────────────────────────────┘
```

- **Grid** hanya merender baris yang terlihat, jadi tetap 60 fps pada 100.000+ item.
- **Halaman Album**: entry **Album** di sidebar menampilkan semua album sebagai grid cover
  (album manual dulu, lalu folder/kamera). Klik satu kartu untuk membuka isinya; tombol
  **← Albums** di header kembali. Buat album lewat **+ New album…**; klik-kanan kartu album
  manual untuk **Rename…** / **Delete album**.
- **Header tanggal** menempel di atas saat menggulir; **scrollbar** di kanan menampilkan bubble tanggal saat di-drag/hover.
- Zoom thumbnail: tombol **−** / **+** di header (atau `Ctrl` + scroll di grid bila tersedia).

## 4. Mencari & memfilter

- **Pencarian** (`Ctrl+F`): cocokkan nama file, merek/model kamera, nama album, dan nama tag. Hasil
  muncul setelah ±0,2 detik.
- **Filter tanggal** (**Tanggal ▾**): Hari ini / 7 hari terakhir / 30 hari terakhir / Tahun ini /
  Kapan saja. Tombol **✕** membersihkan filter.
- **Filter cepat** dari sidebar: Semua foto, Gambar, Video, Favorit, Tong sampah, album, dan tag.

## 5. Melihat

- Klik ganda sebuah item untuk membuka **viewer layar penuh**. `Esc` menutup, panah kiri/kanan
  berpindah, roda tetikus untuk zoom, dan tombol **Fit** mengembalikan ke ukuran layar.
- Untuk **video**, kontrol putar/jeda muncul; `Space` putar/jeda, `M` bisukan. Video diputar dengan
  backend mpv (bila tersedia saat build) atau Qt Multimedia/FFmpeg.
- Klik kanan sebuah item → **Details…** untuk melihat EXIF lengkap (kamera, lensa, ISO, bukaan,
  panjang fokus, koordinat GPS, dimensi, durasi), plus tombol **Reveal** (buka di file manager) dan
  **Copy path**.

## 6. Mengelola file

### Favorit

Klik kanan → **Tambah/Hapus favorit**, atau gunakan tombol hati di sidebar untuk melihat semua
favorit.

### Album manual

1. **+ Album baru…** di sidebar (bagian ALBUM) lalu beri nama.
2. Untuk memasukkan foto: aktifkan **Pilih**, pilih item, lalu **Tambah ke album…** di bilah bawah dan
   pilih album (atau ketik nama baru → **Buat + tambah**).
3. Klik kanan album di sidebar untuk **Rename…** atau **Delete album**. Menghapus album **tidak**
   menghapus file.

### Tag

- Untuk menandai: klik kanan satu item → **Tambahkan tag…**, atau aktifkan **Pilih** lalu tombol
  **Tag…** di bilah bawah untuk banyak item sekaligus. Ketik nama baru lalu **Tambahkan**, atau pilih
  dari daftar **Semua tag** yang sudah ada.
- Item yang sudah ditandai menampilkan chip tag yang bisa dilepas (tombol **✕**) langsung di dialog
  yang sama — berguna untuk memperbaiki tag yang salah.
- Halaman **Tag** di sidebar (bagian COLLECTIONS) menampilkan satu kartu per tag: cover, nama, dan
  jumlah item. Klik kartu untuk memfilter grid; klik kanan kartu untuk **Rename tag…** atau
  **Hapus tag**. Menghapus tag tidak menghapus file.
- Tag tersimpan di database dan ikut terindeks pencarian, jadi mengetik nama tag di kotak cari
  menampilkan semua itemnya.

### Seleksi & aksi massal

Klik **Pilih** untuk masuk mode seleksi:

- Klik satu sel = pilih/batalkan.
- `Shift`+klik = perluas rentang dari pilihan terakhir.
- **Pilih semua**, **Bersihkan**, **Favoritkan**, **Hapus favorit**, **Tambah ke album…**,
  **Hapus dari album**, **Pindahkan ke tong sampah**, **Pulihkan**, **Selesai**.

### Ganti nama / pindahkan

- Klik kanan → **Rename…** untuk mengganti nama file (di disk dan di library sekaligus).
- Klik kanan → **Pindahkan ke…** untuk memindahkan file ke folder lain.

### Tong sampah & undo

- **Pindahkan ke tong sampah** bersifat *soft delete*: baris disembunyikan, **file di disk tidak
  disentuh**. Pulihkan lewat sidebar **Tong sampah** → **Pulihkan**.
- **Kosongkan tong sampah** menghapus baris dari database; file di disk tetap ada.
- Semua aksi rename / pindah / hapus / pulihkan bisa dibatalkan dengan **Batalkan** atau **`Ctrl+Z`**
  (riwayat 32 aksi terakhir).

> Kalau Anda memindahkan file ke folder **di luar** semua folder library, pemantau akan menganggapnya
> hilang dan menghapus barisnya dari library (file di disk tidak diapa-apakan). Pindahkan di dalam
> library agar tetap terlacak.

## 7. Pengaturan (Settings)

Buka halaman **Pengaturan** lewat ikon **⚙** di kanan header atau entri **Pengaturan** di sidebar.
Halaman ini memakai rel kategori di kiri dan kartu bertajuk di kanan; setiap kontrol langsung
tersimpan, tidak ada tombol Apply.

- **General** — halaman awal (All photos / Albums / resume terakhir), konfirmasi sebelum memindahkan
  item ke recycle bin, overlay frame-time saat mulai, dan **Developer mode**.
- **Interface** — tema (System / Light / Dark), warna aksen (6 pilihan), bahasa (System / English /
  Bahasa Indonesia, berlaku langsung), ikon sistem dari tema ikon desktop (bisa dimatikan untuk
  memakai glif teks bawaan), tampil/sembunyikan dan lebar sidebar, ukuran thumbnail, jarak
  antar tile, header tanggal, animasi transisi, serta sorotan hover pada tile. Ada juga **Items
  loaded in the grid**: batas jumlah item yang dimuat (0 = semua; di pustaka yang sangat besar
  batas ini yang menjaga frame pertama tetap cepat, dan sisa item muncul begitu batasnya
  dinaikkan).
- **Library** — daftar folder pustaka dengan jumlah item dan tombol **Remove**, **Add folder…**,
  **Rescan now**; pantau perubahan berkas otomatis; pindai ulang saat mulai; opsi pemindaian:
  ikuti symlink, sertakan folder tersembunyi, lewati folder sampah (daftar polanya terlihat & bisa
  diedit lewat tombol **Customize…**: tambah pola, hapus per baris, atau **Reset** ke bawaan). Di
  bawahnya ada **folder yang
  diabaikan** (tambah lewat dialog, tombol Remove per baris; seluruh isi folder ikut dilewati) dan
  **pola nama** (glob dipisah koma, mis. `Screenshots`, `backup*`). Junk diatur oleh toggle **Lewati
  folder sampah**; folder yang diabaikan selalu aktif. Menambahkan folder ke daftar
  abaikan tidak pernah menghapus berkas dari disk.
- **Thumbnails** — anggaran cache memori (adaptif, atau 32–2048 MB) dan info/aksi cache disk.
- **Playback** — putar video otomatis, ulang, mulai tanpa suara, volume default, dan backend.
- **Storage** — info cache/database, tombol **Bersihkan cache thumbnail** dan **Tambah folder…**.
- **Tentang** — versi aplikasi, backend video, lisensi (GPL-3.0-or-later) dan tautan sumber.

**Developer mode** (default nonaktif) menyembunyikan permukaan khusus pengembang dari UI normal:
Tombol **Perf** di toolbar dan pintasan `F12`, tombol **Generate 50,000 rows** di layar kosong,
placeholder *People/Places/Smart* dan label `DB · …` di sidebar, serta path database dan info
thumbnail di bilah status. Nyalakan hanya jika Anda ingin alat diagnostik itu; flag CLI
(`--bench-scroll`, `--seed-benchmark`) tetap bekerja tanpa mode ini.

Preferensi disimpan di `~/.config/kin/Kin Galeri.conf`. Opsi pemindaian (symlink/tersembunyi/sampah,
termasuk folder & pola yang diabaikan) berlaku pada pemindaian berikutnya; tekan **Rescan now**
untuk menerapkannya segera.

## 8. Papan tombol

| Tombol | Aksi |
|---|---|
| `Ctrl+F` | Fokus ke pencarian |
| `Ctrl+R` | Muat ulang library |
| `Ctrl+Z` | Batalkan aksi terakhir (rename/pindah/hapus/pulihkan) |
| `F12` | Tampilkan/sembunyikan overlay frame-time |
| `Esc` | Tutup panel detail / viewer |
| `←` / `→` | Item sebelumnya/berikutnya di viewer |
| `Space` | Putar/jeda video |
| `M` | Bisukan video |
| `+` / `−` | Perbesar/perkecil thumbnail (tombol header) |

## 9. Baris perintah

```
kin-galeri [opsi] [file...]

  -i, --index <folder>        Index folder (bisa diulang)
      --db <file>             Pakai file database lain
      --cache <dir>           Pakai direktori cache lain
      --reset-db              Hapus database sebelum mulai
      --thumbnail-size <px>   Panjang sisi thumbnail (default 256)
      --thumb-memory-cache <MB>  Budget thumbnail di memori (default: adaptif)
      --limit <N>             Hanya tampilkan N item terbaru (0 = semua; menimpa setelan
                              Settings → Interface → "Items loaded in the grid")
      --no-ui                 Mode headless (indexing saja)
      --software-render       Paksa renderer software (driver GPU bermasalah)
      --rebuild-search-index  Bangun ulang index pencarian lalu keluar
      --perf-overlay          Mulai dengan overlay frame-time
      --bench-scroll <s>      Benchmark gulir otomatis selama <s> detik
```

Argumen `file...` membuka berkas gambar/video yang diberikan langsung di penampil, bukan di
halaman Pustaka. Inilah yang dipakai desktop entry (`Exec=kin-galeri %F`), jadi **Open with → Kin
Galeri** dari Thunar, Nautilus, atau file manager lain akan menampilkan berkas yang Anda klik
meskipun berkas itu belum ada di pustaka.

## 10. Lokasi data

| Isi | Lokasi |
|---|---|
| Database library | `~/.local/share/kin-galeri/library.db` |
| Cache thumbnail | `~/.cache/kin-galeri/thumbs` (aman dihapus) |
| Konfigurasi | `~/.config/kin/Kin Galeri.conf` |

Database dan cache **terpisah** dari folder foto, jadi menghapusnya tidak menyentuh file asli. Cache
thumbnail bisa dibersihkan dari sidebar (**Bersihkan cache thumbnail**).

## 11. Pemecahan masalah

- **Pencarian tidak menemukan apa pun padahal foto ada** → bangun ulang index pencarian:
  `kin-galeri --rebuild-search-index`.
- **Jendela hitam / crash pada driver tertentu (Wayland)** → jalankan dengan
  `kin-galeri --software-render`.
- **UI pecah pada skala 125%/150%** → pastikan `QT_SCALE_FACTOR_ROUNDING_POLICY=PassThrough`
  (aplikasi menetapkannya sendiri; jangan ditimpa).
- **Video tidak jalan** → cek backend di header (tombol **Perf** tidak menampilkan backend; lihat
  log startup `video backend:`). Untuk Qt Multimedia, pastikan plugin FFmpeg terpasang.
- **Folder tidak ikut terpantau** → jumlah folder yang dipantau dibatasi (4096 direktori per akar);
  jalankan **Ctrl+R** untuk memindai ulang.
