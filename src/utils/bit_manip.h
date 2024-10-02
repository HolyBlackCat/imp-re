#pragma once

#include "meta/common.h"

#include <type_traits>

namespace BitManip
{
    // Is `T` a valid bit mask type? Either an integer or a enum with overloaded `&` and `|`.
    template <typename T>
    concept BitMaskType =
        std::is_integral_v<T> ||
        (
            std::is_enum_v<T> &&
            // Intentionally not checking all the operations. Getting a hard error if they're missing is fine.
            std::is_same_v<decltype(std::declval<T>() & std::declval<T>()), T> &&
            std::is_same_v<decltype(std::declval<T>() | std::declval<T>()), T>
        );

    // Converts a bit mask type to its unsigned integral counterpart.
    template <BitMaskType T>
    using UnderlyingMaskType = std::make_unsigned_t<std::underlying_type_t<T>>;

    // Makes a number unsigned and converts enums to their underlying types.
    template <BitMaskType T>
    [[nodiscard]] constexpr UnderlyingMaskType<T> ToUnderlyingMask(T value)
    {
        return UnderlyingMaskType<T>(value);
    }

    // Like `std::has_single_bit()`, but supports more types.
    template <BitMaskType T>
    [[nodiscard]] constexpr bool HasSingleBit(T value)
    {
        return std::has_single_bit(ToUnderlyingMask(value));
    }

    // A function parameter that can only receive a single-bit constexpr mask.
    template <BitMaskType T>
    struct ConstSingleBit
    {
        const T value{};
        consteval ConstSingleBit(T value)
        {
            if (!HasSingleBit(value))
                throw "Expected a single bit.";
        }
        [[nodiscard]] constexpr T operator+() const {return value;}
    };

    // ---

    enum class GetKind
    {
        any, // True if any of the bits are set.
        all, // True if all of the bits are set (or if the required bits are zero).
        const_one, // Only accept a compile-time constant holding a single bit.
        tristate, // Return a tristate enum (zero/one/maybe). If the required bits are zero, returns zero.
    };
    using enum GetKind;

    enum class TriState
    {
        zero = 0,
        one = 1,
        mixed = -1,
    };
    using enum TriState;

    template <GetKind Kind, BitMaskType T>
    using GetParam = std::conditional_t<Kind == GetKind::const_one, ConstSingleBit<T>, T>;
    template <GetKind Kind>
    using GetResult = std::conditional_t<Kind == GetKind::tristate, TriState, bool>;

    // Bit getters:

    // `value` has `bit` set. The bit is known at compile time to only be a single bit.
    template <GetKind Kind = GetKind::const_one, Meta::deduce..., BitMaskType T>
    [[nodiscard]] constexpr GetResult<Kind> GetBits(T value, GetParam<Kind, T> bits)
    {
        if constexpr (Kind == GetKind::any)
            return bool(value & bits);
        else if constexpr (Kind == GetKind::all)
            return bool((value & bits) == bits);
        else if constexpr (Kind == GetKind::const_one)
            return bool(value & bits.value);
        else if constexpr (Kind == GetKind::tristate)
            return (value & bits) == T{} ? TriState::zero : (value & bits) == bits ? TriState::one : TriState::mixed;
        else
            static_assert(Meta::always_false<Meta::value_tag<Kind>>, "Invalid GetKind enum.");
    }

    // Bit setters:

    // Sets or unsets `bits` in `value`.
    template <Meta::deduce..., BitMaskType T>
    constexpr void SetBits(T &value, T bits, bool set = true) {if (set) value |= bits; else value &= ~bits;}
    // Sets or unsets `bits` in `value`, returning a copy and leaving the original unchanged.
    template <Meta::deduce..., BitMaskType T>
    [[nodiscard]] constexpr T SetBitsCopy(T value, T bits, bool set = true) {return set ? value | bits : value & ~bits;}

    // Combined get/set property:

    // Don't use directly, prefer `BitProperty`.
    template <BitMaskType T, GetKind Kind, bool IsConst = false>
    class BitPropertyType
    {
        Meta::maybe_const<IsConst, T> &target;
        T bits;

      public:
        constexpr BitPropertyType(Meta::maybe_const<IsConst, T> &target, T bits) : target(target), bits(bits) {}

        [[nodiscard]] constexpr GetResult<Kind> get() const {return GetBits<Kind>(target, bits);}
        [[nodiscard]] explicit constexpr operator GetResult<Kind>() const {return get();}

        constexpr void set(bool value = true) requires (!IsConst) {SetBits(target, bits, value);}
        BitPropertyType &operator=(bool value) requires (!IsConst) {set(value); return *this;}
    };
    // Return those from cvref-templated methods. Example:
    // `auto foo(this auto &&self) -> AnyBitsProperty<decltype(self), MyMaskType> {return {mask, bit};}`
    template <typename Self, BitMaskType T, GetKind Kind = GetKind::const_one>
    using BitProperty = BitPropertyType<T, Kind, std::is_const_v<std::remove_reference_t<Self>>>;
}
