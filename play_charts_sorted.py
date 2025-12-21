from __future__ import annotations

def configure_args(args):
    if getattr(args, "playlist_notes_per_chart", None) is None:
        setattr(args, "playlist_notes_per_chart", 10)
    if getattr(args, "playlist_switch_mode", None) is None:
        setattr(args, "playlist_switch_mode", "hit")
    if getattr(args, "playlist_charts_dir", None) is None:
        setattr(args, "playlist_charts_dir", "charts")


def sort_metas(metas, args):
    return sorted(list(metas), key=lambda m: (int(getattr(m, "total_notes", 0)), str(getattr(m, "input_path", ""))))


def playlist_filter(meta):
    if bool(getattr(meta, "seg_notes", 0)) <= 0:
        return False
    if bool(getattr(meta, "seg_max_chord", 1)) > 1:
        return False
    return True
