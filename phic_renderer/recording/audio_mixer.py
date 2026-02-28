"""
Audio mixer for extracting and synchronizing audio segments.

Handles audio extraction from source files and sync calculations.
"""

import subprocess
import tempfile
import os
import shutil
import wave
from typing import Dict, List, Optional, Tuple

import numpy as np


class AudioMixer:
    """
    Extracts and prepares audio segments for video muxing.

    Handles trimming, format conversion, and sync offset calculation.
    """

    def __init__(
        self,
        bgm_path: str,
        start_time: float = 0.0,
        duration: Optional[float] = None,
        offset: float = 0.0
    ):
        """
        Initialize audio mixer.

        Args:
            bgm_path: Path to source audio file
            start_time: Start time in seconds
            duration: Duration in seconds (None = to end)
            offset: Chart offset for sync adjustment
        """
        self.bgm_path = bgm_path
        self.start_time = start_time
        self.duration = duration
        self.offset = offset
        self.temp_file: Optional[str] = None

    def prepare_audio(self) -> str:
        """
        Extract and prepare audio segment.

        Creates a temporary WAV file with the audio segment.

        Returns:
            Path to prepared audio file

        Raises:
            RuntimeError: If audio extraction fails
        """
        fd, self.temp_file = tempfile.mkstemp(suffix='.wav', prefix='phigros_audio_')
        os.close(fd)

        cmd = [
            'ffmpeg',
            '-y',
            '-hide_banner',
            '-loglevel', 'error',
            '-nostats',
            '-ss', str(max(0.0, float(self.start_time) + float(self.offset))),
            '-i', self.bgm_path,
        ]

        if self.duration is not None:
            cmd.extend(['-t', str(self.duration)])

        cmd.extend([
            '-acodec', 'pcm_s16le',
            '-ar', '44100',
            '-ac', '2',
            self.temp_file
        ])

        try:
            result = subprocess.run(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=60
            )

            if result.returncode != 0:
                error_msg = result.stderr.decode('utf-8', errors='ignore')
                raise RuntimeError(f"Failed to extract audio: {error_msg}")

            return self.temp_file

        except subprocess.TimeoutExpired:
            raise RuntimeError("Audio extraction timed out")
        except Exception as e:
            self.cleanup()
            raise RuntimeError(f"Audio extraction failed: {e}")

    def cleanup(self) -> None:
        """Remove temporary audio file."""
        if self.temp_file and os.path.exists(self.temp_file):
            try:
                os.remove(self.temp_file)
            except:
                pass
            self.temp_file = None

    def __del__(self):
        """Cleanup on destruction."""
        self.cleanup()

    def get_sync_offset(self) -> float:
        """
        Calculate audio sync offset.

        Returns:
            Sync offset in seconds (chart_offset + start_time)
        """
        return self.offset + self.start_time


def _check_ffmpeg() -> None:
    if shutil.which('ffmpeg') is None:
        raise RuntimeError('ffmpeg not found')


def _decode_to_s16le(
    path: str,
    *,
    ss: float = 0.0,
    duration: Optional[float] = None,
    sr: int = 44100,
    ch: int = 2,
) -> np.ndarray:
    _check_ffmpeg()
    ss = max(0.0, float(ss))

    cmd: List[str] = [
        'ffmpeg',
        '-hide_banner',
        '-loglevel', 'error',
        '-nostats',
        '-ss', str(ss),
        '-i', str(path),
    ]
    if duration is not None:
        cmd.extend(['-t', str(max(0.0, float(duration)))])
    cmd.extend([
        '-ac', str(int(ch)),
        '-ar', str(int(sr)),
        '-f', 's16le',
        'pipe:1',
    ])

    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if p.returncode != 0:
        msg = p.stderr.decode('utf-8', errors='ignore')
        raise RuntimeError(f'ffmpeg decode failed: {msg}')

    raw = p.stdout
    if not raw:
        return np.zeros((0, int(ch)), dtype=np.int16)
    arr = np.frombuffer(raw, dtype=np.int16)
    if arr.size % int(ch) != 0:
        arr = arr[: arr.size - (arr.size % int(ch))]
    return arr.reshape((-1, int(ch)))


def mix_wav(
    *,
    out_wav_path: str,
    duration: float,
    bgm_tracks: List[Tuple[str, float, Optional[float], float, float]],
    hitsound_events: List[Tuple[str, float, float]],
    sr: int = 44100,
    ch: int = 2,
) -> str:
    total = int(max(0.0, float(duration)) * int(sr))
    mix = np.zeros((total, int(ch)), dtype=np.int32)

    for path, start_at, seg_dur, vol, ss in bgm_tracks:
        if not path:
            continue
        start_at = float(start_at)
        if start_at >= float(duration):
            continue
        if seg_dur is None:
            seg_dur = float(duration) - start_at
        seg_dur = max(0.0, min(float(seg_dur), float(duration) - start_at))
        if seg_dur <= 1e-6:
            continue
        pcm = _decode_to_s16le(path, ss=float(ss), duration=seg_dur, sr=sr, ch=ch)
        if pcm.size == 0:
            continue
        off = int(max(0.0, start_at) * int(sr))
        n = min(pcm.shape[0], max(0, total - off))
        if n <= 0:
            continue
        mix[off:off + n] += (pcm[:n].astype(np.int32) * float(vol)).astype(np.int32)

    sfx_cache: Dict[str, np.ndarray] = {}
    for path, t, vol in hitsound_events:
        if not path:
            continue
        t = float(t)
        if t < 0.0 or t >= float(duration):
            continue
        pcm = sfx_cache.get(path)
        if pcm is None:
            pcm = _decode_to_s16le(path, ss=0.0, duration=None, sr=sr, ch=ch)
            sfx_cache[path] = pcm
        if pcm.size == 0:
            continue
        off = int(t * int(sr))
        n = min(pcm.shape[0], max(0, total - off))
        if n <= 0:
            continue
        mix[off:off + n] += (pcm[:n].astype(np.int32) * float(vol)).astype(np.int32)

    mix = np.clip(mix, -32768, 32767).astype(np.int16)
    with wave.open(out_wav_path, 'wb') as wf:
        wf.setnchannels(int(ch))
        wf.setsampwidth(2)
        wf.setframerate(int(sr))
        wf.writeframes(mix.tobytes())

    return out_wav_path
