using System;

namespace Drift
{
    public class RigidBody : Component
    {
        private static ulong RbId => EntityRegistry.World.rigidBody2dId();
        private static ulong VelId => EntityRegistry.World.velocity2dId();

        private drift.RigidBody2D? Rb
        {
            get
            {
                var p = EntityRegistry.World.getComponentMut(Entity._id, RbId);
                return p != IntPtr.Zero ? new(p, false) : null;
            }
        }

        private drift.Velocity2D? Vel
        {
            get
            {
                var p = EntityRegistry.World.getComponentMut(Entity._id, VelId);
                return p != IntPtr.Zero ? new(p, false) : null;
            }
        }

        protected internal override void OnAttach()
        {
            EntityRegistry.Commands.entity(Entity._id)
                .insertRigidBody(new drift.RigidBody2D())
                .insertVelocity(new drift.Velocity2D());
        }

        public drift.BodyType Type
        {
            get => Rb?.type ?? drift.BodyType.Static;
            set { if (Rb is { } r) r.type = value; }
        }

        public bool FixedRotation
        {
            get => Rb?.fixedRotation ?? false;
            set { if (Rb is { } r) r.fixedRotation = value; }
        }

        public float GravityScale
        {
            get => Rb?.gravityScale ?? 1f;
            set { if (Rb is { } r) r.gravityScale = value; }
        }

        public drift.Vec2 Velocity
        {
            get => Vel?.linear ?? drift.Vec2.Zero();
            set { if (Vel is { } v) v.linear = value; }
        }

        public float AngularVelocity
        {
            get => Vel?.angular ?? 0f;
            set { if (Vel is { } v) v.angular = value; }
        }

        protected internal override void OnDetach()
        {
            if (RbId != 0) EntityRegistry.Commands.remove(Entity._id, RbId);
            if (VelId != 0) EntityRegistry.Commands.remove(Entity._id, VelId);
        }
    }
}
