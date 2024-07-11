#pragma once

#include <type_traits>

// Declares an optional member variable. Example:
//   IMP_COND_MEMBER_VAR(condition, type) name;
// Adding an initializer is allowed, of course.
// If the condition contains commas, it must be parenthesized.
// The type must be valid even if the condition is false, unsure how to fix this.
#define IMP_COND_MEMBER_VAR(cond, ...) \
    struct DETAIL_IMP_COND_MEMBER_VAR_CAT(_disabled_member_,__LINE__) {}; \
    DETAIL_IMP_COND_MEMBER_VAR_NUA ::std::conditional_t<cond, __VA_ARGS__, DETAIL_IMP_COND_MEMBER_VAR_CAT(_disabled_member_,__LINE__)>

#ifdef _MSC_VER
#define DETAIL_IMP_COND_MEMBER_VAR_NUA [[msvc::no_unique_address]]
#else
#define DETAIL_IMP_COND_MEMBER_VAR_NUA [[no_unique_address]]
#endif

#define DETAIL_IMP_COND_MEMBER_VAR_CAT(x, y) DETAIL_IMP_COND_MEMBER_VAR_CAT_(x, y)
#define DETAIL_IMP_COND_MEMBER_VAR_CAT_(x, y) x##y
