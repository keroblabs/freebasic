REM Phase 7 Test: ENVIRON / ENVIRON$
ENVIRON "FBTEST=HELLO_WORLD"
PRINT ENVIRON$("FBTEST")
REM Test numeric index form
v$ = ENVIRON$(1)
IF LEN(v$) > 0 THEN
    PRINT "First env var exists"
ELSE
    PRINT "No env vars?"
END IF
REM Test non-existent var
PRINT "NOEXIST: "; ENVIRON$("FB_NONEXIST_12345")
END
