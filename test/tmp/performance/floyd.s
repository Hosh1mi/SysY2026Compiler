	.arch armv8-a
	.text
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #64
	stp x19, x20, [sp]
	movz w20, #0
	stp x21, x22, [sp, #16]
	stp x23, x24, [sp, #32]
	stp x25, x26, [sp, #48]
	bl getint
	adrp x9, w
	add x21, x9, :lo12:w
	mov w19, w0
	mov x0, x21
	bl getarray
	movz w0, #62
	bl _sysy_starttime
	adrp x9, temp
	add x16, x9, :lo12:temp
	mov x17, x21
	mov x21, x16
	mov w22, w20
.Lmain_bb1:
	cmp w22, w19
	b.ge .Lmain_bb2
.Lmain_bb19:
	cmp w22, #0
	b.lt .Lmain_bb25
.Lmain_bb20:
	sub w23, w19, #3
	mov x12, x17
	mov x13, x21
	mov w15, w20
	orr w14, wzr, #0x80000003
.Lmain_bb21:
	cmp w15, w23
	cset w10, lt
	cmp w19, w14
	cset w9, ge
	and w9, w9, w10
	cbz w9, .Lmain_bb33
.Lmain_bb24:
	ldr w9, [x12]
	add x10, x12, w19, sxtw #2
	str w9, [x13]
	ldr w9, [x10]
	add x11, x13, w19, sxtw #2
	str w9, [x11]
	add x10, x10, w19, sxtw #2
	ldr w9, [x10]
	add x11, x11, w19, sxtw #2
	str w9, [x11]
	add x9, x10, w19, sxtw #2
	ldr w12, [x9]
	add x13, x11, w19, sxtw #2
	str w12, [x13]
	add x10, x13, w19, sxtw #2
	add x9, x9, w19, sxtw #2
	add w15, w15, #4
	mov x12, x9
	mov x13, x10
	b .Lmain_bb21
.Lmain_bb2:
	mov x13, x16
	mov x14, x16
	mov w15, w20
.Lmain_bb3:
	cmp w15, w19
	b.ge .Lmain_bb18
.Lmain_bb31:
	mov x17, x16
	mov x21, x16
	mov x22, x14
	mov w23, w20
.Lmain_bb4:
	cmp w23, w19
	b.ge .Lmain_bb17
.Lmain_bb32:
	mov x24, x17
	mov x25, x21
	mov x26, x13
	mov w12, w20
.Lmain_bb5:
	cmp w12, w19
	b.ge .Lmain_bb16
.Lmain_bb6:
	cmp w23, #0
	b.lt .Lmain_bb15
.Lmain_bb7:
	cmp w15, #0
	b.lt .Lmain_bb15
.Lmain_bb8:
	ldr w11, [x22]
	cmp w11, #0
	b.lt .Lmain_bb15
.Lmain_bb9:
	cmp w15, #0
	b.lt .Lmain_bb15
.Lmain_bb10:
	ldr w9, [x26]
	cmp w9, #0
	b.lt .Lmain_bb15
.Lmain_bb11:
	ldr w9, [x25]
	cmp w9, #0
	b.lt .Lmain_bb12
.Lmain_bb13:
	ldr w9, [x26]
	ldr w10, [x24]
	add w9, w11, w9
	cmp w10, w9
	b.le .Lmain_bb15
.Lmain_bb14:
	ldr w10, [x22]
	ldr w9, [x26]
	add w9, w10, w9
	str w9, [x24]
.Lmain_bb15:
	add w12, w12, #1
	add x26, x26, #4
	add x25, x25, #4
	add x24, x24, #4
	b .Lmain_bb5
.Lmain_bb12:
	ldr w10, [x22]
	ldr w9, [x26]
	add w9, w10, w9
	str w9, [x24]
	b .Lmain_bb15
.Lmain_bb16:
	add w23, w23, #1
	add x22, x22, w19, sxtw #2
	add x21, x21, w19, sxtw #2
	add x17, x17, w19, sxtw #2
	b .Lmain_bb4
.Lmain_bb17:
	add w15, w15, #1
	add x14, x14, #4
	add x13, x13, w19, sxtw #2
	b .Lmain_bb3
.Lmain_bb18:
	mul w20, w19, w19
	adrp x9, dst
	add x9, x9, :lo12:dst
	mov x19, x9
	adrp x10, __sysy_par_ctx_0_0
	adrp x9, __sysy_par_ctx_0_1
	movz w1, #0
	str x19, [x10, :lo12:__sysy_par_ctx_0_0]
	mov w0, w1
	str x16, [x9, :lo12:__sysy_par_ctx_0_1]
	mov w2, w20
	bl __sysy_parallel_for
	movz w0, #64
	bl _sysy_stoptime
	mov w0, w20
	mov x1, x19
	bl putarray
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	movz w0, #0
	add sp, sp, #64
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb22:
	cmp w11, w19
	b.ge .Lmain_bb28
.Lmain_bb23:
	ldr w9, [x12]
	str w9, [x10]
	add w11, w11, #1
	add x10, x10, w19, sxtw #2
	add x12, x12, w19, sxtw #2
	b .Lmain_bb22
.Lmain_bb25:
	sub w15, w19, #3
	mov x11, x21
	mov w14, w20
	orr w13, wzr, #0x80000003
	movn w12, #0
.Lmain_bb26:
	cmp w14, w15
	cset w10, lt
	cmp w19, w13
	cset w9, ge
	and w9, w9, w10
	cbz w9, .Lmain_bb34
.Lmain_bb30:
	add x10, x11, w19, sxtw #2
	str w12, [x11]
	add x9, x10, w19, sxtw #2
	str w12, [x10]
	add x11, x9, w19, sxtw #2
	str w12, [x9]
	add x9, x11, w19, sxtw #2
	str w12, [x11]
	add w14, w14, #4
	mov x11, x9
	b .Lmain_bb26
.Lmain_bb27:
	cmp w10, w19
	b.ge .Lmain_bb28
.Lmain_bb29:
	str w11, [x9]
	add w10, w10, #1
	add x9, x9, w19, sxtw #2
	b .Lmain_bb27
.Lmain_bb28:
	add w22, w22, #1
	add x21, x21, #4
	add x17, x17, #4
	b .Lmain_bb1
.Lmain_bb33:
	mov x10, x13
	mov w11, w15
	b .Lmain_bb22
.Lmain_bb34:
	mov x9, x11
	mov w10, w14
	movn w11, #0
	b .Lmain_bb27
	.size main, .-main
	.p2align 2
	.global __sysy_par_body_0
	.type __sysy_par_body_0, %function
__sysy_par_body_0:
	adrp x9, __sysy_par_ctx_0_1
	ldr x17, [x9, :lo12:__sysy_par_ctx_0_1]
	adrp x9, __sysy_par_ctx_0_0
	ldr x15, [x9, :lo12:__sysy_par_ctx_0_0]
	mov w8, w0
	orr w9, wzr, #0x7ffffff0
	cmp w8, w9
	mov w16, w1
	cset w12, le
	add w9, w8, #15
	add x14, x17, w8, sxtw #2
	cmp w9, w16
	sub w10, w16, w8
	add x13, x15, w8, sxtw #2
	cset w11, lt
	add x9, x14, w10, sxtw #2
	cmp x9, x13
	add x9, x13, w10, sxtw #2
	cset w10, ls
	cmp x9, x14
	cset w9, ls
	and w11, w12, w11
	orr w9, w10, w9
	sub w12, w16, #8
	and w9, w11, w9
	cbz w9, .L__sysy_par_body_0_bb10
.L__sysy_par_body_0_bb9:
	mov x9, x13
	mov x10, x14
	mov w11, w8
.L__sysy_par_body_0_bb1:
	cmp w11, w12
	b.gt .L__sysy_par_body_0_bb11
.L__sysy_par_body_0_bb2:
	ldp q17, q16, [x10]
	stp q17, q16, [x9]
	add w11, w11, #8
	add x10, x10, #32
	add x9, x9, #32
	b .L__sysy_par_body_0_bb1
.L__sysy_par_body_0_bb3:
	add x12, x17, w14, sxtw #2
	add x15, x15, w14, sxtw #2
	sub w17, w16, #3
	orr w13, wzr, #0x80000003
.L__sysy_par_body_0_bb4:
	cmp w14, w17
	cset w10, lt
	cmp w16, w13
	cset w9, ge
	and w9, w9, w10
	cbz w9, .L__sysy_par_body_0_bb12
.L__sysy_par_body_0_bb8:
	ldr w9, [x12]
	str w9, [x15]
	ldr w9, [x12, #4]
	str w9, [x15, #4]
	ldr w9, [x12, #8]
	str w9, [x15, #8]
	ldr w9, [x12, #12]
	add x10, x15, #4
	add x11, x12, #4
	add x10, x10, #4
	add x11, x11, #4
	str w9, [x15, #12]
	add x10, x10, #4
	add x12, x11, #4
	add w14, w14, #4
	add x15, x10, #4
	add x12, x12, #4
	b .L__sysy_par_body_0_bb4
.L__sysy_par_body_0_bb5:
	cmp w11, w16
	b.ge .L__sysy_par_body_0_bb6
.L__sysy_par_body_0_bb7:
	ldr w9, [x12], #4
	add w11, w11, #1
	str w9, [x10], #4
	b .L__sysy_par_body_0_bb5
.L__sysy_par_body_0_bb6:
	ret
.L__sysy_par_body_0_bb10:
	mov w14, w8
	b .L__sysy_par_body_0_bb3
.L__sysy_par_body_0_bb11:
	mov w14, w11
	b .L__sysy_par_body_0_bb3
.L__sysy_par_body_0_bb12:
	mov x10, x15
	mov w11, w14
	b .L__sysy_par_body_0_bb5
	.size __sysy_par_body_0, .-__sysy_par_body_0
	.bss
	.global temp
	.p2align 4
temp:
	.zero 8388608
	.global w
	.p2align 4
w:
	.zero 8388608
	.global dst
	.p2align 4
dst:
	.zero 8388608
	.global __sysy_par_ctx_0_0
	.p2align 3
__sysy_par_ctx_0_0:
	.zero 8
	.global __sysy_par_ctx_0_1
	.p2align 3
__sysy_par_ctx_0_1:
	.zero 8

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
