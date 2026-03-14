using System;

namespace Drift
{
    public class Shake : EcsComponent<drift.CameraShake>
    {
        protected override ulong ComponentId => EntityRegistry.World.cameraShakeId();
        protected override drift.CameraShake Wrap(IntPtr p) => new(p, false);

        protected internal override void OnAttach()
            => EntityRegistry.Commands.entity(Entity._id)
                .insertCameraShake(new drift.CameraShake());

        public void Trigger(float magnitude) => Ecs?.trigger(magnitude);

        public float Intensity => Ecs?.intensity ?? 0f;

        public float Decay
        {
            get => Ecs?.decay ?? 5f;
            set { if (Ecs is { } c) c.decay = value; }
        }
    }
}
