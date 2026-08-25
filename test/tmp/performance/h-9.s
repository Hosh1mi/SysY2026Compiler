	.arch armv8-a
	.text
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #96
	stp x27, x28, [sp, #64]
	movz w28, #0
	stp x19, x20, [sp]
	movz w27, #3
	stp x21, x22, [sp, #16]
	stp x23, x24, [sp, #32]
	stp x25, x26, [sp, #48]
	bl getint
	str w0, [sp, #80]
	bl getint
	mov w22, w0
	movz w0, #52
	bl _sysy_starttime
	movz w25, #57186
	movz w23, #57607
	movz w20, #36553
	movz w19, #15187
	mov w26, w28
	mov w21, w28
	movk w25, #304, lsl #16
	movz w24, #23333
	movk w23, #1525, lsl #16
	movk w20, #5497, lsl #16
	movk w19, #38711, lsl #16
.Lmain_bb1:
	ldr w9, [sp, #80]
	cmp w26, w9
	b.ge .Lmain_bb18
.Lmain_bb2:
	tbz w26, #0, .Lmain_bb3
.Lmain_bb10:
	madd w10, w22, w25, w24
	smull x9, w10, w20
	asr x9, x9, #55
	add w9, w9, w9, lsr #31
	msub w10, w9, w23, w10
	cmp w10, #0
	add w9, w10, w23
	csel w22, w9, w10, lt
	smull x9, w22, w19
	asr x9, x9, #44
	add w10, w9, w9, lsr #31
	movn w9, #10006
	msub w9, w10, w9, w22
	add w13, w21, w9
	tbz w13, #0, .Lmain_bb31
	b .Lmain_bb33
.Lmain_bb31:
	mov w11, w28
	mov w12, w13
.Lmain_bb11:
	asr w12, w12, #1
	add w11, w11, #1
	tbz w12, #0, .Lmain_bb11
.Lmain_bb12:
	cmp w12, #9
	b.lt .Lmain_bb16
.Lmain_bb35:
	mov w10, w27
	b .Lmain_bb13
.Lmain_bb15:
	add w10, w10, #2
	mul w9, w10, w10
	cmp w9, w12
	b.gt .Lmain_bb16
.Lmain_bb13:
	sdiv w9, w12, w10
	msub w9, w9, w10, w12
	cbnz w9, .Lmain_bb15
.Lmain_bb14:
	sdiv w12, w12, w10
	sdiv w9, w12, w10
	msub w9, w9, w10, w12
	add w11, w11, #1
	cbz w9, .Lmain_bb14
	b .Lmain_bb15
.Lmain_bb16:
	cmp w12, #2
	add w9, w11, #1
	csel w10, w9, w11, gt
.Lmain_bb17:
	add w9, w13, w10
	cmp w9, #0
	cneg w9, w9, mi
	and w9, w9, #255
	cneg w21, w9, mi
	mov w0, w21
	bl putint
	movz w0, #10
	bl putch
	add w26, w26, #1
	b .Lmain_bb1
.Lmain_bb3:
	madd w10, w22, w25, w24
	smull x9, w10, w20
	asr x9, x9, #55
	add w9, w9, w9, lsr #31
	msub w10, w9, w23, w10
	cmp w10, #0
	add w9, w10, w23
	csel w22, w9, w10, lt
	movz w9, #50349
	movk w9, #26824, lsl #16
	smull x9, w22, w9
	asr x9, x9, #44
	add w9, w9, w9, lsr #31
	sub w13, w21, w9
	tbz w13, #0, .Lmain_bb19
	b .Lmain_bb21
.Lmain_bb19:
	mov w11, w28
	mov w12, w13
.Lmain_bb4:
	asr w12, w12, #1
	add w11, w11, #1
	tbz w12, #0, .Lmain_bb4
.Lmain_bb5:
	cmp w12, #9
	b.lt .Lmain_bb9
.Lmain_bb23:
	mov w10, w27
	b .Lmain_bb6
.Lmain_bb8:
	add w10, w10, #2
	mul w9, w10, w10
	cmp w9, w12
	b.gt .Lmain_bb9
.Lmain_bb6:
	sdiv w9, w12, w10
	msub w9, w9, w10, w12
	cbnz w9, .Lmain_bb8
.Lmain_bb7:
	sdiv w12, w12, w10
	sdiv w9, w12, w10
	msub w9, w9, w10, w12
	add w11, w11, #1
	cbz w9, .Lmain_bb7
	b .Lmain_bb8
.Lmain_bb9:
	cmp w12, #2
	add w9, w11, #1
	csel w10, w9, w11, gt
	b .Lmain_bb17
.Lmain_bb18:
	movz w0, #76
	bl _sysy_stoptime
	adrp x9, seed
	str w22, [x9, :lo12:seed]
	ldp x27, x28, [sp, #64]
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	movz w0, #0
	add sp, sp, #96
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb21:
	mov w12, w13
	mov w11, w28
	b .Lmain_bb5
.Lmain_bb33:
	mov w12, w13
	mov w11, w28
	b .Lmain_bb12
	.size main, .-main
	.data
	.global seed
	.p2align 2
seed:
	.zero 4
