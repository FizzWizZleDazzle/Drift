# drift_web_app(<target> [ASSETS <dir>...] [PRELOAD <src>@<dst>...] [SHELL <file>])
#
# Makes a game target build for the browser: an .html page plus .js/.wasm/.data
# files. Each ASSETS directory is packaged so the game finds it at "assets/"
# (the same relative path it uses on desktop); PRELOAD entries map any other
# directory into the virtual file system. Does nothing outside Emscripten, so
# it is safe to call unconditionally.

set(DRIFT_WEB_SHELL ${CMAKE_CURRENT_LIST_DIR}/drift_shell.html CACHE INTERNAL "Default Drift web shell page")

function(drift_web_app target)
    if(NOT EMSCRIPTEN)
        return()
    endif()
    cmake_parse_arguments(ARG "" "SHELL" "ASSETS;PRELOAD" ${ARGN})
    set(shell ${DRIFT_WEB_SHELL})
    if(ARG_SHELL)
        get_filename_component(shell ${ARG_SHELL} ABSOLUTE)
    endif()
    set_target_properties(${target} PROPERTIES SUFFIX ".html")
    target_link_options(${target} PRIVATE "--shell-file=${shell}")
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${shell})
    foreach(dir IN LISTS ARG_ASSETS)
        get_filename_component(abs ${dir} ABSOLUTE)
        target_link_options(${target} PRIVATE "--preload-file=${abs}@/assets")
    endforeach()
    foreach(entry IN LISTS ARG_PRELOAD)
        target_link_options(${target} PRIVATE "--preload-file=${entry}")
    endforeach()
endfunction()
