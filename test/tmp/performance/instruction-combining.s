	.arch armv8-a
	.text
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #32
	str x21, [sp, #16]
	movz w21, #0
	stp x19, x20, [sp]
	bl getint
	mov w19, w0
	movz w0, #10015
	bl _sysy_starttime
	movz w12, #34953
	movz w13, #49153
	movz w11, #32767
	sub w17, w19, #3
	mov w16, w21
	orr w15, wzr, #0x80000003
	movz w14, #60
	movk w12, #34952, lsl #16
	movk w13, #8191, lsl #16
	movk w11, #16384, lsl #16
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
	add w21, w21, w9
	smull x10, w21, w11
	asr x10, x10, #59
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
	asr x10, x10, #59
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
	smull x9, w20, w12
	asr x10, x10, #59
	asr x9, x9, #32
	add w10, w10, w10, lsr #31
	msub w10, w10, w13, w21
	add w9, w9, w20
	asr w9, w9, #5
	add w9, w9, w9, lsr #31
	add w10, w10, w9
	smull x9, w10, w11
	asr x9, x9, #59
	add w9, w9, w9, lsr #31
	msub w21, w9, w13, w10
	add w16, w16, #4
	b .Lmain_bb1
.Lmain_bb2:
	cmp w16, w19
	b.ge .Lmain_bb4
.Lmain_bb3:
	mul w12, w16, w14
	smull x9, w12, w11
	asr x9, x9, #32
	add w9, w9, w12
	asr w9, w9, #5
	add w9, w9, w9, lsr #31
	add w12, w20, w9
	smull x9, w12, w10
	asr x9, x9, #59
	add w9, w9, w9, lsr #31
	msub w20, w9, w13, w12
	add w16, w16, #1
	b .Lmain_bb2
.Lmain_bb4:
	movz w0, #10030
	bl _sysy_stoptime
	mov w0, w20
	bl putint
	movz w0, #10
	bl putch
	adrp x9, loopCount
	str w19, [x9, :lo12:loopCount]
	ldp x20, x21, [sp, #8]
	ldr x19, [sp]
	movz w0, #0
	add sp, sp, #32
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb6:
	movz w11, #34953
	movz w13, #49153
	movz w10, #32767
	mov w20, w21
	movz w14, #60
	movk w11, #34952, lsl #16
	movk w13, #8191, lsl #16
	movk w10, #16384, lsl #16
	b .Lmain_bb2
	.size main, .-main
	.data
	.global loopCount
	.p2align 2
loopCount:
	.zero 4
