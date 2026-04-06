REM Tower of Hanoi - Phase 4 Milestone
DECLARE SUB Hanoi (n%, fromPeg$, toPeg$, auxPeg$)

Hanoi 3, "A", "C", "B"

SUB Hanoi (n%, fromPeg$, toPeg$, auxPeg$)
    IF n% = 1 THEN
        PRINT "Move disk 1 from "; fromPeg$; " to "; toPeg$
    ELSE
        Hanoi n% - 1, fromPeg$, auxPeg$, toPeg$
        PRINT "Move disk"; n%; "from "; fromPeg$; " to "; toPeg$
        Hanoi n% - 1, auxPeg$, toPeg$, fromPeg$
    END IF
END SUB
