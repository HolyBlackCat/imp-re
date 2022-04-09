#pragma once

#include <exception>
#include <utility>

namespace Macro
{
    template <typename T>
    class ScopeGuard
    {
        T func;
      public:
        ScopeGuard(T &&func) : func(std::move(func)) {}
        ScopeGuard(const ScopeGuard &) = delete;
        ScopeGuard &operator=(const ScopeGuard &) = delete;
        ~ScopeGuard() {func();}
    };

    template <typename T>
    class ScopeGuardFail
    {
        T func;
        int exc = std::uncaught_exceptions();
      public:
        ScopeGuardFail(T &&func) : func(std::move(func)) {}
        ScopeGuardFail(const ScopeGuardFail &) = delete;
        ScopeGuardFail &operator=(const ScopeGuardFail &) = delete;
        ~ScopeGuardFail()
        {
            if (std::uncaught_exceptions() > exc)
                func();
        }
    };

    template <typename T>
    class ScopeGuardSuccess
    {
        T func;
        int exc = std::uncaught_exceptions();
      public:
        ScopeGuardSuccess(T &&func) : func(std::move(func)) {}
        ScopeGuardSuccess(const ScopeGuardSuccess &) = delete;
        ScopeGuardSuccess &operator=(const ScopeGuardSuccess &) = delete;
        ~ScopeGuardSuccess() noexcept(false)
        {
            if (std::uncaught_exceptions() <= exc)
                func();
        }
    };
}

#define FINALLY_impl_cat(a, b) FINALLY_impl_cat_(a, b)
#define FINALLY_impl_cat_(a, b) a##b

#define FINALLY \
    ::Macro::ScopeGuard FINALLY_impl_cat(_finally_object_,__LINE__) = [&]() -> void

#define FINALLY_ON_THROW \
    ::Macro::ScopeGuardFail FINALLY_impl_cat(_finally_object_,__LINE__) = [&]() -> void

#define FINALLY_ON_SUCCESS \
    ::Macro::ScopeGuardSuccess FINALLY_impl_cat(_finally_object_,__LINE__) = [&]() -> void
