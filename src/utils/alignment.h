#pragma once

#include <bit>
#include <cstddef>

namespace Storage
{
    template <std::size_t A> inline constexpr bool is_valid_alignment_v = std::has_single_bit(A);

    enum class AlignDir {up, down};

    // Aligns `value` up to `Alignment`.
    // Aligns up by default.
    template <std::size_t Alignment, AlignDir Dir = AlignDir::up>
    requires is_valid_alignment_v<Alignment>
    [[nodiscard]] constexpr std::size_t Align(std::size_t value)
    {
        if constexpr (Alignment == 1)
        {
            return value;
        }
        else
        {
            constexpr std::size_t p = std::countr_zero(Alignment);
            return (value + (Dir == AlignDir::up ? Alignment - 1 : 0)) & (std::size_t(-1) << p);
        }
    }

    // Aligns `ptr` up to `Alignment`. `ptr` must point to `void` or a single-byte arithmetic type.
    // Aligns up by default.
    template <std::size_t Alignment, typename T>
    requires std::is_void_v<std::remove_cv_t<T>> || (std::is_integral_v<std::remove_cv_t<T>> && sizeof(T) == 1)
    [[nodiscard]] T *Align(T *ptr)
    {
        static_assert(sizeof(std::size_t) >= sizeof(void *)); // If this somehow fires, the integral overload of `Align` must be rewritten to use `uintptr_t`.
        return reinterpret_cast<T *>(Align<Alignment>(reinterpret_cast<std::size_t>(ptr)));
    }
}
