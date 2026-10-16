cmake_minimum_required(VERSION 3.16)
project(burger_rush C)

set(CMAKE_C_STANDARD 99)

# raylib 5.5 : utilise celle du systeme si elle existe, sinon la telecharge
find_package(raylib 5.5 QUIET)
if (NOT raylib_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.5
        GIT_SHALLOW TRUE
    )
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(raylib)
endif()

add_executable(burger_rush src/main.c)
target_link_libraries(burger_rush PRIVATE raylib)

if (UNIX AND NOT APPLE)
    target_link_libraries(burger_rush PRIVATE m GL)
elseif (WIN32)
    target_link_libraries(burger_rush PRIVATE opengl32)
elseif (APPLE)
    target_link_libraries(burger_rush PRIVATE "-framework OpenGL")
endif()
