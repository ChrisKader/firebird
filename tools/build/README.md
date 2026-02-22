# Build Bootstrap

Cross-platform dependency bootstrap script:

`tools/build/bootstrap.py`

Project convention: keep build outputs under `.build/` (single root).

## Usage

Check only:

```bash
python3 tools/build/bootstrap.py --target desktop --check-only
python3 tools/build/bootstrap.py --target android --check-only
python3 tools/build/bootstrap.py --target ios --check-only
python3 tools/build/bootstrap.py --target web --check-only
python3 tools/build/bootstrap.py --target webqt --check-only
python3 tools/build/bootstrap.py --target all --check-only
```

Desktop with KDDockWidgets bootstrap (matches default CMake desktop config):

```bash
python3 tools/build/bootstrap.py --target desktop --bootstrap-kddockwidgets --yes
```

Install missing dependencies (best effort):

```bash
python3 tools/build/bootstrap.py --target all --yes
```

Dry-run install commands:

```bash
python3 tools/build/bootstrap.py --target all --dry-run --yes
```

Notes:

- Uses native package managers when available: `apt`, `dnf`, `pacman`, `zypper`, `brew`, `choco`, `winget`.
- Installs Qt mobile kits into `.build/qt` for `--target all` and can install Android NDK via `sdkmanager`.
- If a dependency still cannot be installed automatically, the script prints a host-specific hint.
- CMake meta-build driver (bootstrap remains separate):

```bash
cmake -S . -B .build/meta -DFIREBIRD_META_BUILD=ON
cmake --build .build/meta --target firebird-build-all --parallel
```

- Host matrix for `--target all`:
  - macOS: `macos`, `linux`, `windows`, `android`, `ios`, `web`, `webqt`
  - Linux: `linux`, `windows`, `android`, `web`, `webqt`
  - Windows: `windows`, `linux`, `android`, `web`, `webqt`

- Linux/Windows desktop cross-builds use local Docker builder images.
  Bootstrap builds these images automatically for `--target all`, or explicitly:

```bash
python3 tools/build/bootstrap.py --target all --bootstrap-docker-builders --yes
```

  Docker must be installed and running (`docker info` must succeed).

- Select specific platform builds via CMake:

```bash
cmake -S . -B .build/meta -DFIREBIRD_META_BUILD=ON -DFIREBIRD_TARGETS="desktop;web"
cmake --build .build/meta --target firebird-build-all --parallel
```

- Qt WebAssembly + KDDockWidgets build:

```bash
cmake -S . -B .build/meta -DFIREBIRD_META_BUILD=ON -DFIREBIRD_TARGETS=webqt
cmake --build .build/meta --target firebird-build-web-qt --parallel
```

- Build + serve Web Qt locally (sets EMSDK/EM_CONFIG and starts a local server with
  `Cross-Origin-Opener-Policy: same-origin` and
  `Cross-Origin-Embedder-Policy: require-corp` so pthread/SharedArrayBuffer works):

```bash
python3 tools/build/serve_webqt.py --port 8080
```
- Optional vcpkg bootstrap:

```bash
python3 tools/build/bootstrap.py --target desktop --with-vcpkg --bootstrap-vcpkg --yes
```

- Optional KDDockWidgets bootstrap (Qt6 backend used by default desktop build):

```bash
python3 tools/build/bootstrap.py --target desktop --bootstrap-kddockwidgets --yes
```

- Optional CI fallback in meta-build (off by default):

```bash
cmake -S . -B .build/meta -DFIREBIRD_META_BUILD=ON -DFIREBIRD_ENABLE_CI_FALLBACK=ON
```

- Use with CMake:

```bash
cmake -S . -B .build/desktop-vcpkg \
  -DCMAKE_TOOLCHAIN_FILE=.build/vcpkg/scripts/buildsystems/vcpkg.cmake
```
