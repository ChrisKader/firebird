#!/usr/bin/env python3
from __future__ import annotations

import argparse
from functools import partial
import http.server
import os
from pathlib import Path
import shutil
import subprocess
import sys
from typing import Dict, Iterable, Optional, Tuple


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_META_BUILD_DIR = REPO_ROOT / ".build" / "meta"
DEFAULT_WEBQT_BUILD_DIR = REPO_ROOT / ".build" / "web-qt"
DEFAULT_EM_CACHE = REPO_ROOT / ".build" / "emscripten-cache"


def run(cmd: Iterable[str], cwd: Path, env: Optional[Dict[str, str]] = None) -> None:
    argv = [str(part) for part in cmd]
    print("+", " ".join(argv))
    subprocess.run(argv, cwd=str(cwd), env=env, check=True)


def detect_emsdk_root() -> Optional[Path]:
    env_root = os.environ.get("EMSDK")
    if env_root:
        candidate = Path(env_root)
        if (candidate / ".emscripten").exists():
            return candidate.resolve()

    local = REPO_ROOT / ".build" / "emsdk"
    if (local / ".emscripten").exists():
        return local.resolve()

    emcc_path = shutil.which("emcc")
    if emcc_path:
        resolved = Path(emcc_path).resolve()
        parts = resolved.parts
        if "upstream" in parts and "emscripten" in parts:
            upstream_index = parts.index("upstream")
            if upstream_index > 0:
                candidate = Path(*parts[:upstream_index])
                if (candidate / ".emscripten").exists():
                    return candidate.resolve()
    return None


def webqt_env() -> Dict[str, str]:
    env = os.environ.copy()
    emsdk_root = detect_emsdk_root()
    if not emsdk_root:
        raise RuntimeError(
            "EMSDK layout was not found (.emscripten missing). Run:\n"
            "  python3 tools/build/bootstrap.py --target webqt --bootstrap-emsdk --yes"
        )
    emsdk_cache = emsdk_root / "upstream" / "emscripten" / "cache"
    if emsdk_cache.exists():
        em_cache = emsdk_cache.resolve()
    else:
        DEFAULT_EM_CACHE.mkdir(parents=True, exist_ok=True)
        em_cache = DEFAULT_EM_CACHE.resolve()
    env["EM_CACHE"] = str(em_cache)
    env["EMSDK"] = str(emsdk_root)
    env["EM_CONFIG"] = str((emsdk_root / ".emscripten").resolve())
    return env


def find_webqt_artifact_dir(search_root: Path) -> Tuple[Path, Optional[Path]]:
    if not search_root.exists():
        raise RuntimeError(f"Web Qt build directory not found: {search_root}")

    html_files = sorted(search_root.rglob("*.html"))
    if html_files:
        preferred = next((path for path in html_files if "firebird" in path.name.lower()), html_files[0])
        return preferred.parent, preferred

    # Some Qt WASM outputs may rely on index-less serving; serve the build root.
    return search_root, None


class CrossOriginIsolationHTTPRequestHandler(http.server.SimpleHTTPRequestHandler):
    """Serve files with headers required for SharedArrayBuffer/WebAssembly threads."""

    def end_headers(self) -> None:
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()


def main() -> int:
    parser = argparse.ArgumentParser(description="Build and serve Firebird Web Qt output locally.")
    parser.add_argument("--host", default="127.0.0.1", help="Host interface for local web server.")
    parser.add_argument("--port", type=int, default=8080, help="Port for local web server.")
    parser.add_argument(
        "--meta-build-dir",
        default=str(DEFAULT_META_BUILD_DIR),
        help="Meta-build directory that contains firebird-build-web-qt target.",
    )
    parser.add_argument(
        "--webqt-build-dir",
        default=str(DEFAULT_WEBQT_BUILD_DIR),
        help="Web Qt output root to scan for .html/.js/.wasm artifacts.",
    )
    parser.add_argument("--no-build", action="store_true", help="Skip cmake configure/build and only serve files.")
    args = parser.parse_args()

    meta_build_dir = Path(args.meta_build_dir).resolve()
    webqt_build_dir = Path(args.webqt_build_dir).resolve()

    env = webqt_env()

    if not args.no_build:
        run(
            (
                "cmake",
                "-S",
                str(REPO_ROOT),
                "-B",
                str(meta_build_dir),
                "-DFIREBIRD_META_BUILD=ON",
                "-DFIREBIRD_TARGETS=webqt",
            ),
            cwd=REPO_ROOT,
            env=env,
        )
        run(
            ("cmake", "--build", str(meta_build_dir), "--target", "firebird-build-web-qt", "--parallel"),
            cwd=REPO_ROOT,
            env=env,
        )

    serve_dir, html_entry = find_webqt_artifact_dir(webqt_build_dir)

    handler = partial(CrossOriginIsolationHTTPRequestHandler, directory=str(serve_dir))
    with http.server.ThreadingHTTPServer((args.host, args.port), handler) as server:
        if html_entry:
            rel_entry = html_entry.relative_to(serve_dir).as_posix()
            print(f"Serving: {serve_dir}")
            print(f"URL: http://{args.host}:{args.port}/{rel_entry}")
        else:
            print(f"Serving: {serve_dir}")
            print(f"URL: http://{args.host}:{args.port}/")
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            print("\nServer stopped.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
