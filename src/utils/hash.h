#pragma once

#include <concepts>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <tuple>
#include <utility>

#include "meta/common.h"

namespace Hash
{
    // Combines hashes.
    inline void Append(std::size_t &dst, std::size_t src)
    {
        dst ^= src + std::size_t(0x9E3779B97F4A7C16) + (dst << 6) + (dst >> 2); // That number is a fractional part of the golden ratio. Boost uses a similar thing.
    }

    // Combines hashes.
    inline void Append(std::size_t &dst, std::initializer_list<std::size_t> src)
    {
        for (std::size_t it : src)
            Append(dst, it);
    }

    // Combines hashes.
    [[nodiscard]] inline std::size_t Combine(std::size_t a, std::size_t b)
    {
        Append(a, b);
        return a;
    }

    // Combines hashes.
    [[nodiscard]] inline std::size_t Combine(std::initializer_list<std::size_t> list)
    {
        auto it = list.begin();
        if (it == list.end())
            return 0;
        std::size_t hash = *it++;
        while (it != list.end())
            Append(hash, *it++);
        return hash;
    }

    // A functor that extends `std::hash` with more supported types.
    template <typename T = void>
    struct Hasher {};

    // Type-independent specialization.
    template <>
    struct Hasher<void>
    {
        template <typename T>
        [[nodiscard]] std::size_t operator()(const T &obj) const
        requires requires{Hasher<T>{}(obj);}
        {
            return Hasher<T>{}(obj);
        }
    };

    // Computes hash for an object.
    template <typename T>
    [[nodiscard]] std::size_t Compute(const T &obj)
    {
        return Hasher<T>{}(obj);
    }

    // Computes combined hash for several objects.
    template <typename ...P>
    [[nodiscard]] std::size_t Compute(const P &... obj)
    {
        return Combine({(Compute)(obj)...});
    }
}

// Hasher specializations for our custom hash providers.
namespace Hash
{
    namespace impl
    {
        // Checks if `std::hash` is specialized for the given type.
        template <typename T>
        concept std_hashable = requires(const T obj){std::hash<T>{}(obj);};

        // Dummy ADL target for the hashing function.
        inline std::size_t hash_value(/* const T & */) = delete;

        template <typename T>
        concept adl_hashable = requires(T t){{hash_value(t)} -> std::same_as<std::size_t>;};
    }

    // Specialization using `std::hash`.
    template <impl::std_hashable T>
    struct Hasher<T>
    {
        [[nodiscard]] std::size_t operator()(const T &obj) const {return std::hash<T>{}(obj);}
    };

    // Specialization using `hash_value()` via ADL.
    // the name `hash_value` was chosen because it's also used by parallel-hashmap
    //   (and also `std::filesystem::path`, but that's not important because that also supports `std::hash`).
    template <impl::adl_hashable T>
    struct Hasher<T>
    {
        [[nodiscard]] std::size_t operator()(const T &obj) const {using impl::hash_value; return hash_value(obj);}
    };

    // Specialization using `.hash()`.
    template <typename T> requires requires(const T obj){{obj.hash()} -> std::same_as<std::size_t>;}
    struct Hasher<T>
    {
        [[nodiscard]] std::size_t operator()(const T &obj) const {return obj.hash();}
    };
}

// Hasher specializations for the standard types.
namespace Hash
{
    namespace impl
    {
        template <typename T>
        concept container = requires(T t)
        {
            typename T::value_type;
            t.begin();
            t.end();
        };

        template <typename T>
        concept tuple_like = requires(T t)
        {
            std::tuple_size<T>::value; // Note that we don't use `std::tuple_size_v<T>` here because it doesn't appear to be SFINAE-friendly.
        };
    }

    // Containers.
    template <typename T> requires (impl::container<T> && !impl::std_hashable<T>)
    struct Hasher<T>
    {
        [[nodiscard]] std::size_t operator()(const T &obj) const
        {
            std::size_t ret = 0;
            for (const auto &elem : obj)
                Append(ret, Hash::Compute(elem)); // Qualified calls to `Hash::Compute` prevent unwanted ADL.
            return ret;
        }
    };

    // Tuple-like types.
    template <typename T> requires (impl::tuple_like<T> && !impl::container<T> && !impl::std_hashable<T>)
    struct Hasher<T>
    {
        [[nodiscard]] std::size_t operator()(const T &obj) const
        {
            std::size_t ret = 0;
            Meta::const_for<std::tuple_size_v<T>>([&](auto index)
            {
                constexpr auto i = index.value;
                Append(ret, Hash::Compute(std::get<i>(obj))); // Qualified calls to `Hash::Compute` prevent unwanted ADL.
            });
            return ret;
        }
    };
}
