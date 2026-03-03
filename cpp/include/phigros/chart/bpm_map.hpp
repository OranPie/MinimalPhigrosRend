#pragma once
#include <vector>
#include <algorithm>
#include <cmath>

namespace phigros::chart {

struct BpmSeg {
    double beat0;
    double bpm;
    double sec_prefix;
};

class BpmMap {
public:
    std::vector<BpmSeg> segs;

    BpmMap() = default;

    // Build from list of (beat, bpm) pairs
    static BpmMap build(std::vector<std::pair<double, double>> items) {
        std::sort(items.begin(), items.end(),
                  [](auto& a, auto& b) { return a.first < b.first; });
        BpmMap m;
        double sec_prefix = 0.0;
        for (size_t i = 0; i < items.size(); ++i) {
            m.segs.push_back({items[i].first, items[i].second, sec_prefix});
            if (i + 1 < items.size()) {
                double db = items[i + 1].first - items[i].first;
                sec_prefix += db * 60.0 / std::max(1e-9, items[i].second);
            }
        }
        return m;
    }

    double beat_to_sec(double beat, double bpmfactor = 1.0) const {
        if (segs.empty()) return 0.0;
        // Binary search: find last seg with beat0 <= beat
        int lo = 0, hi = static_cast<int>(segs.size());
        while (lo + 1 < hi) {
            int mid = (lo + hi) / 2;
            if (segs[mid].beat0 <= beat) lo = mid; else hi = mid;
        }
        const auto& s = segs[lo];
        return (s.sec_prefix + (beat - s.beat0) * 60.0 / std::max(1e-9, s.bpm)) * bpmfactor;
    }
};

} // namespace phigros::chart
