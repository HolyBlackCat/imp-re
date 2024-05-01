#include "simplify_straight_edges.h"

#include <doctest/doctest.h>

#include <sstream>

TEST_CASE("geometry.simplify_straight_edges")
{
    std::ostringstream ss;

    auto l = Geom::SimplifyStraightEdges<int>([&](ivec2 point, Geom::PointInfo info)
    {
        ss << point << int(info.type) << info.closed << '\n';
    });

    ss << "---\n";
    l(ivec2(1,0), {.type = Geom::PointType::normal, .closed = true});
    l(ivec2(2,0), {.type = Geom::PointType::normal, .closed = true});
    l(ivec2(2,2), {.type = Geom::PointType::normal, .closed = true});
    l(ivec2(0,2), {.type = Geom::PointType::normal, .closed = true});
    l(ivec2(0,0), {.type = Geom::PointType::normal, .closed = true});
    l(ivec2(1,0), {.type = Geom::PointType::last, .closed = true});
    ss << "---\n";
    l(ivec2(1,0), {.type = Geom::PointType::normal, .closed = true});
    l(ivec2(2,0), {.type = Geom::PointType::normal, .closed = true});
    l(ivec2(2,2), {.type = Geom::PointType::normal, .closed = true});
    l(ivec2(0,2), {.type = Geom::PointType::normal, .closed = true});
    l(ivec2(0,1), {.type = Geom::PointType::normal, .closed = true});
    l(ivec2(1,0), {.type = Geom::PointType::last, .closed = true});
    ss << "---\n";
    l(ivec2(2,0), {.type = Geom::PointType::normal, .closed = false});
    l(ivec2(3,0), {.type = Geom::PointType::normal, .closed = false});
    l(ivec2(3,1), {.type = Geom::PointType::last, .closed = false});
    ss << "---\n";
    l(ivec2(4,0), {.type = Geom::PointType::normal, .closed = false});
    l(ivec2(5,0), {.type = Geom::PointType::normal, .closed = false});
    l(ivec2(6,0), {.type = Geom::PointType::normal, .closed = false});
    l(ivec2(6,1), {.type = Geom::PointType::normal, .closed = false});
    l(ivec2(6,2), {.type = Geom::PointType::last, .closed = false});
    ss << "---\n";
    l(ivec2(4,0), {.type = Geom::PointType::extra_edge_first, .closed = false});
    l(ivec2(5,0), {.type = Geom::PointType::normal, .closed = false});
    l(ivec2(6,0), {.type = Geom::PointType::normal, .closed = false});
    l(ivec2(6,1), {.type = Geom::PointType::extra_edge_pre_last, .closed = false});
    l(ivec2(6,2), {.type = Geom::PointType::last, .closed = false});

    REQUIRE(ss.str() == R"(---
[2,0]01
[2,2]01
[0,2]01
[0,0]01
[2,0]11
---
[2,0]01
[2,2]01
[0,2]01
[0,1]01
[1,0]01
[2,0]11
---
[2,0]00
[3,0]00
[3,1]10
---
[4,0]00
[6,0]00
[6,2]10
---
[4,0]20
[5,0]00
[6,0]00
[6,1]30
[6,2]10
)");
}
