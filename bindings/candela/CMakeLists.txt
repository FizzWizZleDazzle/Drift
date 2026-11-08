# Candela games for Drift.
#
# The `drift` program embeds candela: host/ is a Rust library that compiles
# a game against the `drift` module (drift/) and runs its systems, and
# runner/drift.cpp is the engine side. On the web the same program builds
# once per game, with the game's files packaged in.
#
# DRIFT_CANDELA_SOURCE points Cargo at a candela checkout instead of the
# crates.io release, for candela changes not released yet.

set(DRIFT_CANDELA_SOURCE "" CACHE PATH "candela source checkout to build the host against (empty: crates.io)")
find_program(DRIFT_CARGO cargo HINTS $ENV{HOME}/.cargo/bin REQUIRED)

if(EMSCRIPTEN)
    set(_rust_target wasm32-unknown-emscripten)
else()
    set(_rust_target "")
endif()
set(_cargo_dir ${CMAKE_CURRENT_BINARY_DIR}/cargo)
set(_cargo_args build --release --manifest-path ${CMAKE_CURRENT_SOURCE_DIR}/host/Cargo.toml
    --target-dir ${_cargo_dir})
if(_rust_target)
    list(APPEND _cargo_args --target ${_rust_target})
    set(_host_lib ${_cargo_dir}/${_rust_target}/release/${CMAKE_STATIC_LIBRARY_PREFIX}drift_candela_host${CMAKE_STATIC_LIBRARY_SUFFIX})
else()
    set(_host_lib ${_cargo_dir}/release/${CMAKE_STATIC_LIBRARY_PREFIX}drift_candela_host${CMAKE_STATIC_LIBRARY_SUFFIX})
endif()
if(DRIFT_CANDELA_SOURCE)
    list(APPEND _cargo_args
        --config "patch.crates-io.candela-lang.path=\"${DRIFT_CANDELA_SOURCE}\""
        --config "patch.crates-io.candela-vm.path=\"${DRIFT_CANDELA_SOURCE}/vm\"")
endif()
file(GLOB _host_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/host/src/*.rs)
add_custom_command(
    OUTPUT ${_host_lib}
    COMMAND ${DRIFT_CARGO} ${_cargo_args}
    DEPENDS ${_host_sources} ${CMAKE_CURRENT_SOURCE_DIR}/host/Cargo.toml
    COMMENT "Building the Candela host (cargo)"
    VERBATIM)
add_custom_target(drift_candela_host_build DEPENDS ${_host_lib})
add_library(drift_candela_host STATIC IMPORTED GLOBAL)
set_target_properties(drift_candela_host PROPERTIES IMPORTED_LOCATION ${_host_lib})
add_dependencies(drift_candela_host drift_candela_host_build)

# One `drift` executable per target; links the engine and the host.
function(drift_candela_program target)
    add_executable(${target} ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/runner/drift.cpp)
    target_link_libraries(${target} PRIVATE drift drift_candela_host)
    target_compile_definitions(${target} PRIVATE
        DRIFT_CANDELA_MODULE_DIR="${CMAKE_CURRENT_FUNCTION_LIST_DIR}/drift")
    if(EMSCRIPTEN)
        # Rust unwinds with WebAssembly exceptions on this target.
        target_link_options(${target} PRIVATE -fwasm-exceptions)
    elseif(UNIX)
        target_link_libraries(${target} PRIVATE dl pthread m)
    endif()
endfunction()

# drift_candela_web_game(<target> PROJECT <dir>): a Candela game as a web
# page. The game's folder (drift.toml, its scripts, its assets) and the
# drift module are packaged into the page's file system.
function(drift_candela_web_game target)
    cmake_parse_arguments(ARG "" "PROJECT" "" ${ARGN})
    get_filename_component(project ${ARG_PROJECT} ABSOLUTE)
    drift_candela_program(${target})
    drift_project_value(${project}/drift.toml entry entry "main.cdl")
    file(GLOB scripts CONFIGURE_DEPENDS ${project}/*.cdl)
    set(preload "")
    foreach(script IN LISTS scripts)
        get_filename_component(n ${script} NAME)
        list(APPEND preload ${script}@/${n})
    endforeach()
    list(APPEND preload ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/drift@/drift-module)
    drift_web_app(${target} PROJECT ${project} PRELOAD ${preload})
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${scripts}
        ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/drift/src/main.cdl)
endfunction()

if(EMSCRIPTEN)
    drift_candela_web_game(flappy_bird_cdl PROJECT ${CMAKE_CURRENT_SOURCE_DIR}/examples/flappy_bird)
    return()
endif()

drift_candela_program(drift_run)
set_target_properties(drift_run PROPERTIES OUTPUT_NAME drift)

# ---- The candela `drift` package of the dylib binding ----
# The C shim over the engine that `candela run` games load. Coin Dash and the
# tests below still use it.

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
    # The same checks with the simulation run after the systems.
    add_test(NAME candela_test_drift_sequential
        COMMAND ${DRIFT_CANDELA_EXE} run tests/test_drift.cdl ${CMAKE_CURRENT_BINARY_DIR} 0
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(candela_test_drift_sequential PROPERTIES
        ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen;SDL_AUDIO_DRIVER=dummy"
        TIMEOUT 180)
endif()
