// EXPECT: A=0F X=0F Y=00 S=FF P=24
// FLAGS: -p 6502-undoc
    .cpu _6502
    * = $1000

    lda #$FF
    ldx #$0F
    
    // $8F = SAX abs (Store A AND X)
    .byte $8F, $00, $20 // SAX $2000 -> $0F
    
    // Verify result
    lda $2000 // A=$0F
    ldx #$0F // X=$0F
    
    rts
