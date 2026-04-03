#!/usr/bin/env python3
from __future__ import annotations

import os
import shlex
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parent.parent
CPP_DIR = ROOT_DIR / "cpp"
CONFIG_DIR = ROOT_DIR / "config"
DEFAULT_RESPACK = ROOT_DIR / "respack.zip"
DEFAULT_CONFIG = CONFIG_DIR / "config.jsonc"
DEFAULT_BINARY = CPP_DIR / "build" / "phigros_render"
DEFAULT_CHARTS_DIR = ROOT_DIR / "charts"

BUILD_PROFILES = {
    "desktop": "Native renderer/player for the current desktop platform.",
    "python": "Python bindings via pybind11/scikit-build-compatible CMake target.",
    "web": "WebAssembly build through Emscripten.",
    "android": "Android shared library for the Gradle wrapper project.",
    "ios": "iOS/Xcode-oriented renderer build.",
    "tests": "Native test/tool build with optional test execution.",
}


@dataclass
class BuildRequest:
    profile: str
    build_type: str = "Release"
    jobs: int | None = None
    use_bgfx: bool = False
    use_sdl3: bool = True
    use_libav: bool = True
    use_lzma: bool = False
    use_encryption: bool = True
    use_sanitizers: bool = False
    android_abi: str = "arm64-v8a"
    android_api: int = 24
    run_tests: bool = False
    clean: bool = False
    extra_cmake_args: list[str] | None = None
    build_target: str | None = None


def format_command(cmd: list[str]) -> str:
    return " ".join(shlex.quote(part) for part in cmd)


def run_commands(commands: list[list[str]], cwd: Path = ROOT_DIR) -> int:
    for command in commands:
        print(f"$ {format_command(command)}")
        completed = subprocess.run(command, cwd=cwd)
        if completed.returncode != 0:
            return completed.returncode
    return 0


def cpu_jobs() -> int:
    count = os.cpu_count() or 4
    return max(1, count)


def discover_charts(charts_dir: Path) -> list[dict[str, str]]:
    results: list[dict[str, str]] = []
    if not charts_dir.is_dir():
        return results

    for folder in sorted(charts_dir.iterdir()):
        if not folder.is_dir():
            continue
        for chart_file in sorted(folder.glob("*.json")):
            audio = next(iter(folder.glob("*.ogg")), None) or next(iter(folder.glob("*.mp3")), None)
            bg = next(iter(folder.glob("*.png")), None) or next(iter(folder.glob("*.jpg")), None)
            results.append(
                {
                    "label": f"{folder.name} / {chart_file.stem}",
                    "path": str(chart_file),
                    "audio": str(audio) if audio else "",
                    "bg": str(bg) if bg else "",
                }
            )

    for packed in sorted(charts_dir.glob("*.zip")):
        results.append({"label": f"{packed.stem} [.zip]", "path": str(packed), "audio": "", "bg": ""})

    return results


def _cmake_bool(value: bool) -> str:
    return "ON" if value else "OFF"


def _android_ndk() -> str:
    candidates = [
        os.environ.get("ANDROID_NDK_ROOT"),
        os.environ.get("ANDROID_NDK"),
        str(Path.home() / "Android/Sdk/ndk-bundle"),
        str(Path.home() / "Library/Android/sdk/ndk-bundle"),
        "/opt/android-ndk",
        "/usr/local/android-ndk",
    ]
    for candidate in candidates:
        if candidate and Path(candidate).is_dir():
            return candidate
    return ""


def build_output_dir(request: BuildRequest) -> Path:
    if request.profile == "desktop":
        return CPP_DIR / "build"
    if request.profile == "python":
        return CPP_DIR / "build_py"
    if request.profile == "web":
        return CPP_DIR / "build_web"
    if request.profile == "android":
        return CPP_DIR / f"build_android_{request.android_abi}"
    if request.profile == "ios":
        return CPP_DIR / "build_ios"
    if request.profile == "tests":
        return CPP_DIR / "build_tests"
    raise ValueError(f"Unsupported profile: {request.profile}")


def build_commands(request: BuildRequest) -> list[list[str]]:
    extra = request.extra_cmake_args or []
    build_dir = build_output_dir(request)
    commands: list[list[str]] = []

    if request.clean and build_dir.exists():
        shutil.rmtree(build_dir)

    common_build = ["cmake", "--build", str(build_dir), "--parallel", str(request.jobs or cpu_jobs())]
    if request.profile == "desktop":
        configure = [
            "cmake",
            "-S",
            str(CPP_DIR),
            "-B",
            str(build_dir),
            f"-DCMAKE_BUILD_TYPE={request.build_type}",
            f"-DUSE_BGFX={_cmake_bool(request.use_bgfx)}",
            f"-DUSE_SDL3={_cmake_bool(request.use_sdl3)}",
            f"-DUSE_LIBAV={_cmake_bool(request.use_libav)}",
            f"-DUSE_LZMA={_cmake_bool(request.use_lzma)}",
            f"-DUSE_ENCRYPTION={_cmake_bool(request.use_encryption)}",
            f"-DUSE_SANITIZERS={_cmake_bool(request.use_sanitizers)}",
        ] + extra
        commands.extend([configure, common_build + (["--target", request.build_target] if request.build_target else [])])
        return commands

    if request.profile == "python":
        configure = [
            "cmake",
            "-S",
            str(CPP_DIR),
            "-B",
            str(build_dir),
            f"-DCMAKE_BUILD_TYPE={request.build_type}",
            "-DBUILD_PYTHON_BINDINGS=ON",
            "-DBUILD_RENDER_APP=OFF",
            f"-DUSE_BGFX={_cmake_bool(request.use_bgfx)}",
            f"-DUSE_LIBAV={_cmake_bool(request.use_libav)}",
            f"-DUSE_LZMA={_cmake_bool(request.use_lzma)}",
            f"-DUSE_ENCRYPTION={_cmake_bool(request.use_encryption)}",
            f"-DUSE_SANITIZERS={_cmake_bool(request.use_sanitizers)}",
        ] + extra
        commands.extend([configure, common_build + ["--target", request.build_target or "_core"]])
        return commands

    if request.profile == "web":
        configure = [
            "emcmake",
            "cmake",
            "-S",
            str(CPP_DIR),
            "-B",
            str(build_dir),
            f"-DCMAKE_BUILD_TYPE={request.build_type}",
            "-DUSE_BGFX=OFF",
            "-DUSE_SDL3=OFF",
            "-DUSE_LIBAV=OFF",
            f"-DUSE_LZMA={_cmake_bool(request.use_lzma)}",
            "-DUSE_ENCRYPTION=OFF",
        ] + extra
        commands.extend([configure, ["emmake"] + common_build + ["--target", request.build_target or "phigros_render"]])
        return commands

    if request.profile == "android":
        ndk = _android_ndk()
        if not ndk:
            raise RuntimeError("Android NDK not found. Set ANDROID_NDK_ROOT or ANDROID_NDK.")
        toolchain = Path(ndk) / "build/cmake/android.toolchain.cmake"
        if not toolchain.is_file():
            raise RuntimeError(f"android.toolchain.cmake not found: {toolchain}")
        configure = [
            "cmake",
            "-S",
            str(CPP_DIR),
            "-B",
            str(build_dir),
            f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
            f"-DANDROID_ABI={request.android_abi}",
            f"-DANDROID_PLATFORM=android-{request.android_api}",
            "-DANDROID_STL=c++_shared",
            f"-DCMAKE_BUILD_TYPE={request.build_type}",
            "-DPHIGROS_ANDROID_LIB=ON",
            "-DUSE_BGFX=OFF",
            "-DUSE_SDL3=OFF",
        ] + extra
        commands.extend([configure, common_build + ["--target", request.build_target or "phigros_render"]])
        return commands

    if request.profile == "ios":
        configure = [
            "cmake",
            "-S",
            str(CPP_DIR),
            "-B",
            str(build_dir),
            "-G",
            "Xcode",
            "-DCMAKE_SYSTEM_NAME=iOS",
            f"-DCMAKE_BUILD_TYPE={request.build_type}",
            "-DUSE_BGFX=OFF",
            "-DUSE_SDL3=OFF",
            "-DUSE_LIBAV=OFF",
        ] + extra
        commands.extend([configure, ["cmake", "--build", str(build_dir), "--config", request.build_type]])
        return commands

    if request.profile == "tests":
        configure = [
            "cmake",
            "-S",
            str(CPP_DIR),
            "-B",
            str(build_dir),
            f"-DCMAKE_BUILD_TYPE={request.build_type}",
            "-DBUILD_RENDER_APP=OFF",
            f"-DUSE_LZMA={_cmake_bool(request.use_lzma)}",
            f"-DUSE_ENCRYPTION={_cmake_bool(request.use_encryption)}",
            f"-DUSE_SANITIZERS={_cmake_bool(request.use_sanitizers)}",
        ] + extra
        target = request.build_target or "run-tests"
        commands.extend([configure, common_build + ["--target", target]])
        if request.run_tests and target != "run-tests":
            for name in ("test_easing", "test_engine", "test_parser", "test_logger", "test_zip_extract", "verify_chart"):
                test_binary = build_dir / name
                if test_binary.exists():
                    commands.append([str(test_binary)])
        return commands

    raise ValueError(f"Unsupported profile: {request.profile}")


def launch_binary_command(
    *,
    binary: str,
    chart_path: str,
    respack: str = "",
    config_path: str = "",
    bg_path: str = "",
    font_path: str = "",
    audio_path: str = "",
    screenshot_dir: str = "",
    screenshot_fps: str = "",
    duration: str = "",
    width: str = "",
    height: str = "",
    mode: str = "",
    backend: str = "",
    record_output: str = "",
    compile_output: str = "",
    scriptplay_path: str = "",
    script_path: str = "",
    save_replay_path: str = "",
    play_replay_path: str = "",
    debug_flags: str = "",
    log_level: str = "",
    log_filter: str = "",
    log_file: str = "",
    audio_offset_ms: str = "",
    playback_speed: str = "",
    benchmark_iterations: str = "",
    headless: bool = False,
    score_only: bool = False,
    benchmark: bool = False,
    play_mode: bool = False,
    profile_mode: bool = False,
    record_profile: bool = False,
    overlay_transparent: bool = False,
    log_no_color: bool = False,
    log_time: bool = False,
    truncate_at_duration: bool = False,
    extra_args: str = "",
) -> list[str]:
    if not binary:
        raise ValueError("Binary path is required.")
    if not chart_path and not script_path:
        raise ValueError("Chart path or chart script path is required.")

    command = [binary]
    if chart_path:
        command.append(chart_path)

    pairs = [
        ("--respack", respack),
        ("--config", config_path),
        ("--bg", bg_path),
        ("--font", font_path),
        ("--audio", audio_path),
        ("--screenshot-dir", screenshot_dir),
        ("--screenshot-fps", screenshot_fps),
        ("--duration", duration),
        ("--width", width),
        ("--height", height),
        ("--mode", mode),
        ("--backend", backend),
        ("--record", record_output),
        ("--compile", compile_output),
        ("--scriptplay", scriptplay_path),
        ("--script", script_path),
        ("--save-replay", save_replay_path),
        ("--play-replay", play_replay_path),
        ("--debug-flags", debug_flags),
        ("--log-level", log_level),
        ("--log-filter", log_filter),
        ("--log-file", log_file),
        ("--audio-offset", audio_offset_ms),
        ("--playback-speed", playback_speed),
        ("--benchmark-iterations", benchmark_iterations),
    ]
    for flag, value in pairs:
        if str(value).strip():
            command.extend([flag, str(value).strip()])

    toggles = [
        ("--headless", headless),
        ("--score-only", score_only),
        ("--benchmark", benchmark),
        ("--play", play_mode),
        ("--profile", profile_mode),
        ("--record-profile", record_profile),
        ("--overlay-transparent", overlay_transparent),
        ("--log-no-color", log_no_color),
        ("--log-time", log_time),
        ("--truncate-at-duration", truncate_at_duration),
    ]
    for flag, enabled in toggles:
        if enabled:
            command.append(flag)

    if extra_args.strip():
        command.extend(shlex.split(extra_args))
    return command


def clipboard_copy(text: str) -> bool:
    if sys.platform == "darwin" and shutil.which("pbcopy"):
        subprocess.run(["pbcopy"], input=text.encode("utf-8"), check=False)
        return True
    if shutil.which("xclip"):
        subprocess.run(["xclip", "-selection", "clipboard"], input=text.encode("utf-8"), check=False)
        return True
    if shutil.which("wl-copy"):
        subprocess.run(["wl-copy"], input=text.encode("utf-8"), check=False)
        return True
    return False
