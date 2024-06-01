#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

#include "meta/constexpr_hash.h"

namespace Meta
{
    // Constexpr functions to get type names, and hashes for them.

    namespace impl
    {
        template <typename T>
        [[nodiscard]] constexpr std::string_view RawTypeName()
        {
            #ifndef _MSC_VER
            return __PRETTY_FUNCTION__;
            #else
            return __FUNCSIG__;
            #endif
        }

        struct TypeNameFormat
        {
            std::size_t junk_leading = 0;
            std::size_t junk_total = 0;
        };

        constexpr TypeNameFormat type_name_format = []{
            TypeNameFormat ret;
            std::string_view sample = RawTypeName<int>();
            ret.junk_leading = sample.find("int");
            ret.junk_total = sample.size() - 3;
            return ret;
        }();
        static_assert(type_name_format.junk_leading != std::size_t(-1), "Unable to determine the type name format on this compiler.");

        template <typename T>
        constexpr auto type_name_storage = []{
            std::array<char, RawTypeName<T>().size() - type_name_format.junk_total + 1> ret{};
            std::copy_n(RawTypeName<T>().data() + type_name_format.junk_leading, ret.size() - 1, ret.data());
            return ret;
        }();
    }

    // Returns the type name, as a `std::string_view`. The string is null-terminated, but the terminator is not included in the view.
    template <typename T>
    [[nodiscard]] constexpr std::string_view TypeName()
    {
        return {impl::type_name_storage<T>.data(), impl::type_name_storage<T>.size() - 1};
    }

    // Returns the type name as a C-string.
    template <typename T>
    [[nodiscard]] constexpr const char *TypeNameCstr()
    {
        return impl::type_name_storage<T>.data();
    }

    // Returns a hash of the type name.
    // `hash_t` is `uint32_t`.
    template <typename T>
    [[nodiscard]] constexpr hash_t TypeHash(hash_t seed = 0)
    {
        constexpr auto name = TypeName<T>();
        return const_hash(name.data(), name.size(), seed);
    }

    // Hash test:
    // static_assert(TypeHash<int>() == 3464699359);

    /* Alternative `__cxa_demangle`-based type name implementation.

        #include <cxxabi.h>

        class Demangle
        {
            #ifndef _MSC_VER
            char *buf_ptr = nullptr;
            std::size_t buf_size = 0;
            #endif

          public:
            Demangle() {}

            Demangle(Demangle &&o) noexcept
            #ifndef _MSC_VER
                : buf_ptr(std::exchange(o.buf_ptr, {})), buf_size(std::exchange(o.buf_size, {}))
            #endif
            {}

            Demangle &operator=(Demangle o) noexcept
            {
                #ifndef _MSC_VER
                std::swap(buf_ptr, o.buf_ptr);
                std::swap(buf_size, o.buf_size);
                #else
                (void)o;
                #endif
                return *this;
            }

            ~Demangle()
            {
                #ifndef _MSC_VER
                // Freeing a nullptr is a no-op.
                std::free(buf_ptr);
                #endif
            }

            // Demangles a name.
            // On GCC ang Clang invokes __cxa_demangle, on MSVC returns the string unchanged.
            // The returned pointer remains as long as both the passed string and the class instance are alive.
            [[nodiscard]] const char *operator()(const char *name)
            {
                #ifndef _MSC_VER
                int status = -4;
                buf_ptr = abi::__cxa_demangle(name, buf_ptr, &buf_size, &status);
                ASSERT(status != -2, "Unable to demangle a name.");
                if (status != 0) // -1 = out of memory, -2 = invalid string, -3 = invalid usage
                    return name;
                return buf_ptr;
                #else
                return name;
                #endif
            }
        };

        // Returns the pretty name of a type.
        // For each specific T, caches the name on the first call.
        template <typename T>
        [[nodiscard]] const char *TypeName()
        {
            static Demangle d;
            static const char *ret = d(typeid(T).name());
            return ret;
        }
        template <typename T>
        [[nodiscard]] const char *TypeName(const T &)
        {
            return TypeName<T>();
        }
    */
}
