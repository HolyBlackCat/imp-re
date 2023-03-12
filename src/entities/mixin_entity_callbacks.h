#pragma once

#include <utility>

#include "entities/core.h"
#include "meta/common.h"
#include "meta/type_info.h"
#include "strings/format.h"

// Gives components optional callbacks, called when a controller creates or destroys them as a part of an entity.
// Usage:
//     void _init(Game::Controller &c) {...}
//     void _deinit(Game::Controller &c) {...}
// NOTE: You don't need to call the parent callback, that's done automatically.
// NOTE: If your class doesn't directly contain `IMP_..._COMPONENT`, the callbacks will be silently ignored.
// NOTE: Bases are initialized first, and deinitialized last.
// NOTE: Prefer non-empty components. We sometimes can't determine how to sort empty ones,
//         then you'll get a runtime error at program startup. See below for more details.

namespace Ent
{
    namespace Mixins
    {
        template <typename Tag, typename NextBase>
        struct EntityCallbacks : NextBase
        {
          private:
            // Manages an automatically detected list of callbacks in all components of an entity.
            // `Func` specifies the function, it must be `template <typename T> using MyFunc = Meta::value_tag<&T::my_func>`.
            // `Reverse` reverses the component sorting order.
            // The components are sorted by end address (sic, we use address right past the end of a component, for better sorting order),
            //   and then by `is_base_of`. If this leaves any ambiguities, you get a compilation error.
            // This gives the most natural sorting order: bases first, then the derived classes.
            template <template <typename> typename Func, bool Reverse>
            struct FuncList
            {
                // Whether component `C` seems to override the function.
                template <Component<Tag> C>
                static constexpr bool overrides_func_weak = requires{requires std::is_same_v<C, Meta::member_pointer_owner_t<decltype(Func<C>::value)>>;};

                // Same as `overrides_func_weak`, but with more strict checks.
                // `overrides_func_weak` must be equivalent to `overrides_func_strong`, or we trigger a static assertion.
                template <Component<Tag> C>
                static constexpr bool overrides_func_strong = requires{requires std::is_same_v<void (C::*const)(typename Tag::Controller &), decltype(Func<C>::value)>;};

                template <EntityType<Tag> E>
                static constexpr std::size_t num_matching_components = []<typename ...C>(Meta::type_list<C...>)
                {
                    // Check that `overrides_func_weak` matches `overrides_func_strong`.
                    static_assert(((overrides_func_weak<C> == overrides_func_strong<C>) && ...), "Bad entity callback.");
                    return (overrides_func_weak<C> + ... + std::size_t(0));
                }
                (Ent::EntityComponents<Tag, E>{});

                template <EntityType<Tag> E>
                static void call(typename Tag::Controller &c, E &e)
                {
                    static const auto arr = [&]<typename ...C>(Meta::type_list<C...>)
                    {
                        using func_t = void (*)(typename Tag::Controller &c, E &e);
                        struct Entry
                        {
                            // The index of this entry before sorting. Used for indexing into the `bases` array.
                            std::size_t index = 0;
                            // The component name. Used for better error messages.
                            std::string_view comp_name;
                            // The function.
                            func_t func = nullptr;
                            // The address JUST PAST the component owning this function, used for sorting.
                            // This gives a better sorting order.
                            char *address = nullptr;
                            // Which classes are the bases of this one, used for sorting.
                            std::array<bool, num_matching_components<E>> bases{};
                        };
                        std::array<Entry, num_matching_components<E>> funcs_with_addresses{};
                        std::size_t i = 0;
                        ([&]{
                            if constexpr (overrides_func_weak<C>)
                            {
                                using ThisClass = C;
                                funcs_with_addresses[i] = {
                                    .index = i,
                                    .comp_name = Meta::TypeName<C>(),
                                    .func = [](typename Tag::Controller &c, E &e){(e.*Func<C>::value)(c);},
                                    .address = (char *)&static_cast<C &>(e) + sizeof(C),
                                    .bases = {std::is_base_of_v<C, ThisClass>...},
                                };
                                i++;
                            }
                        }(), ...);
                        std::sort(funcs_with_addresses.begin(), funcs_with_addresses.end(), [](const Entry &a, const Entry &b)
                        {
                            // By component address.
                            if (auto d = a.address - b.address)
                                return (d < 0) != Reverse;
                            // By is-base-of relation.
                            if (int d = b.bases[a.index] - a.bases[b.index])
                                return (d < 0) != Reverse;
                            // Identity.
                            if (&a == &b)
                                return false;
                            throw std::runtime_error(FMT(
                                "Unsure how to order two components of entity `{}` to invoke a callback: `{}` and `{}`.\n"
                                "They have the same end address and don't inherit from one another. Make them non-empty to assign different end addresses.",
                            Meta::TypeName<E>(), a.comp_name, b.comp_name));
                        });
                        std::array<func_t, num_matching_components<E>> ret{};
                        for (i = 0; i < num_matching_components<E>; i++)
                            ret[i] = funcs_with_addresses[i].func;
                        return ret;
                    }
                    (Ent::EntityComponents<Tag, E>{});
                    for (auto func : arr)
                        func(c, e);
                }
            };

            template <typename T> using FuncInit = Meta::value_tag<&T::_init>;
            template <typename T> using FuncDeinit = Meta::value_tag<&T::_deinit>;

          public:
            struct CustomizedEntityBase : NextBase::CustomizedEntityBase
            {
                virtual void OnCreated(typename Tag::Controller &controller) = 0;
                virtual void OnDestroyed(typename Tag::Controller &controller) = 0;
            };

            template <EntityType<Tag> E>
            struct FullEntity : NextBase::template FullEntity<E>
            {
                using NextBase::template FullEntity<E>::FullEntity;

                void OnCreated(typename Tag::Controller &controller) override
                {
                    FuncList<FuncInit, false>::call(controller, static_cast<E &>(*this));
                }

                void OnDestroyed(typename Tag::Controller &controller) override
                {
                    FuncList<FuncDeinit, true>::call(controller, static_cast<E &>(*this));
                }
            };

            struct Controller : NextBase::Controller
            {
                using NextBase::Controller::Controller;

                template <EntityType<Tag> E>
                void OnEntityCreated(typename Tag::template FullEntity<E> &e)
                {
                    e.OnCreated(*this);
                    NextBase::Controller::OnEntityCreated(e);
                }

                void OnEntityDestroyed(CustomizedEntityBase &e)
                {
                    NextBase::Controller::OnEntityDestroyed(e);
                    e.OnDestroyed(*this);
                }
            };
        };
    }
}
