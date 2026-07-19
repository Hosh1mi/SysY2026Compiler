	.text
	.global main
	.p2align 2
main:
	sub sp, sp, #64
	adrp x10, cnt
	stp x20, x19, [sp, #0]
	stp x22, x21, [sp, #16]
	stp x24, x23, [sp, #32]
	str x30, [sp, #48]
	ldr w24, [x10, :lo12:cnt]
	bl getint
	adrp x22, keys
	add x22, x22, :lo12:keys
	mov w23, w0
	mov x0, x22
	bl getarray
	adrp x20, values
	add x20, x20, :lo12:values
	mov w21, w0
	mov x0, x20
	bl getarray
	adrp x9, requests
	add	x0, x9, :lo12:requests
	bl getarray
	mov w19, w0
	movz w0, #81
	bl _sysy_starttime
	movz w6, #0
main_label_while_cond_19:
	cmp w6, w21
	b.lt main_label_while_body_20
main_label_while_cond_22.preheader:
	adrp x10, __sysy_par_ctx_0_0
	str w23, [x10, :lo12:__sysy_par_ctx_0_0]
	mov w0, wzr
	mov w1, wzr
	mov w2, w19
	bl __sysy_parallel_for
	movz w0, #93
	bl _sysy_stoptime
	adrp x9, ans
	add x9, x9, :lo12:ans
	mov w0, w19
	mov x1, x9
	bl putarray
	adrp x10, hashmod
	str w23, [x10, :lo12:hashmod]
	adrp x10, cnt
	str w24, [x10, :lo12:cnt]
	mov w0, wzr
	b .Lmain_epilogue
main_label_while_body_20:
	ldr w5, [x22]
	adrp x3, head
	add x3, x3, :lo12:head
	ldr w4, [x20]
	sdiv w10, w5, w23
	msub w9, w10, w23, w5
	add x3, x3, w9, sxtw #2
	ldr w9, [x3]
	cbz w9, main_label_70
main_label_79:
	cbnz w9, main_label_82
main_label_96:
	ldr w0, [x3]
	adrp x1, next
	add w2, w24, #1
	add x1, x1, :lo12:next
	add x1, x1, w2, sxtw #2
	str w0, [x1]
	adrp x0, key
	add x0, x0, :lo12:key
	add x0, x0, w2, sxtw #2
	str w2, [x3]
	str w5, [x0]
	adrp x0, value
	add x0, x0, :lo12:value
	add x0, x0, w2, sxtw #2
	str w4, [x0]
	adrp x0, nextvalue
	add x0, x0, :lo12:nextvalue
	add x0, x0, w2, sxtw #2
	str wzr, [x0]
main_label_104:
	add w24, w24, #1
	add w6, w6, #1
	add x22, x22, #4
	add x20, x20, #4
	b main_label_while_cond_19
main_label_70:
	adrp x0, key
	add w9, w24, #1
	add x0, x0, :lo12:key
	add x0, x0, w9, sxtw #2
	str w9, [x3]
	str w5, [x0]
	adrp x0, value
	add x0, x0, :lo12:value
	add x0, x0, w9, sxtw #2
	str w4, [x0]
	adrp x0, next
	adrp x10, nextvalue
	add x0, x0, :lo12:next
	add x10, x10, :lo12:nextvalue
	add x0, x0, w9, sxtw #2
	add	x9, x10, w9, sxtw #2
	str wzr, [x0]
	str wzr, [x9]
	b main_label_104
main_label_82:
	adrp x0, key
	add x0, x0, :lo12:key
	add x0, x0, w9, sxtw #2
	ldr w0, [x0]
	cmp w0, w5
	b.eq main_label_86
main_label_93:
	adrp x0, next
	add x0, x0, :lo12:next
	add x0, x0, w9, sxtw #2
	ldr w9, [x0]
	b main_label_79
main_label_86:
	adrp x0, nextvalue
	add x0, x0, :lo12:nextvalue
	add x0, x0, w9, sxtw #2
	ldr w9, [x0]
	adrp x1, nextvalue
	add w2, w24, #1
	add x1, x1, :lo12:nextvalue
	add x1, x1, w2, sxtw #2
	str w9, [x1]
	adrp x9, value
	add x9, x9, :lo12:value
	add x9, x9, w2, sxtw #2
	str w2, [x0]
	str w4, [x9]
	b main_label_104
.Lmain_epilogue:
	ldp x20, x19, [sp, #0]
	ldp x22, x21, [sp, #16]
	ldp x24, x23, [sp, #32]
	ldr x30, [sp, #48]
	add sp, sp, #64
	ret
	.global __sysy_par_body_0
	.p2align 2
__sysy_par_body_0:
	adrp x10, __sysy_par_ctx_0_0
	ldr w7, [x10, :lo12:__sysy_par_ctx_0_0]
	mov w6, w0
__sysy_par_body_0_label_while_cond_22:
	cmp w6, w1
	b.lt __sysy_par_body_0_label_while_body_23
__sysy_par_body_0_label_par_ret:
	ret
__sysy_par_body_0_label_while_body_23:
	adrp x2, requests
	add x2, x2, :lo12:requests
	add x2, x2, w6, sxtw #2
	ldr w5, [x2]
	sdiv w10, w5, w7
	msub w2, w10, w7, w5
	adrp x10, head
	add x10, x10, :lo12:head
	add	x2, x10, w2, sxtw #2
	ldr w4, [x2]
__sysy_par_body_0_label_28:
	cbnz w4, __sysy_par_body_0_label_31
	movz w3, #0
__sysy_par_body_0_label_56:
	adrp x2, ans
	add x2, x2, :lo12:ans
	add x2, x2, w6, sxtw #2
	str w3, [x2]
	add w6, w6, #1
	b __sysy_par_body_0_label_while_cond_22
__sysy_par_body_0_label_31:
	adrp x2, key
	add x2, x2, :lo12:key
	add x2, x2, w4, sxtw #2
	ldr w2, [x2]
	cmp w2, w5
	b.eq __sysy_par_body_0_label_36.preheader
__sysy_par_body_0_label_52:
	adrp x2, next
	add x2, x2, :lo12:next
	add x2, x2, w4, sxtw #2
	ldr w4, [x2]
	b __sysy_par_body_0_label_28
__sysy_par_body_0_label_36.preheader:
	cbz w4, .L__sysy_par_body_0_edge_0
	movz w3, #0
	b __sysy_par_body_0_label_40
.L__sysy_par_body_0_edge_0:
	movz w3, #0
__sysy_par_body_0_label_46:
	add w2, w3, w3, lsl #1
	lsl w3, w3, #1
	cmp w5, #100
	csel w3, w3, w2, gt
	b __sysy_par_body_0_label_56
__sysy_par_body_0_label_40:
	adrp x2, value
	add x2, x2, :lo12:value
	adrp x10, nextvalue
	add x2, x2, w4, sxtw #2
	add x10, x10, :lo12:nextvalue
	ldr w2, [x2]
	add	x4, x10, w4, sxtw #2
	ldr w4, [x4]
	add w3, w3, w2
	cbz	w4, __sysy_par_body_0_label_46
	b __sysy_par_body_0_label_40
	.data
	.global hashmod
	.p2align 2
hashmod:
	.word 0

	.global cnt
	.p2align 2
cnt:
	.word 0

	.bss
	.global head
	.p2align 4
head:
	.zero 40000000

	.global next
	.p2align 4
next:
	.zero 40000000

	.global nextvalue
	.p2align 4
nextvalue:
	.zero 40000000

	.global key
	.p2align 4
key:
	.zero 40000000

	.global value
	.p2align 4
value:
	.zero 40000000

	.global keys
	.p2align 4
keys:
	.zero 40000000

	.global values
	.p2align 4
values:
	.zero 40000000

	.global requests
	.p2align 4
requests:
	.zero 40000000

	.global ans
	.p2align 4
ans:
	.zero 40000000

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
