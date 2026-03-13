* = $0200

// Demonstrate cycle tracking
// LDA #$10 = 2 cycles
// ADC #$20 = 2 cycles
// STA $1000 = 4 cycles
lda #$10
adc #$20
sta $1000
