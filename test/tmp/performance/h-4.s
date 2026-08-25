	.arch armv8-a
	.text
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #176
	stp x25, x26, [sp, #128]
	movz w25, #0
	stp x19, x20, [sp, #80]
	stp x21, x22, [sp, #96]
	stp x23, x24, [sp, #112]
	stp x27, x28, [sp, #144]
	bl getint
	mov w19, w0
	movz w0, #33
	bl _sysy_starttime
	str w19, [sp, #160]
	movz w24, #1
	orr x22, xzr, #0xffffffff80000000
	orr x21, xzr, #0x7fffffff
	movz w20, #0
	movz w19, #32768, lsl #16
.Lmain_bb1:
	ldr w9, [sp, #160]
	cbz w9, .Lmain_bb13
.Lmain_bb2:
	bl getint
	mov w28, w0
	bl getint
	mov w27, w0
	bl getint
	mov w26, w0
	cmp w28, w27
	b.ge .Lmain_bb22
.Lmain_bb3:
	cmp w26, #0
	cset w13, gt
	movz w9, #65535
	cmp w26, w9
	cset w12, le
	movn w9, #32768, lsl #16
	cmp w28, #0
	sub w9, w9, w26
	cset w10, ge
	add w23, w9, #1
	cmp w27, w23
	and w9, w13, w12
	cset w11, le
	sxtw x14, w28
	and w9, w9, w10
	cmp x14, x22
	and w9, w9, w24
	cset w10, ge
	and w9, w9, w11
	cmp x14, x21
	and w10, w9, w10
	sxtw x12, w27
	cset w11, le
	orr x9, xzr, #0xffffffff80000001
	cmp x12, x9
	and w9, w10, w11
	sub x13, x12, #1
	cset w10, ge
	cmp x13, x21
	and w9, w9, w10
	cset w10, le
	and w10, w9, w10
	sub x9, x21, x14
	cmp x9, x22
	movz x9, #0
	sub x9, x9, x14
	cset w11, ge
	cmp x9, #0
	sub x12, x21, x13
	movz x9, #0
	sub x13, x9, x13
	and w9, w10, w11
	cset w10, le
	cmp x12, x22
	cset w11, ge
	cmp x13, #0
	and w9, w9, w10
	cset w10, le
	and w9, w9, w11
	and w9, w9, w10
	cbz w9, .Lmain_bb5
.Lmain_bb4:
	movz w9, #3
	str w9, [sp, #16]
	movz w9, #1000
	str w9, [sp, #24]
	movz w9, #1001
	str w9, [sp, #32]
	movz w9, #26809
	movk w9, #297, lsl #16
	str w9, [sp, #48]
	movz w9, #501
	movk w9, #15232, lsl #16
	str w24, [sp]
	mov w0, w28
	str w24, [sp, #8]
	mov w1, w27
	str w20, [sp, #40]
	mov w2, w26
	str w24, [sp, #56]
	mov w3, w24
	str w9, [sp, #64]
	mov w4, w24
	str w20, [sp, #72]
	mov w5, w20
	movn w6, #0
	movn w7, #32768, lsl #16
	bl __compiler.summable_mod_sum
	mov w9, w0
	cmp w9, w19
	b.ne .Lmain_bb21
.Lmain_bb5:
	movz w9, #7
	mul w10, w26, w9
	sub w8, w27, w10
	cmp w28, w8
	cset w13, lt
	cmp w26, #0
	movz w9, #9362
	cset w12, gt
	movk w9, #4681, lsl #16
	cmp w26, w9
	add w9, w10, w19
	cset w10, le
	cmp w28, #0
	cset w11, ge
	cmp w27, w9
	cset w9, ge
	and w9, w12, w9
	cmp w27, w23
	and w9, w9, w10
	cset w10, le
	and w9, w9, w10
	and w9, w9, w11
	and w9, w9, w13
	cbz w9, .Lmain_bb16
.Lmain_bb14:
	mov w7, w28
	movz w11, #19923
	movz w16, #26809
	movz w10, #26581
	movz w15, #500
	movz w14, #65036
	movz w13, #65035
	movz w12, #501
	mov w0, w25
	movn w28, #32768, lsl #16
	movk w11, #4194, lsl #16
	movz w23, #1001
	movk w16, #297, lsl #16
	movk w10, #7051, lsl #16
	movk w15, #15232, lsl #16
	movk w14, #50303, lsl #16
	movk w13, #50303, lsl #16
	movk w12, #15232, lsl #16
.Lmain_bb6:
	sub w9, w28, w7
	cmp w7, w9
	csel w17, w9, w7, lt
	add w6, w7, w26
	sub w9, w28, w6
	cmp w6, w9
	csel w7, w9, w6, lt
	add w9, w17, w17, lsl #1
	smull x9, w9, w11
	asr x9, x9, #38
	add w5, w6, w26
	add w9, w9, w9, lsr #31
	madd w6, w9, w23, w17
	sub w9, w28, w5
	cmp w5, w9
	csel w17, w9, w5, lt
	add w9, w7, w7, lsl #1
	smull x9, w9, w11
	asr x9, x9, #38
	add w9, w9, w9, lsr #31
	madd w2, w9, w23, w7
	add w4, w5, w26
	sub w9, w28, w4
	cmp w4, w9
	csel w5, w9, w4, lt
	add w9, w17, w17, lsl #1
	smull x9, w9, w11
	asr x9, x9, #38
	add w7, w4, w26
	add w9, w9, w9, lsr #31
	madd w4, w9, w23, w17
	sub w9, w28, w7
	cmp w7, w9
	csel w17, w9, w7, lt
	add w9, w5, w5, lsl #1
	smull x9, w9, w11
	asr x9, x9, #38
	add w9, w9, w9, lsr #31
	madd w3, w9, w23, w5
	add w7, w7, w26
	sub w9, w28, w7
	cmp w7, w9
	csel w5, w9, w7, lt
	add w9, w17, w17, lsl #1
	smull x9, w9, w11
	asr x9, x9, #38
	add w9, w9, w9, lsr #31
	madd w17, w9, w23, w17
	add w7, w7, w26
	sub w9, w28, w7
	cmp w7, w9
	csel w1, w9, w7, lt
	smull x9, w6, w10
	asr x9, x9, #53
	add w9, w9, w9, lsr #31
	msub w9, w9, w16, w6
	add w9, w0, w9
	add w6, w9, #1
	add w9, w5, w5, lsl #1
	smull x9, w9, w11
	asr x9, x9, #38
	add w9, w9, w9, lsr #31
	madd w5, w9, w23, w5
	smull x9, w2, w10
	asr x9, x9, #53
	add w9, w9, w9, lsr #31
	msub w9, w9, w16, w2
	add w9, w6, w9
	add w2, w9, #1
	add w9, w1, w1, lsl #1
	smull x9, w9, w11
	asr x9, x9, #38
	add w9, w9, w9, lsr #31
	madd w6, w9, w23, w1
	smull x9, w4, w10
	asr x9, x9, #53
	add w9, w9, w9, lsr #31
	msub w9, w9, w16, w4
	add w9, w2, w9
	add w7, w7, w26
	add w2, w9, #1
	sub w9, w28, w7
	cmp w7, w9
	csel w4, w9, w7, lt
	smull x9, w3, w10
	asr x9, x9, #53
	add w9, w9, w9, lsr #31
	msub w9, w9, w16, w3
	add w9, w2, w9
	add w3, w9, #1
	smull x9, w17, w10
	asr x9, x9, #53
	add w9, w9, w9, lsr #31
	msub w9, w9, w16, w17
	add w9, w3, w9
	add w3, w9, #1
	add w9, w4, w4, lsl #1
	smull x9, w9, w11
	asr x9, x9, #38
	add w9, w9, w9, lsr #31
	madd w17, w9, w23, w4
	smull x9, w5, w10
	asr x9, x9, #53
	add w9, w9, w9, lsr #31
	msub w9, w9, w16, w5
	add w9, w3, w9
	add w5, w9, #1
	smull x9, w6, w10
	asr x9, x9, #53
	add w9, w9, w9, lsr #31
	msub w9, w9, w16, w6
	add w6, w5, w9
	smull x9, w17, w10
	asr x9, x9, #53
	add w9, w9, w9, lsr #31
	msub w5, w9, w16, w17
	cmp w6, w15
	add w17, w6, #1
	add w9, w6, w14
	csel w17, w9, w17, ge
	cmp w17, w13
	add w9, w17, w12
	csel w9, w9, w17, le
	add w9, w9, w5
	cmp w9, w15
	add w17, w9, #1
	add w9, w9, w14
	csel w17, w9, w17, ge
	cmp w17, w13
	add w9, w17, w12
	add w7, w7, w26
	csel w0, w9, w17, le
	cmp w7, w8
	b.lt .Lmain_bb6
.Lmain_bb7:
	cmp w7, w27
	b.ge .Lmain_bb19
.Lmain_bb17:
	mov w28, w7
.Lmain_bb8:
	movz w12, #19923
	movz w14, #26809
	movz w11, #26581
	movz w13, #501
	movz w10, #15363
	movn w23, #32768, lsl #16
	movn w17, #49152, lsl #16
	movz w16, #8192, lsl #16
	movk w12, #4194, lsl #16
	movz w15, #1001
	movk w14, #297, lsl #16
	movk w11, #7051, lsl #16
	movk w13, #15232, lsl #16
	movk w10, #35246, lsl #16
.Lmain_bb9:
	sub w9, w23, w28
	cmp w28, w9
	csel w8, w9, w28, lt
	sub w9, w17, w8
	cmp w8, w9
	csel w8, w9, w8, lt
	sub w9, w16, w8
	cmp w8, w9
	csel w8, w9, w8, lt
	add w9, w8, w8, lsl #1
	smull x9, w9, w12
	asr x9, x9, #38
	add w9, w9, w9, lsr #31
	madd w8, w9, w15, w8
	smull x9, w8, w11
	asr x9, x9, #53
	add w9, w9, w9, lsr #31
	msub w9, w9, w14, w8
	add w9, w0, w9
	add w8, w9, #1
	smull x9, w8, w10
	asr x9, x9, #32
	add w9, w9, w8
	asr w9, w9, #29
	add w9, w9, w9, lsr #31
	msub w0, w9, w13, w8
	add w28, w28, w26
	cmp w28, w27
	b.lt .Lmain_bb9
.Lmain_bb20:
	mov w9, w0
.Lmain_bb10:
	mov w0, w9
.Lmain_bb12:
	bl putint
	movz w0, #10
	bl putch
	ldr w9, [sp, #160]
	sub w9, w9, #1
	str w9, [sp, #160]
	b .Lmain_bb1
.Lmain_bb13:
	movz w0, #42
	bl _sysy_stoptime
	ldp x27, x28, [sp, #144]
	ldp x25, x26, [sp, #128]
	ldp x23, x24, [sp, #112]
	ldp x21, x22, [sp, #96]
	ldp x19, x20, [sp, #80]
	movz w0, #0
	add sp, sp, #176
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb16:
	mov w0, w25
	b .Lmain_bb8
.Lmain_bb19:
	mov w9, w0
	b .Lmain_bb10
.Lmain_bb21:
	mov w0, w9
	b .Lmain_bb12
.Lmain_bb22:
	mov w0, w25
	b .Lmain_bb12
	.size main, .-main
	.p2align 2
	.global __compiler.summable_mod_sum
	.type __compiler.summable_mod_sum, %function
__compiler.summable_mod_sum:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #288
	ldr w9, [sp, #312]
	str w9, [sp, #112]
	ldr w9, [sp, #320]
	str w9, [sp, #116]
	ldr w9, [sp, #328]
	str w9, [sp, #120]
	ldr w9, [sp, #336]
	str w9, [sp, #124]
	ldr w9, [sp, #344]
	str w9, [sp, #128]
	ldr w9, [sp, #352]
	str w9, [sp, #132]
	ldr w9, [sp, #360]
	stp x21, x22, [sp, #32]
	str w9, [sp, #136]
	sxtw x22, w0
	sxtw x9, w1
	sxtw x21, w2
	sub x9, x9, x22
	add x9, x9, x21
	sub x9, x9, #1
	sdiv x9, x9, x21
	ldr w11, [sp, #304]
	cmp w3, #0
	stp x19, x20, [sp, #16]
	cset w20, ne
	cmp w11, #0
	stp x25, x26, [sp, #64]
	cset w19, ne
	stp x27, x28, [sp, #80]
	sxtw x28, w4
	str x9, [sp, #248]
	sxtw x26, w6
	cmp w19, #0
	csel x9, x26, x28, ne
	str x9, [sp, #216]
	sxtw x27, w5
	sxtw x25, w7
	cmp w19, #0
	csel x9, x25, x27, ne
	str x9, [sp, #224]
	cmp w19, #0
	csel x9, x28, x26, ne
	str x9, [sp, #232]
	cmp w19, #0
	csel x9, x27, x25, ne
	str x9, [sp, #240]
	ldr x9, [sp, #248]
	ldr w10, [sp, #376]
	stp x23, x24, [sp, #48]
	ldr w23, [sp, #368]
	cmp x9, #1, lsl #12
	sxtw x9, w10
	str x9, [sp, #208]
	movz x10, #0
	stp w4, w5, [sp, #96]
	add x9, sp, #264
	stp w6, w7, [sp, #104]
	str x10, [x9]
	add x9, sp, #272
	str x10, [x9]
	ldr x9, [sp, #248]
	sub x9, x9, #1, lsl #12
	csel x9, x9, x10, gt
	str x9, [sp, #256]
	ldr w9, [sp, #112]
	sxtw x9, w9
	str x9, [sp, #144]
	ldr w9, [sp, #116]
	sxtw x9, w9
	str x9, [sp, #152]
	ldr w9, [sp, #120]
	sxtw x9, w9
	str x9, [sp, #160]
	ldr w9, [sp, #124]
	sxtw x9, w9
	str x9, [sp, #168]
	ldr w9, [sp, #128]
	sxtw x9, w9
	str x9, [sp, #176]
	ldr w9, [sp, #132]
	sxtw x9, w9
	str x9, [sp, #184]
	ldr w9, [sp, #136]
	sxtw x9, w9
	str x9, [sp, #192]
	sxtw x9, w23
	str x9, [sp, #200]
.L__compiler.summable_mod_sum_bb1:
	add x9, sp, #272
	ldr x11, [x9]
	ldr x9, [sp, #256]
	cmp x11, x9
	b.ge .L__compiler.summable_mod_sum_bb12
.L__compiler.summable_mod_sum_bb2:
	mul x9, x21, x11
	add x13, x22, x9
	mul x9, x28, x13
	add x10, x9, x27
	mul x9, x26, x13
	ldr x16, [sp, #256]
	add x9, x9, x25
	cmp x10, x9
	cset w9, lt
	and w15, w20, w9
	add x24, x11, #1
	movz x14, #2
.L__compiler.summable_mod_sum_bb3:
	cmp x24, x16
	b.ge .L__compiler.summable_mod_sum_bb8
.L__compiler.summable_mod_sum_bb4:
	sub x9, x16, x24
	sdiv x9, x9, x14
	add x12, x24, x9
	mul x9, x21, x12
	add x9, x22, x9
	mul x10, x28, x9
	mul x9, x26, x9
	add x10, x10, x27
	add x9, x9, x25
	cmp x10, x9
	cset w9, lt
	and w9, w20, w9
	cmp w9, w15
	b.eq .L__compiler.summable_mod_sum_bb5
.L__compiler.summable_mod_sum_bb6:
	mov x16, x12
	b .L__compiler.summable_mod_sum_bb3
.L__compiler.summable_mod_sum_bb5:
	add x24, x12, #1
	b .L__compiler.summable_mod_sum_bb3
.L__compiler.summable_mod_sum_bb8:
	cbz w15, .L__compiler.summable_mod_sum_bb10
.L__compiler.summable_mod_sum_bb9:
	ldr x9, [sp, #216]
	mul x10, x9, x13
	ldr x9, [sp, #224]
	add x0, x10, x9
	ldr x9, [sp, #216]
	mul x1, x9, x21
	ldr x9, [sp, #184]
	ldp x3, x4, [sp, #144]
	ldp x5, x6, [sp, #160]
	ldr x7, [sp, #176]
	sub x2, x24, x11
	str x9, [sp]
	bl __compiler.sms.sum_monotone
	mov x10, x0
.L__compiler.summable_mod_sum_bb11:
	add x9, sp, #264
	ldr x9, [x9]
	add x10, x9, x10
	add x9, sp, #264
	str x10, [x9]
	add x9, sp, #272
	str x24, [x9]
	b .L__compiler.summable_mod_sum_bb1
.L__compiler.summable_mod_sum_bb10:
	ldr x9, [sp, #232]
	mul x10, x9, x13
	ldr x9, [sp, #240]
	add x0, x10, x9
	ldr x9, [sp, #232]
	mul x1, x9, x21
	ldr x9, [sp, #184]
	ldp x3, x4, [sp, #144]
	ldp x5, x6, [sp, #160]
	ldr x7, [sp, #176]
	sub x2, x24, x11
	str x9, [sp]
	bl __compiler.sms.sum_monotone
	mov x10, x0
	b .L__compiler.summable_mod_sum_bb11
.L__compiler.summable_mod_sum_bb12:
	add x9, sp, #264
	ldr x11, [x9]
	ldr x9, [sp, #256]
	ldr x10, [sp, #192]
	mul x9, x10, x9
	add x10, x11, x9
	ldr x9, [sp, #208]
	add x11, x10, x9
	ldr x9, [sp, #200]
	sdiv x10, x11, x9
	msub x10, x10, x9, x11
	cmp x10, #0
	add x9, x10, x9
	csel x11, x9, x10, lt
	ldr x9, [sp, #200]
	ldr x13, [sp, #256]
	sub x9, x11, x9
	mov w10, w9
	cmp x11, #0
	movz w9, #0
	mov w12, w11
	csel w14, w9, w10, eq
.L__compiler.summable_mod_sum_bb13:
	ldr x9, [sp, #248]
	cmp x13, x9
	b.ge .L__compiler.summable_mod_sum_bb15
.L__compiler.summable_mod_sum_bb14:
	mul x9, x21, x13
	add x9, x22, x9
	mov w11, w9
	ldp w10, w9, [sp, #96]
	madd w16, w10, w11, w9
	ldp w10, w9, [sp, #104]
	madd w15, w10, w11, w9
	cmp w16, w15
	cset w9, lt
	cmp w19, #0
	csel w10, w15, w16, ne
	cmp w19, #0
	and w11, w20, w9
	csel w9, w16, w15, ne
	cmp w11, #0
	csel w11, w10, w9, ne
	ldr w9, [sp, #116]
	mul w10, w11, w9
	ldr w9, [sp, #120]
	sdiv w10, w10, w9
	ldr w9, [sp, #124]
	mul w10, w10, w9
	ldr w9, [sp, #112]
	madd w10, w11, w9, w10
	ldr w9, [sp, #128]
	add w11, w10, w9
	ldr w9, [sp, #132]
	sdiv w10, w11, w9
	msub w9, w10, w9, w11
	add w11, w12, w9
	add w10, w14, w9
	ldr w9, [sp, #136]
	add w12, w11, w9
	add w11, w10, w9
	sdiv w10, w12, w23
	sdiv w9, w11, w23
	msub w12, w10, w23, w12
	msub w14, w9, w23, w11
	add x13, x13, #1
	b .L__compiler.summable_mod_sum_bb13
.L__compiler.summable_mod_sum_bb15:
	ldp x27, x28, [sp, #80]
	ldp x25, x26, [sp, #64]
	ldp x23, x24, [sp, #48]
	ldp x21, x22, [sp, #32]
	ldp x19, x20, [sp, #16]
	cmp w12, w14
	movz w9, #32768, lsl #16
	csel w0, w12, w9, eq
	add sp, sp, #288
	ldp xzr, x30, [sp], #16
	ret
	.size __compiler.summable_mod_sum, .-__compiler.summable_mod_sum
	.p2align 2
	.global __compiler.sms.floor_div
	.type __compiler.sms.floor_div, %function
__compiler.sms.floor_div:
	sdiv x11, x0, x1
	msub x9, x11, x1, x0
	cmp x9, #0
	movz x10, #0
	movz x9, #1
	csel x9, x9, x10, lt
	sub x0, x11, x9
	ret
	.size __compiler.sms.floor_div, .-__compiler.sms.floor_div
	.p2align 2
	.global __compiler.sms.gcd
	.type __compiler.sms.gcd, %function
__compiler.sms.gcd:
	movz x9, #0
	cmp x0, #0
	sub x9, x9, x0
	mov x10, x1
	csel x0, x9, x0, lt
.L__compiler.sms.gcd_bb1:
	cmp x10, #0
	b.eq .L__compiler.sms.gcd_bb3
.L__compiler.sms.gcd_bb2:
	sdiv x9, x0, x10
	msub x9, x9, x10, x0
	mov x0, x10
	mov x10, x9
	b .L__compiler.sms.gcd_bb1
.L__compiler.sms.gcd_bb3:
	ret
	.size __compiler.sms.gcd, .-__compiler.sms.gcd
	.p2align 2
	.global __compiler.sms.floor_sum
	.type __compiler.sms.floor_sum, %function
__compiler.sms.floor_sum:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #48
	stp x19, x20, [sp]
	stp x21, x22, [sp, #16]
	mov x20, x0
	mov x19, x1
	mov x22, x2
	str x23, [sp, #32]
	mov x21, x3
	mov x0, x22
	mov x1, x19
	bl __compiler.sms.floor_div
	mov x23, x0
	mov x0, x21
	mov x1, x19
	bl __compiler.sms.floor_div
	sub x9, x20, #1
	mul x9, x20, x9
	movz x16, #2
	sdiv x9, x9, x16
	mul x10, x23, x9
	mul x12, x23, x19
	mul x11, x0, x19
	mul x9, x0, x20
	sub x13, x22, x12
	sub x12, x21, x11
	add x0, x10, x9
	mov x15, x20
.L__compiler.sms.floor_sum_bb1:
	cmp x13, x19
	b.ge .L__compiler.sms.floor_sum_bb2
.L__compiler.sms.floor_sum_bb8:
	mov x11, x13
.L__compiler.sms.floor_sum_bb3:
	cmp x12, x19
	b.lt .L__compiler.sms.floor_sum_bb5
.L__compiler.sms.floor_sum_bb4:
	sdiv x10, x12, x19
	mul x9, x15, x10
	msub x12, x10, x19, x12
	add x0, x0, x9
.L__compiler.sms.floor_sum_bb5:
	mul x9, x11, x15
	add x9, x9, x12
	cmp x9, x19
	b.lt .L__compiler.sms.floor_sum_bb6
.L__compiler.sms.floor_sum_bb7:
	sdiv x15, x9, x19
	msub x12, x15, x19, x9
	mov x13, x19
	mov x19, x11
	b .L__compiler.sms.floor_sum_bb1
.L__compiler.sms.floor_sum_bb2:
	sdiv x10, x13, x19
	sub x9, x15, #1
	mul x9, x9, x15
	mul x9, x9, x10
	sdiv x9, x9, x16
	msub x11, x10, x19, x13
	add x0, x0, x9
	b .L__compiler.sms.floor_sum_bb3
.L__compiler.sms.floor_sum_bb6:
	ldp x22, x23, [sp, #24]
	ldp x20, x21, [sp, #8]
	ldr x19, [sp]
	add sp, sp, #48
	ldp xzr, x30, [sp], #16
	ret
	.size __compiler.sms.floor_sum, .-__compiler.sms.floor_sum
	.p2align 2
	.global __compiler.sms.sum_nonnegative
	.type __compiler.sms.sum_nonnegative, %function
__compiler.sms.sum_nonnegative:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #16
	stp x19, x20, [sp]
	mov x12, x0
	mov x11, x1
	mov x0, x2
	mov x20, x3
	cmp x11, #0
	b.ge .L__compiler.sms.sum_nonnegative_bb2
.L__compiler.sms.sum_nonnegative_bb1:
	sub x9, x0, #1
	mul x10, x11, x9
	movz x9, #0
	add x12, x12, x10
	sub x11, x9, x11
.L__compiler.sms.sum_nonnegative_bb2:
	sub x9, x0, #1
	mul x10, x0, x9
	movz x9, #2
	sdiv x9, x10, x9
	mul x10, x0, x12
	mul x9, x11, x9
	add x19, x10, x9
	mov x1, x20
	mov x2, x11
	mov x3, x12
	bl __compiler.sms.floor_sum
	mul x9, x20, x0
	sub x0, x19, x9
	ldp x19, x20, [sp]
	add sp, sp, #16
	ldp xzr, x30, [sp], #16
	ret
	.size __compiler.sms.sum_nonnegative, .-__compiler.sms.sum_nonnegative
	.p2align 2
	.global __compiler.sms.sum_signed
	.type __compiler.sms.sum_signed, %function
__compiler.sms.sum_signed:
	stp xzr, x30, [sp, #-16]!
	mov x11, x2
	mov x12, x1
	sub x9, x11, #1
	mul x9, x12, x9
	mov x13, x0
	mov x10, x3
	add x9, x13, x9
	cmp x13, #0
	b.lt .L__compiler.sms.sum_signed_bb3
.L__compiler.sms.sum_signed_bb1:
	cmp x9, #0
	b.lt .L__compiler.sms.sum_signed_bb3
.L__compiler.sms.sum_signed_bb2:
	mov x0, x13
	mov x1, x12
	mov x2, x11
	mov x3, x10
	bl __compiler.sms.sum_nonnegative
	ldp xzr, x30, [sp], #16
	ret
.L__compiler.sms.sum_signed_bb3:
	cmp x13, #0
	b.gt .L__compiler.sms.sum_signed_bb6
.L__compiler.sms.sum_signed_bb4:
	cmp x9, #0
	b.gt .L__compiler.sms.sum_signed_bb6
.L__compiler.sms.sum_signed_bb5:
	movz x9, #0
	sub x0, x9, x13
	sub x1, x9, x12
	mov x2, x11
	mov x3, x10
	bl __compiler.sms.sum_nonnegative
	movz x9, #0
	sub x0, x9, x0
	ldp xzr, x30, [sp], #16
	ret
.L__compiler.sms.sum_signed_bb6:
	movz x0, #0
	ldp xzr, x30, [sp], #16
	ret
	.size __compiler.sms.sum_signed, .-__compiler.sms.sum_signed
	.p2align 2
	.global __compiler.sms.key
	.type __compiler.sms.key, %function
__compiler.sms.key:
	stp xzr, x30, [sp, #-16]!
	mov x10, x0
	cmp x1, #0
	b.eq .L__compiler.sms.key_bb1
.L__compiler.sms.key_bb2:
	movz x9, #32768, lsl #16
	add x0, x10, x9
	movz x1, #1, lsl #32
	bl __compiler.sms.floor_div
	ldp xzr, x30, [sp], #16
	ret
.L__compiler.sms.key_bb1:
	movz x1, #32768, lsl #16
	mov x0, x10
	bl __compiler.sms.floor_div
	ldp xzr, x30, [sp], #16
	ret
	.size __compiler.sms.key, .-__compiler.sms.key
	.p2align 2
	.global __compiler.sms.end_key_run
	.type __compiler.sms.end_key_run, %function
__compiler.sms.end_key_run:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #64
	stp x19, x20, [sp]
	stp x23, x24, [sp, #32]
	mov x23, x1
	mov x19, x3
	mul x9, x23, x19
	stp x21, x22, [sp, #16]
	mov x24, x0
	stp x25, x26, [sp, #48]
	mov x22, x4
	mov x25, x2
	add x0, x24, x9
	mov x1, x22
	bl __compiler.sms.key
	add x26, x19, #1
	mov x21, x0
	movz x20, #2
.L__compiler.sms.end_key_run_bb1:
	cmp x26, x25
	b.ge .L__compiler.sms.end_key_run_bb6
.L__compiler.sms.end_key_run_bb2:
	sub x9, x25, x26
	sdiv x9, x9, x20
	add x19, x26, x9
	mul x9, x23, x19
	add x0, x24, x9
	mov x1, x22
	bl __compiler.sms.key
	cmp x0, x21
	b.eq .L__compiler.sms.end_key_run_bb3
.L__compiler.sms.end_key_run_bb4:
	mov x25, x19
	b .L__compiler.sms.end_key_run_bb1
.L__compiler.sms.end_key_run_bb3:
	add x26, x19, #1
	b .L__compiler.sms.end_key_run_bb1
.L__compiler.sms.end_key_run_bb6:
	mov x0, x26
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	add sp, sp, #64
	ldp xzr, x30, [sp], #16
	ret
	.size __compiler.sms.end_key_run, .-__compiler.sms.end_key_run
	.p2align 2
	.global __compiler.sms.sum_monotone
	.type __compiler.sms.sum_monotone, %function
__compiler.sms.sum_monotone:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #304
	ldr x9, [sp, #320]
	stp x21, x22, [sp, #16]
	add x22, sp, #248
	stp x19, x20, [sp]
	movz x10, #0
	stp x23, x24, [sp, #32]
	stp x25, x26, [sp, #48]
	stp x27, x28, [sp, #64]
	stp x0, x1, [sp, #80]
	stp x2, x3, [sp, #96]
	stp x4, x5, [sp, #112]
	stp x6, x7, [sp, #128]
	str x9, [sp, #144]
	add x9, sp, #256
	str x10, [x22]
	add x21, sp, #280
	str x10, [x9]
	add x20, sp, #288
.L__compiler.sms.sum_monotone_bb1:
	add x9, sp, #256
	ldr x19, [x9]
	ldr x9, [sp, #96]
	cmp x19, x9
	b.ge .L__compiler.sms.sum_monotone_bb21
.L__compiler.sms.sum_monotone_bb2:
	ldr x9, [sp, #80]
	ldr x10, [sp, #112]
	mul x0, x10, x9
	ldr x9, [sp, #88]
	ldr x2, [sp, #96]
	mul x9, x10, x9
	str x9, [sp, #152]
	movz x4, #0
	mov x1, x9
	mov x3, x19
	bl __compiler.sms.end_key_run
	ldr x9, [sp, #88]
	mul x10, x9, x19
	sub x9, x0, x19
	stp x0, x9, [sp, #160]
	ldr x9, [sp, #80]
	ldr x0, [sp, #152]
	ldr x1, [sp, #120]
	add x9, x9, x10
	str x9, [sp, #176]
	bl __compiler.sms.gcd
	ldr x9, [sp, #120]
	sdiv x9, x9, x0
	str x9, [sp, #184]
	ldr x9, [sp, #168]
	ldr x10, [sp, #184]
	cmp x10, x9
	movz x10, #0
	add x9, sp, #264
	str x10, [x9]
	ldr x9, [sp, #168]
	ldr x10, [sp, #184]
	csel x9, x10, x9, lt
	str x9, [sp, #192]
.L__compiler.sms.sum_monotone_bb3:
	add x9, sp, #264
	ldr x9, [x9]
	str x9, [sp, #200]
	ldp x9, x10, [sp, #192]
	cmp x10, x9
	b.ge .L__compiler.sms.sum_monotone_bb20
.L__compiler.sms.sum_monotone_bb4:
	ldr x9, [sp, #200]
	ldr x10, [sp, #88]
	mul x10, x10, x9
	ldr x9, [sp, #176]
	add x9, x9, x10
	ldr x10, [sp, #112]
	mul x11, x10, x9
	str x9, [sp, #216]
	ldr x9, [sp, #184]
	ldr x10, [sp, #152]
	mul x10, x10, x9
	ldr x9, [sp, #168]
	sub x12, x9, #1
	ldr x9, [sp, #200]
	sub x12, x12, x9
	ldr x9, [sp, #184]
	sdiv x12, x12, x9
	ldr x9, [sp, #120]
	sxtw x11, w11
	sdiv x9, x11, x9
	str x9, [sp, #224]
	ldr x9, [sp, #120]
	add x10, x11, x10
	sdiv x11, x10, x9
	movz x10, #0
	add x9, sp, #272
	str x10, [x9]
	add x9, x12, #1
	str x9, [sp, #208]
	ldr x9, [sp, #224]
	sub x9, x11, x9
	str x9, [sp, #232]
.L__compiler.sms.sum_monotone_bb5:
	add x9, sp, #272
	ldr x19, [x9]
	ldr x9, [sp, #208]
	cmp x19, x9
	b.ge .L__compiler.sms.sum_monotone_bb19
.L__compiler.sms.sum_monotone_bb6:
	ldr x9, [sp, #224]
	ldr x10, [sp, #128]
	mul x0, x10, x9
	ldr x9, [sp, #232]
	ldr x2, [sp, #208]
	mul x1, x10, x9
	movz x4, #1
	mov x3, x19
	bl __compiler.sms.end_key_run
	ldr x9, [sp, #184]
	ldr x10, [sp, #88]
	mul x9, x10, x9
	mul x10, x9, x19
	ldr x9, [sp, #216]
	add x10, x9, x10
	ldr x9, [sp, #104]
	mul x10, x9, x10
	ldr x9, [sp, #232]
	mul x11, x9, x19
	ldr x9, [sp, #224]
	add x11, x9, x11
	ldr x9, [sp, #128]
	mul x9, x9, x11
	sxtw x9, w9
	add x10, x10, x9
	ldr x9, [sp, #136]
	add x27, x10, x9
	ldr x9, [sp, #184]
	ldr x10, [sp, #88]
	mul x10, x10, x9
	ldr x9, [sp, #104]
	mul x11, x9, x10
	ldr x9, [sp, #232]
	ldr x10, [sp, #128]
	mul x9, x10, x9
	add x26, x11, x9
	str x0, [sp, #240]
	movz x9, #0
	str x9, [x21]
	sub x28, x0, x19
.L__compiler.sms.sum_monotone_bb7:
	ldr x19, [x21]
	cmp x19, x28
	b.ge .L__compiler.sms.sum_monotone_bb18
.L__compiler.sms.sum_monotone_bb8:
	movz x4, #1
	mov x0, x27
	mov x1, x26
	mov x2, x28
	mov x3, x19
	bl __compiler.sms.end_key_run
	mul x9, x26, x19
	add x9, x27, x9
	sxtw x23, w9
	mov x25, x0
	movz x9, #0
	str x9, [x20]
	sub x24, x25, x19
.L__compiler.sms.sum_monotone_bb9:
	ldr x14, [x20]
	cmp x14, x24
	b.ge .L__compiler.sms.sum_monotone_bb17
.L__compiler.sms.sum_monotone_bb10:
	mul x9, x26, x14
	add x9, x23, x9
	cmp x9, #0
	add x19, x14, #1
	cset w13, lt
	mov x12, x24
	movz x11, #2
.L__compiler.sms.sum_monotone_bb11:
	cmp x19, x12
	b.ge .L__compiler.sms.sum_monotone_bb16
.L__compiler.sms.sum_monotone_bb12:
	sub x9, x12, x19
	sdiv x9, x9, x11
	add x10, x19, x9
	mul x9, x26, x10
	add x9, x23, x9
	cmp x9, #0
	cset w9, lt
	cmp w9, w13
	b.eq .L__compiler.sms.sum_monotone_bb13
.L__compiler.sms.sum_monotone_bb14:
	mov x12, x10
	b .L__compiler.sms.sum_monotone_bb11
.L__compiler.sms.sum_monotone_bb13:
	add x19, x10, #1
	b .L__compiler.sms.sum_monotone_bb11
.L__compiler.sms.sum_monotone_bb16:
	ldr x3, [sp, #144]
	mul x9, x26, x14
	add x0, x23, x9
	sub x2, x19, x14
	mov x1, x26
	bl __compiler.sms.sum_signed
	ldr x9, [x22]
	add x9, x9, x0
	str x9, [x22]
	str x19, [x20]
	b .L__compiler.sms.sum_monotone_bb9
.L__compiler.sms.sum_monotone_bb17:
	str x25, [x21]
	b .L__compiler.sms.sum_monotone_bb7
.L__compiler.sms.sum_monotone_bb18:
	ldr x10, [sp, #240]
	add x9, sp, #272
	str x10, [x9]
	b .L__compiler.sms.sum_monotone_bb5
.L__compiler.sms.sum_monotone_bb19:
	ldr x9, [sp, #200]
	add x10, x9, #1
	add x9, sp, #264
	str x10, [x9]
	b .L__compiler.sms.sum_monotone_bb3
.L__compiler.sms.sum_monotone_bb20:
	ldr x10, [sp, #160]
	add x9, sp, #256
	str x10, [x9]
	b .L__compiler.sms.sum_monotone_bb1
.L__compiler.sms.sum_monotone_bb21:
	ldr x0, [x22]
	ldp x27, x28, [sp, #64]
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	add sp, sp, #304
	ldp xzr, x30, [sp], #16
	ret
	.size __compiler.sms.sum_monotone, .-__compiler.sms.sum_monotone
