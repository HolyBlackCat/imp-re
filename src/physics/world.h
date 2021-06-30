#pragma once

#include <cstddef>
#include <memory>

#include "physics/common.h"

IMP_PHYSICS_BULLET_HEADERS_BEGIN
#include <bullet/btBulletDynamicsCommon.h>
IMP_PHYSICS_BULLET_HEADERS_END

#include "macros/enum_flag_operators.h"
#include "macros/maybe_const.h"
#include "meta/basic.h"

namespace Physics
{
    enum class WorldFlags
    {
        noDebugRenderer = 1 << 0,
    };
    IMP_ENUM_FLAG_OPERATORS(WorldFlags)

    class World
    {
      public:
        using collision_configuration_t = btDefaultCollisionConfiguration;
        using dispatcher_t              = btCollisionDispatcher;
        using broadphase_t              = btDbvtBroadphase;
        using constraint_solver_t       = btSequentialImpulseConstraintSolver;
        using world_t                   = btDiscreteDynamicsWorld;

      private:
        struct Data
        {
            collision_configuration_t collision_configuration;
            dispatcher_t dispatcher;
            broadphase_t broadphase;
            constraint_solver_t constraint_solver;
            world_t world;
            std::shared_ptr<btIDebugDraw> debug_renderer;

            Data()
                : dispatcher(&collision_configuration),
                world(&dispatcher, &broadphase, &constraint_solver, &collision_configuration)
            {}
        };
        std::unique_ptr<Data> data;

      public:
        constexpr World() {}

        World(std::nullptr_t)
            : data(std::make_unique<Data>())
        {}

        [[nodiscard]] explicit operator bool() const
        {
            return bool(data);
        }

        MAYBE_CONST(
            [[nodiscard]] CV collision_configuration_t &GetCollisionConfiguration() CV {return data->collision_configuration;}
            [[nodiscard]] CV dispatcher_t              &GetDispatcher            () CV {return data->dispatcher;             }
            [[nodiscard]] CV broadphase_t              &GetBroadphase            () CV {return data->broadphase;             }
            [[nodiscard]] CV constraint_solver_t       &GetConstraintSolver      () CV {return data->constraint_solver;      }
            [[nodiscard]] CV world_t                   &GetWorld                 () CV {return data->world;                  }
        )

        // Saves a pointer to the renderer, and passes it to `GetWorld().setDebugDrawer(...)`.
        // Passing null removes the renderer.
        void UseDebugRenderer(std::shared_ptr<btIDebugDraw> debug_renderer)
        {
            data->debug_renderer = std::move(debug_renderer);
            GetWorld().setDebugDrawer(data->debug_renderer.get());
        }

        // Calls `GetWorld().debugDrawWorld().`
        // This funciton exists because `debugDrawWorld` is not otherwise `const`.
        void DebugRender() const
        {
            data->world.debugDrawWorld();
        }


        void SetGravity(Vector3 v)
        {
            GetWorld().setGravity(btVector3(v));
        }

    };
}
