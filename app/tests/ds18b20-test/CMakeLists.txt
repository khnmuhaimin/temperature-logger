cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app LANGUAGES C)

list(APPEND APP_SOURCES "main.c")
target_sources(app PRIVATE ${APP_SOURCES})