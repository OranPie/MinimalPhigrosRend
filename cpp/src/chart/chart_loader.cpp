#include "phigros/chart/chart_loader.hpp"
#include "phigros/core/logger.hpp"
#include <algorithm>
#include <fstream>
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

static bool has_extension(const fs::path& p, const std::set<std::string>& exts) {
    auto ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
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

static bool is_music_file(const fs::path& p) {
    return has_extension(p, {".ogg", ".mp3", ".wav"});
}

static bool is_illustration_file(const fs::path& p) {
    return has_extension(p, {".png", ".jpg", ".jpeg", ".webp"});
}

static bool is_chart_file(const fs::path& p) {
    return has_extension(p, {".json", ".pec", ".phbc"});
}

bool is_zip_archive(const fs::path& p) {
    return has_extension(p, {".zip", ".pez"});
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

    // Find all chart files
    std::vector<fs::path> chart_files;
    for (const auto& entry : fs::directory_iterator(folder_path)) {
        if (entry.is_regular_file() && is_chart_file(entry.path())) {
            chart_files.push_back(entry.path());
        }
    }

    // Find shared assets
    auto music = find_music_file(folder_path);
    auto illustration = find_illustration_file(folder_path);

    // Create entries for each chart
    for (const auto& chart_file : chart_files) {
        ChartEntry entry;
        entry.name = folder_name;
        entry.chart_path = chart_file.string();
        entry.source_type = "folder";

        // Determine difficulty from filename
        std::string stem = chart_file.stem().string();
        std::transform(stem.begin(), stem.end(), stem.begin(), ::toupper);
        entry.difficulty = stem;

        // Assign assets
        if (music) entry.assets.music_path = *music;
        if (illustration) entry.assets.illustration_path = *illustration;

        entries.push_back(entry);
        PHLOG_TRACE(Chart, "ChartLoader: folder entry chart=" << entry.chart_path
            << " diff=" << entry.difficulty
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
        mz_zip_archive_file_stat st;
        if (mz_zip_reader_file_stat(&zip, i, &st))
            all_files.push_back(st.m_filename);
    }

    auto extract_str = [&](const std::string& name) -> std::string {
        int idx = mz_zip_reader_locate_file(&zip, name.c_str(), nullptr, 0);
        if (idx < 0) return {};
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, idx, &st)) return {};
        std::string buf(static_cast<size_t>(st.m_uncomp_size), '\0');
        if (!mz_zip_reader_extract_to_mem(&zip, idx, buf.data(), buf.size(), 0)) return {};
        return buf;
    };

    auto find_ext = [&](const std::set<std::string>& exts) -> std::string {
        for (const auto& f : all_files) {
            fs::path p(f);
            auto ext = p.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (exts.count(ext)) return f;
        }
        return {};
    };

    std::string zip_str = zip_path.string();
    std::string song_name = zip_path.stem().string();
    std::string fallback_music = find_ext({".ogg", ".mp3", ".wav", ".flac"});
    std::string fallback_image = find_ext({".png", ".jpg", ".jpeg", ".webp"});

    auto make_entry = [&](const std::string& chart_file, const std::string& diff,
                          const std::string& music_file, const std::string& image_file) {
        ChartEntry e;
        e.name = song_name;
        e.difficulty = diff;
        std::transform(e.difficulty.begin(), e.difficulty.end(), e.difficulty.begin(), ::toupper);
        if (!is_difficulty_name(e.difficulty)) e.difficulty.clear();
        e.chart_path = zip_str + ":" + chart_file;
        e.source_type = "zip";
        if (!music_file.empty()) e.assets.music_path = zip_str + ":" + music_file;
        if (!image_file.empty()) e.assets.illustration_path = zip_str + ":" + image_file;
        return e;
    };

    std::string info_json = extract_str("info.json");
    if (!info_json.empty()) {
        try {
            json info = json::parse(info_json);
            if (info.contains("name") && info["name"].is_string())
                song_name = info["name"].get<std::string>();

            std::string music_file = info.value("music", fallback_music);
            std::string image_file = info.value("illustration", fallback_image);
            if (image_file.empty()) image_file = info.value("background", fallback_image);

            if (info.contains("charts") && info["charts"].is_array()) {
                for (const auto& c : info["charts"]) {
                    std::string path = c.value("path", "");
                    std::string type = c.value("type", "");
                    if (!path.empty())
                        entries.push_back(make_entry(path, type, music_file, image_file));
                }
            } else if (info.contains("chart") && info["chart"].is_string()) {
                entries.push_back(make_entry(info["chart"].get<std::string>(), "", music_file, image_file));
            }
        } catch (...) {
            PHLOG_WARN(Chart, "ChartLoader: failed to parse info.json, falling back to scan: "
                << zip_path.string());
        }
    } else {
        auto info_yml = extract_str("info.yml");
        if (!info_yml.empty()) {
            auto meta = parse_simple_key_value(info_yml, true);
            if (auto it = meta.find("name"); it != meta.end() && !it->second.empty())
                song_name = it->second;

            std::string music_file = fallback_music;
            if (auto it = meta.find("music"); it != meta.end() && !it->second.empty())
                music_file = it->second;

            std::string image_file = fallback_image;
            if (auto it = meta.find("illustration"); it != meta.end() && !it->second.empty())
                image_file = it->second;
            if (image_file.empty()) {
                if (auto it = meta.find("background"); it != meta.end() && !it->second.empty())
                    image_file = it->second;
            }

            std::string diff;
            if (auto it = meta.find("level"); it != meta.end())
                diff = parse_level_prefix(it->second);

            if (auto it = meta.find("chart"); it != meta.end() && !it->second.empty())
                entries.push_back(make_entry(it->second, diff, music_file, image_file));
        }

        if (entries.empty()) {
            auto info_txt = extract_str("info.txt");
            if (!info_txt.empty()) {
                auto meta = parse_simple_key_value(info_txt, false);
                if (auto it = meta.find("Name"); it != meta.end() && !it->second.empty())
                    song_name = it->second;
                std::string music_file = meta.count("Song") ? meta["Song"] : fallback_music;
                std::string image_file = meta.count("Picture") ? meta["Picture"] : fallback_image;
                std::string diff = meta.count("Level") ? parse_level_prefix(meta["Level"]) : std::string{};
                if (auto it = meta.find("Chart"); it != meta.end() && !it->second.empty())
                    entries.push_back(make_entry(it->second, diff, music_file, image_file));
            }
        }
    }

    if (entries.empty()) {
        std::vector<std::string> chart_files;
        for (const auto& f : all_files) {
            fs::path p(f);
            auto name = p.filename().string();
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            if (!is_chart_file(p)) continue;
            if (name == "info.json" || name == "meta.json") continue;
            chart_files.push_back(f);
        }
        std::sort(chart_files.begin(), chart_files.end());
        for (const auto& chart_file : chart_files) {
            std::string diff;
            auto stem = fs::path(chart_file).stem().string();
            std::transform(stem.begin(), stem.end(), stem.begin(), ::toupper);
            if (is_difficulty_name(stem)) diff = stem;
            entries.push_back(make_entry(chart_file, diff, fallback_music, fallback_image));
        }
    }

    mz_zip_reader_end(&zip);
    PHLOG_DEBUG(Chart, "ChartLoader: zip chart yielded " << entries.size()
        << " entry(s): " << zip_path.string());
    return entries;
}

// ── JSON chart loader ────────────────────────────────────────────────────────

ChartEntry load_json_chart(const fs::path& json_path) {
    ChartEntry entry;
    entry.chart_path = json_path.string();
    entry.source_type = "json";

    // Extract name from filename or parent directory
    std::string stem = json_path.stem().string();
    fs::path parent = json_path.parent_path();

    if (is_difficulty_name(stem)) {
        // Difficulty file in a directory
        entry.difficulty = stem;
        std::transform(entry.difficulty.begin(), entry.difficulty.end(),
                      entry.difficulty.begin(), ::toupper);
        entry.name = parent.filename().string();
    } else {
        // Standalone chart
        entry.name = stem;
        entry.difficulty = "";
    }

    // Try to find assets in the same directory
    auto music = find_music_file(parent);
    auto illustration = find_illustration_file(parent);

    if (music) entry.assets.music_path = *music;
    if (illustration) entry.assets.illustration_path = *illustration;

    PHLOG_TRACE(Chart, "ChartLoader: standalone chart path=" << entry.chart_path
        << " name=" << entry.name
        << " diff=" << (entry.difficulty.empty() ? "<none>" : entry.difficulty)
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

    int file_index = mz_zip_reader_locate_file(&zip, file_in_zip.c_str(), nullptr, 0);
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
