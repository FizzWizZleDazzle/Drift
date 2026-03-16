#pragma once

#include <drift/Resource.hpp>
#include <drift/Types.hpp>
#include <vector>

namespace drift {

// Indexed access to collision events per frame.
class CollisionBridge : public Resource {
public:
    struct Entry {
        EntityId a = InvalidEntityId;
        EntityId b = InvalidEntityId;
    };

    // Collision start events
    int collisionStartCount() const { return static_cast<int>(starts_.size()); }
    EntityId collisionStartA(int i) const { return (i >= 0 && i < (int)starts_.size()) ? starts_[i].a : InvalidEntityId; }
    EntityId collisionStartB(int i) const { return (i >= 0 && i < (int)starts_.size()) ? starts_[i].b : InvalidEntityId; }

    // Collision end events
    int collisionEndCount() const { return static_cast<int>(ends_.size()); }
    EntityId collisionEndA(int i) const { return (i >= 0 && i < (int)ends_.size()) ? ends_[i].a : InvalidEntityId; }
    EntityId collisionEndB(int i) const { return (i >= 0 && i < (int)ends_.size()) ? ends_[i].b : InvalidEntityId; }

    // Sensor start events
    int sensorStartCount() const { return static_cast<int>(sensorStarts_.size()); }
    EntityId sensorStartA(int i) const { return (i >= 0 && i < (int)sensorStarts_.size()) ? sensorStarts_[i].a : InvalidEntityId; }
    EntityId sensorStartB(int i) const { return (i >= 0 && i < (int)sensorStarts_.size()) ? sensorStarts_[i].b : InvalidEntityId; }

    // Sensor end events
    int sensorEndCount() const { return static_cast<int>(sensorEnds_.size()); }
    EntityId sensorEndA(int i) const { return (i >= 0 && i < (int)sensorEnds_.size()) ? sensorEnds_[i].a : InvalidEntityId; }
    EntityId sensorEndB(int i) const { return (i >= 0 && i < (int)sensorEnds_.size()) ? sensorEnds_[i].b : InvalidEntityId; }

    // Called by the bridge system each frame
    void clear() {
        starts_.clear();
        ends_.clear();
        sensorStarts_.clear();
        sensorEnds_.clear();
    }

    void addCollisionStart(EntityId a, EntityId b) { starts_.push_back({a, b}); }
    void addCollisionEnd(EntityId a, EntityId b) { ends_.push_back({a, b}); }
    void addSensorStart(EntityId a, EntityId b) { sensorStarts_.push_back({a, b}); }
    void addSensorEnd(EntityId a, EntityId b) { sensorEnds_.push_back({a, b}); }

    DRIFT_RESOURCE(CollisionBridge)

private:
    std::vector<Entry> starts_;
    std::vector<Entry> ends_;
    std::vector<Entry> sensorStarts_;
    std::vector<Entry> sensorEnds_;
};

} // namespace drift
