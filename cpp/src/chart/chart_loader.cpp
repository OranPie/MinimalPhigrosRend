#include "phigros/chart/chart_loader.hpp"
#include "phigros/core/logger.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <set>
#include <iostream>
#include <miniz.h>
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

static bool is_music_file(const fs::path& p) {
    return has_extension(p, {".ogg", ".mp3", ".wav"});
}

static bool is_illustration_file(const fs::path& p) {
    return has_extension(p, {".png", ".jpg", ".jpeg", ".webp"});
}

static bool is_chart_file(const fs::path& p) {
    return has_extension(p, {".json", ".pec", ".phbc"});
}

static bool is_difficulty_name(const std::string& stem) {
    static const std::set<std::string> diffs = {"EZ", "HD", "IN", "AT", "SP", "EX"};
    std::string upper = stem;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    return diffs.count(upper) > 0;
}

std::string extract_base_name(const std::string& filename) {
    // Remove extension
    size_t dot = filename.find_last_of('.');
    std::string base = (dot != std::string::npos) ? filename.substr(0, dot) : filename;

    // If it's a difficulty name, return empty (no base name)
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

    if (!fs::is_directory(folder_path)) return entries;

    std::string folder_name = folder_path.filename().string();

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
    }

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

    // Collect all filenames
    std::vector<std::string> all_files;
    all_files.reserve(file_count);
    for (int i = 0; i < file_count; ++i) {
        mz_zip_archive_file_stat st;
        if (mz_zip_reader_file_stat(&zip, i, &st))
            all_files.push_back(st.m_filename);
    }

    // Helper: extract a file from the open zip to string
    auto extract_str = [&](const std::string& name) -> std::string {
        int idx = mz_zip_reader_locate_file(&zip, name.c_str(), nullptr, 0);
        if (idx < 0) return {};
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, idx, &st)) return {};
        std::string buf(static_cast<size_t>(st.m_uncomp_size), '\0');
        if (!mz_zip_reader_extract_to_mem(&zip, idx, buf.data(), buf.size(), 0)) return {};
        return buf;
    };

    // Helper: find first file matching extension(s)
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

    // Fallback asset discovery
    std::string fallback_music = find_ext({".ogg", ".mp3", ".wav"});
    std::string fallback_image = find_ext({".png", ".jpg", ".jpeg"});

    // Try to read info.json
    std::string info_raw = extract_str("info.json");
    if (info_raw.empty()) {
        // Some zips use info.yml — skip for now, fall back to scan
    }

    if (!info_raw.empty()) {
        try {
            json info = json::parse(info_raw);

            // Override name from metadata
            if (info.contains("name") && info["name"].is_string())
                song_name = info["name"].get<std::string>();

            // Resolve music/illustration from info
            std::string music_file = info.value("music", fallback_music);
            std::string image_file = info.value("illustration", fallback_image);
            if (image_file.empty()) image_file = info.value("background", fallback_image);

            auto make_entry = [&](const std::string& chart_file, const std::string& diff) {
                ChartEntry e;
                e.name = song_name;
                e.difficulty = diff;
                e.chart_path = zip_str + ":" + chart_file;
                e.source_type = "zip";
                if (!music_file.empty()) e.assets.music_path = zip_str + ":" + music_file;
                if (!image_file.empty()) e.assets.illustration_path = zip_str + ":" + image_file;
                return e;
            };

            // Multi-difficulty: "charts" array
            if (info.contains("charts") && info["charts"].is_array()) {
                for (const auto& c : info["charts"]) {
                    std::string path = c.value("path", "");
                    std::string type = c.value("type", "");
                    if (!path.empty())
                        entries.push_back(make_entry(path, type));
                }
            }
            // Single chart: "chart" field
            else if (info.contains("chart") && info["chart"].is_string()) {
                std::string chart_file = info["chart"].get<std::string>();
                entries.push_back(make_entry(chart_file, ""));
            }
        } catch (...) {
            // info.json parse failed — fall through to scan
        }
    }

    // Fallback: scan for first .json file
    if (entries.empty()) {
        std::string chart_file;
        for (const auto& f : all_files) {
            fs::path p(f);
            auto ext = p.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".json") { chart_file = f; break; }
        }
        if (!chart_file.empty()) {
            ChartEntry e;
            e.name = song_name;
            e.difficulty = "";
            e.chart_path = zip_str + ":" + chart_file;
            e.source_type = "zip";
            if (!fallback_music.empty()) e.assets.music_path = zip_str + ":" + fallback_music;
            if (!fallback_image.empty()) e.assets.illustration_path = zip_str + ":" + fallback_image;
            entries.push_back(e);
        }
    }

    mz_zip_reader_end(&zip);
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

    return entry;
}

// ── Directory scanner ────────────────────────────────────────────────────────

std::vector<ChartEntry> scan_charts_directory(const std::string& dir_path) {
    std::vector<ChartEntry> all_entries;

    if (!fs::exists(dir_path) || !fs::is_directory(dir_path)) {
        PHLOG_ERROR(Chart, "ChartLoader: directory not found: " << dir_path);
        return all_entries;
    }

    for (const auto& entry : fs::directory_iterator(dir_path)) {
        if (entry.is_directory()) {
            // Folder chart
            auto folder_entries = load_folder_chart(entry.path());
            all_entries.insert(all_entries.end(),
                             folder_entries.begin(), folder_entries.end());
        } else if (entry.is_regular_file()) {
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            if (ext == ".zip") {
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

    return all_entries;
}

// ── Zip utilities ────────────────────────────────────────────────────────────

bool is_zip_path(const std::string& path) {
    return path.find(':') != std::string::npos && path.find(".zip:") != std::string::npos;
}

std::pair<std::string, std::string> split_zip_path(const std::string& path) {
    size_t colon = path.find(':');
    if (colon == std::string::npos) return {path, ""};
    return {path.substr(0, colon), path.substr(colon + 1)};
}

std::vector<uint8_t> extract_zip_file(const std::string& zip_path, const std::string& file_in_zip) {
    std::vector<uint8_t> result;

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_file(&zip, zip_path.c_str(), 0)) {
        return result;
    }

    int file_index = mz_zip_reader_locate_file(&zip, file_in_zip.c_str(), nullptr, 0);
    if (file_index < 0) {
        mz_zip_reader_end(&zip);
        return result;
    }

    mz_zip_archive_file_stat file_stat;
    if (!mz_zip_reader_file_stat(&zip, file_index, &file_stat)) {
        mz_zip_reader_end(&zip);
        return result;
    }

    result.resize(file_stat.m_uncomp_size);
    if (!mz_zip_reader_extract_to_mem(&zip, file_index, result.data(), result.size(), 0)) {
        result.clear();
    }

    mz_zip_reader_end(&zip);
    return result;
}

} // namespace phigros::chart
