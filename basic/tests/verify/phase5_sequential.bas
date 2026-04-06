REM Phase 5 Test: Sequential file I/O
' Write data
OPEN "test_seq.txt" FOR OUTPUT AS #1
PRINT #1, "Hello, World!"
PRINT #1, 42
PRINT #1, 3.14
WRITE #1, "Alice", 30
CLOSE #1

' Read back with LINE INPUT
OPEN "test_seq.txt" FOR INPUT AS #1
DIM line1 AS STRING, line2 AS STRING, line3 AS STRING
LINE INPUT #1, line1
LINE INPUT #1, line2
LINE INPUT #1, line3

' Read WRITE-formatted data with INPUT
DIM person AS STRING, age AS INTEGER
INPUT #1, person, age
CLOSE #1

PRINT line1
PRINT line2
PRINT line3
PRINT person; age

' Use FREEFILE
DIM f AS INTEGER
f = FREEFILE
PRINT "Free file:"; f

' Test EOF
OPEN "test_seq.txt" FOR INPUT AS #1
DIM count AS INTEGER
count = 0
DO WHILE NOT EOF(1)
    DIM tmp AS STRING
    LINE INPUT #1, tmp
    count = count + 1
LOOP
CLOSE #1
PRINT "Lines:"; count

' Cleanup
KILL "test_seq.txt"
PRINT "Done"
