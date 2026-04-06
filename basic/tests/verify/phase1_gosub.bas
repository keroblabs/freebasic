' Test GOSUB/RETURN
PRINT "Start"
GOSUB Greet
PRINT "Middle"
GOSUB Greet
PRINT "Done"
END

Greet:
    PRINT "Hello from gosub!"
RETURN
