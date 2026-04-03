#!/usr/bin/env python3
from __future__ import annotations

import argparse
import sys
from pathlib import Path

from launcher_common import BUILD_PROFILES, BuildRequest, build_commands, build_output_dir, cpu_jobs, format_command, run_commands


def ask_choice(prompt: str, options: list[str], default: str) -> str:
    print(prompt)
    for index, option in enumerate(options, start=1):
        marker = " (default)" if option == default else ""
        print(f"  {index}. {option}{marker}")
    raw = input("> ").strip()
    if not raw:
        return default
    if raw.isdigit():
        idx = int(raw) - 1
        if 0 <= idx < len(options):
            return options[idx]
    if raw in options:
        return raw
    print(f"Invalid choice, using {default}.")
    return default


def ask_bool(prompt: str, default: bool) -> bool:
    suffix = "Y/n" if default else "y/N"
    raw = input(f"{prompt} [{suffix}] ").strip().lower()
    if not raw:
        return default
    return raw in {"y", "yes", "1", "true"}


def ask_text(prompt: str, default: str = "") -> str:
    label = f"{prompt} [{default}] " if default else f"{prompt} "
    raw = input(label).strip()
    return raw or default


def interactive_request() -> BuildRequest:
    profile = ask_choice("Select a build profile:", list(BUILD_PROFILES.keys()), "desktop")
    build_type = ask_choice("Build type:", ["Release", "Debug", "RelWithDebInfo"], "Release")
    jobs_raw = ask_text("Parallel jobs", str(cpu_jobs()))
    request = BuildRequest(profile=profile, build_type=build_type, jobs=int(jobs_raw or cpu_jobs()))
    request.clean = ask_bool("Delete the existing build directory first?", False)

    if profile in {"desktop", "python", "tests"}:
        request.use_lzma = ask_bool("Enable LZMA support?", False)
        request.use_encryption = ask_bool("Enable OpenSSL encryption support?", True)
        request.use_sanitizers = ask_bool("Enable sanitizers?", build_type == "Debug")
    if profile == "desktop":
        request.use_bgfx = ask_bool("Enable bgfx renderer backend?", False)
        request.use_sdl3 = ask_bool("Use SDL3?", True)
        request.use_libav = ask_bool("Enable libav integration?", True)
        request.build_target = ask_text("Optional CMake target (blank = default)", "")
    elif profile == "python":
        request.use_libav = ask_bool("Enable libav while building bindings?", False)
        request.build_target = ask_text("Python target", "_core")
    elif profile == "web":
        request.use_lzma = ask_bool("Enable LZMA in wasm build?", False)
    elif profile == "android":
        request.android_abi = ask_choice(
            "Android ABI:",
            ["arm64-v8a", "armeabi-v7a", "x86", "x86_64"],
            "arm64-v8a",
        )
        request.android_api = int(ask_text("Android API level", "24"))
    elif profile == "tests":
        request.build_target = ask_text("Test target", "run-tests")
        request.run_tests = request.build_target != "run-tests" and ask_bool("Run built test binaries after build?", True)

    extra = ask_text("Extra CMake args (space separated)", "")
    if extra:
        import shlex

        request.extra_cmake_args = shlex.split(extra)
    return request


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Interactive build orchestrator for MinimalPhigrosRend.")
    parser.add_argument("--list", action="store_true", help="List available build profiles.")
    parser.add_argument("--profile", choices=list(BUILD_PROFILES.keys()), help="Build profile to execute.")
    parser.add_argument("--build-type", default="Release", choices=["Release", "Debug", "RelWithDebInfo"])
    parser.add_argument("--jobs", type=int, default=cpu_jobs())
    parser.add_argument("--clean", action="store_true", help="Delete the selected build directory before configuring.")
    parser.add_argument("--target", help="Optional CMake build target override.")
    parser.add_argument("--abi", default="arm64-v8a", help="Android ABI.")
    parser.add_argument("--api", type=int, default=24, help="Android API level.")
    parser.add_argument("--enable-bgfx", action="store_true")
    parser.add_argument("--disable-sdl3", action="store_true")
    parser.add_argument("--disable-libav", action="store_true")
    parser.add_argument("--enable-lzma", action="store_true")
    parser.add_argument("--disable-encryption", action="store_true")
    parser.add_argument("--sanitizers", action="store_true")
    parser.add_argument("--run-tests", action="store_true", help="Run test binaries after a non run-tests build.")
    parser.add_argument("--print-only", action="store_true", help="Print commands without executing them.")
    parser.add_argument("extra_cmake_args", nargs=argparse.REMAINDER, help="Extra args passed to the configure command.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.list:
        for name, description in BUILD_PROFILES.items():
            print(f"{name:8} {description}")
        return 0

    if not args.profile:
        if not sys.stdin.isatty():
            print("--profile is required in non-interactive mode.", file=sys.stderr)
            return 2
        request = interactive_request()
    else:
        extra = list(args.extra_cmake_args)
        if extra and extra[0] == "--":
            extra = extra[1:]
        request = BuildRequest(
            profile=args.profile,
            build_type=args.build_type,
            jobs=args.jobs,
            clean=args.clean,
            build_target=args.target,
            use_bgfx=args.enable_bgfx,
            use_sdl3=not args.disable_sdl3,
            use_libav=not args.disable_libav,
            use_lzma=args.enable_lzma,
            use_encryption=not args.disable_encryption,
            use_sanitizers=args.sanitizers,
            android_abi=args.abi,
            android_api=args.api,
            run_tests=args.run_tests,
            extra_cmake_args=extra,
        )

    try:
        commands = build_commands(request)
    except Exception as exc:
        print(f"Build setup failed: {exc}", file=sys.stderr)
        return 1

    print(f"Profile: {request.profile}")
    print(f"Build directory: {build_output_dir(request)}")
    print("Planned commands:")
    for command in commands:
        print(f"  {format_command(command)}")

    if args.print_only:
        return 0

    if not args.profile:
        if not ask_bool("Execute these commands now?", True):
            return 0
    return run_commands(commands)


if __name__ == "__main__":
    raise SystemExit(main())
