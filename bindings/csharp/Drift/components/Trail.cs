using System;

namespace Drift
{
    public class Trail : EcsComponent<drift.TrailRenderer>
    {
        protected override ulong ComponentId => EntityRegistry.World.trailRendererId();
        protected override drift.TrailRenderer Wrap(IntPtr p) => new(p, false);

        protected internal override void OnAttach()
            => EntityRegistry.Commands.entity(Entity._id)
                .insertTrailRenderer(new drift.TrailRenderer());

        public float Width
        {
            get => Ecs?.width ?? 4f;
            set { if (Ecs is { } c) c.width = value; }
        }

        public float Lifetime
        {
            get => Ecs?.lifetime ?? 0.5f;
            set { if (Ecs is { } c) c.lifetime = value; }
        }

        public drift.ColorF ColorStart
        {
            get => Ecs?.colorStart ?? new drift.ColorF(1, 1, 1, 1);
            set { if (Ecs is { } c) c.colorStart = value; }
        }

        public drift.ColorF ColorEnd
        {
            get => Ecs?.colorEnd ?? new drift.ColorF(1, 1, 1, 0);
            set { if (Ecs is { } c) c.colorEnd = value; }
        }

        public float MinDistance
        {
            get => Ecs?.minDistance ?? 2f;
            set { if (Ecs is { } c) c.minDistance = value; }
        }

        public float ZOrder
        {
            get => Ecs?.zOrder ?? -1f;
            set { if (Ecs is { } c) c.zOrder = value; }
        }
    }
}
