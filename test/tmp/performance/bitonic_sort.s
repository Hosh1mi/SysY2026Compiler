	.arch armv8-a
	.text
	.p2align 2
	.global bitonic_merge
	.type bitonic_merge, %function
bitonic_merge:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #32
	stp x19, x20, [sp]
	mov w19, w1
	str x21, [sp, #16]
	mov w20, w2
.Lbitonic_merge_bb1:
	cmp w19, #1
	b.le .Lbitonic_merge_bb28
.Lbitonic_merge_bb2:
	asr w19, w19, #1
	cmp w20, #1
	b.eq .Lbitonic_merge_bb3
.Lbitonic_merge_bb15:
	adrp x10, arr
	adrp x9, arr
	add x10, x10, :lo12:arr
	add x9, x9, :lo12:arr
	add w16, w0, w19
	add x13, x10, w0, sxtw #2
	add x14, x9, w16, sxtw #2
	sub w15, w16, #1
	mov w11, w0
	orr w12, wzr, #0x80000001
.Lbitonic_merge_bb16:
	cmp w11, w15
	cset w10, lt
	cmp w16, w12
	cset w9, ge
	and w9, w9, w10
	cbz w9, .Lbitonic_merge_bb22
.Lbitonic_merge_bb17:
	ldr s17, [x13]
	ldr s16, [x14]
	fcmp s17, s16
	b.ge .Lbitonic_merge_bb19
.Lbitonic_merge_bb18:
	str s16, [x13]
	str s17, [x14]
.Lbitonic_merge_bb19:
	ldr s17, [x13, #4]
	ldr s16, [x14, #4]
	fcmp s17, s16
	add x10, x13, #4
	add x9, x14, #4
	b.ge .Lbitonic_merge_bb21
.Lbitonic_merge_bb20:
	str s16, [x13, #4]
	str s17, [x14, #4]
.Lbitonic_merge_bb21:
	add w11, w11, #2
	add x13, x10, #4
	add x14, x9, #4
	b .Lbitonic_merge_bb16
.Lbitonic_merge_bb3:
	adrp x10, arr
	adrp x9, arr
	add x10, x10, :lo12:arr
	add x9, x9, :lo12:arr
	add w16, w0, w19
	add x13, x10, w0, sxtw #2
	add x14, x9, w16, sxtw #2
	sub w15, w16, #1
	mov w11, w0
	orr w12, wzr, #0x80000001
.Lbitonic_merge_bb4:
	cmp w11, w15
	cset w10, lt
	cmp w16, w12
	cset w9, ge
	and w9, w9, w10
	cbz w9, .Lbitonic_merge_bb10
.Lbitonic_merge_bb5:
	ldr s17, [x13]
	ldr s16, [x14]
	fcmp s17, s16
	b.ls .Lbitonic_merge_bb7
.Lbitonic_merge_bb6:
	str s16, [x13]
	str s17, [x14]
.Lbitonic_merge_bb7:
	ldr s17, [x13, #4]
	ldr s16, [x14, #4]
	fcmp s17, s16
	add x10, x13, #4
	add x9, x14, #4
	b.ls .Lbitonic_merge_bb9
.Lbitonic_merge_bb8:
	str s16, [x13, #4]
	str s17, [x14, #4]
.Lbitonic_merge_bb9:
	add w11, w11, #2
	add x13, x10, #4
	add x14, x9, #4
	b .Lbitonic_merge_bb4
.Lbitonic_merge_bb10:
	cmp w11, w16
	b.ge .Lbitonic_merge_bb27
.Lbitonic_merge_bb29:
	mov x9, x14
	mov x10, x13
.Lbitonic_merge_bb11:
	cmp w11, w16
	b.ge .Lbitonic_merge_bb27
.Lbitonic_merge_bb12:
	ldr s17, [x10]
	ldr s16, [x9]
	fcmp s17, s16
	b.ls .Lbitonic_merge_bb14
.Lbitonic_merge_bb13:
	str s16, [x10]
	str s17, [x9]
.Lbitonic_merge_bb14:
	add w11, w11, #1
	add x10, x10, #4
	add x9, x9, #4
	b .Lbitonic_merge_bb11
.Lbitonic_merge_bb22:
	cmp w11, w16
	b.ge .Lbitonic_merge_bb27
.Lbitonic_merge_bb30:
	mov x9, x14
	mov x10, x13
.Lbitonic_merge_bb23:
	cmp w11, w16
	b.ge .Lbitonic_merge_bb27
.Lbitonic_merge_bb24:
	ldr s17, [x10]
	ldr s16, [x9]
	fcmp s17, s16
	b.ge .Lbitonic_merge_bb26
.Lbitonic_merge_bb25:
	str s16, [x10]
	str s17, [x9]
.Lbitonic_merge_bb26:
	add w11, w11, #1
	add x10, x10, #4
	add x9, x9, #4
	b .Lbitonic_merge_bb23
.Lbitonic_merge_bb27:
	add w21, w0, w19
	mov w1, w19
	mov w2, w20
	bl bitonic_merge
	mov w0, w21
	b .Lbitonic_merge_bb1
.Lbitonic_merge_bb28:
	ldp x20, x21, [sp, #8]
	ldr x19, [sp]
	add sp, sp, #32
	ldp xzr, x30, [sp], #16
	ret
	.size bitonic_merge, .-bitonic_merge
	.p2align 2
	.global bitonic_sort_rec
	.type bitonic_sort_rec, %function
bitonic_sort_rec:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #32
	stp x21, x22, [sp, #16]
	mov w21, w1
	stp x19, x20, [sp]
	mov w22, w0
	mov w20, w2
	cmp w21, #1
	b.gt .Lbitonic_sort_rec_bb1
.Lbitonic_sort_rec_bb2:
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	add sp, sp, #32
	ldp xzr, x30, [sp], #16
	ret
.Lbitonic_sort_rec_bb1:
	asr w19, w21, #1
	movz w2, #1
	mov w0, w22
	mov w1, w19
	bl bitonic_sort_rec
	add w0, w22, w19
	movz w2, #0
	mov w1, w19
	bl bitonic_sort_rec
	mov w0, w22
	mov w1, w21
	mov w2, w20
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	add sp, sp, #32
	ldp xzr, x30, [sp], #16
	b bitonic_merge
	.size bitonic_sort_rec, .-bitonic_sort_rec
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #48
	stp x21, x22, [sp, #16]
	movz w21, #0
	stp x19, x20, [sp]
	str d8, [sp, #32]
	bl getint
	mov w22, w0
	bl getint
	mov w19, w0
	movz w0, #51
	bl _sysy_starttime
	adrp x9, __sysy_par_ctx_0_0
	movz w1, #0
	str w19, [x9, :lo12:__sysy_par_ctx_0_0]
	mov w0, w1
	mov w2, w22
	bl __sysy_parallel_for
	movz w20, #0
	movz w19, #1
.Lmain_bb1:
	cmp w21, #510
	b.ge .Lmain_bb3
.Lmain_bb2:
	mov w0, w20
	mov w1, w22
	mov w2, w19
	bl bitonic_sort_rec
	add w21, w21, #1
	b .Lmain_bb1
.Lmain_bb3:
	adrp x10, arr
	adrp x9, arr
	sub w11, w22, #1
	add x10, x10, :lo12:arr
	add x9, x9, :lo12:arr
	ldr s17, [x10, w11, sxtw #2]
	ldr s16, [x9]
	fsub s8, s17, s16
	movz w0, #67
	bl _sysy_stoptime
	movz w9, #64487
	movk w9, #16671, lsl #16
	fmov s16, w9
	fsub s17, s8, s16
	movz w9, #0
	fmov s16, w9
	fsub s16, s16, s17
	fcmp s17, #0.0
	movz w9, #55050
	fcsel s17, s16, s17, lt
	movk w9, #15395, lsl #16
	fmov s16, w9
	fcmp s17, s16
	cset w0, le
	bl putint
	movz w0, #10
	bl putch
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	ldr d8, [sp, #32]
	movz w0, #0
	add sp, sp, #48
	ldp xzr, x30, [sp], #16
	ret
	.size main, .-main
	.p2align 2
	.global __sysy_par_body_0
	.type __sysy_par_body_0, %function
__sysy_par_body_0:
	adrp x10, __sysy_par_ctx_0_0
	ldr w17, [x10, :lo12:__sysy_par_ctx_0_0]
	adrp x9, arr
	mov w2, w0
	mov w16, w1
	add x9, x9, :lo12:arr
	movz w14, #35757
	add x3, x9, w2, sxtw #2
	sub w4, w16, #3
	orr w5, wzr, #0x80000003
	movz w6, #31
	movz w7, #10000
	movk w14, #26843, lsl #16
	movz w12, #17530, lsl #16
.L__sysy_par_body_0_bb1:
	cmp w2, w4
	cset w10, lt
	cmp w16, w5
	cset w9, ge
	and w9, w9, w10
	cbz w9, .L__sysy_par_body_0_bb6
.L__sysy_par_body_0_bb5:
	add w11, w2, #1
	add w10, w2, #2
	add w9, w2, #3
	madd w0, w2, w6, w17
	madd w1, w11, w6, w17
	madd w8, w10, w6, w17
	madd w15, w9, w6, w17
	smull x13, w0, w14
	smull x11, w1, w14
	smull x10, w8, w14
	smull x9, w15, w14
	asr x13, x13, #44
	asr x11, x11, #44
	asr x10, x10, #44
	asr x9, x9, #44
	add w13, w13, w13, lsr #31
	add w11, w11, w11, lsr #31
	add w10, w10, w10, lsr #31
	add w9, w9, w9, lsr #31
	msub w13, w13, w7, w0
	msub w11, w11, w7, w1
	msub w10, w10, w7, w8
	msub w9, w9, w7, w15
	scvtf s23, w13
	scvtf s21, w11
	scvtf s19, w10
	scvtf s17, w9
	fmov s22, w12
	fmov s20, w12
	fmov s18, w12
	fmov s16, w12
	fdiv s22, s23, s22
	fdiv s20, s21, s20
	fdiv s18, s19, s18
	fdiv s16, s17, s16
	add x9, x3, #4
	add x9, x9, #4
	stp s22, s20, [x3]
	add x9, x9, #4
	stp s18, s16, [x3, #8]
	add w2, w2, #4
	add x3, x9, #4
	b .L__sysy_par_body_0_bb1
.L__sysy_par_body_0_bb2:
	cmp w2, w16
	b.ge .L__sysy_par_body_0_bb3
.L__sysy_par_body_0_bb4:
	madd w12, w2, w14, w17
	smull x10, w12, w11
	asr x10, x10, #44
	add w10, w10, w10, lsr #31
	msub w10, w10, w13, w12
	scvtf s17, w10
	fmov s16, w9
	fdiv s16, s17, s16
	str s16, [x3], #4
	add w2, w2, #1
	b .L__sysy_par_body_0_bb2
.L__sysy_par_body_0_bb3:
	ret
.L__sysy_par_body_0_bb6:
	movz w11, #35757
	movz w14, #31
	movz w13, #10000
	movk w11, #26843, lsl #16
	movz w9, #17530, lsl #16
	b .L__sysy_par_body_0_bb2
	.size __sysy_par_body_0, .-__sysy_par_body_0
	.bss
	.global arr
	.p2align 4
arr:
	.zero 131072
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
