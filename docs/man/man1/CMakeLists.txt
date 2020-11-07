# Copyright 2020 Shun Sakai. All rights reserved.
# SPDX-License-Identifier: BSD-2-Clause

option(AVIF_BUILD_MAN "Build man page" OFF)
if(AVIF_BUILD_MAN)
    find_program(ASCIIDOCTOR_EXE asciidoctor)
    find_program(A2X_EXE a2x)
    if(NOT (ASCIIDOCTOR_EXE OR A2X_EXE))
        message(FATAL_ERROR "man page: asciidoctor or a2x is required to build")
    endif()

    set(MAN_PAGES avifenc.1 avifdec.1)

    set(ASCIIDOC_ATTRIBUTES -a manversion=${PROJECT_VERSION})
    # For codec option
    if(AVIF_CODEC_AOM)
        # This is also for the AOM-Specific Advanced options
        list(APPEND ASCIIDOC_ATTRIBUTES -a available-aom)
    endif()
    if(AVIF_CODEC_DAV1D)
        list(APPEND ASCIIDOC_ATTRIBUTES -a available-dav1d)
    endif()
    if(AVIF_CODEC_LIBGAV1)
        list(APPEND ASCIIDOC_ATTRIBUTES -a available-libgav1)
    endif()
    if(AVIF_CODEC_RAV1E)
        list(APPEND ASCIIDOC_ATTRIBUTES -a available-rav1e)
    endif()
    if(AVIF_CODEC_SVT)
        list(APPEND ASCIIDOC_ATTRIBUTES -a available-svt)
    endif()

    if(ASCIIDOCTOR_EXE)
        message(STATUS "using asciidoctor: ${ASCIIDOCTOR_EXE}")

        foreach(MAN_PAGE IN LISTS MAN_PAGES)
            add_custom_command(OUTPUT ${MAN_PAGE}
                COMMAND ${ASCIIDOCTOR_EXE} -D "${CMAKE_CURRENT_BINARY_DIR}" -b manpage ${ASCIIDOC_ATTRIBUTES} "${CMAKE_CURRENT_SOURCE_DIR}/${MAN_PAGE}.adoc"
                DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${MAN_PAGE}.adoc"
                VERBATIM)
        endforeach()
    else()
        message(STATUS "using a2x: ${A2X_EXE}")

        foreach(MAN_PAGE IN LISTS MAN_PAGES)
            add_custom_command(OUTPUT ${MAN_PAGE}
                COMMAND ${A2X_EXE} -D "${CMAKE_CURRENT_BINARY_DIR}" -f manpage ${ASCIIDOC_ATTRIBUTES} "${CMAKE_CURRENT_SOURCE_DIR}/${MAN_PAGE}.adoc"
                DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${MAN_PAGE}.adoc"
                VERBATIM)
        endforeach()
    endif()
    add_custom_target(man ALL DEPENDS ${MAN_PAGES})

    foreach(MAN_PAGE IN LISTS MAN_PAGES)
        install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${MAN_PAGE}"
            DESTINATION "${CMAKE_INSTALL_MANDIR}/man1")
    endforeach()
endif()
