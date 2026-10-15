# Kin Galeri

Galeri foto dan video lokal untuk Linux. Membaca folder di disk Anda, tidak
mengunggah apa pun, tidak butuh akun. Dibangun dengan Qt 6 dan QML, satu proses,
satu basis data SQLite per pustaka.

![CI](https://github.com/mrrizkin/KinGaleri/actions/workflows/ci.yml/badge.svg)
![Rilis](https://img.shields.io/github/v/release/mrrizkin/KinGaleri)

## Fitur

- **Cepat pada pustaka besar** — 100.000 berkas: frame pertama ~1,3 detik, gulir
  166–172 fps, memori ~330 MB. Angka dan metodenya di [`docs/PERFORMANCE.md`](docs/PERFORMANCE.md).
- **Thumbnail adaptif** — cache di disk dengan anggaran memori yang menyesuaikan
  ukuran jendela, plus fallback HEIC/HEIF.
- **Album, tag, favorit, Tong sampah** — dengan undo untuk pindah nama, pindah
  folder, dan hapus.
- **Tampilan berdasarkan tanggal, sumber, tempat, dan event** — pengelompokan
  tempat memakai indeks kota offline, jadi tidak ada permintaan jaringan.
- **Pengelompokan wajah opsional** — deteksi + embedding lewat ONNX Runtime,
  aktif hanya bila Anda ingin, dan modelnya diunduh saat Anda menekan tombolnya.
  Pada LFW: purity 98,36%, pairwise F1 96,33%.
- **Pemutaran video** lewat mpv (default pada Arch/AppImage/`.deb`) atau Qt
  Multimedia (Flatpak).
- **Pencarian teks penuh** dengan FTS5, tema terang/gelap dengan enam warna aksen,
  antarmuka Indonesia/Inggris, HiDPI, dan mode render perangkat lunak.

## Instalasi

| Platform | Perintah |
|---|---|
| Debian/Ubuntu | `sudo apt install ./kin-galeri_*_amd64.deb` |
| Arch | `pacman -U kin-galeri-git-*.pkg.tar.zst` |
| AppImage | `chmod +x Kin_Galeri-*.AppImage && ./Kin_Galeri-*.AppImage` |
| Flatpak | `flatpak install flathub org.kde.Platform//6.8`<br>`flatpak install ./KinGaleri.flatpak` |

Ambil berkasnya dari [halaman rilis](https://github.com/mrrizkin/KinGaleri/releases),
lalu verifikasi dengan `sha256sum -c SHA256SUMS`.

Membangun dari sumber: lihat [`CONTRIBUTING.md`](CONTRIBUTING.md) dan
[`docs/PACKAGING.md`](docs/PACKAGING.md). Kebutuhannya Qt ≥ 6.5, CMake ≥ 3.21, dan
compiler C++20; opsi yang tersedia termasuk `KG_ENABLE_MPV`, `KG_ENABLE_MULTIMEDIA`,
`KG_ENABLE_ML`, dan `KG_BUILD_TESTS`.

## Mulai cepat

1. Jalankan Kin Galeri. Kalau pustaka masih kosong, folder home ditawarkan untuk
   dipindai.
2. **Pengaturan → Pustaka → Tambah folder** untuk menunjuk folder foto Anda.
3. Pindai berjalan di latar; progresnya terlihat di bilah status.
4. Klik ganda untuk membuka penampil, `Esc` untuk kembali.

Baris perintah berguna untuk otomasi dan pengujian:

```bash
kin-galeri --no-ui --no-home-scan --db /tmp/lib.db --index /media/foto   # indeks tanpa jendela
kin-galeri --version                                                     # Qt, backend video, status ML
```

Panduan lengkap pemakaian ada di [`docs/USER-GUIDE.md`](docs/USER-GUIDE.md) dan
[wiki](https://github.com/mrrizkin/KinGaleri/wiki).

## Dokumentasi

| Dokumen | Isi |
|---|---|
| [`docs/USER-GUIDE.md`](docs/USER-GUIDE.md) | Cara memakai aplikasinya sehari-hari |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | Modul, skema basis data, threading, dan kenapa keputusan diambil |
| [`docs/PERFORMANCE.md`](docs/PERFORMANCE.md) | Metode benchmark dan semua angka yang dilaporkan |
| [`docs/PACKAGING.md`](docs/PACKAGING.md) | Tiap target, dependensinya, dan cara rilis diterbitkan |
| [`docs/TECH-DEBT.md`](docs/TECH-DEBT.md) | Yang masih terbuka, apa adanya |
| [`docs/PRD.md`](docs/PRD.md) | Kebutuhan produk dan kriteria keberhasilan |
| [Wiki](https://github.com/mrrizkin/KinGaleri/wiki) | Pemasangan, pintasan papan tombol, pemecahan masalah |

## Berkontribusi

Baca [`CONTRIBUTING.md`](CONTRIBUTING.md) — di sana ada cara build, gate yang harus
lolos sebelum membuka PR (`ctest`, `scripts/qml-lint.sh`, `scripts/i18n-audit.py`),
aturan penulisan string yang bisa diterjemahkan, dan skema versi.

## Lisensi

GPL-3.0-or-later. Lihat [`LICENSE`](LICENSE).

Nama tempat offline berasal dari indeks kota publik (`resources/data/cities.tsv`);
atribusi lengkapnya ada di `CONTRIBUTING.md`.
