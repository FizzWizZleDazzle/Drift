using System;

namespace Drift
{
    public class BoxCollider : EcsComponent<drift.BoxCollider2D>
    {
        protected override ulong ComponentId => EntityRegistry.World.boxCollider2dId();
        protected override drift.BoxCollider2D Wrap(IntPtr p) => new(p, false);

        protected internal override void OnAttach()
            => EntityRegistry.Commands.entity(Entity._id).insertBoxCollider(new drift.BoxCollider2D());

        public drift.Vec2 Size
        {
            get { var c = Ecs; return c != null ? new drift.Vec2(c.halfSize.x * 2f, c.halfSize.y * 2f) : drift.Vec2.Zero(); }
            set { if (Ecs is { } c) c.halfSize = new drift.Vec2(value.x * 0.5f, value.y * 0.5f); }
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
