	.arch armv8-a
	.text
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #80
	stp x19, x20, [sp]
	movz w20, #0
	stp x21, x22, [sp, #16]
	stp x23, x24, [sp, #32]
	stp x25, x26, [sp, #48]
	stp x27, x28, [sp, #64]
	bl getint
	mov w19, w0
	mov w21, w20
.Lmain_bb1:
	cmp w21, w19
	b.ge .Lmain_bb25
.Lmain_bb21:
	adrp x9, A
	add x10, x9, :lo12:A
	sxtw x9, w21
	add x9, x10, x9, lsl #12
	mov x23, x9
	mov w22, w20
.Lmain_bb22:
	cmp w22, w19
	b.ge .Lmain_bb24
.Lmain_bb23:
	bl getint
	str w0, [x23], #4
	add w22, w22, #1
	b .Lmain_bb22
.Lmain_bb2:
	cmp w21, w19
	b.ge .Lmain_bb7
.Lmain_bb3:
	adrp x9, B
	add x10, x9, :lo12:B
	sxtw x9, w21
	add x9, x10, x9, lsl #12
	mov x23, x9
	mov w22, w20
.Lmain_bb4:
	cmp w22, w19
	b.ge .Lmain_bb6
.Lmain_bb5:
	bl getint
	str w0, [x23], #4
	add w22, w22, #1
	b .Lmain_bb4
.Lmain_bb6:
	add w21, w21, #1
	b .Lmain_bb2
.Lmain_bb7:
	movz w0, #65
	bl _sysy_starttime
	adrp x11, A
	adrp x10, B
	adrp x9, C
	add x25, x11, :lo12:A
	add x26, x10, :lo12:B
	add x27, x9, :lo12:C
	mov w28, w20
	movz w24, #2
	movz w23, #0
	movz w22, #3
	movz w21, #1
.Lmain_bb8:
	cmp w28, #5
	b.ge .Lmain_bb26
.Lmain_bb20:
	adrp x10, __sysy_par_ctx_2_0
	adrp x9, __sysy_par_ctx_2_1
	str w19, [x10, :lo12:__sysy_par_ctx_2_0]
	mov w0, w24
	str x27, [x9, :lo12:__sysy_par_ctx_2_1]
	mov w1, w23
	mov w2, w19
	bl __sysy_parallel_for
	adrp x12, __sysy_par_ctx_3_0
	adrp x11, __sysy_par_ctx_3_1
	adrp x10, __sysy_par_ctx_3_2
	adrp x9, __sysy_par_ctx_3_3
	str w19, [x12, :lo12:__sysy_par_ctx_3_0]
	mov w0, w22
	str x25, [x11, :lo12:__sysy_par_ctx_3_1]
	mov w1, w23
	str x27, [x10, :lo12:__sysy_par_ctx_3_2]
	mov w2, w19
	str x26, [x9, :lo12:__sysy_par_ctx_3_3]
	bl __sysy_parallel_for
	adrp x10, __sysy_par_ctx_0_0
	adrp x9, __sysy_par_ctx_0_1
	str w19, [x10, :lo12:__sysy_par_ctx_0_0]
	mov w0, w23
	str x26, [x9, :lo12:__sysy_par_ctx_0_1]
	mov w1, w23
	mov w2, w19
	bl __sysy_parallel_for
	adrp x12, __sysy_par_ctx_1_0
	adrp x11, __sysy_par_ctx_1_1
	adrp x10, __sysy_par_ctx_1_2
	adrp x9, __sysy_par_ctx_1_3
	str w19, [x12, :lo12:__sysy_par_ctx_1_0]
	mov w0, w21
	str x25, [x11, :lo12:__sysy_par_ctx_1_1]
	mov w1, w23
	str x26, [x10, :lo12:__sysy_par_ctx_1_2]
	mov w2, w19
	str x27, [x9, :lo12:__sysy_par_ctx_1_3]
	bl __sysy_parallel_for
	add w28, w28, #1
	b .Lmain_bb8
.Lmain_bb9:
	cmp w14, w19
	b.ge .Lmain_bb19
.Lmain_bb10:
	movi v18.4s, #0
	mov v18.s[0], w21
	adrp x9, B
	add x10, x9, :lo12:B
	sxtw x9, w14
	add x9, x10, x9, lsl #12
	mov x12, x9
	mov w16, w20
	orr w11, wzr, #0x7ffffff8
.Lmain_bb11:
	cmp w16, w11
	cset w10, le
	add w9, w16, #7
	cmp w9, w19
	cset w9, lt
	and w9, w10, w9
	cbz w9, .Lmain_bb13
.Lmain_bb12:
	ldp q17, q16, [x12]
	add v17.4s, v18.4s, v17.4s
	add v18.4s, v17.4s, v16.4s
	add x9, x12, #16
	add w16, w16, #8
	add x12, x9, #16
	b .Lmain_bb11
.Lmain_bb13:
	addv s16, v18.4s
	adrp x9, B
	add x10, x9, :lo12:B
	sxtw x9, w14
	fmov w13, s16
	add x9, x10, x9, lsl #12
	add x17, x9, w16, sxtw #2
	sub w21, w19, #3
	orr w15, wzr, #0x80000003
.Lmain_bb14:
	cmp w16, w21
	cset w10, lt
	cmp w19, w15
	cset w9, ge
	and w9, w9, w10
	cbz w9, .Lmain_bb27
.Lmain_bb18:
	ldp w12, w11, [x17]
	ldp w10, w9, [x17, #8]
	add w12, w13, w12
	add x13, x17, #4
	add w12, w12, w11
	add x11, x13, #4
	add w10, w12, w10
	add x12, x11, #4
	add w13, w10, w9
	add w16, w16, #4
	add x17, x12, #4
	b .Lmain_bb14
.Lmain_bb15:
	cmp w10, w19
	b.ge .Lmain_bb17
.Lmain_bb16:
	ldr w9, [x12], #4
	add w11, w11, w9
	add w10, w10, #1
	b .Lmain_bb15
.Lmain_bb17:
	add w14, w14, #1
	mov w21, w11
	b .Lmain_bb9
.Lmain_bb19:
	movz w0, #84
	bl _sysy_stoptime
	mov w0, w21
	bl putint
	movz w0, #10
	bl putch
	ldp x27, x28, [sp, #64]
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	movz w0, #0
	add sp, sp, #80
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb24:
	add w21, w21, #1
	b .Lmain_bb1
.Lmain_bb25:
	mov w21, w20
	b .Lmain_bb2
.Lmain_bb26:
	mov w21, w20
	mov w14, w20
	b .Lmain_bb9
.Lmain_bb27:
	mov x12, x17
	mov w11, w13
	mov w10, w16
	b .Lmain_bb15
	.size main, .-main
	.p2align 2
	.global __sysy_par_body_0
	.type __sysy_par_body_0, %function
__sysy_par_body_0:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #48
	stp x19, x20, [sp]
	adrp x9, __sysy_par_ctx_0_1
	stp x21, x22, [sp, #16]
	stp x23, x24, [sp, #32]
	ldr x10, [x9, :lo12:__sysy_par_ctx_0_1]
	adrp x9, __sysy_par_ctx_0_0
	ldr w22, [x9, :lo12:__sysy_par_ctx_0_0]
	mov w21, w0
	sxtw x9, w21
	add x9, x10, x9, lsl #12
	mov w23, w1
	mov x24, x9
	movz w20, #0
	movz x19, #4096
.L__sysy_par_body_0_bb1:
	cmp w21, w23
	b.ge .L__sysy_par_body_0_bb2
.L__sysy_par_body_0_bb3:
	lsl w2, w22, #2
	mov x0, x24
	mov w1, w20
	bl memset
	add w21, w21, #1
	add x24, x24, x19
	b .L__sysy_par_body_0_bb1
.L__sysy_par_body_0_bb2:
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	add sp, sp, #48
	ldp xzr, x30, [sp], #16
	ret
	.size __sysy_par_body_0, .-__sysy_par_body_0
	.p2align 2
	.global __sysy_par_body_1
	.type __sysy_par_body_1, %function
__sysy_par_body_1:
	sub sp, sp, #32
	stp x19, x20, [sp]
	adrp x9, __sysy_par_ctx_1_0
	str x21, [sp, #16]
	ldr w17, [x9, :lo12:__sysy_par_ctx_1_0]
	adrp x11, __sysy_par_ctx_1_1
	adrp x10, __sysy_par_ctx_1_2
	adrp x9, __sysy_par_ctx_1_3
	ldr x16, [x11, :lo12:__sysy_par_ctx_1_1]
	ldr x15, [x10, :lo12:__sysy_par_ctx_1_2]
	ldr x14, [x9, :lo12:__sysy_par_ctx_1_3]
	mov w12, w0
	mov w8, w1
	movz w7, #0
	sub w13, w17, #1
.L__sysy_par_body_1_bb1:
	cmp w12, w8
	b.ge .L__sysy_par_body_1_bb2
.L__sysy_par_body_1_bb3:
	sxtw x9, w12
	add x9, x16, x9, lsl #12
	add x6, x9, w13, sxtw #2
	mov w11, w13
	movn x9, #3
.L__sysy_par_body_1_bb4:
	cmp w11, #0
	b.lt .L__sysy_par_body_1_bb20
.L__sysy_par_body_1_bb5:
	ldr w10, [x6]
	cbz w10, .L__sysy_par_body_1_bb21
.L__sysy_par_body_1_bb19:
	sub w11, w11, #1
	add x6, x6, x9
	b .L__sysy_par_body_1_bb4
.L__sysy_par_body_1_bb2:
	ldp x20, x21, [sp, #8]
	ldr x19, [sp]
	add sp, sp, #32
	ret
.L__sysy_par_body_1_bb6:
	sxtw x9, w12
	add x3, x16, x9, lsl #12
	add x2, x15, x9, lsl #12
.L__sysy_par_body_1_bb7:
	cmp w4, w17
	b.ge .L__sysy_par_body_1_bb8
.L__sysy_par_body_1_bb9:
	add x9, x3, w4, sxtw #2
	ldr w5, [x9]
	cmp w5, #1
	b.eq .L__sysy_par_body_1_bb16
.L__sysy_par_body_1_bb10:
	sxtw x9, w4
	sxtw x10, w12
	add x1, x14, x9, lsl #12
	cmp w17, #15
	add x0, x15, x10, lsl #12
	cset w11, gt
	add x9, x1, w17, sxtw #2
	dup v20.4s, w5
	cmp x9, x0
	add x9, x0, w17, sxtw #2
	cset w10, ls
	cmp x9, x1
	cset w9, ls
	orr w9, w10, w9
	sub w6, w17, #8
	and w9, w11, w9
	cbz w9, .L__sysy_par_body_1_bb23
.L__sysy_par_body_1_bb22:
	mov x9, x1
	mov x10, x0
	mov w11, w7
.L__sysy_par_body_1_bb11:
	cmp w11, w6
	b.gt .L__sysy_par_body_1_bb24
.L__sysy_par_body_1_bb12:
	ldp q19, q18, [x10]
	ldp q17, q16, [x9]
	mla v17.4s, v19.4s, v20.4s
	mla v16.4s, v18.4s, v20.4s
	stp q17, q16, [x10]
	add w11, w11, #8
	add x10, x10, #32
	add x9, x9, #32
	b .L__sysy_par_body_1_bb11
.L__sysy_par_body_1_bb8:
	add w12, w12, #1
	b .L__sysy_par_body_1_bb1
.L__sysy_par_body_1_bb13:
	sxtw x9, w4
	add x9, x14, x9, lsl #12
	add x19, x2, w20, sxtw #2
	add x6, x9, w20, sxtw #2
	sub w0, w17, #3
	orr w1, wzr, #0x80000003
.L__sysy_par_body_1_bb14:
	cmp w20, w0
	cset w10, lt
	cmp w17, w1
	cset w9, ge
	and w9, w9, w10
	cbz w9, .L__sysy_par_body_1_bb25
.L__sysy_par_body_1_bb18:
	ldr w10, [x19]
	ldr w9, [x6]
	madd w9, w10, w5, w9
	str w9, [x19]
	ldr w10, [x19, #4]
	ldr w9, [x6, #4]
	madd w9, w10, w5, w9
	str w9, [x19, #4]
	ldr w10, [x19, #8]
	ldr w9, [x6, #8]
	madd w9, w10, w5, w9
	str w9, [x19, #8]
	ldr w10, [x19, #12]
	ldr w9, [x6, #12]
	madd w9, w10, w5, w9
	add x21, x19, #4
	add x11, x6, #4
	add x10, x21, #4
	add x11, x11, #4
	str w9, [x19, #12]
	add x10, x10, #4
	add x6, x11, #4
	add w20, w20, #4
	add x19, x10, #4
	add x6, x6, #4
	b .L__sysy_par_body_1_bb14
.L__sysy_par_body_1_bb15:
	cmp w11, w17
	b.ge .L__sysy_par_body_1_bb16
.L__sysy_par_body_1_bb17:
	ldr w10, [x19]
	ldr w9, [x6], #4
	madd w9, w10, w5, w9
	str w9, [x19], #4
	add w11, w11, #1
	b .L__sysy_par_body_1_bb15
.L__sysy_par_body_1_bb16:
	add w4, w4, #1
	b .L__sysy_par_body_1_bb7
.L__sysy_par_body_1_bb20:
	mov w4, w7
	b .L__sysy_par_body_1_bb6
.L__sysy_par_body_1_bb21:
	mov w4, w11
	b .L__sysy_par_body_1_bb6
.L__sysy_par_body_1_bb23:
	mov w20, w7
	b .L__sysy_par_body_1_bb13
.L__sysy_par_body_1_bb24:
	mov w20, w11
	b .L__sysy_par_body_1_bb13
.L__sysy_par_body_1_bb25:
	mov w11, w20
	b .L__sysy_par_body_1_bb15
	.size __sysy_par_body_1, .-__sysy_par_body_1
	.p2align 2
	.global __sysy_par_body_2
	.type __sysy_par_body_2, %function
__sysy_par_body_2:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #48
	stp x19, x20, [sp]
	adrp x9, __sysy_par_ctx_2_1
	stp x21, x22, [sp, #16]
	stp x23, x24, [sp, #32]
	ldr x10, [x9, :lo12:__sysy_par_ctx_2_1]
	adrp x9, __sysy_par_ctx_2_0
	ldr w22, [x9, :lo12:__sysy_par_ctx_2_0]
	mov w21, w0
	sxtw x9, w21
	add x9, x10, x9, lsl #12
	mov w23, w1
	mov x24, x9
	movz w20, #0
	movz x19, #4096
.L__sysy_par_body_2_bb1:
	cmp w21, w23
	b.ge .L__sysy_par_body_2_bb2
.L__sysy_par_body_2_bb3:
	lsl w2, w22, #2
	mov x0, x24
	mov w1, w20
	bl memset
	add w21, w21, #1
	add x24, x24, x19
	b .L__sysy_par_body_2_bb1
.L__sysy_par_body_2_bb2:
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	add sp, sp, #48
	ldp xzr, x30, [sp], #16
	ret
	.size __sysy_par_body_2, .-__sysy_par_body_2
	.p2align 2
	.global __sysy_par_body_3
	.type __sysy_par_body_3, %function
__sysy_par_body_3:
	sub sp, sp, #32
	stp x19, x20, [sp]
	adrp x9, __sysy_par_ctx_3_0
	str x21, [sp, #16]
	ldr w17, [x9, :lo12:__sysy_par_ctx_3_0]
	adrp x11, __sysy_par_ctx_3_1
	adrp x10, __sysy_par_ctx_3_2
	adrp x9, __sysy_par_ctx_3_3
	ldr x16, [x11, :lo12:__sysy_par_ctx_3_1]
	ldr x15, [x10, :lo12:__sysy_par_ctx_3_2]
	ldr x14, [x9, :lo12:__sysy_par_ctx_3_3]
	mov w12, w0
	mov w8, w1
	movz w7, #0
	sub w13, w17, #1
.L__sysy_par_body_3_bb1:
	cmp w12, w8
	b.ge .L__sysy_par_body_3_bb2
.L__sysy_par_body_3_bb3:
	sxtw x9, w12
	add x9, x16, x9, lsl #12
	add x6, x9, w13, sxtw #2
	mov w11, w13
	movn x9, #3
.L__sysy_par_body_3_bb4:
	cmp w11, #0
	b.lt .L__sysy_par_body_3_bb20
.L__sysy_par_body_3_bb5:
	ldr w10, [x6]
	cbz w10, .L__sysy_par_body_3_bb21
.L__sysy_par_body_3_bb19:
	sub w11, w11, #1
	add x6, x6, x9
	b .L__sysy_par_body_3_bb4
.L__sysy_par_body_3_bb2:
	ldp x20, x21, [sp, #8]
	ldr x19, [sp]
	add sp, sp, #32
	ret
.L__sysy_par_body_3_bb6:
	sxtw x9, w12
	add x3, x16, x9, lsl #12
	add x2, x15, x9, lsl #12
.L__sysy_par_body_3_bb7:
	cmp w4, w17
	b.ge .L__sysy_par_body_3_bb8
.L__sysy_par_body_3_bb9:
	add x9, x3, w4, sxtw #2
	ldr w5, [x9]
	cmp w5, #1
	b.eq .L__sysy_par_body_3_bb16
.L__sysy_par_body_3_bb10:
	sxtw x9, w4
	sxtw x10, w12
	add x1, x14, x9, lsl #12
	cmp w17, #15
	add x0, x15, x10, lsl #12
	cset w11, gt
	add x9, x1, w17, sxtw #2
	dup v20.4s, w5
	cmp x9, x0
	add x9, x0, w17, sxtw #2
	cset w10, ls
	cmp x9, x1
	cset w9, ls
	orr w9, w10, w9
	sub w6, w17, #8
	and w9, w11, w9
	cbz w9, .L__sysy_par_body_3_bb23
.L__sysy_par_body_3_bb22:
	mov x9, x1
	mov x10, x0
	mov w11, w7
.L__sysy_par_body_3_bb11:
	cmp w11, w6
	b.gt .L__sysy_par_body_3_bb24
.L__sysy_par_body_3_bb12:
	ldp q19, q18, [x10]
	ldp q17, q16, [x9]
	mla v17.4s, v19.4s, v20.4s
	mla v16.4s, v18.4s, v20.4s
	stp q17, q16, [x10]
	add w11, w11, #8
	add x10, x10, #32
	add x9, x9, #32
	b .L__sysy_par_body_3_bb11
.L__sysy_par_body_3_bb8:
	add w12, w12, #1
	b .L__sysy_par_body_3_bb1
.L__sysy_par_body_3_bb13:
	sxtw x9, w4
	add x9, x14, x9, lsl #12
	add x19, x2, w20, sxtw #2
	add x6, x9, w20, sxtw #2
	sub w0, w17, #3
	orr w1, wzr, #0x80000003
.L__sysy_par_body_3_bb14:
	cmp w20, w0
	cset w10, lt
	cmp w17, w1
	cset w9, ge
	and w9, w9, w10
	cbz w9, .L__sysy_par_body_3_bb25
.L__sysy_par_body_3_bb18:
	ldr w10, [x19]
	ldr w9, [x6]
	madd w9, w10, w5, w9
	str w9, [x19]
	ldr w10, [x19, #4]
	ldr w9, [x6, #4]
	madd w9, w10, w5, w9
	str w9, [x19, #4]
	ldr w10, [x19, #8]
	ldr w9, [x6, #8]
	madd w9, w10, w5, w9
	str w9, [x19, #8]
	ldr w10, [x19, #12]
	ldr w9, [x6, #12]
	madd w9, w10, w5, w9
	add x21, x19, #4
	add x11, x6, #4
	add x10, x21, #4
	add x11, x11, #4
	str w9, [x19, #12]
	add x10, x10, #4
	add x6, x11, #4
	add w20, w20, #4
	add x19, x10, #4
	add x6, x6, #4
	b .L__sysy_par_body_3_bb14
.L__sysy_par_body_3_bb15:
	cmp w11, w17
	b.ge .L__sysy_par_body_3_bb16
.L__sysy_par_body_3_bb17:
	ldr w10, [x19]
	ldr w9, [x6], #4
	madd w9, w10, w5, w9
	str w9, [x19], #4
	add w11, w11, #1
	b .L__sysy_par_body_3_bb15
.L__sysy_par_body_3_bb16:
	add w4, w4, #1
	b .L__sysy_par_body_3_bb7
.L__sysy_par_body_3_bb20:
	mov w4, w7
	b .L__sysy_par_body_3_bb6
.L__sysy_par_body_3_bb21:
	mov w4, w11
	b .L__sysy_par_body_3_bb6
.L__sysy_par_body_3_bb23:
	mov w20, w7
	b .L__sysy_par_body_3_bb13
.L__sysy_par_body_3_bb24:
	mov w20, w11
	b .L__sysy_par_body_3_bb13
.L__sysy_par_body_3_bb25:
	mov w11, w20
	b .L__sysy_par_body_3_bb15
	.size __sysy_par_body_3, .-__sysy_par_body_3
	.bss
	.global A
	.p2align 4
A:
	.zero 4194304
	.global B
	.p2align 4
B:
	.zero 4194304
	.global C
	.p2align 4
C:
	.zero 4194304
	.global __sysy_par_ctx_0_0
	.p2align 2
__sysy_par_ctx_0_0:
	.zero 4
	.global __sysy_par_ctx_0_1
	.p2align 3
__sysy_par_ctx_0_1:
	.zero 8
	.global __sysy_par_ctx_1_0
	.p2align 2
__sysy_par_ctx_1_0:
	.zero 4
	.global __sysy_par_ctx_1_1
	.p2align 3
__sysy_par_ctx_1_1:
	.zero 8
	.global __sysy_par_ctx_1_2
	.p2align 3
__sysy_par_ctx_1_2:
	.zero 8
	.global __sysy_par_ctx_1_3
	.p2align 3
__sysy_par_ctx_1_3:
	.zero 8
	.global __sysy_par_ctx_2_0
	.p2align 2
__sysy_par_ctx_2_0:
	.zero 4
	.global __sysy_par_ctx_2_1
	.p2align 3
__sysy_par_ctx_2_1:
	.zero 8
	.global __sysy_par_ctx_3_0
	.p2align 2
__sysy_par_ctx_3_0:
	.zero 4
	.global __sysy_par_ctx_3_1
	.p2align 3
__sysy_par_ctx_3_1:
	.zero 8
	.global __sysy_par_ctx_3_2
	.p2align 3
__sysy_par_ctx_3_2:
	.zero 8
	.global __sysy_par_ctx_3_3
	.p2align 3
__sysy_par_ctx_3_3:
	.zero 8

	.text
	.align 2
	.global __sysy_par_dispatch
__sysy_par_dispatch:
	cmp w0, #0
	b.eq .Lsysy_disp_0
	cmp w0, #1
	b.eq .Lsysy_disp_1
	cmp w0, #2
	b.eq .Lsysy_disp_2
	cmp w0, #3
	b.eq .Lsysy_disp_3
	ret
.Lsysy_disp_0:
	mov w0, w1
	mov w1, w2
	b __sysy_par_body_0
.Lsysy_disp_1:
	mov w0, w1
	mov w1, w2
	b __sysy_par_body_1
.Lsysy_disp_2:
	mov w0, w1
	mov w1, w2
	b __sysy_par_body_2
.Lsysy_disp_3:
	mov w0, w1
	mov w1, w2
	b __sysy_par_body_3

	.arch armv8-a
	.file	"par_runtime_only.c"
	.text
	.align	2
	.p2align 4,,11
	.type	__sysy_bind_cpu.part.0, %function
__sysy_bind_cpu.part.0:
.LFB3:
	.cfi_startproc
	adrp	x5, .LANCHOR0
	mov	x1, 0
	add	x5, x5, :lo12:.LANCHOR0
	mov	w4, 0
	b	.L5
	.p2align 2,,3
.L3:
	add	w4, w4, 1
.L2:
	cmp	x1, 1024
	beq	.L15
.L5:
	lsr	x2, x1, 6
	and	w3, w1, 63
	add	x1, x1, 1
	lsl	x6, x2, 3
	ldr	x2, [x5, x2, lsl 3]
	lsr	x2, x2, x3
	tbz	x2, 0, .L2
	cmp	w0, w4
	bne	.L3
	stp	x29, x30, [sp, -144]!
	.cfi_def_cfa_offset 144
	.cfi_offset 29, -144
	.cfi_offset 30, -136
	mov	x4, 1
	lsl	x4, x4, x3
	movi	v0.4s, 0
	add	x2, sp, 16
	mov	x29, sp
	mov	x1, 128
	mov	w0, 0
	stp	q0, q0, [x2]
	stp	q0, q0, [x2, 32]
	stp	q0, q0, [x2, 64]
	stp	q0, q0, [x2, 96]
	ldr	x3, [x2, x6]
	orr	x3, x3, x4
	str	x3, [x2, x6]
	bl	sched_setaffinity
	ldp	x29, x30, [sp], 144
	.cfi_restore 30
	.cfi_restore 29
	.cfi_def_cfa_offset 0
	ret
	.p2align 2,,3
.L15:
	ret
	.cfi_endproc
.LFE3:
	.size	__sysy_bind_cpu.part.0, .-__sysy_bind_cpu.part.0
	.align	2
	.p2align 4,,11
	.type	__sysy_worker, %function
__sysy_worker:
.LFB1:
	.cfi_startproc
	stp	x29, x30, [sp, -48]!
	.cfi_def_cfa_offset 48
	.cfi_offset 29, -48
	.cfi_offset 30, -40
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -32
	.cfi_offset 20, -24
	adrp	x20, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	.cfi_offset 21, -16
	.cfi_offset 22, -8
	ldr	w0, [x0, 128]
	cbz	w0, .L17
	mov	w0, 3
	bl	__sysy_bind_cpu.part.0
.L17:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w19, 0
	add	x21, x20, 132
	add	x22, x20, 148
	.p2align 3,,7
.L18:
	ldar	w0, [x21]
	cmp	w0, w19
	beq	.L18
.L25:
	ldr	w0, [x20, 136]
	add	w19, w19, 1
	ldr	w1, [x20, 140]
	ldr	w2, [x20, 144]
	bl	__sysy_par_dispatch
	stlr	w19, [x22]
	ldar	w0, [x21]
	cmp	w0, w19
	beq	.L18
	b	.L25
	.cfi_endproc
.LFE1:
	.size	__sysy_worker, .-__sysy_worker
	.align	2
	.p2align 4,,11
	.global	__sysy_parallel_for
	.type	__sysy_parallel_for, %function
__sysy_parallel_for:
.LFB2:
	.cfi_startproc
	stp	x29, x30, [sp, -80]!
	.cfi_def_cfa_offset 80
	.cfi_offset 29, -80
	.cfi_offset 30, -72
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -64
	.cfi_offset 20, -56
	mov	w20, w2
	stp	x21, x22, [sp, 32]
	.cfi_offset 21, -48
	.cfi_offset 22, -40
	mov	w21, w1
	mov	w22, w0
	stp	x23, x24, [sp, 48]
	.cfi_offset 23, -32
	.cfi_offset 24, -24
	sub	w23, w2, w1
	cmp	w23, 1
	ble	.L33
	adrp	x19, .LANCHOR0
	add	x24, x19, :lo12:.LANCHOR0
	ldr	w0, [x24, 152]
	cbz	w0, .L28
	ldr	w0, [x24, 156]
.L29:
	cbz	w0, .L33
	add	x19, x19, :lo12:.LANCHOR0
	add	w2, w21, w23, asr 1
	mov	x0, x19
	str	w22, [x19, 136]
	str	w2, [x19, 140]
	str	w20, [x19, 144]
	ldr	w20, [x0, 132]!
	add	w20, w20, 1
	stlr	w20, [x0]
	mov	w0, w22
	mov	w1, w21
	bl	__sysy_par_dispatch
	add	x0, x19, 148
	.p2align 3,,7
.L34:
	ldar	w1, [x0]
	cmp	w1, w20
	bne	.L34
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 80
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa_offset 0
	ret
	.p2align 2,,3
.L33:
	.cfi_restore_state
	mov	w2, w20
	mov	w1, w21
	mov	w0, w22
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 80
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa_offset 0
	b	__sysy_par_dispatch
	.p2align 2,,3
.L28:
	.cfi_restore_state
	mov	x2, x24
	mov	x1, 128
	mov	w0, 0
	str	x25, [sp, 64]
	.cfi_offset 25, -16
	mov	w25, 1
	str	w25, [x24, 152]
	bl	sched_getaffinity
	cbnz	w0, .L30
	str	w25, [x24, 128]
.L31:
	mov	w0, 2
	bl	__sysy_bind_cpu.part.0
	b	.L32
	.p2align 2,,3
.L30:
	ldr	w0, [x24, 128]
	cbnz	w0, .L31
.L32:
	mov	w2, 3840
	adrp	x0, __sysy_worker
	movk	w2, 0x5, lsl 16
	add	x0, x0, :lo12:__sysy_worker
	adrp	x1, __sysy_wstack+1048576
	mov	x3, 0
	add	x1, x1, :lo12:__sysy_wstack+1048576
	bl	clone
	add	x1, x19, :lo12:.LANCHOR0
	cmp	w0, 0
	cset	w0, gt
	ldr	x25, [sp, 64]
	.cfi_restore 25
	str	w0, [x1, 156]
	b	.L29
	.cfi_endproc
.LFE2:
	.size	__sysy_parallel_for, .-__sysy_parallel_for
	.bss
	.align	4
	.set	.LANCHOR0,. + 0
	.type	__sysy_orig_mask, %object
	.size	__sysy_orig_mask, 128
__sysy_orig_mask:
	.zero	128
	.type	__sysy_orig_mask_valid, %object
	.size	__sysy_orig_mask_valid, 4
__sysy_orig_mask_valid:
	.zero	4
	.type	__sysy_job_seq, %object
	.size	__sysy_job_seq, 4
__sysy_job_seq:
	.zero	4
	.type	__sysy_job_id, %object
	.size	__sysy_job_id, 4
__sysy_job_id:
	.zero	4
	.type	__sysy_job_lo, %object
	.size	__sysy_job_lo, 4
__sysy_job_lo:
	.zero	4
	.type	__sysy_job_hi, %object
	.size	__sysy_job_hi, 4
__sysy_job_hi:
	.zero	4
	.type	__sysy_done_seq, %object
	.size	__sysy_done_seq, 4
__sysy_done_seq:
	.zero	4
	.type	__sysy_worker_started, %object
	.size	__sysy_worker_started, 4
__sysy_worker_started:
	.zero	4
	.type	__sysy_worker_ok, %object
	.size	__sysy_worker_ok, 4
__sysy_worker_ok:
	.zero	4
	.type	__sysy_wstack, %object
	.size	__sysy_wstack, 1048576
__sysy_wstack:
	.zero	1048576
	.ident	"GCC: (Ubuntu 11.4.0-1ubuntu1~22.04.3) 11.4.0"
	.section	.note.GNU-stack,"",@progbits
