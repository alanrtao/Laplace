cmake_minimum_required (VERSION 3.10)
project(laplace)

# CACHE, INTERNAL, and FORCE are all required for some reason to override libgit flags
set(BUILD_SHARED_LIBS OFF CACHE "" INTERNAL FORCE)
set(BUILD_TESTS OFF CACHE "" INTERNAL FORCE)
set(USE_GSSAPI OFF CACHE "" INTERNAL FORCE)
set(USE_SSH OFF CACHE "" INTERNAL FORCE)
set(USE_HTTPS OFF CACHE "" INTERNAL FORCE)
set(USE_NTLMCLIENT OFF CACHE "" INTERNAL FORCE)
set(BUILD_CLI OFF CACHE "" INTERNAL FORCE)
add_subdirectory(external/libgit2)

file(GLOB_RECURSE sources src/**.cpp src/**.h)
add_executable(laplace ${sources})

target_compile_options(laplace PRIVATE -O2)
target_link_libraries(laplace PRIVATE libgit2)