"""Shared primitives for the phigros_ui package.

This module is the single source of truth for:
  * Default paths (binary, respack, config, charts dir)
  * Build profile definitions and :class:`BuildRequest` -> command list
  * Chart discovery (delegates to ``phigros_cpp.scan_charts_directory`` if
    present, falls back to a simple filesystem walk).
  * :class:`RendererOptions` — a dataclass that fully describes a renderer
    invocation.  Every UI tab builds one of these and hands it to
    :func:`renderer_command`, which produces the final argv.

The legacy :func:`launch_binary_command` free function is preserved as a
thin wrapper over :class:`RendererOptions` so that ``scripts/build.py`` and
any third-party scripts continue to work unmodified.
"""

from __future__ import annotations

import dataclasses
import os
import shlex
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable

ROOT_DIR = Path(__file__).resolve().parent.parent
CPP_DIR = ROOT_DIR / "cpp"
CONFIG_DIR = ROOT_DIR / "config"
DEFAULT_RESPACK = ROOT_DIR / "respack.zip"
DEFAULT_CONFIG = CONFIG_DIR / "config.jsonc"
DEFAULT_BINARY = CPP_DIR / "build" / "phigros_render"
DEFAULT_CHARTS_DIR = ROOT_DIR / "charts"
INTERNAL_ASSET_LABEL = "<internal>"

BUILD_PROFILES = {
    "desktop": "Native renderer/player for the current desktop platform.",
    "python": "Python bindings via pybind11/scikit-build-compatible CMake target.",
    "web": "WebAssembly build through Emscripten.",
    "android": "Android shared library for the Gradle wrapper project.",
    "ios": "iOS/Xcode-oriented renderer build.",
    "tests": "Native test/tool build with optional test execution.",
}


# --------------------------------------------------------------------------- #
# Build requests                                                              #
# --------------------------------------------------------------------------- #


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


def is_internal_asset_path(path: str) -> bool:
    lower = path.lower()
    return ".zip:" in lower or ".pez:" in lower


def discover_charts(charts_dir: Path) -> list[dict[str, str]]:
    results: list[dict[str, str]] = []
    if not charts_dir.is_dir():
        return results

    try:
        from phigros_cpp import scan_charts_directory  # type: ignore

        for entry in scan_charts_directory(str(charts_dir)):
            chart_path = getattr(entry, "chart_path", "")
            assets = getattr(entry, "assets", None)
            audio_path = getattr(assets, "music_path", "") if assets is not None else ""
            bg_path = getattr(assets, "illustration_path", "") if assets is not None else ""
            name = getattr(entry, "name", Path(chart_path).stem)
            difficulty = getattr(entry, "difficulty", "")
            source_type = getattr(entry, "source_type", "")
            suffix = f" [{difficulty}]" if difficulty else ""
            source = f" ({source_type})" if source_type else ""
            results.append(
                {
                    "label": f"{name}{suffix}{source}",
                    "path": chart_path,
                    "audio": INTERNAL_ASSET_LABEL if is_internal_asset_path(audio_path) else audio_path,
                    "bg": INTERNAL_ASSET_LABEL if is_internal_asset_path(bg_path) else bg_path,
                    "name": name,
                    "difficulty": difficulty,
                    "source_type": source_type,
                }
            )
        if results:
            return results
    except Exception:
        pass

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
                    "name": folder.name,
                    "difficulty": chart_file.stem,
                    "source_type": "folder",
                }
            )

    for packed in sorted(charts_dir.glob("*.zip")):
        results.append(
            {
                "label": f"{packed.stem} [.zip]",
                "path": str(packed),
                "audio": "",
                "bg": "",
                "name": packed.stem,
                "difficulty": "",
                "source_type": "zip",
            }
        )

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
            "cmake", "-S", str(CPP_DIR), "-B", str(build_dir),
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
            "cmake", "-S", str(CPP_DIR), "-B", str(build_dir),
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
            "emcmake", "cmake", "-S", str(CPP_DIR), "-B", str(build_dir),
            f"-DCMAKE_BUILD_TYPE={request.build_type}",
            "-DUSE_BGFX=OFF", "-DUSE_SDL3=OFF", "-DUSE_LIBAV=OFF",
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
            "cmake", "-S", str(CPP_DIR), "-B", str(build_dir),
            f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
            f"-DANDROID_ABI={request.android_abi}",
            f"-DANDROID_PLATFORM=android-{request.android_api}",
            "-DANDROID_STL=c++_shared",
            f"-DCMAKE_BUILD_TYPE={request.build_type}",
            "-DPHIGROS_ANDROID_LIB=ON",
            "-DUSE_BGFX=OFF", "-DUSE_SDL3=OFF",
        ] + extra
        commands.extend([configure, common_build + ["--target", request.build_target or "phigros_render"]])
        return commands

    if request.profile == "ios":
        configure = [
            "cmake", "-S", str(CPP_DIR), "-B", str(build_dir),
            "-G", "Xcode",
            "-DCMAKE_SYSTEM_NAME=iOS",
            f"-DCMAKE_BUILD_TYPE={request.build_type}",
            "-DUSE_BGFX=OFF", "-DUSE_SDL3=OFF", "-DUSE_LIBAV=OFF",
        ] + extra
        commands.extend([configure, ["cmake", "--build", str(build_dir), "--config", request.build_type]])
        return commands

    if request.profile == "tests":
        configure = [
            "cmake", "-S", str(CPP_DIR), "-B", str(build_dir),
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


# --------------------------------------------------------------------------- #
# Renderer invocations — unified interface                                    #
# --------------------------------------------------------------------------- #


# Ordered flag/value pairs, matching the CLI.  Kept as a module-level tuple so
# ``renderer_command`` and any preset/persistence code share the same ordering.
_RENDERER_VALUE_FLAGS: tuple[tuple[str, str], ...] = (
    ("respack", "--respack"),
    ("config_path", "--config"),
    ("bg_path", "--bg"),
    ("font_path", "--font"),
    ("audio_path", "--audio"),
    ("screenshot_dir", "--screenshot-dir"),
    ("screenshot_fps", "--screenshot-fps"),
    ("duration", "--duration"),
    ("width", "--width"),
    ("height", "--height"),
    ("approach", "--approach"),
    ("chart_speed", "--chart-speed"),
    ("expand", "--expand"),
    ("note_scale_x", "--note-scale-x"),
    ("note_scale_y", "--note-scale-y"),
    ("note_alpha", "--note-alpha"),
    ("font_size", "--font-size"),
    ("mode", "--mode"),
    ("backend", "--backend"),
    ("record_output", "--record"),
    ("record_preset", "--record-preset"),
    ("record_codec", "--record-codec"),
    ("record_hw", "--record-hw"),
    ("record_fps", "--record-fps"),
    ("sim_fps", "--sim-fps"),
    ("record_resolution", "--record-resolution"),
    ("record_capture_resolution", "--record-capture-resolution"),
    ("record_queue_depth", "--record-queue-depth"),
    ("record_start", "--record-start"),
    ("record_end", "--record-end"),
    ("compile_output", "--compile"),
    ("sample_rate", "--sample-rate"),
    ("compress_algo", "--compress"),
    ("encrypt_algo", "--encrypt"),
    ("password", "--password"),
    ("scriptplay_path", "--scriptplay"),
    ("script_path", "--script"),
    ("save_replay_path", "--save-replay"),
    ("play_replay_path", "--play-replay"),
    ("debug_flags", "--debug-flags"),
    ("log_level", "--log-level"),
    ("log_filter", "--log-filter"),
    ("log_file", "--log-file"),
    ("audio_offset_ms", "--audio-offset"),
    ("playback_speed", "--playback-speed"),
    ("benchmark_iterations", "--benchmark-iterations"),
    ("list_charts_dir", "--list-charts"),
    ("dump_frame_t", "--dump-frame"),
)

_RENDERER_TOGGLE_FLAGS: tuple[tuple[str, str], ...] = (
    ("headless", "--headless"),
    ("score_only", "--score-only"),
    ("benchmark", "--benchmark"),
    ("play_mode", "--play"),
    ("profile_mode", "--profile"),
    ("record_profile", "--record-profile"),
    ("overlay_transparent", "--overlay-transparent"),
    ("log_no_color", "--log-no-color"),
    ("log_time", "--log-time"),
    ("truncate_at_duration", "--truncate-at-duration"),
    ("info_mode", "--info"),
)


@dataclass
class RendererOptions:
    """Complete, serialisable description of one ``phigros_render`` invocation.

    Field names match the legacy :func:`launch_binary_command` keyword arguments
    so presets saved from older launchers remain loadable.  New code should
    prefer constructing a :class:`RendererOptions` directly and calling
    :meth:`build_command`.
    """

    binary: str = str(DEFAULT_BINARY)
    chart_path: str = ""

    # value flags
    respack: str = ""
    config_path: str = ""
    bg_path: str = ""
    font_path: str = ""
    audio_path: str = ""
    screenshot_dir: str = ""
    screenshot_fps: str = ""
    duration: str = ""
    width: str = ""
    height: str = ""
    approach: str = ""
    chart_speed: str = ""
    expand: str = ""
    note_scale_x: str = ""
    note_scale_y: str = ""
    note_alpha: str = ""
    font_size: str = ""
    mode: str = ""
    backend: str = ""
    record_output: str = ""
    record_preset: str = ""
    record_codec: str = ""
    record_hw: str = ""
    record_fps: str = ""
    sim_fps: str = ""
    record_resolution: str = ""
    record_capture_resolution: str = ""
    record_queue_depth: str = ""
    record_start: str = ""
    record_end: str = ""
    compile_output: str = ""
    sample_rate: str = ""
    compress_algo: str = ""
    encrypt_algo: str = ""
    password: str = ""
    scriptplay_path: str = ""
    script_path: str = ""
    save_replay_path: str = ""
    play_replay_path: str = ""
    debug_flags: str = ""
    log_level: str = ""
    log_filter: str = ""
    log_file: str = ""
    audio_offset_ms: str = ""
    playback_speed: str = ""
    benchmark_iterations: str = ""
    list_charts_dir: str = ""
    dump_frame_t: str = ""

    # toggles
    headless: bool = False
    score_only: bool = False
    benchmark: bool = False
    play_mode: bool = False
    profile_mode: bool = False
    record_profile: bool = False
    overlay_transparent: bool = False
    log_no_color: bool = False
    log_time: bool = False
    truncate_at_duration: bool = False
    info_mode: bool = False

    # repeatable mod files (each emits --mod <path>)
    mod_paths: list = field(default_factory=list)

    # free-form suffix, appended after everything else
    extra_args: str = ""

    # ------------------------------------------------------------------ #
    # command assembly                                                   #
    # ------------------------------------------------------------------ #

    def build_command(self) -> list[str]:
        if not self.binary:
            raise ValueError("Binary path is required.")
        if not self.chart_path and not self.script_path:
            raise ValueError("Chart path or chart script path is required.")

        command: list[str] = [self.binary]
        if self.chart_path:
            command.append(self.chart_path)

        for attr, flag in _RENDERER_VALUE_FLAGS:
            value = str(getattr(self, attr, "") or "").strip()
            if value:
                command.extend([flag, value])

        for attr, flag in _RENDERER_TOGGLE_FLAGS:
            if bool(getattr(self, attr, False)):
                command.append(flag)

        for mod_path in (self.mod_paths or []):
            if mod_path.strip():
                command.extend(["--mod", mod_path.strip()])

        if self.extra_args.strip():
            command.extend(shlex.split(self.extra_args))
        return command

    # ------------------------------------------------------------------ #
    # serialisation                                                      #
    # ------------------------------------------------------------------ #

    def to_dict(self) -> dict[str, object]:
        return dataclasses.asdict(self)

    @classmethod
    def from_dict(cls, data: dict[str, object]) -> "RendererOptions":
        known = {f.name for f in dataclasses.fields(cls)}
        return cls(**{k: v for k, v in data.items() if k in known})


def renderer_command(options: RendererOptions) -> list[str]:
    """Convenience free-function form of :meth:`RendererOptions.build_command`."""
    return options.build_command()


def launch_binary_command(**kwargs) -> list[str]:
    """Legacy kwargs-driven constructor — builds :class:`RendererOptions` then
    delegates to :meth:`RendererOptions.build_command`.  Preserved so that
    ``scripts/launcher_common.py`` re-exports and third-party callers keep
    working unchanged."""
    return RendererOptions.from_dict(kwargs).build_command()


# --------------------------------------------------------------------------- #
# Clipboard helper (works on macOS/X11/Wayland; falls back to Qt if loaded)   #
# --------------------------------------------------------------------------- #


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
    # Last-ditch: QApplication clipboard, if a Qt app is already running.
    try:
        from PySide6.QtWidgets import QApplication  # type: ignore

        app = QApplication.instance()
        if app is not None:
            app.clipboard().setText(text)
            return True
    except Exception:
        pass
    return False
