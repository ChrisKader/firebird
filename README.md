Firebird Emu
==========

![CI (Linux)](https://github.com/nspire-emus/firebird/actions/workflows/main.yml/badge.svg?branch=master) ![Build for Windows](https://github.com/nspire-emus/firebird/actions/workflows/windows.yml/badge.svg?branch=master) ![Build macOS](https://github.com/nspire-emus/firebird/actions/workflows/macOS.yml/badge.svg?branch=master) ![Build for iOS](https://github.com/nspire-emus/firebird/actions/workflows/ios.yml/badge.svg?branch=master) ![Build for Android](https://github.com/nspire-emus/firebird/actions/workflows/android.yml/badge.svg?branch=master) ![Build for Web](https://github.com/nspire-emus/firebird/actions/workflows/web.yml/badge.svg?branch=master) [![build result](https://build.opensuse.org/projects/home:Vogtinator:firebird-emu:unstable/packages/firebird-emu/badge.svg?type=percent)](https://build.opensuse.org/package/show/home:Vogtinator:firebird-emu:unstable/firebird-emu)

This project is currently the community TI-Nspire emulator, originally created by Goplat.  
It supports the emulation of Nspire Touchpad (CAS), CX (CAS) and CX II (-T/CAS) calcs on Android, iOS, Linux, macOS and Windows.

Download:
---------

* [Latest release](https://github.com/nspire-emus/firebird/releases/latest)

Documentation:
--------------

* [CX II PMIC/ADC reverse-engineering notes](docs/cx2-pmic-register-map.md)
* [Project structure and engineering guide](docs/PROJECT_STRUCTURE.md)
* [Rule reference map (source-backed)](docs/REFERENCE_MAP.md)

Screenshots:
-------------------

_Linux:_

[![](http://i.imgur.com/eGJOMsSl.png)](http://i.imgur.com/eGJOMsS.png)

_Windows:_ | _Android:_
--- | ---
[![](https://i.imgur.com/aibTt9Cl.png)](https://i.imgur.com/aibTt9C.png) | [![](https://i.imgur.com/cLphTgnm.png)](https://i.imgur.com/cLphTgn.png)
_macOS:_ | _iOS:_
[![](https://i.imgur.com/ymDtYsj.png)](https://i.imgur.com/O8R2aSo.png) | [![](https://i.imgur.com/LT1u2bim.png)](https://i.imgur.com/LT1u2bi.png)

Building
--------

First, install Qt 6 and CMake (desktop build).  
Then run:

```
python3 tools/build/bootstrap.py --target desktop --bootstrap-kddockwidgets --yes
cmake -S . -B .build/desktop
cmake --build .build/desktop -j8
```

Single CMake driver (all platforms from one entrypoint):

```bash
python3 tools/build/bootstrap.py --target all --yes
cmake -S . -B .build/meta -DFIREBIRD_META_BUILD=ON
cmake --build .build/meta --target firebird-build-all --parallel
```

Local host matrix:

1. macOS: `macos`, `linux`, `windows`, `android`, `ios`, `web`
2. Linux: `linux`, `windows`, `android`, `web`
3. Windows: `windows`, `linux`, `android`, `web`

For Linux/Windows desktop cross-builds, bootstrap prepares local Docker builder images.
Docker must be installed and running for those cross desktop builds.
Android local builds also require an Android NDK; bootstrap installs one via `sdkmanager` and reports `ANDROID_NDK_ROOT` status.

Build specific platforms only:

```bash
cmake -S . -B .build/meta -DFIREBIRD_META_BUILD=ON -DFIREBIRD_TARGETS="desktop;web"
cmake --build .build/meta --target firebird-build-all --parallel
```

Notes:

1. Build artifacts are expected under `.build/` (single root build folder).
2. Desktop default build enables the KDDockWidgets backend.
3. If KDDockWidgets is not present yet, bootstrap it to `.build/deps/kddockwidgets-2.4`:
   `python3 tools/build/bootstrap.py --target desktop --bootstrap-kddockwidgets --yes`
4. If your local build dir was configured before this default changed, rerun configure with:
   `cmake -S . -B .build/desktop -DFIREBIRD_ENABLE_KDDOCKWIDGETS=ON`
5. Optional vcpkg toolchain:
   `python3 tools/build/bootstrap.py --target desktop --with-vcpkg --bootstrap-vcpkg --yes`
   then configure with
   `-DCMAKE_TOOLCHAIN_FILE=.build/vcpkg/scripts/buildsystems/vcpkg.cmake`
6. Optional CI fallback (disabled by default in meta-build):
   `-DFIREBIRD_ENABLE_CI_FALLBACK=ON`

Platform-specific CI paths:

1. Linux: `.github/workflows/main.yml`
2. Windows: `.github/workflows/windows.yml`
3. macOS: `.github/workflows/macOS.yml`
4. iOS: `.github/workflows/ios.yml`
5. Android: `.github/workflows/android.yml`
6. Web (Emscripten): `.github/workflows/web.yml`

Bootstrap helper:

1. Dependency checker/installer: `tools/build/bootstrap.py`
2. Usage: `tools/build/README.md`


License
-------
This work (except the icons from the KDE project) is licensed under the GPLv3.
