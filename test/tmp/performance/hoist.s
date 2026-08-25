	.arch armv8-a
	.text
	.p2align 2
	.global func
	.type func, %function
func:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #160
	stp x19, x20, [sp]
	stp x21, x22, [sp, #16]
	stp x23, x24, [sp, #32]
	stp x25, x26, [sp, #48]
	stp x27, x28, [sp, #64]
	mov w10, w4
	mov w11, w5
	mov w12, w6
	mov w4, w7
	ldr w6, [sp, #176]
	ldr w7, [sp, #184]
	ldr w28, [sp, #192]
	ldr w27, [sp, #200]
	ldr w5, [sp, #208]
	ldr w9, [sp, #216]
	ldr w14, [sp, #224]
	adrp x13, loopCount
	ldr w13, [x13, :lo12:loopCount]
	str w13, [sp, #136]
	lsl w13, w0, #1
	str w13, [sp, #84]
	lsl w13, w1, #1
	str w13, [sp, #92]
	lsl w13, w2, #1
	str w13, [sp, #100]
	lsl w13, w3, #1
	str w13, [sp, #108]
	lsl w13, w10, #1
	str w13, [sp, #116]
	lsl w26, w11, #1
	lsl w25, w12, #1
	lsl w24, w4, #1
	lsl w23, w6, #1
	lsl w22, w7, #1
	lsl w21, w28, #1
	lsl w20, w27, #1
	lsl w13, w5, #1
	str w13, [sp, #140]
	lsl w13, w9, #1
	str w13, [sp, #144]
	lsl w13, w14, #1
	str w13, [sp, #148]
	lsl w13, w14, #5
	str w13, [sp, #152]
	lsl w13, w5, #2
	str w13, [sp, #120]
	lsl w19, w27, #2
	lsl w17, w28, #2
	lsl w16, w7, #2
	lsl w15, w6, #2
	lsl w14, w4, #2
	lsl w13, w12, #2
	stp w12, w4, [sp, #128]
	lsl w12, w11, #2
	str w11, [sp, #124]
	lsl w11, w10, #2
	str w10, [sp, #112]
	lsl w10, w3, #2
	str w3, [sp, #104]
	lsl w4, w2, #2
	str w2, [sp, #96]
	lsl w3, w1, #2
	str w1, [sp, #88]
	lsl w2, w0, #2
	str w0, [sp, #80]
	movz w0, #30
	movz w1, #27
	ldr w8, [sp, #140]
	mul w8, w8, w1
	movz w1, #34
	madd w5, w5, w0, w8
	ldr w8, [sp, #120]
	add w5, w5, w8
	add w5, w5, w8
	add w5, w5, w8
	madd w0, w9, w0, w5
	movz w9, #35
	ldr w5, [sp, #144]
	madd w5, w5, w9, w0
	ldr w9, [sp, #148]
	madd w5, w9, w1, w5
	ldr w9, [sp, #152]
	add w9, w5, w9
	add w9, w9, w2
	add w9, w8, w9
	add w9, w20, w9
	add w9, w27, w9
	add w9, w20, w9
	add w9, w27, w9
	add w9, w20, w9
	add w9, w27, w9
	add w9, w19, w9
	add w9, w27, w9
	add w9, w20, w9
	add w9, w27, w9
	add w9, w20, w9
	add w9, w27, w9
	add w9, w20, w9
	add w9, w27, w9
	add w9, w20, w9
	add w9, w27, w9
	add w9, w20, w9
	add w9, w27, w9
	add w9, w20, w9
	add w9, w27, w9
	add w9, w20, w9
	add w9, w27, w9
	add w9, w20, w9
	add w9, w27, w9
	add w9, w20, w9
	add w9, w27, w9
	add w9, w20, w9
	add w9, w27, w9
	add w9, w19, w9
	add w9, w27, w9
	add w9, w20, w9
	add w9, w27, w9
	add w9, w20, w9
	add w9, w27, w9
	add w9, w20, w9
	add w9, w27, w9
	add w9, w20, w9
	add w9, w27, w9
	add w9, w20, w9
	add w9, w27, w9
	add w9, w20, w9
	add w9, w27, w9
	add w9, w20, w9
	add w9, w27, w9
	add w9, w20, w9
	add w9, w27, w9
	add w9, w19, w9
	add w9, w27, w9
	add w9, w20, w9
	add w9, w27, w9
	add w9, w20, w9
	add w9, w27, w9
	add w9, w20, w9
	add w9, w27, w9
	add w9, w20, w9
	add w9, w27, w9
	add w9, w20, w9
	add w9, w27, w9
	add w9, w20, w9
	add w9, w27, w9
	add w9, w19, w9
	add w9, w21, w9
	add w9, w28, w9
	add w9, w21, w9
	add w9, w28, w9
	add w9, w21, w9
	add w9, w28, w9
	add w9, w17, w9
	add w9, w28, w9
	add w9, w21, w9
	add w9, w28, w9
	add w9, w21, w9
	add w9, w28, w9
	add w9, w21, w9
	add w9, w28, w9
	add w9, w21, w9
	add w9, w28, w9
	add w9, w21, w9
	add w9, w28, w9
	add w9, w21, w9
	add w9, w28, w9
	add w9, w21, w9
	add w9, w28, w9
	add w9, w21, w9
	add w9, w28, w9
	add w9, w21, w9
	add w9, w28, w9
	add w9, w17, w9
	add w9, w28, w9
	add w9, w21, w9
	add w9, w28, w9
	add w9, w21, w9
	add w9, w28, w9
	add w9, w21, w9
	add w9, w28, w9
	add w9, w21, w9
	add w9, w28, w9
	add w9, w21, w9
	add w9, w28, w9
	add w9, w21, w9
	add w9, w28, w9
	add w9, w21, w9
	add w9, w28, w9
	add w9, w21, w9
	add w9, w28, w9
	add w9, w21, w9
	add w9, w28, w9
	add w9, w17, w9
	add w9, w28, w9
	add w9, w21, w9
	add w9, w28, w9
	add w9, w21, w9
	add w9, w28, w9
	add w9, w21, w9
	add w9, w28, w9
	add w9, w21, w9
	add w9, w28, w9
	add w9, w21, w9
	add w9, w28, w9
	add w9, w21, w9
	add w9, w28, w9
	add w9, w17, w9
	add w9, w22, w9
	add w9, w7, w9
	add w9, w22, w9
	add w9, w7, w9
	add w9, w22, w9
	add w9, w7, w9
	add w9, w22, w9
	add w9, w7, w9
	add w9, w16, w9
	add w9, w7, w9
	add w9, w22, w9
	add w9, w7, w9
	add w9, w22, w9
	add w9, w7, w9
	add w9, w22, w9
	add w9, w7, w9
	add w9, w22, w9
	add w9, w7, w9
	add w9, w22, w9
	add w9, w7, w9
	add w9, w22, w9
	add w9, w7, w9
	add w9, w22, w9
	add w9, w7, w9
	add w9, w22, w9
	add w9, w7, w9
	add w9, w16, w9
	add w9, w7, w9
	add w9, w22, w9
	add w9, w7, w9
	add w9, w22, w9
	add w9, w7, w9
	add w9, w22, w9
	add w9, w7, w9
	add w9, w22, w9
	add w9, w7, w9
	add w9, w22, w9
	add w9, w7, w9
	add w9, w22, w9
	add w9, w7, w9
	add w9, w22, w9
	add w9, w7, w9
	add w9, w22, w9
	add w9, w7, w9
	add w9, w22, w9
	add w9, w7, w9
	add w9, w16, w9
	add w9, w7, w9
	add w9, w22, w9
	add w9, w7, w9
	add w9, w22, w9
	add w9, w7, w9
	add w9, w22, w9
	add w9, w7, w9
	add w9, w22, w9
	add w9, w7, w9
	add w9, w22, w9
	add w9, w7, w9
	add w9, w22, w9
	add w9, w7, w9
	add w9, w16, w9
	add w9, w23, w9
	add w9, w6, w9
	add w9, w23, w9
	add w9, w6, w9
	add w9, w23, w9
	add w9, w6, w9
	add w9, w23, w9
	add w9, w6, w9
	add w9, w15, w9
	add w9, w6, w9
	add w9, w23, w9
	add w9, w6, w9
	add w9, w23, w9
	add w9, w6, w9
	add w9, w23, w9
	add w9, w6, w9
	add w9, w23, w9
	add w9, w6, w9
	add w9, w23, w9
	add w9, w6, w9
	add w9, w23, w9
	add w9, w6, w9
	add w9, w23, w9
	add w9, w6, w9
	add w9, w15, w9
	add w9, w6, w9
	add w9, w23, w9
	add w9, w6, w9
	add w9, w23, w9
	add w9, w6, w9
	add w9, w23, w9
	add w9, w6, w9
	add w9, w23, w9
	add w9, w6, w9
	add w9, w23, w9
	add w9, w6, w9
	add w9, w23, w9
	add w9, w6, w9
	add w9, w23, w9
	add w9, w6, w9
	add w9, w23, w9
	add w9, w6, w9
	add w9, w23, w9
	add w9, w6, w9
	add w9, w23, w9
	add w9, w6, w9
	add w9, w15, w9
	add w9, w6, w9
	add w9, w23, w9
	add w9, w6, w9
	add w9, w23, w9
	add w9, w6, w9
	add w9, w23, w9
	add w9, w6, w9
	add w9, w23, w9
	add w9, w6, w9
	add w9, w23, w9
	add w9, w6, w9
	add w9, w23, w9
	add w9, w6, w9
	add w9, w15, w9
	add w15, w24, w9
	ldr w9, [sp, #132]
	add w15, w9, w15
	add w15, w24, w15
	add w15, w9, w15
	add w15, w24, w15
	add w15, w9, w15
	add w15, w24, w15
	add w15, w9, w15
	add w15, w24, w15
	add w15, w9, w15
	add w15, w14, w15
	add w15, w9, w15
	add w15, w24, w15
	add w15, w9, w15
	add w15, w24, w15
	add w15, w9, w15
	add w15, w24, w15
	add w15, w9, w15
	add w15, w24, w15
	add w15, w9, w15
	add w15, w24, w15
	add w15, w9, w15
	add w15, w24, w15
	add w15, w9, w15
	add w15, w14, w15
	add w15, w9, w15
	add w15, w24, w15
	add w15, w9, w15
	add w15, w24, w15
	add w15, w9, w15
	add w15, w24, w15
	add w15, w9, w15
	add w15, w24, w15
	add w15, w9, w15
	add w15, w24, w15
	add w15, w9, w15
	add w15, w24, w15
	add w15, w9, w15
	add w15, w24, w15
	add w15, w9, w15
	add w15, w24, w15
	add w15, w9, w15
	add w15, w24, w15
	add w15, w9, w15
	add w15, w24, w15
	add w15, w9, w15
	add w15, w14, w15
	add w15, w9, w15
	add w15, w24, w15
	add w15, w9, w15
	add w15, w24, w15
	add w15, w9, w15
	add w15, w24, w15
	add w15, w9, w15
	add w15, w24, w15
	add w15, w9, w15
	add w15, w24, w15
	add w15, w9, w15
	add w15, w24, w15
	add w9, w9, w15
	add w9, w14, w9
	add w14, w25, w9
	ldr w9, [sp, #128]
	add w14, w9, w14
	add w14, w25, w14
	add w14, w9, w14
	add w14, w25, w14
	add w14, w9, w14
	add w14, w25, w14
	add w14, w9, w14
	add w14, w25, w14
	add w14, w9, w14
	add w14, w13, w14
	add w14, w9, w14
	add w14, w25, w14
	add w14, w9, w14
	add w14, w25, w14
	add w14, w9, w14
	add w14, w25, w14
	add w14, w9, w14
	add w14, w25, w14
	add w14, w9, w14
	add w14, w25, w14
	add w14, w9, w14
	add w14, w13, w14
	add w14, w9, w14
	add w14, w25, w14
	add w14, w9, w14
	add w14, w25, w14
	add w14, w9, w14
	add w14, w25, w14
	add w14, w9, w14
	add w14, w25, w14
	add w14, w9, w14
	add w14, w25, w14
	add w14, w9, w14
	add w14, w25, w14
	add w14, w9, w14
	add w14, w25, w14
	add w14, w9, w14
	add w14, w25, w14
	add w14, w9, w14
	add w14, w25, w14
	add w14, w9, w14
	add w14, w25, w14
	add w14, w9, w14
	add w14, w25, w14
	add w14, w9, w14
	add w14, w13, w14
	add w14, w9, w14
	add w14, w25, w14
	add w14, w9, w14
	add w14, w25, w14
	add w14, w9, w14
	add w14, w25, w14
	add w14, w9, w14
	add w14, w25, w14
	add w14, w9, w14
	add w14, w25, w14
	add w14, w9, w14
	add w14, w25, w14
	add w9, w9, w14
	add w9, w13, w9
	add w13, w26, w9
	ldr w9, [sp, #124]
	add w13, w9, w13
	add w13, w26, w13
	add w13, w9, w13
	add w13, w26, w13
	add w13, w9, w13
	add w13, w26, w13
	add w13, w9, w13
	add w13, w26, w13
	add w13, w9, w13
	add w13, w26, w13
	add w13, w9, w13
	add w13, w12, w13
	add w13, w9, w13
	add w13, w26, w13
	add w13, w9, w13
	add w13, w26, w13
	add w13, w9, w13
	add w13, w26, w13
	add w13, w9, w13
	add w13, w26, w13
	add w13, w9, w13
	add w13, w12, w13
	add w13, w9, w13
	add w13, w26, w13
	add w13, w9, w13
	add w13, w26, w13
	add w13, w9, w13
	add w13, w26, w13
	add w13, w9, w13
	add w13, w26, w13
	add w13, w9, w13
	add w13, w26, w13
	add w13, w9, w13
	add w13, w26, w13
	add w13, w9, w13
	add w13, w26, w13
	add w13, w9, w13
	add w13, w26, w13
	add w13, w9, w13
	add w13, w26, w13
	add w13, w9, w13
	add w13, w26, w13
	add w13, w9, w13
	add w13, w26, w13
	add w13, w9, w13
	add w13, w12, w13
	add w13, w9, w13
	add w13, w26, w13
	add w13, w9, w13
	add w13, w26, w13
	add w13, w9, w13
	add w13, w26, w13
	add w13, w9, w13
	add w13, w26, w13
	add w13, w9, w13
	add w13, w26, w13
	add w13, w9, w13
	add w13, w26, w13
	add w9, w9, w13
	add w12, w12, w9
	ldr w9, [sp, #116]
	add w13, w9, w12
	ldr w12, [sp, #112]
	add w13, w12, w13
	add w13, w9, w13
	add w13, w12, w13
	add w13, w9, w13
	add w13, w12, w13
	add w13, w9, w13
	add w13, w12, w13
	add w13, w9, w13
	add w13, w12, w13
	add w13, w9, w13
	add w13, w12, w13
	add w13, w11, w13
	add w13, w12, w13
	add w13, w9, w13
	add w13, w12, w13
	add w13, w9, w13
	add w13, w12, w13
	add w13, w9, w13
	add w13, w12, w13
	add w13, w11, w13
	add w13, w12, w13
	add w13, w9, w13
	add w13, w12, w13
	add w13, w9, w13
	add w13, w12, w13
	add w13, w9, w13
	add w13, w12, w13
	add w13, w9, w13
	add w13, w12, w13
	add w13, w9, w13
	add w13, w12, w13
	add w13, w9, w13
	add w13, w12, w13
	add w13, w9, w13
	add w13, w12, w13
	add w13, w9, w13
	add w13, w12, w13
	add w13, w9, w13
	add w13, w12, w13
	add w13, w9, w13
	add w13, w12, w13
	add w13, w9, w13
	add w13, w12, w13
	add w13, w9, w13
	add w13, w12, w13
	add w13, w11, w13
	add w13, w12, w13
	add w13, w9, w13
	add w13, w12, w13
	add w13, w9, w13
	add w13, w12, w13
	add w13, w9, w13
	add w13, w12, w13
	add w13, w9, w13
	add w13, w12, w13
	add w13, w9, w13
	add w13, w12, w13
	add w9, w9, w13
	add w9, w12, w9
	add w11, w11, w9
	ldr w9, [sp, #108]
	add w12, w9, w11
	ldr w11, [sp, #104]
	add w12, w11, w12
	add w12, w9, w12
	add w12, w11, w12
	add w12, w9, w12
	add w12, w11, w12
	add w12, w9, w12
	add w12, w11, w12
	add w12, w9, w12
	add w12, w11, w12
	add w12, w9, w12
	add w12, w11, w12
	add w12, w9, w12
	add w12, w11, w12
	add w12, w10, w12
	add w12, w11, w12
	add w12, w9, w12
	add w12, w11, w12
	add w12, w9, w12
	add w12, w11, w12
	add w12, w10, w12
	add w12, w11, w12
	add w12, w9, w12
	add w12, w11, w12
	add w12, w9, w12
	add w12, w11, w12
	add w12, w9, w12
	add w12, w11, w12
	add w12, w9, w12
	add w12, w11, w12
	add w12, w9, w12
	add w12, w11, w12
	add w12, w9, w12
	add w12, w11, w12
	add w12, w9, w12
	add w12, w11, w12
	add w12, w9, w12
	add w12, w11, w12
	add w12, w9, w12
	add w12, w11, w12
	add w12, w9, w12
	add w12, w11, w12
	add w12, w9, w12
	add w12, w11, w12
	add w12, w9, w12
	add w12, w11, w12
	add w12, w10, w12
	add w12, w11, w12
	add w12, w9, w12
	add w12, w11, w12
	add w12, w9, w12
	add w12, w11, w12
	add w12, w9, w12
	add w12, w11, w12
	add w12, w9, w12
	add w12, w11, w12
	add w12, w9, w12
	add w12, w11, w12
	add w9, w9, w12
	add w9, w11, w9
	add w10, w10, w9
	ldr w9, [sp, #100]
	add w11, w9, w10
	ldr w10, [sp, #96]
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w4, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w4, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w4, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w9, w9, w11
	add w9, w10, w9
	add w9, w4, w9
	add w9, w3, w9
	ldr w10, [sp, #88]
	add w11, w10, w9
	ldr w9, [sp, #92]
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w3, w11
	add w11, w10, w11
	add w11, w3, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w3, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w9, w9, w11
	add w9, w10, w9
	add w9, w3, w9
	add w9, w10, w9
	ldr w10, [sp, #80]
	add w11, w10, w9
	ldr w9, [sp, #84]
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w2, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w2, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w2, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w11, w9, w11
	add w11, w10, w11
	add w9, w9, w11
	add w9, w10, w9
	add w9, w2, w9
	add w10, w10, w9
	movz w9, #34079
	movk w9, #20971, lsl #16
	smull x9, w10, w9
	asr x9, x9, #37
	add w10, w9, w9, lsr #31
	adrp x9, __sysy_par_ctx_0_0
	str w10, [x9, :lo12:__sysy_par_ctx_0_0]
	movz w1, #0
	adrp x9, __sysy_par_scalar_start_0
	str w1, [x9, :lo12:__sysy_par_scalar_start_0]
	adrp x9, __sysy_par_scalar_bound_0
	ldr w2, [sp, #136]
	str w2, [x9, :lo12:__sysy_par_scalar_bound_0]
	adrp x9, __sysy_par_scalar_partial_0_0
	str w1, [x9, :lo12:__sysy_par_scalar_partial_0_0]
	adrp x9, __sysy_par_scalar_partial_0_1
	str w1, [x9, :lo12:__sysy_par_scalar_partial_0_1]
	mov w0, w1
	bl __sysy_parallel_for
	adrp x9, __sysy_par_scalar_partial_0_1
	ldr w15, [x9, :lo12:__sysy_par_scalar_partial_0_1]
	adrp x9, __sysy_par_scalar_partial_0_0
	ldr w16, [x9, :lo12:__sysy_par_scalar_partial_0_0]
	movz w13, #12033
	cmp w15, #0
	movk w13, #22888, lsl #16
	cset w14, ge
	sub w9, w13, w15
	sub w10, w16, w9
	cmp w16, w9
	add w12, w16, w15
	movz w9, #53503
	movk w9, #42647, lsl #16
	csel w11, w10, w12, ge
	sub w10, w9, w15
	sub w9, w16, w10
	cmp w16, w10
	csel w9, w9, w12, le
	cmp w14, #0
	csel w10, w11, w9, ne
	movz w9, #16511
	movk w9, #183, lsl #16
	smull x9, w10, w9
	ldp x27, x28, [sp, #64]
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	asr x9, x9, #54
	add w9, w9, w9, lsr #31
	msub w0, w9, w13, w10
	add sp, sp, #160
	ldp xzr, x30, [sp], #16
	ret
	.size func, .-func
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #80
	str x19, [sp, #64]
	bl getint
	adrp x9, loopCount
	str w0, [x9, :lo12:loopCount]
	movz w0, #121
	bl _sysy_starttime
	movz w7, #1
	str w7, [sp]
	mov w0, w7
	str w7, [sp, #8]
	mov w1, w7
	str w7, [sp, #16]
	mov w2, w7
	str w7, [sp, #24]
	mov w3, w7
	str w7, [sp, #32]
	mov w4, w7
	str w7, [sp, #40]
	mov w5, w7
	str w7, [sp, #48]
	mov w6, w7
	bl func
	mov w19, w0
	movz w0, #123
	bl _sysy_stoptime
	mov w0, w19
	bl putint
	movz w0, #10
	bl putch
	movz w0, #0
	ldr x19, [sp, #64]
	add sp, sp, #80
	ldp xzr, x30, [sp], #16
	ret
	.size main, .-main
	.p2align 2
	.global __sysy_par_body_0
	.type __sysy_par_body_0, %function
__sysy_par_body_0:
	adrp x10, __sysy_par_ctx_0_0
	ldr w15, [x10, :lo12:__sysy_par_ctx_0_0]
	adrp x9, __sysy_par_scalar_start_0
	ldr w13, [x9, :lo12:__sysy_par_scalar_start_0]
	mov w12, w0
	mov w14, w1
	movz w16, #12033
	movz w10, #16511
	movz w6, #0
	sub w8, w14, #3
	mov w7, w12
	orr w17, wzr, #0x80000003
	movk w16, #22888, lsl #16
	movk w10, #183, lsl #16
.L__sysy_par_body_0_bb1:
	cmp w7, w8
	cset w11, lt
	cmp w14, w17
	cset w9, ge
	and w9, w9, w11
	cbz w9, .L__sysy_par_body_0_bb6
.L__sysy_par_body_0_bb5:
	add w11, w6, w15
	smull x9, w11, w10
	asr x9, x9, #54
	add w9, w9, w9, lsr #31
	msub w9, w9, w16, w11
	add w11, w9, w15
	smull x9, w11, w10
	asr x9, x9, #54
	add w9, w9, w9, lsr #31
	msub w9, w9, w16, w11
	add w11, w9, w15
	smull x9, w11, w10
	asr x9, x9, #54
	add w9, w9, w9, lsr #31
	msub w9, w9, w16, w11
	add w11, w9, w15
	smull x9, w11, w10
	asr x9, x9, #54
	add w9, w9, w9, lsr #31
	msub w6, w9, w16, w11
	add w7, w7, #4
	b .L__sysy_par_body_0_bb1
.L__sysy_par_body_0_bb2:
	cmp w7, w14
	b.ge .L__sysy_par_body_0_bb3
.L__sysy_par_body_0_bb4:
	add w11, w11, w15
	smull x9, w11, w10
	asr x9, x9, #54
	add w9, w9, w9, lsr #31
	msub w11, w9, w16, w11
	add w7, w7, #1
	b .L__sysy_par_body_0_bb2
.L__sysy_par_body_0_bb3:
	adrp x10, __sysy_par_scalar_partial_0_0
	adrp x9, __sysy_par_scalar_partial_0_1
	cmp w12, w13
	add x10, x10, :lo12:__sysy_par_scalar_partial_0_0
	add x9, x9, :lo12:__sysy_par_scalar_partial_0_1
	csel x9, x10, x9, eq
	str w11, [x9]
	ret
.L__sysy_par_body_0_bb6:
	movz w16, #12033
	movz w10, #16511
	mov w11, w6
	movk w16, #22888, lsl #16
	movk w10, #183, lsl #16
	b .L__sysy_par_body_0_bb2
	.size __sysy_par_body_0, .-__sysy_par_body_0
	.data
	.global loopCount
	.p2align 2
loopCount:
	.zero 4
	.bss
	.global __sysy_par_ctx_0_0
	.p2align 2
__sysy_par_ctx_0_0:
	.zero 4
	.global __sysy_par_scalar_start_0
	.p2align 2
__sysy_par_scalar_start_0:
	.zero 4
	.global __sysy_par_scalar_bound_0
	.p2align 2
__sysy_par_scalar_bound_0:
	.zero 4
	.global __sysy_par_scalar_partial_0_0
	.p2align 2
__sysy_par_scalar_partial_0_0:
	.zero 4
	.global __sysy_par_scalar_partial_0_1
	.p2align 2
__sysy_par_scalar_partial_0_1:
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
