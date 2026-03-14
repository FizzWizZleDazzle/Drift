using System;

namespace Drift
{
    public class CircleCollider : EcsComponent<drift.CircleCollider2D>
    {
        protected override ulong ComponentId => EntityRegistry.World.circleCollider2dId();
        protected override drift.CircleCollider2D Wrap(IntPtr p) => new(p, false);

        protected internal override void OnAttach()
            => EntityRegistry.Commands.entity(Entity._id).insertCircleCollider(new drift.CircleCollider2D());

        public float Radius
        {
            get => Ecs?.radius ?? 0.5f;
            set { if (Ecs is { } c) c.radius = value; }
        }

        public drift.Vec2 Offset
        {
            get => Ecs?.offset ?? drift.Vec2.Zero();
            set { if (Ecs is { } c) c.offset = value; }
        }

        public bool IsSensor
        {
            get => Ecs?.isSensor ?? false;
            set { if (Ecs is { } c) c.isSensor = value; }
        }

        public float Density
        {
            get => Ecs?.density ?? 1f;
            set { if (Ecs is { } c) c.density = value; }
        }

        public float Friction
        {
            get => Ecs?.friction ?? 0.3f;
            set { if (Ecs is { } c) c.friction = value; }
        }

        public float Restitution
        {
            get => Ecs?.restitution ?? 0f;
            set { if (Ecs is { } c) c.restitution = value; }
        }
    }
}
