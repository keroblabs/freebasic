' Phase 2: SUB and FUNCTION test
DECLARE SUB ShowNum (n AS INTEGER)
DECLARE FUNCTION Double% (n AS INTEGER)

CALL ShowNum(5)
PRINT Double%(7)

SUB ShowNum (n AS INTEGER)
    PRINT "Number is:"; n
END SUB

FUNCTION Double% (n AS INTEGER)
    Double% = n * 2
END FUNCTION
