from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
CHART_PATH = REPO_ROOT / "charts" / "SultanRage.MonstDeath" / "EZ.json"


def test_chart_wrapper_end_to_end():
    import phigros_cpp as pc

    chart = pc.load_chart(str(CHART_PATH))
    assert isinstance(chart, pc.Chart)
    assert chart.playable_count == 159
    assert len(chart.notes_data()) == chart.notes_count

    frame = chart.frame(0.0)
    assert frame.t == 0.0

    evaluator = chart.evaluator()
    frames = evaluator.build_frames([0.0, 0.5, 1.0])
    assert len(frames) == 3
    assert evaluator.sim_t >= 1.0

    result = pc.simulate_autoplay(chart)
    assert isinstance(result, pc.AutoplayRun)
    assert result.score.score == 1000000
    assert result.playable_count == 159
    assert len(result.hit_events_data()) == 159
