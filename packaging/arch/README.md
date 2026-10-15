# AUR: `kin-galeri-git`

`PKGBUILD` here is the AUR package. It is a `-git` package because the project has
no release tags yet, so a versioned PKGBUILD would have nothing to point at;
`pkgver()` derives a monotonically increasing version from the commit count.

The package is verified in CI: the `arch` job in
[`package.yml`](../../.github/workflows/package.yml) builds it in an
`archlinux:base-devel` container as a non-root user, which is the only way
`makepkg` runs.

## Build it locally first

```bash
cd packaging/arch
makepkg -si          # -s pulls makedepends, -i installs
```

`check()` runs the same `ctest` suite as everywhere else, so a broken commit fails
the package build rather than shipping.

## Publishing to the AUR

One-time setup:

1. Create an account at <https://aur.archlinux.org> and add your SSH public key
   under *My Account → SSH Public Key*.
2. Clone the (empty) package repository:

   ```bash
   git clone ssh://aur@aur.archlinux.org/kin-galeri-git.git
   cd kin-galeri-git
   ```

Then for every release:

```bash
cp /path/to/KinGaleri/packaging/arch/PKGBUILD .
makepkg --printsrcinfo > .SRCINFO     # required, and easy to forget
git add PKGBUILD .SRCINFO
git commit -m "kin-galeri-git: $(grep ^pkgver PKGBUILD)"
git push
```

`.SRCINFO` must be regenerated whenever `PKGBUILD` changes — the AUR does not
derive it, and a stale one makes the package show the wrong metadata.

## Dependencies

* `mpvqt` is a `makedepend`: Arch packages it, and when it is present CMake
  enables the mpv backend automatically. Users who want it already get it from
  the `mpv` optdepend.
* `libheif` is an `optdepend` because it supplies the external `heif-convert`
  binary used as the HEIC/HEIF fallback. Without it, HEIC images are skipped
  rather than crashing.
