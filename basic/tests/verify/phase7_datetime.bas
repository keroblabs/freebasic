REM Phase 7 Test: DATE$ / TIME$ / TIMER
d$ = DATE$
PRINT "Date length:"; LEN(d$)
REM Check format: MM-DD-YYYY (dash at positions 3 and 6)
IF MID$(d$, 3, 1) = "-" AND MID$(d$, 6, 1) = "-" THEN
    PRINT "Date format OK"
ELSE
    PRINT "Date format BAD"
END IF
t$ = TIME$
PRINT "Time length:"; LEN(t$)
REM Check format: HH:MM:SS (colon at positions 3 and 6)
IF MID$(t$, 3, 1) = ":" AND MID$(t$, 6, 1) = ":" THEN
    PRINT "Time format OK"
ELSE
    PRINT "Time format BAD"
END IF
v! = TIMER
IF v! >= 0 AND v! < 86400 THEN
    PRINT "Timer in valid range"
ELSE
    PRINT "Timer out of range!"
END IF
END
