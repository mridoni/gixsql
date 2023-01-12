       IDENTIFICATION DIVISION.
       
       PROGRAM-ID. TSQL041A. 
       
       
       ENVIRONMENT DIVISION. 
       
       CONFIGURATION SECTION. 
       SOURCE-COMPUTER. IBM-AT. 
       OBJECT-COMPUTER. IBM-AT. 
       
       INPUT-OUTPUT SECTION. 
       FILE-CONTROL. 
       
       DATA DIVISION.  
       
       FILE SECTION.  
       
       WORKING-STORAGE SECTION. 
       
           01 DATASRC PIC X(64).
           01 DBUSR  PIC X(64).

           01 BFLD1 PIC X(300) USAGE VARRAW.      
           01 BFLD2 PIC X(300) USAGE VARRAW.      

           01 HASH-1 PIC X(64).
           01 HASH-2 PIC X(64).

           01 REC-ID          PIC 9999.

           01 CUR-OP PIC X(32).

       EXEC SQL 
            INCLUDE SQLCA 
       END-EXEC. 
         
       PROCEDURE DIVISION. 
 
       000-CONNECT.

           EXEC SQL WHENEVER SQLERROR PERFORM 999-ERR END-EXEC.

           DISPLAY "DATASRC" UPON ENVIRONMENT-NAME.
           ACCEPT DATASRC FROM ENVIRONMENT-VALUE.
           DISPLAY "DATASRC_USR" UPON ENVIRONMENT-NAME.
           ACCEPT DBUSR FROM ENVIRONMENT-VALUE.
           
           DISPLAY '***************************************'.
           DISPLAY " DATASRC  : " DATASRC.
           DISPLAY " AUTH     : " DBUSR.
           DISPLAY '***************************************'.

           MOVE 'CONNECT' TO CUR-OP.
           EXEC SQL
              CONNECT TO :DATASRC USER :DBUSR
           END-EXEC.      
           
           IF SQLCODE <> 0 THEN
              DISPLAY 'CONNECT SQLCODE. ' SQLCODE
              DISPLAY 'CONNECT SQLERRM. ' SQLERRM
              GO TO 100-EXIT
           END-IF.

       100-MAIN.
            
           MOVE 'SELECT-1' TO CUR-OP.
           EXEC SQL
              SELECT 
                ID, DATA 
              INTO 
                :REC-ID, :BFLD1 
              FROM BINTEST
              WHERE ID = 1
           END-EXEC.

           MOVE 'INSERT-1' TO CUR-OP.
           EXEC SQL
              INSERT INTO BINTEST(ID, DATA)
                VALUES(2, :BFLD1)
           END-EXEC.

           MOVE 'SELECT-H-1' TO CUR-OP.
           EXEC SQL
              SELECT 
                MD5(DATA) INTO :HASH-1
              FROM BINTEST
              WHERE ID = 1
           END-EXEC.

           MOVE 'SELECT-H-2' TO CUR-OP.
           EXEC SQL
              SELECT 
                MD5(DATA) INTO :HASH-2
              FROM BINTEST
              WHERE ID = 2
           END-EXEC.

           DISPLAY 'HASH-1: ' HASH-1.
           DISPLAY 'HASH-2: ' HASH-2.

       
       100-DISCONNECT.

           MOVE 'RESET' TO CUR-OP.
           EXEC SQL
              CONNECT RESET
           END-EXEC.      

           IF HASH-1 EQUALS HASH-2 THEN
                DISPLAY 'HASH COMPARE OK'
           ELSE
                DISPLAY 'HASH COMPARE KO'
                MOVE 1 TO RETURN-CODE
           END-IF.
       
       100-EXIT. 
             STOP RUN.

       999-ERR.
            DISPLAY 'ERROR AT: ' CUR-OP
            DISPLAY 'SQLCODE : ' SQLCODE
            DISPLAY 'SQLERRMC: ' SQLERRMC(1:SQLERRML)
            MOVE 1 TO RETURN-CODE