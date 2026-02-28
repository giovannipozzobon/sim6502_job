; EXPECT: A=42 X=00 Y=00 S=FF PC=020E
;
; Self-modifying code: redirect a JMP by overwriting its lo address byte.
;
; Layout at $0200:
;   $0200-$0201  LDA #$0C       ; lo byte of 'right' ($020C)
;   $0202-$0204  STA $0206      ; overwrite lo byte of JMP target (at $0206)
;   $0205-$0207  JMP wrong      ; initially JMP $0208, patched to JMP $020C
;   $0208-$0209  wrong: LDA #$FF  ; wrong path — never reached after patch
;   $020A-$020B  BRK
;   $020C-$020D  right: LDA #$42  ; right path — taken after patch
;   $020E-$020F  BRK
;
; Hi byte at $0207 is already $02 for both targets; only lo byte needs patching.

        LDA #$0C
        STA $0206
        JMP wrong
wrong:  LDA #$FF
        BRK
right:  LDA #$42
        BRK
