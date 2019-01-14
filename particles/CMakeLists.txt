cmake_minimum_required(VERSION 3.13)

add_library(particle-sdk INTERFACE)
target_compile_options(particle-sdk INTERFACE ${CAIRO_CFLAGS_OTHER})
target_include_directories(particle-sdk INTERFACE ${CAIRO_INCLUDE_DIRS})

if (CORE_PLUGINS_AS_SHARED_LIBRARIES)
  set(lib_type MODULE)
  set(dynlist_lib_type SHARED)
else ()
  set(lib_type STATIC)
  set(dynlist_lib_type STATIC)
endif ()

# Only an exposable, not a particle. Used by a couple of modules
add_library(dynlist ${dynlist_lib_type} dynlist.c dynlist.h)
target_link_libraries(dynlist PRIVATE particle-sdk)

set(CMAKE_SHARED_MODULE_PREFIX particle_)

set(particles empty list map progress-bar ramp string)

foreach (particle ${particles})
  add_library(${particle} ${lib_type} ${particle}.c)
  target_link_libraries(${particle} particle-sdk)
endforeach ()

target_link_libraries(string ${CAIRO_LIBRARIES})

if (CORE_PLUGINS_AS_SHARED_LIBRARIES)
  install(TARGETS ${particles} dynlist DESTINATION lib/f00bar)
endif ()
