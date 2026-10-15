# Utang teknis

Hanya yang **masih terbuka**. Yang sudah selesai ada di riwayat git — setiap
perbaikan punya commit-nya sendiri beserta alasan dan cara memverifikasinya.

## Tinggi — memengaruhi kebenaran atau kepercayaan

**Peringatan `UnqualifiedAccess` qmllint (241 buah).** Komponen bersarang membaca
id `root` milik komponen luar, yang persis dilarang `pragma ComponentBehavior: Bound`.
183 di antaranya adalah satu pola: delegate inline membaca `root.<sesuatu>`.
Kategori ini dimatikan di `.qmllint.ini` dengan komentar alasan; kategori lain
aktif dan `./scripts/qml-lint.sh` gagal bila ada peringatan baru. Perbaikan
sesungguhnya bukan menambah pragma, melainkan memindahkan delegate inline ke
berkasnya sendiri atau memberinya `required property` — dan `required property`
tidak bisa disuntikkan view ke delegate inline, jadi ini pekerjaan nyata, bukan
satu baris. Utang ini tidak menimbulkan gejala hari ini, tapi menjaga qmllint
sebagai gerbang hanya bisa dipercaya setelah selesai.

**`KinGaleri.Mpv` tidak punya `qmldir` statis.** Tipe `MpvItem` didaftarkan saat
runtime dengan `qmlRegisterType`, jadi qmllint tidak bisa memvalidasi
`MpvVideoSurface.qml` dan berkas itu terpaksa mematikan beberapa kategori
peringatan untuk dirinya sendiri. Jalur perbaikannya menjadikannya modul QML
sungguhan (`QML_ELEMENT` di dalam `qt_add_qml_module`) atau menyediakan qmldir
stub untuk lint. Jalur ini belum ditempuh karena mpvqt tidak mengirim qmldir
dan modul statis menambah plugin/perakitan ke semua target.

## Sedang — nyata, tapi tidak menghalangi

**Materialisasi baris grid masih O(n).** Sisa ~776 ms untuk 100.000 baris
(~8 µs/baris, 18 pemanggilan `query.value()` per baris). Startup sudah di bawah
anggaran lewat batas grid dan cache agregat, jadi ini bukan penghalang rilis,
tetapi dua arah perbaikan sudah jelas: isi model secara bertahap (tampilkan frame
pertama dengan sebagian baris, lanjutkan di latar), dan hindari `query.value()`
berulang dengan pembacaan berbasis indeks.

**Klaster wajah bersifat greedy dan tidak bisa dibatalkan per-wajah.** Gabung
(merge) sudah ada; **pisah (split)** belum — itu M5. Satu wajah yang salah masuk
tidak bisa dikeluarkan sendirian. Ambang 0,50 membuat ini jarang (purity 98,36%
di LFW), tapi "jarang" bukan "tidak pernah". Perbaikan alternatif yang sudah
tercatat: batasi update centroid/medoid, atau DBSCAN/HDBSCAN cosine dengan
`eps ≈ 0,6`. Validasi apa pun di area ini harus memakai pustaka pribadi yang
nyata, karena dataset potret rapi tidak mewakili foto keluarga.

**Pemodelan People untuk jumlah klaster besar.** Ganti nama dan gabung orang
diuji pada ukuran pustaka yang wajar. Bila jumlah klaster mencapai ribuan, model
khusus untuk daftar orang kemungkinan diperlukan; belum ada pengukuran.

**Tampilan Tong sampah tidak memakai partial index.** Dapat diterima: recycle bin
secara alami kecil, dan `deleted_at IS NOT NULL` jarang dievaluasi.

## Rendah — catatan, bukan pekerjaan

**`Database` menamai koneksi SQLite per-thread, bukan per-berkas atau per-instance.**
Aman untuk aplikasi saat ini, dan destruktor menutup kasus pemakaian berurutan,
tetapi bila suatu saat dua pustaka dibuka bersamaan di satu thread,
`connection()` akan diam-diam mengembalikan koneksi yang pertama.

**Teks lisensi pihak ketiga tidak diagregasi di dalam AppImage.** ONNX Runtime
(MIT) dipasang beserta lisensinya oleh paket Arch, tetapi AppImage hanya membawa
binernya. Kepatuhan yang layak: kumpulkan lisensi ke dalam bundle dan tampilkan
lewat menu Tentang.

**Flatpak memakai Qt Multimedia, bukan mpv.** Bukan bug — keputusan sadar, dengan
alasan di `PACKAGING.md` — tetapi artinya perilaku pemutaran berbeda antar target,
dan itu perlu diingat saat menangani laporan bug video.

## Yang **bukan** utang

Beberapa hal terlihat seperti utang tetapi sebenarnya keputusan:

- Model wajah 37 MB diunduh saat pengguna menekan tombolnya, bukan saat pasang.
  Itu menjaga paket tetap kecil dan tidak ada trafik jaringan tanpa persetujuan
  pengguna.
- Peringatan `setHighDpiScaleFactorRoundingPolicy` dari Qt 6.8 bukan berasal dari
  kode ini; Qt 6.12 tidak memunculkannya. Nilai yang diminta sudah dipasang
  sebagai variabel lingkungan sebelum `QGuiApplication`.
