// EXPECT: A=44 X=44 Y=00 S=FF P=24
// FLAGS: -p 6502-undoc
    .cpu _6502
    * = $1000

    // Test LAX (Load A and X)
    // $AF = LAX abs
    lda #$44
    sta $1234
    
    .byte $AF, $34, $12 // LAX $1234
    
    rts
