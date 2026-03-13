#pragma once

namespace drift {

class App;

// Base class for all subsystem resources (singletons stored in App).
class Resource {
public:
    virtual ~Resource() = default;
    virtual const char* name() const = 0;
};

#ifndef SWIG
// Res<T> = read-only resource access. Used as system parameter.
template<typename T>
struct Res {
    const T* ptr;
    explicit Res(const T* p) : ptr(p) {}
    const T& operator*() const { return *ptr; }
    const T* operator->() const { return ptr; }
};

// ResMut<T> = read-write resource access. Used as system parameter.
template<typename T>
struct ResMut {
    T* ptr;
    explicit ResMut(T* p) : ptr(p) {}
    T& operator*() { return *ptr; }
    T* operator->() { return ptr; }
    const T& operator*() const { return *ptr; }
    const T* operator->() const { return ptr; }
};
#endif

} // namespace drift
