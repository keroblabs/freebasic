REM Bubble Sort - Phase 3 Milestone
DEFINT A-Z
DIM a(1 TO 10)
' Initialize with unsorted data
DATA 64, 34, 25, 12, 22, 11, 90, 1, 45, 78
FOR i = 1 TO 10
    READ a(i)
NEXT i

' Bubble sort
FOR i = 1 TO 9
    FOR j = 1 TO 10 - i
        IF a(j) > a(j + 1) THEN
            SWAP a(j), a(j + 1)
        END IF
    NEXT j
NEXT i

' Print sorted
PRINT "Sorted:";
FOR i = 1 TO 10
    PRINT a(i);
NEXT i
PRINT
