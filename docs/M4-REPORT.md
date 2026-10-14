# M4 — Polish & Packaging

**Status:** selesai, dengan satu catatan lingkungan: skrip/manifest pemaketan **ditulis dan
divalidasi sebagian**, tetapi artefak AppImage/Flatpak/`.deb` tidak bisa dibangun di mesin dev ini
karena `linuxdeploy`, `flatpak-builder`, dan `dpkg-deb` tidak terpasang dan tidak ada `sudo`.
Lihat §7.4.

Laporan ini melengkapi `docs/M2-REPORT.md`. Item 1 dari daftar utang ("tutup utang kecil") juga
dikerjakan di sini karena ia menutup celah yang ditemukan saat M2 (§5–§6).

---

## 1. Tema terang / gelap

- `src/app/AppSettings.{h,cpp}` (baru) menyimpan preferensi lewat `QSettings`
  (`appearance/theme`, `appearance/language`) — sengaja **tidak** di `library.db`, karena tema adalah
  properti aplikasi, bukan koleksi foto.
- Nilai `theme`: `system` / `light` / `dark`. Untuk `system`, `dark()` mengikuti
  `QStyleHints::colorScheme()`, dan `colorSchemeChanged` diteruskan agar tema ikut berubah saat
  desktop berganti mode ketika aplikasi berjalan.
- `src/ui/qml/Theme.qml` sekarang menurunkan **seluruh palet** dari satu properti `dark`. Ia tetap
  singleton tanpa logika per-frame; hanya satu boolean yang berubah.
- `Main.qml` mengikat `Theme.dark` ke `app.settings.dark` lewat `Binding`, dan menu ⚙ (kanan atas)
  berisi pilihan System / Light / Dark.

Kontras kedua palet: latar `#14161a`↔`#f6f7f9`, teks `#eef1f6`↔`#1b1f27`, aksen `#4c8dff`↔`#2f6fe4`.

## 2. i18n EN / ID

- Semua string antarmuka sudah dibungkus `qsTr()` (129 pesan di QML + 1 di C++).
- Katalog Indonesia: `resources/i18n/kin-galeri_id.ts` (124 pesan diterjemahkan).
- Digabungkan saat build oleh `qt_add_translations()` (lihat `CMakeLists.txt`) ke resource
  `:/i18n/kin-galeri_id.qm`. `AppSettings::applyLanguage()` memuatnya saat preferensi berubah;
  mengganti bahasa memicu `LanguageChange` sehingga binding `qsTr()` di QML dievaluasi ulang **tanpa
  restart**.
- Bila Qt tanpa LinguistTools, build tetap jalan tanpa terjemahan (hanya pesan status).
- Bahasa Inggris adalah bahasa sumber, jadi tidak butuh `.qm`; memilih English menghapus translator.

Penerjemahan sebagian tidak apa-apa: entri yang belum diterjemahkan jatuh ke teks sumber (Inggris).

## 3. HiDPI & fractional scaling (F-3.10)

`main.cpp` menetapkan `QT_SCALE_FACTOR_ROUNDING_POLICY=PassThrough` **sebelum** `QGuiApplication`
dibuat (kebijakan dibaca di konstruktor). Ini menjaga faktor 1.25×/1.5× tidak dibulatkan ke bilangan
bulat, yang membuat UI terlihat "chunky". Variabel dihormati bila pengguna sudah menetapkannya, dan
di Wayland faktor ditentukan kompositor.

## 4. Fallback `--software-render`

Menutup risiko PRD §8 (driver Wayland/GPU bermasalah). Flag dibaca langsung dari `argv` di
`applyPreAppEnvironment()` dan men-set `QT_QUICK_BACKEND=software` sebelum `QGuiApplication` memilih
backend scenegraph. Ini penting: sesudah konstruktor, perubahan sudah terlambat.

## 5. Aksesibilitas

- `MediaCell` mendapat `Accessible.role: Button`, `Accessible.name` = nama file,
  `Accessible.description` (Gambar/Video), dan `Accessible.selected`.
- `Accessible.name` ditambahkan ke kontrol berikon di header (zoom −/+, bersihkan tanggal, ⚙) dan ke
  kolom pencarian. Kontrol berteks sudah otomatis punya nama dari `text`.
- Seluruh aksi penting tetap punya jalan pintas papan tombol (lihat user guide).

**Belum:** kustomisasi shortcut oleh pengguna (PRD M4 menyebutnya). Aksi saat ini memakai `Shortcut`
statis di QML; menjadikannya dapat dikonfigurasi butuh halaman pengaturan + skema tombol sendiri.
Ditandai sebagai item M5, bukan diklaim selesai.

## 6. Unit test (item 1)

Direktori `tests/` (sebelumnya tidak ada) berisi 7 target Qt Test + CTest, diaktifkan dengan
`-DKG_BUILD_TESTS=ON`. `kin-galeri-testcore` (static library) mengompilasi subset sumber non-QML
sehingga target aplikasi tidak terganggu.

| Target | Mengunci perilaku |
|---|---|
| `tst_clioptions` | default adaptive (`-1`), rentang validasi, flag baru |
| `tst_appsettings` | normalisasi tema/bahasa, persistensi QSettings |
| `tst_pathutils` | konversi `file://` → path (bug move M2) |
| `tst_librarywatcher` | debounce, subdirektori, `stop()`, idempoten |
| `tst_mediagridmodel` | album CRUD, seleksi, filter tanggal, rename/move + rollback + undo, trash + undo |
| `tst_librarymaintenance` | pemulihan FTS |
| `tst_folderindexer` | index awal, incremental skip, upsert, rekonsiliasi file hilang |

Semua memakai `QTemporaryDir`; library pengguna tidak tersentuh. Dua bug ditemukan oleh test ini:
`Database` tidak punya destruktor (koneksi bocor antar-instance), dan ekspektasi `scanned` menghitung
file yang tidak didukung.

## 7. Packaging

### 7.1 Aturan install

`CMakeLists.txt` sekarang memasang binary ke `bin/`, `.desktop` ke
`share/applications/`, dan ikon SVG ke `share/icons/hicolor/scalable/apps/`. **Tidak** ada
`qt_generate_deploy_qml_app_script()`: pembundelan Qt diserahkan ke alat packaging (plugin Qt dari
linuxdeploy untuk AppImage; runtime untuk Flatpak). Ini juga menghindari ketergantungan pada
kombinasi Qt/CMake deploy-tool tertentu.

### 7.2 AppImage — `packaging/appimage/build.sh`

Menjalankan `cmake --install` ke sebuah `AppDir`, lalu `linuxdeploy --plugin qt`. Alat diunduh ke
`build-appimage/tools/` saat pertama. Desktop file + ikon juga disalin ke akar AppDir.

### 7.3 Flatpak — `packaging/flatpak/io.github.mrrizkin.KinGaleri.yml`

Memakai `org.kde.Platform`/`Sdk` 6.8 (sudah berisi Qt6 + FFmpeg). Backend mpv dimatikan karena
libmpv tidak ada di runtime; playback lewat Qt Multimedia (default PRD §10.1).

### 7.4 `.deb` — `packaging/deb/build-deb.sh` + CPack

CPack `DEB` dengan `SHLIBDEPS ON`; skrip menolak jalan bila `dpkg-deb`/`dpkg-shlibdeps` tidak ada.

**Catatan lingkungan.** Di mesin dev (Artix, tanpa `sudo`): `linuxdeploy`, `flatpak-builder`, dan
`dpkg-deb` tidak terpasang, jadi ketiga artefak **tidak dibangun di sini**. Yang bisa diverifikasi:
`cmake --install` menghasilkan pohon install yang benar (`usr/bin/kin-galeri`,
`.desktop`, ikon), binary hasil install berjalan (`--no-ui` exit 0), dan
`desktop-file-validate` melaporkan desktop file valid.

## 8. Profiling performa (PRD §4.1)

Build mpv, `QT_QPA_PLATFORM=offscreen`, `--bench-scroll`:

| Dataset | Startup | Frame | Dropped | fps | avg | RSS | peak | thumbs |
|---|---|---|---|---|---|---|---|---|
| 50k sintetis | 595 ms | 1435 | **0** | 172.9 | 5.78 ms | 265 MB | 265 MB | 930 |
| 100k, run dingin | 1753 ms | 1413 | 3 (0.21%) | 170.7 | 5.86 ms | 332 MB | 351 MB | 948 |
| 100k, run hangat | 990–1004 ms | 1118–1129 | **0** | 172–174 | 5.8 ms | 322 MB | 339 MB | 948 |

Warm start < 1.5 s dan RSS 100k 322 MB (target PRD < 550 MB) — **semua target §4.1 terpenuhi**.
Angka startup 1753 ms hanya muncul pada run pertama pasca-seeding (page cache dingin), bukan regresi
dari perubahan M4.

## 9. Verifikasi

- `cmake --build build` bersih; `QT_QPA_PLATFORM=offscreen` menjalankan QML tanpa error
  (`TypeError`/binding loop tidak ada) — dicek juga setelah penambahan `Accessible`.
- GUI X11 `:0.0`: tema **gelap** + bahasa **Indonesia** tampil benar (`Semua foto`, `Tong sampah`,
  `Album baru…`, `memantau 2 folder`); tema **terang** + bahasa **Inggris** juga benar.
- `tests/`: 7/7 lulus.
- `--rebuild-search-index` diuji pada DB nyata (`/tmp/kgidx.db`).
- Install: `DESTDIR=... cmake --install` benar; binary hasil install jalan.
- `desktop-file-validate`: OK.

## 10. Yang masih terbuka

- **Kustomisasi shortcut** (PRD M4) — belum; lihat §5.
- **AppImage/Flatpak/.deb belum dibangun** di lingkungan ini (§7.4). Skrip/manifest siap.
- **Terjemahan C++**: string yang dibangun di C++ (mis. ringkasan `"5 items · 508 B"`) masih Inggris.
  Untuk melokalkannya perlu `QCoreApplication::translate` di model/AppController.
- **HEIC/HEIF** fallback `heif-convert` masih belum ada (risiko PRD §8).
