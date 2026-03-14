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
    explicit Res(const T* p) : ptr_(p) {}
    const T& operator*() const { assert(ptr_ && "Res<T>: null resource access"); return *ptr_; }
    const T* operator->() const { assert(ptr_ && "Res<T>: null resource access"); return ptr_; }
    const T* get() const { return ptr_; }

private:
    const T* ptr_;
};

// ResMut<T> = read-write resource access. Used as system parameter.
template<typename T>
struct ResMut {
    explicit ResMut(T* p) : ptr_(p) {}
    T& operator*() { assert(ptr_ && "ResMut<T>: null resource access"); return *ptr_; }
    T* operator->() { assert(ptr_ && "ResMut<T>: null resource access"); return ptr_; }
    const T& operator*() const { assert(ptr_ && "ResMut<T>: null resource access"); return *ptr_; }
    const T* operator->() const { assert(ptr_ && "ResMut<T>: null resource access"); return ptr_; }
    T* get() { return ptr_; }
    const T* get() const { return ptr_; }

private:
    T* ptr_;
};
#endif

} // namespace drift
