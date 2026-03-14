using System;

namespace Drift
{
    public class ParticleEffect : EcsComponent<drift.ParticleEmitter>
    {
        protected override ulong ComponentId => EntityRegistry.World.particleEmitterId();
        protected override drift.ParticleEmitter Wrap(IntPtr p) => new(p, false);

        protected internal override void OnAttach()
            => EntityRegistry.Commands.entity(Entity._id)
                .insertParticleEmitter(new drift.ParticleEmitter());

        public drift.TextureHandle Texture
        {
            get => Ecs?.config?.texture ?? new drift.TextureHandle();
            set { var e = Ecs; if (e?.config != null) e.config.texture = value; }
        }

        public float SpawnRate
        {
            get => Ecs?.config?.spawnRate ?? 10f;
            set { var e = Ecs; if (e?.config != null) e.config.spawnRate = value; }
        }

        public int MaxParticles
        {
            get => Ecs?.config?.maxParticles ?? 256;
            set { var e = Ecs; if (e?.config != null) e.config.maxParticles = value; }
        }

        public drift.ColorF ColorStart
        {
            get => Ecs?.config?.colorStart ?? new drift.ColorF(1, 1, 1, 1);
            set { var e = Ecs; if (e?.config != null) e.config.colorStart = value; }
        }

        public drift.ColorF ColorEnd
        {
            get => Ecs?.config?.colorEnd ?? new drift.ColorF(1, 1, 1, 0);
            set { var e = Ecs; if (e?.config != null) e.config.colorEnd = value; }
        }

        public drift.ParticleBlendMode BlendMode
        {
            get => Ecs?.config?.blendMode ?? drift.ParticleBlendMode.Alpha;
            set { var e = Ecs; if (e?.config != null) e.config.blendMode = value; }
        }

        public float ZOrder
        {
            get => Ecs?.config?.zOrder ?? 10f;
            set { var e = Ecs; if (e?.config != null) e.config.zOrder = value; }
        }

        public void Play() { var e = Ecs; if (e != null) e.playing = true; }
        public void Stop() { var e = Ecs; if (e != null) e.playing = false; }
        public bool IsPlaying => Ecs?.playing ?? false;
    }
}
