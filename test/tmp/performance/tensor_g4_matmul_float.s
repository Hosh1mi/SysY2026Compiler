	.arch armv8-a
	.text
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #729, lsl #12
	sub sp, sp, #48
	stp x19, x20, [sp]
	movz w20, #0
	str d8, [sp, #32]
	fmov s8, w20
	stp x21, x22, [sp, #16]
	add x21, sp, #48
	bl getint
	mov w19, w0
	adrp x9, __sysy_par_ctx_0_0
	movz w1, #0
	str w19, [x9, :lo12:__sysy_par_ctx_0_0]
	movz w2, #864
	mov w0, w1
	bl __sysy_parallel_for
	movz w0, #25
	bl _sysy_starttime
	adrp x10, left
	adrp x9, right
	add x14, x10, :lo12:left
	add x13, x9, :lo12:right
	mov x1, x21
	mov w15, w20
.Lmain_bb1:
	cmp w15, #864
	b.ge .Lmain_bb11
.Lmain_bb2:
	movz w9, #864
	mul w12, w15, w9
	mov x17, x13
	add x16, x1, w12, sxtw #2
	mov w21, w20
.Lmain_bb3:
	cmp w21, #864
	b.ge .Lmain_bb10
.Lmain_bb4:
	add x10, x14, w12, sxtw #2
	mov x9, x17
	fmov s22, s8
	mov w11, w20
.Lmain_bb5:
	cmp w11, #861
	b.ge .Lmain_bb30
.Lmain_bb9:
	ldp s18, s20, [x10]
	ldr s17, [x9]
	fmul s21, s18, s17
	ldr s16, [x9, #3456]
	ldp s19, s17, [x10, #8]
	ldr s18, [x9, #6912]
	fadd s21, s22, s21
	fmul s20, s20, s16
	ldr s16, [x9, #10368]
	fadd s20, s21, s20
	fmul s18, s19, s18
	fadd s18, s20, s18
	fmul s19, s17, s16
	add x10, x10, #4
	fadd s22, s18, s19
	add x9, x9, #3456
	add x10, x10, #4
	add x9, x9, #3456
	add x10, x10, #4
	add x9, x9, #3456
	add w11, w11, #4
	add x10, x10, #4
	add x9, x9, #3456
	b .Lmain_bb5
.Lmain_bb6:
	cmp w11, #864
	b.ge .Lmain_bb8
.Lmain_bb7:
	ldr s17, [x10], #4
	ldr s16, [x9]
	fmul s16, s17, s16
	fadd s18, s18, s16
	add w11, w11, #1
	add x9, x9, #3456
	b .Lmain_bb6
.Lmain_bb8:
	str s18, [x16], #4
	add w21, w21, #1
	add x17, x17, #4
	b .Lmain_bb3
.Lmain_bb10:
	add w15, w15, #1
	b .Lmain_bb1
.Lmain_bb11:
	adrp x9, result
	movz w2, #36864
	add x0, x9, :lo12:result
	movk w2, #45, lsl #16
	bl memcpy
	mov w16, w20
	mov w14, w20
.Lmain_bb12:
	cmp w14, #863
	b.ge .Lmain_bb20
.Lmain_bb13:
	movz w9, #9363
	add w11, w14, w19
	movk w9, #37449, lsl #16
	smull x9, w11, w9
	asr x9, x9, #32
	add w9, w9, w11
	asr w9, w9, #2
	movz w10, #7
	add w9, w9, w9, lsr #31
	msub w9, w9, w10, w11
	add w9, w9, #1
	scvtf s17, w9
	movz w9, #16000, lsl #16
	fmov s16, w9
	fmul s17, s17, s16
	movz w10, #17496, lsl #16
	fmov s16, w10
	adrp x9, result
	add x9, x9, :lo12:result
	mov x10, x9
	movz w9, #3456
	smaddl x9, w14, w9, x10
	fmul s18, s17, s16
	mov x22, x9
	movz w13, #26215
	movz w9, #55050
	mov w21, w20
	movz w17, #5
	movk w13, #26214, lsl #16
	movz w11, #16128, lsl #16
	movz w10, #0
	movk w9, #15395, lsl #16
.Lmain_bb14:
	cmp w21, #864
	b.ge .Lmain_bb16
.Lmain_bb15:
	add w15, w21, w19
	smull x12, w15, w13
	asr x12, x12, #33
	add w12, w12, w12, lsr #31
	msub w12, w12, w17, w15
	add w12, w12, #1
	scvtf s17, w12
	fmov s16, w11
	fmul s17, s17, s16
	ldr s16, [x22], #4
	fmul s20, s18, s17
	fsub s17, s16, s20
	fmov s16, w10
	fsub s16, s16, s17
	fcmp s17, #0.0
	fcsel s17, s16, s17, lt
	fmov s16, w9
	fcmp s17, s16
	add w12, w16, #1
	csel w16, w12, w16, hi
	add w21, w21, #1
	b .Lmain_bb14
.Lmain_bb16:
	add w12, w14, #1
	movz w9, #9363
	add w11, w12, w19
	movk w9, #37449, lsl #16
	smull x9, w11, w9
	asr x9, x9, #32
	add w9, w9, w11
	asr w9, w9, #2
	movz w10, #7
	add w9, w9, w9, lsr #31
	msub w9, w9, w10, w11
	add w9, w9, #1
	scvtf s17, w9
	movz w9, #16000, lsl #16
	fmov s16, w9
	fmul s17, s17, s16
	movz w10, #17496, lsl #16
	fmov s16, w10
	adrp x9, result
	add x9, x9, :lo12:result
	mov x10, x9
	movz w9, #3456
	smaddl x9, w12, w9, x10
	fmul s18, s17, s16
	mov x22, x9
	movz w13, #26215
	movz w9, #55050
	mov w21, w20
	movz w17, #5
	movk w13, #26214, lsl #16
	movz w11, #16128, lsl #16
	movz w10, #0
	movk w9, #15395, lsl #16
.Lmain_bb17:
	cmp w21, #864
	b.ge .Lmain_bb19
.Lmain_bb18:
	add w15, w21, w19
	smull x12, w15, w13
	asr x12, x12, #33
	add w12, w12, w12, lsr #31
	msub w12, w12, w17, w15
	add w12, w12, #1
	scvtf s17, w12
	fmov s16, w11
	fmul s17, s17, s16
	ldr s16, [x22], #4
	fmul s20, s18, s17
	fsub s17, s16, s20
	fmov s16, w10
	fsub s16, s16, s17
	fcmp s17, #0.0
	fcsel s17, s16, s17, lt
	fmov s16, w9
	fcmp s17, s16
	add w12, w16, #1
	csel w16, w12, w16, hi
	add w21, w21, #1
	b .Lmain_bb17
.Lmain_bb19:
	add w14, w14, #2
	b .Lmain_bb12
.Lmain_bb20:
	cmp w14, #864
	b.ge .Lmain_bb32
.Lmain_bb31:
	mov w15, w16
	mov w16, w14
.Lmain_bb21:
	cmp w16, #864
	b.ge .Lmain_bb33
.Lmain_bb22:
	movz w9, #9363
	add w11, w16, w19
	movk w9, #37449, lsl #16
	smull x9, w11, w9
	asr x9, x9, #32
	add w9, w9, w11
	asr w9, w9, #2
	movz w10, #7
	add w9, w9, w9, lsr #31
	msub w9, w9, w10, w11
	add w9, w9, #1
	scvtf s17, w9
	movz w9, #16000, lsl #16
	fmov s16, w9
	fmul s17, s17, s16
	movz w10, #17496, lsl #16
	fmov s16, w10
	adrp x9, result
	add x9, x9, :lo12:result
	mov x10, x9
	movz w9, #3456
	smaddl x9, w16, w9, x10
	fmul s18, s17, s16
	mov x22, x9
	movz w13, #26215
	movz w9, #55050
	mov w21, w20
	movz w17, #5
	movk w13, #26214, lsl #16
	movz w11, #16128, lsl #16
	movz w10, #0
	movk w9, #15395, lsl #16
.Lmain_bb23:
	cmp w21, #864
	b.ge .Lmain_bb25
.Lmain_bb24:
	add w14, w21, w19
	smull x12, w14, w13
	asr x12, x12, #33
	add w12, w12, w12, lsr #31
	msub w12, w12, w17, w14
	add w12, w12, #1
	scvtf s17, w12
	fmov s16, w11
	fmul s17, s17, s16
	ldr s16, [x22], #4
	fmul s20, s18, s17
	fsub s17, s16, s20
	fmov s16, w10
	fsub s16, s16, s17
	fcmp s17, #0.0
	fcsel s17, s16, s17, lt
	fmov s16, w9
	fcmp s17, s16
	add w12, w15, #1
	csel w15, w12, w15, hi
	add w21, w21, #1
	b .Lmain_bb23
.Lmain_bb25:
	add w16, w16, #1
	b .Lmain_bb21
.Lmain_bb26:
	movz w0, #42
	bl _sysy_stoptime
	cbz w19, .Lmain_bb27
.Lmain_bb28:
	movz w0, #0
	bl putint
.Lmain_bb29:
	movz w0, #10
	bl putch
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	ldr d8, [sp, #32]
	movz w0, #0
	add sp, sp, #729, lsl #12
	add sp, sp, #48
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb27:
	movz w0, #1
	bl putint
	b .Lmain_bb29
.Lmain_bb30:
	fmov s18, s22
	b .Lmain_bb6
.Lmain_bb32:
	mov w19, w16
	b .Lmain_bb26
.Lmain_bb33:
	mov w19, w15
	b .Lmain_bb26
	.size main, .-main
	.p2align 2
	.global __sysy_par_body_0
	.type __sysy_par_body_0, %function
__sysy_par_body_0:
	adrp x9, __sysy_par_ctx_0_0
	ldr w14, [x9, :lo12:__sysy_par_ctx_0_0]
	mov w17, w0
	mov w15, w1
	movz w16, #0
.L__sysy_par_body_0_bb1:
	cmp w17, w15
	b.ge .L__sysy_par_body_0_bb2
.L__sysy_par_body_0_bb3:
	movz w9, #9363
	add w11, w17, w14
	movk w9, #37449, lsl #16
	smull x9, w11, w9
	asr x9, x9, #32
	add w9, w9, w11
	asr w9, w9, #2
	movz w10, #7
	add w9, w9, w9, lsr #31
	msub w9, w9, w10, w11
	add w9, w9, #1
	scvtf s17, w9
	movz w11, #16000, lsl #16
	fmov s16, w11
	adrp x10, left
	add x10, x10, :lo12:left
	mov x11, x10
	adrp x9, right
	movz w10, #3456
	smaddl x11, w17, w10, x11
	add x9, x9, :lo12:right
	smaddl x9, w17, w10, x9
	fmul s18, s17, s16
	mov x7, x11
	mov x6, x9
	movz w11, #26215
	mov w13, w16
	movz w8, #5
	movk w11, #26214, lsl #16
	movz w9, #16128, lsl #16
.L__sysy_par_body_0_bb4:
	cmp w13, #864
	b.ge .L__sysy_par_body_0_bb6
.L__sysy_par_body_0_bb5:
	add w12, w13, w14
	smull x10, w12, w11
	asr x10, x10, #33
	add w10, w10, w10, lsr #31
	msub w10, w10, w8, w12
	add w10, w10, #1
	scvtf s17, w10
	fmov s16, w9
	fmul s16, s17, s16
	str s18, [x7], #4
	add w13, w13, #1
	str s16, [x6], #4
	b .L__sysy_par_body_0_bb4
.L__sysy_par_body_0_bb2:
	ret
.L__sysy_par_body_0_bb6:
	add w17, w17, #1
	b .L__sysy_par_body_0_bb1
	.size __sysy_par_body_0, .-__sysy_par_body_0
	.bss
	.global left
	.p2align 4
left:
	.zero 2985984
	.global right
	.p2align 4
right:
	.zero 2985984
	.global result
	.p2align 4
result:
	.zero 2985984
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
