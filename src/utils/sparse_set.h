#pragma once

#include <cstddef>
#include <numeric>
#include <ostream>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include "macros/finally.h"
#include "meta/common.h"
#include "program/errors.h"

// This header implements "sparse sets".
// They can store unique numbers, with values less than its capacity.
// The capacity can only be increased manually, and can never be decreased (without losing all elements).
// Like a vector, provides O(1) insertion and element access, O(n) erase (or O(1) if you don't care about preserving order).
// Also lets you find element indices in O(1), or check if they are present or not.
// Under the hood, uses two vectors of the specified capacity.

// A CRTP base.
// Derived classes must have:
//   private:
//     friend BasicSparseSetInterface<Derived, T>;
//     T SparseSet_GetCapacity() const;
//     T SparseSet_GetPos() const;
//     void SparseSet_SetPos(T new_pos);
//     T SparseSet_GetValue(T loc) const;
//     void SparseSet_SetValue(T loc, T new_value);
//     T SparseSet_GetIndex(T loc) const;
//     void SparseSet_SetIndex(T loc, T new_value);
// Where `loc` is `0 <= loc < SparseSet_GetCapacity()`.
// "values" and "indices" lists have the same size.
// Both contain unique sequental integers starting from 0 (you must initially fill them with incremental integers,
//   and when you increase capacity, you can safely add more incremental integers).
// At any point, `values[indices[x]] == x`, and vice versa.
// `values` is always ordered so that the existing elements come first.
template <typename Derived, typename T>
class BasicSparseSetInterface
{
    static_assert(std::is_integral_v<T>, "The template parameter must be integral.");
  public:
    using elem_t = T;

  protected:
          Derived &GetDerived()       {return *static_cast<      Derived *>(this);}
    const Derived &GetDerived() const {return *static_cast<const Derived *>(this);}

    // Swaps two values and the two respective indices.
    // Each of the two elements can be specified either as a value or as an index, as configured by the template parameters.
    template <bool AIsIndex, bool BIsIndex>
    void SwapElements(elem_t a, elem_t b)
    {
        elem_t a_value = AIsIndex ? GetDerived().SparseSet_GetValue(a) : a;
        elem_t a_index = AIsIndex ? a : GetDerived().SparseSet_GetIndex(a);
        elem_t b_value = BIsIndex ? GetDerived().SparseSet_GetValue(b) : b;
        elem_t b_index = BIsIndex ? b : GetDerived().SparseSet_GetIndex(b);

        elem_t tmp;

        tmp = GetDerived().SparseSet_GetValue(a_index);
        GetDerived().SparseSet_SetValue(a_index, GetDerived().SparseSet_GetValue(b_index));
        GetDerived().SparseSet_SetValue(b_index, tmp);

        tmp = GetDerived().SparseSet_GetIndex(a_value);
        GetDerived().SparseSet_SetIndex(a_value, GetDerived().SparseSet_GetIndex(b_value));
        GetDerived().SparseSet_SetIndex(b_value, tmp);
    }

  public:
    // The maximum number of elements.
    [[nodiscard]] elem_t Capacity() const
    {
        return GetDerived().SparseSet_GetCapacity();
    }
    // The current number of elements.
    [[nodiscard]] elem_t ElemCount() const
    {
        return GetDerived().SparseSet_GetPos();
    }
    // The amount of elements that can be inserted before the capacity is exhausted.
    [[nodiscard]] elem_t RemainingCapacity() const
    {
        return Capacity() - ElemCount();
    }
    // Returns true if the capacity is completely exhausted.
    [[nodiscard]] bool IsFull() const
    {
        return RemainingCapacity() == 0;
    }

    // Returns true if the element exists in the set.
    // If the index is out of range, returns false instead of throwing.
    [[nodiscard]] bool Contains(elem_t elem) const
    {
        if (elem < 0 || elem >= Capacity())
            return false;
        return GetDerived().SparseSet_GetIndex(elem) < ElemCount();
    }

    // Adds a new element to the set, a one that wasn't there before.
    // Throws if no free capacity.
    [[nodiscard]] elem_t InsertAny()
    {
        if (IsFull())
            throw std::runtime_error("Attempt to insert into a full `SparseSet`.");

        elem_t old_pos = ElemCount();
        GetDerived().SparseSet_SetPos(old_pos + 1);
        return GetDerived().SparseSet_GetValue(old_pos);
    }

    // Adds a new element to the set, returns true on success.
    // Returns false if the element was already present.
    bool Insert(elem_t elem)
    {
        if (Contains(elem))
            return false;

        SwapElements<false, true>(elem, ElemCount());
        GetDerived().SparseSet_SetPos(ElemCount() + 1);
        return true;
    }

    // Erases an element from the set, returns true on success.
    // Returns false if no such element.
    // Might change the element order.
    bool EraseUnordered(elem_t elem)
    {
        if (!Contains(elem))
            return false;

        GetDerived().SparseSet_SetPos(ElemCount() - 1);
        SwapElements<false, true>(elem, ElemCount());
        return true;
    }

    // Erases an element from the set, returns true on success.
    // Returns false if no such element.
    // Preserves the element order.
    bool EraseOrdered(elem_t elem)
    {
        if (!Contains(elem))
            return false;

        elem_t index = GetDerived().SparseSet_GetIndex(elem);

        GetDerived().SparseSet_SetPos(ElemCount() - 1);

        for (elem_t i = index; i < ElemCount(); i++)
        {
            elem_t new_value = GetDerived().SparseSet_GetValue(i + 1);
            GetDerived().SparseSet_SetValue(i, new_value);
            GetDerived().SparseSet_SetIndex(new_value, i);
        }
        GetDerived().SparseSet_SetValue(ElemCount(), elem);
        GetDerived().SparseSet_SetIndex(elem, ElemCount());

        return true;
    }

    // Erases all elements while maintaining capacity.
    void EraseAllElements()
    {
        GetDerived().SparseSet_SetPos(0);
    }

    // Returns i-th element.
    // If `index >= ElemCount()`, starts returning all missing elements.
    // If `index >= Capacity()` or is negative, throws.
    [[nodiscard]] elem_t GetElem(elem_t index) const
    {
        if (index < 0 || index >= Capacity())
            throw std::runtime_error("Out of range index for an `SparseSet` element.");
        return GetDerived().SparseSet_GetValue(index);
    }

    // Returns the index of `elem` that can be used with `GetElem()`.
    // If the elem doesn't exist, returns `>= ElemCount()`.
    // If `elem >= Capacity()` or is negative, throws.
    elem_t GetElemIndex(elem_t elem) const
    {
        if (elem < 0 || elem >= Capacity())
            throw std::runtime_error("Out of range elem for an `SparseSet` index search.");
        return GetDerived().SparseSet_GetIndex(elem);
    }

    // Prints the set and asserts consistency.
    template <typename ...P>
    void DebugPrint(std::basic_ostream<P...> &s)
    {
        // Print.
        s << '[';
        for (elem_t i = 0; i < ElemCount(); i++)
        {
            if (i != 0) s << ',';
            s << GetElem(i);
        }
        s << "]\n";

        // Assert consistency.
        ASSERT([&]{
            for (elem_t i = 0; i < Capacity(); i++)
            {
                if (GetElem(GetElemIndex(i)) != i)
                    return false;
            }
            return true;
        }(), "Consistency check failed for an `SparseSet`.");
    }
};

// An implementation of sparse set that owns its underlying storage.
template <typename T>
class SparseSet : public BasicSparseSetInterface<SparseSet<T>, T>
{
    friend BasicSparseSetInterface<SparseSet<T>, T>;

    Meta::ResetIfMovedFrom<T> pos = 0;

    std::vector<T> values, indices;

    T SparseSet_GetCapacity() const {return values.size(); /* Sic. */}
    T SparseSet_GetPos() const {return pos.value;}
    void SparseSet_SetPos(T new_pos) {pos = new_pos;}
    T SparseSet_GetValue(T loc) const {return values[loc];}
    void SparseSet_SetValue(T loc, T new_value) {values[loc] = new_value;}
    T SparseSet_GetIndex(T loc) const {return indices[loc];}
    void SparseSet_SetIndex(T loc, T new_value) {indices[loc] = new_value;}

  public:
    constexpr SparseSet() {}

    explicit SparseSet(T new_capacity)
    {
        Reserve(new_capacity);
    }

    // Increase the capacity up to the specified value. Can't decrease capacity.
    void Reserve(T new_capacity)
    {
        if (new_capacity <= this->Capacity())
            return;

        std::size_t old_capacity = this->Capacity();

        values.resize(new_capacity);
        FINALLY_ON_THROW{values.resize(old_capacity);};

        indices.resize(new_capacity);
        // Not needed since nothing throws below this point.
        // FINALLY_ON_THROW{indices.resize(old_capacity);};

        std::iota(values.begin() + old_capacity, values.end(), old_capacity);
        std::iota(indices.begin() + old_capacity, indices.end(), old_capacity);
    }
};

// An implementation of sparse set that uses external storage. Move-only.
template <typename T>
class SparseSetNonOwning : public BasicSparseSetInterface<SparseSetNonOwning<T>, T>
{
    friend BasicSparseSetInterface<SparseSetNonOwning<T>, T>;

    Meta::ResetIfMovedFrom<T> pos = 0;
    Meta::ResetIfMovedFrom<T *> storage = nullptr;
    Meta::ResetIfMovedFrom<T> capacity = 0;

    T SparseSet_GetCapacity() const {return capacity.value;}
    T SparseSet_GetPos() const {return pos.value;}
    void SparseSet_SetPos(T new_pos) {pos = new_pos;}
    T SparseSet_GetValue(T loc) const {return storage.value[loc];}
    void SparseSet_SetValue(T loc, T new_value) {storage.value[loc] = new_value;}
    T SparseSet_GetIndex(T loc) const {return storage.value[capacity.value + loc];}
    void SparseSet_SetIndex(T loc, T new_value) {storage.value[capacity.value + loc] = new_value;}

  public:
    constexpr SparseSetNonOwning() {}

    SparseSetNonOwning(SparseSetNonOwning &&) = default;
    SparseSetNonOwning &operator=(SparseSetNonOwning &&) = default;

    // Constructs an empty set using the provided storage.
    // The capacity is set to `new_storage.size() / 2`.
    explicit constexpr SparseSetNonOwning(std::span<T> new_storage)
    {
        storage = new_storage.data();
        capacity = new_storage.size() / 2;

        for (T i = 0; i < capacity.value; i++)
        {
            SparseSet_SetValue(i, i);
            SparseSet_SetIndex(i, i);
        }
    }
};
