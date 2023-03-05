#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <sstream>
#include <type_traits>

#define VERSION "3.14.3"

#pragma GCC diagnostic ignored "-Wpragmas" // Silence GCC warning about the next line disabling a warning that GCC doesn't have.
#pragma GCC diagnostic ignored "-Wstring-plus-int" // Silence clang warning about `1+R"()"` paUern.

namespace data
{
    const struct {std::string tag, name;} type_list[]
    {
        {"b"     , "bool"              },
        {"c"     , "char"              },
        {"uc"    , "unsigned char"     },
        {"sc"    , "signed char"       },
        {"s"     , "short"             },
        {"us"    , "unsigned short"    },
        {"i"     , "int"               },
        {"u"     , "unsigned int"      },
        {"l"     , "long"              },
        {"ul"    , "unsigned long"     },
        {"ll"    , "long long"         },
        {"ull"   , "unsigned long long"},
        {"f"     , "float"             },
        {"d"     , "double"            },
        {"ld"    , "long double"       },
        {"i8"    , "std::int8_t"       },
        {"u8"    , "std::uint8_t"      },
        {"i16"   , "std::int16_t"      },
        {"u16"   , "std::uint16_t"     },
        {"i32"   , "std::int32_t"      },
        {"u32"   , "std::uint32_t"     },
        {"i64"   , "std::int64_t"      },
        {"u64"   , "std::uint64_t"     },
        {"x"     , "std::ptrdiff_t"    },
        {"z"     , "std::size_t"       },
    };
    constexpr int type_list_len = std::extent_v<decltype(type_list)>;

    const std::string fields[4] {"x","y","z","w"};
    constexpr int fields_alt_count = 1;
    const std::string fields_alt[fields_alt_count][4]
    {
        {"r","g","b","a"},
        // "s","t","p","q", // Who uses this anyway.
    };

    const std::string custom_operator_symbol = "/", custom_operator_list[]{"dot","cross"};

    const std::string compare_modes[] = {"any", "all", "none", "not_all", "elemwise"};
}

namespace impl
{
    std::ofstream output_file;

    std::stringstream ss;
    const std::stringstream::fmtflags stdfmt = ss.flags();

    bool at_line_start = 1;
    int indentation = 0;
    int section_depth = 0;

    constexpr const char *indentation_string = "    ", *indentation_string_labels = "  ";

    void init(int argc, char **argv)
    {
        if (argc < 2)
        {
            std::cout << "Expected output file name.";
            std::exit(-1);
        }
        if (argc > 2)
        {
            std::cout << "Invalid usage.";
            std::exit(-1);
        }

        output_file.open(argv[1]);
        if (!output_file)
        {
            std::cout << "Unable to open `" << argv[1] << "`.\n";
            std::exit(-1);
        }
    }
}

template <typename ...P> [[nodiscard]] std::string make_str(const P &... params)
{
    impl::ss.clear();
    impl::ss.str("");
    impl::ss.flags(impl::stdfmt);
    (impl::ss << ... << params);
    return impl::ss.str();
}

void output_str(const std::string &str)
{
    for (const char *ptr = str.c_str(); *ptr; ptr++)
    {
        char ch = *ptr;

        if (ch == '}' && impl::indentation > 0)
            impl::indentation--;

        if (impl::at_line_start)
        {
            if (std::strchr(" \t\r", ch))
                continue;

            if (ch == '\n')
            {
                impl::output_file.put('\n');
                continue;
            }

            for (int i = 0; i < impl::indentation; i++)
                impl::output_file << (i == impl::indentation-1 && ch == '@' ? impl::indentation_string_labels : impl::indentation_string);
            impl::at_line_start = 0;
        }

        if (ch != '@')
            impl::output_file.put(ch == '$' ? ' ' : ch);

        if (ch == '{')
            impl::indentation++;

        if (ch == '\n')
            impl::at_line_start = 1;
    }
}

template <typename ...P> void output(const P &... params)
{
    output_str(make_str(params...));
}

void section(std::string header, std::function<void()> func)
{
    output(header, "\n{\n");
    func();
    output("}\n");
}
void section_sc(std::string header, std::function<void()> func) // 'sc' stands for 'end with semicolon'
{
    output(header, "\n{\n");
    func();
    output("};\n");
}

void decorative_section(std::string name, std::function<void()> func)
{
    output("//{", std::string(impl::section_depth+1, ' '), name, "\n");
    impl::indentation--;
    impl::section_depth++;
    func();
    impl::section_depth--;
    output("//}", std::string(impl::section_depth+1, ' '), name, "\n");
    impl::indentation++;
}

void next_line()
{
    output("\n");
}

int main(int argc, char **argv)
{
    impl::init(argc, argv);

    { // Header
        output(1+R"(
            // mat.h
            // Vector and matrix math
            // Version )", VERSION, R"(
            // Generated, don't touch.

            #pragma once
        )");
        next_line();
    }

    { // Includes
        output(1+R"(
            #include <algorithm>
            #include <bit>
            #include <cmath>
            #include <concepts>
            #include <cstddef>
            #include <cstdint>
            #include <istream>
            #include <iterator>
            #include <ostream>
            #include <tuple>
            #include <type_traits>
            #include <utility>
        )");
        next_line();
    }

    next_line();

    { // Includes
        output(1+R"(
            #ifndef IMP_MATH_IS_CONSTANT
            #  ifndef _MSC_VER
            #    define IMP_MATH_IS_CONSTANT(...) __builtin_constant_p(__VA_ARGS__)
            #  else
            #    define IMP_MATH_IS_CONSTANT(...) false
            #  endif
            #endif

            #ifndef IMP_MATH_UNREACHABLE
            #  ifndef _MSC_VER
            #    define IMP_MATH_UNREACHABLE(...) __builtin_unreachable()
            #  else
            #    define IMP_MATH_UNREACHABLE(...) __assume(false)
            #  endif
            #endif

            #ifndef IMP_MATH_SMALL_FUNC
            #  ifndef _MSC_VER
            #    define IMP_MATH_SMALL_FUNC   __attribute__((__always_inline__, __artificial__)) inline // Need explicit inline, otherwise `artificial` complains, even on implicitly inline functions.
            #    define IMP_MATH_SMALL_LAMBDA __attribute__((__always_inline__, __artificial__))
            #  else
            #    define IMP_MATH_SMALL_FUNC   [[msvc::forceinline]]
            #    define IMP_MATH_SMALL_LAMBDA [[msvc::forceinline]] // There is also `__forceinline`, but it doesn't work on lambdas.
            #  endif
            #endif
        )");
        next_line();
    }

    output("// Vectors and matrices\n");
    next_line();

    section("namespace Math", []
    {
        section("inline namespace Utility // Scalar concepts", []
        {
            output(1+R"(
                template <typename T> concept cvref_unqualified = std::is_same_v<T, std::remove_cvref_t<T>>;

                // Whether a type is a scalar.
                template <typename T> struct helper_is_scalar : std::is_arithmetic<T> {}; // Not `std::is_scalar`, because that includes pointers.
                template <typename T> concept scalar = cvref_unqualified<T> && helper_is_scalar<T>::value;
                template <typename T> concept scalar_maybe_const = scalar<std::remove_const_t<T>>;
            )");
        });

        next_line();

        section("inline namespace Vector // Declarations", []
        {
            { // Main templates
                output(1+R"(
                    template <int D, scalar T> struct vec;
                    template <int D, scalar T> struct rect;
                    template <int W, int H, scalar T> struct mat;
                )");
            }
        });

        next_line();

        section("inline namespace Alias // Short type aliases", []
        {
            { // Fixed size.
                // Vectors of specific size
                for (int i = 2; i <= 4; i++)
                    output(" template <scalar T> using vec", i, " = vec<", i, ",T>;");
                next_line();

                // Rects of specific size
                for (int i = 2; i <= 4; i++)
                    output(" template <scalar T> using rect", i, " = rect<", i, ",T>;");
                next_line();

                // Matrices of specific size
                for (int h = 2; h <= 4; h++)
                {
                    for (int w = 2; w <= 4; w++)
                        output(" template <scalar T> using mat", w, "x", h, " = mat<", w, ",", h, ",T>;");
                    next_line();
                }

                // Square matrices of specific size
                for (int i = 2; i <= 4; i++)
                    output(" template <scalar T> using mat", i, " = mat", i, "x", i, "<T>;");
                next_line();
            }
            next_line();

            { // Fixed type, and possibly size.
                for (int i = 0; i < data::type_list_len; i++)
                {
                    const auto &type = data::type_list[i];

                    // Varying size.
                    output(
                        "template <int D> using ", type.tag, "vec = vec<D,", type.name, ">;\n"
                        "template <int D> using ", type.tag, "rect = rect<D,", type.name, ">;\n"
                        "template <int W, int H> using ", type.tag, "mat = mat<W,H,", type.name, ">;\n"
                    );

                    // Fixed size vectors.
                    for (int d = 2; d <= 4; d++)
                        output(" using ", type.tag, "vec", d, " = vec<", d, ',', type.name, ">;");
                    next_line();
                    // Fixed size rects.
                    for (int d = 2; d <= 4; d++)
                        output(" using ", type.tag, "rect", d, " = rect<", d, ',', type.name, ">;");
                    next_line();
                    // Fixed size matrices.
                    for (int h = 2; h <= 4; h++)
                    {
                        for (int w = 2; w <= 4; w++)
                            output(" using ", type.tag, "mat", w, "x", h, " = mat<", w, ",", h, ",", type.name, ">;");
                        next_line();
                    }
                    // Fixed size square matrices.
                    for (int i = 2; i <= 4; i++)
                        output(" using ", type.tag, "mat", i, " = ", type.tag, "mat", i, "x", i, ";");
                    next_line();

                    if (i != data::type_list_len-1)
                        next_line();
                }
            }
        });

        next_line();

        section("namespace Custom // Customization points", []
        {
            output(1+R"(
                // Specializing this adds corresponding constructors and conversion operators to vectors and matrices.
                template <scalar From, scalar To>
                struct Convert
                {
                    // To operator()(const From &) const {...}
                };

                template <typename From, typename To>
                concept convertible = requires(const Convert<From, To> conv, const From from)
                {
                    { conv(from) } -> std::same_as<To>;
                };
            )");
        });

        next_line();

        section("inline namespace Utility // Helper templates", []
        {
            output(1+R"(
                // Some of the concept definitions here are redundant.
                // In some cases this sanitizes user specializations. In some cases it should help with subsumption.

                // Check if `T` is a vector type.
                template <typename T> struct helper_is_vector : std::false_type {};
                template <int D, typename T> struct helper_is_vector<vec<D,T>> : std::true_type {};
                template <typename T> concept vector = cvref_unqualified<T>/*redundant*/ && helper_is_vector<T>::value;
                template <typename T> concept vector_maybe_const = vector<std::remove_const_t<T>>;

                template <typename T> concept vector_or_scalar = scalar<T> || vector<T>;
                template <typename T> concept vector_or_scalar_maybe_const = scalar_maybe_const<T> || vector_maybe_const<T>;

                // Checks if any of `P...` are vector types.
                template <typename ...P> inline constexpr bool any_vectors_v = (vector<P> || ...);

                // Check if `T` is a matrix type.
                template <typename T> struct helper_is_matrix : std::false_type {};
                template <int W, int H, typename T> struct helper_is_matrix<mat<W,H,T>> : std::true_type {};
                template <typename T> concept matrix = cvref_unqualified<T>/*redundant*/ && helper_is_matrix<T>::value;
                template <typename T> concept square_matrix = matrix<T> && T::width == T::height;
                template <typename T> concept matrix_maybe_const = matrix<std::remove_const_t<T>>;
                template <typename T> concept square_matrix_maybe_const = square_matrix<std::remove_const_t<T>>;

                // For vectors returns their element type, for scalars returns them unchanged.
                template <typename T> struct helper_vec_base {using type = T;};
                template <int D, typename T> struct helper_vec_base<      vec<D,T>> {using type =       T;};
                template <int D, typename T> struct helper_vec_base<const vec<D,T>> {using type = const T;};
                template <vector_or_scalar_maybe_const T> using vec_base_t = typename helper_vec_base<T>::type;
                // This version accepts any type, and returns unknown types unchanged.
                template <typename T> using vec_base_weak_t = typename helper_vec_base<T>::type;

                // Whether `T` is a vector with the base type `U`.
                template <typename T, typename U> concept vector_with_base = vector<T> && std::same_as<U, vec_base_t<T>>;
                // Whether `T` is a vector or scalar with the base type `U`.
                template <typename T, typename U> concept vector_or_scalar_with_base = vector_or_scalar<T> && std::same_as<U, vec_base_t<T>>;

                // For vectors returns the number of elements, for scalars returns 1.
                template <typename T> struct helper_vec_size : std::integral_constant<int, 1> {};
                template <int D, typename T> struct helper_vec_size<      vec<D,T>> : std::integral_constant<int, D> {};
                template <int D, typename T> struct helper_vec_size<const vec<D,T>> : std::integral_constant<int, D> {};
                template <vector_or_scalar_maybe_const T> inline constexpr int vec_size_v = helper_vec_size<T>::value;
                template <typename T> inline constexpr int vec_size_weak_v = helper_vec_size<T>::value;

                // If `D == 1` or `T == void`, returns `T`. Otherwise returns `vec<D,T>`.
                template <int D, typename T> struct helper_ver_or_scalar {using type = vec<D,T>;};
                template <int D, typename T> requires(D == 1 || std::is_void_v<T>) struct helper_ver_or_scalar<D, T> {using type = T;};
                template <int D, typename T> using vec_or_scalar_t = typename helper_ver_or_scalar<D,T>::type;

                // If the set {D...} is either {N} or {1,N}, returns `N`.
                // If the set {D...} is empty, returns `1`.
                // Otherwise returns 0.
                template <int ...D> inline constexpr int common_vec_size_or_zero_v = []{
                    int ret = 1;
                    bool ok = ((D == 1 ? true : ret == 1 || ret == D ? (void(ret = D), true) : false) && ...);
                    return ok * ret;
                }();

                template <int ...D> concept have_common_vec_size = common_vec_size_or_zero_v<D...> != 0;

                // If the set {D...} is either {N} or {1,N}, returns `N`.
                // If the set {D...} is empty, returns `1`.
                // Otherwise causes a soft error.
                template <int ...D> requires have_common_vec_size<D...>
                inline constexpr int common_vec_size_v = common_vec_size_or_zero_v<D...>;

                // If `A` is a vector, changes its element type to `B`. If `A` is scalar, returns `B`.
                // In any case, preserves constness of `A`.
                template <typename A, typename B> struct helper_change_vec_base {using type = B;};
                template <typename A, typename B> struct helper_change_vec_base<const A,B> {using type = const typename helper_change_vec_base<A, B>::type;};
                template <int D, typename A, typename B> struct helper_change_vec_base<vec<D,A>,B> {using type = vec<D,B>;};
                template <vector_or_scalar_maybe_const A, scalar B> using change_vec_base_t = typename helper_change_vec_base<A,B>::type;
                // This version accepts any types, and treats them as scalars.
                template <typename A, typename B> using change_vec_base_weak_t = typename helper_change_vec_base<A,B>::type;

                // Whether `T` is a floating-point type, or a vector of such.
                template <typename T> struct helper_is_floating_point_scalar : std::is_floating_point<T> {};
                template <typename T> concept floating_point_scalar = scalar<T> && helper_is_floating_point_scalar<T>::value;
                template <typename T> concept floating_point_vector = vector<T>/*reject const types*/ && floating_point_scalar<vec_base_t<T>>;
                template <typename T> concept floating_point_vector_or_scalar = floating_point_scalar<T> || floating_point_vector<T>;

                // Whether `T` is an integral type, or a vector of such.
                template <typename T> struct helper_is_integral_scalar : std::is_integral<T> {};
                template <typename T> concept integral_scalar = scalar<T> && helper_is_integral_scalar<T>::value;
                template <typename T> concept integral_vector = vector<T> && integral_scalar<vec_base_t<T>>;
                template <typename T> concept integral_vector_or_scalar = integral_scalar<T> || integral_vector<T>;

                // Whether `T` is a signed/unsigned integral type, or a vector of such.
                template <typename T> struct helper_is_unsigned_integral_scalar : std::is_unsigned<T> {};
                template <typename T> concept   signed_integral_scalar = integral_scalar<T> && !helper_is_unsigned_integral_scalar<T>::value;
                template <typename T> concept unsigned_integral_scalar = integral_scalar<T> &&  helper_is_unsigned_integral_scalar<T>::value;
                template <typename T> concept   signed_integral_vector = integral_vector<T> &&   signed_integral_scalar<vec_base_t<T>>;
                template <typename T> concept unsigned_integral_vector = integral_vector<T> && unsigned_integral_scalar<vec_base_t<T>>;
                template <typename T> concept   signed_integral_vector_or_scalar =   signed_integral_scalar<T> ||   signed_integral_vector<T>;
                template <typename T> concept unsigned_integral_vector_or_scalar = unsigned_integral_scalar<T> || unsigned_integral_vector<T>;

                template <typename T> concept signed_maybe_floating_point_scalar = signed_integral_scalar<T> || floating_point_scalar<T>;
                template <typename T> concept signed_maybe_floating_point_vector = signed_integral_vector<T> || floating_point_vector<T>;
                template <typename T> concept signed_maybe_floating_point_vector_or_scalar = signed_integral_vector_or_scalar<T> || floating_point_vector_or_scalar<T>;

                // Returns a reasonable 'floating-point counterpart' for a type.
                // Currently if the type is not floating-point, returns `float`. Otherwise returns the same type.
                // If `T` is a vector, it's base type is changed according to the same rules.
                template <vector_or_scalar T> using floating_point_t = std::conditional_t<floating_point_vector_or_scalar<T>, T, change_vec_base_t<T, float>>;

                // 3-way compares two scalar or vector types to determine which one is 'larger'.
                // Considers the types equivalent only if they are the same.
                template <cvref_unqualified A, cvref_unqualified B> inline constexpr std::partial_ordering compare_types_v = []{
                    if constexpr (std::is_same_v<A, B>)
                    $   return std::partial_ordering::equivalent;
                    else if constexpr (!vector_or_scalar<A> || !vector_or_scalar<B>)
                    $   return std::partial_ordering::unordered;
                    else if constexpr (vec_size_v<A> != vec_size_v<B>)
                    $   return std::partial_ordering::unordered;
                    else if constexpr (floating_point_vector_or_scalar<A> < floating_point_vector_or_scalar<B>)
                    $   return std::partial_ordering::less;
                    else if constexpr (floating_point_vector_or_scalar<A> > floating_point_vector_or_scalar<B>)
                    $   return std::partial_ordering::greater;
                    else if constexpr (signed_integral_vector_or_scalar<A> != signed_integral_vector_or_scalar<B>)
                    $   return std::partial_ordering::unordered;
                    else if constexpr (sizeof(vec_base_t<A>) < sizeof(vec_base_t<B>))
                    $   return std::partial_ordering::less;
                    else if constexpr (sizeof(vec_base_t<A>) > sizeof(vec_base_t<B>))
                    $   return std::partial_ordering::greater;
                    else
                    $   return std::partial_ordering::unordered;
                }();

                // Internal, see below for the public interface.
                // Given a list of scalar and vector types, determines the "larger' type among them according to `compare_types_v`.
                // Returns `void` on failure.
                // If vector types are present, all of them must have the same size, and the resulting type will also be a vector.
                template <typename ...P> struct helper_larger {};
                template <typename T> struct helper_larger<T> {using type = T;};
                template <typename A, typename B, typename C, typename ...P> requires requires{typename helper_larger<B,C,P...>::type;} struct helper_larger<A,B,C,P...> {using type = typename helper_larger<A, typename helper_larger<B,C,P...>::type>::type;};
                template <typename A, typename B> requires(compare_types_v<A,B> == std::partial_ordering::equivalent) struct helper_larger<A,B> {using type = A;};
                template <typename A, typename B> requires(compare_types_v<A,B> == std::partial_ordering::less      ) struct helper_larger<A,B> {using type = B;};
                template <typename A, typename B> requires(compare_types_v<A,B> == std::partial_ordering::greater   ) struct helper_larger<A,B> {using type = A;};
                // Causes a soft error if there's no larger type.
                template <cvref_unqualified ...P> using larger_t = vec_or_scalar_t<common_vec_size_v<vec_size_weak_v<P>...>, typename helper_larger<std::remove_cv_t<vec_base_weak_t<P>>...>::type>;

                // Checks if it's possible to determine the 'larger' type among `P`.
                template <typename ...P> concept have_larger_type = requires{typename larger_t<P...>;};

                // Whether the conversion of `A` to `B` is not narrowing. Doesn't fail when there's no conversion, should return false in that case.
                template <typename A, typename B> concept safely_convertible_to = std::is_same_v<larger_t<A, B>, B>;
            )");

            next_line();

            output("struct uninit {}; // A constructor tag to leave a vector/matrix uninitialized.\n");

            next_line();

            output("// Wrappers for different kinds of comparisons.\n");
            for (const std::string &mode : data::compare_modes)
            {
                output("template <vector_or_scalar T> struct compare_",mode," {const T &value; [[nodiscard]] explicit constexpr compare_",mode,"(const T &value) : value(value) {}};\n");
            }

            output("// Tags for different kinds of comparisons.\n");
            for (const std::string &mode : data::compare_modes)
                output("struct compare_",mode,"_tag {template <vector_or_scalar T> [[nodiscard]] constexpr compare_",mode,"<T> operator()(const T &value) const {return compare_",mode,"(value);}};\n");
        });

        next_line();

        section("inline namespace Utility // Helpers for operators", []
        {
            output(1+R"(
                // Returns i-th vector element. For other types ignores the index.
                template <typename T>
                [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr decltype(auto) vec_elem(int i, T &&vec)
                {
                    if constexpr (std::is_lvalue_reference_v<T>)
                    {
                        if constexpr (!vector<std::remove_cvref_t<T>>)
                        $   return vec;
                        else
                        $   return vec[i];
                    }
                    else
                    {
                        if constexpr (!vector<std::remove_cvref_t<T>>)
                        $   return std::move(vec);
                        else
                        $   return std::move(vec[i]);
                    }
                }

                // Helper for applying a function to one or several scalars or vectors.
                // Mixing scalars and vectors is allowed, but vectors must have the same size.
                // If at least one vector is passed, the result is also a vector.
                // If `D != 1`, forces the result to be the vector of this size, or causes a hard error if not possible.
                template <int D = 1, typename F, typename ...P, typename = std::enable_if_t<(vector_or_scalar_maybe_const<std::remove_reference_t<P>> && ...)>> // Trying to put this condition into `requires` crashes Clang 14.
                IMP_MATH_SMALL_FUNC constexpr auto apply_elementwise(F &&func, P &&... params) -> vec_or_scalar_t<common_vec_size_v<D, vec_size_v<std::remove_reference_t<P>>...>, decltype(std::declval<F>()(vec_elem(0, std::declval<P>())...))>
                {
                    constexpr int size = common_vec_size_v<D, vec_size_v<std::remove_reference_t<P>>...>;
                    using R = vec_or_scalar_t<size, decltype(std::declval<F>()(vec_elem(0, std::declval<P>())...))>;

                    if constexpr (std::is_void_v<R>)
                    {
                        for (int i = 0; i < size; i++)
                        $   func(vec_elem(i, params)...); // No forwarding to prevent moving.
                        return void();
                    }
                    else
                    {
                        R ret{};
                        for (int i = 0; i < size; i++)
                        $   vec_elem(i, ret) = func(vec_elem(i, params)...); // No forwarding to prevent moving.
                        return ret;
                    }
                }

                template <vector_or_scalar T> [[nodiscard]] constexpr bool any_nonzero_elements(const T &value)
                {
                    if constexpr (vector<T>)
                    $   return value.any();
                    else
                    $   return bool(value);
                }
                template <vector_or_scalar T> [[nodiscard]] constexpr bool all_nonzero_elements(const T &value)
                {
                    if constexpr (vector<T>)
                    $   return value.all();
                    else
                    $   return bool(value);
                }
                template <vector_or_scalar T> [[nodiscard]] constexpr bool none_nonzero_elements(const T &value)
                {
                    if constexpr (vector<T>)
                    $   return value.none();
                    else
                    $   return !bool(value);
                }
                template <vector_or_scalar T> [[nodiscard]] constexpr bool not_all_nonzero_elements(const T &value)
                {
                    if constexpr (vector<T>)
                    $   return value.not_all();
                    else
                    $   return !bool(value);
                }
            )");
        });

        next_line();

        section("inline namespace Vector // Operators", []
        {
            const std::string
                ops2[]{"+","-","*","/","%","^","&","|","<<",">>"},
                ops1[]{"~","+","-"},
                ops1bool[]{"!"},
                ops1incdec[]{"++","--"},
                ops2as[]{"+=","-=","*=","/=","%=","^=","&=","|=","<<=",">>="};

            struct CompOp
            {
                std::string op;
                std::string std;
            };
            // Those support comparator tags `any`, `all`, `memberwise`.
            const CompOp ops2comp[] = {
                {"<","std::less"},
                {">","std::greater"},
                {"<=","std::less_equal"},
                {">=","std::greater_equal"},
                {"==","std::equal_to"},
                {"!=","std::not_equal_to"},
                {"&&","std::logical_and"},
                {"||","std::logical_or"},
            };

            for (auto op : ops2)
            {
                // {vec,scalar} @ {vec,scalar}
                output("template <vector_or_scalar A, vector_or_scalar B> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr auto operator",op,"(const A &a, const B &b)"
                       " -> vec<common_vec_size_v<vec_size_v<A>, vec_size_v<B>>, decltype(std::declval<vec_base_t<A>>() ",op," std::declval<vec_base_t<B>>())> {return apply_elementwise([](vec_base_t<A> a, vec_base_t<B> b) IMP_MATH_SMALL_LAMBDA {return a ",op," b;}, a, b);}\n");
            }

            for (auto op : ops1)
            {
                // @ vec
                output("template <vector V> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr auto operator",op,"(const V &v)"
                       " -> change_vec_base_t<V, decltype(",op,"v.x)> {return apply_elementwise([](vec_base_t<V> v) IMP_MATH_SMALL_LAMBDA {return ",op,"v;}, v);}\n");
            }

            for (auto op : ops1bool)
            {
                // @ vec
                output("template <vector_or_scalar_with_base<bool> V> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr auto operator",op,"(const V &v)"
                       " -> change_vec_base_t<V, decltype(",op,"v.x)> {return apply_elementwise([](vec_base_t<V> v) IMP_MATH_SMALL_LAMBDA {return ",op,"v;}, v);}\n");
            }

            for (auto op : ops1incdec)
            {
                // @ vec
                output("template <vector V> IMP_MATH_SMALL_FUNC constexpr V &operator",op,"(V &v) {apply_elementwise([](vec_base_t<V> &v) IMP_MATH_SMALL_LAMBDA {",op,"v;}, v); return v;}\n");

                // vec @
                output("template <vector V> IMP_MATH_SMALL_FUNC constexpr V operator",op,"(V &v, int) {V ret = v; apply_elementwise([](vec_base_t<V> &v) IMP_MATH_SMALL_LAMBDA {",op,"v;}, v); return ret;}\n");
            }

            for (auto op : ops2as)
            {
                // vec @= {vec,scalar}
                // Note the explicit `vector<A> && vector_or_scalar<B>` check here. The concepts in the template argument list already check the same thing, but they do it too late.
                output("template <vector A, safely_convertible_to<A> B> IMP_MATH_SMALL_FUNC constexpr auto operator",op,"(A &a, const B &b)"
                       " -> decltype(std::enable_if_t<vector<A> && vector_or_scalar<B>>(), void(std::declval<vec_base_t<A> &>() ",op," std::declval<vec_base_t<B>>()), std::declval<A &>())"
                       " {apply_elementwise([](vec_base_t<A> &a, vec_base_t<B> b) IMP_MATH_SMALL_LAMBDA {a ",op," b;}, a, b); return a;}\n");
            }

            for (const auto& [op, std_op] : ops2comp)
            {
                // Default implementation without an explicit mode.
                std::string default_mode = op == "==" ? "all" : op == "!=" ? "any" : "elemwise";
                // A concept constraining the default implementation.
                std::string default_concept = op == "&&" || op == "||" ? "vector_or_scalar_with_base<bool>" : "vector_or_scalar";

                if (!default_mode.empty())
                {
                    output("template <",default_concept," A, ",default_concept," B> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr ",
                           default_mode != "elemwise" ? "bool" : "vec<common_vec_size_v<vec_size_v<A>, vec_size_v<B>>, bool>",
                           " operator",op,"(const A &a, const B &b) {if constexpr (vector<A>) return compare_",default_mode,"(a) ",op," b; else return a ",op," compare_",default_mode,"(b);}\n");
                }

                for (const std::string &mode : data::compare_modes)
                {
                    bool elemwise = mode == "elemwise";

                    // comp({vec,scalar}) @ {vec,scalar}
                    output("template <vector_or_scalar A, vector_or_scalar B> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr ",!elemwise?"bool":"vec<common_vec_size_v<vec_size_v<A>, vec_size_v<B>>, bool>",
                           " operator",op,"(compare_",mode,"<A> &&a, const B &b)"
                           " {return ",mode == "elemwise" ? "" : make_str(mode,"_nonzero_elements("),"apply_elementwise(",std_op,"{}, a.value, b)",mode == "elemwise" ? "" : ")",";}\n");

                    // {vec,scalar} @ comp({vec,scalar})
                    output("template <vector_or_scalar A, vector_or_scalar B> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr ",!elemwise?"bool":"vec<common_vec_size_v<vec_size_v<A>, vec_size_v<B>>, bool>",
                           " operator",op,"(const A &a, compare_",mode,"<B> &&b)"
                           " {return ",mode == "elemwise" ? "" : make_str(mode,"_nonzero_elements("),"apply_elementwise(",std_op,"{}, a, b.value)",mode == "elemwise" ? "" : ")",";}\n");
                }
            }

            next_line();

            decorative_section("input/output", [&]
            {
                output(
                R"( template <typename A, typename B, int D, typename T> std::basic_ostream<A,B> &operator<<(std::basic_ostream<A,B> &s, const vec<D,T> &v)
                    {
                        s.width(0);
                        s << '[';
                        for (int i = 0; i < D; i++)
                        {
                            if (i != 0)
                            $   s << ',';
                            s << v[i];
                        }
                        s << ']';
                        return s;
                    }
                    template <typename A, typename B, int W, int H, typename T> std::basic_ostream<A,B> &operator<<(std::basic_ostream<A,B> &s, const mat<W,H,T> &v)
                    {
                        s.width(0);
                        s << '[';
                        for (int y = 0; y < H; y++)
                        {
                            if (y != 0)
                            $   s << ';';
                            for (int x = 0; x < W; x++)
                            {
                                if (x != 0)
                                $   s << ',';
                                s << v[x][y];
                            }
                        }
                        s << ']';
                        return s;
                    }
                    template <typename A, typename B, int D, typename T> std::basic_istream<A,B> &operator>>(std::basic_istream<A,B> &s, vec<D,T> &v)
                    {
                        s.width(0);
                        for (int i = 0; i < D; i++)
                        $   s >> v[i];
                        return s;
                    }
                    template <typename A, typename B, int W, int H, typename T> std::basic_istream<A,B> &operator>>(std::basic_istream<A,B> &s, mat<W,H,T> &v)
                    {
                        s.width(0);
                        for (int y = 0; y < H; y++)
                        for (int x = 0; x < W; x++)
                        $   s >> v[x][y];
                        return s;
                    }
                )");
            });

            next_line();

            decorative_section("matrix multiplication", [&]
            {
                auto Matrix = [&](int x, int y, std::string t) -> std::string
                {
                    if (x == 1 && y == 1)
                        return t;
                    if (x == 1)
                        return make_str("vec",y,"<",t,">");
                    if (y == 1)
                        return make_str("vec",x,"<",t,">");
                    return make_str("mat",x,"x",y,"<",t,">");
                };
                auto Field = [&](int x, int y, int w, int h) -> std::string
                {
                    if (w == 1 && h == 1)
                        return "";
                    if (w == 1)
                        return data::fields[y];
                    if (h == 1)
                        return data::fields[x];
                    return make_str(data::fields[x], ".", data::fields[y]);
                };

                for (int w2 = 1; w2 <= 4; w2++)
                for (int h1 = 1; h1 <= 4; h1++)
                for (int w1h2 = 2; w1h2 <= 4; w1h2++) // Starting from 1 would generate `vec * vec` templates (outer products), which would conflict with member-wise multiplication.
                {
                    if (w2 == 1 && h1 == 1) // This disables generation of `vec * vec` templates (dot products), which would conflict with member-wise multiplication.
                        continue;
                    output("template <typename A, typename B> [[nodiscard]] constexpr ",Matrix(w2,h1,"larger_t<A,B>")," operator*(const ",Matrix(w1h2,h1,"A")," &a, const ",Matrix(w2,w1h2,"B")," &b) {return {");
                    for (int y = 0; y < h1; y++)
                    for (int x = 0; x < w2; x++)
                    {
                        if (y != 0 || x != 0)
                            output(", ");
                        for (int j = 0; j < w1h2; j++)
                        {
                            if (j != 0)
                                output(" + ");
                            output("a.",Field(j,y,w1h2,h1),"*b.",Field(x,j,w2,w1h2));
                        }
                    }
                    output("};}\n");
                }

                next_line();

                // Only in those two cases return type matches the type of the first parameter.
                output("template <typename A, typename B, int D> constexpr vec<D,A> &operator*=(vec<D,A> &a, const mat<D,D,B> &b) {a = a * b; return a;}\n");
                output("template <typename A, typename B, int W, int H> constexpr mat<W,H,A> &operator*=(mat<W,H,A> &a, const mat<W,W,B> &b) {a = a * b; return a;}\n"); // `mat<W,W,B>` is not a typo!
            });
        });

        next_line();

        section("inline namespace Utility // Low-level helper functions", []
        {
            decorative_section("Custom operators", []
            {
                for (auto op : data::custom_operator_list)
                    output("struct op_type_",op," {};\n");

                next_line();

                for (auto op : data::custom_operator_list)
                {
                    output(1+R"(
                        template <typename A> struct op_expr_type_)",op,R"(
                        {
                            A &&a;
                            template <typename B> [[nodiscard]] constexpr decltype(auto) operator)",data::custom_operator_symbol,R"((B &&b) {return std::forward<A>(a).)",op,R"((std::forward<B>(b));}
                            template <typename B> constexpr decltype(auto) operator)",data::custom_operator_symbol,R"(=(B &&b) {a = std::forward<A>(a).)",op,R"((std::forward<B>(b)); return std::forward<A>(a);}
                        };
                    )");
                }

                next_line();

                for (auto op : data::custom_operator_list)
                    output("template <typename T> inline constexpr op_expr_type_",op,"<T> operator",data::custom_operator_symbol,"(T &&param, op_type_",op,") {return {std::forward<T>(param)};}\n");
            });

            next_line();

            decorative_section("Ranges", []
            {
                output(1+R"(
                    template <integral_vector_or_scalar T> class vector_range_t
                    {
                        T vec_begin{};
                        T vec_end{};

                      @public:
                        class iterator
                        {
                            friend class vector_range_t<T>;

                            T vec_begin{};
                            T vec_end{};
                            T vec_cur{};
                            bool finished = true;

                            iterator(T vec_begin, T vec_end) : vec_begin(vec_begin), vec_end(vec_end), vec_cur(vec_begin), finished(compare_any(vec_begin) >= vec_end) {}

                          @public:
                            using difference_type   = std::ptrdiff_t;
                            using value_type        = T;
                            using pointer           = const T *;
                            using reference         = const T &;
                            using iterator_category = std::forward_iterator_tag;

                            iterator() {}

                            iterator &operator++()
                            {
                                for (int i = 0; i < vec_size_v<T>; i++)
                                {
                                    auto &elem = vec_elem(i, vec_cur);
                                    elem++;
                                    if (elem < vec_elem(i, vec_end))
                                    $   break;
                                    elem = vec_elem(i, vec_begin);
                                    if (i == vec_size_v<T> - 1)
                                    $   finished = true;
                                }

                                return *this;
                            }
                            iterator operator++(int)
                            {
                                iterator ret = *this;
                                ++(*this);
                                return ret;
                            }

                            reference operator*() const
                            {
                                return vec_cur;
                            }
                            pointer operator->() const
                            {
                                return &vec_cur;
                            }

                            bool operator==(const iterator &other) const
                            {
                                if (finished != other.finished)
                                $   return false;
                                if (finished && other.finished)
                                $   return true;
                                return vec_cur == other.vec_cur;
                            }
                        };

                        vector_range_t() {}
                        vector_range_t(T vec_begin, T vec_end) : vec_begin(vec_begin), vec_end(vec_end) {}

                        iterator begin() const
                        {
                            return iterator(vec_begin, vec_end);
                        }

                        iterator end() const
                        {
                            return {};
                        }

                        [[nodiscard]] friend vector_range_t operator+(const vector_range_t &range, std::same_as<T> auto offset)
                        {
                            return vector_range_t(range.vec_begin + offset, range.vec_end + offset);
                        }
                        [[nodiscard]] friend vector_range_t operator+(std::same_as<T> auto offset, const vector_range_t &range)
                        {
                            return range + offset;
                        }
                    };

                    template <integral_vector_or_scalar T> class vector_range_halfbound
                    {
                        T vec_begin{};

                      @public:
                        vector_range_halfbound(T vec_begin) : vec_begin(vec_begin) {}

                        [[nodiscard]] friend vector_range_t<T> operator<(const vector_range_halfbound &range, std::same_as<T> auto point)
                        {
                            return vector_range_t<T>(range.vec_begin, point);
                        }
                        [[nodiscard]] friend vector_range_t<T> operator<=(const vector_range_halfbound &range, std::same_as<T> auto point)
                        {
                            return range < point+1;
                        }
                    };

                    struct vector_range_factory
                    {
                        template <vector_or_scalar T> vector_range_t<T> operator()(T size) const
                        {
                            return vector_range_t<T>(T(0), size);
                        }

                        template <int D, typename T> vector_range_t<vec<D,T>> operator()(rect<D,T> r) const
                        {
                            return vector_range_t<vec<D,T>>(r.a, r.size());
                        }

                        template <vector_or_scalar T> friend vector_range_halfbound<T> operator<=(T point, vector_range_factory)
                        {
                            return {point};
                        }
                        template <vector_or_scalar T> friend vector_range_halfbound<T> operator<(T point, vector_range_factory)
                        {
                            return point+1 <= vector_range_factory{};
                        }
                    };
                )");
            });
        });

        next_line();

        section("inline namespace Common // Common functions", []
        {
            output(1+R"(
                // Named operators.
            )");
            for (auto op : data::custom_operator_list)
                output("inline constexpr op_type_", op, " ", op, ";\n");

            next_line();

            output(1+R"(
                // Comparison tags.
            )");
            for (const std::string &mode : data::compare_modes)
                output("inline constexpr compare_",mode,"_tag ",mode,";\n");

            next_line();

            output(1+R"(
                // Helper class for writing nested loops.
                // Example usage:
                //   for (auto v : vec_a <= vector_range <= vec_b) // `<` are also allowed, in one or both positions.
                //   for (auto v : vector_range(vec_a)) // Equivalent to `vec..(0) <= vector_range < vec_a`.
            )");
            output("inline constexpr vector_range_factory vector_range;\n");

            next_line();

            output(1+R"(
                // The value of pi.
                template <scalar T> [[nodiscard]] constexpr T pi() {return T(3.14159265358979323846l);}
                constexpr float       f_pi  = pi<float>();
                constexpr double      d_pi  = pi<double>();
                constexpr long double ld_pi = pi<long double>();

                // Conversions between degrees and radians.
                template <vector_or_scalar T> [[nodiscard]] constexpr auto to_rad(T in)
                {
                    using fp_t = floating_point_t<T>;
                    return in * pi<fp_t>() / fp_t(180);
                }
                template <vector_or_scalar T> [[nodiscard]] constexpr auto to_deg(T in)
                {
                    using fp_t = floating_point_t<T>;
                    return in * fp_t(180) / pi<fp_t>();
                }

                // Returns the sign of the argument as `int` or `ivecN`.
                template <vector_or_scalar T> [[nodiscard]] constexpr change_vec_base_t<T,int> sign(T val)
                {
                    // Works on scalars and vectors.
                    return (val > 0) - (val < 0);
                }
                // Returns the sign of `a - b`. Unlike `sign(a - b)`, not affected by overflow.
                // Refuses to work if one of the arguments is a signed integer, and the other is unsigned.
                template <vector_or_scalar A, vector_or_scalar B> requires have_larger_type<A, B>
                [[nodiscard]] constexpr auto diffsign(A a, B b) -> vec_or_scalar_t<common_vec_size_v<vec_size_v<A>,vec_size_v<B>>,int>
                {
                    // Works on scalars and vectors.
                    return (a > b) - (a < b);
                }

                // `clamp[_var][_min|_max|_abs] (value, min, max)`.
                // Clamps scalars or vectors.
                // `_var` functions modify the first parameter instead of returning the result.
                // `_min` functions don't have a `max` parameter, and vice versa.
                // `_abs` functions don't have a `min` parameter, they use `-max` as `min`.
                // If both `min` and `max` are omitted, 0 and 1 are assumed.
                // If bounds contradict each other, only the `max` bound is used.

                template <vector_or_scalar A, safely_convertible_to<A> B>
                constexpr void clamp_var_min(A &var, B min)
                {
                    if constexpr (!any_vectors_v<A,B>)
                    {
                        if (!(var >= min)) // The condition is written like this to catch NaNs, they always compare to false.
                        $   var = min;
                    }
                    else
                    {
                        apply_elementwise(clamp_var_min<vec_base_t<A>, vec_base_t<B>>, var, min);
                    }
                }

                template <vector_or_scalar A, safely_convertible_to<A> B>
                constexpr void clamp_var_max(A &var, B max)
                {
                    if constexpr (!any_vectors_v<A,B>)
                    {
                        if (!(var <= max)) // The condition is written like this to catch NaNs, they always compare to false.
                        $   var = max;
                    }
                    else
                    {
                        apply_elementwise(clamp_var_max<vec_base_t<A>, vec_base_t<B>>, var, max);
                    }
                }

                template <vector_or_scalar A, safely_convertible_to<A> B, safely_convertible_to<A> C>
                constexpr void clamp_var(A &var, B min, C max)
                {
                    clamp_var_min(var, min);
                    clamp_var_max(var, max);
                }

                template <vector_or_scalar A, safely_convertible_to<A> B> requires signed_maybe_floating_point_vector_or_scalar<B>
                constexpr void clamp_var_abs(A &var, B abs_max)
                {
                    clamp_var(var, -abs_max, abs_max);
                }

                template <vector_or_scalar A, safely_convertible_to<A> B>
                [[nodiscard]] constexpr A clamp_min(A val, B min)
                {
                    clamp_var_min(val, min);
                    return val;
                }

                template <vector_or_scalar A, safely_convertible_to<A> B>
                [[nodiscard]] constexpr A clamp_max(A val, B max)
                {
                    clamp_var_max(val, max);
                    return val;
                }

                template <vector_or_scalar A, safely_convertible_to<A> B, safely_convertible_to<A> C>
                [[nodiscard]] constexpr A clamp(A val, B min, C max)
                {
                    clamp_var(val, min, max);
                    return val;
                }

                template <vector_or_scalar A, safely_convertible_to<A> B> requires signed_maybe_floating_point_vector_or_scalar<B>
                [[nodiscard]] constexpr A clamp_abs(A val, B abs_max)
                {
                    clamp_var_abs(val, abs_max);
                    return val;
                }

                template <vector_or_scalar A> [[nodiscard]] constexpr A clamp(A val) {return clamp(val, 0, 1);}
                template <vector_or_scalar A> [[nodiscard]] constexpr A clamp_min(A val) {return clamp_min(val, 0);}
                template <vector_or_scalar A> [[nodiscard]] constexpr A clamp_max(A val) {return clamp_max(val, 1);}
                template <vector_or_scalar A> [[nodiscard]] constexpr A clamp_abs(A val) {return clamp_abs(val, 1);}
                template <vector_or_scalar A> constexpr void clamp_var(A &var) {clamp_var(var, 0, 1);}
                template <vector_or_scalar A> constexpr void clamp_var_min(A &var) {clamp_var_min(var, 0);}
                template <vector_or_scalar A> constexpr void clamp_var_max(A &var) {clamp_var_max(var, 1);}
                template <vector_or_scalar A> constexpr void clamp_var_abs(A &var) {clamp_var_abs(var, 1);}

                // Rounds a floating-point scalar or vector.
                // Returns an integral type (`int` by default).
                template <signed_integral_scalar I = int, floating_point_vector_or_scalar F>
                [[nodiscard]] change_vec_base_t<F,I> iround(F x)
                {
                    if constexpr (!any_vectors_v<F>)
                    {
                        // This seems to be faster than `std::lround()`.
                        return I(std::round(x));
                    }
                    else
                    {
                        return apply_elementwise(iround<I, vec_base_t<F>>, x);
                    }
                }

                // Various useful functions.
                // Some of them are imported from `std` and extended to operate on vectors. Some are custom.

                using std::abs;
                template <vector T>
                [[nodiscard]] T abs(T x)
                {
                    return apply_elementwise([](auto val){return std::abs(val);}, x);
                }

                using std::round;
                template <floating_point_vector T>
                [[nodiscard]] T round(T x)
                {
                    return apply_elementwise([](auto val){return std::round(val);}, x);
                }

                using std::floor;
                template <floating_point_vector T>
                [[nodiscard]] T floor(T x)
                {
                    return apply_elementwise([](auto val){return std::floor(val);}, x);
                }

                using std::ceil;
                template <floating_point_vector T>
                [[nodiscard]] T ceil(T x)
                {
                    return apply_elementwise([](auto val){return std::ceil(val);}, x);
                }

                using std::trunc;
                template <floating_point_vector T>
                [[nodiscard]] T trunc(T x)
                {
                    return apply_elementwise([](auto val){return std::trunc(val);}, x);
                }

                template <floating_point_vector T>
                [[nodiscard]] T round_maxabs(T x) // Round away from zero.
                {
                    return apply_elementwise([](auto val){return val < 0 ? std::floor(val) : std::ceil(val);}, x);
                }

                template <floating_point_vector T>
                [[nodiscard]] T frac(T x)
                {
                    if constexpr (!any_vectors_v<T>)
                    $   return std::modf(x, 0);
                    else
                    $   return apply_elementwise(frac<vec_base_t<T>>, x);
                }

                using std::nextafter;
                template <floating_point_vector_or_scalar A, floating_point_vector_or_scalar B>
                requires any_vectors_v<A, B> && std::is_same_v<vec_base_t<A>, vec_base_t<B>> && have_larger_type<A, B>
                [[nodiscard]] A nextafter(A a, B b)
                {
                    return apply_elementwise([](auto a, auto b){return std::nextafter(a, b);}, a, b);
                }

                // Integer division, slightly changed to behave nicely for negative values of the left operand:
                //           i : -4  -3  -2  -1  0  1  2  3  4
                // div_ex(i,2) : -2  -2  -1  -1  0  0  1  1  2
                template <integral_vector_or_scalar A, integral_vector_or_scalar B>
                [[nodiscard]] constexpr A div_ex(A a, B b)
                {
                    if constexpr (!any_vectors_v<A,B>)
                    {
                        if (a >= 0)
                        $   return a / b;
                        else
                        $   return (a + 1) / b - sign(b);
                    }
                    else
                    {
                        return apply_elementwise(div_ex<vec_base_t<A>, vec_base_t<B>>, a, b);
                    }
                }

                // True integral modulo that remains periodic for negative values of the left operand.
                template <integral_vector_or_scalar A, integral_vector_or_scalar B>
                [[nodiscard]] constexpr A mod_ex(A a, B b)
                {
                    if constexpr (!any_vectors_v<A,B>)
                    {
                        if (a >= 0)
                        $   return a % b;
                        else
                        $   return abs(b) - 1 + (a + 1) % b;
                    }
                    else
                    {
                        return apply_elementwise(mod_ex<vec_base_t<A>, vec_base_t<B>>, a, b);
                    }
                }

                // Divide `a / b`, rounding away from zero.
                // Supports both integers and floating-point numbers, including vectors.
                template <signed_maybe_floating_point_vector_or_scalar A, signed_maybe_floating_point_vector_or_scalar B>
                [[nodiscard]] constexpr larger_t<A, B> div_maxabs(A a, B b)
                {
                    if constexpr (!any_vectors_v<A, B>)
                    {
                        if constexpr (integral_scalar<A> && integral_scalar<B>)
                        {
                            return (a + (abs(b) - 1) * sign(a)) / b;
                        }
                        else
                        {
                            using T = larger_t<A, B>;
                            T ret = T(a) / T(b);
                            return round_maxabs(ret);
                        }
                    }
                    else
                    {
                        return apply_elementwise(div_maxabs<vec_base_t<A>, vec_base_t<B>>, a, b);
                    }
                }

                // A simple implementation of `pow` for non-negative integral powers.
                template <vector_or_scalar A, integral_scalar B>
                [[nodiscard]] constexpr A ipow(A a, B b)
                {
                    A ret = 1;
                    while (b > 0)
                    {
                        if (b & 1)
                            ret *= a;
                        a *= a;
                        b >>= 1;
                    }
                    return ret;
                }

                using std::pow;
                template <vector_or_scalar A, vector_or_scalar B>
                requires any_vectors_v<A, B>
                [[nodiscard]] auto pow(A a, B b)
                {
                    return apply_elementwise([](auto val_a, auto val_b){return std::pow(val_a, val_b);}, a, b);
                }

                // Computes the smooth step function. Doesn't clamp `x`.
                template <floating_point_vector_or_scalar T>
                [[nodiscard]] constexpr T smoothstep(T x)
                {
                    // No special handling required for `T` being a vector.
                    return (3 - 2*x) * x*x;
                }

                // Performs linear interpolation. Returns `a * (1-factor) + b * factor`.
                template <floating_point_scalar F, vector_or_scalar A, vector_or_scalar B>
                requires have_larger_type<A, B>
                [[nodiscard]] constexpr auto mix(F factor, A a, B b)
                {
                    // No special handling required for the parameters being vectors.
                    using type = larger_t<A, B>;
                    return type(a) * (1-factor) + type(b) * factor;
                }

                // Returns a `min` or `max` value of the parameters.
                template <typename ...P> [[nodiscard]] constexpr larger_t<P...> min(P ... params)
                {
                    if constexpr (!any_vectors_v<P...>)
                    $   return std::min({larger_t<P...>(params)...});
                    else
                    $   return apply_elementwise(min<vec_base_t<P>...>, params...);
                }
                template <typename ...P> [[nodiscard]] constexpr larger_t<P...> max(P ... params)
                {
                    if constexpr (!any_vectors_v<P...>)
                    $   return std::max({larger_t<P...>(params)...});
                    else
                    $   return apply_elementwise(max<vec_base_t<P>...>, params...);
                }

                // Returns `[min(a,b), max(a,b)]`. Like `std::minmax`, but returns by value and can handle vectors.
                template <typename A, typename B> [[nodiscard]] constexpr std::pair<larger_t<A, B>, larger_t<A, B>> sort_two(A a, B b)
                {
                    using T = larger_t<A, B>;
                    std::pair<T, T> ret;
                    for (int i = 0; i < vec_size_weak_v<T>; i++)
                    {
                        auto a_elem = vec_elem(i, a);
                        auto b_elem = vec_elem(i, b);
                        if (b_elem < a_elem)
                        $   vec_elem(i, ret.first) = b_elem, vec_elem(i, ret.second) = a_elem;
                        else
                        $   vec_elem(i, ret.first) = a_elem, vec_elem(i, ret.second) = b_elem;
                    }
                    return ret;
                }
                // Sorts `{a,b}` in place. Sorts vectors element-wise.
                template <typename T> constexpr void sort_two_var(T &a, T &b)
                {
                    if constexpr (!any_vectors_v<T>)
                    {
                        if (b < a)
                        $   std::swap(a, b);
                    }
                    else
                    {
                        apply_elementwise(sort_two_var<vec_base_t<T>>, a, b);
                    }
                }
            )");
        });

        next_line();

        section("inline namespace Misc // Misc functions", []
        {
            output(1+R"(
                // A functor that performs linear mapping on scalars or vectors.
                template <floating_point_vector_or_scalar T>
                struct linear_mapping
                {
                    T scale = T(1), offset = T(0);

                    constexpr linear_mapping() {}

                    constexpr linear_mapping(T src_a, T src_b, T dst_a, T dst_b)
                    {
                        T factor = 1 / (src_a - src_b);
                        scale = (dst_a - dst_b) * factor;
                        offset = (dst_b * src_a - dst_a * src_b) * factor;
                    }

                    constexpr T operator()(T x) const
                    {
                        return x * scale + offset;
                    }

                    using matrix_t = mat<vec_size_v<T>+1, vec_size_v<T>+1, vec_base_t<T>>;
                    constexpr matrix_t matrix() const
                    {
                        matrix_t ret{};
                        for (int i = 0; i < vec_size_v<T>; i++)
                        {
                            ret[i][i] = scale[i];
                            ret[vec_size_v<T>][i] = offset[i];
                        }
                        return ret;
                    }
                };

                // Like `nextafter()`, but works with integers as well.
                template <vector_or_scalar A, vector_or_scalar B>
                [[nodiscard]] larger_t<A, B> next_value_towards(A value, B target)
                {
                    using type = larger_t<A, B>;
                    if constexpr (floating_point_vector_or_scalar<type>)
                    $   return nextafter(type(value), type(target));
                    else
                    $   return type(value) + diffsign(type(target), type(value)); // The plain `sign()` could overflow here.
                }
                // Returns the next or previous representable value.
                // Refuses to increment the largest representable value, and returns it unchanged.
                // If asked to increment infinity in either direction, returns the closest representable value.
                // If given NaN, returns NaN.
                template <bool Prev, vector_or_scalar T>
                [[nodiscard]] T next_or_prev_value(T value)
                {
                    return next_value_towards(value, Prev ? std::numeric_limits<vec_base_t<T>>::lowest() : std::numeric_limits<vec_base_t<T>>::max());
                }
                template <vector_or_scalar T> [[nodiscard]] T next_value(T value) {return next_or_prev_value<false>(value);}
                template <vector_or_scalar T> [[nodiscard]] T prev_value(T value) {return next_or_prev_value<true >(value);}

                // Shrinks a vector as little as possible to give it specific proportions.
                // Always returns a floating-point type.
                template <vector A, vector B> requires have_larger_type<A, B>
                [[nodiscard]] constexpr auto shrink_to_proportions(A value, B proportions)
                {
                    using type = larger_t<floating_point_t<A>,floating_point_t<B>>;
                    return (type(value) / type(proportions)).min() * type(proportions);
                }
                // Expands a vector as little as possible to give it specific proportions.
                // Always returns a floating-point type.
                template <vector A, vector B> requires have_larger_type<A, B>
                [[nodiscard]] constexpr auto expand_to_proportions(A value, B proportions)
                {
                    using type = larger_t<floating_point_t<A>,floating_point_t<B>>;
                    return (type(value) / type(proportions)).max() * type(proportions);
                }

                // Finds an intersection point of two lines.
                template <floating_point_scalar T>
                [[nodiscard]] constexpr vec2<T> line_intersection(vec2<T> a1, vec2<T> a2, vec2<T> b1, vec2<T> b2)
                {
                    auto delta_a = a2 - a1;
                    auto delta_b = b2 - b1;
                    return ((a1.y - b1.y) * delta_b.x - (a1.x - b1.x) * delta_b.y) / (delta_a.x * delta_b.y - delta_a.y * delta_b.x) * delta_a + a1;
                }

                // Finds an intersection point of a line and a plane.
                template <floating_point_scalar T>
                [[nodiscard]] constexpr vec3<T> line_plane_intersection(vec3<T> line_point, vec3<T> line_dir, vec3<T> plane_point, vec3<T> plane_normal)
                {
                    return (plane_point - line_point).dot(plane_normal) / line_dir.dot(plane_normal) * line_dir + line_point;
                }

                // Projects a point onto a line. `dir` is assumed to be normalized.
                template <vector T>
                [[nodiscard]] constexpr T project_onto_line_norm(T point, T dir)
                {
                    return dir * point.dot(dir);
                }
                // Projects a point onto a line.
                template <floating_point_vector T>
                [[nodiscard]] constexpr T project_onto_line(T point, T dir)
                {
                    return project_onto_line_norm(point, dir.norm());
                }

                // Projects a point onto a plane. `plane_normal` is assumed to be normalized.
                template <floating_point_scalar T>
                [[nodiscard]] constexpr vec3<T> project_onto_plane_norm(vec3<T> point, vec3<T> plane_normal)
                {
                    return point - project_onto_line_norm(point, plane_normal);
                }
                // Projects a point onto a plane.
                template <floating_point_scalar T>
                [[nodiscard]] constexpr vec3<T> project_onto_plane(vec3<T> point, vec3<T> plane_normal)
                {
                    return project_onto_plane_norm(point, plane_normal.norm());
                }

                // Compares the angles of `a` and `b` without doing any trigonometry. Works with integers too.
                // The assumed angles are in range [0;2pi), with +X having angle 0.
                // Zero vectors are considered to be greater than everything else.
                template <scalar T>
                [[nodiscard]] constexpr bool less_positively_rotated(vec2<T> a, vec2<T> b)
                {
                    // This check makes (0,0) worse than any other vector,
                    // and doesn't seem to affect the result if zero vectors are not involved.
                    if (int d = (a == vec2<T>()) - (b == vec2<T>()))
                        return d < 0;

                    if (int d = (a.y < 0) - (b.y < 0))
                        return d < 0;
                    if (int d = (a.y == 0 && a.x < 0) - (b.y == 0 && b.x < 0))
                        return d < 0;

                    return a.x * b.y > b.x * a.y;
                }

                // Same, but angle 0 is mapped to `dir` instead of +X.
                template <scalar T>
                [[nodiscard]] constexpr bool less_positively_rotated(vec2<T> dir, vec2<T> a, vec2<T> b)
                {
                    mat2<T> mat(dir, dir.rot90());
                    return less_positively_rotated(a * mat, b * mat);
                }

                // Rounds `value` to type `I`, with compensation: `comp` is added to it before rounding, then updated to the difference between rounded and unrounded value.
                // This makes the average return value converge to `value`.
                template <integral_scalar I = int, floating_point_vector_or_scalar F>
                [[nodiscard]] constexpr change_vec_base_t<F,I> round_with_compensation(F value, F &comp)
                {
                    // Works on scalars and vectors.
                    change_vec_base_t<F,I> ret = iround<I>(value += comp);
                    comp = value - ret;
                    return ret;
                }

                // Produces points to fill a cuboid (line, rect, cube, and so on), either entirely or only the borders.
                // `a` and `b` are the corners, inclusive. `step` is the step, the sign is ignored.
                // `pred` lets you select what parts of the cuboid to output. It's is either `nullptr` (output everything)
                // or `bool pred(unsigned int mask)`, where the mask receives all combinations of N bits, where N is `vec_size_v<T>`.
                // If `pred` returns true, the corresponding region is emitted using repeated calls to `func`, which is `bool func(T &&point)`.
                // If `func` returns true, the function stops immediately and also returns true. Otherwise returns false when done.
                // The number of `1`s in the mask (`std::popcount(mask)`) describes the dimensions of the region: 0 = points, 1 = lines, 2 = rects, and so on.
                // If the i-th bit is set, the region extends in i-th dimension. Each mask corresponds to a set of parallel lines/planes/etc,
                // and the zero mask corresponds to the corners of the cuboid.
                template <signed_maybe_floating_point_vector_or_scalar T, typename F1 = std::nullptr_t, typename F2>
                bool for_each_cuboid_point(T a, T b, T step, F1 &&pred, F2 &&func)
                {
                    // Fix the sign of the `step`.
                    for (int i = 0; i < vec_size_v<T>; i++)
                    {
                        vec_elem(i, step) *= sign(vec_elem(i, b) - vec_elem(i, a)) * sign(vec_elem(i, step));
                        // We don't want zero step.
                        if (vec_elem(i, step) == 0) vec_elem(i, step) = 1;
                    }

                    using int_vec = change_vec_base_t<T, int>;
                    int_vec count = abs(div_maxabs(b - a, step)) - 1;

                    if constexpr (std::is_null_pointer_v<std::remove_cvref_t<F1>>)
                    {
                        // A simple algorithm to fill the whole cuboid.
                        for (int_vec pos : vector_range(count + 2))
                        {
                            T value;
                            for (int i = 0; i < vec_size_v<T>; i++)
                            $   vec_elem(i, value) = vec_elem(i, pos) == vec_elem(i, count) + 1 ? vec_elem(i, b) : vec_elem(i, a) + vec_elem(i, step) * vec_elem(i, pos);
                            if (func(std::move(value)))
                            $   return true;
                        }
                    }
                    else
                    {
                        // A more advanced algorithm to control separate regions.
                        for (unsigned int i = 0; i < 1u << vec_size_v<T>; i++)
                        {
                            // Stop early if we don't want this region.
                            // The casts stop `pred` from doing weird things.
                            if (!bool(pred((unsigned int)i)))
                            $   continue;

                            // Get the number of points in the region, in each dimension.
                            bool bad_region = false;
                            int_vec region_size;
                            for (int j = 0; j < vec_size_v<T>; j++)
                            {
                                if (i & 1u << j)
                                {
                                    if ((vec_elem(j, region_size) = vec_elem(j, count)) <= 0)
                                    {
                                        bad_region = true;
                                        break;
                                    }
                                }
                                else
                                {
                                    vec_elem(j, region_size) = vec_elem(j, a) == vec_elem(j, b) ? 1 : 2;
                                }
                            }
                            if (bad_region)
                            $   continue; // A degenerate region.

                            // Output points.
                            for (int_vec pos : vector_range(region_size))
                            {
                                T value;
                                for (int j = 0; j < vec_size_v<T>; j++)
                                {
                                    if (!(i & 1u << j))
                                    $   vec_elem(j, value) = vec_elem(j, vec_elem(j, pos) ? b : a);
                                    else
                                    $   vec_elem(j, value) = vec_elem(j, a) + (vec_elem(j, pos) + 1) * vec_elem(j, step);
                                }
                                if (func(std::move(value)))
                                $   return true;
                            }
                        }
                    }

                    return false;
                }

                // Produces points to fill a cuboid (line, rect, cube, and so on), either entirely or only the borders. Writes the points of type `T` to `*iter++`.
                // `a` and `b` are the corners, inclusive. `step` is the step, the sign is ignored.
                // `D` is the dimensions of the output. `D == -1` and `D == vec_size_v<T>` mean "fill the whole cuboid".
                // `D == 0` only outputs the corner points, `D == 1` outputs lines, `D == 2` outputs planes, and so on.
                template <int D = -1, signed_maybe_floating_point_vector_or_scalar T, typename I>
                requires(D >= -1 && D <= vec_size_v<T>)
                void make_cuboid(T a, T b, T step, I iter)
                {
                    if constexpr (D == -1 || D == vec_size_v<T>)
                    {
                        for_each_cuboid_point(a, b, step, nullptr, [&](T &&point)
                        {
                            *iter++ = std::move(point);
                            return false;
                        });
                    }
                    else
                    {
                        for_each_cuboid_point(a, b, step, [](unsigned int mask)
                        {
                            return std::popcount(mask) <= D;
                        },
                        [&](T &&point)
                        {
                            *iter++ = std::move(point);
                            return false;
                        });
                    }
                }

                // Same, but writes the output to a container.
                template <typename C, int D = -1, signed_maybe_floating_point_vector_or_scalar T>
                [[nodiscard]] C make_cuboid(T a, T b, T step)
                {
                    C ret;
                    make_cuboid(a, b, step, std::back_inserter(ret));
                    return ret;
                }
            )");
        });

        next_line();

        section("inline namespace Vector // Definitions", []
        {
            decorative_section("Vectors", [&]
            {
                for (int w = 2; w <= 4; w++)
                {
                    if (w != 2)
                        next_line();

                    section_sc(make_str("template <typename T> struct vec<",w,",T> // vec",w), [&]
                    {
                        // In pattern, `@` = field name, `#` field index.
                        auto Fields = [&](std::string fold_op, std::string pattern = "@") -> std::string
                        {
                            std::string ret;
                            for (int i = 0; i < w; i++)
                            {
                                if (i != 0)
                                    ret += fold_op;
                                for (char ch : pattern)
                                {
                                    if (ch == '@')
                                        ret += data::fields[i];
                                    else if (ch == '#')
                                        ret += std::to_string(i);
                                    else
                                        ret += ch;
                                }
                            }
                            return ret;
                        };

                        { // Aliases
                            output("using type = T;\n");
                            output("using rect_type = rect",w,"<T>;\n");
                        }

                        { // Properties
                            output("static constexpr int size = ",w,";\n");
                            output("static constexpr bool is_floating_point = floating_point_scalar<type>;\n");
                        }

                        { // Members
                            output("type ",Fields(", "),";\n");
                        }

                        { // Member aliases.
                            for (int j = 0; j < data::fields_alt_count; j++)
                            {
                                for (int i = 0; i < w; i++)
                                {
                                    output("[[nodiscard]] IMP_MATH_SMALL_FUNC constexpr type &",data::fields_alt[j][i],"() {return ",data::fields[i],";} ");
                                    output("[[nodiscard]] IMP_MATH_SMALL_FUNC constexpr const type &",data::fields_alt[j][i],"() const {return ",data::fields[i],";}\n");
                                }
                            }
                        }

                        { // Constructors
                            // Default
                            output("IMP_MATH_SMALL_FUNC constexpr vec() : ",Fields(", ", "@{}")," {}\n");

                            // Uninitialized
                            output("IMP_MATH_SMALL_FUNC constexpr vec(uninit) {}\n");

                            // Element-wise
                            output("IMP_MATH_SMALL_FUNC constexpr vec(",Fields(", ","type @"),") : ",Fields(", ","@(@)")," {}\n");

                            // Fill with a single value
                            output("IMP_MATH_SMALL_FUNC explicit constexpr vec(type obj) : ",Fields(", ","@(obj)")," {}\n");

                            // Converting
                            output("template <scalar U> IMP_MATH_SMALL_FUNC explicit(!safely_convertible_to<U,T>) constexpr vec(vec",w,"<U> obj) : ",Fields(", ","@(obj.@)")," {}\n");
                        }

                        { // Customization point constructor and conversion operator
                            output(1+R"(
                                template <typename U> requires Custom::convertible<U, vec> explicit constexpr vec(const U &obj) {*this = Custom::Convert<U, vec>{}(obj);}
                                template <typename U> requires Custom::convertible<vec, U> explicit operator U() const {return Custom::Convert<vec, U>{}(*this);}
                            )");
                        }

                        { // Convert to type
                            output("template <scalar U> [[nodiscard]] constexpr vec",w,"<U> to() const {return vec",w,"<U>(",Fields(", ", "U(@)"),");}\n");
                        }

                        { // Member access
                            // Operator []
                            output("[[nodiscard]] IMP_MATH_SMALL_FUNC constexpr       type &operator[](int i)       {if (!IMP_MATH_IS_CONSTANT(i)) return *(      type *)((      char *)this + sizeof(type)*i);",Fields("", " else if (i == #) return @;")," IMP_MATH_UNREACHABLE();}\n");
                            output("[[nodiscard]] IMP_MATH_SMALL_FUNC constexpr const type &operator[](int i) const {if (!IMP_MATH_IS_CONSTANT(i)) return *(const type *)((const char *)this + sizeof(type)*i);",Fields("", " else if (i == #) return @;")," IMP_MATH_UNREACHABLE();}\n");

                            // As array
                            output("[[nodiscard]] IMP_MATH_SMALL_FUNC type *as_array() {return &x;}\n");
                            output("[[nodiscard]] IMP_MATH_SMALL_FUNC const type *as_array() const {return &x;}\n");
                        }

                        { // Boolean
                            // Convert to bool
                            output("[[nodiscard]] explicit constexpr operator bool() const requires(!std::is_same_v<type, bool>) {return any();} // Use the explicit methods below for vectors of bool.\n");

                            // Any of
                            output("[[nodiscard]] constexpr bool any() const {return ",Fields(" || "),";}\n");

                            // All of
                            output("[[nodiscard]] constexpr bool all() const {return ",Fields(" && "),";}\n");

                            // None of
                            output("[[nodiscard]] constexpr bool none() const {return !any();}\n");

                            // Not all of
                            output("[[nodiscard]] constexpr bool not_all() const {return !all();}\n");
                        }

                        { // Apply operators
                            // Sum
                            output("[[nodiscard]] constexpr auto sum() const {return ", Fields(" + "), ";}\n");

                            // Difference
                            if (w == 2)
                                output("[[nodiscard]] constexpr auto diff() const {return ", Fields(" - "), ";}\n");

                            // Product
                            output("[[nodiscard]] constexpr auto prod() const {return ", Fields(" * "), ";}\n");

                            // Ratio
                            if (w == 2)
                                output("[[nodiscard]] constexpr auto ratio() const {return ", Fields(" / ","floating_point_t<type>(@)"), ";}\n");

                            // Min
                            output("[[nodiscard]] constexpr type min() const {return std::min({", Fields(","), "});}\n");
                            // Max
                            output("[[nodiscard]] constexpr type max() const {return std::max({", Fields(","), "});}\n");

                            // Abs
                            output("[[nodiscard]] constexpr vec abs() const {return vec(", Fields(", ", "std::abs(@)"), ");}\n");

                            // Index
                            // Can't forward the elements here, since the indices can repeat.
                            output("template <typename C> [[nodiscard]] constexpr auto index(C &&container) const -> vec<common_vec_size_v<",w,",vec_size_v<std::decay_t<decltype(container[x])>>>,vec_base_t<std::decay_t<decltype(container[x])>>> {return {",Fields(", ", "Math::vec_elem(#,container[@])"),"};}\n");
                        }

                        { // Resize
                            for (int i = 2; i <= 4; i++)
                            {
                                if (i == w)
                                    continue;
                                output("[[nodiscard]] constexpr vec",i,"<type> to_vec",i,"(");
                                for (int j = w; j < i; j++)
                                {
                                    if (j != w)
                                        output(", ");
                                    output("type n",data::fields[j]);
                                }
                                output(") const {return {");
                                for (int j = 0; j < i; j++)
                                {
                                    if (j != 0)
                                        output(", ");
                                    if (j >= w)
                                        output("n");
                                    output(data::fields[j]);
                                }
                                output("};}\n");
                            }
                            for (int i = w+1; i <= 4; i++)
                            {
                                output("[[nodiscard]] constexpr vec",i,"<type> to_vec",i,"() const {return {");
                                for (int j = 0; j < i; j++)
                                {
                                    if (j != 0)
                                        output(", ");

                                    if (j >= w)
                                        output("0");
                                    else
                                        output(data::fields[j]);
                                }
                                output("};}\n");
                            }
                        }

                        { // Length and normalization
                            // Squared length
                            output("[[nodiscard]] constexpr auto len_sqr() const {return ");
                            for (int i = 0; i < w; i++)
                            {
                                if (i != 0)
                                    output(" + ");
                                output(data::fields[i],"*",data::fields[i]);
                            }
                            output(";}\n");

                            // Length
                            output("[[nodiscard]] constexpr auto len() const {return std::sqrt(len_sqr());}\n");

                            // Normalize
                            output("[[nodiscard]] constexpr auto norm() const -> vec",w,"<decltype(type{}/len())> {if (auto l = len()) return *this / l; else return vec(0);}\n");

                            // Approximate length and normalization.
                            output("[[nodiscard]] constexpr auto approx_len() const {return floating_point_t<type>(len_sqr() + 1) / 2;} // Accurate only around `len()==1`.\n");
                            output("[[nodiscard]] constexpr auto approx_inv_len() const {return 2 / floating_point_t<type>(len_sqr() + 1);}\n");
                            output("[[nodiscard]] constexpr auto approx_norm() const {return *this * approx_inv_len();} // Guaranteed to converge to `len()==1` eventually, when starting from any finite `len_sqr()`.\n");
                        }

                        { // Angles and directions
                            // Construct axis of specific length.
                            output("[[nodiscard]] static constexpr vec axis(int a, type len = 1) {vec ret{}; ret[mod_ex(a,",w,")] = len; return ret;}\n");
                            // Zero all components except the specified one.
                            output("[[nodiscard]] constexpr vec keep_component(int a) {vec ret{}; a = mod_ex(a,",w,"); ret[a] = (*this)[a]; return ret;}\n");

                            if (w == 2)
                            {
                                // Construct from angle
                                output("[[nodiscard]] static constexpr vec dir(type angle, type len = 1) requires is_floating_point {return vec(std::cos(angle) * len, std::sin(angle) * len);}\n");

                                // Get angle
                                output("template <scalar U = floating_point_t<type>> [[nodiscard]] constexpr U angle() const {return std::atan2(U(y), U(x));}\n"); // Note that atan2 is well-defined even when applied to (0,0).

                                // Rotate by 90 degree increments
                                output("[[nodiscard]] constexpr vec rot90(int steps = 1) const {switch (steps & 3) {default: return *this; case 1: return {-y,x}; case 2: return -*this; case 3: return {y,-x};}}\n");

                                // Return one of the 4 main directions, `vec2(1,0).rot90(index)`
                                output("[[nodiscard]] static constexpr vec dir4(int index, type len = 1) {return vec(len,0).rot90(index);}\n");
                                // Return one of the 4 diagonal directions, `vec2(1,1).rot90(index)`
                                output("[[nodiscard]] static constexpr vec dir4_diag(int index, type len = 1) {return vec(len,len).rot90(index);}\n");

                                // Return one of the 8 main directions (including diagonals).
                                output("[[nodiscard]] static constexpr vec dir8(int index, type len = 1) {vec array[8]{vec(len,0),vec(len,len),vec(0,len),vec(-len,len),vec(-len,0),vec(-len,-len),vec(0,-len),vec(len,-len)}; return array[index & 7];}\n");

                                // Get the 4-direction
                                output("[[nodiscard]] constexpr int angle4_round() const {type s = sum(); type d = diff(); return d<0&&s>=0?1:x<0&&d<=0?2:y<0&&s<=0?3:0;} // Non-cardinal directions round to the closest one, diagnoals round backwards, (0,0) returns zero.\n");
                                output("[[nodiscard]] constexpr int angle4_floor() const {return y>0&&x<=0?1:x<0?2:y<0?3:0;}\n");

                                // Get the 8-direction
                                output("[[nodiscard]] constexpr int angle8_sign() const {return y>0?(x>0?1:x==0?2:3):y<0?(x<0?5:x==0?6:7):(x<0?4:0);} // Non-cardinal directions count as diagonals, (0,0) returns zero.\n");
                                output("[[nodiscard]] constexpr int angle8_floor() const {type s = sum(); type d = diff(); return y<0&&d>=0?(x<0?5:s<0?6:7):x<=0&&d<0?(y<=0?4:s<=0?3:2):y>0&&d<=0?1:0;}\n");
                            }
                        }

                        { // Dot and cross products
                            // Dot product
                            output("template <typename U> [[nodiscard]] constexpr auto dot(const vec",w,"<U> &o) const {return ");
                            for (int i = 0; i < w; i++)
                            {
                                if (i != 0)
                                    output(" + ");
                                output(data::fields[i]," * o.",data::fields[i]);
                            }
                            output(";}\n");

                            // Cross product
                            if (w == 3)
                                output("template <typename U> [[nodiscard]] constexpr auto cross(const vec3<U> &o) const -> vec3<decltype(x * o.x - x * o.x)> {return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};}\n");

                            // Cross product z component
                            if (w == 2)
                                output("template <typename U> [[nodiscard]] constexpr auto cross(const vec2<U> &o) const {return x * o.y - y * o.x;}\n");
                        }

                        { // Tie
                            output("[[nodiscard]] constexpr auto tie() & {return std::tie(",Fields(","),");}\n");
                            output("[[nodiscard]] constexpr auto tie() const & {return std::tie(",Fields(","),");}\n");
                        }

                        { // Get
                            output("template <int I> [[nodiscard]] constexpr type &get() & {return std::get<I>(tie());}\n");
                            output("template <int I> [[nodiscard]] constexpr const type &get() const & {return std::get<I>(tie());}\n");
                        }

                        { // Comparison helpers
                            for (const std::string &mode : data::compare_modes)
                                output("[[nodiscard]] IMP_MATH_SMALL_FUNC constexpr compare_",mode,"<vec> operator()(compare_",mode,"_tag) const {return compare_",mode,"(*this);}\n");
                        }

                        { // Rect helpers
                            output("[[nodiscard]] IMP_MATH_SMALL_FUNC constexpr rect",w,"<T> tiny_rect() const {return rect_to(next_value(*this));}\n");
                            output("template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr rect",w,"<larger_t<T,U>> rect_to(vec",w,"<U> b) const {rect",w,"<larger_t<T,U>> ret; ret.a = *this; ret.b = b; return ret;}\n");
                            output("template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr rect",w,"<larger_t<T,U>> rect_size(vec",w,"<U> b) const {return rect_to(*this + b);}\n");
                            output("template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr rect",w,"<larger_t<T,U>> rect_size(U b) const {return rect_size(vec",w,"<U>(b));}\n");
                            output("template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr rect",w,"<larger_t<T,U>> centered_rect_size(vec",w,"<U> b) const {return (*this - b/2).rect_size(b);}\n");
                            output("template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr rect",w,"<larger_t<T,U>> centered_rect_size(U b) const {return centered_rect_size(vec",w,"<U>(b));}\n");
                            output("template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr rect",w,"<larger_t<T,U>> centered_rect_halfsize(vec",w,"<U> b) const {return (*this - b).rect_to(*this + b);}\n");
                            output("template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr rect",w,"<larger_t<T,U>> centered_rect_halfsize(U b) const {return centered_rect_halfsize(vec",w,"<U>(b));}\n");
                        }
                    });
                }

                next_line();

                // Deduction guides
                output("template <typename ...P> requires(sizeof...(P) >= 2 && sizeof...(P) <= 4) vec(P...) -> vec<sizeof...(P), larger_t<P...>>;\n");
            });

            next_line();

            decorative_section("Matrices", [&]
            {
                for (int w = 2; w <= 4; w++)
                for (int h = 2; h <= 4; h++)
                {
                    if (w != 2 || h != 2)
                        next_line();

                    section_sc(make_str("template <typename T> struct mat<",w,",",h,",T> // mat", w, "x", h), [&]
                    {
                        // In pattern, `@` = field name, `#` field index.
                        auto LargeFields = [&](std::string fold_op, std::string pattern = "@") -> std::string
                        {
                            std::string ret;
                            for (int i = 0; i < w; i++)
                            {
                                if (i != 0)
                                    ret += fold_op;
                                for (char ch : pattern)
                                {
                                    if (ch == '@')
                                        ret += data::fields[i];
                                    else if (ch == '#')
                                        ret += std::to_string(i);
                                    else
                                        ret += ch;
                                }
                            }
                            return ret;
                        };
                        auto SmallFields = [&](std::string fold_op, std::string pre = "", std::string post = "", std::string mid = ".") -> std::string
                        {
                            std::string ret;
                            for (int y = 0; y < h; y++)
                            for (int x = 0; x < w; x++)
                            {
                                if (x != 0 || y != 0)
                                    ret += fold_op;
                                ret += pre + data::fields[x] + mid + data::fields[y] + post;
                            }
                            return ret;
                        };

                        { // Aliases
                            output("using type = T;\n");
                            output("using member_type = vec", h,"<T>;\n");
                        }

                        { // Properties
                            output("static constexpr int width = ",w,", height = ",h,";\n");
                            if (w == h)
                                output("static constexpr int size = ",w,";\n");

                            output("static constexpr bool is_floating_point = floating_point_scalar<type>;\n");
                        }

                        { // Members
                            output("member_type ",LargeFields(", "),";\n");
                        }

                        { // Member aliases.
                            for (int j = 0; j < data::fields_alt_count; j++)
                            {
                                for (int i = 0; i < w; i++)
                                {
                                    output("[[nodiscard]] IMP_MATH_SMALL_FUNC constexpr type &",data::fields_alt[j][i],"() {return ",data::fields[i],";} ");
                                    output("[[nodiscard]] IMP_MATH_SMALL_FUNC constexpr const type &",data::fields_alt[j][i],"() const {return ",data::fields[i],";}\n");
                                }
                            }
                        }

                        { // Constructors
                            // Default
                            output("constexpr mat() : mat(");
                            for (int y = 0; y < h; y++)
                            for (int x = 0; x < w; x++)
                            {
                                if (x || y)
                                    output(",");
                                output("01"[x == y]);
                            }
                            output(") {}\n");

                            // Uninitialized
                            output("constexpr mat(uninit) : ",LargeFields(", ", "@(uninit{})")," {}\n");

                            // Element-wise
                            output("constexpr mat(",LargeFields(", ","const member_type &@"),") : ");
                            for (int i = 0; i < w; i++)
                            {
                                if (i != 0)
                                    output(", ");
                                output(data::fields[i],"(",data::fields[i],")");
                            }
                            output(" {}\n");

                            // Matrix element-wise
                            output("constexpr mat(",SmallFields(", ","type ","",""),") : ");
                            for (int x = 0; x < w; x++)
                            {
                                if (x != 0)
                                    output(", ");
                                output(data::fields[x],"(");
                                for (int y = 0; y < h; y++)
                                {
                                    if (y != 0)
                                        output(",");
                                    output(data::fields[x],data::fields[y]);
                                }
                                output(")");
                            }
                            output(" {}\n");

                            // Converting
                            output("template <scalar U> explicit(!safely_convertible_to<U,T>) constexpr mat(const mat",w,"x",h,"<U> &obj) : ");
                            for (int i = 0; i < w; i++)
                            {
                                if (i != 0)
                                    output(", ");
                                output(data::fields[i],"(obj.",data::fields[i],")");
                            }
                            output(" {}\n");
                        }

                        { // Customization point constructor and conversion operator
                            output(1+R"(
                                template <typename U> requires Custom::convertible<U, mat> explicit constexpr mat(const U &obj) {*this = Custom::Convert<U, mat>{}(obj);}
                                template <typename U> requires Custom::convertible<mat, U> explicit operator U() const {return Custom::Convert<mat, U>{}(*this);}
                            )");
                        }

                        { // Convert to type
                            output("template <scalar U> [[nodiscard]] constexpr mat",w,"x",h,"<U> to() const {return mat",w,"x",h,"<U>(",SmallFields(", ","U(",")"),");}\n");
                        }

                        { // Member access
                            // Operator []
                            output("[[nodiscard]] IMP_MATH_SMALL_FUNC constexpr       member_type &operator[](int i)       {if (!IMP_MATH_IS_CONSTANT(i)) return *(      member_type *)((      char *)this + sizeof(member_type)*i);",LargeFields("", " else if (i == #) return @;")," IMP_MATH_UNREACHABLE();}\n");
                            output("[[nodiscard]] IMP_MATH_SMALL_FUNC constexpr const member_type &operator[](int i) const {if (!IMP_MATH_IS_CONSTANT(i)) return *(const member_type *)((const char *)this + sizeof(member_type)*i);",LargeFields("", " else if (i == #) return @;")," IMP_MATH_UNREACHABLE();}\n");

                            // As array
                            output("[[nodiscard]] IMP_MATH_SMALL_FUNC type *as_array() {return &x.x;}\n");
                            output("[[nodiscard]] IMP_MATH_SMALL_FUNC const type *as_array() const {return &x.x;}\n");
                        }

                        { // Resize
                            // One-dimensional
                            for (int i = 2; i <= 4; i++)
                            {
                                if (i == w)
                                    continue;
                                output("[[nodiscard]] constexpr mat",i,"x",h,"<type> to_vec",i,"(");
                                for (int j = w; j < i; j++)
                                {
                                    if (j != w)
                                        output(", ");
                                    output("const member_type &n",data::fields[j]);
                                }
                                output(") const {return {");
                                for (int j = 0; j < i; j++)
                                {
                                    if (j != 0)
                                        output(", ");
                                    if (j >= w)
                                        output("n");
                                    output(data::fields[j]);
                                }
                                output("};}\n");
                            }
                            for (int i = w+1; i <= 4; i++)
                            {
                                output("[[nodiscard]] constexpr mat",i,"x",h,"<type> to_vec",i,"() const {return to_vec",i,"(");
                                for (int j = w; j < i; j++)
                                {
                                    if (j != w)
                                        output(", ");
                                    output("{}");
                                }
                                output(");}\n");
                            }

                            // Two-dimensional
                            for (int hhh = 2; hhh <= 4; hhh++)
                            {
                                for (int www = 2; www <= 4; www++)
                                {
                                    if (www == w && hhh == h)
                                        continue;
                                    output("[[nodiscard]] constexpr mat",www,"x",hhh,"<type> to_mat",www,"x",hhh,"() const {return {");
                                    for (int hh = 0; hh < hhh; hh++)
                                    {
                                        for (int ww = 0; ww < www; ww++)
                                        {
                                            if (ww != 0 || hh != 0)
                                                output(",");
                                            if (ww < w && hh < h)
                                                output(data::fields[ww],".",data::fields[hh]);
                                            else
                                                output("01"[ww == hh]);
                                        }
                                    }
                                    output("};}\n");
                                    if (www == hhh)
                                        output("[[nodiscard]] constexpr mat",www,"x",hhh,"<type> to_mat",www,"() const {return to_mat",www,"x",www,"();}\n");
                                }
                            }
                        }

                        { // Transpose
                            output("[[nodiscard]] constexpr mat",h,"x",w,"<T> transpose() const {return {");
                            for (int x = 0; x < w; x++)
                            for (int y = 0; y < h; y++)
                            {
                                if (x != 0 || y != 0)
                                    output(",");
                                output(data::fields[x],".",data::fields[y]);
                            }
                            output("};}\n");
                        }

                        { // Inverse
                            if (w == h)
                            {
                                // NOTE: `ret{}` is used instead of `ret`, because otherwise those functions wouldn't be constexpr due to an uninitialized variable.

                                switch (w)
                                {
                                  case 2:
                                    output(1+R"(
                                        [[nodiscard]] constexpr mat inverse() requires is_floating_point
                                        {
                                            mat ret{};

                                            ret.x.x =  y.y;
                                            ret.y.x = -y.x;

                                            type d = x.x * ret.x.x + x.y * ret.y.x;
                                            if (d == 0) return {};
                                            d = 1 / d;
                                            ret.x.x *= d;
                                            ret.y.x *= d;

                                            ret.x.y = (-x.y) * d;
                                            ret.y.y = ( x.x) * d;

                                            return ret;
                                        }
                                    )");
                                    break;
                                  case 3:
                                    output(1+R"(
                                        [[nodiscard]] constexpr mat inverse() const requires is_floating_point
                                        {
                                            mat ret{};

                                            ret.x.x =  y.y * z.z - z.y * y.z;
                                            ret.y.x = -y.x * z.z + z.x * y.z;
                                            ret.z.x =  y.x * z.y - z.x * y.y;

                                            type d = x.x * ret.x.x + x.y * ret.y.x + x.z * ret.z.x;
                                            if (d == 0) return {};
                                            d = 1 / d;
                                            ret.x.x *= d;
                                            ret.y.x *= d;
                                            ret.z.x *= d;

                                            ret.x.y = (-x.y * z.z + z.y * x.z) * d;
                                            ret.y.y = ( x.x * z.z - z.x * x.z) * d;
                                            ret.z.y = (-x.x * z.y + z.x * x.y) * d;
                                            ret.x.z = ( x.y * y.z - y.y * x.z) * d;
                                            ret.y.z = (-x.x * y.z + y.x * x.z) * d;
                                            ret.z.z = ( x.x * y.y - y.x * x.y) * d;

                                            return ret;
                                        }
                                    )");
                                    break;
                                  case 4:
                                    output(1+R"(
                                        [[nodiscard]] constexpr mat inverse() const requires is_floating_point
                                        {
                                            mat ret;

                                            ret.x.x =  y.y * z.z * w.w - y.y * z.w * w.z - z.y * y.z * w.w + z.y * y.w * w.z + w.y * y.z * z.w - w.y * y.w * z.z;
                                            ret.y.x = -y.x * z.z * w.w + y.x * z.w * w.z + z.x * y.z * w.w - z.x * y.w * w.z - w.x * y.z * z.w + w.x * y.w * z.z;
                                            ret.z.x =  y.x * z.y * w.w - y.x * z.w * w.y - z.x * y.y * w.w + z.x * y.w * w.y + w.x * y.y * z.w - w.x * y.w * z.y;
                                            ret.w.x = -y.x * z.y * w.z + y.x * z.z * w.y + z.x * y.y * w.z - z.x * y.z * w.y - w.x * y.y * z.z + w.x * y.z * z.y;

                                            type d = x.x * ret.x.x + x.y * ret.y.x + x.z * ret.z.x + x.w * ret.w.x;
                                            if (d == 0) return {};
                                            d = 1 / d;
                                            ret.x.x *= d;
                                            ret.y.x *= d;
                                            ret.z.x *= d;
                                            ret.w.x *= d;

                                            ret.x.y = (-x.y * z.z * w.w + x.y * z.w * w.z + z.y * x.z * w.w - z.y * x.w * w.z - w.y * x.z * z.w + w.y * x.w * z.z) * d;
                                            ret.y.y = ( x.x * z.z * w.w - x.x * z.w * w.z - z.x * x.z * w.w + z.x * x.w * w.z + w.x * x.z * z.w - w.x * x.w * z.z) * d;
                                            ret.z.y = (-x.x * z.y * w.w + x.x * z.w * w.y + z.x * x.y * w.w - z.x * x.w * w.y - w.x * x.y * z.w + w.x * x.w * z.y) * d;
                                            ret.w.y = ( x.x * z.y * w.z - x.x * z.z * w.y - z.x * x.y * w.z + z.x * x.z * w.y + w.x * x.y * z.z - w.x * x.z * z.y) * d;
                                            ret.x.z = ( x.y * y.z * w.w - x.y * y.w * w.z - y.y * x.z * w.w + y.y * x.w * w.z + w.y * x.z * y.w - w.y * x.w * y.z) * d;
                                            ret.y.z = (-x.x * y.z * w.w + x.x * y.w * w.z + y.x * x.z * w.w - y.x * x.w * w.z - w.x * x.z * y.w + w.x * x.w * y.z) * d;
                                            ret.z.z = ( x.x * y.y * w.w - x.x * y.w * w.y - y.x * x.y * w.w + y.x * x.w * w.y + w.x * x.y * y.w - w.x * x.w * y.y) * d;
                                            ret.w.z = (-x.x * y.y * w.z + x.x * y.z * w.y + y.x * x.y * w.z - y.x * x.z * w.y - w.x * x.y * y.z + w.x * x.z * y.y) * d;
                                            ret.x.w = (-x.y * y.z * z.w + x.y * y.w * z.z + y.y * x.z * z.w - y.y * x.w * z.z - z.y * x.z * y.w + z.y * x.w * y.z) * d;
                                            ret.y.w = ( x.x * y.z * z.w - x.x * y.w * z.z - y.x * x.z * z.w + y.x * x.w * z.z + z.x * x.z * y.w - z.x * x.w * y.z) * d;
                                            ret.z.w = (-x.x * y.y * z.w + x.x * y.w * z.y + y.x * x.y * z.w - y.x * x.w * z.y - z.x * x.y * y.w + z.x * x.w * y.y) * d;
                                            ret.w.w = ( x.x * y.y * z.z - x.x * y.z * z.y - y.x * x.y * z.z + y.x * x.z * z.y + z.x * x.y * y.z - z.x * x.z * y.y) * d;

                                            return ret;
                                        }
                                    )");
                                    break;
                                }
                            }
                        }

                        { // Matrix presets
                            auto MakePreset = [&](int min_sz, int max_sz, std::string name, std::string params, std::string param_names, std::string body, bool float_only = 1)
                            {
                                if (w != h)
                                    return;

                                if (w == min_sz)
                                {
                                    output("[[nodiscard]] static constexpr mat ",name,"(",params,")",float_only?" requires is_floating_point":"","\n{\n");
                                    output(body,"}\n");
                                }
                                else if (w >= min_sz && w <= max_sz)
                                {
                                    output("[[nodiscard]] static constexpr mat ",name,"(",params,") {return mat",min_sz,"<T>::",name,"(",param_names,").to_mat",w,"();}\n");
                                }
                            };

                            MakePreset(2, 3, "scale", "vec2<type> v", "v", 1+R"(
                                return { v.x , 0   ,
                                    $    0   , v.y };
                            )", false);

                            MakePreset(3, 4, "scale", "vec3<type> v", "v", 1+R"(
                                return { v.x , 0   , 0   ,
                                    $    0   , v.y , 0   ,
                                    $    0   , 0   , v.z };
                            )", false);

                            MakePreset(3, 3, "ortho", "vec2<type> min, vec2<type> max", "min, max", 1+R"(
                                return { 2 / (max.x - min.x) , 0                   , (min.x + max.x) / (min.x - max.x) ,
                                    $    0                   , 2 / (max.y - min.y) , (min.y + max.y) / (min.y - max.y) ,
                                    $    0                   , 0                   , 1                                 };
                            )");

                            MakePreset(4, 4, "ortho", "vec2<type> min, vec2<type> max, type near, type far", "min, max, near, far", 1+R"(
                                return { 2 / (max.x - min.x) , 0                   , 0                , (min.x + max.x) / (min.x - max.x) ,
                                    $    0                   , 2 / (max.y - min.y) , 0                , (min.y + max.y) / (min.y - max.y) ,
                                    $    0                   , 0                   , 2 / (near - far) , (near + far) / (near - far)       ,
                                    $    0                   , 0                   , 0                , 1                                 };
                            )");

                            MakePreset(4, 4, "look_at", "vec3<type> src, vec3<type> dst, vec3<type> local_up", "src, dst, local_up", 1+R"(
                                vec3<type> v3 = (src-dst).norm();
                                vec3<type> v1 = local_up.cross(v3).norm();
                                vec3<type> v2 = v3.cross(v1);
                                return { v1.x , v1.y , v1.z , -src.x*v1.x-src.y*v1.y-src.z*v1.z ,
                                    $    v2.x , v2.y , v2.z , -src.x*v2.x-src.y*v2.y-src.z*v2.z ,
                                    $    v3.x , v3.y , v3.z , -src.x*v3.x-src.y*v3.y-src.z*v3.z ,
                                    $    0    , 0    , 0    , 1                                 };
                            )");

                            MakePreset(3, 3, "translate", "vec2<type> v", "v", 1+R"(
                                return { 1, 0, v.x ,
                                    $    0, 1, v.y ,
                                    $    0, 0, 1   };
                            )", false);

                            MakePreset(4, 4, "translate", "vec3<type> v", "v", 1+R"(
                                return { 1 , 0 , 0 , v.x ,
                                    $    0 , 1 , 0 , v.y ,
                                    $    0 , 0 , 1 , v.z ,
                                    $    0 , 0 , 0 , 1   };
                            )", false);

                            MakePreset(2, 3, "rotate", "type angle", "angle", 1+R"(
                                type c = std::cos(angle);
                                type s = std::sin(angle);
                                return { c, -s ,
                                    $    s, c  };
                            )");

                            MakePreset(3, 4, "rotate_with_normalized_axis", "vec3<type> axis, type angle", "axis, angle", 1+R"(
                                type c = std::cos(angle);
                                type s = std::sin(angle);
                                return { axis.x * axis.x * (1 - c) + c          , axis.x * axis.y * (1 - c) - axis.z * s , axis.x * axis.z * (1 - c) + axis.y * s,
                                    $    axis.y * axis.x * (1 - c) + axis.z * s , axis.y * axis.y * (1 - c) + c          , axis.y * axis.z * (1 - c) - axis.x * s,
                                    $    axis.x * axis.z * (1 - c) - axis.y * s , axis.y * axis.z * (1 - c) + axis.x * s , axis.z * axis.z * (1 - c) + c         };
                            )", false);
                            MakePreset(3, 4, "rotate", "vec3<type> axis, type angle", "axis, angle", 1+R"(
                                return rotate_with_normalized_axis(axis.norm(), angle);
                            )");

                            MakePreset(4, 4, "perspective", "type wh_aspect, type y_fov, type near, type far", "wh_aspect, y_fov, near, far", 1+R"(
                                y_fov = type(1) / std::tan(y_fov / 2);
                                return { y_fov / wh_aspect , 0     , 0                           , 0                             ,
                                    $    0                 , y_fov , 0                           , 0                             ,
                                    $    0                 , 0     , (near + far) / (near - far) , 2 * near * far / (near - far) ,
                                    $    0                 , 0     , -1                          , 0                             };
                            )");
                        }
                    });
                }

                next_line();

                { // Deduction guides
                    // From scalars
                    for (int w = 2; w <= 4; w++)
                        output("template <scalar ...P> requires(sizeof...(P) == ",w*w,") mat(P...) -> mat<",w,", ",w,", larger_t<P...>>;\n");

                    // From vectors
                    for (int h = 2; h <= 4; h++)
                        output("template <typename ...P> requires(sizeof...(P) >= 2 && sizeof...(P) <= 4 && ((vec_size_v<P> == ",h,") && ...)) mat(P...) -> mat<sizeof...(P), ",h,", larger_t<typename P::type...>>;\n");
                }
            });

            next_line();

            decorative_section("Rects", [&]
            {
                output(1+R"(
                    template <int D, scalar T> struct rect
                    {
                        using type = T;
                        using vec_type = vec<D,T>;
                        static constexpr int dim = D; // `size` is already used as a function name.
                        static constexpr bool is_floating_point = floating_point_scalar<type>;
                        vec_type a, b; // `a` is inclusive, `b` is exclusive.
                        IMP_MATH_SMALL_FUNC constexpr rect() {} // No fancy constructors, use helpers in `vec`.
                        IMP_MATH_SMALL_FUNC constexpr rect(uninit) : a(uninit{}), b(uninit{}) {}
                        template <scalar U> IMP_MATH_SMALL_FUNC explicit(!safely_convertible_to<U,T>) constexpr rect(rect<D,U> r) : a(r.a), b(r.b) {}
                        template <scalar U> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr rect<D,U> to() const {return vec<D,U>(a).rect_to(vec<D,U>(b));}
                        [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr vec_type size() const {return b - a;}
                        [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr bool has_length() const {return (b > a).any();}
                        [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr bool has_area() const {return (b > a).all();}
                        [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr rect fix() const {rect ret = *this; sort_two_var(ret.a, ret.b); return ret;} // Swap components of `a` and `b` to order them correctly.
                        // Offsetting.
                        template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr rect<D,larger_t<T,U>> offset_a(vec<D,U> x) const {rect<D,larger_t<T,U>> ret = *this; ret.a += x; return ret;}
                        template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr rect<D,larger_t<T,U>> offset_a(U        x) const {rect<D,larger_t<T,U>> ret = *this; ret.a += x; return ret;}
                        template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr rect<D,larger_t<T,U>> offset_b(vec<D,U> x) const {rect<D,larger_t<T,U>> ret = *this; ret.b += x; return ret;}
                        template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr rect<D,larger_t<T,U>> offset_b(U        x) const {rect<D,larger_t<T,U>> ret = *this; ret.b += x; return ret;}
                        template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr rect<D,larger_t<T,U>> offset  (vec<D,U> x) const {return offset_a(x).offset_b(x);}
                        template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr rect<D,larger_t<T,U>> offset  (U        x) const {return offset_a(x).offset_b(x);}
                        // Operators. Those apply to both corners. Don't forget to `.fix()` the rect if you negate it.
                        [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr rect operator+() const {return *this;}
                        [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr rect operator-() const {return (-a).rect_to(-b);}
                    )");
                    for (const char *op : {"+","-","*","/"})
                    {
                        output(
                            "template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC friend constexpr rect<D,larger_t<T,U>> operator",op,"(rect r, vec<D,U> x) {return (r.a ",op," x).rect_to(r.b ",op," x);}\n"
                            "template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC friend constexpr rect<D,larger_t<T,U>> operator",op,"(rect r, U        x) {return (r.a ",op," x).rect_to(r.b ",op," x);}\n"
                            "template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC friend constexpr rect<D,larger_t<T,U>> operator",op,"(vec<D,U> x, rect r) {return (x ",op," r.a).rect_to(x ",op," r.b);}\n"
                            "template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC friend constexpr rect<D,larger_t<T,U>> operator",op,"(U        x, rect r) {return (x ",op," r.a).rect_to(x ",op," r.b);}\n"
                            "template <safely_convertible_to<T> U = T> IMP_MATH_SMALL_FUNC friend constexpr rect<D,larger_t<T,U>> operator",op,"=(rect &r, vec<D,U> x) {r = r ",op," x; return r;}\n"
                            "template <safely_convertible_to<T> U = T> IMP_MATH_SMALL_FUNC friend constexpr rect<D,larger_t<T,U>> operator",op,"=(rect &r, U        x) {r = r ",op," x; return r;}\n"
                        );
                    }
                    output(1+R"(
                        // Expanding and shrinking.
                        template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr rect<D,larger_t<T,U>> expand(vec<D,U> x) const {return offset_a(-x).offset_b(x);}
                        template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr rect<D,larger_t<T,U>> expand(U        x) const {return offset_a(-x).offset_b(x);}
                        template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr rect<D,larger_t<T,U>> shrink(vec<D,U> x) const {return offset_a(x).offset_b(-x);}
                        template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr rect<D,larger_t<T,U>> shrink(U        x) const {return offset_a(x).offset_b(-x);}
                        template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr rect<D,larger_t<T,U>> expand_dir(vec<D,U> x) const {return offset_a(min(x,larger_t<T,U>{})).offset_b(max(x,larger_t<T,U>{}));}
                        template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr rect<D,larger_t<T,U>> expand_dir(U        x) const {return expand_dir(vec<D,U>(x));}
                        template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr rect<D,larger_t<T,U>> shrink_dir(vec<D,U> x) const {return offset_a(max(x,larger_t<T,U>{})).offset_b(min(x,larger_t<T,U>{}));}
                        template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr rect<D,larger_t<T,U>> shrink_dir(U        x) const {return shrink_dir(vec<D,U>(x));}
                        // Checking collisions.
                        template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr bool contains(vec<D,U> p) const {return (p >= a).all() && (p </*sic*/ b).all();}
                        template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr bool contains(rect<D,U> r) const {return (r.a >= a).all() && (r.b <= b).all();}
                        template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr bool touches(rect r) const {return (r.a < b).all() && (r.b > a).all();}
                        // Modifying the rect.
                        template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr rect<D,larger_t<T,U>> combine(vec<D,U>  p) const {return combine(p.tiny_rect());}
                        template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr rect<D,larger_t<T,U>> combine(rect<D,U> r) const {return min(a, r.a).rect_to(max(b, r.b));}
                        template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr rect<D,larger_t<T,U>> intersect(rect<D,U> r) const {return max(a, r.a).rect_to(min(b, r.b));}
                        template <scalar U = T> [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr vec<D,larger_t<T,U>> clamp(vec<D,U> p) const {return min(max(p, a), prev_value(b)); }
                        // Constructing the contour.
                        [[nodiscard]] IMP_MATH_SMALL_FUNC constexpr vec_type corner(int i) const requires(dim==2) {return vec_type((i+1)&2?b.x:a.x, i&2?b.y:a.y);}
                        [[nodiscard]] constexpr std::array<vec_type, 4> to_contour() const requires(dim==2) {std::array<vec_type, 4> ret; for (int i=0;i<4;i++) ret[i]=corner(i); return ret;}
                        // Comparisons.
                        [[nodiscard]] IMP_MATH_SMALL_FUNC friend constexpr bool operator==(rect x, rect y) {return x.a == y.a && x.b == y.b;}
                    };

                    // input/output
                    template <typename A, typename B, int D, typename T> constexpr std::basic_ostream<A,B> &operator<<(std::basic_ostream<A, B> &s, const rect<D, T> &r)
                    {
                        return s << r.a << ".." << r.b;
                    }
                    template <typename A, typename B, int D, typename T> constexpr std::basic_istream<A,B> &operator>>(std::basic_istream<A, B> &s, rect<D, T> &r)
                    {
                        return s >> r.a >> r.b;
                    }
                )");
            });
        });

        next_line();

        section("namespace Export", []
        {
            output(1+R"(
                using Vector::vec; // Vector and matrix definitions. We use this instead of `using namespace Vector` to avoid bringing...
                using Vector::mat; // ...the overloaded operators into the global namespace, mostly for better error messages and build speed.
                using namespace Alias; // Convenient type aliases.
                using namespace Common; // Common functions.

                // Common types.
                using std::int8_t;
                using std::uint8_t;
                using std::int16_t;
                using std::uint16_t;
                using std::int32_t;
                using std::uint32_t;
                using std::int64_t;
                using std::uint64_t;
                using std::size_t;
                using std::ptrdiff_t;
                using std::intptr_t;
                using std::uintptr_t;

                // Common standard functions.
                using std::sqrt;
                using std::cos;
                using std::sin;
                using std::tan;
                using std::acos;
                using std::asin;
                using std::atan;
                using std::atan2;
            )");
        });
    });

    next_line();

    section("namespace std", []
    {
        output(1+R"(
            template <int D, typename T> struct less         <Math::vec<D,T>> {constexpr bool operator()(const Math::vec<D,T> &a, const Math::vec<D,T> &b) const {return a.tie() <  b.tie();}};
            template <int D, typename T> struct greater      <Math::vec<D,T>> {constexpr bool operator()(const Math::vec<D,T> &a, const Math::vec<D,T> &b) const {return a.tie() >  b.tie();}};
            template <int D, typename T> struct less_equal   <Math::vec<D,T>> {constexpr bool operator()(const Math::vec<D,T> &a, const Math::vec<D,T> &b) const {return a.tie() <= b.tie();}};
            template <int D, typename T> struct greater_equal<Math::vec<D,T>> {constexpr bool operator()(const Math::vec<D,T> &a, const Math::vec<D,T> &b) const {return a.tie() >= b.tie();}};

            template <int D, typename T> struct hash<Math::vec<D,T>>
            {
                std::size_t operator()(const Math::vec<D,T> &v) const
                {
                    std::size_t ret = std::hash<decltype(v.x)>{}(v.x);
                    for (int i = 1; i < D; i++)
                    $   ret ^= std::hash<decltype(v.x)>{}(v[i]) + 0x9e3779b9 + (ret << 6) + (ret >> 2); // From Boost.
                    return ret;
                }
            };
        )");
    });

    next_line();
    output("// Quaternions\n");
    next_line();

    section("namespace Math", []
    {
        output(1+R"#(
            inline namespace Quat // Quaternions.
            {
                template <floating_point_scalar T> struct quat
                {
                    using type = T;
                    using vec3_t = vec3<T>;
                    using vec4_t = vec4<T>;
                    using mat3_t = mat3<T>;
                    type x = 0, y = 0, z = 0, w = 1; // This represents zero rotation.

                    constexpr quat() {}
                    constexpr quat(type x, type y, type z, type w) : x(x), y(y), z(z), w(w) {}
                    explicit constexpr quat(const vec4_t &vec) : x(vec.x), y(vec.y), z(vec.z), w(vec.w) {}

                    // Normalizes the axis. If it's already normalized, use `with_normalized_axis()` instead.
                    constexpr quat(vec3_t axis, type angle) {*this = with_normalized_axis(axis.norm(), angle);}
                    [[nodiscard]] static constexpr quat with_normalized_axis(vec3_t axis, type angle) {angle *= type(0.5); return quat((axis * std::sin(angle)).to_vec4(std::cos(angle)));}

                    [[nodiscard]] constexpr vec4_t as_vec() const {return {x, y, z, w};}
                    [[nodiscard]] constexpr vec3_t xyz() const {return {x, y, z};}
                    [[nodiscard]] type *as_array() {return &x;}
                    [[nodiscard]] const type *as_array() const {return &x;}

                    [[nodiscard]] constexpr quat norm() const {return quat(as_vec().norm());}
                    [[nodiscard]] constexpr quat approx_norm() const {return quat(as_vec().approx_norm());}

                    [[nodiscard]] constexpr vec3_t axis_denorm() const { return xyz(); }
                    [[nodiscard]] constexpr vec3_t axis_norm() const { return xyz().norm(); }
                    [[nodiscard]] constexpr float angle() const { return 2 * std::atan2(xyz().len(), w); }

                    // Negates the rotation. Not strictly an inversion in the mathematical sense, since the length stays unchanged (while it's supposed to become `1 / old_length`).
                    [[nodiscard]] constexpr quat inverse() const {return quat(xyz().to_vec4(-w));}
                    // Negates the three imaginary parts of the quaternion, `xyz`. Effectively inverts the rotation, but works slower than `inverse()`. Useful only for low-level quaternion things.
                    [[nodiscard]] constexpr quat conjugate() const {return quat((-xyz()).to_vec4(w));}

                    // Uses iterative normalization to keep denormalization from accumulating due to lack of precision.
                    template <typename U> [[nodiscard]] constexpr quat<larger_t<T,U>> operator*(const quat<U> &other) const {return mult_without_norm(other).approx_norm();}
                    constexpr quat &operator*=(const quat &other) {return *this = *this * other;}

                    // Simple quaternion multiplication, without any normalization.
                    template <typename U> [[nodiscard]] constexpr quat<larger_t<T,U>> mult_without_norm(const quat<U> &other) const
                    {
                        return quat<larger_t<T,U>>(vec4<larger_t<T,U>>(
                        $   x * other.w + w * other.x - z * other.y + y * other.z,
                        $   y * other.w + z * other.x + w * other.y - x * other.z,
                        $   z * other.w - y * other.x + x * other.y + w * other.z,
                        $   w * other.w - x * other.x - y * other.y - z * other.z
                        ));
                    }

                    // Transforms a vector by this quaternion. Only makes sense if the quaternion is normalized.
                    template <typename U> [[nodiscard]] constexpr vec3<larger_t<T,U>> operator*(const vec3<U> &other) const
                    {
                        // This is called the "Euler-Rodrigues formula".
                        // We could also use `*this * other * this->conjugate()`, but that looks less optimized.
                        vec3<larger_t<T,U>> tmp = xyz().cross(other);
                        return other + 2 * w * tmp + 2 * xyz().cross(tmp);
                    }

                    // Transforms a vector by this quaternion, inversed. Mimics a similar matrix operation.
                    template <typename U> [[nodiscard]] friend constexpr vec3<larger_t<T,U>> operator*(const vec3<U> &v, const quat &q)
                    {
                        return q.inverse() * v;
                    }

                    // Returns a rotation matrix for this quaternion. Only makes sense if the quaternion is normalized.
                    [[nodiscard]] constexpr mat3_t matrix() const
                    {
                        return mat3_t(
                        $   1 - (2*y*y + 2*z*z), 2*x*y - 2*z*w, 2*x*z + 2*y*w,
                        $   2*x*y + 2*z*w, 1 - (2*x*x + 2*z*z), 2*y*z - 2*x*w,
                        $   2*x*z - 2*y*w, 2*y*z + 2*x*w, 1 - (2*x*x + 2*y*y)
                        );
                    }

                    // Returns a rotation matrix for this quaternion. Works even if the quaternion is not normalized.
                    [[nodiscard]] constexpr mat3_t matrix_from_denorm() const
                    {
                        type f = 1 / as_vec().len_sqr();
                        mat3_t m = matrix();
                        return mat3_t(m.x * f, m.y * f, m.z * f);
                    }
                };

                using fquat = quat<float>;
                using dquat = quat<double>;
                using ldquat = quat<long double>;

                template <typename A, typename B, typename T> constexpr std::basic_ostream<A,B> &operator<<(std::basic_ostream<A,B> &s, const quat<T> &q)
                {
                    s.width(0);
                    if (q.axis_denorm() == vec3<T>(0))
                    $   s << "[angle=0";
                    else
                    $   s << "[axis=" << q.axis_denorm()/q.axis_denorm().max() << " angle=" << to_deg(q.angle()) << "(deg)";
                    return s << " len=" << q.as_vec().len() << ']';
                }

                template <typename A, typename B, typename T> constexpr std::basic_istream<A,B> &operator>>(std::basic_istream<A,B> &s, quat<T> &q)
                {
                    vec4<T> vec;
                    s >> vec;
                    q = quat(vec);
                    return s;
                }
            }

            inline namespace Utility
            {
                // Check if `T` is a quaternion type (possibly const).
                template <typename T> struct is_quat_impl : std::false_type {};
                template <typename T> struct is_quat_impl<      quat<T>> : std::true_type {};
                template <typename T> struct is_quat_impl<const quat<T>> : std::true_type {};
                template <typename T> inline constexpr bool is_quat_v = is_quat_impl<T>::value;
            }

            namespace Export
            {
                using namespace Quat;
            }
        )#");
    });

    next_line();

    section("namespace std", []
    {
        output(1+R"(
            template <typename T> struct less         <Math::quat<T>> {constexpr bool operator()(const Math::quat<T> &a, const Math::quat<T> &b) const {return a.as_vec().tie() <  b.as_vec().tie();}};
            template <typename T> struct greater      <Math::quat<T>> {constexpr bool operator()(const Math::quat<T> &a, const Math::quat<T> &b) const {return a.as_vec().tie() >  b.as_vec().tie();}};
            template <typename T> struct less_equal   <Math::quat<T>> {constexpr bool operator()(const Math::quat<T> &a, const Math::quat<T> &b) const {return a.as_vec().tie() <= b.as_vec().tie();}};
            template <typename T> struct greater_equal<Math::quat<T>> {constexpr bool operator()(const Math::quat<T> &a, const Math::quat<T> &b) const {return a.as_vec().tie() >= b.as_vec().tie();}};

            template <typename T> struct hash<Math::quat<T>>
            {
                std::size_t operator()(const Math::quat<T> &q) const
                {
                    return std::hash<Math::vec4<T>>{}(q.as_vec());
                }
            };
        )");
    });

    next_line();

    output("using namespace Math::Export;\n");

    if (!impl::output_file)
        return -1;
}
