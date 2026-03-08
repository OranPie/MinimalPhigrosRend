// Example: Using the chart loader in your application
//
// This demonstrates how to:
// 1. Scan a charts directory
// 2. Load charts from folders, zips, and standalone files
// 3. Handle assets (music, illustrations)

#include "phigros/chart/chart_loader.hpp"
#include "phigros/chart/parser.hpp"
#include <iostream>

using namespace phigros::chart;

int main() {
    // ── Step 1: Scan charts directory ────────────────────────────────────────
    std::string charts_dir = "charts/";
    auto entries = scan_charts_directory(charts_dir);

    std::cout << "Found " << entries.size() << " charts\n\n";

    // ── Step 2: Display available charts ─────────────────────────────────────
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        std::cout << "[" << i << "] " << e.name;
        if (!e.difficulty.empty()) std::cout << " (" << e.difficulty << ")";
        std::cout << " [" << e.source_type << "]\n";
    }

    // ── Step 3: Load a specific chart ───────────────────────────────────────
    if (entries.empty()) return 0;

    const auto& selected = entries[0];
    std::cout << "\nLoading: " << selected.name << "\n";

    // Check if chart is in a zip
    if (is_zip_path(selected.chart_path)) {
        auto [zip_path, file_in_zip] = split_zip_path(selected.chart_path);
        std::cout << "  Extracting from zip: " << zip_path << "\n";
        std::cout << "  File: " << file_in_zip << "\n";

        // Extract chart JSON from zip
        auto data = extract_zip_file(zip_path, file_in_zip);
        if (data.empty()) {
            std::cerr << "Failed to extract chart from zip\n";
            return 1;
        }

        // Parse JSON
        std::string json_str(data.begin(), data.end());
        auto j = nlohmann::json::parse(json_str);

        // Load chart (detect format automatically)
        phigros::ChartData chart;
        if (j.contains("META")) {
            chart = parse_rpe(j, 1280, 720);
        } else {
            chart = parse_official(j, 1280, 720);
        }

        std::cout << "  Lines: " << chart.lines.size() << "\n";
        std::cout << "  Notes: " << chart.notes.size() << "\n";

        // Extract music if needed
        if (!selected.assets.music_path.empty() && is_zip_path(selected.assets.music_path)) {
            auto [music_zip, music_file] = split_zip_path(selected.assets.music_path);
            auto music_data = extract_zip_file(music_zip, music_file);
            std::cout << "  Music extracted: " << music_data.size() << " bytes\n";
            // Save to temp file or load directly with audio library
        }

    } else {
        // Regular file (folder or standalone)
        std::cout << "  Loading from file: " << selected.chart_path << "\n";

        std::ifstream f(selected.chart_path);
        auto j = nlohmann::json::parse(f);

        phigros::ChartData chart;
        if (j.contains("META")) {
            chart = parse_rpe(j, 1280, 720);
        } else {
            chart = parse_official(j, 1280, 720);
        }

        std::cout << "  Lines: " << chart.lines.size() << "\n";
        std::cout << "  Notes: " << chart.notes.size() << "\n";

        // Music and illustration paths are directly accessible
        if (!selected.assets.music_path.empty()) {
            std::cout << "  Music: " << selected.assets.music_path << "\n";
        }
        if (!selected.assets.illustration_path.empty()) {
            std::cout << "  Image: " << selected.assets.illustration_path << "\n";
        }
    }

    return 0;
}
