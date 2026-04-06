REM Phase 5 Test: Random-access file I/O
TYPE RecordType
    fname AS STRING * 20
    age AS INTEGER
END TYPE

DIM rec AS RecordType

OPEN "test_rnd.dat" FOR RANDOM AS #1 LEN = 22

' Write records
rec.fname = "Alice"
rec.age = 25
PUT #1, 1, rec

rec.fname = "Bob"
rec.age = 30
PUT #1, 2, rec

rec.fname = "Charlie"
rec.age = 35
PUT #1, 3, rec

' Read back record 2
GET #1, 2, rec
PRINT RTRIM$(rec.fname); rec.age

' Read record 1
GET #1, 1, rec
PRINT RTRIM$(rec.fname); rec.age

' Test LOF
PRINT "LOF:"; LOF(1)

CLOSE #1
KILL "test_rnd.dat"
PRINT "Done"
