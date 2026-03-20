def test_import_smoke():
    import phigros_cpp

    cfg = phigros_cpp.RenderConfig()
    assert cfg.window_w == 1280
    assert cfg.window_h == 720


def test_compute_score():
    import phigros_cpp

    result = phigros_cpp.compute_score(1.0, 1, 1)
    assert result.score == 1000000
    assert result.acc_ratio == 1.0
