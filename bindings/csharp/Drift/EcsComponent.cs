using System;

namespace Drift
{
    public abstract class EcsComponent<TNative> : Component where TNative : class
    {
        protected abstract ulong ComponentId { get; }
        protected abstract TNative Wrap(IntPtr ptr);

        protected TNative? Ecs
        {
            get
            {
                var ptr = EntityRegistry.World.getComponentMut(Entity._id, ComponentId);
                return ptr != IntPtr.Zero ? Wrap(ptr) : null;
            }
        }

        protected internal override void OnDetach()
        {
            if (ComponentId != 0)
                EntityRegistry.Commands.remove(Entity._id, ComponentId);
        }
    }
}
