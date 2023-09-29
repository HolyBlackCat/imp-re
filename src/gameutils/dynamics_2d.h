#pragma once

#include "utils/mat.h"

// Classes to deal with forces and movement in 2D.

namespace Dynamics2d
{
    template <typename T>
    struct BodyMass
    {
        using type = T;

        T inv_mass = 1;
        T inv_angular_mass = 1;

        void SetMass(T mass)
        {
            inv_mass = mass <= 0 ? 0 : 1 / mass;
        }
        void SetAngularMass(T angular_mass)
        {
            inv_angular_mass = angular_mass <= 0 ? 0 : 1 / angular_mass;
        }

        // Converts force vector at a specific point to torque.
        [[nodiscard]] static T ForceToTorque(vec2<T> point, vec2<T> force)
        {
            return point /cross/ force;
        }

        // Given a point and the desired acceleration at that point, returns the force vector that needs to be applied to that point.
        // Can handle `inv_angular_mass` being zero, but CAN'T handle `inv_mass` being zero.
        // If you have two objects you want to stop relative to one another, you should halve the result you get for each object.
        [[nodiscard]] vec2<T> GetRequiredForce(vec2<T> point, vec2<T> acc) const
        {
            // This formula is obtained from Mathics3 by solving `ApplyForce(point, force) == acc` to get `force`,
            // then manually simplifying the resulting formula in terms of vector operations.
            //     p={px,py}; f={fx,fy}; v={vx,vy}
            //     sol = Solve[Thread[Normalize[Cross[p]]*(px*fy-py*fx)*InvAngMass+f*InvMass==v],f]
            //     Simplify[sol] /. x_*y_+x_*z_->x*(y+z)
            //     (* Now try to rewrite it manually *)
            //     manual = (v+InvAngMass*p*Total[p*v]/InvMass)/(InvMass+InvAngMass*(p.p))
            //     Simplify[manual]
            //     (* Compare the results by eye to make sure they are the same *)
            return (acc + inv_angular_mass * (point /dot/ acc) / inv_mass * point) / (inv_mass + inv_angular_mass * point.len_sq());
        }

        // Given a point, a force direction, and the desired acceleration in a specific direction (acceleration projected on `acc_dir` will be `acc_value`),
        // returns the scalar force value in that direction, needed to create that acceleration.
        // `force_dir` doesn't need to be normalized, the return value is a factor for it.
        // Can return a negative value.
        // Ensures `acceleration /dot/ acc_dir == acc_value`, hence `acc_dir` should usually be normalized,
        //     but it don't have to be, if you scale `acc_value` by its length.
        // If you have two objects you want to stop relative to one another, you should halve the result you get for each object.
        [[nodiscard]] T GetRequiredForceInDirection(vec2<T> point, vec2<T> force_dir, vec2<T> acc_dir, T acc_value) const
        {
            // Inferred in Mathics3:
            //     p={px,py}; fd={fdx,fdy}; f={fx,fy}=fd*force; n={nx,ny}
            //     sol = Solve[Thread[(Normalize[Cross[p]]*(px*fy-py*fx)*InvAngMass+f*InvMass).n==speed],force]
            //     (* Manually rewriting the denominator into a vector form *)
            //     Expand[((InvAngMass*(px*fdy-py*fdx)*(px*ny-py*nx)+InvMass*Total[fd*n]))]
            return acc_value / (inv_angular_mass * (point /cross/ force_dir) * (point /cross/ acc_dir) + inv_mass * (force_dir /dot/ acc_dir));
        }
    };

    template <typename T>
    struct BodyVelocity
    {
        vec2<T> vel;
        T angular_vel = 0;

        // Returns the velocity at `point`, everything in local coordinates.
        [[nodiscard]] vec2<T> GetVelocityAtPoint(vec2<T> point) const
        {
            return vel + point.rot90() * angular_vel;
        }

        // Applies force as if to the center of mass, without the rotational component.
        void ApplyForceToCenterOfMass(const BodyMass<T> &body_mass, vec2<T> force)
        {
            vel += force * body_mass.inv_mass;
        }

        // Applies only rotational force, without changing the linear velocity.
        void ApplyTorque(const BodyMass<T> &body_mass, T force)
        {
            angular_vel += force * body_mass.inv_angular_mass;
        }

        // Applies both linear and rotational force at a specific point.
        void ApplyForce(const BodyMass<T> &body_mass, vec2<T> point, vec2<T> force)
        {
            ApplyForceToCenterOfMass(body_mass, force);
            ApplyTorque(body_mass, BodyMass<T>::ForceToTorque(point, force));
        }
    };

    template <typename T>
    class BodyPos
    {
        vec2<T> pos;
        T angle = 0;
        vec2<T> dir;

      public:
        constexpr BodyPos() {}

        [[nodiscard]] vec2<T> GetPos() const {return pos;}

        [[nodiscard]] T GetAngle() const {return angle;}
        [[nodiscard]] vec2<T> GetDir() const {return dir;}

        void SetPos(vec2<T> new_pos)
        {
            pos = new_pos;
        }

        void SetAngle(T new_angle)
        {
            angle = new_angle;
            dir = vec2<T>::dir(angle);
        }

        [[nodiscard]] vec2<T> DirFromLocal(vec2<T> dir) const
        {
            return dir.to_rotation_matrix() * dir;
        }
        [[nodiscard]] vec2<T> PosFromLocal(vec2<T> point) const
        {
            return DirFromLocal(point) + pos;
        }
        [[nodiscard]] vec2<T> DirToLocal(vec2<T> dir) const
        {
            return dir.to_rotation_matrix().transpose() * dir;
        }
        [[nodiscard]] vec2<T> PosToLocal(vec2<T> point) const
        {
            return DirToLocal(point - pos);
        }
    };

    template <typename T>
    struct CollisionInput
    {
        const BodyMass<T> *mass = nullptr;
        const BodyVelocity<T> *vel = nullptr;
        const BodyPos<T> *pos = nullptr;
    };

    // `normal` is the collision normal in world space, pointing from `a` to `b`. Must be normalized.
    // `point` is the collision point in world space.
    // `bounce_func` is `(T speed) -> T`. Given a vertical bounce speed (as if fully elastic bounce), returns a FACTOR that modifies the bounce.
    //     Returning 0 results in no bounce, 1 results in a full bounce (fully elastic collision). Other values can be used too.
    // `friction_func` is `(T speed) -> T`. It returns the tangent speed FACTOR, given the assumed tangent speed as if there was no friction and no bounce.
    //     The input `speed` can be negative (positive means B moves clockwise around A, negative if counter-clockwise).
    //     Returning 1 results in no friction. Returning 0 results in full friction.
    template <typename T, typename BounceFunc, typename FrictionFunc>
    void ComputeCollisionResponse(const CollisionInput<T> &a, const CollisionInput<T> &b, vec2<T> point, vec2<T> normal, BounceFunc bounce_func, FrictionFunc friction_func)
    {
        // Collision point in local coords.
        vec2<T> a_point = a.pos->PosToLocal(point);
        vec2<T> b_point = b.pos->PosToLocal(point);

        // Collision normal in local coords, pointing outwards from the respective body.
        vec2<T> a_normal = a.pos->DirToLocal(normal);
        vec2<T> b_normal = b.pos->DirToLocal(-normal); // Sic!

        // Velocity of each body at the collision point, in world coords.
        vec2<T> a_world_vel = a.pos->DirFromLocal(a.vel->GetVelocityAtPoint(a_point));
        vec2<T> b_world_vel = b.pos->DirFromLocal(b.vel->GetVelocityAtPoint(b_point));

        // Delta velocity in world coords.
        vec2<T> delta_vel = b_world_vel - a_world_vel;

        // Delta velocity in local coords, of the other body relative to this one.
        vec2<T> a_delta_vel = a.pos->DirToLocal(delta_vel);
        vec2<T> b_delta_vel = b.pos->DirToLocal(-delta_vel); // Sic.

        // Force along normal for frictionless, fully elastic collision. Applying it only one of the objects would make the collision inelastic.
        T a_initial_force_part = a.mass->GetRequiredForceInDirection(a_point, a_normal, a_normal, a_normal /dot/ a_delta_vel);
        T b_initial_force_part = b.mass->GetRequiredForceInDirection(b_point, b_normal, b_normal, b_normal /dot/ b_delta_vel);

        // The bounce factor.
        T bounce = bounce_func(-(delta_vel /dot/ normal));

        // Force along normal for frictionless collision with the specified bounce.
        T initial_force = (a_initial_force_part + b_initial_force_part) * T(0.5) * bounce;

        // Modified velocities as if for frictionless, fully elastic collision.
        BodyVelocity<T> a_initial_vel = *a.vel;
        BodyVelocity<T> b_initial_vel = *b.vel;
        a_initial_vel.ApplyForce(*a.mass, a_point, a_normal, initial_force);
        b_initial_vel.ApplyForce(*b.mass, b_point, b_normal, initial_force);

        // Compute tangent speed for frictionless, fully elastic collision.
        // It can be negative. It's positive if B moves clockwise around A.
        T initial_tangent_speed = (b.pos->DirFromLocal(b_initial_vel.GetVelocityAtPoint(b_point)) - a.pos->DirFromLocal(a_initial_vel.GetVelocityAtPoint(a_point))) /dot/ normal.rot90();
        T desired_tangent_speed = friction_func(initial_tangent_speed);

        #error how do I interpolate tangent speed with two bodies?
    }
}
