# HANDOFF — UI Bug Hunt for Kin Galeri (untuk AI agent multimodal)

> Dokumen ini untuk agent berikutnya yang **bisa membaca gambar**. Tugas Anda: menjalankan
> aplikasi, mengambil screenshot sendiri, menemukan & memperbaiki bug UI pada QML, lalu
> memverifikasi ulang. Tidak ada screenshot di repo ini secara sengaja — ambil sendiri.

---

## 0. Ringkasan proyek

- **Nama**: Kin Galeri (klon Immich lokal-first untuk Linux).
- **Path**: `/home/mrrizkin/Projects/work/oss/gallery` — **git repo** (lihat `git log` untuk riwayat;
  dokumen ini ditulis sebelum repo di-init, keterangan lama "bukan git repo" sudah tidak berlaku).
- **Stack**: Qt 6.12.0 + QML (Qt Quick Controls) + C++20, video via libmpv (`KinMpvItem`).
  Linux **Artix**, tanpa `sudo`. X11 di `DISPLAY=:0.0`.
- **Binary**: `build/kin-galeri`.
- **Status milestone**: PRD & `docs/M4-REPORT.md` — M0–M4 selesai (kecuali artefak paket
  AppImage/Flatpak/.deb yang belum bisa dibuild karena tool tidak terpasang).

## 1. Yang jadi tugas Anda

1. Jalankan app, ambil screenshot **hanya jendela aplikasi** (jangan seluruh desktop).
2. Inspeksi visual: tema light & dark, semua menu/dialog/overlay, i18n ID/EN, HiDPI.
3. Perbaiki bug di QML/C++ terkait.
4. Rebuild, jalankan smoke test, pastikan 7 unit test tetap lulus.
5. Laporkan temuan + bukti before/after. Jangan simpan screenshot ke repo kecuali
   diminta; taruh di `/tmp`.

## 2. Menjalankan aplikasi

```bash
cd /home/mrrizkin/Projects/work/oss/gallery
cmake --build build            # build utama
pkill -x kin-galeri 2>/dev/null

# Data uji: 5 gambar (a.png, b.png, sub/d.png, sub/e.png, f.png) di /tmp/kgtest
DISPLAY=:0.0 ./build/kin-galeri --db /tmp/kgidx.db --cache /tmp/kgidxc
```

- Jendela: **1440x920** pada posisi **(2, 36)**. Cari id-nya:
  ```bash
  DISPLAY=:0.0 xdotool search --name 'Kin Galeri'   # pilih yang 1440x920
  ```
- **Isolasi config** (tema/bahasa) supaya tidak menyentuh config asli:
  ```bash
  XDG_CONFIG_HOME=/tmp/kgm4cfg DISPLAY=:0.0 ./build/kin-galeri --db /tmp/kgidx.db --cache /tmp/kgidxc
  # file: /tmp/kgm4cfg/kin/Kin Galeri.conf
  # [appearance]
  # theme=light|dark|system
  # language=id|en|system
  ```
- Config asli: `~/.config/kin/Kin Galeri.conf`.
- App menulis log ke **stderr**; abaikan noise `Gtk`/`murrine`/`canberra`.

## 3. Mengambil screenshot (WAJIB: hanya jendela app)

Agent TUI (tempat Anda berjalan) **sering mencuri fokus** sehingga klik `xdotool` nyasar.
Selalu raise + activate dulu, dan capture **window id saja**:

```bash
WID=$(DISPLAY=:0.0 xdotool search --name 'Kin Galeri' | while read id; do
        g=$(DISPLAY=:0.0 xdotool getwindowgeometry "$id" 2>/dev/null | grep -o '[0-9]*x[0-9]*')
        [ "$g" = "1440x920" ] && echo "$id"; done | head -1)
DISPLAY=:0.0 xdotool windowactivate --sync "$WID"
DISPLAY=:0.0 xdotool windowraise "$WID"
sleep 0.6

# opsi A: ImageMagick (paling aman, window-only)
DISPLAY=:0.0 import -window "$WID" /tmp/ui-dark-main.png
# opsi B: scrot window terfokus
DISPLAY=:0.0 scrot -u -o /tmp/ui-dark-main.png
```

> `scrot` tanpa `-u` = seluruh layar → ikut menangkap desktop/terminal user. Hindari.

### Peta koordinat (screen coords, jendela di 2,36)

| Elemen | Koordinat |
|---|---|
| Header (y≈48) | Search ±168 · `Date ▾` ±1080 · `Select` ±1157 · Grafik/Perf ±1358 · **⚙ ±1410** |
| Sel grid baris 1 (y≈235) | x ≈ 325, 525, 725, 925, 1125 |
| Selection bar (y≈893) | Select all ±403 · Clear ±467 · Add to album… ±578 · Favorite ±665 · Unfavorite ±770 · Move to recycle bin ±1290 · Done ±1385 |
| Context menu sel (klik kanan 325,235; x≈362) | Add to favorites ±250 · Details ±282 · Select ±315 · Add to album ±350 · Rename… ±416 · Move to… ±447 · Move to recycle bin ±481 · Copy path ±515 · Show in file manager ±545 |
| Baris album sidebar (y≈418) | Rename ±440 · Delete album ±469 |
| Dialog | field teks baru album ±(635,493) · OK ±(770,537) · tombol Create album picker ±(845,489) |

**Tips**: klik pertama pada dialog ber-field teks kadang hanya memfokuskan. Klik field
sekali lagi sebelum `xdotool type`. `xdotool type` lebih andal daripada clipboard.

## 4. Lead / dugaan bug (mulai dari sini)

> **Status ringkas (disunting setelah verifikasi):** lead 1–3 di bawah **sudah tidak berlaku**.
> Menu ⚙ yang mereka rujuk tidak ada lagi di header — ikon paling kanan sekarang adalah tombol
> navigasi ke halaman Settings (terverifikasi lewat tooltip "Settings"), dan pilihan tema/bahasa
> pindah ke **Settings → Interface**. Sisa lead (4–8) tetap relevan.

1. ~~**Tema dari config kadang tidak diterapkan pada cold start.**~~ **Tidak tereproduksi.** Cold
   start dengan `theme=light` di config langsung merender UI terang (screenshot
   `/tmp/kgv/shot-light-en.png`), begitu pula `theme=dark` (`/tmp/kgv/shot-dark-id.png`). `Binding`
   di `Main.qml` aman. Kalau regresi muncul lagi, langkah reproduksi lama tetap berguna: restart
   beberapa kali dengan `XDG_CONFIG_HOME=/tmp/kgm4cfg`.
2. ~~**Teks menu terpotong**: `Language: Bahasa Indo…`.~~ **Tidak berlaku.** Pilihan bahasa kini ada
   di halaman Interface, bukan di menu dropdown; label penuh ("Bahasa Indonesia") tidak terpotong.
3. ~~**Menu ⚙ nyaris keluar tepi kanan jendela.**~~ **Tidak berlaku.** Tidak ada lagi dropdown di
   header; yang ada hanya tombol ikon Settings dengan tooltip di sebelah kiri tepi kanan.
4. **Konsistensi tema terang**: pastikan semua elemen ikut palet terang — menu, semua Dialog
   (rename/move/album picker/error), selection bar, scrollbar, `ViewerOverlay`, `PerfOverlay`,
   `MpvVideoSurface`, `Sidebar`.
5. ~~**i18n belum menyeluruh**~~ **Sudah ditutup.** 43 string C++ dibungkus `tr()`
   (`AppController` 17, `MediaGridModel` 26), konteks C++ ditambahkan ke `.ts` (349 pesan total).
   Temuan penting: konteks `tr()` untuk kelas di `namespace kg` adalah nama metaobject
   (`kg::AppController`), **bukan** nama kelas telanjang — `.ts` sempat memakai nama pendek sehingga
   seluruh terjemahan C++ diam-diam jatuh ke Inggris. Dijaga `tests/tst_i18n.cpp`. **Jangan**
   jalankan target `update_translations` di mesin ini — `lupdate` dari `qt6-tools` 6.12.0 tidak
   punya dukungan QML dan akan menghapus ke-258 pesan QML. Sunting `.ts` dengan tangan.
6. ~~**HiDPI/fractional scaling** belum diverifikasi visual.~~ **Sudah diverifikasi.**
   `QT_SCALE_FACTOR=1.25` (jendela jadi 1800×1150) dan `1.5` (1920×1053) dirender tanpa teks
   terpotong, tanpa kontrol tumpang tindih, dan tanpa ikon buram. Bukti: `/tmp/kgv/scale125.png`,
   `/tmp/kgv/scale150.png`, montase `/tmp/kgv/montage.png`.
7. ~~**`--software-render`** belum diuji visual.~~ **Sudah diverifikasi**, lewat dua jalur:
   `QT_QUICK_BACKEND=software` dan flag `--software-render`. Keduanya menghasilkan frame identik
   tanpa artefak render (`/tmp/kgv/s-env.png`, `/tmp/kgv/s-flag.png`).
8. **Aksesibilitas** hanya bisa dicek dengan inspector (Accerciser/AT-SPI), bukan visual.

## 5. Peta file UI

```
src/ui/qml/
  Main.qml            # window, header (menu Date, Select, Undo, tombol Settings), dialog rename/move/error, selectionBar, shortcut
  Theme.qml           # SINGLETON (QT_QML_SINGLETON_TYPE) — seluruh palet dari `dark` + `accent` + `systemIcons`
  Sidebar.qml         # LIBRARY / ALBUMS / COLLECTIONS, dialog album, tombol bawah
  MediaGrid.qml       # grid, scroll, context menu, selection handling, album picker
  MediaRow.qml        # satu baris grid
  MediaCell.qml       # sel: thumbnail, checkbox, tint, Accessible.*, MouseArea
  TimelineHeader.qml  # header "Today" dsb
  ViewerOverlay.qml   # overlay fullscreen viewer
  MpvVideoSurface.qml # permukaan video (di-build hanya jika backend mpv)
  PerfOverlay.qml     # overlay F12
src/app/AppSettings.{h,cpp}   # QSettings theme/language, QTranslator
src/app/AppController.{h,cpp} # jembatan model <-> UI, indexFolders, dsb
```

## 6. Perintah build & verifikasi

```bash
# Build utama
cmake --build build

# Smoke test runtime QML tanpa GUI (harus tanpa error QML)
QT_QPA_PLATFORM=offscreen timeout 6 ./build/kin-galeri --no-ui --db /tmp/kgidx.db --cache /tmp/kgidxc; echo "exit=$?"
QT_QPA_PLATFORM=offscreen timeout 6 ./build/kin-galeri --db /tmp/kgidx.db --cache /tmp/kgidxc 2>&1 \
  | grep -iE "error|undefined|TypeError|binding loop" | grep -iv "Gtk\|murrine\|canberra"

# Unit test (12 target, harus 12/12 pass)
cmake -S . -B build-tests -G Ninja -DKG_BUILD_TESTS=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure

# Lint QML — pakai gate-nya, JANGAN `qmllint` dari PATH:
# qt5-declarative memasang /usr/bin/qmllint (Qt 5) yang membayangi Qt 6 milik
# /usr/lib/qt6/bin. Target `all_qmllint` juga tidak gagal saat ada warning dan
# dilewati bila stamp-nya masih segar; gate menambahkan `-W 0`.
./scripts/qml-lint.sh                       # harus 0 Warning, exit 0
```

## 7. Batasan / jangan disentuh

- **Jangan** mengubah handling video-EOF di `src/ui/quick/KinMpvItem.{h,cpp}` +
  `MpvVideoSurface.qml` — sudah selesai & terverifikasi (lihat `docs/M2-REPORT.md` §6). Diperiksa
  ulang di sesi ini: di akhir klip muncul tombol replay ↻ dengan frame terakhir tetap tampil dan
  tanpa spinner.
- Jangan regresi 11 unit test (`build-tests`).
- Jangan naikkan jumlah warning qmllint: `./scripts/qml-lint.sh` sekarang keluar dengan 0
  peringatan. Kalau muncul peringatan baru, perbaiki atau matikan secara sempit dengan komentar
  alasan di tempat (lihat `MpvVideoSurface.qml` dan `.qmllint.ini` sebagai contoh).
- Jangan hapus fitur tema/i18n yang sudah ada; perbaiki, bukan matikan.
- Tool yang **tidak** tersedia: `linuxdeploy`, `flatpak-builder`, `dpkg-deb`, `sudo`.
- `qt_generate_deploy_qml_app_script` **sengaja** tidak dipakai (gagal di Qt 6.12 + CMake 4.4).
- Qt bin tools di `/usr/lib/qt6/bin`.

## 8. Definition of done

- Bug UI diperbaiki; light & dark konsisten; menu tidak terpotong.
- `cmake --build build` bersih; smoke offscreen tanpa error QML; `./scripts/qml-lint.sh` exit 0.
- `ctest` 12/12 lulus.
- Ringkas temuan + langkah reproduksi + bukti (screenshot /tmp) di laporan.
- Update `docs/M4-REPORT.md` §10 (open items) bila ada item yang tertutup.

## 9. Dokumen rujukan

- `docs/M4-REPORT.md` — tema, i18n, HiDPI, software-render, a11y, packaging, benchmark.
- `docs/M2-REPORT.md` — fitur Organize & Filter + verifikasi (rename/move/undo, watcher).
- `docs/USER-GUIDE.md` — panduan pengguna (ID).
- `docs/TECH-DEBT-REPORT.md` §10 — utang teknis tersisa.
- `PRD.md` / `README.md` — kebutuhan & status.
