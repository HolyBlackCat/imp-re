#include <concepts>
#include <iostream>
#include <iterator>

#include <entities/complete.h>

#include <doctest/doctest.h>

namespace
{
    struct Game : Ent::BasicTag<Game, Ent::Mixins::GlobalEntityLists, Ent::Mixins::EntityCallbacks, Ent::Mixins::EntityLinks> {};

    // Robust functons:

    template <Meta::ConstString Name>
    [[nodiscard]] bool HasLink(Game::Controller &game, auto &target)
    {
        bool ret = game.has_link<Name>(target);

        constexpr bool is_single_target = Game::valid_single_target_link_owner<std::remove_cvref_t<decltype(target)>, Name>;
        constexpr bool is_multi_target = Game::valid_multi_target_link_owner<std::remove_cvref_t<decltype(target)>, Name>;
        static_assert(is_single_target != is_multi_target);

        auto Check = [&](auto &game)
        {
            // A bit out of place, but why not check it here.
            REQUIRE_FALSE(game.template has_link_to<Name>(target, nullptr));
            REQUIRE_FALSE(game.has_link_to(Name.view(), target, nullptr));

            // For multi-target links, clamps the number to [0;1].
            // For single-target links, leaves it unchanged.
            auto ClampNumTargets = [&](std::size_t value) -> std::size_t
            {
                if constexpr (is_multi_target)
                    return value > 0 ? 1 : 0;
                else
                    return value;
            };

            REQUIRE(ret == game.template has_link<Name>(target));
            REQUIRE(ret == game.has_link(Name.view(), target));
            REQUIRE(ret == ClampNumTargets(game.template num_link_targets<Name>(target)));
            REQUIRE(ret == ClampNumTargets(game.num_link_targets(Name.view(), target)));

            if constexpr (is_single_target)
            {
                if (ret)
                {
                    REQUIRE_NOTHROW((void)game.template get_link<Name>(target));
                    REQUIRE_NOTHROW((void)game.get_link(Name.view(), target));
                    REQUIRE(game.template get_link_opt<Name>(target) != nullptr);
                    REQUIRE(game.get_link_opt(Name.view(), target) != nullptr);
                    REQUIRE(game.template get_link_low_opt<Name>(target).target_id.is_nonzero());
                    REQUIRE(game.template get_link_low_opt<Name>(target).target_link_name.empty() == false);
                    REQUIRE(game.get_link_low_opt(Name.view(), target).target_id.is_nonzero());
                    REQUIRE(game.get_link_low_opt(Name.view(), target).target_link_name.empty() == false);
                }
                else
                {
                    REQUIRE_THROWS_WITH((void)game.template get_link<Name>(target), doctest::Contains("link is null"));
                    REQUIRE_THROWS_WITH((void)game.get_link(Name.view(), target), doctest::Contains("link is null"));
                    REQUIRE(game.template get_link_opt<Name>(target) == nullptr);
                    REQUIRE(game.get_link_opt(Name.view(), target) == nullptr);
                    REQUIRE(game.template get_link_low_opt<Name>(target).target_id.is_nonzero() == false);
                    REQUIRE(game.template get_link_low_opt<Name>(target).target_link_name.empty());
                    REQUIRE(game.get_link_low_opt(Name.view(), target).target_id.is_nonzero() == false);
                    REQUIRE(game.get_link_low_opt(Name.view(), target).target_link_name.empty());
                }
            }
            else
            {
                auto size = game.template get_links<Name>(target).size();
                REQUIRE_EQ(ret, size > 0);
                REQUIRE(std::distance(game.template get_links<Name>(target).begin(), game.template get_links<Name>(target).end()) == size);
                REQUIRE_NE(ret, game.template get_links<Name>(target).empty());
                REQUIRE_EQ(ret, game.template get_links<Name>(target).non_empty());
            }
        };
        Check(game);
        Check(std::as_const(game));

        return ret;
    }

    template <Meta::ConstString OuterName, Meta::ConstString OuterOtherName>
    [[nodiscard]] bool AreLinked(Game::Controller &game, auto &a, auto &b)
    {
        // Check whether the link exists.
        bool ret = game.has_link_to<OuterName>(a, b);
        // Make sure the specified link names are used, instead of some other links
        if constexpr (Game::valid_single_target_link_owner<std::remove_cvref_t<decltype(a)>, OuterName>)
        {
            if (game.get_link_low_opt<OuterName>(a).target_link_name != OuterOtherName.view())
                ret = false;
        }
        else
        {
            if (game.get_links<OuterName>(a).find(b.id()).target_link_name() != OuterOtherName.view())
                ret = false;
        }

        auto CheckDirection = [ret]<Meta::ConstString NameA, Meta::ConstString NameB>(Game::Controller &mutable_game, Meta::ConstStringTag<NameA>, Meta::ConstStringTag<NameB>, auto &a, auto &b)
        {
            bool has_link = HasLink<NameA>(mutable_game, a);
            REQUIRE(ret <= has_link);

            auto CheckControllerConstness = [&](auto &game)
            {
                if constexpr (Game::valid_single_target_link_owner<std::remove_cvref_t<decltype(a)>, NameA>)
                {
                    // If `has_link == false`, in `HasLink()` we check that those functions throw.
                    // So we don't need to do it here.
                    if (has_link)
                    {
                        REQUIRE(&game.template get_link<NameA>(a) == &b);
                        REQUIRE(&game.get_link(NameA.view(), a) == &b);
                    }

                    [[maybe_unused]] bool name_matches = game.template get_link_low_opt<NameA>(a).target_link_name == NameB.view();
                    REQUIRE_EQ(name_matches, game.get_link_low_opt(NameA.view(), a).target_link_name == NameB.view());

                    REQUIRE_EQ(ret, game.template get_link_opt<NameA>(a) == &b && name_matches);
                    REQUIRE_EQ(ret, game.get_link_opt(NameA.view(), a) == &b && name_matches);
                    REQUIRE_EQ(ret, game.template get_link_low_opt<NameA>(a).target_id == b.id() && name_matches);
                    REQUIRE_EQ(ret, game.get_link_low_opt(NameA.view(), a).target_id == b.id() && name_matches);
                }

                [[maybe_unused]] bool has_link_result = game.template has_link_to<NameA>(a, b);
                REQUIRE_EQ(has_link_result, game.has_link_to(NameA.view(), a, b));

                [[maybe_unused]] bool has_named_link_result = game.template has_link_to<NameA, NameB>(a, b);
                REQUIRE_EQ(has_named_link_result, game.template has_link_to<NameA>(a, NameB.view(), b));
                REQUIRE_EQ(has_named_link_result, game.has_link_to(NameA.view(), a, NameB.view(), b));

                REQUIRE_EQ(ret, has_named_link_result);
                REQUIRE(has_named_link_result <= has_link_result);
            };

            CheckControllerConstness(mutable_game);
            CheckControllerConstness(std::as_const(mutable_game));
        };

        CheckDirection(game, Meta::ConstStringTag<OuterName>{}, Meta::ConstStringTag<OuterOtherName>{}, a, b);
        CheckDirection(game, Meta::ConstStringTag<OuterOtherName>{}, Meta::ConstStringTag<OuterName>{}, b, a);

        return ret;
    }

    // Multi-variant functions:

    enum class AttachKind
    {
        static_direct, // Non-type-erased overload.
        static_inverse,
        halfstatic_direct, // Half-type-erased overload.
        halfstatic_inverse,
        dynamic_direct, // Fully type-erased overload.
        dynamic_inverse,
        _count [[maybe_unused]],
    };
    template <Meta::ConstString NameA, Meta::ConstString NameB>
    void Link(Game::Controller &game, AttachKind kind, auto &x, auto &y)
    {
        switch (kind)
        {
          case AttachKind::static_direct:
            game.link<NameA, NameB>(x, y);
            break;
          case AttachKind::static_inverse:
            game.link<NameB, NameA>(y, x);
            break;
          case AttachKind::halfstatic_direct:
            game.link<NameA>(x, std::string(NameB.view()), y.id());
            break;
          case AttachKind::halfstatic_inverse:
            game.link<NameB>(y, std::string(NameA.view()), x.id());
            break;
          case AttachKind::dynamic_direct:
            game.link(NameA.view(), x, std::string(NameB.view()), y.id());
            break;
          case AttachKind::dynamic_inverse:
            game.link(NameB.view(), y, std::string(NameA.view()), x.id());
            break;
        }
    }

    // Test classes:

    struct A : Game::LinkOne<"a">
    {
        IMP_COMPONENT(Game)
    };

    struct B : Game::LinkOne<"b">
    {
        IMP_COMPONENT(Game)
    };

    struct X : A, B
    {
        virtual ~X() = default;
    };

    struct Y : Game::LinkOne<"y">
    {
        IMP_STANDALONE_COMPONENT(Game)
        virtual ~Y() = default;
    };

    struct Z : Game::LinkMany<"z">
    {
        IMP_STANDALONE_COMPONENT(Game)
        virtual ~Z() = default;
    };
    struct W : Game::LinkMany<"w1">, Game::LinkMany<"w2">
    {
        IMP_STANDALONE_COMPONENT(Game)
    };
}

TEST_CASE("entities.links.single")
{
    SUBCASE("common")
    {
        enum class DetachKind
        {
            static_direct, // Non-type-erased overload.
            static_inverse,
            static_id_direct, // Non-type-erased overload with target ID specified.
            static_id_inverse,
            dynamic_direct, // Type-erased overload.
            dynamic_inverse,
            dynamic_id_direct, // Type-erased overload with target ID specified.
            dynamic_id_inverse,
            destroy_first,
            destroy_second,
            attach_to_other_direct, // Detaching by attaching a different object.
            attach_to_other_inverse,
            attach_to_other_with_other_link_direct, // Attaching a differnet object to a different link, without detaching anything.
            attach_to_other_with_other_link_inverse,
            attach_with_different_link_direct, // Attaching the same object using a different link, detaching the original link.
            attach_with_different_link_inverse,
            _count [[maybe_unused]],
        };

        for (AttachKind attach_kind{}; attach_kind != AttachKind::_count; attach_kind = AttachKind(std::to_underlying(attach_kind) + 1))
        for (DetachKind detach_kind{}; detach_kind != DetachKind::_count; detach_kind = DetachKind(std::to_underlying(detach_kind) + 1))
        {
            CAPTURE(attach_kind);
            CAPTURE(detach_kind);

            Game::Controller game = nullptr;

            auto &x = game.create<X>();
            auto &y = game.create<Y>();
            auto &other = game.create<Y>();

            // Check invalid link name handling in the dynamic overloads.
            REQUIRE_THROWS_WITH(game.link("invalid", x, "invalid", y), "Unknown link name.");

            auto RequireNotLinked = [&]
            {
                REQUIRE(!HasLink<"a">(game, x));
                REQUIRE(!HasLink<"b">(game, x));
                REQUIRE(!HasLink<"y">(game, y));
                REQUIRE(!HasLink<"y">(game, other));
                REQUIRE(!AreLinked<"a", "y">(game, x, y));
            };

            auto RequireLinked = [&]
            {
                REQUIRE(!HasLink<"b">(game, x));
                REQUIRE(!HasLink<"y">(game, other));
                REQUIRE(AreLinked<"a", "y">(game, x, y));
            };

            auto RequireLinkedWithDifferentLink = [&]
            {
                REQUIRE(!HasLink<"a">(game, x));
                REQUIRE(!HasLink<"y">(game, other));
                REQUIRE(AreLinked<"b", "y">(game, x, y));
            };

            auto RequireLinkedWithOther = [&]
            {
                REQUIRE(!HasLink<"b">(game, x));
                REQUIRE(!HasLink<"y">(game, y));
                REQUIRE(AreLinked<"a", "y">(game, x, other));
            };

            auto RequireLinkedWithTwoLinks = [&]
            {
                REQUIRE(AreLinked<"a", "y">(game, x, y));
                REQUIRE(AreLinked<"b", "y">(game, x, other));
            };

            RequireNotLinked();

            Link<"a", "y">(game, attach_kind, x, y);

            RequireLinked();

            switch (detach_kind)
            {
              case DetachKind::static_direct:
                game.unlink<"a">(x);
                RequireNotLinked();
                break;
              case DetachKind::static_inverse:
                game.unlink<"y">(y);
                RequireNotLinked();
                break;
              case DetachKind::static_id_direct:
                REQUIRE_THROWS_WITH(game.unlink<"a">(x, other.id()), doctest::Contains("isn't linked"));
                RequireLinked();
                game.unlink<"a">(x, Game::Id{});
                RequireLinked();
                game.unlink<"a">(x);
                RequireNotLinked();
                break;
              case DetachKind::static_id_inverse:
                REQUIRE_THROWS_WITH(game.unlink<"y">(y, other.id()), doctest::Contains("isn't linked"));
                RequireLinked();
                game.unlink<"y">(y, Game::Id{});
                RequireLinked();
                game.unlink<"y">(y);
                RequireNotLinked();
                break;
              case DetachKind::dynamic_direct:
                game.unlink("a", x);
                RequireNotLinked();
                break;
              case DetachKind::dynamic_inverse:
                game.unlink("y", y);
                RequireNotLinked();
                break;
              case DetachKind::dynamic_id_direct:
                REQUIRE_THROWS_WITH(game.unlink("a", x, other.id()), doctest::Contains("isn't linked"));
                RequireLinked();
                game.unlink("a", x, Game::Id{});
                RequireLinked();
                game.unlink("a", x);
                RequireNotLinked();
                break;
              case DetachKind::dynamic_id_inverse:
                REQUIRE_THROWS_WITH(game.unlink("y", y, other.id()), doctest::Contains("isn't linked"));
                RequireLinked();
                game.unlink("y", y, Game::Id{});
                RequireLinked();
                game.unlink("y", y);
                RequireNotLinked();
                break;
              case DetachKind::destroy_first:
                game.destroy(x);
                REQUIRE(!HasLink<"y">(game, y));
                break;
              case DetachKind::destroy_second:
                game.destroy(y);
                REQUIRE(!HasLink<"a">(game, x));
                REQUIRE(!HasLink<"b">(game, x));
                break;
              case DetachKind::attach_to_other_direct:
                game.link<"a", "y">(x, other);
                RequireLinkedWithOther();
                break;
              case DetachKind::attach_to_other_inverse:
                game.link<"y", "a">(other, x);
                RequireLinkedWithOther();
                break;
              case DetachKind::attach_to_other_with_other_link_direct:
                game.link<"b", "y">(x, other);
                RequireLinkedWithTwoLinks();
                break;
              case DetachKind::attach_to_other_with_other_link_inverse:
                game.link<"y", "b">(other, x);
                RequireLinkedWithTwoLinks();
                break;
              case DetachKind::attach_with_different_link_direct:
                game.link<"b", "y">(x, y);
                RequireLinkedWithDifferentLink();
                break;
              case DetachKind::attach_with_different_link_inverse:
                game.link<"y", "b">(y, x);
                RequireLinkedWithDifferentLink();
                break;
            }
        }
    }

    // Linking two objects twice, using different links in each.
    SUBCASE("dual_link")
    {
        Game::Controller game = nullptr;

        auto &f = game.create<X>();
        auto &g = game.create<X>();

        game.link<"a", "b">(f, g);
        game.link<"b", "a">(f, g);
        REQUIRE(AreLinked<"a", "b">(game, f, g));
        REQUIRE(AreLinked<"b", "a">(game, f, g));
        REQUIRE(!AreLinked<"a", "a">(game, f, g));
        REQUIRE(!AreLinked<"b", "b">(game, f, g));
    }
}

TEST_CASE("entities.links.multi")
{
    SUBCASE("common")
    {
        enum class DetachKind
        {
            static_direct, // Non-type-erased overload.
            static_inverse,
            static_id_direct, // Non-type-erased overload with target ID specified.
            static_id_inverse,
            dynamic_direct, // Type-erased overload.
            dynamic_inverse,
            dynamic_id_direct, // Type-erased overload with target ID specified.
            dynamic_id_inverse,
            single_attach_other, // Detach the single link by binding a different object to it.
            multi_attach_other_static_direct, // Attach another object to the multi-target link, in different ways.
            multi_attach_other_static_inverse,
            multi_attach_other_halfstatic_direct,
            multi_attach_other_halfstatic_inverse,
            multi_attach_other_dynamic_direct,
            multi_attach_other_dynamic_inverse,
            _count [[maybe_unused]],
        };

        for (AttachKind attach_kind{}; attach_kind != AttachKind::_count; attach_kind = AttachKind(std::to_underlying(attach_kind) + 1))
        for (DetachKind detach_kind{}; detach_kind != DetachKind::_count; detach_kind = DetachKind(std::to_underlying(detach_kind) + 1))
        {
            CAPTURE(attach_kind);
            CAPTURE(detach_kind);

            Game::Controller game = nullptr;

            auto &x = game.create<X>();
            auto &z = game.create<Z>();
            auto &other = game.create<Z>();
            auto &other_single = game.create<X>();

            static_assert(std::contiguous_iterator<std::remove_cvref_t<decltype(game.get_links<"z">(z).begin())>>);

            auto RequireNotLinked = [&]
            {
                REQUIRE(!HasLink<"a">(game, x));
                REQUIRE(!HasLink<"b">(game, x));
                REQUIRE(!HasLink<"a">(game, other_single));
                REQUIRE(!HasLink<"b">(game, other_single));
                REQUIRE(!HasLink<"z">(game, z));
                REQUIRE(!HasLink<"z">(game, other));
            };
            auto RequireLinked = [&]
            {
                REQUIRE(!HasLink<"b">(game, x));
                REQUIRE(!HasLink<"z">(game, other));
                REQUIRE(!HasLink<"a">(game, other_single));
                REQUIRE(!HasLink<"b">(game, other_single));
                REQUIRE(AreLinked<"a", "z">(game, x, z));
            };
            auto RequireLinkedToOther = [&]
            {
                REQUIRE(!HasLink<"b">(game, x));
                REQUIRE(!HasLink<"z">(game, z));
                REQUIRE(!HasLink<"a">(game, other_single));
                REQUIRE(!HasLink<"b">(game, other_single));
                REQUIRE(AreLinked<"a", "z">(game, x, other));
            };
            auto RequireMutliAlsoLinkedToOther = [&]
            {
                REQUIRE(!HasLink<"b">(game, x));
                REQUIRE(!HasLink<"b">(game, other_single));
                REQUIRE(AreLinked<"z", "a">(game, z, x));
                REQUIRE(AreLinked<"z", "a">(game, z, other_single));
                REQUIRE(game.get_links<"z">(z).size() == 2);
                REQUIRE(game.get_links<"z">(z)[0].id() == x.id());
                REQUIRE(game.get_links<"z">(z)[1].id() == other_single.id());
                REQUIRE(&game.get_link<"a">(other_single) == &z);
                REQUIRE(!game.has_link<"b">(other_single));
            };

            RequireNotLinked();

            Link<"a", "z">(game, attach_kind, x, z);

            RequireLinked();

            switch (detach_kind)
            {
              case DetachKind::static_direct:
                game.unlink<"a">(x);
                RequireNotLinked();
                break;
              case DetachKind::static_inverse:
                game.unlink<"z">(z);
                RequireNotLinked();
                break;
              case DetachKind::static_id_direct:
                REQUIRE_THROWS_WITH(game.unlink<"a">(x, other.id()), doctest::Contains("isn't linked"));
                RequireLinked();
                game.unlink<"a">(x, Game::Id{});
                RequireLinked();
                game.unlink<"a">(x);
                RequireNotLinked();
                break;
              case DetachKind::static_id_inverse:
                REQUIRE_THROWS_WITH(game.unlink<"z">(z, other.id()), doctest::Contains("isn't linked"));
                RequireLinked();
                game.unlink<"z">(z, Game::Id{});
                RequireLinked();
                game.unlink<"z">(z);
                RequireNotLinked();
                break;
              case DetachKind::dynamic_direct:
                game.unlink("a", x);
                RequireNotLinked();
                break;
              case DetachKind::dynamic_inverse:
                game.unlink("z", z);
                RequireNotLinked();
                break;
              case DetachKind::dynamic_id_direct:
                REQUIRE_THROWS_WITH(game.unlink("a", x, other.id()), doctest::Contains("isn't linked"));
                RequireLinked();
                game.unlink("a", x, Game::Id{});
                RequireLinked();
                game.unlink("a", x);
                RequireNotLinked();
                break;
              case DetachKind::dynamic_id_inverse:
                REQUIRE_THROWS_WITH(game.unlink("z", z, other.id()), doctest::Contains("isn't linked"));
                RequireLinked();
                game.unlink("z", z, Game::Id{});
                RequireLinked();
                game.unlink("z", z);
                RequireNotLinked();
                break;
              case DetachKind::single_attach_other:
                game.link<"a", "z">(x, other);
                RequireLinkedToOther();
                break;
              case DetachKind::multi_attach_other_static_direct:
                game.link<"z", "a">(z, other_single);
                RequireMutliAlsoLinkedToOther();
                break;
              case DetachKind::multi_attach_other_static_inverse:
                game.link<"a", "z">(other_single, z);
                RequireMutliAlsoLinkedToOther();
                break;
              case DetachKind::multi_attach_other_halfstatic_direct:
                game.link<"z">(z, "a", other_single);
                RequireMutliAlsoLinkedToOther();
                break;
              case DetachKind::multi_attach_other_halfstatic_inverse:
                game.link<"a">(other_single, "z", z);
                RequireMutliAlsoLinkedToOther();
                break;
              case DetachKind::multi_attach_other_dynamic_direct:
                game.link("z", z, "a", other_single);
                RequireMutliAlsoLinkedToOther();
                break;
              case DetachKind::multi_attach_other_dynamic_inverse:
                game.link("a", other_single, "z", z);
                RequireMutliAlsoLinkedToOther();
                break;
            }
        }
    }

    SUBCASE("repeated_attach")
    {
        Game::Controller game = nullptr;

        auto &a = game.create<X>();
        auto &b = game.create<W>();
        auto &w = game.create<W>();

        game.link<"w1", "a">(w, a);
        REQUIRE(game.get_links<"w1">(w).size() == 1);
        REQUIRE(game.get_links<"w1">(w)[0].id() == a.id());

        game.link<"w1", "w1">(w, b);
        REQUIRE(game.get_links<"w1">(w).size() == 2);
        REQUIRE(game.get_links<"w1">(w)[0].id() == a.id());
        REQUIRE(game.get_links<"w1">(w)[1].id() == b.id());

        // Binding again moves target to the end of the list.
        game.link<"w1", "a">(w, a);
        REQUIRE(game.get_links<"w1">(w).size() == 2);
        REQUIRE(game.get_links<"w1">(w)[0].id() == b.id());
        REQUIRE(game.get_links<"w1">(w)[1].id() == a.id());

        // Binding again using a different slot of target unbinds the original.
        game.link<"w1", "w2">(w, b);
        REQUIRE(game.get_links<"w1">(w).size() == 2);
        REQUIRE(game.get_links<"w1">(w)[0].id() == a.id());
        REQUIRE(game.get_links<"w1">(w)[1].id() == b.id());
        REQUIRE(&game.get_link<"a">(a) == &w);
        REQUIRE(game.get_links<"w2">(b).size() == 1);
        REQUIRE(game.get_links<"w2">(b)[0].id() == w.id());
    }
}

// Signature checks.
namespace
{
    using C = Game::Controller;
    using E = Game::Entity;
    using EI = Game::EntityOrId;
    using CEI = Game::ConstEntityOrId;
    using DefaultLinkContainer = Game::LinkContainer<Game::NoLinkData, std::vector<Game::LinkElemLow<Game::NoLinkData>>, Game::LinkContainerTraits<std::vector<Game::LinkElemLow<Game::NoLinkData>>>>;

    static_assert(requires{static_cast<bool (*)(const X &                  )>(&C::has_link<"a">);});
    static_assert(requires{static_cast<bool (*)(std::string_view, const E &)>(&C::has_link     );});

    static_assert(requires{static_cast<std::size_t (*)(const X &                  )>(&C::num_link_targets<"a">);});
    static_assert(requires{static_cast<std::size_t (*)(std::string_view, const E &)>(&C::num_link_targets     );});

    static_assert(requires{static_cast<      E &(C::*)(      X &                  )      >(&C::get_link<"a">);});
    static_assert(requires{static_cast<const E &(C::*)(const X &                  ) const>(&C::get_link<"a">);});
    static_assert(requires{static_cast<      E &(C::*)(std::string_view,       E &)      >(&C::get_link     );});
    static_assert(requires{static_cast<const E &(C::*)(std::string_view, const E &) const>(&C::get_link     );});

    static_assert(requires{static_cast<      E *(C::*)(      X &                  )      >(&C::get_link_opt<"a">);});
    static_assert(requires{static_cast<const E *(C::*)(const X &                  ) const>(&C::get_link_opt<"a">);});
    static_assert(requires{static_cast<      E *(C::*)(std::string_view,       E &)      >(&C::get_link_opt     );});
    static_assert(requires{static_cast<const E *(C::*)(std::string_view, const E &) const>(&C::get_link_opt     );});

    static_assert(requires{static_cast<const Game::LinkTargetIdWithName &(*)(const X &                  )>(&C::get_link_low_opt<"a">);});
    static_assert(requires{static_cast<const Game::LinkTargetIdWithName &(*)(std::string_view, const E &)>(&C::get_link_low_opt     );});

    static_assert(requires{static_cast<      DefaultLinkContainer &(*)(      Z &)>(&C::get_links<"z">);});
    static_assert(requires{static_cast<const DefaultLinkContainer &(*)(const Z &)>(&C::get_links<"z">);});

    static_assert(requires{static_cast<bool (*)(const X &                  , CEI)>(&C::has_link_to<"a">);});
    static_assert(requires{static_cast<bool (*)(std::string_view, const E &, CEI)>(&C::has_link_to     );});

    static_assert(requires{static_cast<bool (*)(const X &                                    , const Y &)>(&C::has_link_to<"a", "y">);});
    static_assert(requires{static_cast<bool (*)(const X &                  , std::string_view, CEI      )>(&C::has_link_to<"a"     >);});
    static_assert(requires{static_cast<bool (*)(std::string_view, const E &, std::string_view, CEI      )>(&C::has_link_to          );});

    static_assert(requires{static_cast<void (C::*)(X &                  , Y &            )>(&C::link<"a", "y">);});
    static_assert(requires{static_cast<void (C::*)(X &                  , std::string, EI)>(&C::link<"a"     >);});
    static_assert(requires{static_cast<void (C::*)(std::string_view, E &, std::string, EI)>(&C::link          );});

    static_assert(requires{static_cast<void (C::*)(X &                  )>(&C::unlink<"a">);});
    static_assert(requires{static_cast<void (C::*)(std::string_view, E &)>(&C::unlink     );});

    static_assert(requires{static_cast<void (C::*)(X &                  , EI)>(&C::unlink<"a">);});
    static_assert(requires{static_cast<void (C::*)(std::string_view, E &, EI)>(&C::unlink     );});
}
