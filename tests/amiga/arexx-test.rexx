/* arexx-test.rexx: FileZilla's ARexx port, end to end.
   rx arexx-test.rexx [url password dir file]
   Defaults: test.rebex.net's public SFTP account. Prints one line a step
   and RESULT: PASS or FAIL at the end. */
OPTIONS RESULTS
PARSE ARG url pass dir file .
IF url = '' THEN url = 'sftp://demo@test.rebex.net'
IF pass = '' THEN pass = 'password'
IF dir = '' THEN dir = 'pub/example'
IF file = '' THEN file = 'readme.txt'
fails = 0
IF ~SHOW('P', 'FILEZILLA.1') THEN DO
    SAY 'no port FILEZILLA.1'
    SAY 'RESULT: FAIL'
    EXIT 10
END
ADDRESS COMMAND 'Delete >NIL: RAM:' || file
ADDRESS 'FILEZILLA.1'
'VERSION';  CALL Step 'VERSION', RESULT
'HELP';     CALL Step 'HELP', RESULT
'HELP GET'; CALL Step 'HELP GET', RESULT
'FAULT 1001'; CALL Step 'FAULT 1001', RESULT
'CD /'
IF RC = 10 & RC2 = 1001 THEN SAY 'ok   CD before CONNECT: RC 10 RC2 1001'
ELSE DO; SAY 'FAIL CD before CONNECT: RC' RC; fails = fails + 1; END
t0 = TIME('E')
'CONNECT' url 'PASSWORD' pass; CALL Step 'CONNECT' url
'STATUS';   CALL Step 'STATUS', RESULT
'CD' dir;   CALL Step 'CD', RESULT
'LIST';     CALL Step 'LIST', TRANSLATE(LEFT(RESULT, 70), ' ', '0a'x)
'GET' file 'TO RAM:'; CALL Step 'GET' file
IF ~EXISTS('RAM:' || file) THEN DO; SAY 'FAIL no RAM:' || file; fails = fails + 1; END
'PUT RAM:' || file
IF RC ~= 0 THEN SAY 'note PUT (read-only server): RC' RC 'RC2' RC2
'DISCONNECT'; CALL Step 'DISCONNECT'
'STATUS';   CALL Step 'STATUS', RESULT
SAY 'time' TIME('E') - t0 's'
IF fails = 0 THEN SAY 'RESULT: PASS'
ELSE SAY 'RESULT: FAIL' fails
EXIT 0

Step: PROCEDURE EXPOSE fails RC RC2
    PARSE ARG what, res
    IF RC = 0 THEN SAY 'ok  ' what || ':' res
    ELSE DO
        SAY 'FAIL' what || ': RC' RC 'RC2' RC2
        fails = fails + 1
    END
    RETURN
