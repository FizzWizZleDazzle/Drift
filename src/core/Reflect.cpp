#include <drift/Reflect.hpp>
#include <drift/AssetServer.hpp>
#include <drift/Log.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <unordered_map>

namespace drift::reflect {

using json = nlohmann::json;

namespace {

struct Registry {
    std::unordered_map<std::type_index, std::unique_ptr<TypeInfo>> types;
    std::vector<std::unique_ptr<ComponentInfo>> componentStore;
    std::vector<const ComponentInfo*> components;
    std::unordered_map<std::string, const ComponentInfo*> byName;
    std::unordered_map<std::type_index, const ComponentInfo*> byType;
    std::unique_ptr<TypeInfo> builtins[static_cast<int>(Kind::Vector) + 1];
};

Registry& registry() {
    static Registry r;
    return r;
}

template<typename T>
TypeInfo* makeBuiltin(const char* name, Kind kind) {
    auto* t = new TypeInfo();
    t->name = name;
    t->kind = kind;
    detail::setLifecycle<T>(*t);
    return t;
}

TypeInfo* createBuiltin(Kind k) {
    switch (k) {
    case Kind::Bool:    return makeBuiltin<bool>("bool", k);
    case Kind::Int8:    return makeBuiltin<int8_t>("int8", k);
    case Kind::Int16:   return makeBuiltin<int16_t>("int16", k);
    case Kind::Int32:   return makeBuiltin<int32_t>("int32", k);
    case Kind::Int64:   return makeBuiltin<int64_t>("int64", k);
    case Kind::UInt8:   return makeBuiltin<uint8_t>("uint8", k);
    case Kind::UInt16:  return makeBuiltin<uint16_t>("uint16", k);
    case Kind::UInt32:  return makeBuiltin<uint32_t>("uint32", k);
    case Kind::UInt64:  return makeBuiltin<uint64_t>("uint64", k);
    case Kind::Float:   return makeBuiltin<float>("float", k);
    case Kind::Double:  return makeBuiltin<double>("double", k);
    case Kind::Vec2:    return makeBuiltin<Vec2>("Vec2", k);
    case Kind::Vec4:    return makeBuiltin<Vec4>("Vec4", k);
    case Kind::Rect:    return makeBuiltin<Rect>("Rect", k);
    case Kind::Color:   return makeBuiltin<Color>("Color", k);
    case Kind::ColorF:  return makeBuiltin<ColorF>("ColorF", k);
    case Kind::String:  return makeBuiltin<std::string>("string", k);
    case Kind::Texture: return makeBuiltin<TextureHandle>("Texture", k);
    case Kind::Font:    return makeBuiltin<FontHandle>("Font", k);
    case Kind::Sound:   return makeBuiltin<SoundHandle>("Sound", k);
    case Kind::Entity:  return makeBuiltin<EntityId>("Entity", k);
    default:            return nullptr;
    }
}

// Floats go through their shortest decimal form so files read 0.3, not
// 0.30000001192092896.
json fnum(float f) {
    char buf[32];
    auto res = std::to_chars(buf, buf + sizeof(buf), f);
    return std::strtod(std::string(buf, res.ptr).c_str(), nullptr);
}

float num(const json& j, size_t i, float fallback) {
    return (j.is_array() && i < j.size() && j[i].is_number()) ? j[i].get<float>() : fallback;
}

} // namespace

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

namespace detail {

TypeInfo* typeSlot(std::type_index type, const char* name, Kind kind, bool reset) {
    auto& r = registry();
    auto& slot = r.types[type];
    if (!slot) {
        slot = std::make_unique<TypeInfo>();
    } else if (reset) {
        // Re-describing keeps the pointer stable for anything that refers to it.
        slot->fields.clear();
        slot->enumValues.clear();
    }
    slot->name = name;
    slot->kind = kind;
    return slot.get();
}

const TypeInfo* builtin(Kind kind) {
    auto& slot = registry().builtins[static_cast<int>(kind)];
    if (!slot) slot.reset(createBuiltin(kind));
    return slot.get();
}

void addComponent(const char* name, std::type_index type, const TypeInfo* info) {
    auto& r = registry();
    auto it = r.byType.find(type);
    if (it != r.byType.end()) {
        auto* existing = const_cast<ComponentInfo*>(it->second);
        if (existing->name != name) {
            r.byName.erase(existing->name);
            existing->name = name;
            r.byName[name] = existing;
        }
        existing->info = info;
        return;
    }
    auto c = std::make_unique<ComponentInfo>();
    c->name = name;
    c->type = type;
    c->info = info;
    r.components.push_back(c.get());
    r.byName[name] = c.get();
    r.byType[type] = c.get();
    r.componentStore.push_back(std::move(c));
}

} // namespace detail

void reportMissingFieldType(const TypeInfo& owner, const char* field) {
    DRIFT_LOG_ERROR("reflect: field '%s.%s' has a type that is not described; "
                    "describe it with reflect::type or reflect::enumType first",
                    owner.name.c_str(), field ? field : "?");
}

ComponentId ComponentInfo::id(const World& world) const {
    return world.componentRegistry().get(type);
}

const std::vector<const ComponentInfo*>& components() {
    return registry().components;
}

const ComponentInfo* findComponent(const std::string& name) {
    auto& r = registry();
    auto it = r.byName.find(name);
    return it != r.byName.end() ? it->second : nullptr;
}

const ComponentInfo* findComponent(std::type_index type) {
    auto& r = registry();
    auto it = r.byType.find(type);
    return it != r.byType.end() ? it->second : nullptr;
}

const TypeInfo* findType(std::type_index type) {
    auto& r = registry();
    auto it = r.types.find(type);
    return it != r.types.end() ? it->second.get() : nullptr;
}

// ---------------------------------------------------------------------------
// Value
// ---------------------------------------------------------------------------

Value::Value(const TypeInfo& type, const void* source) : type_(type) {
    mem_ = ::operator new(type.size, std::align_val_t(std::max<size_t>(type.align, 1)));
    type.construct(mem_);
    if (source) type.copy(mem_, source);
}

Value::~Value() {
    type_.destroy(mem_);
    ::operator delete(mem_, std::align_val_t(std::max<size_t>(type_.align, 1)));
}

// ---------------------------------------------------------------------------
// Integer access
// ---------------------------------------------------------------------------

int64_t readInt(const TypeInfo& t, const void* p) {
    switch (t.kind) {
    case Kind::Bool:   return *static_cast<const bool*>(p) ? 1 : 0;
    case Kind::Int8:   return *static_cast<const int8_t*>(p);
    case Kind::Int16:  return *static_cast<const int16_t*>(p);
    case Kind::Int32:  return *static_cast<const int32_t*>(p);
    case Kind::Int64:  return *static_cast<const int64_t*>(p);
    case Kind::UInt8:  return *static_cast<const uint8_t*>(p);
    case Kind::UInt16: return *static_cast<const uint16_t*>(p);
    case Kind::UInt32: return *static_cast<const uint32_t*>(p);
    case Kind::UInt64:
    case Kind::Entity: return static_cast<int64_t>(*static_cast<const uint64_t*>(p));
    case Kind::Enum:
        switch (t.size) {
        case 1: return *static_cast<const int8_t*>(p);
        case 2: return *static_cast<const int16_t*>(p);
        case 4: return *static_cast<const int32_t*>(p);
        default: return *static_cast<const int64_t*>(p);
        }
    default: return 0;
    }
}

void writeInt(const TypeInfo& t, void* p, int64_t v) {
    switch (t.kind) {
    case Kind::Bool:   *static_cast<bool*>(p) = v != 0; break;
    case Kind::Int8:   *static_cast<int8_t*>(p) = static_cast<int8_t>(v); break;
    case Kind::Int16:  *static_cast<int16_t*>(p) = static_cast<int16_t>(v); break;
    case Kind::Int32:  *static_cast<int32_t*>(p) = static_cast<int32_t>(v); break;
    case Kind::Int64:  *static_cast<int64_t*>(p) = v; break;
    case Kind::UInt8:  *static_cast<uint8_t*>(p) = static_cast<uint8_t>(v); break;
    case Kind::UInt16: *static_cast<uint16_t*>(p) = static_cast<uint16_t>(v); break;
    case Kind::UInt32: *static_cast<uint32_t*>(p) = static_cast<uint32_t>(v); break;
    case Kind::UInt64:
    case Kind::Entity: *static_cast<uint64_t*>(p) = static_cast<uint64_t>(v); break;
    case Kind::Enum:
        switch (t.size) {
        case 1: *static_cast<int8_t*>(p) = static_cast<int8_t>(v); break;
        case 2: *static_cast<int16_t*>(p) = static_cast<int16_t>(v); break;
        case 4: *static_cast<int32_t*>(p) = static_cast<int32_t>(v); break;
        default: *static_cast<int64_t*>(p) = v; break;
        }
        break;
    default: break;
    }
}

const char* enumName(const TypeInfo& t, int64_t value) {
    for (const auto& ev : t.enumValues) {
        if (ev.value == value) return ev.name.c_str();
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

json toJson(const TypeInfo& t, const void* p, const JsonContext& ctx) {
    switch (t.kind) {
    case Kind::Bool:
        return *static_cast<const bool*>(p);
    case Kind::Int8: case Kind::Int16: case Kind::Int32: case Kind::Int64:
        return readInt(t, p);
    case Kind::UInt8: case Kind::UInt16: case Kind::UInt32: case Kind::UInt64:
        return static_cast<uint64_t>(readInt(t, p));
    case Kind::Float:
        return fnum(*static_cast<const float*>(p));
    case Kind::Double:
        return *static_cast<const double*>(p);
    case Kind::Vec2: {
        auto v = *static_cast<const Vec2*>(p);
        return json::array({fnum(v.x), fnum(v.y)});
    }
    case Kind::Vec4: {
        auto v = *static_cast<const Vec4*>(p);
        return json::array({fnum(v.x), fnum(v.y), fnum(v.z), fnum(v.w)});
    }
    case Kind::Rect: {
        auto r = *static_cast<const Rect*>(p);
        return json::array({fnum(r.x), fnum(r.y), fnum(r.w), fnum(r.h)});
    }
    case Kind::Color: {
        auto c = *static_cast<const Color*>(p);
        return json::array({c.r, c.g, c.b, c.a});
    }
    case Kind::ColorF: {
        auto c = *static_cast<const ColorF*>(p);
        return json::array({fnum(c.r), fnum(c.g), fnum(c.b), fnum(c.a)});
    }
    case Kind::Enum: {
        int64_t v = readInt(t, p);
        if (const char* n = enumName(t, v)) return n;
        return v;
    }
    case Kind::Chars: {
        const char* s = static_cast<const char*>(p);
        return std::string(s, strnlen(s, t.size));
    }
    case Kind::String:
        return *static_cast<const std::string*>(p);
    case Kind::Texture: {
        auto h = *static_cast<const TextureHandle*>(p);
        const char* path = ctx.assets ? ctx.assets->pathOf(h) : nullptr;
        return path ? json(path) : json(nullptr);
    }
    case Kind::Sound: {
        auto h = *static_cast<const SoundHandle*>(p);
        const char* path = ctx.assets ? ctx.assets->pathOf(h) : nullptr;
        return path ? json(path) : json(nullptr);
    }
    case Kind::Font: {
        auto h = *static_cast<const FontHandle*>(p);
        const char* path = ctx.assets ? ctx.assets->pathOf(h) : nullptr;
        if (!path) return nullptr;
        return json{{"path", path}, {"size", ctx.assets->fontSize(h)}};
    }
    case Kind::Entity: {
        EntityId e = *static_cast<const EntityId*>(p);
        if (e == InvalidEntityId || !ctx.saveEntity) return nullptr;
        return ctx.saveEntity(e);
    }
    case Kind::Struct: {
        json obj = json::object();
        for (const auto& f : t.fields) {
            obj[f.name] = toJson(*f.type, static_cast<const char*>(p) + f.offset, ctx);
        }
        return obj;
    }
    case Kind::Vector: {
        json arr = json::array();
        size_t n = t.vecSize(p);
        for (size_t i = 0; i < n; ++i) {
            arr.push_back(toJson(*t.element, t.vecAt(const_cast<void*>(p), i), ctx));
        }
        return arr;
    }
    }
    return nullptr;
}

void fromJson(const TypeInfo& t, void* p, const json& in, const JsonContext& ctx) {
    switch (t.kind) {
    case Kind::Bool:
        if (in.is_boolean()) *static_cast<bool*>(p) = in.get<bool>();
        else if (in.is_number()) *static_cast<bool*>(p) = in.get<double>() != 0.0;
        break;
    case Kind::Int8: case Kind::Int16: case Kind::Int32: case Kind::Int64:
    case Kind::UInt8: case Kind::UInt16: case Kind::UInt32: case Kind::UInt64:
        if (in.is_number_integer()) writeInt(t, p, in.get<int64_t>());
        else if (in.is_number()) writeInt(t, p, static_cast<int64_t>(in.get<double>()));
        break;
    case Kind::Float:
        if (in.is_number()) *static_cast<float*>(p) = in.get<float>();
        break;
    case Kind::Double:
        if (in.is_number()) *static_cast<double*>(p) = in.get<double>();
        break;
    case Kind::Vec2: {
        auto& v = *static_cast<Vec2*>(p);
        v = {num(in, 0, v.x), num(in, 1, v.y)};
        break;
    }
    case Kind::Vec4: {
        auto& v = *static_cast<Vec4*>(p);
        v = {num(in, 0, v.x), num(in, 1, v.y), num(in, 2, v.z), num(in, 3, v.w)};
        break;
    }
    case Kind::Rect: {
        auto& r = *static_cast<Rect*>(p);
        r = {num(in, 0, r.x), num(in, 1, r.y), num(in, 2, r.w), num(in, 3, r.h)};
        break;
    }
    case Kind::Color: {
        auto& c = *static_cast<Color*>(p);
        auto ch = [&](size_t i, uint8_t d) {
            float f = num(in, i, d);
            return static_cast<uint8_t>(f < 0.f ? 0.f : (f > 255.f ? 255.f : f));
        };
        c = {ch(0, c.r), ch(1, c.g), ch(2, c.b), ch(3, c.a)};
        break;
    }
    case Kind::ColorF: {
        auto& c = *static_cast<ColorF*>(p);
        c = {num(in, 0, c.r), num(in, 1, c.g), num(in, 2, c.b), num(in, 3, c.a)};
        break;
    }
    case Kind::Enum:
        if (in.is_string()) {
            const std::string s = in.get<std::string>();
            bool found = false;
            for (const auto& ev : t.enumValues) {
                if (ev.name == s) { writeInt(t, p, ev.value); found = true; break; }
            }
            if (!found) DRIFT_LOG_WARN("reflect: '%s' is not a value of %s", s.c_str(), t.name.c_str());
        } else if (in.is_number_integer()) {
            writeInt(t, p, in.get<int64_t>());
        }
        break;
    case Kind::Chars:
        if (in.is_string() && t.size > 0) {
            const std::string s = in.get<std::string>();
            char* dst = static_cast<char*>(p);
            std::memset(dst, 0, t.size);
            std::memcpy(dst, s.data(), std::min(s.size(), t.size - 1));
        }
        break;
    case Kind::String:
        if (in.is_string()) *static_cast<std::string*>(p) = in.get<std::string>();
        break;
    case Kind::Texture:
        if (in.is_null()) *static_cast<TextureHandle*>(p) = {};
        else if (in.is_string() && ctx.assets)
            *static_cast<TextureHandle*>(p) = ctx.assets->loadTexture(in.get<std::string>().c_str());
        break;
    case Kind::Sound:
        if (in.is_null()) *static_cast<SoundHandle*>(p) = {};
        else if (in.is_string() && ctx.assets)
            *static_cast<SoundHandle*>(p) = ctx.assets->loadSound(in.get<std::string>().c_str());
        break;
    case Kind::Font:
        if (in.is_null()) *static_cast<FontHandle*>(p) = {};
        else if (in.is_object() && in.contains("path") && ctx.assets) {
            int size = in.value("size", 16);
            *static_cast<FontHandle*>(p) =
                ctx.assets->loadFont(in["path"].get<std::string>().c_str(), size);
        }
        break;
    case Kind::Entity:
        if (in.is_null()) *static_cast<EntityId*>(p) = InvalidEntityId;
        else *static_cast<EntityId*>(p) = ctx.loadEntity ? ctx.loadEntity(in) : InvalidEntityId;
        break;
    case Kind::Struct:
        if (!in.is_object()) break;
        for (const auto& f : t.fields) {
            auto it = in.find(f.name);
            if (it != in.end()) fromJson(*f.type, static_cast<char*>(p) + f.offset, *it, ctx);
        }
        break;
    case Kind::Vector:
        if (!in.is_array()) break;
        t.vecResize(p, in.size());
        for (size_t i = 0; i < in.size(); ++i) {
            fromJson(*t.element, t.vecAt(p, i), in[i], ctx);
        }
        break;
    }
}

} // namespace drift::reflect
