using System;

namespace Drift
{
    public class CameraComponent : EcsComponent<drift.Camera>
    {
        protected override ulong ComponentId => EntityRegistry.World.cameraId();
        protected override drift.Camera Wrap(IntPtr p) => new(p, false);

        protected internal override void OnAttach()
            => EntityRegistry.Commands.entity(Entity._id)
                .insert(new drift.Camera { zoom = 1f, active = true });

        public float Zoom
        {
            get => Ecs?.zoom ?? 1f;
            set { if (Ecs is { } c) c.zoom = value; }
        }

        public bool Active
        {
            get => Ecs?.active ?? false;
            set { if (Ecs is { } c) c.active = value; }
        }
    }
}
