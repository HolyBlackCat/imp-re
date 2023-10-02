#pragma once

#include "entities/core.h"
#include "entities/lists.h"

// This mixin provides a global entity list.
// It also provides extra methods for the controller to look up entities by id, using such a list.

namespace Ent
{
    namespace Mixins
    {
        template <typename Tag, typename NextBase>
        struct GlobalEntityLists : NextBase
        {
            // As usual, those are not instantiated unless touched.
            using AllEntitiesOrdered = typename NextBase::template Category<Ent::OrderedList>;
            using AllEntitiesUnordered = typename NextBase::template Category<Ent::UnorderedList>;

            struct Controller : NextBase::Controller
            {
                using NextBase::Controller::Controller;

                using NextBase::Controller::get;

                // Check an entity ID for validity.
                [[nodiscard]] bool valid(typename NextBase::Id id) const
                {
                    return this->template get<AllEntitiesUnordered>().has_entity_with_id(id);
                }

                // Get entity by ID, throw if invalid.
                [[nodiscard]]       typename Tag::Entity &get(typename NextBase::Id id)       {return this->template get<AllEntitiesUnordered>().entity_with_id(id);}
                [[nodiscard]] const typename Tag::Entity &get(typename NextBase::Id id) const {return this->template get<AllEntitiesUnordered>().entity_with_id(id);}

                // Get entity by ID, or null if invalid.
                [[nodiscard]]       typename Tag::Entity *get_opt(typename NextBase::Id id)       {return this->template get<AllEntitiesUnordered>().entity_with_id_opt(id);}
                [[nodiscard]] const typename Tag::Entity *get_opt(typename NextBase::Id id) const {return this->template get<AllEntitiesUnordered>().entity_with_id_opt(id);}
            };
        };
    }
}
