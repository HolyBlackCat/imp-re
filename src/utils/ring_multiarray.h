#pragma once

#include "math/rect_diff_iteration.h"
#include "utils/multiarray.h"
#include "utils/mat.h"

namespace TileGrids
{
    // A simple ring buffer multidimensional array.
    // Maintains a capacity value that can be larger than size, but the unused elements are zeroed rather than destroyed, for simplicity.
    // Maintains a range of valid indices as a `rect2`, which can become arbitrarily large as the ring rotates.
    template <int D, typename T, std::signed_integral Index = std::ptrdiff_t>
    requires (D == 2) // Only 2D for now, because `RectDiffIterator` only supports 2D at this point.
    class RingMultiarray
    {
      public:
        using underlying_array_t = MultiArray<D, T, Index>;
        using type = underlying_array_t::type;
        using index_t = underlying_array_t::index_t;
        using index_vec_t = underlying_array_t::index_vec_t;
        using index_rect_t = underlying_array_t::index_rect_t;

      private:
        MultiArray<D, T, Index> underlying{};

        index_vec_t::rect_type bounds_rect;

      public:
        // Capacity grow/shrink factors.
        static constexpr index_t
            capacity_grow_num = 3, // Grow capacity by at least 1.5x.
            capacity_grow_den = 2, // ^
            capacity_shrink_margin_num = 1, // Shrink capacity by at least 0.5x.
            capacity_shrink_margin_den = 2, // ^ (Current capacity is multiplied by this amount, the new one must be not larger for the shink to happen.)
            capacity_shrink_num = 4, // When shrinking, the new capacity will be this larger than the new size.
            capacity_shrink_den = 3; // ^ (Will only shrink if `capacity_shrink_margin_{num,den}` above is satisfied.)

        constexpr RingMultiarray() {}

        constexpr RingMultiarray(index_vec_t new_capacity, index_rect_t new_bounds)
            : underlying(new_capacity), bounds_rect(new_bounds)
        {
            ASSERT(new_bounds.size()(all) <= new_capacity, "Can't set bounds larger than the capacity.");
        }

        // A simple constructor that sets the bounds to `0..n-1`, and the capacity to `n`.
        explicit constexpr RingMultiarray(index_vec_t size) : RingMultiarray(size,index_vec_t{}.rect_to(size)) {}

        [[nodiscard]] index_vec_t capacity() const {return underlying.size();}

        // The current min and max coordinates in the array. The difference between those is the size.
        // They can be positive or negative, possibly greater than the capacity (as long as the difference is less or equal than the capacity).
        [[nodiscard]] index_vec_t::rect_type bounds() const {return bounds_rect;}

        [[nodiscard]] index_vec_t size() const {return bounds().size();}

        [[nodiscard]] auto at(this auto &&self, index_vec_t pos) -> Meta::copy_cvref<decltype(self), type>
        {
            ASSERT(self.bounds().contains(pos), FMT("RingMultiarray index {} is out of bounds, {}.", fmt::streamed(pos), fmt::streamed(self.bounds())));
            return decltype(self)(self).underlying.at(mod_ex(pos, self.underlying.size()));
        }

        // Resize with automatic capacity management. (See `capacity_*` constants above.)
        void resize(index_rect_t new_bounds)
        {
            bool should_reallocate = false;

            index_vec_t new_capacity;
            for (int i = 0; i < D; i++)
            {
                index_t &out = Math::vec_elem(i, new_capacity);
                index_t cur = Math::vec_elem(i, capacity());
                index_t needed = Math::vec_elem(i, new_bounds.size());

                if (cur < needed)
                {
                    // Must increase capacity.
                    out = std::max(cur * capacity_grow_num / capacity_grow_den, needed);
                    should_reallocate = true;
                }
                else
                {
                    // Have enough capacity, try shrinking.
                    out = std::min(needed * capacity_shrink_num / capacity_shrink_den, cur);
                    if (out < cur * capacity_shrink_margin_num / capacity_shrink_margin_den)
                        should_reallocate = true; // Only reallocate if the shrink is significant enough.
                }
            }

            if (should_reallocate)
                resize_with_capacity(new_capacity, new_bounds);
            else
                resize_keeping_capacity(new_bounds);
        }

        // Sets the capacity. Asserts when trying to make it smaller than the size.
        // `bounds()` and `size()` stay unchanged.
        void change_capacity(index_vec_t new_capacity)
        {
            if (capacity() == new_capacity)
                return; // Already correct capacity.

            ASSERT(new_capacity(all) >= size(), "Can't make the capacity less than the size.");

            RingMultiarray ret(new_capacity);
            ret.bounds_rect = bounds_rect;

            for (index_vec_t pos : vector_range(bounds()))
                ret.at(pos) = std::move(at(pos));

            *this = std::move(ret);
        }

        // Resize while keeping capacity.
        void resize_keeping_capacity(index_rect_t new_bounds)
        {
            ASSERT(new_bounds.size()(all) <= capacity(), "Resizing a ring array beyond its capacity.");

            // Zero the old elements.
            for (index_vec_t pos : Math2::RectDiffIterator(bounds(), new_bounds))
                at(pos) = type{};

            // Assume the new elements are already zeroed.

            bounds_rect = new_bounds;
        }

        // Resize and set a custom capacity.
        void resize_with_capacity(index_vec_t new_capacity, index_rect_t new_bounds)
        {
            if (capacity() == new_capacity)
                resize_keeping_capacity(new_bounds);
            else
                *this = std::move(*this).resize_copy_with_capacity(new_capacity, new_bounds);
        }

        // Create a new array with a copy/move of some of the elements, with a specific capacity.
        [[nodiscard]] RingMultiarray resize_copy_with_capacity(this auto &&self, index_vec_t new_capacity, index_rect_t new_bounds)
        {
            ASSERT(new_bounds.size()(all) <= new_capacity, "Resizing a ring array beyond its new capacity.");

            RingMultiarray ret(new_capacity, new_bounds);
            for (index_vec_t pos : vector_range(self.bounds().intersect(new_bounds)))
                ret.at(pos) = decltype(self)(self).at(pos);

            // Assume the new elements are already zeroed.

            return ret;
        }
    };
}
