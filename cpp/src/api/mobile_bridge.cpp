#include "phigros/api/mobile_bridge.h"

// miniz.h must come before miniz_zip.h (defines mz_alloc_func, tinfl_decompressor, etc.)
#include "miniz.h"
#include "miniz_zip.h"
#include "phigros/api/python_api.hpp"
#include "phigros/config/render_config.hpp"
#include "phigros/engine/effects.hpp"
#include "phigros/engine/hold_logic.hpp"
#include "phigros/engine/judge.hpp"
#include "phigros/engine/judge_input.hpp"
#include "phigros/engine/manual_judge.hpp"
#include "phigros/engine/scriptplay.hpp"
#include "phigros/render/renderer.hpp"

#include <algorithm>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace phigros::api {
namespace {

config::RenderConfig make_render_config(const phigros_mobile_config* input) {
    config::RenderConfig cfg;
    if (!input) return cfg;

    if (input->window_width > 0) cfg.window_w = input->window_width;
    if (input->window_height > 0) cfg.window_h = input->window_height;
    if (input->chart_speed > 0.0) cfg.chart_speed = input->chart_speed;
    if (input->note_scale > 0.0) cfg.note_scale_x = input->note_scale;
    cfg.audio_offset_ms = input->audio_offset_ms;
    if (input->respack_path && input->respack_path[0] != '\0')
        cfg.respack_path = input->respack_path;
    cfg.backend = "mobile_bridge";
    cfg.autoplay = true;
    cfg.gameplay_mode = "autoplay";
    return cfg;
}

struct PendingTouch {
    int pointer_id;
    phigros_mobile_touch_phase phase;
    float x, y;
    int64_t timestamp_ms;
    bool flick = false;
};

struct PointerState {
    float x = 0, y = 0;
    int64_t ts_ms = 0;
    float vx = 0, vy = 0;     /* velocity in pixels/second */
    bool flick_pending = false; /* velocity crossed threshold on last MOVED */

    static constexpr float FLICK_SPEED_THRESHOLD = 800.0f; /* px/s */
};


class MobileBridge {
public:
    explicit MobileBridge(const phigros_mobile_config* input)
        : config_(make_render_config(input)) {}

    int load_chart(const char* path, const char* password) {
        std::lock_guard<std::mutex> lock(mu_);
        try {
            prepared_ = api::load_prepared_chart(path ? path : "",
                                                 config_,
                                                 password ? password : "");
            evaluator_ = std::make_unique<api::FrameEvaluator>(*prepared_, "aggressive", 4);
            current_time_ = prepared_->chart.offset;
            chart_ended_  = false;
            paused_       = 1;
            pending_touches_.clear();
            pointer_down_.clear();
            init_play_state_locked();
            rebuild_snapshot_locked();
            last_error_.clear();
            return 0;
        } catch (const std::exception& e) {
            prepared_.reset();
            evaluator_.reset();
            set_error_locked(e.what());
            return -1;
        }
    }

    int attach_surface(void* surface, int width, int height) {
        std::lock_guard<std::mutex> lock(mu_);
        native_surface_ = surface;
        if (width  > 0) config_.window_w = width;
        if (height > 0) config_.window_h = height;
        try {
            rebuild_snapshot_locked();
            last_error_.clear();
            return 0;
        } catch (const std::exception& e) {
            set_error_locked(e.what());
            return -1;
        }
    }

    int set_time(double time_seconds) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!prepared_) { set_error_locked("No chart loaded"); return -1; }
        current_time_ = std::clamp(time_seconds,
                                   prepared_->chart.offset,
                                   prepared_->simulation_end);
        chart_ended_ = false;
        try {
            rebuild_snapshot_locked();
            last_error_.clear();
            return 0;
        } catch (const std::exception& e) {
            set_error_locked(e.what());
            return -1;
        }
    }

    int set_paused(int paused_value) {
        std::lock_guard<std::mutex> lock(mu_);
        paused_ = paused_value ? 1 : 0;
        last_error_.clear();
        return 0;
    }

    int restart() {
        std::lock_guard<std::mutex> lock(mu_);
        if (!prepared_) { set_error_locked("No chart loaded"); return -1; }
        current_time_ = prepared_->chart.offset;
        chart_ended_  = false;
        pending_touches_.clear();
        pointer_down_.clear();
        active_pointers_.clear();
        if (evaluator_) evaluator_->reset();
        init_play_state_locked();
        try {
            rebuild_snapshot_locked();
            last_error_.clear();
            return 0;
        } catch (const std::exception& e) {
            set_error_locked(e.what());
            return -1;
        }
    }

    int set_play_mode(const char* mode) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!mode) { set_error_locked("Null mode"); return -1; }
        play_mode_ = mode;
        config_.gameplay_mode = mode;
        config_.autoplay = (play_mode_ == "autoplay");
        last_error_.clear();
        return 0;
    }

    int load_script(const char* path) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!path) { set_error_locked("Null script path"); return -1; }
        script_path_ = path;
        if (prepared_) {
            try {
                scriptplay_.load(script_path_,
                                 prepared_->chart,
                                 engine::Judge::BAD * 0.5);
                last_error_.clear();
                return 0;
            } catch (const std::exception& e) {
                set_error_locked(e.what());
                return -1;
            }
        }
        last_error_.clear();
        return 0;
    }

    int on_touch(int pointer_id,
                 phigros_mobile_touch_phase phase,
                 float x, float y,
                 int64_t timestamp_ms) {
        std::lock_guard<std::mutex> lock(mu_);
        bool flick = false;
        auto it = pointer_down_.find(pointer_id);
        const bool is_begin = (phase == PHIGROS_MOBILE_TOUCH_BEGAN);
        const bool is_end = (phase == PHIGROS_MOBILE_TOUCH_ENDED ||
                             phase == PHIGROS_MOBILE_TOUCH_CANCELLED);
        if (it != pointer_down_.end()) {
            PointerState& ps = it->second;
            int64_t dt_ms = timestamp_ms - ps.ts_ms;
            if (dt_ms > 0) {
                float dt_s = static_cast<float>(dt_ms) / 1000.0f;
                ps.vx = (x - ps.x) / dt_s;
                ps.vy = (y - ps.y) / dt_s;
                float speed2 = ps.vx * ps.vx + ps.vy * ps.vy;
                if (speed2 >= PointerState::FLICK_SPEED_THRESHOLD *
                              PointerState::FLICK_SPEED_THRESHOLD) {
                    ps.flick_pending = true;
                }
            }
            flick = ps.flick_pending && (phase == PHIGROS_MOBILE_TOUCH_MOVED || is_end);
            ps.x = x; ps.y = y; ps.ts_ms = timestamp_ms;
            if (is_end) ps.flick_pending = false;
        } else if (is_begin) {
            PointerState ps;
            ps.x = x; ps.y = y; ps.ts_ms = timestamp_ms;
            pointer_down_[pointer_id] = ps;
        }
        /* Maintain immediate active-pointer set for get_state() */
        if (is_end) active_pointers_.erase(pointer_id);
        else        active_pointers_.insert(pointer_id);

        pending_touches_.push_back({pointer_id, phase, x, y, timestamp_ms, flick});
        last_error_.clear();
        return 0;
    }

    int tick(double dt_seconds) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!prepared_) { set_error_locked("No chart loaded"); return -1; }
        try {
            if (paused_ == 0 && !chart_ended_) {
                current_time_ += dt_seconds;
                if (current_time_ >= prepared_->simulation_end) {
                    current_time_ = prepared_->simulation_end;
                    chart_ended_  = true;
                }
            }
            if (play_mode_ == "autoplay") {
                last_frame_ = evaluator_->build_frame(current_time_, config_);
            } else {
                tick_play_locked(dt_seconds);
            }
            sync_hud_counters_locked();
            last_error_.clear();
            return 0;
        } catch (const std::exception& e) {
            set_error_locked(e.what());
            return -1;
        }
    }

    int get_frame(phigros_frame_data* out) {
        if (!out) return -1;
        std::lock_guard<std::mutex> lock(mu_);
        int lc = std::min(static_cast<int>(last_frame_.lines.size()), PHIGROS_MAX_LINES);
        for (int i = 0; i < lc; ++i) {
            const auto& s = last_frame_.lines[i];
            auto& d = out->lines[i];
            d.lid = s.lid;
            d.x = static_cast<float>(s.x);
            d.y = static_cast<float>(s.y);
            d.rot = static_cast<float>(s.rot);
            d.alpha = static_cast<float>(s.alpha01);
            d.scale_x = s.scale_x;
            d.scale_y = s.scale_y;
            d.r = s.color.r; d.g = s.color.g; d.b = s.color.b;
            d.incline = s.incline;
            d.is_cover = s.is_cover ? 1 : 0;
            d.z_order = s.z_order;
        }
        out->line_count = lc;
        int nc = std::min(static_cast<int>(last_frame_.notes.size()), PHIGROS_MAX_NOTES);
        for (int i = 0; i < nc; ++i) {
            const auto& s = last_frame_.notes[i];
            auto& d = out->notes[i];
            d.nid = s.nid; d.kind = s.kind;
            d.wx = static_cast<float>(s.wx);
            d.wy = static_cast<float>(s.wy);
            d.wx2 = static_cast<float>(s.wx_tail);
            d.wy2 = static_cast<float>(s.wy_tail);
            d.alpha = static_cast<float>(s.alpha);
            d.line_rot = static_cast<float>(s.line_rot);
            d.size_px = static_cast<float>(s.size_px);
            d.skew = s.skew;
            d.r = s.color.r; d.g = s.color.g; d.b = s.color.b;
            d.judged = s.judged ? 1 : 0;
            d.miss = s.miss ? 1 : 0;
            d.holding = s.holding ? 1 : 0;
            d.is_hold = s.is_hold ? 1 : 0;
            d.draw_hold_head = s.draw_hold_head ? 1 : 0;
            d.hold_hit_failed = s.hold_hit_failed ? 1 : 0;
            d.is_mh = s.mh ? 1 : 0;
        }
        out->note_count = nc;
        const auto& h = last_frame_.hud;
        out->hud.combo = h.combo;
        out->hud.max_combo = h.max_combo;
        out->hud.score = h.score;
        out->hud.total_notes = h.total_notes;
        out->hud.accuracy = static_cast<float>(h.accuracy);
        out->hud.progress = static_cast<float>(h.progress);
        out->hud.show_combo = h.show_combo ? 1 : 0;
        std::strncpy(out->hud.score_text, h.score_text.c_str(), 15);
        out->hud.score_text[15] = '\0';
        std::strncpy(out->hud.acc_text, h.acc_text.c_str(), 15);
        out->hud.acc_text[15] = '\0';
        out->chart_time = current_time_;
        out->chart_ended = chart_ended_ ? 1 : 0;

        /* Fill hit-flash effects */
        int ec = 0;
        for (const auto& fx : fx_.flashes) {
            if (ec >= PHIGROS_MAX_EFFECTS) break;
            if (!fx.alive(current_time_)) continue;
            auto& e = out->effects[ec++];
            e.x = static_cast<float>(fx.x);
            e.y = static_cast<float>(fx.y);
            e.t0 = static_cast<float>(fx.t0);
            e.radius_start = fx.radius_start;
            e.radius_end   = fx.radius_end;
            e.r = fx.color.r; e.g = fx.color.g; e.b = fx.color.b;
            e.is_good = 0;
        }
        out->effect_count = ec;
        return 0;
    }

    int get_state(phigros_mobile_state* out) {
        if (!out) return -1;
        std::lock_guard<std::mutex> lock(mu_);
        out->chart_loaded = prepared_ ? 1 : 0;
        out->paused = paused_;
        out->window_width = config_.window_w;
        out->window_height = config_.window_h;
        out->line_count = prepared_ ? static_cast<int>(prepared_->chart.lines.size()) : 0;
        out->total_notes = prepared_ ? static_cast<int>(prepared_->chart.notes.size()) : 0;
        out->playable_notes = prepared_ ? prepared_->scoring_notes : 0;
        out->visible_notes = static_cast<int>(last_visible_notes_);
        out->judged_notes = last_judged_notes_;
        out->max_combo = last_max_combo_;
        out->active_touch_count = static_cast<int>(active_pointers_.size());
        out->chart_time = current_time_;
        out->chart_offset = prepared_ ? prepared_->chart.offset : 0.0;
        out->chart_duration = prepared_
            ? (prepared_->simulation_end - prepared_->chart.offset) : 0.0;
        return 0;
    }

    int copy_last_error(char* buffer, size_t buffer_size) const {
        if (!buffer || buffer_size == 0) return -1;
        std::lock_guard<std::mutex> lock(mu_);
        const size_t count = std::min(buffer_size - 1, last_error_.size());
        std::memcpy(buffer, last_error_.data(), count);
        buffer[count] = '\0';
        return static_cast<int>(count);
    }

private:
    void set_error_locked(const std::string& msg) { last_error_ = msg; }

    /* Initialize or reset non-autoplay play state after load/restart */
    void init_play_state_locked() {
        if (!prepared_) return;
        const auto& notes = prepared_->chart.notes;
        states_play_.resize(notes.size());
        for (size_t i = 0; i < notes.size(); ++i)
            states_play_[i] = NoteState{&notes[i]};
        judge_play_ = engine::Judge{};
        manual_judge_ = engine::ManualJudge{};
        fx_ = engine::EffectManager{};
        pointer_down_.clear();
        active_pointers_.clear();
        if (play_mode_ == "scriptplay" && !script_path_.empty()) {
            try {
                scriptplay_.load(script_path_, prepared_->chart, engine::Judge::BAD * 0.5);
            } catch (...) { /* errors surfaced on next tick */ }
        } else {
            scriptplay_ = engine::ScriptPlayPlayer{};
        }
    }

    /* Advance manual/scriptplay simulation and rebuild last_frame_ */
    void tick_play_locked(double /*dt*/) {
        if (!prepared_ || states_play_.empty()) return;
        const auto& chart = prepared_->chart;
        const int W = config_.window_w;
        const int H = config_.window_h;

        /* 1. Build current frame for note world positions */
        last_frame_ = render::build_frame(
            current_time_, chart, states_play_, judge_play_, config_);

        /* 2. Process inputs */
        if (play_mode_ == "scriptplay") {
            scriptplay_.tick(current_time_, chart.notes,
                             states_play_, judge_play_);
        } else {
            /* Build JudgeInputFrame from pending touches */
            engine::JudgeInputFrame input;
            for (auto& pt : pending_touches_) {
                bool is_end = (pt.phase == PHIGROS_MOBILE_TOUCH_ENDED ||
                               pt.phase == PHIGROS_MOBILE_TOUCH_CANCELLED);
                auto it = pointer_down_.find(pt.pointer_id);
                bool was_down = (it != pointer_down_.end());
                engine::JudgeAction a;
                a.id = static_cast<int64_t>(pt.pointer_id);
                a.has_position = true;
                a.x = pt.x; a.y = pt.y;
                a.press   = (pt.phase == PHIGROS_MOBILE_TOUCH_BEGAN) || (!was_down && !is_end);
                a.release = is_end;
                a.down    = !is_end;
                a.flick   = pt.flick;
                input.add(a);
                if (is_end) {
                    pointer_down_.erase(pt.pointer_id);
                } else {
                    PointerState& ps = pointer_down_[pt.pointer_id];
                    ps.x = pt.x; ps.y = pt.y; ps.ts_ms = pt.timestamp_ms;
                    if (a.press) { ps.vx = 0; ps.vy = 0; }
                }
            }
            pending_touches_.clear();
            /* Prune dead flashes before processing new judgments */
            fx_.update(current_time_, current_time_ * 1000.0);
            manual_judge_.process_frame(input, last_frame_,
                                        chart.notes, states_play_,
                                        judge_play_, fx_,
                                        current_time_, W, H);
        }
        pending_touches_.clear();

        /* 3. Hold maintenance + miss detection */
        engine::hold_maintenance(states_play_, 0, current_time_,
                                 engine::Judge::BAD * 0.5, judge_play_);
        engine::hold_finalize(states_play_, 0, current_time_,
                              engine::Judge::BAD * 0.5,
                              engine::Judge::BAD, judge_play_);
        engine::detect_misses(states_play_, 0, current_time_,
                              engine::Judge::BAD, judge_play_);

        /* 4. Rebuild frame with updated judge state */
        last_frame_ = render::build_frame(
            current_time_, chart, states_play_, judge_play_, config_);
    }

    void rebuild_snapshot_locked() {
        if (!prepared_) return;
        if (play_mode_ == "autoplay" && evaluator_) {
            last_frame_ = evaluator_->build_frame(current_time_, config_);
        } else if (!states_play_.empty()) {
            last_frame_ = render::build_frame(
                current_time_, prepared_->chart,
                states_play_, judge_play_, config_);
        }
        sync_hud_counters_locked();
        (void)native_surface_;
    }

    void sync_hud_counters_locked() {
        last_visible_notes_ = last_frame_.notes.size();
        if (play_mode_ == "autoplay" && evaluator_) {
            last_judged_notes_ = evaluator_->judge().judged_cnt;
            last_max_combo_    = evaluator_->judge().max_combo;
        } else {
            last_judged_notes_ = judge_play_.judged_cnt;
            last_max_combo_    = judge_play_.max_combo;
        }
    }

    mutable std::mutex mu_;
    config::RenderConfig config_;
    std::optional<api::PreparedChart> prepared_;
    std::unique_ptr<api::FrameEvaluator> evaluator_;

    /* Play mode ("autoplay" | "manual" | "scriptplay") */
    std::string play_mode_ = "autoplay";
    std::string script_path_;

    /* State for manual / scriptplay modes */
    std::vector<NoteState>      states_play_;
    engine::Judge               judge_play_;
    engine::ManualJudge         manual_judge_;
    engine::ScriptPlayPlayer    scriptplay_;

    /* Touch input queue (consumed each tick for manual mode) */
    std::vector<PendingTouch>                     pending_touches_;
    std::unordered_map<int, PointerState>         pointer_down_;
    std::unordered_set<int>                       active_pointers_; /* immediate-update for get_state */

    /* Persistent hit-effect accumulator for manual/scriptplay modes */
    engine::EffectManager                         fx_;

    /* Cached rendered frame */
    render::FrameSnapshot last_frame_;

    std::string last_error_;
    void*  native_surface_    = nullptr;
    double current_time_      = 0.0;
    bool   chart_ended_       = false;
    size_t last_visible_notes_ = 0;
    int    last_judged_notes_  = 0;
    int    last_max_combo_     = 0;
    int    paused_             = 1;
};

} // namespace
} // namespace phigros::api

struct phigros_mobile_handle {
    explicit phigros_mobile_handle(const phigros_mobile_config* config)
        : bridge(config) {}

    phigros::api::MobileBridge bridge;
};

extern "C" {

phigros_mobile_handle* phigros_mobile_create(const phigros_mobile_config* config) {
    try {
        return new phigros_mobile_handle(config);
    } catch (...) {
        return nullptr;
    }
}

void phigros_mobile_destroy(phigros_mobile_handle* handle) {
    delete handle;
}

int phigros_mobile_load_chart(phigros_mobile_handle* handle,
                              const char* path,
                              const char* password) {
    if (!handle || !path) return -1;
    return handle->bridge.load_chart(path, password);
}

int phigros_mobile_attach_surface(phigros_mobile_handle* handle,
                                  void* native_surface,
                                  int width,
                                  int height) {
    if (!handle) return -1;
    return handle->bridge.attach_surface(native_surface, width, height);
}

int phigros_mobile_set_time(phigros_mobile_handle* handle, double time_seconds) {
    if (!handle) return -1;
    return handle->bridge.set_time(time_seconds);
}

int phigros_mobile_set_paused(phigros_mobile_handle* handle, int paused) {
    if (!handle) return -1;
    return handle->bridge.set_paused(paused);
}

int phigros_mobile_restart(phigros_mobile_handle* handle) {
    if (!handle) return -1;
    return handle->bridge.restart();
}

int phigros_mobile_on_touch(phigros_mobile_handle* handle,
                            int pointer_id,
                            phigros_mobile_touch_phase phase,
                            float x,
                            float y,
                            int64_t timestamp_ms) {
    if (!handle) return -1;
    return handle->bridge.on_touch(pointer_id, phase, x, y, timestamp_ms);
}

int phigros_mobile_set_play_mode(phigros_mobile_handle* handle, const char* mode) {
    if (!handle || !mode) return -1;
    return handle->bridge.set_play_mode(mode);
}

int phigros_mobile_load_script(phigros_mobile_handle* handle, const char* path) {
    if (!handle || !path) return -1;
    return handle->bridge.load_script(path);
}

int phigros_mobile_tick(phigros_mobile_handle* handle, double dt_seconds) {
    if (!handle) return -1;
    return handle->bridge.tick(dt_seconds);
}

int phigros_mobile_get_frame(phigros_mobile_handle* handle,
                             phigros_frame_data* out) {
    if (!handle || !out) return -1;
    return handle->bridge.get_frame(out);
}

int phigros_mobile_get_state(phigros_mobile_handle* handle,
                             phigros_mobile_state* out_state) {
    if (!handle) return -1;
    return handle->bridge.get_state(out_state);
}

int phigros_mobile_copy_last_error(const phigros_mobile_handle* handle,
                                   char* buffer,
                                   size_t buffer_size) {
    if (!handle || !buffer || buffer_size == 0) return -1;
    return handle->bridge.copy_last_error(buffer, buffer_size);
}

} // extern "C"

// ── Zip extraction (no handle needed, outside extern "C" block is fine) ────

namespace {

namespace fs = std::filesystem;

bool is_safe_zip_member_path(const fs::path& member_path) {
    if (member_path.empty() || member_path.is_absolute()) return false;
    for (const auto& part : member_path) {
        if (part == "..") return false;
    }
    return true;
}

} // namespace

extern "C"
int phigros_extract_chart_zip(const char* zip_path, const char* dest_dir) {
    if (!zip_path || !dest_dir) return -1;

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, zip_path, 0)) return -1;

    int extracted = 0;
    const int n = static_cast<int>(mz_zip_reader_get_num_files(&zip));
    for (int i = 0; i < n; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) continue;
        if (stat.m_is_directory) continue;

        std::string fname(stat.m_filename);
        // Strip any leading path components — keep only the filename
        auto slash = fname.rfind('/');
        if (slash != std::string::npos) fname = fname.substr(slash + 1);
        if (fname.empty()) continue;

        // Only extract chart files
        auto dot = fname.rfind('.');
        if (dot == std::string::npos) continue;
        std::string ext = fname.substr(dot + 1);
        for (auto& c : ext) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        if (ext != "json" && ext != "phbc") continue;

        std::string dest = std::string(dest_dir) + "/" + fname;
        if (mz_zip_reader_extract_to_file(&zip, i, dest.c_str(), 0))
            ++extracted;
    }
    mz_zip_end(&zip);
    return extracted;
}

extern "C"
int phigros_extract_zip_to_dir(const char* zip_path, const char* dest_dir) {
    if (!zip_path || !dest_dir) return -1;

    try {
        fs::create_directories(dest_dir);
    } catch (...) {
        return -1;
    }

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, zip_path, 0)) return -1;

    int extracted = 0;
    const int n = static_cast<int>(mz_zip_reader_get_num_files(&zip));
    for (int i = 0; i < n; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) continue;

        fs::path rel_path(stat.m_filename);
        if (!is_safe_zip_member_path(rel_path)) continue;

        try {
            const fs::path dest_path = fs::path(dest_dir) / rel_path;
            if (stat.m_is_directory) {
                fs::create_directories(dest_path);
                continue;
            }
            if (dest_path.has_parent_path()) {
                fs::create_directories(dest_path.parent_path());
            }
            if (mz_zip_reader_extract_to_file(&zip, i, dest_path.string().c_str(), 0))
                ++extracted;
        } catch (...) {
            mz_zip_end(&zip);
            return -1;
        }
    }
    mz_zip_end(&zip);
    return extracted;
}
