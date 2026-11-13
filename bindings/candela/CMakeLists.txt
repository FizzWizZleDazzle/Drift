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
# drift module are packaged into the page's file system. Every script in
# the folder keeps its path relative to the folder, so imports such as
# "./src/player.cdl" find it. Hidden folders, build output folders (build,
# build-*) and the asset folder, which is packaged whole, are skipped.
function(drift_candela_web_game target)
    cmake_parse_arguments(ARG "" "PROJECT" "" ${ARGN})
    get_filename_component(project ${ARG_PROJECT} ABSOLUTE)
    drift_candela_program(${target})
    drift_project_value(${project}/drift.toml entry entry "main.cdl")
    drift_project_value(${project}/drift.toml assets assets_dir "assets")
    file(GLOB_RECURSE found CONFIGURE_DEPENDS RELATIVE ${project} ${project}/*.cdl)
    set(scripts "")
    set(preload "")
    foreach(rel IN LISTS found)
        if(rel MATCHES "(^|/)(\\.[^/]*|build|build-[^/]*)/" OR rel MATCHES "^${assets_dir}/")
            continue()
        endif()
        set(script ${project}/${rel})
        string(FIND ${script} ${CMAKE_BINARY_DIR}/ in_binary_dir)
        if(in_binary_dir EQUAL 0)
            continue()
        endif()
        list(APPEND scripts ${script})
        list(APPEND preload ${script}@/${rel})
    endforeach()
    list(APPEND preload ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/drift@/drift-module)
    drift_web_app(${target} PROJECT ${project} PRELOAD ${preload})
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${scripts}
        ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/drift/src/main.cdl)
endfunction()

# Game folders outside this tree to build as web pages too, each as the
# target play_<folder name>: -DDRIFT_CANDELA_WEB_GAMES="/path/to/game;...".
set(DRIFT_CANDELA_WEB_GAMES "" CACHE STRING "Candela game folders to build as web pages")

if(EMSCRIPTEN)
    drift_candela_web_game(flappy_bird_cdl PROJECT ${CMAKE_CURRENT_SOURCE_DIR}/examples/flappy_bird)
    drift_candela_web_game(coin_dash_cdl PROJECT ${CMAKE_CURRENT_SOURCE_DIR}/examples/coin_dash)
    foreach(game IN LISTS DRIFT_CANDELA_WEB_GAMES)
        get_filename_component(name ${game} NAME)
        string(MAKE_C_IDENTIFIER ${name} name)
        if(TARGET play_${name})
            message(FATAL_ERROR "DRIFT_CANDELA_WEB_GAMES: two games would build as play_${name}")
        endif()
        drift_candela_web_game(play_${name} PROJECT ${game})
    endforeach()
    return()
endif()

drift_candela_program(drift_run)
set_target_properties(drift_run PROPERTIES OUTPUT_NAME drift)
