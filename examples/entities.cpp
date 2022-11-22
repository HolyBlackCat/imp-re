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
using WithPos       = Game::Category<Ent::OrderedList, Pos>;
using WithPosAndVel = Game::Category<Ent::UnorderedList, Pos, Vel>;
using TheMap        = Game::Category<Ent::SingleEntity, Map>;
using TheMapComp    = Game::Category<Ent::SingleComponent<Map>, Map>;

IMP_MAIN(,)
{
    Spike &spike1 = game.create<Spike>();
    spike1.pos = fvec2(10,100);

    Player &player1 = game.create<Player>();
    player1.pos = fvec2(20,200);
    player1.vel = fvec2(0.2f,0.02f);

    Player &player2 = game.create<Player>();
    player2.pos = fvec2(30,300);
    player2.vel = fvec2(0.3f,0.03f);

    game.create<Map>().map = 42;

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
    };

    std::cout << "--- Non-const:\n";
    test(game);
    std::cout << "--- Const:\n";
    test(std::as_const(game));

    return 0;
}
