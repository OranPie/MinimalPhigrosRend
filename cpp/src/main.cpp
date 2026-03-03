#include "phigros/chart/official.hpp"
#include "phigros/chart/rpe.hpp"
#include "phigros/chart/pec.hpp"
#include <fstream>
#include <iostream>
#include <string>
#include <algorithm>
#include <chrono>
#include <cstring>

using namespace phigros;

static std::string detect_format(const std::string& path) {
    // PEC files are plain text, not JSON
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".pec")
        return "pec";

    // Try to parse as JSON and detect format
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "Cannot open: " << path << "\n";
        return "";
    }
    std::string text((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());

    // Check if it looks like JSON
    size_t pos = text.find_first_not_of(" \t\r\n");
    if (pos == std::string::npos || text[pos] != '{') {
        // Not JSON → treat as PEC text
        return "pec_text";
    }

    try {
        auto j = nlohmann::json::parse(text);
        if (j.contains("META") || j.contains("BPMList"))
            return "rpe";
        if (j.contains("judgeLineList") || j.contains("formatVersion"))
            return "official";
    } catch (...) {
        return "pec_text";
    }
    return "official"; // fallback
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: phigros_core <chart_path> [width] [height]\n";
        std::cout << "  Loads a chart and prints statistics.\n";
        return 1;
    }

    std::string path = argv[1];
    int W = (argc >= 3) ? std::atoi(argv[2]) : 1280;
    int H = (argc >= 4) ? std::atoi(argv[3]) : 720;

    std::string fmt = detect_format(path);
    if (fmt.empty()) return 1;

    std::cout << "Loading chart: " << path << "\n";
    std::cout << "  Format: " << fmt << "\n";
    std::cout << "  Resolution: " << W << "x" << H << "\n";

    auto t0 = std::chrono::high_resolution_clock::now();

    ChartData chart;
    try {
        if (fmt == "official") {
            std::ifstream f(path);
            auto j = nlohmann::json::parse(f);
            chart = chart::load_official(j, W, H);
        } else if (fmt == "rpe") {
            std::ifstream f(path);
            auto j = nlohmann::json::parse(f);
            chart = chart::load_rpe(j, W, H);
        } else if (fmt == "pec") {
            chart = chart::load_pec(path, W, H);
        } else if (fmt == "pec_text") {
            std::ifstream f(path);
            std::string text((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
            chart = chart::load_pec_text(text, W, H);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    int total = static_cast<int>(chart.notes.size());
    int playable = 0, taps = 0, drags = 0, holds = 0, flicks = 0;
    double chart_end = 0.0;
    for (auto& n : chart.notes) {
        if (!n.fake) {
            ++playable;
            if (n.kind == 1) ++taps;
            else if (n.kind == 2) ++drags;
            else if (n.kind == 3) ++holds;
            else if (n.kind == 4) ++flicks;
        }
        chart_end = std::max(chart_end, n.t_end);
    }

    std::cout << "  Loaded in " << ms << " ms\n";
    std::cout << "  Offset: " << chart.offset << " s\n";
    std::cout << "  Lines: " << chart.lines.size() << "\n";
    std::cout << "  Notes: " << total << " (playable: " << playable << ")\n";
    std::cout << "    Tap: " << taps << "  Drag: " << drags
              << "  Hold: " << holds << "  Flick: " << flicks << "\n";
    std::cout << "  Duration: " << chart_end << " s\n";

    // Quick sanity: evaluate first line at t=0
    if (!chart.lines.empty()) {
        auto& ln = chart.lines[0];
        double x = ln.pos_x(0.0);
        double y = ln.pos_y(0.0);
        double r = ln.rot(0.0);
        double a = ln.alpha(0.0);
        double s = ln.scroll_px.integral(0.0);
        std::cout << "  Line 0 at t=0: x=" << x << " y=" << y
                  << " rot=" << r << " alpha=" << a << " scroll=" << s << "\n";
    }

    return 0;
}
