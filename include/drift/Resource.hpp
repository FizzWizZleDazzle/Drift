#pragma once

#include <cassert>

namespace drift {

class App;

// Base class for all subsystem resources (singletons stored in App).
class Resource {
public:
    virtual ~Resource() = default;
    virtual const char* name() const { return ""; }
};

// Macro for engine resources that need SWIG-visible names.
#define DRIFT_RESOURCE(Name) const char* name() const override { return #Name; }

#ifndef SWIG
// Res<T> = read-only resource access. Used as system parameter.
template<typename T>
struct Res {
    const T* ptr;
    explicit Res(const T* p) : ptr(p) {}
    const T& operator*() const { assert(ptr && "Res<T>: null resource access"); return *ptr; }
    const T* operator->() const { assert(ptr && "Res<T>: null resource access"); return ptr; }
};

// ResMut<T> = read-write resource access. Used as system parameter.
template<typename T>
struct ResMut {
    T* ptr;
    explicit ResMut(T* p) : ptr(p) {}
    T& operator*() { assert(ptr && "ResMut<T>: null resource access"); return *ptr; }
    T* operator->() { assert(ptr && "ResMut<T>: null resource access"); return ptr; }
    const T& operator*() const { assert(ptr && "ResMut<T>: null resource access"); return *ptr; }
    const T* operator->() const { assert(ptr && "ResMut<T>: null resource access"); return ptr; }
};
#endif

} // namespace drift
