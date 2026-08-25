	.arch armv8-a
	.text
	.p2align 2
	.global func
	.type func, %function
func:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #16
	str d8, [sp, #8]
	mov w9, w0
	str x19, [sp]
	fmov s8, s0
	cmp w9, #0
	b.lt .Lfunc_bb3
.Lfunc_bb1:
	sub w19, w9, #1
	fmov s0, s8
	mov w0, w19
	bl func
	fadd s8, s8, s0
	fmov s0, s8
	mov w0, w19
	bl func
	fsub s0, s8, s0
.Lfunc_bb2:
	ldr d8, [sp, #8]
	ldr x19, [sp]
	add sp, sp, #16
	ldp xzr, x30, [sp], #16
	ret
.Lfunc_bb3:
	movz w9, #0
	fmov s0, w9
	b .Lfunc_bb2
	.size func, .-func
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	movz w0, #21
	bl _sysy_starttime
	bl getint
	movz w9, #8389
	movk w9, #16256, lsl #16
	fmov s0, w9
	bl func
	fcmp s0, #0.0
	cset w10, eq
	cset w9, vs
	orr w9, w10, w9
	cbz w9, .Lmain_bb2
.Lmain_bb1:
	movz w0, #112
	bl putch
.Lmain_bb2:
	movz w0, #31
	bl _sysy_stoptime
	movz w0, #0
	ldp xzr, x30, [sp], #16
	ret
	.size main, .-main
