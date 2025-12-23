"""Note lifecycle management and visibility culling.

This module provides NoteManager for managing note visibility and state.
"""

from __future__ import annotations
from typing import List, Optional, Any
import logging

from ..types import RuntimeNote, RuntimeLine, NoteState


class NoteManager:
    """Manages note lifecycle and visibility.

    Handles visibility culling, note state tracking, and provides
    filtered views of notes for rendering and judgment.
    """

    def __init__(self, notes: List[RuntimeNote], states: Optional[List[NoteState]] = None):
        """Initialize note manager.

        Args:
            notes: All notes in the chart
            states: Optional pre-initialized note states
        """
        self.all_notes = notes
        self.states = states or []
        self._visible_indices: List[int] = []
        self._logger = logging.getLogger(__name__)

    def update_visibility(
        self,
        t: float,
        approach_time: float,
        lines: List[RuntimeLine],
        cull_screen: bool = True,
        cull_enter_time: bool = True,
    ) -> None:
        """Update visible notes based on current time and culling settings.

        Args:
            t: Current game time
            approach_time: How far ahead to show notes (seconds)
            lines: Judgment lines
            cull_screen: Enable screen-space culling
            cull_enter_time: Enable time-based culling
        """
        self._visible_indices.clear()

        for i, note in enumerate(self.all_notes):
            # Time-based culling
            if cull_enter_time:
                if t < note.t_enter:
                    continue  # Not entered screen yet
                if t > note.t_end + 0.5:  # Allow some buffer after note ends
                    continue

            # Approach time culling
            if t < note.t_hit - approach_time:
                continue

            # Note is potentially visible
            self._visible_indices.append(i)

        self._logger.debug(
            "Visibility update: t=%.3f, visible=%d/%d",
            t,
            len(self._visible_indices),
            len(self.all_notes),
        )

    def get_visible_notes(self) -> List[RuntimeNote]:
        """Get currently visible notes.

        Returns:
            List of visible notes
        """
        return [self.all_notes[i] for i in self._visible_indices]

    def get_visible_indices(self) -> List[int]:
        """Get indices of visible notes.

        Returns:
            List of indices into self.all_notes
        """
        return self._visible_indices

    def get_note_count(self) -> int:
        """Get total number of notes.

        Returns:
            Total note count
        """
        return len(self.all_notes)

    def get_visible_count(self) -> int:
        """Get number of currently visible notes.

        Returns:
            Visible note count
        """
        return len(self._visible_indices)

    def find_next_note_index(self, t: float) -> int:
        """Find index of next note to be judged.

        Args:
            t: Current game time

        Returns:
            Index of next note, or len(all_notes) if all notes are past
        """
        for i, note in enumerate(self.all_notes):
            if note.t_hit > t:
                return i
        return len(self.all_notes)

    def get_notes_in_range(self, t_start: float, t_end: float) -> List[RuntimeNote]:
        """Get notes within a time range.

        Args:
            t_start: Start time
            t_end: End time

        Returns:
            Notes with t_hit in [t_start, t_end]
        """
        return [
            note
            for note in self.all_notes
            if t_start <= note.t_hit <= t_end
        ]


__all__ = ["NoteManager"]
