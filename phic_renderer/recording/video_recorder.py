"""
Video recorder with ffmpeg integration.

Directly encodes frames to MP4/WEBM video with optional audio.
"""

import os
import subprocess
import shutil
import time
from typing import Optional
import numpy as np
from .presets import EncodingPreset, get_preset


def check_ffmpeg() -> bool:
    """
    Check if ffmpeg is available in the system.

    Returns:
        True if ffmpeg is found, False otherwise
    """
    return shutil.which('ffmpeg') is not None


class VideoRecorder:
    """
    Records frames directly to video file using ffmpeg.

    Supports audio mux and configurable encoding presets.
    """

    def __init__(
        self,
        output_file: str,
        width: int,
        height: int,
        fps: float,
        preset: str = "balanced",
        audio_path: Optional[str] = None,
        codec: str = "libx264"
    ):
        """
        Initialize video recorder.

        Args:
            output_file: Output video file path
            width: Frame width
            height: Frame height
            fps: Target framerate
            preset: Encoding preset name (fast, balanced, quality, archive)
            audio_path: Optional path to audio file to mux
            codec: Video codec (libx264, libx265, libvpx-vp9)

        Raises:
            RuntimeError: If ffmpeg is not available
            ValueError: If preset is invalid
        """
        if not check_ffmpeg():
            raise RuntimeError("ffmpeg not found. Please install ffmpeg to use video recording.")

        self.output_file = output_file
        self.width = width
        self.height = height
        self.fps = fps
        self.audio_path = audio_path
        self.codec = codec

        # Get encoding preset
        preset_obj = get_preset(preset)
        if preset_obj is None:
            raise ValueError(f"Invalid preset: {preset}. Choose from: fast, balanced, quality, archive")

        self.preset = preset_obj
        self.process: Optional[subprocess.Popen] = None
        self.is_open = False

        self.frames_written = 0
        self.bytes_written = 0
        self.write_calls = 0
        self.write_time_sec = 0.0
        self.write_time_max_sec = 0.0
        self.slow_write_calls = 0
        self.open_wall_t0 = 0.0
        self.last_out_size_bytes: Optional[int] = None
        self.last_out_size_t: Optional[float] = None

    def _build_ffmpeg_command(self) -> list:
        """
        Build ffmpeg command line arguments.

        Returns:
            List of command arguments
        """
        cmd = [
            'ffmpeg',
            '-hide_banner',
            '-loglevel', 'error',
            '-nostats',
            '-y',  # Overwrite output file
            '-f', 'rawvideo',
            '-pix_fmt', 'rgb24',
            '-s', f'{self.width}x{self.height}',
            '-r', str(self.fps),
            '-i', 'pipe:0',  # Read from stdin
        ]

        # Add audio input if provided
        if self.audio_path:
            cmd.extend(['-i', self.audio_path])

        # Video codec settings
        cmd.extend([
            '-c:v', self.codec,
            '-preset', self.preset.ffmpeg_preset,
            '-crf', str(self.preset.crf),
            '-pix_fmt', self.preset.pixel_format,
        ])

        # Add extra codec arguments
        if self.preset.extra_args:
            cmd.extend(self.preset.extra_args)

        # Audio codec settings (if audio present)
        if self.audio_path:
            cmd.extend([
                '-c:a', 'aac',
                '-b:a', '192k',
            ])

        # Output file
        cmd.append(self.output_file)

        return cmd

    def open(self) -> None:
        """
        Start ffmpeg subprocess and open stdin pipe.

        Raises:
            RuntimeError: If ffmpeg process fails to start
        """
        cmd = self._build_ffmpeg_command()

        try:
            self.process = subprocess.Popen(
                cmd,
                stdin=subprocess.PIPE,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE
            )
            self.is_open = True
            self.open_wall_t0 = float(time.perf_counter())
            self.frames_written = 0
            self.bytes_written = 0
            self.write_calls = 0
            self.write_time_sec = 0.0
            self.write_time_max_sec = 0.0
            self.slow_write_calls = 0
            self.last_out_size_bytes = None
            self.last_out_size_t = None
        except Exception as e:
            raise RuntimeError(f"Failed to start ffmpeg: {e}")

    def write_frame(self, frame: np.ndarray) -> None:
        """
        Write a frame to the video.

        Args:
            frame: RGB frame data (H, W, 3) uint8

        Raises:
            ValueError: If recorder is not open
            RuntimeError: If write fails
        """
        if not self.is_open or self.process is None:
            raise ValueError("Recorder not open. Call open() first.")

        # Ensure frame is uint8
        if frame.dtype != np.uint8:
            frame = (frame * 255).astype(np.uint8)

        # Write raw RGB data to ffmpeg stdin
        try:
            self.process.stdin.write(frame.tobytes())
        except Exception as e:
            raise RuntimeError(f"Failed to write frame: {e}")

    def write_frame_bytes(self, frame_bytes: bytes) -> None:
        """
        Write a raw RGB24 frame (bytes) to the video.

        Args:
            frame_bytes: Raw RGB bytes, length must be width*height*3

        Raises:
            ValueError: If recorder is not open or buffer size mismatch
            RuntimeError: If write fails
        """
        if not self.is_open or self.process is None:
            raise ValueError("Recorder not open. Call open() first.")

        expected = int(self.width) * int(self.height) * 3
        if len(frame_bytes) != expected:
            raise ValueError(f"Invalid frame buffer size: got {len(frame_bytes)}, expected {expected}")

        t0 = float(time.perf_counter())
        try:
            self.process.stdin.write(frame_bytes)
        except Exception as e:
            raise RuntimeError(f"Failed to write frame: {e}")
        finally:
            dt = float(time.perf_counter()) - t0
            self.write_calls += 1
            self.write_time_sec += float(dt)
            if float(dt) > float(self.write_time_max_sec):
                self.write_time_max_sec = float(dt)
            if float(dt) >= 0.050:
                self.slow_write_calls += 1

        self.frames_written += 1
        self.bytes_written += int(len(frame_bytes))

    def write_audio(self, samples: np.ndarray, sample_rate: int) -> None:
        """
        Audio is provided via audio_path, not streamed.

        Args:
            samples: Ignored
            sample_rate: Ignored

        Note:
            Audio is muxed from the file specified in __init__
        """
        pass  # Audio is handled via file input, not streaming

    def close(self) -> None:
        """
        Finalize video and close ffmpeg process.

        Raises:
            RuntimeError: If ffmpeg encoding fails
        """
        if not self.is_open or self.process is None:
            return

        try:
            # Finalize: signal EOF to ffmpeg, then drain pipes.
            # Important: communicate() may attempt to flush stdin; avoid passing a closed file.
            if self.process.stdin is not None:
                try:
                    self.process.stdin.close()
                except:
                    pass
                self.process.stdin = None

            _stdout, stderr = self.process.communicate(timeout=30)

            # Check return code
            if self.process.returncode != 0:
                error_msg = stderr.decode('utf-8', errors='ignore')
                raise RuntimeError(f"ffmpeg encoding failed: {error_msg}")

        except subprocess.TimeoutExpired:
            self.process.kill()
            raise RuntimeError("ffmpeg process timed out")
        except Exception as e:
            if self.process:
                self.process.kill()
            raise RuntimeError(f"Failed to finalize video: {e}")
        finally:
            self.is_open = False
            self.process = None

    def get_stats(self) -> dict:
        if not self.is_open:
            return {}

        now = float(time.perf_counter())
        wall = max(1e-9, now - float(self.open_wall_t0))
        fps_wall = float(self.frames_written) / float(wall)
        mbps_in = (float(self.bytes_written) / float(wall)) / (1024.0 * 1024.0)
        avg_write_ms = (float(self.write_time_sec) / max(1.0, float(self.write_calls))) * 1000.0
        max_write_ms = float(self.write_time_max_sec) * 1000.0

        out_size = None
        try:
            if self.output_file and os.path.exists(str(self.output_file)):
                out_size = int(os.path.getsize(str(self.output_file)))
        except:
            out_size = None

        out_mbps = None
        if out_size is not None:
            try:
                if self.last_out_size_bytes is not None and self.last_out_size_t is not None:
                    dt = max(1e-6, now - float(self.last_out_size_t))
                    out_mbps = ((float(out_size) - float(self.last_out_size_bytes)) / float(dt)) / (1024.0 * 1024.0)
                self.last_out_size_bytes = int(out_size)
                self.last_out_size_t = float(now)
            except:
                out_mbps = None

        return {
            "frames_written": int(self.frames_written),
            "bytes_written": int(self.bytes_written),
            "fps_wall": float(fps_wall),
            "mbps_in": float(mbps_in),
            "avg_write_ms": float(avg_write_ms),
            "max_write_ms": float(max_write_ms),
            "slow_write_calls": int(self.slow_write_calls),
            "out_size_bytes": out_size,
            "out_mbps": out_mbps,
            "codec": str(self.codec),
            "preset": str(getattr(self.preset, "name", "")),
            "has_audio": bool(self.audio_path),
        }

    def supports_audio(self) -> bool:
        """
        Check if audio is supported.

        Returns:
            True - video recorder supports audio muxing
        """
        return True

    def get_output_path(self) -> Optional[str]:
        """
        Get output file path.

        Returns:
            Path to output video file
        """
        return self.output_file
