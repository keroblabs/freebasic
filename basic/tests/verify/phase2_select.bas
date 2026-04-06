' Phase 2: SELECT CASE test
FOR x = 1 TO 10
    SELECT CASE x
        CASE 1
            PRINT "one"
        CASE 2, 3
            PRINT "two or three"
        CASE 4 TO 6
            PRINT "four to six"
        CASE IS > 8
            PRINT "greater than eight"
        CASE ELSE
            PRINT "other:"; x
    END SELECT
NEXT x
