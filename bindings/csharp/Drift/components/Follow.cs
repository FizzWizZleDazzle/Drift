using System;

namespace Drift
{
    public class Follow : EcsComponent<drift.CameraFollow>
    {
        protected override ulong ComponentId => EntityRegistry.World.cameraFollowId();
        protected override drift.CameraFollow Wrap(IntPtr p) => new(p, false);

        protected internal override void OnAttach()
            => EntityRegistry.Commands.entity(Entity._id)
                .insertCameraFollow(new drift.CameraFollow());

        public Entity? Target
        {
            get
            {
                var id = Ecs?.target ?? 0UL;
                return id != 0 ? EntityRegistry.GetEntity(id) : null;
            }
            set { if (Ecs is { } c) c.target = value?._id ?? 0; }
        }

        public float Smoothing
        {
            get => Ecs?.smoothing ?? 5f;
            set { if (Ecs is { } c) c.smoothing = value; }
        }

        public drift.Vec2 Offset
        {
            get => Ecs?.offset ?? drift.Vec2.Zero();
            set { if (Ecs is { } c) c.offset = value; }
        }

        public drift.Vec2 DeadZone
        {
            get => Ecs?.deadZone ?? drift.Vec2.Zero();
            set { if (Ecs is { } c) c.deadZone = value; }
        }
    }
}
