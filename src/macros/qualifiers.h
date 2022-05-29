#pragma once

/* Example usage:
 *   QUAL_MAYBE_CONST( QUAL int &GetX() QUAL {...} )
 * This expands to:
 *         int &GetX()       {...}
 *   const int &GetX() const {...}
 *
 * Note that if `QUAL` appears inside of parentheses, those parentheses must be preceeded by `QUAL_IN`.
 * Example:
 *   QUAL_MAYBE_CONST( QUAL int &Foo QUAL_IN(QUAL int &x); )
 * This expands to:
 *         int &Foo(      int &x);
 *   const int &Foo(const int &x);
 *
 * We also have `QUAL_CONST_L_OR_R(...)` that gives `const &` and `&&`,
 * and `QUAL_MAYBE_CONST_L_R` that gives `&`, `const &`, `&&`, and `const &&`.
 */
#define QUAL_MAYBE_CONST(...) \
    IMPL_QUAL      ((IMPL_QUAL_null,__VA_ARGS__)()) \
    IMPL_QUAL_const((IMPL_QUAL_null,__VA_ARGS__)())
#define QUAL_CONST_L_OR_R(...) \
    IMPL_QUAL_const_lref((IMPL_QUAL_null,__VA_ARGS__)()) \
    IMPL_QUAL_rref      ((IMPL_QUAL_null,__VA_ARGS__)())
#define QUAL_MAYBE_CONST_L_R(...) \
    IMPL_QUAL_lref      ((IMPL_QUAL_null,__VA_ARGS__)()) \
    IMPL_QUAL_const_lref((IMPL_QUAL_null,__VA_ARGS__)()) \
    IMPL_QUAL_rref      ((IMPL_QUAL_null,__VA_ARGS__)()) \
    IMPL_QUAL_const_rref((IMPL_QUAL_null,__VA_ARGS__)())

#define QUAL )(IMPL_QUAL_identity,
#define QUAL_IN(...) )(IMPL_QUAL_p_open,)(IMPL_QUAL_null,__VA_ARGS__)(IMPL_QUAL_p_close,)(IMPL_QUAL_null,

#define IMPL_QUAL_null(...)
#define IMPL_QUAL_identity(...) __VA_ARGS__
#define IMPL_QUAL_p_open(...) (
#define IMPL_QUAL_p_close(...) )


#define IMPL_QUAL_body(cv, m, ...) m(cv) __VA_ARGS__

#define IMPL_QUAL(seq) IMPL_QUAL_a seq
#define IMPL_QUAL_a(...) __VA_OPT__(IMPL_QUAL_body(,__VA_ARGS__) IMPL_QUAL_b)
#define IMPL_QUAL_b(...) __VA_OPT__(IMPL_QUAL_body(,__VA_ARGS__) IMPL_QUAL_a)

#define IMPL_QUAL_const(seq) IMPL_QUAL_const_a seq
#define IMPL_QUAL_const_a(...) __VA_OPT__(IMPL_QUAL_body(const,__VA_ARGS__) IMPL_QUAL_const_b)
#define IMPL_QUAL_const_b(...) __VA_OPT__(IMPL_QUAL_body(const,__VA_ARGS__) IMPL_QUAL_const_a)

#define IMPL_QUAL_lref(seq) IMPL_QUAL_lref_a seq
#define IMPL_QUAL_lref_a(...) __VA_OPT__(IMPL_QUAL_body(&,__VA_ARGS__) IMPL_QUAL_lref_b)
#define IMPL_QUAL_lref_b(...) __VA_OPT__(IMPL_QUAL_body(&,__VA_ARGS__) IMPL_QUAL_lref_a)

#define IMPL_QUAL_const_lref(seq) IMPL_QUAL_const_lref_a seq
#define IMPL_QUAL_const_lref_a(...) __VA_OPT__(IMPL_QUAL_body(const &,__VA_ARGS__) IMPL_QUAL_const_lref_b)
#define IMPL_QUAL_const_lref_b(...) __VA_OPT__(IMPL_QUAL_body(const &,__VA_ARGS__) IMPL_QUAL_const_lref_a)

#define IMPL_QUAL_rref(seq) IMPL_QUAL_rref_a seq
#define IMPL_QUAL_rref_a(...) __VA_OPT__(IMPL_QUAL_body(&&,__VA_ARGS__) IMPL_QUAL_rref_b)
#define IMPL_QUAL_rref_b(...) __VA_OPT__(IMPL_QUAL_body(&&,__VA_ARGS__) IMPL_QUAL_rref_a)

#define IMPL_QUAL_const_rref(seq) IMPL_QUAL_const_rref_a seq
#define IMPL_QUAL_const_rref_a(...) __VA_OPT__(IMPL_QUAL_body(const &&,__VA_ARGS__) IMPL_QUAL_const_rref_b)
#define IMPL_QUAL_const_rref_b(...) __VA_OPT__(IMPL_QUAL_body(const &&,__VA_ARGS__) IMPL_QUAL_const_rref_a)
