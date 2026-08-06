#include "TestHarness.h"
#include "rpa/core/Geometry.h"

using namespace rpa::core;

namespace {

/// The development machine: two monitors, no scaling, so logical == physical.
ScaleMapping unscaled() {
    return ScaleMapping{Rect{0, 0, 3627, 1080}, 3627, 1080};
}

/// A single 1920x1080 monitor at 125%, which Qt reports as 1536x864. This is the
/// configuration that produced misaligned picks, and it cannot be reproduced on
/// unscaled hardware -- hence testing the arithmetic rather than the pixels.
ScaleMapping scaled125() {
    return ScaleMapping{Rect{0, 0, 1920, 1080}, 1536, 864};
}

}  // namespace

RPA_TEST(geometry_unscaled_mapping_is_identity) {
    const ScaleMapping map = unscaled();
    CHECK_EQ(map.scaleX(), 1.0);
    CHECK_EQ(map.scaleY(), 1.0);

    const Rect physical = map.toPhysical(100, 200, 300, 400);
    CHECK_EQ(physical.x, 100);
    CHECK_EQ(physical.y, 200);
    CHECK_EQ(physical.width, 300);
    CHECK_EQ(physical.height, 400);
}

RPA_TEST(geometry_scaled_display_converts_picks_to_physical_pixels) {
    const ScaleMapping map = scaled125();
    CHECK_EQ(map.scaleX(), 1.25);
    CHECK_EQ(map.scaleY(), 1.25);

    // A button the user framed at logical (400, 300) really sits at physical
    // (500, 375). Skipping this conversion is what sent clicks 100px adrift.
    const Rect physical = map.toPhysical(400, 300, 160, 40);
    CHECK_EQ(physical.x, 500);
    CHECK_EQ(physical.y, 375);
    CHECK_EQ(physical.width, 200);
    CHECK_EQ(physical.height, 50);
}

RPA_TEST(geometry_scaled_crop_keeps_capture_resolution) {
    const ScaleMapping map = scaled125();

    // The template must be cut at capture resolution: 200x50, not the 160x40 the
    // widget saw. A logical-sized crop is the wrong scale and would never match
    // the physical screen grab the matcher runs against.
    const Rect crop = map.toCapture(400, 300, 160, 40);
    CHECK_EQ(crop.x, 500);
    CHECK_EQ(crop.y, 375);
    CHECK_EQ(crop.width, 200);
    CHECK_EQ(crop.height, 50);
}

RPA_TEST(geometry_capture_crop_never_adds_desktop_origin) {
    // A monitor left of the primary one puts the desktop origin negative.
    // toPhysical must include it; toCapture must not, or every crop is offset.
    const ScaleMapping map{Rect{-1920, -120, 3840, 1200}, 3840, 1200};

    const Rect physical = map.toPhysical(10, 20, 30, 40);
    CHECK_EQ(physical.x, -1910);
    CHECK_EQ(physical.y, -100);

    const Rect crop = map.toCapture(10, 20, 30, 40);
    CHECK_EQ(crop.x, 10);
    CHECK_EQ(crop.y, 20);
    CHECK_EQ(crop.width, 30);
    CHECK_EQ(crop.height, 40);
}

RPA_TEST(geometry_crop_past_the_edge_is_clamped) {
    const ScaleMapping map = scaled125();

    // Logical 1500 + 200 exceeds the 1536 logical width; physical must stop at
    // 1920 rather than index past the end of the capture buffer.
    const Rect crop = map.toCapture(1500, 800, 200, 200);
    CHECK_EQ(crop.x, 1875);
    CHECK_EQ(crop.y, 1000);
    CHECK_EQ(crop.width, 45);
    CHECK_EQ(crop.height, 80);
}

RPA_TEST(geometry_fully_out_of_bounds_crop_degenerates) {
    const ScaleMapping map = scaled125();
    const Rect crop = map.toCapture(5000, 5000, 100, 100);
    CHECK_EQ(crop.width, 0);
    CHECK_EQ(crop.height, 0);
}

RPA_TEST(geometry_axes_scale_independently) {
    // Mixed-DPI desktops can compress one axis more than the other, so a single
    // shared factor would skew every pick.
    const ScaleMapping map{Rect{0, 0, 3840, 1080}, 3072, 1080};
    CHECK_EQ(map.scaleX(), 1.25);
    CHECK_EQ(map.scaleY(), 1.0);

    const Rect physical = map.toPhysical(100, 100, 100, 100);
    CHECK_EQ(physical.x, 125);
    CHECK_EQ(physical.y, 100);
    CHECK_EQ(physical.width, 125);
    CHECK_EQ(physical.height, 100);
}

RPA_TEST(geometry_pairs_a_real_mixed_dpi_desktop) {
    // Measured on the development machine with --dump-geometry: an external
    // 1920x1080 at 100% beside a laptop QHD panel at 150%. Qt reports the second
    // as 1707x960 while Windows captures it at 2560x1440, and the desktop-wide
    // ratio (4480/3627) matches neither monitor -- which is exactly why the picker
    // needs a per-screen mapping.
    const std::vector<Rect> logical{Rect{0, 0, 1920, 1080}, Rect{1920, 0, 1707, 960}};
    const std::vector<Rect> physical{Rect{0, 0, 1920, 1080}, Rect{1920, 0, 2560, 1440}};

    const std::vector<ScreenPairing> pairs = pairScreens(logical, physical);
    CHECK_EQ(pairs.size(), std::size_t{2});

    // The unscaled monitor maps one to one.
    CHECK_EQ(pairs[0].mapping().scaleX(), 1.0);
    CHECK_EQ(pairs[0].mapping().scaleY(), 1.0);
    const Rect onFirst = pairs[0].mapping().toPhysical(500, 400, 100, 50);
    CHECK_EQ(onFirst.x, 500);
    CHECK_EQ(onFirst.y, 400);

    // The 150% monitor scales, and its physical origin is added.
    CHECK_EQ(pairs[1].mapping().scaleY(), 1.5);
    const Rect onSecond = pairs[1].mapping().toPhysical(200, 300, 100, 50);
    CHECK_EQ(onSecond.x, 1920 + 300);
    CHECK_EQ(onSecond.y, 450);
    CHECK_EQ(onSecond.width, 150);
    CHECK_EQ(onSecond.height, 75);
}

RPA_TEST(geometry_desktop_wide_ratio_is_wrong_for_every_monitor) {
    // Guards the bug this replaced: one mapping stretched across a mixed-DPI
    // desktop is wrong everywhere, so a whole-desktop ScaleMapping must not agree
    // with either per-screen mapping.
    const ScaleMapping wholeDesktop{Rect{0, 0, 4480, 1440}, 3627, 1080};
    CHECK(wholeDesktop.scaleX() != 1.0);
    CHECK(wholeDesktop.scaleX() != 1.5);
    CHECK(wholeDesktop.scaleY() != 1.0);
    CHECK(wholeDesktop.scaleY() != 1.5);

    // On the unscaled monitor it inflates a pick by about a quarter.
    const Rect wrong = wholeDesktop.toPhysical(500, 400, 100, 50);
    CHECK(wrong.x > 600);
    CHECK(wrong.y > 500);
}

RPA_TEST(geometry_pairing_refuses_mismatched_screen_counts) {
    // Screens can appear or vanish between the two queries. Pairing the wrong
    // rectangles would send clicks to another monitor entirely, so refuse.
    const std::vector<Rect> logical{Rect{0, 0, 1920, 1080}};
    const std::vector<Rect> physical{Rect{0, 0, 1920, 1080}, Rect{1920, 0, 2560, 1440}};
    CHECK(pairScreens(logical, physical).empty());
    CHECK(pairScreens({}, {}).empty());
}

RPA_TEST(geometry_pairing_refuses_a_physical_screen_smaller_than_logical) {
    // A physical monitor is never smaller than the toolkit's description of it,
    // so this means the two lists are not describing the same desktop.
    const std::vector<Rect> logical{Rect{0, 0, 1920, 1080}};
    const std::vector<Rect> physical{Rect{0, 0, 1280, 720}};
    CHECK(pairScreens(logical, physical).empty());
}

RPA_TEST(geometry_pairing_is_order_independent) {
    // Neither the toolkit nor the OS promises a left-to-right enumeration order.
    const std::vector<Rect> logical{Rect{1920, 0, 1707, 960}, Rect{0, 0, 1920, 1080}};
    const std::vector<Rect> physical{Rect{1920, 0, 2560, 1440}, Rect{0, 0, 1920, 1080}};

    const std::vector<ScreenPairing> pairs = pairScreens(logical, physical);
    CHECK_EQ(pairs.size(), std::size_t{2});
    CHECK_EQ(pairs[0].logical.x, 0);
    CHECK_EQ(pairs[0].physical.width, 1920);
    CHECK_EQ(pairs[1].logical.x, 1920);
    CHECK_EQ(pairs[1].physical.width, 2560);
}

RPA_TEST(geometry_unreported_screen_extent_behaves_as_unscaled) {
    // QGuiApplication::screens() can come back empty on a headless session.
    // Falling back to 1.0 keeps coordinates usable; dividing by zero would make
    // every one of them NaN.
    const ScaleMapping map{Rect{0, 0, 1920, 1080}, 0, 0};
    CHECK_EQ(map.scaleX(), 1.0);
    CHECK_EQ(map.scaleY(), 1.0);

    const Rect physical = map.toPhysical(10, 10, 10, 10);
    CHECK_EQ(physical.x, 10);
    CHECK_EQ(physical.y, 10);
}
