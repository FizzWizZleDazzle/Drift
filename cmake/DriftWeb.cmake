# drift_web_app(<target> [PROJECT <dir>] [ASSETS <dir>...] [PRELOAD <src>@<dst>...] [SHELL <file>])
#
# Makes a game target build for the browser: an .html page plus .js/.wasm/.data
# files. PROJECT is the game's folder (default: the current source directory).
# Its drift.toml, when there is one, is packaged at "/drift.toml", where the
# game finds it as it does on desktop, and the directory its [project] assets
# key names (default "assets") is packaged under the same name. ASSETS
# directories replace it and are packaged at "assets/"; PRELOAD entries map
# any other directory into the virtual file system. Does nothing outside
# Emscripten, so it is safe to call unconditionally.

set(DRIFT_WEB_SHELL ${CMAKE_CURRENT_LIST_DIR}/drift_shell.html CACHE INTERNAL "Default Drift web shell page")

# drift_project_value(<drift.toml> <key> <out-var> <default>)
# Reads one string key of the [project] table. Only `key = "value"` lines are
# understood, which is all [project] holds.
function(drift_project_value file key out default)
    set(value ${default})
    if(EXISTS ${file})
        file(STRINGS ${file} lines)
        set(in_project FALSE)
        foreach(line IN LISTS lines)
            if(line MATCHES "^[ \t]*\\[([^]]*)\\]")
                if(CMAKE_MATCH_1 STREQUAL "project")
                    set(in_project TRUE)
                else()
                    set(in_project FALSE)
                endif()
            elseif(in_project AND line MATCHES "^[ \t]*${key}[ \t]*=[ \t]*\"([^\"]*)\"")
                set(value ${CMAKE_MATCH_1})
            endif()
        endforeach()
    endif()
    set(${out} ${value} PARENT_SCOPE)
endfunction()

function(drift_web_app target)
    if(NOT EMSCRIPTEN)
        return()
    endif()
    cmake_parse_arguments(ARG "" "SHELL;PROJECT" "ASSETS;PRELOAD" ${ARGN})
    set(shell ${DRIFT_WEB_SHELL})
    if(ARG_SHELL)
        get_filename_component(shell ${ARG_SHELL} ABSOLUTE)
    endif()
    set(project ${CMAKE_CURRENT_SOURCE_DIR})
    if(ARG_PROJECT)
        get_filename_component(project ${ARG_PROJECT} ABSOLUTE)
    endif()
    set_target_properties(${target} PROPERTIES SUFFIX ".html")
    target_link_options(${target} PRIVATE "--shell-file=${shell}")
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${shell})

    set(config ${project}/drift.toml)
    if(EXISTS ${config})
        target_link_options(${target} PRIVATE "--preload-file=${config}@/drift.toml")
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${config})
    endif()
    if(ARG_ASSETS)
        foreach(dir IN LISTS ARG_ASSETS)
            get_filename_component(abs ${dir} ABSOLUTE)
            target_link_options(${target} PRIVATE "--preload-file=${abs}@/assets")
        endforeach()
    else()
        drift_project_value(${config} assets dir "assets")
        if(IS_DIRECTORY ${project}/${dir})
            target_link_options(${target} PRIVATE "--preload-file=${project}/${dir}@/${dir}")
        endif()
    endif()
    foreach(entry IN LISTS ARG_PRELOAD)
        target_link_options(${target} PRIVATE "--preload-file=${entry}")
    endforeach()
endfunction()
