#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <vector>
#include <utility>

#include "program/errors.h"
#include "utils/alignment.h"

namespace Storage
{
    // Whether the pool can allocate blocks aligned to `A`.
    // We don't support overaligned types.
    template <std::size_t A>
    concept MonotonicPoolSupportedAlignment = is_valid_alignment_v<A> && A <= __STDCPP_DEFAULT_NEW_ALIGNMENT__;

    // Whether `T` can be allocated in the pool.
    // We don't support overaligned types.
    // We also don't support non-trivially-destructible types, since we don't track the stored objects and can't destroy them correctly.
    template <typename T>
    concept MonotonicPoolSupportedType = MonotonicPoolSupportedAlignment<alignof(T)> && std::is_trivially_destructible_v<T>;

    // Manages a memory pool.
    // Can only remove all objects at once, but maintains some persistent memory even after that.
    class MonotonicPool
    {
        struct State
        {
            // The current pool we're allocating from.
            std::unique_ptr<std::uint8_t[]> pool;
            // How much memory `pool` points to.
            std::size_t pool_size = 0;
            // The current offset into `pool`.
            std::size_t pool_pos = 0;

            // Older smaller pools. We don't allocate new objects in those, but keep them until the next `DestroyContent()`.
            std::vector<std::unique_ptr<std::uint8_t[]>> old_pools;
            // How much memory is currently sitting in `old_pools`.
            std::size_t old_pools_combined_size = 0;
        };
        State state{};

      public:
        // Creates an empty pool. You can still allocate objects in it, but it will start growing from a tiny size.
        MonotonicPool() {}

        // Creates a pool of the specified size.
        explicit MonotonicPool(std::size_t initial_size)
        {
            state.pool = std::make_unique_for_overwrite<std::uint8_t[]>(initial_size);
            state.pool_size = initial_size;
        }

        MonotonicPool(MonotonicPool &&other) noexcept : state(std::exchange(other.state, {})) {}
        MonotonicPool &operator=(MonotonicPool other) noexcept
        {
            std::swap(state, other.state);
            return *this;
        }

        // Returns true if the pool has any objects allocated in it,
        // or false if it's freshly constructed or had `DestroyContent()` just called on it.
        [[nodiscard]] constexpr bool HasContent() const
        {
            return state.pool_pos > 0 || state.old_pools_combined_size > 0;
        }

        // The current persistent pool capacity that will be kept through `DestroyContent()` calls.
        // For true memory consumption, see `CurrentMemoryUsage()`.
        [[nodiscard]] constexpr std::size_t PermanentCapacity() const
        {
            return state.pool_size;
        }

        // The current amount of memory occupied with user objects.
        // There's a small amount of metadata that's not included here.
        // Some of this memory will be freed on the next `DestroyContent()` call, but not all of it. See `PermanentCapacity()`.
        [[nodiscard]] constexpr std::size_t CurrentMemoryUsage() const
        {
            return state.pool_size + state.old_pools_combined_size;
        }

        // Destroys all contents of the pool.
        // Keeps some memory for future allocations.
        void DestroyContent()
        {
            state.pool_pos = 0;
            state.old_pools_combined_size = 0;
            state.old_pools.clear();
            state.old_pools.shrink_to_fit();
        }

        // Allocate a block of raw memory.
        // If `func` is specified, it's `(std::uint8_t *ptr) -> bool`. It's given the resulting pointer.
        // If it returns false or throws, the allocation is cancelled (if it returned false, `AllocateRawMemory()` returns null).
        // Though if the pool was resized, the new size is kept regardless.
        template <std::size_t A = 1, typename F = std::nullptr_t>
        requires MonotonicPoolSupportedAlignment<A>
        [[nodiscard]] std::uint8_t *AllocateRawMemory(std::size_t size, F &&func = nullptr)
        {
            std::size_t needed_block_start = Align<A>(state.pool_pos);
            std::size_t needed_block_end = needed_block_start + size;
            if (needed_block_end > state.pool_size)
            {
                // Need a larger pool.

                // Allocate the new pool.
                std::size_t new_pool_size = std::max(size * 2, state.pool_size * 2);
                auto new_pool = std::make_unique<std::uint8_t[]>(new_pool_size);

                // Replace the current pool with the new one.
                state.old_pools.push_back(std::move(state.pool)); // This line goes first because it can throw.
                state.old_pools_combined_size += state.pool_size;
                state.pool = std::move(new_pool);
                state.pool_size = new_pool_size;
                state.pool_pos = 0;

                // Update the offsets.
                needed_block_start = 0;
                needed_block_end = size;
            }

            const auto ret = state.pool.get() + needed_block_start;

            // Invoke the user callback.
            if constexpr (!std::is_null_pointer_v<F>)
            {
                if (!bool(std::forward<F>(func)(auto(ret))))
                    return nullptr;
            }

            // Confirm the allocation.
            state.pool_pos = needed_block_end;
            return ret;
        }

        // Allocate a single object.
        template <MonotonicPoolSupportedType T, typename ...P>
        requires std::is_constructible_v<T, P &&...>
        [[nodiscard]] T *AllocateOne(P &&... params)
        {
            T *ret = nullptr;
            (void)AllocateRawMemory<alignof(T)>(sizeof(T), [&](std::uint8_t *ptr)
            {
                ret = ::new((void *)ptr) T(std::forward<P>(params)...);
                return true;
            });
            return ret;
        }

        // Allocate multiple objects.
        template <MonotonicPoolSupportedType T>
        requires std::default_initializable<T>
        [[nodiscard]] std::span<T> AllocateArray(std::size_t n)
        {
            T *ret = nullptr;
            (void)AllocateRawMemory<alignof(T)>(sizeof(T) * n, [&](std::uint8_t *ptr)
            {
                ret = ::new((void *)ptr) T[n]{};

                // This will never happen on GCC and Clang. I heard MSVC used to misbehave here, but I can no longer reproduce.
                // Hopefully they fixed their shit?
                ASSERT((std::uint8_t *)ret == ptr);
                return true;
            });
            return {ret, n};
        }
    };

    // Stores a pointer to a `MonotonicPool`.
    // Expects to be given an empty pool, and clears it when destroyed.
    class TemporaryPoolRef
    {
        MonotonicPool *pool = nullptr;

      public:
        constexpr TemporaryPoolRef() {}
        constexpr TemporaryPoolRef(std::nullptr_t) {}
        constexpr TemporaryPoolRef(MonotonicPool &pool) : TemporaryPoolRef(&pool) {}
        constexpr TemporaryPoolRef(MonotonicPool *pool) : pool(pool)
        {
            ASSERT(!pool || !pool->HasContent(), "`TemporaryPoolRef` expects an empty pool.");
        }

        constexpr TemporaryPoolRef(TemporaryPoolRef &&other) noexcept : pool(std::exchange(other.pool, nullptr)) {}
        TemporaryPoolRef &operator=(TemporaryPoolRef other) noexcept
        {
            std::swap(pool, other.pool);
            return *this;
        }

        ~TemporaryPoolRef()
        {
            pool->DestroyContent();
        }

        [[nodiscard]] constexpr explicit operator bool() const noexcept {return bool(pool);}

        // Intentionally doesn't propagate const.
        [[nodiscard]] MonotonicPool &Pool() const noexcept {return *pool;}
        [[nodiscard]] MonotonicPool *operator->() const noexcept {return pool;}
    };
}
