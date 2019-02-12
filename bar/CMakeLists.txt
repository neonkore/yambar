cmake_minimum_required(VERSION 3.13)

# X11/XCB bar backend
if (ENABLE_X11)
  add_library(bar-xcb STATIC EXCLUDE_FROM_ALL xcb.c xcb.h)
  target_link_libraries(bar-xcb xcb-stuff PkgConfig::cairo)
endif ()

# Wayland/wlroots bar backend
if (ENABLE_WAYLAND)
  function (wayland_protocol _deps)
    set(deps "")
    foreach (xml_file ${ARGN})
      get_filename_component(base ${xml_file} NAME_WE)
      set(out_c ${base}.c)
      set(out_h ${base}.h)

      add_custom_command(
        OUTPUT ${out_h}
        COMMAND wayland-scanner client-header < ${xml_file} > ${out_h}
        VERBATIM
        MAIN_DEPENDENCY ${xml_file}
        )
      add_custom_command(
        OUTPUT ${out_c}
        COMMAND wayland-scanner private-code < ${xml_file} > ${out_c}
        VERBATIM
        MAIN_DEPENDENCY ${xml_file}
        )

      list(APPEND deps ${out_h})
      list(APPEND deps ${out_c})
    endforeach ()

    set(${_deps} ${deps} PARENT_SCOPE)
  endfunction ()

  pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
  execute_process(
    COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=pkgdatadir wayland-protocols
    OUTPUT_VARIABLE WAYLAND_PROTOCOLS
    OUTPUT_STRIP_TRAILING_WHITESPACE)

  wayland_protocol(
    wayland_protos
    ${PROJECT_SOURCE_DIR}/external/wlr-protocols/unstable/wlr-layer-shell-unstable-v1.xml
    ${WAYLAND_PROTOCOLS}/stable/xdg-shell/xdg-shell.xml
    ${WAYLAND_PROTOCOLS}/unstable/xdg-output/xdg-output-unstable-v1.xml
    )

  add_library(wayland-protocols STATIC EXCLUDE_FROM_ALL ${wayland_protos})
  target_include_directories(wayland-protocols PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

  add_library(bar-wayland STATIC EXCLUDE_FROM_ALL wayland.c wayland.h)
  target_compile_definitions(bar-wayland PRIVATE _GNU_SOURCE)
  target_link_libraries(
    bar-wayland wayland-protocols PkgConfig::wayland PkgConfig::cairo)
endif ()

add_library(bar STATIC EXCLUDE_FROM_ALL bar.c bar.h private.h backend.h)

target_link_libraries(bar
  $<$<BOOL:${ENABLE_X11}>:bar-xcb>
  $<$<BOOL:${ENABLE_WAYLAND}>:bar-wayland>
  ${CMAKE_THREAD_LIBS_INIT}
)
