cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app LANGUAGES C)

# zephyr_include_directories("./../../include")

# file(GLOB APP_SOURCES "./../../src/*.c")
# list(REMOVE_ITEM APP_SOURCES "./../../src/main.c")
list(APPEND APP_SOURCES "./main.c")
target_sources(app PRIVATE ${APP_SOURCES})