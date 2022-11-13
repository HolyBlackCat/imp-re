#pragma once

// Some predefined entity list types for the entity system.

#include <stdexcept>
#include <type_traits>
#include <utility>

#include "program/compiler.h"

IMP_PLATFORM_IF(clang)(
    IMP_DIAGNOSTICS_PUSH
    IMP_DIAGNOSTICS_IGNORE("-Wdeprecated-builtins")
)
#include <parallel_hashmap/btree.h>
#include <parallel_hashmap/phmap.h>
IMP_PLATFORM_IF(clang)(
    IMP_DIAGNOSTICS_POP
)

#include "entities/core.h"


namespace Ent
{
    // Classic entity lists:

    namespace impl
    {
        // A comparator for entity pointers that uses the incremental id.
        template <TagType Tag>
        struct UniqueIdLess
        {
            bool operator()(Entity<Tag> *a, Entity<Tag> *b) const
            {
                return dynamic_cast<Entity<Tag> *>(a)->UniqueId() < dynamic_cast<Entity<Tag> *>(b)->UniqueId();
            }
        };

        // A possibly ordered set.
        template <bool Ordered>
        struct MaybeOrderedList
        {
            template <TagType Tag, Predicate<Tag> Pred>
            class Type : ListBase<Tag>
            {
                friend ListFriend;
                using set_t = std::conditional_t<Ordered, phmap::btree_set<Entity<Tag> *, UniqueIdLess<Tag>>, phmap::flat_hash_set<Entity<Tag> *>>;
                set_t set;

                template <bool IsConst>
                using OriginalIter = std::conditional_t<IsConst, typename set_t::const_iterator, typename set_t::iterator>;

                template <bool IsConst>
                class CustomIter : public OriginalIter<IsConst>
                {
                    using base_t = OriginalIter<IsConst>;

                  public:
                    CustomIter(base_t base) : base_t(std::move(base)) {}

                    using value_type = Entity<Tag>;
                    using reference = std::conditional_t<IsConst, const Entity<Tag> &, Entity<Tag> &>;
                    using pointer = std::remove_reference_t<reference> *;

                    reference operator*() const
                    {
                        return *base_t::operator*();
                    }
                    pointer operator->() const
                    {
                        return *base_t::operator->();
                    }
                };

                void Insert(Entity<Tag> &value) override
                {
                    if constexpr (Ordered)
                        set.insert(set.end(), &value);
                    else
                        set.insert(&value);
                }
                void Erase(Entity<Tag> &value) noexcept override
                {
                    [[maybe_unused]] bool ok = set.erase(&value) > 0;
                    ASSERT(ok, "Attempt to erase a non-existent element from a list.");
                }
                Entity<Tag> *AnyEntity() noexcept override
                {
                    return set.empty() ? nullptr : *set.begin();
                }

                template <bool IsConst>
                struct MaybeConstRange
                {
                    Type &target;

                    [[nodiscard]] auto begin() const {return CustomIter<IsConst>(target.set.begin());}
                    [[nodiscard]] auto end() const {return CustomIter<IsConst>(target.set.end());}
                };
                using Range = MaybeConstRange<false>;
                using ConstRange = MaybeConstRange<true>;

              public:
                [[nodiscard]] int size() const {return int(set.size());}
                [[nodiscard]] bool has_elems() const {return !set.empty();}

                [[nodiscard]] auto begin() {return CustomIter<false>(set.begin());}
                [[nodiscard]] auto end() {return CustomIter<false>(set.end());}
                [[nodiscard]] auto begin() const {return CustomIter<true>(set.begin());}
                [[nodiscard]] auto end() const {return CustomIter<true>(set.end());}

                // Return one or zero elements, throw otherwise.
                [[nodiscard]] Entity<Tag> *single_opt()
                {
                    if (set.size() > 1)
                        throw std::runtime_error(FMT("Expected at most one entity in this list, but got {}.", set.size()));
                    return set.empty() ? nullptr : *set.begin();
                }
                [[nodiscard]] const Entity<Tag> *single_opt() const
                {
                    return const_cast<Type *>(this)->single_opt();
                }
                // Return one element, throw otherwise.
                [[nodiscard]] Entity<Tag> &single()
                {
                    if (set.size() != 1)
                        throw std::runtime_error(FMT("Expected one entity in this list, but got {}.", set.size()));
                    return **set.begin();
                }
                [[nodiscard]] const Entity<Tag> &single() const
                {
                    return const_cast<Type *>(this)->single();
                }
                // Return at least one element, throw otherwise.
                [[nodiscard]] Range at_least_one()
                {
                    if (set.empty())
                        throw std::runtime_error("Expected at least one entity in this list.");
                    return Range{*this};
                }
                [[nodiscard]] ConstRange at_least_one() const
                {
                    return {const_cast<Type *>(this)->at_least_one().target};
                }
            };
        };
    }

    // An ordered entity list.
    using OrderedList = impl::MaybeOrderedList<true>;
    // An unordered entity list.
    using UnorderedList = impl::MaybeOrderedList<false>;


    // Single-entity lists:

    namespace impl
    {
        // Whether `T` is a specialization of `HasComponents` with a single argument.
        template <typename T, typename Tag>
        concept SingleComponentPredicate = Predicate<T, Tag> && Meta::specialization_of<T, Tag::template HasComponents> && Meta::list_size<typename T::required_components> == 1;

        // If `T` is a `SingleComponentPredicate`, returns its target component. Otherwise `void`.
        template <typename Tag, typename Pred>
        struct SinglePredicateComponent {};
        template <typename Tag, SingleComponentPredicate<Tag> Pred>
        struct SinglePredicateComponent<Tag, Pred> {using type = Meta::list_type_at<typename Pred::required_components, 0>;};

        // A single-entity "list", that can either dereference to a whole entity or to a specific component.
        template <bool IsSingleComponent>
        struct Single
        {
            template <TagType Tag, Predicate<Tag> Pred>
            class Type : ListBase<Tag>
            {
                friend ListFriend;
                static_assert(IsSingleComponent <= SingleComponentPredicate<Pred, Tag>, "For single-component lists, the predicate must be `HasComponents` with a single argument.");

                // If `IsSingleComponent` is false, returns `Entity<Tag>`.
                // Otherwise returns the sole component that `Pred` requires (the `static_assert` above checks that we have one).
                using elem_t = typename std::conditional_t<IsSingleComponent, SinglePredicateComponent<Tag, Pred>, std::enable_if<true, Entity<Tag>>>::type;

                Entity<Tag> *current = nullptr;

                void Insert(Entity<Tag> &entity) override
                {
                    if (current)
                        throw std::runtime_error("Expected at most one entity for this entity list.");
                    current = &entity;
                }
                void Erase(Entity<Tag> &entity) noexcept override
                {
                    (void)entity;
                    ASSERT(current == &entity, "Internal error: Attempt to erase a wrong entity from a single-entity list.");
                    current = nullptr;
                }
                Entity<Tag> *AnyEntity() noexcept override
                {
                    return current;
                }

              public:
                // Returns true if the target exists.
                [[nodiscard]] explicit operator bool() const
                {
                    return bool(current);
                }

                // Returns the target, or throws if none.
                [[nodiscard]] elem_t &operator*() const
                {
                    return get();
                }
                // Returns the target, or throws if none.
                [[nodiscard]] elem_t *operator->() const
                {
                    return &get();
                }

                // Returns the target or throws if none.
                [[nodiscard]] elem_t &get() const
                {
                    if (auto ret = get_opt())
                        return *ret;
                    else
                        throw std::runtime_error("A single-entity list contains no entity.");
                }
                // Returns the target or null if none.
                [[nodiscard]] elem_t *get_opt() const
                {
                    return current ? &dynamic_cast<elem_t &>(*current) : nullptr;
                }
            };
        };
    }

    // A single-entity list.
    using SingleEntity = impl::Single<false>;

    // A single-entity list that returns a specific component by default.
    // It requires the predicate to be `HasComponents` with a single argument.
    using SingleComponent = impl::Single<true>;
}
