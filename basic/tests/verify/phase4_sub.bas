REM Phase 4 Test: SUB procedures
DECLARE SUB Greet (name$)
DECLARE SUB AddOne (x%)

DIM n AS INTEGER
n = 10
CALL AddOne(n)
PRINT "After AddOne:"; n

Greet "World"

SUB Greet (name$)
    PRINT "Hello, "; name$; "!"
END SUB

SUB AddOne (x%)
    x% = x% + 1
END SUB
