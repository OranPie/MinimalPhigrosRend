#include "phigros/render/hold_renderer.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace phigros;
using namespace phigros::render;

static int failures = 0;

static void check(const char* name, bool cond) {
    if (!cond) {
        std::printf("FAIL %s\n", name);
        ++failures;
    }
}

static void check_near(const char* name, double got, double expected, double tol = 1e-3) {
    if (std::abs(got - expected) > tol) {
        std::printf("FAIL %s: got=%.6f expected=%.6f\n", name, got, expected);
        ++failures;
    }
}

static io::Respack make_respack() {
    io::Respack rp;
    rp.hold.tex = reinterpret_cast<SDL_Texture*>(1);
    rp.hold.w = 64;
    rp.hold.h = 96;
    rp.cfg.hold_tail_h = 16;
    rp.cfg.hold_head_h = 16;
    return rp;
}

static NoteSnapshot make_hold(bool draw_head = true) {
    NoteSnapshot ns{};
    ns.nid = 1;
    ns.kind = 3;
    ns.wx = 100.0;
    ns.wy = 100.0;
    ns.wx_tail = 100.0;
    ns.wy_tail = 200.0;
    ns.alpha = 1.0;
    ns.size_px = 1.0;
    ns.color = {255, 255, 255};
    ns.is_hold = true;
    ns.draw_hold_head = draw_head;
    return ns;
}

static DrawList render_one(const io::Respack& rp, const NoteSnapshot& ns) {
    HoldRenderer renderer;
    renderer.init(640, 960, 1.0, 1.0);
    DrawList dl;
    SpriteBatch batch;
    batch.dl = &dl;
    renderer.draw(batch, rp, std::vector<NoteSnapshot>{ns}, 0.0, 640, 960, 1.0);
    return dl;
}

static void test_respack_info_flags() {
    {
        auto cfg = io::parse_info_yml(R"yml(
name: FlagTest
holdKeepHead: True
holdRepeat: 'yes'
holdCompact: "1"
hitFxRotate: on
hitFxTinted: false
)yml");
        check("info_bool_hold_keep_head", cfg.hold_keep_head);
        check("info_bool_hold_repeat", cfg.hold_repeat);
        check("info_bool_hold_compact", cfg.hold_compact);
        check("info_bool_hitfx_rotate", cfg.hitfx_rotate);
        check("info_bool_hitfx_tinted_false", !cfg.hitfx_tinted);
    }

    const auto zip_path = std::filesystem::temp_directory_path() / "phigros_respack_info_flags.zip";
    std::filesystem::remove(zip_path);

    mz_zip_archive writer{};
    bool wrote = mz_zip_writer_init_file(&writer, zip_path.string().c_str(), 0);
    const char info[] = "holdKeepHead: true\nholdRepeat: true\nholdCompact: false\n";
    wrote = wrote && mz_zip_writer_add_mem(
        &writer, "NestedSkin/info.yml", info, sizeof(info) - 1, MZ_BEST_SPEED);
    wrote = wrote && mz_zip_writer_finalize_archive(&writer);
    mz_zip_writer_end(&writer);
    check("zip_write_nested_info", wrote);

    mz_zip_archive reader{};
    bool opened = mz_zip_reader_init_file(&reader, zip_path.string().c_str(), 0);
    check("zip_open_nested_info", opened);
    if (opened) {
        auto data = io::zip_extract(reader, "info.yml");
        check("zip_extract_nested_info", !data.empty());
        auto cfg = io::parse_info_yml(std::string(data.begin(), data.end()));
        check("zip_nested_hold_keep_head", cfg.hold_keep_head);
        check("zip_nested_hold_repeat", cfg.hold_repeat);
        check("zip_nested_hold_compact_false", !cfg.hold_compact);
        mz_zip_reader_end(&reader);
    }
    std::filesystem::remove(zip_path);

    {
        math::RGB perfect{};
        uint8_t alpha = 0;
        io::parse_hex_color("0xe1ffec9f", perfect, alpha);
        check("argb_alpha_perfect", alpha == 0xe1);
        check("argb_rgb_perfect", perfect.r == 0xff && perfect.g == 0xec && perfect.b == 0x9f);
        io::parse_hex_color("ebb4e1ff", perfect, alpha);
        check("argb_alpha_no_prefix", alpha == 0xeb);
        check("argb_rgb_no_prefix", perfect.r == 0xb4 && perfect.g == 0xe1 && perfect.b == 0xff);
    }
}

int main() {
    test_respack_info_flags();

    // Phira hold atlas order is tail / body / head from top to bottom.
    {
        auto rp = make_respack();
        auto dl = render_one(rp, make_hold(true));
        check("normal_cmd_count", dl.cmds.size() == 3);
        check("normal_tail_source", dl.cmds[0].sy == 0 && dl.cmds[0].sh == 16);
        check("normal_body_source", dl.cmds[1].sy == 16 && dl.cmds[1].sh == 64);
        check("normal_head_source", dl.cmds[2].sy == 80 && dl.cmds[2].sh == 16);
        check_near("normal_tail_h", dl.cmds[0].h, 9.6);
        check_near("normal_body_h", dl.cmds[1].h, 80.8);
        check_near("normal_head_h", dl.cmds[2].h, 9.6);
    }

    // During a held hold, holdKeepHead=false hides the head and lets body reach
    // the head/line position instead of leaving a reserved head-sized gap.
    {
        auto rp = make_respack();
        auto dl = render_one(rp, make_hold(false));
        check("holding_no_head_cmd_count", dl.cmds.size() == 2);
        check("holding_no_head_body_source", dl.cmds[1].sy == 16 && dl.cmds[1].sh == 64);
        check_near("holding_no_head_body_h", dl.cmds[1].h, 90.4);
        check_near("holding_no_head_body_center_y", dl.cmds[1].y, 145.2);
    }

    {
        auto rp = make_respack();
        rp.cfg.hold_keep_head = true;
        auto dl = render_one(rp, make_hold(false));
        check("keep_head_cmd_count", dl.cmds.size() == 3);
        check("keep_head_head_source", dl.cmds[2].sy == 80 && dl.cmds[2].sh == 16);
    }

    {
        auto rp = make_respack();
        rp.cfg.hold_repeat = true;
        auto dl = render_one(rp, make_hold(true));
        check("repeat_cmd_count", dl.cmds.size() == 5);
        check("repeat_body_sources",
              dl.cmds[1].sy == 16 && dl.cmds[2].sy == 16 && dl.cmds[3].sy == 16);
        check_near("repeat_body_tile_1_h", dl.cmds[1].h, 38.4);
        check_near("repeat_body_tile_2_h", dl.cmds[2].h, 38.4);
        check_near("repeat_body_tile_3_h", dl.cmds[3].h, 4.0);
    }

    {
        auto rp = make_respack();
        rp.cfg.hold_compact = true;
        auto dl = render_one(rp, make_hold(true));
        check("compact_cmd_count", dl.cmds.size() == 3);
        check_near("compact_tail_center_y", dl.cmds[0].y, 200.0);
        check_near("compact_body_h", dl.cmds[1].h, 100.0);
        check_near("compact_head_center_y", dl.cmds[2].y, 100.0);
    }

    {
        auto rp = make_respack();
        auto ns = make_hold(true);
        ns.miss = true;
        auto dl = render_one(rp, ns);
        check("miss_dim_rgb", dl.cmds[0].r == 128 && dl.cmds[0].g == 128 && dl.cmds[0].b == 128);
        check("miss_dim_alpha", dl.cmds[0].a == 127);
    }

    if (failures == 0)
        std::printf("All hold renderer tests passed.\n");
    else
        std::printf("%d hold renderer test(s) FAILED.\n", failures);
    return failures ? 1 : 0;
}
