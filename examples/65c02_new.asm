.cpu _65c02
* = $0200
// Test new 65C02 instructions
lda #$0F
tsb $0100
stz $0101
jmp $0000
