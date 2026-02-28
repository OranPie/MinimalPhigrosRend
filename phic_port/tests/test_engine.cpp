#include "phic/core/engine.hpp"
#include "phic/core/parser.hpp"

#include <cassert>
#include <string>
#include <vector>

int main() {
    const std::string chart_json = R"JSON({
        "name": "test",
        "judgeLineList": [
            {
                "bpm": 120.0,
                "notesAbove": [
                    {"time": 64.0, "lane": 1, "type": 1},
                    {"time": 128.0, "lane": 2, "type": 3, "holdTime": 32.0}
                ]
            }
        ]
    })JSON";

    const auto parsed = phic::parse_chart_bytes(chart_json, "official");
    assert(parsed.ok);
    assert(parsed.chart.notes.size() == 2);
    assert(parsed.chart.notes[0].kind == phic::NoteKind::Tap);
    assert(parsed.chart.notes[1].kind == phic::NoteKind::Hold);
    assert(!parsed.chart.notes[0].above);
    assert(!parsed.chart.notes[1].above);
    assert(!parsed.chart.notes[0].fake);
    assert(parsed.chart.notes[0].alpha01 > 0.99);
    assert(parsed.chart.notes[0].t_hit > 0.99 && parsed.chart.notes[0].t_hit < 1.01);
    assert(parsed.chart.notes[1].t_hit > 1.99 && parsed.chart.notes[1].t_hit < 2.01);
    assert(parsed.chart.notes[1].hold_end > 2.49 && parsed.chart.notes[1].hold_end < 2.51);

    const std::string rpe_type_chart = R"JSON({
        "name": "rpe-types",
        "notes": [
            {"time": 1.0, "lane": 0, "type": 2, "endTime": 1.3, "above": 0, "alpha": 128, "isFake": 1, "speed": 1.5},
            {"time": 2.0, "lane": 1, "type": 3, "above": 1, "alpha": 1.0},
            {"time": 3.0, "lane": 2, "type": 4}
        ]
    })JSON";
    const auto parsed_rpe = phic::parse_chart_bytes(rpe_type_chart, "rpe");
    assert(parsed_rpe.ok);
    assert(parsed_rpe.chart.notes.size() == 3);
    assert(parsed_rpe.chart.notes[0].kind == phic::NoteKind::Hold);
    assert(parsed_rpe.chart.notes[1].kind == phic::NoteKind::Flick);
    assert(parsed_rpe.chart.notes[2].kind == phic::NoteKind::Drag);
    assert(parsed_rpe.chart.notes[0].above);
    assert(parsed_rpe.chart.notes[0].fake);
    assert(parsed_rpe.chart.notes[0].alpha01 > 0.49 && parsed_rpe.chart.notes[0].alpha01 < 0.51);
    assert(parsed_rpe.chart.notes[0].speed_mul > 1.49 && parsed_rpe.chart.notes[0].speed_mul < 1.51);
    assert(parsed_rpe.chart.notes[0].t_hit > 0.49 && parsed_rpe.chart.notes[0].t_hit < 0.51);
    assert(parsed_rpe.chart.notes[0].hold_end > parsed_rpe.chart.notes[0].t_hit);

    phic::RenderConfig cfg;
    cfg.autoplay = true;

    phic::Engine engine(cfg);
    engine.load_chart(parsed.chart);

    auto first_frame = engine.step(0.0, {});
    assert(!first_frame.frame_commands.empty());
    assert(first_frame.frame_commands[0].t_hit_sec > 0.99 && first_frame.frame_commands[0].t_hit_sec < 1.01);
    assert(first_frame.frame_commands[0].hold_end_sec >= first_frame.frame_commands[0].t_hit_sec);

    phic::Engine::StepResult step;
    for (int i = 0; i < 500; ++i) {
        step = engine.step(1.0 / 120.0, {});
    }

    assert(step.stats.judged_cnt == 2);
    assert(step.stats.hit_total == 2);
    assert(step.stats.max_combo == 2);
    assert(step.stats.accuracy() > 0.99);

    phic::Engine engine2(phic::RenderConfig{});
    engine2.load_chart(parsed.chart);
    for (int i = 0; i < 500; ++i) {
        step = engine2.step(1.0 / 120.0, {});
    }
    assert(step.stats.judged_cnt == 2);
    assert(step.stats.hit_total == 0);

    phic::RenderConfig speed_cfg;
    speed_cfg.autoplay = true;
    speed_cfg.note_speed = 2.0;
    phic::Engine speed_engine(speed_cfg);
    speed_engine.load_chart(parsed.chart);
    phic::Engine::StepResult speed_step;
    for (int i = 0; i < 120; ++i) {
        speed_step = speed_engine.step(1.0 / 120.0, {});
    }
    assert(speed_step.time_sec > 1.9 && speed_step.time_sec < 2.1);
    assert(speed_step.stats.judged_cnt >= 1);

    phic::RenderConfig mod_cfg;
    mod_cfg.mods.mirror = true;
    mod_cfg.mods.transpose_sec = 1.0;
    mod_cfg.mods.thin_out_every = 2;
    mod_cfg.mods.hold_convert_tap = true;
    mod_cfg.mods.quantize = true;
    mod_cfg.mods.quantize_step_sec = 0.25;
    mod_cfg.mods.wave = true;
    mod_cfg.mods.wave_amplitude_lane = 1.0;
    mod_cfg.mods.wave_period_sec = 2.0;
    mod_cfg.mods.stutter = true;
    mod_cfg.mods.stutter_repeat = 2;
    mod_cfg.mods.stutter_interval_sec = 0.05;

    phic::Engine mod_engine(mod_cfg);
    mod_engine.load_chart(parsed.chart);
    const auto& notes = mod_engine.chart().notes;
    assert(!notes.empty());
    assert(notes[0].kind == phic::NoteKind::Tap || notes[0].kind == phic::NoteKind::Drag);
    assert(notes[0].t_hit >= 2.0 && notes[0].t_hit <= 2.1);
    for (const auto& n : notes) {
        assert(n.kind != phic::NoteKind::Hold);
    }
    if (notes.size() > 1) {
        assert(notes[1].t_hit >= notes[0].t_hit);
    }

    phic::RenderConfig align_cfg;
    align_cfg.mods.attach_enable = true;
    align_cfg.mods.attach_kind = phic::NoteKind::Flick;
    align_cfg.mods.attach_lane_offset = 0;
    align_cfg.mods.attach_time_offset_sec = 0.03;
    align_cfg.mods.attach_has_side = true;
    align_cfg.mods.attach_side_mode = phic::ModConfig::SideMode::Flip;
    align_cfg.mods.attach_filter.active = true;
    align_cfg.mods.attach_filter.kinds.push_back(phic::NoteKind::Tap);
    align_cfg.mods.fade_enable = true;
    align_cfg.mods.fade_mode = phic::ModConfig::FadeMode::Constant;
    align_cfg.mods.fade_constant_alpha = 0.4;
    align_cfg.mods.note_overrides_enable = true;
    align_cfg.mods.note_overrides_apply_to_hold = true;
    align_cfg.mods.note_overrides_set.has_side = true;
    align_cfg.mods.note_overrides_set.side_mode = phic::ModConfig::SideMode::ForceAbove;
    phic::ModConfig::NoteRule rule{};
    rule.filter.active = true;
    rule.filter.kinds.push_back(phic::NoteKind::Flick);
    rule.set.has_alpha = true;
    rule.set.alpha01 = 0.7;
    align_cfg.mods.note_rules.push_back(rule);

    phic::Engine align_engine(align_cfg);
    align_engine.load_chart(parsed.chart);
    const auto& aligned_notes = align_engine.chart().notes;
    assert(aligned_notes.size() == 3);
    int flick_cnt = 0;
    bool has_attached = false;
    for (const auto& n : aligned_notes) {
        assert(n.above);
        if (n.kind == phic::NoteKind::Flick) {
            ++flick_cnt;
            assert(n.alpha01 > 0.69 && n.alpha01 < 0.71);
            if (n.t_hit > 1.02 && n.t_hit < 1.04) {
                has_attached = true;
            }
        } else {
            assert(n.alpha01 > 0.39 && n.alpha01 < 0.41);
        }
    }
    assert(flick_cnt >= 1);
    assert(has_attached);

    return 0;
}
