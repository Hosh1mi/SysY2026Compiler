	.text
	.global main
	.p2align 2
main:
	sub sp, sp, #80
	stp x20, x19, [sp, #0]
	stp x22, x21, [sp, #16]
	stp x24, x23, [sp, #32]
	stp x26, x25, [sp, #48]
	str x30, [sp, #64]
	bl getint
	adrp x25, a
	add x25, x25, :lo12:a
	mov w26, w0
	mov x0, x25
	bl getarray
	mov w24, w0
	movz w0, #28
	bl _sysy_starttime
	mov w0, wzr
	mov w1, wzr
	mov w2, w26
	bl __sysy_parallel_for
	adrp x23, matrix
	add x23, x23, :lo12:matrix
	movz w22, #0
main_label_while_cond_14:
	cmp w22, w24
	b.lt main_label_while_body_15
main_label_while_cond_17.preheader:
	sub w4, w24, #3
	movz w9, #0
	movz w3, #0
main_label_123:
	cmp w3, w4
	b.lt main_label_128
main_label_while_cond_17:
	cmp w3, w24
	b.lt main_label_while_body_18
main_label_while_end_19:
	sub w0, wzr, w9
	cmp w9, #0
	csel w19, w0, w9, lt
	movz w0, #49
	bl _sysy_stoptime
	mov w0, w19
	bl putint
	movz w0, #10
	bl putch
	mov w0, wzr
	b .Lmain_epilogue
main_label_while_body_15:
	ldr w21, [x25]
	mov x20, x23
	mov x7, x20
	movz w6, #0
	sdiv w19, w26, w21
main_label_31:
	cmp w6, w19
	b.lt main_label_35.preheader
main_label_53:
	add w22, w22, #1
	add x25, x25, #4
	b main_label_while_cond_14
main_label_while_body_18:
	ldr	w1, [x23], #4
	mul w0, w3, w3
	add w3, w3, #1
	madd	w9, w0, w1, w9
	b main_label_while_cond_17
main_label_35.preheader:
	add w5, w6, #1
	cmp w5, w21
	csel w4, w5, w21, lt
	cmp w4, #0
	b.gt main_label_38.preheader
main_label_51:
	add x7, x7, w21, sxtw #2
	add x20, x20, #4
	mov w6, w5
	b main_label_31
main_label_38.preheader:
	sub w3, w4, #1
	mov x0, x20
	mov x9, x7
	movz w2, #0
main_label_90:
	cmp w2, w3
	b.lt main_label_97
main_label_95:
	cmp w2, w4
	b.ge main_label_51
main_label_38:
	cmp w6, w2
	b.lt main_label_35.backedge
main_label_42:
	ldr w1, [x9]
	str w1, [x0]
main_label_35.backedge:
	add w2, w2, #1
	cmp w2, w4
	add x9, x9, #4
	add x0, x0, w19, sxtw #2
	b.ge main_label_51
	b main_label_38
main_label_97:
	cmp w6, w2
	b.lt main_label_105
main_label_100:
	ldr w1, [x9]
	str w1, [x0]
main_label_105:
	cmp w2, w6
	add	x1, x9, #4
	add x0, x0, w19, sxtw #2
	b.ge main_label_118
main_label_113:
	ldr w9, [x1]
	str w9, [x0]
main_label_118:
	add w2, w2, #2
	add	x9, x1, #4
	add x0, x0, w19, sxtw #2
	b main_label_90
main_label_128:
	ldr w0, [x23]
	mul w1, w3, w3
	madd	w2, w1, w0, w9
	add w9, w3, #1
	add	x0, x23, #4
	mul w1, w9, w9
	ldr	w9, [x0], #4
	madd	w2, w1, w9, w2
	add w9, w3, #2
	mul w1, w9, w9
	ldr w9, [x0]
	madd	w2, w1, w9, w2
	add w9, w3, #3
	add	x1, x0, #4
	mul w0, w9, w9
	ldr w9, [x1]
	add w3, w3, #4
	add	x23, x1, #4
	madd	w9, w0, w9, w2
	b main_label_123
.Lmain_epilogue:
	ldp x20, x19, [sp, #0]
	ldp x22, x21, [sp, #16]
	ldp x24, x23, [sp, #32]
	ldp x26, x25, [sp, #48]
	ldr x30, [sp, #64]
	add sp, sp, #80
	ret
	.global __sysy_par_body_0
	.p2align 2
__sysy_par_body_0:
	adrp x4, matrix
	add x4, x4, :lo12:matrix
	add x4, x4, w0, sxtw #2
	mov w3, w0
__sysy_par_body_0_label_while_cond_9:
	cmp w3, w1
	b.lt __sysy_par_body_0_label_while_body_10
__sysy_par_body_0_label_par_ret:
	ret
__sysy_par_body_0_label_while_body_10:
	str w3, [x4]
	and w2, w3, #3
	cbz w2, __sysy_par_body_0_label_if_then_12
__sysy_par_body_0_label_if_else_13:
	add w3, w3, #1
	add x4, x4, #4
	b __sysy_par_body_0_label_while_cond_9
__sysy_par_body_0_label_if_then_12:
	movz w10, #4
	str w10, [x4]
	b __sysy_par_body_0_label_if_else_13
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
