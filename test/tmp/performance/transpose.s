	.arch armv8-a
	.text
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #80
	stp x21, x22, [sp, #16]
	movz w21, #0
	stp x19, x20, [sp]
	movz w22, #1
	stp x23, x24, [sp, #32]
	stp x25, x26, [sp, #48]
	stp x27, x28, [sp, #64]
	bl getint
	adrp x9, a
	add x23, x9, :lo12:a
	mov w20, w0
	mov x0, x23
	bl getarray
	mov w19, w0
	movz w0, #28
	bl _sysy_starttime
	movz w1, #0
	mov w0, w1
	mov w2, w20
	bl __sysy_parallel_for
	cmp w19, #0
	movz w9, #34464
	cset w14, ge
	movk w9, #1, lsl #16
	cmp w19, w9
	movz w13, #11520
	cset w10, le
	movk w13, #305, lsl #16
	cmp w19, w13
	cset w11, le
	cmp w20, #0
	adrp x9, matrix
	cset w12, ge
	and w10, w14, w10
	add x9, x9, :lo12:matrix
	cmp w20, w13
	and w10, w10, w11
	mov x13, x9
	cset w11, le
	and w10, w10, w12
	and w9, w10, w11
	cbz w9, .Lmain_bb52
.Lmain_bb51:
	mov x11, x23
	mov w10, w21
.Lmain_bb1:
	cmp w10, w19
	b.ge .Lmain_bb4
.Lmain_bb2:
	ldr w9, [x11]
	cmp w9, #0
	b.le .Lmain_bb53
.Lmain_bb50:
	add w10, w10, #1
	add x11, x11, #4
	b .Lmain_bb1
.Lmain_bb3:
	cmp w24, w19
	b.ge .Lmain_bb55
.Lmain_bb40:
	ldr w22, [x23]
	sdiv w17, w20, w22
	mov x26, x13
	mov x25, x26
	mov w16, w21
	orr w14, wzr, #0x80000003
.Lmain_bb41:
	cmp w16, w17
	b.ge .Lmain_bb49
.Lmain_bb42:
	add w16, w16, #1
	cmp w16, w22
	csel w15, w16, w22, lt
	cmp w15, #0
	b.le .Lmain_bb48
.Lmain_bb43:
	cmp w15, #3
	cset w10, gt
	cmp w15, w14
	cset w9, ge
	sub w28, w15, #3
	and w9, w9, w10
	cbz w9, .Lmain_bb77
.Lmain_bb75:
	mov x8, x25
	mov x12, x26
	mov w27, w21
.Lmain_bb44:
	ldr w9, [x12]
	str w9, [x8]
	ldr w9, [x12, #4]
	add x10, x8, w17, sxtw #2
	str w9, [x10]
	ldr w9, [x12, #8]
	add x11, x10, w17, sxtw #2
	str w9, [x11]
	ldr w9, [x12, #12]
	add x10, x12, #4
	add x10, x10, #4
	add x10, x10, #4
	add x12, x11, w17, sxtw #2
	add w27, w27, #4
	str w9, [x12]
	add x10, x10, #4
	add x8, x12, w17, sxtw #2
	cmp w27, w28
	b.ge .Lmain_bb45
.Lmain_bb76:
	mov x12, x10
	b .Lmain_bb44
.Lmain_bb4:
	mov x16, x13
	mov w15, w21
	mov w17, w21
.Lmain_bb5:
	cmp w17, w19
	b.ge .Lmain_bb39
.Lmain_bb6:
	cbz w22, .Lmain_bb74
.Lmain_bb7:
	adrp x9, a
	movn w10, #0
	add x9, x9, :lo12:a
	add w10, w19, w10
	add x27, x9, w10, sxtw #2
	sub w8, w19, #1
	mov w7, w21
	mov w23, w17
	orr w28, wzr, #0x80000001
	movn x10, #3
.Lmain_bb8:
	cmp w7, w8
	cset w11, lt
	cmp w19, w28
	cset w9, ge
	and w9, w9, w11
	cbz w9, .Lmain_bb26
.Lmain_bb9:
	ldr w26, [x27]
	sdiv w25, w20, w26
	cmp w25, #0
	b.le .Lmain_bb17
.Lmain_bb10:
	mul w24, w26, w25
	mov w14, w25
	mov w11, w21
.Lmain_bb11:
	cmp w23, w24
	b.ge .Lmain_bb17
.Lmain_bb12:
	sdiv w9, w23, w25
	msub w12, w9, w25, w23
	cmp w9, w12
	b.gt .Lmain_bb17
.Lmain_bb13:
	cmp w12, w14
	b.gt .Lmain_bb17
.Lmain_bb14:
	cmp w12, w14
	b.eq .Lmain_bb15
.Lmain_bb16:
	madd w23, w12, w26, w9
	mov w14, w12
	mov w11, w9
	b .Lmain_bb11
.Lmain_bb15:
	cmp w9, w11
	b.lt .Lmain_bb16
.Lmain_bb17:
	add x27, x27, x10
	ldr w26, [x27]
	sdiv w25, w20, w26
	cmp w25, #0
	b.le .Lmain_bb25
.Lmain_bb18:
	mul w24, w26, w25
	mov w14, w25
	mov w11, w21
.Lmain_bb19:
	cmp w23, w24
	b.ge .Lmain_bb25
.Lmain_bb20:
	sdiv w9, w23, w25
	msub w12, w9, w25, w23
	cmp w9, w12
	b.gt .Lmain_bb25
.Lmain_bb21:
	cmp w12, w14
	b.gt .Lmain_bb25
.Lmain_bb22:
	cmp w12, w14
	b.eq .Lmain_bb23
.Lmain_bb24:
	madd w23, w12, w26, w9
	mov w14, w12
	mov w11, w9
	b .Lmain_bb19
.Lmain_bb23:
	cmp w9, w11
	b.lt .Lmain_bb24
.Lmain_bb25:
	add w7, w7, #2
	add x27, x27, x10
	b .Lmain_bb8
.Lmain_bb26:
	cmp w7, w19
	b.ge .Lmain_bb37
.Lmain_bb66:
	mov x28, x27
	mov w27, w7
	movn x9, #3
.Lmain_bb27:
	cmp w27, w19
	b.ge .Lmain_bb37
.Lmain_bb28:
	ldr w26, [x28]
	sdiv w25, w20, w26
	cmp w25, #0
	b.le .Lmain_bb35
.Lmain_bb29:
	mul w24, w26, w25
	mov w14, w25
	mov w11, w21
.Lmain_bb30:
	cmp w23, w24
	b.ge .Lmain_bb35
.Lmain_bb31:
	sdiv w10, w23, w25
	msub w12, w10, w25, w23
	cmp w10, w12
	b.gt .Lmain_bb35
.Lmain_bb32:
	cmp w12, w14
	b.gt .Lmain_bb35
.Lmain_bb33:
	cmp w12, w14
	b.eq .Lmain_bb34
.Lmain_bb36:
	madd w23, w12, w26, w10
	mov w14, w12
	mov w11, w10
	b .Lmain_bb30
.Lmain_bb34:
	cmp w10, w11
	b.lt .Lmain_bb36
.Lmain_bb35:
	add w27, w27, #1
	add x28, x28, x9
	b .Lmain_bb27
.Lmain_bb37:
	add x9, x13, w23, sxtw #2
.Lmain_bb38:
	ldr w9, [x9]
	mul w10, w17, w17
	madd w15, w10, w9, w15
	add w17, w17, #1
	add x16, x16, #4
	b .Lmain_bb5
.Lmain_bb39:
	movz w9, #0
	cmp w15, #0
	sub w9, w9, w15
	csel w19, w9, w15, lt
	movz w0, #49
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
	add sp, sp, #80
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb45:
	cmp w27, w15
	b.ge .Lmain_bb48
.Lmain_bb78:
	mov x12, x8
	mov w11, w27
.Lmain_bb47:
	ldr w9, [x10], #4
	add w11, w11, #1
	str w9, [x12]
	cmp w11, w15
	add x12, x12, w17, sxtw #2
	b.lt .Lmain_bb47
.Lmain_bb48:
	add x26, x26, w22, sxtw #2
	add x25, x25, #4
	b .Lmain_bb41
.Lmain_bb49:
	add w24, w24, #1
	add x23, x23, #4
	b .Lmain_bb3
.Lmain_bb52:
	mov w24, w21
	b .Lmain_bb3
.Lmain_bb53:
	mov w24, w21
	b .Lmain_bb3
.Lmain_bb55:
	mov w22, w21
	b .Lmain_bb4
.Lmain_bb74:
	mov x9, x16
	b .Lmain_bb38
.Lmain_bb77:
	mov x12, x25
	mov x10, x26
	mov w11, w21
	b .Lmain_bb47
	.size main, .-main
	.p2align 2
	.global __sysy_par_body_0
	.type __sysy_par_body_0, %function
__sysy_par_body_0:
	adrp x9, matrix
	mov w13, w0
	mov w16, w1
	add x9, x9, :lo12:matrix
	add x14, x9, w13, sxtw #2
	sub w15, w16, #1
	orr w12, wzr, #0x80000001
	movz w11, #4
.L__sysy_par_body_0_bb1:
	cmp w13, w15
	cset w10, lt
	cmp w16, w12
	cset w9, ge
	and w9, w9, w10
	cbz w9, .L__sysy_par_body_0_bb7
.L__sysy_par_body_0_bb2:
	str w13, [x14]
	tst w13, #3
	b.eq .L__sysy_par_body_0_bb3
.L__sysy_par_body_0_bb4:
	add w10, w13, #1
	str w10, [x14, #4]
	add x9, x14, #4
	tst w10, #3
	b.eq .L__sysy_par_body_0_bb5
.L__sysy_par_body_0_bb6:
	add w13, w13, #2
	add x14, x9, #4
	b .L__sysy_par_body_0_bb1
.L__sysy_par_body_0_bb3:
	str w11, [x14]
	b .L__sysy_par_body_0_bb4
.L__sysy_par_body_0_bb5:
	str w11, [x14, #4]
	b .L__sysy_par_body_0_bb6
.L__sysy_par_body_0_bb7:
	cmp w13, w16
	b.ge .L__sysy_par_body_0_bb12
.L__sysy_par_body_0_bb13:
	mov x9, x14
	mov w10, w13
	movz w11, #4
.L__sysy_par_body_0_bb8:
	cmp w10, w16
	b.ge .L__sysy_par_body_0_bb12
.L__sysy_par_body_0_bb9:
	str w10, [x9]
	tst w10, #3
	b.eq .L__sysy_par_body_0_bb10
.L__sysy_par_body_0_bb11:
	add w10, w10, #1
	add x9, x9, #4
	b .L__sysy_par_body_0_bb8
.L__sysy_par_body_0_bb10:
	str w11, [x9]
	b .L__sysy_par_body_0_bb11
.L__sysy_par_body_0_bb12:
	ret
	.size __sysy_par_body_0, .-__sysy_par_body_0
	.bss
	.global matrix
	.p2align 4
matrix:
	.zero 80000000
	.global a
	.p2align 4
a:
	.zero 400000

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
