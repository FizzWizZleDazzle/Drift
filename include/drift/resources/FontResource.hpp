#pragma once

#include <drift/Resource.hpp>
#include <drift/Handle.hpp>
#include <drift/Types.hpp>

namespace drift {

class RendererResource;

class FontResource : public Resource {
public:
    explicit FontResource(RendererResource& renderer);
    ~FontResource() override;

    DRIFT_RESOURCE(FontResource)

    FontHandle loadFont(const char* path, float sizePx);
    void destroyFont(FontHandle font);
    void drawText(FontHandle font, const char* text, Vec2 position,
                  Color color = Color::white(), float scale = 1.f);
    Vec2 measureText(FontHandle font, const char* text, float scale = 1.f) const;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace drift
