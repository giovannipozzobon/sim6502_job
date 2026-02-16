.processor 6502
; Demonstrate cycle tracking
; LDA #$10 = 2 cycles
; ADC #$20 = 2 cycles
; STA $1000 = 4 cycles
LDA #$10
ADC #$20
STA $1000
