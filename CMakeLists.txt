cmake_minimum_required(VERSION 3.10)
project(penguin C)

set(CMAKE_C_STANDARD 23)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBGIT2 REQUIRED libgit2)

add_executable(penguin src/main.c
        src/pen-main/penguin-main.h
        src/pen-main/penguin-main.c
        src/antarctic/antarctic_env.h
        src/antarctic/antarctic_env.c
        src/pen-lan/lex.h
        src/pen-lan/lex.c
        src/pen-lan/parse.h
        src/pen-lan/parse.c
        src/pen-lan/expand.h
        src/pen-lan/expand.c
        src/pen-lan/lan_structs.h
        src/pen-util/pen-util.h)

target_include_directories(penguin PRIVATE pen-main antarctic pen-lan pen-util ${LIBGIT2_INCLUDE_DIRS})

target_link_libraries(penguin PRIVATE readline ${LIBGIT2_LIBRARIES})
