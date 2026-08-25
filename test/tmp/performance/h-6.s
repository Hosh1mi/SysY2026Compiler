	.arch armv8-a
	.text
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #64
	movz w10, #52429
	movz w9, #39322
	stp d8, d9, [sp, #32]
	movz w11, #16256, lsl #16
	str d10, [sp, #48]
	movk w10, #16268, lsl #16
	movk w9, #16537, lsl #16
	stp x19, x20, [sp]
	fmov s10, w11
	stp x21, x22, [sp, #16]
	fmov s9, w10
	fmov s8, w9
	movz w22, #1
	movz w20, #0
	bl getint
	mov w19, w0
	movz w0, #41
	bl _sysy_starttime
	movz w13, #52429
	movz w12, #13107
	movz w11, #52429
	movz w10, #55050
	mov w21, w20
	fmov s24, s10
	fmov s23, s9
	fmov s22, s8
	movk w13, #16332, lsl #16
	movk w12, #16627, lsl #16
	movk w11, #16604, lsl #16
	movk w10, #15395, lsl #16
	movz w9, #16664, lsl #16
	orr w16, wzr, #0x80000007
.Lmain_bb1:
	cmp w21, w19
	b.ge .Lmain_bb2
.Lmain_bb8:
	fmov s17, w13
	fmov s16, w12
	fsub s18, s22, s17
	fsub s21, s23, s16
	fmov s17, w11
	fmul s16, s18, s21
	fsub s20, s24, s17
	fmul s17, s16, s20
	fmov s16, w10
	fdiv s19, s16, s17
	cmp w22, w19
	b.ge .Lmain_bb15
.Lmain_bb9:
	fmov s16, w9
	fmul s18, s18, s16
	fmul s17, s21, s16
	fmul s16, s20, s16
	fmul s21, s18, s19
	fmul s20, s17, s19
	sub w17, w19, #7
	cmp w22, w17
	fmul s19, s16, s19
	cset w15, lt
	cmp w19, w16
	cset w14, ge
	and w14, w14, w15
	cbz w14, .Lmain_bb19
.Lmain_bb17:
	mov w15, w22
	fmov s16, s24
	fmov s17, s23
	fmov s18, s22
.Lmain_bb10:
	fsub s18, s18, s21
	fsub s17, s17, s20
	fsub s16, s16, s19
	fsub s18, s18, s21
	fsub s17, s17, s20
	fsub s16, s16, s19
	fsub s18, s18, s21
	fsub s17, s17, s20
	fsub s16, s16, s19
	fsub s18, s18, s21
	fsub s17, s17, s20
	fsub s16, s16, s19
	fsub s18, s18, s21
	fsub s17, s17, s20
	fsub s16, s16, s19
	fsub s18, s18, s21
	fsub s17, s17, s20
	fsub s16, s16, s19
	fsub s18, s18, s21
	fsub s17, s17, s20
	fsub s16, s16, s19
	fsub s18, s18, s21
	fsub s17, s17, s20
	fsub s16, s16, s19
	add w15, w15, #8
	cmp w15, w17
	b.lt .Lmain_bb10
.Lmain_bb11:
	cmp w15, w19
	b.ge .Lmain_bb22
.Lmain_bb13:
	fsub s18, s18, s21
	fsub s17, s17, s20
	fsub s16, s16, s19
	add w15, w15, #1
	cmp w15, w19
	b.lt .Lmain_bb13
.Lmain_bb23:
	mov w22, w15
	fmov s24, s16
	fmov s23, s17
	fmov s22, s18
.Lmain_bb15:
	add w21, w21, #1
	b .Lmain_bb1
.Lmain_bb2:
	movz w11, #4719
	movz w10, #42467
	movz w9, #20447
	sub w15, w19, #7
	fmov s16, s24
	fmov s17, s23
	fmov s18, s22
	orr w13, wzr, #0x80000007
	movk w11, #15491, lsl #16
	movk w10, #15771, lsl #16
	movk w9, #15757, lsl #16
.Lmain_bb3:
	cmp w20, w15
	cset w14, lt
	cmp w19, w13
	cset w12, ge
	and w12, w12, w14
	cbz w12, .Lmain_bb16
.Lmain_bb7:
	fmov s21, w11
	fmov s20, w10
	fmov s19, w9
	fadd s18, s18, s21
	fadd s17, s17, s20
	fadd s16, s16, s19
	fadd s18, s18, s21
	fadd s17, s17, s20
	fadd s16, s16, s19
	fmov s21, w11
	fmov s20, w10
	fmov s19, w9
	fadd s18, s18, s21
	fadd s17, s17, s20
	fadd s16, s16, s19
	fadd s18, s18, s21
	fadd s17, s17, s20
	fadd s16, s16, s19
	fmov s21, w11
	fmov s20, w10
	fmov s19, w9
	fadd s18, s18, s21
	fadd s17, s17, s20
	fadd s16, s16, s19
	fadd s18, s18, s21
	fadd s17, s17, s20
	fadd s16, s16, s19
	fmov s21, w11
	fmov s20, w10
	fmov s19, w9
	fadd s18, s18, s21
	fadd s17, s17, s20
	fadd s16, s16, s19
	fadd s18, s18, s21
	fadd s17, s17, s20
	fadd s16, s16, s19
	add w20, w20, #8
	b .Lmain_bb3
.Lmain_bb4:
	cmp w12, w19
	b.ge .Lmain_bb6
.Lmain_bb5:
	fmov s18, w11
	fmov s17, w10
	fmov s16, w9
	fadd s19, s19, s18
	fadd s20, s20, s17
	fadd s9, s9, s16
	add w12, w12, #1
	b .Lmain_bb4
.Lmain_bb6:
	fadd s8, s19, s20
	movz w0, #43
	bl _sysy_stoptime
	fsub s0, s8, s9
	bl putfloat
	ldp d9, d10, [sp, #40]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	ldr d8, [sp, #32]
	movz w0, #0
	add sp, sp, #64
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb16:
	movz w11, #4719
	movz w10, #42467
	movz w9, #20447
	mov w12, w20
	fmov s9, s16
	fmov s20, s17
	fmov s19, s18
	movk w11, #15491, lsl #16
	movk w10, #15771, lsl #16
	movk w9, #15757, lsl #16
	b .Lmain_bb4
.Lmain_bb19:
	mov w15, w22
	fmov s16, s24
	fmov s17, s23
	fmov s18, s22
	b .Lmain_bb13
.Lmain_bb22:
	mov w22, w15
	fmov s24, s16
	fmov s23, s17
	fmov s22, s18
	b .Lmain_bb15
	.size main, .-main
