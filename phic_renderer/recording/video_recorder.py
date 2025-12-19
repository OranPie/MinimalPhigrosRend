"""
Video recorder with ffmpeg integration.

Directly encodes frames to MP4/WEBM video with optional audio.
"""

import subprocess
import shutil
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

        try:
            self.process.stdin.write(frame_bytes)
        except Exception as e:
            raise RuntimeError(f"Failed to write frame: {e}")

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
