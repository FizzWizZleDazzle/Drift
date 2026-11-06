# The candela `drift` package's native library: a C shim over the engine,
# with Drift linked in statically. It is written into dist/ beside the
# package's candela sources, where src/main.cdl's dylib block finds it.

if(EMSCRIPTEN)
    message(FATAL_ERROR "DRIFT_BUILD_CANDELA: candela loads native libraries, which the web build cannot provide")
endif()

add_library(drift_candela SHARED shim/drift_candela.cpp)
target_link_libraries(drift_candela PRIVATE drift)

# The same file name on every platform (libdrift_candela.so / .dylib /
# .dll), so one dylib path in the candela sources resolves everywhere.
set(DRIFT_CANDELA_DIST ${CMAKE_CURRENT_SOURCE_DIR}/dist)
set_target_properties(drift_candela PROPERTIES
    PREFIX "lib"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    LIBRARY_OUTPUT_DIRECTORY ${DRIFT_CANDELA_DIST}$<0:>
    RUNTIME_OUTPUT_DIRECTORY ${DRIFT_CANDELA_DIST}$<0:>
)

# Only the dc_ entry points are exported; the engine and its dependencies
# stay internal to the library.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(drift_candela PRIVATE -Wl,--exclude-libs,ALL
        -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/shim/exports.map)
    set_property(TARGET drift_candela APPEND PROPERTY LINK_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/shim/exports.map)
endif()

# The candela tests drive the library headlessly through SDL's offscreen
# video driver. They need the candela toolchain on PATH or in ~/.candela.
find_program(DRIFT_CANDELA_EXE candela HINTS $ENV{HOME}/.candela)
if(DRIFT_BUILD_TESTS AND DRIFT_CANDELA_EXE)
    foreach(_t test_drift test_lighting play_coin_dash)
        add_test(NAME candela_${_t}
            COMMAND ${DRIFT_CANDELA_EXE} run tests/${_t}.cdl ${CMAKE_CURRENT_BINARY_DIR}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(candela_${_t} PROPERTIES
            ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen;SDL_AUDIO_DRIVER=dummy"
            TIMEOUT 180)
    endforeach()
endif()
