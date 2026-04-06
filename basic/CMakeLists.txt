cmake_minimum_required(VERSION 3.16)

set(MODULE_NAME         fbasic)

project(${MODULE_NAME} C)

add_definitions(-D_POSIX_C_SOURCE=200809L)
add_definitions(-D_DEFAULT_SOURCE)

set(SOURCE_FILES    src/main.c 
                    src/lexer.c 
                    src/value.c 
                    src/symtable.c 
                    src/ast.c 
                    src/parser.c 
                    src/interpreter.c 
                    src/coerce.c 
                    src/error.c 
                    src/console.c 
                    src/builtins_str.c 
                    src/builtins_math.c 
                    src/print_using.c 
                    src/array.c 
                    src/udt.c 
                    src/builtins_convert.c 
                    src/callframe.c 
                    src/fileio.c 
                    src/system_api.c
                )

add_executable(${MODULE_NAME} ${SOURCE_FILES})

target_include_directories( ${MODULE_NAME} PRIVATE
                            include
                        )

target_link_libraries(${MODULE_NAME} PRIVATE m dl)
