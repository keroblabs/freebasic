REM Phase 4 Test: Scoping
DECLARE SUB TestShared ()
DECLARE SUB TestByVal (x%)

DIM SHARED globalVar%
globalVar% = 100

TestShared
PRINT "Global after TestShared:"; globalVar%

DIM n%
n% = 42
TestByVal (n%)
PRINT "n after TestByVal with parens:"; n%

SUB TestShared
    PRINT "Inside TestShared, globalVar ="; globalVar%
    globalVar% = 200
END SUB

SUB TestByVal (x%)
    x% = 999
END SUB
