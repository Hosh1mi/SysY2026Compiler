	.arch armv8-a
	.text
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #16, lsl #12
	sub sp, sp, #96
	stp x19, x20, [sp]
	adrp x9, COUNT
	stp x21, x22, [sp, #16]
	stp x23, x24, [sp, #32]
	stp x25, x26, [sp, #48]
	stp x27, x28, [sp, #64]
	stp d8, d9, [sp, #80]
	ldr w20, [x9, :lo12:COUNT]
	movz w9, #16256, lsl #16
	movz w19, #0
	fmov s8, w9
	add x23, sp, #8, lsl #12
	fmov s9, w19
	add x24, sp, #96
	add x23, x23, #96
	movz w21, #1
	bl getint
	mov w22, w0
	movz w0, #22
	bl _sysy_starttime
	fmov s23, s8
	movz w13, #26215
	movz w11, #52429
	movz w10, #52429
	fmov s22, s9
	fmov s8, s9
	mov w8, w19
	mov w27, w19
	movz w26, #10
	movk w13, #26214, lsl #16
	movz w25, #0
	movk w11, #15820, lsl #16
	movk w10, #15948, lsl #16
	movz w9, #16256, lsl #16
	orr w17, wzr, #0x80000003
.Lmain_bb1:
	cmp w27, w20
	b.ge .Lmain_bb15
.Lmain_bb2:
	smull x12, w27, w13
	asr x12, x12, #34
	fmov s16, w10
	add w12, w12, w12, lsr #31
	msub w12, w12, w26, w27
	fadd s17, s23, s16
	cmp w12, #0
	fmov s18, w11
	cset w12, ne
	fmov s16, w9
	cmp w12, #0
	fadd s18, s22, s18
	fcsel s23, s16, s17, ne
	fmov s16, w25
	cmp w12, #0
	fcsel s22, s16, s18, ne
	cmp w8, w22
	b.ge .Lmain_bb9
.Lmain_bb3:
	sub w28, w22, #3
	cmp w8, w28
	cset w14, lt
	cmp w22, w17
	cset w12, ge
	and w12, w12, w14
	cbz w12, .Lmain_bb22
.Lmain_bb20:
	mov w16, w8
.Lmain_bb4:
	scvtf s16, w16
	add w15, w16, #1
	scvtf s18, w15
	add w12, w16, #2
	fadd s19, s22, s16
	scvtf s17, w12
	fadd s24, s23, s16
	fadd s21, s22, s18
	fadd s20, s23, s18
	add w14, w16, #3
	str s19, [x24, w16, sxtw #2]
	scvtf s16, w14
	fadd s19, s22, s17
	fadd s18, s23, s17
	fadd s17, s22, s16
	str s24, [x23, w16, sxtw #2]
	str s21, [x24, w15, sxtw #2]
	fadd s16, s23, s16
	str s20, [x23, w15, sxtw #2]
	str s19, [x24, w12, sxtw #2]
	str s18, [x23, w12, sxtw #2]
	add w16, w16, #4
	str s17, [x24, w14, sxtw #2]
	cmp w16, w28
	str s16, [x23, w14, sxtw #2]
	b.lt .Lmain_bb4
.Lmain_bb5:
	cmp w16, w22
	b.ge .Lmain_bb25
.Lmain_bb23:
	mov w12, w16
.Lmain_bb7:
	scvtf s16, w12
	fadd s17, s22, s16
	fadd s16, s23, s16
	str s17, [x24, w12, sxtw #2]
	add w16, w12, #1
	str s16, [x23, w12, sxtw #2]
	cmp w16, w22
	b.ge .Lmain_bb26
.Lmain_bb24:
	mov w12, w16
	b .Lmain_bb7
.Lmain_bb9:
	sub w28, w22, #3
	fmov s21, s9
	mov w16, w19
	orr w14, wzr, #0x80000003
.Lmain_bb10:
	cmp w16, w28
	cset w15, lt
	cmp w22, w14
	cset w12, ge
	and w12, w12, w15
	cbz w12, .Lmain_bb28
.Lmain_bb14:
	ldr s19, [x24, w16, sxtw #2]
	ldr s18, [x23, w16, sxtw #2]
	add w12, w16, #1
	ldr s17, [x24, w12, sxtw #2]
	ldr s16, [x23, w12, sxtw #2]
	fmul s20, s19, s18
	add w12, w16, #2
	ldr s19, [x24, w12, sxtw #2]
	ldr s18, [x23, w12, sxtw #2]
	fadd s21, s21, s20
	fmul s25, s17, s16
	add w12, w16, #3
	ldr s17, [x24, w12, sxtw #2]
	ldr s16, [x23, w12, sxtw #2]
	fadd s20, s21, s25
	fmul s18, s19, s18
	fadd s18, s20, s18
	fmul s19, s17, s16
	fadd s21, s18, s19
	add w16, w16, #4
	b .Lmain_bb10
.Lmain_bb11:
	cmp w12, w22
	b.ge .Lmain_bb13
.Lmain_bb12:
	ldr s17, [x24, w12, sxtw #2]
	ldr s16, [x23, w12, sxtw #2]
	fmul s16, s17, s16
	fadd s18, s18, s16
	add w12, w12, #1
	b .Lmain_bb11
.Lmain_bb13:
	fadd s8, s8, s18
	add w27, w27, #1
	b .Lmain_bb1
.Lmain_bb15:
	movz w0, #39
	bl _sysy_stoptime
	movz w9, #19627
	movk w9, #23170, lsl #16
	fmov s16, w9
	fsub s17, s8, s16
	movz w9, #14269
	movk w9, #13702, lsl #16
	fmov s16, w9
	fcmp s17, s16
	b.gt .Lmain_bb18
.Lmain_bb16:
	movz w9, #14269
	movk w9, #46470, lsl #16
	fmov s16, w9
	fcmp s17, s16
	b.mi .Lmain_bb18
.Lmain_bb17:
	movz w0, #10
	bl putint
.Lmain_bb19:
	adrp x9, COUNT
	str w20, [x9, :lo12:COUNT]
	mov w0, w19
	ldp d8, d9, [sp, #80]
	ldp x27, x28, [sp, #64]
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	add sp, sp, #16, lsl #12
	add sp, sp, #96
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb18:
	movz w0, #1
	bl putint
	mov w19, w21
	b .Lmain_bb19
.Lmain_bb22:
	mov w12, w8
	b .Lmain_bb7
.Lmain_bb25:
	mov w8, w16
	b .Lmain_bb9
.Lmain_bb26:
	mov w8, w16
	b .Lmain_bb9
.Lmain_bb28:
	fmov s18, s21
	mov w12, w16
	b .Lmain_bb11
	.size main, .-main
	.data
	.global COUNT
	.p2align 2
COUNT:
	.word 100000
