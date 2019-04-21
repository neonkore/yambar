cmake_minimum_required(VERSION 3.13)

find_program(SCDOC_EXECUTABLE scdoc)
find_program(GZIP_EXECUTABLE gzip)

function(add_man_pages)
  foreach (src ${ARGN})
    string(REPLACE ".scd" ".gz" man "${src}")
    string(REGEX MATCH "\.[0-9]+\.scd" man_target "${src}")
    string(REGEX MATCH "[0-9]+" man_target "${man_target}")

    add_custom_command(
      OUTPUT ${man}
      COMMENT "Generating man page ${man}"
      COMMAND ${SCDOC_EXECUTABLE} < ${CMAKE_CURRENT_SOURCE_DIR}/${src} |
      ${GZIP_EXECUTABLE} > ${man}
      VERBATIM
      MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/${src}
      )

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${man}
      DESTINATION share/man/man${man_target})
  endforeach()

  string(REPLACE ".scd" ".gz" out "${ARGN}")
  list(TRANSFORM out PREPEND "${CMAKE_CURRENT_BINARY_DIR}/")
  add_custom_target(man-pages ALL SOURCES ${ARGN} DEPENDS ${out})
endfunction()

add_man_pages(
  f00bar.1.scd
  f00bar.5.scd

  f00bar-modules.5.scd
  f00bar-module-alsa.5.scd

  f00bar-particles.5.scd
  )
