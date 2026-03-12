// =============================================================================
// drift.i - Master SWIG interface for the Drift 2D game engine
// =============================================================================
// Generates C# bindings (P/Invoke + proxy classes) for the Drift engine.
// Enables C# subclassing of Plugin, PluginGroup, SystemBase, Script, and
// EventHandler via SWIG directors.
//
// Usage:
//   swig -c++ -csharp -namespace Drift -outdir generated/csharp \
//        -I<path-to>/include -o drift_wrap.cxx drift.i
//
// The generated files:
//   drift_wrap.cxx          - C++ wrapper compiled into the native library
//   generated/csharp/*.cs   - C# proxy classes for use in .NET projects
// =============================================================================

%module(directors="1") drift

// ---------------------------------------------------------------------------
// Target language: C#
// ---------------------------------------------------------------------------
#ifdef SWIGCSHARP

// Use the Drift namespace for all generated C# classes
%pragma(csharp) moduleimports=%{
using System;
using System.Runtime.InteropServices;
%}

// Name of the native shared library to P/Invoke into
%pragma(csharp) imclassimports=%{
using System;
using System.Runtime.InteropServices;
%}

#endif // SWIGCSHARP

// ---------------------------------------------------------------------------
// Standard SWIG library includes
// ---------------------------------------------------------------------------
%include <std_string.i>
%include <stdint.i>

// ---------------------------------------------------------------------------
// Global configuration
// ---------------------------------------------------------------------------

// Enable directors (C++ -> C# virtual dispatch) module-wide
%feature("director");

// Suppress SWIG warnings for director classes with non-public destructors
#pragma SWIG nowarn=473

// ---------------------------------------------------------------------------
// Include sub-interface files in dependency order
// ---------------------------------------------------------------------------

// 1. C# operator overloads and language extensions (typemap(cscode) blocks).
//    Must come BEFORE the headers are parsed so the typemaps are in place.
%include "csharp_ext.i"

// 2. Value types, handle templates, enums, and typemaps.
//    Parses Types.h, Handle.h, Math.h, Config.h.
%include "drift_types.i"

// 3. System/Plugin/Resource/Script directors and all class bindings.
//    Parses Resource.h, Plugin.h, System.h, App.h, World.h, Entity.h,
//    Script.h, all resource headers, and DefaultPlugins.h.
%include "drift_system.i"
