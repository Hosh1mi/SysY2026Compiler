	.arch armv8-a
	.text
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #16
	stp x19, x20, [sp]
	bl getint
	mov w20, w0
	movz w0, #23
	bl _sysy_starttime
	cmp w20, #1
	b.ge .Lmain_bb1
.Lmain_bb3:
	movz w19, #0
.Lmain_bb2:
	movz w0, #28
	bl _sysy_stoptime
	mov w0, w19
	bl putint
	adrp x9, lim
	str w20, [x9, :lo12:lim]
	ldp x19, x20, [sp]
	movz w0, #0
	add sp, sp, #16
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb1:
	adrp x13, __sysy_par_ctx_0_0
	movz w1, #1
	adrp x12, __sysy_par_scalar_start_0
	adrp x11, __sysy_par_scalar_bound_0
	movz w0, #0
	adrp x10, __sysy_par_scalar_partial_0_0
	adrp x9, __sysy_par_scalar_partial_0_1
	str w20, [x13, :lo12:__sysy_par_ctx_0_0]
	mov w2, w20
	str w1, [x12, :lo12:__sysy_par_scalar_start_0]
	str w20, [x11, :lo12:__sysy_par_scalar_bound_0]
	str w0, [x10, :lo12:__sysy_par_scalar_partial_0_0]
	str w0, [x9, :lo12:__sysy_par_scalar_partial_0_1]
	bl __sysy_parallel_for
	adrp x9, __sysy_par_scalar_partial_0_1
	ldr w15, [x9, :lo12:__sysy_par_scalar_partial_0_1]
	adrp x9, __sysy_par_scalar_partial_0_0
	ldr w16, [x9, :lo12:__sysy_par_scalar_partial_0_0]
	movz w13, #51719
	cmp w15, #0
	movk w13, #15258, lsl #16
	cset w14, ge
	sub w9, w13, w15
	sub w10, w16, w9
	cmp w16, w9
	add w12, w16, w15
	movz w9, #13817
	movk w9, #50277, lsl #16
	csel w11, w10, w12, ge
	sub w10, w9, w15
	sub w9, w16, w10
	cmp w16, w10
	csel w9, w9, w12, le
	cmp w14, #0
	csel w10, w11, w9, ne
	movz w9, #12185
	movk w9, #17592, lsl #16
	smull x9, w10, w9
	asr x9, x9, #60
	add w9, w9, w9, lsr #31
	msub w19, w9, w13, w10
	b .Lmain_bb2
	.size main, .-main
	.p2align 2
	.global __sysy_par_body_0
	.type __sysy_par_body_0, %function
__sysy_par_body_0:
	adrp x9, __sysy_par_scalar_start_0
	ldr w10, [x9, :lo12:__sysy_par_scalar_start_0]
	adrp x9, __sysy_par_scalar_bound_0
	ldr w9, [x9, :lo12:__sysy_par_scalar_bound_0]
	mov w16, w0
	cmp w16, w10
	cset w17, eq
	adrp x10, __sysy_par_ctx_0_0
	ldr w7, [x10, :lo12:__sysy_par_ctx_0_0]
	cmp w1, w9
	cset w9, eq
	cmp w9, #1
	cset w9, ne
	and w10, w17, w9
	cmp w10, #0
	movz w5, #0
	sub w9, w1, #1
	movz w12, #51719
	movz w10, #12185
	movz w6, #7
	csel w8, w9, w1, ne
	mov w15, w5
	movk w12, #15258, lsl #16
	movk w10, #17592, lsl #16
.L__sysy_par_body_0_bb1:
	cmp w16, #1
	b.eq .L__sysy_par_body_0_bb15
.L__sysy_par_body_0_bb11:
	mov w14, w5
	mov w11, w16
	b .L__sysy_par_body_0_bb2
.L__sysy_par_body_0_bb3:
	rbit w9, w11
	clz w9, w9
	asr w11, w11, w9
	add w14, w14, w9
	cmp w11, #1
	b.eq .L__sysy_par_body_0_bb14
	b .L__sysy_par_body_0_bb2
.L__sysy_par_body_0_bb5:
	add w11, w9, #1
	rbit w9, w11
	clz w9, w9
	add w13, w14, #1
	asr w11, w11, w9
	add w14, w13, w9
	cmp w11, #1
	b.eq .L__sysy_par_body_0_bb14
.L__sysy_par_body_0_bb2:
	tbz w11, #0, .L__sysy_par_body_0_bb3
.L__sysy_par_body_0_bb4:
	add w9, w11, w11, lsl #1
	cmp w9, w7
	b.lt .L__sysy_par_body_0_bb5
	b .L__sysy_par_body_0_bb13
.L__sysy_par_body_0_bb14:
	mov w9, w14
.L__sysy_par_body_0_bb8:
	add w11, w15, w9
	smull x9, w11, w10
	asr x9, x9, #60
	add w9, w9, w9, lsr #31
	msub w15, w9, w12, w11
	add w9, w16, #1
	cmp w16, w8
	b.ge .L__sysy_par_body_0_bb9
.L__sysy_par_body_0_bb10:
	mov w16, w9
	b .L__sysy_par_body_0_bb1
.L__sysy_par_body_0_bb9:
	adrp x10, __sysy_par_scalar_partial_0_0
	adrp x9, __sysy_par_scalar_partial_0_1
	add x10, x10, :lo12:__sysy_par_scalar_partial_0_0
	add x9, x9, :lo12:__sysy_par_scalar_partial_0_1
	cmp w17, #0
	csel x9, x10, x9, ne
	str w15, [x9]
	ret
.L__sysy_par_body_0_bb13:
	mov w9, w6
	b .L__sysy_par_body_0_bb8
.L__sysy_par_body_0_bb15:
	mov w9, w5
	b .L__sysy_par_body_0_bb8
	.size __sysy_par_body_0, .-__sysy_par_body_0
	.data
	.global lim
	.p2align 2
lim:
	.zero 4
	.bss
	.global __sysy_par_ctx_0_0
	.p2align 2
__sysy_par_ctx_0_0:
	.zero 4
	.global __sysy_par_scalar_start_0
	.p2align 2
__sysy_par_scalar_start_0:
	.zero 4
	.global __sysy_par_scalar_bound_0
	.p2align 2
__sysy_par_scalar_bound_0:
	.zero 4
	.global __sysy_par_scalar_partial_0_0
	.p2align 2
__sysy_par_scalar_partial_0_0:
	.zero 4
	.global __sysy_par_scalar_partial_0_1
	.p2align 2
__sysy_par_scalar_partial_0_1:
	.zero 4

	.text
	.align 2
	.global __sysy_par_dispatch
__sysy_par_dispatch:
	cmp w0, #0
	b.eq .Lsysy_disp_0
	ret
.Lsysy_disp_0:
	mov w0, w1
	mov w1, w2
	b __sysy_par_body_0

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
