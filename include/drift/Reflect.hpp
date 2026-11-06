#pragma once

// Component reflection: a type describes its name and fields once, and tools
// (scene files, prefabs, the dev inspector) read and write it generically.
//
//   struct Health { float hp = 100.f; float max = 100.f; };
//   reflect::component<Health>(app.world(), "Health")
//       .field("hp", &Health::hp)
//       .field("max", &Health::max);

#include <drift/Types.hpp>
#include <drift/Handle.hpp>
#include <drift/World.hpp>

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <new>
#include <string>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <vector>

namespace drift {

class AssetServer;

namespace reflect {

enum class Kind : uint8_t {
    Bool,
    Int8, Int16, Int32, Int64,
    UInt8, UInt16, UInt32, UInt64,
    Float, Double,
    Vec2, Vec4, Rect, Color, ColorF,
    Enum,       // integer-backed, values listed by name
    Chars,      // fixed char array (char[N]), NUL terminated
    String,     // std::string
    Texture,    // TextureHandle, saved as the file path it was loaded from
    Font,       // FontHandle, saved as path and pixel size
    Sound,      // SoundHandle, saved as path
    Entity,     // EntityId reference, remapped when scenes load
    Struct,     // nested reflected struct
    Vector,     // std::vector of a reflected type
};

struct TypeInfo;

struct Field {
    std::string name;
    size_t offset = 0;
    const TypeInfo* type = nullptr;
};

struct EnumValue {
    std::string name;
    int64_t value = 0;
};

struct TypeInfo {
    std::string name;
    Kind kind = Kind::Struct;
    size_t size = 0;
    size_t align = 0;

    std::vector<Field> fields;            // Struct
    std::vector<EnumValue> enumValues;    // Enum
    const TypeInfo* element = nullptr;    // Vector

    // Lifecycle on raw storage of `size` bytes.
    void (*construct)(void* p) = nullptr;
    void (*destroy)(void* p) = nullptr;
    void (*copy)(void* dst, const void* src) = nullptr;   // dst already constructed

    // Vector access
    size_t (*vecSize)(const void* v) = nullptr;
    void* (*vecAt)(void* v, size_t i) = nullptr;
    void (*vecResize)(void* v, size_t n) = nullptr;
};

// A reflected component: its type plus the name it is saved under.
struct ComponentInfo {
    std::string name;
    std::type_index type = std::type_index(typeid(void));
    const TypeInfo* info = nullptr;

    // Id of this component in `world`, 0 if the world never registered it.
    ComponentId id(const World& world) const;
};

// ---- Registry ----

// Every reflected component, in registration order.
const std::vector<const ComponentInfo*>& components();
const ComponentInfo* findComponent(const std::string& name);
const ComponentInfo* findComponent(std::type_index type);
const TypeInfo* findType(std::type_index type);

// ---- Values ----

// Owned storage for one value of a reflected type, default constructed (or
// copied from `source`) and destroyed with the type's own lifecycle.
class Value {
public:
    explicit Value(const TypeInfo& type, const void* source = nullptr);
    ~Value();
    Value(const Value&) = delete;
    Value& operator=(const Value&) = delete;

    void* get() { return mem_; }
    const void* get() const { return mem_; }
    const TypeInfo& type() const { return type_; }

private:
    const TypeInfo& type_;
    void* mem_ = nullptr;
};

int64_t readInt(const TypeInfo& t, const void* p);       // Enum and integer kinds
void writeInt(const TypeInfo& t, void* p, int64_t v);
const char* enumName(const TypeInfo& t, int64_t value);  // nullptr if not listed

// ---- JSON ----

// Hooks for values that depend on context: asset paths and entity references.
struct JsonContext {
    AssetServer* assets = nullptr;
    // Entity -> JSON (default: null). Scenes write their local ids here.
    std::function<nlohmann::json(EntityId)> saveEntity;
    // JSON -> entity (default: InvalidEntityId).
    std::function<EntityId(const nlohmann::json&)> loadEntity;
};

// Missing fields in `in` keep their current value, so partial JSON works.
nlohmann::json toJson(const TypeInfo& type, const void* value, const JsonContext& ctx);
void fromJson(const TypeInfo& type, void* value, const nlohmann::json& in, const JsonContext& ctx);

// ---- Internals used by the templates below ----

namespace detail {

TypeInfo* typeSlot(std::type_index type, const char* name, Kind kind, bool reset);
const TypeInfo* builtin(Kind kind);
void addComponent(const char* name, std::type_index type, const TypeInfo* info);

template<typename T>
void setLifecycle(TypeInfo& t) {
    t.size = sizeof(T);
    t.align = alignof(T);
    t.construct = [](void* p) { new (p) T(); };
    t.destroy = [](void* p) { static_cast<T*>(p)->~T(); };
    t.copy = [](void* d, const void* s) { *static_cast<T*>(d) = *static_cast<const T*>(s); };
}

template<typename T> struct IsVector : std::false_type {};
template<typename U, typename A> struct IsVector<std::vector<U, A>> : std::true_type { using Elem = U; };

template<typename T> struct IsCharArray : std::false_type {};
template<size_t N> struct IsCharArray<char[N]> : std::true_type {};

} // namespace detail

// Type description for T, or nullptr when T has not been described.
// Primitives, math types, handles and std::vector of described types are
// built in; enums and structs come from enumType() / type().
template<typename T>
const TypeInfo* typeOf() {
    using detail::builtin;
    if constexpr (std::is_same_v<T, bool>) return builtin(Kind::Bool);
    else if constexpr (std::is_same_v<T, int8_t>) return builtin(Kind::Int8);
    else if constexpr (std::is_same_v<T, int16_t>) return builtin(Kind::Int16);
    else if constexpr (std::is_same_v<T, int32_t>) return builtin(Kind::Int32);
    else if constexpr (std::is_same_v<T, int64_t>) return builtin(Kind::Int64);
    else if constexpr (std::is_same_v<T, uint8_t>) return builtin(Kind::UInt8);
    else if constexpr (std::is_same_v<T, uint16_t>) return builtin(Kind::UInt16);
    else if constexpr (std::is_same_v<T, uint32_t>) return builtin(Kind::UInt32);
    else if constexpr (std::is_same_v<T, uint64_t>) return builtin(Kind::UInt64);
    else if constexpr (std::is_same_v<T, float>) return builtin(Kind::Float);
    else if constexpr (std::is_same_v<T, double>) return builtin(Kind::Double);
    else if constexpr (std::is_same_v<T, Vec2>) return builtin(Kind::Vec2);
    else if constexpr (std::is_same_v<T, Vec4>) return builtin(Kind::Vec4);
    else if constexpr (std::is_same_v<T, Rect>) return builtin(Kind::Rect);
    else if constexpr (std::is_same_v<T, Color>) return builtin(Kind::Color);
    else if constexpr (std::is_same_v<T, ColorF>) return builtin(Kind::ColorF);
    else if constexpr (std::is_same_v<T, std::string>) return builtin(Kind::String);
    else if constexpr (std::is_same_v<T, TextureHandle>) return builtin(Kind::Texture);
    else if constexpr (std::is_same_v<T, FontHandle>) return builtin(Kind::Font);
    else if constexpr (std::is_same_v<T, SoundHandle>) return builtin(Kind::Sound);
    else if constexpr (detail::IsCharArray<T>::value) {
        TypeInfo* t = detail::typeSlot(std::type_index(typeid(T)), "chars", Kind::Chars, false);
        t->size = sizeof(T);
        t->align = 1;
        t->construct = [](void* p) { std::memset(p, 0, sizeof(T)); };
        t->destroy = [](void*) {};
        t->copy = [](void* d, const void* s) { std::memcpy(d, s, sizeof(T)); };
        return t;
    } else if constexpr (detail::IsVector<T>::value) {
        using Elem = typename detail::IsVector<T>::Elem;
        const TypeInfo* elem = typeOf<Elem>();
        if (!elem) return nullptr;
        TypeInfo* t = detail::typeSlot(std::type_index(typeid(T)), "vector", Kind::Vector, false);
        detail::setLifecycle<T>(*t);
        t->element = elem;
        t->vecSize = [](const void* v) { return static_cast<const T*>(v)->size(); };
        t->vecAt = [](void* v, size_t i) -> void* { return &(*static_cast<T*>(v))[i]; };
        t->vecResize = [](void* v, size_t n) { static_cast<T*>(v)->resize(n); };
        return t;
    } else {
        return findType(std::type_index(typeid(T)));
    }
}

// Builder for a struct description. Fields whose type is not described are
// skipped with an error in the log.
template<typename T>
class TypeBuilder {
public:
    explicit TypeBuilder(TypeInfo* info) : info_(info) {}

    template<typename M>
    TypeBuilder& field(const char* name, M T::* member) {
        return add(name, offsetOf(member), typeOf<M>());
    }

    // An EntityId field that refers to another entity (remapped on load).
    TypeBuilder& entity(const char* name, EntityId T::* member) {
        return add(name, offsetOf(member), detail::builtin(Kind::Entity));
    }

    const TypeInfo* info() const { return info_; }

private:
    template<typename M>
    static size_t offsetOf(M T::* member) {
        alignas(T) unsigned char storage[sizeof(T)];
        T* obj = new (storage) T();
        size_t off = static_cast<size_t>(
            reinterpret_cast<unsigned char*>(&(obj->*member)) - storage);
        obj->~T();
        return off;
    }

    TypeBuilder& add(const char* name, size_t offset, const TypeInfo* type);

    TypeInfo* info_;
};

void reportMissingFieldType(const TypeInfo& owner, const char* field);

template<typename T>
TypeBuilder<T>& TypeBuilder<T>::add(const char* name, size_t offset, const TypeInfo* type) {
    if (!type) {
        reportMissingFieldType(*info_, name);
        return *this;
    }
    info_->fields.push_back(Field{name, offset, type});
    return *this;
}

// Describe a plain struct (a nested value inside components). Calling it
// again for the same type replaces the earlier description.
template<typename T>
TypeBuilder<T> type(const char* name) {
    static_assert(std::is_default_constructible_v<T>, "reflected types need a default constructor");
    TypeInfo* t = detail::typeSlot(std::type_index(typeid(T)), name, Kind::Struct, true);
    detail::setLifecycle<T>(*t);
    return TypeBuilder<T>(t);
}

// Describe an enum by listing its values.
template<typename E>
void enumType(const char* name, std::initializer_list<std::pair<const char*, E>> values) {
    static_assert(std::is_enum_v<E>, "enumType needs an enum");
    TypeInfo* t = detail::typeSlot(std::type_index(typeid(E)), name, Kind::Enum, true);
    detail::setLifecycle<E>(*t);
    for (const auto& v : values) {
        t->enumValues.push_back(EnumValue{v.first, static_cast<int64_t>(v.second)});
    }
}

// Describe a component and register it with `world` under `name` (if the
// world does not know it yet). Scenes save it under this name and the
// inspector lists it.
template<typename T>
TypeBuilder<T> component(World& world, const char* name) {
    if (!world.componentRegistry().has<T>()) {
        world.registerComponent<T>(name);
    } else {
        world.componentRegistry().addByName(name, world.componentRegistry().get<T>());
    }
    TypeBuilder<T> b = type<T>(name);
    detail::addComponent(name, std::type_index(typeid(T)), b.info());
    return b;
}

} // namespace reflect
} // namespace drift
