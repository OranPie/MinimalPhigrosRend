#include "phigros/chart/official.hpp"
#include "phigros/chart/rpe.hpp"
#include "phigros/chart/pec.hpp"
#include "phigros/chart/chart_loader.hpp"
#include "phigros/chart/phbc_io.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <algorithm>
#include <chrono>
#include <cstring>

using namespace phigros;

static std::string detect_format_text(const std::string& text) {
    size_t pos = text.find_first_not_of(" \t\r\n");
    if (pos == std::string::npos) return "";
    char c = text[pos];
    if (c == 'b' || c == 'c' || c == 'n' || c == '#' || (c >= '0' && c <= '9'))
        return "pec_text";

    try {
        auto j = nlohmann::json::parse(text);
        if (j.contains("META") || j.contains("BPMList"))
            return "rpe";
        if (j.contains("judgeLineList") || j.contains("formatVersion"))
            return "official";
    } catch (...) {
        return "pec_text";
    }
    return "official";
}

static std::string detect_format(const std::string& path) {
    if (auto resolved = chart::resolve_chart_entry(path))
        return detect_format(resolved->chart_path);

    if (chart::is_zip_path(path)) {
        auto [zip_path, file_in_zip] = chart::split_zip_path(path);
        auto data = chart::extract_zip_file(zip_path, file_in_zip);
        if (data.empty()) {
            std::cerr << "Cannot extract: " << path << "\n";
            return "";
        }
        std::string ext = std::filesystem::path(file_in_zip).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".phbc") return "phbc";
        if (ext == ".pec") return "pec_text";
        return detect_format_text(std::string(data.begin(), data.end()));
    }

    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".phbc") return "phbc";
    if (ext == ".pec") return "pec";

    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        std::cerr << "Cannot open: " << path << "\n";
        return "";
    }
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return detect_format_text(text);
}

static ChartData load_chart_from_zip_reference(const std::string& path, int W, int H) {
    auto [zip_path, file_in_zip] = chart::split_zip_path(path);
    auto data = chart::extract_zip_file(zip_path, file_in_zip);
    if (data.empty())
        throw std::runtime_error("Failed to extract chart from zip: " + path);

    std::string ext = std::filesystem::path(file_in_zip).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".phbc") {
        std::string blob(data.begin(), data.end());
        std::istringstream in(blob, std::ios::in | std::ios::binary);
        return chart::read_phbc(in).to_chart_data();
    }

    std::string text(data.begin(), data.end());
    std::string fmt = (ext == ".pec") ? "pec_text" : detect_format_text(text);
    if (fmt == "official")
        return chart::load_official(nlohmann::json::parse(text), W, H);
    if (fmt == "rpe")
        return chart::load_rpe(nlohmann::json::parse(text), W, H);
    return chart::load_pec_text(text, W, H);
}

static ChartData load_chart_any(const std::string& path, int W, int H) {
    if (auto resolved = chart::resolve_chart_entry(path))
        return load_chart_any(resolved->chart_path, W, H);
    if (chart::is_zip_path(path))
        return load_chart_from_zip_reference(path, W, H);
    if (path.size() >= 5 && path.substr(path.size() - 5) == ".phbc") {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) throw std::runtime_error("Cannot open: " + path);
        return chart::read_phbc(f).to_chart_data();
    }

    std::string fmt = detect_format(path);
    if (fmt == "official") {
        std::ifstream f(path);
        auto j = nlohmann::json::parse(f);
        return chart::load_official(j, W, H);
    }
    if (fmt == "rpe") {
        std::ifstream f(path);
        auto j = nlohmann::json::parse(f);
        return chart::load_rpe(j, W, H);
    }
    if (fmt == "pec")
        return chart::load_pec(path, W, H);
    if (fmt == "pec_text") {
        std::ifstream f(path);
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return chart::load_pec_text(text, W, H);
    }
    throw std::runtime_error("Unsupported chart format: " + path);
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
        chart = load_chart_any(path, W, H);
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
