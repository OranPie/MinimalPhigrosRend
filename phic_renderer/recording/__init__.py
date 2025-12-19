"""
Recording module for video and frame capture.

Provides flexible recording capabilities with multiple output formats:
- PNG frame sequences
- Direct MP4/WEBM video encoding
- Audio synchronization
"""

from .base import RecorderBackend

__all__ = ['RecorderBackend']
