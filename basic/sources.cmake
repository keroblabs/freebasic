set(BASIC_DIR ${CMAKE_CURRENT_LIST_DIR})

set(BASIC_SOURCE_FILES
    ${BASIC_DIR}/src/lexer.c
    ${BASIC_DIR}/src/value.c
    ${BASIC_DIR}/src/symtable.c
    ${BASIC_DIR}/src/ast.c
    ${BASIC_DIR}/src/parser.c
    ${BASIC_DIR}/src/interpreter.c
    ${BASIC_DIR}/src/coerce.c
    ${BASIC_DIR}/src/error.c
    ${BASIC_DIR}/src/console.c
    ${BASIC_DIR}/src/builtins_str.c
    ${BASIC_DIR}/src/builtins_math.c
    ${BASIC_DIR}/src/print_using.c
    ${BASIC_DIR}/src/array.c
    ${BASIC_DIR}/src/udt.c
    ${BASIC_DIR}/src/builtins_convert.c
    ${BASIC_DIR}/src/callframe.c
    ${BASIC_DIR}/src/fileio.c
)

set(BASIC_INCLUDE_DIR ${BASIC_DIR}/include)
