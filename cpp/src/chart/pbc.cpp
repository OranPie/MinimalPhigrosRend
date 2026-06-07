#include "phigros/chart/pbc.hpp"
#include "phigros/core/logger.hpp"
#include "phigros/math/easing.hpp"
#include "phigros/math/tracks.hpp"
#include "phigros/math/util.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <stdexcept>
#include <type_traits>

using namespace phigros::math;

namespace phigros::chart {
namespace {

constexpr double RPE_WIDTH = 1350.0;
constexpr double RPE_HEIGHT = 900.0;

struct BinaryReader {
    const std::vector<uint8_t>& data;
    size_t pos = 0;
    uint32_t time_ms = 0;

    explicit BinaryReader(const std::vector<uint8_t>& bytes) : data(bytes) {}

    void require(size_t n) const {
        if (pos + n > data.size())
            throw std::runtime_error("PBC read past end of file");
    }

    uint8_t byte() {
        require(1);
        return data[pos++];
    }

    bool boolean() {
        return byte() == 1;
    }

    int32_t i32() {
        require(4);
        int32_t v = 0;
        std::memcpy(&v, data.data() + pos, 4);
        pos += 4;
        return v;
    }

    float f32() {
        require(4);
        float v = 0.0f;
        std::memcpy(&v, data.data() + pos, 4);
        pos += 4;
        return v;
    }

    uint64_t uleb() {
        uint64_t result = 0;
        int shift = 0;
        while (true) {
            uint8_t b = byte();
            result |= (uint64_t(b & 0x7f) << shift);
            if ((b & 0x80) == 0) break;
            shift += 7;
            if (shift > 63) throw std::runtime_error("PBC invalid ULEB128");
        }
        return result;
    }

    void reset_time() {
        time_ms = 0;
    }

    double time() {
        uint64_t delta = uleb();
        if (delta > static_cast<uint64_t>(UINT32_MAX) - time_ms)
            throw std::runtime_error("PBC time overflow");
        time_ms += static_cast<uint32_t>(delta);
        return static_cast<double>(time_ms) / 1000.0;
    }

    std::string string() {
        uint64_t n = uleb();
        if (n > data.size() - pos) throw std::runtime_error("PBC invalid string length");
        std::string out(reinterpret_cast<const char*>(data.data() + pos),
                        reinterpret_cast<const char*>(data.data() + pos + n));
        pos += static_cast<size_t>(n);
        return out;
    }
};

struct TweenSpec {
    enum class Kind { Static, Clamped, Bezier };
    Kind kind = Kind::Static;
    uint8_t id = 2; // prpr tween 2 = linear
    double l = 0.0, r = 1.0;
    double x1 = 0.0, y1 = 0.0, x2 = 1.0, y2 = 1.0;
};

static double prpr_tween(uint8_t id, double x) {
    x = clamp(x, 0.0, 1.0);
    switch (id) {
        case 0: return 0.0;
        case 1: return 1.0;
        case 2: return x;
        case 3: return 1.0 - std::cos(x * M_PI / 2.0); // sine in
        case 4: return std::sin(x * M_PI / 2.0);       // sine out
        case 5: {
            double y = x * 2.0;
            return y < 1.0
                ? (1.0 - std::cos(y * M_PI / 2.0)) / 2.0
                : 1.0 - (1.0 - std::cos((2.0 - y) * M_PI / 2.0)) / 2.0;
        }
        case 6: return x * x;
        case 7: return 1.0 - (1.0 - x) * (1.0 - x);
        case 8: return x < 0.5 ? 2.0 * x * x : 1.0 - std::pow(-2.0 * x + 2.0, 2.0) / 2.0;
        case 9: return x * x * x;
        case 10: return 1.0 - std::pow(1.0 - x, 3.0);
        case 11: return x < 0.5 ? 4.0 * x * x * x : 1.0 - std::pow(-2.0 * x + 2.0, 3.0) / 2.0;
        case 12: return std::pow(x, 4.0);
        case 13: return 1.0 - std::pow(1.0 - x, 4.0);
        case 14: return x < 0.5 ? 8.0 * std::pow(x, 4.0) : 1.0 - std::pow(-2.0 * x + 2.0, 4.0) / 2.0;
        case 15: return std::pow(x, 5.0);
        case 16: return 1.0 - std::pow(1.0 - x, 5.0);
        case 17: return x < 0.5 ? 16.0 * std::pow(x, 5.0) : 1.0 - std::pow(-2.0 * x + 2.0, 5.0) / 2.0;
        case 18: return x == 0.0 ? 0.0 : std::pow(2.0, 10.0 * x - 10.0);
        case 19: return x == 1.0 ? 1.0 : 1.0 - std::pow(2.0, -10.0 * x);
        case 20: {
            if (x == 0.0 || x == 1.0) return x;
            return x < 0.5
                ? std::pow(2.0, 20.0 * x - 10.0) / 2.0
                : (2.0 - std::pow(2.0, -20.0 * x + 10.0)) / 2.0;
        }
        case 21: return 1.0 - std::sqrt(std::max(0.0, 1.0 - x * x));
        case 22: return std::sqrt(std::max(0.0, 1.0 - std::pow(x - 1.0, 2.0)));
        case 23: {
            return x < 0.5
                ? (1.0 - std::sqrt(std::max(0.0, 1.0 - std::pow(2.0 * x, 2.0)))) / 2.0
                : (std::sqrt(std::max(0.0, 1.0 - std::pow(-2.0 * x + 2.0, 2.0))) + 1.0) / 2.0;
        }
        case 24: {
            constexpr double c1 = 1.70158;
            constexpr double c3 = c1 + 1.0;
            return c3 * x * x * x - c1 * x * x;
        }
        case 25: {
            constexpr double c1 = 1.70158;
            constexpr double c3 = c1 + 1.0;
            double y = x - 1.0;
            return 1.0 + c3 * y * y * y + c1 * y * y;
        }
        case 26: {
            constexpr double c2 = 1.70158 * 1.525;
            return x < 0.5
                ? (std::pow(2.0 * x, 2.0) * ((c2 + 1.0) * 2.0 * x - c2)) / 2.0
                : (std::pow(2.0 * x - 2.0, 2.0) * ((c2 + 1.0) * (2.0 * x - 2.0) + c2) + 2.0) / 2.0;
        }
        case 27: {
            if (x == 0.0 || x == 1.0) return x;
            constexpr double c4 = (2.0 * M_PI) / 3.0;
            return -std::pow(2.0, 10.0 * x - 10.0) * std::sin((x * 10.0 - 10.75) * c4);
        }
        case 28: {
            if (x == 0.0 || x == 1.0) return x;
            constexpr double c4 = (2.0 * M_PI) / 3.0;
            return std::pow(2.0, -10.0 * x) * std::sin((x * 10.0 - 0.75) * c4) + 1.0;
        }
        case 29: {
            if (x == 0.0 || x == 1.0) return x;
            constexpr double c5 = (2.0 * M_PI) / 4.5;
            return x < 0.5
                ? -(std::pow(2.0, 20.0 * x - 10.0) * std::sin((20.0 * x - 11.125) * c5)) / 2.0
                : (std::pow(2.0, -20.0 * x + 10.0) * std::sin((20.0 * x - 11.125) * c5)) / 2.0 + 1.0;
        }
        case 30: {
            constexpr double n1 = 7.5625, d1 = 2.75;
            double y = 1.0 - x;
            double v;
            if (y < 1.0 / d1) v = n1 * y * y;
            else if (y < 2.0 / d1) { y -= 1.5 / d1; v = n1 * y * y + 0.75; }
            else if (y < 2.5 / d1) { y -= 2.25 / d1; v = n1 * y * y + 0.9375; }
            else { y -= 2.625 / d1; v = n1 * y * y + 0.984375; }
            return 1.0 - v;
        }
        case 31: {
            constexpr double n1 = 7.5625, d1 = 2.75;
            double y = x;
            if (y < 1.0 / d1) return n1 * y * y;
            if (y < 2.0 / d1) { y -= 1.5 / d1; return n1 * y * y + 0.75; }
            if (y < 2.5 / d1) { y -= 2.25 / d1; return n1 * y * y + 0.9375; }
            y -= 2.625 / d1;
            return n1 * y * y + 0.984375;
        }
        case 32: {
            return x < 0.5
                ? (1.0 - prpr_tween(31, 1.0 - 2.0 * x)) / 2.0
                : (1.0 + prpr_tween(31, 2.0 * x - 1.0)) / 2.0;
        }
        default:
            return x;
    }
}

static double apply_tween(const TweenSpec& tw, double p) {
    p = clamp(p, 0.0, 1.0);
    switch (tw.kind) {
        case TweenSpec::Kind::Static:
            return prpr_tween(tw.id, p);
        case TweenSpec::Kind::Clamped: {
            double l = tw.l, r = tw.r;
            double mapped = lerp(l, r, p);
            double a = prpr_tween(tw.id, l);
            double b = prpr_tween(tw.id, r);
            double denom = b - a;
            if (std::abs(denom) < 1e-9) return p;
            return (prpr_tween(tw.id, mapped) - a) / denom;
        }
        case TweenSpec::Kind::Bezier:
            return cubic_bezier_y_for_x(tw.x1, tw.y1, tw.x2, tw.y2, p);
    }
    return p;
}

template <typename T>
struct Keyframe {
    double time = 0.0;
    T value{};
    TweenSpec tween{};
};

template <typename T>
struct Anim {
    std::vector<Keyframe<T>> keyframes;
    std::shared_ptr<Anim<T>> next;
};

static TweenSpec read_tween(BinaryReader& r) {
    uint8_t b = r.byte();
    TweenSpec tw;
    if ((b & 0xc0) == 0) {
        tw.kind = TweenSpec::Kind::Static;
        tw.id = b;
    } else if ((b & 0xc0) == 0x80) {
        tw.kind = TweenSpec::Kind::Clamped;
        tw.id = b & 0x7f;
        tw.l = r.f32();
        tw.r = r.f32();
    } else {
        tw.kind = TweenSpec::Kind::Bezier;
        tw.x1 = r.f32();
        tw.y1 = r.f32();
        tw.x2 = r.f32();
        tw.y2 = r.f32();
    }
    return tw;
}

static math::RGB read_rgb(BinaryReader& r, uint8_t* alpha = nullptr) {
    uint8_t rr = r.byte();
    uint8_t gg = r.byte();
    uint8_t bb = r.byte();
    uint8_t aa = r.byte();
    if (alpha) *alpha = aa;
    return {rr, gg, bb};
}

template <typename T>
static T read_value(BinaryReader& r);

template <>
float read_value<float>(BinaryReader& r) { return r.f32(); }

template <>
std::string read_value<std::string>(BinaryReader& r) { return r.string(); }

template <>
math::RGB read_value<math::RGB>(BinaryReader& r) { return read_rgb(r); }

template <typename T>
static Keyframe<T> read_keyframe(BinaryReader& r) {
    Keyframe<T> kf;
    kf.time = r.time();
    kf.value = read_value<T>(r);
    kf.tween = read_tween(r);
    return kf;
}

template <typename T>
static Anim<T> read_anim(BinaryReader& r) {
    std::shared_ptr<Anim<T>> head;
    std::shared_ptr<Anim<T>>* link = &head;
    while (true) {
        uint8_t marker = r.byte();
        if (marker == 0) break;
        auto node = std::make_shared<Anim<T>>();
        if (marker == 1) {
            // Empty animation layer.
        } else if (marker == 2) {
            uint64_t n = r.uleb();
            if (n > 1'000'000) throw std::runtime_error("PBC animation too large");
            r.reset_time();
            node->keyframes.reserve(static_cast<size_t>(n));
            for (uint64_t i = 0; i < n; ++i)
                node->keyframes.push_back(read_keyframe<T>(r));
        } else {
            throw std::runtime_error("PBC invalid animation marker");
        }
        *link = node;
        link = &((*link)->next);
    }
    return head ? *head : Anim<T>{};
}

template <typename T>
static T anim_layer_eval(const Anim<T>& anim, double t, const T& def);

template <>
float anim_layer_eval<float>(const Anim<float>& anim, double t, const float& def) {
    if (anim.keyframes.empty()) return def;
    if (t <= anim.keyframes.front().time) return anim.keyframes.front().value;
    for (size_t i = 0; i + 1 < anim.keyframes.size(); ++i) {
        const auto& a = anim.keyframes[i];
        const auto& b = anim.keyframes[i + 1];
        if (t <= b.time) {
            double denom = b.time - a.time;
            double p = denom <= 1e-9 ? 1.0 : (t - a.time) / denom;
            return static_cast<float>(lerp(a.value, b.value, apply_tween(a.tween, p)));
        }
    }
    return anim.keyframes.back().value;
}

template <>
std::string anim_layer_eval<std::string>(const Anim<std::string>& anim, double t, const std::string& def) {
    if (anim.keyframes.empty()) return def;
    if (t <= anim.keyframes.front().time) return anim.keyframes.front().value;
    for (size_t i = 0; i + 1 < anim.keyframes.size(); ++i) {
        if (t <= anim.keyframes[i + 1].time) return anim.keyframes[i].value;
    }
    return anim.keyframes.back().value;
}

template <>
math::RGB anim_layer_eval<math::RGB>(const Anim<math::RGB>& anim, double t, const math::RGB& def) {
    if (anim.keyframes.empty()) return def;
    if (t <= anim.keyframes.front().time) return anim.keyframes.front().value;
    for (size_t i = 0; i + 1 < anim.keyframes.size(); ++i) {
        const auto& a = anim.keyframes[i];
        const auto& b = anim.keyframes[i + 1];
        if (t <= b.time) {
            double denom = b.time - a.time;
            double p = denom <= 1e-9 ? 1.0 : (t - a.time) / denom;
            double e = apply_tween(a.tween, p);
            return {
                static_cast<int>(clamp(std::round(lerp(a.value.r, b.value.r, e)), 0.0, 255.0)),
                static_cast<int>(clamp(std::round(lerp(a.value.g, b.value.g, e)), 0.0, 255.0)),
                static_cast<int>(clamp(std::round(lerp(a.value.b, b.value.b, e)), 0.0, 255.0))
            };
        }
    }
    return anim.keyframes.back().value;
}

template <typename T>
static T anim_eval(const Anim<T>& anim, double t, const T& def) {
    T out = anim_layer_eval(anim, t, def);
    if (anim.next) {
        if constexpr (std::is_same_v<T, float>) {
            out += anim_eval(*anim.next, t, def);
        } else {
            out = anim_eval(*anim.next, t, out);
        }
    }
    return out;
}

static std::shared_ptr<PiecewiseText> make_piecewise_text(const Anim<std::string>& anim) {
    std::vector<TextSeg> segs;
    std::function<void(const Anim<std::string>&)> add_layer = [&](const Anim<std::string>& layer) {
        const auto& kfs = layer.keyframes;
        for (size_t i = 0; i + 1 < kfs.size(); ++i) {
            double t0 = kfs[i].time;
            double t1 = kfs[i + 1].time;
            if (t1 < t0) std::swap(t0, t1);
            if (t1 <= t0 + 1e-9) continue;
            segs.push_back({t0, t1, kfs[i].value, kfs[i + 1].value, "", 1, 0.0, 1.0});
        }
        if (kfs.size() == 1) {
            segs.push_back({kfs.front().time, 1e9, kfs.front().value, kfs.front().value, "", 1, 0.0, 1.0});
        }
        if (layer.next) add_layer(*layer.next);
    };
    add_layer(anim);
    std::stable_sort(segs.begin(), segs.end(), [](const TextSeg& a, const TextSeg& b) {
        return a.t0 < b.t0;
    });
    std::string def = anim_eval<std::string>(anim, 0.0, std::string{});
    return std::make_shared<PiecewiseText>(std::move(segs), def);
}

static std::shared_ptr<PiecewiseEased> make_piecewise_float(const Anim<float>& anim, double def) {
    std::vector<EasedSeg> segs;
    std::function<void(const Anim<float>&)> add_layer = [&](const Anim<float>& layer) {
        const auto& kfs = layer.keyframes;
        for (size_t i = 0; i + 1 < kfs.size(); ++i) {
            EasedSeg seg;
            seg.t0 = kfs[i].time;
            seg.t1 = kfs[i + 1].time;
            if (seg.t1 < seg.t0) std::swap(seg.t0, seg.t1);
            seg.v0 = kfs[i].value;
            seg.v1 = kfs[i + 1].value;
            if (kfs[i].tween.kind == TweenSpec::Kind::Bezier) {
                seg.easing_type = -1;
                seg.bez_x1 = kfs[i].tween.x1;
                seg.bez_y1 = kfs[i].tween.y1;
                seg.bez_x2 = kfs[i].tween.x2;
                seg.bez_y2 = kfs[i].tween.y2;
            } else {
                seg.easing_type = 0; // fallback to linear; TrackFn below preserves exact tween for core tracks.
            }
            if (seg.t1 > seg.t0 + 1e-9) segs.push_back(seg);
        }
        if (layer.next) add_layer(*layer.next);
    };
    add_layer(anim);
    if (segs.empty()) return nullptr;
    std::stable_sort(segs.begin(), segs.end(), [](const EasedSeg& a, const EasedSeg& b) {
        return a.t0 < b.t0;
    });
    return std::make_shared<PiecewiseEased>(std::move(segs), def);
}

static math::IntegralTrack make_integral_track_from_height(const Anim<float>& anim, int H) {
    auto to_px = [H](double y) { return y * 0.5 * H; };
    std::vector<double> cuts{0.0};
    std::function<void(const Anim<float>&)> collect = [&](const Anim<float>& layer) {
        for (const auto& kf : layer.keyframes) cuts.push_back(kf.time);
        if (layer.next) collect(*layer.next);
    };
    collect(anim);
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end(), [](double a, double b) {
        return std::abs(a - b) < 1e-6;
    }), cuts.end());
    if (cuts.size() < 2) return {};

    std::vector<Seg1D> segs;
    segs.reserve(cuts.size() - 1);
    for (size_t i = 0; i + 1 < cuts.size(); ++i) {
        double t0 = cuts[i];
        double t1 = cuts[i + 1];
        if (t1 <= t0 + 1e-9) continue;
        double h0 = to_px(anim_eval<float>(anim, t0, 0.0f));
        double h1 = to_px(anim_eval<float>(anim, t1, 0.0f));
        double v = (h1 - h0) / (t1 - t0);
        segs.push_back({t0, t1, v, v, h0});
    }
    return IntegralTrack(std::move(segs));
}

struct PbcObject {
    Anim<float> alpha;
    Anim<float> scale_x;
    Anim<float> scale_y;
    Anim<float> rotation;
    Anim<float> translation_x;
    Anim<float> translation_y;
};

struct PbcCtrlObject {
    Anim<float> alpha;
    Anim<float> size;
    Anim<float> pos;
    Anim<float> y;
};

struct PbcNote {
    PbcObject object;
    uint8_t kind = 0;
    double time = 0.0;
    double end_time = 0.0;
    double height = 0.0;
    double end_height = 0.0;
    double speed = 1.0;
    bool above = true;
    bool fake = false;
};

struct PbcLine {
    PbcObject object;
    PbcCtrlObject ctrl_object;
    Anim<float> height;
    Anim<float> incline;
    Anim<math::RGB> color;
    Anim<std::string> text;
    std::string texture_path;
    uint8_t kind = 0;
    std::vector<PbcNote> notes;
    int parent = -1;
    bool show_below = true;
    bool rot_with_parent = false;
    uint8_t attach_ui = 0;
    int z_index = 0;
};

static PbcObject read_object(BinaryReader& r) {
    PbcObject obj;
    obj.alpha = read_anim<float>(r);
    obj.scale_x = read_anim<float>(r);
    obj.scale_y = read_anim<float>(r);
    obj.rotation = read_anim<float>(r);
    obj.translation_x = read_anim<float>(r);
    obj.translation_y = read_anim<float>(r);
    return obj;
}

static PbcCtrlObject read_ctrl_object(BinaryReader& r) {
    PbcCtrlObject obj;
    uint8_t marker = r.byte();
    if (marker != 8) {
        throw std::runtime_error("PBC invalid CtrlObject marker");
    }
    obj.alpha = read_anim<float>(r);
    obj.size = read_anim<float>(r);
    obj.pos = read_anim<float>(r);
    obj.y = read_anim<float>(r);
    return obj;
}

static PbcNote read_note(BinaryReader& r) {
    PbcNote note;
    note.object = read_object(r);
    note.kind = r.byte();
    if (note.kind == 1) {
        note.end_time = r.f32();
        note.end_height = r.f32();
    } else if (note.kind > 3) {
        throw std::runtime_error("PBC invalid note kind");
    }
    note.time = r.time();
    note.height = r.f32();
    if (note.kind != 1) {
        note.end_time = note.time;
        note.end_height = note.height;
    }
    if (r.boolean()) note.speed = r.f32();
    note.above = r.boolean();
    note.fake = r.boolean();
    return note;
}

static PbcLine read_line(BinaryReader& r) {
    r.reset_time();
    PbcLine line;
    line.object = read_object(r);
    line.kind = r.byte();
    if (line.kind == 1) {
        line.texture_path = r.string();
    } else if (line.kind == 2) {
        line.text = read_anim<std::string>(r);
    } else if (line.kind == 3) {
        (void)read_anim<float>(r); // Paint events are not rendered by this engine.
    } else if (line.kind > 3) {
        throw std::runtime_error("PBC invalid judge line kind");
    }
    line.height = read_anim<float>(r);
    uint64_t note_count = r.uleb();
    if (note_count > 1'000'000) throw std::runtime_error("PBC note count too large");
    line.notes.reserve(static_cast<size_t>(note_count));
    for (uint64_t i = 0; i < note_count; ++i)
        line.notes.push_back(read_note(r));
    line.color = read_anim<math::RGB>(r);
    uint64_t parent = r.uleb();
    line.parent = parent == 0 ? -1 : static_cast<int>(parent - 1);
    uint8_t flags = r.byte();
    line.show_below = (flags & 1) != 0;
    line.rot_with_parent = (flags & 2) != 0;
    line.attach_ui = r.byte();
    line.ctrl_object = read_ctrl_object(r);
    line.incline = read_anim<float>(r);
    line.z_index = r.i32();
    return line;
}

static void read_chart_settings(BinaryReader& r, ChartData& chart) {
    bool pe_alpha_extension = r.boolean();
    chart.metadata.hold_partial_cover = r.boolean();
    chart.metadata.format = "pbc";
    if (pe_alpha_extension) {
        // Negative alpha extension is represented through LineState::alpha_raw.
        // The parser keeps raw alpha values in line.alpha instead of clamping here.
        PHLOG_DEBUG(Chart, "PBC pe_alpha_extension enabled");
    }
}

static double world_x_to_px(double x, int W) {
    return (x + 1.0) * 0.5 * W;
}

static double world_y_to_px(double y, int H) {
    return (1.0 - y) * 0.5 * H;
}

static double world_y_units_to_px(double y, int H) {
    return y * 0.5 * H;
}

static int map_note_kind(uint8_t pbc_kind) {
    switch (pbc_kind) {
        case 0: return 1; // Click/Tap
        case 1: return 3; // Hold
        case 2: return 4; // Flick
        case 3: return 2; // Drag
        default: return 1;
    }
}

static void apply_parent_composition(std::vector<Line>& lines, const std::vector<PbcLine>& src) {
    const int n = static_cast<int>(lines.size());
    std::vector<TrackFn> base_x(n), base_y(n), base_r(n);
    for (int i = 0; i < n; ++i) {
        base_x[i] = lines[i].pos_x;
        base_y[i] = lines[i].pos_y;
        base_r[i] = lines[i].rot;
    }

    struct Comp { TrackFn x, y, r; };
    std::vector<Comp> cache(n);
    std::vector<int> state(n, 0);
    std::function<Comp(int)> build = [&](int i) -> Comp {
        if (i < 0 || i >= n) {
            auto zero = [](double) { return 0.0; };
            return {zero, zero, zero};
        }
        if (state[i] == 2) return cache[i];
        if (state[i] == 1) throw std::runtime_error("PBC parent cycle detected");
        state[i] = 1;
        int p = src[i].parent;
        if (p < 0 || p >= n) {
            cache[i] = {base_x[i], base_y[i], base_r[i]};
        } else {
            auto parent = build(p);
            auto bx = base_x[i], by = base_y[i], br = base_r[i];
            auto px = parent.x, py = parent.y, pr = parent.r;
            TrackFn x = [bx, px](double t) { return bx(t) + px(t); };
            TrackFn y = [by, py](double t) { return by(t) + py(t); };
            TrackFn r = src[i].rot_with_parent
                ? TrackFn([br, pr](double t) { return br(t) + pr(t); })
                : br;
            cache[i] = {x, y, r};
        }
        state[i] = 2;
        return cache[i];
    };

    for (int i = 0; i < n; ++i) {
        auto c = build(i);
        lines[i].pos_x = c.x;
        lines[i].pos_y = c.y;
        lines[i].rot = c.r;
    }
}

} // namespace

ChartData load_pbc_bytes(const std::vector<uint8_t>& data, int W, int H) {
    BinaryReader r(data);
    ChartData chart;
    chart.offset = r.f32();

    uint64_t line_count = r.uleb();
    if (line_count > 100'000) throw std::runtime_error("PBC line count too large");
    std::vector<PbcLine> src_lines;
    src_lines.reserve(static_cast<size_t>(line_count));
    for (uint64_t i = 0; i < line_count; ++i)
        src_lines.push_back(read_line(r));
    read_chart_settings(r, chart);

    PHLOG_INFO(Chart, "PBC load: lines=" << src_lines.size()
        << " offset=" << chart.offset
        << " size=" << W << "x" << H);

    chart.lines.reserve(src_lines.size());
    int nid = 0;
    for (size_t i = 0; i < src_lines.size(); ++i) {
        const PbcLine& pl = src_lines[i];
        Line line;
        line.lid = static_cast<int>(i);

        auto obj_x = std::make_shared<Anim<float>>(pl.object.translation_x);
        auto obj_y = std::make_shared<Anim<float>>(pl.object.translation_y);
        auto obj_rot = std::make_shared<Anim<float>>(pl.object.rotation);
        auto obj_alpha = std::make_shared<Anim<float>>(pl.object.alpha);
        auto height = std::make_shared<Anim<float>>(pl.height);

        line.pos_x = [obj_x, W](double t) {
            return world_x_to_px(anim_eval<float>(*obj_x, t, 0.0f), W);
        };
        line.pos_y = [obj_y, H](double t) {
            return world_y_to_px(anim_eval<float>(*obj_y, t, 0.0f), H);
        };
        line.rot = [obj_rot](double t) {
            return anim_eval<float>(*obj_rot, t, 0.0f) * M_PI / 180.0;
        };
        line.alpha = [obj_alpha](double t) {
            return static_cast<double>(anim_eval<float>(*obj_alpha, t, 1.0f));
        };
        line.scroll_fn = [height, H](double t) {
            return world_y_units_to_px(anim_eval<float>(*height, t, 0.0f), H);
        };
        line.scroll_px = make_integral_track_from_height(pl.height, H);
        line.color_rgb = anim_eval<math::RGB>(pl.color, 0.0, math::RGB{255, 255, 255});
        auto color_anim = std::make_shared<Anim<math::RGB>>(pl.color);
        line.compiled_color = [color_anim](double t) {
            return anim_eval<math::RGB>(*color_anim, t, math::RGB{255, 255, 255});
        };
        if (pl.kind == 1) line.texture_path = pl.texture_path;
        if (pl.kind == 2) {
            line.text = make_piecewise_text(pl.text);
        }
        line.father = pl.parent;
        line.rotate_with_father = pl.rot_with_parent;
        line.z_order = pl.z_index;
        line.is_cover = !pl.show_below;
        if (pl.attach_ui != 0) line.attach_ui = std::to_string(pl.attach_ui);
        line.incline = make_piecewise_float(pl.incline, 0.0);
        line.scale_x = make_piecewise_float(pl.object.scale_x, 1.0);
        line.scale_y = make_piecewise_float(pl.object.scale_y, 1.0);
        chart.lines.push_back(std::move(line));

        for (const auto& pn : pl.notes) {
            Note note;
            note.nid = nid++;
            note.line_id = static_cast<int>(i);
            note.kind = map_note_kind(pn.kind);
            note.above = pn.above;
            note.fake = pn.fake;
            note.t_hit = pn.time;
            note.t_end = note.kind == 3 ? std::max(pn.end_time, pn.time) : pn.time;
            note.x_local_px = anim_eval<float>(pn.object.translation_x, 0.0, 0.0f) * W * 0.5;
            note.y_offset_px = -anim_eval<float>(pn.object.translation_y, 0.0, 0.0f) * H * 0.5;
            note.speed_mul = pn.speed;
            double sx = anim_eval<float>(pn.object.scale_x, 0.0, 1.0f);
            double sy = anim_eval<float>(pn.object.scale_y, 0.0, 1.0f);
            note.size_px = std::max(0.0, (std::abs(sx) + std::abs(sy)) * 0.5);
            note.alpha01 = clamp(anim_eval<float>(pn.object.alpha, 0.0, 1.0f), 0.0, 1.0);
            note.scroll_hit = world_y_units_to_px(pn.height, H);
            note.scroll_end = world_y_units_to_px(note.kind == 3 ? pn.end_height : pn.height, H);
            chart.notes.push_back(std::move(note));
        }
    }

    apply_parent_composition(chart.lines, src_lines);

    std::stable_sort(chart.notes.begin(), chart.notes.end(), [](const Note& a, const Note& b) {
        if (a.t_hit != b.t_hit) return a.t_hit < b.t_hit;
        return a.nid < b.nid;
    });
    chart.finalize();
    return chart;
}

ChartData load_pbc(const std::string& path, int W, int H) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open PBC file: " + path);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return load_pbc_bytes(data, W, H);
}

} // namespace phigros::chart
