REM Phase 7 Milestone: System utility program
PRINT "=== System Info ==="
d$ = DATE$
PRINT "Date length:"; LEN(d$)
t$ = TIME$
PRINT "Time length:"; LEN(t$)
v! = TIMER
IF v! >= 0 AND v! < 86400 THEN
    PRINT "Timer: valid"
END IF
PRINT
ENVIRON "FB_VERSION=4.5"
PRINT "FB_VERSION = "; ENVIRON$("FB_VERSION")
PRINT "Free memory:"; FRE(0); "bytes"
PRINT
ON ERROR GOTO DirErr
MKDIR "TESTUTIL7"
CHDIR "TESTUTIL7"
CHDIR ".."
RMDIR "TESTUTIL7"
PRINT "Directory operations: OK"
GOTO Done

DirErr:
PRINT "Dir error:"; ERR
RESUME NEXT

Done:
PRINT "=== Milestone passed ==="
END
