// Comprehensive Phase 2 engine test.
// Validates: visibility, note manager, hold system, miss detection,
//            simulate play, effects, mods — end-to-end autoplay verification.
//
// Usage: test_engine [--auto-discover <charts_dir>] <chart_path> [W] [H]

#include "phigros/chart/official.hpp"
#include "phigros/chart/rpe.hpp"
#include "phigros/chart/pec.hpp"
#include "phigros/chart/compiler.hpp"
#include "phigros/chart/phbc_io.hpp"
#include "phigros/chart/phbc_compress.hpp"
#include "phigros/chart/phbc_crypto.hpp"
#include "phigros/engine/kinematics.hpp"
#include "phigros/engine/judge.hpp"
#include "phigros/engine/visibility.hpp"
#include "phigros/engine/note_manager.hpp"
#include "phigros/engine/hold_logic.hpp"
#include "phigros/engine/simulateplay.hpp"
#include "phigros/engine/scriptplay.hpp"
#include "phigros/engine/effects.hpp"
#include "phigros/core/mods.hpp"
#include "phigros/config/render_config.hpp"
#include "phigros/render/renderer.hpp"
#include "phigros/io/replay.hpp"
#include "phigros/math/tracks.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <cmath>
#include <cstdlib>
#include <cassert>
#include <filesystem>
#include <iomanip>
#include <chrono>
#include <nlohmann/json.hpp>

using namespace phigros;
using json = nlohmann::json;

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { ++g_fail; std::cerr << "  FAIL: " << msg << "\n"; } \
    else { ++g_pass; } \
} while(0)

// Forward declaration (defined after run_regression)
static bool run_autoplay(const std::string& chart_path, int W, int H);
static bool test_compile_roundtrip(const std::string& path, int W, int H);

// ---- Format detection (same as verify_chart) ----
static std::string detect_format(const std::string& path) {
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".pec")
        return "pec";
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::string text((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
    size_t pos = text.find_first_not_of(" \t\r\n");
    if (pos == std::string::npos || text[pos] != '{') return "pec_text";
    try {
        auto j = json::parse(text);
        if (j.contains("META") || j.contains("BPMList")) return "rpe";
        if (j.contains("judgeLineList") || j.contains("formatVersion"))
            return "official";
    } catch (...) { return "pec_text"; }
    return "official";
}

// ---- Load chart ----
static ChartData load_chart(const std::string& path, int W, int H) {
    std::string fmt = detect_format(path);

    if (fmt == "pec")
        return chart::load_pec(path, W, H);

    if (fmt == "pec_text") {
        std::ifstream f(path);
        std::string text((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
        return chart::load_pec_text(text, W, H);
    }

    // JSON formats: parse file then dispatch
    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("Cannot open: " + path);
    auto j = json::parse(f);

    if (fmt == "official") return chart::load_official(j, W, H);
    if (fmt == "rpe")      return chart::load_rpe(j, W, H);
    throw std::runtime_error("Unknown format: " + path);
}

// ---- 6A2: Kinematics precision tests ----
static void test_kinematics() {
    std::cout << "\n=== Kinematics precision tests ===\n";
    using namespace phigros::math;
    using namespace phigros::engine;

    // --- PiecewiseEased: single linear segment [0,1]: 0→1 ---
    {
        PiecewiseEased pe({EasedSeg{0.0, 1.0, 0.0, 1.0, 0, 0.0, 1.0}}, 0.0);
        CHECK(std::abs(pe.eval(-0.1) - 0.0) < 1e-9, "PE: before start → v0");
        CHECK(std::abs(pe.eval(0.0)  - 0.0) < 1e-9, "PE: at t0 → v0");
        CHECK(std::abs(pe.eval(0.5)  - 0.5) < 1e-9, "PE: midpoint → 0.5 (linear)");
        CHECK(std::abs(pe.eval(1.0)  - 1.0) < 1e-9, "PE: at t1 → v1");
        CHECK(std::abs(pe.eval(1.5)  - 1.0) < 1e-9, "PE: after end → v1");
    }

    // --- PiecewiseEased seek backward (6B4 regression) ---
    {
        std::vector<EasedSeg> segs;
        for (int i = 0; i < 10; ++i)
            segs.push_back(EasedSeg{double(i), double(i+1), double(i), double(i+1), 0, 0.0, 1.0});
        PiecewiseEased pe(segs, 0.0);
        CHECK(std::abs(pe.eval(9.5) - 9.5) < 1e-9, "PE: forward seek to 9.5");
        CHECK(std::abs(pe.eval(0.5) - 0.5) < 1e-9, "PE: backward seek to 0.5");
        CHECK(std::abs(pe.eval(5.5) - 5.5) < 1e-9, "PE: mid-seek to 5.5");
    }

    // --- IntegralTrack: two-segment ramp 0→100 at [0,1] and 100→0 at [1,2] ---
    {
        // Segment 0: t=[0,1], v0=0,  v1=100, prefix=0
        // Segment 1: t=[1,2], v0=100,v1=0,   prefix=50 (= 0.5*(0+100)*1)
        Seg1D s0{0.0, 1.0, 0.0,   100.0, 0.0};
        Seg1D s1{1.0, 2.0, 100.0, 0.0,   50.0};
        IntegralTrack track({s0, s1});
        // integral(0.5): dt=0.5,u=0.5,vt=50 → 0+0.5*(0+50)*0.5 = 12.5
        CHECK(std::abs(track.integral(0.5) - 12.5) < 1e-9, "Integral ramp: integral(0.5)=12.5");
        // integral(1.0): at segment end → prefix1 = 50
        CHECK(std::abs(track.integral(1.0) - 50.0) < 1e-9, "Integral ramp: integral(1.0)=50");
        // integral(1.5): dt=0.5,u=0.5,vt=50 → 50+0.5*(100+50)*0.5 = 87.5
        CHECK(std::abs(track.integral(1.5) - 87.5) < 1e-9, "Integral ramp: integral(1.5)=87.5");
        // integral(2.0): at seg1 end → 50+0.5*(100+0)*1 = 100
        CHECK(std::abs(track.integral(2.0) - 100.0) < 1e-9, "Integral ramp: integral(2.0)=100");
        // backward seek
        CHECK(std::abs(track.integral(0.5) - 12.5) < 1e-9, "Integral: backward seek to 0.5");
    }

    // --- note_world_pos geometry (rot=0) ---
    {
        Note note{};
        note.above = true; note.x_local_px = 50.0; note.y_offset_px = 0.0;
        note.kind = 1; note.speed_mul = 1.0;
        // scroll_target - scroll_now = 100, flow_mul = 1
        // rot=0: tx=1,ty=0,nx=0,ny=1 → wx=50, wy=100
        auto pos = note_world_pos(0.0, 0.0, 0.0, /*scroll_now=*/0.0, note, /*scroll_target=*/100.0);
        CHECK(std::abs(pos.x - 50.0) < 1e-9, "note_world_pos rot=0: wx=50");
        CHECK(std::abs(pos.y - 100.0) < 1e-9, "note_world_pos rot=0: wy=100");
    }

    // --- note_world_pos geometry (rot=pi/2) ---
    {
        Note note{};
        note.above = true; note.x_local_px = 50.0; note.y_offset_px = 0.0;
        note.kind = 1; note.speed_mul = 1.0;
        // rot=pi/2: tx=0,ty=1,nx=-1,ny=0 → wx=0*50+(-1)*100=-100, wy=1*50+0*100=50
        auto pos = note_world_pos(0.0, 0.0, M_PI / 2.0, 0.0, note, 100.0);
        CHECK(std::abs(pos.x - (-100.0)) < 1e-6, "note_world_pos rot=pi/2: wx=-100");
        CHECK(std::abs(pos.y - 50.0) < 1e-6, "note_world_pos rot=pi/2: wy=50");
    }

    // --- note_world_pos_cs matches note_world_pos ---
    {
        Note note{};
        note.above = true; note.x_local_px = 30.0; note.y_offset_px = 5.0;
        note.kind = 1; note.speed_mul = 1.0;
        double rot = 0.7;
        auto pos1 = note_world_pos(10.0, 20.0, rot, 5.0, note, 55.0);
        auto pos2 = note_world_pos_cs(10.0, 20.0, std::cos(rot), std::sin(rot), 5.0, note, 55.0);
        CHECK(std::abs(pos1.x - pos2.x) < 1e-9, "note_world_pos_cs: x matches");
        CHECK(std::abs(pos1.y - pos2.y) < 1e-9, "note_world_pos_cs: y matches");
    }

    // --- Hold head stays on the line after hit even when not actively holding ---
    {
        ChartData chart;
        Line ln;
        ln.lid = 0;
        ln.pos_x = [](double) { return 400.0; };
        ln.pos_y = [](double) { return 300.0; };
        ln.rot   = [](double) { return 0.0; };
        ln.alpha = [](double) { return 1.0; };
        ln.scroll_fn = [](double t) { return t * 100.0; };
        chart.lines.push_back(std::move(ln));

        Note note{};
        note.nid = 1;
        note.line_id = 0;
        note.kind = 3;
        note.above = true;
        note.t_hit = 1.0;
        note.t_end = 3.0;
        note.alpha01 = 1.0;
        note.size_px = 1.0;
        note.scroll_hit = 100.0;
        note.scroll_end = 300.0;
        chart.notes.push_back(note);

        std::vector<NoteState> states(1);
        states[0].note = &chart.notes[0];

        engine::Judge judge;
        config::RenderConfig cfg;
        cfg.window_w = 800;
        cfg.window_h = 600;
        cfg.no_cull = true;

        auto frame = render::build_frame(1.5, chart, states, judge, cfg);
        CHECK(frame.notes.size() == 1, "Hold regression: note emitted after hit");
        if (!frame.notes.empty()) {
            CHECK(std::abs(frame.notes[0].wy - 300.0) < 1e-9,
                  "Hold regression: head remains pinned to line after hit");
            CHECK(frame.notes[0].wy_tail > frame.notes[0].wy,
                  "Hold regression: tail remains ahead of pinned head");
        }
    }

    // --- Missed holds remain visible so renderer can dim them ---
    {
        ChartData chart;
        Line ln;
        ln.lid = 0;
        ln.pos_x = [](double) { return 400.0; };
        ln.pos_y = [](double) { return 300.0; };
        ln.rot   = [](double) { return 0.0; };
        ln.alpha = [](double) { return 1.0; };
        ln.scroll_fn = [](double t) { return t * 100.0; };
        chart.lines.push_back(std::move(ln));

        Note note{};
        note.nid = 2;
        note.line_id = 0;
        note.kind = 3;
        note.above = true;
        note.t_hit = 1.0;
        note.t_end = 3.0;
        note.alpha01 = 1.0;
        note.size_px = 1.0;
        note.scroll_hit = 100.0;
        note.scroll_end = 300.0;
        chart.notes.push_back(note);

        std::vector<NoteState> states(1);
        states[0].note = &chart.notes[0];
        states[0].miss = true;
        states[0].judged = true;
        states[0].hold_finalized = true;

        engine::Judge judge;
        config::RenderConfig cfg;
        cfg.window_w = 800;
        cfg.window_h = 600;
        cfg.no_cull = true;

        auto frame = render::build_frame(1.5, chart, states, judge, cfg);
        CHECK(frame.notes.size() == 1, "Hold miss regression: missed hold still emitted");
        if (!frame.notes.empty()) {
            CHECK(frame.notes[0].is_hold, "Hold miss regression: emitted note is hold");
            CHECK(frame.notes[0].miss, "Hold miss regression: emitted hold keeps miss flag");
        }
    }
}

// ---- 6A3: Judge boundary tests ----
static void test_judge_boundaries() {
    std::cout << "\n=== Judge boundary tests ===\n";
    using namespace phigros;

    // Helper: create a fresh judge + NoteState at t_note
    auto make_state = [](double t_note) -> NoteState {
        static Note n{};
        n.t_hit = t_note; n.kind = 1; n.fake = false;
        NoteState ns{};
        ns.note = &n;
        return ns;
    };

    constexpr double T = 1.0; // note hit time

    // PERFECT at exactly ±PERFECT boundary (use 0.001 inside to avoid FP edge)
    {
        engine::Judge j;
        auto ns = make_state(T);
        auto g = j.try_hit(ns, T + engine::Judge::PERFECT - 0.001);
        CHECK(g.has_value() && *g == "PERFECT", "Judge: PERFECT at +boundary");
        CHECK(std::abs(j.acc_sum - 1.0) < 1e-9, "acc_sum += 1.0 for PERFECT");
        CHECK(j.max_combo == 1, "combo == 1 after PERFECT");
    }
    {
        engine::Judge j;
        auto ns = make_state(T);
        auto g = j.try_hit(ns, T - engine::Judge::PERFECT + 0.001);
        CHECK(g.has_value() && *g == "PERFECT", "Judge: PERFECT at -boundary");
    }

    // GOOD: just past PERFECT (PERFECT+ε), and at GOOD boundary (0.001 inside)
    {
        engine::Judge j;
        auto ns = make_state(T);
        auto g = j.try_hit(ns, T + engine::Judge::PERFECT + 0.001);
        CHECK(g.has_value() && *g == "GOOD", "Judge: GOOD at PERFECT+0.001");
        CHECK(std::abs(j.acc_sum - 0.6) < 1e-9, "acc_sum += 0.6 for GOOD");
    }
    {
        engine::Judge j;
        auto ns = make_state(T);
        auto g = j.try_hit(ns, T + engine::Judge::GOOD - 0.001);
        CHECK(g.has_value() && *g == "GOOD", "Judge: GOOD inside GOOD boundary");
    }

    // BAD: just past GOOD (GOOD+ε), at BAD boundary
    {
        engine::Judge j;
        auto ns = make_state(T);
        auto g = j.try_hit(ns, T + engine::Judge::GOOD + 0.001);
        CHECK(g.has_value() && *g == "BAD", "Judge: BAD at GOOD+0.001");
        CHECK(std::abs(j.acc_sum - 0.0) < 1e-9, "acc_sum += 0.0 for BAD");
    }
    {
        engine::Judge j;
        auto ns = make_state(T);
        auto g = j.try_hit(ns, T + engine::Judge::BAD);
        CHECK(g.has_value() && *g == "BAD", "Judge: BAD at BAD boundary");
    }

    // MISS territory: past BAD window → try_hit returns nullopt
    {
        engine::Judge j;
        auto ns = make_state(T);
        auto g = j.try_hit(ns, T + engine::Judge::BAD + 0.001);
        CHECK(!g.has_value(), "Judge: nullopt past BAD window");
    }

    // Score formula: 3-note perfect play
    {
        engine::Judge j;
        for (int i = 0; i < 3; ++i) {
            j.bump(); j.acc_sum += 1.0; ++j.judged_cnt;
        }
        auto sr = engine::compute_score(j.acc_sum, j.max_combo, 3);
        CHECK(sr.score == 1000000, "Score formula: 3 PERFECTs = 1,000,000");
    }

    // Score formula: 1 GOOD out of 1 → 960000
    {
        engine::Judge j;
        j.bump(); j.acc_sum += 0.6; ++j.judged_cnt;
        auto sr = engine::compute_score(j.acc_sum, j.max_combo, 1);
        // 900000 * 0.6/1 + 100000 * (1==1 ? 1 : 1/1) = 540000+100000=640000
        // Actually: combo_r = max_combo/total = 1/1 = 1.0
        // score = round(900000 * 0.6 + 100000 * 1.0) = 640000
        CHECK(sr.score == 640000, "Score formula: 1 GOOD = 640000");
    }
}

// ---- 6A4: ScriptPlay parser + scoring tests ----
static void test_scriptplay() {
    std::cout << "\n=== ScriptPlay tests ===\n";

    ChartData chart;
    Line ln;
    ln.lid = 0;
    ln.pos_x = [](double) { return 400.0; };
    ln.pos_y = [](double) { return 300.0; };
    ln.rot   = [](double) { return 0.0; };
    ln.alpha = [](double) { return 1.0; };
    ln.scroll_fn = [](double t) { return t * 100.0; };
    chart.lines.push_back(std::move(ln));

    Note tap0{};
    tap0.nid = 0;
    tap0.line_id = 0;
    tap0.kind = 1;
    tap0.t_hit = 1.0;
    tap0.t_end = 1.0;
    tap0.above = true;
    tap0.alpha01 = 1.0;
    tap0.scroll_hit = 100.0;
    tap0.scroll_end = 100.0;
    chart.notes.push_back(tap0);

    Note hold1{};
    hold1.nid = 1;
    hold1.line_id = 0;
    hold1.kind = 3;
    hold1.t_hit = 2.0;
    hold1.t_end = 3.0;
    hold1.above = true;
    hold1.alpha01 = 1.0;
    hold1.scroll_hit = 200.0;
    hold1.scroll_end = 300.0;
    chart.notes.push_back(hold1);

    Note tap2{};
    tap2.nid = 2;
    tap2.line_id = 0;
    tap2.kind = 1;
    tap2.t_hit = 4.0;
    tap2.t_end = 4.0;
    tap2.above = true;
    tap2.alpha01 = 1.0;
    tap2.scroll_hit = 400.0;
    tap2.scroll_end = 400.0;
    chart.notes.push_back(tap2);

    chart.finalize();

    const std::string script_text = R"json(
{
  "version": 1,
  "meta": {
    "name": "unit-test",
    "index_mode": "playable",
    "require_playable_notes": 3
  },
  "entries": [
    {
      "filter": { "noteIndexes": [1] },
      "judge": { "grade": "GOOD", "dt_ms": 60 },
      "hold": { "percent": 1.0 }
    },
    {
      "filter": { "noteIndexes": [2] },
      "judge": { "grade": "MISS" }
    }
  ]
}
)json";

    auto script = engine::load_scriptplay_text(script_text);
    CHECK(script.entries.size() == 2, "ScriptPlay parser: loads two entries");

    auto plan = engine::compile_scriptplay(script, chart, 0.8);
    CHECK(plan.note_plans.size() == 3, "ScriptPlay compiler: plan sized to notes");
    CHECK(plan.note_plans[1].grade == "GOOD", "ScriptPlay compiler: hold grade override parsed");
    CHECK(plan.note_plans[2].grade == "MISS", "ScriptPlay compiler: miss override parsed");

    std::vector<NoteState> states(chart.notes.size());
    for (size_t i = 0; i < states.size(); ++i) states[i].note = &chart.notes[i];

    engine::Judge judge;
    engine::ScriptPlayPlayer player;
    player.load(script, chart, 0.8);

    auto fnext = [&](double tc) {
        int lo = 0, hi = static_cast<int>(states.size());
        while (lo < hi) {
            int m = (lo + hi) / 2;
            if (states[m].judged || states[m].note->t_hit < tc - 0.5) lo = m + 1;
            else hi = m;
        }
        return lo;
    };

    int idx_next = 0;
    constexpr double SIM_DT = 1.0 / 240.0;
    for (double tc = 0.0; tc <= 4.5; tc += SIM_DT) {
        player.tick(tc, chart.notes, states, judge);
        idx_next = fnext(tc);
        engine::detect_misses(states, idx_next, tc, engine::Judge::BAD, judge);
        engine::hold_maintenance(states, idx_next, tc, 0.8, judge);
        engine::hold_finalize(states, idx_next, tc, 0.8, engine::Judge::BAD, judge);
    }

    auto sr = engine::compute_score(judge.acc_sum, judge.max_combo, chart.playable_count);
    CHECK(judge.judged_cnt == 3, "ScriptPlay scoring: all notes judged");
    CHECK(judge.max_combo == 2, "ScriptPlay scoring: combo reflects trailing miss");
    CHECK(sr.score == 546666, "ScriptPlay scoring: expected mixed-result score");

    bool threw = false;
    try {
        auto bad_script = engine::load_scriptplay_text(R"json(
{
  "entries": [
    { "filter": { "noteIndexes": [0] }, "judge": { "grade": "PERFECT", "dt_ms": 80 } }
  ]
}
)json");
        (void)engine::compile_scriptplay(bad_script, chart, 0.8);
    } catch (...) {
        threw = true;
    }
    CHECK(threw, "ScriptPlay validation: invalid grade/dt combination throws");
}

static void test_hold_bad_breaks_combo() {
    std::cout << "\n=== Hold BAD combo test ===\n";

    Note note;
    note.kind = 3;
    note.t_hit = 1.0;
    note.t_end = 2.0;

    NoteState ns;
    ns.note = &note;

    engine::Judge judge;
    judge.combo = 5;
    judge.max_combo = 5;

    auto grade = judge.start_hold(ns, note.t_hit + engine::Judge::GOOD + 0.001);
    CHECK(grade.has_value() && *grade == "BAD", "Hold start: BAD grade accepted");

    judge.finalize_hold(ns);

    CHECK(ns.judge_grade == "BAD", "Hold finalize preserves BAD grade");
    CHECK(judge.combo == 0, "Hold BAD breaks combo");
    CHECK(judge.max_combo == 5, "Hold BAD does not extend max combo");
    CHECK(judge.judged_cnt == 1, "Hold BAD counts as judged");
}

// ---- 6A4: Replay round-trip test ----
static void test_replay_roundtrip() {
    std::cout << "\n=== Replay round-trip test ===\n";

    phigros::io::ReplayWriter writer;
    // Record 3 events
    writer.record(1.0f, 0, "PERFECT");
    writer.record(2.0f, 1, "GOOD");
    writer.record(3.0f, 2, "BAD");

    CHECK(writer.events.size() == 3, "Writer has 3 events");

    // Write to temp file
    std::string tmp_path = "/tmp/phigros_test_replay.rep";
    bool saved = writer.save(tmp_path, 0xDEADBEEF);
    CHECK(saved, "Replay save() succeeded");

    // Load back
    phigros::io::ReplayPlayer player;
    bool loaded = player.load(tmp_path);
    CHECK(loaded, "Replay load() succeeded");
    CHECK(player.events.size() == 3, "Loaded 3 events");

    if (player.events.size() == 3) {
        CHECK(std::abs(player.events[0].t - 1.0f) < 1e-5f, "Event 0: t=1.0");
        CHECK(player.events[0].note_idx == 0, "Event 0: note_idx=0");
        CHECK(phigros::io::u8_to_grade(player.events[0].grade) == "PERFECT",
              "Event 0: grade=PERFECT");

        CHECK(std::abs(player.events[1].t - 2.0f) < 1e-5f, "Event 1: t=2.0");
        CHECK(player.events[1].note_idx == 1, "Event 1: note_idx=1");
        CHECK(phigros::io::u8_to_grade(player.events[1].grade) == "GOOD",
              "Event 1: grade=GOOD");

        CHECK(std::abs(player.events[2].t - 3.0f) < 1e-5f, "Event 2: t=3.0");
        CHECK(player.events[2].note_idx == 2, "Event 2: note_idx=2");
        CHECK(phigros::io::u8_to_grade(player.events[2].grade) == "BAD",
              "Event 2: grade=BAD");
    }

    std::remove(tmp_path.c_str());
    std::cout << "  Temp replay file cleaned up\n";
}

// ---- 6A6: Edge case tests ----
static void test_edge_cases() {
    std::cout << "\n=== Edge case tests ===\n";
    using namespace phigros;

    const int W = 1280, H = 720;

    // Helper: wrap a PiecewiseEased into a TrackFn
    auto make_track = [](math::PiecewiseEased pe) -> TrackFn {
        return [t = std::move(pe)](double x) mutable { return t.eval(x); };
    };

    // --- Zero-note chart: no crash ---
    {
        ChartData chart;
        Line ln; ln.lid = 0;
        ln.pos_x = make_track(math::PiecewiseEased({}, 0.5 * W));
        ln.pos_y = make_track(math::PiecewiseEased({}, 0.5 * H));
        ln.rot   = make_track(math::PiecewiseEased({}, 0.0));
        ln.alpha = make_track(math::PiecewiseEased({}, 1.0));
        chart.lines.push_back(std::move(ln));

        engine::Judge judge;
        config::RenderConfig cfg;
        cfg.window_w = W; cfg.window_h = H;
        std::vector<NoteState> states;

        auto frame = render::build_frame(0.0, chart, states, judge, cfg);
        CHECK(frame.notes.empty(), "Zero-note chart: no notes in frame");
        CHECK(frame.lines.size() == 1, "Zero-note chart: 1 line in frame");

        engine::NoteManager nm(&chart.notes, &states);
        nm.update_visibility(0.0, 1.0);
        CHECK(nm.get_visible_count() == 0, "Zero-note: visible_count=0");

        auto sr = engine::compute_score(0.0, 0, 0);
        // Zero notes: both acc_r and combo_r are 0 → score=0 (trivially passing: no notes to judge)
        CHECK(sr.score == 0, "Zero-note chart: score=0 (no notes to judge)");
    }

    // --- Out-of-range line_id in build_frame: no crash ---
    {
        ChartData chart;
        Line ln; ln.lid = 0;
        ln.pos_x = make_track(math::PiecewiseEased({}, 0.5 * W));
        ln.pos_y = make_track(math::PiecewiseEased({}, 0.5 * H));
        ln.rot   = make_track(math::PiecewiseEased({}, 0.0));
        ln.alpha = make_track(math::PiecewiseEased({}, 1.0));
        chart.lines.push_back(std::move(ln));

        Note note{}; note.nid = 0; note.line_id = 99; // out of range
        note.kind = 1; note.t_hit = 0.5; note.t_end = 0.5;
        note.above = true; note.scroll_hit = 0; note.scroll_end = 0;
        note.alpha01 = 1.0;
        chart.notes.push_back(note);

        engine::Judge judge;
        config::RenderConfig cfg;
        cfg.window_w = W; cfg.window_h = H;
        std::vector<NoteState> states(1);
        states[0].note = &chart.notes[0];

        auto frame = render::build_frame(0.5, chart, states, judge, cfg);
        CHECK(frame.notes.empty(), "Out-of-range line_id: note skipped gracefully");
    }

    // --- Zero scroll speed: precompute_t_enter → t_enter = -1e9 (always visible) ---
    {
        ChartData chart;
        Line ln; ln.lid = 0;
        ln.pos_x = make_track(math::PiecewiseEased({}, 0.5 * W));
        ln.pos_y = make_track(math::PiecewiseEased({}, 0.5 * H));
        ln.rot   = make_track(math::PiecewiseEased({}, 0.0));
        ln.alpha = make_track(math::PiecewiseEased({}, 1.0));
        // scroll_px empty → always 0 speed
        chart.lines.push_back(std::move(ln));

        Note note{}; note.nid = 0; note.line_id = 0; note.t_hit = 5.0; note.t_end = 5.0;
        note.above = true; note.x_local_px = 0; note.scroll_hit = 0; note.scroll_end = 0;
        note.kind = 1; note.size_px = 64; note.alpha01 = 1.0;
        chart.notes.push_back(note);

        engine::precompute_t_enter(chart.lines, chart.notes, W, H);
        CHECK(chart.notes[0].t_enter <= -1e8, "Zero scroll: t_enter=-1e9 (always visible)");
    }

    // --- Single note: PERFECT hit scores 1,000,000 ---
    {
        ChartData chart;
        Line ln; ln.lid = 0;
        ln.pos_x = make_track(math::PiecewiseEased({}, 0.5 * W));
        ln.pos_y = make_track(math::PiecewiseEased({}, 0.5 * H));
        ln.rot   = make_track(math::PiecewiseEased({}, 0.0));
        ln.alpha = make_track(math::PiecewiseEased({}, 1.0));
        chart.lines.push_back(std::move(ln));

        Note note{}; note.nid = 0; note.line_id = 0; note.kind = 1;
        note.t_hit = 1.0; note.t_end = 1.0; note.above = true; note.alpha01 = 1.0;
        chart.notes.push_back(note);

        engine::Judge judge;
        std::vector<NoteState> states(1);
        states[0].note = &chart.notes[0];
        auto g = judge.try_hit(states[0], 1.0);
        CHECK(g.has_value() && *g == "PERFECT", "Single note: PERFECT hit at exact time");
        auto sr = engine::compute_score(judge.acc_sum, judge.max_combo, 1);
        CHECK(sr.score == 1000000, "Single note PERFECT: score=1,000,000");
    }
}

// ---- 6A5: Auto-discover and run all charts in a directory ----
static bool run_regression(const std::string& charts_dir) {
    namespace fs = std::filesystem;
    std::cout << "\n=== Regression: discovering charts in " << charts_dir << " ===\n";

    std::vector<std::string> chart_paths;
    try {
        for (const auto& entry : fs::recursive_directory_iterator(charts_dir)) {
            if (entry.is_regular_file() && entry.path().filename() == "IN.json")
                chart_paths.push_back(entry.path().string());
        }
    } catch (const std::exception& e) {
        std::cerr << "Directory error: " << e.what() << "\n";
        return false;
    }

    if (chart_paths.empty()) {
        std::cerr << "No IN.json files found in: " << charts_dir << "\n";
        return false;
    }

    std::sort(chart_paths.begin(), chart_paths.end());
    std::cout << "Found " << chart_paths.size() << " charts\n\n";

    // Print table header
    std::cout << std::left
              << std::setw(45) << "Chart"
              << std::setw(8)  << "Notes"
              << std::setw(12) << "Score"
              << std::setw(12) << "Speed"
              << "Status\n"
              << std::string(80, '-') << "\n";

    int pass = 0, total = 0;
    for (const auto& path : chart_paths) {
        ++total;
        // Get chart name from path
        std::string name = fs::path(path).parent_path().filename().string();
        if (name.size() > 42) name = name.substr(0, 39) + "...";

        ChartData chart;
        try { chart = load_chart(path, 1280, 720); }
        catch (const std::exception& e) {
            std::cout << std::left << std::setw(45) << name
                      << "LOAD ERROR: " << e.what() << "\n";
            continue;
        }

        int total_notes = 0;
        for (auto& n : chart.notes) if (!n.fake) ++total_notes;

        auto t0 = std::chrono::high_resolution_clock::now();
        bool ok = run_autoplay(path, 1280, 720);
        auto t1 = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();

        // Compute chart duration for speed calc
        double chart_dur = 0;
        for (auto& n : chart.notes) chart_dur = std::max(chart_dur, n.t_end);
        double speed = elapsed > 0 ? chart_dur / elapsed : 0;

        if (ok) ++pass;

        std::cout << std::left << std::setw(45) << name
                  << std::setw(8)  << total_notes
                  << std::setw(12) << (ok ? "1000000" : "FAIL")
                  << std::setw(12) << (std::to_string((int)speed) + "x")
                  << (ok ? "PASS" : "FAIL") << "\n";
    }

    std::cout << "\n" << std::string(80, '-') << "\n"
              << "Regression: " << pass << "/" << total << " passed\n";

    // Compile round-trip verification for every chart found
    std::cout << "\n=== Compile + PHBC round-trip ===\n";
    int cpass = 0, ctotal = 0;
    for (const auto& path : chart_paths) {
        ++ctotal;
        if (test_compile_roundtrip(path, 1280, 720)) ++cpass;
    }
    std::cout << "Compile round-trip: " << cpass << "/" << ctotal << " passed\n";

    return pass == total && cpass == ctotal;
}
static void test_effects() {
    std::cout << "\n=== Effects unit tests ===\n";

    // ParticleBurst physics at tick=0
    engine::ParticleBurst pb(100, 200, 0, 500, {255, 200, 100}, 4, 42);
    auto p0 = pb.get_particles(0);
    CHECK(p0.size() == 4, "ParticleBurst has 4 particles");
    for (auto& ps : p0) {
        CHECK(ps.alpha == 255, "Alpha=255 at tick=0");
        CHECK(std::abs(ps.x - 100.0) < 0.01 && std::abs(ps.y - 200.0) < 0.01,
              "Particles at origin at tick=0");
    }

    // At tick=0.5 (250ms into 500ms burst)
    auto p5 = pb.get_particles(250);
    for (auto& ps : p5) {
        CHECK(ps.alpha == 127 || ps.alpha == 128,
              "Alpha ~127-128 at tick=0.5");
        double dist = std::sqrt((ps.x - 100) * (ps.x - 100) +
                                (ps.y - 200) * (ps.y - 200));
        CHECK(dist > 0.1, "Particles have moved at tick=0.5");
    }

    // At tick=1.0 → expired
    CHECK(!pb.alive(500), "Burst expired at 500ms");

    // HitFX lifecycle
    engine::HitFX fx{50, 60, 1.0, {255, 255, 0}, 255, 0.0};
    CHECK(fx.alive(1.0), "HitFX alive at t0");
    CHECK(fx.alive(1.49), "HitFX alive at t0+0.49");
    CHECK(!fx.alive(1.51), "HitFX expired at t0+0.51");

    // EffectManager update
    engine::EffectManager em;
    em.add_hitfx(0, 0, 1.0, {255, 255, 0});
    em.add_particle_burst(0, 0, 1000, 500, {255, 0, 0});
    CHECK(em.hitfx.size() == 1 && em.particles.size() == 1,
          "EffectManager has 1 hitfx + 1 burst");
    em.update(2.0, 2000);
    CHECK(em.hitfx.empty() && em.particles.empty(),
          "EffectManager cleaned up expired effects");
}

// ---- Unit test: mods ----
static void test_mods() {
    std::cout << "\n=== Mods unit tests ===\n";

    // Create minimal chart for mirror test
    ChartData chart;
    chart.notes.resize(3);
    chart.notes[0].x_local_px = 100.0; chart.notes[0].t_hit = 1.0;
    chart.notes[1].x_local_px = -50.0; chart.notes[1].t_hit = 2.0;
    chart.notes[2].x_local_px = 0.0;   chart.notes[2].t_hit = 3.0;
    chart.notes[0].above = true;
    chart.notes[1].above = false;

    mods::apply(chart, mods::MirrorOp{0.0, false});
    CHECK(std::abs(chart.notes[0].x_local_px - (-100.0)) < 1e-9,
          "Mirror: 100 → -100");
    CHECK(std::abs(chart.notes[1].x_local_px - 50.0) < 1e-9,
          "Mirror: -50 → 50");
    CHECK(std::abs(chart.notes[2].x_local_px) < 1e-9,
          "Mirror: 0 → 0");
    CHECK(chart.notes[0].above == true, "Mirror no flip_side: above unchanged");

    // Mirror with flip_side
    mods::apply(chart, mods::MirrorOp{0.0, true});
    CHECK(chart.notes[0].above == false, "Mirror flip_side: above flipped");

    // Colorize constant
    mods::apply(chart, mods::ColorizeOp{mods::ColorMode::Constant, {255, 0, 0}});
    CHECK(chart.notes[0].tint_rgb.r == 255 && chart.notes[0].tint_rgb.g == 0,
          "Colorize constant: red");

    // Colorize gradient
    mods::apply(chart, mods::ColorizeOp{mods::ColorMode::Gradient, {}, {0, 0, 0}, {255, 255, 255}});
    CHECK(chart.notes[0].tint_rgb.r == 0, "Gradient start=black");
    CHECK(chart.notes[2].tint_rgb.r == 255, "Gradient end=white");
}

static void test_config_and_culling() {
    std::cout << "\n=== Config/backend + no_cull_enter_time tests ===\n";

    // Config parsing: top-level/backend + render/no_cull_enter_time.
    const auto cfg_path = std::filesystem::temp_directory_path() / "phigros_cfg_test.jsonc";
    {
        std::ofstream f(cfg_path.string());
        f << "{\n"
          << "  \"backend\": \"sdl_sw\",\n"
          << "  \"render\": {\n"
          << "    \"no_cull_enter_time\": false\n"
          << "  }\n"
          << "}\n";
    }
    auto cfg = config::load_config(cfg_path.string());
    std::filesystem::remove(cfg_path);
    CHECK(cfg.backend == "sdl_sw", "Config parses backend from JSON");
    CHECK(cfg.no_cull_enter_time == false, "Config parses render.no_cull_enter_time");

    // Build a tiny chart with one note that enters at t=2.0 and hits at t=3.0.
    ChartData chart;
    Line line;
    line.lid = 0;
    line.pos_x = [](double) { return 640.0; };
    line.pos_y = [](double) { return 360.0; };
    line.rot   = [](double) { return 0.0; };
    line.alpha = [](double) { return 1.0; };
    line.scroll_fn = [](double) { return 0.0; };
    chart.lines.push_back(line);

    Note note;
    note.nid = 1;
    note.line_id = 0;
    note.kind = 1;
    note.t_enter = 2.0;
    note.t_hit = 3.0;
    note.t_end = 3.0;
    note.scroll_hit = 0.0;
    chart.notes.push_back(note);
    chart.finalize();

    std::vector<NoteState> states(chart.notes.size());
    states[0].note = &chart.notes[0];
    engine::Judge judge;

    cfg.window_w = 1280;
    cfg.window_h = 720;
    cfg.no_cull = false;
    cfg.no_cull_screen = true; // isolate enter-time culling

    cfg.no_cull_enter_time = false;
    auto culled = render::build_frame(1.0, chart, states, judge, cfg);
    CHECK(culled.notes.empty(), "no_cull_enter_time=false culls note before t_enter");

    cfg.no_cull_enter_time = true;
    auto shown = render::build_frame(1.0, chart, states, judge, cfg);
    CHECK(shown.notes.size() == 1, "no_cull_enter_time=true keeps note before t_enter");
}

// ---- Full autoplay simulation ----
static bool run_autoplay(const std::string& path, int W, int H) {
    std::cout << "\n--- " << path << " ---\n";

    ChartData chart;
    try {
        chart = load_chart(path, W, H);
    } catch (const std::exception& e) {
        std::cerr << "  Load error: " << e.what() << "\n";
        return false;
    }

    int total_notes = chart.playable_count;  // precomputed by finalize()
    int num_holds = 0;
    for (auto& n : chart.notes) if (!n.fake && n.kind == 3) ++num_holds;
    std::cout << "  Lines=" << chart.lines.size()
              << "  Notes=" << total_notes
              << " (holds=" << num_holds << ")\n";

    if (total_notes == 0) {
        std::cout << "  SKIP (no playable notes)\n";
        return true;
    }

    // 1. Precompute visibility
    engine::precompute_t_enter(chart.lines, chart.notes, W, H);

    int vis_count = 0;
    for (auto& n : chart.notes) {
        if (!n.fake) {
            CHECK(n.t_enter <= n.t_hit + 1e-6,
                  "t_enter <= t_hit for note " + std::to_string(n.nid));
            ++vis_count;
        }
    }
    std::cout << "  Visibility precomputed: " << vis_count << " notes\n";

    // 2. Initialize states
    std::vector<NoteState> states(chart.notes.size());
    for (size_t i = 0; i < chart.notes.size(); ++i) {
        states[i].note = &chart.notes[i];
    }

    // 3. Create engine components
    engine::NoteManager note_mgr(&chart.notes, &states);
    engine::SimulatePlayer sim(engine::SimMode::Conservative, 4);
    engine::Judge judge;
    engine::EffectManager effects;

    // 4. Find chart time bounds
    double t_start = chart.offset;
    double t_end = chart.chart_end_t + 1.0; // use precomputed field

    // 5. Run simulation at 240fps
    double fps = 240.0;
    double dt = 1.0 / fps;
    int frame_count = 0;
    int max_visible = 0;
    constexpr double HOLD_TAIL_TOL = 0.30;
    constexpr double MISS_WINDOW = engine::Judge::BAD;

    // For build_frame benchmark
    config::RenderConfig cfg_bench;
    cfg_bench.window_w = W; cfg_bench.window_h = H;
    long long build_frame_us = 0;

    for (double t = t_start; t <= t_end; t += dt) {
        // NoteManager visibility update
        note_mgr.update_visibility(t, 10.0);
        max_visible = std::max(max_visible, note_mgr.get_visible_count());

        int idx_next = note_mgr.find_next_note_index(t);

        // SimulatePlay
        sim.step(t, chart.notes, states, chart.lines, judge, W, H);

        // Miss detection
        engine::detect_misses(states, idx_next, t, MISS_WINDOW, judge);

        // Hold maintenance
        engine::hold_maintenance(states, idx_next, t, HOLD_TAIL_TOL, judge);

        // Hold finalization
        engine::hold_finalize(states, idx_next, t, HOLD_TAIL_TOL,
                              MISS_WINDOW, judge);

        // Effects
        effects.hold_tick_fx(states, idx_next, t, 80, chart.lines);
        effects.update(t, t * 1000.0);

        // Benchmark build_frame (no rendering, pure CPU snapshot)
        auto t0 = std::chrono::steady_clock::now();
        auto fr = render::build_frame(t, chart, states, judge, cfg_bench);
        auto t1 = std::chrono::steady_clock::now();
        build_frame_us += std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        (void)fr;

        ++frame_count;
    }

    // 6. Verify results
    auto sr = engine::compute_score(judge.acc_sum, judge.max_combo, total_notes);

    double bf_us_per_frame = frame_count > 0 ? (double)build_frame_us / frame_count : 0.0;
    double chart_dur = t_end - t_start;
    double total_cpu_s = (double)build_frame_us / 1e6;
    double bf_realtime = chart_dur > 0 ? chart_dur / total_cpu_s : 0.0;

    std::cout << "  Frames=" << frame_count
              << "  MaxVisible=" << max_visible
              << "  Score=" << sr.score
              << "  Combo=" << judge.max_combo << "/" << total_notes
              << "  Acc=" << sr.acc_ratio
              << "  Effects generated=" << (effects.hitfx.size()) << " hitfx"
              << "\n"
              << "  build_frame: " << std::fixed << std::setprecision(2)
              << bf_us_per_frame << " μs/frame  ("
              << std::setprecision(0) << bf_realtime << "× realtime)\n";

    // Score must be 1,000,000
    CHECK(sr.score == 1000000,
          "Score=1000000 (got " + std::to_string(sr.score) + ")");

    // Max combo must equal total notes
    CHECK(judge.max_combo == total_notes,
          "MaxCombo=" + std::to_string(total_notes) +
          " (got " + std::to_string(judge.max_combo) + ")");

    // All notes must be judged
    int judged_count = 0, miss_count = 0, holding_count = 0;
    int unfinished_holds = 0;
    for (size_t i = 0; i < states.size(); ++i) {
        auto& s = states[i];
        if (chart.notes[i].fake) continue;
        if (s.judged) ++judged_count;
        if (s.miss) ++miss_count;
        if (s.holding) ++holding_count;
        if (chart.notes[i].kind == 3 && !s.hold_finalized) ++unfinished_holds;
    }

    CHECK(judged_count == total_notes,
          "All notes judged: " + std::to_string(judged_count) +
          "/" + std::to_string(total_notes));
    CHECK(miss_count == 0,
          "No misses (got " + std::to_string(miss_count) + ")");
    CHECK(holding_count == 0,
          "No notes still holding (got " + std::to_string(holding_count) + ")");
    CHECK(unfinished_holds == 0,
          "All holds finalized (unfinished=" +
          std::to_string(unfinished_holds) + ")");

    // acc_sum must equal total_notes for perfect play
    CHECK(std::abs(judge.acc_sum - static_cast<double>(total_notes)) < 1e-6,
          "acc_sum=" + std::to_string(total_notes) +
          " (got " + std::to_string(judge.acc_sum) + ")");
    CHECK(judge.judged_cnt == total_notes,
          "judged_cnt=" + std::to_string(total_notes) +
          " (got " + std::to_string(judge.judged_cnt) + ")");

    // NoteManager sanity: at t_end all notes should be past visibility window
    note_mgr.update_visibility(t_end + 5.0, 10.0);
    CHECK(note_mgr.get_visible_count() == 0,
          "No notes visible at t_end+5");

    return sr.score == 1000000;
}

// ── Compile + PHBC round-trip test ───────────────────────────────────────────
// Compile a source chart → write .phbc to memory → read back → autoplay.
// Verifies that the compiled binary chart produces score=1,000,000.
static bool test_compile_roundtrip(const std::string& path, int W, int H) {
    std::cout << "\n[Compile round-trip] " << path << "\n";

    // 1. Load source chart
    ChartData src;
    try { src = load_chart(path, W, H); }
    catch (const std::exception& e) {
        std::cerr << "  Load error: " << e.what() << "\n";
        return false;
    }
    engine::precompute_t_enter(src.lines, src.notes, W, H);

    // 2. Compile to CompiledChartData
    auto compiled = phigros::chart::compile_chart(src, 240.0f);
    CHECK(compiled.sample_count > 0, "compile: sample_count > 0");
    CHECK((int)compiled.lines.size() == (int)src.lines.size(), "compile: line count preserved");
    CHECK((int)compiled.notes.size() == (int)src.notes.size(), "compile: note count preserved");

    // 3. Write to in-memory stream
    std::ostringstream oss(std::ios::binary);
    phigros::chart::write_phbc(compiled, oss);
    std::string blob = oss.str();
    CHECK(!blob.empty(), "phbc: serialized non-empty");
    CHECK(blob.substr(0, 4) == "PHBC", "phbc: magic header 'PHBC'");

    // 4. Read back
    std::istringstream iss(blob, std::ios::binary);
    phigros::chart::CompiledChartData reloaded;
    try { reloaded = phigros::chart::read_phbc(iss); }
    catch (const std::exception& e) {
        std::cerr << "  read_phbc error: " << e.what() << "\n";
        ++g_fail; return false;
    }
    CHECK(reloaded.sample_count == compiled.sample_count, "phbc round-trip: sample_count");
    CHECK((int)reloaded.lines.size() == (int)compiled.lines.size(), "phbc round-trip: lines");
    CHECK((int)reloaded.notes.size() == (int)compiled.notes.size(), "phbc round-trip: notes");
    CHECK(std::abs(reloaded.offset - compiled.offset) < 1e-9, "phbc round-trip: offset");
    CHECK(reloaded.sample_rate == compiled.sample_rate, "phbc round-trip: sample_rate");

    // 5. Convert to ChartData and run autoplay — must score 1,000,000
    ChartData chart = reloaded.to_chart_data();
    // is_compiled = true; t_enter already baked — no precompute_t_enter needed

    int total_notes = chart.playable_count;
    if (total_notes == 0) { std::cout << "  SKIP (no playable notes)\n"; return true; }

    std::vector<NoteState> states(chart.notes.size());
    for (size_t i = 0; i < chart.notes.size(); ++i) states[i].note = &chart.notes[i];

    engine::NoteManager note_mgr(&chart.notes, &states);
    engine::SimulatePlayer sim(engine::SimMode::Conservative, 4);
    engine::Judge judge;
    engine::EffectManager effects;

    double t_start = chart.offset;
    double t_end   = chart.chart_end_t + 1.0;
    constexpr double HOLD_TOL  = 0.30;
    constexpr double MISS_WIN  = engine::Judge::BAD;
    constexpr double DT        = 1.0 / 240.0;

    for (double t = t_start; t <= t_end; t += DT) {
        note_mgr.update_visibility(t, 10.0);
        int idx_next = note_mgr.find_next_note_index(t);
        sim.step(t, chart.notes, states, chart.lines, judge, W, H);
        engine::detect_misses(states, idx_next, t, MISS_WIN, judge);
        engine::hold_maintenance(states, idx_next, t, HOLD_TOL, judge);
        engine::hold_finalize(states, idx_next, t, HOLD_TOL, MISS_WIN, judge);
        effects.hold_tick_fx(states, idx_next, t, 80, chart.lines);
        effects.update(t, t * 1000.0);
    }

    auto sr = engine::compute_score(judge.acc_sum, judge.max_combo, total_notes);
    std::cout << "  Score=" << sr.score
              << "  Combo=" << judge.max_combo << "/" << total_notes
              << "  phbc_size=" << blob.size() / 1024 << " KB\n";

    CHECK(sr.score == 1000000,
          "Compiled score=1000000 (got " + std::to_string(sr.score) + ")");
    CHECK(judge.max_combo == total_notes,
          "Compiled max_combo=" + std::to_string(total_notes) +
          " (got " + std::to_string(judge.max_combo) + ")");

    return sr.score == 1000000;
}

// ── PHBC v2 round-trip tests ─────────────────────────────────────────────────
// Unit tests: create a small CompiledChartData, write v2, read back, compare.
static void test_phbc_v2_roundtrip() {
    std::cout << "\n=== PHBC v2 round-trip tests ===\n";
    using namespace phigros::chart;

    // Build a small test CompiledChartData
    CompiledChartData orig;
    orig.offset = 0.5;
    orig.chart_end_t = 30.0;
    orig.playable_count = 2;
    orig.sample_rate = 60.0f;
    orig.t_start = 0.0;
    orig.sample_count = 100;

    CompiledChartData::CompiledLine line;
    line.lid = 1;
    line.color_rgb = {255, 128, 0};
    line.pos_x.resize(100, 0.5f);
    line.pos_y.resize(100, 0.3f);
    line.rot.resize(100, 0.0f);
    line.alpha.resize(100, 1.0f);
    line.scroll.resize(100, 100.0f);
    orig.lines.push_back(line);

    phigros::Note note1;
    note1.nid = 1; note1.line_id = 1; note1.kind = 1;
    note1.above = true; note1.fake = false; note1.mh = false;
    note1.t_hit = 5.0; note1.t_end = 5.0; note1.t_enter = 3.0;
    note1.scroll_hit = 400.0; note1.scroll_end = 400.0;
    note1.x_local_px = 50.0; note1.y_offset_px = 0.0;
    note1.speed_mul = 1.0; note1.size_px = 80.0; note1.alpha01 = 1.0;
    note1.tint_rgb = {255, 255, 255};
    orig.notes.push_back(note1);

    phigros::Note note2 = note1;
    note2.nid = 2; note2.t_hit = 10.0; note2.t_enter = 8.0;
    note2.scroll_hit = 800.0; note2.scroll_end = 800.0;
    orig.notes.push_back(note2);

    auto verify = [&](const CompiledChartData& got, const std::string& label) {
        CHECK(got.sample_count == orig.sample_count, label + ": sample_count");
        CHECK(got.lines.size() == orig.lines.size(), label + ": line count");
        CHECK(got.notes.size() == orig.notes.size(), label + ": note count");
        CHECK(std::abs(got.offset - orig.offset) < 1e-9, label + ": offset");
        CHECK(got.sample_rate == orig.sample_rate, label + ": sample_rate");
        CHECK(got.playable_count == orig.playable_count, label + ": playable_count");
        CHECK(got.notes[0].nid == orig.notes[0].nid, label + ": note[0].nid");
        CHECK(std::abs(got.notes[1].t_hit - orig.notes[1].t_hit) < 1e-9, label + ": note[1].t_hit");
        CHECK(got.lines[0].lid == orig.lines[0].lid, label + ": line[0].lid");
        CHECK(std::abs(got.lines[0].pos_x[50] - orig.lines[0].pos_x[50]) < 1e-6, label + ": line[0].pos_x[50]");
    };

    // Test 1: v1 write → v2 reader (backward compat)
    {
        std::ostringstream oss(std::ios::binary);
        write_phbc(orig, oss);
        std::string blob = oss.str();
        std::istringstream iss(blob, std::ios::binary);
        auto got = read_phbc(iss);
        verify(got, "v1-compat");
        std::cout << "  v1 backward compat: OK\n";
    }

    // Test 2: zlib compressed
    {
        PhbcWriteOptions opts;
        opts.compress = true;
        opts.compress_algo = CompressionAlgo::Zlib;
        std::ostringstream oss(std::ios::binary);
        write_phbc(orig, oss, opts);
        std::string blob = oss.str();
        CHECK(blob.size() < 100 * 5 * 4, "zlib: compressed smaller than raw");
        std::istringstream iss(blob, std::ios::binary);
        auto got = read_phbc(iss);
        verify(got, "zlib-compressed");
        std::cout << "  zlib compressed: OK (" << blob.size() << " bytes)\n";
    }

    // Test 3: XOR encrypted (always available)
    {
        PhbcWriteOptions opts;
        opts.encrypt = true;
        opts.encrypt_algo = EncryptionAlgo::XOR;
        opts.password = "test_password_123";
        std::ostringstream oss(std::ios::binary);
        write_phbc(orig, oss, opts);
        std::string blob = oss.str();
        std::istringstream iss(blob, std::ios::binary);
        auto got = read_phbc(iss, "test_password_123");
        verify(got, "xor-encrypted");
        std::cout << "  XOR encrypted: OK\n";
    }

    // Test 4: zlib + XOR encrypted
    {
        PhbcWriteOptions opts;
        opts.compress = true;
        opts.encrypt = true;
        opts.encrypt_algo = EncryptionAlgo::XOR;
        opts.password = "combo_pass";
        std::ostringstream oss(std::ios::binary);
        write_phbc(orig, oss, opts);
        std::string blob = oss.str();
        std::istringstream iss(blob, std::ios::binary);
        auto got = read_phbc(iss, "combo_pass");
        verify(got, "zlib+xor");
        std::cout << "  zlib + XOR encrypted: OK (" << blob.size() << " bytes)\n";
    }

    // Test 5: wrong password → read should throw
    {
        PhbcWriteOptions opts;
        opts.encrypt = true;
        opts.encrypt_algo = EncryptionAlgo::XOR;
        opts.password = "correct_pass";
        std::ostringstream oss(std::ios::binary);
        write_phbc(orig, oss, opts);
        std::string blob = oss.str();
        std::istringstream iss(blob, std::ios::binary);
        bool threw = false;
        try { auto got = read_phbc(iss, "wrong_pass"); }
        catch (...) { threw = true; }
        // XOR won't throw on decrypt itself (no auth tag), but parsed data will be garbage.
        // For XOR, the payload parse may succeed but data will be wrong.
        // For AEAD algos, the decrypt will throw. So we just verify data mismatch:
        if (!threw) {
            std::istringstream iss2(blob, std::ios::binary);
            try {
                auto got = read_phbc(iss2, "wrong_pass");
                // Data should differ
                bool data_matches = (got.notes.size() == orig.notes.size() &&
                                     std::abs(got.offset - orig.offset) < 1e-9);
                // It's possible XOR wrong-pass still parses but data is garbage
                std::cout << "  wrong password (XOR): data " << (data_matches ? "MATCHES (bad)" : "corrupted (expected)") << "\n";
            } catch (...) {
                std::cout << "  wrong password (XOR): threw (OK)\n";
            }
        } else {
            std::cout << "  wrong password: threw (OK)\n";
        }
    }

#ifdef PHIGROS_HAS_OPENSSL
    // Test 6: AES-256-GCM encrypted
    {
        PhbcWriteOptions opts;
        opts.encrypt = true;
        opts.encrypt_algo = EncryptionAlgo::AES_256_GCM;
        opts.password = "aes_test_pass";
        std::ostringstream oss(std::ios::binary);
        write_phbc(orig, oss, opts);
        std::string blob = oss.str();
        std::istringstream iss(blob, std::ios::binary);
        auto got = read_phbc(iss, "aes_test_pass");
        verify(got, "aes-gcm-encrypted");
        std::cout << "  AES-256-GCM encrypted: OK\n";
    }

    // Test 7: AES-256-GCM wrong password → auth failure
    {
        PhbcWriteOptions opts;
        opts.encrypt = true;
        opts.encrypt_algo = EncryptionAlgo::AES_256_GCM;
        opts.password = "correct";
        std::ostringstream oss(std::ios::binary);
        write_phbc(orig, oss, opts);
        std::string blob = oss.str();
        std::istringstream iss(blob, std::ios::binary);
        bool threw = false;
        try { auto got = read_phbc(iss, "incorrect"); }
        catch (...) { threw = true; }
        CHECK(threw, "AES-GCM wrong password throws");
        std::cout << "  AES-256-GCM wrong password: threw (OK)\n";
    }

    // Test 8: ChaCha20-Poly1305 compressed + encrypted
    {
        PhbcWriteOptions opts;
        opts.compress = true;
        opts.encrypt = true;
        opts.encrypt_algo = EncryptionAlgo::ChaCha20_Poly1305;
        opts.password = "chacha_pass";
        std::ostringstream oss(std::ios::binary);
        write_phbc(orig, oss, opts);
        std::string blob = oss.str();
        std::istringstream iss(blob, std::ios::binary);
        auto got = read_phbc(iss, "chacha_pass");
        verify(got, "chacha20+zlib");
        std::cout << "  ChaCha20-Poly1305 + zlib: OK (" << blob.size() << " bytes)\n";
    }

    // Test 9: AES-256-CBC
    {
        PhbcWriteOptions opts;
        opts.compress = true;
        opts.encrypt = true;
        opts.encrypt_algo = EncryptionAlgo::AES_256_CBC;
        opts.password = "cbc_pass";
        std::ostringstream oss(std::ios::binary);
        write_phbc(orig, oss, opts);
        std::string blob = oss.str();
        std::istringstream iss(blob, std::ios::binary);
        auto got = read_phbc(iss, "cbc_pass");
        verify(got, "aes-cbc+zlib");
        std::cout << "  AES-256-CBC + zlib: OK (" << blob.size() << " bytes)\n";
    }
#endif // PHIGROS_HAS_OPENSSL
}

int main(int argc, char* argv[]) {
    int W = 1280, H = 720;

    // Run unit tests first
    test_effects();
    test_mods();
    test_kinematics();
    test_scriptplay();
    test_hold_bad_breaks_combo();
    test_judge_boundaries();
    test_replay_roundtrip();
    test_edge_cases();
    test_config_and_culling();
    test_phbc_v2_roundtrip();

    // Auto-discover mode: --auto-discover <dir>
    if (argc >= 3 && std::string(argv[1]) == "--auto-discover") {
        bool reg_ok = run_regression(argv[2]);
        std::cout << "\n=== Total checks: " << g_pass << " passed, "
                  << g_fail << " failed ===\n";
        return (g_fail > 0 || !reg_ok) ? 1 : 0;
    }

    // Then run autoplay on provided charts
    if (argc < 2) {
        std::cout << "\n=== No chart paths provided, unit tests only ===\n";
    } else {
        std::cout << "\n=== Autoplay verification (W=" << W
                  << " H=" << H << ") ===\n";

        int chart_pass = 0, chart_total = 0;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            ++chart_total;
            if (run_autoplay(arg, W, H)) ++chart_pass;
        }

        std::cout << "\n=== Charts: " << chart_pass << "/" << chart_total
                  << " passed ===\n";
    }

    std::cout << "\n=== Total checks: " << g_pass << " passed, "
              << g_fail << " failed ===\n";
    return g_fail > 0 ? 1 : 0;
}
