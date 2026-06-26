.text

#if defined(__linux__) || defined(__FreeBSD__) /* HOST_OS_LINUX */
.globl ClobberAndCall
.type ClobberAndCall, @function
.align 4
ClobberAndCall:
#else /* HOST_OS_MACOS */
.globl _ClobberAndCall
.align 4
_ClobberAndCall:
#endif

/* Save callee-saved registers that we will clobber.
   Stack must be 16-byte aligned. */
addi.d  $sp, $sp, -112
st.d    $ra, $sp, 104
st.d    $fp, $sp, 96
addi.d  $fp, $sp, 112

st.d    $s0, $sp, 88
st.d    $s1, $sp, 80
st.d    $s2, $sp, 72
st.d    $s3, $sp, 64
st.d    $s4, $sp, 56
st.d    $s5, $sp, 48
st.d    $s6, $sp, 40
st.d    $s7, $sp, 32
st.d    $s8, $sp, 24

/* Clobber all volatile registers (a1-a7, t0-t8).
   $a0 holds the function pointer so we save that for the call. */
move    $t0, $a0         /* save function pointer */

ori     $ra, $zero, 1
ori     $a1, $zero, 1
ori     $a2, $zero, 1
ori     $a3, $zero, 1
ori     $a4, $zero, 1
ori     $a5, $zero, 1
ori     $a6, $zero, 1
ori     $a7, $zero, 1

ori     $t1, $zero, 1
ori     $t2, $zero, 1
ori     $t3, $zero, 1
ori     $t4, $zero, 1
ori     $t5, $zero, 1
ori     $t6, $zero, 1
ori     $t7, $zero, 1
ori     $t8, $zero, 1

/* Also clobber callee-saved registers to verify save/restore */
ori     $s0, $zero, 1
ori     $s1, $zero, 1
ori     $s2, $zero, 1
ori     $s3, $zero, 1
ori     $s4, $zero, 1
ori     $s5, $zero, 1
ori     $s6, $zero, 1
ori     $s7, $zero, 1
ori     $s8, $zero, 1

/* Call the function via $t0 (saved $a0) */
jirl    $ra, $t0, 0

/* Restore callee-saved registers */
ld.d    $s8, $sp, 24
ld.d    $s7, $sp, 32
ld.d    $s6, $sp, 40
ld.d    $s5, $sp, 48
ld.d    $s4, $sp, 56
ld.d    $s3, $sp, 64
ld.d    $s2, $sp, 72
ld.d    $s1, $sp, 80
ld.d    $s0, $sp, 88

ld.d    $fp, $sp, 96
ld.d    $ra, $sp, 104
addi.d  $sp, $sp, 112

jirl    $zero, $ra, 0