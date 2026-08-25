	.arch armv8-a
	.text
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #32
	stp x19, x20, [sp]
	movz w20, #0
	str d8, [sp, #24]
	fmov s8, w20
	str x21, [sp, #16]
	bl getint
	mov w19, w0
	bl getint
	mov w21, w0
	movz w0, #53
	bl _sysy_starttime
	adrp x10, __sysy_par_ctx_0_0
	adrp x9, __sysy_par_ctx_0_1
	movz w1, #0
	str w19, [x10, :lo12:__sysy_par_ctx_0_0]
	mov w0, w1
	str w21, [x9, :lo12:__sysy_par_ctx_0_1]
	mov w2, w19
	bl __sysy_parallel_for
	mov w14, w20
.Lmain_bb1:
	cmp w14, #1000
	b.ge .Lmain_bb16
.Lmain_bb22:
	mov w13, w20
.Lmain_bb2:
	cmp w13, w19
	b.ge .Lmain_bb15
.Lmain_bb3:
	add w12, w13, #1
	cmp w12, w19
	b.ge .Lmain_bb26
.Lmain_bb4:
	adrp x9, A
	add x9, x9, :lo12:A
	movz w10, #516
	smaddl x9, w12, w10, x9
	add x17, x9, w13, sxtw #2
	mov w11, w12
	mov w15, w13
	movz w16, #0
.Lmain_bb5:
	ldr s17, [x17]
	adrp x9, A
	fmov s18, w16
	add x9, x9, :lo12:A
	smaddl x9, w15, w10, x9
	fsub s16, s18, s17
	fcmp s17, #0.0
	ldr s19, [x9, w13, sxtw #2]
	fcsel s17, s16, s17, lt
	fsub s16, s18, s19
	fcmp s19, #0.0
	fcsel s16, s16, s19, lt
	fcmp s17, s16
	csel w15, w11, w15, hi
	add w11, w11, #1
	cmp w11, w19
	add x17, x17, #516
	b.lt .Lmain_bb5
.Lmain_bb6:
	cmp w19, #0
	b.lt .Lmain_bb9
.Lmain_bb7:
	adrp x10, A
	adrp x9, A
	add x11, x10, :lo12:A
	add x9, x9, :lo12:A
	movz w10, #516
	smaddl x9, w15, w10, x9
	smaddl x11, w13, w10, x11
	mov x15, x9
	mov x10, x11
	mov w9, w20
.Lmain_bb8:
	ldr s17, [x10]
	ldr s16, [x15]
	str s16, [x10], #4
	add w11, w9, #1
	str s17, [x15], #4
	cmp w9, w19
	b.ge .Lmain_bb9
.Lmain_bb28:
	mov w9, w11
	b .Lmain_bb8
.Lmain_bb9:
	cmp w12, w19
	b.ge .Lmain_bb23
.Lmain_bb10:
	adrp x9, A
	add x9, x9, :lo12:A
	movz w10, #516
	smaddl x9, w13, w10, x9
	add x21, x9, w13, sxtw #2
	mov w17, w12
	b .Lmain_bb11
.Lmain_bb14:
	add w17, w17, #1
	cmp w17, w19
	b.ge .Lmain_bb24
.Lmain_bb11:
	ldr s17, [x21]
	fcmp s17, #0.0
	b.eq .Lmain_bb14
.Lmain_bb12:
	adrp x9, A
	add x9, x9, :lo12:A
	smaddl x9, w17, w10, x9
	add x11, x9, w13, sxtw #2
	ldr s16, [x11]
	fdiv s18, s16, s17
	cmp w13, w19
	b.gt .Lmain_bb14
.Lmain_bb30:
	mov x16, x21
	mov w9, w13
.Lmain_bb13:
	ldr s16, [x16], #4
	ldr s17, [x11]
	fmul s16, s18, s16
	fsub s16, s17, s16
	str s16, [x11], #4
	add w15, w9, #1
	cmp w9, w19
	b.ge .Lmain_bb14
.Lmain_bb31:
	mov w9, w15
	b .Lmain_bb13
.Lmain_bb24:
	mov w13, w12
	b .Lmain_bb2
.Lmain_bb15:
	add w14, w14, #1
	b .Lmain_bb1
.Lmain_bb16:
	adrp x9, A
	add x9, x9, :lo12:A
	sub w13, w19, #3
	mov x12, x9
	orr w11, wzr, #0x80000003
.Lmain_bb17:
	cmp w20, w13
	cset w10, lt
	cmp w19, w11
	cset w9, ge
	and w9, w9, w10
	cbz w9, .Lmain_bb32
.Lmain_bb21:
	ldr s17, [x12]
	ldr s16, [x12, #520]
	fadd s18, s8, s17
	ldr s17, [x12, #1040]
	fadd s18, s18, s16
	ldr s16, [x12, #1560]
	fadd s17, s18, s17
	add x9, x12, #520
	fadd s8, s17, s16
	add x9, x9, #520
	add x9, x9, #520
	add w20, w20, #4
	add x12, x9, #520
	b .Lmain_bb17
.Lmain_bb18:
	cmp w10, w19
	b.ge .Lmain_bb20
.Lmain_bb19:
	ldr s16, [x9]
	fadd s8, s8, s16
	add w10, w10, #1
	add x9, x9, #520
	b .Lmain_bb18
.Lmain_bb20:
	movz w0, #78
	bl _sysy_stoptime
	movz w9, #15811
	movk w9, #17947, lsl #16
	fmov s16, w9
	fsub s17, s8, s16
	movz w9, #0
	fmov s16, w9
	fsub s16, s16, s17
	fcmp s17, #0.0
	fcsel s17, s16, s17, lt
	movz w9, #16544, lsl #16
	fmov s16, w9
	fcmp s17, s16
	cset w0, le
	bl putint
	movz w0, #10
	bl putch
	ldp x20, x21, [sp, #8]
	ldr d8, [sp, #24]
	ldr x19, [sp]
	movz w0, #0
	add sp, sp, #32
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb23:
	mov w13, w12
	b .Lmain_bb2
.Lmain_bb26:
	mov w15, w13
	b .Lmain_bb6
.Lmain_bb32:
	mov x9, x12
	mov w10, w20
	b .Lmain_bb18
	.size main, .-main
	.p2align 2
	.global __sysy_par_body_0
	.type __sysy_par_body_0, %function
__sysy_par_body_0:
	adrp x10, __sysy_par_ctx_0_0
	adrp x9, __sysy_par_ctx_0_1
	ldr w6, [x10, :lo12:__sysy_par_ctx_0_0]
	ldr w8, [x9, :lo12:__sysy_par_ctx_0_1]
	mov w17, w0
	mov w7, w1
	movz w5, #0
.L__sysy_par_body_0_bb1:
	cmp w17, w7
	b.ge .L__sysy_par_body_0_bb2
.L__sysy_par_body_0_bb3:
	cmp w6, #0
	b.lt .L__sysy_par_body_0_bb6
.L__sysy_par_body_0_bb4:
	adrp x9, A
	add x10, x9, :lo12:A
	movz w9, #516
	smaddl x9, w17, w9, x10
	movz w10, #34079
	movz w16, #29
	mov x15, x9
	mov w14, w5
	movz w13, #23
	movz w12, #100
	movk w10, #20971, lsl #16
.L__sysy_par_body_0_bb5:
	mul w9, w14, w13
	madd w9, w17, w16, w9
	add w11, w9, w8
	smull x9, w11, w10
	asr x9, x9, #37
	add w9, w9, w9, lsr #31
	msub w9, w9, w12, w11
	add w9, w9, #1
	scvtf s16, w9
	str s16, [x15], #4
	add w11, w14, #1
	cmp w14, w6
	b.ge .L__sysy_par_body_0_bb6
.L__sysy_par_body_0_bb7:
	mov w14, w11
	b .L__sysy_par_body_0_bb5
.L__sysy_par_body_0_bb2:
	ret
.L__sysy_par_body_0_bb6:
	add w17, w17, #1
	b .L__sysy_par_body_0_bb1
	.size __sysy_par_body_0, .-__sysy_par_body_0
	.bss
	.global A
	.p2align 4
A:
	.zero 66048
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
