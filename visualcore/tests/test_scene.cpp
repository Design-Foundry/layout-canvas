#include <catch2/catch_test_macros.hpp>
#include "vc/scene.hpp"

using vc::Rect;
using vc::Scene;
using vc::Shape;

TEST_CASE("visible keeps only shapes that intersect the world rect, in order") {
    Scene scene;
    scene.add({{0, 0, 10, 10}, {}});
    scene.add({{500, 500, 10, 10}, {}});
    scene.add({{95, 95, 10, 10}, {}});

    std::vector<Shape> out{Shape{}};
    scene.visible({0, 0, 100, 100}, out);

    REQUIRE(out.size() == 2);
    REQUIRE(out[0].bounds.x == 0);
    REQUIRE(out[1].bounds.x == 95);
}

TEST_CASE("random shapes stay inside the area and are reproducible") {
    const Rect area{-1000, -1000, 2000, 2000};
    Scene a;
    Scene b;
    vc::addRandomShapes(a, 500, area, 7);
    vc::addRandomShapes(b, 500, area, 7);

    REQUIRE(a.shapes().size() == 500);
    for (std::size_t i = 0; i < a.shapes().size(); ++i) {
        const Rect& r = a.shapes()[i].bounds;
        REQUIRE(r.x >= area.x);
        REQUIRE(r.y >= area.y);
        REQUIRE(r.x + r.w <= area.x + area.w);
        REQUIRE(r.y + r.h <= area.y + area.h);
        REQUIRE(r.x == b.shapes()[i].bounds.x);
    }
}
