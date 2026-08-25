	.arch armv8-a
	.text
	.p2align 2
	.global kernel_nussinov
	.type kernel_nussinov, %function
kernel_nussinov:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #64
	stp x21, x22, [sp, #16]
	mov w21, w0
	stp x19, x20, [sp]
	stp x23, x24, [sp, #32]
	mov x24, x1
	str x25, [sp, #48]
	mov x20, x2
	sub w22, w21, #1
	cmp w21, #1
	b.lt .Lkernel_nussinov_bb3
.Lkernel_nussinov_bb4:
	movz w23, #0
	mov w25, w23
	movz w19, #1
.Lkernel_nussinov_bb1:
	cmp w25, w22
	b.ge .Lkernel_nussinov_bb3
.Lkernel_nussinov_bb2:
	adrp x13, __sysy_par_ctx_1_0
	adrp x12, __sysy_par_ctx_1_1
	adrp x11, __sysy_par_ctx_1_2
	adrp x10, __sysy_par_ctx_1_3
	adrp x9, __sysy_par_ctx_1_4
	str w25, [x13, :lo12:__sysy_par_ctx_1_0]
	sub w2, w22, w25
	str w22, [x12, :lo12:__sysy_par_ctx_1_1]
	mov w0, w19
	str x24, [x11, :lo12:__sysy_par_ctx_1_2]
	mov w1, w23
	str x20, [x10, :lo12:__sysy_par_ctx_1_3]
	str w21, [x9, :lo12:__sysy_par_ctx_1_4]
	bl __sysy_parallel_for
	add w25, w25, #1
	b .Lkernel_nussinov_bb1
.Lkernel_nussinov_bb3:
	adrp x10, __sysy_par_ctx_0_0
	adrp x9, __sysy_par_ctx_0_1
	str w21, [x10, :lo12:__sysy_par_ctx_0_0]
	mov w2, w21
	str x20, [x9, :lo12:__sysy_par_ctx_0_1]
	ldp x24, x25, [sp, #40]
	ldp x22, x23, [sp, #24]
	ldp x20, x21, [sp, #8]
	ldr x19, [sp]
	movz w1, #0
	mov w0, w1
	add sp, sp, #64
	ldp xzr, x30, [sp], #16
	b __sysy_parallel_for
	.size kernel_nussinov, .-kernel_nussinov
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #32
	stp x19, x20, [sp]
	adrp x9, n
	str x21, [sp, #16]
	ldr w19, [x9, :lo12:n]
	adrp x9, seq
	add x21, x9, :lo12:seq
	mov x0, x21
	bl getarray
	adrp x9, table
	add x20, x9, :lo12:table
	mov x0, x20
	bl getarray
	movz w0, #79
	bl _sysy_starttime
	mov w0, w19
	mov x1, x21
	mov x2, x20
	bl kernel_nussinov
	movz w0, #81
	bl _sysy_stoptime
	mul w0, w19, w19
	mov x1, x20
	bl putarray
	adrp x9, n
	str w19, [x9, :lo12:n]
	ldp x20, x21, [sp, #8]
	ldr x19, [sp]
	movz w0, #0
	add sp, sp, #32
	ldp xzr, x30, [sp], #16
	ret
	.size main, .-main
	.p2align 2
	.global __sysy_par_body_0
	.type __sysy_par_body_0, %function
__sysy_par_body_0:
	sub sp, sp, #16
	stp x19, x20, [sp]
	adrp x10, __sysy_par_ctx_0_0
	adrp x9, __sysy_par_ctx_0_1
	ldr w17, [x10, :lo12:__sysy_par_ctx_0_0]
	ldr x16, [x9, :lo12:__sysy_par_ctx_0_1]
	mov w15, w0
	mov w8, w1
	movz w7, #0
.L__sysy_par_body_0_bb1:
	cmp w15, w8
	b.ge .L__sysy_par_body_0_bb2
.L__sysy_par_body_0_bb3:
	movz w9, #5600
	smaddl x9, w15, w9, x16
	movz w13, #41705
	sub w3, w17, #3
	mov x4, x9
	mov w1, w7
	orr w5, wzr, #0x80000003
	movz w6, #11
	movk w13, #11915, lsl #16
.L__sysy_par_body_0_bb4:
	cmp w1, w3
	cset w10, lt
	cmp w17, w5
	cset w9, ge
	and w9, w9, w10
	cbz w9, .L__sysy_par_body_0_bb9
.L__sysy_par_body_0_bb8:
	ldr w0, [x4, #4]
	ldr w19, [x4]
	ldr w14, [x4, #12]
	smull x11, w0, w13
	ldr w2, [x4, #8]
	smull x12, w19, w13
	asr x11, x11, #33
	smull x9, w14, w13
	add w11, w11, w11, lsr #31
	smull x10, w2, w13
	asr x12, x12, #33
	msub w0, w11, w6, w0
	asr x9, x9, #33
	add w12, w12, w12, lsr #31
	msub w20, w12, w6, w19
	add w9, w9, w9, lsr #31
	msub w11, w9, w6, w14
	asr x10, x10, #33
	add w10, w10, w10, lsr #31
	msub w12, w10, w6, w2
	add x19, x4, #4
	add x9, x19, #4
	add x9, x9, #4
	str w20, [x4]
	add x9, x9, #4
	str w0, [x4, #4]
	str w12, [x4, #8]
	str w11, [x4, #12]
	add w1, w1, #4
	mov x4, x9
	b .L__sysy_par_body_0_bb4
.L__sysy_par_body_0_bb2:
	ldp x19, x20, [sp]
	add sp, sp, #16
	ret
.L__sysy_par_body_0_bb5:
	cmp w1, w17
	b.ge .L__sysy_par_body_0_bb7
.L__sysy_par_body_0_bb6:
	ldr w11, [x4]
	smull x9, w11, w10
	asr x9, x9, #33
	add w9, w9, w9, lsr #31
	msub w9, w9, w12, w11
	str w9, [x4], #4
	add w1, w1, #1
	b .L__sysy_par_body_0_bb5
.L__sysy_par_body_0_bb7:
	add w15, w15, #1
	b .L__sysy_par_body_0_bb1
.L__sysy_par_body_0_bb9:
	movz w10, #41705
	movz w12, #11
	movk w10, #11915, lsl #16
	b .L__sysy_par_body_0_bb5
	.size __sysy_par_body_0, .-__sysy_par_body_0
	.p2align 2
	.global __sysy_par_body_1
	.type __sysy_par_body_1, %function
__sysy_par_body_1:
	sub sp, sp, #32
	stp x19, x20, [sp]
	adrp x9, __sysy_par_ctx_1_0
	str x21, [sp, #16]
	ldr w3, [x9, :lo12:__sysy_par_ctx_1_0]
	adrp x11, __sysy_par_ctx_1_2
	ldr x6, [x11, :lo12:__sysy_par_ctx_1_2]
	adrp x12, __sysy_par_ctx_1_1
	adrp x10, __sysy_par_ctx_1_3
	adrp x9, __sysy_par_ctx_1_4
	ldr w5, [x12, :lo12:__sysy_par_ctx_1_1]
	ldr x7, [x10, :lo12:__sysy_par_ctx_1_3]
	ldr w8, [x9, :lo12:__sysy_par_ctx_1_4]
	mov w2, w0
	mov w4, w1
	add w17, w3, #1
	movz w11, #5600
	movz w15, #3
	movz w14, #0
.L__sysy_par_body_1_bb1:
	cmp w2, w4
	b.ge .L__sysy_par_body_1_bb2
.L__sysy_par_body_1_bb3:
	add w13, w17, w2
	sub w1, w5, w13
	add w0, w1, #1
	add w16, w0, w3
	cmp w16, #1
	sub w21, w16, #1
	cset w20, ge
	cbz w20, .L__sysy_par_body_1_bb6
.L__sysy_par_body_1_bb4:
	smaddl x9, w1, w11, x7
	add x12, x9, w16, sxtw #2
	ldr w10, [x12]
	ldr w9, [x9, w21, sxtw #2]
	cmp w10, w9
	b.ge .L__sysy_par_body_1_bb6
.L__sysy_par_body_1_bb5:
	str w9, [x12]
.L__sysy_par_body_1_bb6:
	cmp w0, w8
	cset w19, lt
	cbz w19, .L__sysy_par_body_1_bb9
.L__sysy_par_body_1_bb7:
	smaddl x10, w1, w11, x7
	smaddl x9, w0, w11, x7
	add x12, x10, w16, sxtw #2
	ldr w10, [x12]
	ldr w9, [x9, w16, sxtw #2]
	cmp w10, w9
	b.ge .L__sysy_par_body_1_bb9
.L__sysy_par_body_1_bb8:
	lsl w9, w9, #1
	str w9, [x12]
.L__sysy_par_body_1_bb9:
	cbz w20, .L__sysy_par_body_1_bb16
.L__sysy_par_body_1_bb10:
	cbz w19, .L__sysy_par_body_1_bb16
.L__sysy_par_body_1_bb11:
	cmp w1, w21
	b.lt .L__sysy_par_body_1_bb12
.L__sysy_par_body_1_bb14:
	smaddl x10, w1, w11, x7
	smaddl x9, w0, w11, x7
	add x12, x10, w16, sxtw #2
	ldr w10, [x12]
	ldr w9, [x9, w21, sxtw #2]
	cmp w10, w9
	b.ge .L__sysy_par_body_1_bb16
.L__sysy_par_body_1_bb15:
	str w9, [x12]
.L__sysy_par_body_1_bb16:
	cmp w0, w16
	b.ge .L__sysy_par_body_1_bb21
.L__sysy_par_body_1_bb17:
	movz w9, #5600
	smaddl x10, w1, w9, x7
	add w12, w1, #2
	smaddl x9, w12, w9, x7
	add w12, w17, w5
	sub w12, w12, w13
	add x20, x9, w12, sxtw #2
	add x19, x10, w0, sxtw #2
	add x21, x10, w16, sxtw #2
	movz x9, #5600
	b .L__sysy_par_body_1_bb18
.L__sysy_par_body_1_bb20:
	add w0, w0, #1
	cmp w0, w16
	add x19, x19, #4
	add x20, x20, x9
	b.ge .L__sysy_par_body_1_bb21
.L__sysy_par_body_1_bb18:
	ldr w13, [x19]
	ldr w12, [x20]
	ldr w1, [x21]
	add w10, w13, w12
	cmp w1, w10
	b.ge .L__sysy_par_body_1_bb20
.L__sysy_par_body_1_bb19:
	lsl w10, w13, #1
	add w10, w10, w12
	str w10, [x21]
	b .L__sysy_par_body_1_bb20
.L__sysy_par_body_1_bb21:
	add w2, w2, #1
	b .L__sysy_par_body_1_bb1
.L__sysy_par_body_1_bb2:
	ldp x20, x21, [sp, #8]
	ldr x19, [sp]
	add sp, sp, #32
	ret
.L__sysy_par_body_1_bb12:
	ldr w20, [x6, w1, sxtw #2]
	ldr w19, [x6, w16, sxtw #2]
	smaddl x10, w1, w11, x7
	smaddl x9, w0, w11, x7
	ldr w9, [x9, w21, sxtw #2]
	add x12, x10, w16, sxtw #2
	ldr w10, [x12]
	add w19, w20, w19
	cmp w19, #3
	csel w19, w15, w14, eq
	add w9, w9, w19
	cmp w10, w9
	b.ge .L__sysy_par_body_1_bb16
.L__sysy_par_body_1_bb13:
	str w9, [x12]
	b .L__sysy_par_body_1_bb16
	.size __sysy_par_body_1, .-__sysy_par_body_1
	.data
	.global n
	.p2align 2
n:
	.word 1400
	.bss
	.global seq
	.p2align 4
seq:
	.zero 5600
	.global table
	.p2align 4
table:
	.zero 7840000
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
	.p2align 2
__sysy_par_ctx_1_4:
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
