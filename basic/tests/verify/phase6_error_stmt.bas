REM Phase 6 Test: ERROR n statement
ON ERROR GOTO Handler
PRINT "Testing ERROR statement"
ERROR 53
PRINT "After file not found error"
ERROR 9
PRINT "After subscript error"
GOTO Done

Handler:
PRINT "Error"; ERR
RESUME NEXT

Done:
PRINT "Done"
END
