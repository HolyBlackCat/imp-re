#pragma once

namespace Geom
{
    enum class PointType
    {
        // Just a normal point.
        normal,
        // The last point of a loop (or of an open chain).
        last,

        // An extra artificial vertex at the beginning of an open loop (optional).
        // `TilesToEdges` adds it to all open loops, because for Box2D first and last edges (of an open chain) have no collision.
        extra_edge_first,
        // If the first vertex was `extra_edge_first`, then this one will appear before the last one.
        extra_edge_pre_last,
    };

    struct PointInfo
    {
        PointType type = PointType::normal;

        // If the loop is open, every point in it has this set to false.
        // Closed loops receive the starting point again at the end with `info.last = false`, but open loops don't.
        bool closed = true;
    };
}
