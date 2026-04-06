REM Phase 3 Test: Arrays
DEFINT A-Z
DIM a(5)
FOR i = 0 TO 5
    a(i) = i * 10
NEXT i
FOR i = 0 TO 5
    PRINT a(i);
NEXT i
PRINT

' Multi-dimensional
DIM b(2, 3)
FOR i = 0 TO 2
    FOR j = 0 TO 3
        b(i, j) = i * 10 + j
    NEXT j
NEXT i
PRINT b(1, 2)

' Explicit bounds
DIM c(1 TO 5)
FOR i = 1 TO 5
    c(i) = i
NEXT i
PRINT c(3)

' LBOUND / UBOUND
PRINT LBOUND(c); UBOUND(c)
