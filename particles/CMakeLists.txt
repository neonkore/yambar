cmake_minimum_required(VERSION 3.13)

add_library(particle-sdk INTERFACE)
target_compile_options(particle-sdk INTERFACE ${CAIRO_CFLAGS_OTHER})
target_include_directories(particle-sdk INTERFACE ${CAIRO_INCLUDE_DIRS})

# Only an exposable, not a particle. Used by a couple of modules
add_library(dynlist SHARED dynlist.c dynlist.h)
target_link_libraries(dynlist PRIVATE particle-sdk)

set(CMAKE_SHARED_MODULE_PREFIX particle_)

set(particles empty list map progress-bar ramp string)

foreach (particle ${particles})
  add_library(${particle} MODULE ${particle}.c)
  target_link_libraries(${particle} particle-sdk)
endforeach ()

target_link_libraries(string ${CAIRO_LIBRARIES})

install(TARGETS ${particles} dynlist DESTINATION lib/f00bar)
