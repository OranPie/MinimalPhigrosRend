#include "phigros/chart/chart_loader.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <algorithm>
#include <chrono>
#include <cstring>

using namespace phigros;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: phigros_core <chart_path> [width] [height]\n";
        std::cout << "  Loads a chart and prints statistics.\n";
        return 1;
    }

    std::string path = argv[1];
    int W = (argc >= 3) ? std::atoi(argv[2]) : 1280;
    int H = (argc >= 4) ? std::atoi(argv[3]) : 720;

    auto fmt = chart::detect_chart_format(path);
    if (fmt == chart::ChartFormat::Unknown) return 1;

    std::cout << "Loading chart: " << path << "\n";
    std::cout << "  Format: " << chart::chart_format_name(fmt) << "\n";
    std::cout << "  Resolution: " << W << "x" << H << "\n";

    auto t0 = std::chrono::high_resolution_clock::now();

    ChartData chart;
    try {
        chart = chart::load_chart_data(path, W, H);
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
