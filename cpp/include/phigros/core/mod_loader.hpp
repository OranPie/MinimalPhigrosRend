#pragma once
// mod_loader.hpp — parse .mod.json files into phigros::mods::Mod structs.
//
// .mod.json format:
//   {
//     "name": "My Mod",
//     "description": "Optional description",
//     "ops": [
//       { "type": "mirror", "center": 0.0, "flip_side": false },
//       { "type": "colorize", "mode": "gradient", "from": [255,50,50], "to": [50,50,255] },
//       { "type": "speed", "mul": 1.5 },
//       { "type": "opacity", "alpha": 0.8 },
//       { "type": "wave", "amplitude": 100, "frequency": 1.0, "phase": 0.0 },
//       { "type": "shuffle", "seed": 42, "range": 200 },
//       { "type": "note_filter", "keep": [1, 4] },
//       { "type": "note_filter", "remove": [3] },
//       { "type": "flip_timing" },
//       { "type": "scale", "x_mul": 1.2, "y_mul": 1.0 }
//     ]
//   }
//
// Note kind constants: 1=tap  2=drag  3=hold  4=flick

#include "phigros/core/mods.hpp"
#include <fstream>
#include <stdexcept>
#include <string>
#include <nlohmann/json.hpp>

namespace phigros::mods {

namespace detail {

inline math::RGB parse_rgb(const nlohmann::json& j) {
    if (j.is_array() && j.size() >= 3)
        return { j[0].get<int>(), j[1].get<int>(), j[2].get<int>() };
    if (j.is_object())
        return { j.value("r", 255), j.value("g", 255), j.value("b", 255) };
    throw std::runtime_error("mod_loader: color must be [R,G,B] array or {r,g,b} object");
}

inline ColorMode parse_color_mode(const std::string& s) {
    if (s == "constant") return ColorMode::Constant;
    if (s == "gradient") return ColorMode::Gradient;
    if (s == "by_kind")  return ColorMode::ByKind;
    if (s == "by_line")  return ColorMode::ByLine;
    if (s == "hue")      return ColorMode::Hue;
    throw std::runtime_error("mod_loader: unknown colorize mode '" + s + "'");
}

inline AnyOp parse_op(const nlohmann::json& j) {
    std::string type = j.at("type").get<std::string>();

    if (type == "mirror") {
        MirrorOp op;
        op.center    = j.value("center", 0.0);
        op.flip_side = j.value("flip_side", false);
        return op;
    }

    if (type == "colorize") {
        ColorizeOp op;
        op.mode = parse_color_mode(j.value("mode", std::string("constant")));
        if (j.contains("color"))  op.color = parse_rgb(j["color"]);
        if (j.contains("from"))   op.from  = parse_rgb(j["from"]);
        if (j.contains("to"))     op.to    = parse_rgb(j["to"]);
        if (j.contains("hue_s"))  op.hue_s = j["hue_s"].get<double>();
        if (j.contains("hue_v"))  op.hue_v = j["hue_v"].get<double>();
        if (j.contains("by_kind")) {
            for (auto& [k, v] : j["by_kind"].items())
                op.by_kind[std::stoi(k)] = parse_rgb(v);
        }
        if (j.contains("by_line")) {
            for (auto& [k, v] : j["by_line"].items())
                op.by_line[std::stoi(k)] = parse_rgb(v);
        }
        return op;
    }

    if (type == "speed") {
        SpeedOp op;
        op.mul = j.value("mul", 1.0);
        return op;
    }

    if (type == "opacity") {
        OpacityOp op;
        op.alpha = j.value("alpha", 1.0);
        return op;
    }

    if (type == "wave") {
        WaveOp op;
        op.amplitude = j.value("amplitude", 100.0);
        op.frequency = j.value("frequency", 1.0);
        op.phase     = j.value("phase", 0.0);
        return op;
    }

    if (type == "shuffle") {
        ShuffleOp op;
        op.seed  = j.value("seed",  42u);
        op.range = j.value("range", 200.0);
        return op;
    }

    if (type == "note_filter") {
        NoteFilterOp op;
        if (j.contains("keep"))
            op.keep = j["keep"].get<std::vector<int>>();
        if (j.contains("remove"))
            op.remove = j["remove"].get<std::vector<int>>();
        if (op.keep.empty() && op.remove.empty())
            throw std::runtime_error("mod_loader: note_filter requires 'keep' or 'remove'");
        return op;
    }

    if (type == "flip_timing") {
        return FlipTimingOp{};
    }

    if (type == "scale") {
        ScaleOp op;
        op.x_mul = j.value("x_mul", 1.0);
        op.y_mul = j.value("y_mul", 1.0);
        return op;
    }

    throw std::runtime_error("mod_loader: unknown op type '" + type + "'");
}

} // namespace detail

// Parse a Mod from a JSON object.
inline Mod parse_mod(const nlohmann::json& j) {
    Mod mod;
    mod.name        = j.value("name", std::string("unnamed"));
    mod.description = j.value("description", std::string(""));
    if (j.contains("ops")) {
        for (const auto& op_j : j.at("ops"))
            mod.ops.push_back(detail::parse_op(op_j));
    }
    return mod;
}

// Load a Mod from a .mod.json file path.
inline Mod load_mod(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("mod_loader: cannot open '" + path + "'");
    nlohmann::json j;
    try { j = nlohmann::json::parse(f, nullptr, true, /*allow_comments=*/true); }
    catch (const nlohmann::json::exception& e) {
        throw std::runtime_error("mod_loader: JSON parse error in '" + path + "': " + e.what());
    }
    return parse_mod(j);
}

} // namespace phigros::mods
