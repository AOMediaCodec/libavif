function(avif_fetchcontent_makeavailable_cmake name)
    FetchContent_GetProperties(${name})
    if(NOT ${name}_POPULATED)
        # Force static build
        set(BUILD_SHARED_LIBS OFF)
        set(BUILD_TESTING OFF)

        FetchContent_MakeAvailable(${name})

        if(ANDROID_ABI)
            set(${name}_BINARY_DIR "${${name}_BINARY_DIR}/${ANDROID_ABI}")
        endif()

        # We must explicitly move the path variables to the parent scope
        FetchContent_GetProperties(${name})
        set(${name}_SOURCE_DIR ${${name}_SOURCE_DIR} PARENT_SCOPE)
        set(${name}_BINARY_DIR ${${name}_BINARY_DIR} PARENT_SCOPE)
        set(${name}_POPULATED ${${name}_POPULATED} PARENT_SCOPE)
    endif()
endfunction()
