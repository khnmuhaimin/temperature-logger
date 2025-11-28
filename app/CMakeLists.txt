cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app LANGUAGES C)

zephyr_include_directories(include)

message(STATUS "Value of BUILD_TEST_APP environment variable is: ${CONFIG_BUILD_TEST_APP}")
if(NOT CONFIG_BUILD_TEST_APP EQUAL "y")
    file(GLOB APP_SOURCES "src/*.c")
    target_sources(app PRIVATE ${APP_SOURCES})
endif()