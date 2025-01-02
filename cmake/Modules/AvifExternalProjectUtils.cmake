macro(avif_fetchcontent_populate_cmake name)
    if(NOT ${name}_POPULATED)
        FetchContent_Populate(${name})

        # Force static build
        set(BUILD_SHARED_LIBS_ORIG ${BUILD_SHARED_LIBS})
        set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")

        add_subdirectory(${${name}_SOURCE_DIR} ${${name}_BINARY_DIR} EXCLUDE_FROM_ALL)

        set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_ORIG} CACHE BOOL "" FORCE)
    endif()
endmacro()
