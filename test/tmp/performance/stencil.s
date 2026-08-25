	.arch armv8-a
	.text
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #32
	stp x21, x22, [sp, #16]
	adrp x9, image_in
	add x21, x9, :lo12:image_in
	stp x19, x20, [sp]
	movz w22, #0
	mov x0, x21
	bl getarray
	mov w19, w0
	movz w0, #23
	bl _sysy_starttime
	movz w1, #0
	movz w2, #1022
	mov w0, w1
	bl __sysy_parallel_for
	mov w14, w22
	movz w13, #1023
.Lmain_bb1:
	cmp w14, #1024
	b.ge .Lmain_bb2
.Lmain_bb9:
	adrp x9, image_in
	lsl w12, w14, #10
	add x9, x9, :lo12:image_in
	ldr w10, [x9, w12, sxtw #2]
	adrp x9, image_out
	add x11, x9, :lo12:image_out
	str w10, [x11, w12, sxtw #2]
	adrp x9, image_in
	orr w12, w12, w13
	add x9, x9, :lo12:image_in
	ldr w10, [x9, w12, sxtw #2]
	adrp x9, image_out
	add x11, x9, :lo12:image_out
	str w10, [x11, w12, sxtw #2]
	add w14, w14, #1
	b .Lmain_bb1
.Lmain_bb2:
	adrp x11, image_out
	adrp x10, image_in
	add x20, x11, :lo12:image_out
	adrp x9, image_out
	add x11, x10, :lo12:image_in
	add x9, x9, :lo12:image_out
	orr x10, xzr, #0x3ff000
	add x11, x11, x10
	add x9, x9, x10
	mov x10, x11
	mov x11, x20
	mov x12, x21
.Lmain_bb3:
	cmp w22, #1016
	b.gt .Lmain_bb4
.Lmain_bb8:
	ldp q19, q17, [x12]
	ldp q18, q16, [x10]
	str q19, [x11]
	str q18, [x9]
	str q17, [x11, #16]
	str q16, [x9, #16]
	add w22, w22, #8
	add x12, x12, #32
	add x11, x11, #32
	add x10, x10, #32
	add x9, x9, #32
	b .Lmain_bb3
.Lmain_bb4:
	adrp x10, image_out
	adrp x9, image_in
	orr w13, wzr, #0xffc00
	adrp x12, image_out
	adrp x11, image_in
	add x10, x10, :lo12:image_out
	add x9, x9, :lo12:image_in
	add w13, w22, w13
	add x12, x12, :lo12:image_out
	add x11, x11, :lo12:image_in
	add x10, x10, w13, sxtw #2
	add x14, x9, w13, sxtw #2
	add x12, x12, w22, sxtw #2
	add x11, x11, w22, sxtw #2
	mov w13, w22
.Lmain_bb5:
	cmp w13, #1024
	b.ge .Lmain_bb7
.Lmain_bb6:
	ldr w9, [x11], #4
	str w9, [x12], #4
	ldr w9, [x14], #4
	add w13, w13, #1
	str w9, [x10], #4
	b .Lmain_bb5
.Lmain_bb7:
	movz w0, #59
	bl _sysy_stoptime
	movz w0, #16, lsl #16
	mov x1, x20
	bl putarray
	mov w0, w19
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	add sp, sp, #32
	ldp xzr, x30, [sp], #16
	ret
	.size main, .-main
	.p2align 2
	.global __sysy_par_body_0
	.type __sysy_par_body_0, %function
__sysy_par_body_0:
	mov w9, w0
	mov w17, w1
	movz w8, #0
.L__sysy_par_body_0_bb1:
	add w16, w9, #1
	cmp w9, w17
	b.ge .L__sysy_par_body_0_bb4
.L__sysy_par_body_0_bb6:
	mov w3, w8
	movz w13, #0
	movz w11, #255
.L__sysy_par_body_0_bb2:
	cmp w3, #1022
	b.ge .L__sysy_par_body_0_bb5
.L__sysy_par_body_0_bb3:
	add w15, w3, #1
	lsl w10, w15, #10
	adrp x9, image_in
	add w14, w10, w16
	add x10, x9, :lo12:image_in
	ldr w7, [x10, w14, sxtw #2]
	lsl w12, w3, #10
	add w6, w12, w16
	adrp x9, image_in
	sub w12, w6, #1
	add x10, x9, :lo12:image_in
	ldr w12, [x10, w12, sxtw #2]
	adrp x9, image_in
	add x10, x9, :lo12:image_in
	ldr w4, [x10, w6, sxtw #2]
	adrp x9, image_in
	add w5, w6, #1
	add x10, x9, :lo12:image_in
	ldr w5, [x10, w5, sxtw #2]
	add w6, w3, #2
	adrp x9, image_in
	sub w2, w14, #1
	lsl w3, w6, #10
	add x10, x9, :lo12:image_in
	ldr w6, [x10, w2, sxtw #2]
	adrp x9, image_in
	add w1, w3, w16
	add w2, w14, #1
	lsl w3, w7, #3
	add x10, x9, :lo12:image_in
	ldr w7, [x10, w2, sxtw #2]
	adrp x9, image_in
	sub w2, w1, #1
	sub w3, w3, w12
	add x10, x9, :lo12:image_in
	ldr w12, [x10, w2, sxtw #2]
	adrp x9, image_in
	add x10, x9, :lo12:image_in
	sub w4, w3, w4
	adrp x9, image_in
	ldr w10, [x10, w1, sxtw #2]
	add w3, w1, #1
	sub w5, w4, w5
	add x9, x9, :lo12:image_in
	ldr w9, [x9, w3, sxtw #2]
	sub w6, w5, w6
	sub w7, w6, w7
	sub w12, w7, w12
	sub w10, w12, w10
	sub w9, w10, w9
	cmp w9, #0
	cset w12, lt
	cmp w9, #255
	csel w10, w11, w9, gt
	adrp x9, image_out
	cmp w12, #0
	add x12, x9, :lo12:image_out
	csel w9, w13, w10, ne
	str w9, [x12, w14, sxtw #2]
	mov w3, w15
	b .L__sysy_par_body_0_bb2
.L__sysy_par_body_0_bb4:
	ret
.L__sysy_par_body_0_bb5:
	mov w9, w16
	b .L__sysy_par_body_0_bb1
	.size __sysy_par_body_0, .-__sysy_par_body_0
	.bss
	.global image_in
	.p2align 4
image_in:
	.zero 4194304
	.global image_out
	.p2align 4
image_out:
	.zero 4194304

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
