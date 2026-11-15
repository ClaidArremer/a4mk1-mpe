        | The engine's modulation loop (0x400fd162..0x400fd1d0, once per voice k = 0..5)
        | reads s = 0x80005548[k], the track that started the voice's note, and indexes
        | the per-track bend / mod wheel / breath / aftertouch arrays with it. This
        | replaces the loop's
        |   0x400fd164  movea.l %d5,%a1 ; addq.l #4,%d6 ; move.l (%a0)+,%d2
        | (patched to jsr mpe_loop_hook), does the same, then lets mpe_voice_index
        | swap %d2 for an index into the MPE per-voice block. %a1 is live after the
        | call; %d0/%d1/%a0 are reloaded before use.
        .section .mpe1,"ax",@progbits
        .align  2
        .globl  mpe_loop_hook
mpe_loop_hook:
        movea.l %d5,%a1
        addq.l  #4,%d6
        move.l  (%a0)+,%d2
        move.l  %a0,%d0
        subi.l  #0x8000554c,%d0                   | 4 * k
        lsr.l   #2,%d0
        move.l  %a1,-(%sp)
        move.l  %d0,-(%sp)
        move.l  %d2,-(%sp)
        jsr     mpe_voice_index
        addq.l  #8,%sp
        move.l  %d0,%d2
        movea.l (%sp)+,%a1
        rts
