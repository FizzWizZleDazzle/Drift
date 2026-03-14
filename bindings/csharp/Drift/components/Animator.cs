using System;
using System.Collections.Generic;

namespace Drift
{
    public class Animator : EcsComponent<drift.SpriteAnimator>
    {
        private readonly Dictionary<string, int> _clipNames = new();

        protected override ulong ComponentId => EntityRegistry.World.spriteAnimatorId();
        protected override drift.SpriteAnimator Wrap(IntPtr p) => new(p, false);

        protected internal override void OnAttach()
            => EntityRegistry.Commands.entity(Entity._id)
                .insertSpriteAnimator(new drift.SpriteAnimator());

        public void AddClip(string name, drift.Rect[] frames, float frameDuration, bool loop = true)
        {
            var anim = Ecs;
            if (anim == null) return;

            int clipIndex = anim.clipCount();
            _clipNames[name] = clipIndex;
            anim.addClip(frames[0], frames.Length, frameDuration, loop);
        }

        public void Play(string name)
        {
            if (!_clipNames.TryGetValue(name, out int index)) return;
            Ecs?.play(index);
        }

        public void Stop() => Ecs?.stop();

        public bool IsFinished => Ecs?.isFinished() ?? true;
    }
}
