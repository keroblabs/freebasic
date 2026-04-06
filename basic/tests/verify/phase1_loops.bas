' Test WHILE..WEND
x = 1
WHILE x <= 5
    PRINT x;
    x = x + 1
WEND
PRINT

' Test DO..LOOP UNTIL
y = 10
DO
    PRINT y;
    y = y - 2
LOOP UNTIL y < 2
PRINT

' Nested FOR
FOR i = 1 TO 3
    FOR j = 1 TO 3
        PRINT i * 10 + j;
    NEXT j
    PRINT
NEXT i
