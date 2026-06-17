cmake_minimum_required(VERSION 3.10)
project(penguin C)

set(CMAKE_C_STANDARD 23)

add_executable(penguin main.c
        pen-main/penguin-main.h
        pen-main/penguin-main.c
        antarctic/antarctic_env.h
        antarctic/antarctic_env.c
        pen-lan/lex.h
        pen-lan/lex.c)

target_include_directories(penguin PRIVATE pen-main antarctic pen-lan)

target_link_libraries(penguin PRIVATE readline)
