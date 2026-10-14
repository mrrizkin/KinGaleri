# HANDOFF — UI Bug Hunt for Kin Galeri (untuk AI agent multimodal)

> Dokumen ini untuk agent berikutnya yang **bisa membaca gambar**. Tugas Anda: menjalankan
> aplikasi, mengambil screenshot sendiri, menemukan & memperbaiki bug UI pada QML, lalu
> memverifikasi ulang. Tidak ada screenshot di repo ini secara sengaja — ambil sendiri.

---

## 0. Ringkasan proyek

- **Nama**: Kin Galeri (klon Immich lokal-first untuk Linux).
- **Path**: `/home/mrrizkin/Projects/work/oss/gallery` — **bukan git repo**.
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

1. **Tema dari config kadang tidak diterapkan pada cold start.** Teramati: config
   `theme=light` tetapi UI render **dark**, sementara menu ⚙ mencentang "Theme: Light";
   setelah relaunch, UI jadi light. Kandidat: `Binding { target: Theme; property: "dark";
   value: window.app.settings.dark }` di `src/ui/qml/Main.qml` (sekitar baris 35) di-evaluasi
   sebelum `AppSettings` siap. Coba reproduksi dengan me-restart beberapa kali
   (`XDG_CONFIG_HOME=/tmp/kgm4cfg`, tema light dan dark). Kalau perlu ganti ke
   `Component.onCompleted` + `Connections { target: app.settings; function onDarkChanged() {...} }`
   atau `Q_PROPERTY` yang push nilai ke singleton.
2. **Teks menu terpotong**: item terakhir menu ⚙ tampil `Language: Bahasa Indo…`.
   Menu terlalu sempit. Perbaiki di `settingsMenu` pada `src/ui/qml/Main.qml`.
3. **Menu ⚙ nyaris keluar tepi kanan jendela** (dibuka dari tombol paling kanan).
   Cek apakah ada beberapa piksel overflow / terpotong oleh tepi window.
4. **Konsistensi tema terang**: pastikan semua elemen ikut palet terang — menu, semua Dialog
   (rename/move/album picker/error), selection bar, scrollbar, `ViewerOverlay`, `PerfOverlay`,
   `MpvVideoSurface`, `Sidebar`.
5. **i18n belum menyeluruh**: string yang dibangun di **C++** masih Inggris, mis. ringkasan
   `5 items · 508 B`, footer `watching N folders`, `Ready`, pesan error/undo. Alihkan ke
   `tr()` + tambahkan ke `resources/i18n/kin-galeri_id.ts` (124 pesan saat ini; konteks =
   basename file QML + C++). Verifikasi `:/i18n/kin-galeri_id.qm` ikut ter-embed.
6. **HiDPI/fractional scaling** belum diverifikasi visual. Uji:
   ```bash
   QT_SCALE_FACTOR=1.25 DISPLAY=:0.0 ./build/kin-galeri ...   # lalu 1.5, 2.0
   ```
   Cari teks terpotong / kontrol tumpang tindih / ikon blur.
7. **`--software-render`** (`QT_QUICK_BACKEND=software`) belum diuji visual — jalankan dan
   cek tidak ada artefak render.
8. **Aksesibilitas** hanya bisa dicek dengan inspector (Accerciser/AT-SPI), bukan visual.

## 5. Peta file UI

```
src/ui/qml/
  Main.qml            # window, header, settingsMenu, dialog rename/move/error, selectionBar, shortcut
  Theme.qml           # SINGLETON (QT_QML_SINGLETON_TYPE) — seluruh palet dari `property bool dark`
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

# Unit test (7 target, harus 7/7 pass)
cmake -S . -B build-tests -G Ninja -DKG_BUILD_TESTS=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure

# Lint QML
/usr/lib/qt6/bin/qmllint src/ui/qml/*.qml
```

## 7. Batasan / jangan disentuh

- **Jangan** mengubah handling video-EOF di `src/ui/quick/KinMpvItem.{h,cpp}` +
  `MpvVideoSurface.qml` — sudah selesai & terverifikasi (lihat `docs/M2-REPORT.md` §6).
- Jangan regresi 7 unit test (`build-tests`).
- Jangan hapus fitur tema/i18n yang sudah ada; perbaiki, bukan matikan.
- Tool yang **tidak** tersedia: `linuxdeploy`, `flatpak-builder`, `dpkg-deb`, `sudo`.
- `qt_generate_deploy_qml_app_script` **sengaja** tidak dipakai (gagal di Qt 6.12 + CMake 4.4).
- Qt bin tools di `/usr/lib/qt6/bin`.

## 8. Definition of done

- Bug UI diperbaiki; light & dark konsisten; menu tidak terpotong.
- `cmake --build build` bersih; smoke offscreen tanpa error QML.
- `ctest` 7/7 lulus.
- Ringkas temuan + langkah reproduksi + bukti (screenshot /tmp) di laporan.
- Update `docs/M4-REPORT.md` §10 (open items) bila ada item yang tertutup.

## 9. Dokumen rujukan

- `docs/M4-REPORT.md` — tema, i18n, HiDPI, software-render, a11y, packaging, benchmark.
- `docs/M2-REPORT.md` — fitur Organize & Filter + verifikasi (rename/move/undo, watcher).
- `docs/USER-GUIDE.md` — panduan pengguna (ID).
- `docs/TECH-DEBT-REPORT.md` §10 — utang teknis tersisa.
- `PRD.md` / `README.md` — kebutuhan & status.
