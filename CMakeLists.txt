cmake_minimum_required (VERSION 3.10)
project(laplace)

set(CMAKE_CXX_STANDARD 23)

# CACHE, INTERNAL, and FORCE are all required for some reason to override libgit flags
set(BUILD_SHARED_LIBS OFF CACHE STRING INTERNAL FORCE)
set(BUILD_TESTS OFF CACHE STRING INTERNAL FORCE)
set(BUILD_EXAMPLES OFF CACHE STRING INTERNAL FORCE)
set(USE_GSSAPI OFF CACHE STRING INTERNAL FORCE)
set(USE_SSH OFF CACHE STRING INTERNAL FORCE)
set(USE_HTTPS OFF CACHE STRING INTERNAL FORCE)
set(USE_NTLMCLIENT OFF CACHE STRING INTERNAL FORCE)
set(BUILD_CLI OFF CACHE STRING INTERNAL FORCE)
set(LINK_WITH_STATIC_LIBRARIES ON CACHE STRING INTERNAL FORCE)
set(USE_ZLIB ON CACHE STRING INTERNAL FORCE)
set(USE_TLS OFF CACHE STRING INTERNAL FORCE)
add_subdirectory(external/libgit2)
add_subdirectory(external/glaze)
add_subdirectory(external/IXWebSocket)

file(GLOB_RECURSE sources src/*.cpp src/*.h)
add_executable(laplace ${sources})
target_compile_options(laplace PRIVATE -O2)
target_include_directories(laplace PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/external/libgit2/include
    ${CMAKE_CURRENT_SOURCE_DIR}/external/glaze/include
    ${CMAKE_CURRENT_SOURCE_DIR}/external/IXWebSocket/ixwebsocket
)
target_link_libraries(laplace PRIVATE
    libgit2package
    glaze_glaze   
    ixwebsocket 
)

