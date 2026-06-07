#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CPP = ROOT / "cpp"

PROFILES = {
    "desktop": "Build the SDL native app for the current desktop.",
    "tests": "Build native test targets.",
    "python": "Build Python bindings.",
    "android": "Print or run Android CMake configure/build for one ABI.",
    "web": "Build the SDL app with Emscripten.",
}


def jobs() -> int:
    return max(1, os.cpu_count() or 4)


def run(cmd: list[str], cwd: Path | None, print_only: bool) -> int:
    print("+ " + " ".join(cmd))
    if print_only:
        return 0
    return subprocess.call(cmd, cwd=str(cwd) if cwd else None)


def cmake_configure_args(args: argparse.Namespace) -> list[str]:
    cmake = [
        "cmake",
        "-S",
        str(CPP),
        "-B",
        str(build_dir(args)),
        f"-DCMAKE_BUILD_TYPE={args.build_type}",
        "-DBUILD_RENDER_APP=ON",
        f"-DBUILD_LEGACY_CLI={'ON' if args.legacy_cli else 'OFF'}",
        f"-DUSE_BGFX={'ON' if args.enable_bgfx else 'OFF'}",
        f"-DUSE_SDL3={'OFF' if args.disable_sdl3 else 'ON'}",
        f"-DUSE_LIBAV={'OFF' if args.disable_libav else 'ON'}",
        f"-DUSE_LZMA={'ON' if args.enable_lzma else 'OFF'}",
        f"-DUSE_ENCRYPTION={'OFF' if args.disable_encryption else 'ON'}",
    ]
    if args.profile == "python":
        cmake += ["-DBUILD_PYTHON_BINDINGS=ON", "-DBUILD_RENDER_APP=OFF", "-DUSE_LIBAV=OFF"]
    if args.profile == "tests":
        cmake += ["-DUSE_BGFX=OFF", "-DUSE_LIBAV=OFF"]
    if args.extra_cmake_args:
        cmake += args.extra_cmake_args
    return cmake


def build_dir(args: argparse.Namespace) -> Path:
    if args.profile == "python":
        return CPP / "build_py"
    if args.profile == "tests":
        return CPP / "build_tests"
    if args.profile == "web":
        return CPP / "build_wasm"
    if args.profile == "android":
        return CPP / f"build_android_{args.abi}"
    return CPP / "build"


def target_for(args: argparse.Namespace) -> str:
    if args.target:
        return args.target
    if args.profile == "tests":
        return "run-tests"
    if args.profile == "python":
        return "_core"
    return "phigros_sdl_app"


def android_commands(args: argparse.Namespace) -> list[list[str]]:
    ndk = os.environ.get("ANDROID_NDK_ROOT") or os.environ.get("ANDROID_NDK")
    if not ndk:
        raise RuntimeError("ANDROID_NDK_ROOT or ANDROID_NDK is required for --profile android")
    toolchain = Path(ndk) / "build/cmake/android.toolchain.cmake"
    if not toolchain.exists():
        raise RuntimeError(f"Android toolchain not found: {toolchain}")
    bdir = build_dir(args)
    return [
        [
            "cmake",
            "-S",
            str(CPP),
            "-B",
            str(bdir),
            f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
            f"-DANDROID_ABI={args.abi}",
            f"-DANDROID_PLATFORM=android-{args.api}",
            "-DANDROID_STL=c++_shared",
            f"-DCMAKE_BUILD_TYPE={args.build_type}",
            "-DBUILD_RENDER_APP=ON",
            "-DBUILD_LEGACY_CLI=OFF",
            "-DUSE_BGFX=OFF",
            "-DUSE_SDL3=OFF",
            "-DUSE_LIBAV=OFF",
            "-DUSE_ENCRYPTION=OFF",
        ],
        ["cmake", "--build", str(bdir), "--target", "phigros_sdl_app", "--parallel", str(args.jobs)],
    ]


def web_commands(args: argparse.Namespace) -> list[list[str]]:
    return [
        [
            "emcmake",
            "cmake",
            "-S",
            str(CPP),
            "-B",
            str(build_dir(args)),
            f"-DCMAKE_BUILD_TYPE={args.build_type}",
            "-DBUILD_RENDER_APP=ON",
            "-DBUILD_LEGACY_CLI=OFF",
            "-DUSE_BGFX=OFF",
            "-DUSE_LIBAV=OFF",
            "-DUSE_SDL3=OFF",
        ],
        ["cmake", "--build", str(build_dir(args)), "--target", "phigros_sdl_app", "--parallel", str(args.jobs)],
    ]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build MinimalPhigrosRend targets.")
    parser.add_argument("--list", action="store_true", help="List profiles.")
    parser.add_argument("--profile", choices=PROFILES.keys(), default="desktop")
    parser.add_argument("--build-type", default="Release", choices=["Debug", "Release", "RelWithDebInfo"])
    parser.add_argument("--jobs", type=int, default=jobs())
    parser.add_argument("--target")
    parser.add_argument("--clean", action="store_true")
    parser.add_argument("--print-only", action="store_true")
    parser.add_argument("--legacy-cli", action="store_true", help="Also enable the legacy phigros_render target.")
    parser.add_argument("--enable-bgfx", action="store_true")
    parser.add_argument("--disable-sdl3", action="store_true")
    parser.add_argument("--disable-libav", action="store_true")
    parser.add_argument("--enable-lzma", action="store_true")
    parser.add_argument("--disable-encryption", action="store_true")
    parser.add_argument("--abi", default="arm64-v8a")
    parser.add_argument("--api", type=int, default=24)
    parser.add_argument("extra_cmake_args", nargs=argparse.REMAINDER)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.list:
        for name, desc in PROFILES.items():
            print(f"{name:8} {desc}")
        return 0

    bdir = build_dir(args)
    if args.clean and bdir.exists() and not args.print_only:
        shutil.rmtree(bdir)

    if args.profile == "android":
        commands = android_commands(args)
    elif args.profile == "web":
        commands = web_commands(args)
    else:
        commands = [
            cmake_configure_args(args),
            ["cmake", "--build", str(bdir), "--target", target_for(args), "--parallel", str(args.jobs)],
        ]

    for command in commands:
        rc = run(command, ROOT, args.print_only)
        if rc != 0:
            return rc
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
