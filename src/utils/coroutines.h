#pragma once

#include <cassert>
#include <coroutine>
#include <cstddef>
#include <memory>
#include <optional>
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

    // Used below to conditionally disable some overloads.
    struct NoType {};

    static constexpr auto ReturnTypeTraits() requires std::is_void_v<R>
    {
        struct Helper
        {
            using return_type_opt = bool; // Represents an optional `R`, returned when resuming the coroutine.
            using return_storage = NoType; // Used internally to store the return value.
            using yield = NoType; // The parameter of `co_yield`.
            using move_yield = NoType; // The parameter of the moving overload of `co_yield`.
        };
        return Helper{};
    }
    static constexpr auto ReturnTypeTraits() requires std::is_reference_v<R>
    {
        struct Helper
        {
            using return_type_opt = std::remove_reference_t<R> *;
            using return_storage = std::remove_reference_t<R> *;
            using yield = R;
            using move_yield = NoType;
        };
        return Helper{};
    }
    static constexpr auto ReturnTypeTraits()
    {
        struct Helper
        {
            using return_type_opt = std::optional<R>;
            using return_storage = std::optional<R> *;
            using yield = const R &;
            using move_yield = R &&;
        };
        return Helper{};
    }

    struct Promise
    {
        Coroutine *coro = nullptr;

        // If we return by value, this points to `std::optional<R>` where we should construct the return value.
        // If we return by reference, we write the address of the reference here.
        // Lastly, if we return void, this is a bool, where `true` indicates a successful return.
        [[no_unique_address]] typename decltype(ReturnTypeTraits())::return_storage return_val{};

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
            return std::forward<T>(value);
        }

        using yield_type = typename decltype(ReturnTypeTraits())::yield;
        using move_yield_type = typename decltype(ReturnTypeTraits())::move_yield;

        std::suspend_always yield_value(yield_type ref) requires(!std::is_same_v<yield_type, NoType> && (std::is_reference_v<R> || std::is_copy_constructible_v<R>))
        {
            if constexpr (std::is_reference_v<R>)
                return_val = &ref;
            else
                return_val->emplace(ref);
            return {};
        }
        std::suspend_always yield_value(move_yield_type ref) requires(!std::is_same_v<move_yield_type, NoType> && std::is_move_constructible_v<R>)
        {
            static_assert(!std::is_reference_v<R>, "Internal error.");
            return_val->emplace(std::move(ref));
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
    // The return type as specified in the template argument.
    using return_type = R;
    // The return type of `operator()`.
    using return_type_opt = typename decltype(ReturnTypeTraits())::return_type_opt;

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

    // Performs a single coroutine step.
    // If the coroutine finishes, returns null. Otherwise returns the yielded value.
    // If the coroutine throws, this also throws and zeroes the coroutine.
    // For null coroutines always returns null.
    // If the coroutine returns `void`, the return type is bool (true if the coroutine hasn't finished yet).
    // If the coroutine returns a reference, the return type is a pointer to the same type.
    // Otherwise, the return type is `std::optional<R>`.
    [[nodiscard]] constexpr return_type_opt operator()()
    {
        if (!promise)
            return {};

        try
        {
            if constexpr (std::is_void_v<R>)
            {
                GetHandle().resume();
                if (promise)
                    return true;
            }
            else if constexpr (std::is_reference_v<R>)
            {
                GetHandle().resume();
                if (promise)
                    return promise->return_val;
            }
            else
            {
                std::optional<R> ret;
                promise->return_val = &ret;
                GetHandle().resume();
                if (promise)
                {
                    assert(ret.has_value());
                    return ret;
                }
            }

            return {}; // The coroutine is done.
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


/* Some test cases:
Coroutine<> c1 = []() -> Coroutine<>
{
    std::cout << "1\n";
    co_await std::suspend_always{};
    std::cout << "2\n";
    co_await std::suspend_always{};
    std::cout << "3\n";
    co_return;
}();

while (auto x = c1())
    std::cout << "---\n";
std::cout << '\n';


Coroutine<int> c2 = []() -> Coroutine<int>
{
    int x;
    std::cout << "1\n";
    co_yield x = 10;
    std::cout << "2\n";
    co_yield 20;
    std::cout << "3\n";
    co_return;
}();

while (auto x = c2())
    std::cout << "--- " << x.value() << '\n';
std::cout << '\n';

Coroutine<int &> c3 = []() -> Coroutine<int &>
{
    int x;
    std::cout << "1\n";
    co_yield x = 10;
    std::cout << "2\n";
    co_yield x = 20;
    std::cout << "3\n";
    co_return;
}();

while (auto x = c3())
    std::cout << "--- " << *x << '\n';
std::cout << '\n';

Coroutine<int &&> c4 = []() -> Coroutine<int &&>
{
    std::cout << "1\n";
    co_yield 10;
    std::cout << "2\n";
    co_yield 20;
    std::cout << "3\n";
    co_return;
}();

while (auto x = c4())
    std::cout << "--- " << *x << '\n';
std::cout << '\n';

Coroutine<std::unique_ptr<int>> c5 = []() -> Coroutine<std::unique_ptr<int>>
{
    std::cout << "1\n";
    co_yield std::make_unique<int>(10);
    std::cout << "2\n";
    co_yield std::make_unique<int>(20);
    std::cout << "3\n";
    co_return;
}();

while (auto x = c5())
    std::cout << "--- " << **x << '\n';
std::cout << '\n';
*/
