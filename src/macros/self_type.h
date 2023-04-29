#pragma once

#include <type_traits>

// Macros to determine the type of the enclosing class.

namespace Macro::SelfType
{
    template <typename Key>
    struct Read
    {
        friend constexpr auto _adl_imp_SelfType(Read<Key>);
    };

    template <typename Key, typename Value>
    struct Write
    {
        friend constexpr auto _adl_imp_SelfType(Read<Key>) {return Value{};}
    };

    constexpr void _adl_imp_SelfType() {} // Dummy ADL target.

    template <typename T>
    using Type = std::remove_pointer_t<decltype(_adl_imp_SelfType(Read<T>{}))>;
}

// Creates a typedef named `self_`, pointing to the enclosing class.
// You can have several of those in a class, if the names are different.
#define IMP_SELF_TYPE(self_) \
    struct IMPL_IMP_SELF_TYPE_cat(_adl_imp_SelfTypeTag, self_) {}; \
    auto _adl_imp_SelfTypeHelper(IMPL_IMP_SELF_TYPE_cat(_adl_imp_SelfTypeTag, self_)) \
    -> decltype(void(::Macro::SelfType::Write<IMPL_IMP_SELF_TYPE_cat(_adl_imp_SelfTypeTag, self_), decltype(this)>{})); \
    using self_ = ::Macro::SelfType::Type<IMPL_IMP_SELF_TYPE_cat(_adl_imp_SelfTypeTag, self_)>;

#define IMPL_IMP_SELF_TYPE_cat(x, y) IMPL_IMP_SELF_TYPE_cat_(x, y)
#define IMPL_IMP_SELF_TYPE_cat_(x, y) x##y
