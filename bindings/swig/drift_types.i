// =============================================================================
// drift_types.i - Typemaps for Vec2, Rect, Color, ColorF, Handle types
// =============================================================================
// Provides SWIG wrapping for all Drift value types, handle templates, and
// enums. Handle<T> is instantiated for each concrete tag type so C# gets
// typed handle structs with valid()/raw() accessors.
// =============================================================================

%{
#include <drift/Types.hpp>
#include <drift/Handle.hpp>
#include <drift/Math.hpp>
#include <drift/Config.hpp>
%}

// ---------------------------------------------------------------------------
// Tell SWIG about stdint types
// ---------------------------------------------------------------------------
%include <stdint.i>

// ---------------------------------------------------------------------------
// Ignore non-SWIG-friendly items
// ---------------------------------------------------------------------------

// Ignore free-standing operator overloads (SWIG can't wrap them directly)
%ignore drift::operator*(float, drift::Vec2);
%ignore drift::operator|(drift::Flip, drift::Flip);
%ignore drift::operator&(drift::Flip, drift::Flip);
%ignore drift::operator|(drift::PanelFlags, drift::PanelFlags);
%ignore drift::operator&(drift::PanelFlags, drift::PanelFlags);

// Ignore constexpr static factory methods (SWIG has trouble with constexpr)
%ignore drift::Vec2::zero;
%ignore drift::Vec2::one;
%ignore drift::Color::white;
%ignore drift::Color::black;
%ignore drift::Color::red;
%ignore drift::Color::green;
%ignore drift::Color::blue;

// ---------------------------------------------------------------------------
// Vec2 - expose as a value type with public fields
// ---------------------------------------------------------------------------
%rename(Add) drift::Vec2::operator+;
%rename(Subtract) drift::Vec2::operator-(Vec2) const;
%rename(Multiply) drift::Vec2::operator*(float) const;
%rename(AddAssign) drift::Vec2::operator+=;
%rename(SubtractAssign) drift::Vec2::operator-=;
%rename(MultiplyAssign) drift::Vec2::operator*=;

// Provide static factory methods as regular methods via %extend
%extend drift::Vec2 {
    static drift::Vec2 Zero() { return drift::Vec2(0.f, 0.f); }
    static drift::Vec2 One() { return drift::Vec2(1.f, 1.f); }

    std::string ToString() {
        char buf[64];
        snprintf(buf, sizeof(buf), "Vec2(%.3f, %.3f)", $self->x, $self->y);
        return std::string(buf);
    }
}

// ---------------------------------------------------------------------------
// Rect
// ---------------------------------------------------------------------------
%extend drift::Rect {
    std::string ToString() {
        char buf[96];
        snprintf(buf, sizeof(buf), "Rect(%.1f, %.1f, %.1f, %.1f)",
                 $self->x, $self->y, $self->w, $self->h);
        return std::string(buf);
    }
}

// ---------------------------------------------------------------------------
// Color - provide static factories via %extend
// ---------------------------------------------------------------------------
%extend drift::Color {
    static drift::Color White() { return drift::Color(255, 255, 255, 255); }
    static drift::Color Black() { return drift::Color(0, 0, 0, 255); }
    static drift::Color Red()   { return drift::Color(255, 0, 0, 255); }
    static drift::Color Green() { return drift::Color(0, 255, 0, 255); }
    static drift::Color Blue()  { return drift::Color(0, 0, 255, 255); }

    std::string ToString() {
        char buf[64];
        snprintf(buf, sizeof(buf), "Color(%u, %u, %u, %u)",
                 (unsigned)$self->r, (unsigned)$self->g,
                 (unsigned)$self->b, (unsigned)$self->a);
        return std::string(buf);
    }
}

// ---------------------------------------------------------------------------
// ColorF
// ---------------------------------------------------------------------------
%extend drift::ColorF {
    std::string ToString() {
        char buf[96];
        snprintf(buf, sizeof(buf), "ColorF(%.3f, %.3f, %.3f, %.3f)",
                 $self->r, $self->g, $self->b, $self->a);
        return std::string(buf);
    }
}

// ---------------------------------------------------------------------------
// Handle<T> template
// ---------------------------------------------------------------------------
// Ignore operator bool (SWIG doesn't handle explicit operator bool well)
%ignore drift::Handle::operator bool;

// Ignore the static make() method (internal use only)
%ignore drift::Handle::make;

// Rename comparison operators
%rename(Equals) drift::Handle::operator==;
%rename(NotEquals) drift::Handle::operator!=;

// Provide a raw() accessor and a proper ToString
%extend drift::Handle {
    uint32_t raw() const { return $self->id; }

    std::string ToString() {
        char buf[32];
        snprintf(buf, sizeof(buf), "Handle(%u)", $self->id);
        return std::string(buf);
    }
}

// ---------------------------------------------------------------------------
// Parse the actual headers so SWIG sees the struct layouts
// ---------------------------------------------------------------------------
%include "drift/Types.hpp"
%include "drift/Handle.hpp"
%include "drift/Math.hpp"
%include "drift/Config.hpp"

// ---------------------------------------------------------------------------
// Instantiate Handle<T> for each concrete tag type
// ---------------------------------------------------------------------------
%template(TextureHandle)     drift::Handle<drift::TextureTag>;
%template(SpritesheetHandle) drift::Handle<drift::SpritesheetTag>;
%template(AnimationHandle)   drift::Handle<drift::AnimationTag>;
%template(SoundHandle)       drift::Handle<drift::SoundTag>;
%template(PlayingSoundHandle) drift::Handle<drift::PlayingSoundTag>;
%template(FontHandle)        drift::Handle<drift::FontTag>;
%template(TilemapHandle)     drift::Handle<drift::TilemapTag>;
%template(EmitterHandle)     drift::Handle<drift::EmitterTag>;
%template(CameraHandle)      drift::Handle<drift::CameraTag>;

// ---------------------------------------------------------------------------
// const char* typemaps: map to C# string (auto-marshal)
// ---------------------------------------------------------------------------
// SWIG's default char* typemaps handle this, but ensure const char* return
// values are treated as strings (not IntPtr).
%typemap(cstype) const char* "string"
%typemap(imtype) const char* "string"

// ---------------------------------------------------------------------------
// void* typemaps: map to System.IntPtr for opaque pointers
// ---------------------------------------------------------------------------
%typemap(cstype)  void* "System.IntPtr"
%typemap(imtype)  void* "global::System.IntPtr"
%typemap(ctype)   void* "void *"
%typemap(in)      void* %{ $1 = $input; %}
%typemap(out)     void* %{ $result = $1; %}
%typemap(csin)    void* "$csinput"
%typemap(csout)   void* "{ return $imcall; }"

// ---------------------------------------------------------------------------
// Ignore std::vector<AccessDescriptor> return (complex C++ type)
// We provide a simplified interface for getDependencies via %extend if needed.
// ---------------------------------------------------------------------------
%ignore drift::SystemBase::getDependencies;
%ignore drift::AccessDescriptor;
