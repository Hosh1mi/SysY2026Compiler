	.arch armv8-a
	.text
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #48
	stp x23, x24, [sp, #32]
	movz w23, #0
	stp x19, x20, [sp]
	stp x21, x22, [sp, #16]
	bl getint
	mov w24, w0
	add w10, w24, #3
	adrp x9, __sysy_par_ctx_0_0
	movz w1, #0
	str w10, [x9, :lo12:__sysy_par_ctx_0_0]
	movz w2, #768
	mov w0, w1
	bl __sysy_parallel_for
	movz w0, #16
	bl _sysy_starttime
	movz w22, #1
	movz w21, #0
	movz w20, #768
	movz w19, #2
.Lmain_bb1:
	cmp w23, #774
	b.ge .Lmain_bb3
.Lmain_bb2:
	mov w0, w22
	mov w1, w21
	mov w2, w20
	bl __sysy_parallel_for
	mov w0, w19
	mov w1, w21
	mov w2, w20
	bl __sysy_parallel_for
	add w23, w23, #1
	b .Lmain_bb1
.Lmain_bb3:
	add w19, w24, #777
	movz w0, #58
	bl _sysy_stoptime
	adrp x9, current
	add x9, x9, :lo12:current
	ldr w9, [x9]
	cmp w9, w19
	b.eq .Lmain_bb4
.Lmain_bb7:
	movz w0, #0
	bl putint
.Lmain_bb8:
	movz w0, #10
	bl putch
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	movz w0, #0
	add sp, sp, #48
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb4:
	adrp x9, current
	add x10, x9, :lo12:current
	movz x9, #18, lsl #16
	add x9, x10, x9
	ldr w9, [x9, #1536]
	cmp w9, w19
	b.ne .Lmain_bb7
.Lmain_bb5:
	adrp x9, current
	add x10, x9, :lo12:current
	movz x9, #62464
	movk x9, #35, lsl #16
	add x9, x10, x9
	ldr w9, [x9, #3068]
	cmp w9, w19
	b.ne .Lmain_bb7
.Lmain_bb6:
	movz w0, #1
	bl putint
	b .Lmain_bb8
	.size main, .-main
	.p2align 2
	.global __sysy_par_body_0
	.type __sysy_par_body_0, %function
__sysy_par_body_0:
	adrp x9, __sysy_par_ctx_0_0
	ldr w12, [x9, :lo12:__sysy_par_ctx_0_0]
	mov w15, w0
	mov w13, w1
	movz w14, #0
.L__sysy_par_body_0_bb1:
	cmp w15, w13
	b.ge .L__sysy_par_body_0_bb2
.L__sysy_par_body_0_bb3:
	adrp x9, current
	dup v16.4s, w12
	add x10, x9, :lo12:current
	movz w9, #3072
	smaddl x9, w15, w9, x10
	mov x11, x9
	mov w10, w14
.L__sysy_par_body_0_bb4:
	cmp w10, #752
	b.gt .L__sysy_par_body_0_bb14
.L__sysy_par_body_0_bb13:
	add x9, x11, #32
	stp q16, q16, [x11]
	stp q16, q16, [x11, #32]
	add w10, w10, #16
	add x11, x9, #32
	b .L__sysy_par_body_0_bb4
.L__sysy_par_body_0_bb2:
	ret
.L__sysy_par_body_0_bb5:
	cmp w16, #760
	b.gt .L__sysy_par_body_0_bb6
.L__sysy_par_body_0_bb12:
	stp q16, q16, [x9]
	add w16, w16, #8
	add x9, x9, #32
	b .L__sysy_par_body_0_bb5
.L__sysy_par_body_0_bb6:
	adrp x9, current
	add x10, x9, :lo12:current
	movz w9, #3072
	smaddl x9, w15, w9, x10
	add x11, x9, w16, sxtw #2
	mov w10, w16
.L__sysy_par_body_0_bb7:
	cmp w10, #765
	b.ge .L__sysy_par_body_0_bb15
.L__sysy_par_body_0_bb11:
	add x9, x11, #4
	add x9, x9, #4
	stp w12, w12, [x11]
	add x9, x9, #4
	stp w12, w12, [x11, #8]
	add w10, w10, #4
	add x11, x9, #4
	b .L__sysy_par_body_0_bb7
.L__sysy_par_body_0_bb8:
	cmp w10, #768
	b.ge .L__sysy_par_body_0_bb10
.L__sysy_par_body_0_bb9:
	str w12, [x9], #4
	add w10, w10, #1
	b .L__sysy_par_body_0_bb8
.L__sysy_par_body_0_bb10:
	add w15, w15, #1
	b .L__sysy_par_body_0_bb1
.L__sysy_par_body_0_bb14:
	mov x9, x11
	mov w16, w10
	b .L__sysy_par_body_0_bb5
.L__sysy_par_body_0_bb15:
	mov x9, x11
	b .L__sysy_par_body_0_bb8
	.size __sysy_par_body_0, .-__sysy_par_body_0
	.p2align 2
	.global __sysy_par_body_1
	.type __sysy_par_body_1, %function
__sysy_par_body_1:
	mov w14, w0
	mov w15, w1
	movz w16, #0
.L__sysy_par_body_1_bb1:
	cmp w14, w15
	b.ge .L__sysy_par_body_1_bb2
.L__sysy_par_body_1_bb3:
	cmp w14, #767
	add w13, w14, #1
	b.eq .L__sysy_par_body_1_bb4
.L__sysy_par_body_1_bb9:
	mov w7, w13
.L__sysy_par_body_1_bb5:
	adrp x11, next
	adrp x10, current
	adrp x9, current
	add x12, x11, :lo12:next
	cmp w14, #1
	sub w8, w14, #1
	movz w17, #767
	add x10, x10, :lo12:current
	add x9, x9, :lo12:current
	movz w11, #3072
	csel w8, w17, w8, lt
	smaddl x12, w14, w11, x12
	smaddl x10, w8, w11, x10
	smaddl x9, w7, w11, x9
	mov x7, x9
	mov x6, x10
	mov x5, x12
	mov w3, w16
	movz w8, #0
.L__sysy_par_body_1_bb6:
	cmp w3, #768
	b.ge .L__sysy_par_body_1_bb8
.L__sysy_par_body_1_bb7:
	adrp x9, current
	add x10, x9, :lo12:current
	smaddl x10, w14, w11, x10
	sub w12, w3, #1
	cmp w3, #1
	csel w2, w17, w12, lt
	ldr w10, [x10, w2, sxtw #2]
	adrp x9, current
	add x9, x9, :lo12:current
	smaddl x9, w14, w11, x9
	cmp w3, #767
	ldr w4, [x6], #4
	ldr w12, [x7], #4
	add w3, w3, #1
	csel w2, w8, w3, eq
	ldr w9, [x9, w2, sxtw #2]
	add w12, w4, w12
	add w10, w12, w10
	add w10, w10, w9
	asr w9, w10, #31
	lsr w9, w9, #30
	add w9, w10, w9
	asr w9, w9, #2
	add w9, w9, #1
	str w9, [x5], #4
	b .L__sysy_par_body_1_bb6
.L__sysy_par_body_1_bb2:
	ret
.L__sysy_par_body_1_bb4:
	mov w7, w16
	b .L__sysy_par_body_1_bb5
.L__sysy_par_body_1_bb8:
	mov w14, w13
	b .L__sysy_par_body_1_bb1
	.size __sysy_par_body_1, .-__sysy_par_body_1
	.p2align 2
	.global __sysy_par_body_2
	.type __sysy_par_body_2, %function
__sysy_par_body_2:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #48
	stp x19, x20, [sp]
	adrp x10, current
	stp x21, x22, [sp, #16]
	adrp x9, next
	mov w21, w0
	add x10, x10, :lo12:current
	add x9, x9, :lo12:next
	movz w19, #3072
	smaddl x10, w21, w19, x10
	smaddl x9, w21, w19, x9
	str x23, [sp, #32]
	mov w20, w1
	mov x23, x9
	mov x22, x10
.L__sysy_par_body_2_bb1:
	cmp w21, w20
	b.ge .L__sysy_par_body_2_bb2
.L__sysy_par_body_2_bb3:
	mov x0, x22
	mov x1, x23
	mov w2, w19
	bl memcpy
	add w21, w21, #1
	add x22, x22, #3072
	add x23, x23, #3072
	b .L__sysy_par_body_2_bb1
.L__sysy_par_body_2_bb2:
	ldp x22, x23, [sp, #24]
	ldp x20, x21, [sp, #8]
	ldr x19, [sp]
	add sp, sp, #48
	ldp xzr, x30, [sp], #16
	ret
	.size __sysy_par_body_2, .-__sysy_par_body_2
	.bss
	.global current
	.p2align 4
current:
	.zero 2359296
	.global next
	.p2align 4
next:
	.zero 2359296
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
	cmp w0, #1
	b.eq .Lsysy_disp_1
	cmp w0, #2
	b.eq .Lsysy_disp_2
	ret
.Lsysy_disp_0:
	mov w0, w1
	mov w1, w2
	b __sysy_par_body_0
.Lsysy_disp_1:
	mov w0, w1
	mov w1, w2
	b __sysy_par_body_1
.Lsysy_disp_2:
	mov w0, w1
	mov w1, w2
	b __sysy_par_body_2

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
