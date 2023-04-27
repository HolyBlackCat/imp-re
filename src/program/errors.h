#pragma once

#include <stdexcept>
#include <string_view>
#include <string>

#include "program/platform.h"
#include "strings/format.h"

namespace Program
{
    // `func` is `void func(const char *message)`.
    // Calls `func` for `e.what()` and for every exception nested in it.
    template <typename F>
    inline void ExceptionToString(const std::exception &e, F &&func)
    {
        func(e.what());
        try
        {
            std::rethrow_if_nested(e);
        }
        catch (const std::exception &next)
        {
            ExceptionToString(next, std::forward<F>(func));
        }
        catch (...)
        {
            func("Unknown exception.");
        }
    }

    [[noreturn]] void HardError(const std::string &message);

    void SetErrorHandlers(bool replace_even_if_already_set = false);


    namespace impl
    {
        // An assertion function.
        // Making it constexpr allows us using it in compile-time contexts (as long as the condition is true, which is exactly the point).
        inline constexpr void Assert(const char *context, const char *function, const bool &condition, std::string_view message_or_expr, const char *expr_or_nothing = nullptr)
        {
            if (condition)
                return;

            if (expr_or_nothing)
            {
                // User specified a custom message.
                HardError(STR("Assertion failed!\n   at   ", (context), "\n   in   ", (function), "\nMessage:\n   ", (message_or_expr)));
            }
            else
            {
                // User didn't specify a message.
                HardError(STR("Assertion failed!\n   at   ", (context), "\n   in   ", (function), "\nExpression:\n   ", (message_or_expr)));
            }
        }

        // A template overload that allows using explicitly-but-not-implicitly-convertible-to-bool expressions as conditions.
        // Passing arrays is banned for obvious reasons, but not in SFINAE-friendly way to avoid falling back to the `bool` overload.
        template <typename T>
        constexpr void Assert(const char *context, const char *function, const T &condition, std::string_view message_or_expr, const char *expr_or_nothing = nullptr)
        {
            static_assert(!std::is_array_v<T>, "The first argument of `ASSERT()` is a boolean, but an array is always truthy.");
            return Assert(context, function, bool(condition), message_or_expr, expr_or_nothing);
        }
    }
}


// An assertion macro that always works, even in release builds.
// Can be called either as `ASSERT_ALWAYS(bool)` or `ASSERT_ALWAYS(bool, std::string_view)`.
// In the first case, the expression is printed in the message, otherwise the custom message is printed.
#define ASSERT_ALWAYS(...) ASSERT_ALWAYS_impl(__LINE__, __VA_ARGS__)
#define ASSERT_ALWAYS_impl(line, ...) ASSERT_ALWAYS_impl_low(line, __VA_ARGS__)
#define ASSERT_ALWAYS_impl_low(line, ...) ::Program::impl::Assert(__FILE__ ":" #line, __PRETTY_FUNCTION__, __VA_ARGS__, #__VA_ARGS__)

// An assertion macro that only works in debug builds. (But you can redefine it if necessary.)
#if IMP_PLATFORM_IS(prod)
#  define ASSERT(...) void(0)
#else
#  define ASSERT(...) ASSERT_ALWAYS(__VA_ARGS__)
#endif
