#include "entities/complete.h"

// A showcase of the entity system.
// Some parts look ugly because they're in a template lambda and need `.template` everywhere, but this way we get more test coverage.
// It's recommended to run this with ASAN when developing, just in case.

struct Game : Ent::BasicTag<Game,
    Ent::Mixins::ComponentsAsCategories // Allow `controller.get<T>()` to accept component types directly.
> {};

struct Pos
{
    IMP_COMPONENT(Game)
    fvec2 pos = fvec2(1,2);
};

struct Vel
{
    IMP_COMPONENT(Game)
    fvec2 vel = fvec2(3,4);
};

struct Spike : Pos {};
struct Player : Pos, Vel {};

struct Map
{
    IMP_STANDALONE_COMPONENT(Game)
    int map = 42;
};

Game::Controller game = nullptr;

// Entity categories.
using WithPos          = Game::Category<Ent::OrderedList, Pos>;
using WithPosUnordered = Game::Category<Ent::UnorderedList, Pos>;
using WithPosAndVel    = Game::Category<Ent::UnorderedList, Pos, Vel>;
using TheMap           = Game::Category<Ent::SingleEntity, Map>;
using TheMapComp       = Game::Category<Ent::SingleComponent<Map>, Map>;

IMP_MAIN(,)
{
    auto spike1 = game.create<Spike>();
    auto [spike1_id, spike1_ref] = spike1; // Check that structured bindings work.
    spike1_ref.pos = fvec2(10,100);

    Player &player1 = game.create<Player>().ref;
    player1.pos = fvec2(20,200);
    player1.vel = fvec2(0.2f,0.02f);

    Player &player2 = game.create<Player>().ref;
    player2.pos = fvec2(30,300);
    player2.vel = fvec2(0.3f,0.03f);

    game.create<Map>().ref.map = 42;

    auto test = [&](auto &game)
    {
        std::cout << "With pos:\n";
        for (auto &e : game.template get<WithPos>().at_least_one())
        {
            static_assert(std::is_const_v<std::remove_reference_t<decltype(e)>> == std::is_const_v<std::remove_reference_t<decltype(game)>>);
            std::cout << "pos=" << e.template get<Pos>().pos;
            if (auto *vel = e.template get_opt<Vel>())
            {
                static_assert(std::is_const_v<std::remove_reference_t<decltype(*vel)>> == std::is_const_v<std::remove_reference_t<decltype(game)>>);
                std::cout << " vel=" << vel->vel;
            }
            std::cout << '\n';
        }

        std::cout << "With vel:\n";
        for (auto &e : game.template get<WithPosAndVel>())
        {
            static_assert(std::is_const_v<std::remove_reference_t<decltype(e)>> == std::is_const_v<std::remove_reference_t<decltype(game)>>);
            std::cout << "pos=" << e.template get<Pos>().pos << " vel=" << e.template get<Vel>().vel << '\n';
        }

        try
        {
            (void)game.template get<WithPos>().single().template get<Pos>().pos;
        }
        catch (...) {}

        try
        {
            if (auto *e = game.template get<WithPos>().single_opt())
            {
                (void)e->template get<Pos>().pos;
                static_assert(std::is_const_v<std::remove_reference_t<decltype(*e)>> == std::is_const_v<std::remove_reference_t<decltype(game)>>);
            }
        }
        catch (...) {}

        std::cout << "The map:\n";
        // Variant 0.
        // `Ent::Mixins::ComponentsAsCategories` lets us do this.
        std::cout << game.template get<Map>()->map << '\n';
        // Variant 1.
        std::cout << game.template get<TheMapComp>()->map << '\n';
        // Variant 2.
        std::cout << game.template get<TheMap>()->template get<Map>().map << '\n';

        // By id.
        auto ByIdTest = [&](auto &list)
        {
            assert(Game::Id{}.get_value() == 0);
            assert(spike1.id.get_value() == 1);
            assert(list.entity_with_id_opt(spike1.id)->template get<Pos>().pos == fvec2(10,100));
            assert(list.entity_with_id_opt(Game::Id{}) == nullptr);
            assert(list.entity_with_id(spike1.id).template get<Pos>().pos == fvec2(10,100));
            try {(void)list.entity_with_id(Game::Id{});} catch (...) {}
            assert(game.template get<WithPosAndVel>().entity_with_id_opt(spike1.id) == nullptr); // Not in this list.
        };
        ByIdTest(game.template get<WithPos>());
        ByIdTest(game.template get<WithPosUnordered>());
    };

    std::cout << "--- Non-const:\n";
    test(game);
    std::cout << "--- Const:\n";
    test(std::as_const(game));

    return 0;
}
