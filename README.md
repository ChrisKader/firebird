Firebird Emu
==========

![Build for Android](https://github.com/nspire-emus/firebird/actions/workflows/android.yml/badge.svg?branch=master) ![Build for macOS](https://github.com/nspire-emus/firebird/actions/workflows/macOS.yml/badge.svg?branch=master) ![Build for Windows](https://github.com/nspire-emus/firebird/actions/workflows/windows.yml/badge.svg?branch=master) [![build result](https://build.opensuse.org/projects/home:Vogtinator:firebird-emu:unstable/packages/firebird-emu/badge.svg?type=percent)](https://build.opensuse.org/package/show/home:Vogtinator:firebird-emu:unstable/firebird-emu)

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
cmake -S . -B build
cmake --build build -j8
```

Notes:

1. Desktop default build enables the KDDockWidgets backend.
2. If your local `build/` dir was configured before this default changed, rerun configure with:
   `cmake -S . -B build -DFIREBIRD_ENABLE_KDDOCKWIDGETS=ON`


License
-------
This work (except the icons from the KDE project) is licensed under the GPLv3.
