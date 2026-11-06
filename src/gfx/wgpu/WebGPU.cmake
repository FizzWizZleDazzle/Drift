# WebGPU renderer backend. Included from the graphics backend block of the
# top-level CMakeLists.txt when DRIFT_BACKEND_WEBGPU is ON.
#
# Desktop: prebuilt wgpu-native release (static library), pinned below.
# Web:     Emscripten's emdawnwebgpu port.

list(APPEND DRIFT_GFX_BACKENDS webgpu)
list(APPEND DRIFT_GFX_BACKEND_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/WebGPUDevice.cpp
    ${CMAKE_CURRENT_LIST_DIR}/WebGPUSurface.cpp
)

add_library(drift_webgpu INTERFACE)

if(EMSCRIPTEN)
    # ASYNCIFY lets device creation, shader validation and readPixels wait
    # for the browser's asynchronous WebGPU callbacks.
    target_compile_options(drift_webgpu INTERFACE --use-port=emdawnwebgpu)
    target_link_options(drift_webgpu INTERFACE --use-port=emdawnwebgpu -sASYNCIFY)
else()
    set(DRIFT_WGPU_NATIVE_VERSION "v29.0.1.1")

    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _wgpu_cpu)
    if(APPLE AND CMAKE_OSX_ARCHITECTURES)
        list(LENGTH CMAKE_OSX_ARCHITECTURES _wgpu_arch_count)
        if(_wgpu_arch_count GREATER 1)
            message(FATAL_ERROR "DRIFT_BACKEND_WEBGPU: universal macOS builds are not supported; set one CMAKE_OSX_ARCHITECTURES value")
        endif()
        string(TOLOWER "${CMAKE_OSX_ARCHITECTURES}" _wgpu_cpu)
    endif()
    if(_wgpu_cpu MATCHES "^(x86_64|amd64)$")
        set(_wgpu_arch x86_64)
    elseif(_wgpu_cpu MATCHES "^(aarch64|arm64)$")
        set(_wgpu_arch aarch64)
    else()
        message(FATAL_ERROR "DRIFT_BACKEND_WEBGPU: no wgpu-native build for processor '${CMAKE_SYSTEM_PROCESSOR}'")
    endif()

    # SHA-256 digests published with the GitHub release.
    if(WIN32 AND MSVC)
        set(_wgpu_platform windows-${_wgpu_arch}-msvc)
        set(_wgpu_hash_x86_64  7e67d7445c42aeb85e30f88930fd8d7d83ee769e3390aeb1ada75ebf3cf78132)
        set(_wgpu_hash_aarch64 4a876421a8c1e5fe72f849b3722214280fe485cb1c56f77f8b0c82414be5b29f)
    elseif(WIN32)
        set(_wgpu_platform windows-${_wgpu_arch}-gnu)
        set(_wgpu_hash_x86_64  d471e3614733c1d4ddd61bfd19868356477d0d37bf531bf8c6cb64a7f579bd2a)
        set(_wgpu_hash_aarch64 "")
    elseif(APPLE)
        set(_wgpu_platform macos-${_wgpu_arch})
        set(_wgpu_hash_x86_64  8e2f7378548ddd0e2cf21e7d864dda46e953f0af724855a33778b85ead206d41)
        set(_wgpu_hash_aarch64 a5797a37b1adf720bcd5dcffb291edbbd5b7b14be0a3874c28e6393a655a7a3e)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(_wgpu_platform linux-${_wgpu_arch})
        set(_wgpu_hash_x86_64  95a4d90c071005a98d03eab348beaa6b07e16eb00d1dcdb9f8348f75eb97ec5a)
        set(_wgpu_hash_aarch64 015fcdf1dbae82e614a783cc38017e5399ae0927a889fe9b69c9b664bc61b47a)
    else()
        message(FATAL_ERROR "DRIFT_BACKEND_WEBGPU: no wgpu-native build for ${CMAKE_SYSTEM_NAME}")
    endif()
    set(_wgpu_hash "${_wgpu_hash_${_wgpu_arch}}")
    if(NOT _wgpu_hash)
        message(FATAL_ERROR "DRIFT_BACKEND_WEBGPU: no wgpu-native build for ${_wgpu_platform}")
    endif()

    FetchContent_Declare(
        wgpu_native
        URL      https://github.com/gfx-rs/wgpu-native/releases/download/${DRIFT_WGPU_NATIVE_VERSION}/wgpu-${_wgpu_platform}-release.zip
        URL_HASH SHA256=${_wgpu_hash}
    )
    FetchContent_MakeAvailable(wgpu_native)

    if(MSVC)
        set(_wgpu_lib ${wgpu_native_SOURCE_DIR}/lib/wgpu_native.lib)
    else()
        set(_wgpu_lib ${wgpu_native_SOURCE_DIR}/lib/libwgpu_native.a)
    endif()

    add_library(drift_wgpu_native STATIC IMPORTED)
    set_target_properties(drift_wgpu_native PROPERTIES
        IMPORTED_LOCATION ${_wgpu_lib}
        INTERFACE_INCLUDE_DIRECTORIES ${wgpu_native_SOURCE_DIR}/include
    )
    if(WIN32)
        target_link_libraries(drift_wgpu_native INTERFACE
            d3dcompiler ws2_32 userenv bcrypt ntdll opengl32 propsys runtimeobject)
    elseif(APPLE)
        target_link_libraries(drift_wgpu_native INTERFACE
            "-framework Metal" "-framework QuartzCore" "-framework CoreFoundation"
            "-framework Foundation" "-framework IOKit" "-framework IOSurface")
    else()
        target_link_libraries(drift_wgpu_native INTERFACE dl pthread m)
    endif()
    target_link_libraries(drift_webgpu INTERFACE drift_wgpu_native)
endif()

list(APPEND DRIFT_GFX_BACKEND_LIBS drift_webgpu)
