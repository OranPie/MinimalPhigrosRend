"""
Base recording interfaces and protocols.

Defines the RecorderBackend protocol that all recording implementations must follow.
"""

from typing import Protocol, Optional
import numpy as np


class RecorderBackend(Protocol):
    """
    Protocol for recording backends.

    All recording implementations (frame recorder, video recorder, etc.)
    must implement this interface.
    """

    def open(self) -> None:
        """
        Initialize the recorder and prepare for recording.

        Raises:
            Exception: If initialization fails
        """
        ...

    def write_frame(self, frame: np.ndarray) -> None:
        """
        Write a single frame to the recording.

        Args:
            frame: RGB frame data as numpy array (H, W, 3)

        Raises:
            Exception: If frame write fails
        """
        ...

    def write_audio(self, samples: np.ndarray, sample_rate: int) -> None:
        """
        Write audio samples to the recording (if supported).

        Args:
            samples: Audio sample data as numpy array
            sample_rate: Audio sample rate in Hz

        Note:
            Not all backends support audio. Check supports_audio() first.
        """
        ...

    def close(self) -> None:
        """
        Finalize the recording and release resources.

        Should be called when recording is complete.

        Raises:
            Exception: If finalization fails
        """
        ...

    def supports_audio(self) -> bool:
        """
        Check if this backend supports audio recording.

        Returns:
            True if audio is supported, False otherwise
        """
        ...

    def get_output_path(self) -> Optional[str]:
        """
        Get the path to the output file.

        Returns:
            Path to output file, or None if not applicable
        """
        ...
