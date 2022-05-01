#pragma once

#include <type_traits>

#include "program/compiler.h"

// Synthesizes operators for a enum of flags: `&`, `|`, and `~`.
#define IMP_ENUM_FLAG_OPERATORS(name_) IMP_ENUM_FLAG_OPERATORS_CUSTOM(static, name_)

// Same, but works at class scope.
#define IMP_ENUM_FLAG_OPERATORS_IN_CLASS(name_) IMP_ENUM_FLAG_OPERATORS_CUSTOM(friend, name_)

// Same, but lets you specify a custom decl-specifier-seq.
#define IMP_ENUM_FLAG_OPERATORS_CUSTOM(prefix_, name_) \
    [[nodiscard, maybe_unused]] IMP_ALWAYS_INLINE prefix_ name_ operator&(name_ a, name_ b) {return name_(::std::underlying_type_t<name_>(a) & ::std::underlying_type_t<name_>(b));} \
    [[nodiscard, maybe_unused]] IMP_ALWAYS_INLINE prefix_ name_ operator|(name_ a, name_ b) {return name_(::std::underlying_type_t<name_>(a) | ::std::underlying_type_t<name_>(b));} \
    [[nodiscard, maybe_unused]] IMP_ALWAYS_INLINE prefix_ name_ operator~(name_ a) {return name_(~::std::underlying_type_t<name_>(a));} \
    [[maybe_unused]] IMP_ALWAYS_INLINE prefix_ name_ &operator&=(name_ &a, name_ b) {return a = a & b;} \
    [[maybe_unused]] IMP_ALWAYS_INLINE prefix_ name_ &operator|=(name_ &a, name_ b) {return a = a | b;}
