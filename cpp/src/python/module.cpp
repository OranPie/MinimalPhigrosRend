#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "phigros/api/python_api.hpp"
#include "phigros/config/render_config.hpp"
#include "phigros/hud/hud.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

namespace py = pybind11;
using namespace pybind11::literals;

using namespace phigros;

namespace {

nlohmann::json py_to_json(const py::handle& obj) {
    if (obj.is_none()) return nullptr;
    if (py::isinstance<py::bool_>(obj)) return obj.cast<bool>();
    if (py::isinstance<py::int_>(obj)) return obj.cast<long long>();
    if (py::isinstance<py::float_>(obj)) return obj.cast<double>();
    if (py::isinstance<py::str>(obj)) return obj.cast<std::string>();
    if (py::isinstance<py::dict>(obj)) {
        nlohmann::json out = nlohmann::json::object();
        for (const auto& item : obj.cast<py::dict>()) {
            out[py::cast<std::string>(item.first)] = py_to_json(item.second);
        }
        return out;
    }
    if (py::isinstance<py::list>(obj) || py::isinstance<py::tuple>(obj)) {
        nlohmann::json out = nlohmann::json::array();
        for (const auto& item : py::reinterpret_borrow<py::iterable>(obj))
            out.push_back(py_to_json(item));
        return out;
    }
    throw py::type_error("Unsupported value in Python->JSON conversion");
}

py::object json_to_py(const nlohmann::json& value) {
    if (value.is_null()) return py::none();
    if (value.is_boolean()) return py::bool_(value.get<bool>());
    if (value.is_number_integer()) return py::int_(value.get<long long>());
    if (value.is_number_unsigned()) return py::int_(value.get<unsigned long long>());
    if (value.is_number_float()) return py::float_(value.get<double>());
    if (value.is_string()) return py::str(value.get<std::string>());
    if (value.is_array()) {
        py::list out;
        for (const auto& item : value) out.append(json_to_py(item));
        return std::move(out);
    }
    py::dict out;
    for (auto it = value.begin(); it != value.end(); ++it)
        out[py::str(it.key())] = json_to_py(it.value());
    return std::move(out);
}

// ── to_dict helpers ───────────────────────────────────────────────────────────

py::dict chart_assets_to_dict(const chart::ChartAssets& assets) {
    py::dict d;
    d["music_path"] = assets.music_path;
    d["illustration_path"] = assets.illustration_path;
    d["extra_files"] = assets.extra_files;
    return d;
}

py::dict chart_entry_to_dict(const chart::ChartEntry& entry) {
    py::dict d;
    d["name"] = entry.name;
    d["difficulty"] = entry.difficulty;
    d["chart_path"] = entry.chart_path;
    d["source_type"] = entry.source_type;
    d["assets"] = chart_assets_to_dict(entry.assets);
    return d;
}

py::dict score_result_to_dict(const engine::ScoreResult& score) {
    py::dict d;
    d["score"] = score.score;
    d["acc_ratio"] = score.acc_ratio;
    d["combo_ratio"] = score.combo_ratio;
    return d;
}

py::dict hit_event_to_dict(const engine::SimHitEvent& event) {
    py::dict d;
    d["note_idx"] = event.note_idx;
    d["judge_t"] = event.judge_t;
    d["delta_ms"] = event.delta_ms;
    d["x"] = event.x;
    d["y"] = event.y;
    d["grade"] = event.grade;
    return d;
}

py::dict hud_to_dict(const hud::HudState& hud_state) {
    py::dict d;
    d["score"] = hud_state.score;
    d["accuracy"] = hud_state.accuracy;
    d["combo"] = hud_state.combo;
    d["max_combo"] = hud_state.max_combo;
    d["progress"] = hud_state.progress;
    d["title"] = hud_state.title;
    d["subtitle"] = hud_state.subtitle;
    d["score_text"] = hud_state.score_text;
    d["acc_text"] = hud_state.acc_text;
    d["show_combo"] = hud_state.show_combo;
    d["total_notes"] = hud_state.total_notes;
    return d;
}

py::dict line_snapshot_to_dict(const render::LineSnapshot& line) {
    py::dict d;
    d["lid"] = line.lid;
    d["x"] = line.x;
    d["y"] = line.y;
    d["rot"] = line.rot;
    d["cos_rot"] = line.cos_rot;
    d["sin_rot"] = line.sin_rot;
    d["alpha01"] = line.alpha01;
    d["scroll"] = line.scroll;
    d["color"] = py::make_tuple(line.color.r, line.color.g, line.color.b);
    d["incline"] = line.incline;
    d["is_cover"] = line.is_cover;
    d["z_order"] = line.z_order;
    d["scale_x"] = line.scale_x;
    d["scale_y"] = line.scale_y;
    d["texture_path"] = line.texture_path ? py::cast(*line.texture_path) : py::cast(std::string{});
    d["text"] = line.text;
    return d;
}

py::dict note_snapshot_to_dict(const render::NoteSnapshot& note) {
    py::dict d;
    d["nid"] = note.nid;
    d["kind"] = note.kind;
    d["wx"] = note.wx;
    d["wy"] = note.wy;
    d["wx_tail"] = note.wx_tail;
    d["wy_tail"] = note.wy_tail;
    d["alpha"] = note.alpha;
    d["line_rot"] = note.line_rot;
    d["size_px"] = note.size_px;
    d["color"] = py::make_tuple(note.color.r, note.color.g, note.color.b);
    d["is_hold"] = note.is_hold;
    d["judged"] = note.judged;
    d["miss"] = note.miss;
    d["mh"] = note.mh;
    d["holding"] = note.holding;
    d["draw_hold_head"] = note.draw_hold_head;
    d["skew"] = note.skew;
    return d;
}

py::dict raw_line_to_dict(const Line& line) {
    py::dict d;
    d["lid"] = line.lid;
    d["texture_path"] = line.texture_path;
    d["anchor"] = py::make_tuple(line.anchor.first, line.anchor.second);
    d["is_gif"] = line.is_gif;
    d["father"] = line.father;
    d["rotate_with_father"] = line.rotate_with_father;
    d["name"] = line.name;
    d["attach_ui"] = line.attach_ui;
    d["z_order"] = line.z_order;
    d["is_cover"] = line.is_cover;
    d["color_rgb"] = py::make_tuple(line.color_rgb.r, line.color_rgb.g, line.color_rgb.b);
    d["alpha_ctrl_count"] = line.alpha_ctrl.size();
    d["pos_ctrl_count"] = line.pos_ctrl.size();
    d["size_ctrl_count"] = line.size_ctrl.size();
    d["y_ctrl_count"] = line.y_ctrl.size();
    d["skew_ctrl_count"] = line.skew_ctrl.size();
    return d;
}

py::dict frame_snapshot_to_dict(const render::FrameSnapshot& frame) {
    py::dict d;
    py::list lines;
    for (const auto& line : frame.lines) lines.append(line_snapshot_to_dict(line));
    py::list notes;
    for (const auto& note : frame.notes) notes.append(note_snapshot_to_dict(note));
    d["t"] = frame.t;
    d["lines"] = lines;
    d["notes"] = notes;
    d["hud"] = hud_to_dict(frame.hud);
    return d;
}

py::dict autoplay_result_to_dict(const api::AutoplayResult& result) {
    py::dict d;
    d["score"] = score_result_to_dict(result.score);
    d["judged_count"] = result.judged_count;
    d["playable_count"] = result.playable_count;
    d["max_combo"] = result.max_combo;
    py::list hit_events;
    for (const auto& event : result.hit_events) hit_events.append(hit_event_to_dict(event));
    d["hit_events"] = hit_events;
    return d;
}

config::RenderConfig config_from_python_dict(const py::dict& data) {
    return config::load_config_json(py_to_json(data));
}

py::object config_to_python_dict(const config::RenderConfig& cfg) {
    return json_to_py(config::config_to_json(cfg));
}

api::PreparedChart compiled_to_prepared(const chart::CompiledChartData& compiled,
                                        const config::RenderConfig& cfg) {
    api::PreparedChart prepared;
    prepared.chart = compiled.to_chart_data();
    prepared.chart.finalize();
    prepared.chart.build_early_notes_index();
    prepared.chart.build_notes_by_enter_index();
    prepared.config = cfg;
    prepared.scoring_notes = prepared.chart.playable_count;
    prepared.simulation_end = prepared.chart.chart_end_t;
    return prepared;
}

// Grade string for a completed autoplay (phi/v/fc/at/a/b/c/f).
// phi  = 1 000 000 (all-perfect)
// v    = full combo + not all-perfect  (max_combo == total)
// fc   = full combo, score < 960 000
// at   = score >= 960 000 (no full combo)
// a    = score >= 920 000
// b    = score >= 880 000
// c    = score >= 820 000
// f    = below c
std::string autoplay_grade(const api::AutoplayResult& r) {
    if (r.score.score >= 1000000) return "phi";
    if (r.max_combo == r.playable_count) {
        return (r.score.score >= 960000) ? "v" : "fc";
    }
    if (r.score.score >= 960000) return "at";
    if (r.score.score >= 920000) return "a";
    if (r.score.score >= 880000) return "b";
    if (r.score.score >= 820000) return "c";
    return "f";
}

// Full note metadata dict from a ChartData note (all public fields).
py::dict raw_note_to_dict(const Note& n) {
    py::dict d;
    d["nid"]          = n.nid;
    d["line_id"]      = n.line_id;
    d["kind"]         = n.kind;
    d["above"]        = n.above;
    d["fake"]         = n.fake;
    d["mh"]           = n.mh;
    d["t_hit"]        = n.t_hit;
    d["t_end"]        = n.t_end;
    d["t_enter"]      = n.t_enter;
    d["x_local_px"]   = n.x_local_px;
    d["y_offset_px"]  = n.y_offset_px;
    d["speed_mul"]    = n.speed_mul;
    d["size_px"]      = n.size_px;
    d["alpha01"]      = n.alpha01;
    d["scroll_hit"]   = n.scroll_hit;
    d["scroll_end"]   = n.scroll_end;
    d["tint_rgb"]     = py::make_tuple(n.tint_rgb.r, n.tint_rgb.g, n.tint_rgb.b);
    d["visible_time"] = n.visible_time;
    d["hitsound_path"] = n.hitsound_path;
    return d;
}

} // namespace

PYBIND11_MODULE(_core, m) {
    m.doc() = "Python bindings for MinimalPhigrosRend chart evaluation and processing";

    py::class_<chart::ChartAssets>(m, "ChartAssets")
        .def(py::init<>())
        .def_readonly("music_path", &chart::ChartAssets::music_path)
        .def_readonly("illustration_path", &chart::ChartAssets::illustration_path)
        .def_readonly("extra_files", &chart::ChartAssets::extra_files)
        .def("to_dict", &chart_assets_to_dict);

    py::class_<chart::ChartEntry>(m, "ChartEntry")
        .def(py::init<>())
        .def_readonly("name", &chart::ChartEntry::name)
        .def_readonly("difficulty", &chart::ChartEntry::difficulty)
        .def_readonly("chart_path", &chart::ChartEntry::chart_path)
        .def_readonly("assets", &chart::ChartEntry::assets)
        .def_readonly("source_type", &chart::ChartEntry::source_type)
        .def("to_dict", &chart_entry_to_dict);

    py::enum_<config::LineAlphaMode>(m, "LineAlphaMode")
        .value("OFF", config::LineAlphaMode::Off)
        .value("NEGATIVE_ONLY", config::LineAlphaMode::NegativeOnly)
        .value("ALWAYS", config::LineAlphaMode::Always)
        .export_values();

    py::class_<config::RenderConfig>(m, "RenderConfig")
        .def(py::init<>())
        .def_readwrite("window_w", &config::RenderConfig::window_w)
        .def_readwrite("window_h", &config::RenderConfig::window_h)
        .def_readwrite("expand_factor", &config::RenderConfig::expand_factor)
        .def_readwrite("note_scale_x", &config::RenderConfig::note_scale_x)
        .def_readwrite("note_scale_y", &config::RenderConfig::note_scale_y)
        .def_readwrite("note_flow_speed_multiplier", &config::RenderConfig::note_flow_speed_multiplier)
        .def_readwrite("note_speed_mul_affects_travel", &config::RenderConfig::note_speed_mul_affects_travel)
        .def_readwrite("note_alpha", &config::RenderConfig::note_alpha)
        .def_readwrite("font_size", &config::RenderConfig::font_size)
        .def_readwrite("font_align", &config::RenderConfig::font_align)
        .def_readwrite("overlay_transparent", &config::RenderConfig::overlay_transparent)
        .def_readwrite("line_alpha_mode", &config::RenderConfig::line_alpha_mode)
        .def_readwrite("approach", &config::RenderConfig::approach)
        .def_readwrite("chart_speed", &config::RenderConfig::chart_speed)
        .def_readwrite("playback_speed", &config::RenderConfig::playback_speed)
        .def_readwrite("no_cull", &config::RenderConfig::no_cull)
        .def_readwrite("no_cull_screen", &config::RenderConfig::no_cull_screen)
        .def_readwrite("no_cull_enter_time", &config::RenderConfig::no_cull_enter_time)
        .def_readwrite("overrender", &config::RenderConfig::overrender)
        .def_readwrite("rpe_easing_shift", &config::RenderConfig::rpe_easing_shift)
        .def("to_dict", &config_to_python_dict);

    py::class_<engine::ScoreResult>(m, "ScoreResult")
        .def_readonly("score", &engine::ScoreResult::score)
        .def_readonly("acc_ratio", &engine::ScoreResult::acc_ratio)
        .def_readonly("combo_ratio", &engine::ScoreResult::combo_ratio)
        .def("to_dict", &score_result_to_dict);

    py::class_<engine::SimHitEvent>(m, "HitEvent")
        .def_readonly("note_idx", &engine::SimHitEvent::note_idx)
        .def_readonly("judge_t", &engine::SimHitEvent::judge_t)
        .def_readonly("delta_ms", &engine::SimHitEvent::delta_ms)
        .def_readonly("x", &engine::SimHitEvent::x)
        .def_readonly("y", &engine::SimHitEvent::y)
        .def_readonly("grade", &engine::SimHitEvent::grade)
        .def("to_dict", &hit_event_to_dict);

    py::class_<hud::HudState>(m, "HudState")
        .def_readonly("score", &hud::HudState::score)
        .def_readonly("accuracy", &hud::HudState::accuracy)
        .def_readonly("combo", &hud::HudState::combo)
        .def_readonly("max_combo", &hud::HudState::max_combo)
        .def_readonly("progress", &hud::HudState::progress)
        .def_readonly("title", &hud::HudState::title)
        .def_readonly("subtitle", &hud::HudState::subtitle)
        .def_readonly("score_text", &hud::HudState::score_text)
        .def_readonly("acc_text", &hud::HudState::acc_text)
        .def_readonly("show_combo", &hud::HudState::show_combo)
        .def_readonly("total_notes", &hud::HudState::total_notes)
        .def("to_dict", &hud_to_dict);

    py::class_<render::LineSnapshot>(m, "LineSnapshot")
        .def_readonly("lid", &render::LineSnapshot::lid)
        .def_readonly("x", &render::LineSnapshot::x)
        .def_readonly("y", &render::LineSnapshot::y)
        .def_readonly("rot", &render::LineSnapshot::rot)
        .def_readonly("cos_rot", &render::LineSnapshot::cos_rot)
        .def_readonly("sin_rot", &render::LineSnapshot::sin_rot)
        .def_readonly("alpha01", &render::LineSnapshot::alpha01)
        .def_readonly("scroll", &render::LineSnapshot::scroll)
        .def_property_readonly("color", [](const render::LineSnapshot& line) {
            return py::make_tuple(line.color.r, line.color.g, line.color.b);
        })
        .def_readonly("incline", &render::LineSnapshot::incline)
        .def_readonly("is_cover", &render::LineSnapshot::is_cover)
        .def_readonly("z_order", &render::LineSnapshot::z_order)
        .def_readonly("scale_x", &render::LineSnapshot::scale_x)
        .def_readonly("scale_y", &render::LineSnapshot::scale_y)
        .def_property_readonly("texture_path", [](const render::LineSnapshot& line) {
            return line.texture_path ? *line.texture_path : std::string{};
        })
        .def_readonly("text", &render::LineSnapshot::text)
        .def("to_dict", &line_snapshot_to_dict);

    py::class_<render::NoteSnapshot>(m, "NoteSnapshot")
        .def_readonly("nid", &render::NoteSnapshot::nid)
        .def_readonly("kind", &render::NoteSnapshot::kind)
        .def_readonly("wx", &render::NoteSnapshot::wx)
        .def_readonly("wy", &render::NoteSnapshot::wy)
        .def_readonly("wx_tail", &render::NoteSnapshot::wx_tail)
        .def_readonly("wy_tail", &render::NoteSnapshot::wy_tail)
        .def_readonly("alpha", &render::NoteSnapshot::alpha)
        .def_readonly("line_rot", &render::NoteSnapshot::line_rot)
        .def_readonly("size_px", &render::NoteSnapshot::size_px)
        .def_property_readonly("color", [](const render::NoteSnapshot& note) {
            return py::make_tuple(note.color.r, note.color.g, note.color.b);
        })
        .def_readonly("is_hold", &render::NoteSnapshot::is_hold)
        .def_readonly("judged", &render::NoteSnapshot::judged)
        .def_readonly("miss", &render::NoteSnapshot::miss)
        .def_readonly("mh", &render::NoteSnapshot::mh)
        .def_readonly("holding", &render::NoteSnapshot::holding)
        .def_readonly("draw_hold_head", &render::NoteSnapshot::draw_hold_head)
        .def_readonly("skew", &render::NoteSnapshot::skew)
        .def("to_dict", &note_snapshot_to_dict);

    py::class_<render::FrameSnapshot>(m, "FrameSnapshot")
        .def_readonly("t", &render::FrameSnapshot::t)
        .def_readonly("lines", &render::FrameSnapshot::lines)
        .def_readonly("notes", &render::FrameSnapshot::notes)
        .def_readonly("hud", &render::FrameSnapshot::hud)
        .def("to_dict", &frame_snapshot_to_dict);

    py::class_<chart::CompiledChartData>(m, "CompiledChart")
        .def_property_readonly("offset", [](const chart::CompiledChartData& c) { return c.offset; })
        .def_property_readonly("chart_end_t", [](const chart::CompiledChartData& c) { return c.chart_end_t; })
        .def_property_readonly("playable_count", [](const chart::CompiledChartData& c) { return c.playable_count; })
        .def_property_readonly("sample_rate", [](const chart::CompiledChartData& c) { return c.sample_rate; })
        .def_property_readonly("sample_count", [](const chart::CompiledChartData& c) { return c.sample_count; })
        .def_property_readonly("lines_count", [](const chart::CompiledChartData& c) { return c.lines.size(); })
        .def_property_readonly("notes_count", [](const chart::CompiledChartData& c) { return c.notes.size(); })
        .def("to_chart", [](const chart::CompiledChartData& compiled, const config::RenderConfig& cfg) {
            return compiled_to_prepared(compiled, cfg);
        }, py::arg("config") = config::RenderConfig{});

    py::class_<api::PreparedChart>(m, "ChartHandle")
        .def_property_readonly("offset", [](const api::PreparedChart& chart_handle) { return chart_handle.chart.offset; })
        .def_property_readonly("chart_end", [](const api::PreparedChart& chart_handle) { return chart_handle.chart.chart_end_t; })
        .def_property_readonly("playable_count", [](const api::PreparedChart& chart_handle) { return chart_handle.chart.playable_count; })
        .def_property_readonly("notes_count", [](const api::PreparedChart& chart_handle) { return chart_handle.chart.notes.size(); })
        .def_property_readonly("lines_count", [](const api::PreparedChart& chart_handle) { return chart_handle.chart.lines.size(); })
        .def_property_readonly("config", [](const api::PreparedChart& chart_handle) { return chart_handle.config; })
        .def("build_frame", [](const api::PreparedChart& chart_handle, double t,
                               std::optional<config::RenderConfig> cfg_override) {
            return api::build_autoplay_frame(chart_handle, t, std::move(cfg_override));
        }, py::arg("t"), py::arg("config") = py::none())
        .def("frames", [](const api::PreparedChart& chart_handle, const std::vector<double>& times,
                          std::optional<config::RenderConfig> cfg_override) {
            return api::build_autoplay_frames(chart_handle, times, std::move(cfg_override));
        }, py::arg("times"), py::arg("config") = py::none())
        .def("compile", [](const api::PreparedChart& chart_handle, float sample_rate) {
            return api::compile_prepared_chart(chart_handle, sample_rate);
        }, py::arg("sample_rate") = 240.0f)
        .def("notes_data", [](const api::PreparedChart& chart_handle) {
            py::list notes;
            for (const auto& note : chart_handle.chart.notes)
                notes.append(raw_note_to_dict(note));
            return notes;
        })
        .def("lines_data", [](const api::PreparedChart& chart_handle) {
            py::list lines;
            for (const auto& line : chart_handle.chart.lines)
                lines.append(raw_line_to_dict(line));
            return lines;
        })
        .def("to_dict", [](const api::PreparedChart& chart_handle,
                           bool include_notes, bool include_lines) {
            py::dict d;
            d["offset"] = chart_handle.chart.offset;
            d["chart_end_t"] = chart_handle.chart.chart_end_t;
            d["playable_count"] = chart_handle.chart.playable_count;
            d["notes_count"] = chart_handle.chart.notes.size();
            d["lines_count"] = chart_handle.chart.lines.size();
            d["config"] = config_to_python_dict(chart_handle.config);
            if (include_notes) {
                py::list notes;
                for (const auto& note : chart_handle.chart.notes)
                    notes.append(raw_note_to_dict(note));
                d["notes"] = notes;
            }
            if (include_lines) {
                py::list lines;
                for (const auto& line : chart_handle.chart.lines)
                    lines.append(raw_line_to_dict(line));
                d["lines"] = lines;
            }
            return d;
        }, py::arg("include_notes") = false, py::arg("include_lines") = false);

    py::class_<api::FrameEvaluator>(m, "FrameEvaluator")
        .def(py::init<const api::PreparedChart&, const std::string&, int>(),
             py::arg("chart"), py::arg("mode") = "aggressive", py::arg("max_pointers") = 2,
             py::keep_alive<1, 2>())
        .def("build_frame", &api::FrameEvaluator::build_frame,
             py::arg("t"), py::arg("config") = py::none())
        .def("build_frames", &api::FrameEvaluator::build_frames,
             py::arg("times"), py::arg("config") = py::none())
        .def("reset", &api::FrameEvaluator::reset)
        .def_property_readonly("sim_t", &api::FrameEvaluator::sim_t);

    py::class_<api::AutoplayResult>(m, "AutoplayResult")
        .def_readonly("score", &api::AutoplayResult::score)
        .def_readonly("judged_count", &api::AutoplayResult::judged_count)
        .def_readonly("playable_count", &api::AutoplayResult::playable_count)
        .def_readonly("max_combo", &api::AutoplayResult::max_combo)
        .def_readonly("hit_events", &api::AutoplayResult::hit_events)
        .def("to_dict", &autoplay_result_to_dict);

    py::class_<chart::PhbcWriteOptions>(m, "PhbcWriteOptions")
        .def(py::init<>())
        .def_readwrite("compress", &chart::PhbcWriteOptions::compress)
        .def_readwrite("compress_algo", &chart::PhbcWriteOptions::compress_algo)
        .def_readwrite("encrypt", &chart::PhbcWriteOptions::encrypt)
        .def_readwrite("encrypt_algo", &chart::PhbcWriteOptions::encrypt_algo)
        .def_readwrite("password", &chart::PhbcWriteOptions::password);

    py::enum_<chart::CompressionAlgo>(m, "CompressionAlgo")
        .value("NONE", chart::CompressionAlgo::None)
        .value("ZLIB", chart::CompressionAlgo::Zlib)
        .value("LZMA", chart::CompressionAlgo::Lzma)
        .export_values();

    py::enum_<chart::EncryptionAlgo>(m, "EncryptionAlgo")
        .value("AES_256_GCM", chart::EncryptionAlgo::AES_256_GCM)
        .value("AES_256_CBC", chart::EncryptionAlgo::AES_256_CBC)
        .value("CHACHA20_POLY1305", chart::EncryptionAlgo::ChaCha20_Poly1305)
        .value("XOR", chart::EncryptionAlgo::XOR)
        .export_values();

    m.def("load_chart", [](const std::string& path,
                           int width,
                           int height,
                           int easing_shift,
                           const std::string& password) {
        config::RenderConfig cfg;
        cfg.window_w = width;
        cfg.window_h = height;
        cfg.rpe_easing_shift = easing_shift;
        return api::load_prepared_chart(path, cfg, password);
    }, py::arg("path"), py::arg("width") = 1280, py::arg("height") = 720,
       py::arg("easing_shift") = 0, py::arg("password") = "");

    m.def("scan_charts_directory", &chart::scan_charts_directory, py::arg("path"));
    m.def("load_config", &config::load_config, py::arg("path"));
    m.def("config_from_dict", &config_from_python_dict, py::arg("data"));
    m.def("compute_score", &engine::compute_score,
          py::arg("acc_sum"), py::arg("max_combo"), py::arg("total_notes"));
    m.def("compile_chart", [](const api::PreparedChart& chart_handle, float sample_rate) {
        return api::compile_prepared_chart(chart_handle, sample_rate);
    }, py::arg("chart"), py::arg("sample_rate") = 240.0f);
    m.def("write_phbc", &api::write_phbc_file,
          py::arg("compiled"), py::arg("path"),
          py::arg("options") = chart::PhbcWriteOptions{});
    m.def("read_phbc", &api::read_phbc_file,
          py::arg("path"), py::arg("password") = "");
    m.def("simulate_autoplay", &api::simulate_autoplay,
          py::arg("chart"), py::arg("fps") = 240.0,
          py::arg("mode") = "aggressive", py::arg("max_pointers") = 2,
          py::arg("duration") = py::none());
}
