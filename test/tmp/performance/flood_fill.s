	.arch armv8-a
	.text
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #112
	stp x19, x20, [sp]
	stp x21, x22, [sp, #16]
	add x22, sp, #80
	stp x23, x24, [sp, #32]
	add x21, sp, #96
	stp x25, x26, [sp, #48]
	movz w20, #0
	stp x27, x28, [sp, #64]
	movz w19, #1
	bl getint
	bl getint
	mov w23, w0
	movz w0, #49
	bl _sysy_starttime
	adrp x9, __sysy_par_ctx_0_0
	movz w1, #0
	str w23, [x9, :lo12:__sysy_par_ctx_0_0]
	movz w2, #250
	mov w0, w1
	bl __sysy_parallel_for
	adrp x10, queue_c
	adrp x12, grid
	adrp x11, queue_r
	adrp x9, visited
	add x6, x10, :lo12:queue_c
	add x12, x12, :lo12:grid
	add x4, x11, :lo12:queue_r
	add x9, x9, :lo12:visited
	movz x10, #60000
	mov x5, x4
	mov x7, x6
	add x12, x12, x10
	add x11, x9, x10
	mov w8, w20
	movz w28, #4500
.Lmain_bb1:
	cmp w8, w28
	b.ge .Lmain_bb28
.Lmain_bb13:
	ldr w9, [x12, #200]
	cmp w9, #3
	b.ne .Lmain_bb16
.Lmain_bb14:
	movz w10, #50
	str w10, [x4]
	movz w9, #1
	str w10, [x6]
	mov x27, x7
	str w9, [x11, #200]
	mov x26, x5
	mov w15, w19
	mov w25, w20
.Lmain_bb15:
	cmp w25, w15
	b.ge .Lmain_bb16
.Lmain_bb17:
	movi v16.4s, #0
	movn w9, #0
	mov v17.16b, v16.16b
	mov v17.s[0], w9
	movz w10, #0
	mov v16.s[0], w10
	movz w17, #1
	mov v17.s[1], w17
	mov v16.s[1], w10
	mov v17.s[2], w10
	mov v16.s[2], w9
	mov v17.s[3], w10
	ldr w24, [x26]
	adrp x9, grid
	ldr w23, [x27]
	add x9, x9, :lo12:grid
	mov v16.s[3], w17
	movz w10, #1200
	smaddl x9, w24, w10, x9
	movz w13, #10
	str w13, [x9, w23, sxtw #2]
	str q17, [x21]
	mov w16, w20
	str q16, [x22]
.Lmain_bb18:
	cmp w16, #4
	b.ge .Lmain_bb19
.Lmain_bb20:
	ldr w13, [x21, w16, sxtw #2]
	ldr w9, [x22, w16, sxtw #2]
	add w14, w24, w13
	cmp w14, #0
	b.lt .Lmain_bb27
.Lmain_bb21:
	cmp w14, #250
	b.ge .Lmain_bb27
.Lmain_bb22:
	add w13, w23, w9
	cmp w13, #0
	b.lt .Lmain_bb27
.Lmain_bb23:
	cmp w13, #250
	b.ge .Lmain_bb27
.Lmain_bb24:
	adrp x9, grid
	add x9, x9, :lo12:grid
	smaddl x9, w14, w10, x9
	ldr w9, [x9, w13, sxtw #2]
	cmp w9, #3
	b.eq .Lmain_bb25
.Lmain_bb27:
	add w16, w16, #1
	b .Lmain_bb18
.Lmain_bb2:
	cmp w14, #250
	b.ge .Lmain_bb12
.Lmain_bb3:
	movi v18.4s, #0
	adrp x9, grid
	mov v18.s[0], w19
	add x10, x9, :lo12:grid
	movz w9, #1200
	smaddl x9, w14, w9, x10
	mov x12, x9
	mov w15, w20
	orr w11, wzr, #0x7ffffff8
.Lmain_bb4:
	cmp w15, w11
	cset w10, le
	cmp w15, #243
	cset w9, lt
	and w9, w10, w9
	cbz w9, .Lmain_bb6
.Lmain_bb5:
	ldp q17, q16, [x12]
	add v17.4s, v18.4s, v17.4s
	add v18.4s, v17.4s, v16.4s
	add x9, x12, #16
	add w15, w15, #8
	add x12, x9, #16
	b .Lmain_bb4
.Lmain_bb6:
	addv s16, v18.4s
	adrp x9, grid
	add x10, x9, :lo12:grid
	movz w9, #1200
	smaddl x9, w14, w9, x10
	fmov w13, s16
	add x16, x9, w15, sxtw #2
.Lmain_bb7:
	cmp w15, #247
	b.ge .Lmain_bb29
.Lmain_bb11:
	ldp w12, w11, [x16]
	ldp w10, w9, [x16, #8]
	add w12, w13, w12
	add x13, x16, #4
	add w12, w12, w11
	add x11, x13, #4
	add w10, w12, w10
	add x12, x11, #4
	add w13, w10, w9
	add w15, w15, #4
	add x16, x12, #4
	b .Lmain_bb7
.Lmain_bb8:
	cmp w10, #250
	b.ge .Lmain_bb10
.Lmain_bb9:
	ldr w9, [x12], #4
	add w11, w11, w9
	add w10, w10, #1
	b .Lmain_bb8
.Lmain_bb10:
	add w14, w14, #1
	mov w19, w11
	b .Lmain_bb2
.Lmain_bb12:
	movz w0, #79
	bl _sysy_stoptime
	mov w0, w19
	bl putint
	movz w0, #10
	bl putch
	ldp x27, x28, [sp, #64]
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	movz w0, #0
	add sp, sp, #112
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb16:
	add w8, w8, #1
	b .Lmain_bb1
.Lmain_bb19:
	add x26, x26, #4
	add x27, x27, #4
	add w25, w25, #1
	b .Lmain_bb15
.Lmain_bb25:
	adrp x9, visited
	add x9, x9, :lo12:visited
	smaddl x9, w14, w10, x9
	add x3, x9, w13, sxtw #2
	ldr w9, [x3]
	cbnz w9, .Lmain_bb27
.Lmain_bb26:
	adrp x9, queue_r
	str w17, [x3]
	add x3, x9, :lo12:queue_r
	str w14, [x3, w15, sxtw #2]
	adrp x9, queue_c
	add x14, x9, :lo12:queue_c
	str w13, [x14, w15, sxtw #2]
	add w9, w15, #1
	mov w15, w9
	b .Lmain_bb27
.Lmain_bb28:
	mov w19, w20
	mov w14, w20
	b .Lmain_bb2
.Lmain_bb29:
	mov x12, x16
	mov w10, w15
	mov w11, w13
	b .Lmain_bb8
	.size main, .-main
	.p2align 2
	.global __sysy_par_body_0
	.type __sysy_par_body_0, %function
__sysy_par_body_0:
	adrp x9, __sysy_par_ctx_0_0
	ldr w8, [x9, :lo12:__sysy_par_ctx_0_0]
	mov w5, w0
	mov w7, w1
	movz w6, #0
.L__sysy_par_body_0_bb1:
	cmp w5, w7
	b.ge .L__sysy_par_body_0_bb2
.L__sysy_par_body_0_bb3:
	adrp x10, grid
	adrp x9, visited
	add x11, x10, :lo12:grid
	add x9, x9, :lo12:visited
	movz w10, #1200
	smaddl x11, w5, w10, x11
	smaddl x9, w5, w10, x9
	movz w10, #43691
	movz w4, #7
	mov x17, x9
	mov x16, x11
	mov w12, w6
	movz w15, #23
	movz w14, #6
	movk w10, #10922, lsl #16
	movz w13, #0
.L__sysy_par_body_0_bb4:
	cmp w12, #250
	b.ge .L__sysy_par_body_0_bb6
.L__sysy_par_body_0_bb5:
	mul w9, w12, w15
	madd w9, w5, w4, w9
	add w11, w9, w8
	smull x9, w11, w10
	asr x9, x9, #32
	add w9, w9, w9, lsr #31
	msub w9, w9, w14, w11
	str w9, [x16], #4
	add w12, w12, #1
	str w13, [x17], #4
	b .L__sysy_par_body_0_bb4
.L__sysy_par_body_0_bb2:
	ret
.L__sysy_par_body_0_bb6:
	add w5, w5, #1
	b .L__sysy_par_body_0_bb1
	.size __sysy_par_body_0, .-__sysy_par_body_0
	.bss
	.global grid
	.p2align 4
grid:
	.zero 360000
	.global visited
	.p2align 4
visited:
	.zero 360000
	.global queue_r
	.p2align 4
queue_r:
	.zero 360000
	.global queue_c
	.p2align 4
queue_c:
	.zero 360000
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
