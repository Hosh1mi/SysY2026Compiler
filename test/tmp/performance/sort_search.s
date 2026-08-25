	.arch armv8-a
	.text
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #48
	stp x19, x20, [sp]
	stp x21, x22, [sp, #16]
	movz w21, #0
	stp x23, x24, [sp, #32]
	movz w20, #9999
	movn w19, #0
	movz w0, #88
	bl _sysy_starttime
	movz w1, #0
	movz w2, #10000
	mov w0, w1
	bl __sysy_parallel_for
	adrp x9, sorted_array
	add x17, x9, :lo12:sorted_array
	mov x22, x17
	mov w23, w21
	movz w16, #9999
.Lmain_bb1:
	cmp w23, w16
	b.ge .Lmain_bb18
.Lmain_bb5:
	mov x15, x22
	add x24, x22, #4
	mov w11, w21
	movz w14, #9998
.Lmain_bb6:
	cmp w11, w14
	b.ge .Lmain_bb12
.Lmain_bb7:
	ldr w10, [x15]
	ldr w9, [x24]
	cmp w10, w9
	b.le .Lmain_bb9
.Lmain_bb8:
	str w9, [x15]
	str w10, [x24]
.Lmain_bb9:
	ldr w10, [x15, #4]
	ldr w9, [x24, #4]
	add x13, x15, #4
	add x12, x24, #4
	cmp w10, w9
	b.le .Lmain_bb11
.Lmain_bb10:
	str w9, [x15, #4]
	str w10, [x24, #4]
.Lmain_bb11:
	add w11, w11, #2
	add x15, x13, #4
	add x24, x12, #4
	b .Lmain_bb6
.Lmain_bb3:
	cmp w9, #303
	cset w11, lt
	sub w9, w12, #1
	cmp w11, #0
	csel w10, w10, w9, ne
	add w9, w12, #1
	cmp w11, #0
	csel w21, w9, w21, ne
	cmp w21, w10
	b.gt .Lmain_bb4
.Lmain_bb2:
	add w9, w21, w10
	add w9, w9, w9, lsr #31
	asr w12, w9, #1
	ldr w9, [x17, w12, sxtw #2]
	cmp w9, #303
	b.eq .Lmain_bb20
	b .Lmain_bb3
.Lmain_bb4:
	movz w0, #303
	bl putint
	movz w0, #32
	bl putch
	mov w0, w19
	bl putint
	movz w0, #32
	bl putch
	movz w0, #15
	bl putint
	movz w0, #32
	bl putch
	movz w0, #5
	bl putint
	movz w0, #32
	bl putch
	movz w0, #50
	bl putint
	movz w0, #32
	bl putch
	movz w0, #2
	bl putint
	movz w0, #32
	bl putch
	movz w0, #0
	bl putint
	movz w0, #32
	bl putch
	movz w0, #92
	bl _sysy_stoptime
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	movz w0, #0
	add sp, sp, #48
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb12:
	cmp w11, w16
	b.ge .Lmain_bb17
.Lmain_bb22:
	mov x14, x24
	mov x13, x15
	movz w12, #9999
.Lmain_bb13:
	cmp w11, w12
	b.ge .Lmain_bb17
.Lmain_bb14:
	ldr w10, [x13]
	ldr w9, [x14]
	cmp w10, w9
	b.le .Lmain_bb16
.Lmain_bb15:
	str w9, [x13]
	str w10, [x14]
.Lmain_bb16:
	add w11, w11, #1
	add x13, x13, #4
	add x14, x14, #4
	b .Lmain_bb13
.Lmain_bb17:
	add w23, w23, #1
	b .Lmain_bb1
.Lmain_bb18:
	mov w10, w20
	b .Lmain_bb2
.Lmain_bb20:
	mov w19, w12
	b .Lmain_bb4
	.size main, .-main
	.p2align 2
	.global __sysy_par_body_0
	.type __sysy_par_body_0, %function
__sysy_par_body_0:
	adrp x9, sorted_array
	mov w4, w0
	mov w15, w1
	add x9, x9, :lo12:sorted_array
	movz w8, #38831
	movz w13, #35757
	add x5, x9, w4, sxtw #2
	sub w6, w15, #3
	orr w7, wzr, #0x80000003
	movk w8, #152, lsl #16
	movz w17, #10000
	movk w13, #26843, lsl #16
.L__sysy_par_body_0_bb1:
	cmp w4, w6
	cset w10, lt
	cmp w15, w7
	cset w9, ge
	and w9, w9, w10
	cbz w9, .L__sysy_par_body_0_bb6
.L__sysy_par_body_0_bb5:
	mul w2, w4, w8
	add w10, w4, #2
	mul w16, w10, w8
	add w11, w4, #1
	smull x12, w2, w13
	add w9, w4, #3
	mul w3, w11, w8
	mul w14, w9, w8
	asr x12, x12, #44
	smull x10, w16, w13
	add w12, w12, w12, lsr #31
	smull x11, w3, w13
	smull x9, w14, w13
	msub w1, w12, w17, w2
	asr x10, x10, #44
	asr x11, x11, #44
	asr x9, x9, #44
	add w10, w10, w10, lsr #31
	msub w12, w10, w17, w16
	add w11, w11, w11, lsr #31
	add w9, w9, w9, lsr #31
	msub w3, w11, w17, w3
	msub w9, w9, w17, w14
	add x2, x5, #4
	add x10, x2, #4
	stp w1, w3, [x5]
	add x11, x10, #4
	stp w12, w9, [x5, #8]
	add w4, w4, #4
	add x5, x11, #4
	b .L__sysy_par_body_0_bb1
.L__sysy_par_body_0_bb2:
	cmp w4, w15
	b.ge .L__sysy_par_body_0_bb3
.L__sysy_par_body_0_bb4:
	mul w11, w4, w13
	smull x9, w11, w10
	asr x9, x9, #44
	add w9, w9, w9, lsr #31
	msub w9, w9, w12, w11
	str w9, [x5], #4
	add w4, w4, #1
	b .L__sysy_par_body_0_bb2
.L__sysy_par_body_0_bb3:
	ret
.L__sysy_par_body_0_bb6:
	movz w13, #38831
	movz w10, #35757
	movk w13, #152, lsl #16
	movz w12, #10000
	movk w10, #26843, lsl #16
	b .L__sysy_par_body_0_bb2
	.size __sysy_par_body_0, .-__sysy_par_body_0
	.bss
	.global sorted_array
	.p2align 4
sorted_array:
	.zero 40000

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
