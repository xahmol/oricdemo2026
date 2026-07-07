// crt_math.c - Oscar64 math and float runtime helpers for Oric bare-metal
//
// Extracted from ~/oscar64/include/crt.c by Oric/Oscar64 Authors.
// Only the integer arithmetic and floating-point runtime routines are included;
// the startup, bytecode interpreter, and platform init code are omitted.
// Bytecode handler wrappers (inp_*) that reference startup.exec are excluded.
//
// Required because -rt=oric_crt.c replaces the default runtime; Oscar64 still
// needs these symbol definitions for any code that uses multiply, divide, or float.

#include <crt.h>

#define tmpy    __tmpy
#define tmp     __tmp
#define ip      __ip
#define accu    __accu
#define addr    __addr
#define sp      __sp
#define fp      __fp

// -------------------------------------------------------------------------
// Negation
// -------------------------------------------------------------------------

__asm negaccu
{
        sec
        lda #0
        sbc accu
        sta accu
        lda #0
        sbc accu + 1
        sta accu + 1
        rts
}

__asm negtmp
{
        sec
        lda #0
        sbc tmp
        sta tmp
        lda #0
        sbc tmp + 1
        sta tmp + 1
        rts
}

__asm negtmpb
{
        sec
        lda #0
        sbc tmp + 2
        sta tmp + 2
        lda #0
        sbc tmp + 3
        sta tmp + 3
        rts
}

__asm negaccu32
{
        sec
        lda #0
        sbc accu
        sta accu
        lda #0
        sbc accu + 1
        sta accu + 1
        lda #0
        sbc accu + 2
        sta accu + 2
        lda #0
        sbc accu + 3
        sta accu + 3
        rts
}

__asm negtmp32
{
        sec
        lda #0
        sbc tmp
        sta tmp
        lda #0
        sbc tmp + 1
        sta tmp + 1
        lda #0
        sbc tmp + 2
        sta tmp + 2
        lda #0
        sbc tmp + 3
        sta tmp + 3
        rts
}

__asm negtmp32b
{
        sec
        lda #0
        sbc tmp + 4
        sta tmp + 4
        lda #0
        sbc tmp + 5
        sta tmp + 5
        lda #0
        sbc tmp + 6
        sta tmp + 6
        lda #0
        sbc tmp + 7
        sta tmp + 7
        rts
}

// -------------------------------------------------------------------------
// Unsigned divide: accu / tmp -> accu, remainder -> tmp + 2
// -------------------------------------------------------------------------

__asm divmod
{
        lda accu + 1
        bne WB
        lda tmp + 1
        bne BW

BB:
        sta tmp + 3
        ldx #4
        asl accu
LBB1:   rol
        cmp tmp
        bcc WBB1
        sbc tmp
WBB1:   rol accu
        rol
        cmp tmp
        bcc WBB2
        sbc tmp
WBB2:   rol accu
        dex
        bne LBB1
        sta tmp + 2
        rts

BW:
        lda accu
        sta tmp + 2
        lda accu + 1
        sta tmp + 3
        lda #0
        sta accu
        sta accu + 1
        rts

DM8:
        sta  tmp
        lda  #0
        sta  tmp + 1
        lda  accu + 1
        beq  BB

WB:
        lda tmp + 1
        bne WW
        lda tmp
        bmi WW

        lda #0
        sta tmp + 3
        ldx #16
        asl accu
        rol accu + 1
LWB1:   rol
        cmp tmp
        bcc WWB1
        sbc tmp
WWB1:   rol accu
        rol accu + 1
        dex
        bne LWB1
        sta tmp + 2
        rts

WW:
        lda #0
        sta tmp + 2
        sta tmp + 3

        sty tmpy
        ldy #16
        clc
L1:     rol accu
        rol accu + 1
        rol tmp + 2
        rol tmp + 3
        sec
        lda tmp + 2
        sbc tmp
        tax
        lda tmp + 3
        sbc tmp + 1
        bcc W1
        stx tmp + 2
        sta tmp + 3
W1:     dey
        bne L1
        rol accu
        rol accu + 1
        ldy tmpy
        rts
}

// -------------------------------------------------------------------------
// Unsigned 32-bit divide: accu / tmp -> accu, remainder -> tmp + 4
// -------------------------------------------------------------------------

__asm divmod32
{
        sty tmpy
        ldy #32

        lda #0
        sta tmp + 4
        sta tmp + 5
        sta tmp + 6
        sta tmp + 7

        lda tmp + 2
        ora tmp + 3
        bne W32

        lda tmp + 1
        bne W16

        clc
LB1:    rol accu
        rol accu + 1
        rol accu + 2
        rol accu + 3
        rol
        bcc LB1a

        sbc tmp
        sec
        bcs LB1b
LB1a:
        cmp tmp
        bcc LB1b
        sbc tmp
LB1b:
WB1:    dey
        bne LB1
        sta tmp + 4
        rol accu
        rol accu + 1
        rol accu + 2
        rol accu + 3
        ldy tmpy
        rts

W16:
        lda accu + 3
        bne LS0
        ldx accu + 2
        stx accu + 3
        ldx accu + 1
        stx accu + 2
        ldx accu + 0
        stx accu + 1
        sta accu + 0
        ldy #24
LS0:
        clc
LS1:    rol accu
        rol accu + 1
        rol accu + 2
        rol accu + 3
        rol tmp + 4
        rol tmp + 5
        bcc LS1a

        lda tmp + 4
        sbc tmp
        tax
        lda tmp + 5
        sbc tmp + 1
        sec
        bcs LS1b
LS1a:
        sec
        lda tmp + 4
        sbc tmp
        tax
        lda tmp + 5
        sbc tmp + 1
        bcc WS1
LS1b:
        stx tmp + 4
        sta tmp + 5
WS1:    dey
        bne LS1
        rol accu
        rol accu + 1
        rol accu + 2
        rol accu + 3
        ldy tmpy
        rts

W32:
        ldy #16
        lda accu + 3
        sta tmp + 5
        lda accu + 2
        sta tmp + 4
        lda #0
        sta accu + 2
        sta accu + 3

        clc
L1:     rol accu
        rol accu + 1
        rol tmp + 4
        rol tmp + 5
        rol tmp + 6
        rol tmp + 7

        lda tmp + 4
        cmp tmp + 0
        lda tmp + 5
        sbc tmp + 1
        lda tmp + 6
        sbc tmp + 2
        tax
        lda tmp + 7
        sbc tmp + 3
        bcc W1
        stx tmp + 6
        sta tmp + 7
        lda tmp + 4
        sbc tmp + 0
        sta tmp + 4
        lda tmp + 5
        sbc tmp + 1
        sta tmp + 5
        sec
W1:     dey
        bne L1
        rol accu
        rol accu + 1
        ldy tmpy
        rts
}

// -------------------------------------------------------------------------
// 16-bit multiply: accu * tmp -> tmp + 2
// -------------------------------------------------------------------------

__asm mul16
{
        ldy #0
        sty tmp + 3

        lda tmp
        ldx tmp + 1
        beq W1

        sec
        ror
        bcc L2
L1:
        tax
        clc
        tya
        adc accu
        tay
        lda tmp + 3
        adc accu + 1
        sta tmp + 3
        txa
L2:
        asl accu + 0
        rol accu + 1
        lsr
        bcc L2
        bne L1

        lda tmp + 1
W1:
        lsr
        bcc L4
L3:
        tax
        clc
        tya
        adc accu
        tay
        lda tmp + 3
        adc accu + 1
        sta tmp + 3
        txa
L4:
        asl accu + 0
        rol accu + 1
        lsr
        bcs L3
        bne L4

        sty tmp + 2
}

// -------------------------------------------------------------------------
// 32-bit by 8-bit multiply: accu (32-bit) * A (8-bit) -> tmp + 4
// -------------------------------------------------------------------------

__asm mul32by8
{
        ldy #0
        sty tmp + 4
        sty tmp + 5
        sty tmp + 6

        lsr
        bcs W3
        beq E0
L1:
        asl accu
        rol accu + 1
        rol accu + 2
        rol accu + 3
W2:
        lsr
        bcc L1
W3:
        tax
        clc
        lda tmp + 4
        adc accu
        sta tmp + 4
        lda tmp + 5
        adc accu + 1
        sta tmp + 5
        lda tmp + 6
        adc accu + 2
        sta tmp + 6
        tya
        adc accu + 3
        tay
        txa
        bne L1
E0:
        sty tmp + 7
        rts
}

// -------------------------------------------------------------------------
// 32-bit multiply: accu * tmp -> tmp + 4
// -------------------------------------------------------------------------

__asm mul32
{
        lda tmp + 1
        ora tmp + 2
        ora tmp + 3
        bne WW
        lda tmp + 0
        jmp mul32by8
WW:
        ldy #0
        sty tmp + 4
        sty tmp + 5
        tya

        sec
        ror tmp + 0

        bcc W1
L1:
        tax
        clc
        lda tmp + 4
        adc accu
        sta tmp + 4
        lda tmp + 5
        adc accu + 1
        sta tmp + 5
        tya
        adc accu + 2
        tay
        txa
        adc accu + 3
W1:
        lsr tmp + 1
        bcc W2
        tax
        clc
        lda tmp + 5
        adc accu + 0
        sta tmp + 5
        tya
        adc accu + 1
        tay
        txa
        adc accu + 2
W2:
        lsr tmp + 2
        bcc W3
        tax
        clc
        tya
        adc accu + 0
        tay
        txa
        adc accu + 1
W3:
        lsr tmp + 3
        bcc W4
        clc
        adc accu + 0
W4:
        asl accu
        rol accu + 1
        rol accu + 2
        rol accu + 3

        lsr tmp + 0
        bcc W1
        bne L1

        sty tmp + 6
        sta tmp + 7
        rts
}

// -------------------------------------------------------------------------
// 16-bit by 8-bit multiply: accu (16-bit) * A (8-bit) -> accu
// -------------------------------------------------------------------------

__asm mul16by8
{
        lsr
        beq zero
more:
        ldx #0
        ldy #0
        bcc skip
odd:
        ldy accu
        ldx accu + 1
        bcs skip

loop:
        sta tmpy

        clc
        tya
        adc accu
        tay
        txa
        adc accu + 1
        tax

        lda tmpy
skip:
        asl accu
        rol accu + 1
        lsr
        bcc skip
        bne loop
done:
        clc
        tya
        adc accu
        sta accu
        txa
        adc accu + 1
        sta accu + 1
        rts
zero:
        bcs one
        sta accu
        sta accu + 1
one:
        rts
}

// -------------------------------------------------------------------------
// Signed 16-bit divide/modulo
// -------------------------------------------------------------------------

__asm divs16
{
        bit accu + 1
        bpl L1
        jsr negaccu
        bit tmp + 1
        bpl L2
        jsr negtmp
L3:     jmp divmod
L1:     bit tmp + 1
        bpl L3
        jsr negtmp
L2:     jsr divmod
        jmp negaccu
}

__asm mods16
{
        bit accu + 1
        bpl L1
        jsr negaccu
        bit tmp + 1
        bpl L2
        jsr negtmp
L2:     jsr divmod
        jmp negtmpb
L1:     bit tmp + 1
        bpl L3
        jsr negtmp
L3:     jmp divmod
        rts
}

__asm divmods16
{
        bit accu + 1
        bmi L1
        bit tmp + 1
        bmi L2
        jmp divmod
L2:
        jsr negtmp
        jsr divmod
        jmp negaccu
L1:
        jsr negaccu
        bit tmp + 3
        bmi L3
        jsr divmod
        jsr negtmpb
        jmp negaccu
L3:
        jsr negtmp
        jsr divmod
        jmp negtmpb
}

// -------------------------------------------------------------------------
// Signed 32-bit divide/modulo
// -------------------------------------------------------------------------

__asm divs32
{
        bit accu + 3
        bpl L1
        jsr negaccu32
        bit tmp + 3
        bpl L2
        jsr negtmp32
L3:     jmp divmod32
L1:     bit tmp + 3
        bpl L3
        jsr negtmp32
L2:     jsr divmod32
        jmp negaccu32
}

__asm mods32
{
        bit accu + 3
        bpl L1
        jsr negaccu32
        bit tmp + 3
        bpl L2
        jsr negtmp32
L2:     jsr divmod32
        jmp negtmp32b
L3:     jmp divmod32
L1:     bit tmp + 3
        bpl L3
        jsr negtmp32
        jmp divmod32
}

__asm divmods32
{
        bit accu + 3
        bmi L1
        bit tmp + 3
        bmi L2
        jmp divmod32
L2:
        jsr negtmp32
        jsr divmod32
        jmp negaccu32
L1:
        jsr negaccu32
        bit tmp + 3
        bmi L3
        jsr divmod32
        jsr negtmp32b
        jmp negaccu32
L3:
        jsr negtmp32
        jsr divmod32
        jmp negtmp32b
}

#pragma runtime(mul16, mul16);
#pragma runtime(mul16by8, mul16by8);
#pragma runtime(divu16, divmod);
#pragma runtime(modu16, divmod);
#pragma runtime(divu16by8, divmod.DM8);
#pragma runtime(divs16, divs16);
#pragma runtime(mods16, mods16);
#pragma runtime(divmods16, divmods16);

#pragma runtime(mul32, mul32);
#pragma runtime(mul32by8, mul32by8);
#pragma runtime(divu32, divmod32);
#pragma runtime(modu32, divmod32);
#pragma runtime(divs32, divs32);
#pragma runtime(mods32, mods32);
#pragma runtime(divmods32, divmods32);

// -------------------------------------------------------------------------
// Float runtime
//
// IEEE 754 single-precision helpers.
// Internal representation in accu (4 bytes) and tmp (4+ bytes).
// Exponent bias = $7f. Mantissa is 23-bit with implicit leading 1.
// -------------------------------------------------------------------------

__asm freg
{
split_exp:
        lda (ip), y
        iny
        tax
split_xexp:
        lda $00, x
        sta tmp + 0
        lda $01, x
        sta tmp + 1
        lda $02, x
        sta tmp + 2
        lda $03, x
        sta tmp + 3
split_texp:
        lda tmp + 2
        asl
        lda tmp + 3
        rol
        sta tmp + 5
        beq ZT
        lda tmp + 2
        ora #$80
        sta tmp + 2
ZT:

split_aexp:
        lda accu + 2
        asl
        lda accu + 3
        rol
        sta tmp + 4
        beq ZA
        lda accu + 2
        ora #$80
        sta accu + 2
ZA:
        rts

merge_aexp:
        asl accu + 3
        lda tmp + 4
        ror
        sta accu + 3
        bcs W1
        lda accu + 2
        and #$7f
        sta accu + 2
W1:
        rts
}

__asm faddsub
{
fsub:
        lda tmp + 3
        eor #$80
        sta tmp + 3
fadd:
        lda #$ff
        cmp tmp + 4
        beq INF
        cmp tmp + 5
        bne nINF
INF:
        lda accu + 3
        ora #$7f
        sta accu + 3
        lda #$80
        sta accu + 2
        lda #$00
        sta accu + 0
        sta accu + 1
        rts
nINF:
        sec
        lda tmp + 4
        sbc tmp + 5
        beq fas_aligned
        tax

        bcs fas_align2nd

        cpx #-23
        bcs W1

        lda tmp + 5
        sta tmp + 4
        lda #0
        sta accu
        sta accu + 1
        sta accu + 2
        beq fas_aligned
W1:
        lda accu + 2
L1:
        lsr
        ror accu + 1
        ror accu
        inx
        bne L1
        sta accu + 2
        lda tmp + 5
        sta tmp + 4
        jmp fas_aligned

fas_align2nd:
        cpx #24
        bcs fas_done
        lda tmp + 2
L2:     lsr
        ror tmp + 1
        ror tmp
        dex
        bne L2
        sta tmp + 2

fas_aligned:
        lda accu + 3
        and #$80
        sta accu + 3
        eor tmp + 3
        bmi fas_sub

        clc
        lda accu
        adc tmp
        sta accu
        lda accu + 1
        adc tmp + 1
        sta accu + 1
        lda accu + 2
        adc tmp + 2
        sta accu + 2
        bcc fas_done
        ror accu + 2
        ror accu + 1
        ror accu
        inc tmp + 4
fas_done:
        lda tmp + 4
        cmp #$ff
        beq INF
        lsr
        ora accu + 3
        sta accu + 3
        bcs W2
        lda accu + 2
        and #$7f
        sta accu + 2
W2:
        rts

fas_sub:
        sec
        lda accu
        sbc tmp
        sta accu
        lda accu + 1
        sbc tmp + 1
        sta accu + 1
        lda accu + 2
        sbc tmp + 2
        sta accu + 2
        bcs fas_pos
        sec
        lda #0
        sbc accu
        sta accu
        lda #0
        sbc accu + 1
        sta accu + 1
        lda #0
        sbc accu + 2
        sta accu + 2
        lda accu + 3
        eor #$80
        sta accu + 3
fas_pos:
        lda accu + 2
        bmi fas_done

        ora accu + 1
        ora accu + 0
        beq fas_zero
L3:
        dec tmp + 4
        beq fas_zero
        asl accu
        rol accu + 1
        rol accu + 2
        bpl L3
        jmp fas_done
fas_zero:
        lda #0
        sta accu + 0
        sta accu + 1
        sta accu + 2
        sta accu + 3
        rts
}

__asm crt_fmul8
{
        sec
        ror
        bcc L2
L1:     tax
        clc
        tya
        adc tmp + 6
        sta tmp + 6
        lda tmp + 7
        adc accu + 1
        sta tmp + 7
        lda tmp + 8
        adc accu + 2
        ror
        sta tmp + 8
        txa
        ror tmp + 7
        ror tmp + 6
        lsr
        beq W1
        bcs L1
L2:
        ror tmp + 8
        ror tmp + 7
        ror tmp + 6
        lsr
        bcc L2
        bne L1
W1:
        rts
}

__asm crt_fmul
{
        lda accu
        ora accu + 1
        ora accu + 2
        beq E3
W1:
        lda tmp
        ora tmp + 1
        ora tmp + 2
        bne W2
        sta accu
        sta accu + 1
        sta accu + 2
E3:
        sta accu + 3
        rts
W2:
        lda accu + 3
        eor tmp + 3
        and #$80
        sta accu + 3

        lda #$ff
        cmp tmp + 4
        beq INF
        cmp tmp + 5
        beq INF

        lda #0
        sta tmp + 6
        sta tmp + 7
        sta tmp + 8

        ldy accu
        lda tmp
        bne W4
        lda tmp + 1
        beq W5
        bne W6
W4:
        jsr crt_fmul8
        lda tmp + 1
W6:
        jsr crt_fmul8
W5:
        lda tmp + 2
        jsr crt_fmul8

        sec
        lda tmp + 8
        bmi W3
        asl tmp + 6
        rol tmp + 7
        rol
        clc
W3:     and #$7f
        sta tmp + 8

        lda tmp + 4
        adc tmp + 5
        bcc W7

        sbc #$7f
        bcs INF
        cmp #$ff
        bne W8
INF:
        lda accu + 3
        ora #$7f
        sta accu + 3
        lda #$80
E2:
        sta accu + 2
        lda #$00
        sta accu + 0
        sta accu + 1
        rts
W7:
        sbc #$7e
        bcc ZERO
W8:
        lsr
        ora accu + 3
        sta accu + 3
        lda #0
        ror
        ora tmp + 8
        sta accu + 2
        lda tmp + 7
        sta accu + 1
        lda tmp + 6
        sta accu
        rts
ZERO:
        lda #0
        sta accu + 3
        beq E2
}

__asm crt_fdiv
{
        lda accu
        ora accu + 1
        ora accu + 2
        bne W1
        sta accu + 3
        rts
W1:
        lda accu + 3
        eor tmp + 3
        and #$80
        sta accu + 3

        lda tmp + 5
        beq INF
        lda tmp + 4
        cmp #$ff
        beq INF

        lda #0
        sta tmp + 6
        sta tmp + 7
        sta tmp + 8
        ldx #24

L1:
        lda accu + 0
        cmp tmp + 0
        lda accu + 1
        sbc tmp + 1
        lda accu + 2
        sbc tmp + 2
        bcc W2

L2:
        lda accu + 0
        sbc tmp + 0
        sta accu + 0
        lda accu + 1
        sbc tmp + 1
        sta accu + 1
        lda accu + 2
        sbc tmp + 2
        sta accu + 2
        sec
W2:
        rol tmp + 6
        rol tmp + 7
        rol tmp + 8
        dex
        beq W3

        asl accu
        rol accu + 1
        rol accu + 2
        bcs L2
        bcc L1
W3:
        sec
        lda tmp + 8
        bmi W4
        asl tmp + 6
        rol tmp + 7
        rol
        clc
W4:
        and #$7f
        sta tmp + 8

        lda tmp + 4
        sbc tmp + 5
        bcc W5

        clc
        adc #$7f
        bcs INF
        cmp #$ff
        bne W6
INF:
        lda accu + 3
        ora #$7f
        sta accu + 3
        lda #$80
        sta accu + 2
        lda #$00
        sta accu + 1
        sta accu + 0
        rts
W5:
        adc #$7f
        bcc ZERO
W6:
        lsr
        ora accu + 3
        sta accu + 3
        lda #0
        ror
        ora tmp + 8
        sta accu + 2
        lda tmp + 7
        sta accu + 1
        lda tmp + 6
        sta accu
        rts

ZERO:
        lda #$00
        sta accu + 3
        sta accu + 2
        sta accu + 1
        sta accu + 0
        rts
}

__asm crt_fcmp
{
        lda accu + 3
        eor tmp + 3
        bpl W1

        lda accu + 3
        and #$7f
        ora accu + 2
        ora accu + 1
        ora accu
        bne W2

        lda tmp + 3
        and #$7f
        ora tmp + 2
        ora tmp + 1
        ora tmp + 0
        beq fcmpeq
W2:     lda accu + 3
        bmi fcmpgt
        bpl fcmplt

W1:
        lda accu + 3
        cmp tmp + 3
        bne W3
        lda accu + 2
        cmp tmp + 2
        bne W3
        lda accu + 1
        cmp tmp + 1
        bne W3
        lda accu
        cmp tmp
        bne W3

fcmpeq:
        lda #0
        rts

W3:     bcs W4

        bit accu + 3
        bmi fcmplt

fcmpgt:
        lda #1
        rts

W4:     bit accu + 3
        bmi fcmpgt

fcmplt:
        lda #$ff
        rts
}

// -------------------------------------------------------------------------
// Integer <-> float conversions
// -------------------------------------------------------------------------

__asm uint16_to_float
{
        lda accu
        ora accu + 1
        bne W1
        sta accu + 2
        sta accu + 3
        rts
W1:
        ldx #$8e
        lda accu + 1
        bmi W2
L1:
        dex
        asl accu
        rol
        bpl L1
W2:
        asl
        sta accu + 2
        lda accu
        sta accu + 1
        txa
        lsr
        sta accu + 3
        lda #0
        sta accu
        ror accu + 2
        rts
}

__asm sint16_to_float
{
        bit accu + 1
        bmi W1
        jmp uint16_to_float
W1:
        sec
        lda #0
        sbc accu
        sta accu
        lda #0
        sbc accu + 1
        sta accu + 1
        jsr uint16_to_float
        lda accu + 3
        ora #$80
        sta accu + 3
        rts
}

__asm uint32_to_float
{
        lda accu
        ora accu + 1
        ora accu + 2
        ora accu + 3
        bne W1
        rts
W1:
        ldx #$9e
        lda accu + 3
        bmi W2
L1:
        dex
        asl accu
        rol accu + 1
        rol accu + 2
        rol
        bpl L1
W2:
        bit accu
        bpl W3
        inc accu + 1
        bne W3
        inc accu + 2
        bne W3
        clc
        adc #1
        bcc W3
        lsr
        ror accu + 2
        ror accu + 1
        inx
W3:
        asl
        ldy accu + 1
        sty accu
        ldy accu + 2
        sty accu + 1
        sta accu + 2

        txa
        lsr
        sta accu + 3
        ror accu + 2
        rts
}

__asm sint32_to_float
{
        bit accu + 3
        bmi W1
        jmp uint32_to_float
W1:
        sec
        lda #0
        sbc accu
        sta accu
        lda #0
        sbc accu + 1
        sta accu + 1
        lda #0
        sbc accu + 2
        sta accu + 2
        lda #0
        sbc accu + 3
        sta accu + 3
        jsr uint32_to_float
        lda accu + 3
        ora #$80
        sta accu + 3
        rts
}

__asm f32_to_i16
{
        jsr freg.split_aexp
        lda tmp + 4
        cmp #$7f
        bcs W1
        lda #0
        sta accu
        sta accu + 1
        rts
W1:
        sbc #$8e
        bcc W2
        bit accu + 3
        bmi W5
        lda #$ff
        sta accu
        lda #$7f
        sta accu + 1
        rts
W5:
        lda #$00
        sta accu
        lda #$80
        sta accu + 1
        rts
W2:
        tax
        lda accu + 1
L1:
        lsr accu + 2
        ror
        inx
        bne L1
W3:
        bit accu + 3
        bpl W4

        sec
        eor #$ff
        adc #0
        sta accu
        lda #0
        sbc accu + 2
        sta accu + 1
        rts
W4:
        sta accu
        lda accu + 2
        sta accu + 1
        rts
}

__asm f32_to_u16
{
        jsr freg.split_aexp
        lda tmp + 4
        cmp #$7f
        bcs W1
        lda #0
W0:
        sta accu
        sta accu + 1
        rts
W1:
        sbc #$8e
        bcc W3
        beq W4
        lda #$ff
        bne W0
W3:
        tax
        lda accu + 1
L1:
        lsr accu + 2
        ror
        inx
        bne L1
W2:
        sta accu
        lda accu + 2
        sta accu + 1
        rts
W4:
        lda accu + 1
        bcs W2
}

__asm f32_to_u32
{
        jsr freg.split_aexp
        lda tmp + 4
        cmp #$7f
        bcs W1
        lda #0
F0:
        sta accu
        sta accu + 1
        sta accu + 2
        sta accu + 3
        rts
W1:
        sec
        sbc #$9e
        beq W2
        bcc W3
        lda #$ff
        bne F0

W3:
        tax

        lda #0
L1:
        lsr accu + 2
        ror accu + 1
        ror accu + 0
        ror
        inx
        bne L1

W2:
        ldx accu + 2
        stx accu + 3
        ldx accu + 1
        stx accu + 2
        ldx accu
        stx accu + 1
        sta accu
        rts
}

__asm f32_to_i32
{
        lda accu + 3
        bmi W1
        jmp f32_to_u32
W1:
        jsr f32_to_u32

        sec
        lda #0
        sbc accu
        sta accu
        lda #0
        sbc accu + 1
        sta accu + 1
        lda #0
        sbc accu + 2
        sta accu + 2
        lda #0
        sbc accu + 3
        sta accu + 3
        rts
}

// -------------------------------------------------------------------------
// Bit shift table and float rounding helpers
// -------------------------------------------------------------------------

unsigned char ubitmask[8] = {0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe, 0xff};

unsigned char bitshift[56] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

#pragma runtime(bitshift, bitshift)

__asm fround {
ffloor:
        bit accu + 3
        bpl frdown
        bmi frup

fceil:
        bit accu + 3
        bmi frdown
        bpl frup

frdzero:
        lda #0
        sta accu
        sta accu + 1
        sta accu + 2
        sta accu + 3
        rts

frdown:
        lda tmp + 4
        cmp #$7f
        bcc frdzero
        cmp #$87
        bcc frd1
        cmp #$8f
        bcc frd2
        cmp #$97
        bcs frd3

        sec
        sbc #$8f
        tax
        lda accu
        and ubitmask, x
        sta accu

        jmp frd3
frd1:
        sec
        sbc #$7f
        tax
        lda accu + 2
        and ubitmask, x
        sta accu + 2
        lda #0
        sta accu
        sta accu + 1

        jmp frd3
frd2:
        sec
        sbc #$87
        tax
        lda accu + 1
        and ubitmask, x
        sta accu + 1
        lda #0
        sta accu

        jmp frd3

frd3:
        jmp freg.merge_aexp

frone:
        lda #$7f
        sta tmp + 4
        lda #0
        sta accu + 0
        sta accu + 1
        lda #$80
        sta accu + 2
        jmp freg.merge_aexp

frup:
        lda accu
        ora accu + 1
        ora accu + 2
        beq frdzero
        lda tmp + 4
        cmp #$7f
        bcc frone
        cmp #$87
        bcc fru1
        cmp #$8f
        bcc fru2
        cmp #$97
        bcs fru3

        sec
        sbc #$8f
        tax

        clc
        lda ubitmask, x
        eor #$ff
        adc accu
        sta accu
        lda #0
        adc accu + 1
        sta accu + 1
        lda #0
        adc accu + 2
        bcc W1
        ror
        ror accu + 1
        ror accu
        inc tmp + 4
W1:     sta accu + 2
        jmp frdown
fru1:
        sec
        sbc #$7f
        tax

        clc
        lda #$ff
        adc accu
        lda #$ff
        adc accu + 1
        lda ubitmask, x
        eor #$ff
        adc accu + 2
        bcc W2
        ror
        ror accu + 1
        ror accu
        inc tmp + 4
W2:     sta accu + 2
        jmp frdown
fru2:
        sec
        sbc #$87
        tax

        clc
        lda #$ff
        adc accu
        lda ubitmask, x
        eor #$ff
        adc accu + 1
        sta accu + 1
        lda #0
        adc accu + 2
        bcc W3
        ror
        ror accu + 1
        ror accu
        inc tmp + 4
W3:     sta accu + 2
        jmp frdown
fru3:
        jmp freg.merge_aexp
}

// -------------------------------------------------------------------------
// 32-bit store/load via X register (ZP offset)
// -------------------------------------------------------------------------

__asm store32
{
        lda accu + 0
        sta $00, x
        lda accu + 1
        sta $01, x
        lda accu + 2
        sta $02, x
        lda accu + 3
        sta $03, x
        rts
}

__asm load32
{
        lda $00, x
        sta accu + 0
        lda $01, x
        sta accu + 1
        lda $02, x
        sta accu + 2
        lda $03, x
        sta accu + 3
        rts
}

// -------------------------------------------------------------------------
// Bytecode executor and indirect jump (native-mode stubs)
//
// In native mode (-n), bcexec is never actually called, but the linker
// requires the symbol. jmpaddr is used for function-pointer calls.
// -------------------------------------------------------------------------

__asm bcexec
{
        jmp (accu)
}

__asm jmpaddr
{
        jmp (addr)
}

#pragma runtime(bcexec, bcexec)
#pragma runtime(jmpaddr, jmpaddr)

// -------------------------------------------------------------------------
// Heap allocator — minimal stubs (Oric bare-metal: no OS heap)
//
// malloc returns NULL; free is a no-op.
// The linker always requires these symbols even if the program does not
// call them, because they appear in the Oscar64 runtime symbol table.
// -------------------------------------------------------------------------

__asm crt_malloc
{
        lda #0
        sta accu
        sta accu + 1
        rts
}

__asm crt_free
{
        rts
}

__asm crt_breakpoint
{
        rts
}

#pragma runtime(malloc, crt_malloc)
#pragma runtime(free, crt_free)
#pragma runtime(breakpoint, crt_breakpoint)

// -------------------------------------------------------------------------
// Runtime pragma declarations
// -------------------------------------------------------------------------

#pragma runtime(fsplita, freg.split_aexp)
#pragma runtime(fsplitt, freg.split_texp)
#pragma runtime(fsplitx, freg.split_xexp)
#pragma runtime(fmergea, freg.merge_aexp)
#pragma runtime(fadd, faddsub.fadd)
#pragma runtime(fsub, faddsub.fsub)
#pragma runtime(fmul, crt_fmul)
#pragma runtime(fdiv, crt_fdiv)
#pragma runtime(fcmp, crt_fcmp)
#pragma runtime(ffromi, sint16_to_float)
#pragma runtime(ffromu, uint16_to_float)
#pragma runtime(ftoi, f32_to_i16)
#pragma runtime(ftou, f32_to_u16)
#pragma runtime(ffloor, fround.ffloor)
#pragma runtime(fceil, fround.fceil)
#pragma runtime(ffromli, sint32_to_float)
#pragma runtime(ffromlu, uint32_to_float)
#pragma runtime(ftoli, f32_to_i32)
#pragma runtime(ftolu, f32_to_u32)
#pragma runtime(store32, store32)
#pragma runtime(load32, load32)
