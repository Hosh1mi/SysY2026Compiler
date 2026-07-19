	.text
	.global main
	.p2align 2
main:
	sub sp, sp, #48
	stp x22, x21, [sp, #16]
	adrp x22, a
	add x22, x22, :lo12:a
	stp x20, x19, [sp, #0]
	str x23, [sp, #32]
	str x30, [sp, #40]
	mov x0, x22
	bl getarray
	mov w21, w0
	mov w0, wzr
	mov w1, wzr
	movz w2, #256
	bl __sysy_parallel_for
	movz w0, #68
	bl _sysy_starttime
	movz w10, #58769
	movz w11, #23095
	movk w10, #293, lsl #16
	movk w11, #14271, lsl #16
	cbnz	w21, main_label_15.preheader
	movz w23, #0
main_label_while_end_20:
	movz w0, #73
	bl _sysy_stoptime
	mov w0, w23
	bl putint
	movz w0, #10
	bl putch
	mov w0, wzr
	b .Lmain_epilogue
main_label_15.preheader:
	mov x20, x22
	movz w19, #0
	movz w7, #0
main_label_15:
	cmp w19, w21
	b.lt main_label_19
main_label_47:
	smull x12, w7, w11
	cmp w21, #1
	sub w0, w21, #1
	asr x12, x12, #54
	add w12, w12, w7, lsr #31
	msub w23, w12, w10, w7
	b.ne .Lmain_edge_1
	b main_label_while_end_20
.Lmain_edge_1:
	mov w21, w0
	b main_label_15.preheader
main_label_19:
	ldr w6, [x20]
	cmp w7, #0
	csel w9, wzr, w7, lt
	and w5, w9, #255
	orr w9, w5, w6
	cmp w9, #0
	b.ge main_label_31
	movz w3, #1
	movz w4, #0
	movz w2, #32
main_label_85:
	tst w5, w5
	and w1, w5, #1
	cneg w1, w1, mi
	tst w6, w6
	and w0, w6, #1
	cneg w0, w0, mi
	add w9, w4, w3
	cmp w1, w0
	add w5, w5, w5, lsr #31
	add w6, w6, w6, lsr #31
	csel w4, w9, w4, ne
	cmp w2, #1
	asr w5, w5, #1
	asr w6, w6, #1
	lsl w3, w3, #1
	b.eq	main_label_33
	sub	w2, w2, #1
	b main_label_85
main_label_33:
	adrp x0, crc32table
	add x0, x0, :lo12:crc32table
	add x0, x0, w4, sxtw #2
	ldr w5, [x0]
	asr w12, w7, #31
	bic w12, w12, w12, lsl #8
	add w9, w7, w12
	asr w9, w9, #8
	orr w0, w9, w5
	cmp w0, #0
	b.ge main_label_42
	movz w4, #1
	movz w7, #0
	movz w3, #32
main_label_58:
	tst w9, w9
	and w2, w9, #1
	cneg w2, w2, mi
	tst w5, w5
	and w1, w5, #1
	cneg w1, w1, mi
	add w0, w7, w4
	cmp w2, w1
	add w9, w9, w9, lsr #31
	add w5, w5, w5, lsr #31
	csel w7, w0, w7, ne
	cmp w3, #1
	asr w9, w9, #1
	asr w5, w5, #1
	lsl w4, w4, #1
	b.eq	main_label_44
	sub	w3, w3, #1
	b main_label_58
main_label_44:
	add w19, w19, #1
	add x20, x20, #4
	b main_label_15
main_label_31:
	eor w4, w5, w6
	b main_label_33
main_label_42:
	eor w7, w9, w5
	b main_label_44
.Lmain_epilogue:
	ldp x20, x19, [sp, #0]
	ldp x22, x21, [sp, #16]
	ldr x23, [sp, #32]
	ldr x30, [sp, #40]
	add sp, sp, #48
	ret
	.global __sysy_par_body_0
	.p2align 2
__sysy_par_body_0:
	sub sp, sp, #48
	adrp x9, crc32table
	stp x20, x19, [sp, #0]
	adrp x19, crc32table
	add w2, w0, #1
	add x9, x9, :lo12:crc32table
	adrp x7, crc32table
	str x28, [sp, #16]
	movz w28, #58769
	movz w10, #1
	movz w11, #2
	movz w12, #3
	add x19, x19, :lo12:crc32table
	add x9, x9, w2, sxtw #2
	add w2, w0, #2
	add x7, x7, :lo12:crc32table
	adrp x6, crc32table
	stp d8, d9, [sp, #24]
	movk w28, #293, lsl #16
	dup v8.4s, w0
	mov v0.s[0], wzr
	mov v0.s[1], w10
	mov v0.s[2], w11
	mov v0.s[3], w12
	add x19, x19, w0, sxtw #2
	add x7, x7, w2, sxtw #2
	add w2, w0, #3
	add x6, x6, :lo12:crc32table
	str d10, [sp, #40]
	sub w20, w1, #3
	add v8.4s, v8.4s, v0.4s
	dup v10.4s, w28
	add x6, x6, w2, sxtw #2
	mov	x5, x19
	mov	w4, w0
__sysy_par_body_0_label_12:
	cmp w4, w20
	b.lt __sysy_par_body_0_label_16
__sysy_par_body_0_label_33:
	cmp w4, w1
	b.lt __sysy_par_body_0_label_while_body_16
__sysy_par_body_0_label_36:
	b .L__sysy_par_body_0_epilogue
__sysy_par_body_0_label_while_body_16:
	adrp x3, crc32table
	add x3, x3, :lo12:crc32table
	add x3, x3, w4, sxtw #2
	add w2, w4, w28
	str w2, [x3]
	add w4, w4, #1
	b __sysy_par_body_0_label_33
__sysy_par_body_0_label_16:
	movi v0.4s, #4
	add v9.4s, v8.4s, v0.4s
	add v8.4s, v8.4s, v10.4s
	add w4, w4, #4
	str	q8, [x19], #16
	add x5, x5, #16
	add x9, x9, #16
	add x7, x7, #16
	add x6, x6, #16
	mov v8.16b, v9.16b
	b __sysy_par_body_0_label_12
.L__sysy_par_body_0_epilogue:
	ldp x20, x19, [sp, #0]
	ldr x28, [sp, #16]
	ldp d8, d9, [sp, #24]
	ldr d10, [sp, #40]
	add sp, sp, #48
	ret
	.bss
	.global crc32table
	.p2align 4
crc32table:
	.zero 1024

	.global a
	.p2align 4
a:
	.zero 400080


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
