## Plan: Phased FreeBASIC Interpreter in Pure C

**TL;DR:** Build a FreeBASIC 4.5 interpreter in pure C across 11 phases, starting with a working core (lexer, expression evaluator, PRINT, IF, FOR) that can run simple programs by Phase 1, then incrementally adding I/O, strings, math, arrays, procedures, file I/O, error handling, and optional graphics/sound. The architecture uses a tree-walking interpreter with a tagged-union value system and ref-counted strings. Each phase ends with a concrete milestone — a class of programs the interpreter can run.

**Steps**

### Phase 0 — Foundation & Architecture
1. **Lexer/Tokenizer** — tokenize FB source: keywords (case-insensitive), identifiers with type suffixes (`%`, `&`, `!`, `#`, `$`), numeric literals (decimal, `&H` hex, `&O` octal), string literals, operators, line numbers/labels, `:` as statement separator, `_` line continuation
2. **Value system** — tagged-union `FBValue`: `FB_INTEGER` (int16), `FB_LONG` (int32), `FB_SINGLE` (float), `FB_DOUBLE` (double), `FB_STRING` (ref-counted `{char* data, int32_t len}`)
3. **String allocator** — ref-counted strings with copy-on-write semantics (FB strings are mutable, variable-length, up to 32K, passed by reference)
4. **Symbol table** — case-insensitive hash table with scope stack (module-level, procedure-level); supports DEFtype letter-range defaults
5. **Program representation** — array of parsed statement AST nodes, label/line-number lookup table
6. **Type coercion engine** — automatic promotion: integer → long → single → double; float→int rounding; TRUE = -1, FALSE = 0

### Phase 1 — Core Interpreter + Basic Statements (First Working Programs)
7. **Expression parser** — full precedence-climbing parser: `^`, unary `-`/`+`, `*`/`/`, `\` (int div), `MOD`, `+`/`-`, relationals (`=`,`<>`,`<`,`>`,`<=`,`>=`), `NOT`, `AND`, `OR`, `XOR`, `EQV`, `IMP`. String `+` concatenation and string comparison
8. **Assignment** — `[LET] var = expr` with automatic type coercion
9. **PRINT** — expression list with `;` (compact) and `,` (14-char zone) separators, trailing `;` suppresses newline, number formatting (leading space for positive, `-` for negative)
10. **REM / '** — comments
11. **END / STOP / SYSTEM** — program termination
12. **GOTO** — label and line number targets
13. **IF...THEN...ELSE** — single-line form (`IF expr THEN stmts [ELSE stmts]`, THEN can be followed by line number) AND block form (`IF`/`ELSEIF`/`ELSE`/`END IF`)
14. **FOR...NEXT** — with `STEP`, floating-point counters, `NEXT` matching multiple `FOR`s
15. **WHILE...WEND** — simple pre-test loop
16. **DO...LOOP** — all 4 variants: `DO WHILE`/`DO UNTIL` (pre-test), `LOOP WHILE`/`LOOP UNTIL` (post-test)
17. **GOSUB...RETURN** — runtime return-address stack
18. **DIM** — simple scalar variables with `AS type`
19. **CONST** — symbolic constants
20. **DEFtype** — `DEFINT`, `DEFLNG`, `DEFSNG`, `DEFDBL`, `DEFSTR` letter-range defaults
- **Milestone:** FizzBuzz, Fibonacci, prime sieves, simple loops

### Phase 2 — I/O, Strings, Math (Interactive Programs)
21. **INPUT** — prompt string, multi-variable input, type checking, "Redo from start" on mismatch
22. **LINE INPUT** — read entire line into string variable
23. **INKEY$** — non-blocking single keypress (needs terminal raw mode on Windows/Unix)
24. **CLS** — clear screen (ANSI escape or Win32 console API)
25. **LOCATE** — cursor positioning
26. **COLOR** — text-mode foreground/background (ANSI sequences or Win32 console)
27. **CSRLIN / POS** — cursor row/column queries
28. **SPC / TAB** — print spacing functions
29. **WRITE** — comma-delimited output with quoted strings
30. **BEEP** — emit BEL character
31. **PRINT USING** — all ~15 format codes: `#`, `.`, `,`, `+`, `-`, `$$`, `**`, `^^^^`, `!`, `\...\`, `&`, `_` literal escape
32. **VIEW PRINT / WIDTH** — scrolling region and screen dimensions
33. **All string functions:** `LEFT$`, `RIGHT$`, `MID$` (function), `MID$` (statement — lvalue replacement), `LEN`, `INSTR`, `CHR$`, `ASC`, `STR$`, `VAL`, `UCASE$`, `LCASE$`, `LTRIM$`, `RTRIM$`, `STRING$`, `SPACE$`, `HEX$`, `OCT$`
34. **All math functions:** `ABS`, `INT`, `FIX`, `SQR`, `SIN`, `COS`, `TAN`, `ATN`, `LOG`, `EXP`, `SGN`, `RND`, `RANDOMIZE` (including `RANDOMIZE TIMER`), `CINT`, `CLNG`, `CSNG`, `CDBL`
35. **DATA / READ / RESTORE** — collect all DATA lines into an ordered global pool at parse time; runtime read pointer; RESTORE resets to label or start
36. **SELECT CASE** — value lists, ranges (`x TO y`), `IS` relational operator, `CASE ELSE`, `END SELECT`
37. **INPUT$** — read exactly n characters
- **Milestone:** Text adventure games, quiz programs, calculators, menu-driven apps

### Phase 3 — Arrays & User-Defined Types
38. **DIM arrays** — multi-dimensional (up to 60 dims), `lower TO upper` bounds, `OPTION BASE`, default 0-10 auto-dimensioning
39. **REDIM** — resize dynamic arrays (deallocate + reallocate)
40. **ERASE** — reinitialize static arrays, deallocate dynamic ones
41. **LBOUND / UBOUND** — query bounds by dimension
42. **SWAP** — any type including arrays-of-UDT elements
43. **Static vs Dynamic arrays** — `DIM arr(10)` = static (constant bounds), `DIM arr(N)` or `REDIM` = dynamic
44. **TYPE...END TYPE** — user-defined record types, dot-notation access (`record.field`), arrays of records, fixed-length `STRING * n` fields in types, nested assignment
45. **FIELD / LSET / RSET** — map random-access record buffer fields to string variables
46. **MKI$ / MKL$ / MKS$ / MKD$ / CVI / CVL / CVS / CVD / MKSMBF$ / MKDMBF$ / CVSMBF / CVDMBF** — pack/unpack numerics to string bytes
- **Milestone:** Sorting algorithms, address-book programs, record-based data structures

### Phase 4 — Procedures & Scoping
47. **SUB...END SUB** — pass by reference, `CALL name(args)` and `name args` invocations, `EXIT SUB`, `STATIC` option for persistent locals
48. **FUNCTION...END FUNCTION** — same as SUB + return via `FunctionName = expr`, recursive calls
49. **DECLARE** — forward declaration / prototype with parameter type checking
50. **Call-frame stack** — proper stack frames for recursion, local variable allocation per call
51. **Scoping rules** — module-level vs procedure-level; `DIM SHARED` for globals; `SHARED varlist` inside SUB/FUNCTION to alias module-level vars; `STATIC varlist` for persistent locals
52. **DEF FN...END DEF** — legacy functions sharing module-level scope (except parameters and STATIC vars); `EXIT DEF`
53. **ON...GOSUB / ON...GOTO** — computed branch on expression value (1-based index into label list)
54. **Passing by value** — `CALL Sub((expr))` (extra parens) forces by-value
- **Milestone:** Tower of Hanoi, recursive algorithms, multi-procedure structured programs

### Phase 5 — File I/O
55. **OPEN** (both syntaxes) — all 5 modes: INPUT, OUTPUT, APPEND, RANDOM, BINARY; ACCESS/LOCK clauses; `LEN=reclen`
56. **Sequential I/O** — `PRINT #`, `PRINT # USING`, `WRITE #`, `INPUT #`, `LINE INPUT #`
57. **Random-access I/O** — `GET #n, recnum, var`, `PUT #n, recnum, var` with FIELD or TYPE'd variables
58. **Binary I/O** — GET/PUT at byte offset
59. **File functions** — `EOF`, `LOF`, `LOC`, `SEEK` (statement + function), `FREEFILE`, `FILEATTR`, `INPUT$(n, #filenum)`
60. **File management** — `CLOSE`, `RESET`, `NAME...AS`, `KILL`, `FILES`, `LOCK/UNLOCK`
61. **File numbers** — support #1 through #255
- **Milestone:** File copy utilities, CSV processors, simple database apps

### Phase 6 — Error Handling
62. **ON ERROR GOTO** — register module-level handler; `ON ERROR GOTO 0` to disable; search up call chain
63. **RESUME** — `RESUME` (retry), `RESUME NEXT` (skip), `RESUME label` (goto) — use `setjmp`/`longjmp` to save/restore execution context
64. **ERR / ERL** — global error code and line number
65. **ERROR n** — simulate any error
66. **~75 standard error codes** — "Subscript out of range" (9), "Division by zero" (11), "Type mismatch" (13), "File not found" (53), etc.
67. **TRON / TROFF** — line number tracing for debugging
- **Milestone:** Robust programs with error recovery, file error handling

### Phase 7 — DOS / System Interface
68. **SHELL** — `system()` call
69. **ENVIRON / ENVIRON$** — get/set environment variables
70. **COMMAND$** — command-line arguments
71. **CHDIR / MKDIR / RMDIR** — directory operations
72. **DATE$ / TIME$ / TIMER** — date/time access
73. **FRE / CLEAR** — memory info and variable reset
74. **LPRINT / LPRINT USING** — printer output (redirect to file/pipe)
75. **Stubs for low-level** — `PEEK`, `POKE`, `DEF SEG`, `INP`, `OUT`, `VARPTR`, `VARSEG`, `BLOAD`, `BSAVE`, `CALL ABSOLUTE`, `CALL INTERRUPT`, `WAIT`, `SETMEM` — print warning or no-op
- **Milestone:** System utility programs, environment-aware scripts

### Phase 8 — Graphics (Optional, requires SDL2)
76. **SCREEN modes** — start with modes 0 (text), 1 (320×200×4), 2 (640×200×2), 7 (320×200×16), 12 (640×480×16), 13 (320×200×256)
77. **Pixel operations** — `PSET`, `PRESET`, `POINT` (with STEP for relative coords, WINDOW coordinate mapping)
78. **Lines & boxes** — `LINE (x1,y1)-(x2,y2), color, B/BF, style` — Bresenham + box/fill + line style bitmask
79. **Circles & arcs** — `CIRCLE (x,y), r, color, start, end, aspect` — midpoint algorithm, arc segments, ellipses
80. **Flood fill** — `PAINT (x,y), paintcolor, bordercolor` — scanline fill + pattern tile support
81. **DRAW macro language** — embedded DSL: `U`/`D`/`L`/`R`/`E`/`F`/`G`/`H` movement, `M` absolute/relative, `C` color, `S` scale, `A`/`TA` angle, `B` no-draw, `N` no-move, `P` paint, `X` substring execution
82. **Screen operations** — `GET`/`PUT` (graphics), `VIEW`, `WINDOW`, `PMAP`, `PALETTE`, `PCOPY`, `COLOR` (graphics modes)
- **Milestone:** Drawing programs, retro games, data visualization

### Phase 9 — Sound (Optional, requires SDL2 audio)
83. **SOUND** — `SOUND freq, duration` — tone generation via audio buffer
84. **PLAY macro language** — embedded music DSL: notes `A-G`, sharps/flats `+`/`-`/`#`, octaves `O`/`>`/`<`, lengths `L`, tempo `T`, mode `MF`/`MB`/`MN`, rests `P`, dotted notes
85. **PLAY(n) function** — notes remaining in background buffer
- **Milestone:** Music programs, games with sound effects

### Phase 10 — Event Trapping (Optional)
86. **ON KEY(n) GOSUB** / **KEY(n) ON/OFF/STOP** — keyboard event polling
87. **ON TIMER(n) GOSUB** / **TIMER ON/OFF/STOP** — timer events
88. **Cooperative polling** — check event flags between each statement execution
89. **ON COM / ON PEN / ON STRIG / ON PLAY** — other event sources (most stubbed)
- **Milestone:** Real-time interactive programs, timed events

### Phase 11 — Advanced/Rare Features (As Needed)
90. **CHAIN / COMMON** — load another .BAS program sharing variables
91. **RUN** — restart or load/run another program
92. **OPEN "COMn:"** — serial communications
93. **Multi-module support** — separate compilation units with DECLARE across modules
94. **SLEEP** with timeout, **UEVENT**, other niche features

---

**Verification**
- Each phase includes a milestone class of test programs. Build a test suite of classic FB programs: FizzBuzz, Fibonacci, number guessing game, text adventure, file manager, GORILLAS.BAS (graphics milestone)
- Run automated tests comparing output against a reference FB implementation (QB64 or FreeBASIC in FB-compat mode)
- Use Valgrind/AddressSanitizer to verify string memory management (critical in Phases 0-2)
- PRINT formatting: test extensively against known FB output (number spacing, zones, PRINT USING)

**Decisions**
- **Tree-walking interpreter** over bytecode VM — simpler to implement, easier debugging; bytecode can be retrofitted later for performance
- **Ref-counted strings** over garbage collection — deterministic, simpler for C, avoids GC pauses
- **SDL2 for graphics/sound** — cross-platform, widely available, well-documented; graphics/sound are optional phases
- **setjmp/longjmp for ON ERROR/RESUME** — matches FB's non-local error recovery semantics
- **Console I/O via ANSI escapes + Win32 Console API fallback** — avoids ncurses dependency while supporting LOCATE/COLOR
