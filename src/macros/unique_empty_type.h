#pragma once

#include "meta/const_string.h"

namespace Macros
{
    namespace impl
    {
        template <Meta::ConstString LocFile, int LocLine, typename ...Tags>
        struct UniqueEmpty {};
    }

    // Expands to a unique empty type.
    // The type is unique per source location, plus additionally per the tag types in `...`.
    // `...` can be empty. If you use this in a template and don't pass the template parameters to `...`, you'll get the same type in every instantiation.
    #define IMP_UNIQUE_EMPTY_TYPE(/*typename*/...) ::Macros::impl::UniqueEmpty<__FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__>
}
