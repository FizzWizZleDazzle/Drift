#pragma once

#include <drift/Resource.hpp>
#include <drift/Handle.hpp>
#include <drift/Types.hpp>

namespace drift {

class RendererResource;

class ParticleResource : public Resource {
public:
    explicit ParticleResource(RendererResource& renderer);
    ~ParticleResource() override;

    DRIFT_RESOURCE(ParticleResource)

    EmitterHandle createEmitter(const EmitterDef& def);
    void destroyEmitter(EmitterHandle emitter);
    void setEmitterPosition(EmitterHandle emitter, Vec2 pos);
    void burst(EmitterHandle emitter, int32_t count);
    void startEmitter(EmitterHandle emitter);
    void stopEmitter(EmitterHandle emitter);
    bool isEmitterActive(EmitterHandle emitter) const;

    void update(float dt);
    void render();

    int32_t particleCount() const;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace drift
