REM Phase 6 Test: ON ERROR GOTO and RESUME
PRINT "=== ON ERROR GOTO ==="

ON ERROR GOTO Handler1
PRINT "Before error"
ERROR 5
PRINT "After RESUME NEXT"
GOTO Test2

Handler1:
PRINT "Caught error"; ERR
RESUME NEXT

Test2:
PRINT "=== RESUME to label ==="
ON ERROR GOTO Handler2
DIM x%
x% = 1 \ 0
PRINT "Should not reach here"
GOTO Test3

Handler2:
PRINT "Division error"; ERR
RESUME AfterDiv

AfterDiv:
PRINT "Resumed at label"

Test3:
PRINT "=== RESUME retry ==="
DIM attempt%
attempt% = 0
ON ERROR GOTO Handler3
DIM y%
y% = 1 \ attempt%
PRINT "Success: y ="; y%
GOTO Test4

Handler3:
attempt% = 1
RESUME

Test4:
PRINT "=== ON ERROR GOTO 0 ==="
ON ERROR GOTO 0
PRINT "Error handling disabled"
PRINT "Done"
END
