	.text
	.global kernel_nussinov
	.p2align 2
kernel_nussinov:
	sub sp, sp, #80
	stp x24, x23, [sp, #32]
	mov w24, w0
	cmp w24, #1
	stp x20, x19, [sp, #0]
	stp x22, x21, [sp, #16]
	stp x26, x25, [sp, #48]
	str x30, [sp, #64]
	mov x25, x1
	mov x26, x2
	sub w23, w24, #1
	b.lt kernel_nussinov_label_while_cond_32.loopexit
kernel_nussinov_label_while_body_2:
	add w22, w23, #1
	mov x21, x25
	cmp w22, w24
	add x21, x21, w23, sxtw #2
	b.lt kernel_nussinov_label_while_body_5.preheader
kernel_nussinov_label_while_end_6:
	cmp w23, #1
	b.lt kernel_nussinov_label_while_cond_32.loopexit
	sub	w23, w23, #1
	b kernel_nussinov_label_while_body_2
kernel_nussinov_label_while_cond_32.loopexit:
	adrp x10, __sysy_par_ctx_0_0
	str w24, [x10, :lo12:__sysy_par_ctx_0_0]
	adrp x10, __sysy_par_ctx_0_1
	str x26, [x10, :lo12:__sysy_par_ctx_0_1]
	ldp x20, x19, [sp, #0]
	ldp x22, x21, [sp, #16]
	ldp x26, x25, [sp, #48]
	ldr x30, [sp, #64]
	mov w2, w24
	ldp x24, x23, [sp, #32]
	mov w0, wzr
	mov w1, wzr
	add sp, sp, #80
	b __sysy_parallel_for
kernel_nussinov_label_while_body_5.preheader:
	mov x20, x26
	sxtw x10, w23
	movz x11, #5600
	madd	x20, x10, x11, x20
	mov x19, x26
	sxtw x10, w22
	movz x11, #5600
	madd	x19, x10, x11, x19
	mov w7, w22
kernel_nussinov_label_while_body_5:
	movz w10, #1
	cmp w7, w10
	cset w11, ge
	sub w6, w7, #1
	mov w2, w11
	cbnz w2, kernel_nussinov_label_if_then_7
kernel_nussinov_label_if_then_11:
	mov x1, x20
	mov x0, x19
	add x1, x1, w7, sxtw #2
	add x0, x0, w7, sxtw #2
	ldr w9, [x1]
	ldr w0, [x0]
	cmp w9, w0
	b.lt kernel_nussinov_label_if_then_13
kernel_nussinov_label_if_else_12:
	cbnz w2, kernel_nussinov_label_if_then_15
kernel_nussinov_label_while_cond_27.preheader:
	mov x5, x20
	cmp w22, w7
	add x5, x5, w7, sxtw #2
	b.lt kernel_nussinov_label_while_body_28.preheader
kernel_nussinov_label_while_end_29:
	add w7, w7, #1
	cmp w7, w24
	b.ge kernel_nussinov_label_while_end_6
	b kernel_nussinov_label_while_body_5
kernel_nussinov_label_if_then_7:
	mov x1, x20
	mov x0, x20
	add x1, x1, w7, sxtw #2
	add x0, x0, w6, sxtw #2
	ldr w9, [x1]
	ldr w0, [x0]
	cmp w9, w0
	b.lt kernel_nussinov_label_if_then_9
	b kernel_nussinov_label_if_then_11
kernel_nussinov_label_if_then_9:
	str w0, [x1]
	b kernel_nussinov_label_if_then_11
kernel_nussinov_label_if_then_13:
	lsl w9, w0, #1
	str w9, [x1]
	b kernel_nussinov_label_if_else_12
kernel_nussinov_label_if_then_15:
	cmp w23, w6
	b.lt kernel_nussinov_label_if_then_18
kernel_nussinov_label_if_else_19:
	mov x1, x20
	mov x0, x19
	add x1, x1, w7, sxtw #2
	add x0, x0, w6, sxtw #2
	ldr w9, [x1]
	ldr w0, [x0]
	cmp w9, w0
	b.lt kernel_nussinov_label_if_then_25
	b kernel_nussinov_label_while_cond_27.preheader
kernel_nussinov_label_if_then_25:
	str w0, [x1]
	b kernel_nussinov_label_while_cond_27.preheader
kernel_nussinov_label_if_then_18:
	mov x9, x25
	add x9, x9, w7, sxtw #2
	ldr w0, [x21]
	ldr w9, [x9]
	movz w10, #3
	add w9, w0, w9
	cmp w9, #3
	mov x9, x19
	add x9, x9, w6, sxtw #2
	mov x0, x20
	ldr w1, [x9]
	add x0, x0, w7, sxtw #2
	ldr w9, [x0]
	csel w2, w10, wzr, eq
	add w1, w1, w2
	cmp w9, w1
	b.lt kernel_nussinov_label_if_then_23
	b kernel_nussinov_label_while_cond_27.preheader
kernel_nussinov_label_if_then_23:
	str w1, [x0]
	b kernel_nussinov_label_while_cond_27.preheader
kernel_nussinov_label_while_body_28.preheader:
	mov x3, x26
	sxtw x10, w23
	movz x11, #5600
	madd	x3, x10, x11, x3
	mov w4, w22
	add x3, x3, w22, sxtw #2
kernel_nussinov_label_82:
	cmp w4, w6
	b.lt kernel_nussinov_label_88
kernel_nussinov_label_86:
	cmp w4, w7
	b.ge kernel_nussinov_label_while_end_29
kernel_nussinov_label_while_body_28:
	add w4, w4, #1
	mov x9, x26
	sxtw x10, w4
	movz x11, #5600
	madd	x9, x10, x11, x9
	ldr w2, [x3]
	ldr w0, [x5]
	add x9, x9, w7, sxtw #2
	ldr w1, [x9]
	add w9, w2, w1
	cmp w0, w9
	b.lt kernel_nussinov_label_if_then_30
kernel_nussinov_label_while_cond_27.backedge:
	cmp w4, w7
	add x3, x3, #4
	b.ge kernel_nussinov_label_while_end_29
	b kernel_nussinov_label_while_body_28
kernel_nussinov_label_if_then_30:
	add	w9, w1, w2, lsl #1
	str w9, [x5]
	b kernel_nussinov_label_while_cond_27.backedge
kernel_nussinov_label_88:
	add w9, w4, #1
	mov x10, x26
	sxtw x11, w9
	movz x12, #5600
	madd	x10, x11, x12, x10
	ldr w2, [x3]
	ldr w0, [x5]
	add	x9, x10, w7, sxtw #2
	ldr w1, [x9]
	add w9, w2, w1
	cmp w0, w9
	b.lt kernel_nussinov_label_96
kernel_nussinov_label_99:
	add w4, w4, #2
	mov x9, x26
	sxtw x10, w4
	movz x11, #5600
	madd	x9, x10, x11, x9
	add x3, x3, #4
	ldr w2, [x3]
	add x9, x9, w7, sxtw #2
	ldr w1, [x9]
	ldr w0, [x5]
	add w9, w2, w1
	cmp w0, w9
	b.lt kernel_nussinov_label_111
kernel_nussinov_label_114:
	add x3, x3, #4
	b kernel_nussinov_label_82
kernel_nussinov_label_96:
	add	w9, w1, w2, lsl #1
	str w9, [x5]
	b kernel_nussinov_label_99
kernel_nussinov_label_111:
	add	w9, w1, w2, lsl #1
	str w9, [x5]
	b kernel_nussinov_label_114
.Lkernel_nussinov_epilogue:
	ldp x20, x19, [sp, #0]
	ldp x22, x21, [sp, #16]
	ldp x24, x23, [sp, #32]
	ldp x26, x25, [sp, #48]
	ldr x30, [sp, #64]
	add sp, sp, #80
	ret
	.global main
	.p2align 2
main:
	sub sp, sp, #32
	adrp x10, n
	stp x20, x19, [sp, #0]
	str x21, [sp, #16]
	str x30, [sp, #24]
	ldr w21, [x10, :lo12:n]
	adrp x20, seq
	add x20, x20, :lo12:seq
	mov x0, x20
	bl getarray
	adrp x19, table
	add x19, x19, :lo12:table
	mov x0, x19
	bl getarray
	movz w0, #79
	bl _sysy_starttime
	mov w0, w21
	mov x1, x20
	mov x2, x19
	bl kernel_nussinov
	movz w0, #81
	bl _sysy_stoptime
	mul	w0, w21, w21
	mov x1, x19
	bl putarray
	adrp x10, n
	str w21, [x10, :lo12:n]
	mov w0, wzr
.Lmain_epilogue:
	ldp x20, x19, [sp, #0]
	ldr x21, [sp, #16]
	ldr x30, [sp, #24]
	add sp, sp, #32
	ret
	.global __sysy_par_body_0
	.p2align 2
__sysy_par_body_0:
	sub sp, sp, #16
	adrp x10, __sysy_par_ctx_0_0
	str x28, [sp, #0]
	ldr w9, [x10, :lo12:__sysy_par_ctx_0_0]
	adrp x10, __sysy_par_ctx_0_1
	ldr x7, [x10, :lo12:__sysy_par_ctx_0_1]
	movz w28, #41705
	movk w28, #11915, lsl #16
	mov w6, w0
__sysy_par_body_0_label_while_cond_32:
	cmp w6, w1
	b.lt __sysy_par_body_0_label_while_cond_35.preheader
__sysy_par_body_0_label_par_ret:
	b .L__sysy_par_body_0_epilogue
__sysy_par_body_0_label_while_cond_35.preheader:
	mov x3, x7
	sxtw x10, w6
	movz x11, #5600
	madd	x3, x10, x11, x3
	sub w5, w9, #3
	movz w4, #0
__sysy_par_body_0_label_8:
	cmp w4, w5
	b.lt __sysy_par_body_0_label_12
__sysy_par_body_0_label_while_cond_35:
	cmp w4, w9
	b.lt __sysy_par_body_0_label_while_body_36
__sysy_par_body_0_label_while_end_37:
	add w6, w6, #1
	b __sysy_par_body_0_label_while_cond_32
__sysy_par_body_0_label_while_body_36:
	ldr w2, [x3]
	movz w11, #11
	add w4, w4, #1
	smull x10, w2, w28
	asr x10, x10, #33
	add w10, w10, w2, lsr #31
	msub w2, w10, w11, w2
	str	w2, [x3], #4
	b __sysy_par_body_0_label_while_cond_35
__sysy_par_body_0_label_12:
	ldr w2, [x3]
	movz w11, #11
	add w4, w4, #4
	smull x10, w2, w28
	asr x10, x10, #33
	add w10, w10, w2, lsr #31
	msub w2, w10, w11, w2
	movz w11, #11
	str	w2, [x3], #4
	ldr w2, [x3]
	smull x10, w2, w28
	asr x10, x10, #33
	add w10, w10, w2, lsr #31
	msub w2, w10, w11, w2
	movz w11, #11
	str	w2, [x3], #4
	ldr w2, [x3]
	smull x10, w2, w28
	asr x10, x10, #33
	add w10, w10, w2, lsr #31
	msub w2, w10, w11, w2
	movz w11, #11
	str	w2, [x3], #4
	ldr w2, [x3]
	smull x10, w2, w28
	asr x10, x10, #33
	add w10, w10, w2, lsr #31
	msub w2, w10, w11, w2
	str	w2, [x3], #4
	b __sysy_par_body_0_label_8
.L__sysy_par_body_0_epilogue:
	ldr x28, [sp, #0]
	add sp, sp, #16
	ret
	.data
	.global n
	.p2align 2
n:
	.word 1400

	.bss
	.global seq
	.p2align 4
seq:
	.zero 5600

	.global table
	.p2align 4
table:
	.zero 7840000

	.global __sysy_par_ctx_0_0
	.p2align 2
__sysy_par_ctx_0_0:
	.zero 4

	.global __sysy_par_ctx_0_1
	.p2align 3
__sysy_par_ctx_0_1:
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
