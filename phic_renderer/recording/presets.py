"""
Encoding quality presets for video recording.

Provides pre-configured ffmpeg settings for different quality/speed tradeoffs.
"""

from dataclasses import dataclass
from typing import List, Optional


@dataclass
class EncodingPreset:
    """
    Video encoding preset configuration.

    Attributes:
        name: Preset name
        ffmpeg_preset: FFmpeg preset (ultrafast, fast, medium, slow, veryslow)
        crf: Constant Rate Factor (0-51, lower = better quality)
        codec: Video codec (libx264, libx265, libvpx-vp9)
        pixel_format: Pixel format (yuv420p, yuv444p)
        extra_args: Additional ffmpeg arguments
        description: Human-readable description
        expected_speed: Expected encoding speed relative to realtime
    """
    name: str
    ffmpeg_preset: str
    crf: int
    codec: str = "libx264"
    pixel_format: str = "yuv420p"
    extra_args: List[str] = None
    description: str = ""
    expected_speed: str = ""

    def __post_init__(self):
        if self.extra_args is None:
            self.extra_args = []


# Pre-defined encoding presets
PRESET_FAST = EncodingPreset(
    name="fast",
    ffmpeg_preset="ultrafast",
    crf=28,
    codec="libx264",
    pixel_format="yuv420p",
    extra_args=["-tune", "zerolatency"],
    description="Fastest encoding, lower quality. Good for quick previews.",
    expected_speed="3-5x realtime"
)

PRESET_BALANCED = EncodingPreset(
    name="balanced",
    ffmpeg_preset="medium",
    crf=23,
    codec="libx264",
    pixel_format="yuv420p",
    extra_args=[],
    description="Balanced speed and quality. Recommended for most use cases.",
    expected_speed="1-2x realtime"
)

PRESET_QUALITY = EncodingPreset(
    name="quality",
    ffmpeg_preset="slow",
    crf=18,
    codec="libx264",
    pixel_format="yuv420p",
    extra_args=[],
    description="High quality, slower encoding. Good for final renders.",
    expected_speed="0.5-1x realtime"
)

PRESET_ARCHIVE = EncodingPreset(
    name="archive",
    ffmpeg_preset="veryslow",
    crf=15,
    codec="libx264",
    pixel_format="yuv444p",
    extra_args=["-tune", "film"],
    description="Maximum quality, slowest encoding. Best for archival.",
    expected_speed="0.2-0.5x realtime"
)

# Map preset names to preset objects
PRESETS = {
    "fast": PRESET_FAST,
    "balanced": PRESET_BALANCED,
    "quality": PRESET_QUALITY,
    "archive": PRESET_ARCHIVE,
}


def get_preset(name: str) -> Optional[EncodingPreset]:
    """
    Get an encoding preset by name.

    Args:
        name: Preset name (fast, balanced, quality, archive)

    Returns:
        EncodingPreset object or None if not found
    """
    return PRESETS.get(name.lower())


def list_presets() -> List[str]:
    """
    List all available preset names.

    Returns:
        List of preset names
    """
    return list(PRESETS.keys())
