# # Determine library name (lib) from file name (/path/liblib.a).
function(avif_lib_filename_to_name lib_name lib_filename)
    set(${lib_name} "" PARENT_SCOPE)
    get_filename_component(lib_basename "${lib_filename}" NAME)
    string(REGEX REPLACE "(.)" "\\\\\\1" lib_prefix_regex "${CMAKE_STATIC_LIBRARY_PREFIX}")
    string(REGEX REPLACE "([.])" "\\\\\\1" lib_suffix_regex "${CMAKE_STATIC_LIBRARY_SUFFIX}")
    set(lib_name_regex "^${CMAKE_STATIC_LIBRARY_PREFIX}([^.]+)${lib_suffix_regex}$")
    if(${lib_basename} MATCHES "${lib_name_regex}")
        set(${lib_name} "${CMAKE_MATCH_1}" PARENT_SCOPE)
    endif()
endfunction()

function(merge_static_libs target)
    set(args ${ARGN})

    set(dependencies)

    string(REGEX REPLACE "(.)" "\\\\\\1" src_dir_regex "${CMAKE_CURRENT_SOURCE_DIR}")

    foreach(lib ${args})
        if(TARGET "${lib}")
            get_target_property(target_type ${lib} TYPE)

            if(${target_type} STREQUAL "STATIC_LIBRARY")
                list(APPEND libs $<TARGET_FILE:${lib}>)
                list(APPEND dependencies "${lib}")
            endif()
        elseif("${lib}" MATCHES "(\\${CMAKE_STATIC_LIBRARY_SUFFIX}|dav1d\.a)$")
            if(AVIF_STATIC_SYSTEM_LIBRARY_MERGE OR "${lib}" MATCHES "^${src_dir_regex}[/\\\\]ext")
                list(APPEND libs "${lib}")
                avif_lib_filename_to_name(lib_name "${lib}")
                list(REMOVE_ITEM AVIF_PKG_CONFIG_REQUIRES "${lib_name}" "lib${lib_name}")
                list(REMOVE_ITEM AVIF_PKG_CONFIG_LIBS "${CMAKE_LINK_LIBRARY_FLAG}${lib_name}")
            endif()

            if(EXISTS "${lib}")
                list(APPEND dependencies "${lib}")
            endif()
        endif()
    endforeach()

    set(source_file ${CMAKE_CURRENT_BINARY_DIR}/${target}_depends.c)
    add_library(${target} STATIC ${source_file})

    add_custom_command(
        OUTPUT ${source_file} DEPENDS ${dependencies} COMMAND ${CMAKE_COMMAND} -E echo \"const int dummy = 0\;\" > ${source_file}
    )

    add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E remove $<TARGET_FILE:${target}>)

    if(APPLE)
        add_custom_command(
            TARGET ${target}
            POST_BUILD
            COMMENT "Merge static libraries with libtool"
            COMMAND xcrun libtool -static -o $<TARGET_FILE:${target}> -no_warning_for_no_symbols ${libs}
        )
    elseif(CMAKE_C_COMPILER_ID MATCHES "^(Clang|GNU|Intel|IntelLLVM)$")
        add_custom_command(
            TARGET ${target}
            POST_BUILD
            COMMENT "Merge static libraries with ar"
            COMMAND ${CMAKE_COMMAND} -E echo CREATE $<TARGET_FILE:${target}> >script.ar
        )

        foreach(lib ${libs})
            add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E echo ADDLIB ${lib} >>script.ar)
        endforeach()

        add_custom_command(
            TARGET ${target}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo SAVE >>script.ar
            COMMAND ${CMAKE_COMMAND} -E echo END >>script.ar
            COMMAND ${CMAKE_AR} -M <script.ar
            COMMAND ${CMAKE_COMMAND} -E remove script.ar
        )
    elseif(MSVC)
        if(CMAKE_LIBTOOL)
            set(BUNDLE_TOOL ${CMAKE_LIBTOOL})
        else()
            find_program(BUNDLE_TOOL lib HINTS "${CMAKE_C_COMPILER}/..")

            if(NOT BUNDLE_TOOL)
                message(FATAL_ERROR "Cannot locate lib.exe to bundle libraries")
            endif()
        endif()

        add_custom_command(
            TARGET ${target}
            POST_BUILD
            COMMENT "Merge static libraries with lib.exe"
            COMMAND ${BUNDLE_TOOL} /NOLOGO /OUT:$<TARGET_FILE:${target}> ${libs}
        )
    else()
        message(FATAL_ERROR "Unsupported platform for static link merging")
    endif()
endfunction()
