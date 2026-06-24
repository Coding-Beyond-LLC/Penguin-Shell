cmake_minimum_required(VERSION 3.10)
project(penguin C)

set(CMAKE_C_STANDARD 23)

add_executable(penguin main.c
        pen-main/penguin-main.h
        pen-main/penguin-main.c
        antarctic/antarctic_env.h
        antarctic/antarctic_env.c
        pen-lan/lex.h
        pen-lan/lex.c
        pen-lan/parse.h
        pen-lan/parse.c
        pen-lan/expand.h
        pen-lan/expand.c
        pen-lan/lan_structs.h
        pen-util/pen-util.h)

target_include_directories(penguin PRIVATE pen-main antarctic pen-lan pen-util)

target_link_libraries(penguin PRIVATE readline)
