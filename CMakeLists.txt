cmake_minimum_required (VERSION 3.10)
project(laplace)

option(CMAKE_CXX_STANDARD C++17)

# Check if Clang is available
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(STATUS "Using Clang compiler")
else()
    # Try to find Clang
    find_program(CLANG_CXX_COMPILER clang++)
    if(CLANG_CXX_COMPILER)
        message(STATUS "Found Clang compiler")
        set(CMAKE_CXX_COMPILER ${CLANG_CXX_COMPILER})
    else()
        message(STATUS "Clang compiler not found, using default")
    endif()
endif()

set(LIBGIT2_BUILD_SHARED_LIBS OFF)
set(USE_HTTPS OFF)
set(USE_GSSAPI OFF)
set(USE_SSH OFF)
add_subdirectory(external/libgit2)

file(GLOB_RECURSE sources      src/*.cpp src/*.h)
add_executable(laplace ${sources})

target_link_libraries(laplace PRIVATE git2)