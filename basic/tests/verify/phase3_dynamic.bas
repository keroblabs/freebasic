REM Phase 3 Test: Dynamic arrays
DEFINT A-Z
DIM n AS INTEGER
n = 5
REDIM a(1 TO n)
FOR i = 1 TO n
    a(i) = i * 2
NEXT i
FOR i = 1 TO n
    PRINT a(i);
NEXT i
PRINT

' Resize
n = 3
REDIM a(1 TO n)
FOR i = 1 TO n
    a(i) = i * 100
NEXT i
FOR i = 1 TO n
    PRINT a(i);
NEXT i
PRINT

' ERASE
ERASE a
PRINT "Erased."
