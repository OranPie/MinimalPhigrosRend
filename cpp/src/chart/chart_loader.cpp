#include "phigros/chart/chart_loader.hpp"
#include "phigros/chart/pbc.hpp"
#include "phigros/chart/phbc_io.hpp"
#include "phigros/core/logger.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <set>
#include <iostream>
#include <miniz.h>
#include <map>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace fs = std::filesystem;

namespace phigros::chart {

// ── Utility functions ────────────────────────────────────────────────────────

static std::string parse_level_prefix(const std::string& value);
static ChartEntry direct_entry_for_path(const std::string& path);

static std::string lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static bool has_extension(const fs::path& p, const std::set<std::string>& exts) {
    auto ext = lower_copy(p.extension().string());
    return exts.count(ext) > 0;
}

static std::string trim_copy(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n'))
        s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
        s.pop_back();
    return s;
}

static std::string unquote_copy(std::string s) {
    s = trim_copy(std::move(s));
    if (s.size() >= 2 && ((s.front() == "\""[0] && s.back() == "\""[0]) || (s.front() == '\'' && s.back() == '\'')))
        return s.substr(1, s.size() - 2);
    return s;
}

static bool parse_bool_value(const std::string& s) {
    std::string v = lower_copy(unquote_copy(s));
    return v == "true" || v == "yes" || v == "1" || v == "on";
}

static double parse_double_value(const std::string& s, double def = 0.0) {
    try {
        size_t idx = 0;
        double v = std::stod(unquote_copy(s), &idx);
        (void)idx;
        return v;
    } catch (...) {
        return def;
    }
}

static std::map<std::string, std::string> parse_simple_key_value(const std::string& text,
                                                                 bool strip_hash_comments) {
    std::map<std::string, std::string> out;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        if (strip_hash_comments) {
            auto hash = line.find('#');
            if (hash != std::string::npos) line = line.substr(0, hash);
        }
        line = trim_copy(std::move(line));
        if (line.empty()) continue;
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = trim_copy(line.substr(0, colon));
        std::string val = unquote_copy(line.substr(colon + 1));
        if (!key.empty()) out[key] = val;
    }
    return out;
}

static std::string meta_get(const std::map<std::string, std::string>& meta,
                            const std::string& key) {
    if (auto it = meta.find(key); it != meta.end()) return it->second;
    if (auto it = meta.find(lower_copy(key)); it != meta.end()) return it->second;
    return {};
}

static std::string meta_get_any(const std::map<std::string, std::string>& meta,
                                std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        std::string value = meta_get(meta, key);
        if (!value.empty()) return value;
    }
    return {};
}

static std::optional<double> meta_get_double_opt(const std::map<std::string, std::string>& meta,
                                                 std::initializer_list<const char*> keys) {
    std::string value = meta_get_any(meta, keys);
    if (value.empty()) return std::nullopt;
    return parse_double_value(value);
}

static ChartMetadata metadata_from_map(const std::map<std::string, std::string>& meta) {
    ChartMetadata out;
    out.name = meta_get_any(meta, {"name", "Name"});
    out.level = meta_get_any(meta, {"level", "Level"});
    out.composer = meta_get_any(meta, {"composer", "Composer"});
    out.charter = meta_get_any(meta, {"charter", "Charter"});
    out.illustrator = meta_get_any(meta, {"illustrator", "Illustrator"});
    out.format = lower_copy(meta_get_any(meta, {"format", "Format"}));
    out.song_path = meta_get_any(meta, {"music", "Music", "song", "Song"});
    out.bg_path = meta_get_any(meta, {"illustration", "Illustration", "background", "Background", "picture", "Picture"});
    if (auto v = meta_get_double_opt(meta, {"difficulty", "Difficulty"})) out.difficulty = *v;
    if (auto v = meta_get_double_opt(meta, {"previewStart", "preview_start"})) out.preview_start = *v;
    if (auto v = meta_get_double_opt(meta, {"previewEnd", "preview_end"})) out.preview_end = *v;
    if (auto v = meta_get_double_opt(meta, {"aspectRatio", "aspect_ratio"})) out.aspect_ratio = *v;
    if (auto v = meta_get_double_opt(meta, {"backgroundDim", "background_dim"})) out.background_dim = *v;
    if (auto v = meta_get_double_opt(meta, {"lineLength", "line_length"})) out.line_length = *v;
    std::string hold_partial = meta_get_any(meta, {"holdPartialCover", "hold_partial_cover"});
    if (!hold_partial.empty()) out.hold_partial_cover = parse_bool_value(hold_partial);
    std::string uniform = meta_get_any(meta, {"noteUniformScale", "note_uniform_scale"});
    if (!uniform.empty()) out.note_uniform_scale = parse_bool_value(uniform);
    std::string force = meta_get_any(meta, {"forceAspectRatio", "force_aspect_ratio"});
    if (!force.empty()) out.force_aspect_ratio = parse_bool_value(force);
    return out;
}

static ChartMetadata metadata_from_json(const json& info) {
    ChartMetadata out;
    if (!info.is_object()) return out;
    auto str = [&](const char* key, const char* alt = nullptr) {
        if (info.contains(key) && info[key].is_string()) return info[key].get<std::string>();
        if (alt && info.contains(alt) && info[alt].is_string()) return info[alt].get<std::string>();
        return std::string{};
    };
    out.name = str("name", "Name");
    out.level = str("level", "Level");
    out.composer = str("composer", "Composer");
    out.charter = str("charter", "Charter");
    out.illustrator = str("illustrator", "Illustrator");
    out.format = lower_copy(str("format", "Format"));
    out.song_path = str("music", "song");
    out.bg_path = str("illustration", "background");
    if (info.contains("difficulty") && info["difficulty"].is_number())
        out.difficulty = info["difficulty"].get<double>();
    if (info.contains("previewStart") && info["previewStart"].is_number())
        out.preview_start = info["previewStart"].get<double>();
    if (info.contains("previewEnd") && info["previewEnd"].is_number())
        out.preview_end = info["previewEnd"].get<double>();
    if (info.contains("aspectRatio") && info["aspectRatio"].is_number())
        out.aspect_ratio = info["aspectRatio"].get<double>();
    if (info.contains("backgroundDim") && info["backgroundDim"].is_number())
        out.background_dim = info["backgroundDim"].get<double>();
    if (info.contains("lineLength") && info["lineLength"].is_number())
        out.line_length = info["lineLength"].get<double>();
    if (info.contains("holdPartialCover") && info["holdPartialCover"].is_boolean())
        out.hold_partial_cover = info["holdPartialCover"].get<bool>();
    if (info.contains("noteUniformScale") && info["noteUniformScale"].is_boolean())
        out.note_uniform_scale = info["noteUniformScale"].get<bool>();
    if (info.contains("forceAspectRatio") && info["forceAspectRatio"].is_boolean())
        out.force_aspect_ratio = info["forceAspectRatio"].get<bool>();
    return out;
}

static void apply_metadata_to_chart(ChartData& chart, const ChartMetadata& meta) {
    auto keep_or = [](std::string& dst, const std::string& src) {
        if (!src.empty()) dst = src;
    };

    keep_or(chart.metadata.name, meta.name);
    keep_or(chart.metadata.level, meta.level);
    keep_or(chart.metadata.composer, meta.composer);
    keep_or(chart.metadata.charter, meta.charter);
    keep_or(chart.metadata.illustrator, meta.illustrator);
    keep_or(chart.metadata.format, meta.format);
    if (meta.difficulty != 0.0) chart.metadata.difficulty = meta.difficulty;
    if (meta.preview_start != 0.0) chart.metadata.preview_start = meta.preview_start;
    if (meta.preview_end) chart.metadata.preview_end = meta.preview_end;
    if (meta.aspect_ratio != 16.0 / 9.0) chart.metadata.aspect_ratio = meta.aspect_ratio;
    if (meta.background_dim != 0.6) chart.metadata.background_dim = meta.background_dim;
    if (meta.line_length != 6.0) chart.metadata.line_length = meta.line_length;
    chart.metadata.hold_partial_cover = chart.metadata.hold_partial_cover || meta.hold_partial_cover;
    chart.metadata.note_uniform_scale = chart.metadata.note_uniform_scale || meta.note_uniform_scale;
    chart.metadata.force_aspect_ratio = chart.metadata.force_aspect_ratio || meta.force_aspect_ratio;
    keep_or(chart.metadata.song_path, meta.song_path);
    keep_or(chart.metadata.bg_path, meta.bg_path);

    if (chart.meta_song_path.empty()) chart.meta_song_path = chart.metadata.song_path;
    if (chart.meta_bg_path.empty()) chart.meta_bg_path = chart.metadata.bg_path;
    if (chart.metadata.song_path.empty()) chart.metadata.song_path = chart.meta_song_path;
    if (chart.metadata.bg_path.empty()) chart.metadata.bg_path = chart.meta_bg_path;
}

static std::string read_text_file(const fs::path& path) {
    std::ifstream f(path);
    if (!f) return {};
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static std::string resolve_relative_asset(const fs::path& base, const std::string& rel) {
    if (rel.empty()) return {};
    fs::path p(rel);
    if (p.is_absolute()) return p.string();
    return (base / p).string();
}

static void merge_metadata(ChartMetadata& dst, const ChartMetadata& src, bool overwrite = false) {
    auto set_s = [&](std::string& d, const std::string& s) {
        if (!s.empty() && (overwrite || d.empty())) d = s;
    };
    set_s(dst.name, src.name);
    set_s(dst.level, src.level);
    set_s(dst.composer, src.composer);
    set_s(dst.charter, src.charter);
    set_s(dst.illustrator, src.illustrator);
    set_s(dst.format, src.format);
    set_s(dst.song_path, src.song_path);
    set_s(dst.bg_path, src.bg_path);
    if (src.difficulty != 0.0 && (overwrite || dst.difficulty == 0.0)) dst.difficulty = src.difficulty;
    if (src.preview_start != 0.0 && (overwrite || dst.preview_start == 0.0)) dst.preview_start = src.preview_start;
    if (src.preview_end && (overwrite || !dst.preview_end)) dst.preview_end = src.preview_end;
    if (src.aspect_ratio != 16.0 / 9.0 && (overwrite || dst.aspect_ratio == 16.0 / 9.0))
        dst.aspect_ratio = src.aspect_ratio;
    if (src.background_dim != 0.6 && (overwrite || dst.background_dim == 0.6))
        dst.background_dim = src.background_dim;
    if (src.line_length != 6.0 && (overwrite || dst.line_length == 6.0))
        dst.line_length = src.line_length;
    dst.hold_partial_cover = src.hold_partial_cover || dst.hold_partial_cover;
    dst.note_uniform_scale = src.note_uniform_scale || dst.note_uniform_scale;
    dst.force_aspect_ratio = src.force_aspect_ratio || dst.force_aspect_ratio;
}

static void fill_entry_from_metadata(ChartEntry& entry) {
    if (!entry.metadata.name.empty()) entry.name = entry.metadata.name;
    if (!entry.metadata.level.empty() && entry.difficulty.empty()) {
        std::string diff = parse_level_prefix(entry.metadata.level);
        if (!diff.empty()) entry.difficulty = diff;
    }
    if (entry.format == ChartFormat::Unknown && !entry.metadata.format.empty())
        entry.format = chart_format_from_string(entry.metadata.format);
}

static bool is_probably_text(const std::vector<uint8_t>& data) {
    if (data.empty()) return true;
    size_t sample = std::min<size_t>(data.size(), 4096);
    size_t suspicious = 0;
    for (size_t i = 0; i < sample; ++i) {
        unsigned char c = data[i];
        if (c == 0) return false;
        if (c < 0x09 || (c > 0x0d && c < 0x20)) ++suspicious;
    }
    return suspicious < sample / 20;
}

static ChartFormat detect_format_text(const std::string& text) {
    size_t pos = text.find_first_not_of(" \t\r\n");
    if (pos == std::string::npos) return ChartFormat::Unknown;
    char c = text[pos];
    if (c == 'b' || c == 'c' || c == 'n' || c == '#' || (c >= '0' && c <= '9'))
        return ChartFormat::Pec;

    try {
        auto j = json::parse(text);
        if (j.contains("META") || j.contains("BPMList"))
            return ChartFormat::Rpe;
        if (j.contains("judgeLineList") || j.contains("formatVersion"))
            return ChartFormat::Official;
    } catch (...) {
        return ChartFormat::Pec;
    }
    return ChartFormat::Official;
}

static bool path_is_phbc_name(const std::string& name) {
    return has_extension(fs::path(name), {".phbc"});
}

static std::string normalize_zip_member_name(std::string name) {
    std::replace(name.begin(), name.end(), '\\', '/');
    while (name.rfind("./", 0) == 0) name.erase(0, 2);
    while (!name.empty() && name.front() == '/') name.erase(name.begin());
    return lower_copy(name);
}

static bool is_ignored_zip_member(const std::string& normalized_name) {
    if (normalized_name.rfind("__macosx/", 0) == 0) return true;
    auto slash = normalized_name.find_last_of('/');
    std::string base = slash == std::string::npos
        ? normalized_name
        : normalized_name.substr(slash + 1);
    return base.empty() || base.rfind("._", 0) == 0;
}

static std::string find_zip_member_casefold(mz_zip_archive& zip, const std::string& wanted) {
    int idx = mz_zip_reader_locate_file(&zip, wanted.c_str(), nullptr, 0);
    if (idx >= 0) {
        mz_zip_archive_file_stat stat{};
        if (mz_zip_reader_file_stat(&zip, idx, &stat)) return stat.m_filename;
    }

    const std::string norm_wanted = normalize_zip_member_name(wanted);
    const int file_count = static_cast<int>(mz_zip_reader_get_num_files(&zip));
    for (int i = 0; i < file_count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) continue;
        if (stat.m_is_directory) continue;
        std::string candidate = normalize_zip_member_name(stat.m_filename);
        if (is_ignored_zip_member(candidate)) continue;
        if (candidate == norm_wanted) return stat.m_filename;
        if (candidate.size() > norm_wanted.size() &&
            candidate.compare(candidate.size() - norm_wanted.size(), norm_wanted.size(), norm_wanted) == 0 &&
            candidate[candidate.size() - norm_wanted.size() - 1] == '/') {
            return stat.m_filename;
        }
    }
    return {};
}

static bool is_music_file(const fs::path& p) {
    return has_extension(p, {".ogg", ".mp3", ".wav"});
}

static bool is_illustration_file(const fs::path& p) {
    return has_extension(p, {".png", ".jpg", ".jpeg", ".webp"});
}

static bool is_chart_file(const fs::path& p) {
    return has_extension(p, {".json", ".pec", ".phbc", ".pbc"});
}

bool is_zip_archive(const fs::path& p) {
    return has_extension(p, {".zip", ".pez"});
}

std::string chart_format_name(ChartFormat format) {
    switch (format) {
        case ChartFormat::Official: return "official";
        case ChartFormat::Rpe: return "rpe";
        case ChartFormat::Pec: return "pec";
        case ChartFormat::Phbc: return "phbc";
        case ChartFormat::Pbc: return "pbc";
        case ChartFormat::Unknown:
        default: return "unknown";
    }
}

ChartFormat chart_format_from_string(const std::string& format) {
    std::string v = lower_copy(unquote_copy(format));
    if (v == "official" || v == "pgr" || v == "json") return ChartFormat::Official;
    if (v == "rpe") return ChartFormat::Rpe;
    if (v == "pec") return ChartFormat::Pec;
    if (v == "phbc") return ChartFormat::Phbc;
    if (v == "pbc") return ChartFormat::Pbc;
    return ChartFormat::Unknown;
}

static bool is_difficulty_name(const std::string& stem) {
    static const std::set<std::string> diffs = {"EZ", "HD", "IN", "AT", "SP", "EX"};
    std::string upper = stem;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    return diffs.count(upper) > 0;
}

static std::string parse_level_prefix(const std::string& value) {
    std::string token = trim_copy(value);
    auto space = token.find_first_of(" 	");
    if (space != std::string::npos) token = token.substr(0, space);
    std::transform(token.begin(), token.end(), token.begin(), ::toupper);
    return is_difficulty_name(token) ? token : std::string{};
}

std::string extract_base_name(const std::string& filename) {
    size_t dot = filename.find_last_of('.');
    std::string base = (dot != std::string::npos) ? filename.substr(0, dot) : filename;
    if (is_difficulty_name(base)) return "";
    return base;
}

std::optional<std::string> find_music_file(const fs::path& dir) {
    if (!fs::is_directory(dir)) return std::nullopt;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file() && is_music_file(entry.path())) {
            return entry.path().string();
        }
    }
    return std::nullopt;
}

std::optional<std::string> find_illustration_file(const fs::path& dir) {
    if (!fs::is_directory(dir)) return std::nullopt;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file() && is_illustration_file(entry.path())) {
            return entry.path().string();
        }
    }
    return std::nullopt;
}

// ── Folder chart loader ──────────────────────────────────────────────────────

std::vector<ChartEntry> load_folder_chart(const fs::path& folder_path) {
    std::vector<ChartEntry> entries;

    if (!fs::is_directory(folder_path)) {
        PHLOG_TRACE(Chart, "ChartLoader: not a folder chart directory: " << folder_path.string());
        return entries;
    }

    std::string folder_name = folder_path.filename().string();
    PHLOG_DEBUG(Chart, "ChartLoader: scanning folder chart: " << folder_path.string());

    ChartMetadata package_meta;
    std::string explicit_chart;

    if (auto text = read_text_file(folder_path / "info.yml"); !text.empty()) {
        auto kv = parse_simple_key_value(text, true);
        package_meta = metadata_from_map(kv);
        explicit_chart = meta_get_any(kv, {"chart", "Chart"});
    } else if (auto text_yaml = read_text_file(folder_path / "info.yaml"); !text_yaml.empty()) {
        auto kv = parse_simple_key_value(text_yaml, true);
        package_meta = metadata_from_map(kv);
        explicit_chart = meta_get_any(kv, {"chart", "Chart"});
    } else if (auto text_json = read_text_file(folder_path / "info.json"); !text_json.empty()) {
        try {
            json info = json::parse(text_json);
            package_meta = metadata_from_json(info);
            if (info.contains("chart") && info["chart"].is_string())
                explicit_chart = info["chart"].get<std::string>();
        } catch (...) {
            PHLOG_WARN(Chart, "ChartLoader: failed to parse folder info.json: "
                << (folder_path / "info.json").string());
        }
    } else if (auto text_txt = read_text_file(folder_path / "info.txt"); !text_txt.empty()) {
        auto kv = parse_simple_key_value(text_txt, false);
        package_meta = metadata_from_map(kv);
        explicit_chart = meta_get_any(kv, {"Chart", "chart"});
    }

    if (!package_meta.name.empty()) folder_name = package_meta.name;

    // Find all chart files
    std::vector<fs::path> chart_files;
    if (!explicit_chart.empty()) {
        chart_files.push_back(folder_path / explicit_chart);
    } else {
        for (const auto& entry : fs::directory_iterator(folder_path)) {
            if (entry.is_regular_file() && is_chart_file(entry.path())) {
                auto name = lower_copy(entry.path().filename().string());
                if (name == "info.json" || name == "meta.json") continue;
                chart_files.push_back(entry.path());
            }
        }
    }

    if (chart_files.empty()) {
        for (const auto& entry : fs::directory_iterator(folder_path)) {
            if (entry.is_regular_file()) {
                auto name = lower_copy(entry.path().filename().string());
                if (name == "info.yml" || name == "info.yaml" || name == "info.json" || name == "info.txt")
                    continue;
                chart_files.push_back(entry.path());
                break;
            }
        }
    }

    // Find shared assets
    auto music = find_music_file(folder_path);
    auto illustration = find_illustration_file(folder_path);
    if (!package_meta.song_path.empty())
        music = resolve_relative_asset(folder_path, package_meta.song_path);
    if (!package_meta.bg_path.empty())
        illustration = resolve_relative_asset(folder_path, package_meta.bg_path);

    // Create entries for each chart
    for (const auto& chart_file : chart_files) {
        ChartEntry entry;
        entry.name = folder_name;
        entry.chart_path = chart_file.string();
        entry.source_type = "folder";
        entry.metadata = package_meta;
        entry.format = chart_format_from_string(package_meta.format);
        if (entry.format == ChartFormat::Unknown && fs::exists(chart_file)) {
            std::ifstream f(chart_file, std::ios::binary);
            std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            entry.format = detect_chart_format_bytes(data, package_meta.format, chart_file.filename().string());
        }

        // Determine difficulty from metadata or filename.
        std::string stem = chart_file.stem().string();
        std::transform(stem.begin(), stem.end(), stem.begin(), ::toupper);
        entry.difficulty = parse_level_prefix(entry.metadata.level);
        if (entry.difficulty.empty()) entry.difficulty = stem;

        // Assign assets
        if (music) entry.assets.music_path = *music;
        if (illustration) entry.assets.illustration_path = *illustration;

        fill_entry_from_metadata(entry);
        entries.push_back(entry);
        PHLOG_TRACE(Chart, "ChartLoader: folder entry chart=" << entry.chart_path
            << " diff=" << entry.difficulty
            << " format=" << chart_format_name(entry.format)
            << " music=" << (entry.assets.music_path.empty() ? "<none>" : entry.assets.music_path)
            << " bg=" << (entry.assets.illustration_path.empty() ? "<none>" : entry.assets.illustration_path));
    }

    PHLOG_DEBUG(Chart, "ChartLoader: folder chart yielded " << entries.size()
        << " entry(s): " << folder_path.string());
    return entries;
}

// ── Zip chart loader ─────────────────────────────────────────────────────────
// Phira zip format:
//   info.json  — metadata: name, music, illustration, chart (or charts[])
//   *.json     — chart file(s)
//   *.ogg/mp3  — music
//   *.png/jpg  — illustration
//
// info.json schema (simplified):
//   { "name": "...", "music": "music.ogg", "illustration": "bg.png",
//     "chart": "chart.json",                    // single chart
//     "charts": [{"type":"IN","path":"in.json"}, ...] }  // multi-difficulty

std::vector<ChartEntry> load_zip_chart(const fs::path& zip_path) {
    std::vector<ChartEntry> entries;

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_file(&zip, zip_path.string().c_str(), 0)) {
        PHLOG_ERROR(Chart, "ChartLoader: failed to open zip: " << zip_path.string());
        return entries;
    }

    int file_count = mz_zip_reader_get_num_files(&zip);
    PHLOG_DEBUG(Chart, "ChartLoader: opened zip: " << zip_path.string()
        << " files=" << file_count);

    std::vector<std::string> all_files;
    all_files.reserve(file_count);
    for (int i = 0; i < file_count; ++i) {
        mz_zip_archive_file_stat st{};
        if (mz_zip_reader_file_stat(&zip, i, &st) && !st.m_is_directory) {
            if (is_ignored_zip_member(normalize_zip_member_name(st.m_filename))) continue;
            all_files.push_back(st.m_filename);
        }
    }

    auto extract_str = [&](const std::string& name) -> std::string {
        std::string member = find_zip_member_casefold(zip, name);
        if (member.empty()) return {};
        int idx = mz_zip_reader_locate_file(&zip, member.c_str(), nullptr, 0);
        if (idx < 0) return {};
        mz_zip_archive_file_stat st{};
        if (!mz_zip_reader_file_stat(&zip, idx, &st)) return {};
        std::string buf(static_cast<size_t>(st.m_uncomp_size), '\0');
        if (!mz_zip_reader_extract_to_mem(&zip, idx, buf.data(), buf.size(), 0)) return {};
        return buf;
    };

    auto find_ext = [&](const std::set<std::string>& exts) -> std::string {
        for (const auto& f : all_files) {
            fs::path p(f);
            auto ext = lower_copy(p.extension().string());
            if (exts.count(ext)) return f;
        }
        return {};
    };

    std::string zip_str = zip_path.string();
    std::string song_name = zip_path.stem().string();
    std::string fallback_music = find_ext({".ogg", ".mp3", ".wav", ".flac"});
    std::string fallback_image = find_ext({".png", ".jpg", ".jpeg", ".webp"});
    ChartMetadata package_meta;

    auto make_entry = [&](const std::string& chart_file, const std::string& diff,
                          const std::string& music_file, const std::string& image_file,
                          ChartMetadata meta = {}) {
        ChartEntry e;
        e.name = song_name;
        e.difficulty = diff;
        std::transform(e.difficulty.begin(), e.difficulty.end(), e.difficulty.begin(), ::toupper);
        if (!is_difficulty_name(e.difficulty)) e.difficulty.clear();
        std::string resolved_chart = find_zip_member_casefold(zip, chart_file);
        if (resolved_chart.empty()) resolved_chart = chart_file;
        e.chart_path = zip_str + ":" + resolved_chart;
        e.source_type = "zip";
        merge_metadata(e.metadata, package_meta);
        merge_metadata(e.metadata, meta, true);
        if (!music_file.empty()) {
            std::string resolved = find_zip_member_casefold(zip, music_file);
            e.assets.music_path = zip_str + ":" + (resolved.empty() ? music_file : resolved);
        }
        if (!image_file.empty()) {
            std::string resolved = find_zip_member_casefold(zip, image_file);
            e.assets.illustration_path = zip_str + ":" + (resolved.empty() ? image_file : resolved);
        }
        e.format = chart_format_from_string(e.metadata.format);
        if (e.format == ChartFormat::Unknown) {
            auto [zf, member] = split_zip_path(e.chart_path);
            auto bytes = extract_zip_file(zf, member);
            e.format = detect_chart_format_bytes(bytes, e.metadata.format, member);
        }
        fill_entry_from_metadata(e);
        return e;
    };

    std::string info_yml = extract_str("info.yml");
    if (info_yml.empty()) info_yml = extract_str("info.yaml");
    if (!info_yml.empty()) {
        auto meta = parse_simple_key_value(info_yml, true);
        package_meta = metadata_from_map(meta);
        if (!package_meta.name.empty()) song_name = package_meta.name;
        std::string music_file = package_meta.song_path.empty() ? fallback_music : package_meta.song_path;
        std::string image_file = package_meta.bg_path.empty() ? fallback_image : package_meta.bg_path;
        std::string diff = parse_level_prefix(package_meta.level);
        std::string chart_file = meta_get_any(meta, {"chart", "Chart"});
        if (!chart_file.empty())
            entries.push_back(make_entry(chart_file, diff, music_file, image_file));
    }

    std::string info_json = entries.empty() ? extract_str("info.json") : std::string{};
    if (!info_json.empty()) {
        try {
            json info = json::parse(info_json);
            package_meta = metadata_from_json(info);
            if (!package_meta.name.empty()) song_name = package_meta.name;

            std::string music_file = package_meta.song_path.empty() ? info.value("music", fallback_music) : package_meta.song_path;
            std::string image_file = package_meta.bg_path.empty() ? info.value("illustration", fallback_image) : package_meta.bg_path;
            if (image_file.empty()) image_file = info.value("background", fallback_image);

            if (info.contains("charts") && info["charts"].is_array()) {
                for (const auto& c : info["charts"]) {
                    std::string path = c.value("path", "");
                    std::string type = c.value("type", "");
                    ChartMetadata cm = package_meta;
                    if (c.contains("level") && c["level"].is_string()) cm.level = c["level"].get<std::string>();
                    if (c.contains("difficulty") && c["difficulty"].is_number()) cm.difficulty = c["difficulty"].get<double>();
                    if (c.contains("format") && c["format"].is_string()) cm.format = lower_copy(c["format"].get<std::string>());
                    if (!path.empty())
                        entries.push_back(make_entry(path, type, music_file, image_file, cm));
                }
            } else if (info.contains("chart") && info["chart"].is_string()) {
                entries.push_back(make_entry(info["chart"].get<std::string>(), "", music_file, image_file, package_meta));
            }
        } catch (...) {
            PHLOG_WARN(Chart, "ChartLoader: failed to parse info.json, falling back to scan: "
                << zip_path.string());
        }
    }

    if (entries.empty()) {
        auto info_txt = extract_str("info.txt");
        if (!info_txt.empty()) {
            auto meta = parse_simple_key_value(info_txt, false);
            package_meta = metadata_from_map(meta);
            if (!package_meta.name.empty()) song_name = package_meta.name;
            std::string music_file = package_meta.song_path.empty() ? fallback_music : package_meta.song_path;
            std::string image_file = package_meta.bg_path.empty() ? fallback_image : package_meta.bg_path;
            std::string diff = parse_level_prefix(package_meta.level);
            std::string chart_file = meta_get_any(meta, {"Chart", "chart"});
            if (!chart_file.empty())
                entries.push_back(make_entry(chart_file, diff, music_file, image_file));
        }
    }

    if (entries.empty()) {
        std::vector<std::string> chart_files;
        for (const auto& f : all_files) {
            fs::path p(f);
            auto name = lower_copy(p.filename().string());
            if (!is_chart_file(p)) continue;
            if (name == "info.json" || name == "meta.json" || name == "info.yml" || name == "info.yaml")
                continue;
            chart_files.push_back(f);
        }
        std::sort(chart_files.begin(), chart_files.end());
        for (const auto& chart_file : chart_files) {
            std::string diff;
            auto stem = fs::path(chart_file).stem().string();
            std::transform(stem.begin(), stem.end(), stem.begin(), ::toupper);
            if (is_difficulty_name(stem)) diff = stem;
            entries.push_back(make_entry(chart_file, diff, fallback_music, fallback_image, package_meta));
        }
    }

    mz_zip_reader_end(&zip);
    PHLOG_DEBUG(Chart, "ChartLoader: zip chart yielded " << entries.size()
        << " entry(s): " << zip_path.string());
    return entries;
}

// ── JSON chart loader ────────────────────────────────────────────────────────

ChartEntry load_json_chart(const fs::path& json_path) {
    ChartEntry entry = direct_entry_for_path(json_path.string());
    entry.source_type = "json";

    if (entry.format == ChartFormat::Unknown) {
        std::ifstream f(entry.chart_path, std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        entry.format = detect_chart_format_bytes(data, entry.metadata.format, json_path.filename().string());
    }

    PHLOG_TRACE(Chart, "ChartLoader: standalone chart path=" << entry.chart_path
        << " name=" << entry.name
        << " diff=" << (entry.difficulty.empty() ? "<none>" : entry.difficulty)
        << " format=" << chart_format_name(entry.format)
        << " music=" << (entry.assets.music_path.empty() ? "<none>" : entry.assets.music_path)
        << " bg=" << (entry.assets.illustration_path.empty() ? "<none>" : entry.assets.illustration_path));

    return entry;
}

std::optional<ChartEntry> resolve_chart_entry(const std::string& path,
                                                    const std::string& preferred_difficulty) {
    std::vector<ChartEntry> entries;
    fs::path input(path);

    if (fs::is_directory(input)) {
        entries = load_folder_chart(input);
    } else if (fs::is_regular_file(input) && is_zip_archive(input)) {
        entries = load_zip_chart(input);
    } else {
        return std::nullopt;
    }

    if (entries.empty()) return std::nullopt;

    std::string preferred = preferred_difficulty;
    std::transform(preferred.begin(), preferred.end(), preferred.begin(), ::toupper);
    for (const auto& entry : entries) {
        if (!preferred.empty() && entry.difficulty == preferred)
            return entry;
    }
    return entries.front();
}

// ── Directory scanner ────────────────────────────────────────────────────────

std::vector<ChartEntry> scan_charts_directory(const std::string& dir_path) {
    std::vector<ChartEntry> all_entries;

    if (!fs::exists(dir_path) || !fs::is_directory(dir_path)) {
        PHLOG_ERROR(Chart, "ChartLoader: directory not found: " << dir_path);
        return all_entries;
    }

    PHLOG_INFO(Chart, "ChartLoader: scanning chart directory: " << dir_path);

    for (const auto& entry : fs::directory_iterator(dir_path)) {
        if (entry.is_directory()) {
            // Folder chart
            auto folder_entries = load_folder_chart(entry.path());
            all_entries.insert(all_entries.end(),
                             folder_entries.begin(), folder_entries.end());
        } else if (entry.is_regular_file()) {
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            if (is_zip_archive(entry.path())) {
                // Zip chart
                auto zip_entries = load_zip_chart(entry.path());
                all_entries.insert(all_entries.end(),
                                 zip_entries.begin(), zip_entries.end());
            } else if (is_chart_file(entry.path())) {
                // Standalone JSON/PEC/PHBC
                all_entries.push_back(load_json_chart(entry.path()));
            }
        }
    }

    // Sort by name, then difficulty
    std::sort(all_entries.begin(), all_entries.end(),
              [](const ChartEntry& a, const ChartEntry& b) {
                  if (a.name != b.name) return a.name < b.name;

                  // Difficulty order: EZ < HD < IN < AT < SP < EX
                  static const std::map<std::string, int> diff_order = {
                      {"EZ", 0}, {"HD", 1}, {"IN", 2}, {"AT", 3}, {"SP", 4}, {"EX", 5}
                  };

                  auto it_a = diff_order.find(a.difficulty);
                  auto it_b = diff_order.find(b.difficulty);
                  int order_a = (it_a != diff_order.end()) ? it_a->second : 99;
                  int order_b = (it_b != diff_order.end()) ? it_b->second : 99;

                  return order_a < order_b;
              });

    PHLOG_INFO(Chart, "ChartLoader: discovered " << all_entries.size()
        << " chart entr" << (all_entries.size() == 1 ? "y" : "ies")
        << " under " << dir_path);
    return all_entries;
}

// ── Unified chart loading ───────────────────────────────────────────────────

ChartFormat detect_chart_format_bytes(const std::vector<uint8_t>& data,
                                       const std::string& explicit_format,
                                       const std::string& virtual_name) {
    ChartFormat explicit_fmt = chart_format_from_string(explicit_format);
    if (explicit_fmt != ChartFormat::Unknown) return explicit_fmt;

    if (path_is_phbc_name(virtual_name)) return ChartFormat::Phbc;
    if (data.size() >= 4 &&
        data[0] == 'P' && data[1] == 'H' && data[2] == 'B' && data[3] == 'C') {
        return ChartFormat::Phbc;
    }

    if (!is_probably_text(data)) return ChartFormat::Pbc;

    std::string text(data.begin(), data.end());
    ChartFormat fmt = detect_format_text(text);
    if (fmt != ChartFormat::Unknown) return fmt;

    // Suffix is not authoritative in Phira packages, but for direct ambiguous
    // text files it is still a useful last resort.
    if (has_extension(fs::path(virtual_name), {".pec"})) return ChartFormat::Pec;
    return ChartFormat::Unknown;
}

ChartFormat detect_chart_format(const std::string& path,
                                const std::string& preferred_difficulty) {
    if (auto resolved = resolve_chart_entry(path, preferred_difficulty))
        return resolved->format == ChartFormat::Unknown
            ? detect_chart_format(resolved->chart_path, preferred_difficulty)
            : resolved->format;

    if (is_zip_path(path)) {
        auto [zip_path, file_in_zip] = split_zip_path(path);
        auto data = extract_zip_file(zip_path, file_in_zip);
        return detect_chart_format_bytes(data, {}, file_in_zip);
    }

    std::ifstream f(path, std::ios::binary);
    if (!f) return ChartFormat::Unknown;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return detect_chart_format_bytes(data, {}, fs::path(path).filename().string());
}

static ChartData parse_chart_bytes(const std::vector<uint8_t>& bytes,
                                   ChartFormat fmt,
                                   const std::string& source_path,
                                   int W, int H,
                                   int rpe_easing_shift,
                                   const std::string& password) {
    if (fmt == ChartFormat::Phbc) {
        std::string blob(bytes.begin(), bytes.end());
        std::istringstream in(blob, std::ios::in | std::ios::binary);
        return read_phbc(in, password).to_chart_data();
    }
    if (fmt == ChartFormat::Pbc) {
        return load_pbc_bytes(bytes, W, H);
    }

    std::string text(bytes.begin(), bytes.end());
    if (fmt == ChartFormat::Rpe)
        return load_rpe(json::parse(text), W, H, rpe_easing_shift);
    if (fmt == ChartFormat::Official)
        return load_official(json::parse(text), W, H);
    if (fmt == ChartFormat::Pec)
        return load_pec_text(text, W, H);

    throw std::runtime_error("Unsupported chart format: " + source_path);
}

static ChartEntry direct_entry_for_path(const std::string& path) {
    ChartEntry entry;
    entry.chart_path = path;
    entry.source_type = "file";

    fs::path p(path);
    entry.name = extract_base_name(p.stem().string());
    if (entry.name.empty()) entry.name = p.parent_path().filename().string();
    std::string stem = p.stem().string();
    std::transform(stem.begin(), stem.end(), stem.begin(), ::toupper);
    if (is_difficulty_name(stem)) entry.difficulty = stem;

    fs::path parent = p.parent_path();
    if (fs::is_directory(parent)) {
        auto music = find_music_file(parent);
        auto illustration = find_illustration_file(parent);
        if (music) entry.assets.music_path = *music;
        if (illustration) entry.assets.illustration_path = *illustration;

        // Sibling package metadata is useful when a chart file is opened
        // directly from an extracted Phira package.
        ChartMetadata meta;
        if (auto text = read_text_file(parent / "info.yml"); !text.empty())
            meta = metadata_from_map(parse_simple_key_value(text, true));
        else if (auto text_yaml = read_text_file(parent / "info.yaml"); !text_yaml.empty())
            meta = metadata_from_map(parse_simple_key_value(text_yaml, true));
        else if (auto text_json = read_text_file(parent / "info.json"); !text_json.empty()) {
            try { meta = metadata_from_json(json::parse(text_json)); } catch (...) {}
        }
        entry.metadata = meta;
        if (!meta.song_path.empty()) entry.assets.music_path = resolve_relative_asset(parent, meta.song_path);
        if (!meta.bg_path.empty()) entry.assets.illustration_path = resolve_relative_asset(parent, meta.bg_path);
    }
    entry.format = chart_format_from_string(entry.metadata.format);
    fill_entry_from_metadata(entry);
    return entry;
}

LoadedChart load_chart_with_entry(const std::string& path,
                                  int W, int H,
                                  int rpe_easing_shift,
                                  const std::string& password,
                                  const std::string& preferred_difficulty) {
    ChartEntry entry;
    if (auto resolved = resolve_chart_entry(path, preferred_difficulty)) {
        entry = *resolved;
    } else if (is_zip_path(path)) {
        entry.chart_path = path;
        entry.source_type = "zip";
        auto [zip_path, file_in_zip] = split_zip_path(path);
        entry.name = fs::path(zip_path).stem().string();
        std::string stem = fs::path(file_in_zip).stem().string();
        std::transform(stem.begin(), stem.end(), stem.begin(), ::toupper);
        if (is_difficulty_name(stem)) entry.difficulty = stem;
    } else {
        entry = direct_entry_for_path(path);
    }

    std::vector<uint8_t> bytes;
    std::string virtual_name;
    if (is_zip_path(entry.chart_path)) {
        auto [zip_path, file_in_zip] = split_zip_path(entry.chart_path);
        bytes = extract_zip_file(zip_path, file_in_zip);
        virtual_name = file_in_zip;
    } else {
        std::ifstream f(entry.chart_path, std::ios::binary);
        if (!f) throw std::runtime_error("Cannot open chart file: " + entry.chart_path);
        bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        virtual_name = fs::path(entry.chart_path).filename().string();
    }
    if (bytes.empty()) throw std::runtime_error("Chart data is empty: " + entry.chart_path);

    ChartFormat fmt = entry.format;
    if (fmt == ChartFormat::Unknown)
        fmt = detect_chart_format_bytes(bytes, entry.metadata.format, virtual_name);
    if (fmt == ChartFormat::Unknown)
        throw std::runtime_error("Unsupported chart format: " + entry.chart_path);

    ChartData chart = parse_chart_bytes(bytes, fmt, entry.chart_path, W, H, rpe_easing_shift, password);
    if (chart.metadata.format.empty()) chart.metadata.format = chart_format_name(fmt);
    if (entry.metadata.format.empty()) entry.metadata.format = chart_format_name(fmt);
    apply_metadata_to_chart(chart, entry.metadata);
    if (chart.metadata.format.empty()) chart.metadata.format = chart_format_name(fmt);
    chart.meta_song_path = chart.metadata.song_path;
    chart.meta_bg_path = chart.metadata.bg_path;

    LoadedChart loaded;
    loaded.chart = std::move(chart);
    loaded.entry = std::move(entry);
    loaded.format = fmt;
    return loaded;
}

ChartData load_chart_data(const std::string& path,
                          int W, int H,
                          int rpe_easing_shift,
                          const std::string& password,
                          const std::string& preferred_difficulty) {
    return load_chart_with_entry(path, W, H, rpe_easing_shift, password, preferred_difficulty).chart;
}

// ── Zip utilities ────────────────────────────────────────────────────────────

bool is_zip_path(const std::string& path) {
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower.find(".zip:") != std::string::npos || lower.find(".pez:") != std::string::npos;
}

std::pair<std::string, std::string> split_zip_path(const std::string& path) {
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    size_t marker = lower.find(".zip:");
    if (marker == std::string::npos)
        marker = lower.find(".pez:");
    if (marker == std::string::npos) return {path, ""};

    size_t colon = marker + 4;
    return {path.substr(0, colon), path.substr(colon + 1)};
}

std::vector<uint8_t> extract_zip_file(const std::string& zip_path, const std::string& file_in_zip) {
    std::vector<uint8_t> result;

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_file(&zip, zip_path.c_str(), 0)) {
        PHLOG_TRACE(Chart, "ChartLoader: extract failed to open zip: " << zip_path);
        return result;
    }

    std::string member = find_zip_member_casefold(zip, file_in_zip);
    int file_index = member.empty() ? -1
        : mz_zip_reader_locate_file(&zip, member.c_str(), nullptr, 0);
    if (file_index < 0) {
        mz_zip_reader_end(&zip);
        PHLOG_TRACE(Chart, "ChartLoader: zip member not found: " << zip_path << ":" << file_in_zip);
        return result;
    }

    mz_zip_archive_file_stat file_stat;
    if (!mz_zip_reader_file_stat(&zip, file_index, &file_stat)) {
        mz_zip_reader_end(&zip);
        PHLOG_TRACE(Chart, "ChartLoader: file stat failed: " << zip_path << ":" << file_in_zip);
        return result;
    }

    result.resize(file_stat.m_uncomp_size);
    if (!mz_zip_reader_extract_to_mem(&zip, file_index, result.data(), result.size(), 0)) {
        result.clear();
        PHLOG_TRACE(Chart, "ChartLoader: extract_to_mem failed: " << zip_path << ":" << file_in_zip);
    }

    mz_zip_reader_end(&zip);
    if (!result.empty()) {
        PHLOG_TRACE(Chart, "ChartLoader: extracted " << result.size()
            << " bytes from " << zip_path << ":" << file_in_zip);
    }
    return result;
}

} // namespace phigros::chart
