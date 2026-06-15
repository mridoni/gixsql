       IDENTIFICATION DIVISION.

       PROGRAM-ID. TSQL044A.

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

           01  H-KEY   PIC S9(9) COMP.
           01  H-RES   PIC S9(9) COMP.
           01  H-DISP  PIC 9(4).

       EXEC SQL
            INCLUDE SQLCA
       END-EXEC.

       PROCEDURE DIVISION.

       000-CONNECT.

           DISPLAY "DATASRC" UPON ENVIRONMENT-NAME.
           ACCEPT DATASRC FROM ENVIRONMENT-VALUE.
           DISPLAY "DATASRC_USR" UPON ENVIRONMENT-NAME.
           ACCEPT DBUSR FROM ENVIRONMENT-VALUE.

           EXEC SQL
              CONNECT TO :DATASRC USER :DBUSR
           END-EXEC.

           IF SQLCODE <> 0 THEN
              DISPLAY 'CONNECT SQLCODE. ' SQLCODE
              DISPLAY 'CONNECT SQLERRM. ' SQLERRM
              GO TO 100-EXIT
           END-IF.

       100-MAIN.

      *    H-KEY is USAGE COMP (binary). It is rendered to a display
      *    string at runtime, so it must be bound as text, not as a raw
      *    binary value, both as a WHERE parameter and a FETCH target.

           MOVE 42 TO H-KEY.

           EXEC SQL
                SELECT FLD2 INTO :H-RES FROM TAB1
                    WHERE FLD1 = :H-KEY
           END-EXEC.

           MOVE H-RES TO H-DISP.

           DISPLAY 'SELECT SQLCODE: ' SQLCODE.
           DISPLAY 'FLD2: ' H-DISP.

           EXEC SQL
              CONNECT RESET
           END-EXEC.

           IF SQLCODE <> 0 THEN
              DISPLAY 'DISCONNECT SQLCODE. ' SQLCODE
              DISPLAY 'DISCONNECT SQLERRM. ' SQLERRM
              GO TO 100-EXIT
           END-IF.

       100-EXIT.
             STOP RUN.
