// Comprehensive Phase 2 engine test.
// Validates: visibility, note manager, hold system, miss detection,
//            simulate play, effects, mods — end-to-end autoplay verification.
//
// Usage: test_engine <chart_path> [W] [H]

#include "phigros/chart/official.hpp"
#include "phigros/chart/rpe.hpp"
#include "phigros/chart/pec.hpp"
#include "phigros/engine/kinematics.hpp"
#include "phigros/engine/judge.hpp"
#include "phigros/engine/visibility.hpp"
#include "phigros/engine/note_manager.hpp"
#include "phigros/engine/hold_logic.hpp"
#include "phigros/engine/simulateplay.hpp"
#include "phigros/engine/effects.hpp"
#include "phigros/core/mods.hpp"
#include "phigros/config/render_config.hpp"
#include <fstream>
#include <iostream>
#include <string>
#include <cmath>
#include <cstdlib>
#include <cassert>
#include <nlohmann/json.hpp>

using namespace phigros;
using json = nlohmann::json;

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { ++g_fail; std::cerr << "  FAIL: " << msg << "\n"; } \
    else { ++g_pass; } \
} while(0)

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

// ---- Unit test: effects ----
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

    mods::apply_mirror(chart, 0.0, false);
    CHECK(std::abs(chart.notes[0].x_local_px - (-100.0)) < 1e-9,
          "Mirror: 100 → -100");
    CHECK(std::abs(chart.notes[1].x_local_px - 50.0) < 1e-9,
          "Mirror: -50 → 50");
    CHECK(std::abs(chart.notes[2].x_local_px) < 1e-9,
          "Mirror: 0 → 0");
    CHECK(chart.notes[0].above == true, "Mirror no flip_side: above unchanged");

    // Mirror with flip_side
    mods::apply_mirror(chart, 0.0, true);
    CHECK(chart.notes[0].above == false, "Mirror flip_side: above flipped");

    // Colorize constant
    mods::apply_colorize(chart, mods::ColorMode::Constant, {255, 0, 0});
    CHECK(chart.notes[0].tint_rgb.r == 255 && chart.notes[0].tint_rgb.g == 0,
          "Colorize constant: red");

    // Colorize gradient
    mods::apply_colorize(chart, mods::ColorMode::Gradient,
                         {}, {0, 0, 0}, {255, 255, 255});
    CHECK(chart.notes[0].tint_rgb.r == 0, "Gradient start=black");
    CHECK(chart.notes[2].tint_rgb.r == 255, "Gradient end=white");
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

    int total_notes = 0;
    for (auto& n : chart.notes) if (!n.fake) ++total_notes;
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
    double t_end = 0.0;
    for (auto& n : chart.notes) t_end = std::max(t_end, n.t_end);
    t_end += 1.0; // buffer

    // 5. Run simulation at 240fps
    double fps = 240.0;
    double dt = 1.0 / fps;
    int frame_count = 0;
    int max_visible = 0;
    constexpr double HOLD_TAIL_TOL = 0.30;
    constexpr double MISS_WINDOW = engine::Judge::BAD;

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

        ++frame_count;
    }

    // 6. Verify results
    auto sr = engine::compute_score(judge.acc_sum, judge.max_combo, total_notes);

    std::cout << "  Frames=" << frame_count
              << "  MaxVisible=" << max_visible
              << "  Score=" << sr.score
              << "  Combo=" << judge.max_combo << "/" << total_notes
              << "  Acc=" << sr.acc_ratio
              << "  Effects generated=" << (effects.hitfx.size()) << " hitfx"
              << "\n";

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

int main(int argc, char* argv[]) {
    int W = 1280, H = 720;

    // Run unit tests first
    test_effects();
    test_mods();

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
