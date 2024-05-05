#include "sparse_set.h"

#include <array>
#include <iostream>

#include <doctest/doctest.h>

template class SparseSet<int>;
TEST_CASE("sparse_set.owning")
{
    auto CheckCapacity = [&](const SparseSet<int> &set, int value)
    {
        (void)set;
        (void)value;
        REQUIRE(set.Capacity() == value);
        REQUIRE(set.ElemCount() <= value);
        REQUIRE(set.RemainingCapacity() == set.Capacity() - set.ElemCount());
        REQUIRE(set.IsFull() == (set.RemainingCapacity() == 0));
    };

    auto CheckContents = [&](const SparseSet<int> &set, int pos, std::span<const int> values, std::span<const int> indices)
    {
        (void)pos;
        (void)values;
        (void)indices;
        REQUIRE(values.size() == indices.size());
        CheckCapacity(set, values.size());
        REQUIRE(set.ElemCount() == pos);
        for (int i = 0; i < set.Capacity(); i++)
        {
            REQUIRE(set.GetElem(i) == values[i]);
            REQUIRE(set.GetElemIndex(i) == indices[i]);
            REQUIRE(set.GetElemIndex(set.GetElem(i)) == i);
            REQUIRE(set.GetElem(set.GetElemIndex(i)) == i);

            REQUIRE(set.Contains(set.GetElem(i)) == (i < pos));
        }
    };

    SparseSet<int> set;
    CheckContents(set, 0, {}, {});

    set = SparseSet<int>(5);
    CheckContents(set, 0, {{0,1,2,3,4}}, {{0,1,2,3,4}});

    REQUIRE(set.InsertAny() == 0);
    CheckContents(set, 1, {{0,1,2,3,4}}, {{0,1,2,3,4}});

    REQUIRE(set.InsertAny() == 1);
    CheckContents(set, 2, {{0,1,2,3,4}}, {{0,1,2,3,4}});

    REQUIRE(set.InsertAny() == 2);
    CheckContents(set, 3, {{0,1,2,3,4}}, {{0,1,2,3,4}});
    REQUIRE(set.Insert(2) == false);
    CheckContents(set, 3, {{0,1,2,3,4}}, {{0,1,2,3,4}});

    REQUIRE(set.Insert(4) == true);
    CheckContents(set, 4, {{0,1,2,4,3}}, {{0,1,2,4,3}});

    REQUIRE(set.Insert(3) == true);
    CheckContents(set, 5, {{0,1,2,4,3}}, {{0,1,2,4,3}});

    REQUIRE_THROWS_WITH((void)set.InsertAny(), doctest::Contains("insert into a full"));

    REQUIRE(set.EraseUnordered(2) == true);
    CheckContents(set, 4, {{0,1,3,4,2}}, {{0,1,4,2,3}});
    REQUIRE(set.EraseUnordered(2) == false);
    CheckContents(set, 4, {{0,1,3,4,2}}, {{0,1,4,2,3}});

    REQUIRE(set.EraseOrdered(1) == true);
    CheckContents(set, 3, {{0,3,4,1,2}}, {{0,3,4,1,2}});
    REQUIRE(set.EraseOrdered(1) == false);
    CheckContents(set, 3, {{0,3,4,1,2}}, {{0,3,4,1,2}});

    REQUIRE(set.EraseUnordered(4) == true);
    CheckContents(set, 2, {{0,3,4,1,2}}, {{0,3,4,1,2}});

    REQUIRE(set.EraseOrdered(3) == true);
    CheckContents(set, 1, {{0,3,4,1,2}}, {{0,3,4,1,2}});

    REQUIRE(set.InsertAny() == 3);
    CheckContents(set, 2, {{0,3,4,1,2}}, {{0,3,4,1,2}});

    set.EraseAllElements();
    CheckContents(set, 0, {{0,3,4,1,2}}, {{0,3,4,1,2}});
    set.EraseAllElements();
    CheckContents(set, 0, {{0,3,4,1,2}}, {{0,3,4,1,2}});

    REQUIRE(set.EraseOrdered(0) == false);
    CheckContents(set, 0, {{0,3,4,1,2}}, {{0,3,4,1,2}});
    REQUIRE(set.EraseUnordered(0) == false);
    CheckContents(set, 0, {{0,3,4,1,2}}, {{0,3,4,1,2}});
}

template class SparseSetNonOwning<int>;
TEST_CASE("sparse_set.non_owning")
{
    std::array<int, 10> storage{};
    SparseSetNonOwning<int> set(storage);
    REQUIRE(storage == std::array{0,1,2,3,4,0,1,2,3,4});
    REQUIRE(set.Capacity() == 5);
    REQUIRE(set.ElemCount() == 0);

    SparseSetNonOwning<int> set2({storage.begin(), storage.size() - 1});
    REQUIRE_EQ(set2.Capacity(), 4);
}
