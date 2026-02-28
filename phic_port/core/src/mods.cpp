#include "phic/core/mods.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <unordered_set>

namespace phic {

namespace {

constexpr double kPi = 3.14159265358979323846;

double clamp01(double v) {
    return std::clamp(v, 0.0, 1.0);
}

bool contains_int(const std::vector<int>& xs, int x) {
    return std::find(xs.begin(), xs.end(), x) != xs.end();
}

bool contains_kind(const std::vector<NoteKind>& xs, NoteKind k) {
    return std::find(xs.begin(), xs.end(), k) != xs.end();
}

bool match_note_filter(const RuntimeNote& n, const ModConfig::NoteFilter& filter) {
    if (!filter.active) {
        return true;
    }
    if (!filter.line_ids.empty() && !contains_int(filter.line_ids, n.line_id)) {
        return false;
    }
    if (!filter.kinds.empty() && !contains_kind(filter.kinds, n.kind)) {
        return false;
    }
    if (!filter.exclude_kinds.empty() && contains_kind(filter.exclude_kinds, n.kind)) {
        return false;
    }
    if (filter.has_above && n.above != filter.above) {
        return false;
    }
    if (filter.has_fake && n.fake != filter.fake) {
        return false;
    }
    if (filter.has_t_hit_min && n.t_hit < filter.t_hit_min) {
        return false;
    }
    if (filter.has_t_hit_max && n.t_hit > filter.t_hit_max) {
        return false;
    }
    if (filter.has_t_end_min && n.hold_end < filter.t_end_min) {
        return false;
    }
    if (filter.has_t_end_max && n.hold_end > filter.t_end_max) {
        return false;
    }
    return true;
}

uint8_t kind_mask_bit(NoteKind kind) {
    switch (kind) {
        case NoteKind::Tap:
            return static_cast<uint8_t>(1U << 0);
        case NoteKind::Drag:
            return static_cast<uint8_t>(1U << 1);
        case NoteKind::Hold:
            return static_cast<uint8_t>(1U << 2);
        case NoteKind::Flick:
            return static_cast<uint8_t>(1U << 3);
    }
    return 0;
}

struct CompiledNoteFilter {
    bool active = false;
    std::unordered_set<int> line_ids{};
    uint8_t include_kind_mask = 0;
    bool include_kind_active = false;
    uint8_t exclude_kind_mask = 0;
    bool exclude_kind_active = false;
    bool has_above = false;
    bool above = true;
    bool has_fake = false;
    bool fake = false;
    bool has_t_hit_min = false;
    double t_hit_min = 0.0;
    bool has_t_hit_max = false;
    double t_hit_max = 0.0;
    bool has_t_end_min = false;
    double t_end_min = 0.0;
    bool has_t_end_max = false;
    double t_end_max = 0.0;

    bool matches(const RuntimeNote& n) const {
        if (!active) {
            return true;
        }
        if (!line_ids.empty() && line_ids.find(n.line_id) == line_ids.end()) {
            return false;
        }
        if (include_kind_active) {
            const uint8_t bit = kind_mask_bit(n.kind);
            if ((include_kind_mask & bit) == 0U) {
                return false;
            }
        }
        if (exclude_kind_active) {
            const uint8_t bit = kind_mask_bit(n.kind);
            if ((exclude_kind_mask & bit) != 0U) {
                return false;
            }
        }
        if (has_above && n.above != above) {
            return false;
        }
        if (has_fake && n.fake != fake) {
            return false;
        }
        if (has_t_hit_min && n.t_hit < t_hit_min) {
            return false;
        }
        if (has_t_hit_max && n.t_hit > t_hit_max) {
            return false;
        }
        if (has_t_end_min && n.hold_end < t_end_min) {
            return false;
        }
        if (has_t_end_max && n.hold_end > t_end_max) {
            return false;
        }
        return true;
    }
};

CompiledNoteFilter compile_note_filter(const ModConfig::NoteFilter& filter) {
    CompiledNoteFilter out;
    out.active = filter.active;
    if (!filter.active) {
        return out;
    }
    for (int lid : filter.line_ids) {
        out.line_ids.insert(lid);
    }
    if (!filter.kinds.empty()) {
        out.include_kind_active = true;
        for (NoteKind kind : filter.kinds) {
            out.include_kind_mask = static_cast<uint8_t>(out.include_kind_mask | kind_mask_bit(kind));
        }
    }
    if (!filter.exclude_kinds.empty()) {
        out.exclude_kind_active = true;
        for (NoteKind kind : filter.exclude_kinds) {
            out.exclude_kind_mask = static_cast<uint8_t>(out.exclude_kind_mask | kind_mask_bit(kind));
        }
    }
    out.has_above = filter.has_above;
    out.above = filter.above;
    out.has_fake = filter.has_fake;
    out.fake = filter.fake;
    out.has_t_hit_min = filter.has_t_hit_min;
    out.t_hit_min = filter.t_hit_min;
    out.has_t_hit_max = filter.has_t_hit_max;
    out.t_hit_max = filter.t_hit_max;
    out.has_t_end_min = filter.has_t_end_min;
    out.t_end_min = filter.t_end_min;
    out.has_t_end_max = filter.has_t_end_max;
    out.t_end_max = filter.t_end_max;
    return out;
}

void apply_side(RuntimeNote& n, ModConfig::SideMode side_mode) {
    switch (side_mode) {
        case ModConfig::SideMode::ForceAbove:
            n.above = true;
            break;
        case ModConfig::SideMode::ForceBelow:
            n.above = false;
            break;
        case ModConfig::SideMode::Flip:
            n.above = !n.above;
            break;
        case ModConfig::SideMode::Keep:
            break;
    }
}

void apply_note_set(RuntimeNote& n, const ModConfig::NoteSet& set) {
    if (set.has_kind) {
        n.kind = set.kind;
    }
    if (set.has_speed_mul) {
        n.speed_mul = set.speed_mul;
    }
    if (set.has_alpha) {
        n.alpha01 = clamp01(set.alpha01);
    }
    if (set.has_side) {
        apply_side(n, set.side_mode);
    }
}

void apply_transpose(std::vector<RuntimeNote>& notes, double offset_sec) {
    if (std::abs(offset_sec) <= 1e-12) {
        return;
    }
    for (auto& n : notes) {
        n.t_hit += offset_sec;
        n.hold_end += offset_sec;
    }
}

void apply_full_blue(std::vector<RuntimeNote>& notes, bool convert_non_hold_to_tap) {
    if (!convert_non_hold_to_tap) {
        return;
    }
    for (auto& n : notes) {
        if (n.kind != NoteKind::Hold) {
            n.kind = NoteKind::Tap;
        }
    }
}

void apply_stretch(std::vector<RuntimeNote>& notes, double factor, double anchor) {
    if (std::abs(factor - 1.0) <= 1e-12 || factor <= 0.0) {
        return;
    }
    for (auto& n : notes) {
        n.t_hit = anchor + (n.t_hit - anchor) * factor;
        n.hold_end = anchor + (n.hold_end - anchor) * factor;
    }
}

void apply_lane_scale(std::vector<RuntimeNote>& notes, int lane_count, double scale, double center) {
    lane_count = std::max(1, lane_count);
    if (std::abs(scale - 1.0) <= 1e-12) {
        return;
    }
    if (!std::isfinite(center) || center < 0.0) {
        center = 0.5 * static_cast<double>(lane_count - 1);
    }
    for (auto& n : notes) {
        const double shifted = center + (static_cast<double>(n.lane) - center) * scale;
        n.lane = std::clamp(static_cast<int>(std::lround(shifted)), 0, lane_count - 1);
    }
}

void apply_reverse(std::vector<RuntimeNote>& notes) {
    if (notes.empty()) {
        return;
    }
    double t_min = notes.front().t_hit;
    double t_max = notes.front().t_hit;
    for (const auto& n : notes) {
        t_min = std::min(t_min, n.t_hit);
        t_max = std::max(t_max, n.t_hit);
    }
    const double anchor = 0.5 * (t_min + t_max);
    for (auto& n : notes) {
        const double old_hit = n.t_hit;
        const double old_end = n.hold_end;
        n.t_hit = anchor - (old_hit - anchor);
        n.hold_end = anchor - (old_end - anchor);
        if (n.hold_end < n.t_hit) {
            std::swap(n.hold_end, n.t_hit);
        }
    }
}

void apply_quantize(std::vector<RuntimeNote>& notes, double step) {
    if (step <= 1e-9) {
        return;
    }
    for (auto& n : notes) {
        n.t_hit = std::round(n.t_hit / step) * step;
        n.hold_end = std::max(n.t_hit, std::round(n.hold_end / step) * step);
    }
}

void apply_mirror(std::vector<RuntimeNote>& notes, int lane_count) {
    lane_count = std::max(1, lane_count);
    for (auto& n : notes) {
        n.lane = std::clamp((lane_count - 1) - n.lane, 0, lane_count - 1);
        n.above = !n.above;
    }
}

void apply_wave(std::vector<RuntimeNote>& notes, int lane_count, double amp, double period) {
    lane_count = std::max(1, lane_count);
    period = std::max(1e-9, period);
    for (auto& n : notes) {
        const double phase = (n.t_hit / period) * 2.0 * kPi;
        const int shift = static_cast<int>(std::lround(std::sin(phase) * amp));
        n.lane = std::clamp(n.lane + shift, 0, lane_count - 1);
    }
}

void apply_randomize(std::vector<RuntimeNote>& notes, int lane_count, int seed) {
    lane_count = std::max(1, lane_count);
    std::mt19937 rng(static_cast<std::mt19937::result_type>(seed));
    std::uniform_int_distribution<int> dist(0, lane_count - 1);
    for (auto& n : notes) {
        n.lane = dist(rng);
    }
}

void apply_fade(std::vector<RuntimeNote>& notes, const ModConfig& mods) {
    if (!mods.fade_enable) {
        return;
    }
    const CompiledNoteFilter filter = compile_note_filter(mods.fade_filter);

    for (auto& n : notes) {
        if (n.fake || !filter.matches(n)) {
            continue;
        }

        double alpha = n.alpha01;
        switch (mods.fade_mode) {
            case ModConfig::FadeMode::Constant:
                alpha = mods.fade_constant_alpha;
                break;
            case ModConfig::FadeMode::Linear:
                // Keep parity with Python's current implementation (linear mode is reserved/no-op).
                break;
            case ModConfig::FadeMode::Time:
                if (mods.fade_has_time_start && mods.fade_has_time_end) {
                    if (n.t_hit <= mods.fade_time_start) {
                        alpha = mods.fade_alpha_start;
                    } else if (n.t_hit >= mods.fade_time_end) {
                        alpha = mods.fade_alpha_end;
                    } else {
                        const double span = std::max(1e-9, mods.fade_time_end - mods.fade_time_start);
                        const double p = (n.t_hit - mods.fade_time_start) / span;
                        alpha = mods.fade_alpha_start + (mods.fade_alpha_end - mods.fade_alpha_start) * p;
                    }
                } else if (mods.fade_has_time_start) {
                    if (n.t_hit <= mods.fade_time_start) {
                        alpha = mods.fade_alpha_start;
                    }
                } else if (mods.fade_has_time_end) {
                    if (n.t_hit >= mods.fade_time_end) {
                        alpha = mods.fade_alpha_end;
                    }
                }
                break;
        }
        alpha = std::clamp(alpha, mods.fade_alpha_min, mods.fade_alpha_max);
        n.alpha01 = clamp01(alpha);
    }
}

void apply_thin_out(std::vector<RuntimeNote>& notes, int every) {
    if (every <= 1) {
        return;
    }
    std::vector<RuntimeNote> filtered;
    filtered.reserve(notes.size());
    for (std::size_t i = 0; i < notes.size(); ++i) {
        if ((i % static_cast<std::size_t>(every)) == 0U) {
            filtered.push_back(notes[i]);
        }
    }
    notes.swap(filtered);
}

void apply_stutter(std::vector<RuntimeNote>& notes, int repeat, double interval, double alpha_decay) {
    if (repeat <= 1 || interval <= 1e-9) {
        return;
    }
    std::vector<RuntimeNote> out;
    out.reserve(notes.size() * static_cast<std::size_t>(repeat));
    for (const auto& n : notes) {
        out.push_back(n);
        if (n.fake) {
            continue;
        }
        for (int i = 1; i < repeat; ++i) {
            RuntimeNote dup = n;
            dup.t_hit += interval * static_cast<double>(i);
            if (dup.kind == NoteKind::Hold) {
                dup.hold_end += interval * static_cast<double>(i);
            } else {
                dup.hold_end = dup.t_hit;
            }
            const double alpha_mul = std::pow(alpha_decay, static_cast<double>(i));
            dup.alpha01 = clamp01(dup.alpha01 * alpha_mul);
            out.push_back(dup);
        }
    }
    notes.swap(out);
}

void apply_compress_zip(std::vector<RuntimeNote>& notes, int count) {
    if (count <= 1 || notes.empty()) {
        return;
    }
    const int safe_count = std::max(1, count);
    std::vector<RuntimeNote> out;
    out.reserve(notes.size() * static_cast<std::size_t>(safe_count));
    for (const auto& n : notes) {
        out.push_back(n);
        for (int i = 1; i < safe_count; ++i) {
            RuntimeNote dup = n;
            out.push_back(dup);
        }
    }
    notes.swap(out);
}

void apply_attach(std::vector<RuntimeNote>& notes, const ModConfig& mods) {
    if (!mods.attach_enable || notes.empty()) {
        return;
    }

    const int lane_count = std::max(1, mods.lane_count);
    const CompiledNoteFilter filter = compile_note_filter(mods.attach_filter);
    std::vector<RuntimeNote> out;
    out.reserve(notes.size() * 2);
    for (const auto& n : notes) {
        out.push_back(n);
        if (n.fake || !filter.matches(n)) {
            continue;
        }

        RuntimeNote attached = n;
        attached.kind = mods.attach_kind;
        attached.lane = std::clamp(n.lane + mods.attach_lane_offset, 0, lane_count - 1);
        attached.t_hit = n.t_hit + mods.attach_time_offset_sec;
        if (attached.kind == NoteKind::Hold) {
            attached.hold_end = n.hold_end + mods.attach_time_offset_sec;
        } else {
            attached.hold_end = attached.t_hit;
        }
        if (mods.attach_has_side) {
            apply_side(attached, mods.attach_side_mode);
        }
        out.push_back(attached);
    }
    notes.swap(out);
}

void apply_note_rules(std::vector<RuntimeNote>& notes, const ModConfig& mods) {
    for (const auto& rule : mods.note_rules) {
        const CompiledNoteFilter filter = compile_note_filter(rule.filter);
        for (auto& n : notes) {
            if (!rule.apply_to_hold && n.kind == NoteKind::Hold) {
                continue;
            }
            if (!filter.matches(n)) {
                continue;
            }
            apply_note_set(n, rule.set);
        }
    }

    if (!mods.note_overrides_enable) {
        return;
    }
    for (auto& n : notes) {
        if (!mods.note_overrides_apply_to_hold && n.kind == NoteKind::Hold) {
            continue;
        }
        apply_note_set(n, mods.note_overrides_set);
    }
}

void apply_hold_convert(std::vector<RuntimeNote>& notes) {
    constexpr double kIntervalSec = 0.1;
    constexpr bool kIncludeEnd = true;
    constexpr bool kTapHead = true;

    std::vector<RuntimeNote> out;
    out.reserve(notes.size() * 2);
    for (const auto& n : notes) {
        if (n.kind != NoteKind::Hold || n.hold_end <= n.t_hit + 1e-9) {
            out.push_back(n);
            continue;
        }

        if (kTapHead) {
            RuntimeNote tap = n;
            tap.kind = NoteKind::Tap;
            tap.hold_end = tap.t_hit;
            out.push_back(tap);
        }

        double t = n.t_hit + kIntervalSec;
        double last_drag_t = -1e18;
        while (t < n.hold_end - 1e-9) {
            RuntimeNote drag = n;
            drag.kind = NoteKind::Drag;
            drag.t_hit = t;
            drag.hold_end = t;
            out.push_back(drag);
            last_drag_t = t;
            t += kIntervalSec;
        }

        if (kIncludeEnd) {
            if (last_drag_t < -1e17 || std::abs(last_drag_t - n.hold_end) > kIntervalSec * 0.5) {
                RuntimeNote drag = n;
                drag.kind = NoteKind::Drag;
                drag.t_hit = n.hold_end;
                drag.hold_end = n.hold_end;
                out.push_back(drag);
            }
        }
    }
    notes.swap(out);
}

}  // namespace

void apply_mods(ChartData& chart, const ModConfig& mods) {
    auto& notes = chart.notes;

    if (mods.full_blue) {
        apply_full_blue(notes, mods.full_blue_convert_non_hold_to_tap);
    }
    if (mods.hold_convert_tap) {
        apply_hold_convert(notes);
    }

    apply_transpose(notes, mods.transpose_sec);
    apply_stretch(notes, mods.stretch_factor, mods.stretch_anchor_sec);

    if (mods.reverse_time) {
        apply_reverse(notes);
    }
    if (mods.quantize) {
        apply_quantize(notes, mods.quantize_step_sec);
    }
    if (mods.mirror) {
        apply_mirror(notes, mods.lane_count);
    }
    apply_lane_scale(notes, mods.lane_count, mods.lane_scale, mods.lane_scale_center);
    if (mods.wave) {
        apply_wave(notes, mods.lane_count, mods.wave_amplitude_lane, mods.wave_period_sec);
    }
    if (mods.randomize_lane) {
        apply_randomize(notes, mods.lane_count, mods.random_seed);
    }
    apply_fade(notes, mods);

    apply_thin_out(notes, mods.thin_out_every);

    if (mods.stutter) {
        apply_stutter(notes, mods.stutter_repeat, mods.stutter_interval_sec, mods.stutter_alpha_decay);
    }
    apply_compress_zip(notes, mods.compress_zip_count);
    apply_attach(notes, mods);
    apply_note_rules(notes, mods);

    std::sort(notes.begin(), notes.end(), [](const RuntimeNote& a, const RuntimeNote& b) {
        return a.t_hit < b.t_hit;
    });

    for (std::size_t i = 0; i < notes.size(); ++i) {
        notes[i].id = static_cast<int>(i + 1);
    }
}

}  // namespace phic
