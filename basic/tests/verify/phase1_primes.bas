' Prime numbers using trial division (no arrays yet)
FOR n = 2 TO 50
    isPrime = 1
    FOR d = 2 TO n - 1
        IF n MOD d = 0 THEN
            isPrime = 0
            EXIT FOR
        END IF
    NEXT d
    IF isPrime THEN PRINT n;
NEXT n
PRINT
