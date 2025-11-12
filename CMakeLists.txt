cmake_minimum_required(VERSION 3.24)
project(GrandFishing LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
if(CMAKE_BUILD_TYPE MATCHES Release)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native -flto -DNDEBUG")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native -flto -fstrict-aliasing -DNDEBUG")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /GL /DNDEBUG")
    endif()
endif()
add_executable(GrandFishing
  src/main.cpp
)
include(FetchContent)
FetchContent_Declare(
  sfml
  GIT_REPOSITORY https://github.com/SFML/SFML.git
  GIT_TAG 3.0.2
)
FetchContent_MakeAvailable(sfml)
target_link_libraries(GrandFishing PRIVATE SFML::Graphics SFML::Window SFML::System)