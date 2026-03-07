.processor 6502
; sprite_demo.asm
;
; Demonstrates VIC-II hardware sprites in sim6502-gui.
;
; Three sprites move across the screen simultaneously:
;   Sprite 0 (Red)    -- Arrow shape, moves RIGHT  (+2 per frame)
;   Sprite 1 (Green)  -- Ball  shape, moves diagonally RIGHT+DOWN (+1,+1)
;   Sprite 2 (Yellow) -- Diamond shape, moves LEFT  (-1 per frame)
;
; After 60 animation frames the program halts at BRK.
; Open View -> VIC-II Sprites and View -> VIC-II Screen in the GUI,
; then press Run to see the final positions, or Step to watch movement.
;
; Memory layout
;   $0200  Program code
;   $0400  Screen RAM (D018 VM=1)
;   $07F8  Sprite pointers (screen_base + $3F8)
;   $2000  Sprite 0 data  (pointer = $80, addr = $80 * 64)
;   $2040  Sprite 1 data  (pointer = $81)
;   $2080  Sprite 2 data  (pointer = $82)
;   $D800  Colour RAM (left black for clean background)
;
; Usage (GUI)
;   ./sim6502-gui
;   File -> Open -> examples/sprite_demo.asm
;   File -> Load Symbols -> Presets -> C64
;   View -> VIC-II Screen  (to see the full frame)
;   View -> VIC-II Sprites (to inspect each sprite)
;   Press Run or Step
;
; Usage (CLI)
;   ./sim6502 -p 6502 -I examples/sprite_demo.asm
;   > run
;   > vic2.info
;   > vic2.sprites

.org $0200

start:
    ;------------------------------------------------------------------
    ; 1. VIC bank 0 via CIA2 ($0000-$3FFF)
    ;------------------------------------------------------------------
    LDA #$03
    STA $DD02       ; cia2_ddr_a  bits 0-1 as outputs
    STA $DD00       ; cia2_port_a output 11 = bank 0

    ;------------------------------------------------------------------
    ; 2. VIC-II display registers
    ;------------------------------------------------------------------
    LDA #$00
    STA $D020       ; border black
    STA $D021       ; background black

    LDA #$1B        ; DEN on, 25 rows, no bitmap, yscroll 3
    STA $D011

    LDA #$C8        ; 40 cols, no multicolour, xscroll 0
    STA $D016

    LDA #$14        ; screen at $0400 (VM=1), chars at $1000 (CB=2)
    STA $D018

    ;------------------------------------------------------------------
    ; 3. Clear screen RAM to spaces (char $00 = black block)
    ;------------------------------------------------------------------
    LDA #$00
    LDX #$00
clr_lo:
    STA $0400,X
    STA $0500,X
    STA $0600,X
    INX
    BNE clr_lo

    LDX #$00
clr_hi:
    STA $0700,X
    INX
    CPX #$E8
    BNE clr_hi

    ;------------------------------------------------------------------
    ; 4. Sprite data pointers at $07F8-$07FA
    ;------------------------------------------------------------------
    LDA #$80        ; sprite 0 at $2000
    STA $07F8
    LDA #$81        ; sprite 1 at $2040
    STA $07F9
    LDA #$82        ; sprite 2 at $2080
    STA $07FA

    ;------------------------------------------------------------------
    ; 5. Sprite colours
    ;------------------------------------------------------------------
    LDA #$02        ; red
    STA $D027
    LDA #$05        ; green
    STA $D028
    LDA #$07        ; yellow
    STA $D029

    ;------------------------------------------------------------------
    ; 6. Clear X-MSB register (all sprites X < 256)
    ;------------------------------------------------------------------
    LDA #$00
    STA $D010

    ;------------------------------------------------------------------
    ; 7. Enable sprites 0, 1, 2
    ;------------------------------------------------------------------
    LDA #$07
    STA $D015

    ;------------------------------------------------------------------
    ; 8. Initial sprite positions
    ;    VIC-II visible X range approx 24-343, Y range approx 50-250.
    ;------------------------------------------------------------------
    LDA #50         ; sprite 0 X
    STA $D000
    LDA #80         ; sprite 0 Y
    STA $D001

    LDA #160        ; sprite 1 X
    STA $D002
    LDA #60         ; sprite 1 Y
    STA $D003

    LDA #240        ; sprite 2 X
    STA $D004
    LDA #150        ; sprite 2 Y
    STA $D005

    ;------------------------------------------------------------------
    ; 9. Animation loop -- 60 frames
    ;    Sprite 0: moves right  (+2 px per frame)
    ;    Sprite 1: moves right+down (+1,+1 per frame)
    ;    Sprite 2: moves left   (-1 px per frame)
    ;------------------------------------------------------------------
    LDA #60
    STA $00         ; frame counter in zero page

anim_loop:
    ; Sprite 0 -- move right by 2
    INC $D000
    INC $D000

    ; Sprite 1 -- move right by 1
    INC $D002
    ; Sprite 1 -- move down by 1
    INC $D003

    ; Sprite 2 -- move left by 1
    LDA $D004
    SEC
    SBC #1
    STA $D004

    DEC $00
    BNE anim_loop

done:
    BRK

; ------------------------------------------------------------------
; Sprite bitmaps
;
; Each sprite is 24x21 pixels, stored as 21 rows of 3 bytes each
; (byte 0 = leftmost 8 pixels, byte 2 = rightmost 8 pixels).
; Row 21 byte 63 is unused padding -- must be present for 64-byte slot.
;
; Sprite 0 -- Red arrow pointing right
;   A wedge that grows from 1px at the top to 12px wide at row 10,
;   then shrinks symmetrically back.
; ------------------------------------------------------------------
.org $2000
.byte $00,$00,$00   ; row  0
.byte $20,$00,$00   ; row  1
.byte $30,$00,$00   ; row  2
.byte $38,$00,$00   ; row  3
.byte $3C,$00,$00   ; row  4
.byte $FE,$00,$00   ; row  5
.byte $FF,$00,$00   ; row  6
.byte $FF,$80,$00   ; row  7
.byte $FF,$C0,$00   ; row  8
.byte $FF,$E0,$00   ; row  9
.byte $FF,$F0,$00   ; row 10  (widest -- 12 px)
.byte $FF,$E0,$00   ; row 11
.byte $FF,$C0,$00   ; row 12
.byte $FF,$80,$00   ; row 13
.byte $FF,$00,$00   ; row 14
.byte $FE,$00,$00   ; row 15
.byte $3C,$00,$00   ; row 16
.byte $38,$00,$00   ; row 17
.byte $30,$00,$00   ; row 18
.byte $20,$00,$00   ; row 19
.byte $00,$00,$00   ; row 20
.byte $00            ; padding (byte 63)

; Sprite 1 -- Green ball (filled circle, 16px wide at equator)
.org $2040
.byte $01,$80,$00   ; row  0   2px
.byte $07,$E0,$00   ; row  1   6px
.byte $0F,$F0,$00   ; row  2   8px
.byte $1F,$F8,$00   ; row  3  10px
.byte $3F,$FC,$00   ; row  4  12px
.byte $7F,$FE,$00   ; row  5  14px
.byte $FF,$FF,$00   ; row  6  16px
.byte $FF,$FF,$00   ; row  7  16px
.byte $FF,$FF,$00   ; row  8  16px
.byte $FF,$FF,$00   ; row  9  16px
.byte $FF,$FF,$00   ; row 10  16px (equator)
.byte $FF,$FF,$00   ; row 11  16px
.byte $FF,$FF,$00   ; row 12  16px
.byte $FF,$FF,$00   ; row 13  16px
.byte $7F,$FE,$00   ; row 14  14px
.byte $3F,$FC,$00   ; row 15  12px
.byte $1F,$F8,$00   ; row 16  10px
.byte $0F,$F0,$00   ; row 17   8px
.byte $07,$E0,$00   ; row 18   6px
.byte $01,$80,$00   ; row 19   2px
.byte $00,$00,$00   ; row 20
.byte $00            ; padding

; Sprite 2 -- Yellow diamond (22px wide at row 10, 2px at top and bottom)
.org $2080
.byte $00,$18,$00   ; row  0   2px
.byte $00,$3C,$00   ; row  1   4px
.byte $00,$7E,$00   ; row  2   6px
.byte $00,$FF,$00   ; row  3   8px
.byte $01,$FF,$80   ; row  4  10px
.byte $03,$FF,$C0   ; row  5  12px
.byte $07,$FF,$E0   ; row  6  14px
.byte $0F,$FF,$F0   ; row  7  16px
.byte $1F,$FF,$F8   ; row  8  18px
.byte $3F,$FF,$FC   ; row  9  20px
.byte $7F,$FF,$FE   ; row 10  22px (widest)
.byte $3F,$FF,$FC   ; row 11  20px
.byte $1F,$FF,$F8   ; row 12  18px
.byte $0F,$FF,$F0   ; row 13  16px
.byte $07,$FF,$E0   ; row 14  14px
.byte $03,$FF,$C0   ; row 15  12px
.byte $01,$FF,$80   ; row 16  10px
.byte $00,$FF,$00   ; row 17   8px
.byte $00,$7E,$00   ; row 18   6px
.byte $00,$3C,$00   ; row 19   4px
.byte $00,$18,$00   ; row 20   2px
.byte $00            ; padding
