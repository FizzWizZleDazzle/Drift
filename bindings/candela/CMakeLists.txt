# Candela games for Drift.
#
# The `drift` program embeds candela: host/ is a Rust library that compiles
# a game against the `drift` module (drift/) and runs its systems, and
# runner/drift.cpp is the engine side. On the desktop it is the `drift`
# command; in a web build it is the web runtime, one drift.js/drift.wasm that
# every game page shares, loading the game from a game.pack beside it
# (runner/web_pack.hpp).
#
# DRIFT_CANDELA_SOURCE points Cargo at a candela checkout instead of the
# crates.io release, for candela changes not released yet.

set(DRIFT_CANDELA_SOURCE "" CACHE PATH "candela source checkout to build the host against (empty: crates.io)")
find_program(DRIFT_CARGO cargo HINTS $ENV{HOME}/.cargo/bin REQUIRED)
# Cargo reads the path inside a TOML string, where a backslash is an escape.
file(TO_CMAKE_PATH "${DRIFT_CANDELA_SOURCE}" _candela_source)

if(EMSCRIPTEN)
    set(_rust_target wasm32-unknown-emscripten)
    # The host is most of what a web page downloads: build it for size, as
    # one unit with link-time optimization. Measured against the default
    # profile, the page is smaller and scripts compile and run as fast.
    set(_cargo_env CARGO_PROFILE_RELEASE_OPT_LEVEL=s CARGO_PROFILE_RELEASE_LTO=fat CARGO_PROFILE_RELEASE_CODEGEN_UNITS=1)
else()
    set(_rust_target "")
    set(_cargo_env "")
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
if(_candela_source)
    list(APPEND _cargo_args
        --config "patch.crates-io.candela-lang.path=\"${_candela_source}\""
        --config "patch.crates-io.candela-vm.path=\"${_candela_source}/vm\"")
endif()
file(GLOB _host_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/host/src/*.rs)
# A Rust static library carries its own copy of the Rust standard library,
# and so does any other one linked into the same program (wgpu-native, for
# the WebGPU backend). Their std symbols are mangled per Rust version, except
# rust_eh_personality, the unwinder's personality routine, which both define
# under that one name; GNU ld rejects the pair. On ELF the host's copy is
# made weak, so another library's routine wins when there is one; any
# version of it unwinds the host's frames, since they all read the same
# GCC exception tables.
set(_host_link_lib ${_host_lib})
if(NOT EMSCRIPTEN AND NOT APPLE AND NOT WIN32)
    if(NOT CMAKE_OBJCOPY)
        message(FATAL_ERROR "DRIFT_BUILD_CANDELA: objcopy not found (binutils)")
    endif()
    set(_host_link_lib ${_cargo_dir}/${CMAKE_STATIC_LIBRARY_PREFIX}drift_candela_host${CMAKE_STATIC_LIBRARY_SUFFIX})
    set(_host_post COMMAND ${CMAKE_OBJCOPY} --weaken-symbol=rust_eh_personality ${_host_lib} ${_host_link_lib})
endif()
add_custom_command(
    OUTPUT ${_host_link_lib}
    COMMAND ${CMAKE_COMMAND} -E env ${_cargo_env} ${DRIFT_CARGO} ${_cargo_args}
    ${_host_post}
    DEPENDS ${_host_sources} ${CMAKE_CURRENT_SOURCE_DIR}/host/Cargo.toml
    COMMENT "Building the Candela host (cargo)"
    VERBATIM)
add_custom_target(drift_candela_host_build DEPENDS ${_host_link_lib})
add_library(drift_candela_host STATIC IMPORTED GLOBAL)
set_target_properties(drift_candela_host PROPERTIES IMPORTED_LOCATION ${_host_link_lib})
add_dependencies(drift_candela_host drift_candela_host_build)

set(_runner ${CMAKE_CURRENT_SOURCE_DIR}/runner)
set(_module ${CMAKE_CURRENT_SOURCE_DIR}/drift)

# The `drift` program, for the desktop or, in a web build, as the runtime.
function(drift_candela_program target)
    add_executable(${target} ${_runner}/drift.cpp ${_runner}/data.cpp ${_runner}/net.cpp)
    target_link_libraries(${target} PRIVATE drift drift_candela_host)
    target_compile_definitions(${target} PRIVATE
        DRIFT_CANDELA_MODULE_DIR="${_module}"
        DRIFT_VERSION="${drift_VERSION}")
    if(EMSCRIPTEN)
        # Rust unwinds with WebAssembly exceptions on this target.
        target_link_options(${target} PRIVATE -fwasm-exceptions)
    elseif(WIN32)
        # What the Rust standard library in the host links against.
        target_link_libraries(${target} PRIVATE ws2_32 userenv bcrypt ntdll advapi32 synchronization)
    elseif(UNIX)
        target_link_libraries(${target} PRIVATE dl pthread m)
    endif()
endfunction()

if(EMSCRIPTEN)
    # The web runtime: build/.../web/drift.html, drift.js and drift.wasm, the
    # files every game page is made of. The `drift` module is built in; the
    # game comes from game.pack, which pack_loader.js fetches before main().
    drift_candela_program(drift_web)
    set_target_properties(drift_web PROPERTIES
        OUTPUT_NAME drift
        SUFFIX ".html"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/web)
    target_link_options(drift_web PRIVATE
        "--shell-file=${DRIFT_WEB_SHELL}"
        "--pre-js=${_runner}/pack_loader.js"
        "--embed-file=${_module}@/drift-module"
        -sFORCE_FILESYSTEM=1
        -sENVIRONMENT=web)
    file(GLOB_RECURSE _module_files CONFIGURE_DEPENDS ${_module}/*)
    set_property(TARGET drift_web APPEND PROPERTY LINK_DEPENDS
        ${DRIFT_WEB_SHELL} ${_runner}/pack_loader.js ${_module_files})

    # The page writer of `drift build --web`, run by Node.js, so the pages
    # below are made the way `drift build --web` makes them.
    add_executable(drift_web_pack ${_runner}/web_pack.cpp ${_runner}/web_pack_tool.cpp)
    target_link_libraries(drift_web_pack PRIVATE nlohmann_json::nlohmann_json tomlplusplus::tomlplusplus)
    target_link_options(drift_web_pack PRIVATE -sNODERAWFS=1 -sENVIRONMENT=node -sALLOW_MEMORY_GROWTH=1)

    # drift_candela_web_game(<target> PROJECT <dir>): the game in <dir> as a
    # web page, the folder <target>/ beside the runtime: index.html, drift.js,
    # drift.wasm and the game's game.pack. The page is written again when a
    # file in the game folder or the runtime changes.
    function(drift_candela_web_game target)
        cmake_parse_arguments(ARG "" "PROJECT" "" ${ARGN})
        get_filename_component(project ${ARG_PROJECT} ABSOLUTE)
        set(out ${CMAKE_CURRENT_BINARY_DIR}/${target})
        file(GLOB_RECURSE found CONFIGURE_DEPENDS RELATIVE ${project} ${project}/*)
        set(inputs "")
        foreach(rel IN LISTS found)
            if(NOT rel MATCHES "(^|/)(\\.[^/]*|build|build-[^/]*)/")
                list(APPEND inputs ${project}/${rel})
            endif()
        endforeach()
        add_custom_command(
            OUTPUT ${out}/index.html ${out}/game.pack ${out}/drift.js ${out}/drift.wasm
            COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR} $<TARGET_FILE:drift_web_pack>
                ${project} ${out} $<TARGET_FILE_DIR:drift_web>
            DEPENDS drift_web drift_web_pack ${inputs}
            COMMENT "Writing the web page ${target}/"
            VERBATIM)
        add_custom_target(${target} ALL DEPENDS ${out}/index.html)
    endfunction()

    # Game folders outside this tree to build as web pages too, each as the
    # target and folder play_<folder name>:
    # -DDRIFT_CANDELA_WEB_GAMES="/path/to/game;...".
    set(DRIFT_CANDELA_WEB_GAMES "" CACHE STRING "Candela game folders to build as web pages")

    drift_candela_web_game(flappy_bird_cdl PROJECT ${CMAKE_CURRENT_SOURCE_DIR}/examples/flappy_bird)
    drift_candela_web_game(coin_dash_cdl PROJECT ${CMAKE_CURRENT_SOURCE_DIR}/examples/coin_dash)
    drift_candela_web_game(cave_walk_cdl PROJECT ${CMAKE_CURRENT_SOURCE_DIR}/examples/cave_walk)
    drift_candela_web_game(creature_cdl PROJECT ${CMAKE_CURRENT_SOURCE_DIR}/examples/creature)
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
target_sources(drift_run PRIVATE ${_runner}/web_pack.cpp)
target_link_libraries(drift_run PRIVATE tomlplusplus::tomlplusplus)

# The candela standard library this host was compiled with, in libs/ beside
# the program, where `drift` finds it: std/*.cdl and the native libraries
# std/math, std/time, std/random and std/hash load. It comes from the candela
# source Cargo builds: DRIFT_CANDELA_SOURCE, or the crates.io package.
if(_candela_source)
    set(_candela_root ${_candela_source})
else()
    execute_process(
        COMMAND ${DRIFT_CARGO} metadata --format-version 1 --manifest-path ${CMAKE_CURRENT_SOURCE_DIR}/host/Cargo.toml
        OUTPUT_VARIABLE _metadata RESULT_VARIABLE _metadata_result ERROR_VARIABLE _metadata_error)
    if(NOT _metadata_result EQUAL 0)
        message(FATAL_ERROR "cargo metadata failed, so the candela standard library cannot be found:\n${_metadata_error}")
    endif()
    string(JSON _count LENGTH ${_metadata} packages)
    math(EXPR _last "${_count} - 1")
    foreach(i RANGE ${_last})
        string(JSON _name GET ${_metadata} packages ${i} name)
        if(_name STREQUAL "candela-lang")
            string(JSON _manifest GET ${_metadata} packages ${i} manifest_path)
            get_filename_component(_candela_root ${_manifest} DIRECTORY)
        endif()
    endforeach()
endif()
if(NOT IS_DIRECTORY "${_candela_root}/libs/std")
    message(FATAL_ERROR "no candela standard library at ${_candela_root}/libs/std")
endif()

set(_libs ${CMAKE_CURRENT_BINARY_DIR}/libs)
file(GLOB _std_sources CONFIGURE_DEPENDS ${_candela_root}/libs/std/*.cdl)
add_custom_command(
    OUTPUT ${_libs}/std.stamp
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_libs}/std
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_std_sources} ${_libs}/std
    COMMAND ${CMAKE_COMMAND} -E touch ${_libs}/std.stamp
    DEPENDS ${_std_sources}
    COMMENT "Copying the candela standard library"
    VERBATIM)
add_custom_target(drift_candela_std DEPENDS ${_libs}/std.stamp)
add_dependencies(drift_run drift_candela_std)

# Named and placed as each std module's `dylib` block expects:
# libs/std_src/<name>/<name>.so, .dylib or .dll.
foreach(native math time hash random)
    file(GLOB _native_sources ${_candela_root}/libs/std_src/${native}/*.c)
    add_library(candela_std_${native} SHARED ${_native_sources})
    set_target_properties(candela_std_${native} PROPERTIES
        OUTPUT_NAME ${native}
        PREFIX ""
        C_VISIBILITY_PRESET hidden
        LIBRARY_OUTPUT_DIRECTORY ${_libs}/std_src/${native}
        RUNTIME_OUTPUT_DIRECTORY ${_libs}/std_src/${native}
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/std_import)
    if(NOT MSVC)
        # random.c has a plain `inline` function, which in C99 gets no
        # definition of its own unless the call is inlined.
        target_compile_options(candela_std_${native} PRIVATE -fgnu89-inline)
    endif()
    if(UNIX AND NOT APPLE)
        target_link_libraries(candela_std_${native} PRIVATE m)
    endif()
    add_dependencies(drift_run candela_std_${native})
endforeach()

# Scripts that check the drift module against the engine's golden values;
# each logs a line when every check passed.
if(DRIFT_BUILD_TESTS)
    add_test(NAME candela_procgen COMMAND drift_run run ${CMAKE_CURRENT_SOURCE_DIR}/tests/procgen.cdl)
    set_tests_properties(candela_procgen PROPERTIES
        ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen;SDL_AUDIO_DRIVER=dummy"
        PASS_REGULAR_EXPRESSION "procgen checks passed"
        FAIL_REGULAR_EXPRESSION "FAIL|\\[ERROR\\]"
        TIMEOUT 60)
    add_test(NAME candela_utilities COMMAND drift_run run ${CMAKE_CURRENT_SOURCE_DIR}/tests/utilities.cdl
             --set locale.folder=assets/utilities/lang)
    set_tests_properties(candela_utilities PROPERTIES
        ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen;SDL_AUDIO_DRIVER=dummy"
        PASS_REGULAR_EXPRESSION "utility checks passed"
        FAIL_REGULAR_EXPRESSION "FAIL|\\[ERROR\\]"
        TIMEOUT 60)
    add_test(NAME candela_images COMMAND drift_run run ${CMAKE_CURRENT_SOURCE_DIR}/tests/images.cdl)
    set_tests_properties(candela_images PROPERTIES
        ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen;SDL_AUDIO_DRIVER=dummy"
        PASS_REGULAR_EXPRESSION "image checks passed"
        FAIL_REGULAR_EXPRESSION "FAIL|\\[ERROR\\]"
        TIMEOUT 60)
    add_test(NAME candela_presentation COMMAND drift_run run ${CMAKE_CURRENT_SOURCE_DIR}/tests/presentation.cdl)
    set_tests_properties(candela_presentation PROPERTIES
        ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen;SDL_AUDIO_DRIVER=dummy"
        PASS_REGULAR_EXPRESSION "presentation checks passed"
        FAIL_REGULAR_EXPRESSION "FAIL|\\[ERROR\\]"
        TIMEOUT 60)
endif()
