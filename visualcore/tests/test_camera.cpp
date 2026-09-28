#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "vc/camera.hpp"

using Catch::Matchers::WithinAbs;
using vc::Camera;
using vc::Vec2;

static void requireNear(Vec2 a, Vec2 b, double eps = 1e-9) {
    REQUIRE_THAT(a.x, WithinAbs(b.x, eps));
    REQUIRE_THAT(a.y, WithinAbs(b.y, eps));
}

TEST_CASE("default camera maps world to screen 1:1") {
    Camera cam;
    requireNear(cam.toScreen({120, 80}), {120, 80});
    requireNear(cam.toWorld({120, 80}), {120, 80});
}

TEST_CASE("toWorld and toScreen are inverses") {
    Camera cam;
    cam.pan({-300, 150});
    cam.zoomAt({400, 300}, 3.7);
    const Vec2 p{1234.5, -987.25};
    requireNear(cam.toWorld(cam.toScreen(p)), p);
}

TEST_CASE("zoomAt keeps the point under the cursor fixed") {
    Camera cam;
    cam.pan({50, -20});
    const Vec2 cursor{640, 360};
    const Vec2 before = cam.toWorld(cursor);
    cam.zoomAt(cursor, 2.0);
    requireNear(cam.toWorld(cursor), before);
    REQUIRE_THAT(cam.zoom(), WithinAbs(2.0, 1e-12));
}

TEST_CASE("pan moves the view opposite to the drag, scaled by zoom") {
    Camera cam;
    cam.zoomAt({0, 0}, 2.0);
    cam.pan({100, 0});   // drag right by 100px at 2x
    requireNear(cam.position(), {-50, 0});
}

TEST_CASE("zoom is clamped") {
    Camera cam;
    for (int i = 0; i < 100; ++i) cam.zoomAt({0, 0}, 10.0);
    REQUIRE(cam.zoom() == Camera::kMaxZoom);
    for (int i = 0; i < 100; ++i) cam.zoomAt({0, 0}, 0.1);
    REQUIRE(cam.zoom() == Camera::kMinZoom);
}

TEST_CASE("visibleWorld shrinks as zoom grows") {
    Camera cam;
    cam.zoomAt({0, 0}, 4.0);
    const auto r = cam.visibleWorld({1600, 900});
    REQUIRE_THAT(r.w, WithinAbs(400, 1e-9));
    REQUIRE_THAT(r.h, WithinAbs(225, 1e-9));
}

TEST_CASE("precision holds far from the origin") {
    Camera cam;
    cam.pan({-5'000'000.0, -5'000'000.0});
    cam.zoomAt({500, 500}, 8.0);
    const Vec2 p{5'000'123.25, 5'000'456.5};
    requireNear(cam.toWorld(cam.toScreen(p)), p, 1e-6);
}
