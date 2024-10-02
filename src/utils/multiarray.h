#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <type_traits>
#include <vector>
#include <utility>

#include "meta/common.h"
#include "meta/lists.h"
#include "program/errors.h"
#include "strings/format.h"
#include "utils/mat.h"


template <int D, typename T, std::signed_integral Index = std::ptrdiff_t>
class MultiArray
{
  public:
    static constexpr int dimensions = D;
    static_assert(dimensions >= 2, "Arrays with less than 2 dimensions are not supported.");
    static_assert(dimensions <= 4, "Arrays with more than 4 dimensions are not supported.");

    using type = T;
    using index_t = Index;
    using index_vec_t = vec<D, index_t>;
    using index_rect_t = vec<D, index_t>::rect_type;

    struct ReflHelper; // Our reflection metadata uses this to access private fields.

  private:
    index_vec_t size_vec{};
    std::vector<type> storage;

  public:
    constexpr MultiArray() {}

    explicit MultiArray(index_vec_t size_vec) : size_vec(size_vec), storage(size_vec.prod())
    {
        ASSERT(size_vec.min() >= 0, "Invalid multiarray size.");
        if (size_vec(any) <= 0)
            size_vec = {};
    }
    MultiArray(index_vec_t size_vec, const T &init) : size_vec(size_vec), storage(size_vec.prod(), init)
    {
        ASSERT(size_vec.min() >= 0, "Invalid multiarray size.");
        if (size_vec(any) <= 0)
            size_vec = {};
    }
    template <typename A, A ...I>
    MultiArray(Meta::value_list<I...>, const std::array<type, index_vec_t(I...).prod()> &data) : size_vec(I...), storage(data.begin(), data.end())
    {
        static_assert(std::is_integral_v<A>, "Indices must be integral.");
        static_assert(((I >= 0) && ...), "Invalid multiarray size.");
        if (size_vec(any) <= 0)
            size_vec = {};
    }

    [[nodiscard]] friend bool operator==(const MultiArray &a, const MultiArray &b)
    {
        return a.size_vec == b.size_vec && a.storage == b.storage;
    }

    [[nodiscard]] index_vec_t size() const
    {
        return size_vec;
    }

    [[nodiscard]] index_rect_t bounds() const
    {
        return index_vec_t{}.rect_size(size_vec);
    }

    [[nodiscard]] bool pos_in_range(index_vec_t pos) const
    {
        return bounds().contains(pos);
    }

    // Assert on failure.
    [[nodiscard]] auto at(this auto &&self, index_vec_t pos) -> Meta::copy_cvref<decltype(self), type>
    {
        ASSERT(self.pos_in_range(pos), FMT("Multiarray indices out of range. Indices are {} but the array size is {}.", pos, self.size_vec));

        index_t index = 0;
        index_t factor = 1;

        for (int i = 0; i < dimensions; i++)
        {
            index += factor * pos[i];
            factor *= self.size_vec[i];
        }

        return static_cast<Meta::copy_cvref<decltype(self), type>>(self.storage[index]);
    }
    [[nodiscard]] auto at_or_throw(this auto &&self, index_vec_t pos) -> Meta::copy_cvref<decltype(self), type>
    {
        if (!self.pos_in_range(pos))
            throw std::runtime_error(FMT("Multiarray index {} is out of range. The array size is {}.", pos, self.size_vec));
        return decltype(self)(self).at(pos);
    }
    [[nodiscard]] auto at_or_hard_error(this auto &&self, index_vec_t pos) -> Meta::copy_cvref<decltype(self), type>
    {
        if (!self.pos_in_range(pos))
            Program::HardError(FMT("Multiarray index {} is out of range. The array size is {}.", pos, self.size_vec));
        return decltype(self)(self).at(pos);
    }
    [[nodiscard]] auto at_clamped(this auto &&self, index_vec_t pos) -> Meta::copy_cvref<decltype(self), type>
    {
        clamp_var(pos, 0, self.size_vec-1);
        return decltype(self)(self).at(pos);
    }
    [[nodiscard]] type get_or_zero(this auto &&self, index_vec_t pos)
    {
        if (!self.pos_in_range(pos))
            return {};
        return decltype(self)(self).at(pos);
    }
    void try_set(index_vec_t pos, const type &obj)
    {
        if (!pos_in_range(pos))
            return;
        at(pos) = obj;
    }
    void try_set(index_vec_t pos, type &&obj)
    {
        if (!pos_in_range(pos))
            return;
        at(pos) = std::move(obj);
    }

    [[nodiscard]] index_t element_count() const
    {
        return storage.size();
    }
    [[nodiscard]] type *elements()
    {
        return storage.data();
    }
    [[nodiscard]] const type *elements() const
    {
        return storage.data();
    }

    // Resizes the array and/or offsets it by the specified amount.
    // Any out-of-range elements are destroyed.
    void resize(index_vec_t new_size, index_vec_t offset = {})
    {
        if (new_size == size_vec && offset == 0)
            return;
        *this = std::move(*this).resize_copy(new_size, offset);
    }

    // Resizes a copy of the array and/or offsets it by the specified amount.
    // Any out-of-range elements are destroyed.
    [[nodiscard]] MultiArray resize_copy(this auto &&self, index_vec_t new_size, index_vec_t offset = {})
    {
        if (new_size(any) == 0)
            return {}; // Target is empty, stop early.

        if (new_size == self.size_vec && offset == 0)
            return decltype(self)(self); // No changes are needed.

        MultiArray ret(new_size);
        new_size = ret.size(); // The constructor sanitizes the size.

        if (self.size_vec(any) == 0)
            return ret; // Source is empty, return zeroed array.

        index_vec_t source_start = clamp_min(-offset, 0);
        index_vec_t source_end = clamp_max(new_size - offset, self.size_vec);

        for (index_vec_t pos : source_start <= vector_range < source_end)
            ret.at_or_hard_error(pos + offset) = decltype(self)(self).at(pos);

        return ret;
    }
};

template <typename T, typename Index = std::ptrdiff_t> using Array2D = MultiArray<2, T, Index>;
template <typename T, typename Index = std::ptrdiff_t> using Array3D = MultiArray<3, T, Index>;
template <typename T, typename Index = std::ptrdiff_t> using Array4D = MultiArray<4, T, Index>;
