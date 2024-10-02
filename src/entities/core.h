#pragma once

// The core implementation of the entity system.

#include <compare>
#include <concepts>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "macros/detect_bases.h"
#include "macros/generated.h"
#include "meta/common.h"
#include "meta/lists.h"
#include "meta/type_info.h"
#include "strings/format.h"

/* INTRODUCTION
 *
 * This is an ECS-like system. More like ECC, because we have categories instead of systems.
 *
 * Definitions:
 *
 * - TAG - a struct created with the `Ent::BasicTag` CRTP base. You need to create at least one.
 *   Different tags give you different independent ECC systems.
 *   Most interaction with the system happens using the static members of your tag, not the `Ent` namespace.
 *   You can override some things in your tag to customize the ECC.
 *
 * - COMPONENT - a struct with the `IMP_COMPONENT(Tag)` macro in it.
 *   There is an alternative macro `IMP_STANDALONE_COMPONENT(Tag)`, which
 *   makes your class a valid entity in addition to it being a component, see below.
 *
 * - ENTITY - a struct that inherits from one or more components.
 *   If it's a component itself, it must use the `IMP_STANDALONE_COMPONENT` macro instead of `IMP_COMPONENT`.
 *
 *   - `MyTag::Entity` - a base class that's automatically added to all entities, and which you can `dynamic_cast` to.
 *     It has a few methods to get entity components, but you can also `dynamic_cast` directly to components.
 *
 *   - `EntityDesc` - a type-erased class that describes what component an entity has.
 *
 * - LIST - a container type that can hold entity pointers. It must implement a certain interface.
 *   Some built-in lists are provided for your convenience.
 *
 * - CATEGORY - a typedef pointing to `Tag::Category<List, Components...>`. You can feed categories to
 *   a controller (see below) to get lists of entities with the respective components, stored in the specified container.
 *
 *   - `Tag::CustomCategory<List, Predicate>` - a low-level version of `Category` that lets you specify a custom
 *     predicate taking `EntityDesc`, instead of querying
 *
 * - CONTROLLER - creates and destroys entities, and owns them. Exposes entity lists for all used categories.
 */

namespace Ent
{
    // Tags:

    namespace impl
    {
        // A stateful trick to register a struct as tag.
        // Because any other method of distinguishing tags seems to fail in a CRTP base.

        template <typename T>
        struct TestTag
        {
            friend constexpr void _adl_imp_IsComponentSystemTag(TestTag<T>);
        };

        template <typename T>
        struct RegisterAsTag
        {
            friend constexpr void _adl_imp_IsComponentSystemTag(TestTag<T>) {}
        };

        constexpr void _adl_imp_IsComponentSystemTag() {} // A dummy ADL base.

        template <typename T>
        concept RegisteredAsTag = requires
        {
            // This pattern tests if the function is callable at compile-time.
            // The `bool_constant` trick turns the lack of compile-time callability into a soft error instead of a hard error.
            requires std::bool_constant<(void(_adl_imp_IsComponentSystemTag(TestTag<T>{})), true)>::value;
        };
    }

    // A tag. Use `BasicTag` (defined way below) to create new tags.
    template <typename Tag>
    concept TagType = Meta::cvref_unqualified<Tag> && impl::RegisteredAsTag<Tag>;


    // Components:

    namespace impl
    {
        // Components pass this to `IMP_DETECTABLE_BASE()`.
        template <TagType Tag>
        struct ComponentBaseTag {};

        constexpr void _adl_imp_ComponentMarker() {} // A dummy ADL target.

        // Whether `T` is a component.
        template <typename T, typename Tag>
        concept IsComponentLow = requires(Tag tag, T &t)
        {
            _adl_imp_ComponentMarker(tag, &t);
        };

        // Whether `T` is a component that can be constructed directly as an entity.
        template <typename T, typename Tag>
        concept IsStandaloneComponentLow = requires
        {
            // Can't use `requires(...)` parameters here, because this call is evaluated (even though it's just at compile-time).
            requires _adl_imp_ComponentMarker(Tag{}, (T *)nullptr);
        };

        // Components add this as a friend.
        // This is templated, so you can provide your own specializations (with tag types) that automatically become friends.
        template <typename FriendTag>
        struct ComponentFriend
        {
            ComponentFriend() = delete;
            ~ComponentFriend() = delete;
        };
    }

    // A component is a class that has the `IMP_COMPONENT` macro in it (or one of its variations), with the right tag.
    // Several macros per class are allowed.
    template <typename T, typename Tag>
    concept Component = Meta::cvref_unqualified<T> && TagType<Tag> && impl::IsComponentLow<T, Tag>;

    // A component created with the `IMP_STANDALONE_COMPONENT` macro. Unlike regular components, those can be created directly as entities.
    template <typename T, typename Tag>
    concept StandaloneComponent = Component<T, Tag> && impl::IsStandaloneComponentLow<T, Tag>;

    // Assigns indices to components.
    template <TagType Tag>
    class ComponentRegistry
    {
        struct State
        {
            bool finalized = false;
            int count = 0;
        };
        static State &GetState()
        {
            static State ret;
            return ret;
        }

      public:
        ComponentRegistry() = delete;
        ~ComponentRegistry() = delete;

        // The number of registered components for this tag.
        [[nodiscard]] static int Count() {return GetState().count;}

        // Registers a component at program startup, and gives its index.
        template <Component<Tag> T>
        struct Type
        {
            inline static const int index = []{
                State &state = GetState();
                if (state.finalized)
                    throw std::runtime_error("Entities: Internal error: Attempt to register a component too late.\nMake sure any global controllers are null by default, and are initialized later.");
                return state.count++;
            }();

          private:
            static constexpr std::integral_constant<const int *, &index> registration_helper;
        };

        // After this is called, attempting to register a component will cause an error.
        static void Finalize() {GetState().finalized = true;}
    };


    // Entities:

    namespace impl
    {
        // A `Meta::TypeList` of components an entity class has.
        // Returns an empty list for non-entities.
        template <TagType Tag, typename T>
        using EntityComponentsIfAny = Macro::DetectBases::BasesAndSelf<impl::ComponentBaseTag<Tag>, T, Macro::DetectBases::GoodBasesChecked>;
    }

    // An entity is a class that inherits components and/or is itself a component.
    // It can't be `final`, since we automatically inherit from them to add extra information.
    // If it's a component, it must be an `IMP_STANDALONE_COMPONENT`.
    template <typename T, typename Tag>
    concept EntityType =
        Meta::cvref_unqualified<T> &&
        Meta::list_size<impl::EntityComponentsIfAny<Tag, T>> > 0 &&
        !std::is_final_v<T> &&
        !std::is_abstract_v<T> && // This should be redundant, but gives better errors and removes some warnings.
        (!Component<T, Tag> || StandaloneComponent<T, Tag>);

    // A `Meta::TypeList` of components an entity class has.
    // Fails for non-entities (including for classes without components), so it never returns an empty list.
    template <TagType Tag, EntityType<Tag> T>
    using EntityComponents = impl::EntityComponentsIfAny<Tag, T>;


    // Lists:

    // An abstract base for entity lists.
    // See `concept List` below for the exact requires, and `entities/lists.h` for some examples.
    template <TagType Tag>
    struct ListBase
    {
        ListBase() {}

        // Move-only, to prevent the user from copying those.
        ListBase(ListBase &&) = default;
        ListBase &operator=(ListBase &&) = default;

        virtual ~ListBase() {}

        virtual void Insert(typename Tag::Entity &entity) = 0;
        virtual void Erase(typename Tag::Entity &entity) noexcept = 0;
        // Return any entity in the list, or null if none.
        [[nodiscard]] virtual typename Tag::Entity *AnyEntity() noexcept = 0;
    };

    // Entity lists add this as a friend.
    // See `concept List` below for the exact requires, and `entities/lists.h` for some examples.
    struct ListFriend
    {
        ListFriend() = delete;
        ~ListFriend() = delete;

        // Performs a `static_cast` on the list, with `friend` permissions.
        template <typename T>
        static auto Cast(auto &&list) noexcept -> decltype(static_cast<T>(list))
        {
            return static_cast<T>(list);
        }
    };

    // A list is a tag struct that has a `Type<Tag, Pred>` template in it,
    // which in turn privately inherits from `ListBase`, adds `ListFriend` as a friend, and has any public interface you need.
    template <typename T, typename Tag, typename Pred>
    concept List = Meta::cvref_unqualified<T> && requires
    {
        typename T::template Type<Tag, Pred>;
        // Would also check this, but then `Mixins::GlobalEntityLists` stops working, because of the quirks of the instantiation order.
        // requires(typename T::template Type<Tag, Pred> list) {
        //     requires !std::derived_from<decltype(list), ListBase<Tag>> && std::is_base_of_v<ListBase<Tag>, decltype(list)>; // The base exists, but is not public.
        //     ListFriend::Cast<ListBase<Tag> &>(list); // The base can be accessed with the helper.
        // }
    };


    // Predicates:

    // A type-erased entity description. Cheap to copy.
    template <TagType Tag>
    class EntityDesc
    {
        struct State
        {
            std::vector<char/*bool*/> components;
            std::vector<int> component_indices;
        };
        const State *state = nullptr;

        EntityDesc(const State *state) : state(state) {}

      public:
        constexpr EntityDesc() {}

        template <EntityType<Tag> E>
        [[nodiscard]] static EntityDesc Describe()
        {
            static const State state = []<typename ...C>(Meta::type_list<C...>){
                State state;
                state.components.resize(ComponentRegistry<Tag>::Count());
                (void(state.components[ComponentRegistry<Tag>::template Type<C>::index] = true), ...);
                state.component_indices = {ComponentRegistry<Tag>::template Type<C>::index...};
                return state;
            }(EntityComponents<Tag, E>{});
            return &state;
        }

        [[nodiscard]] explicit operator bool() const
        {
            return bool(state);
        }

        [[nodiscard]] bool Has(int comp_index) const
        {
            return state->components[comp_index];
        }

        template <Component<Tag> C>
        [[nodiscard]] bool Has() const
        {
            return Has(ComponentRegistry<Tag>::template Type<C>::index);
        }

        [[nodiscard]] const std::vector<int> &ComponentIndices() const
        {
            return state->component_indices;
        }
    };

    // A base class for predicates, which filter entities by the components they have.
    template <TagType Tag>
    struct PredicateBase
    {
        virtual bool Matches(const EntityDesc<Tag> &desc) const = 0;
    };

    // A specific predicate derived from `PredicateBase`.
    template <typename T, typename Tag>
    concept Predicate = Meta::cvref_unqualified<T> && std::derived_from<T, PredicateBase<Tag>> && !std::is_abstract_v<T>;


    // Categories:

    namespace impl
    {
        // EntityType categories inherit from this.
        template <TagType tag>
        struct CategoryBase
        {
            CategoryBase() = delete;
            ~CategoryBase() = delete;
        };
    }

    // Matches a specialization of `Category<...>`, defined below.
    template <typename T, typename Tag>
    concept EntityCategory = Meta::cvref_unqualified<T> && std::derived_from<T, impl::CategoryBase<Tag>> && requires{typename T::list_t; typename T::predicate_t;};

    // Checks `Tag::PrepareCategoryType<T>::type` against the `Category` concept.
    // Normally that's an identity type funciton, see the comments on it for details.
    template <typename T, typename Tag>
    concept UnpreparedEntityCategory = EntityCategory<typename Tag::template PrepareCategoryType<T>::type, Tag>;

    // Gives the list type associated with the category `Cat`, after transforming it with `PrepareCategoryType`.
    template <typename Tag, typename Cat>
    using UnpreparedCategoryListType = typename Tag::template PrepareCategoryType<Cat>::type::list_t;

    // Assigns indices to categories.
    template <TagType Tag>
    class CategoryRegistry
    {
      public:
        struct Desc
        {
            // Constructs a list for this category.
            std::unique_ptr<ListBase<Tag>> (*make_list)() = nullptr;
            // Checks if an entity belongs in the list.
            bool (*matches)(const EntityDesc<Tag> &desc) = nullptr;
        };

      private:
        struct State
        {
            bool finalized = false;
            std::vector<Desc> descs;
        };
        static State &GetState()
        {
            static State ret;
            return ret;
        }

      public:
        CategoryRegistry() = delete;
        ~CategoryRegistry() = delete;

        // The number of registered categories.
        [[nodiscard]] static int Count() {return int(GetState().descs.size());}
        // Extra information for registered categories.
        [[nodiscard]] static const std::vector<Desc> &Descriptions() {return GetState().descs;}

        // Registers a category and returns its index.
        template <EntityCategory<Tag> T>
        struct Type
        {
            inline static const int index = []{
                State &state = GetState();
                if (state.finalized)
                    throw std::runtime_error("Entities: Internal error: Attempt to register a category too late.\nMake sure any global controllers are null by default, and are initialized later.");
                int ret = Count();
                state.descs.push_back({
                    .make_list = []() -> std::unique_ptr<ListBase<Tag>>
                    {
                        // `ListFriend::Cast` can't throw so this should be ok.
                        return std::unique_ptr<ListBase<Tag>>(&ListFriend::Cast<ListBase<Tag> &>(*new typename T::list_t));
                    },
                    .matches = [](const EntityDesc<Tag> &desc) -> bool
                    {
                        return typename T::predicate_t{}.Matches(desc);
                    },
                });
                return ret;
            }();
          private:
            static constexpr std::integral_constant<const int *, &index> registration_helper;
        };

        // After this is called, attempting to register a category will cause an error.
        static void Finalize() {GetState().finalized = true;}
    };

    // Lists categories of an entity. The resulting list is sorted.
    // Throws if there are none.
    template <TagType Tag, EntityType<Tag> E>
    [[nodiscard]] const std::vector<int> &EntityCategories()
    {
        // We can't just store the vector, because it tends to be destructed too early.
        // Clang complains that vector constructor is not `constexpr`, so we have to use a pointer.
        static constinit std::vector<int> *ret = nullptr;
        [[maybe_unused]] static const std::nullptr_t once = [&]{
            ret = new std::vector<int>;
            for (int i = 0; i < CategoryRegistry<Tag>::Count(); i++)
            {
                if (CategoryRegistry<Tag>::Descriptions().at(i).matches(EntityDesc<Tag>::template Describe<E>()))
                    ret->push_back(i);
            }
            if (ret->empty())
                throw std::runtime_error(FMT("Entity `{}` doesn't belong to any categories.", Meta::TypeName<E>()));
            return nullptr;
        }();
        return *ret;
    }


    // The tag:

    namespace impl
    {
        // The default tag implementation. This is ultimately a CRTP base.
        template <typename Tag>
        struct DefaultTag
        {
            // In this class defintion, refer to customizable members with `Tag::`,
            // to make sure they can be overriden in mixins.

            // Register `Tag` as a tag type.
            static_assert((void(impl::RegisterAsTag<Tag>{}), true));

            class Controller;
            class Entity;

            // Each entity gets an incremental ID of this type.
            // This is mostly a customization point. User code will use `Id` instead.
            // Inside of this class, use `typename Tag::entity_id_underlying_t` to refer to this type, to allow derived classes to override this.
            using entity_id_underlying_t = unsigned int;

            // Stores unique entity IDs. Those are never reused.
            class Id
            {
                friend Entity;
                typename Tag::entity_id_underlying_t value = 0; // Actual IDs are always >0.

              public:
                constexpr Id() {}
                constexpr Id(std::nullptr_t) {}

                friend constexpr auto operator<=>(const Id &, const Id &) = default;
                // Parallel-hashmap and our own hasher understand this ADL customization point.
                friend constexpr std::size_t hash_value(const Id &id) {return std::hash<typename Tag::entity_id_underlying_t>{}(id.value);}

                // This is a named function, as opposed to `operator bool`, because it doesn't guarantee that the ID is valid.
                // Use `controller.valid(id)` for that (from `Mixins::GlobalEntityLists`).
                [[nodiscard]] bool is_nonzero() const {return value != 0;}

                [[nodiscard]] typename Tag::entity_id_underlying_t get_value() const
                {
                    return value;
                }
            };

            // An implementation of `PredicateBase` for testing if an entity has all the listed components.
            template <Component<Tag> ...Comp>
            struct HasComponents : PredicateBase<Tag>
            {
                bool Matches(const EntityDesc<Tag> &desc) const override
                {
                    return (desc.template Has<Comp>() && ...);
                }
            };

            // An entity category.
            template <typename EntityList, Predicate<Tag> Pred> requires List<EntityList, Tag, Pred>
            struct CustomCategory : impl::CategoryBase<Tag>
            {
                using list_t = typename EntityList::template Type<Tag, Pred>;
                using predicate_t = Pred;
            };

            // A simple entity category, with a predicate checking for certain components.
            template <typename EntityList, Component<Tag> ...Comp> requires List<EntityList, Tag, HasComponents<Comp...>>
            using Category = CustomCategory<EntityList, HasComponents<Comp...>>;

            // This is used to modify a category type when it's passed to `Controller::get()` and others.
            // The input type doesn't have to be a category.
            template <typename T>
            struct PrepareCategoryType {using type = T;};

            // Each entity automatically inherits from this.
            // Mixins can add extra stuff there, but they should keep the class abstract.
            class Entity
            {
                friend Controller;
                typename Tag::entity_id_underlying_t entity_id = 0;

              public:
                Entity() {}

                Entity(const Entity &) = default;
                Entity(Entity &&) = default;
                Entity &operator=(const Entity &) = default;
                Entity &operator=(Entity &&) = default;

                // We use this class to delete entities.
                // We also want it to be abstract to prevent slicing.
                virtual ~Entity() = 0;

                // We provide a bunch of wrappers over `dynamic_cast`:

                // Returns true if the entity has a component.
                template <Component<Tag> Comp> [[nodiscard]] bool has() const {return dynamic_cast<const Comp *>(this);}

                // Returns a component or throws.
                template <Component<Tag> Comp> [[nodiscard]]       Comp &get()       {return dynamic_cast<      Comp &>(*this);}
                template <Component<Tag> Comp> [[nodiscard]] const Comp &get() const {return dynamic_cast<const Comp &>(*this);}

                // Returns a component or null.
                template <Component<Tag> Comp> [[nodiscard]]       Comp *get_opt()       {return dynamic_cast<      Comp *>(this);}
                template <Component<Tag> Comp> [[nodiscard]] const Comp *get_opt() const {return dynamic_cast<const Comp *>(this);}

                // The incremental id of this entity.
                [[nodiscard]] typename Tag::Id id() const
                {
                    typename Tag::Id ret;
                    ret.value = entity_id;
                    return ret;
                }

                // Mostly for internal use.
                // A list of category indices this entity belongs to.
                [[nodiscard]] virtual const std::vector<int> &EntityCategoryIndices() const = 0;
            };

            // A helper class that stores an id, and can be constructed either from an entity or from an id.
            struct EntityOrId
            {
                typename Tag::Id value;
                constexpr EntityOrId() {}
                constexpr EntityOrId(std::nullptr_t) {}
                constexpr EntityOrId(typename Tag::Id value) : value(std::move(value)) {}
                constexpr EntityOrId(Entity &e) : value(e.id()) {} // I don't think I need `typename Tag::Entity` here.
            };
            struct ConstEntityOrId
            {
                typename Tag::Id value;
                constexpr ConstEntityOrId() {}
                constexpr ConstEntityOrId(std::nullptr_t) {}
                constexpr ConstEntityOrId(typename Tag::Id value) : value(std::move(value)) {}
                constexpr ConstEntityOrId(const Entity &e) : value(e.id()) {} // I don't think I need `typename Tag::Entity` here.
            };

            // The final entity type that can be created.
            // Mostly for internal use.
            template <EntityType<Tag> E>
            struct FullEntity : Tag::Entity, E
            {
                using E::E;

                const std::vector<int> &EntityCategoryIndices() const override
                {
                    return EntityCategories<Tag, E>();
                }
            };

            // An entity controller.
            class Controller
            {
                struct State
                {
                    std::vector<std::unique_ptr<ListBase<Tag>>> lists;
                    typename Tag::entity_id_underlying_t id_counter = 1; // `0` is for null entity IDs.
                };
                State state;

              public:
                // Makes a null controller.
                constexpr Controller() {}

                // Makes a valid controller.
                // WARNING: This can't be called before entering `main()`.
                Controller(std::nullptr_t)
                {
                    ComponentRegistry<Tag>::Finalize();
                    CategoryRegistry<Tag>::Finalize();

                    if (CategoryRegistry<Tag>::Count() == 0)
                        throw std::runtime_error("Entity controller must have at least one category.");

                    state.lists.resize(CategoryRegistry<Tag>::Count());
                    for (int i = 0; i < CategoryRegistry<Tag>::Count(); i++)
                        state.lists[i] = CategoryRegistry<Tag>::Descriptions()[i].make_list();
                }

                Controller(Controller &&other) noexcept : state(std::exchange(other.state, {})) {}
                Controller &operator=(Controller other) noexcept
                {
                    std::swap(state, other.state);
                    return *this;
                }

                ~Controller()
                {
                    DestroyAllEntities();
                }

                // Returns true if this is a non-null controller.
                [[nodiscard]] explicit operator bool() const {return !state.lists.empty();}

                // Throw if this is a null controller.
                void ThrowIfNull() const
                {
                    if (!*this)
                        throw std::runtime_error("Attempt to use a null entity controller.");
                }

              protected:
                // Those are for the mixin authors.
                // They are called whenever an entity is created or destroyed.
                template <EntityType<Tag> E> void OnEntityCreated(typename Tag::template FullEntity<E> &e) {(void)e;}
                void OnEntityDestroyed(Entity &e) {(void)e;}

              public:
                // Create an entity in this controller.
                template <EntityType<Tag> E, typename ...P>
                requires std::constructible_from<typename Tag::template FullEntity<E>, P &&...>
                typename Tag::template FullEntity<E> &create(P &&... params)
                {
                    ThrowIfNull();
                    using full_entity_t = typename Tag::template FullEntity<E>;
                    static_assert(std::derived_from<full_entity_t, Entity>, "Do not inherit from `Entity` manually.");

                    const auto &categories = EntityCategories<Tag, E>();

                    // Make the entity.
                    full_entity_t *ret = new full_entity_t(std::forward<P>(params)...);

                    // Construct a guard.
                    std::size_t category_index = 0;
                    auto HandleException = [&]
                    {
                        while (category_index-- > 0)
                            state.lists[categories[category_index]]->Erase(*ret);
                        delete ret;
                    };
                    struct Guard
                    {
                        decltype(HandleException) *func = nullptr;
                        ~Guard() {if (func) (*func)();}
                    };
                    Guard guard{&HandleException};

                    // Assign a unique id.
                    // This has to be done before inserting to the lists, since they can use it.
                    // Note: not `typename Tag::Entity`, we don't want anybody to override the id member.
                    static_cast<Entity &>(*ret).entity_id = state.id_counter++;

                    // Insert the entity to lists. This part can throw.
                    for (; category_index < categories.size(); category_index++)
                        state.lists[categories[category_index]]->Insert(*ret);

                    // Run the user callback.
                    static_cast<typename Tag::Controller &>(*this).OnEntityCreated(*ret);

                    // Disarm the guard.
                    guard.func = nullptr;
                    return *ret;
                }

                // Destroy an entity in this controller.
                template <typename E>
                requires (EntityType<E, Tag> || Component<E, Tag> || std::same_as<E, typename Tag::Entity>) && std::is_polymorphic_v<E>
                void destroy(E &entity_or_component) noexcept
                {
                    ThrowIfNull();
                    typename Tag::Entity &entity = dynamic_cast<typename Tag::Entity &>(entity_or_component);
                    // Run the user callback.
                    static_cast<typename Tag::Controller &>(*this).OnEntityDestroyed(entity);
                    // Remove from lists.
                    const auto &categories = entity.EntityCategoryIndices();
                    for (int list_index : categories)
                        state.lists[list_index]->Erase(entity);
                    // Destroy the entity.
                    // `Entity` always has a virtual destructor, but a component might not have one.
                    delete &entity;
                }

                // Return an entity category.
                template <UnpreparedEntityCategory<Tag> Cat>
                [[nodiscard]] UnpreparedCategoryListType<Tag, Cat> &get()
                {
                    ThrowIfNull();
                    using PreparedCat = typename Tag::template PrepareCategoryType<Cat>::type;
                    return ListFriend::Cast<typename PreparedCat::list_t &>(*state.lists[CategoryRegistry<Tag>::template Type<PreparedCat>::index]);
                }
                template <UnpreparedEntityCategory<Tag> Cat>
                [[nodiscard]] const UnpreparedCategoryListType<Tag, Cat> &get() const
                {
                    return const_cast<Controller *>(this)->get<Cat>();
                }

                // Destroys all entities in the controller.
                void DestroyAllEntities()
                {
                    // Destroy entities from all lists.
                    // Since `EntityCategories()` errors on entities without categories, this doesn't leak.
                    for (const auto &list : state.lists)
                    {
                        while (typename Tag::Entity *entity = list->AnyEntity())
                            destroy(*entity);
                    }
                }
            };
        };

        template <typename Tag>
        DefaultTag<Tag>::Entity::~Entity() {}

        // Returns `M0<Derived,M1<Derived,...Mn<Derived,Base>>>`.
        // `Derived` is the assumed most-derived class in the chain. It's gived to each mixin as a template parameter.
        // Each mixin must inherit from its second template parameter. The last mixin receives `Base`.
        template <typename Derived, typename Base, template <typename, typename> typename ...M>
        struct CombineMixins
        {
            using type = Base;
        };

        template <typename Derived, typename Base, template <typename, typename> typename M0, template <typename, typename> typename ...M>
        struct CombineMixins<Derived, Base, M0, M...>
        {
            using NextBase = typename CombineMixins<Derived, Base, M...>::type;
            using type = M0<Derived, NextBase>;
            static_assert(std::derived_from<type, NextBase>, "Mixins must inherit from their second template parameters.");
        };
    }

    // A CRTP base for producing tag types.
    // `Mixins` is an optional list of mixins. Each receives `Tag` as the first template parameter,
    // and must inherit from the second parameter.
    template <typename Tag, template <typename/*Tag*/, typename/*NextBase*/> typename ...Mixins>
    struct BasicTag : impl::CombineMixins<Tag, impl::DefaultTag<Tag>, Mixins...>::type
    {
        // See `impl::DefaultTag` for the default members.
    };
}


// Component macros:

// Component classes must include this macro. `tag_` is the tag type.
// You can have several of those per entity, with different tags.
#define IMP_COMPONENT(tag_) IMP_COMPONENT_LOW(tag_, __COUNTER__, false, MA_CAT(_adl_imp_ComponentSelfType, __COUNTER__))
// A variation of `IMP_COMPONENT` that can be created directly as an entity, without inheriting from it.
#define IMP_STANDALONE_COMPONENT(tag_) IMP_COMPONENT_LOW(tag_, __COUNTER__, true, MA_CAT(_adl_imp_ComponentSelfType, __COUNTER__))

// A low-level version of `IMP_COMPONENT`.
// `is_standalone_` is a constexpr boolean, true if this component should be standalone.
// `self_type_name_` is the name for the "own type" typedef, that's otherwise unspecified.
#define IMP_COMPONENT_LOW(tag_, counter_, is_standalone_, self_type_name_) \
    /* The own type. */\
    IMP_DETECTABLE_BASE_SELFTYPE(::Ent::impl::ComponentBaseTag<tag_>, self_type_name_) \
    /* This is called to check if a type is a component. */\
    /* Note: I tried to use `same_as<self_type_name_> auto` instead of the classical SFINAE, but it caused redefinition errors on Clang 16. Not sure if it's a bug. */\
    template <typename MA_CAT(_entity_T,counter_), ::std::enable_if_t<::std::is_same_v<MA_CAT(_entity_T,counter_), self_type_name_>, ::std::nullptr_t> = nullptr> \
    constexpr friend bool _adl_imp_ComponentMarker(tag_, MA_CAT(_entity_T,counter_) *) {return is_standalone_;} \
    /* Friend the helper type. We don't currently use it, but the mixins might. */\
    template <typename MA_CAT(_entity_T,counter_)> friend struct ::Ent::impl::ComponentFriend;
