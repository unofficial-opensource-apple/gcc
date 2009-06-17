/* Test the `vcntp8' ARM Neon intrinsic.  */
/* This file was autogenerated by neon-testgen.  */

/* { dg-do assemble } */
/* { dg-require-effective-target arm_neon_ok } */
/* { dg-options "-save-temps -O0 -mfpu=neon -mfloat-abi=softfp" } */

#include "arm_neon.h"

void test_vcntp8 (void)
{
  poly8x8_t out_poly8x8_t;
  poly8x8_t arg0_poly8x8_t;

  out_poly8x8_t = vcnt_p8 (arg0_poly8x8_t);
}

/* { dg-final { scan-assembler "vcnt\.8\[ 	\]+\[dD\]\[0-9\]+, \[dD\]\[0-9\]+!?\(\[ 	\]+@\[a-zA-Z0-9 \]+\)?\n" } } */
/* { dg-final { cleanup-saved-temps } } */
