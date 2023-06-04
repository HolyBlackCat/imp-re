#pragma once

namespace ccore::refl
{

}

#define CCORE_MEMBERS(seq)


struct A
{
    MEMBERS(
        int VAR(foo);
        int VAR(bar) = 42;
        ivec2 VAR(vec){1, 2};
    )

    MEMBERS(
        VAR(foo, int);
        VAR(bar, int) = 42;
        VAR(vec, ivec2){1, 2};
    private:
        ATTR(VertexOnly, Optional) ATTR(another) VAR(foo, int);
    )

    MEMBERS(
        TYPE(int) VAR(foo);
        VAR(int, 42) VAR(bar);
        VAR(vec, ivec2){1, 2};
    private:
        ATTR(VertexOnly, Optional) ATTR(another) VAR(foo, int);
    )

    MEMBERS(
        VAR(foo, int{})
        VAR(bar, 42)
        VAR(vec, ivec2{})
        VAR(vec, ivec2(1, 2))
    private:
        UNINITIALIZED_VAR(foo, int)
        ATTR VertexOnly, Optional ATTR another VAR(foo, int{})
    )
}
