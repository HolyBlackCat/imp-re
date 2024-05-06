#pragma once

#include "macros/enum_flag_operators.h"
#include "utils/mat.h"

#include <iterator>

// Iterating over every point of the difference between two rects.
// For now we only support 2-dimensional integral rects.

namespace Math2
{
    // Use this with CTAD.
    // This is a range and a bidirectional iterator for itself at the same time.
    // `.begin()` returns itself, while `.end()` returns `std::default_sentinel`, but a default-constructed iterator can be used as a sentinel too.
    // You can decrement beyond the starting position, and that also returns `.end()`.
    // The construct puts the iterator at the first element by default, pass `start_at_end = false` to construct at the last element.
    template <int D, Math::scalar T>
    requires (D == 2) // Only 2D for now, for simplicity.
    class RectDiffIterator
    {
        using rect_t = rect<D,T>;

        rect_t rect;
        rect_t sub_rect;

        enum class Bits : unsigned char
        {
            invalid = 1 << 0, // This iterator is invalid (`.begin() - 1` or `.end()`).
            empty_rect = 1 << 1, // This rect is empty (or fully destroyed by subtraction).
            intact_rect = 1 << 2, // This rect is unchanged by subtraction.
            corner_a_removed = 1 << 3,
            corner_b_removed = 1 << 4,
            part_of_left_edge_removed = 1 << 5,
            part_of_right_edge_removed = 1 << 6,
        };
        IMP_ENUM_FLAG_OPERATORS_IN_CLASS(Bits)

        Bits bits = Bits::invalid;

        vec<D,T> cur_pos{};

      public:
        // The minimal set of typedefs for the `std::bidirectional_iterator` concept.
        using value_type = vec<D,T>;
        using difference_type = std::ptrdiff_t;

        constexpr RectDiffIterator() {}

        explicit constexpr RectDiffIterator(rect_t new_rect, rect_t new_sub_rect, bool start_at_end = false)
            : rect(new_rect), sub_rect(new_sub_rect)
        {
            if (!rect.has_area())
            {
                // The rect is empty.
                bits |= Bits::empty_rect;
                return;
            }

            if (!rect.touches(sub_rect) || !sub_rect.has_area())
            {
                // Subtraction doesn't change the rect.
                cur_pos = start_at_end ? rect.b - 1 : rect.a;
                bits = Bits::intact_rect;
                return;
            }

            // Use separate variables for now, we don't always want to assign them to the member variables.
            bool corner_00_removed = sub_rect.contains(rect.a);
            bool corner_11_removed = sub_rect.contains(rect.b - 1);

            if (corner_00_removed && corner_11_removed)
            {
                // Subtraction destroyed the whole rect.
                bits |= Bits::empty_rect;
                return;
            }

            bool corner_01_removed = sub_rect.contains(value_type(rect.a.x, rect.b.y - 1));
            bool corner_10_removed = sub_rect.contains(value_type(rect.b.x - 1, rect.a.y));

            if (corner_10_removed && corner_11_removed)
            {
                // Right half trimmed.
                rect.b.x = sub_rect.a.x;
                cur_pos = start_at_end ? rect.b - 1 : rect.a;
                bits = bits = Bits::intact_rect;
                return;
            }

            if (corner_00_removed && corner_01_removed)
            {
                // Left half trimmed.
                rect.a.x = sub_rect.b.x;
                cur_pos = start_at_end ? rect.b - 1 : rect.a;
                bits = bits = Bits::intact_rect;
                return;
            }

            if (corner_01_removed && corner_11_removed)
            {
                // Bottom half trimmed.
                rect.b.y = sub_rect.a.y;
                cur_pos = start_at_end ? rect.b - 1 : rect.a;
                bits = bits = Bits::intact_rect;
                return;
            }

            if (corner_00_removed && corner_10_removed)
            {
                // Upper half trimmed.
                rect.a.y = sub_rect.b.y;
                cur_pos = start_at_end ? rect.b - 1 : rect.a;
                bits = bits = Bits::intact_rect;
                return;
            }

            if (corner_00_removed)
                bits |= Bits::corner_a_removed;
            if (corner_11_removed)
                bits |= Bits::corner_b_removed;

            if (sub_rect.a.x <= rect.a.x) // This simple check is enough, because the degenerate cases were handled above.
                bits |= Bits::part_of_left_edge_removed;
            if (sub_rect.b.x >= rect.b.x) // ^
                bits |= Bits::part_of_right_edge_removed;

            // Find the first point.
            if (start_at_end)
                operator--();
            else
                operator++();
        }

        [[nodiscard]] const RectDiffIterator &begin() const {return *this;}
        [[nodiscard]] std::default_sentinel_t end() const {return {};}

        [[nodiscard]] bool operator==(std::default_sentinel_t) const
        {
            return bool(bits & Bits::invalid);
        }
        [[nodiscard]] friend bool operator==(const RectDiffIterator &a, const RectDiffIterator &b)
        {
            if (bool(a.bits & Bits::invalid) && bool(b.bits & Bits::invalid))
                return true;
            return a.cur_pos == b.cur_pos;
        }

        // Returns the start or end point of the iteration.
        [[nodiscard]] value_type starting_point(bool backwards) const
        {
            if (backwards)
                return bool(bits & Bits::corner_b_removed) ? value_type(sub_rect.a.x - 1, rect.b.y - 1) : rect.b - 1;
            else
                return bool(bits & Bits::corner_a_removed) ? value_type(sub_rect.b.x, rect.a.y) : rect.a;
        }

        // Acts as ++ or --.
        void increment(bool backwards = false)
        {
            if (bool(bits & Bits::invalid))
            {
                // Find the first point.

                if (bool(bits & Bits::empty_rect))
                    return; // Always empty.

                cur_pos = starting_point(backwards);

                bits &= ~Bits::invalid;
                return;
            }

            if (!backwards)
            {
                cur_pos.x++;

                // Check overlap with subtracted rect, UNLESS it horizontally spans the whole source rect.
                if (!bool(bits & Bits::intact_rect) &&
                    (bits & (Bits::part_of_left_edge_removed | Bits::part_of_right_edge_removed)) != (Bits::part_of_left_edge_removed | Bits::part_of_right_edge_removed)
                )
                {
                    if (sub_rect.contains(cur_pos))
                    {
                        if (bool(bits & Bits::part_of_right_edge_removed))
                        {
                            // Skip immediately to the next line.

                            cur_pos.y++;
                            if (cur_pos.y >= rect.b.y)
                            {
                                bits |= Bits::invalid;
                                return;
                            }

                            cur_pos.x = rect.a.x;

                            if (bool(bits & Bits::part_of_left_edge_removed) && cur_pos.y >= sub_rect.a.y && cur_pos.y < sub_rect.b.y)
                                cur_pos.x = sub_rect.b.x;
                            return;
                        }
                        else
                        {
                            // Skip horizontally to the next free space.
                            cur_pos.x = sub_rect.b.x;
                            return;
                        }
                    }
                }

                // Check if we've reached the right edge, then go to the next line.
                if (cur_pos.x >= rect.b.x)
                {
                    cur_pos.y++;
                    if (cur_pos.y >= rect.b.y)
                    {
                        // This was the last line.
                        bits |= Bits::invalid;
                        return;
                    }

                    cur_pos.x = rect.a.x;

                    // If the point at the beginning of the new line is occupied...
                    if (bool(bits & Bits::part_of_left_edge_removed) && cur_pos.y >= sub_rect.a.y && cur_pos.y < sub_rect.b.y)
                    {
                        if (bool(bits & Bits::part_of_right_edge_removed))
                            cur_pos.y = sub_rect.b.y; // The whole line is occupied.
                        else
                            cur_pos.x = sub_rect.b.x; // The line is only partially occupied.
                    }
                }
            }
            else
            {
                // This is the inverse of the previous branch.

                // If we've reached the left edge, go to the previous line.
                if (cur_pos.x <= rect.a.x)
                {
                    if (cur_pos.y <= rect.a.y)
                    {
                        // This was the first line.
                        bits |= Bits::invalid;
                        return;
                    }

                    cur_pos.y--;
                    cur_pos.x = rect.b.x - 1;

                    // If the point at the end of the previous line is occupied...
                    if (bool(bits & Bits::part_of_right_edge_removed) && cur_pos.y >= sub_rect.a.y && cur_pos.y < sub_rect.b.y)
                    {
                        if (bool(bits & Bits::part_of_left_edge_removed))
                            cur_pos.y = sub_rect.a.y - 1; // The whole line is occupied.
                        else
                            cur_pos.x = sub_rect.a.x - 1; // The line is only partially occupied.
                    }

                    return;
                }

                cur_pos.x--;

                // Check overlap with subtracted rect, UNLESS it horizontally spans the whole source rect.
                if (!bool(bits & Bits::intact_rect) &&
                    (bits & (Bits::part_of_left_edge_removed | Bits::part_of_right_edge_removed)) != (Bits::part_of_left_edge_removed | Bits::part_of_right_edge_removed)
                )
                {
                    if (sub_rect.contains(cur_pos))
                    {
                        if (bool(bits & Bits::part_of_left_edge_removed))
                        {
                            // Skip immediately to the previous line.

                            if (cur_pos.y <= rect.a.y)
                            {
                                bits |= Bits::invalid;
                                return;
                            }
                            cur_pos.y--;

                            cur_pos.x = rect.b.x - 1;

                            if (bool(bits & Bits::part_of_right_edge_removed) && cur_pos.y >= sub_rect.a.y && cur_pos.y < sub_rect.b.y)
                                cur_pos.x = sub_rect.a.x - 1;
                            return;
                        }
                        else
                        {
                            // Skip horizontally to the next free space.
                            cur_pos.x = sub_rect.a.x - 1;
                            return;
                        }
                    }
                }
            }
        }

        [[nodiscard]] const value_type &operator*() const {return cur_pos;}

        RectDiffIterator &operator++()
        {
            increment(false);
            return *this;
        }
        RectDiffIterator &operator--()
        {
            increment(true);
            return *this;
        }
        RectDiffIterator operator++(int)
        {
            RectDiffIterator ret = *this;
            ++ret;
            return ret;
        }
        RectDiffIterator operator--(int)
        {
            RectDiffIterator ret = *this;
            --ret;
            return ret;
        }
    };
}
