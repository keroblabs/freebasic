REM Phase 6 Test: ERR and ERL
ON ERROR GOTO Handler
10 DIM x%
20 x% = 1 \ 0
30 PRINT "After error"
GOTO Done

Handler:
PRINT "Error"; ERR; "at line"; ERL
RESUME NEXT

Done:
PRINT "Done"
END
