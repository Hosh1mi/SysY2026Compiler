	.arch armv8-a
	.text
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #32
	stp x19, x20, [sp]
	adrp x9, global
	stp x21, x22, [sp, #16]
	ldr w21, [x9, :lo12:global]
	movz w22, #0
	bl getint
	mov w19, w0
	movz w0, #34480
	movk w0, #1, lsl #16
	bl _sysy_starttime
	movz w12, #34953
	movz w13, #57345
	movz w11, #16383
	sub w17, w19, #7
	mov w16, w22
	orr w15, wzr, #0x80000007
	movz w14, #60
	movk w12, #34952, lsl #16
	movk w13, #2047, lsl #16
	movk w11, #4096, lsl #16
.Lmain_bb1:
	cmp w16, w17
	cset w10, lt
	cmp w19, w15
	cset w9, ge
	and w9, w9, w10
	cbz w9, .Lmain_bb6
.Lmain_bb5:
	mul w10, w16, w14
	smull x9, w10, w12
	asr x9, x9, #32
	add w9, w9, w10
	add w10, w16, #1
	mul w20, w10, w14
	asr w9, w9, #5
	add w9, w9, w9, lsr #31
	add w21, w22, w9
	smull x10, w21, w11
	asr x10, x10, #55
	smull x9, w20, w12
	add w10, w10, w10, lsr #31
	msub w21, w10, w13, w21
	asr x9, x9, #32
	add w9, w9, w20
	add w10, w16, #2
	mul w20, w10, w14
	asr w9, w9, #5
	add w9, w9, w9, lsr #31
	add w21, w21, w9
	smull x10, w21, w11
	asr x10, x10, #55
	smull x9, w20, w12
	add w10, w10, w10, lsr #31
	msub w21, w10, w13, w21
	asr x9, x9, #32
	add w9, w9, w20
	add w10, w16, #3
	mul w20, w10, w14
	asr w9, w9, #5
	add w9, w9, w9, lsr #31
	add w21, w21, w9
	smull x10, w21, w11
	asr x10, x10, #55
	smull x9, w20, w12
	add w10, w10, w10, lsr #31
	msub w21, w10, w13, w21
	asr x9, x9, #32
	add w9, w9, w20
	add w10, w16, #4
	mul w20, w10, w14
	asr w9, w9, #5
	add w9, w9, w9, lsr #31
	add w21, w21, w9
	smull x10, w21, w11
	asr x10, x10, #55
	smull x9, w20, w12
	add w10, w10, w10, lsr #31
	msub w21, w10, w13, w21
	asr x9, x9, #32
	add w9, w9, w20
	add w10, w16, #5
	mul w20, w10, w14
	asr w9, w9, #5
	add w9, w9, w9, lsr #31
	add w21, w21, w9
	smull x10, w21, w11
	asr x10, x10, #55
	smull x9, w20, w12
	add w10, w10, w10, lsr #31
	msub w21, w10, w13, w21
	asr x9, x9, #32
	add w9, w9, w20
	add w10, w16, #6
	mul w20, w10, w14
	asr w9, w9, #5
	add w9, w9, w9, lsr #31
	add w21, w21, w9
	smull x10, w21, w11
	asr x10, x10, #55
	smull x9, w20, w12
	add w10, w10, w10, lsr #31
	msub w10, w10, w13, w21
	asr x9, x9, #32
	add w9, w9, w20
	add w21, w16, #7
	mul w20, w21, w14
	asr w9, w9, #5
	add w9, w9, w9, lsr #31
	add w22, w10, w9
	smull x10, w22, w11
	smull x9, w20, w12
	asr x10, x10, #55
	asr x9, x9, #32
	add w10, w10, w10, lsr #31
	msub w10, w10, w13, w22
	add w9, w9, w20
	asr w9, w9, #5
	add w9, w9, w9, lsr #31
	add w10, w10, w9
	smull x9, w10, w11
	asr x9, x9, #55
	add w9, w9, w9, lsr #31
	msub w22, w9, w13, w10
	add w16, w16, #8
	b .Lmain_bb1
.Lmain_bb2:
	cmp w16, w19
	b.ge .Lmain_bb4
.Lmain_bb3:
	mul w13, w16, w14
	smull x9, w13, w11
	asr x9, x9, #32
	add w9, w9, w13
	asr w9, w9, #5
	add w9, w9, w9, lsr #31
	add w13, w20, w9
	smull x9, w13, w10
	asr x9, x9, #55
	add w9, w9, w9, lsr #31
	msub w20, w9, w12, w13
	add w9, w16, #1
	mov w21, w16
	mov w16, w9
	b .Lmain_bb2
.Lmain_bb4:
	movz w0, #34496
	movk w0, #1, lsl #16
	bl _sysy_stoptime
	mov w0, w20
	bl putint
	movz w0, #10
	bl putch
	adrp x10, loopCount
	adrp x9, global
	str w19, [x10, :lo12:loopCount]
	str w21, [x9, :lo12:global]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	movz w0, #0
	add sp, sp, #32
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb6:
	movz w11, #34953
	movz w12, #57345
	movz w10, #16383
	mov w20, w22
	movz w14, #60
	movk w11, #34952, lsl #16
	movk w12, #2047, lsl #16
	movk w10, #4096, lsl #16
	b .Lmain_bb2
	.size main, .-main
	.data
	.global loopCount
	.p2align 2
loopCount:
	.zero 4
	.global global
	.p2align 2
global:
	.zero 4
