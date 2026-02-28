#include "phic/core/engine.hpp"
#include "phic/core/mod_config_json.hpp"
#include "phic/core/parser.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string strip_json_comments(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    bool in_string = false;
    bool escape = false;
    for (std::size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (in_string) {
            out.push_back(c);
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
            out.push_back(c);
            continue;
        }
        if (c == '/' && i + 1 < in.size()) {
            if (in[i + 1] == '/') {
                i += 2;
                while (i < in.size() && in[i] != '\n') {
                    ++i;
                }
                if (i < in.size()) {
                    out.push_back('\n');
                }
                continue;
            }
            if (in[i + 1] == '*') {
                i += 2;
                while (i + 1 < in.size() && !(in[i] == '*' && in[i + 1] == '/')) {
                    ++i;
                }
                if (i + 1 < in.size()) {
                    ++i;
                }
                continue;
            }
        }
        out.push_back(c);
    }
    return out;
}

std::string strip_trailing_commas(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    bool in_string = false;
    bool escape = false;
    for (std::size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (in_string) {
            out.push_back(c);
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
            out.push_back(c);
            continue;
        }
        if (c == ',') {
            std::size_t j = i + 1;
            while (j < in.size() && std::isspace(static_cast<unsigned char>(in[j]))) {
                ++j;
            }
            if (j < in.size() && (in[j] == '}' || in[j] == ']')) {
                continue;
            }
        }
        out.push_back(c);
    }
    return out;
}

nlohmann::json parse_json_or_jsonc(const std::string& text) {
    try {
        return nlohmann::json::parse(text);
    } catch (...) {
        return nlohmann::json::parse(strip_trailing_commas(strip_json_comments(text)));
    }
}

bool to_bool(const nlohmann::json& v, bool fallback) {
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number_integer()) return v.get<int>() != 0;
    if (v.is_string()) {
        std::string s = v.get<std::string>();
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (s == "1" || s == "true" || s == "yes" || s == "on") return true;
        if (s == "0" || s == "false" || s == "no" || s == "off") return false;
    }
    return fallback;
}

double to_num(const nlohmann::json& v, double fallback) {
    if (v.is_number()) return v.get<double>();
    if (v.is_string()) {
        try {
            return std::stod(v.get<std::string>());
        } catch (...) {
        }
    }
    return fallback;
}

int to_int(const nlohmann::json& v, int fallback) {
    if (v.is_number_integer()) return v.get<int>();
    if (v.is_number()) return static_cast<int>(std::lround(v.get<double>()));
    if (v.is_string()) {
        try {
            return std::stoi(v.get<std::string>());
        } catch (...) {
        }
    }
    return fallback;
}

bool parse_bool(const nlohmann::json& j, const char* key, bool fallback) {
    const auto it = j.find(key);
    if (it == j.end()) {
        return fallback;
    }
    return to_bool(*it, fallback);
}

double parse_num(const nlohmann::json& j, const char* key, double fallback) {
    const auto it = j.find(key);
    if (it == j.end()) {
        return fallback;
    }
    return to_num(*it, fallback);
}

int parse_int(const nlohmann::json& j, const char* key, int fallback) {
    const auto it = j.find(key);
    if (it == j.end()) {
        return fallback;
    }
    return to_int(*it, fallback);
}


void apply_mods_json(phic::ModConfig& mods, const nlohmann::json& m) {
    phic::merge_mod_config_from_json(mods, m);
}


}  // namespace

int main(int argc, char** argv) {
    std::string input;
    std::string format = "official";
    std::string mods_file;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](std::string& out) {
            if (i + 1 < argc) {
                out = argv[++i];
            }
        };
        if (arg == "--input") next(input);
        else if (arg == "--format") next(format);
        else if (arg == "--mods-file") next(mods_file);
    }

    if (input.empty()) {
        std::cerr << "missing --input\n";
        return 2;
    }

    const std::string payload = read_file(input);
    if (payload.empty()) {
        std::cerr << "failed to read input\n";
        return 2;
    }

    phic::ModConfig mods{};
    if (!mods_file.empty()) {
        const std::string text = read_file(mods_file);
        if (!text.empty()) {
            try {
                const auto root = parse_json_or_jsonc(text);
                if (root.is_object()) {
                    apply_mods_json(mods, root);
                }
            } catch (...) {
                std::cerr << "failed to parse mods file\n";
                return 2;
            }
        }
    }

    const auto parsed = phic::parse_chart_bytes(payload, format);
    if (!parsed.ok) {
        std::cerr << "parse failed: " << parsed.error << "\n";
        return 3;
    }

    phic::RenderConfig cfg{};
    cfg.mods = mods;
    phic::Engine engine(cfg);
    engine.load_chart(parsed.chart);

    const auto& notes = engine.chart().notes;
    nlohmann::json out;
    out["ok"] = true;
    out["count"] = notes.size();
    out["notes"] = nlohmann::json::array();

    int kind_tap = 0;
    int kind_drag = 0;
    int kind_hold = 0;
    int kind_flick = 0;
    for (const auto& n : notes) {
        if (n.kind == phic::NoteKind::Tap) ++kind_tap;
        else if (n.kind == phic::NoteKind::Drag) ++kind_drag;
        else if (n.kind == phic::NoteKind::Hold) ++kind_hold;
        else if (n.kind == phic::NoteKind::Flick) ++kind_flick;

        nlohmann::json jn;
        jn["id"] = n.id;
        jn["line_id"] = n.line_id;
        jn["lane"] = n.lane;
        jn["kind"] = static_cast<int>(n.kind);
        jn["above"] = n.above;
        jn["fake"] = n.fake;
        jn["t_hit"] = n.t_hit;
        jn["hold_end"] = n.hold_end;
        jn["alpha"] = n.alpha01;
        jn["speed_mul"] = n.speed_mul;
        out["notes"].push_back(jn);
    }

    out["kind_counts"] = {
        {"tap", kind_tap},
        {"drag", kind_drag},
        {"hold", kind_hold},
        {"flick", kind_flick},
    };

    std::cout << out.dump() << "\n";
    return 0;
}
