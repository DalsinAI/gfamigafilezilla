/* FZ-Get.rexx: fetch one file with FileZilla over its ARexx port.
   rx FZ-Get.rexx sftp://demo@test.rebex.net password pub/example readme.txt RAM:
   FileZilla must be running (its port is FILEZILLA.1). */
OPTIONS RESULTS
PARSE ARG url pass dir file to
IF ~SHOW('P', 'FILEZILLA.1') THEN DO
    SAY 'FileZilla is not running'
    EXIT 10
END
ADDRESS 'FILEZILLA.1'
'CONNECT' url 'PASSWORD' pass
IF RC ~= 0 THEN CALL Fail
'CD' dir
IF RC ~= 0 THEN CALL Fail
SAY 'In' RESULT
'LIST'
SAY RESULT
'GET' file 'TO' to
IF RC ~= 0 THEN CALL Fail
SAY 'Got' file
'DISCONNECT'
EXIT 0

Fail:
    code = RC2
    'FAULT' code
    SAY 'FileZilla:' RESULT '(' || code || ')'
    EXIT 10
