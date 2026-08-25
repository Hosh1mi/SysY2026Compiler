	.arch armv8-a
	.text
	.p2align 2
	.global kernel_correlation
	.type kernel_correlation, %function
kernel_correlation:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #96
	stp x21, x22, [sp, #16]
	stp x23, x24, [sp, #32]
	stp x25, x26, [sp, #48]
	stp x27, x28, [sp, #64]
	mov w28, w0
	stp x19, x20, [sp]
	mov w27, w1
	mov x26, x2
	mov x24, x4
	adrp x11, __sysy_par_ctx_0_0
	adrp x10, __sysy_par_ctx_0_1
	adrp x9, __sysy_par_ctx_0_2
	movz w22, #0
	str x24, [x11, :lo12:__sysy_par_ctx_0_0]
	mov x25, x3
	str w27, [x10, :lo12:__sysy_par_ctx_0_1]
	mov x23, x5
	str x26, [x9, :lo12:__sysy_par_ctx_0_2]
	mov w0, w22
	mov w1, w22
	mov w2, w28
	bl __sysy_parallel_for
	adrp x12, __sysy_par_ctx_1_0
	adrp x11, __sysy_par_ctx_1_1
	adrp x10, __sysy_par_ctx_1_2
	adrp x9, __sysy_par_ctx_1_3
	str x23, [x12, :lo12:__sysy_par_ctx_1_0]
	movz w0, #1
	str x24, [x11, :lo12:__sysy_par_ctx_1_1]
	movz w1, #0
	str w27, [x10, :lo12:__sysy_par_ctx_1_2]
	mov w2, w28
	str x26, [x9, :lo12:__sysy_par_ctx_1_3]
	bl __sysy_parallel_for
	mul w9, w27, w27
	str w9, [sp, #80]
	mov w21, w22
	movz w20, #2
	movz w19, #0
.Lkernel_correlation_bb1:
	cmp w21, w27
	b.ge .Lkernel_correlation_bb2
.Lkernel_correlation_bb14:
	adrp x13, __sysy_par_ctx_2_0
	adrp x12, __sysy_par_ctx_2_1
	adrp x11, __sysy_par_ctx_2_2
	adrp x9, __sysy_par_ctx_2_3
	str x26, [x13, :lo12:__sysy_par_ctx_2_0]
	str w21, [x12, :lo12:__sysy_par_ctx_2_1]
	str x24, [x11, :lo12:__sysy_par_ctx_2_2]
	str x23, [x9, :lo12:__sysy_par_ctx_2_3]
	ldr w9, [sp, #80]
	adrp x10, __sysy_par_ctx_2_4
	str w9, [x10, :lo12:__sysy_par_ctx_2_4]
	mov w0, w20
	mov w1, w19
	mov w2, w28
	bl __sysy_parallel_for
	add w21, w21, #1
	b .Lkernel_correlation_bb1
.Lkernel_correlation_bb2:
	mov x23, x26
	sub w17, w28, #1
	mov w21, w22
	movz w11, #3200
	movz w20, #1
.Lkernel_correlation_bb3:
	cmp w21, w17
	b.ge .Lkernel_correlation_bb13
.Lkernel_correlation_bb4:
	smaddl x9, w21, w11, x25
	add w19, w21, #1
	str w20, [x9, w21, sxtw #2]
	cmp w19, w28
	b.ge .Lkernel_correlation_bb12
.Lkernel_correlation_bb5:
	movz w10, #3200
	smaddl x8, w21, w10, x25
	mov w24, w19
	movz w16, #0
	b .Lkernel_correlation_bb6
.Lkernel_correlation_bb10:
	ldr w13, [x6]
	smaddl x9, w24, w10, x25
	add w24, w24, #1
	str w13, [x9, w21, sxtw #2]
	cmp w24, w28
	b.ge .Lkernel_correlation_bb12
.Lkernel_correlation_bb6:
	add x6, x8, w24, sxtw #2
	str w16, [x6]
	ldr w14, [x6]
	add x7, x26, w24, sxtw #2
	mov x15, x23
	mov w13, w22
.Lkernel_correlation_bb7:
	cmp w13, w27
	b.ge .Lkernel_correlation_bb8
.Lkernel_correlation_bb11:
	ldr w12, [x15]
	ldr w9, [x7]
	madd w14, w12, w9, w14
	add w13, w13, #1
	add x15, x15, #3200
	add x7, x7, #3200
	b .Lkernel_correlation_bb7
.Lkernel_correlation_bb8:
	ldr w9, [x6]
	cmp w14, w9
	b.eq .Lkernel_correlation_bb10
.Lkernel_correlation_bb9:
	str w14, [x6]
	b .Lkernel_correlation_bb10
.Lkernel_correlation_bb12:
	add x23, x23, #4
	mov w21, w19
	b .Lkernel_correlation_bb3
.Lkernel_correlation_bb13:
	movz w9, #3200
	smaddl x9, w17, w9, x25
	movz w10, #1
	str w10, [x9, w17, sxtw #2]
	ldp x27, x28, [sp, #64]
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	add sp, sp, #96
	ldp xzr, x30, [sp], #16
	ret
	.size kernel_correlation, .-kernel_correlation
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #32
	stp x19, x20, [sp]
	adrp x9, n
	stp x21, x22, [sp, #16]
	ldr w19, [x9, :lo12:n]
	adrp x10, m
	ldr w20, [x10, :lo12:m]
	adrp x9, data
	add x21, x9, :lo12:data
	mov x0, x21
	bl getarray
	movz w0, #79
	bl _sysy_starttime
	adrp x11, corr
	adrp x10, mean
	adrp x9, stddev
	add x22, x11, :lo12:corr
	add x10, x10, :lo12:mean
	add x9, x9, :lo12:stddev
	mov w0, w20
	mov w1, w19
	mov x2, x21
	mov x3, x22
	mov x4, x10
	mov x5, x9
	bl kernel_correlation
	movz w0, #81
	bl _sysy_stoptime
	mul w0, w20, w20
	mov x1, x22
	bl putarray
	adrp x10, m
	adrp x9, n
	str w20, [x10, :lo12:m]
	str w19, [x9, :lo12:n]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	movz w0, #0
	add sp, sp, #32
	ldp xzr, x30, [sp], #16
	ret
	.size main, .-main
	.p2align 2
	.global __sysy_par_body_0
	.type __sysy_par_body_0, %function
__sysy_par_body_0:
	adrp x11, __sysy_par_ctx_0_0
	adrp x10, __sysy_par_ctx_0_1
	adrp x9, __sysy_par_ctx_0_2
	ldr x16, [x11, :lo12:__sysy_par_ctx_0_0]
	ldr w15, [x10, :lo12:__sysy_par_ctx_0_1]
	ldr x7, [x9, :lo12:__sysy_par_ctx_0_2]
	mov w14, w0
	mov w17, w1
	movz w8, #0
.L__sysy_par_body_0_bb1:
	cmp w14, w17
	b.ge .L__sysy_par_body_0_bb2
.L__sysy_par_body_0_bb3:
	add x13, x16, w14, sxtw #2
	movz w10, #0
	str w10, [x13]
	add x11, x7, w14, sxtw #2
	sub w4, w15, #3
	mov w5, w8
	orr w6, wzr, #0x80000003
.L__sysy_par_body_0_bb4:
	cmp w5, w4
	cset w10, lt
	cmp w15, w6
	cset w9, ge
	and w9, w9, w10
	cbz w9, .L__sysy_par_body_0_bb9
.L__sysy_par_body_0_bb8:
	ldr w10, [x13]
	ldr w9, [x11]
	add w10, w10, w9
	str w10, [x13]
	ldr w9, [x11, #3200]
	add w10, w10, w9
	str w10, [x13]
	ldr w9, [x11, #6400]
	add w10, w10, w9
	str w10, [x13]
	ldr w9, [x11, #9600]
	add x11, x11, #3200
	add x11, x11, #3200
	add x12, x11, #3200
	add w11, w10, w9
	str w11, [x13]
	add x9, x12, #3200
	add w5, w5, #4
	mov x11, x9
	b .L__sysy_par_body_0_bb4
.L__sysy_par_body_0_bb2:
	ret
.L__sysy_par_body_0_bb5:
	cmp w12, w15
	b.ge .L__sysy_par_body_0_bb7
.L__sysy_par_body_0_bb6:
	ldr w10, [x13]
	ldr w9, [x6]
	add w11, w10, w9
	str w11, [x13]
	add w12, w12, #1
	add x6, x6, #3200
	b .L__sysy_par_body_0_bb5
.L__sysy_par_body_0_bb7:
	ldr w9, [x13]
	sdiv w10, w9, w15
	add w14, w14, #1
	str w10, [x13]
	b .L__sysy_par_body_0_bb1
.L__sysy_par_body_0_bb9:
	mov x6, x11
	mov w12, w5
	b .L__sysy_par_body_0_bb5
	.size __sysy_par_body_0, .-__sysy_par_body_0
	.p2align 2
	.global __sysy_par_body_1
	.type __sysy_par_body_1, %function
__sysy_par_body_1:
	adrp x12, __sysy_par_ctx_1_0
	ldr x5, [x12, :lo12:__sysy_par_ctx_1_0]
	adrp x11, __sysy_par_ctx_1_1
	adrp x10, __sysy_par_ctx_1_2
	adrp x9, __sysy_par_ctx_1_3
	ldr x7, [x11, :lo12:__sysy_par_ctx_1_1]
	ldr w8, [x10, :lo12:__sysy_par_ctx_1_2]
	ldr x17, [x9, :lo12:__sysy_par_ctx_1_3]
	mov w16, w0
	mov w6, w1
	movz w4, #0
	movz w12, #1
.L__sysy_par_body_1_bb1:
	cmp w16, w6
	b.ge .L__sysy_par_body_1_bb2
.L__sysy_par_body_1_bb3:
	add x15, x5, w16, sxtw #2
	movz w9, #0
	str w9, [x15]
	ldr w11, [x15]
	add x14, x17, w16, sxtw #2
	mov w13, w4
.L__sysy_par_body_1_bb4:
	cmp w13, w8
	b.ge .L__sysy_par_body_1_bb5
.L__sysy_par_body_1_bb10:
	ldr w10, [x14]
	ldr w9, [x7, w16, sxtw #2]
	sub w9, w10, w9
	madd w11, w9, w9, w11
	add w13, w13, #1
	add x14, x14, #3200
	b .L__sysy_par_body_1_bb4
.L__sysy_par_body_1_bb2:
	ret
.L__sysy_par_body_1_bb5:
	ldr w9, [x15]
	cmp w11, w9
	b.eq .L__sysy_par_body_1_bb7
.L__sysy_par_body_1_bb6:
	str w11, [x15]
.L__sysy_par_body_1_bb7:
	ldr w9, [x15]
	sdiv w9, w9, w8
	mul w9, w9, w9
	str w9, [x15]
	cmp w9, #1
	b.gt .L__sysy_par_body_1_bb9
.L__sysy_par_body_1_bb8:
	str w12, [x15]
.L__sysy_par_body_1_bb9:
	add w16, w16, #1
	b .L__sysy_par_body_1_bb1
	.size __sysy_par_body_1, .-__sysy_par_body_1
	.p2align 2
	.global __sysy_par_body_2
	.type __sysy_par_body_2, %function
__sysy_par_body_2:
	adrp x9, __sysy_par_ctx_2_1
	ldr w11, [x9, :lo12:__sysy_par_ctx_2_1]
	adrp x10, __sysy_par_ctx_2_0
	ldr x14, [x10, :lo12:__sysy_par_ctx_2_0]
	adrp x9, __sysy_par_ctx_2_3
	ldr x12, [x9, :lo12:__sysy_par_ctx_2_3]
	adrp x10, __sysy_par_ctx_2_2
	ldr x13, [x10, :lo12:__sysy_par_ctx_2_2]
	movz w9, #3200
	smaddl x9, w11, w9, x14
	adrp x10, __sysy_par_ctx_2_4
	ldr w15, [x10, :lo12:__sysy_par_ctx_2_4]
	add x14, x12, w0, sxtw #2
	mov w16, w1
	add x11, x9, w0, sxtw #2
	add x13, x13, w0, sxtw #2
	mov w12, w0
.L__sysy_par_body_2_bb1:
	cmp w12, w16
	b.ge .L__sysy_par_body_2_bb2
.L__sysy_par_body_2_bb3:
	ldr w10, [x11]
	ldr w9, [x13], #4
	sub w10, w10, w9
	str w10, [x11]
	ldr w9, [x14], #4
	mul w9, w15, w9
	sdiv w9, w10, w9
	str w9, [x11], #4
	add w12, w12, #1
	b .L__sysy_par_body_2_bb1
.L__sysy_par_body_2_bb2:
	ret
	.size __sysy_par_body_2, .-__sysy_par_body_2
	.data
	.global m
	.p2align 2
m:
	.word 800
	.global n
	.p2align 2
n:
	.word 800
	.bss
	.global data
	.p2align 4
data:
	.zero 2560000
	.global corr
	.p2align 4
corr:
	.zero 2560000
	.global mean
	.p2align 4
mean:
	.zero 3200
	.global stddev
	.p2align 4
stddev:
	.zero 3200
	.global __sysy_par_ctx_0_0
	.p2align 3
__sysy_par_ctx_0_0:
	.zero 8
	.global __sysy_par_ctx_0_1
	.p2align 2
__sysy_par_ctx_0_1:
	.zero 4
	.global __sysy_par_ctx_0_2
	.p2align 3
__sysy_par_ctx_0_2:
	.zero 8
	.global __sysy_par_ctx_1_0
	.p2align 3
__sysy_par_ctx_1_0:
	.zero 8
	.global __sysy_par_ctx_1_1
	.p2align 3
__sysy_par_ctx_1_1:
	.zero 8
	.global __sysy_par_ctx_1_2
	.p2align 2
__sysy_par_ctx_1_2:
	.zero 4
	.global __sysy_par_ctx_1_3
	.p2align 3
__sysy_par_ctx_1_3:
	.zero 8
	.global __sysy_par_ctx_2_0
	.p2align 3
__sysy_par_ctx_2_0:
	.zero 8
	.global __sysy_par_ctx_2_1
	.p2align 2
__sysy_par_ctx_2_1:
	.zero 4
	.global __sysy_par_ctx_2_2
	.p2align 3
__sysy_par_ctx_2_2:
	.zero 8
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
