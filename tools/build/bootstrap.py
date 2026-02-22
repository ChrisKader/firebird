#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import platform
from pathlib import Path
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from typing import Callable, Dict, List, Sequence, Tuple


CheckFn = Callable[[], bool]
KDDOCKWIDGETS_VERSION = "2.4.0"
QT_VERSION = "6.10.1"
QT_WASM_VERSION = os.environ.get("FIREBIRD_QT_WASM_VERSION", "")
QT_AQT_BASE = os.environ.get("FIREBIRD_QT_AQT_BASE", "")
EMSDK_VERSION = "4.0.7"
ANDROID_NDK_PACKAGE = "ndk;29.0.14206865"
QT_WASM_ARCHES = ("wasm_multithread", "wasm_singlethread")
DEFAULT_AQT_BASE_CANDIDATES = (
    "https://download.qt.io",
    "https://mirrors.dotsrc.org/qtproject",
    "https://ftp2.nluug.nl/languages/qt",
)
COMMON_REQUIREMENT_KEYS = ("git", "cmake")
DOCKER_LINUX_IMAGE = "firebird-builder-linux:qt6.10.1"
DOCKER_WINDOWS_IMAGE = "firebird-builder-windows:qt6.10.1"


def run_capture(cmd: Sequence[str], env: Dict[str, str] | None = None) -> str:
    try:
        result = subprocess.run(
            list(cmd),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
            env=env,
        )
    except (FileNotFoundError, OSError):
        return ""
    output = result.stdout.strip()
    if not output:
        return ""
    return output.splitlines()[0].strip()


def semver_key(version: str) -> Tuple[int, int, int]:
    match = re.match(r"^(\d+)\.(\d+)\.(\d+)$", version)
    if not match:
        return (0, 0, 0)
    return (int(match.group(1)), int(match.group(2)), int(match.group(3)))


def extract_semver_tokens(text: str) -> List[str]:
    seen = set()
    versions: List[str] = []
    for token in re.findall(r"\b\d+\.\d+\.\d+\b", text):
        if token in seen:
            continue
        seen.add(token)
        versions.append(token)
    return versions


def aqt_install_qt_cmd(*positional: str, base: str = "") -> Tuple[str, ...]:
    cmd: List[str] = [sys.executable, "-m", "aqt", "install-qt"]
    selected_base = base or QT_AQT_BASE
    if selected_base:
        cmd.extend(["-b", selected_base])
    cmd.extend(positional)
    return tuple(cmd)


def aqt_list_qt_cmd(host: str, target: str, *extra_args: str, base: str = "") -> Tuple[str, ...]:
    cmd: List[str] = [sys.executable, "-m", "aqt", "list-qt"]
    selected_base = base or QT_AQT_BASE
    if selected_base:
        cmd.extend(["-b", selected_base])
    cmd.extend([host, target])
    cmd.extend(extra_args)
    return tuple(cmd)


def aqt_base_candidates() -> List[str]:
    candidates: List[str] = []
    if QT_AQT_BASE:
        candidates.append(QT_AQT_BASE)
    for base in DEFAULT_AQT_BASE_CANDIDATES:
        if base not in candidates:
            candidates.append(base)
    return candidates


def select_working_aqt_base(dry_run: bool) -> str:
    if dry_run:
        return QT_AQT_BASE
    for base in aqt_base_candidates():
        try:
            result = subprocess.run(
                aqt_list_qt_cmd("all_os", "wasm", "--latest-version", base=base),
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                check=False,
            )
        except (FileNotFoundError, OSError):
            continue
        if result.returncode == 0:
            return base
    return QT_AQT_BASE


def which_any(names: Sequence[str]) -> bool:
    return any(shutil.which(name) for name in names)


def run(cmd: Sequence[str], dry_run: bool, cwd: str | None = None, env: Dict[str, str] | None = None) -> int:
    if cwd:
        print("+", f"(cd {cwd} && {' '.join(cmd)})")
    else:
        print("+", " ".join(cmd))
    if dry_run:
        return 0
    return subprocess.call(list(cmd), cwd=cwd, env=env)


def in_virtualenv() -> bool:
    return bool(getattr(sys, "real_prefix", None)) or (hasattr(sys, "base_prefix") and sys.prefix != sys.base_prefix)


def detect_package_manager(system_name: str) -> str:
    if system_name == "Darwin":
        return "brew" if shutil.which("brew") else ""
    if system_name == "Linux":
        for pm in ("apt", "dnf", "pacman", "zypper"):
            probe = {"apt": "apt-get", "dnf": "dnf", "pacman": "pacman", "zypper": "zypper"}[pm]
            if shutil.which(probe):
                return pm
        return ""
    if system_name == "Windows":
        if shutil.which("choco"):
            return "choco"
        if shutil.which("winget"):
            return "winget"
        return ""
    return ""


def check_c_compiler() -> bool:
    if platform.system() == "Windows":
        return which_any(("cl", "clang-cl", "gcc"))
    return which_any(("cc", "gcc", "clang")) and which_any(("c++", "g++", "clang++"))


def check_qt() -> bool:
    return which_any(("qt-cmake", "qmake", "qmake6")) or local_qt_tool_exists(("qt-cmake", "qmake", "qmake6"))


def qmake_xspec() -> str:
    for qmake_name in ("qmake", "qmake6"):
        qmake_path = shutil.which(qmake_name)
        if not qmake_path:
            continue
        return run_capture((qmake_path, "-query", "QMAKE_XSPEC")).lower()
    return ""


def check_qt_android_tools() -> bool:
    if local_qt_kit_exists("android_arm64_v8a"):
        return local_qt_host_tools_ok()
    if not (which_any(("androiddeployqt",)) or local_qt_tool_exists(("androiddeployqt",))):
        return False
    return "android" in qmake_xspec()


def check_qt_ios_tools() -> bool:
    if platform.system() != "Darwin":
        return False
    if local_qt_kit_exists("ios"):
        return local_qt_host_tools_ok()
    return "ios" in qmake_xspec()


def check_android_tools() -> bool:
    if which_any(("sdkmanager", "adb")):
        return True
    for sdk_root in android_sdk_roots():
        if android_sdk_has_tools(sdk_root):
            return True
    return False


def android_ndk_is_valid(ndk_root: Path) -> bool:
    if not ndk_root.exists():
        return False
    prebuilt_root = ndk_root / "toolchains" / "llvm" / "prebuilt"
    if not prebuilt_root.exists():
        return False
    for toolchain_dir in prebuilt_root.iterdir():
        if not toolchain_dir.is_dir():
            continue
        if (toolchain_dir / "bin" / "clang").exists():
            return True
    return False


def detect_android_ndk_root() -> Path | None:
    env_ndk = os.environ.get("ANDROID_NDK_ROOT") or os.environ.get("ANDROID_NDK_HOME")
    if env_ndk:
        ndk_path = Path(env_ndk)
        if android_ndk_is_valid(ndk_path):
            return ndk_path

    for sdk_root in android_sdk_roots():
        ndk_base = sdk_root / "ndk"
        if not ndk_base.exists():
            continue
        candidates = sorted([path for path in ndk_base.iterdir() if path.is_dir()], reverse=True)
        for candidate in candidates:
            if android_ndk_is_valid(candidate):
                return candidate
    return None


def check_android_ndk() -> bool:
    return detect_android_ndk_root() is not None


def check_ios_tools() -> bool:
    if platform.system() != "Darwin":
        return False
    return which_any(("xcodebuild", "xcrun"))


def check_ios_simulator_runtime() -> bool:
    if platform.system() != "Darwin" or not which_any(("xcrun",)):
        return False
    try:
        result = subprocess.run(
            ("xcrun", "simctl", "list", "runtimes"),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
        )
    except (FileNotFoundError, OSError):
        return False
    if result.returncode != 0:
        return False
    for line in result.stdout.splitlines():
        lower = line.lower()
        if "ios" in lower and "unavailable" not in lower:
            return True
    return False


def local_vcpkg_path() -> Path:
    exe = "vcpkg.exe" if platform.system() == "Windows" else "vcpkg"
    return Path(".build") / "vcpkg" / exe


def local_qt_root() -> Path:
    return Path(".build") / "qt"


def local_qt_tool_exists(tool_names: Sequence[str]) -> bool:
    qt_root = local_qt_root()
    if not qt_root.exists():
        return False
    suffixes = ("", ".exe", ".bat", ".cmd")
    for tool in tool_names:
        for suffix in suffixes:
            if any(qt_root.glob(f"**/bin/{tool}{suffix}")):
                return True
    return False


def local_qt_kit_exists(arch: str, allow_any_version: bool = False) -> bool:
    if arch.startswith("wasm"):
        qt_root = local_qt_root()
        if not qt_root.exists():
            return False
        if (qt_root / QT_VERSION / arch / "bin" / "qt-cmake").is_file() or (
            qt_root / QT_VERSION / arch / "bin" / "qt-cmake.bat"
        ).is_file():
            return True
        if not allow_any_version:
            return False
        return any(qt_root.glob(f"*/{arch}/bin/qt-cmake")) or any(qt_root.glob(f"*/{arch}/bin/qt-cmake.bat"))
    return local_qmake_for_arch(arch) is not None


def local_qt_host_tools_ok() -> bool:
    if platform.system() != "Darwin":
        return True
    libexec_dir = local_qt_root() / QT_VERSION / "macos" / "libexec"
    if not libexec_dir.exists():
        return False
    for tool in ("rcc", "uic", "moc"):
        tool_path = libexec_dir / tool
        if not tool_path.exists():
            return False
        try:
            result = subprocess.run(
                (str(tool_path), "-v"),
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                check=False,
            )
        except (FileNotFoundError, OSError):
            return False
        if result.returncode != 0:
            return False
        if tool == "moc":
            moc_line = run_capture((str(tool_path), "-v"))
            match = re.search(r"([0-9]+\.[0-9]+\.[0-9]+)", moc_line)
            if match and match.group(1) != QT_VERSION:
                return False
    return True


def local_qmake_for_arch(arch: str, allow_any_version: bool = False) -> Path | None:
    qt_root = local_qt_root()
    if not qt_root.exists():
        return None
    expected_bin = qt_root / QT_VERSION / arch / "bin"
    for name in ("qmake6", "qmake"):
        candidate = expected_bin / name
        if candidate.is_file():
            return candidate
    if not allow_any_version:
        return None
    for candidate in qt_root.glob(f"**/{arch}/bin/qmake*"):
        if candidate.is_file() and QT_VERSION not in str(candidate):
            return candidate
    return None


def local_qt_web_arch() -> str:
    for arch in QT_WASM_ARCHES:
        if local_qt_kit_exists(arch, allow_any_version=True):
            return arch
    return ""


def local_qt_web_info() -> Tuple[str, str, Path] | None:
    qt_root = local_qt_root()
    if not qt_root.exists():
        return None

    # Prefer the pinned Qt version if it exists.
    for arch in QT_WASM_ARCHES:
        candidate = qt_root / QT_VERSION / arch / "bin" / "qt-cmake"
        if candidate.is_file():
            return (QT_VERSION, arch, candidate)
        candidate_bat = qt_root / QT_VERSION / arch / "bin" / "qt-cmake.bat"
        if candidate_bat.is_file():
            return (QT_VERSION, arch, candidate_bat)

    # Otherwise use the newest discovered wasm kit.
    discovered: List[Tuple[Tuple[int, int, int], int, str, str, Path]] = []
    for arch_index, arch in enumerate(QT_WASM_ARCHES):
        for pattern in (f"*/{arch}/bin/qt-cmake", f"*/{arch}/bin/qt-cmake.bat"):
            for candidate in qt_root.glob(pattern):
                version = candidate.parents[2].name if len(candidate.parents) >= 3 else ""
                discovered.append((semver_key(version), -arch_index, version, arch, candidate))
    if not discovered:
        return None
    discovered.sort(reverse=True)
    _, _, version, arch, candidate = discovered[0]
    return (version, arch, candidate)


def local_qt_web_cmake() -> Path | None:
    info = local_qt_web_info()
    if not info:
        return None
    return info[2]


def local_qt_host_path(version: str = QT_VERSION) -> Path | None:
    qt_root = local_qt_root()
    if not qt_root.exists():
        return None
    candidates = [
        qt_root / version / "macos",
        qt_root / version / "gcc_64",
        qt_root / version / "clang_64",
        qt_root / version / "win64_msvc2022_64",
        qt_root / version / "win64_mingw",
        qt_root / version / "mingw_64",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate.resolve()
    return None


def qt_wasm_recommended_emsdk_version() -> str:
    info = local_qt_web_info()
    if not info:
        return EMSDK_VERSION
    qt_wasm_version, qt_wasm_arch, _ = info
    helper = local_qt_root() / qt_wasm_version / qt_wasm_arch / "lib" / "cmake" / "Qt6" / "QtPublicWasmToolchainHelpers.cmake"
    if not helper.exists():
        return EMSDK_VERSION
    try:
        content = helper.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return EMSDK_VERSION
    match = re.search(r'QT_EMCC_RECOMMENDED_VERSION\s+"([^"]+)"', content)
    if match:
        return match.group(1)
    return EMSDK_VERSION


def qt_wasm_version_candidates(aqt_base: str = "") -> List[str]:
    candidates: List[str] = []
    if QT_WASM_VERSION:
        candidates.append(QT_WASM_VERSION)
    if QT_VERSION not in candidates:
        candidates.append(QT_VERSION)

    try:
        result = subprocess.run(
            aqt_list_qt_cmd("all_os", "wasm", base=aqt_base),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
        )
    except (FileNotFoundError, OSError):
        return candidates
    if result.returncode != 0:
        return candidates

    available = extract_semver_tokens(result.stdout)
    if not available:
        return candidates
    pinned_minor = semver_key(QT_VERSION)[:2]
    same_minor = [version for version in available if semver_key(version)[:2] == pinned_minor]
    if not same_minor:
        return candidates
    latest_patch = max(same_minor, key=semver_key)
    if latest_patch not in candidates:
        candidates.append(latest_patch)
    return candidates


def check_qt_web_tools() -> bool:
    return local_qt_web_cmake() is not None


def local_qmake_version(qmake_path: Path) -> str:
    try:
        return qmake_path.parents[2].name
    except IndexError:
        return ""


def local_emsdk_root() -> Path:
    return Path(".build") / "emsdk"


def local_emsdk_emcc_path() -> Path:
    exe = "emcc.bat" if platform.system() == "Windows" else "emcc"
    return local_emsdk_root() / "upstream" / "emscripten" / exe


def detect_emsdk_root() -> Path | None:
    candidates: List[Path] = []
    env_root = os.environ.get("EMSDK")
    if env_root:
        candidates.append(Path(env_root))
    candidates.append(local_emsdk_root())

    emcc_path = shutil.which("emcc")
    if emcc_path:
        resolved = Path(emcc_path).resolve()
        parts = resolved.parts
        if "upstream" in parts and "emscripten" in parts:
            try:
                upstream_index = parts.index("upstream")
                if upstream_index > 0:
                    candidates.append(Path(*parts[:upstream_index]))
            except ValueError:
                pass

    seen = set()
    for candidate in candidates:
        key = str(candidate)
        if key in seen:
            continue
        seen.add(key)
        emcc_candidate = candidate / "upstream" / "emscripten" / ("emcc.bat" if platform.system() == "Windows" else "emcc")
        emconfig_candidate = candidate / ".emscripten"
        if emcc_candidate.exists() and emconfig_candidate.exists():
            return candidate.resolve()
    return None


def emsdk_emcc_path(emsdk_root: Path) -> Path | None:
    candidates = [
        emsdk_root / "upstream" / "emscripten" / ("emcc.bat" if platform.system() == "Windows" else "emcc"),
        emsdk_root / "upstream" / "emscripten" / "emcc",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def emsdk_version(emsdk_root: Path) -> str:
    emcc = emsdk_emcc_path(emsdk_root)
    if not emcc:
        return ""
    out = run_capture((str(emcc), "--version"))
    match = re.search(r"(\d+\.\d+\.\d+)", out)
    if match:
        return match.group(1)
    return ""


def check_emsdk_layout() -> bool:
    emsdk_root = detect_emsdk_root()
    if not emsdk_root:
        return False
    required = qt_wasm_recommended_emsdk_version()
    current = emsdk_version(emsdk_root)
    if required and current and required != current:
        return False
    return True


def check_emscripten() -> bool:
    return which_any(("emcc",)) or local_emsdk_emcc_path().exists()


def detect_emscripten_toolchain_file() -> Path | None:
    candidates: List[Path] = []
    emsdk_root = detect_emsdk_root()
    if emsdk_root:
        candidates.append(emsdk_root / "upstream" / "emscripten" / "cmake" / "Modules" / "Platform" / "Emscripten.cmake")
    emsdk_env = os.environ.get("EMSDK")
    if emsdk_env:
        candidates.append(Path(emsdk_env) / "upstream" / "emscripten" / "cmake" / "Modules" / "Platform" / "Emscripten.cmake")
    candidates.append(local_emsdk_root() / "upstream" / "emscripten" / "cmake" / "Modules" / "Platform" / "Emscripten.cmake")

    emcc_path = shutil.which("emcc")
    if emcc_path:
        resolved = Path(emcc_path).resolve()
        emcc_dir = resolved.parent
        candidates.extend(
            [
                emcc_dir / "cmake" / "Modules" / "Platform" / "Emscripten.cmake",
                emcc_dir.parent / "libexec" / "cmake" / "Modules" / "Platform" / "Emscripten.cmake",
                emcc_dir.parent / "upstream" / "emscripten" / "cmake" / "Modules" / "Platform" / "Emscripten.cmake",
            ]
        )

    seen = set()
    for candidate in candidates:
        key = str(candidate)
        if key in seen:
            continue
        seen.add(key)
        if candidate.exists():
            return candidate.resolve()
    return None


def android_sdk_roots() -> List[Path]:
    roots: List[Path] = []
    env_roots = (os.environ.get("ANDROID_SDK_ROOT"), os.environ.get("ANDROID_HOME"))
    for root in env_roots:
        if root:
            roots.append(Path(root))
    roots.extend(
        [
            Path.home() / "Library" / "Android" / "sdk",
            Path.home() / "Android" / "Sdk",
            Path(".build") / "android-sdk",
        ]
    )
    if platform.system() == "Darwin":
        roots.extend(
            [
                Path("/opt/homebrew/share/android-commandlinetools"),
                Path("/usr/local/share/android-commandlinetools"),
            ]
        )

    unique_roots: List[Path] = []
    seen = set()
    for root in roots:
        key = str(root)
        if key in seen:
            continue
        seen.add(key)
        unique_roots.append(root)
    return unique_roots


def android_sdk_has_tools(sdk_root: Path) -> bool:
    adb_names = ("adb", "adb.exe")
    sdkmanager_names = ("sdkmanager", "sdkmanager.bat", "sdkmanager.exe")
    adb_ok = any((sdk_root / "platform-tools" / name).exists() for name in adb_names)
    sdk_ok = any((sdk_root / "cmdline-tools" / "latest" / "bin" / name).exists() for name in sdkmanager_names)
    sdk_ok = sdk_ok or any((sdk_root / "cmdline-tools" / "bin" / name).exists() for name in sdkmanager_names)
    sdk_ok = sdk_ok or any((sdk_root / "tools" / "bin" / name).exists() for name in sdkmanager_names)
    return adb_ok and sdk_ok


def kddockwidgets_prefix_path() -> Path:
    return Path(".build") / "deps" / "kddockwidgets-2.4"


def kddockwidgets_config_path() -> Path:
    return (
        kddockwidgets_prefix_path()
        / "lib"
        / "cmake"
        / "KDDockWidgets-qt6"
        / "KDDockWidgets-qt6Config.cmake"
    )


def check_cmake_package(package_name: str) -> bool:
    if not shutil.which("cmake"):
        return False
    rc = subprocess.call(
        (
            "cmake",
            "--find-package",
            f"-DNAME={package_name}",
            "-DLANGUAGE=CXX",
            "-DMODE=EXIST",
        ),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return rc == 0


def check_kddockwidgets() -> bool:
    if kddockwidgets_config_path().exists():
        return True
    return check_cmake_package("KDDockWidgets-qt6") or check_cmake_package("KDDockWidgets")


def check_vcpkg() -> bool:
    return which_any(("vcpkg",)) or local_vcpkg_path().exists()


def check_docker() -> bool:
    if not which_any(("docker",)):
        return False
    return subprocess.call(("docker", "info"), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) == 0


def check_docker_image(tag: str) -> bool:
    if not which_any(("docker",)):
        return False
    return subprocess.call(("docker", "image", "inspect", tag), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) == 0


def check_linux_desktop_cross_image() -> bool:
    return check_docker_image(DOCKER_LINUX_IMAGE)


def check_windows_desktop_cross_image() -> bool:
    return check_docker_image(DOCKER_WINDOWS_IMAGE)


def get_qt_version() -> str:
    if shutil.which("qmake"):
        version = run_capture(("qmake", "-query", "QT_VERSION"))
        if version:
            return f"Qt {version}"
    if shutil.which("qmake6"):
        version = run_capture(("qmake6", "-query", "QT_VERSION"))
        if version:
            return f"Qt {version}"
    # If only local Qt exists in .build/qt, show the configured baseline.
    if local_qt_root().exists():
        return f"Qt {QT_VERSION} (local .build/qt)"
    return ""


def get_qt_android_version() -> str:
    local_qmake = local_qmake_for_arch("android_arm64_v8a", allow_any_version=True)
    if local_qmake:
        found_version = local_qmake_version(local_qmake) or QT_VERSION
        if found_version != QT_VERSION:
            return f"Qt {found_version} (android_arm64_v8a, expected {QT_VERSION})"
        return f"Qt {found_version} (android_arm64_v8a)"
    if check_qt_android_tools():
        return get_qt_version()
    return ""


def get_qt_ios_version() -> str:
    local_qmake = local_qmake_for_arch("ios", allow_any_version=True)
    if local_qmake:
        found_version = local_qmake_version(local_qmake) or QT_VERSION
        if found_version != QT_VERSION:
            return f"Qt {found_version} (ios, expected {QT_VERSION})"
        return f"Qt {found_version} (ios)"
    if check_qt_ios_tools():
        return get_qt_version()
    return ""


def get_qt_web_version() -> str:
    info = local_qt_web_info()
    if not info:
        return ""
    found_version, arch, _ = info
    if found_version and found_version != QT_VERSION:
        return f"Qt {found_version} ({arch}, expected {QT_VERSION})"
    return f"Qt {found_version or QT_VERSION} ({arch})"


def get_kddockwidgets_version() -> str:
    config_path = kddockwidgets_prefix_path() / "lib" / "cmake" / "KDDockWidgets-qt6" / "KDDockWidgets-qt6ConfigVersion.cmake"
    if config_path.exists():
        try:
            content = config_path.read_text(encoding="utf-8", errors="ignore")
            match = re.search(r'PACKAGE_VERSION\s+"([^"]+)"', content)
            if match:
                return f"KDDockWidgets {match.group(1)} (.build/deps)"
        except OSError:
            pass
        return "KDDockWidgets (.build/deps)"
    if check_cmake_package("KDDockWidgets-qt6") or check_cmake_package("KDDockWidgets"):
        return "KDDockWidgets (system package)"
    return ""


def get_android_tools_version() -> str:
    versions: List[str] = []
    if shutil.which("adb"):
        adb_line = run_capture(("adb", "--version"))
        if adb_line:
            versions.append(adb_line)
    if shutil.which("sdkmanager"):
        sdk_line = run_capture(("sdkmanager", "--version"))
        if sdk_line:
            versions.append(f"sdkmanager {sdk_line}")
    return "; ".join(versions)


def get_android_ndk_version() -> str:
    ndk_root = detect_android_ndk_root()
    if not ndk_root:
        return ""
    version_file = ndk_root / "source.properties"
    if version_file.exists():
        try:
            for line in version_file.read_text(encoding="utf-8", errors="ignore").splitlines():
                if line.startswith("Pkg.Revision="):
                    return f"NDK {line.split('=', 1)[1].strip()}"
        except OSError:
            pass
    return f"NDK {ndk_root.name}"


def get_ios_tools_version() -> str:
    if shutil.which("xcodebuild"):
        line = run_capture(("xcodebuild", "-version"))
        if line:
            return line
    return ""


def get_ios_runtime_version() -> str:
    if platform.system() != "Darwin" or not shutil.which("xcrun"):
        return ""
    try:
        result = subprocess.run(
            ("xcrun", "simctl", "list", "runtimes"),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
        )
    except (FileNotFoundError, OSError):
        return ""
    if result.returncode != 0:
        return ""
    for line in result.stdout.splitlines():
        lower = line.lower()
        if "ios" in lower and "unavailable" not in lower:
            return " ".join(line.split())
    return ""


def get_emcc_version() -> str:
    em_cache = os.environ.copy()
    em_cache.setdefault("EM_CACHE", "/tmp/emscripten-cache")
    return run_capture(("emcc", "--version"), env=em_cache)


def get_emsdk_layout_version() -> str:
    root = detect_emsdk_root()
    if not root:
        return ""
    current = emsdk_version(root)
    required = qt_wasm_recommended_emsdk_version()
    if current and required and current != required:
        return f"EMSDK {root} (version {current}, expected {required})"
    if current:
        return f"EMSDK {root} (version {current})"
    return f"EMSDK {root}"


def get_make_version() -> str:
    if shutil.which("make"):
        return run_capture(("make", "--version"))
    if shutil.which("nmake"):
        return run_capture(("nmake", "/?"))
    return ""


def kddockwidgets_wasm_prefix_path() -> Path:
    return Path(".build") / "deps" / "kddockwidgets-2.4-wasm"


def check_kddockwidgets_wasm() -> bool:
    config_path = (
        kddockwidgets_wasm_prefix_path()
        / "lib"
        / "cmake"
        / "KDDockWidgets-qt6"
        / "KDDockWidgets-qt6Config.cmake"
    )
    return config_path.exists()


def get_kddockwidgets_wasm_version() -> str:
    config_path = (
        kddockwidgets_wasm_prefix_path()
        / "lib"
        / "cmake"
        / "KDDockWidgets-qt6"
        / "KDDockWidgets-qt6ConfigVersion.cmake"
    )
    if config_path.exists():
        try:
            content = config_path.read_text(encoding="utf-8", errors="ignore")
            match = re.search(r'PACKAGE_VERSION\s+"([^"]+)"', content)
            if match:
                return f"KDDockWidgets {match.group(1)} (wasm)"
        except OSError:
            pass
        return "KDDockWidgets (wasm)"
    return ""


VERSION_PROBES: Dict[str, Callable[[], str]] = {
    "git": lambda: run_capture(("git", "--version")),
    "cmake": lambda: run_capture(("cmake", "--version")),
    "ninja": lambda: run_capture(("ninja", "--version")),
    "compiler": lambda: run_capture(("cc", "--version")) or run_capture(("cl",)),
    "qt-desktop": get_qt_version,
    "kddockwidgets": get_kddockwidgets_version,
    "java": lambda: run_capture(("java", "-version")),
    "android-sdk": get_android_tools_version,
    "android-ndk": get_android_ndk_version,
    "qt-android": get_qt_android_version,
    "xcode": get_ios_tools_version,
    "ios-sim-runtime": get_ios_runtime_version,
    "qt-ios": get_qt_ios_version,
    "qt-web": get_qt_web_version,
    "kddockwidgets-web": get_kddockwidgets_wasm_version,
    "emcc": get_emcc_version,
    "emsdk-layout": get_emsdk_layout_version,
    "node": lambda: run_capture(("node", "--version")),
    "make": get_make_version,
    "vcpkg": lambda: run_capture(("vcpkg", "version")) or "local .build/vcpkg",
    "docker": lambda: run_capture(("docker", "--version")),
    "desktop-linux-cross": lambda: DOCKER_LINUX_IMAGE if check_linux_desktop_cross_image() else "",
    "desktop-windows-cross": lambda: DOCKER_WINDOWS_IMAGE if check_windows_desktop_cross_image() else "",
}


def requirement_version(key: str) -> str:
    probe = VERSION_PROBES.get(key)
    if not probe:
        return ""
    return probe()


@dataclass
class Requirement:
    key: str
    label: str
    check: CheckFn
    install_keys: Tuple[str, ...]
    manual_hint: str = ""


REQUIREMENTS: Dict[str, List[Requirement]] = {
    "desktop": [
        Requirement("git", "Git", lambda: which_any(("git",)), ("git",)),
        Requirement("cmake", "CMake", lambda: which_any(("cmake",)), ("cmake",)),
        Requirement("ninja", "Ninja", lambda: which_any(("ninja",)), ("ninja",)),
        Requirement("compiler", "C/C++ compiler toolchain", check_c_compiler, ("build-essential",)),
        Requirement(
            "qt-desktop",
            "Qt 6 desktop tooling",
            check_qt,
            ("qt6-desktop",),
            "Run with --bootstrap-qt to install Qt desktop kit into .build/qt.",
        ),
        Requirement(
            "kddockwidgets",
            "KDDockWidgets 2.4.0 (Qt6)",
            check_kddockwidgets,
            (),
            "Run with --bootstrap-kddockwidgets to install into .build/deps/kddockwidgets-2.4.",
        ),
    ],
    "android": [
        Requirement("java", "Java 17+", lambda: which_any(("java", "javac")), ("openjdk17",)),
        Requirement(
            "android-sdk",
            "Android SDK tools (sdkmanager/adb)",
            check_android_tools,
            ("android-sdk",),
            "Install Android SDK command-line tools and platform-tools.",
        ),
        Requirement(
            "qt-android",
            "Qt Android tooling (androiddeployqt + Qt kit)",
            check_qt_android_tools,
            (),
            "Run with --bootstrap-qt to install Qt Android kit into .build/qt.",
        ),
        Requirement(
            "android-ndk",
            "Android NDK",
            check_android_ndk,
            (),
            f"Install Android NDK ({ANDROID_NDK_PACKAGE}) with sdkmanager.",
        ),
    ],
    "ios": [
        Requirement(
            "xcode",
            "Xcode command-line tools",
            check_ios_tools,
            (),
            "Install Xcode and run: xcode-select --install",
        ),
        Requirement(
            "ios-sim-runtime",
            "iOS Simulator runtime",
            check_ios_simulator_runtime,
            (),
            "Install an iOS simulator runtime in Xcode > Settings > Components.",
        ),
        Requirement(
            "qt-ios",
            "Qt iOS tooling",
            check_qt_ios_tools,
            (),
            "Run with --bootstrap-qt to install Qt iOS kit into .build/qt.",
        ),
    ],
    "web": [
        Requirement("emcc", "Emscripten compiler", check_emscripten, ("emscripten",)),
        Requirement("node", "Node.js", lambda: which_any(("node",)), ("nodejs",)),
        Requirement("make", "Make", lambda: which_any(("make", "nmake")), ("make",)),
    ],
    "webqt": [
        Requirement("git", "Git", lambda: which_any(("git",)), ("git",)),
        Requirement("cmake", "CMake", lambda: which_any(("cmake",)), ("cmake",)),
        Requirement("ninja", "Ninja", lambda: which_any(("ninja",)), ("ninja",)),
        Requirement("emcc", "Emscripten compiler", check_emscripten, ("emscripten",)),
        Requirement(
            "emsdk-layout",
            "Emscripten SDK layout (EMSDK/.emscripten)",
            check_emsdk_layout,
            (),
            f"Run with --bootstrap-emsdk to install/activate Emscripten SDK {EMSDK_VERSION} in .build/emsdk.",
        ),
        Requirement("node", "Node.js", lambda: which_any(("node",)), ("nodejs",)),
        Requirement("make", "Make", lambda: which_any(("make", "nmake")), ("make",)),
        Requirement(
            "qt-web",
            "Qt WebAssembly tooling",
            check_qt_web_tools,
            (),
            "Run with --bootstrap-qt to install Qt wasm kit into .build/qt.",
        ),
        Requirement(
            "kddockwidgets-web",
            "KDDockWidgets 2.4.0 (Qt6 wasm)",
            check_kddockwidgets_wasm,
            (),
            "Run with --bootstrap-kddockwidgets to build/install wasm KDDockWidgets into .build/deps/kddockwidgets-2.4-wasm.",
        ),
    ],
}


PKG_MAP: Dict[str, Dict[str, Tuple[str, ...]]] = {
    "apt": {
        "git": ("git",),
        "cmake": ("cmake",),
        "ninja": ("ninja-build",),
        "build-essential": ("build-essential",),
        "qt6-desktop": ("qt6-base-dev", "qt6-declarative-dev"),
        "openjdk17": ("openjdk-17-jdk",),
        "android-sdk": ("android-sdk",),
        "emscripten": ("emscripten",),
        "nodejs": ("nodejs", "npm"),
        "make": ("make",),
        "gh-cli": ("gh",),
        "docker": ("docker.io",),
    },
    "dnf": {
        "git": ("git",),
        "cmake": ("cmake",),
        "ninja": ("ninja-build",),
        "build-essential": ("gcc-c++", "make"),
        "qt6-desktop": ("qt6-qtbase-devel", "qt6-qtdeclarative-devel"),
        "openjdk17": ("java-17-openjdk-devel",),
        "android-sdk": ("android-tools",),
        "emscripten": ("emscripten",),
        "nodejs": ("nodejs", "npm"),
        "make": ("make",),
        "gh-cli": ("gh",),
        "docker": ("docker",),
    },
    "pacman": {
        "git": ("git",),
        "cmake": ("cmake",),
        "ninja": ("ninja",),
        "build-essential": ("base-devel",),
        "qt6-desktop": ("qt6-base", "qt6-declarative"),
        "openjdk17": ("jdk17-openjdk",),
        "android-sdk": ("android-tools",),
        "emscripten": ("emscripten",),
        "nodejs": ("nodejs", "npm"),
        "make": ("make",),
        "gh-cli": ("github-cli",),
        "docker": ("docker",),
    },
    "zypper": {
        "git": ("git",),
        "cmake": ("cmake",),
        "ninja": ("ninja",),
        "build-essential": ("gcc-c++", "make"),
        "qt6-desktop": ("qt6-base-devel", "qt6-declarative-devel"),
        "openjdk17": ("java-17-openjdk-devel",),
        "android-sdk": ("android-tools",),
        "emscripten": ("emscripten",),
        "nodejs": ("nodejs20", "npm20"),
        "make": ("make",),
        "gh-cli": ("gh",),
        "docker": ("docker",),
    },
    "brew": {
        "git": ("git",),
        "cmake": ("cmake",),
        "ninja": ("ninja",),
        "build-essential": (),
        "qt6-desktop": ("qt",),
        "openjdk17": ("openjdk@17",),
        "android-sdk": ("cask:android-commandlinetools", "android-platform-tools"),
        "emscripten": ("emscripten",),
        "nodejs": ("node",),
        "make": ("make",),
        "gh-cli": ("gh",),
        "docker": ("cask:docker",),
    },
    "choco": {
        "git": ("git",),
        "cmake": ("cmake",),
        "ninja": ("ninja",),
        "build-essential": ("visualstudio2022buildtools",),
        "qt6-desktop": ("qt6-default",),
        "openjdk17": ("microsoft-openjdk17",),
        "android-sdk": ("android-sdk",),
        "emscripten": ("emscripten",),
        "nodejs": ("nodejs-lts",),
        "make": ("make",),
        "gh-cli": ("gh",),
        "docker": ("docker-desktop",),
    },
    "winget": {
        "git": ("Git.Git",),
        "cmake": ("Kitware.CMake",),
        "ninja": ("Ninja-build.Ninja",),
        "build-essential": ("Microsoft.VisualStudio.2022.BuildTools",),
        "qt6-desktop": ("QtProject.Qt",),
        "openjdk17": ("Microsoft.OpenJDK.17",),
        "android-sdk": ("Google.AndroidStudio",),
        "emscripten": (),
        "nodejs": ("OpenJS.NodeJS.LTS",),
        "make": (),
        "gh-cli": ("GitHub.cli",),
        "docker": ("Docker.DockerDesktop",),
    },
}


def collect_package_installs(pm: str, install_keys: Sequence[str]) -> Tuple[List[str], List[str]]:
    resolved: List[str] = []
    unresolved: List[str] = []
    table = PKG_MAP.get(pm, {})
    for key in install_keys:
        entries = table.get(key, ())
        if not entries:
            unresolved.append(key)
            continue
        resolved.extend(entries)
    unique = []
    for item in resolved:
        if item not in unique:
            unique.append(item)
    return unique, unresolved


def install_packages(pm: str, packages: Sequence[str], dry_run: bool) -> int:
    if not packages:
        return 0
    if pm == "apt":
        if run(("sudo", "apt-get", "update"), dry_run) != 0:
            return 1
        return run(("sudo", "apt-get", "install", "-y", *packages), dry_run)
    if pm == "dnf":
        return run(("sudo", "dnf", "install", "-y", *packages), dry_run)
    if pm == "pacman":
        return run(("sudo", "pacman", "-Sy", "--noconfirm", *packages), dry_run)
    if pm == "zypper":
        return run(("sudo", "zypper", "--non-interactive", "install", *packages), dry_run)
    if pm == "brew":
        rc = 0
        for pkg in packages:
            if pkg.startswith("cask:"):
                rc = run(("brew", "install", "--cask", pkg.split(":", 1)[1]), dry_run)
            else:
                rc = run(("brew", "install", pkg), dry_run)
            if rc != 0:
                return rc
        return 0
    if pm == "choco":
        return run(("choco", "install", "-y", *packages), dry_run)
    if pm == "winget":
        rc = 0
        for pkg in packages:
            rc = run(("winget", "install", "--id", pkg, "--silent", "--accept-package-agreements", "--accept-source-agreements"), dry_run)
            if rc != 0:
                return rc
        return 0
    return 1


def resolve_targets(raw: str, system_name: str) -> List[str]:
    if raw == "all":
        if system_name == "Darwin":
            return ["desktop", "android", "ios", "web", "webqt"]
        return ["desktop", "android", "web", "webqt"]
    return [raw]


def host_desktop_targets(system_name: str, include_ios: bool) -> List[str]:
    if system_name == "Darwin":
        return ["macos", "linux", "windows"] + (["ios"] if include_ios else [])
    if system_name == "Linux":
        return ["linux", "windows"]
    if system_name == "Windows":
        return ["windows", "linux"]
    return []


def bootstrap_vcpkg(dry_run: bool) -> int:
    vcpkg_root = Path(".build") / "vcpkg"
    if not vcpkg_root.exists():
        vcpkg_root.parent.mkdir(parents=True, exist_ok=True)
        if run(("git", "clone", "https://github.com/microsoft/vcpkg.git", str(vcpkg_root)), dry_run) != 0:
            return 1

    if platform.system() == "Windows":
        return run(("cmd", "/c", "bootstrap-vcpkg.bat", "-disableMetrics"), dry_run, cwd=str(vcpkg_root))
    return run(("./bootstrap-vcpkg.sh", "-disableMetrics"), dry_run, cwd=str(vcpkg_root))


def bootstrap_kddockwidgets(dry_run: bool) -> int:
    src_dir = Path(".build") / "kddockwidgets-src"
    build_dir = Path(".build") / "kddockwidgets-build"
    install_prefix = kddockwidgets_prefix_path()

    src_dir.parent.mkdir(parents=True, exist_ok=True)
    install_prefix.parent.mkdir(parents=True, exist_ok=True)

    if not src_dir.exists():
        if run(
            (
                "git",
                "clone",
                "--depth",
                "1",
                "--branch",
                f"v{KDDOCKWIDGETS_VERSION}",
                "https://github.com/KDAB/KDDockWidgets.git",
                str(src_dir),
            ),
            dry_run,
        ) != 0:
            return 1

    if run(
        (
            "cmake",
            "-S",
            str(src_dir),
            "-B",
            str(build_dir),
            "-G",
            "Ninja",
            "-DCMAKE_BUILD_TYPE=Release",
            f"-DCMAKE_INSTALL_PREFIX={install_prefix}",
            "-DKDDockWidgets_NO_SPDLOG=ON",
            "-DKDDockWidgets_EXAMPLES=OFF",
            "-DKDDockWidgets_TESTS=OFF",
        ),
        dry_run,
    ) != 0:
        return 1

    if run(("cmake", "--build", str(build_dir), "--parallel"), dry_run) != 0:
        return 1

    return run(("cmake", "--install", str(build_dir)), dry_run)


def bootstrap_kddockwidgets_wasm(dry_run: bool) -> int:
    qt_cmake = local_qt_web_cmake()
    if not qt_cmake:
        return 1
    web_qt_info = local_qt_web_info()
    emscripten_toolchain = detect_emscripten_toolchain_file()
    ninja_path = shutil.which("ninja")

    src_dir = Path(".build") / "kddockwidgets-src"
    build_dir = Path(".build") / "kddockwidgets-build-wasm"
    install_prefix = kddockwidgets_wasm_prefix_path()
    emsdk_root = detect_emsdk_root()
    if emsdk_root and (emsdk_root / "upstream" / "emscripten" / "cache").exists():
        em_cache_dir = (emsdk_root / "upstream" / "emscripten" / "cache").resolve()
    else:
        em_cache_dir = (Path(".build") / "emscripten-cache").resolve()

    src_dir.parent.mkdir(parents=True, exist_ok=True)
    install_prefix.parent.mkdir(parents=True, exist_ok=True)
    em_cache_dir.mkdir(parents=True, exist_ok=True)

    if not src_dir.exists():
        if run(
            (
                "git",
                "clone",
                "--depth",
                "1",
                "--branch",
                f"v{KDDOCKWIDGETS_VERSION}",
                "https://github.com/KDAB/KDDockWidgets.git",
                str(src_dir),
            ),
            dry_run,
        ) != 0:
            return 1

    configure_cmd: List[str] = [
        str(qt_cmake),
        "-S",
        str(src_dir),
        "-B",
        str(build_dir),
        "-G",
        "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        f"-DCMAKE_INSTALL_PREFIX={install_prefix}",
        "-DBUILD_SHARED_LIBS=OFF",
        "-DKDDockWidgets_STATIC=ON",
        "-DKDDockWidgets_NO_SPDLOG=ON",
        "-DKDDockWidgets_EXAMPLES=OFF",
        "-DKDDockWidgets_TESTS=OFF",
    ]
    if web_qt_info:
        web_qt_version = web_qt_info[0]
        host_qt_path = local_qt_host_path(web_qt_version)
        if host_qt_path:
            configure_cmd.append(f"-DQT_HOST_PATH={host_qt_path}")
    if emscripten_toolchain:
        configure_cmd.append(f"-DQT_CHAINLOAD_TOOLCHAIN_FILE={emscripten_toolchain}")
    if ninja_path:
        configure_cmd.append(f"-DCMAKE_MAKE_PROGRAM={ninja_path}")

    wasm_env = os.environ.copy()
    wasm_env.setdefault("EM_CACHE", str(em_cache_dir))
    if emsdk_root:
        wasm_env.setdefault("EMSDK", str(emsdk_root))
        em_config = emsdk_root / ".emscripten"
        if em_config.exists():
            wasm_env.setdefault("EM_CONFIG", str(em_config))

    if run(tuple(configure_cmd), dry_run, env=wasm_env) != 0:
        return 1

    if run(("cmake", "--build", str(build_dir), "--parallel"), dry_run, env=wasm_env) != 0:
        return 1

    return run(("cmake", "--install", str(build_dir)), dry_run, env=wasm_env)


def bootstrap_emsdk(dry_run: bool) -> int:
    emsdk_required_version = qt_wasm_recommended_emsdk_version()
    emsdk_root = local_emsdk_root()
    if not emsdk_root.exists():
        emsdk_root.parent.mkdir(parents=True, exist_ok=True)
        if run(("git", "clone", "https://github.com/emscripten-core/emsdk.git", str(emsdk_root)), dry_run) != 0:
            return 1

    if platform.system() == "Windows":
        if run(("cmd", "/c", "emsdk.bat", "install", emsdk_required_version), dry_run, cwd=str(emsdk_root)) != 0:
            return 1
        return run(("cmd", "/c", "emsdk.bat", "activate", emsdk_required_version), dry_run, cwd=str(emsdk_root))

    if run(("./emsdk", "install", emsdk_required_version), dry_run, cwd=str(emsdk_root)) != 0:
        return 1
    return run(("./emsdk", "activate", emsdk_required_version), dry_run, cwd=str(emsdk_root))


def bootstrap_docker_builder_images(system_name: str, dry_run: bool) -> int:
    desktop_targets = host_desktop_targets(system_name, include_ios=False)
    needed_images: List[Tuple[str, Path]] = []
    if "linux" in desktop_targets:
        needed_images.append((DOCKER_LINUX_IMAGE, Path("tools/build/docker/linux-desktop.Dockerfile")))
    if "windows" in desktop_targets:
        needed_images.append((DOCKER_WINDOWS_IMAGE, Path("tools/build/docker/windows-desktop.Dockerfile")))

    for tag, dockerfile in needed_images:
        if check_docker_image(tag):
            continue
        if not dockerfile.exists():
            print(f"Missing Dockerfile for builder image: {dockerfile}")
            return 1
        rc = run(
            (
                "docker",
                "build",
                "-f",
                str(dockerfile),
                "-t",
                tag,
                ".",
            ),
            dry_run,
        )
        if rc != 0:
            return rc
    return 0


def qt_host_tag(system_name: str) -> str:
    if system_name == "Darwin":
        return "mac"
    if system_name == "Linux":
        return "linux"
    if system_name == "Windows":
        return "windows"
    return ""


def qt_desktop_arch(system_name: str) -> str:
    if system_name == "Darwin":
        return "clang_64"
    if system_name == "Linux":
        return "gcc_64"
    if system_name == "Windows":
        return "win64_msvc2022_64"
    return ""


def bootstrap_qt_kits(system_name: str, targets: Sequence[str], dry_run: bool) -> int:
    host = qt_host_tag(system_name)
    desktop_arch = qt_desktop_arch(system_name)
    if not host or not desktop_arch:
        return 1

    pip_cmd = [sys.executable, "-m", "pip", "install"]
    if not in_virtualenv():
        pip_cmd.append("--user")
    pip_cmd.extend(["--upgrade", "aqtinstall"])
    if run(tuple(pip_cmd), dry_run) != 0:
        return 1

    output_dir = local_qt_root()
    output_dir.mkdir(parents=True, exist_ok=True)
    selected_aqt_base = select_working_aqt_base(dry_run)
    if selected_aqt_base:
        print(f"Using Qt mirror: {selected_aqt_base}")

    kits: List[Tuple[str, str]] = []
    if "desktop" in targets:
        kits.append(("desktop", desktop_arch))
    if "android" in targets:
        kits.append(("android", "android_arm64_v8a"))
    if "ios" in targets and system_name == "Darwin":
        kits.append(("ios", "ios"))
    if "webqt" in targets:
        kits.append(("wasm", "wasm_multithread"))

    # Keep deterministic order and avoid duplicate installs.
    seen = set()
    unique_kits: List[Tuple[str, str]] = []
    for kit in kits:
        if kit in seen:
            continue
        seen.add(kit)
        unique_kits.append(kit)

    for target, arch in unique_kits:
        if local_qt_kit_exists(arch):
            if not (system_name == "Darwin" and arch in ("android_arm64_v8a", "ios") and not local_qt_host_tools_ok()):
                continue
            if not dry_run:
                qt_version_dir = output_dir / QT_VERSION
                if qt_version_dir.exists():
                    shutil.rmtree(qt_version_dir)
        if arch in QT_WASM_ARCHES:
            wasm_arches = [arch] + [candidate for candidate in QT_WASM_ARCHES if candidate != arch]
            wasm_versions = qt_wasm_version_candidates(selected_aqt_base)
            wasm_installed = False
            for wasm_version in wasm_versions:
                for wasm_arch in wasm_arches:
                    if local_qt_kit_exists(wasm_arch, allow_any_version=True):
                        wasm_installed = True
                        break
                    rc = run(
                        aqt_install_qt_cmd(
                            "all_os",
                            "wasm",
                            wasm_version,
                            wasm_arch,
                            "--outputdir",
                            str(output_dir),
                            base=selected_aqt_base,
                        ),
                        dry_run,
                    )
                    if rc == 0:
                        wasm_installed = True
                        break
                if wasm_installed:
                    break
            if not wasm_installed:
                return 1
            continue
        if run(
            aqt_install_qt_cmd(
                host,
                target,
                QT_VERSION,
                arch,
                "--outputdir",
                str(output_dir),
                base=selected_aqt_base,
            ),
            dry_run,
        ) != 0:
            return 1
    return 0


def find_sdkmanager() -> Path | None:
    direct = shutil.which("sdkmanager")
    if direct:
        return Path(direct)
    for sdk_root in android_sdk_roots():
        candidates = [
            sdk_root / "cmdline-tools" / "latest" / "bin" / "sdkmanager",
            sdk_root / "cmdline-tools" / "bin" / "sdkmanager",
            sdk_root / "tools" / "bin" / "sdkmanager",
        ]
        for candidate in candidates:
            if candidate.exists():
                return candidate
    return None


def bootstrap_android_ndk(dry_run: bool) -> int:
    if detect_android_ndk_root():
        return 0
    sdkmanager = find_sdkmanager()
    if not sdkmanager:
        return 1
    return run((str(sdkmanager), "--install", ANDROID_NDK_PACKAGE), dry_run)


def bootstrap_ios_runtime(dry_run: bool) -> int:
    if platform.system() != "Darwin":
        return 1
    if check_ios_simulator_runtime():
        return 0
    if not shutil.which("xcodebuild"):
        return 1
    return run(("xcodebuild", "-downloadPlatform", "iOS"), dry_run)


def refresh_missing(requirements: Sequence[Requirement]) -> List[Requirement]:
    return [req for req in requirements if not req.check()]


def supports_color() -> bool:
    if os.environ.get("NO_COLOR"):
        return False
    if os.environ.get("FORCE_COLOR"):
        return True
    try:
        return sys.stdout.isatty()
    except Exception:
        return False


def status_tag(ok: bool) -> str:
    if ok:
        text = "[GO]"
        return f"\033[32m{text}\033[0m" if supports_color() else text
    text = "[NO-GO]"
    return f"\033[31m{text}\033[0m" if supports_color() else text


def format_requirement_line(req: Requirement, ok: bool) -> str:
    status = status_tag(ok)
    version = requirement_version(req.key)
    if version:
        return f"{status} {req.label} ({version})"
    return f"{status} {req.label}"


def host_os_name(system_name: str) -> str:
    if system_name == "Darwin":
        return "macOS"
    if system_name == "Linux":
        return "Linux"
    if system_name == "Windows":
        return "Windows"
    return system_name


def host_install_hints(req: Requirement, pm: str) -> List[str]:
    if req.key == "emsdk-layout":
        required = qt_wasm_recommended_emsdk_version()
        return [f"Run with --bootstrap-emsdk to install/activate Emscripten SDK {required} in .build/emsdk."]
    if req.install_keys and pm:
        packages, _ = collect_package_installs(pm, req.install_keys)
        if packages:
            return [f"{pm}: {', '.join(packages)}"]
    if req.manual_hint:
        return [req.manual_hint]
    return []


def build_commands_for_target(target: str, system_name: str) -> List[str]:
    if target == "desktop":
        return [
            "cmake -S . -B .build/desktop",
            "cmake --build .build/desktop -j8",
        ]
    if target == "android":
        return [
            "mkdir -p .build/android && cd .build/android",
            "qmake ../../firebird.pro CONFIG+=release ANDROID_ABIS=arm64-v8a",
            "make -j8",
        ]
    if target == "ios":
        if system_name != "Darwin":
            return []
        return [
            "mkdir -p .build/ios && cd .build/ios",
            "qmake ../../firebird.pro -spec macx-ios-clang CONFIG+=release",
            "xcodebuild -project firebird-emu.xcodeproj -scheme firebird-emu -configuration Release -sdk iphonesimulator -destination 'generic/platform=iOS Simulator' CODE_SIGNING_ALLOWED=NO build",
        ]
    if target == "web":
        return ["make -C emscripten BUILD_DIR=../.build/web -j8"]
    if target == "webqt":
        return [
            "cmake -S . -B .build/meta -DFIREBIRD_META_BUILD=ON -DFIREBIRD_TARGETS=webqt",
            "cmake --build .build/meta --target firebird-build-web-qt --parallel",
        ]
    return []


def target_display_name(target: str) -> str:
    names = {
        "desktop": "Desktop",
        "android": "Android",
        "ios": "iOS",
        "web": "Web",
        "webqt": "Web Qt",
    }
    return names.get(target, target.capitalize())


def target_supported_on_host(target: str, system_name: str) -> bool:
    if target == "ios":
        return system_name == "Darwin"
    return True


def remote_build_hint(target: str) -> str:
    hints = {
        "ios": "Use macOS CI runner via .github/workflows/ios.yml",
        "desktop": "Use host-specific desktop CI workflow",
        "android": "Use .github/workflows/android.yml",
        "web": "Use .github/workflows/web.yml",
        "webqt": "Use a host with Qt wasm kit and run local web-qt meta target",
    }
    return hints.get(target, "Use CI workflow for this platform")


def print_report(
    system_name: str,
    pm: str,
    targets: Sequence[str],
    local_targets: Sequence[str],
    requirements_by_target: Dict[str, List[Requirement]],
    unique_requirements: Sequence[Requirement],
    status_by_key: Dict[str, bool],
    show_build_commands: bool,
) -> List[Requirement]:
    missing = [req for req in unique_requirements if not status_by_key[req.key]]

    common_keys = [key for key in COMMON_REQUIREMENT_KEYS if key in status_by_key]
    if common_keys:
        print("\nAll Platforms:")
        for key in common_keys:
            req = next(req for req in unique_requirements if req.key == key)
            print(f"  {format_requirement_line(req, status_by_key[key])}")
    else:
        print("\nAll Platforms:")
        print("  (none)")

    for target in targets:
        if target in local_targets:
            target_ok = all(status_by_key.get(req.key, False) for req in requirements_by_target[target])
            print(f"\n{target_display_name(target)}: {status_tag(target_ok)}")
        else:
            print(f"\n{target_display_name(target)}: {status_tag(False)}")
        if target not in local_targets:
            print(f"  Host ({host_os_name(system_name)}): remote-only on this machine.")
            print(f"  Remote build: {remote_build_hint(target)}")
            continue

        target_reqs = requirements_by_target[target]
        filtered_target_reqs = [req for req in target_reqs if req.key not in common_keys]

        print("  All:")
        if not filtered_target_reqs:
            print("    (none)")
        for req in filtered_target_reqs:
            line = format_requirement_line(req, status_by_key.get(req.key, False))
            print(f"    {line}")

    print("\nMissing by platform:")
    for target in targets:
        print(f"  {target_display_name(target)}:")
        if target not in local_targets:
            print("    - Local build unsupported on this host")
            print(f"    - {remote_build_hint(target)}")
            continue

        target_reqs = requirements_by_target[target]
        target_missing = [req for req in target_reqs if not status_by_key.get(req.key, False)]
        if not target_missing:
            print("    (none)")
            continue
        for req in target_missing:
            version = requirement_version(req.key)
            if version:
                print(f"    - {req.label} ({version})")
            else:
                print(f"    - {req.label}")

    host_name = host_os_name(system_name)
    print("\nInstall plan for this host:")
    print(f"  Host: {host_name}")
    printed_any = False
    for target in targets:
        if target not in local_targets:
            print(f"  {target_display_name(target)}:")
            print(f"    - Local build unsupported on this host")
            print(f"    - {remote_build_hint(target)}")
            printed_any = True
            continue

        target_reqs = requirements_by_target[target]
        target_missing = [req for req in target_reqs if not status_by_key.get(req.key, False)]
        if not target_missing:
            continue
        printed_any = True
        print(f"  {target_display_name(target)}:")
        for req in target_missing:
            # Host install hints are only shown for requirements that are still NO-GO.
            hints = host_install_hints(req, pm)
            if hints:
                print(f"    - {req.label}: {'; '.join(hints)}")
            else:
                print(f"    - {req.label}: no automatic install hint available")
    if not printed_any:
        print("  (none)")

    if show_build_commands:
        print("\nBuild commands after bootstrap:")
        for target in targets:
            commands = build_commands_for_target(target, system_name)
            if not commands:
                continue
            print(f"  {target_display_name(target)}:")
            for command in commands:
                print(f"    {command}")

    return missing


def collect_status(requirements: Sequence[Requirement]) -> Dict[str, bool]:
    status: Dict[str, bool] = {}
    for req in requirements:
        status[req.key] = req.check()
    return status


def main() -> int:
    parser = argparse.ArgumentParser(description="Cross-platform build dependency bootstrap for Firebird.")
    parser.add_argument("--target", choices=["desktop", "android", "ios", "web", "webqt", "all"], default="all")
    parser.add_argument("--check-only", action="store_true", help="Only check dependencies, do not install.")
    parser.add_argument("--report", action="store_true", help="Print requirement report with versions and exit.")
    parser.add_argument("--show-build-commands", action="store_true", help="Include post-bootstrap build commands in output.")
    parser.add_argument("--yes", action="store_true", help="Do not prompt before installation.")
    parser.add_argument("--dry-run", action="store_true", help="Print install commands without executing them.")
    parser.add_argument("--with-vcpkg", action="store_true", help="Also require vcpkg for CMake toolchain usage.")
    parser.add_argument("--bootstrap-vcpkg", action="store_true", help="Clone/bootstrap vcpkg into .build/vcpkg if missing.")
    parser.add_argument(
        "--bootstrap-qt",
        action="store_true",
        help=f"Install Qt {QT_VERSION} kits into .build/qt using aqtinstall.",
    )
    parser.add_argument(
        "--bootstrap-kddockwidgets",
        action="store_true",
        help=f"Clone/build/install KDDockWidgets v{KDDOCKWIDGETS_VERSION} into .build/deps/kddockwidgets-2.4 if missing.",
    )
    parser.add_argument(
        "--bootstrap-emsdk",
        action="store_true",
        help=f"Clone/install/activate Emscripten SDK {EMSDK_VERSION} into .build/emsdk if missing.",
    )
    parser.add_argument(
        "--bootstrap-docker-builders",
        action="store_true",
        help="Build local Docker desktop-cross builder images used for Linux/Windows desktop cross builds.",
    )
    args = parser.parse_args()

    system_name = platform.system()
    pm = detect_package_manager(system_name)

    print(f"Platform: {system_name}")
    print(f"Package manager: {pm or 'not detected'}")

    targets = resolve_targets(args.target, system_name)
    local_targets = [target for target in targets if target_supported_on_host(target, system_name)]
    remote_only_targets = [target for target in targets if target not in local_targets]
    print(f"Targets: {', '.join(targets)}")
    if remote_only_targets:
        joined = ", ".join(target_display_name(target) for target in remote_only_targets)
        print(f"Remote-only on this host: {joined}")

    auto_bootstrap_qt = args.bootstrap_qt or args.target == "all" or any(
        target in targets for target in ("desktop", "android", "ios", "webqt")
    )
    auto_bootstrap_kddockwidgets = args.bootstrap_kddockwidgets or args.target == "all" or any(
        target in targets for target in ("desktop", "webqt")
    )
    auto_bootstrap_emsdk = args.bootstrap_emsdk or args.target == "all"
    auto_bootstrap_docker_builders = args.bootstrap_docker_builders or args.target == "all"
    requirements_by_target: Dict[str, List[Requirement]] = {}
    reqs: List[Requirement] = []
    for target in targets:
        requirements_by_target[target] = list(REQUIREMENTS[target])
    for target in local_targets:
        reqs.extend(REQUIREMENTS[target])
    if args.target == "all" and "desktop" in targets:
        desktop_targets = host_desktop_targets(system_name, include_ios=("ios" in targets))
        if any(target in ("linux", "windows") for target in desktop_targets):
            docker_req = Requirement(
                "docker",
                "Docker engine",
                check_docker,
                ("docker",),
                "Install/start Docker and ensure `docker info` succeeds.",
            )
            reqs.append(docker_req)
            requirements_by_target.setdefault("desktop", []).append(docker_req)

            if "linux" in desktop_targets and system_name != "Linux":
                linux_cross_req = Requirement(
                    "desktop-linux-cross",
                    f"Linux desktop cross-builder image ({DOCKER_LINUX_IMAGE})",
                    check_linux_desktop_cross_image,
                    (),
                    "Run with --bootstrap-docker-builders to build the Linux desktop builder image.",
                )
                reqs.append(linux_cross_req)
                requirements_by_target.setdefault("desktop", []).append(linux_cross_req)

            if "windows" in desktop_targets and system_name != "Windows":
                windows_cross_req = Requirement(
                    "desktop-windows-cross",
                    f"Windows desktop cross-builder image ({DOCKER_WINDOWS_IMAGE})",
                    check_windows_desktop_cross_image,
                    (),
                    "Run with --bootstrap-docker-builders to build the Windows desktop builder image.",
                )
                reqs.append(windows_cross_req)
                requirements_by_target.setdefault("desktop", []).append(windows_cross_req)
    if args.with_vcpkg and "desktop" in local_targets:
        reqs.append(
            Requirement(
                "vcpkg",
                "vcpkg package manager",
                check_vcpkg,
                (),
                "Install vcpkg or run with --bootstrap-vcpkg to create .build/vcpkg.",
            )
        )
        requirements_by_target.setdefault("desktop", []).append(reqs[-1])

    seen = set()
    unique_reqs: List[Requirement] = []
    for req in reqs:
        if req.key in seen:
            continue
        seen.add(req.key)
        unique_reqs.append(req)

    status_by_key = collect_status(unique_reqs)
    missing = [req for req in unique_reqs if not status_by_key.get(req.key, False)]

    if not args.check_only and not args.report and missing:
        print("\nAttempting to install missing requirements on this host...")

    if not args.check_only and not args.report and auto_bootstrap_qt and any(req.key in ("qt-desktop", "qt-android", "qt-ios", "qt-web") for req in missing):
        print(f"\nBootstrapping Qt kits into {local_qt_root()} ...")
        if bootstrap_qt_kits(system_name, local_targets, args.dry_run) != 0:
            print("Qt bootstrap failed.")
            return 1
        missing = refresh_missing(unique_reqs)

    if not args.check_only and not args.report and args.bootstrap_vcpkg and any(req.key == "vcpkg" for req in missing):
        print("\nBootstrapping vcpkg into .build/vcpkg ...")
        if bootstrap_vcpkg(args.dry_run) != 0:
            print("vcpkg bootstrap failed.")
            return 1
        missing = refresh_missing(unique_reqs)

    needs_desktop_kddock = any(req.key == "kddockwidgets" for req in missing) or (
        args.bootstrap_kddockwidgets and "desktop" in targets and "desktop" in local_targets
    )
    needs_web_kddock = any(req.key == "kddockwidgets-web" for req in missing) or (
        args.bootstrap_kddockwidgets and "webqt" in targets and "webqt" in local_targets
    )
    if not args.check_only and not args.report and auto_bootstrap_kddockwidgets and (needs_desktop_kddock or needs_web_kddock):
        if needs_desktop_kddock:
            print("\nBootstrapping KDDockWidgets into .build/deps/kddockwidgets-2.4 ...")
            if bootstrap_kddockwidgets(args.dry_run) != 0:
                print("KDDockWidgets bootstrap failed.")
                return 1
            missing = refresh_missing(unique_reqs)

        if needs_web_kddock:
            print("\nBootstrapping KDDockWidgets (wasm) into .build/deps/kddockwidgets-2.4-wasm ...")
            if bootstrap_kddockwidgets_wasm(args.dry_run) != 0:
                print("KDDockWidgets wasm bootstrap failed.")
                return 1
            missing = refresh_missing(unique_reqs)

    emsdk_missing = any(req.key in ("emcc", "emsdk-layout") for req in missing)
    should_bootstrap_emsdk = auto_bootstrap_emsdk or any(req.key == "emsdk-layout" for req in missing)
    if not args.check_only and not args.report and should_bootstrap_emsdk and emsdk_missing:
        emsdk_required_version = qt_wasm_recommended_emsdk_version()
        print(f"\nBootstrapping Emscripten SDK {emsdk_required_version} into .build/emsdk ...")
        if bootstrap_emsdk(args.dry_run) != 0:
            print("Emscripten SDK bootstrap failed.")
            return 1
        missing = refresh_missing(unique_reqs)

    if not args.check_only and not args.report and any(req.key == "android-ndk" for req in missing):
        print(f"\nBootstrapping Android NDK ({ANDROID_NDK_PACKAGE}) ...")
        if bootstrap_android_ndk(args.dry_run) != 0:
            print("Android NDK bootstrap failed.")
            return 1
        missing = refresh_missing(unique_reqs)

    if not args.check_only and not args.report and any(req.key == "ios-sim-runtime" for req in missing):
        print("\nBootstrapping iOS platform/runtime via Xcode ...")
        if bootstrap_ios_runtime(args.dry_run) != 0:
            print("iOS platform/runtime bootstrap failed.")
            return 1
        missing = refresh_missing(unique_reqs)

    if not args.check_only and not args.report:
        install_keys: List[str] = []
        for req in missing:
            install_keys.extend(req.install_keys)
        install_keys = list(dict.fromkeys(install_keys))

        packages, _ = collect_package_installs(pm, install_keys) if pm else ([], install_keys)
        if packages:
            print("\nInstallable packages:")
            for pkg in packages:
                print(f"  - {pkg}")
            if not args.yes and not args.dry_run:
                answer = input("\nProceed with package installation? [y/N] ").strip().lower()
                if answer not in ("y", "yes"):
                    print("Aborted.")
                    return 1
            rc = install_packages(pm, packages, args.dry_run)
            if rc != 0:
                print("\nInstallation failed.")
                return rc

        missing = refresh_missing(unique_reqs)
        if auto_bootstrap_docker_builders and any(req.key in ("desktop-linux-cross", "desktop-windows-cross") for req in missing):
            print("\nBootstrapping Docker desktop-cross builder images ...")
            if not check_docker() and not args.dry_run:
                print("Docker is required and must be running before builder images can be created.")
                return 1
            if bootstrap_docker_builder_images(system_name, args.dry_run) != 0:
                print("Docker builder image bootstrap failed.")
                return 1

    # Final status pass after optional install/bootstrap.
    status_by_key = collect_status(unique_reqs)
    missing = [req for req in unique_reqs if not status_by_key.get(req.key, False)]

    print("")
    missing = print_report(
        system_name,
        pm,
        targets,
        local_targets,
        requirements_by_target,
        unique_reqs,
        status_by_key,
        args.show_build_commands,
    )

    if args.report:
        return 0 if not missing else 1
    if args.check_only:
        print("\nCheck-only completed.")
        return 0 if not missing else 1

    if missing:
        print("\nFailed to satisfy all requirements on this host:")
        for req in missing:
            hints = host_install_hints(req, pm)
            if hints:
                print(f"  - {req.label}: {'; '.join(hints)}")
            else:
                print(f"  - {req.label}")
        return 1

    print("\nBootstrap complete. All requested requirements are GO.")
    if local_emsdk_emcc_path().exists():
        if platform.system() == "Windows":
            print("Emscripten environment hint:")
            print("  .build\\emsdk\\emsdk_env.bat")
        else:
            print("Emscripten environment hint:")
            print("  source .build/emsdk/emsdk_env.sh")
    if local_qt_root().exists():
        print("Qt install root:")
        print(f"  {local_qt_root()}")
    if args.with_vcpkg and check_vcpkg():
        print("vcpkg toolchain hint:")
        print("  -DCMAKE_TOOLCHAIN_FILE=.build/vcpkg/scripts/buildsystems/vcpkg.cmake")
    return 0


if __name__ == "__main__":
    sys.exit(main())
