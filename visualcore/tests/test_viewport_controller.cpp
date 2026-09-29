#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include "vc/viewport_controller.hpp"

using Catch::Matchers::WithinAbs;
using vc::Camera;
using vc::Vec2;
using vc::ViewportController;

static void requireNear(Vec2 a, Vec2 b, double eps = 1e-9) {
    REQUIRE_THAT(a.x, WithinAbs(b.x, eps));
    REQUIRE_THAT(a.y, WithinAbs(b.y, eps));
}

TEST_CASE("dragging pans the camera by the pointer delta") {
    Camera cam;
    ViewportController controller{cam};

    controller.pointerDown({10, 10});
    controller.pointerMove({60, 30});

    requireNear(cam.position(), {-50, -20});
    REQUIRE(controller.isPanning());
}

TEST_CASE("each move pans only by the distance since the previous move") {
    Camera cam;
    ViewportController controller{cam};

    controller.pointerDown({0, 0});
    controller.pointerMove({10, 0});
    controller.pointerMove({25, 0});

    requireNear(cam.position(), {-25, 0});
}

TEST_CASE("moving without pressing does not pan") {
    Camera cam;
    ViewportController controller{cam};

    controller.pointerMove({60, 30});

    requireNear(cam.position(), {0, 0});
    REQUIRE_FALSE(controller.isPanning());
}

TEST_CASE("moving after release does not pan") {
    Camera cam;
    ViewportController controller{cam};

    controller.pointerDown({0, 0});
    controller.pointerMove({10, 0});
    controller.pointerUp();
    controller.pointerMove({100, 0});

    requireNear(cam.position(), {-10, 0});
    REQUIRE_FALSE(controller.isPanning());
}

TEST_CASE("wheel keeps the world point under the cursor fixed") {
    Camera cam;
    ViewportController controller{cam};
    const Vec2 cursor{640, 360};
    const Vec2 before = cam.toWorld(cursor);

    controller.wheel(cursor, -120);

    requireNear(cam.toWorld(cursor), before);
}

TEST_CASE("scrolling up zooms in, scrolling down zooms out") {
    Camera cam;
    ViewportController controller{cam};

    controller.wheel({0, 0}, -100);
    REQUIRE(cam.zoom() > 1.0);

    controller.wheel({0, 0}, 200);
    REQUIRE(cam.zoom() < 1.0);
}

TEST_CASE("opposite wheel deltas cancel out") {
    Camera cam;
    ViewportController controller{cam};

    controller.wheel({300, 200}, 100);
    controller.wheel({300, 200}, -100);

    REQUIRE_THAT(cam.zoom(), WithinAbs(1.0, 1e-12));
    requireNear(cam.position(), {0, 0});
}

TEST_CASE("a pan during a zoomed view is scaled by the zoom") {
    Camera cam;
    ViewportController controller{cam};
    controller.wheel({0, 0}, -std::log(2.0) / ViewportController::kWheelZoomSpeed);

    controller.pointerDown({0, 0});
    controller.pointerMove({100, 0});

    REQUIRE_THAT(cam.zoom(), WithinAbs(2.0, 1e-9));
    requireNear(cam.position(), {-50, 0});
}
