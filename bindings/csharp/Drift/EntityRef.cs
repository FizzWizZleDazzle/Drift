namespace Drift
{
    /// <summary>
    /// Lightweight struct handle for entities that don't need behavioral components.
    /// Use Entity for full component support; use EntityRef for simple references.
    /// </summary>
    public readonly struct EntityRef
    {
        public readonly ulong Id;

        public EntityRef(ulong id) => Id = id;

        public bool IsAlive => EntityRegistry.World.isAlive(Id);

        public drift.Vec2 Position
        {
            get
            {
                var ptr = EntityRegistry.World.getComponentMut(Id, EntityRegistry.World.transform2dId());
                if (ptr == System.IntPtr.Zero) return drift.Vec2.Zero();
                return new drift.Transform2D(ptr, false).position;
            }
            set
            {
                var ptr = EntityRegistry.World.getComponentMut(Id, EntityRegistry.World.transform2dId());
                if (ptr != System.IntPtr.Zero)
                    new drift.Transform2D(ptr, false).position = value;
            }
        }

        public void Destroy() => EntityRegistry.Commands.despawn(Id);

        public static implicit operator ulong(EntityRef r) => r.Id;
    }
}
