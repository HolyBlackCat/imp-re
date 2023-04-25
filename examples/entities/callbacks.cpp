struct Game : Ent::BasicTag<Game, Ent::Mixins::EntityCallbacks> {};

struct A
{
    IMP_COMPONENT(Game)

    // Make this non-empty.
    // Empty components and callbacks don't play well together, see `entities/mixin_entity_callbacks.h` for details.
    int a{};

    void _init(Game::Controller &con, Game::Entity &)
    {
        std::cout << "A::_init(this=" << this << ", con=" << &con << ")\n";
    }
    void _deinit(Game::Controller &con, Game::Entity &)
    {
        std::cout << "A::_deinit(this=" << this << ", con=" << &con << ")\n";
    }
};

struct B
{
    IMP_COMPONENT(Game)

    int b{};

    void _init(Game::Controller &con, Game::Entity &)
    {
        std::cout << "B::_init(this=" << this << ", con=" << &con << ")\n";
    }
    void _deinit(Game::Controller &con, Game::Entity &)
    {
        std::cout << "B::_deinit(this=" << this << ", con=" << &con << ")\n";
    }
};

struct AB : A, B
{
    IMP_STANDALONE_COMPONENT(Game)

    void _init(Game::Controller &con, Game::Entity &)
    {
        std::cout << "AB::_init(this=" << this << ", con=" << &con << ")\n";
    }
    void _deinit(Game::Controller &con, Game::Entity &)
    {
        std::cout << "AB::_deinit(this=" << this << ", con=" << &con << ")\n";
    }
};

using DummyCategory = Game::Category<Ent::UnorderedList, AB>;

IMP_MAIN(,)
{
    Game::Controller game = nullptr;

    std::cout << "Controller is at " << &game << '\n';

    {
        // Init order: A, B, AB.
        auto &ab = game.create<AB>();
        std::cout << "&ab = " << &ab << '\n';
        // Deinit order: AB, B, A.
        game.destroy(ab);
    }
    std::cout << "---\n";

    (void)game.get<DummyCategory>();
}
