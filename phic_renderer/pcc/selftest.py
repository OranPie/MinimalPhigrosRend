from __future__ import annotations

import json
import os
import tempfile

from ..math.easing import ease_01
from ..math.tracks import EasedSeg, IntegralTrack, PiecewiseEased, Seg1D
from ..types import RuntimeLine, RuntimeNote
from ..assets.loader import load_chart
from ..chart.pcc import load_pcc
from .api import save_pcc
from .exporter import export_input_to_pcc


def run() -> None:
    W, H = 1920, 1080
    px = PiecewiseEased([EasedSeg(0.0, 1.0, W * 0.5, W * 0.5, ease_01)], default=W * 0.5)
    py = PiecewiseEased([EasedSeg(0.0, 1.0, H * 0.5, H * 0.5, ease_01)], default=H * 0.5)
    rot = PiecewiseEased([EasedSeg(0.0, 1.0, 0.0, 0.0, ease_01)], default=0.0)
    alpha = PiecewiseEased([EasedSeg(0.0, 1.0, 1.0, 1.0, ease_01)], default=1.0)
    scroll = IntegralTrack([Seg1D(0.0, 5.0, 200.0, 200.0, 0.0)])

    lines = [RuntimeLine(lid=0, pos_x=px, pos_y=py, rot=rot, alpha=alpha, scroll_px=scroll, color_rgb=(255, 255, 255))]
    notes = [
        RuntimeNote(
            nid=0,
            line_id=0,
            kind=1,
            above=True,
            fake=False,
            t_hit=1.0,
            t_end=1.0,
            x_local_px=0.25 * W,
            y_offset_px=0.0,
            speed_mul=1.0,
            size_px=1.0,
            alpha01=1.0,
        )
    ]

    with tempfile.TemporaryDirectory() as td:
        p = os.path.join(td, 'test.pcc')
        save_pcc(p, 0.0, lines, notes, W=W, H=H, meta={'title': 't'}, compress=True, encrypt=False)
        off2, lines2, notes2 = load_pcc(p, W=W, H=H)
        assert abs(off2 - 0.0) < 1e-6
        assert len(lines2) == 1
        assert len(notes2) == 1
        assert abs(notes2[0].t_hit - 1.0) < 1e-3

        # E2E: minimal official json -> export_pcc -> load_chart(.pcc)
        j = {
            'formatVersion': 3,
            'offset': 0.0,
            'judgeLineList': [
                {
                    'bpm': 120.0,
                    'judgeLineMoveEvents': [],
                    'judgeLineRotateEvents': [],
                    'judgeLineDisappearEvents': [],
                    'speedEvents': [],
                    'notesAbove': [
                        {'type': 1, 'time': 10.0, 'positionX': 0.0, 'speed': 1.0},
                    ],
                    'notesBelow': [],
                }
            ],
        }
        src = os.path.join(td, 'src.json')
        with open(src, 'w', encoding='utf-8') as f:
            json.dump(j, f)
        out = os.path.join(td, 'out.pcc')
        export_input_to_pcc(src, out, W=W, H=H, password=None, compress=True)

        fmt3, off3, lines3, notes3 = load_chart(out, W=W, H=H)
        assert fmt3 == 'pcc'
        assert abs(off3 - 0.0) < 1e-6
        assert len(lines3) == 1
        assert len(notes3) == 1


if __name__ == '__main__':
    run()
