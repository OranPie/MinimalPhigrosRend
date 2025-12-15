from __future__ import annotations
 
import time
from typing import Any, Dict, Optional, Tuple
 
 
class OpenALAudio:
    def __init__(self, **kwargs: Any):
        self._kwargs = kwargs
        try:
            import openal  # type: ignore
        except:
            raise SystemExit(
                "OpenAL backend requires the 'openal' package. "
                "Install it (pip install openal) to use --audio_backend openal."
            )
        try:
            import soundfile as sf  # type: ignore
        except:
            raise SystemExit(
                "OpenAL backend requires 'soundfile' for decoding (ogg/wav). "
                "Install it (pip install soundfile)."
            )

        self._openal = openal
        self._sf = sf
        self._device = openal.oalOpenDevice(None)
        self._context = openal.oalCreateContext(self._device, None)
        openal.oalMakeContextCurrent(self._context)

        self._music_source = openal.oalGetListener()
        self._music: Optional[Any] = None
        self._music_start_monotonic: float = 0.0
        self._music_start_pos_sec: float = 0.0
        self._music_pause_monotonic: float = 0.0
        self._music_paused_accum: float = 0.0
        self._music_is_paused: bool = False

        self._buffer_cache: Dict[str, Any] = {}

    def close(self) -> None:
        try:
            if self._music is not None:
                try:
                    self._music.stop()
                except:
                    pass
        except:
            pass

        try:
            self._openal.oalMakeContextCurrent(None)
        except:
            pass
        try:
            self._openal.oalDestroyContext(self._context)
        except:
            pass
        try:
            self._openal.oalCloseDevice(self._device)
        except:
            pass

    def play_music_file(self, path: str, volume: float = 1.0, start_pos_sec: float = 0.0) -> None:
        try:
            start_pos = max(0.0, float(start_pos_sec))
        except:
            start_pos = 0.0

        if start_pos <= 1e-9:
            snd = self.load_sound(path)
        else:
            data, samplerate = self._sf.read(str(path), dtype="float32", always_2d=True)
            channels = int(data.shape[1])
            start_idx = int(start_pos * float(samplerate))
            if start_idx > 0:
                if start_idx >= int(data.shape[0]):
                    data = data[:1, :]
                else:
                    data = data[start_idx:, :]
            pcm = (data.clip(-1.0, 1.0) * 32767.0).astype("int16")
            snd = self._openal.Sound(pcm, channels=channels, frequency=int(samplerate))

        ch = self.play_sound(snd, volume=volume)
        self._music = ch
        self._music_start_monotonic = time.monotonic()
        self._music_start_pos_sec = float(start_pos)
        self._music_pause_monotonic = 0.0
        self._music_paused_accum = 0.0
        self._music_is_paused = False

    def stop_music(self) -> None:
        if self._music is None:
            return
        try:
            self._music.stop()
        except:
            pass
        self._music = None
        self._music_start_pos_sec = 0.0

    def pause_music(self) -> None:
        if self._music is None or self._music_is_paused:
            return
        try:
            self._music.pause()
        except:
            pass
        self._music_is_paused = True
        self._music_pause_monotonic = time.monotonic()

    def unpause_music(self) -> None:
        if self._music is None or (not self._music_is_paused):
            return
        try:
            self._music.play()
        except:
            pass
        if self._music_pause_monotonic > 0.0:
            self._music_paused_accum += max(0.0, time.monotonic() - self._music_pause_monotonic)
        self._music_pause_monotonic = 0.0
        self._music_is_paused = False

    def music_pos_sec(self) -> Optional[float]:
        if self._music is None:
            return None
        now = time.monotonic()
        if self._music_is_paused and self._music_pause_monotonic > 0.0:
            now = self._music_pause_monotonic
        return float(self._music_start_pos_sec) + max(0.0, now - self._music_start_monotonic - self._music_paused_accum)

    def load_sound(self, path: str) -> Any:
        if path in self._buffer_cache:
            return self._buffer_cache[path]

        data, samplerate = self._sf.read(str(path), dtype="float32", always_2d=True)
        channels = int(data.shape[1])

        # float32 [-1,1] -> int16
        pcm = (data.clip(-1.0, 1.0) * 32767.0).astype("int16")

        snd = self._openal.Sound(pcm, channels=channels, frequency=int(samplerate))
        self._buffer_cache[path] = snd
        return snd

    def play_sound(self, sound: Any, volume: float = 1.0) -> Any:
        try:
            src = sound.play()
        except:
            return None
        try:
            src.set_gain(float(volume))
        except:
            pass
        return src

    def stop_channel(self, channel: Any) -> None:
        if channel is None:
            return
        try:
            channel.stop()
        except:
            pass
