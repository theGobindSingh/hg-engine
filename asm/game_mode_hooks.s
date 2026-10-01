.text
.align 2
.thumb

.global GMS_MainTaskHook
GMS_MainTaskHook:
    bl   GMS_DoMainTask
    cmp  r0, #1
    bne  1f
    ldr  r1, =0x021E5A64 | 1
    bx   r1
1:  ldr  r1, =0x021E5A80 | 1
    bx   r1

.pool
