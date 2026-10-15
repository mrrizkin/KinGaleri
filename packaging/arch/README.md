# AUR: `kin-galeri-git`

`PKGBUILD` here is the AUR package, and it is **not published to the AUR**: account
registration there is currently closed while they deal with a flood of automated
sign-ups, so the package cannot be created at all right now. That is not a defect
here, and nothing in this directory has to change when registration reopens.

Arch users are not blocked in the meantime - the package ships as
`kin-galeri-git-<version>-x86_64.pkg.tar.zst` on the release page:

```bash
pacman -U kin-galeri-git-*.pkg.tar.zst
```

or build it here with `makepkg -si`.

It is a `-git` package because it tracks the moving `main` branch. `pkgver()`
derives the version with `git describe`, so a build from a tagged commit is exactly
that version and one between tags gets the `.r<count>.g<sha>` suffix Arch expects.

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

**Blocked until AUR registration reopens.** When it does, the only prerequisite is
an account; the steps below have not been executed yet and are the whole job.

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
