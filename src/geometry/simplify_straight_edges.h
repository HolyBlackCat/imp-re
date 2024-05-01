#pragma once

#include "geometry/common.h"
#include "utils/mat.h"

namespace Geom
{
    // Given a `TilesToEdges`-compatible functor, returns a similar functor that skips redundant points,
    //   replacing series of straight edges with a single straight edge.
    // `func` is `(vec2<T> point, PointInfo info [, bool convex]) -> void` that receives the edge loops.
    // The starting point of the loop is sent again at the end with `info.last == true`.
    // Optionally `func` can accept the third `bool convex` parameter, which is true if the point is convex.
    // We have custom behavior for `PointType::extra_...` point types, they (the first and last point of some open contours) are not simplified.
    template <typename T>
    [[nodiscard]] auto SimplifyStraightEdges(auto func)
    {
        return [
            func = std::move(func),
            prev_pos = vec2<T>(), // Previous `pos` argument, to determine the current edge.
            prev_corner = vec2<T>(), // Previous non-redundant point, or the first (maybe redundant) point of a loop.
            prev_dir = vec2<T>(), // Direction starting from `prev_corner`.
            point_index_in_loop = 0, // Index of vertex in this edge loop, not greater than 2.
            first_corner = vec2<T>(), // The first non-redundant point of a loop (which can be after the first `prev_corner`).
            first_corner_is_convex = bool{},
            waiting_for_first_corner = true, // The next corner will be the first corner of the loop.
            is_open_contour_with_extra_points = false // Whether this is an open contour with `PointType::extra_...` points at the beginning and end.

        ](vec2<T> pos, PointInfo info) mutable
        {
            auto RunCallback = [&](vec2<T> pos, PointInfo info, bool convex)
            {
                constexpr bool func_accepts_convexity = requires{func(vec2<T>{}, PointInfo{}, bool{});};
                if constexpr (func_accepts_convexity)
                    func(pos, info, convex);
                else
                    func(pos, info);
            };

            // Handle extra points.
            if (info.type == PointType::extra_edge_first)
            {
                RunCallback(pos, info, 0);
                is_open_contour_with_extra_points = true;
                return;
            }
            if (is_open_contour_with_extra_points && info.type == PointType::last)
            {
                RunCallback(pos, info, 0);
                is_open_contour_with_extra_points = false;
                return;
            }

            // Otherwise:

            if (point_index_in_loop == 0)
            {
                point_index_in_loop++;
                prev_corner = pos;

                if (!info.closed)
                    RunCallback(pos, {.type = PointType::normal, .closed = info.closed}, true/*I guess?*/);
            }
            else if (point_index_in_loop == 1)
            {
                point_index_in_loop++;
                prev_dir = pos - prev_corner;

            }
            else
            {
                if (auto c = prev_dir /cross/ (pos - prev_corner))
                {
                    RunCallback(prev_pos, {.type = PointType::normal, .closed = info.closed}, c > 0);

                    prev_corner = prev_pos;
                    prev_dir = pos - prev_pos;
                    if (waiting_for_first_corner)
                    {
                        waiting_for_first_corner = false;
                        first_corner = prev_pos;
                        first_corner_is_convex = c > 0;
                    }
                }
            }

            if (info.type == PointType::last || info.type == PointType::extra_edge_pre_last)
            {
                if (info.closed)
                {
                    if (auto c = (pos - prev_pos) /cross/ (first_corner - pos))
                    {
                        // If the starting vertex is a corner, need to explicitly emit it here.
                        RunCallback(pos, {.type = PointType::normal, .closed = info.closed}, c > 0);
                    }

                    RunCallback(first_corner, {.type = PointType::last, .closed = info.closed}, first_corner_is_convex);
                }
                else
                {
                    RunCallback(pos, info, first_corner_is_convex);
                }

                waiting_for_first_corner = true;
                point_index_in_loop = 0;
            }
            else
            {
                prev_pos = pos;
            }
        };
    }
}
