.processor 65c02
; Test new 65C02 instructions
LDA #$0F
TSB $0100
STZ $0101
BRA $0000
