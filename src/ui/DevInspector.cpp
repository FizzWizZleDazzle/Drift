#ifdef DRIFT_DEV

#include "ui/DevPanels.hpp"

#include <drift/App.hpp>
#include <drift/AssetServer.hpp>
#include <drift/Reflect.hpp>
#include <drift/World.hpp>
#include <drift/Hierarchy.hpp>
#include <drift/components/GlobalTransform.hpp>
#include <drift/components/Hierarchy.hpp>
#include <drift/components/Name.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/resources/RendererResource.hpp>

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace drift {

namespace {

struct InspectorState {
    EntityId selected = InvalidEntityId;
    char filter[64] = {};
};

std::string entityLabel(World& world, EntityId e) {
    const auto* name = world.get<Name>(e, world.nameId());
    char buf[96];
    if (name && name->value[0]) {
        std::snprintf(buf, sizeof(buf), "%s##%llu", name->value, static_cast<unsigned long long>(e));
    } else {
        std::snprintf(buf, sizeof(buf), "Entity %llu##%llu", static_cast<unsigned long long>(e),
                      static_cast<unsigned long long>(e));
    }
    return buf;
}

// ---- Field editors ----

bool editValue(const char* label, const reflect::TypeInfo& t, void* p, App& app);

bool editInt(const char* label, const reflect::TypeInfo& t, void* p) {
    ImGuiDataType dt = ImGuiDataType_S32;
    switch (t.kind) {
    case reflect::Kind::Int8:   dt = ImGuiDataType_S8; break;
    case reflect::Kind::Int16:  dt = ImGuiDataType_S16; break;
    case reflect::Kind::Int32:  dt = ImGuiDataType_S32; break;
    case reflect::Kind::Int64:  dt = ImGuiDataType_S64; break;
    case reflect::Kind::UInt8:  dt = ImGuiDataType_U8; break;
    case reflect::Kind::UInt16: dt = ImGuiDataType_U16; break;
    case reflect::Kind::UInt32: dt = ImGuiDataType_U32; break;
    case reflect::Kind::UInt64: dt = ImGuiDataType_U64; break;
    default: break;
    }
    return ImGui::DragScalar(label, dt, p, 0.2f);
}

bool editEnum(const char* label, const reflect::TypeInfo& t, void* p) {
    int64_t cur = reflect::readInt(t, p);
    const char* curName = reflect::enumName(t, cur);
    char fallback[32];
    if (!curName) {
        std::snprintf(fallback, sizeof(fallback), "%lld", static_cast<long long>(cur));
        curName = fallback;
    }
    bool changed = false;
    if (ImGui::BeginCombo(label, curName)) {
        for (const auto& ev : t.enumValues) {
            bool sel = ev.value == cur;
            if (ImGui::Selectable(ev.name.c_str(), sel)) {
                reflect::writeInt(t, p, ev.value);
                changed = true;
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool editTexture(const char* label, TextureHandle& h, App& app) {
    auto* assets = app.getResource<AssetServer>();
    auto* renderer = app.getResource<RendererResource>();
    bool changed = false;
    ImGui::PushID(label);
    if (h && renderer) {
        Vec2 size = renderer->textureSize(h);
        float s = size.y > 0.f ? 32.f / size.y : 1.f;
        ImGui::Image(static_cast<ImTextureID>(renderer->imguiTextureId(h)),
                     ImVec2(std::min(size.x * s, 96.f), 32.f));
        ImGui::SameLine();
    }
    const char* path = assets ? assets->pathOf(h) : nullptr;
    char buf[256];
    if (path) {
        std::snprintf(buf, sizeof(buf), "%s", path);
    } else if (h) {
        std::snprintf(buf, sizeof(buf), "(made in code #%u)", h.id);
    } else {
        buf[0] = '\0';
    }
    if (ImGui::InputText(label, buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue) && assets) {
        TextureHandle loaded = buf[0] ? assets->loadTexture(buf) : TextureHandle{};
        if (loaded || !buf[0]) {
            h = loaded;
            changed = true;
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Type a path and press Enter to load");
    ImGui::PopID();
    return changed;
}

bool editValue(const char* label, const reflect::TypeInfo& t, void* p, App& app) {
    using reflect::Kind;
    switch (t.kind) {
    case Kind::Bool:
        return ImGui::Checkbox(label, static_cast<bool*>(p));
    case Kind::Int8: case Kind::Int16: case Kind::Int32: case Kind::Int64:
    case Kind::UInt8: case Kind::UInt16: case Kind::UInt32: case Kind::UInt64:
        return editInt(label, t, p);
    case Kind::Float:
        return ImGui::DragFloat(label, static_cast<float*>(p), 0.1f);
    case Kind::Double:
        return ImGui::DragScalar(label, ImGuiDataType_Double, p, 0.1f);
    case Kind::Vec2:
        return ImGui::DragFloat2(label, &static_cast<Vec2*>(p)->x, 0.5f);
    case Kind::Vec4:
        return ImGui::DragFloat4(label, &static_cast<Vec4*>(p)->x, 0.05f);
    case Kind::Rect:
        return ImGui::DragFloat4(label, &static_cast<Rect*>(p)->x, 0.5f);
    case Kind::Color: {
        auto& c = *static_cast<Color*>(p);
        float f[4] = {c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f};
        if (!ImGui::ColorEdit4(label, f)) return false;
        auto b = [](float v) { return static_cast<uint8_t>(std::lround(std::clamp(v, 0.f, 1.f) * 255.f)); };
        c = {b(f[0]), b(f[1]), b(f[2]), b(f[3])};
        return true;
    }
    case Kind::ColorF:
        return ImGui::ColorEdit4(label, &static_cast<ColorF*>(p)->r, ImGuiColorEditFlags_Float);
    case Kind::Enum:
        return editEnum(label, t, p);
    case Kind::Chars:
        return ImGui::InputText(label, static_cast<char*>(p), t.size);
    case Kind::String: {
        auto& s = *static_cast<std::string*>(p);
        char buf[512];
        std::snprintf(buf, sizeof(buf), "%s", s.c_str());
        if (!ImGui::InputText(label, buf, sizeof(buf))) return false;
        s = buf;
        return true;
    }
    case Kind::Texture:
        return editTexture(label, *static_cast<TextureHandle*>(p), app);
    case Kind::Font:
    case Kind::Sound: {
        auto* assets = app.getResource<AssetServer>();
        uint32_t id = *static_cast<uint32_t*>(p);
        const char* path = nullptr;
        if (assets) {
            path = t.kind == Kind::Font ? assets->pathOf(FontHandle{id}) : assets->pathOf(SoundHandle{id});
        }
        ImGui::LabelText(label, "%s", path ? path : (id ? "(made in code)" : "(none)"));
        return false;
    }
    case Kind::Entity: {
        auto& e = *static_cast<EntityId*>(p);
        bool changed = ImGui::InputScalar(label, ImGuiDataType_U64, &e);
        World& world = app.world();
        if (e != InvalidEntityId && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", world.isAlive(e) ? entityLabel(world, e).c_str() : "(not alive)");
        }
        return changed;
    }
    case Kind::Struct: {
        bool changed = false;
        if (ImGui::TreeNode(label)) {
            for (const auto& f : t.fields) {
                changed |= editValue(f.name.c_str(), *f.type, static_cast<char*>(p) + f.offset, app);
            }
            ImGui::TreePop();
        }
        return changed;
    }
    case Kind::Vector: {
        bool changed = false;
        size_t n = t.vecSize(p);
        char head[96];
        std::snprintf(head, sizeof(head), "%s [%zu]###%s", label, n, label);
        if (ImGui::TreeNode(head)) {
            if (ImGui::SmallButton("+")) { t.vecResize(p, n + 1); changed = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("-") && n > 0) { t.vecResize(p, n - 1); changed = true; }
            n = t.vecSize(p);
            for (size_t i = 0; i < n; ++i) {
                char idx[16];
                std::snprintf(idx, sizeof(idx), "[%zu]", i);
                ImGui::PushID(static_cast<int>(i));
                changed |= editValue(idx, *t.element, t.vecAt(p, i), app);
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        return changed;
    }
    }
    return false;
}

// ---- Entities ----

std::vector<EntityId> listEntities(World& world) {
    std::unordered_set<EntityId> set;
    for (const reflect::ComponentInfo* info : reflect::components()) {
        ComponentId cid = info->id(world);
        if (!cid) continue;
        for (EntityId e : world.entitiesWith(cid)) set.insert(e);
    }
    std::vector<EntityId> out(set.begin(), set.end());
    std::sort(out.begin(), out.end());
    return out;
}

void drawTree(World& world, InspectorState& st, EntityId e,
              const std::unordered_map<EntityId, std::vector<EntityId>>& kids) {
    auto it = kids.find(e);
    bool leaf = it == kids.end() || it->second.empty();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (leaf) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (e == st.selected) flags |= ImGuiTreeNodeFlags_Selected;
    bool open = ImGui::TreeNodeEx(entityLabel(world, e).c_str(), flags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) st.selected = e;
    if (open && !leaf) {
        for (EntityId c : it->second) drawTree(world, st, c, kids);
        ImGui::TreePop();
    }
}

void drawComponents(App& app, InspectorState& st) {
    World& world = app.world();
    EntityId e = st.selected;
    ImGui::Text("Entity %llu", static_cast<unsigned long long>(e));

    const reflect::ComponentInfo* removeMe = nullptr;
    for (const reflect::ComponentInfo* info : reflect::components()) {
        ComponentId cid = info->id(world);
        if (!cid || !world.hasComponent(e, cid)) continue;
        const void* current = world.getComponent(e, cid);
        if (!current) continue;

        ImGui::PushID(info->name.c_str());
        bool open = ImGui::CollapsingHeader(info->name.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Remove component")) removeMe = info;
            ImGui::EndPopup();
        }
        if (open) {
            reflect::Value edit(*info->info, current);
            bool changed = false;
            for (const auto& f : info->info->fields) {
                changed |= editValue(f.name.c_str(), *f.type,
                                     static_cast<char*>(edit.get()) + f.offset, app);
            }
            if (info->info->fields.empty()) ImGui::TextDisabled("(no fields)");
            if (changed) world.setComponent(e, cid, edit.get(), info->info->size);
        }
        ImGui::PopID();
    }
    if (removeMe) world.removeComponent(e, removeMe->id(world));

    ImGui::Spacing();
    if (ImGui::BeginCombo("##add", "Add component")) {
        for (const reflect::ComponentInfo* info : reflect::components()) {
            ComponentId cid = info->id(world);
            if (!cid || world.hasComponent(e, cid)) continue;
            if (ImGui::Selectable(info->name.c_str())) {
                reflect::Value v(*info->info);
                world.setComponent(e, cid, v.get(), info->info->size);
            }
        }
        ImGui::EndCombo();
    }
}

// Outline the selected entity in the world.
void highlight(App& app, EntityId e) {
    World& world = app.world();
    auto* renderer = app.getResource<RendererResource>();
    if (!renderer) return;
    const auto& reg = world.componentRegistry();
    Vec2 pos;
    float rot = 0.f;
    Vec2 scale = Vec2::one();
    ComponentId gid = reg.getByName("GlobalTransform2D");
    if (const auto* g = gid ? world.get<GlobalTransform2D>(e, gid) : nullptr) {
        pos = g->position; rot = g->rotation; scale = g->scale;
    } else if (const auto* t = world.get<Transform2D>(e, world.transform2dId())) {
        pos = t->position; rot = t->rotation; scale = t->scale;
    } else {
        return;
    }

    Color c{255, 220, 0, 255};
    uint8_t oldLayer = renderer->layer();
    const auto* sprite = world.get<Sprite>(e, world.spriteId());
    if (sprite && sprite->layer == ScreenLayer) renderer->setLayer(ScreenLayer);
    if (sprite && sprite->texture) {
        Vec2 size = renderer->textureSize(sprite->texture);
        if (sprite->srcRect.w > 0.f) size = {sprite->srcRect.w, sprite->srcRect.h};
        Vec2 corners[4] = {{0, 0}, {size.x, 0}, {size.x, size.y}, {0, size.y}};
        float cs = std::cos(rot), sn = std::sin(rot);
        for (auto& p : corners) {
            Vec2 l = {(p.x - sprite->origin.x) * scale.x, (p.y - sprite->origin.y) * scale.y};
            p = {pos.x + l.x * cs - l.y * sn, pos.y + l.x * sn + l.y * cs};
        }
        renderer->drawPolyline(corners, 4, c, 2.f, true, 1e6f);
    }
    renderer->drawLine({pos.x - 8.f, pos.y}, {pos.x + 8.f, pos.y}, c, 2.f, 1e6f);
    renderer->drawLine({pos.x, pos.y - 8.f}, {pos.x, pos.y + 8.f}, c, 2.f, 1e6f);
    renderer->setLayer(oldLayer);
}

void drawInspector(App& app, InspectorState& st) {
    World& world = app.world();
    if (st.selected != InvalidEntityId && !world.isAlive(st.selected)) st.selected = InvalidEntityId;

    if (ImGui::Button("Spawn")) {
        EntityId e = world.createEntity();
        Name n("Entity");
        world.set(e, world.nameId(), n);
        world.set(e, world.transform2dId(), Transform2D{});
        st.selected = e;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(st.selected == InvalidEntityId);
    if (ImGui::Button("Despawn")) {
        despawnRecursive(world, st.selected);
        st.selected = InvalidEntityId;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.f);
    ImGui::InputTextWithHint("##filter", "filter by name", st.filter, sizeof(st.filter));

    std::vector<EntityId> all = listEntities(world);
    ImGui::Text("%zu entities", all.size());

    float listHeight = ImGui::GetContentRegionAvail().y * 0.4f;
    if (ImGui::BeginChild("entities", ImVec2(0.f, listHeight), ImGuiChildFlags_Borders)) {
        if (st.filter[0]) {
            for (EntityId e : all) {
                std::string label = entityLabel(world, e);
                if (label.find(st.filter) == std::string::npos) continue;
                if (ImGui::Selectable(label.c_str(), e == st.selected)) st.selected = e;
            }
        } else {
            ComponentId parentId = world.componentRegistry().get<Parent>();
            std::unordered_set<EntityId> listed(all.begin(), all.end());
            std::unordered_map<EntityId, std::vector<EntityId>> kids;
            std::vector<EntityId> roots;
            for (EntityId e : all) {
                const auto* p = world.get<Parent>(e, parentId);
                if (p && listed.count(p->entity)) kids[p->entity].push_back(e);
                else roots.push_back(e);
            }
            for (EntityId r : roots) drawTree(world, st, r, kids);
        }
    }
    ImGui::EndChild();

    if (ImGui::BeginChild("components")) {
        if (st.selected != InvalidEntityId) {
            drawComponents(app, st);
        } else {
            ImGui::TextDisabled("Select an entity");
        }
    }
    ImGui::EndChild();

    if (st.selected != InvalidEntityId) highlight(app, st.selected);
}

} // namespace

UIResource::DevPanel makeInspectorPanel() {
    auto state = std::make_shared<InspectorState>();
    return [state](App& app) { drawInspector(app, *state); };
}

} // namespace drift

#endif // DRIFT_DEV
