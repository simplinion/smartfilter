cmake_minimum_required(VERSION 3.10)

project(smartfilter VERSION 0.1.0 LANGUAGES C)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.local" CACHE PATH
        "Installation prefix" FORCE)
endif()
include(GNUInstallDirs)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(smartfilter_base base/base.c)
target_include_directories(smartfilter_base PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(smartfilter main.c level1/level1.c)
target_link_libraries(smartfilter PRIVATE smartfilter_base)
target_compile_definitions(smartfilter PRIVATE _POSIX_C_SOURCE=200809L)

install(TARGETS smartfilter RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

include(CTest)
if(BUILD_TESTING)
    add_executable(pipeline_test tests/pipeline_test.c)
    target_link_libraries(pipeline_test PRIVATE smartfilter_base)
    add_test(NAME pipeline COMMAND pipeline_test)
endif()
