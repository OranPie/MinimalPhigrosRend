def test_import_smoke():
    import phigros_cpp

    cfg = phigros_cpp.RenderConfig()
    assert cfg.window_w == 1280
    assert cfg.window_h == 720
    assert hasattr(phigros_cpp, "Chart")
    assert hasattr(phigros_cpp, "FrameEvaluator")


def test_compute_score():
    import phigros_cpp

    result = phigros_cpp.compute_score(1.0, 1, 1)
    assert result.score == 1000000
    assert result.acc_ratio == 1.0


def test_render_config_from_dict():
    import phigros_cpp

    cfg = phigros_cpp.RenderConfig.from_dict({
        "window": {"w": 1600, "h": 900},
        "render": {"chart_speed": 1.5, "playback_speed": 1.25},
    })
    assert cfg.window_w == 1600
    assert cfg.window_h == 900
    assert cfg.chart_speed == 1.5
    assert cfg.playback_speed == 1.25
