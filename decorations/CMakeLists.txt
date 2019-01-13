cmake_minimum_required(VERSION 3.13)

add_library(decoration-sdk INTERFACE)
target_compile_options(decoration-sdk INTERFACE ${CAIRO_CFLAGS_OTHER})
target_include_directories(decoration-sdk INTERFACE ${CAIRO_INCLUDE_DIRS})

set(CMAKE_SHARED_MODULE_PREFIX decoration_)

set(decorations background stack underline)

foreach (deco ${decorations})
  add_library(${deco} MODULE ${deco}.c)
  target_link_libraries(${deco} decoration-sdk)
endforeach ()

install(TARGETS ${decorations} DESTINATION lib/f00bar)
