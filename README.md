# FBasic — A FreeBASIC Interpreter in Pure C

FBasic is a tree-walking BASIC interpreter written in pure C (C99), targeting compatibility with classic BASIC dialects. It includes a hand-written lexer, recursive-descent parser, AST representation, and a full interpreter with ref-counted strings, multi-dimensional arrays, user-defined types, file I/O, error handling, and system integration.

## Features

**Implemented (Phases 0–7):**

- **Core language** — `PRINT`, `IF/ELSEIF/ELSE/END IF`, `FOR/NEXT`, `WHILE/WEND`, `DO/LOOP`, `GOTO`, `GOSUB/RETURN`, `SELECT CASE`, `LET`, `DIM`, `CONST`, `DEFtype`, `END/STOP/SYSTEM`
- **Expressions** — Full operator precedence (`^`, `*`, `/`, `\`, `MOD`, `+`, `-`, relationals, `NOT`, `AND`, `OR`, `XOR`, `EQV`, `IMP`), string concatenation, type coercion
- **String functions** — `LEFT$`, `RIGHT$`, `MID$`, `LEN`, `INSTR`, `CHR$`, `ASC`, `STR$`, `VAL`, `UCASE$`, `LCASE$`, `LTRIM$`, `RTRIM$`, `STRING$`, `SPACE$`, `HEX$`, `OCT$`
- **Math functions** — `ABS`, `INT`, `FIX`, `SQR`, `SIN`, `COS`, `TAN`, `ATN`, `LOG`, `EXP`, `SGN`, `RND`, `RANDOMIZE`, `CINT`, `CLNG`, `CSNG`, `CDBL`
- **I/O** — `INPUT`, `LINE INPUT`, `WRITE`, `PRINT USING`, `CLS`, `LOCATE`, `COLOR`, `DATA/READ/RESTORE`
- **Arrays** — Multi-dimensional (up to 60 dims), `DIM`, `REDIM`, `ERASE`, `LBOUND`, `UBOUND`, `SWAP`, static and dynamic
- **User-defined types** — `TYPE...END TYPE`, dot-notation field access, arrays of records, `FIELD/LSET/RSET`
- **Procedures** — `SUB`, `FUNCTION`, `DECLARE`, `DEF FN`, pass-by-reference (default) and by-value, recursion, `DIM SHARED`, `STATIC`, `ON GOSUB/GOTO`
- **File I/O** — `OPEN` (INPUT, OUTPUT, APPEND, RANDOM, BINARY), `PRINT#`, `WRITE#`, `INPUT#`, `LINE INPUT#`, `GET/PUT`, `EOF`, `LOF`, `LOC`, `SEEK`, `FREEFILE`, `NAME`, `KILL`, `CLOSE/RESET`
- **Error handling** — `ON ERROR GOTO`, `RESUME/RESUME NEXT/RESUME label`, `ERR/ERL`, `ERROR n`, `TRON/TROFF`, ~75 standard error codes, `setjmp`/`longjmp`-based dispatch
- **System interface** — `SHELL`, `ENVIRON/ENVIRON$`, `COMMAND$`, `CHDIR/MKDIR/RMDIR`, `DATE$/TIME$/TIMER`, `FRE/CLEAR`, `PEEK/POKE/DEF SEG` (stubs)
- **Numeric conversions** — `MKI$/CVI`, `MKL$/CVL`, `MKS$/CVS`, `MKD$/CVD`, `MKSMBF$/CVSMBF`, `MKDMBF$/CVDMBF`

**Planned (Phases 8–11):**

- Graphics via SDL2 (`SCREEN`, `PSET`, `LINE`, `CIRCLE`, `PAINT`, `DRAW`, `GET/PUT`)
- Sound via SDL2 (`SOUND`, `PLAY` macro language)
- Event trapping (`ON KEY`, `ON TIMER`)
- `CHAIN/COMMON`, `RUN`, multi-module support

## Building

### Prerequisites

- GCC (or any C99-compatible compiler)
- CMake 3.16+ (optional — a standalone Makefile is also provided)
- Linux (POSIX APIs used for terminal, time, file system)

### With CMake

```sh
mkdir build && cd build
cmake ../basic -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

The binary is produced at `build/fbasic`.

### With Make

```sh
cd basic
make -j$(nproc)
```

The binary is produced at `basic/fbasic`.

## Usage

```sh
./fbasic program.bas
```

Diagnostic modes:

```sh
./fbasic --lex program.bas     # Dump token stream
./fbasic --parse program.bas   # Dump AST
```

### Example

```basic
PRINT "Hello, FBasic!"
PRINT 2 + 3
PRINT "The answer is"; 42
END
```

```
Hello, FBasic!
 5
The answer is 42
```

## Running Tests

The test suite covers phases 1–7 with 32 test programs and expected-output comparison:

```sh
cd basic
make test
```

Individual phases can be tested separately:

```sh
make test-phase1   # Core language (7 tests)
make test-phase2   # I/O, strings, math (6 tests)
make test-phase3   # Arrays & UDTs (4 tests)
make test-phase4   # Procedures & scoping (4 tests)
make test-phase5   # File I/O (2 tests)
make test-phase6   # Error handling (5 tests)
make test-phase7   # System interface (6 tests)
```

## Project Structure

```
freebasic/
├── README.md
├── basic/                      # Source tree
│   ├── CMakeLists.txt
│   ├── Makefile
│   ├── include/
│   │   ├── fb.h                # Master include
│   │   ├── token.h             # Token types & Token struct
│   │   ├── lexer.h             # Lexer API
│   │   ├── ast.h               # AST node types
│   │   ├── parser.h            # Parser API
│   │   ├── value.h             # FBValue tagged union, FBString, FBType enum
│   │   ├── coerce.h            # Type promotion & arithmetic
│   │   ├── error.h             # FBErrorCode enum, error reporting
│   │   ├── symtable.h          # Symbol table (hash map, scoping)
│   │   ├── interpreter.h       # Interpreter state & execution
│   │   ├── array.h             # FBArray multi-dimensional arrays
│   │   ├── udt.h               # User-defined type registry
│   │   ├── callframe.h         # Call stack for SUB/FUNCTION
│   │   ├── fileio.h            # File I/O abstraction layer
│   │   ├── platform.h        # System operations abstraction
│   │   ├── console.h           # Terminal/console operations
│   │   ├── builtins_str.h      # String built-in functions
│   │   ├── builtins_math.h     # Math built-in functions
│   │   ├── builtins_convert.h  # MKI$/CVI numeric conversion
│   │   └── print_using.h       # PRINT USING formatter
│   ├── src/                    # Implementation (.c files)
│   └── tests/verify/           # Test programs & expected output
│       ├── phase1_*.bas        # Phase 1 test programs
│       ├── phase1_expected/    # Expected output for phase 1
│       ├── ...
│       └── phase7_expected/
├── build/                      # CMake build directory
└── docs/
    ├── implementation.md       # Overall implementation plan
    ├── implementation_phase_*.md  # Detailed phase specs
    └── freebasic.txt           # Language reference (spec document)
```

## Architecture

```
Source (.bas) → Lexer → Tokens → Parser → AST → Interpreter → Output
```

- **Lexer** — Single-pass tokenizer. Case-insensitive keywords, type suffixes (`%`, `&`, `!`, `#`, `$`), hex/octal literals, line continuation (`_`).
- **Parser** — Recursive descent with precedence climbing for expressions. Produces an AST of `ASTNode` structs.
- **Value system** — Tagged union `FBValue` with types: `FB_INTEGER` (int16), `FB_LONG` (int32), `FB_SINGLE` (float), `FB_DOUBLE` (double), `FB_STRING` (ref-counted), `FB_UDT`.
- **Strings** — Ref-counted `FBString` with copy-on-write. Automatic memory management via `fbstr_ref()`/`fbstr_unref()`.
- **Interpreter** — Tree-walking execution. Statement dispatch via `switch` on AST node type. Manages symbol tables, call stack, file table, DATA pointer, error handler, and GOSUB return stack.
- **Portability layer** — `FBFileOps` and `FBSysOps` vtable structs abstract file I/O and system calls for future platform ports.

## Design Decisions

- **Automatic type coercion**: `INTEGER → LONG → SINGLE → DOUBLE` following classic BASIC rules
- **Pass-by-reference default**: Variables passed to SUB/FUNCTION by reference; extra parens `(expr)` or `BYVAL` forces by-value
- **TRUE = -1, FALSE = 0**: Bitwise semantics matching classic BASIC
- **DEFtype support**: `DEFINT A-Z` etc. sets default types for undeclared variables by first letter
- **Error handling**: `setjmp`/`longjmp` for non-local jumps from `ON ERROR GOTO` handlers with proper `RESUME` support

## License

This project is for educational and personal use.
