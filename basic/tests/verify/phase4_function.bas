REM Phase 4 Test: FUNCTION procedures
DECLARE FUNCTION Square% (n%)
DECLARE FUNCTION Factorial& (n%)
DECLARE FUNCTION Max% (a%, b%)

PRINT "Square(5) ="; Square%(5)
PRINT "Factorial(6) ="; Factorial&(6)
PRINT "Max(3, 7) ="; Max%(3, 7)

FUNCTION Square% (n%)
    Square% = n% * n%
END FUNCTION

FUNCTION Factorial& (n%)
    IF n% <= 1 THEN
        Factorial& = 1
    ELSE
        Factorial& = n% * Factorial&(n% - 1)
    END IF
END FUNCTION

FUNCTION Max% (a%, b%)
    IF a% > b% THEN
        Max% = a%
    ELSE
        Max% = b%
    END IF
END FUNCTION
