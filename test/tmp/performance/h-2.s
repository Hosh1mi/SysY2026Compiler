	.arch armv8-a
	.text
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #9, lsl #12
	sub sp, sp, #3216
	stp x19, x20, [sp]
	adrp x9, COUNT
	stp x21, x22, [sp, #16]
	stp x23, x24, [sp, #32]
	stp x25, x26, [sp, #48]
	stp d8, d9, [sp, #64]
	ldr w20, [x9, :lo12:COUNT]
	movz w9, #16256, lsl #16
	movz w19, #0
	fmov s8, w9
	add x22, sp, #4, lsl #12
	fmov s9, w19
	add x23, sp, #80
	add x22, x22, #3696
	movz w21, #1
	movz w0, #22
	bl _sysy_starttime
	fmov s23, s8
	movz w13, #26215
	movz w11, #52429
	movz w10, #52429
	fmov s22, s9
	fmov s8, s9
	mov w16, w19
	mov w26, w19
	movz w25, #10
	movk w13, #26214, lsl #16
	movz w24, #0
	movk w11, #15820, lsl #16
	movk w10, #15948, lsl #16
	movz w9, #16256, lsl #16
	movz w17, #5000
.Lmain_bb1:
	cmp w26, w20
	b.ge .Lmain_bb10
.Lmain_bb2:
	smull x12, w26, w13
	asr x12, x12, #34
	fmov s16, w10
	add w12, w12, w12, lsr #31
	msub w12, w12, w25, w26
	fadd s17, s23, s16
	cmp w12, #0
	fmov s18, w11
	cset w12, ne
	fmov s16, w9
	cmp w12, #0
	fadd s18, s22, s18
	fcsel s23, s16, s17, ne
	fmov s16, w24
	cmp w12, #0
	fcsel s22, s16, s18, ne
	cmp w16, w17
	b.ge .Lmain_bb4
.Lmain_bb15:
	mov w14, w16
	movz w12, #4999
.Lmain_bb3:
	scvtf s16, w14
	fadd s17, s22, s16
	fadd s16, s23, s16
	str s17, [x23, w14, sxtw #2]
	add w16, w14, #1
	str s16, [x22, w14, sxtw #2]
	cmp w14, w12
	b.ge .Lmain_bb4
.Lmain_bb16:
	mov w14, w16
	b .Lmain_bb3
.Lmain_bb4:
	fmov s21, s9
	mov w15, w19
	movz w14, #4997
.Lmain_bb5:
	cmp w15, w14
	b.ge .Lmain_bb19
.Lmain_bb9:
	ldr s19, [x23, w15, sxtw #2]
	ldr s18, [x22, w15, sxtw #2]
	add w12, w15, #1
	ldr s17, [x23, w12, sxtw #2]
	ldr s16, [x22, w12, sxtw #2]
	fmul s20, s19, s18
	add w12, w15, #2
	ldr s19, [x23, w12, sxtw #2]
	ldr s18, [x22, w12, sxtw #2]
	fadd s21, s21, s20
	fmul s25, s17, s16
	add w12, w15, #3
	ldr s17, [x23, w12, sxtw #2]
	ldr s16, [x22, w12, sxtw #2]
	fadd s20, s21, s25
	fmul s18, s19, s18
	fadd s18, s20, s18
	fmul s19, s17, s16
	fadd s21, s18, s19
	add w15, w15, #4
	b .Lmain_bb5
.Lmain_bb6:
	cmp w12, w14
	b.ge .Lmain_bb8
.Lmain_bb7:
	ldr s17, [x23, w12, sxtw #2]
	ldr s16, [x22, w12, sxtw #2]
	fmul s16, s17, s16
	fadd s18, s18, s16
	add w12, w12, #1
	b .Lmain_bb6
.Lmain_bb8:
	fadd s8, s8, s18
	add w26, w26, #1
	b .Lmain_bb1
.Lmain_bb10:
	movz w0, #39
	bl _sysy_stoptime
	movz w9, #3406
	movk w9, #23188, lsl #16
	fmov s16, w9
	fsub s16, s8, s16
	fcvtzs w9, s16
	scvtf s17, w9
	movz w9, #14269
	movk w9, #13702, lsl #16
	fmov s16, w9
	fcmp s17, s16
	b.gt .Lmain_bb13
.Lmain_bb11:
	movz w9, #14269
	movk w9, #46470, lsl #16
	fmov s16, w9
	fcmp s17, s16
	b.mi .Lmain_bb13
.Lmain_bb12:
	movz w0, #10
	bl putint
.Lmain_bb14:
	adrp x9, COUNT
	str w20, [x9, :lo12:COUNT]
	mov w0, w19
	ldp d8, d9, [sp, #64]
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	add sp, sp, #9, lsl #12
	add sp, sp, #3216
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb13:
	movz w0, #1
	bl putint
	mov w19, w21
	b .Lmain_bb14
.Lmain_bb19:
	fmov s18, s21
	mov w12, w15
	movz w14, #5000
	b .Lmain_bb6
	.size main, .-main
	.data
	.global COUNT
	.p2align 2
COUNT:
	.word 500000
