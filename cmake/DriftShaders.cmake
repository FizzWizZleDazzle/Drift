# drift_add_shaders(<target> OUTPUT_DIR <dir> SHADERS <file.wgsl>...)
#
# For each WGSL shader body, writes into OUTPUT_DIR:
#   <name>.wgsl       the body (WebGPU backend; the engine adds the prelude)
#   <name>.vert.glsl  and <name>.frag.glsl, translated by naga for the
#                     OpenGL / WebGL2 backend
# RendererResource::loadShader("<dir>/<name>") then works on every backend.
# Requires the naga CLI (cargo install naga-cli).

set(DRIFT_SHADER_PRELUDE ${CMAKE_CURRENT_LIST_DIR}/../src/gfx/shaders/prelude.wgsl
    CACHE INTERNAL "Drift WGSL shader prelude")

function(drift_add_shaders target)
    cmake_parse_arguments(ARG "" "OUTPUT_DIR" "SHADERS" ${ARGN})
    if(NOT ARG_OUTPUT_DIR)
        message(FATAL_ERROR "drift_add_shaders: OUTPUT_DIR is required")
    endif()
    find_program(DRIFT_NAGA naga HINTS $ENV{HOME}/.cargo/bin)
    if(NOT DRIFT_NAGA)
        message(FATAL_ERROR "drift_add_shaders: naga not found. Install it with: cargo install naga-cli")
    endif()

    set(outputs "")
    foreach(src IN LISTS ARG_SHADERS)
        get_filename_component(abs ${src} ABSOLUTE)
        get_filename_component(name ${src} NAME_WE)
        set(combined ${CMAKE_CURRENT_BINARY_DIR}/drift_shaders/${name}.full.wgsl)
        set(out_wgsl ${ARG_OUTPUT_DIR}/${name}.wgsl)
        set(out_vert ${ARG_OUTPUT_DIR}/${name}.vert.glsl)
        set(out_frag ${ARG_OUTPUT_DIR}/${name}.frag.glsl)
        add_custom_command(
            OUTPUT ${out_wgsl} ${out_vert} ${out_frag}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${ARG_OUTPUT_DIR} ${CMAKE_CURRENT_BINARY_DIR}/drift_shaders
            COMMAND ${CMAKE_COMMAND} -DPRELUDE=${DRIFT_SHADER_PRELUDE} -DBODY=${abs} -DOUT=${combined}
                    -P ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/DriftConcat.cmake
            # naga picks the stage from the .vert/.frag extension and exits 0
            # on errors, so translate to intermediates; the copies fail if
            # translation produced nothing.
            COMMAND ${CMAKE_COMMAND} -E rm -f ${combined}.vert ${combined}.frag
            COMMAND ${DRIFT_NAGA} --keep-coordinate-space --profile es300 --entry-point vs_main ${combined} ${combined}.vert
            COMMAND ${DRIFT_NAGA} --keep-coordinate-space --profile es300 --entry-point fs_main ${combined} ${combined}.frag
            COMMAND ${CMAKE_COMMAND} -E copy ${combined}.vert ${out_vert}
            COMMAND ${CMAKE_COMMAND} -E copy ${combined}.frag ${out_frag}
            COMMAND ${CMAKE_COMMAND} -E copy ${abs} ${out_wgsl}
            DEPENDS ${abs} ${DRIFT_SHADER_PRELUDE}
            COMMENT "Translating shader ${name}.wgsl"
            VERBATIM)
        list(APPEND outputs ${out_wgsl} ${out_vert} ${out_frag})
    endforeach()
    add_custom_target(${target}_shaders DEPENDS ${outputs})
    add_dependencies(${target} ${target}_shaders)
endfunction()
