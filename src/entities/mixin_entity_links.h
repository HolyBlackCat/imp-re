#pragma once

#include <concepts>
#include <iterator>
#include <string_view>
#include <string>
#include <utility>
#include <vector>

#include "entities/core.h"
#include "meta/const_string.h"
#include "program/errors.h"
#include "strings/format.h"

/* Lets you link entities together.
The links can be detached manually, and are detached automatically when an entity dies.

Requires `Ent::Mixins::GlobalEntityLists` and `Ent::Mixins::EntityCallbacks`.

Manual:
* Inherit your component from any amount of:
  * `LinkOne<"name">` to add a link to a single other entity.
  * `LinkMany<"name">` to add a link to several other entities.
    This uses a vector under the hood, but the container can be customized, and/or extra user data can be added. See code for details.

NOTE: The names must be unique in an entity.

NOTE: Some functions below require the parameter to be `dynamic_cast`able to `Entity`, notably `link()` and `unlink()`.
      If they complain, make sure your types are polymorphic.

NOTE: Most functions are overloaded in a common way.
      Functions taking a single entity can be invoked as:
      * `foo<"name">(component, ...)`
      * `foo("name", component, ...)`
      Functions taking two entities can be invoked as:
      * `foo<"name_a", "name_b">(component_a, component_b, ...)`
      * `foo<"name_a">(component_a, "name_b", component_b, ...)`
      * `foo("name_a", component_a, "name_b", component_b, ...)`

Functions:

* Add link.
    controller.link<"a", "b">(a, b)
* Remove link link. For multi-target links, this removes all links.
    controller.unlink<"a">(a)`
* Remove link to a specific entity. Throws if not linked.
    controller.unlink<"a">(a, target)

* Check if a link has (at least one) target.
    controller.has_link<"a">(a)
* Get the number of targets of a link. For single-target links, this is always 0 or 1.
    controller.num_link_targets<"a">(a)

* Get the target of a single-target link.
  * As entity reference. Throw if no target.
      controller.get_link<"a">(a)
  * As entity pointer. Null if no target.
      controller.get_link_opt<"a">(a)
  * As a pointer to a structure, containing target id and target link name. Null if no target.
      controller.get_link_low_opt<"a">(a)

* Get the targets of a multi-target link.
    controller.get_links<"a">(a)`
  This has no type-erased overload, because the container type can be customized.
  This returns `LinkContainer`, a container wrapper. See the code for details.

* Check if a link exists:
  * Without checking the target link name.
      controller.has_link_to<"a">(a, target)
  * With checking the target link name.
      controller.has_link_to<"a", "b">(a, b)
*/

namespace Ent
{
    namespace Mixins
    {
        namespace impl::EntityLinks
        {
            // Dummy ADL targets:
            constexpr void _adl_link_marker() {}
            constexpr void _adl_link_attach() {}
            constexpr void _adl_link_detach() {}
            constexpr void _adl_link_num_targets() {}
            constexpr void _adl_link_single_target() {}
            constexpr void _adl_link_multi_target() {} // This one is not type-erased, because the return type can vary.
            constexpr void _adl_link_has_target() {}

            // Returns true if `T` is a `Link??<"...">` entity component.
            template <typename T>
            constexpr bool is_link_component = requires(const T *t) {_adl_link_marker(t);};

            // Returns the name of the link type `T` (its first template argument).
            template <typename T> requires is_link_component<T>
            constexpr Meta::ConstString link_component_name = _adl_link_marker((const T *)nullptr).value;

            // `A` is either `nullptr` (then returns true) or `ConstString` (then checks equality with `B`).
            template <auto A, Meta::ConstString B>
            constexpr bool matches_name_or_null = requires{requires std::is_null_pointer_v<decltype(A)> || A.view() == B.view();};

            // A little hack. If `T` is a `std::contiguous_iterator`, add the respective `iterator_concept`.
            template <typename T>
            struct MaybeAddContiguousIteratorConcept {};
            template <std::contiguous_iterator T>
            struct MaybeAddContiguousIteratorConcept<T> {using iterator_concept = std::contiguous_iterator_tag;};
        }

        template <typename Tag, typename NextBase>
        struct EntityLinks : NextBase
        {
            // Mostly for internal use. Can't be overriden in mixins.
            struct LinkTargetIdWithName
            {
                // The target id.
                typename Tag::Id target_id;
                // The link name in the target/
                std::string target_link_name;
            };

            struct Entity : NextBase::Entity
            {
                // Attaches `linked_id` to our link `name` and `linked_id`'s link `linked_name`. Throws on failure.
                // The effect depends on the kind of the link: replaces the target in a single-target link,
                // or adds a target to a multi-target link (removing the old link to the same entity, if any).
                // If `symmetric` is true, acts symmetrically.
                // Unsafe! Prefer the high-level functions in the controller.
                virtual void _detail_link_attach(typename Tag::Controller &con, bool symmetric, std::string_view name, typename Tag::Id linked_id, std::string linked_name) = 0;

                // Detaches `id` from the link named `name`, or returns `false` if not linked. Throws on failure.
                // If `id` is null, detaches all linked entities. Always returns true in this case.
                // If `con` is specified, acts symmetrically.
                // Unsafe! Prefer the high-level functions in the controller.
                virtual bool _detail_link_detach(typename Tag::Controller *con, std::string_view name, typename Tag::Id linked_id) = 0;

                // Returns the number of targets for a link. Throws if the name is invalid.
                // Prefer the high-level functions in the controller.
                [[nodiscard]] virtual std::size_t _detail_link_num_targets(std::string_view name) const = 0;

                // Returns the target of a single-target link. Null if no target.
                // Throws if this is a multi-target link, or if the name is invalid.
                // Prefer the high-level functions in the controller.
                [[nodiscard]] virtual const LinkTargetIdWithName &_detail_link_single_target(std::string_view name) const = 0;

                // Returns true if the link has `linked_id` as the target (or one of the targets). Throws if the name is invalid.
                // If the `linked_id` is null, always returns false.
                // If `maybe_linked_name` isn't empty, also checks it for equality. Otherwise ignores the name.
                // Prefer the high-level functions in the controller.
                [[nodiscard]] virtual bool _detail_link_has_target(std::string_view name, typename Tag::Id linked_id, std::string_view maybe_linked_name) const = 0;
            };

            template <EntityType<Tag> E>
            struct FullEntity : NextBase::template FullEntity<E>
            {
                using NextBase::template FullEntity<E>::FullEntity;

              private:
                static constexpr int num_links = []<typename ...C>(Meta::type_list<C...>)
                {
                    return (impl::EntityLinks::is_link_component<C> + ... + 0);
                }
                (Ent::EntityComponents<Tag, E>{});

                // Dispatches a function call to one of the links of this entity, named `name`. Throws if the name is invalid.
                // `F` is a functor type performing the call.
                // If `IsConst` is true, the target entity is const.
                // `R` is the return type, and `P...` are the parameters.
                template <typename F, bool IsConst, typename R, typename ...P>
                static constexpr R LinkCallFunc(std::string_view name, Meta::maybe_const<IsConst, FullEntity> &e, P ...params)
                {
                    constexpr auto arr = []<typename ...C>(Meta::type_list<C...>)
                    {
                        std::array<std::pair<std::string_view, R (*)(Meta::maybe_const<IsConst, FullEntity> &, P...)>, num_links> arr{};
                        std::size_t i = 0;
                        ([&]{
                            if constexpr (impl::EntityLinks::is_link_component<C>)
                            {
                                arr[i++] = {
                                    impl::EntityLinks::link_component_name<C>.view(),
                                    [](Meta::maybe_const<IsConst, FullEntity> &e, P ...params) -> R
                                    {
                                        return F{}(dynamic_cast<Meta::maybe_const<IsConst, C> &>(e), std::forward<P>(params)...);
                                    }
                                };
                            }
                        }(), ...);
                        std::sort(arr.begin(), arr.end(), [](const auto &a, const auto &b){return a.first < b.first;});
                        if (auto it = std::adjacent_find(arr.begin(), arr.end(), [](const auto &a, const auto &b){return a.first == b.first;}); it != arr.end())
                            throw std::runtime_error(FMT("Duplicate link name in an entity: {}", it->first));
                        return arr;
                    }
                    (Ent::EntityComponents<Tag, E>{});

                    auto it = std::partition_point(arr.begin(), arr.end(), [&](const auto &elem){return elem.first < name;});
                    if (it == arr.end() || it->first != name)
                        throw std::runtime_error("Unknown link name.");
                    return it->second(e, std::forward<P>(params)...);
                }

                struct FuncLinkAttach
                {
                    decltype(auto) operator()(auto &&... params)
                    {
                        using impl::EntityLinks::_adl_link_attach;
                        return _adl_link_attach<nullptr>(decltype(params)(params)...);
                    }
                };
                struct FuncLinkDetach
                {
                    decltype(auto) operator()(auto &&... params)
                    {
                        using impl::EntityLinks::_adl_link_detach;
                        return _adl_link_detach<nullptr>(decltype(params)(params)...);
                    }
                };
                struct FuncLinkNumTargets
                {
                    decltype(auto) operator()(auto &&... params)
                    {
                        using impl::EntityLinks::_adl_link_num_targets;
                        return _adl_link_num_targets<nullptr>(decltype(params)(params)...);
                    }
                };
                struct FuncLinkSingleTarget
                {
                    const LinkTargetIdWithName &operator()(auto &&... params)
                    {
                        using impl::EntityLinks::_adl_link_single_target;
                        if constexpr (std::is_void_v<decltype(_adl_link_single_target<nullptr>(decltype(params)(params)...))>)
                            throw std::runtime_error("This is not a single-target link, it doesn't have a single value.");
                        else
                            return _adl_link_single_target<nullptr>(decltype(params)(params)...);
                    }
                };
                struct FuncLinkHasTarget
                {
                    decltype(auto) operator()(auto &&... params)
                    {
                        using impl::EntityLinks::_adl_link_has_target;
                        return _adl_link_has_target<nullptr>(decltype(params)(params)...);
                    }
                };

              public:
                void _detail_link_attach(typename Tag::Controller &con, bool symmetric, std::string_view name, typename Tag::Id linked_id, std::string linked_name) override
                {
                    LinkCallFunc<
                        FuncLinkAttach,
                        false,
                        void,
                        typename Tag::Controller &/*con*/, typename Tag::Entity &/*self*/, bool/*symmetric*/, typename Tag::Id/*linked_id*/, std::string/*linked_name*/
                    >(name, *this, con, *this, symmetric, linked_id, std::move(linked_name));
                }

                bool _detail_link_detach(typename Tag::Controller *con, std::string_view name, typename Tag::Id linked_id) override
                {
                    return LinkCallFunc<
                        FuncLinkDetach,
                        false,
                        bool,
                        typename Tag::Controller */*con*/, typename Tag::Entity */*self*/, typename Tag::Id/*detached_id*/
                    >(name, *this, con, con ? this : nullptr, linked_id);
                }

                [[nodiscard]] std::size_t _detail_link_num_targets(std::string_view name) const override
                {
                    return LinkCallFunc<
                        FuncLinkNumTargets,
                        true,
                        std::size_t
                        /* No parameters. */
                    >(name, *this);
                }

                [[nodiscard]] const LinkTargetIdWithName &_detail_link_single_target(std::string_view name) const override
                {
                    return LinkCallFunc<
                        FuncLinkSingleTarget,
                        true,
                        const LinkTargetIdWithName &
                        /* No parameters. */
                    >(name, *this);
                }

                [[nodiscard]] bool _detail_link_has_target(std::string_view name, typename Tag::Id linked_id, std::string_view maybe_linked_name) const override
                {
                    return LinkCallFunc<
                        FuncLinkHasTarget,
                        true,
                        bool,
                        typename Tag::Id/*linked_id*/, std::string_view/*maybe_linked_name*/
                    >(name, *this, linked_id, maybe_linked_name);
                }
            };

            // Whether `T` can be `dynamic_cast`ed to `Entity`.
            template <typename T>
            static constexpr bool link_dynamic_castable_to_entity = requires(T &t){dynamic_cast<typename Tag::Entity &>(t);};

            // Whether `T` inherits from a `Link??<Name>`, and is `dynamic_cast`able to `Entity`.
            template <typename T, Meta::ConstString Name>
            static constexpr bool valid_link_owner = []{
                using impl::EntityLinks::_adl_link_detach;
                return requires(T &t)
                {
                    _adl_link_detach<Name>(t, nullptr, nullptr, {});
                };
            }();

            // Same as `valid_link_owner`, but also checks that the link is a single-target one.
            template <typename T, Meta::ConstString Name>
            static constexpr bool valid_single_target_link_owner = valid_link_owner<T, Name> && []{
                using impl::EntityLinks::_adl_link_single_target;
                return requires(const T &t)
                {
                    requires !std::is_void_v<decltype(_adl_link_single_target<Name>(t))>;
                };
            }();

            // Same as `valid_link_owner`, but also checks that the link is a multi-target one.
            template <typename T, Meta::ConstString Name>
            static constexpr bool valid_multi_target_link_owner = valid_link_owner<T, Name> && []{
                using impl::EntityLinks::_adl_link_multi_target;
                return requires(T &t)
                {
                    requires !std::is_void_v<decltype(_adl_link_multi_target<Name>(t))>;
                };
            }();

            // Inherit your entity/component from this to add a single-target entity link.
            template <Meta::ConstString Name> requires(Name.size > 0)
            class LinkOne
            {
                IMP_COMPONENT(Tag)

                LinkTargetIdWithName target{};

              protected:
                ~LinkOne() = default;

              public:
                constexpr LinkOne() {}

                // Copying is a no-op.
                constexpr LinkOne(const LinkOne &) {}
                constexpr LinkOne &operator=(const LinkOne &) {*this;}

                // This is used to find all links of an entity at compile-time.
                friend constexpr Meta::ConstStringTag<Name> _adl_link_marker(const std::same_as<LinkOne> auto *) {return {};}

                // Sets the link. If already linked, replaces the old link.
                // If `symmetric` is true, acts symmetrically.
                template <auto SelfName> requires impl::EntityLinks::matches_name_or_null<SelfName, Name>
                friend void _adl_link_attach(LinkOne &self_link, typename Tag::Controller &con, typename Tag::Entity &self, bool symmetric, typename Tag::Id linked_id, std::string linked_name) noexcept
                {
                    if (!linked_id.is_nonzero())
                        Program::HardError("Can't link a null entity.");

                    using impl::EntityLinks::_adl_link_detach;
                    _adl_link_detach<nullptr>(self_link, &con, &self, {});

                    if (symmetric)
                        con.get(linked_id)._detail_link_attach(con, false, linked_name, self.id(), std::string(Name.view()));

                    self_link.target.target_id = linked_id;
                    self_link.target.target_link_name = std::move(linked_name);
                }

                // If `detached_id` is non-zero, checks it against the contained id and returns false on mismatch.
                // Otherwise just removes the link unconditionally, if any, and returns `true` regardless.
                // If `con` and `self` are both null, breaks the link asymmetrically. Otherwise both must be non-null, then acts symmetrically.
                template <auto SelfName> requires impl::EntityLinks::matches_name_or_null<SelfName, Name>
                friend bool _adl_link_detach(LinkOne &self_link, typename Tag::Controller *con, typename Tag::Entity *self, typename Tag::Id detached_id) noexcept
                {
                    ASSERT(bool(con) == bool(self));

                    if (detached_id.is_nonzero() && self_link.target.target_id != detached_id)
                        return false; // Linked to a different entity.
                    if (!self_link.target.target_id.is_nonzero())
                        return true; // Not linked.

                    if (con)
                        con->get(self_link.target.target_id)._detail_link_detach(nullptr, self_link.target.target_link_name, self->id());

                    self_link.target.target_id = nullptr;
                    self_link.target.target_link_name = {};
                    return true;
                }

                // Returns 1 if the link exists, otherwise 0.
                template <auto SelfName> requires impl::EntityLinks::matches_name_or_null<SelfName, Name>
                friend std::size_t _adl_link_num_targets(const LinkOne &self_link) noexcept
                {
                    return self_link.target.target_id.is_nonzero() ? 1 : 0;
                }

                // Returns the target or null.
                template <auto SelfName> requires impl::EntityLinks::matches_name_or_null<SelfName, Name>
                friend const LinkTargetIdWithName &_adl_link_single_target(const LinkOne &self_link) noexcept
                {
                    return self_link.target;
                }

                // Returns `void`, because this is not a multi-target link.
                template <auto SelfName> requires impl::EntityLinks::matches_name_or_null<SelfName, Name>
                friend void _adl_link_multi_target(const LinkOne &self_link) noexcept
                {
                    (void)self_link;
                }

                // Returns true if the `linked_id` is non-zero and matches the target id.
                // If `maybe_linked_name` is specified, also checks it for equality.
                template <auto SelfName> requires impl::EntityLinks::matches_name_or_null<SelfName, Name>
                friend bool _adl_link_has_target(const LinkOne &self_link, typename Tag::Id linked_id, std::string_view maybe_linked_name) noexcept
                {
                    return linked_id.is_nonzero()
                        && self_link.target.target_id == linked_id
                        && (maybe_linked_name.empty() || self_link.target.target_link_name == maybe_linked_name);
                }

                void _deinit(typename Tag::Controller &con, typename Tag::Entity &e)
                {
                    using impl::EntityLinks::_adl_link_detach;
                    _adl_link_detach<nullptr>(*this, &con, &e, {});
                }
            };

            // Multi-target entity links use this to signify 'no extra data per connection'.
            struct NoLinkData {};

            // Multi-target entity links expose those as elements.
            template <typename Data>
            class LinkElem
            {
                static_assert(std::default_initializable<Data>, "The user data must be default-constructible.");

              protected:
                typename Tag::Id target_id;
                std::string target_link;

              public:
                // User data, if any.
                [[no_unique_address]] Data data{};

              protected:
                constexpr LinkElem() {}

                template <typename ...P>
                constexpr LinkElem(typename Tag::Id target_id, std::string target_link)
                    : target_id(std::move(target_id)), target_link(std::move(target_link))
                {}

                LinkElem(const LinkElem &) = default;
                LinkElem &operator=(const LinkElem &) = default;
                ~LinkElem() = default;

              public:
                // The target entity id, always non-null.
                [[nodiscard]] const typename Tag::Id id() const {return target_id;}
                // The target entity's link name.
                [[nodiscard]] const std::string &target_link_name() const {return target_link;}
            };

            // When specifying a custom container for a multi-target entity link, use this as the element type.
            // Otherwise a `static_assert` is triggered.
            // This exposes protected members of `LinkElem`.
            template <typename Data>
            class LinkElemLow : public LinkElem<Data>
            {
              public:
                using LinkElem<Data>::target_id;
                using LinkElem<Data>::target_link;

                // Inheriting constructors doesn't change their access modifier, so we must duplicate them here.

                constexpr LinkElemLow() {}

                template <typename ...P>
                constexpr LinkElemLow(typename Tag::Id target_id, std::string target_link)
                    : LinkElem<Data>(std::move(target_id), std::move(target_link))
                {}
            };

            // Multi-target entity links use those traits to interact with the container storing targets.
            template <typename T>
            struct LinkContainerTraits
            {
                // This specialization supports sequental containers, like `std::vector`.

                template <bool IsConst>
                using iterator_t = std::conditional_t<IsConst, typename T::const_iterator, typename T::iterator>;

                template <typename U>
                [[nodiscard]] static bool empty(const U &cont)
                {
                    return cont.empty();
                }
                template <typename U>
                [[nodiscard]] static std::size_t size(const U &cont)
                {
                    return cont.size();
                }

                template <typename U>
                [[nodiscard]] static auto begin(U &&cont)
                {
                    return std::forward<U>(cont).begin();
                }
                template <typename U>
                [[nodiscard]] static auto end(U &&cont)
                {
                    return std::forward<U>(cont).end();
                }

                // Accesses an element by index. Should throw if out of range.
                // NOTE: This is optional. If your container isn't random-access, omit this.
                template <typename U>
                [[nodiscard]] static auto &&at(U &&cont, std::size_t i)
                requires requires{std::forward<U>(cont).at(i);}
                {
                    return std::forward<U>(cont).at(i);
                }

                // Finds an element by id, returns the iterator.
                // Returns `.end()` on failure.
                template <typename U>
                [[nodiscard]] static auto find(U &&cont, typename Tag::Id id)
                {
                    return std::find_if(cont.begin(), cont.end(), [&](const auto &elem){return elem.id() == id;});
                }

                // Inserts a new element.
                // Returns false if the ID already exists in the container, does nothing in this case.
                template <typename U, typename ...P>
                static bool insert(U &&cont, typename Tag::Id id, std::string name)
                {
                    if (find(cont, id) != cont.end())
                        return false;
                    cont.emplace_back(std::move(id), std::move(name));
                    return true;
                }

                // Erases an element by iterator.
                template <typename U, typename I>
                static void erase(U &&cont, I &&iter)
                {
                    std::forward<U>(cont).erase(std::forward<I>(iter));
                }

                // Erases all elements.
                template <typename U>
                static void clear(U &&cont)
                {
                    std::forward<U>(cont) = {};
                }

                // Returns one of the elements. Throws if none.
                // Since this is paired with `pop_one()`, we should return an element that's cheap to remove, in this case the last one.
                template <typename U>
                [[nodiscard]] static auto &&peek_one(U &&cont)
                {
                    if (cont.empty())
                        throw std::logic_error("The container is empty.");
                    return std::forward<U>(cont).back();
                }

                // Erases one of the elements. Throws if none.
                // Erases the same element as returned by `peek_one()`.
                template <typename U>
                static void pop_one(U &&cont)
                {
                    if (cont.empty())
                        throw std::logic_error("The container is empty.");
                    std::forward<U>(cont).pop_back();
                }
            };

            // This is used internally to access the container stored in `LinkContainer`, see below.
            struct LinkContainerFriend
            {
                LinkContainerFriend() = delete;
                ~LinkContainerFriend() = delete;

                template <typename T>
                [[nodiscard]] static auto &&GetContainer(T &&object) {return std::forward<T>(object).container;}
            };

            // A list of multi-target link targets. `LinkMany` stores and exposes this.
            template <typename Data, typename Container, typename ContainerTraits>
            class LinkContainer
            {
                friend LinkContainerFriend;

                static_assert(std::is_same_v<typename Container::value_type, LinkElemLow<Data>>, "Wrong container element type.");

                Container container;

                template <bool IsConst>
                using BaseIter = typename ContainerTraits::template iterator_t<IsConst>;

                // Wraps `BaseIterator` to dereference to `DesiredElemType &`.
                template <bool IsConst>
                class Iter : public impl::EntityLinks::MaybeAddContiguousIteratorConcept<BaseIter<IsConst>>
                {
                    // I'd rather inherit from `BaseIterator`, but this causes problems at least on libstdc++:
                    //   `std::to_address()` (which is needed for `std::random_access_iterator`)
                    //   is hardcoded to return `.base().operator->()` if the type inherits from a "debug iterator" base,
                    //   which is the case when we set `-D_GLIBCXX_DEBUG`.
                    //   This means that it ignores the return type of our custom `operator->()`,
                    //   and the resulting type mismatch causes the concept to reject our class. D:<

                    using Base = BaseIter<IsConst>;

                    Base base;

                  public:
                    constexpr Iter() requires std::is_default_constructible_v<Base> {}
                    constexpr Iter(Base base) : base(std::move(base)) {}

                    using value_type = LinkElem<Data>;
                    using reference = Meta::maybe_const<IsConst, value_type> &;
                    using pointer = Meta::maybe_const<IsConst, value_type> *;
                    using difference_type = typename std::iterator_traits<Base>::difference_type;
                    using iterator_category = typename std::iterator_traits<Base>::iterator_category;
                    using iterator_concept = std::contiguous_iterator_tag;

                    reference operator*() const {return base.operator*();}
                    pointer operator->() const {return base.operator->();}

                    reference operator[](const difference_type &i) const
                    requires requires{base.operator[](i);}
                    {
                        return base.operator[](i);
                    }

                    friend bool operator==(const Iter &a, const Iter &b) requires requires{a.base == b.base;} {return a.base == b.base;}
                    friend bool operator!=(const Iter &a, const Iter &b) requires requires{a.base != b.base;} {return a.base != b.base;}
                    friend bool operator< (const Iter &a, const Iter &b) requires requires{a.base <  b.base;} {return a.base <  b.base;}
                    friend bool operator<=(const Iter &a, const Iter &b) requires requires{a.base <= b.base;} {return a.base <= b.base;}
                    friend bool operator> (const Iter &a, const Iter &b) requires requires{a.base >  b.base;} {return a.base >  b.base;}
                    friend bool operator>=(const Iter &a, const Iter &b) requires requires{a.base >= b.base;} {return a.base >= b.base;}

                    Iter &operator++()
                    {
                        ++base;
                        return *this;
                    }
                    Iter operator++(int)
                    {
                        Iter ret = *this;
                        ++ret;
                        return ret;
                    }
                    Iter &operator--()
                    requires requires{--base;}
                    {
                        --base;
                        return *this;
                    }
                    Iter operator--(int)
                    requires requires{--base;}
                    {
                        Iter ret = *this;
                        --ret;
                        return ret;
                    }
                    friend Iter operator+(const Iter &a, const difference_type &b)
                    requires requires{a.base + b;}
                    {
                        return Iter(a.base + b);
                    }
                    friend Iter operator+(const difference_type &a, const Iter &b)
                    requires requires{a + b.base;}
                    {
                        return Iter(a + b.base);
                    }
                    friend Iter operator-(const Iter &a, const difference_type &b)
                    requires requires{a.base - b;}
                    {
                        return Iter(a.base - b);
                    }
                    friend difference_type operator-(const Iter &a, const Iter &b)
                    requires requires{a.base - b.base;}
                    {
                        return a.base - b.base;
                    }
                    Iter &operator+=(const difference_type &b)
                    requires requires{base += b;}
                    {
                        base += b;
                        return Iter();
                    }
                    Iter &operator-=(const difference_type &b)
                    requires requires{base -= b;}
                    {
                        base -= b;
                        return Iter();
                    }
                };

                void ThrowIfEmpty() const
                {
                    if (empty())
                        throw std::logic_error("This operation requires at least one link target to exist.");
                }

              public:
                [[nodiscard]] bool empty() const {return ContainerTraits::empty(container);}
                [[nodiscard]] bool non_empty() const {return !empty();}
                [[nodiscard]] std::size_t size() const {return ContainerTraits::size(container);}

                // Finds a linked entity by id, or throws if none such.
                [[nodiscard]] LinkElem<Data> &find(typename Tag::Id id)
                {
                    if (LinkElem<Data> *ret = find_opt(id))
                        return *ret;
                    else
                        throw std::runtime_error("No such entity is linked.");
                }
                [[nodiscard]] const LinkElem<Data> &find(typename Tag::Id id) const
                {
                    return const_cast<LinkContainer *>(this)->find(id);
                }

                // Finds a linked entity by id, or returns null if none such.
                [[nodiscard]] LinkElem<Data> *find_opt(typename Tag::Id id) noexcept
                {
                    if (auto it = ContainerTraits::find(container, id); it != ContainerTraits::end(container))
                        return &*it;
                    else
                        return nullptr;
                }
                [[nodiscard]] const LinkElem<Data> *find_opt(typename Tag::Id id) const noexcept
                {
                    return const_cast<LinkContainer *>(this)->find_opt(id);
                }

                // Returns a linked entity by index, or throws if out of range.
                // Only available if the underlying container is random-access.
                [[nodiscard]] LinkElem<Data> &operator[](std::size_t i)
                requires requires{ContainerTraits::at(container, i);}
                {
                    return ContainerTraits::at(container, i);
                }
                [[nodiscard]] const LinkElem<Data> &operator[](std::size_t i) const
                requires requires{ContainerTraits::at(container, i);}
                {
                    return ContainerTraits::at(container, i);
                }

                using iterator = Iter<false>;
                using const_iterator = Iter<true>;

                [[nodiscard]] iterator begin() {return ContainerTraits::begin(container);}
                [[nodiscard]] iterator end() {return ContainerTraits::end(container);}
                [[nodiscard]] const_iterator begin() const {return ContainerTraits::begin(container);}
                [[nodiscard]] const_iterator end() const {return ContainerTraits::end(container);}

                [[nodiscard]]       LinkElem<Data> &front()       {ThrowIfEmpty(); return *begin();}
                [[nodiscard]] const LinkElem<Data> &front() const {ThrowIfEmpty(); return *begin();}
                [[nodiscard]]       LinkElem<Data> &back()       {ThrowIfEmpty(); return *std::prev(end());}
                [[nodiscard]] const LinkElem<Data> &back() const {ThrowIfEmpty(); return *std::prev(end());}
            };

            // Inherit your entity/component from this to add a multi-target entity link.
            template <
                Meta::ConstString Name,
                // User data.
                typename Data = NoLinkData,
                // A container used to store the links.
                typename Container = std::vector<LinkElemLow<Data>>,
                // Container traits. Either specialize `LinkContainerTraits` or pass custom traits here.
                typename ContainerTraits = LinkContainerTraits<Container>
            >
            requires(Name.size > 0)
            class LinkMany
            {
                IMP_COMPONENT(Tag)

                using LinkCont = LinkContainer<Data, Container, ContainerTraits>;
                LinkCont elems;

                // Returns the underlying container. Always access it using `ContainerTraits`.
                      auto &GetCont()       {return LinkContainerFriend::GetContainer(elems);}
                const auto &GetCont() const {return LinkContainerFriend::GetContainer(elems);}

              protected:
                ~LinkMany() = default;

              public:
                constexpr LinkMany() {}

                // Copying is a no-op.
                constexpr LinkMany(const LinkMany &) {}
                constexpr LinkMany &operator=(const LinkMany &) {*this;}

                // This is used to find all links of an entity at compile-time.
                friend constexpr Meta::ConstStringTag<Name> _adl_link_marker(const std::same_as<LinkMany> auto *) {return {};}

                // Adds another entity to the link.
                // If this entity is already linked, unlinks it and links again. This happens even if the link name on that entity's side doesn't match.
                // If `symmetric` is true, acts symmetrically.
                template <auto SelfName> requires impl::EntityLinks::matches_name_or_null<SelfName, Name>
                friend void _adl_link_attach(LinkMany &self_link, typename Tag::Controller &con, typename Tag::Entity &self, bool symmetric, typename Tag::Id linked_id, std::string linked_name) noexcept
                {
                    if (!linked_id.is_nonzero())
                        Program::HardError("Can't link a null entity.");

                    // Detach if already attached.
                    using impl::EntityLinks::_adl_link_detach;
                    _adl_link_detach<Name>(self_link, &con, &self, linked_id);

                    if (symmetric)
                        con.get(linked_id)._detail_link_attach(con, false, linked_name, self.id(), std::string(Name.view()));

                    ContainerTraits::insert(self_link.GetCont(), linked_id, std::move(linked_name));
                }

                // If `id` is non-zero, only unlinks that entity, and returns `false` if it's not linked.
                // If `id` is zero, unlinks all entities, and always returns `true`.
                // If `con` and `self` are both null, breaks the link asymmetrically. Otherwise both must be non-null, then acts symmetrically.
                template <auto SelfName> requires impl::EntityLinks::matches_name_or_null<SelfName, Name>
                friend bool _adl_link_detach(LinkMany &self_link, typename Tag::Controller *con, typename Tag::Entity *self, typename Tag::Id detached_id) noexcept
                {
                    ASSERT(bool(con) == bool(self));

                    if (detached_id.is_nonzero())
                    {
                        // Unlink one.

                        auto it = ContainerTraits::find(self_link.GetCont(), detached_id);
                        if (it == ContainerTraits::end(self_link.GetCont()))
                            return false;
                        if (con)
                            con->get(it->id())._detail_link_detach(nullptr, static_cast<LinkElemLow<Data> &>(*it).target_link, self->id());
                        ContainerTraits::erase(self_link.GetCont(), it);
                    }
                    else
                    {
                        // Unlink all.

                        if (con)
                        {
                            while (!self_link.elems.empty())
                            {
                                auto &one = ContainerTraits::peek_one(self_link.GetCont());
                                con->get(one.id())._detail_link_detach(nullptr, static_cast<LinkElemLow<Data> &>(one).target_link, self->id());
                                ContainerTraits::pop_one(self_link.GetCont());
                            }
                        }
                        else
                        {
                            ContainerTraits::clear(self_link.GetCont());
                        }
                    }

                    return true;
                }

                // Returns 1 if the link exists, otherwise 0.
                template <auto SelfName> requires impl::EntityLinks::matches_name_or_null<SelfName, Name>
                friend std::size_t _adl_link_num_targets(const LinkMany &self_link) noexcept
                {
                    return self_link.elems.size();
                }

                // Returns `void`, because this is not a single-target link.
                template <auto SelfName> requires impl::EntityLinks::matches_name_or_null<SelfName, Name>
                friend void _adl_link_single_target(const LinkMany &self_link) noexcept
                {
                    (void)self_link;
                }

                template <auto SelfName> requires impl::EntityLinks::matches_name_or_null<SelfName, Name>
                friend LinkCont &_adl_link_multi_target(LinkMany &self_link) noexcept
                {
                    return self_link.elems;
                }

                // Returns true if the list of targets contains `linked_id`.
                template <auto SelfName> requires impl::EntityLinks::matches_name_or_null<SelfName, Name>
                friend bool _adl_link_has_target(const LinkMany &self_link, typename Tag::Id linked_id, std::string_view maybe_linked_name) noexcept
                {
                    if (!linked_id.is_nonzero())
                        return false;
                    auto elem = self_link.elems.find_opt(linked_id);
                    return elem && (maybe_linked_name.empty() || elem->target_link_name() == maybe_linked_name);
                }

                // Returns a container with the linked entity IDs, and possibly user data.
                template <Meta::ConstString SelfName> requires(SelfName == Name)
                [[nodiscard]] LinkCont &link()
                {
                    return elems;
                }
                template <Meta::ConstString SelfName> requires(SelfName == Name)
                [[nodiscard]] const LinkCont &link() const
                {
                    return elems;
                }

              private:
                void _deinit(typename Tag::Controller &con, typename Tag::Entity &e)
                {
                    using impl::EntityLinks::_adl_link_detach;
                    _adl_link_detach<nullptr>(*this, &con, &e, {});
                }
            };

            struct Controller : NextBase::Controller
            {
                using NextBase::Controller::Controller;

                // Link status:

                // Returns true if the link exists. For multi-target links, returns true if there is at least one target.
                // This is the static overload, with the link name fixed at compile-time.
                template <Meta::ConstString Name, Meta::deduce..., typename T>
                requires valid_link_owner<T, Name>
                [[nodiscard]] static bool has_link(const T &e)
                {
                    return num_link_targets<Name>(e) > 0;
                }
                // Returns true if the link exists. For multi-target links, returns true if there is at least one target.
                // This is the type-erased overload. Throws if the `name` is invalid.
                [[nodiscard]] static bool has_link(std::string_view name, const typename Tag::Entity &e)
                {
                    return num_link_targets(name, e) > 0;
                }

                // Returns the number of targets of a link. For single-target links, that's either 0 or 1.
                // This is the static overload, with the link name fixed at compile-time.
                template <Meta::ConstString Name, Meta::deduce..., typename T>
                requires valid_link_owner<T, Name>
                [[nodiscard]] static std::size_t num_link_targets(const T &e)
                {
                    using impl::EntityLinks::_adl_link_num_targets;
                    return _adl_link_num_targets<Name>(e);
                }
                // Returns the number of targets of a link. For single-target links, that's either 0 or 1.
                // This is the type-erased overload. Throws if the `name` is invalid.
                [[nodiscard]] static std::size_t num_link_targets(std::string_view name, const typename Tag::Entity &e)
                {
                    return e._detail_link_num_targets(name);
                }


                // Link target:

                // Returns the target of a single-target link, throws if none. Doesn't compile if this is not a single-target link.
                // This is the static overload, with the link name fixed at compile-time.
                template <Meta::ConstString Name, Meta::deduce..., typename T>
                requires valid_single_target_link_owner<T, Name>
                [[nodiscard]] typename Tag::Entity &get_link(T &e)
                {
                    typename Tag::Entity *ret = get_link_opt<Name>(e);
                    if (!ret)
                        throw std::runtime_error("This link is null.");
                    return *ret;
                }
                // Returns the target of a single-target link, throws if none. Throws if not a single-target link.
                // This is the type-erased overload. Throws if the `name` is invalid.
                [[nodiscard]] typename Tag::Entity &get_link(std::string_view name, typename Tag::Entity &e)
                {
                    typename Tag::Entity *ret = get_link_opt(name, e);
                    if (!ret)
                        throw std::runtime_error("This link is null.");
                    return *ret;
                }
                // Returns the target of a single-target link, throws if none. Doesn't compile if this is not a single-target link.
                // This is the static overload, with the link name fixed at compile-time.
                template <Meta::ConstString Name, Meta::deduce..., typename T>
                requires valid_single_target_link_owner<T, Name>
                [[nodiscard]] const typename Tag::Entity &get_link(const T &e) const
                {
                    return const_cast<Controller &>(*this).get_link<Name>(const_cast<T &>(e));
                }
                // Returns the target of a single-target link, throws if none. Throws if not a single-target link.
                // This is the type-erased overload. Throws if the `name` is invalid.
                [[nodiscard]] const typename Tag::Entity &get_link(std::string_view name, const typename Tag::Entity &e) const
                {
                    return const_cast<Controller &>(*this).get_link(name, const_cast<typename Tag::Entity &>(e));
                }

                // Returns the target of a single-target link, null if none. Doesn't compile if this is not a single-target link.
                // This is the static overload, with the link name fixed at compile-time.
                template <Meta::ConstString Name, Meta::deduce..., typename T>
                requires valid_single_target_link_owner<T, Name>
                [[nodiscard]] typename Tag::Entity *get_link_opt(T &e)
                {
                    return static_cast<typename Tag::Controller &>(*this).get_opt(get_link_low_opt<Name>(e).target_id);
                }
                // Returns the target of a single-target link, null if none. Throws if not a single-target link.
                // This is the type-erased overload. Throws if the `name` is invalid.
                [[nodiscard]] typename Tag::Entity *get_link_opt(std::string_view name, typename Tag::Entity &e)
                {
                    return static_cast<typename Tag::Controller &>(*this).get_opt(get_link_low_opt(name, e).target_id);
                }
                // Returns the target of a single-target link, null if none. Doesn't compile if this is not a single-target link.
                // This is the static overload, with the link name fixed at compile-time.
                template <Meta::ConstString Name, Meta::deduce..., typename T>
                requires valid_single_target_link_owner<T, Name>
                [[nodiscard]] const typename Tag::Entity *get_link_opt(const T &e) const
                {
                    return const_cast<Controller &>(*this).get_link_opt<Name>(const_cast<T &>(e));
                }
                // Returns the target of a single-target link, null if none. Throws if not a single-target link.
                // This is the type-erased overload. Throws if the `name` is invalid.
                [[nodiscard]] const typename Tag::Entity *get_link_opt(std::string_view name, const typename Tag::Entity &e) const
                {
                    return const_cast<Controller &>(*this).get_link_opt(name, const_cast<typename Tag::Entity &>(e));
                }

                // Returns the target id and the target link name of a single-target link, null if none. Doesn't compile if this is not a single-target link.
                // This is the static overload, with the link name fixed at compile-time.
                template <Meta::ConstString Name, Meta::deduce..., typename T>
                requires valid_single_target_link_owner<T, Name>
                [[nodiscard]] static const LinkTargetIdWithName &get_link_low_opt(const T &e)
                {
                    using impl::EntityLinks::_adl_link_single_target;
                    return _adl_link_single_target<Name>(e);
                }
                // Returns the target id and the target link name of a single-target link, null if none. Throws if not a single-target link.
                // This is the type-erased overload. Throws if the `name` is invalid.
                [[nodiscard]] static const LinkTargetIdWithName &get_link_low_opt(std::string_view name, const typename Tag::Entity &e)
                {
                    return e._detail_link_single_target(name);
                }


                // Multiple link targets:

                // Returns the targets of a mutli-target link.
                // Doesn't compile if this is not a multi-target link. There's no type-erased overload, because the return type can vary.
                template <Meta::ConstString Name, Meta::deduce..., typename T>
                requires valid_multi_target_link_owner<T, Name>
                [[nodiscard]] static auto &get_links(T &e)
                {
                    using impl::EntityLinks::_adl_link_multi_target;
                    return _adl_link_multi_target<Name>(e);
                }
                // Returns the targets of a mutli-target link.
                // Doesn't compile if this is not a multi-target link. There's no type-erased overload, because the return type can vary.
                template <Meta::ConstString Name, Meta::deduce..., typename T>
                requires valid_multi_target_link_owner<T, Name>
                [[nodiscard]] static const auto &get_links(const T &e)
                {
                    using impl::EntityLinks::_adl_link_multi_target;
                    return _adl_link_multi_target<Name>(const_cast<T &>(e));
                }


                // Seaching link for a target:

                // Returns true if the link has the specified target.
                // This is the static overload, with the link name fixed at compile-time.
                template <Meta::ConstString Name, Meta::deduce..., typename T> requires valid_link_owner<T, Name>
                [[nodiscard]] static bool has_link_to(const T &e, typename Tag::ConstEntityOrId target)
                {
                    using impl::EntityLinks::_adl_link_has_target;
                    return _adl_link_has_target<Name>(e, target.value, {});
                }
                // Returns true if the link has the specified target.
                // This is the type-erased overload.
                [[nodiscard]] static bool has_link_to(std::string_view name, const typename Tag::Entity &e, typename Tag::ConstEntityOrId target)
                {
                    return e._detail_link_has_target(name, target.value, {});
                }


                // Seaching link for a target, also specifying the target name:

                // Returns true if the link has the specified target.
                // This is the fully static overload, with both names fixed at compile-time.
                template <Meta::ConstString NameA, Meta::ConstString NameB, Meta::deduce..., typename A, typename B>
                requires valid_link_owner<A, NameA> && valid_link_owner<B, NameB> && link_dynamic_castable_to_entity<B>
                [[nodiscard]] static bool has_link_to(const A &a, const B &b)
                {
                    using impl::EntityLinks::_adl_link_has_target;
                    return _adl_link_has_target<NameA>(a, dynamic_cast<const typename Tag::Entity &>(b).id(), NameB.view());
                }
                // Returns true if the link has the specified target. Also checks the target link name.
                // This is the half-type-erased overload, with only the second entity being type-erased.
                template <Meta::ConstString Name, Meta::deduce..., typename T> requires valid_link_owner<T, Name>
                [[nodiscard]] static bool has_link_to(const T &e, std::string_view linked_name, typename Tag::ConstEntityOrId target)
                {
                    if (linked_name.empty())
                        return false;
                    using impl::EntityLinks::_adl_link_has_target;
                    return _adl_link_has_target<Name>(e, target.value, linked_name);
                }
                // Returns true if the link has the specified target. Also checks the target link name.
                // This is the completely type-erased overload.
                [[nodiscard]] static bool has_link_to(std::string_view name, const typename Tag::Entity &e, std::string_view linked_name, typename Tag::ConstEntityOrId target)
                {
                    if (linked_name.empty())
                        return false;
                    return e._detail_link_has_target(name, target.value, linked_name);
                }


                // Adding link:

                // Links `a` and `b` using links `NameA` and `NameB` respectively.
                // This is the fully static overload, with both names fixed at compile-time.
                template <Meta::ConstString NameA, Meta::ConstString NameB, Meta::deduce..., typename A, typename B>
                requires valid_link_owner<A, NameA> && valid_link_owner<B, NameB>
                    // Note, both need to be `dynamic_cast`able. The underlying function will cast `A`.
                    && link_dynamic_castable_to_entity<A> && link_dynamic_castable_to_entity<B>
                void link(A &a, B &b)
                {
                    link<NameA>(a, std::string(NameB.view()), dynamic_cast<typename Tag::Entity &>(b).id());
                }

                // Links `e` and `other_id` using links `Name` and `other_name` respectively.
                // This is the half-type-erased overload, with only the second entity being type-erased.
                template <Meta::ConstString Name, Meta::deduce..., typename T>
                requires valid_link_owner<T, Name> && link_dynamic_castable_to_entity<T>
                void link(T &e, std::string other_name, typename Tag::EntityOrId other)
                {
                    using impl::EntityLinks::_adl_link_attach;
                    _adl_link_attach<Name>(e, static_cast<typename Tag::Controller &>(*this), dynamic_cast<typename Tag::Entity &>(e), true, other.value, std::move(other_name));
                }

                // Links `a` and `b` using links `name_a` and `name_b` respectively.
                // This is the completely type-erased overload.
                void link(std::string_view name_a, typename Tag::Entity &a, std::string name_b, typename Tag::EntityOrId b)
                {
                    // Note that the first name is `std::string_view`, and the second is `std::string`.
                    // This is wonky, but also the most efficient.
                    a._detail_link_attach(static_cast<typename Tag::Controller &>(*this), true, name_a, b.value, name_b);
                }


                // Removing link:

                // Unlink all entities.
                // This is the static overload, with the link name fixed at compile-time.
                template <Meta::ConstString Name, Meta::deduce..., typename T>
                requires valid_link_owner<T, Name> && link_dynamic_castable_to_entity<T>
                void unlink(T &e)
                {
                    using impl::EntityLinks::_adl_link_detach;
                    _adl_link_detach<Name>(e, static_cast<typename Tag::Controller *>(this), &dynamic_cast<typename Tag::Entity &>(e), {});
                }
                // Unlink all entities.
                // This is the type-erased overload. Throws if the `name` is invalid.
                void unlink(std::string_view name, typename Tag::Entity &e)
                {
                    e._detail_link_detach(static_cast<typename Tag::Controller *>(this), name, {});
                }

                // Unlink a single entity. If `id` is null, has no effect. Throws if it's not linked.
                // This is the static overload, with the link name fixed at compile-time.
                template <Meta::ConstString Name, Meta::deduce..., typename T>
                requires valid_link_owner<T, Name> && link_dynamic_castable_to_entity<T>
                void unlink(T &e, typename Tag::EntityOrId target)
                {
                    if (!target.value.is_nonzero())
                        return; // We can't just forward a zero, because `DetachLinkLowUnsafeAsymmetric()` treats it as 'unlink all targets'.
                    using impl::EntityLinks::_adl_link_detach;
                    if (!_adl_link_detach<Name>(e, static_cast<typename Tag::Controller *>(this), &dynamic_cast<typename Tag::Entity &>(e), target.value))
                        throw std::runtime_error("That entity isn't linked.");
                }
                // Unlink a single entity. If `target` is null, has no effect. Throws if it's not linked.
                // This is the type-erased overload. Throws if the `name` is invalid.
                void unlink(std::string_view name, typename Tag::Entity &e, typename Tag::EntityOrId target)
                {
                    if (!target.value.is_nonzero())
                        return; // We can't just forward a zero, because `DetachLinkLowUnsafeAsymmetric()` treats it as 'unlink all targets'.
                    if (!e._detail_link_detach(static_cast<typename Tag::Controller *>(this), name, target.value))
                        throw std::runtime_error("That entity isn't linked.");
                }
            };
        };
    }
}
