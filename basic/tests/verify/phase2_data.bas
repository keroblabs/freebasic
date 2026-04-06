' Phase 2: DATA/READ/RESTORE test
DATA 10, 20, 30
DATA "Hello", "World"

READ a, b, c
PRINT a; b; c

READ d$, e$
PRINT d$; " "; e$

RESTORE
READ x
PRINT "After restore:"; x
