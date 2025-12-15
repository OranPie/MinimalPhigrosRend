from __future__ import annotations

from typing import Any, Optional

import pygame


class PygameAudio:
    def __init__(self, *, channels: int = 32, **kwargs: Any):
        try:
            pygame.mixer.pre_init(44100, -16, 2, 512)
        except:
            pass
        try:
            pygame.mixer.init()
            pygame.mixer.set_num_channels(int(channels))
        except:
            pass

        self._music_start_pos_sec: float = 0.0

    def close(self) -> None:
        try:
            pygame.mixer.quit()
        except:
            pass

    def play_music_file(self, path: str, volume: float = 1.0, start_pos_sec: float = 0.0) -> None:
        pygame.mixer.music.load(str(path))
        pygame.mixer.music.set_volume(float(volume))
        try:
            self._music_start_pos_sec = max(0.0, float(start_pos_sec))
        except:
            self._music_start_pos_sec = 0.0
        try:
            pygame.mixer.music.play(loops=0, start=float(self._music_start_pos_sec))
        except:
            pygame.mixer.music.play()

    def stop_music(self) -> None:
        try:
            pygame.mixer.music.stop()
        except:
            pass
        self._music_start_pos_sec = 0.0

    def pause_music(self) -> None:
        try:
            pygame.mixer.music.pause()
        except:
            pass

    def unpause_music(self) -> None:
        try:
            pygame.mixer.music.unpause()
        except:
            pass

    def music_pos_sec(self) -> Optional[float]:
        try:
            ms = pygame.mixer.music.get_pos()
            if ms is None or ms < 0:
                return float(self._music_start_pos_sec)
            return float(self._music_start_pos_sec) + float(ms) / 1000.0
        except:
            return None

    def load_sound(self, path: str) -> Any:
        return pygame.mixer.Sound(str(path))

    def play_sound(self, sound: Any, volume: float = 1.0) -> Any:
        try:
            sound.set_volume(float(volume))
        except:
            pass
        try:
            return sound.play()
        except:
            return None

    def stop_channel(self, channel: Any) -> None:
        if channel is None:
            return
        try:
            channel.stop()
        except:
            pass
