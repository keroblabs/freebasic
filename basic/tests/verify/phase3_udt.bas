REM Phase 3 Test: TYPE...END TYPE
TYPE PersonType
    firstName AS STRING * 15
    lastName AS STRING * 15
    age AS INTEGER
END TYPE

DIM p AS PersonType
p.firstName = "John"
p.lastName = "Doe"
p.age = 30

PRINT p.firstName; " "; p.lastName; ", age"; p.age

' Array of TYPE
DIM people(1 TO 3) AS PersonType
people(1).firstName = "Alice"
people(1).age = 25
people(2).firstName = "Bob"
people(2).age = 30
people(3).firstName = "Charlie"
people(3).age = 35

FOR i% = 1 TO 3
    PRINT people(i%).firstName; ":"; people(i%).age
NEXT i%
