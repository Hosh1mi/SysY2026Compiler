	.arch armv8-a
	.text
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #1, lsl #12
	sub sp, sp, #96
	stp x21, x22, [sp, #16]
	add x22, sp, #96
	stp x19, x20, [sp]
	movz w21, #0
	stp x23, x24, [sp, #32]
	stp x25, x26, [sp, #48]
	stp x27, x28, [sp, #64]
	bl getint
	mov w20, w0
	bl getint
	adrp x9, A
	add w10, w20, w20, lsr #31
	str w0, [sp, #80]
	add x25, x9, :lo12:A
	asr w23, w10, #1
	mov w24, w21
	movz x19, #4096
.Lmain_bb1:
	cmp w24, w20
	b.ge .Lmain_bb2
.Lmain_bb23:
	cmp w24, w23
	b.ge .Lmain_bb25
.Lmain_bb24:
	mov x0, x25
	bl getarray
.Lmain_bb25:
	add w24, w24, #1
	add x25, x25, x19
	b .Lmain_bb1
.Lmain_bb2:
	adrp x9, B
	add x9, x9, :lo12:B
	mov x25, x9
	mov w24, w21
	movz x19, #4096
.Lmain_bb3:
	cmp w24, w20
	b.ge .Lmain_bb7
.Lmain_bb4:
	cmp w24, w23
	b.lt .Lmain_bb6
.Lmain_bb5:
	mov x0, x25
	bl getarray
.Lmain_bb6:
	add w24, w24, #1
	add x25, x25, x19
	b .Lmain_bb3
.Lmain_bb7:
	movz w0, #25
	bl _sysy_starttime
	adrp x11, __sysy_par_ctx_0_0
	adrp x10, __sysy_par_ctx_0_1
	adrp x9, __sysy_par_ctx_0_2
	movz w1, #0
	str w23, [x11, :lo12:__sysy_par_ctx_0_0]
	mov w0, w1
	str w20, [x10, :lo12:__sysy_par_ctx_0_1]
	mov w2, w20
	str w23, [x9, :lo12:__sysy_par_ctx_0_2]
	bl __sysy_parallel_for
	mov w24, w21
.Lmain_bb8:
	cmp w24, w20
	b.ge .Lmain_bb9
.Lmain_bb19:
	mov x19, x22
	lsl w23, w20, #2
	movz w1, #0
	mov x0, x19
	mov w2, w23
	bl memset
	adrp x9, C
	add x10, x9, :lo12:C
	sxtw x9, w24
	add x9, x10, x9, lsl #12
	mov x27, x9
	mov w28, w21
	movz w26, #1
	movz w25, #0
.Lmain_bb20:
	cmp w28, w20
	b.ge .Lmain_bb21
.Lmain_bb22:
	ldr w12, [x27]
	adrp x11, __sysy_par_ctx_1_0
	adrp x10, __sysy_par_ctx_1_1
	adrp x9, __sysy_par_ctx_1_2
	str x22, [x11, :lo12:__sysy_par_ctx_1_0]
	mov w0, w26
	str w28, [x10, :lo12:__sysy_par_ctx_1_1]
	mov w1, w25
	str w12, [x9, :lo12:__sysy_par_ctx_1_2]
	mov w2, w20
	bl __sysy_parallel_for
	add w28, w28, #1
	add x27, x27, #4
	b .Lmain_bb20
.Lmain_bb9:
	ldr w9, [sp, #80]
	cmp w9, #0
	b.le .Lmain_bb18
.Lmain_bb26:
	mov w11, w21
	mov w13, w21
.Lmain_bb10:
	cmp w13, w20
	b.ge .Lmain_bb17
.Lmain_bb11:
	adrp x9, A
	add x10, x9, :lo12:A
	sxtw x9, w13
	add x9, x10, x9, lsl #12
	sub w19, w20, #3
	mov x17, x9
	mov w16, w21
	mov w15, w11
	orr w14, wzr, #0x80000003
.Lmain_bb12:
	cmp w16, w19
	cset w10, lt
	cmp w20, w14
	cset w9, ge
	and w9, w9, w10
	cbz w9, .Lmain_bb27
.Lmain_bb16:
	ldp w9, w11, [x17]
	madd w12, w9, w9, w15
	ldp w10, w9, [x17, #8]
	madd w11, w11, w11, w12
	madd w10, w10, w10, w11
	madd w15, w9, w9, w10
	add x12, x17, #4
	add x9, x12, #4
	add x9, x9, #4
	add w16, w16, #4
	add x17, x9, #4
	b .Lmain_bb12
.Lmain_bb13:
	cmp w10, w20
	b.ge .Lmain_bb15
.Lmain_bb14:
	ldr w9, [x12], #4
	madd w11, w9, w9, w11
	add w10, w10, #1
	b .Lmain_bb13
.Lmain_bb15:
	add w13, w13, #1
	b .Lmain_bb10
.Lmain_bb17:
	ldr w9, [sp, #80]
	mul w21, w11, w9
.Lmain_bb18:
	movz w0, #105
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
	add sp, sp, #1, lsl #12
	add sp, sp, #96
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb21:
	adrp x9, A
	add x10, x9, :lo12:A
	sxtw x9, w24
	add x9, x10, x9, lsl #12
	mov x0, x9
	mov x1, x19
	mov w2, w23
	bl memcpy
	add w24, w24, #1
	b .Lmain_bb8
.Lmain_bb27:
	mov x12, x17
	mov w10, w16
	mov w11, w15
	b .Lmain_bb13
	.size main, .-main
	.p2align 2
	.global __sysy_par_body_0
	.type __sysy_par_body_0, %function
__sysy_par_body_0:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #64
	stp x19, x20, [sp]
	adrp x11, __sysy_par_ctx_0_0
	stp x21, x22, [sp, #16]
	adrp x10, __sysy_par_ctx_0_1
	stp x23, x24, [sp, #32]
	adrp x9, __sysy_par_ctx_0_2
	stp x25, x26, [sp, #48]
	ldr w23, [x11, :lo12:__sysy_par_ctx_0_0]
	ldr w22, [x10, :lo12:__sysy_par_ctx_0_1]
	ldr w21, [x9, :lo12:__sysy_par_ctx_0_2]
	mov w26, w0
	mov w25, w1
	movz w24, #0
	movz w20, #255
.L__sysy_par_body_0_bb1:
	cmp w26, w25
	b.ge .L__sysy_par_body_0_bb2
.L__sysy_par_body_0_bb3:
	cmp w26, w23
	b.lt .L__sysy_par_body_0_bb5
.L__sysy_par_body_0_bb4:
	adrp x9, A
	add x10, x9, :lo12:A
	sxtw x9, w26
	add x9, x10, x9, lsl #12
	lsl w2, w22, #2
	mov x0, x9
	mov w1, w20
	bl memset
.L__sysy_par_body_0_bb5:
	add w19, w26, #1
	cmp w26, w21
	b.ge .L__sysy_par_body_0_bb7
.L__sysy_par_body_0_bb6:
	adrp x9, B
	add x10, x9, :lo12:B
	sxtw x9, w26
	add x9, x10, x9, lsl #12
	lsl w2, w22, #2
	mov x0, x9
	mov w1, w20
	bl memset
.L__sysy_par_body_0_bb7:
	adrp x11, C
	adrp x10, A
	add x13, x11, :lo12:C
	adrp x9, B
	add x11, x10, :lo12:A
	add x10, x9, :lo12:B
	sxtw x12, w26
	sxtw x9, w26
	add x13, x13, x12, lsl #12
	add x11, x11, x12, lsl #12
	add x9, x10, x9, lsl #12
	mov x12, x13
	movz w10, #21846
	mov x17, x9
	mov x16, x11
	mov w14, w24
	movz w15, #3
	movz w13, #7
	movk w10, #21845, lsl #16
.L__sysy_par_body_0_bb8:
	cmp w14, w22
	b.ge .L__sysy_par_body_0_bb10
.L__sysy_par_body_0_bb9:
	ldr w11, [x16], #4
	ldr w9, [x17], #4
	lsl w11, w11, #1
	madd w9, w9, w15, w11
	madd w9, w9, w9, w13
	smull x9, w9, w10
	asr x9, x9, #32
	add w9, w9, w9, lsr #31
	str w9, [x12], #4
	add w14, w14, #1
	b .L__sysy_par_body_0_bb8
.L__sysy_par_body_0_bb2:
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	add sp, sp, #64
	ldp xzr, x30, [sp], #16
	ret
.L__sysy_par_body_0_bb10:
	mov w26, w19
	b .L__sysy_par_body_0_bb1
	.size __sysy_par_body_0, .-__sysy_par_body_0
	.p2align 2
	.global __sysy_par_body_1
	.type __sysy_par_body_1, %function
__sysy_par_body_1:
	adrp x9, __sysy_par_ctx_1_1
	ldr w15, [x9, :lo12:__sysy_par_ctx_1_1]
	adrp x9, __sysy_par_ctx_1_0
	ldr x17, [x9, :lo12:__sysy_par_ctx_1_0]
	mov w7, w0
	adrp x9, A
	adrp x12, __sysy_par_ctx_1_2
	orr w11, wzr, #0x7ffffff0
	ldr w14, [x12, :lo12:__sysy_par_ctx_1_2]
	add x10, x9, :lo12:A
	cmp w7, w11
	mov w13, w1
	sxtw x9, w15
	cset w16, le
	add w11, w7, #15
	add x9, x10, x9, lsl #12
	cmp w11, w13
	add x8, x9, w7, sxtw #2
	sub w11, w13, w7
	add x6, x17, w7, sxtw #2
	cset w12, lt
	add x9, x8, w11, sxtw #2
	dup v20.4s, w14
	cmp x9, x6
	add x9, x6, w11, sxtw #2
	cset w10, ls
	cmp x9, x8
	cset w9, ls
	and w11, w16, w12
	orr w9, w10, w9
	sub w12, w13, #8
	and w9, w11, w9
	cbz w9, .L__sysy_par_body_1_bb10
.L__sysy_par_body_1_bb9:
	mov x9, x8
	mov x10, x6
	mov w11, w7
.L__sysy_par_body_1_bb1:
	cmp w11, w12
	b.gt .L__sysy_par_body_1_bb11
.L__sysy_par_body_1_bb2:
	ldp q17, q16, [x10]
	ldp q19, q18, [x9]
	mla v17.4s, v20.4s, v19.4s
	mla v16.4s, v20.4s, v18.4s
	stp q17, q16, [x10]
	add w11, w11, #8
	add x10, x10, #32
	add x9, x9, #32
	b .L__sysy_par_body_1_bb1
.L__sysy_par_body_1_bb3:
	adrp x9, A
	add x10, x9, :lo12:A
	sxtw x9, w15
	add x9, x10, x9, lsl #12
	add x8, x17, w16, sxtw #2
	add x7, x9, w16, sxtw #2
	sub w17, w13, #3
	orr w15, wzr, #0x80000003
.L__sysy_par_body_1_bb4:
	cmp w16, w17
	cset w10, lt
	cmp w13, w15
	cset w9, ge
	and w9, w9, w10
	cbz w9, .L__sysy_par_body_1_bb12
.L__sysy_par_body_1_bb8:
	ldr w10, [x8]
	ldr w9, [x7]
	madd w9, w14, w9, w10
	str w9, [x8]
	ldr w10, [x8, #4]
	ldr w9, [x7, #4]
	madd w9, w14, w9, w10
	str w9, [x8, #4]
	ldr w10, [x8, #8]
	ldr w9, [x7, #8]
	madd w9, w14, w9, w10
	str w9, [x8, #8]
	ldr w10, [x8, #12]
	ldr w9, [x7, #12]
	madd w9, w14, w9, w10
	add x12, x8, #4
	add x11, x7, #4
	add x10, x12, #4
	add x11, x11, #4
	str w9, [x8, #12]
	add x10, x10, #4
	add x12, x11, #4
	add w16, w16, #4
	add x8, x10, #4
	add x7, x12, #4
	b .L__sysy_par_body_1_bb4
.L__sysy_par_body_1_bb5:
	cmp w11, w13
	b.ge .L__sysy_par_body_1_bb6
.L__sysy_par_body_1_bb7:
	ldr w10, [x12]
	ldr w9, [x15], #4
	madd w9, w14, w9, w10
	str w9, [x12], #4
	add w11, w11, #1
	b .L__sysy_par_body_1_bb5
.L__sysy_par_body_1_bb6:
	ret
.L__sysy_par_body_1_bb10:
	mov w16, w7
	b .L__sysy_par_body_1_bb3
.L__sysy_par_body_1_bb11:
	mov w16, w11
	b .L__sysy_par_body_1_bb3
.L__sysy_par_body_1_bb12:
	mov x15, x7
	mov x12, x8
	mov w11, w16
	b .L__sysy_par_body_1_bb5
	.size __sysy_par_body_1, .-__sysy_par_body_1
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
	.p2align 2
__sysy_par_ctx_0_1:
	.zero 4
	.global __sysy_par_ctx_0_2
	.p2align 2
__sysy_par_ctx_0_2:
	.zero 4
	.global __sysy_par_ctx_1_0
	.p2align 3
__sysy_par_ctx_1_0:
	.zero 8
	.global __sysy_par_ctx_1_1
	.p2align 2
__sysy_par_ctx_1_1:
	.zero 4
	.global __sysy_par_ctx_1_2
	.p2align 2
__sysy_par_ctx_1_2:
	.zero 4

	.text
	.align 2
	.global __sysy_par_dispatch
__sysy_par_dispatch:
	cmp w0, #0
	b.eq .Lsysy_disp_0
	cmp w0, #1
	b.eq .Lsysy_disp_1
	ret
.Lsysy_disp_0:
	mov w0, w1
	mov w1, w2
	b __sysy_par_body_0
.Lsysy_disp_1:
	mov w0, w1
	mov w1, w2
	b __sysy_par_body_1

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
