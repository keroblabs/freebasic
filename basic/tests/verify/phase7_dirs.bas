REM Phase 7 Test: MKDIR / CHDIR / RMDIR
ON ERROR GOTO DirErr
MKDIR "FBTESTDIR7"
PRINT "Directory created"
CHDIR "FBTESTDIR7"
PRINT "Changed into directory"
CHDIR ".."
PRINT "Changed back"
RMDIR "FBTESTDIR7"
PRINT "Directory removed"
GOTO Done

DirErr:
PRINT "Directory error:"; ERR
RESUME NEXT

Done:
PRINT "Done"
END
