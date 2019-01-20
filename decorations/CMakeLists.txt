cmake_minimum_required(VERSION 3.7)

add_library(decoration-sdk INTERFACE)
target_compile_options(decoration-sdk INTERFACE ${CAIRO_CFLAGS_OTHER})
target_include_directories(decoration-sdk INTERFACE ${CAIRO_INCLUDE_DIRS})

set(CMAKE_SHARED_MODULE_PREFIX decoration_)

set(decorations background stack underline)

if (CORE_PLUGINS_AS_SHARED_LIBRARIES)
  set(lib_type MODULE)
else()
  set(lib_type STATIC)
endif ()

foreach (deco ${decorations})
  add_library(${deco} ${lib_type} ${deco}.c)
  target_link_libraries(${deco} decoration-sdk)
endforeach ()

if (CORE_PLUGINS_AS_SHARED_LIBRARIES)
  install(TARGETS ${decorations} DESTINATION lib/f00bar)
endif ()
