# Contributing to Kin Galeri

Thanks for taking the time to contribute! Kin Galeri is free software under the
[GPL-3.0-or-later](LICENSE), and contributions of every size are welcome:
bug reports, translations, documentation, and code.

By participating you agree that your contributions are licensed under the same
license as the project (inbound = outbound).

## Reporting bugs

Open an issue at <https://github.com/mrrizkin/KinGaleri/issues> and include:

- what you did, what you expected, and what actually happened;
- your distro and Qt version (`qmake6 -query QT_VERSION` or the About panel);
- the video backend in use (libmpv or Qt Multimedia);
- the relevant log output. Start the app from a terminal to see it.

If the bug involves a specific file, mention its type (not the private photo
itself). Please never attach personal media you do not want public.

## Building and running

The full build instructions live in [README.md](README.md#build). In short:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
./build/kin-galeri
```

Optional features (for example the libmpv backend) are controlled by the
`KG_*` CMake options documented in the README.

## Tests

Every change is expected to keep the test suite green:

```bash
cmake -S . -B build-tests -G Ninja -DKG_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-tests
cd build-tests && QT_QPA_PLATFORM=offscreen ctest
```

Add a test next to the existing ones in `tests/` when you fix a bug or add
behaviour that can be checked without a display. New settings must be covered in
`tst_appsettings.cpp`.

## Code style

- C++20, Qt 6 conventions. Match the style of the file you are editing.
- 4-space indentation, braces on their own line for functions/classes.
- Prefer `QStringLiteral`/`QLatin1String` and keep hot loops allocation-light.
- User-visible strings must be wrapped in `qsTr()` and added to
  `resources/i18n/kin-galeri_id.ts` (edit the `.ts` **by hand** or with
  `lconvert`; do not run the `update_translations` target, the local `lupdate`
  lacks QML support).
- Keep commits focused and explain the *why* in the message.

## Pull requests

1. Fork the repository and create a branch from `main`.
2. Make your change with tests where practical.
3. Run the test suite.
4. Open a pull request describing the problem and the approach.

Small, self-contained PRs are reviewed fastest. If you plan a large
refactor, open an issue first so we can agree on the direction.
