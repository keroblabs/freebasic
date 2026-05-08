cmake_minimum_required(VERSION 3.16)

set(MODULE_NAME         fbasic)

project(${MODULE_NAME} C)

add_definitions(-D_POSIX_C_SOURCE=200809L)
add_definitions(-D_DEFAULT_SOURCE)

include(../../../basic/sources.cmake)

set(SOURCE_FILES    ${BASIC_SOURCE_FILES}

                    ../main.c
                    ../linux_fileio.c
                    ../system_api_linux.c
                )

add_executable(${MODULE_NAME} ${SOURCE_FILES})

target_include_directories( ${MODULE_NAME} PRIVATE
                            ${BASIC_INCLUDE_DIR}
                            ../../include
                        )

target_link_libraries(${MODULE_NAME} PRIVATE m dl)
