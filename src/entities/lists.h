#pragma once

// Some predefined entity list types for the entity system.

#include <stdexcept>
#include <type_traits>
#include <utility>

#include "program/compiler.h"

IMP_PLATFORM_IF(clang)(
    IMP_DIAGNOSTICS_PUSH
    IMP_DIAGNOSTICS_IGNORE("-Wdeprecated-builtins")
    IMP_DIAGNOSTICS_IGNORE("-Wdeprecated-declarations")
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
            using is_transparent = void;

            bool operator()(typename Tag::Entity *a, typename Tag::Entity *b) const
            {
                return a->id() < b->id();
            }
            bool operator()(typename Tag::Entity *a, typename Tag::Id b) const
            {
                return a->id() < b;
            }
            bool operator()(typename Tag::Id a, typename Tag::Entity *b) const
            {
                return a < b->id();
            }
        };

        // An equality comparator for entity pointers that uses the incremental id.
        template <TagType Tag>
        struct UniqueIdEq
        {
            using is_transparent = void;

            bool operator()(typename Tag::Entity *a, typename Tag::Entity *b) const
            {
                return a->id() == b->id();
            }
            bool operator()(typename Tag::Entity *a, typename Tag::Id b) const
            {
                return a->id() == b;
            }
            bool operator()(typename Tag::Id a, typename Tag::Entity *b) const
            {
                return a == b->id();
            }
        };

        // A hasher for entity pointers that uses the incremental id.
        // We could just use the default hasher and hash the pointers themselves, but it's nice to be able to find entities by id.
        template <TagType Tag>
        struct UniqueIdHash
        {
            using is_transparent = void;

            std::size_t operator()(typename Tag::Entity *e) const
            {
                return operator()(e->id());
            }
            std::size_t operator()(typename Tag::Id e) const
            {
                return phmap::Hash<decltype(e.get_value())>{}(e.get_value());
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
                using set_t = std::conditional_t<Ordered,
                    phmap::btree_set<typename Tag::Entity *, UniqueIdLess<Tag>>,
                    phmap::flat_hash_set<typename Tag::Entity *, UniqueIdHash<Tag>, UniqueIdEq<Tag>>
                >;
                set_t set;

                template <bool IsConst>
                using OriginalIter = std::conditional_t<IsConst, typename set_t::const_iterator, typename set_t::iterator>;

                template <bool IsConst>
                class CustomIter : public OriginalIter<IsConst>
                {
                    using base_t = OriginalIter<IsConst>;

                  public:
                    CustomIter(base_t base) : base_t(std::move(base)) {}

                    using value_type = typename Tag::Entity;
                    using reference = std::conditional_t<IsConst, const typename Tag::Entity &, typename Tag::Entity &>;
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

                void Insert(typename Tag::Entity &value) override
                {
                    if constexpr (Ordered)
                        set.insert(set.end(), &value);
                    else
                        set.insert(&value);
                }
                void Erase(typename Tag::Entity &value) noexcept override
                {
                    [[maybe_unused]] bool ok = set.erase(&value) > 0;
                    ASSERT(ok, "Attempt to erase a non-existent element from a list.");
                }
                typename Tag::Entity *AnyEntity() noexcept override
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
                [[nodiscard]] typename Tag::Entity *single_opt()
                {
                    if (set.size() > 1)
                        throw std::runtime_error(FMT("Expected at most one entity in this list, but got {}.", set.size()));
                    return set.empty() ? nullptr : *set.begin();
                }
                [[nodiscard]] const typename Tag::Entity *single_opt() const
                {
                    return const_cast<Type *>(this)->single_opt();
                }
                // Return one element, throw otherwise.
                [[nodiscard]] typename Tag::Entity &single()
                {
                    if (set.size() != 1)
                        throw std::runtime_error(FMT("Expected one entity in this list, but got {}.", set.size()));
                    return **set.begin();
                }
                [[nodiscard]] const typename Tag::Entity &single() const
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

                // Find entity by id.
                [[nodiscard]] bool has_entity_with_id(typename Tag::Id id) const
                {
                    return bool(entity_with_id_opt(id));
                }
                [[nodiscard]] typename Tag::Entity &entity_with_id(typename Tag::Id id)
                {
                    auto ret = entity_with_id_opt(id);
                    if (!ret)
                        throw std::runtime_error("No entity with this ID in this list.");
                    return *ret;
                }
                [[nodiscard]] const typename Tag::Entity &entity_with_id(typename Tag::Id id) const
                {
                    return const_cast<Type *>(this)->entity_with_id(id);
                }
                [[nodiscard]] typename Tag::Entity *entity_with_id_opt(typename Tag::Id id)
                {
                    auto it = set.find(id);
                    return it == set.end() ? nullptr : *it;
                }
                [[nodiscard]] const typename Tag::Entity *entity_with_id_opt(typename Tag::Id id) const
                {
                    return const_cast<Type *>(this)->entity_with_id_opt(id);
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
        // A single-entity "list", that can either dereference to a whole entity or to a specific component.
        // If `Comp` is void, dereferences to a whole entity.
        template <typename Comp>
        struct Single
        {
            template <TagType Tag, Predicate<Tag> Pred>
            class Type : ListBase<Tag>
            {
                friend ListFriend;

                // If `IsSingleComponent` is false, returns `typename Tag::Entity`.
                // Otherwise returns the sole component that `Pred` requires (the `static_assert` above checks that we have one).
                using elem_t = typename std::conditional_t<std::is_void_v<Comp>, typename Tag::Entity, Comp>;

                typename Tag::Entity *current = nullptr;

                void Insert(typename Tag::Entity &entity) override
                {
                    if (current)
                        throw std::runtime_error("Expected at most one entity for this entity list.");
                    current = &entity;
                }
                void Erase(typename Tag::Entity &entity) noexcept override
                {
                    (void)entity;
                    ASSERT(current == &entity, "Internal error: Attempt to erase a wrong entity from a single-entity list.");
                    current = nullptr;
                }
                typename Tag::Entity *AnyEntity() noexcept override
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
    using SingleEntity = impl::Single<void>;

    // A single-entity list that returns a specific component by default.
    template <typename Comp> requires(!std::is_void_v<Comp>)
    using SingleComponent = impl::Single<Comp>;

    namespace Mixins
    {
        // When a component is used as a category type, automatically generate a category for it, with `SingleComponent<T>` as a list.
        template <typename Tag, typename NextBase>
        struct ComponentsAsCategories : NextBase
        {
            template <typename T>
            struct PrepareCategoryType {using type = typename NextBase::template PrepareCategoryType<T>::type;};
            template <Component<Tag> T>
            struct PrepareCategoryType<T> {using type = typename Tag::template Category<SingleComponent<T>, T>;};
        };
    }
}
