REM Robust file handler - Phase 6 Milestone
ON ERROR GOTO FileError
OPEN "nonexistent_file_12345.txt" FOR INPUT AS #1
PRINT "File opened OK"
GOTO Done

FileError:
PRINT "File error"; ERR
SELECT CASE ERR
    CASE 53
        PRINT "File not found"
    CASE 55
        PRINT "File already open"
    CASE ELSE
        PRINT "Unknown error"
END SELECT
RESUME Done

Done:
PRINT "Program completed gracefully"
END
