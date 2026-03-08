#pragma once
#include "phigros/chart/parser.hpp"
#include <string>
#include <vector>
#include <filesystem>
#include <optional>

namespace phigros::chart {

// Asset bundle for a chart
struct ChartAssets {
    std::string music_path;      // .ogg, .mp3, .wav
    std::string illustration_path; // .png, .jpg, .jpeg
    std::vector<std::string> extra_files; // info.txt, info.yml, etc.
};

// Chart entry with metadata
struct ChartEntry {
    std::string name;            // Song name (e.g., "ATHAZA.LeaF")
    std::string difficulty;      // "EZ", "HD", "IN", "AT", "SP", etc.
    std::string chart_path;      // Path to JSON/PEC/PHBC file
    ChartAssets assets;          // Associated assets
    std::string source_type;     // "folder", "zip", "json"
};

// ── Chart discovery ──────────────────────────────────────────────────────────

// Scan a directory for charts (folders, zips, and standalone JSONs)
std::vector<ChartEntry> scan_charts_directory(const std::string& dir_path);

// Load a single folder chart (e.g., "ATHAZA.LeaF/")
std::vector<ChartEntry> load_folder_chart(const std::filesystem::path& folder_path);

// Load a single zip chart (e.g., "Horizon.Eason_AC.21311.zip")
std::vector<ChartEntry> load_zip_chart(const std::filesystem::path& zip_path);

// Load a single JSON chart with asset autofill
ChartEntry load_json_chart(const std::filesystem::path& json_path);

// ── Asset resolution ─────────────────────────────────────────────────────────

// Find music file in directory (searches for .ogg, .mp3, .wav)
std::optional<std::string> find_music_file(const std::filesystem::path& dir);

// Find illustration file in directory (searches for .png, .jpg, .jpeg)
std::optional<std::string> find_illustration_file(const std::filesystem::path& dir);

// Extract base name from chart filename (e.g., "ATHAZA.LeaF" from "ATHAZA.LeaF.ogg")
std::string extract_base_name(const std::string& filename);

// ── Zip utilities ────────────────────────────────────────────────────────────

// Extract a file from a zip archive to memory
// Returns empty vector on failure
std::vector<uint8_t> extract_zip_file(const std::string& zip_path, const std::string& file_in_zip);

// Check if a path is a zip file reference (format: "path.zip:file.json")
bool is_zip_path(const std::string& path);

// Split zip path into zip file and internal file (e.g., "a.zip:b.json" -> {"a.zip", "b.json"})
std::pair<std::string, std::string> split_zip_path(const std::string& path);

} // namespace phigros::chart
