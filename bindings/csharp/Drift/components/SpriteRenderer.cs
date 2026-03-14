using System;

namespace Drift
{
    public class SpriteRenderer : EcsComponent<drift.Sprite>
    {
        protected override ulong ComponentId => EntityRegistry.World.spriteId();
        protected override drift.Sprite Wrap(IntPtr p) => new(p, false);

        protected internal override void OnAttach()
            => EntityRegistry.Commands.entity(Entity._id).insert(new drift.Sprite());

        public drift.TextureHandle Texture
        {
            get => Ecs?.texture ?? new drift.TextureHandle();
            set { if (Ecs is { } s) s.texture = value; }
        }

        public drift.Rect SrcRect
        {
            get => Ecs?.srcRect ?? new drift.Rect();
            set { if (Ecs is { } s) s.srcRect = value; }
        }

        public drift.Vec2 Origin
        {
            get => Ecs?.origin ?? drift.Vec2.Zero();
            set { if (Ecs is { } s) s.origin = value; }
        }

        public drift.Color Tint
        {
            get => Ecs?.tint ?? drift.Color.White();
            set { if (Ecs is { } s) s.tint = value; }
        }

        public drift.Flip Flip
        {
            get => Ecs?.flip ?? drift.Flip.None;
            set { if (Ecs is { } s) s.flip = value; }
        }

        public float ZOrder
        {
            get => Ecs?.zOrder ?? 0f;
            set { if (Ecs is { } s) s.zOrder = value; }
        }

        public bool Visible
        {
            get => Ecs?.visible ?? true;
            set { if (Ecs is { } s) s.visible = value; }
        }
    }
}
