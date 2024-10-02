#pragma once

#include <concepts>
#include <cstddef>
#include <type_traits>

#include "macros/self_type.h"
#include "meta/common.h"
#include "meta/stateful.h"

namespace Macro::DetectBases
{
    namespace impl
    {
        // The tag for stateful lists.
        template <typename T, template <typename/*base*/, typename/*derived*/> typename Pred>
        struct BasesTag {};

        template <typename Tag, typename T, template <typename/*base*/, typename/*derived*/> typename Pred>
        constexpr void _adl_imp_DetectBase(void *) {} // A dummy ADL target.

        // We need a struct to conveniently `friend` it.
        struct Helper
        {
            Helper() = delete;
            ~Helper() = delete;

            template <typename Tag, typename T, template <typename/*base*/, typename/*derived*/> typename Pred>
            static auto Detect() -> decltype(void(_adl_imp_DetectBase<Tag, T, Pred>((T *)nullptr))) {}
        };
    }

    // Accept all bases.
    template <typename Base, typename Derived> struct AnyBases : std::is_base_of<Base, Derived> {};
    // Accept only unambiguous accessible bases.
    template <typename Base, typename Derived> struct GoodBases : std::bool_constant<std::derived_from<Derived, Base>> {};
    // Fail with a hard error if any base is ambiguous or inaccessible.
    template <typename Base, typename Derived> struct GoodBasesChecked : GoodBases<Base, Derived>
    {
        static_assert(GoodBases<Base, Derived>::value == AnyBases<Base, Derived>::value, "Ambiguous or inaccessible base.");
    };

    // Lists all bases of `T` (including `T` itself) that contain the macro `IMP_DETECTABLE_BASE(Tag)`.
    // `Tag` is the tag given to `IMP_DETECTABLE_BASE`.
    // `T` is the derived class we're inspecting.
    // `Pred` should either be `Is` or `std::is_base_of` (which allows ambiguous or inaccessible bases).
    template <typename Tag, typename T, template <typename/*base*/, typename/*derived*/> typename Pred>
    using BasesAndSelf = decltype(impl::Helper::Detect<Tag, T, Pred>(), Meta::Stateful::List::Elems<impl::BasesTag<T, Pred>>{});
}

// Place this in a class to make it detectable as a base.
// `tag_` is a tag struct. You can have multiple macros in the same class, if the tags are different.
#define IMP_DETECTABLE_BASE(tag_) \
    IMP_DETECTABLE_BASE_SELFTYPE(tag_, IMPL_IMP_DETECTABLE_BASE_cat(_adl_imp_BaseSelfType, __COUNTER__))

// A version of `IMP_DETECTABLE_BASE` that lets you specify the name of the "own type" typedef.
// `IMP_DETECTABLE_BASE` generates a name automatically.
#define IMP_DETECTABLE_BASE_SELFTYPE(tag_, self_type_name_) \
    friend ::Macro::DetectBases::impl::Helper; \
    /* Figure out the own type. */\
    IMP_SELF_TYPE(self_type_name_) \
    template < \
        /* The tag. */\
        typename _adl_imp_Tag, \
        /* The derived type we're finding the bases for. */\
        typename _adl_imp_Derived, \
        /* If true, use `std::is_base_of` instead of `std::derived_from` . */\
        template <typename, typename> typename _adl_imp_Pred, \
        /* Stop if the tag is wrong. */\
        ::std::enable_if_t<std::is_same_v<_adl_imp_Tag, tag_>, std::nullptr_t> = nullptr, \
        /* Check the predicate. */\
        ::std::enable_if_t<_adl_imp_Pred<self_type_name_, _adl_imp_Derived>::value, ::std::nullptr_t> = nullptr, \
        /* Write the base to the list. */\
        /* Note `decltype`, unsure why it's necessary. */\
        typename = decltype(::Meta::Stateful::List::PushBack<::Macro::DetectBases::impl::BasesTag<_adl_imp_Derived, _adl_imp_Pred>, self_type_name_>{}), \
        /* Finally, disable this overload. */\
        ::std::enable_if_t<::Meta::always_false<_adl_imp_Derived>, ::std::nullptr_t> = nullptr \
    > \
    friend constexpr void _adl_imp_DetectBase(void *) {}


#define IMPL_IMP_DETECTABLE_BASE_cat(x, y) IMPL_IMP_DETECTABLE_BASE_cat_(x, y)
#define IMPL_IMP_DETECTABLE_BASE_cat_(x, y) x##y
