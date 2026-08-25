	.arch armv8-a
	.text
	.p2align 2
	.global step
	.type step, %function
step:
	adrp x9, height
	ldr w2, [x9, :lo12:height]
	adrp x9, width
	ldr w12, [x9, :lo12:width]
	mov x14, x0
	mov x13, x1
	cmp w2, #1
	b.ge .Lstep_bb2
.Lstep_bb1:
	adrp x10, height
	adrp x9, width
	str w2, [x10, :lo12:height]
	str w12, [x9, :lo12:width]
	ret
.Lstep_bb2:
	adrp x11, __sysy_par_ctx_0_0
	adrp x10, __sysy_par_ctx_0_1
	adrp x9, __sysy_par_ctx_0_2
	str w12, [x11, :lo12:__sysy_par_ctx_0_0]
	movz w0, #0
	str x14, [x10, :lo12:__sysy_par_ctx_0_1]
	movz w1, #1
	str x13, [x9, :lo12:__sysy_par_ctx_0_2]
	b __sysy_parallel_for
	.size step, .-step
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #80
	stp x19, x20, [sp]
	adrp x9, active
	stp x21, x22, [sp, #16]
	stp x23, x24, [sp, #32]
	stp x25, x26, [sp, #48]
	str x27, [sp, #64]
	ldr w20, [x9, :lo12:active]
	movz w21, #1
	movz w22, #2
	bl getint
	adrp x9, width
	str w0, [x9, :lo12:width]
	bl getint
	adrp x9, height
	str w0, [x9, :lo12:height]
	bl getint
	mov w19, w0
	bl getch
	mov w23, w21
.Lmain_bb1:
	adrp x9, height
	ldr w9, [x9, :lo12:height]
	cmp w23, w9
	b.gt .Lmain_bb9
.Lmain_bb2:
	adrp x9, sheet1
	add x10, x9, :lo12:sheet1
	movz w9, #2000
	smaddl x9, w23, w9, x10
	add x26, x9, #4
	mov w27, w21
	movz w25, #1
	movz w24, #0
.Lmain_bb3:
	adrp x9, width
	ldr w9, [x9, :lo12:width]
	cmp w27, w9
	b.gt .Lmain_bb8
.Lmain_bb4:
	bl getch
	cmp w0, #35
	b.eq .Lmain_bb5
.Lmain_bb6:
	str w24, [x26]
.Lmain_bb7:
	add w27, w27, #1
	add x26, x26, #4
	b .Lmain_bb3
.Lmain_bb5:
	str w25, [x26]
	b .Lmain_bb7
.Lmain_bb8:
	bl getch
	add w23, w23, #1
	b .Lmain_bb1
.Lmain_bb9:
	movz w0, #95
	bl _sysy_starttime
	adrp x10, sheet1
	adrp x9, sheet2
	add x23, x10, :lo12:sheet1
	add x24, x9, :lo12:sheet2
.Lmain_bb10:
	cmp w19, #0
	b.le .Lmain_bb15
.Lmain_bb11:
	cmp w20, #1
	b.eq .Lmain_bb12
.Lmain_bb13:
	mov x0, x24
	mov x1, x23
	bl step
	mov w20, w21
.Lmain_bb14:
	sub w19, w19, #1
	b .Lmain_bb10
.Lmain_bb12:
	mov x0, x23
	mov x1, x24
	bl step
	mov w20, w22
	b .Lmain_bb14
.Lmain_bb15:
	movz w0, #106
	bl _sysy_stoptime
	cmp w20, #2
	b.eq .Lmain_bb16
.Lmain_bb33:
	mov w22, w21
.Lmain_bb18:
	adrp x9, height
	ldr w9, [x9, :lo12:height]
	cmp w22, w9
	b.gt .Lmain_bb26
.Lmain_bb19:
	adrp x9, sheet1
	add x10, x9, :lo12:sheet1
	movz w9, #2000
	smaddl x9, w22, w9, x10
	add x25, x9, #4
	mov w26, w21
	movz w24, #35
	movz w23, #46
.Lmain_bb20:
	adrp x9, width
	ldr w9, [x9, :lo12:width]
	cmp w26, w9
	b.gt .Lmain_bb25
.Lmain_bb21:
	ldr w9, [x25]
	cmp w9, #1
	b.eq .Lmain_bb22
.Lmain_bb23:
	mov w0, w23
	bl putch
.Lmain_bb24:
	add w26, w26, #1
	add x25, x25, #4
	b .Lmain_bb20
.Lmain_bb16:
	adrp x10, height
	adrp x9, width
	ldr w14, [x10, :lo12:height]
	ldr w13, [x9, :lo12:width]
	mov w15, w21
.Lmain_bb17:
	cmp w15, w14
	b.gt .Lmain_bb34
.Lmain_bb27:
	adrp x10, sheet1
	adrp x9, sheet2
	add x11, x10, :lo12:sheet1
	add x9, x9, :lo12:sheet2
	movz w10, #2000
	smaddl x11, w15, w10, x11
	smaddl x9, w15, w10, x9
	add x22, x11, #4
	add x12, x9, #4
	sub w23, w13, #3
	mov w17, w21
	orr w16, wzr, #0x80000003
.Lmain_bb28:
	cmp w17, w23
	cset w10, le
	cmp w13, w16
	cset w9, ge
	and w9, w9, w10
	cbz w9, .Lmain_bb35
.Lmain_bb32:
	ldr w9, [x12]
	str w9, [x22]
	ldr w9, [x12, #4]
	str w9, [x22, #4]
	ldr w9, [x12, #8]
	str w9, [x22, #8]
	ldr w9, [x12, #12]
	add x10, x22, #4
	add x11, x12, #4
	add x10, x10, #4
	add x11, x11, #4
	str w9, [x22, #12]
	add x10, x10, #4
	add x12, x11, #4
	add w17, w17, #4
	add x22, x10, #4
	add x12, x12, #4
	b .Lmain_bb28
.Lmain_bb22:
	mov w0, w24
	bl putch
	b .Lmain_bb24
.Lmain_bb25:
	movz w0, #10
	bl putch
	add w22, w22, #1
	b .Lmain_bb18
.Lmain_bb26:
	adrp x10, steps
	adrp x9, active
	str w19, [x10, :lo12:steps]
	str w20, [x9, :lo12:active]
	ldp x26, x27, [sp, #56]
	ldp x24, x25, [sp, #40]
	ldp x22, x23, [sp, #24]
	ldp x20, x21, [sp, #8]
	ldr x19, [sp]
	movz w0, #0
	add sp, sp, #80
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb29:
	cmp w11, w13
	b.gt .Lmain_bb31
.Lmain_bb30:
	ldr w9, [x12], #4
	add w11, w11, #1
	str w9, [x10], #4
	b .Lmain_bb29
.Lmain_bb31:
	add w15, w15, #1
	b .Lmain_bb17
.Lmain_bb34:
	mov w22, w21
	b .Lmain_bb18
.Lmain_bb35:
	mov x10, x22
	mov w11, w17
	b .Lmain_bb29
	.size main, .-main
	.p2align 2
	.global __sysy_par_body_0
	.type __sysy_par_body_0, %function
__sysy_par_body_0:
	sub sp, sp, #64
	mov w4, w0
	stp x19, x20, [sp]
	mov w0, w1
	stp x21, x22, [sp, #16]
	adrp x11, __sysy_par_ctx_0_0
	stp x23, x24, [sp, #32]
	adrp x10, __sysy_par_ctx_0_1
	stp x25, x26, [sp, #48]
	adrp x9, __sysy_par_ctx_0_2
	ldr w1, [x11, :lo12:__sysy_par_ctx_0_0]
	ldr x2, [x10, :lo12:__sysy_par_ctx_0_1]
	ldr x3, [x9, :lo12:__sysy_par_ctx_0_2]
	movz w19, #1
.L__sysy_par_body_0_bb1:
	cmp w1, #1
	b.lt .L__sysy_par_body_0_bb10
.L__sysy_par_body_0_bb2:
	movn w9, #0
	add w11, w4, w9
	movz w9, #2000
	smaddl x13, w11, w9, x2
	add w10, w4, #1
	smaddl x11, w10, w9, x2
	smaddl x12, w4, w9, x2
	smaddl x10, w4, w9, x2
	smaddl x9, w4, w9, x3
	add x17, x13, #4
	add x16, x13, #8
	add x25, x12, #8
	add x23, x11, #4
	add x22, x11, #8
	add x21, x10, #4
	add x20, x9, #4
	mov x24, x11
	mov x15, x12
	mov x8, x13
	mov w14, w19
	movz w5, #1
	movz w6, #0
.L__sysy_par_body_0_bb3:
	ldr w11, [x8]
	ldr w10, [x17]
	ldr w9, [x16]
	ldr w7, [x15]
	ldr w13, [x25]
	ldr w12, [x24]
	add w10, w11, w10
	ldr w11, [x23]
	add w26, w10, w9
	ldr w10, [x22]
	ldr w9, [x21]
	add w7, w26, w7
	add w13, w7, w13
	add w12, w13, w12
	add w11, w12, w11
	add w10, w11, w10
	cmp w9, #1
	b.eq .L__sysy_par_body_0_bb4
.L__sysy_par_body_0_bb6:
	cmp w10, #3
	b.eq .L__sysy_par_body_0_bb7
.L__sysy_par_body_0_bb8:
	str w6, [x20]
.L__sysy_par_body_0_bb9:
	add w7, w14, #1
	cmp w14, w1
	add x8, x8, #4
	add x17, x17, #4
	add x16, x16, #4
	add x15, x15, #4
	add x25, x25, #4
	add x24, x24, #4
	add x23, x23, #4
	add x22, x22, #4
	add x21, x21, #4
	add x20, x20, #4
	b.ge .L__sysy_par_body_0_bb10
.L__sysy_par_body_0_bb13:
	mov w14, w7
	b .L__sysy_par_body_0_bb3
.L__sysy_par_body_0_bb4:
	cmp w10, #2
	b.ne .L__sysy_par_body_0_bb6
.L__sysy_par_body_0_bb5:
	str w5, [x20]
	b .L__sysy_par_body_0_bb9
.L__sysy_par_body_0_bb7:
	str w5, [x20]
	b .L__sysy_par_body_0_bb9
.L__sysy_par_body_0_bb10:
	add w9, w4, #1
	cmp w4, w0
	b.ge .L__sysy_par_body_0_bb11
.L__sysy_par_body_0_bb12:
	mov w4, w9
	b .L__sysy_par_body_0_bb1
.L__sysy_par_body_0_bb11:
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	add sp, sp, #64
	ret
	.size __sysy_par_body_0, .-__sysy_par_body_0
	.data
	.global active
	.p2align 2
active:
	.word 1
	.global width
	.p2align 2
width:
	.zero 4
	.global height
	.p2align 2
height:
	.zero 4
	.global steps
	.p2align 2
steps:
	.zero 4
	.bss
	.global sheet1
	.p2align 4
sheet1:
	.zero 1000000
	.global sheet2
	.p2align 4
sheet2:
	.zero 1000000
	.global __sysy_par_ctx_0_0
	.p2align 2
__sysy_par_ctx_0_0:
	.zero 4
	.global __sysy_par_ctx_0_1
	.p2align 3
__sysy_par_ctx_0_1:
	.zero 8
	.global __sysy_par_ctx_0_2
	.p2align 3
__sysy_par_ctx_0_2:
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
