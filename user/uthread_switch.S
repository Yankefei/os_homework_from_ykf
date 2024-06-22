	.text

	/*
         * save the old thread's registers,
         * restore the new thread's registers.
         */

.globl thread_switch
.globl getStackBase

thread_switch:

	sd ra, 0(a0)
	sd sp, 8(a0)
	sd s0, 16(a0)
	sd s1, 24(a0)
	sd s2, 32(a0)
	sd s3, 40(a0)
	sd s4, 48(a0)
	sd s5, 56(a0)
	sd s6, 64(a0)
	sd s7, 72(a0)
	sd s8, 80(a0)
	sd s9, 88(a0)
	sd s10, 96(a0)
	sd s11, 104(a0)

	# 注，中间调用的部分，和函数 copy_stack的一致，只是用汇编进行了改写
	# 主要内容是循环拷贝栈帧，以及判断等逻辑

	# 已使用寄存器情况  t0  t1  t2  t3  t4  t5  t6
	# t0, a0->stack
	# t1, t_stack_end (change)
	# t2, t_stack_end
	# t3, index (change)
	# t4, temp
  # t5,
	# t6,

	ld t0, 112(a0)

	call getStackBase
	mv t1, a0   # 将返回值保存到t2
	mv t2, a0

	li t3, 4096
copy_loop:
    lb t4, 0(t1)       # 从源地址 t1 加载一个字节到 t4
    sb t4, 0(t0)       # 将字节存储到目标地址 t0
    addi t1, t1, 1     # 源地址递增
    addi t0, t0, 1     # 目标地址递增
    addi t3, t3, -1    # 计数器递减
    bnez t3, copy_loop # 如果计数器不为0，继续循环


	# 已使用寄存器情况  t0  t1  t2  t3  t4  t5  t6
	# t0,
	# t1, current_stack_end
	# t2, t_stack_end
	# t3, current_stack_end(change)
	# t4, temp
  # t5, index
	# t6, current->stack

	mv a0, a1
	call getStackBase
	mv t1, a0
	mv t3, a0

beq t2,  t1,   true_branch

# 只设置 esp
	li t4, 4096
	add t2, t2, t4
	sd t2, 8(a1)  # 如果t2 t1 不一样，则说明是第一次切换，只能用 t2 来使用
	j end_if

true_branch:

	li t5, 4096
	ld t6, 112(a1)

copy_loop2:
    lb t4, 0(t6)       # 从源地址 t6 加载一个字节到 t4
    sb t4, 0(t3)       # 将字节存储到目标地址   t3
    addi t6, t6, 1     # 源地址递增
    addi t3, t3, 1     # 目标地址递增
    addi t5, t5, -1    # 计数器递减
    bnez t5, copy_loop2 # 如果计数器不为0，继续循环

end_if:


	ld ra, 0(a1)
	ld sp, 8(a1)
	ld s0, 16(a1)
	ld s1, 24(a1)
	ld s2, 32(a1)
	ld s3, 40(a1)
	ld s4, 48(a1)
	ld s5, 56(a1)
	ld s6, 64(a1)
	ld s7, 72(a1)
	ld s8, 80(a1)
	ld s9, 88(a1)
	ld s10, 96(a1)
	ld s11, 104(a1)

	ret    /* return to ra */
