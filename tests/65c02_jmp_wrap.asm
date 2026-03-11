; EXPECT: A=42 X=00 Y=00 S=FF PC=0402
.processor 65c02
.org $0200

start:
    ; Set up pointer at $02FF and $0300
    ; We want JMP ($02FF) to jump to $0400
    LDA #$00
    STA $02FF
    LDA #$04
    STA $0300
    
    ; If bug exists (NMOS style), it will read high byte from $0200
    ; $0200 contains 'start' ($A9)
    ; So it would jump to $A900
    
    JMP ($02FF)

.org $0400
target:
    LDA #$42
    BRK
