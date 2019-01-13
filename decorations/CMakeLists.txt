cmake_minimum_required(VERSION 3.13)

add_library(decoration-sdk INTERFACE)
target_compile_options(decoration-sdk INTERFACE ${CAIRO_CFLAGS_OTHER})
target_include_directories(decoration-sdk INTERFACE ${CAIRO_INCLUDE_DIRS})

set(CMAKE_SHARED_MODULE_PREFIX decoration_)

add_library(background MODULE background.c)
target_link_libraries(background decoration-sdk)

add_library(stack MODULE stack.c)
target_link_libraries(stack decoration-sdk)

add_library(underline MODULE underline.c)
target_link_libraries(underline decoration-sdk)

install(
  TARGETS
  background
  stack
  underline

  DESTINATION lib/f00bar)
