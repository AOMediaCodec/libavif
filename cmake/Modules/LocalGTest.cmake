set(AVIF_LOCAL_GTEST_GIT_TAG v1.14.0)

set(GTest_FOUND ON CACHE BOOL "")
set(GTEST_INCLUDE_DIRS ${AVIF_SOURCE_DIR}/ext/googletest/googletest/include)
set(GTEST_LIB_FILENAME
    ${AVIF_SOURCE_DIR}/ext/googletest/build/lib/${CMAKE_STATIC_LIBRARY_PREFIX}gtest${CMAKE_STATIC_LIBRARY_SUFFIX}
)
set(GTEST_MAIN_LIB_FILENAME
    ${AVIF_SOURCE_DIR}/ext/googletest/build/lib/${CMAKE_STATIC_LIBRARY_PREFIX}gtest_main${CMAKE_STATIC_LIBRARY_SUFFIX}
)
if(EXISTS ${GTEST_INCLUDE_DIRS}/gtest/gtest.h AND EXISTS ${GTEST_LIB_FILENAME} AND EXISTS ${GTEST_MAIN_LIB_FILENAME})
    message(STATUS "libavif(AVIF_GTEST=LOCAL): compiled library found in ext/googletest")

    add_library(GTest::GTest STATIC IMPORTED)
    set_target_properties(GTest::GTest PROPERTIES IMPORTED_LOCATION "${GTEST_LIB_FILENAME}" AVIF_LOCAL ON)

    if(TARGET Threads::Threads)
        target_link_libraries(GTest::GTest INTERFACE Threads::Threads)
    endif()
    target_include_directories(GTest::GTest INTERFACE "${GTEST_INCLUDE_DIRS}")

    add_library(GTest::Main STATIC IMPORTED)
    target_link_libraries(GTest::Main INTERFACE GTest::GTest)
    set_target_properties(GTest::Main PROPERTIES IMPORTED_LOCATION "${GTEST_MAIN_LIB_FILENAME}" AVIF_LOCAL ON)
else()
    message(STATUS "libavif(AVIF_GTEST=LOCAL): compiled library not found in ext/googletest; using FetchContent")
    if(EXISTS "${AVIF_SOURCE_DIR}/ext/googletest")
        message(STATUS "libavif(AVIF_GTEST=LOCAL): ext/googletest found; using as FetchContent SOURCE_DIR")
        set(FETCHCONTENT_SOURCE_DIR_GOOGLETEST "${AVIF_SOURCE_DIR}/ext/googletest")
        message(CHECK_START "libavif(AVIF_LOCAL_GTEST): configuring googletest")
    else()
        message(CHECK_START "libavif(AVIF_LOCAL_GTEST): fetching and configuring googletest")
    endif()

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG ${AVIF_LOCAL_GTEST_GIT_TAG}
        GIT_SHALLOW ON
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)

    avif_fetchcontent_populate_cmake(googletest)

    set_target_properties(gtest gtest_main PROPERTIES AVIF_LOCAL ON)

    add_library(GTest::GTest ALIAS gtest)
    add_library(GTest::Main ALIAS gtest_main)

    message(CHECK_PASS "complete")
endif()
