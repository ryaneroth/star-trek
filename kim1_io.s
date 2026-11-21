.setcpu "6502"

.export _kim_getc
.export _kim_putc

.segment "CODE"

; KIM-1 monitor serial input and output routines.
_kim_getc:
        jsr     $1E5A
        rts

; cc65 fastcall passes the character in A, as expected by OUTCH.
_kim_putc:
        jsr     $1EA0
        rts
