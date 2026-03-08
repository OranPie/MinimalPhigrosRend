#include "phigros/chart/chart_loader.hpp"
#include <iostream>
#include <iomanip>

using namespace phigros::chart;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: chart_scanner <charts_directory>\n";
        std::cout << "  Scans a directory for charts (folders, zips, JSONs)\n";
        return 1;
    }

    std::string dir_path = argv[1];
    std::cout << "Scanning: " << dir_path << "\n\n";

    auto entries = scan_charts_directory(dir_path);

    if (entries.empty()) {
        std::cout << "No charts found.\n";
        return 0;
    }

    std::cout << "Found " << entries.size() << " chart(s):\n\n";

    // Print table header
    std::cout << std::left
              << std::setw(40) << "Name"
              << std::setw(8) << "Diff"
              << std::setw(10) << "Type"
              << std::setw(8) << "Music"
              << std::setw(8) << "Image"
              << "Path\n";
    std::cout << std::string(120, '-') << "\n";

    // Print entries
    for (const auto& entry : entries) {
        std::string name = entry.name;
        if (name.length() > 38) name = name.substr(0, 35) + "...";

        std::string has_music = entry.assets.music_path.empty() ? "✗" : "✓";
        std::string has_image = entry.assets.illustration_path.empty() ? "✗" : "✓";

        std::cout << std::left
                  << std::setw(40) << name
                  << std::setw(8) << entry.difficulty
                  << std::setw(10) << entry.source_type
                  << std::setw(8) << has_music
                  << std::setw(8) << has_image
                  << entry.chart_path << "\n";
    }

    std::cout << "\n";

    // Print asset details for first few entries
    int detail_count = std::min(3, (int)entries.size());
    if (detail_count > 0) {
        std::cout << "Asset details (first " << detail_count << "):\n\n";
        for (int i = 0; i < detail_count; ++i) {
            const auto& e = entries[i];
            std::cout << "[" << (i+1) << "] " << e.name;
            if (!e.difficulty.empty()) std::cout << " (" << e.difficulty << ")";
            std::cout << "\n";
            std::cout << "  Chart: " << e.chart_path << "\n";
            if (!e.assets.music_path.empty())
                std::cout << "  Music: " << e.assets.music_path << "\n";
            if (!e.assets.illustration_path.empty())
                std::cout << "  Image: " << e.assets.illustration_path << "\n";
            std::cout << "\n";
        }
    }

    return 0;
}
