include(ExternalProject)
ExternalProject_Add(libgav1
    PREFIX libgav1
    SOURCE_DIR ${PROJECT_SOURCE_DIR}/ext/libgav1
    BINARY_DIR ${PROJECT_SOURCE_DIR}/ext/libgav1/build
    #GIT_REPOSITORY "https://chromium.googlesource.com/codecs/libgav1"
    #GIT_TAG 45a1d76
    PATCH_COMMAND cmake -E make_directory "${PROJECT_SOURCE_DIR}/ext/libgav1/build"
    URL "https://chromium.googlesource.com/codecs/libgav1/+archive/45a1d76.tar.gz"
    INSTALL_COMMAND ""
    CMAKE_CACHE_DEFAULT_ARGS
        -DLIBGAV1_THREADPOOL_USE_STD_MUTEX:bool=1)

find_package(Git)
if(GIT_FOUND)
    ExternalProject_Add_Step(libgav1 clone-abseil
        COMMAND git clone --depth 1 https://github.com/abseil/abseil-cpp.git "${PROJECT_SOURCE_DIR}/ext/libgav1/third_party/abseil-cpp"
        COMMENT "Downloading abseil-cpp"
        DEPENDERS configure
        DEPENDEES download)
else()
    ExternalProject_Add(abseil-cpp
        PREFIX abseil-cpp
        #GIT_REPOSITORY "https://github.com/abseil/abseil-cpp.git"
        URL https://github.com/abseil/abseil-cpp/tarball/master
        CONFIGURE_COMMAND ""
        BUILD_COMMAND cmake -E touch "${CMAKE_CURRENT_BINARY_DIR}/abseil-ts"
        INSTALL_COMMAND ""
        BUILD_BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/abseil-ts")

    file(MAKE_DIRECTORY ${PROJECT_SOURCE_DIR}/ext/libgav1/third_party/abseil-cpp)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/copy-abseil.cmake "
file(COPY \"${CMAKE_CURRENT_BINARY_DIR}/abseil-cpp/src/abseil-cpp\"
DESTINATION \"${PROJECT_SOURCE_DIR}/ext/libgav1/third_party\")")
    ExternalProject_Add_Step(libgav1 copy-abseil
        COMMAND cmake -E make_directory libgav1/src/libgav1/third_party/abseil-cpp
        COMMAND cmake -P "${CMAKE_CURRENT_BINARY_DIR}/copy-abseil.cmake"
        COMMENT "Copying abseil-cpp"
        DEPENDERS configure
        DEPENDEES download)
    add_dependencies(libgav1 abseil-cpp)
endif()

set(LIBGAV1_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/ext/libgav1/src/")
set(LIBGAV1_LIBRARY "${PROJECT_SOURCE_DIR}/ext/libgav1/build/${CMAKE_STATIC_LIBRARY_PREFIX}gav1${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(LIBGAV1_LIBRARIES ${LIBGAV1_LIBRARY})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(libgav1
                                  FOUND_VAR LIBGAV1_FOUND
                                  REQUIRED_VARS LIBGAV1_LIBRARY LIBGAV1_LIBRARIES LIBGAV1_INCLUDE_DIR
                                  VERSION_VAR _LIBGAV1_VERSION)

# show the LIBGAV1_INCLUDE_DIR, LIBGAV1_LIBRARY and LIBGAV1_LIBRARIES variables
# only in the advanced view
mark_as_advanced(LIBGAV1_INCLUDE_DIR LIBGAV1_LIBRARY LIBGAV1_LIBRARIES)