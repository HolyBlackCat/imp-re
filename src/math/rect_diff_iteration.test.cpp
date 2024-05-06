#include "rect_diff_iteration.h"

#include "utils/multiarray.h"

#include <doctest/doctest.h>

#include <iostream>

TEST_CASE("math.rect_diff_iteration")
{
    static_assert(std::bidirectional_iterator<Math2::RectDiffIterator<2,int>>);

    auto TestCase = [](irect2 a, irect2 b, const Array2D<int> &target)
    {
        CAPTURE(a);
        CAPTURE(b);

        (void)target;

        for (bool backwards : {false, true})
        {
            CAPTURE(backwards);

            Array2D<int> arr(a.size());

            int i = 1;
            if (!backwards)
            {
                for (ivec2 pos : Math2::RectDiffIterator(a, b))
                {
                    // std::cout << i << pos << '\n';
                    CAPTURE(pos);
                    REQUIRE(a.contains(pos));
                    arr.at(pos - a.a) = i++;
                }
                REQUIRE(arr == target);
            }
            else
            {
                int max_value = *std::max_element(target.elements(), target.elements() + target.element_count());
                Array2D<int> target_fixed(target.size());
                for (xvec2 pos : vector_range(target.size()))
                    target_fixed.at(pos) = target.at(pos) ? max_value + 1 - target.at(pos) : 0;

                for (auto it = Math2::RectDiffIterator(a, b, true); it != std::default_sentinel; --it)
                {
                    ivec2 pos = *it;
                    std::cout << i << pos << '\n';
                    CAPTURE(pos);
                    REQUIRE(a.contains(pos));
                    arr.at(pos - a.a) = i++;
                }
                REQUIRE(arr == target_fixed);
            }

        }
    };

    Array2D<int> target_full(Meta::value_list<7,6>{}, {
         1, 2, 3, 4, 5, 6, 7,
         8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,
        22,23,24,25,26,27,28,
        29,30,31,32,33,34,35,
        36,37,38,39,40,41,42,
    });

    // Second rect doesn't overlap.
    TestCase(ivec2(10,5).rect_size(ivec2(7,6)), ivec2(20,6).rect_size(ivec2(10,1)), target_full);
    // Second rect has zero area.
    TestCase(ivec2(10,5).rect_size(ivec2(7,6)), ivec2(9,8).rect_size(ivec2(10,0)), target_full);

    for (int a : {8,10,12})
    for (int b : {19,17,15})
    for (int c : {3,5,7})
    for (int d : {13,11,9})
    {
        CAPTURE(a);
        CAPTURE(b);
        CAPTURE(c);
        CAPTURE(d);

        Array2D<int> target;

        if (a <= 10)
        {
            if (b >= 17)
            {
                if (c <= 5)
                {
                    if (d >= 11)
                    {
                        target = {Meta::value_list<7,6>{}, {
                             0, 0, 0, 0, 0, 0, 0,
                             0, 0, 0, 0, 0, 0, 0,
                             0, 0, 0, 0, 0, 0, 0,
                             0, 0, 0, 0, 0, 0, 0,
                             0, 0, 0, 0, 0, 0, 0,
                             0, 0, 0, 0, 0, 0, 0,
                        }};
                    }
                    else
                    {
                        target = {Meta::value_list<7,6>{}, {
                             0, 0, 0, 0, 0, 0, 0,
                             0, 0, 0, 0, 0, 0, 0,
                             0, 0, 0, 0, 0, 0, 0,
                             0, 0, 0, 0, 0, 0, 0,
                             1, 2, 3, 4, 5, 6, 7,
                             8, 9,10,11,12,13,14,
                        }};
                    }
                }
                else
                {
                    if (d >= 11)
                    {
                        target = {Meta::value_list<7,6>{}, {
                             1, 2, 3, 4, 5, 6, 7,
                             8, 9,10,11,12,13,14,
                             0, 0, 0, 0, 0, 0, 0,
                             0, 0, 0, 0, 0, 0, 0,
                             0, 0, 0, 0, 0, 0, 0,
                             0, 0, 0, 0, 0, 0, 0,
                        }};
                    }
                    else
                    {
                        target = {Meta::value_list<7,6>{}, {
                             1, 2, 3, 4, 5, 6, 7,
                             8, 9,10,11,12,13,14,
                             0, 0, 0, 0, 0, 0, 0,
                             0, 0, 0, 0, 0, 0, 0,
                            15,16,17,18,19,20,21,
                            22,23,24,25,26,27,28,
                        }};
                    }
                }
            }
            else
            {
                if (c <= 5)
                {
                    if (d >= 11)
                    {
                        target = {Meta::value_list<7,6>{}, {
                             0, 0, 0, 0, 0, 1, 2,
                             0, 0, 0, 0, 0, 3, 4,
                             0, 0, 0, 0, 0, 5, 6,
                             0, 0, 0, 0, 0, 7, 8,
                             0, 0, 0, 0, 0, 9,10,
                             0, 0, 0, 0, 0,11,12,
                        }};
                    }
                    else
                    {
                        target = {Meta::value_list<7,6>{}, {
                             0, 0, 0, 0, 0, 1, 2,
                             0, 0, 0, 0, 0, 3, 4,
                             0, 0, 0, 0, 0, 5, 6,
                             0, 0, 0, 0, 0, 7, 8,
                             9,10,11,12,13,14,15,
                            16,17,18,19,20,21,22,
                        }};
                    }
                }
                else
                {
                    if (d >= 11)
                    {
                        target = {Meta::value_list<7,6>{}, {
                             1, 2, 3, 4, 5, 6, 7,
                             8, 9,10,11,12,13,14,
                             0, 0, 0, 0, 0,15,16,
                             0, 0, 0, 0, 0,17,18,
                             0, 0, 0, 0, 0,19,20,
                             0, 0, 0, 0, 0,21,22,
                        }};
                    }
                    else
                    {
                        target = {Meta::value_list<7,6>{}, {
                             1, 2, 3, 4, 5, 6, 7,
                             8, 9,10,11,12,13,14,
                             0, 0, 0, 0, 0,15,16,
                             0, 0, 0, 0, 0,17,18,
                            19,20,21,22,23,24,25,
                            26,27,28,29,30,31,32,
                        }};
                    }
                }
            }
        }
        else
        {
            if (b >= 17)
            {
                if (c <= 5)
                {
                    if (d >= 11)
                    {
                        target = {Meta::value_list<7,6>{}, {
                             1, 2, 0, 0, 0, 0, 0,
                             3, 4, 0, 0, 0, 0, 0,
                             5, 6, 0, 0, 0, 0, 0,
                             7, 8, 0, 0, 0, 0, 0,
                             9,10, 0, 0, 0, 0, 0,
                            11,12, 0, 0, 0, 0, 0,
                        }};
                    }
                    else
                    {
                        target = {Meta::value_list<7,6>{}, {
                             1, 2, 0, 0, 0, 0, 0,
                             3, 4, 0, 0, 0, 0, 0,
                             5, 6, 0, 0, 0, 0, 0,
                             7, 8, 0, 0, 0, 0, 0,
                             9,10,11,12,13,14,15,
                            16,17,18,19,20,21,22,
                        }};
                    }
                }
                else
                {
                    if (d >= 11)
                    {
                        target = {Meta::value_list<7,6>{}, {
                             1, 2, 3, 4, 5, 6, 7,
                             8, 9,10,11,12,13,14,
                            15,16, 0, 0, 0, 0, 0,
                            17,18, 0, 0, 0, 0, 0,
                            19,20, 0, 0, 0, 0, 0,
                            21,22, 0, 0, 0, 0, 0,
                        }};
                    }
                    else
                    {
                        target = {Meta::value_list<7,6>{}, {
                             1, 2, 3, 4, 5, 6, 7,
                             8, 9,10,11,12,13,14,
                            15,16, 0, 0, 0, 0, 0,
                            17,18, 0, 0, 0, 0, 0,
                            19,20,21,22,23,24,25,
                            26,27,28,29,30,31,32,
                        }};
                    }
                }
            }
            else
            {
                if (c <= 5)
                {
                    if (d >= 11)
                    {
                        target = {Meta::value_list<7,6>{}, {
                             1, 2, 0, 0, 0, 3, 4,
                             5, 6, 0, 0, 0, 7, 8,
                             9,10, 0, 0, 0,11,12,
                            13,14, 0, 0, 0,15,16,
                            17,18, 0, 0, 0,19,20,
                            21,22, 0, 0, 0,23,24,
                        }};
                    }
                    else
                    {
                        target = {Meta::value_list<7,6>{}, {
                             1, 2, 0, 0, 0, 3, 4,
                             5, 6, 0, 0, 0, 7, 8,
                             9,10, 0, 0, 0,11,12,
                            13,14, 0, 0, 0,15,16,
                            17,18,19,20,21,22,23,
                            24,25,26,27,28,29,30,
                        }};
                    }
                }
                else
                {
                    if (d >= 11)
                    {
                        target = {Meta::value_list<7,6>{}, {
                             1, 2, 3, 4, 5, 6, 7,
                             8, 9,10,11,12,13,14,
                            15,16, 0, 0, 0,17,18,
                            19,20, 0, 0, 0,21,22,
                            23,24, 0, 0, 0,25,26,
                            27,28, 0, 0, 0,29,30,
                        }};
                    }
                    else
                    {
                        target = {Meta::value_list<7,6>{}, {
                             1, 2, 3, 4, 5, 6, 7,
                             8, 9,10,11,12,13,14,
                            15,16, 0, 0, 0,17,18,
                            19,20, 0, 0, 0,21,22,
                            23,24,25,26,27,28,29,
                            30,31,32,33,34,35,36,
                        }};
                    }
                }
            }
        }

        TestCase(ivec2(10,5).rect_size(ivec2(7,6)), ivec2(a,c).rect_to(ivec2(b,d)), target);
    }
}
