* = $0200
 // EXPECT: A=05 X=00 Y=00 S=FF PC=0203

    jsr sub
    brk
    sub:
    lda #$05
    rts
