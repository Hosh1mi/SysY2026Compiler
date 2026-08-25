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
	str d8, [sp, #72]
	fmov s8, w20
	stp x21, x22, [sp, #16]
	stp x23, x24, [sp, #32]
	stp x25, x26, [sp, #48]
	str x27, [sp, #64]
	bl getint
	mov w19, w0
	mov w22, w20
.Lmain_bb1:
	cmp w22, w19
	b.ge .Lmain_bb44
.Lmain_bb40:
	adrp x9, A
	add x10, x9, :lo12:A
	sxtw x9, w22
	add x9, x10, x9, lsl #12
	mov x23, x9
	mov w21, w20
.Lmain_bb41:
	cmp w21, w19
	b.ge .Lmain_bb43
.Lmain_bb42:
	bl getfloat
	str s0, [x23], #4
	add w21, w21, #1
	b .Lmain_bb41
.Lmain_bb2:
	cmp w22, w19
	b.ge .Lmain_bb7
.Lmain_bb3:
	adrp x9, C
	add x10, x9, :lo12:C
	sxtw x9, w22
	add x9, x10, x9, lsl #12
	mov x23, x9
	mov w21, w20
.Lmain_bb4:
	cmp w21, w19
	b.ge .Lmain_bb6
.Lmain_bb5:
	bl getfloat
	str s0, [x23], #4
	add w21, w21, #1
	b .Lmain_bb4
.Lmain_bb6:
	add w22, w22, #1
	b .Lmain_bb2
.Lmain_bb7:
	movz w0, #55
	bl _sysy_starttime
	adrp x10, A
	adrp x9, B
	add x23, x10, :lo12:A
	add x22, x9, :lo12:B
	mov w21, w20
.Lmain_bb8:
	cmp w21, #5
	b.ge .Lmain_bb31
.Lmain_bb9:
	adrp x9, __sysy_par_ctx_0_0
	movz w1, #0
	str w19, [x9, :lo12:__sysy_par_ctx_0_0]
	mov w0, w1
	mov w2, w19
	bl __sysy_parallel_for
	mov w12, w20
.Lmain_bb10:
	cmp w12, w19
	b.ge .Lmain_bb30
.Lmain_bb11:
	sxtw x9, w12
	add x10, x23, x9, lsl #12
	add x10, x10, w12, sxtw #2
	ldr s22, [x10]
	dup v19.4s, v22.s[0]
	fmov v18.4s, #1
	add x14, x22, x9, lsl #12
	cmp w19, #15
	sub w13, w19, #8
	b.le .Lmain_bb48
.Lmain_bb47:
	mov x9, x14
	mov w10, w20
.Lmain_bb12:
	cmp w10, w13
	b.gt .Lmain_bb49
.Lmain_bb13:
	ldp q17, q16, [x9]
	fdiv v17.4s, v17.4s, v19.4s
	fdiv v16.4s, v16.4s, v19.4s
	fadd v17.4s, v17.4s, v18.4s
	fadd v16.4s, v16.4s, v18.4s
	stp q17, q16, [x9]
	add w10, w10, #8
	add x9, x9, #32
	b .Lmain_bb12
.Lmain_bb14:
	sxtw x9, w12
	add x9, x22, x9, lsl #12
	add x24, x9, w17, sxtw #2
	sub w15, w19, #3
	orr w16, wzr, #0x80000003
	movz w9, #16256, lsl #16
.Lmain_bb15:
	cmp w17, w15
	cset w11, lt
	cmp w19, w16
	cset w10, ge
	and w10, w10, w11
	cbz w10, .Lmain_bb50
.Lmain_bb29:
	ldr s19, [x24]
	fdiv s20, s19, s22
	ldr s18, [x24, #4]
	ldr s17, [x24, #8]
	ldr s16, [x24, #12]
	fdiv s19, s18, s22
	fdiv s17, s17, s22
	fdiv s16, s16, s22
	fmov s21, w9
	fmov s18, w9
	fadd s20, s20, s21
	fadd s19, s19, s21
	fadd s17, s17, s18
	fadd s16, s16, s18
	add x10, x24, #4
	add x10, x10, #4
	add x10, x10, #4
	str s20, [x24]
	add x10, x10, #4
	str s19, [x24, #4]
	str s17, [x24, #8]
	str s16, [x24, #12]
	add w17, w17, #4
	mov x24, x10
	b .Lmain_bb15
.Lmain_bb16:
	cmp w11, w19
	b.ge .Lmain_bb18
.Lmain_bb17:
	ldr s16, [x10]
	fdiv s17, s16, s22
	fmov s16, w9
	fadd s16, s17, s16
	str s16, [x10], #4
	add w11, w11, #1
	b .Lmain_bb16
.Lmain_bb18:
	add w16, w12, #1
	cmp w16, w19
	b.ge .Lmain_bb45
.Lmain_bb19:
	sxtw x9, w12
	add x24, x22, x9, lsl #12
	mov w17, w16
	b .Lmain_bb20
.Lmain_bb27:
	add w17, w17, #1
	cmp w17, w19
	b.ge .Lmain_bb46
.Lmain_bb20:
	sxtw x9, w17
	add x10, x23, x9, lsl #12
	add x10, x10, w12, sxtw #2
	ldr s21, [x10]
	cmp w19, #15
	add x26, x22, x9, lsl #12
	add x10, x14, w19, sxtw #2
	cset w25, gt
	dup v20.4s, v21.s[0]
	cmp x10, x26
	add x9, x26, w19, sxtw #2
	cset w10, ls
	cmp x9, x14
	cset w9, ls
	orr w9, w10, w9
	and w9, w25, w9
	cbz w9, .Lmain_bb53
.Lmain_bb52:
	mov x9, x14
	mov x10, x26
	mov w11, w20
.Lmain_bb21:
	cmp w11, w13
	b.gt .Lmain_bb23
.Lmain_bb22:
	ldp q17, q16, [x9]
	fmul v17.4s, v20.4s, v17.4s
	fmul v16.4s, v20.4s, v16.4s
	ldp q19, q18, [x10]
	fsub v17.4s, v19.4s, v17.4s
	fsub v16.4s, v18.4s, v16.4s
	stp q17, q16, [x10]
	add w11, w11, #8
	add x10, x10, #32
	add x9, x9, #32
	b .Lmain_bb21
.Lmain_bb23:
	sxtw x9, w17
	add x10, x22, x9, lsl #12
	add x26, x10, w11, sxtw #2
	add x27, x24, w11, sxtw #2
	orr w25, wzr, #0x80000003
.Lmain_bb24:
	cmp w11, w15
	cset w10, lt
	cmp w19, w25
	cset w9, ge
	and w9, w9, w10
	cbz w9, .Lmain_bb55
.Lmain_bb28:
	ldr s16, [x27]
	ldr s17, [x26]
	fmul s16, s21, s16
	fsub s16, s17, s16
	str s16, [x26]
	ldr s16, [x27, #4]
	ldr s17, [x26, #4]
	fmul s19, s21, s16
	fsub s16, s17, s19
	str s16, [x26, #4]
	ldr s16, [x27, #8]
	ldr s17, [x26, #8]
	fmul s16, s21, s16
	fsub s16, s17, s16
	str s16, [x26, #8]
	ldr s16, [x27, #12]
	ldr s17, [x26, #12]
	fmul s19, s21, s16
	fsub s16, s17, s19
	add x10, x26, #4
	add x9, x27, #4
	add x10, x10, #4
	add x9, x9, #4
	str s16, [x26, #12]
	add x10, x10, #4
	add x9, x9, #4
	add w11, w11, #4
	add x26, x10, #4
	add x27, x9, #4
	b .Lmain_bb24
.Lmain_bb25:
	cmp w11, w19
	b.ge .Lmain_bb27
.Lmain_bb26:
	ldr s16, [x9], #4
	ldr s17, [x10]
	fmul s16, s21, s16
	fsub s16, s17, s16
	str s16, [x10], #4
	add w11, w11, #1
	b .Lmain_bb25
.Lmain_bb46:
	mov w12, w16
	b .Lmain_bb10
.Lmain_bb30:
	add w21, w21, #1
	b .Lmain_bb8
.Lmain_bb31:
	movz w0, #70
	bl _sysy_stoptime
	fmov s17, s8
	mov w15, w20
.Lmain_bb32:
	cmp w15, w19
	b.ge .Lmain_bb39
.Lmain_bb33:
	adrp x9, B
	add x10, x9, :lo12:B
	sxtw x9, w15
	add x9, x10, x9, lsl #12
	sub w14, w19, #3
	mov x13, x9
	mov w12, w20
	orr w11, wzr, #0x80000003
.Lmain_bb34:
	cmp w12, w14
	cset w10, lt
	cmp w19, w11
	cset w9, ge
	and w9, w9, w10
	cbz w9, .Lmain_bb56
.Lmain_bb38:
	ldp s16, s18, [x13]
	fadd s19, s17, s16
	ldp s17, s16, [x13, #8]
	fadd s18, s19, s18
	fadd s17, s18, s17
	add x9, x13, #4
	fadd s17, s17, s16
	add x9, x9, #4
	add x9, x9, #4
	add w12, w12, #4
	add x13, x9, #4
	b .Lmain_bb34
.Lmain_bb35:
	cmp w10, w19
	b.ge .Lmain_bb37
.Lmain_bb36:
	ldr s16, [x9], #4
	fadd s17, s17, s16
	add w10, w10, #1
	b .Lmain_bb35
.Lmain_bb37:
	add w15, w15, #1
	b .Lmain_bb32
.Lmain_bb39:
	fmov s0, s17
	bl putfloat
	movz w0, #10
	bl putch
	ldp x26, x27, [sp, #56]
	ldp x24, x25, [sp, #40]
	ldp x22, x23, [sp, #24]
	ldp x20, x21, [sp, #8]
	ldr d8, [sp, #72]
	ldr x19, [sp]
	movz w0, #0
	add sp, sp, #80
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb43:
	add w22, w22, #1
	b .Lmain_bb1
.Lmain_bb44:
	mov w22, w20
	b .Lmain_bb2
.Lmain_bb45:
	mov w12, w16
	b .Lmain_bb10
.Lmain_bb48:
	mov w17, w20
	b .Lmain_bb14
.Lmain_bb49:
	mov w17, w10
	b .Lmain_bb14
.Lmain_bb50:
	mov x10, x24
	mov w11, w17
	movz w9, #16256, lsl #16
	b .Lmain_bb16
.Lmain_bb53:
	mov w11, w20
	b .Lmain_bb23
.Lmain_bb55:
	mov x9, x27
	mov x10, x26
	b .Lmain_bb25
.Lmain_bb56:
	mov x9, x13
	mov w10, w12
	b .Lmain_bb35
	.size main, .-main
	.p2align 2
	.global __sysy_par_body_0
	.type __sysy_par_body_0, %function
__sysy_par_body_0:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #48
	stp x19, x20, [sp]
	adrp x10, B
	stp x21, x22, [sp, #16]
	adrp x12, __sysy_par_ctx_0_0
	stp x23, x24, [sp, #32]
	ldr w21, [x12, :lo12:__sysy_par_ctx_0_0]
	mov w22, w0
	adrp x9, C
	add x11, x10, :lo12:B
	add x9, x9, :lo12:C
	sxtw x10, w22
	add x11, x11, x10, lsl #12
	add x9, x9, x10, lsl #12
	mov w20, w1
	mov x24, x9
	mov x23, x11
	movz x19, #4096
.L__sysy_par_body_0_bb1:
	cmp w22, w20
	b.ge .L__sysy_par_body_0_bb2
.L__sysy_par_body_0_bb3:
	lsl w2, w21, #2
	mov x0, x23
	mov x1, x24
	bl memcpy
	add w22, w22, #1
	add x23, x23, x19
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
