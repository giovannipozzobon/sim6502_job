.target c64
.requires vic2
; audio_demo.asm
;
; Plays "Frère Jacques" repeatedly using raster-synchronized timing.
;
; CLI Usage:
;   ./sim6502 examples/audio_demo.asm -L 10000000 -c run
;
; GUI Usage:
;   1. Load and press Run (F5)

.org $0200

start:
    ; 1. Set Master Volume to Maximum
    LDA #$0F
    STA $D418

    ; 2. Set Envelope (Voice 1)
    LDA #$00        ; Fast Attack
    STA $D405
    LDA #$F0        ; Full Sustain
    STA $D406

    ; 3. Enable Triangle Waveform + Gate
    LDA #$11
    STA $D404

tune_loop:
    LDX #0          ; Reset note index
    
next_note:
    ; Update Frequency registers (Order: Lo then Hi)
    LDA note_table_lo,x
    STA $D400       ; Freq Lo
    LDA note_table_hi,x
    STA $D401       ; Freq Hi

    .inspect "SID #1"

    ; Wait for a duration (approx 0.3 seconds = 20 frames)
    LDA #20
    JSR wait_frames

    ; Brief silence between notes
    LDA #0
    STA $D401
    STA $D400
    LDA #2
    JSR wait_frames

    ; Move to next note
    INX
    CPX #16         ; Melody length
    BNE next_note
    
    ; Small pause at the end of the loop
    LDA #30
    JSR wait_frames
    
    JMP tune_loop

; --- Timing Routine ---
wait_frames:
    STA $02
.frame_loop:
.wait_line0:
    LDA $D012
    BNE .wait_line0
.wait_not0:
    LDA $D012
    BEQ .wait_not0
    DEC $02
    BNE .frame_loop
    RTS

; --- Note Data ---
.align $100
note_table_lo:
    .byte $0B, $2B, $77, $0B, $0B, $2B, $77, $0B, $77, $BF, $BD, $BD, $77, $BF, $BD, $BD
note_table_hi:
    .byte $11, $13, $15, $11, $11, $13, $15, $11, $15, $16, $19, $19, $15, $16, $19, $19
