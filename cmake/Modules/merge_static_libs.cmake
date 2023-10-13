function(merge_static_libs target unmerged_libs)
    set(args ${ARGN})

    set(dependencies)

    foreach(lib ${args})
        if(TARGET "${lib}")
            get_target_property(target_type ${lib} TYPE)

            if(${target_type} STREQUAL "STATIC_LIBRARY")
                list(APPEND libs $<TARGET_FILE:${lib}>)
                list(APPEND dependencies "${lib}")
            else()
                list(APPEND unmerged_libs "${lib}")
            endif()
        elseif("${lib}" MATCHES "(\\${CMAKE_STATIC_LIBRARY_SUFFIX}|dav1d\.a)$")
            list(APPEND libs "${lib}")

            if(EXISTS "${lib}")
                list(APPEND dependencies "${lib}")
            endif()
        else()
            list(APPEND unmerged_libs "${lib}")
        endif()
    endforeach()

    set(source_file ${CMAKE_CURRENT_BINARY_DIR}/${target}_depends.c)
    add_library(${target} STATIC ${source_file})

    add_custom_command(
        OUTPUT ${source_file} COMMAND ${CMAKE_COMMAND} -E echo "const int dummy = 0;" > ${source_file} DEPENDS ${dependencies}
    )

    add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E remove $<TARGET_FILE:${target}>)

    if(APPLE)
        add_custom_command(
            TARGET ${target}
            POST_BUILD
            COMMENT "Merge static libraries with libtool"
            COMMAND xcrun libtool -static -o $<TARGET_FILE:${target}> ${libs}
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
