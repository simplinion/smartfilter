cmake_minimum_required(VERSION 3.12)

project(smartfilter VERSION 0.1.0 LANGUAGES C)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.local" CACHE PATH
        "Installation prefix" FORCE)
endif()
include(GNUInstallDirs)

option(SMARTFILTER_INSTALL_SERVICE "Prepare, enable and start the current-user embedding service on install" ON)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(smartfilter_base base/base.c)
target_include_directories(smartfilter_base PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(smartfilter main.c level0/level0.c level1/level1.c level1/compact.c level1/paths.c level2/level2.c level3/level3.c level4/level4.c level5/client.c level5/level5.c)
target_link_libraries(smartfilter PRIVATE smartfilter_base m)
target_compile_definitions(smartfilter PRIVATE _POSIX_C_SOURCE=200809L _XOPEN_SOURCE=700)

install(TARGETS smartfilter RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS service/embedding_server.py DESTINATION ${CMAKE_INSTALL_BINDIR} RENAME smartfilter-embeddings)
install(PROGRAMS service/download_model.py DESTINATION ${CMAKE_INSTALL_BINDIR} RENAME smartfilter-download-model)
install(FILES service/requirements.txt service/README.md service/PROTOCOL.md service/install_service.py
        DESTINATION ${CMAKE_INSTALL_DATADIR}/smartfilter)
configure_file(service/smartfilter-embeddings.service.in smartfilter-embeddings.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/smartfilter-embeddings.service
        DESTINATION ${CMAKE_INSTALL_DATADIR}/smartfilter/systemd)
if(SMARTFILTER_INSTALL_SERVICE)
    configure_file(cmake/install_service.cmake.in install_service.cmake @ONLY)
    install(SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/install_service.cmake)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(events_test tests/events_test.c level4/level4.c)
    target_link_libraries(events_test PRIVATE smartfilter_base)
    add_test(NAME event_metadata COMMAND events_test)
    add_executable(pipeline_test tests/pipeline_test.c)
    target_link_libraries(pipeline_test PRIVATE smartfilter_base)
    add_test(NAME pipeline COMMAND pipeline_test)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    add_executable(model_probe tests/model_probe.c level3/level3.c level0/level0.c)
    target_link_libraries(model_probe PRIVATE smartfilter_base m)
    target_compile_definitions(model_probe PRIVATE _POSIX_C_SOURCE=200809L)
    if(Python3_Interpreter_FOUND)
        if(SMARTFILTER_INSTALL_SERVICE)
            add_test(NAME cmake_service_install COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake_service_install_test.py
                ${CMAKE_CURRENT_BINARY_DIR})
        endif()
        add_test(NAME service_install COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/service_install_test.py)
        add_test(NAME semantic_service_lifecycle COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/semantic_service_lifecycle_test.py)
        add_test(NAME semantic_protocol COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/semantic_test.py
            $<TARGET_FILE:smartfilter>)
        add_test(NAME events_cli COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/events_cli_test.py
            $<TARGET_FILE:smartfilter>)
        add_test(NAME categories COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/categories_test.py
            $<TARGET_FILE:smartfilter> $<TARGET_FILE:model_probe>
            ${CMAKE_CURRENT_SOURCE_DIR}/level3/model/reference.json)
        add_test(NAME severity COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/severity_test.py
            $<TARGET_FILE:smartfilter>)
        add_test(NAME blocks COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/blocks_test.py
            $<TARGET_FILE:smartfilter>)
        add_test(NAME compaction COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/compact_test.py
            $<TARGET_FILE:smartfilter>)
        add_test(NAME urls COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/urls_test.py
            $<TARGET_FILE:smartfilter>)
        add_test(NAME paths COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/paths_test.py
            $<TARGET_FILE:smartfilter>)
    endif()
endif()
