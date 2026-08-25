	.arch armv8-a
	.text
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #32
	stp x19, x20, [sp]
	movz w19, #0
	str x21, [sp, #16]
	bl getint
	adrp x10, __sysy_par_ctx_0_0
	str w0, [x10, :lo12:__sysy_par_ctx_0_0]
	add w11, w0, w0, lsl #2
	adrp x9, __sysy_par_ctx_0_1
	movz w1, #0
	str w11, [x9, :lo12:__sysy_par_ctx_0_1]
	movz w2, #8, lsl #16
	mov w0, w1
	bl __sysy_parallel_for
	movz w0, #14
	bl _sysy_starttime
	adrp x11, c
	adrp x10, a
	adrp x9, b
	movi v21.4s, #3
	add x16, x11, :lo12:c
	movi v20.4s, #7
	add x15, x10, :lo12:a
	add x14, x9, :lo12:b
	mov x11, x15
	mov x10, x14
	mov x9, x16
	mov w12, w19
	orr w13, wzr, #0x7fff8
.Lmain_bb1:
	cmp w12, w13
	b.gt .Lmain_bb2
.Lmain_bb14:
	ldp q17, q16, [x11]
	ldp q19, q18, [x10]
	mla v17.4s, v19.4s, v21.4s
	mla v16.4s, v18.4s, v21.4s
	sub v17.4s, v17.4s, v20.4s
	sub v16.4s, v16.4s, v20.4s
	stp q17, q16, [x9]
	add w12, w12, #8
	add x11, x11, #32
	add x10, x10, #32
	add x9, x9, #32
	b .Lmain_bb1
.Lmain_bb2:
	add x11, x15, w12, sxtw #2
	add x14, x14, w12, sxtw #2
	add x15, x16, w12, sxtw #2
	movz w13, #3
.Lmain_bb3:
	cmp w12, #128, lsl #12
	b.ge .Lmain_bb15
.Lmain_bb13:
	ldr w10, [x11], #4
	ldr w9, [x14], #4
	madd w9, w9, w13, w10
	sub w9, w9, #7
	str w9, [x15], #4
	add w12, w12, #1
	b .Lmain_bb3
.Lmain_bb4:
	cmp w21, #3060
	b.ge .Lmain_bb9
.Lmain_bb5:
	adrp x11, a
	adrp x10, b
	adrp x9, c
	add x11, x11, :lo12:a
	add x10, x10, :lo12:b
	add x9, x9, :lo12:c
	mov x17, x9
	mov x16, x10
	mov x15, x11
	mov w13, w20
	mov w12, w19
	movz w14, #3
.Lmain_bb6:
	cmp w12, #128, lsl #12
	b.ge .Lmain_bb8
.Lmain_bb7:
	ldr w11, [x15], #4
	ldr w10, [x16], #4
	ldr w9, [x17], #4
	madd w10, w10, w14, w11
	sub w10, w10, #7
	cmp w9, w10
	add w9, w13, #1
	csel w13, w9, w13, ne
	add w12, w12, #1
	b .Lmain_bb6
.Lmain_bb8:
	add w21, w21, #1
	cmp w13, w20
	b.eq .Lmain_bb18
.Lmain_bb16:
	mov w20, w13
	b .Lmain_bb4
.Lmain_bb9:
	movz w0, #31
	bl _sysy_stoptime
	cbz w20, .Lmain_bb10
.Lmain_bb11:
	movz w0, #0
	bl putint
.Lmain_bb12:
	movz w0, #10
	bl putch
	ldp x20, x21, [sp, #8]
	ldr x19, [sp]
	movz w0, #0
	add sp, sp, #32
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb10:
	movz w0, #1
	bl putint
	b .Lmain_bb12
.Lmain_bb15:
	mov w21, w19
	mov w20, w19
	b .Lmain_bb4
.Lmain_bb18:
	mov w20, w13
	b .Lmain_bb9
	.size main, .-main
	.p2align 2
	.global __sysy_par_body_0
	.type __sysy_par_body_0, %function
__sysy_par_body_0:
	adrp x12, __sysy_par_ctx_0_0
	ldr w6, [x12, :lo12:__sysy_par_ctx_0_0]
	adrp x11, __sysy_par_ctx_0_1
	adrp x10, a
	ldr w8, [x11, :lo12:__sysy_par_ctx_0_1]
	mov w5, w0
	add x10, x10, :lo12:a
	adrp x9, b
	add x4, x10, w5, sxtw #2
	add x9, x9, :lo12:b
	movz w12, #2027
	movz w10, #737
	mov w7, w1
	add x3, x9, w5, sxtw #2
	movz w16, #97
	movk w12, #5405, lsl #16
	movz w15, #3
	movz w14, #89
	movk w10, #47127, lsl #16
.L__sysy_par_body_0_bb1:
	cmp w5, w7
	b.ge .L__sysy_par_body_0_bb2
.L__sysy_par_body_0_bb3:
	madd w13, w5, w15, w8
	smull x9, w13, w10
	add w17, w5, w6
	smull x11, w17, w12
	asr x9, x9, #32
	asr x11, x11, #35
	add w9, w9, w13
	add w11, w11, w11, lsr #31
	asr w9, w9, #6
	msub w11, w11, w16, w17
	add w9, w9, w9, lsr #31
	msub w9, w9, w14, w13
	str w11, [x4], #4
	add w5, w5, #1
	str w9, [x3], #4
	b .L__sysy_par_body_0_bb1
.L__sysy_par_body_0_bb2:
	ret
	.size __sysy_par_body_0, .-__sysy_par_body_0
	.bss
	.global a
	.p2align 4
a:
	.zero 2097152
	.global b
	.p2align 4
b:
	.zero 2097152
	.global c
	.p2align 4
c:
	.zero 2097152
	.global __sysy_par_ctx_0_0
	.p2align 2
__sysy_par_ctx_0_0:
	.zero 4
	.global __sysy_par_ctx_0_1
	.p2align 2
__sysy_par_ctx_0_1:
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
