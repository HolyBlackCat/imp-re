#pragma once

#include <type_traits>
#include <utility>

#include "macros/generated.h"

namespace Macro
{
    template <typename F> class WithExpr
    {
        F &&lambda;

      public:
        constexpr WithExpr(F &&lambda) : lambda(std::move(lambda)) {}

        template <typename P> friend decltype(auto) operator->*(P &&param, WithExpr &&expr)
        {
            auto copy = std::forward<P>(param); // We make a copy here, rather than by passing `param` by value, to enable NRVO.
            expr.lambda(copy);
            return copy;
        }
    };
}

// `adjust(object, ...)` returns a copy of `object`, modified by `...`, which is a comma-separated list of expressions like `.x += 10` or `(.x) + 10`.
// The latter is equivalent to `.x = _.x + 10`. You can use `_` to refer to the temporary object.
// Note that `() + 10` is also a valid expression, which modifies the whole temporary object. Normally it's unnecessary (you can remove `adjust()` completely and use `+ 10` directly),
// but it can be useful in some cases, e.g. if the operation returns a base class instance by value, and the derived class inherits `operator=` from the base.
// The version of the macro with the `_` suffix is for use at namespace scope. It uses a lambda without the default capture, since Clang refuses to handle those at namespace scope.
#define adjust(...) ADJUST(__VA_ARGS__)
#define adjust_(...) ADJUST_(__VA_ARGS__)
// `object with(...)` expands to `adjust(object, ...)`, but without the nice code completion in the `...` argument.
// `with(...)` has the same priority as `->*`.
#define with(...) WITH(__VA_ARGS__)
#define with_(...) WITH_(__VA_ARGS__)

#define ADJUST(object, ...) IMPL_ADJUST(&, object, __VA_ARGS__)
#define ADJUST_(object, ...) IMPL_ADJUST(, object, __VA_ARGS__)
#define WITH(...) IMPL_WITH(&, __VA_ARGS__)
#define WITH_(...) IMPL_WITH(, __VA_ARGS__)

#define IMPL_ADJUST(capture, object, ...) (void(), [capture]{ auto _ = object; MA_VA_FOR_EACH(,IMPL_ADJUST_STEP,__VA_ARGS__) return _; }())
#define IMPL_ADJUST_STEP(data, i, expr) _ IMPL_ADJUST_END( IMPL_ADJUST_STEP_ expr );
#define IMPL_ADJUST_STEP_(...) IMPL_ADJUST_STEP_ __VA_ARGS__ = _ __VA_ARGS__
#define IMPL_ADJUST_END(...) IMPL_ADJUST_END_(__VA_ARGS__)
#define IMPL_ADJUST_END_(...) IMPL_ADJUST_END_##__VA_ARGS__
#define IMPL_ADJUST_END_IMPL_ADJUST_STEP_

#define IMPL_WITH(capture, ...) ->* ::Macro::WithExpr([capture](auto &_) { MA_VA_FOR_EACH(,IMPL_ADJUST_STEP,__VA_ARGS__) })
