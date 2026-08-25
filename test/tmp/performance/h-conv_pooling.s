	.arch armv8-a
	.text
	.p2align 2
	.global kernel_conv_pooling
	.type kernel_conv_pooling, %function
kernel_conv_pooling:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #96
	stp x19, x20, [sp]
	stp x21, x22, [sp, #16]
	mov w22, w5
	stp x23, x24, [sp, #32]
	movz w20, #0
	stp x25, x26, [sp, #48]
	mov x23, x0
	stp x27, x28, [sp, #64]
	mov x24, x1
	str x2, [sp, #80]
	mov x25, x3
	mov w21, w6
	sub w19, w4, w22
	mov w26, w20
.Lkernel_conv_pooling_bb1:
	cmp w19, w26
	b.lt .Lkernel_conv_pooling_bb2
.Lkernel_conv_pooling_bb11:
	cmp w19, #0
	b.lt .Lkernel_conv_pooling_bb13
.Lkernel_conv_pooling_bb12:
	adrp x13, __sysy_par_ctx_0_0
	adrp x12, __sysy_par_ctx_0_1
	adrp x11, __sysy_par_ctx_0_2
	adrp x10, __sysy_par_ctx_0_3
	adrp x9, __sysy_par_ctx_0_4
	str w22, [x13, :lo12:__sysy_par_ctx_0_0]
	mov w0, w20
	str w26, [x12, :lo12:__sysy_par_ctx_0_1]
	mov w1, w20
	str x23, [x11, :lo12:__sysy_par_ctx_0_2]
	mov w2, w19
	str x25, [x10, :lo12:__sysy_par_ctx_0_3]
	str x24, [x9, :lo12:__sysy_par_ctx_0_4]
	bl __sysy_parallel_for
.Lkernel_conv_pooling_bb13:
	add w26, w26, #1
	b .Lkernel_conv_pooling_bb1
.Lkernel_conv_pooling_bb2:
	add w9, w19, #1
	sub w27, w9, w22
	mov w28, w20
	movz w26, #1
	movz w19, #0
.Lkernel_conv_pooling_bb3:
	cmp w27, w28
	b.lt .Lkernel_conv_pooling_bb7
.Lkernel_conv_pooling_bb4:
	cmp w27, #0
	b.lt .Lkernel_conv_pooling_bb6
.Lkernel_conv_pooling_bb5:
	adrp x13, __sysy_par_ctx_1_0
	adrp x12, __sysy_par_ctx_1_1
	adrp x11, __sysy_par_ctx_1_2
	adrp x10, __sysy_par_ctx_1_3
	adrp x9, __sysy_par_ctx_1_4
	str w22, [x13, :lo12:__sysy_par_ctx_1_0]
	mov w0, w26
	str w28, [x12, :lo12:__sysy_par_ctx_1_1]
	mov w1, w19
	str x24, [x11, :lo12:__sysy_par_ctx_1_2]
	mov w2, w27
	str x25, [x10, :lo12:__sysy_par_ctx_1_3]
	str x23, [x9, :lo12:__sysy_par_ctx_1_4]
	bl __sysy_parallel_for
.Lkernel_conv_pooling_bb6:
	add w28, w28, #1
	b .Lkernel_conv_pooling_bb3
.Lkernel_conv_pooling_bb7:
	add w9, w27, #1
	sdiv w22, w9, w21
	mov w15, w20
	movz w20, #2
	movz w19, #0
.Lkernel_conv_pooling_bb8:
	cmp w15, w22
	b.ge .Lkernel_conv_pooling_bb9
.Lkernel_conv_pooling_bb10:
	mul w14, w15, w21
	adrp x13, __sysy_par_ctx_2_0
	adrp x12, __sysy_par_ctx_2_1
	adrp x9, __sysy_par_ctx_2_2
	str w21, [x13, :lo12:__sysy_par_ctx_2_0]
	str x23, [x12, :lo12:__sysy_par_ctx_2_1]
	str w14, [x9, :lo12:__sysy_par_ctx_2_2]
	ldr x9, [sp, #80]
	adrp x11, __sysy_par_ctx_2_3
	adrp x10, __sysy_par_ctx_2_4
	str x9, [x11, :lo12:__sysy_par_ctx_2_3]
	add w24, w15, #1
	str w15, [x10, :lo12:__sysy_par_ctx_2_4]
	mov w0, w20
	mov w1, w19
	mov w2, w22
	bl __sysy_parallel_for
	mov w15, w24
	b .Lkernel_conv_pooling_bb8
.Lkernel_conv_pooling_bb9:
	ldp x27, x28, [sp, #64]
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	add sp, sp, #96
	ldp xzr, x30, [sp], #16
	ret
	.size kernel_conv_pooling, .-kernel_conv_pooling
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #64
	stp x19, x20, [sp]
	adrp x9, ks
	stp x21, x22, [sp, #16]
	stp x23, x24, [sp, #32]
	str x25, [sp, #48]
	ldr w22, [x9, :lo12:ks]
	adrp x9, n
	ldr w23, [x9, :lo12:n]
	adrp x9, ps
	ldr w21, [x9, :lo12:ps]
	lsl w9, w22, #1
	sub w9, w23, w9
	add w9, w9, #2
	sdiv w20, w9, w21
	adrp x9, input
	add x19, x9, :lo12:input
	mov x0, x19
	bl getfarray
	adrp x9, kernel
	add x24, x9, :lo12:kernel
	mov x0, x24
	bl getfarray
	movz w0, #107
	bl _sysy_starttime
	adrp x10, conv_output
	adrp x9, pooling_output
	add x10, x10, :lo12:conv_output
	add x25, x9, :lo12:pooling_output
	mov x0, x19
	mov x1, x10
	mov x2, x25
	mov x3, x24
	mov w4, w23
	mov w5, w22
	mov w6, w21
	bl kernel_conv_pooling
	movz w0, #109
	bl _sysy_stoptime
	mul w0, w20, w20
	mov x1, x25
	bl putfarray
	adrp x11, n
	adrp x10, ks
	adrp x9, ps
	str w23, [x11, :lo12:n]
	str w22, [x10, :lo12:ks]
	str w21, [x9, :lo12:ps]
	ldp x24, x25, [sp, #40]
	ldp x22, x23, [sp, #24]
	ldp x20, x21, [sp, #8]
	ldr x19, [sp]
	movz w0, #0
	add sp, sp, #64
	ldp xzr, x30, [sp], #16
	ret
	.size main, .-main
	.p2align 2
	.global __sysy_par_body_0
	.type __sysy_par_body_0, %function
__sysy_par_body_0:
	adrp x10, __sysy_par_ctx_0_1
	ldr w16, [x10, :lo12:__sysy_par_ctx_0_1]
	adrp x9, __sysy_par_ctx_0_4
	ldr x10, [x9, :lo12:__sysy_par_ctx_0_4]
	adrp x9, __sysy_par_ctx_0_3
	ldr x7, [x9, :lo12:__sysy_par_ctx_0_3]
	movz w9, #8000
	smaddl x14, w16, w9, x10
	adrp x12, __sysy_par_ctx_0_0
	adrp x11, __sysy_par_ctx_0_2
	ldr w8, [x12, :lo12:__sysy_par_ctx_0_0]
	ldr x15, [x11, :lo12:__sysy_par_ctx_0_2]
	movz w10, #0
	fmov s24, w10
	mov w13, w0
	mov w17, w1
.L__sysy_par_body_0_bb1:
	mov x6, x7
	fmov s18, s24
	mov w5, w10
.L__sysy_par_body_0_bb2:
	cmp w5, w8
	b.ge .L__sysy_par_body_0_bb9
.L__sysy_par_body_0_bb3:
	add w11, w16, w5
	movz w9, #8000
	smaddl x9, w11, w9, x15
	add x3, x9, w13, sxtw #2
	sub w1, w8, #3
	mov x2, x6
	fmov s23, s18
	mov w12, w10
	orr w4, wzr, #0x80000003
.L__sysy_par_body_0_bb4:
	cmp w12, w1
	cset w11, lt
	cmp w8, w4
	cset w9, ge
	and w9, w9, w11
	cbz w9, .L__sysy_par_body_0_bb12
.L__sysy_par_body_0_bb8:
	ldp s17, s21, [x3]
	ldp s16, s20, [x2]
	fmul s22, s17, s16
	fadd s22, s23, s22
	ldp s19, s17, [x3, #8]
	ldp s18, s16, [x2, #8]
	fmul s23, s21, s20
	fadd s20, s22, s23
	fmul s18, s19, s18
	fadd s18, s20, s18
	fmul s19, s17, s16
	add x11, x3, #4
	fadd s23, s18, s19
	add x9, x2, #4
	add x11, x11, #4
	add x9, x9, #4
	add x11, x11, #4
	add x9, x9, #4
	add w12, w12, #4
	add x3, x11, #4
	add x2, x9, #4
	b .L__sysy_par_body_0_bb4
.L__sysy_par_body_0_bb5:
	cmp w12, w8
	b.ge .L__sysy_par_body_0_bb7
.L__sysy_par_body_0_bb6:
	ldr s17, [x11], #4
	ldr s16, [x9], #4
	fmul s16, s17, s16
	fadd s18, s18, s16
	add w12, w12, #1
	b .L__sysy_par_body_0_bb5
.L__sysy_par_body_0_bb7:
	add w5, w5, #1
	add x6, x6, #60
	b .L__sysy_par_body_0_bb2
.L__sysy_par_body_0_bb9:
	str s18, [x14, w13, sxtw #2]
	add w9, w13, #1
	cmp w13, w17
	b.ge .L__sysy_par_body_0_bb10
.L__sysy_par_body_0_bb11:
	mov w13, w9
	b .L__sysy_par_body_0_bb1
.L__sysy_par_body_0_bb10:
	ret
.L__sysy_par_body_0_bb12:
	mov x9, x2
	mov x11, x3
	fmov s18, s23
	b .L__sysy_par_body_0_bb5
	.size __sysy_par_body_0, .-__sysy_par_body_0
	.p2align 2
	.global __sysy_par_body_1
	.type __sysy_par_body_1, %function
__sysy_par_body_1:
	adrp x10, __sysy_par_ctx_1_1
	ldr w16, [x10, :lo12:__sysy_par_ctx_1_1]
	adrp x9, __sysy_par_ctx_1_4
	ldr x10, [x9, :lo12:__sysy_par_ctx_1_4]
	adrp x9, __sysy_par_ctx_1_3
	ldr x7, [x9, :lo12:__sysy_par_ctx_1_3]
	movz w9, #8000
	smaddl x14, w16, w9, x10
	adrp x12, __sysy_par_ctx_1_0
	adrp x11, __sysy_par_ctx_1_2
	ldr w8, [x12, :lo12:__sysy_par_ctx_1_0]
	ldr x15, [x11, :lo12:__sysy_par_ctx_1_2]
	movz w10, #0
	fmov s24, w10
	mov w13, w0
	mov w17, w1
.L__sysy_par_body_1_bb1:
	mov x6, x7
	fmov s18, s24
	mov w5, w10
.L__sysy_par_body_1_bb2:
	cmp w5, w8
	b.ge .L__sysy_par_body_1_bb9
.L__sysy_par_body_1_bb3:
	add w11, w16, w5
	movz w9, #8000
	smaddl x9, w11, w9, x15
	add x3, x9, w13, sxtw #2
	sub w1, w8, #3
	mov x2, x6
	fmov s23, s18
	mov w12, w10
	orr w4, wzr, #0x80000003
.L__sysy_par_body_1_bb4:
	cmp w12, w1
	cset w11, lt
	cmp w8, w4
	cset w9, ge
	and w9, w9, w11
	cbz w9, .L__sysy_par_body_1_bb12
.L__sysy_par_body_1_bb8:
	ldp s17, s21, [x3]
	ldp s16, s20, [x2]
	fmul s22, s17, s16
	fadd s22, s23, s22
	ldp s19, s17, [x3, #8]
	ldp s18, s16, [x2, #8]
	fmul s23, s21, s20
	fadd s20, s22, s23
	fmul s18, s19, s18
	fadd s18, s20, s18
	fmul s19, s17, s16
	add x11, x3, #4
	fadd s23, s18, s19
	add x9, x2, #4
	add x11, x11, #4
	add x9, x9, #4
	add x11, x11, #4
	add x9, x9, #4
	add w12, w12, #4
	add x3, x11, #4
	add x2, x9, #4
	b .L__sysy_par_body_1_bb4
.L__sysy_par_body_1_bb5:
	cmp w12, w8
	b.ge .L__sysy_par_body_1_bb7
.L__sysy_par_body_1_bb6:
	ldr s17, [x11], #4
	ldr s16, [x9], #4
	fmul s16, s17, s16
	fadd s18, s18, s16
	add w12, w12, #1
	b .L__sysy_par_body_1_bb5
.L__sysy_par_body_1_bb7:
	add w5, w5, #1
	add x6, x6, #60
	b .L__sysy_par_body_1_bb2
.L__sysy_par_body_1_bb9:
	str s18, [x14, w13, sxtw #2]
	add w9, w13, #1
	cmp w13, w17
	b.ge .L__sysy_par_body_1_bb10
.L__sysy_par_body_1_bb11:
	mov w13, w9
	b .L__sysy_par_body_1_bb1
.L__sysy_par_body_1_bb10:
	ret
.L__sysy_par_body_1_bb12:
	mov x9, x2
	mov x11, x3
	fmov s18, s23
	b .L__sysy_par_body_1_bb5
	.size __sysy_par_body_1, .-__sysy_par_body_1
	.p2align 2
	.global __sysy_par_body_2
	.type __sysy_par_body_2, %function
__sysy_par_body_2:
	sub sp, sp, #16
	stp x19, x20, [sp]
	adrp x11, __sysy_par_ctx_2_2
	ldr w16, [x11, :lo12:__sysy_par_ctx_2_2]
	adrp x9, __sysy_par_ctx_2_4
	ldr w11, [x9, :lo12:__sysy_par_ctx_2_4]
	adrp x12, __sysy_par_ctx_2_1
	ldr x17, [x12, :lo12:__sysy_par_ctx_2_1]
	adrp x10, __sysy_par_ctx_2_3
	adrp x9, __sysy_par_ctx_2_0
	ldr x12, [x10, :lo12:__sysy_par_ctx_2_3]
	ldr w7, [x9, :lo12:__sysy_par_ctx_2_0]
	movz w10, #8000
	movz w9, #1972
	smaddl x6, w16, w10, x17
	smaddl x4, w11, w9, x12
	mov w15, w0
	mov w8, w1
	movz w5, #0
	movz w12, #16256, lsl #16
	movz w11, #16384, lsl #16
	movz w10, #16576, lsl #16
	movz w9, #16832, lsl #16
.L__sysy_par_body_2_bb1:
	cmp w15, w8
	b.ge .L__sysy_par_body_2_bb2
.L__sysy_par_body_2_bb3:
	mul w2, w15, w7
	ldr s17, [x6, w2, sxtw #2]
	mov w1, w5
.L__sysy_par_body_2_bb4:
	cmp w1, w7
	b.ge .L__sysy_par_body_2_bb11
.L__sysy_par_body_2_bb5:
	add w14, w16, w1
	movz w13, #8000
	smaddl x13, w14, w13, x17
	add x19, x13, w2, sxtw #2
	sub w20, w7, #3
	mov w0, w5
	orr w3, wzr, #0x80000003
.L__sysy_par_body_2_bb6:
	cmp w0, w20
	cset w14, lt
	cmp w7, w3
	cset w13, ge
	and w13, w13, w14
	cbz w13, .L__sysy_par_body_2_bb12
.L__sysy_par_body_2_bb10:
	ldp s16, s18, [x19]
	fcmp s17, s16
	fcsel s19, s17, s16, hi
	ldp s17, s16, [x19, #8]
	fcmp s19, s18
	fcsel s18, s19, s18, hi
	fcmp s18, s17
	fcsel s17, s18, s17, hi
	fcmp s17, s16
	add x13, x19, #4
	add x13, x13, #4
	fcsel s17, s17, s16, hi
	add x13, x13, #4
	add w0, w0, #4
	add x19, x13, #4
	b .L__sysy_par_body_2_bb6
.L__sysy_par_body_2_bb2:
	ldp x19, x20, [sp]
	add sp, sp, #16
	ret
.L__sysy_par_body_2_bb7:
	cmp w14, w7
	b.ge .L__sysy_par_body_2_bb9
.L__sysy_par_body_2_bb8:
	ldr s16, [x13], #4
	fcmp s17, s16
	fcsel s17, s17, s16, hi
	add w14, w14, #1
	b .L__sysy_par_body_2_bb7
.L__sysy_par_body_2_bb9:
	add w1, w1, #1
	b .L__sysy_par_body_2_bb4
.L__sysy_par_body_2_bb11:
	add x14, x4, w15, sxtw #2
	str s17, [x14]
	ldr s21, [x14]
	fmov s16, w5
	fsub s20, s16, s21
	fmul s17, s20, s20
	fmov s16, w11
	fdiv s19, s17, s16
	fmul s22, s17, s20
	fmov s16, w10
	fdiv s18, s22, s16
	fmul s17, s22, s20
	fmov s16, w9
	fdiv s16, s17, s16
	fmov s17, w12
	fadd s17, s20, s17
	fadd s17, s17, s19
	fadd s17, s17, s18
	fadd s16, s17, s16
	fmov s17, w12
	fadd s16, s16, s17
	fdiv s16, s17, s16
	fmul s16, s21, s16
	add w15, w15, #1
	str s16, [x14]
	b .L__sysy_par_body_2_bb1
.L__sysy_par_body_2_bb12:
	mov x13, x19
	mov w14, w0
	b .L__sysy_par_body_2_bb7
	.size __sysy_par_body_2, .-__sysy_par_body_2
	.data
	.global n
	.p2align 2
n:
	.word 2000
	.global ks
	.p2align 2
ks:
	.word 15
	.global ps
	.p2align 2
ps:
	.word 4
	.bss
	.global input
	.p2align 4
input:
	.zero 16000000
	.global kernel
	.p2align 4
kernel:
	.zero 900
	.global conv_output
	.p2align 4
conv_output:
	.zero 16000000
	.global pooling_output
	.p2align 4
pooling_output:
	.zero 972196
	.global __sysy_par_ctx_0_0
	.p2align 2
__sysy_par_ctx_0_0:
	.zero 4
	.global __sysy_par_ctx_0_1
	.p2align 2
__sysy_par_ctx_0_1:
	.zero 4
	.global __sysy_par_ctx_0_2
	.p2align 3
__sysy_par_ctx_0_2:
	.zero 8
	.global __sysy_par_ctx_0_3
	.p2align 3
__sysy_par_ctx_0_3:
	.zero 8
	.global __sysy_par_ctx_0_4
	.p2align 3
__sysy_par_ctx_0_4:
	.zero 8
	.global __sysy_par_ctx_1_0
	.p2align 2
__sysy_par_ctx_1_0:
	.zero 4
	.global __sysy_par_ctx_1_1
	.p2align 2
__sysy_par_ctx_1_1:
	.zero 4
	.global __sysy_par_ctx_1_2
	.p2align 3
__sysy_par_ctx_1_2:
	.zero 8
	.global __sysy_par_ctx_1_3
	.p2align 3
__sysy_par_ctx_1_3:
	.zero 8
	.global __sysy_par_ctx_1_4
	.p2align 3
__sysy_par_ctx_1_4:
	.zero 8
	.global __sysy_par_ctx_2_0
	.p2align 2
__sysy_par_ctx_2_0:
	.zero 4
	.global __sysy_par_ctx_2_1
	.p2align 3
__sysy_par_ctx_2_1:
	.zero 8
	.global __sysy_par_ctx_2_2
	.p2align 2
__sysy_par_ctx_2_2:
	.zero 4
	.global __sysy_par_ctx_2_3
	.p2align 3
__sysy_par_ctx_2_3:
	.zero 8
	.global __sysy_par_ctx_2_4
	.p2align 2
__sysy_par_ctx_2_4:
	.zero 4

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
