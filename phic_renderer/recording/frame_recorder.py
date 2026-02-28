"""
Frame recorder for PNG sequence output.

Maintains backward compatibility with existing frame-by-frame recording.
"""

import os
from typing import Optional
import numpy as np
from PIL import Image


class FrameRecorder:
    """
    Records frames as PNG image sequence.

    Maintains backward compatibility with original recording mode.
    No audio support - frames only.
    """

    def __init__(self, output_dir: str, width: int, height: int, fps: float):
        """
        Initialize frame recorder.

        Args:
            output_dir: Directory to save PNG frames
            width: Frame width
            height: Frame height
            fps: Target framerate (for reference only)
        """
        self.output_dir = output_dir
        self.width = width
        self.height = height
        self.fps = fps
        self.frame_count = 0
        self.is_open = False

    def open(self) -> None:
        """Create output directory if it doesn't exist."""
        os.makedirs(self.output_dir, exist_ok=True)
        self.is_open = True
        self.frame_count = 0

    def write_frame(self, frame: np.ndarray) -> None:
        """
        Write a frame as PNG.

        Args:
            frame: RGB frame data (H, W, 3) uint8

        Raises:
            ValueError: If recorder is not open
        """
        if not self.is_open:
            raise ValueError("Recorder not open. Call open() first.")

        # Generate filename with zero-padded frame number
        filename = f"frame_{self.frame_count:06d}.png"
        filepath = os.path.join(self.output_dir, filename)

        # Convert numpy array to PIL Image and save
        if frame.dtype != np.uint8:
            frame = (frame * 255).astype(np.uint8)

        img = Image.fromarray(frame, mode='RGB')
        img.save(filepath, 'PNG')

        self.frame_count += 1

    def write_frame_bytes(self, frame_bytes: bytes) -> None:
        """Write a frame from raw RGB24 bytes.

        Args:
            frame_bytes: Raw RGB bytes, length must be width*height*3

        Raises:
            ValueError: If recorder is not open or buffer size mismatch
        """
        if not self.is_open:
            raise ValueError("Recorder not open. Call open() first.")

        expected = int(self.width) * int(self.height) * 3
        if len(frame_bytes) != expected:
            raise ValueError(f"Invalid frame buffer size: got {len(frame_bytes)}, expected {expected}")

        filename = f"frame_{self.frame_count:06d}.png"
        filepath = os.path.join(self.output_dir, filename)

        img = Image.frombytes('RGB', (int(self.width), int(self.height)), frame_bytes)
        img.save(filepath, 'PNG')

        self.frame_count += 1

    def write_audio(self, samples: np.ndarray, sample_rate: int) -> None:
        """
        Audio not supported in frame recorder.

        Args:
            samples: Ignored
            sample_rate: Ignored
        """
        pass  # Frame recorder doesn't support audio

    def close(self) -> None:
        """Finalize recording."""
        self.is_open = False

    def supports_audio(self) -> bool:
        """
        Check if audio is supported.

        Returns:
            False - frame recorder doesn't support audio
        """
        return False

    def get_output_path(self) -> Optional[str]:
        """
        Get output directory path.

        Returns:
            Path to output directory
        """
        return self.output_dir
