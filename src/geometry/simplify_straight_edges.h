#pragma once

namespace Geom
{
    // Given a `TilesToEdges`-compatible functor, returns a similar functor that skips redundant points,
    //   replacing series of straight edges with a single straight edge.
    // `func` is `void(vec2<T> point, bool last)` that receives the edge loops. The starting point of the loop is sent again at the end with `last == true`.
    // Optionally `func` can accept the third `bool convex` parameter, which is true if the point is convex.
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
            waiting_for_first_corner = true // The next corner will be the first corner of the loop.
        ](vec2<T> pos, bool last) mutable
        {
            constexpr bool func_accepts_convexity = requires{func(vec2<T>{}, bool{}, bool{});};

            if (point_index_in_loop == 0)
            {
                point_index_in_loop++;
                prev_corner = pos;
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
                    if constexpr (func_accepts_convexity)
                        func(prev_pos, false, c > 0);
                    else
                        func(prev_pos, false);

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

            if (last)
            {
                if (auto c = (pos - prev_pos) /cross/ (first_corner - pos))
                {
                    // If the starting vertex is a corner, need to explicitly emit it here.

                    if constexpr (func_accepts_convexity)
                        func(pos, false, c > 0);
                    else
                        func(pos, false);
                }

                if constexpr (func_accepts_convexity)
                    func(first_corner, true, first_corner_is_convex);
                else
                    func(first_corner, true);

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
