

// audio_demo.asm
//
// Plays "Frere Jacques" repeatedly using raster-synchronized timing.
//
// CLI Usage:
//   ./sim6502 examples/audio_demo.asm -L 10000000 -c run
//
// GUI Usage:
//   1. Load and press Run (F5)

* = $0200

start:
    // 1. Set Master Volume to Maximum
    lda #$0F
    sta $D418

    // 2. Set Envelope (Voice 1)
    lda #$00        // Fast Attack
    sta $D405
    lda #$F0        // Full Sustain
    sta $D406

    // 3. Enable Triangle Waveform + Gate
    lda #$11
    sta $D404

tune_loop:
    ldx #0          // Reset note index
    
next_note:
    // Update Frequency registers (Order: Lo then Hi)
    lda note_table_lo,x
    sta $D400       // Freq Lo
    lda note_table_hi,x
    sta $D401       // Freq Hi

    //.inspect "SID #1"

    // Wait for a duration (approx 0.3 seconds = 20 frames)
    lda #20
    jsr wait_frames

    // Brief silence between notes
    lda #0
    sta $D401
    sta $D400
    lda #2
    jsr wait_frames

    // Move to next note
    inx
    cpx #16         // Melody length
    bne next_note
    
    // Small pause at the end of the loop
    lda #30
    jsr wait_frames
    
    jmp tune_loop

// --- Timing Routine ---
wait_frames:
    sta $02
frame_loop:
wait_line0:
    lda $D012
    bne wait_line0
wait_not0:
    lda $D012
    beq wait_not0
    dec $02
    bne frame_loop
    rts

// --- Note Data ---
.align $100
note_table_lo:
    .byte $0B, $2B, $77, $0B, $0B, $2B, $77, $0B, $77, $BF, $BD, $BD, $77, $BF, $BD, $BD
note_table_hi:
    .byte $11, $13, $15, $11, $11, $13, $15, $11, $15, $16, $19, $19, $15, $16, $19, $19
