	.arch armv8-a
	.text
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #32
	stp x19, x20, [sp]
	adrp x9, a
	stp x21, x22, [sp, #16]
	add x9, x9, :lo12:a
	movz w19, #0
	mov x21, x9
	mov w20, w19
.Lmain_bb1:
	cmp w20, #1000
	b.ge .Lmain_bb4
.Lmain_bb2:
	mov x0, x21
	bl getarray
	mov w9, w0
	cmp w9, #1000
	b.ne .Lmain_bb7
.Lmain_bb3:
	add w20, w20, #1
	add x21, x21, #4000
	b .Lmain_bb1
.Lmain_bb4:
	movz w0, #23
	bl _sysy_starttime
	movz w1, #0
	movz w2, #1000
	mov w0, w1
	bl __sysy_parallel_for
	movz w0, #1
	movz w1, #0
	movz w2, #1000
	bl __sysy_parallel_for
	adrp x9, c
	add x9, x9, :lo12:c
	mov x17, x9
	mov w16, w19
	mov w21, w19
.Lmain_bb5:
	cmp w16, #1000
	b.ge .Lmain_bb6
.Lmain_bb8:
	adrp x9, c
	add x10, x9, :lo12:c
	movz w9, #4000
	smaddl x9, w16, w9, x10
	mov x22, x17
	mov x20, x9
	mov w12, w19
	movz w15, #0
.Lmain_bb9:
	cmp w12, #999
	b.ge .Lmain_bb15
.Lmain_bb13:
	ldr w11, [x22]
	sub w9, w15, w11
	str w9, [x20]
	ldr w10, [x22, #4000]
	sub w9, w15, w10
	str w9, [x20, #4]
	sub w11, w21, w11
	add x14, x20, #4
	add x13, x22, #4000
	add w12, w12, #2
	sub w21, w11, w10
	add x20, x14, #4
	add x22, x13, #4000
	b .Lmain_bb9
.Lmain_bb6:
	movz w0, #93
	bl _sysy_stoptime
	mov w0, w21
	bl putint
	mov w9, w19
.Lmain_bb7:
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	mov w0, w9
	add sp, sp, #32
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb10:
	cmp w12, #1000
	b.ge .Lmain_bb11
.Lmain_bb12:
	ldr w10, [x15]
	sub w9, w13, w10
	str w9, [x14], #4
	add w12, w12, #1
	sub w11, w11, w10
	add x15, x15, #4000
	b .Lmain_bb10
.Lmain_bb11:
	add x17, x17, #4
	add w16, w16, #1
	mov w21, w11
	b .Lmain_bb5
.Lmain_bb15:
	mov x15, x22
	mov x14, x20
	mov w11, w21
	movz w13, #0
	b .Lmain_bb10
	.size main, .-main
	.p2align 2
	.global __sysy_par_body_0
	.type __sysy_par_body_0, %function
__sysy_par_body_0:
	mov w15, w0
	mov w13, w1
	movz w14, #0
.L__sysy_par_body_0_bb1:
	cmp w15, w13
	b.ge .L__sysy_par_body_0_bb2
.L__sysy_par_body_0_bb3:
	adrp x9, b
	add x11, x9, :lo12:b
	movz w10, #4000
	smaddl x10, w15, w10, x11
	adrp x9, a
	add x9, x9, :lo12:a
	add x12, x9, w15, sxtw #2
	mov x17, x10
	mov w16, w14
.L__sysy_par_body_0_bb4:
	cmp w16, #997
	b.ge .L__sysy_par_body_0_bb9
.L__sysy_par_body_0_bb8:
	ldr w9, [x12]
	str w9, [x17]
	ldr w9, [x12, #4000]
	str w9, [x17, #4]
	ldr w9, [x12, #8000]
	str w9, [x17, #8]
	ldr w9, [x12, #12000]
	add x10, x17, #4
	add x11, x12, #4000
	add x10, x10, #4
	add x11, x11, #4000
	str w9, [x17, #12]
	add x10, x10, #4
	add x12, x11, #4000
	add w16, w16, #4
	add x17, x10, #4
	add x12, x12, #4000
	b .L__sysy_par_body_0_bb4
.L__sysy_par_body_0_bb2:
	ret
.L__sysy_par_body_0_bb5:
	cmp w11, #1000
	b.ge .L__sysy_par_body_0_bb7
.L__sysy_par_body_0_bb6:
	ldr w9, [x12]
	add w11, w11, #1
	str w9, [x10], #4
	add x12, x12, #4000
	b .L__sysy_par_body_0_bb5
.L__sysy_par_body_0_bb7:
	add w15, w15, #1
	b .L__sysy_par_body_0_bb1
.L__sysy_par_body_0_bb9:
	mov x10, x17
	mov w11, w16
	b .L__sysy_par_body_0_bb5
	.size __sysy_par_body_0, .-__sysy_par_body_0
	.p2align 2
	.global __sysy_par_body_1
	.type __sysy_par_body_1, %function
__sysy_par_body_1:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #4080
	stp x19, x20, [sp]
	stp x21, x22, [sp, #16]
	stp x23, x24, [sp, #32]
	mov w20, w0
	stp x25, x26, [sp, #48]
	mov w23, w1
	stp x27, x28, [sp, #64]
	add x24, sp, #80
	movz w22, #0
	movn w21, #32768, lsl #16
.L__sysy_par_body_1_bb1:
	cmp w20, w23
	b.ge .L__sysy_par_body_1_bb2
.L__sysy_par_body_1_bb3:
	mov x19, x24
	movz w1, #0
	movz w2, #4000
	mov x0, x19
	bl memset
	adrp x10, a
	adrp x9, b
	add x11, x10, :lo12:a
	add x9, x9, :lo12:b
	movz w10, #4000
	smaddl x11, w20, w10, x11
	smaddl x9, w20, w10, x9
	mov x16, x9
	mov x15, x11
	mov w14, w22
.L__sysy_par_body_1_bb4:
	cmp w14, #1000
	b.ge .L__sysy_par_body_1_bb5
.L__sysy_par_body_1_bb20:
	ldr w11, [x15]
	ldr w12, [x16]
	movz w17, #1
	adrp x10, b
	and w13, w11, w17
	dup v23.4s, w17
	adrp x9, a
	add x11, x10, :lo12:b
	dup v22.4s, w13
	add x9, x9, :lo12:a
	dup v21.4s, w12
	movz w10, #4000
	smaddl x11, w14, w10, x11
	smaddl x9, w14, w10, x9
	mov x10, x11
	mov x11, x19
	mov w17, w22
.L__sysy_par_body_1_bb21:
	cmp w17, #992
	b.gt .L__sysy_par_body_1_bb22
.L__sysy_par_body_1_bb26:
	ldp q17, q16, [x10]
	and v17.16b, v17.16b, v23.16b
	ldp q18, q20, [x9]
	and v16.16b, v16.16b, v23.16b
	mul v19.4s, v21.4s, v18.4s
	and v17.16b, v22.16b, v17.16b
	and v16.16b, v22.16b, v16.16b
	sub v18.4s, v17.4s, v23.4s
	mul v17.4s, v21.4s, v20.4s
	sub v16.4s, v16.4s, v23.4s
	and v18.16b, v19.16b, v18.16b
	and v16.16b, v17.16b, v16.16b
	ldp q17, q19, [x11]
	add v17.4s, v17.4s, v18.4s
	add v16.4s, v19.4s, v16.4s
	stp q17, q16, [x11]
	add w17, w17, #8
	add x11, x11, #32
	add x10, x10, #32
	add x9, x9, #32
	b .L__sysy_par_body_1_bb21
.L__sysy_par_body_1_bb2:
	ldp x27, x28, [sp, #64]
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	add sp, sp, #4080
	ldp xzr, x30, [sp], #16
	ret
.L__sysy_par_body_1_bb5:
	adrp x9, c
	add x10, x9, :lo12:c
	movz w9, #4000
	smaddl x14, w20, w9, x10
	add w13, w20, #1
	mov x15, x14
	mov w17, w22
	mov w16, w21
.L__sysy_par_body_1_bb6:
	cmp w17, #997
	b.ge .L__sysy_par_body_1_bb28
.L__sysy_par_body_1_bb19:
	ldr w12, [x24, w17, sxtw #2]
	add w9, w17, #1
	str w12, [x15]
	ldr w11, [x24, w9, sxtw #2]
	add w9, w17, #2
	str w11, [x15, #4]
	ldr w10, [x24, w9, sxtw #2]
	cmp w12, w16
	str w10, [x15, #8]
	csel w12, w12, w16, lt
	add w9, w17, #3
	ldr w9, [x24, w9, sxtw #2]
	cmp w11, w12
	csel w12, w11, w12, lt
	add x11, x15, #4
	cmp w10, w12
	add x11, x11, #4
	csel w10, w10, w12, lt
	str w9, [x15, #12]
	add x12, x11, #4
	cmp w9, w10
	add w17, w17, #4
	csel w16, w9, w10, lt
	add x15, x12, #4
	b .L__sysy_par_body_1_bb6
.L__sysy_par_body_1_bb7:
	cmp w11, #1000
	b.ge .L__sysy_par_body_1_bb8
.L__sysy_par_body_1_bb18:
	ldr w9, [x24, w11, sxtw #2]
	cmp w9, w12
	str w9, [x15], #4
	add w11, w11, #1
	csel w12, w9, w12, lt
	b .L__sysy_par_body_1_bb7
.L__sysy_par_body_1_bb8:
	dup v16.4s, w12
	mov x11, x14
	mov w10, w22
.L__sysy_par_body_1_bb9:
	cmp w10, #984
	b.gt .L__sysy_par_body_1_bb29
.L__sysy_par_body_1_bb17:
	add x9, x11, #32
	stp q16, q16, [x11]
	stp q16, q16, [x11, #32]
	add w10, w10, #16
	add x11, x9, #32
	b .L__sysy_par_body_1_bb9
.L__sysy_par_body_1_bb10:
	cmp w14, #992
	b.gt .L__sysy_par_body_1_bb11
.L__sysy_par_body_1_bb16:
	stp q16, q16, [x9]
	add w14, w14, #8
	add x9, x9, #32
	b .L__sysy_par_body_1_bb10
.L__sysy_par_body_1_bb11:
	adrp x9, c
	add x10, x9, :lo12:c
	movz w9, #4000
	smaddl x9, w20, w9, x10
	add x11, x9, w14, sxtw #2
	mov w10, w14
.L__sysy_par_body_1_bb12:
	cmp w10, #997
	b.ge .L__sysy_par_body_1_bb30
.L__sysy_par_body_1_bb15:
	add x9, x11, #4
	add x9, x9, #4
	stp w12, w12, [x11]
	add x9, x9, #4
	stp w12, w12, [x11, #8]
	add w10, w10, #4
	add x11, x9, #4
	b .L__sysy_par_body_1_bb12
.L__sysy_par_body_1_bb13:
	cmp w10, #1000
	b.ge .L__sysy_par_body_1_bb27
.L__sysy_par_body_1_bb14:
	str w12, [x9], #4
	add w10, w10, #1
	b .L__sysy_par_body_1_bb13
.L__sysy_par_body_1_bb22:
	adrp x10, b
	adrp x9, a
	add x11, x10, :lo12:b
	add x9, x9, :lo12:a
	movz w10, #4000
	smaddl x11, w14, w10, x11
	smaddl x9, w14, w10, x9
	add x28, x11, w17, sxtw #2
	add x8, x9, w17, sxtw #2
	mov w27, w17
	movz w25, #1
.L__sysy_par_body_1_bb23:
	cmp w27, #1000
	b.ge .L__sysy_par_body_1_bb25
.L__sysy_par_body_1_bb24:
	ldr w11, [x28], #4
	ldr w10, [x8], #4
	add x26, x24, w27, sxtw #2
	ldr w17, [x26]
	and w9, w11, w25
	mul w10, w12, w10
	and w9, w13, w9
	sub w9, w9, #1
	and w9, w10, w9
	add w17, w17, w9
	str w17, [x26]
	add w27, w27, #1
	b .L__sysy_par_body_1_bb23
.L__sysy_par_body_1_bb25:
	add w14, w14, #1
	add x15, x15, #4
	add x16, x16, #4
	b .L__sysy_par_body_1_bb4
.L__sysy_par_body_1_bb27:
	mov w20, w13
	b .L__sysy_par_body_1_bb1
.L__sysy_par_body_1_bb28:
	mov w11, w17
	mov w12, w16
	b .L__sysy_par_body_1_bb7
.L__sysy_par_body_1_bb29:
	mov x9, x11
	mov w14, w10
	b .L__sysy_par_body_1_bb10
.L__sysy_par_body_1_bb30:
	mov x9, x11
	b .L__sysy_par_body_1_bb13
	.size __sysy_par_body_1, .-__sysy_par_body_1
	.bss
	.global a
	.p2align 4
a:
	.zero 4000000
	.global b
	.p2align 4
b:
	.zero 4000000
	.global c
	.p2align 4
c:
	.zero 4000000

	.text
	.align 2
	.global __sysy_par_dispatch
__sysy_par_dispatch:
	cmp w0, #0
	b.eq .Lsysy_disp_0
	cmp w0, #1
	b.eq .Lsysy_disp_1
	ret
.Lsysy_disp_0:
	mov w0, w1
	mov w1, w2
	b __sysy_par_body_0
.Lsysy_disp_1:
	mov w0, w1
	mov w1, w2
	b __sysy_par_body_1

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
