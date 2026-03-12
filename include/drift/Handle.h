#pragma once

#include <cstdint>

namespace drift {

// Generational handle: 20-bit index + 12-bit generation packed into uint32_t.
// Tag type T is a phantom type for type safety (e.g. Handle<struct TextureTag>).
template<typename T>
struct Handle {
    uint32_t id = 0;

    Handle() = default;
    explicit Handle(uint32_t id) : id(id) {}

    bool valid() const { return id != 0; }
    explicit operator bool() const { return id != 0; }

    uint32_t index() const { return id & 0xFFFFF; }
    uint32_t generation() const { return id >> 20; }

    static Handle make(uint32_t index, uint32_t gen) {
        return Handle{(gen << 20) | (index & 0xFFFFF)};
    }

    bool operator==(Handle o) const { return id == o.id; }
    bool operator!=(Handle o) const { return id != o.id; }
};

// Concrete handle typedefs
struct TextureTag {};
struct SpritesheetTag {};
struct AnimationTag {};
struct SoundTag {};
struct PlayingSoundTag {};
struct FontTag {};
struct TilemapTag {};
struct EmitterTag {};
struct CameraTag {};

using TextureHandle      = Handle<TextureTag>;
using SpritesheetHandle  = Handle<SpritesheetTag>;
using AnimationHandle    = Handle<AnimationTag>;
using SoundHandle        = Handle<SoundTag>;
using PlayingSoundHandle = Handle<PlayingSoundTag>;
using FontHandle         = Handle<FontTag>;
using TilemapHandle      = Handle<TilemapTag>;
using EmitterHandle      = Handle<EmitterTag>;
using CameraHandle       = Handle<CameraTag>;

} // namespace drift
