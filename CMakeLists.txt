cmake_minimum_required(VERSION 3.16)
project(zeffalie_come_back C)

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

file(GLOB SOURCES src/*.c)
add_executable(zeffalie_come_back ${SOURCES})
target_link_libraries(zeffalie_come_back PRIVATE raylib)

if (UNIX AND NOT APPLE)
    target_link_libraries(zeffalie_come_back PRIVATE m GL)
elseif (WIN32)
    target_link_libraries(zeffalie_come_back PRIVATE opengl32 ws2_32)
    target_sources(zeffalie_come_back PRIVATE assets/icon.rc)   # icone de l'exe
elseif (APPLE)
    target_link_libraries(zeffalie_come_back PRIVATE "-framework OpenGL")
endif()
