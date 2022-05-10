#pragma once

#include <cassert>
#include <coroutine>
#include <cstddef>
#include <memory>
#include <utility>

/* A minimalistic coroutine implementation.
How to use:
  Make a function that returns `Coroutine<R>`.
  Inside of this coroutine, you can:
  - Suspend:
    > if `R == void`, use `co_await std::suspend_always{};` or with any other argument.
    > if `R != void`, use `co_yield ...;`, where `...` must be convertible to `R &&` (which for `R == T &` turns into `T &`).
  - Finish, using `co_return;` or by flowing the end of the function.

  Using the handle `Coroutine<R>`, you can:
  - Run a single step, using `operator()`.
    It returns a pointer to the `co_yield`ed value, or null if the coroutine just finished or was already null.
    Except for `void` coroutines, where it returns a `bool` with `false` in place of null, and `true` in place of a non-zero pointer.
  - Check status using `operator bool`, which returns false if the coroutine has finished or is null.
  - Destroy the coroutine by destroying the handle.

  The handle is move-only, but you can make a copyable handle of type `Coroutine<R>::Shared` by calling `.shared()`.
  It has the same interface as the regular handle, but is copyable.
*/

template <typename R = void>
class Coroutine
{
    template <typename, typename...>
    friend class std::coroutine_traits;

    static constexpr auto ReturnTypeTraits() requires std::is_void_v<R>
    {
        struct Helper
        {
            using ptr = bool;
            using yield = std::nullptr_t;
        };
        return Helper{};
    }
    static constexpr auto ReturnTypeTraits()
    {
        struct Helper
        {
            using ptr = std::remove_reference_t<R> *;
            using yield = R &&;
        };
        return Helper{};
    }

    struct Promise
    {
        Coroutine *coro = nullptr;

        typename decltype(ReturnTypeTraits())::ptr return_val{};

        constexpr std::suspend_always initial_suspend() noexcept {return {};}
        constexpr std::suspend_never final_suspend() noexcept
        {
            coro->promise = nullptr;
            return {};
        }

        constexpr Coroutine get_return_object()
        {
            assert(!coro);
            Coroutine ret;
            ret.promise = this;
            coro = &ret;
            return ret;
        }

        constexpr void return_void() {}

        constexpr void unhandled_exception() {throw;}

        // Only void coroutines can `co_await`.
        template <typename T> requires std::is_void_v<R>
        T &&await_transform(T &&value)
        {
            return_val = true;
            return std::forward<T>(value);
        }

        using yield_type = typename decltype(ReturnTypeTraits())::yield;

        std::suspend_always yield_value(yield_type ref) requires(!std::is_void_v<R>)
        {
            return_val = &ref;
            return {};
        }
    };
    using Handle = std::coroutine_handle<Promise>;

    Promise *promise = nullptr;

    [[nodiscard]] Handle GetHandle()
    {
        if (promise)
            return Handle::from_promise(*promise);
        else
            return {};
    }

  public:
    using return_type = R;
    using return_type_opt = typename decltype(ReturnTypeTraits())::ptr;

    constexpr Coroutine() {}

    constexpr Coroutine(Coroutine &&other) noexcept : promise(std::exchange(other.promise, nullptr))
    {
        if (promise)
            promise->coro = this;
    }
    constexpr Coroutine &operator=(Coroutine other) noexcept
    {
        std::swap(promise, other.promise);
        if (promise) promise->coro = this;
        if (other.promise) other.promise->coro = &other;
        return *this;
    }

    ~Coroutine()
    {
        if (promise)
            GetHandle().destroy();
    }

    // Returns false if this object is null.
    // Finishing a coroutine automatically makes this return false.
    [[nodiscard]] constexpr explicit operator bool() const
    {
        return bool(promise);
    }

    // The return type is `std::remove_reference_t<R> *`, except for void coroutines returns `bool`.
    // Performs a single coroutine step.
    // If the coroutine finishes, returns null. Otherwise returns a pointer to the yielded value, or `true` for void coroutines.
    // If the coroutine throws, this also throws and zeroes the coroutine.
    // For null coroutines always returns null.
    [[nodiscard]] constexpr return_type_opt operator()()
    {
        if (!promise)
            return {};

        try
        {
            GetHandle().resume();
            if (promise)
                return promise->return_val;
            else
                return {}; // The coroutine just finished.
        }
        catch (...)
        {
            Coroutine() = std::move(*this); // Clean the current coroutine.
            throw;
        }
    }

    // Wraps a `std::shared_ptr<Coroutine>`.
    class Shared
    {
      public:
        std::shared_ptr<Coroutine> coroutine;

        [[nodiscard]] constexpr explicit operator bool() const
        {
            return coroutine && *coroutine;
        }

        [[nodiscard]] constexpr return_type_opt operator()()
        {
            if (!coroutine)
                return {};
            return_type_opt ret = (*coroutine)();
            if (!ret)
                coroutine = nullptr;
            return ret;
        }
    };

    // Moves this coroutine into a `shared_ptr`, returns a callable wrapper for it.
    [[nodiscard]] Shared share() &&
    {
        Shared ret;
        ret.coroutine = std::make_shared<Coroutine>(std::move(*this));
        return ret;
    }
};

template <typename R, typename ...P>
struct std::coroutine_traits<Coroutine<R>, P...>
{
    using promise_type = typename Coroutine<R>::Promise;
};
