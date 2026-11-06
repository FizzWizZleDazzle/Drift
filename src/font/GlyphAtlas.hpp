#pragma once

#include <drift/Handle.hpp>
#include <drift/Types.hpp>

#include <cstdint>
#include <vector>

namespace drift {

class RendererResource;

// Glyph bitmaps packed into fixed-size texture pages with a shelf packer.
// Pages are added as needed. Uploads are batched per page as a band of
// dirty rows and sent by flush().
class GlyphAtlas {
public:
    static constexpr int PageSize = 1024;
    static constexpr int PageBudget = 4;   // pages kept before the cache resets

    GlyphAtlas(RendererResource& renderer, TextureFilter filter)
        : renderer_(renderer), filter_(filter) {}

    struct Region {
        TextureHandle texture;
        int page = -1;
        int x = 0, y = 0, w = 0, h = 0;
    };

    // Reserves a w x h cell. Returns false only for cells larger than a page.
    bool allocate(int w, int h, Region& out);
    // Writes coverage (one byte per pixel) into a region as white + alpha.
    void writeAlpha(const Region& r, const uint8_t* alpha, int stride);
    void flush();

    // True once more pages than PageBudget were needed; reset() then drops
    // every glyph so the cache rebuilds with what is drawn now.
    bool overBudget() const { return static_cast<int>(pages_.size()) > PageBudget; }
    void reset();

private:
    struct Shelf { int y, h, x; };
    struct Page {
        TextureHandle texture;
        std::vector<uint8_t> rgba;
        std::vector<Shelf> shelves;
        int nextY = 0;
        int dirtyY0 = PageSize, dirtyY1 = 0;
    };

    bool allocateIn(Page& p, int w, int h, int& x, int& y);
    bool addPage();

    RendererResource& renderer_;
    TextureFilter filter_;
    std::vector<Page> pages_;
};

} // namespace drift
