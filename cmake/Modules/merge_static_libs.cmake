function(merge_static_libs target)
  set(args ${ARGN})

  foreach(lib ${args})
    if("${lib}" MATCHES "(\\${CMAKE_STATIC_LIBRARY_SUFFIX}|dav1d\.a)$")
      list(APPEND libs ${lib})
    endif()
  endforeach()

  if(APPLE)
    add_custom_command(
      TARGET ${target}
      POST_BUILD
      COMMENT "Merge static libraries with libtool"
      COMMAND ${CMAKE_COMMAND} -E rename $<TARGET_FILE:${target}> $<TARGET_FILE:${target}>.tmp
      COMMAND xcrun libtool -static -o $<TARGET_FILE:${target}> $<TARGET_FILE:${target}>.tmp ${libs}
      COMMAND ${CMAKE_COMMAND} -E remove $<TARGET_FILE:${target}>.tmp
    )
  elseif(CMAKE_C_COMPILER_ID MATCHES "^(Clang|GNU|Intel|IntelLLVM)$")
    add_custom_command(
      TARGET ${target}
      POST_BUILD
      COMMENT "Merge static libraries with ar"
      COMMAND ${CMAKE_COMMAND} -E rename $<TARGET_FILE:${target}> $<TARGET_FILE:${target}>.tmp
      COMMAND ${CMAKE_COMMAND} -E echo CREATE $<TARGET_FILE:${target}> >script.ar
      COMMAND ${CMAKE_COMMAND} -E echo ADDLIB $<TARGET_FILE:${target}>.tmp >>script.ar
    )

    foreach(lib ${libs})
      add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo ADDLIB ${lib} >>script.ar
      )
    endforeach()

    add_custom_command(
      TARGET ${target}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E echo SAVE >>script.ar
      COMMAND ${CMAKE_COMMAND} -E echo END >>script.ar
      COMMAND ${CMAKE_AR} -M <script.ar
      COMMAND ${CMAKE_COMMAND} -E remove $<TARGET_FILE:${target}>.tmp script.ar
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
      COMMAND ${CMAKE_COMMAND} -E rename $<TARGET_FILE:${target}> $<TARGET_FILE:${target}>.tmp
      COMMAND ${BUNDLE_TOOL} /NOLOGO /OUT:$<TARGET_FILE:${target}> $<TARGET_FILE:${target}>.tmp ${libs}
      COMMAND ${CMAKE_COMMAND} -E remove $<TARGET_FILE:${target}>.tmp
    )
  else()
    message(FATAL_ERROR "Unsupported platform for static link merging")
  endif()
endfunction()
