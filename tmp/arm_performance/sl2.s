	.text
	.global main
	.p2align 2
main:
	sub sp, sp, #64
	stp x20, x19, [sp, #0]
	stp x22, x21, [sp, #16]
	stp x24, x23, [sp, #32]
	str x25, [sp, #48]
	str x30, [sp, #56]
	bl getint
	mov w25, w0
	bl getint
	mov w24, w0
	movz w0, #13
	bl _sysy_starttime
	adrp x11, __sysy_par_ctx_0_0
	str w25, [x11, :lo12:__sysy_par_ctx_0_0]
	mov w0, wzr
	mov w1, wzr
	mov w2, w25
	bl __sysy_parallel_for
	cmp w25, #2
	sub w23, w25, #1
	b.gt .Lmain_edge_0
	movz w22, #1
	movz w21, #1
	b main_label_while_end_12
.Lmain_edge_0:
	movz w22, #1
	b main_label_while_cond_13.preheader
main_label_while_end_12:
	movz w0, #53
	bl _sysy_stoptime
	adrp x9, x
	add x9, x9, :lo12:x
	mov w0, w25
	mov x1, x9
	bl putarray
	add w9, w25, w25, lsr #31
	asr w9, w9, #1
	adrp x11, x
	movz x13, #63744
	add x11, x11, :lo12:x
	sxtw x12, w9
	movk x13, #21, lsl #16
	madd	x11, x12, x13, x11
	sxtw x12, w9
	movz x13, #2400
	madd	x9, x12, x13, x11
	mov w0, w25
	mov x1, x9
	bl putarray
	sub w9, w22, #1
	adrp x11, x
	movz x13, #63744
	add x11, x11, :lo12:x
	sxtw x12, w9
	movk x13, #21, lsl #16
	madd	x11, x12, x13, x11
	sub w0, w21, #1
	sxtw x12, w0
	movz x13, #2400
	madd	x9, x12, x13, x11
	mov w0, w25
	mov x1, x9
	bl putarray
	mov w0, wzr
	b .Lmain_epilogue
main_label_while_cond_13.preheader:
	cmp w25, #2
	b.gt .Lmain_edge_1
	movz w21, #1
	b main_label_while_end_15
.Lmain_edge_1:
	movz w21, #1
	b main_label_while_cond_16.preheader
main_label_while_end_15:
	add w22, w22, #1
	cmp w22, w23
	b.lt	main_label_while_cond_13.preheader
	b main_label_while_end_12
main_label_while_cond_16.preheader:
	cmp w25, #2
	b.gt main_label_while_body_17.preheader
main_label_while_end_18:
	add w21, w21, #1
	cmp w21, w23
	b.lt	main_label_while_cond_16.preheader
	b main_label_while_end_15
main_label_while_body_17.preheader:
	adrp x20, x
	movz x12, #63744
	add x20, x20, :lo12:x
	sxtw x11, w22
	movk x12, #21, lsl #16
	madd	x20, x11, x12, x20
	sxtw x11, w21
	movz x12, #2400
	madd	x20, x11, x12, x20
	sub w0, w22, #1
	adrp x19, x
	movz x12, #63744
	add x19, x19, :lo12:x
	sxtw x11, w0
	movk x12, #21, lsl #16
	madd	x19, x11, x12, x19
	sxtw x11, w21
	movz x12, #2400
	madd	x19, x11, x12, x19
	add w9, w22, #1
	adrp x7, x
	movz x12, #63744
	add x7, x7, :lo12:x
	sxtw x11, w9
	movk x12, #21, lsl #16
	madd	x7, x11, x12, x7
	sxtw x11, w21
	movz x12, #2400
	madd	x7, x11, x12, x7
	adrp x6, x
	movz x12, #63744
	add x6, x6, :lo12:x
	sxtw x11, w22
	movk x12, #21, lsl #16
	madd	x6, x11, x12, x6
	sub w9, w21, #1
	sxtw x11, w9
	movz x12, #2400
	madd	x6, x11, x12, x6
	adrp x5, x
	movz x12, #63744
	add x5, x5, :lo12:x
	sxtw x11, w22
	movk x12, #21, lsl #16
	madd	x5, x11, x12, x5
	add w1, w21, #1
	sxtw x11, w1
	movz x12, #2400
	madd	x5, x11, x12, x5
	adrp x4, x
	movz x12, #63744
	add x4, x4, :lo12:x
	sxtw x11, w22
	movk x12, #21, lsl #16
	madd	x4, x11, x12, x4
	sxtw x11, w21
	movz x12, #2400
	madd	x4, x11, x12, x4
	adrp x3, x
	movz x12, #63744
	add x3, x3, :lo12:x
	sxtw x11, w22
	movk x12, #21, lsl #16
	madd	x3, x11, x12, x3
	sxtw x11, w21
	movz x12, #2400
	madd	x3, x11, x12, x3
	adrp x2, x
	movz x12, #63744
	add x2, x2, :lo12:x
	sxtw x11, w0
	movk x12, #21, lsl #16
	madd	x2, x11, x12, x2
	sxtw x11, w9
	movz x12, #2400
	madd	x2, x11, x12, x2
	add x20, x20, #4
	add x19, x19, #4
	add x7, x7, #4
	add x6, x6, #4
	add x5, x5, #4
	add x3, x3, #8
	movz w1, #1
main_label_while_body_17:
	ldr w0, [x19]
	ldr w9, [x7]
	add w1, w1, #1
	cmp w1, w23
	add x19, x19, #4
	add w0, w0, w9
	ldr w9, [x6]
	add x7, x7, #4
	add x6, x6, #4
	add w0, w0, w9
	ldr w9, [x5]
	add x5, x5, #4
	add w0, w0, w9
	ldr w9, [x4]
	add x4, x4, #4
	add w0, w0, w9
	ldr w9, [x3]
	add x3, x3, #4
	add w0, w0, w9
	ldr w9, [x2]
	add x2, x2, #4
	add w9, w0, w9
	sdiv w9, w9, w24
	str	w9, [x20], #4
	b.ge main_label_while_end_18
	b main_label_while_body_17
.Lmain_epilogue:
	ldp x20, x19, [sp, #0]
	ldp x22, x21, [sp, #16]
	ldp x24, x23, [sp, #32]
	ldr x25, [sp, #48]
	ldr x30, [sp, #56]
	add sp, sp, #64
	ret
	.global __sysy_par_body_0
	.p2align 2
__sysy_par_body_0:
	sub sp, sp, #32
	adrp x10, __sysy_par_ctx_0_0
	stp x20, x19, [sp, #0]
	stp d8, d9, [sp, #16]
	ldr w20, [x10, :lo12:__sysy_par_ctx_0_0]
	mov w19, w0
__sysy_par_body_0_label_while_cond_1:
	cmp w19, w1
	b.ge	.L__sysy_par_body_0_epilogue
	movz w9, #0
__sysy_par_body_0_label_while_cond_4:
	cmp w9, w20
	b.lt __sysy_par_body_0_label_while_cond_7.preheader
__sysy_par_body_0_label_while_end_6:
	add w19, w19, #1
	b __sysy_par_body_0_label_while_cond_1
__sysy_par_body_0_label_while_cond_7.preheader:
	adrp x4, x
	movz x11, #63744
	add x4, x4, :lo12:x
	sxtw x10, w19
	movk x11, #21, lsl #16
	madd	x4, x10, x11, x4
	sxtw x10, w9
	movz x11, #2400
	madd	x4, x10, x11, x4
	adrp x2, y
	movz x11, #63744
	add x2, x2, :lo12:y
	sxtw x10, w19
	movk x11, #21, lsl #16
	madd	x2, x10, x11, x2
	sxtw x10, w9
	movz x11, #2400
	madd	x2, x10, x11, x2
	sub w7, w20, #3
	movi v9.4s, #1
	movi v8.4s, #0
	sub w6, w20, #7
	movz w5, #0
__sysy_par_body_0_label_32:
	cmp w5, w6
	b.lt __sysy_par_body_0_label_37
__sysy_par_body_0_label_12:
	cmp w5, w7
	b.lt __sysy_par_body_0_label_15
__sysy_par_body_0_label_21:
	cmp w5, w20
	b.lt __sysy_par_body_0_label_while_body_8
__sysy_par_body_0_label_24:
	add w9, w9, #1
	b __sysy_par_body_0_label_while_cond_4
__sysy_par_body_0_label_while_body_8:
	adrp x3, x
	movz x11, #63744
	add x3, x3, :lo12:x
	sxtw x10, w19
	movk x11, #21, lsl #16
	madd	x3, x10, x11, x3
	sxtw x10, w9
	movz x11, #2400
	madd	x3, x10, x11, x3
	movz w10, #1
	movz x11, #63744
	add x3, x3, w5, sxtw #2
	str w10, [x3]
	adrp x3, y
	add x3, x3, :lo12:y
	sxtw x10, w19
	movk x11, #21, lsl #16
	madd	x3, x10, x11, x3
	sxtw x10, w9
	movz x11, #2400
	madd	x3, x10, x11, x3
	add x3, x3, w5, sxtw #2
	str wzr, [x3]
	add w5, w5, #1
	b __sysy_par_body_0_label_21
__sysy_par_body_0_label_15:
	add w5, w5, #4
	str	q9, [x4], #16
	str	q8, [x2], #16
	b __sysy_par_body_0_label_12
__sysy_par_body_0_label_37:
	mov x3, x2
	str	q9, [x4], #16
	str	q8, [x3]
	add	x3, x2, #16
	mov x2, x3
	add w5, w5, #8
	str	q9, [x4], #16
	str	q8, [x2]
	add	x2, x3, #16
	b __sysy_par_body_0_label_32
.L__sysy_par_body_0_epilogue:
	ldp x20, x19, [sp, #0]
	ldp d8, d9, [sp, #16]
	add sp, sp, #32
	ret
	.bss
	.global x
	.p2align 4
x:
	.zero 864000000

	.global y
	.p2align 4
y:
	.zero 864000000

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
	.global	__aarch64_ldadd4_rel
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
	mov	x1, x22
	mov	w0, 1
	bl	__aarch64_ldadd4_rel
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
	add	w23, w21, w23, asr 1
	mov	x1, x19
	mov	w0, 1
	str	w22, [x19, 136]
	str	w23, [x19, 140]
	str	w20, [x19, 144]
	ldr	w20, [x1, 132]!
	add	w20, w20, w0
	bl	__aarch64_ldadd4_rel
	mov	w0, w22
	mov	w2, w23
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
