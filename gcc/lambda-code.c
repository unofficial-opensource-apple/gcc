/*  Loop transformation code generation
    Copyright (C) 2003, 2004 Free Software Foundation, Inc.
    Contributed by Daniel Berlin <dberlin@dberlin.org>

    This file is part of GCC.
    
    GCC is free software; you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2, or (at your option) any later
    version.
    
    GCC is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.
    
    You should have received a copy of the GNU General Public License
    along with GCC; see the file COPYING.  If not, write to the Free
    Software Foundation, 59 Temple Place - Suite 330, Boston, MA
    02111-1307, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "errors.h"
#include "ggc.h"
#include "tree.h"
#include "target.h"
#include "rtl.h"
#include "basic-block.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "tree-dump.h"
#include "timevar.h"
#include "cfgloop.h"
#include "tree-fold-const.h"
#include "expr.h"
#include "optabs.h"
#include "tree-chrec.h"
#include "tree-data-ref.h"
#include "tree-pass.h"
#include "tree-scalar-evolution.h"
#include "varray.h"
#include "lambda.h"

/* This loop nest code generation is based on non-singular matrix
   math.  */

/* Lattice stuff that is internal to the code generation algorithm.  */

typedef struct
{
  lambda_matrix base;
  int dimension;
  lambda_vector origin;
  lambda_matrix origin_invariants;
  int invariants;
} * lambda_lattice;

#define LATTICE_BASE(T) ((T)->base)
#define LATTICE_DIMENSION(T) ((T)->dimension)
#define LATTICE_ORIGIN(T) ((T)->origin)
#define LATTICE_ORIGIN_INVARIANTS(T) ((T)->origin_invariants)
#define LATTICE_INVARIANTS(T) ((T)->invariants)

static bool lle_equal (lambda_linear_expression, lambda_linear_expression,
		       int, int);
static lambda_lattice lambda_lattice_new (int, int);
static lambda_lattice lambda_lattice_compute_base (lambda_loopnest);

static tree find_induction_var_from_exit_cond (struct loop *);


/* Create a new lambda body vector.  */

lambda_body_vector
lambda_body_vector_new (int size)
{
  lambda_body_vector ret;
  
  ret = ggc_alloc (sizeof (*ret));
  LBV_COEFFICIENTS (ret) = lambda_vector_new (size);
  LBV_SIZE (ret) = size;
  LBV_DENOMINATOR (ret) = 1;
  return ret;
}

/* Compute the new coefficients for the vector based on the
  *inverse* of the transformation matrix.  */

lambda_body_vector
lambda_body_vector_compute_new (lambda_trans_matrix transform,
				lambda_body_vector vect)
{
  lambda_body_vector temp;
  int depth;
  
  /* Make sure the matrix is square.  */
  if (LTM_ROWSIZE (transform) != LTM_COLSIZE (transform))
    abort ();

  depth = LTM_ROWSIZE (transform);
  
  temp = lambda_body_vector_new (depth);
  LBV_DENOMINATOR (temp) = LBV_DENOMINATOR (vect) * LTM_DENOMINATOR (transform); 
  lambda_vector_matrix_mult (LBV_COEFFICIENTS (vect), depth,
			     LTM_MATRIX (transform),
			     depth, LBV_COEFFICIENTS (temp));
  LBV_SIZE (temp) = LBV_SIZE (vect);
  return temp;
}

/* Print out a lambda body vector.  */

void
print_lambda_body_vector (FILE *outfile, lambda_body_vector body)
{
  print_lambda_vector (outfile, LBV_COEFFICIENTS (body), LBV_SIZE (body));
}

/* Return TRUE if two linear expressions are equal.  */

static bool
lle_equal (lambda_linear_expression lle1, lambda_linear_expression lle2,
	   int depth, int invariants)
{
  int i;

  if (lle1 == NULL || lle2 == NULL)
    return false;
  if (LLE_CONSTANT (lle1) != LLE_CONSTANT (lle2))
    return false;
  if (LLE_DENOMINATOR (lle1) != LLE_DENOMINATOR (lle2))
    return false;
  for (i = 0; i < depth; i++)
    if (LLE_COEFFICIENTS (lle1)[i] != LLE_COEFFICIENTS (lle2)[i])
      return false;
  for (i = 0; i < invariants; i++)
    if (LLE_INVARIANT_COEFFICIENTS (lle1)[i] != LLE_INVARIANT_COEFFICIENTS (lle2)[i])
      return false;
  return true;
}
/* Create a new linear expression with dimension DIM, and total number
   of invariants INVARIANTS.  */

lambda_linear_expression 
lambda_linear_expression_new (int dim, int invariants)
{
  lambda_linear_expression ret;
  
  ret = ggc_alloc_cleared (sizeof (*ret));
  
  LLE_COEFFICIENTS (ret) = lambda_vector_new (dim);
  LLE_CONSTANT (ret) = 0;
  LLE_INVARIANT_COEFFICIENTS (ret) = lambda_vector_new (invariants);  
  LLE_DENOMINATOR (ret) = 1;
  LLE_NEXT (ret) = NULL;

  return ret;
}

static void
print_linear_expression (FILE *outfile, lambda_vector expr, int size, 
			 char start)
{
  int i;
  bool starting = true;
  for (i = 0; i < size; i++)
    {
      if (expr[i] != 0)
	{
	  if (starting)
	    {
	      if (expr[i] < 0)
		fprintf (outfile, "-");
	      starting = false;
	    }
	  else if (expr[i] > 0)
	    fprintf (outfile, " + ");
	  else
	    fprintf (outfile, " - ");
	  if (expr[i] == 1 || expr [i] == -1)
	    fprintf (outfile, "%c", start + i);
	  else
	    fprintf (outfile, "%d%c", abs(expr[i]), start + i);
	}
    }
}
void
print_lambda_linear_expression (FILE *outfile, 
				lambda_linear_expression expr,
				int depth, int invariants, char start)
{
  fprintf (outfile, "\tLinear expression: ");
  print_linear_expression (outfile, LLE_COEFFICIENTS (expr),
			   depth, start);
  fprintf (outfile, " constant: %d ", LLE_CONSTANT (expr));
  fprintf (outfile, "  invariants: ");
  print_linear_expression (outfile, LLE_INVARIANT_COEFFICIENTS (expr),
			   invariants, 'A');
  fprintf (outfile, "  denominator: %d\n", LLE_DENOMINATOR (expr));
}

/* Print a loop.  */

void
print_lambda_loop (FILE *outfile, lambda_loop loop, int depth, 
		   int invariants, char start)
{
  int step;
  lambda_linear_expression expr;
  
  if (!loop)
    abort ();
  
  expr = LL_LINEAR_OFFSET (loop);
  step = LL_STEP (loop);
  fprintf (outfile, "  step size = %d \n", step);

  if (expr)
    {
      fprintf (outfile, "  linear offset: \n");
      print_lambda_linear_expression (outfile, expr, depth, invariants, 
				      start);
    }

  fprintf (outfile, "  lower bound: \n");
  expr = LL_LOWER_BOUND (loop);
  for (; expr != NULL; expr = LLE_NEXT (expr))
    print_lambda_linear_expression (outfile, expr, depth, invariants,
				    start);
  fprintf (outfile, "  upper bound: \n");
  expr = LL_UPPER_BOUND (loop);
  for (; expr != NULL; expr = LLE_NEXT (expr))
    print_lambda_linear_expression (outfile, expr, depth, invariants,
				    start);


}

/* Create a new loop nest structure.  */

lambda_loopnest
lambda_loopnest_new (int depth, int invariants)
{
  lambda_loopnest ret;
  ret = ggc_alloc (sizeof (*ret));
  
  LN_LOOPS (ret) = ggc_alloc_cleared (depth * sizeof (lambda_loop));
  LN_DEPTH (ret) = depth;
  LN_INVARIANTS (ret) = invariants;

  return ret;
}


/* Print a lambda loopnest structure.  */

void 
print_lambda_loopnest (FILE *outfile, lambda_loopnest nest, char start)
{
  int i;
  for (i = 0; i < LN_DEPTH (nest); i++)
    {
      fprintf (outfile, "Loop %c\n", start + i);
      print_lambda_loop (outfile, LN_LOOPS (nest)[i], LN_DEPTH (nest), 
			 LN_INVARIANTS (nest), 'i');
      fprintf (outfile, "\n");
    }
}

/* Allocate a new lattice structure.  */

static lambda_lattice
lambda_lattice_new (int depth, int invariants)
{
  lambda_lattice ret;
  ret = ggc_alloc (sizeof (*ret));
  LATTICE_BASE (ret) = lambda_matrix_new (depth, depth);
  LATTICE_ORIGIN (ret) = lambda_vector_new (depth);
  LATTICE_ORIGIN_INVARIANTS (ret) = lambda_matrix_new (depth, invariants);
  LATTICE_DIMENSION (ret) = depth;
  LATTICE_INVARIANTS (ret) = invariants;
  return ret;
}

/* Compute the lattice base for a loopnest.  */

static lambda_lattice 
lambda_lattice_compute_base (lambda_loopnest nest)
{
  lambda_lattice ret;
  int depth, invariants;
  lambda_matrix base;
  
  int i, j, step;
  lambda_loop loop;
  lambda_linear_expression expression;

  depth = LN_DEPTH (nest);
  invariants = LN_INVARIANTS (nest);

  ret = lambda_lattice_new (depth, invariants);  
  base = LATTICE_BASE (ret);
  for (i = 0; i < depth; i++)
    {
      loop = LN_LOOPS(nest)[i];
      if (!loop)
	abort ();
      step = LL_STEP (loop);
      /* If we have a step of 1, then the base is one, and the
	 origin and invariant coefficients are 0.  */
      if (step == 1)
	{	
	  for (j = 0; j < depth; j++)
	    base[i][j] = 0;
	  base[i][i] = 1;	 
	  LATTICE_ORIGIN(ret)[i] = 0;	  
	  for (j = 0; j < invariants; j++)
	    LATTICE_ORIGIN_INVARIANTS(ret)[i][j] = 0;
	}
      else
	{
	  /* Otherwise, we need the lower bound expression (which must
	     be an affine function)  to determine the base.  */
	  expression = LL_LOWER_BOUND (loop);
	  if (!expression 
	      || LLE_NEXT (expression) 
	      || LLE_DENOMINATOR (expression) != 1)
	    abort ();
	  
	  for (j = 0; j < i; j ++)
	    base[i][j] = LLE_COEFFICIENTS (expression)[j] 
	      * LL_STEP (LN_LOOPS(nest)[j]);
	  base[i][i] = step;
	  for (j = i + 1; j < depth; j++)
	    base[i][j] = 0;
	  
	  /* Origin for this loop is the constant of the lower bound
	     expression.  */
	  LATTICE_ORIGIN (ret)[i] = LLE_CONSTANT (expression);
	  
	  
	  /* Coefficient for the invariants are equal to the invariant
	     coefficients in the expression.  */
	  for (j = 0; j < invariants; j++)
	    LATTICE_ORIGIN_INVARIANTS (ret)[i][j] = 
	      LLE_INVARIANT_COEFFICIENTS (expression)[j];
	}
    }
  return ret;
}


/* Compute the greatest common denominator of two numbers using
   euclid's algorithm.  */

static int 
gcd (int a, int b)
{
  
  int x, y, z;
  
  x = (a > 0) ? a : -1 * a;
  y = (b > 0) ? b : -1 * b;

  while (x>0)
    {
      z = y % x;
      y = x;
      x = z;
    }

  return (y);
}

/* Compute the greatest common denominator of a vector of numbers.  */

static int
gcd_vector (lambda_vector vector, int size)
{
  int i;
  int gcd1 = 0;
  
  
  if (size > 0)
    {      
      gcd1 = vector[0];
      for (i = 1; i < size; i++)
	gcd1 = gcd (gcd1, vector[i]);
    }
  return gcd1;
}

/* Compute the least common multiple of two numbers.  */

static int
lcm (int a, int b)
{
  return (abs (a) * abs (b) / gcd (a, b) );  
}

/* Compute the loop bounds for the auxillary space.
   Input system is Ax <= b.  TRANS is the unimodular transformation
   matrix. Comments are transcribed from the equations in the paper.  */

static lambda_loopnest
lambda_compute_auxillary_space (lambda_loopnest nest, 
				lambda_trans_matrix trans)
{
  lambda_matrix A, B, A1, B1, temp0;
  lambda_vector a, a1, temp1;
  lambda_matrix invertedtrans;
  int determinant, depth, invariants, size, newsize;
  int i, j, k;
  lambda_loopnest auxillary_nest;
  lambda_loop loop;
  lambda_linear_expression expression;
  lambda_lattice lattice;
  
  int multiple, f1, f2;

  depth = LN_DEPTH (nest);
  invariants = LN_INVARIANTS (nest);
  A = lambda_matrix_new (128, depth);
  B = lambda_matrix_new (128, invariants);
  a = lambda_vector_new (128);
  
  A1 = lambda_matrix_new (128, depth);
  B1 = lambda_matrix_new (128, invariants);
  a1 = lambda_vector_new (128);
  
  /* Store the bounds in the matrix A, vector a, and invariant matrix
     B, so that we have Ax <= a + B.  */
  size = 0;  
  for (i = 0; i < depth; i++)
    {
      loop = LN_LOOPS(nest)[i];

      /* First we do the lower bound.  */
      if (LL_STEP (loop) > 0)
	expression = LL_LOWER_BOUND (loop);
      else
	expression = LL_UPPER_BOUND (loop);
      
      for (; expression != NULL; expression = LLE_NEXT (expression))
	{
	  
	  /* Fill in the coefficient.  */
	  for (j = 0; j < i; j++)
	    A[size][j] = LLE_COEFFICIENTS (expression)[j];

	  /* And the invariant coefficient.  */
	  for (j = 0; j < invariants; j++)
	    B[size][j] = LLE_INVARIANT_COEFFICIENTS(expression)[j];

	  /* And the constant.  */
	  a[size] = LLE_CONSTANT (expression);
	  
	  /* Convert (2x+3y+2+b)/4 <= z to 2x+3y-4z <= -2-b.  */	  
	  A[size][i] = -1 * LLE_DENOMINATOR (expression);
	  a[size] *= -1;
	  for (j = 0; j < invariants; j++)
	    B[size][j] *= -1;
	  
	  size++;
	}

      /* Then do the exact same thing for the upper bounds.  */
      if (LL_STEP (loop) > 0)
	expression = LL_UPPER_BOUND (loop);
      else
	expression = LL_LOWER_BOUND (loop);
      
      for (; expression != NULL; expression = LLE_NEXT (expression))
	{
	  
	  /* Fill in the coefficient.  */
	  for (j = 0; j < i; j++)
	    A[size][j] = LLE_COEFFICIENTS (expression)[j];

	  /* And the invariant coefficient.  */
	  for (j = 0; j < invariants; j++)
	    B[size][j] = LLE_INVARIANT_COEFFICIENTS(expression)[j];

	  /* And the constant.  */
	  a[size] = LLE_CONSTANT (expression);
	  
	  /* Convert z <= (2x+3y+2+b)/4 to -2x-3y+4z <= 2+b.  */
	  for (j = 0; j < i; j++)
	    A[size][j] *= -1;
	  A[size][i] = LLE_DENOMINATOR (expression);	  
	  size++;
	}
    }

  /* Compute the lattice base x = base * y + origin, where y is the
     base space.  */
  lattice = lambda_lattice_compute_base (nest);


  /* Ax <= a + B becomes ALy <= a+B - A*origin.  L is the lattice base  */

  /* A1 = AL */
  lambda_matrix_mult (A, LATTICE_BASE (lattice), A1, size, depth, depth);
  
  /* a1 = a - A * origin constant.  */
  lambda_matrix_vector_mult (A, size, depth, LATTICE_ORIGIN (lattice),
			     a1);
  lambda_vector_add_mc (a, 1, a1, -1, a1, size);
  
  /* B1 = B - A * origin invariant.  */
  lambda_matrix_mult (A, LATTICE_ORIGIN_INVARIANTS (lattice), B1, size, depth,
		      invariants);
  lambda_matrix_add_mc (B, 1, B1, -1, B1, size, invariants);
  

  /* Compute the auxillary space.
     Equations:
     given A1 * y <= b1 + B1
     Compute the auxillary space for z1=HUy
     A1 * y <= a1 + B1.
     Let z be the target space.
     z = Tx = T(Ly+origin) = TLy + T*origin 
     z1 = z-T*origin = TLy = HUy.  */
  
  invertedtrans = lambda_matrix_new (depth, depth);
  
  /* Compute the inverse of U.  */
  determinant = lambda_matrix_inverse (LTM_MATRIX (trans),
				       invertedtrans, depth);

  /* A = A1 inv(U).  */
  lambda_matrix_mult (A1, invertedtrans, A, size, depth, depth);
  
  /* Perform Fourier-Motzkin on Ax <= a + B.  */

  temp0 = B1;
  B1 = B;
  B = temp0;
  
  temp1 = a1;
  a1 = a;
  a = temp1;
  
  auxillary_nest = lambda_loopnest_new (depth, invariants);
  
  for (i = depth - 1; i >= 0; i--)
    {
      loop = lambda_loop_new ();
      LN_LOOPS(auxillary_nest)[i] = loop;
      LL_STEP (loop) = 1;
      
      for (j = 0; j < size; j++)
	{
	  if (A[j][i] < 0)
	    { 
	      /* Lower bound.  */
	      expression = lambda_linear_expression_new (depth, invariants);
	      
	      for (k = 0; k < i; k++)
		LLE_COEFFICIENTS(expression)[k] = A[j][k];
	      for (k = 0; k < invariants; k++)
		LLE_INVARIANT_COEFFICIENTS(expression)[k] = -1 * B[j][k];
	      LLE_DENOMINATOR (expression) = -1 * A[j][i];
	      LLE_CONSTANT (expression) = -1 * a[j];
	      /* Ignore if identical to the existing lower bound.  */
	      if (!lle_equal (LL_LOWER_BOUND (loop),
			      expression, depth, invariants))
		{		 
		  LLE_NEXT (expression) = LL_LOWER_BOUND (loop);
		  LL_LOWER_BOUND (loop) = expression;
		}

	      
	    } 
	  else if (A[j][i] > 0)
	    {
	      /* Upper bound.  */
	      expression = lambda_linear_expression_new (depth, invariants);
	      for (k = 0; k < i; k++)
		LLE_COEFFICIENTS (expression)[k] = -1 * A[j][k];
	      LLE_CONSTANT (expression) = a[j];
	      
	      for (k = 0; k < invariants; k++)
		LLE_INVARIANT_COEFFICIENTS (expression)[k] = B[j][k];
	      
	      LLE_DENOMINATOR (expression) = A[j][i];
	      /* Ignore if identical to the existing upper bound.  */
	      if (!lle_equal (LL_UPPER_BOUND (loop),
			      expression, depth, invariants))
		{		 
		  LLE_NEXT (expression) = LL_UPPER_BOUND (loop);
		  LL_UPPER_BOUND (loop) = expression;
		}

	    } 
	}            
      /* creates a new system by deleting the i'th variable. */     
      newsize = 0;
      for (j = 0; j < size; j++)
	{
	  if (A[j][i] == 0)
	    {
	      lambda_vector_copy (A[j], A1[newsize], depth);
	      lambda_vector_copy (B[j], B1[newsize], invariants);
	      a1[newsize] = a[j];
	      newsize++;
	    }
	  else if (A[j][i] > 0)
	    {
	      for (k = 0; k < size; k++)
		{
		  if (A[k][i] < 0)
		    {
		      multiple = lcm (A[j][i], A[k][i]);
		      f1 = multiple / A[j][i];
		      f2 = -1 * multiple / A[k][i];
		      
		      lambda_vector_add_mc (A[j], f1, A[k], f2,
					    A1[newsize], depth);
		      lambda_vector_add_mc (B[j], f1, B[k], f2, 
					    B1[newsize], invariants);
		      a1[newsize] = f1 * a[j] + f2 * a[k];
		      newsize++;
		    }
		}
	    }
	}
      
      temp0 = A;
      A = A1;
      A1 = temp0;
      
      temp0 = B;
      B = B1;
      B1 = temp0;
      
      temp1 = a;
      a = a1;
      a1 = temp1;
      
      size = newsize;
    }
  
  return auxillary_nest;
}


/* Compute the loop bounds for the target space, using the bounds of
   the auxillary nest.
   Output a new set of linear bounds and linear offsets.  */

static lambda_loopnest 
lambda_compute_target_space (lambda_loopnest auxillary_nest,
			     lambda_trans_matrix H,
			     lambda_vector stepsigns)
{
  lambda_matrix inverse, H1;
  int determinant, i, j;
  int gcd1, gcd2;
  int factor;
  
  
  lambda_loopnest target_nest;
  int depth, invariants;
  lambda_matrix target;
  
  lambda_loop auxillary_loop, target_loop;
  lambda_linear_expression expression, auxillary_expr, target_expr, tmp_expr;
  
  depth = LN_DEPTH (auxillary_nest);
  invariants = LN_INVARIANTS (auxillary_nest);
  
  inverse = lambda_matrix_new (depth, depth);
  determinant = lambda_matrix_inverse (LTM_MATRIX (H), inverse, depth);
  
  /* H1 is H excluding its diagonal.  */
  H1 = lambda_matrix_new (depth, depth);
  lambda_matrix_copy (LTM_MATRIX (H), H1, depth, depth);
  
  for (i = 0; i < depth; i++)
    H1[i][i] = 0;
  
  /* Computes the linear offsets of the loop bounds.  */
  target = lambda_matrix_new (depth, depth);
  lambda_matrix_mult (H1, inverse, target, depth, depth, depth);
  
  target_nest = lambda_loopnest_new (depth, invariants);
  
  for (i = 0; i < depth; i++)
    {
      
      /* Get a new loop structure.  */
      target_loop = lambda_loop_new ();
      LN_LOOPS(target_nest)[i] = target_loop;
      
      /* Computes the gcd of the coefficients of the linear part.  */
      gcd1 = gcd_vector (target[i], i);
      
      /* Include the denominator in the GCD  */
      gcd1 = gcd (gcd1, determinant);
      
      /* Now divide through by the gcd  */
      for (j = 0; j < i; j++)
	target[i][j] = target[i][j] / gcd1;

      expression = lambda_linear_expression_new (depth, invariants);
      lambda_vector_copy (target[i], LLE_COEFFICIENTS(expression), depth);
      LLE_DENOMINATOR (expression) = determinant / gcd1;
      LLE_CONSTANT (expression) = 0;
      lambda_vector_clear (LLE_INVARIANT_COEFFICIENTS (expression), invariants);
      LL_LINEAR_OFFSET (target_loop) = expression;
    }
  
  /* For each loop, compute the new bounds.  */
  for (i = 0; i < depth; i++)
    {
      auxillary_loop = LN_LOOPS(auxillary_nest)[i];
      target_loop = LN_LOOPS(target_nest)[i];
      LL_STEP (target_loop) = LTM_MATRIX(H)[i][i];
      factor = LTM_MATRIX(H)[i][i];
      
      /* First we do the lower bound.  */
      auxillary_expr = LL_LOWER_BOUND (auxillary_loop);

      for (; auxillary_expr != NULL; auxillary_expr = LLE_NEXT (auxillary_expr))
	{
	  target_expr = lambda_linear_expression_new (depth, invariants);
	  lambda_vector_matrix_mult (LLE_COEFFICIENTS (auxillary_expr),
				     depth, inverse, depth,
				     LLE_COEFFICIENTS (target_expr));
	  lambda_vector_mult_const (LLE_COEFFICIENTS (target_expr),
				    LLE_COEFFICIENTS (target_expr), depth,
				    factor);
	  
	  LLE_CONSTANT (target_expr) = LLE_CONSTANT (auxillary_expr) * factor;
	  lambda_vector_copy (LLE_INVARIANT_COEFFICIENTS (auxillary_expr),
			      LLE_INVARIANT_COEFFICIENTS (target_expr),
			      invariants);
	  lambda_vector_mult_const (LLE_INVARIANT_COEFFICIENTS (target_expr),
				    LLE_INVARIANT_COEFFICIENTS (target_expr), 
				    invariants, factor);
	  LLE_DENOMINATOR (target_expr) = LLE_DENOMINATOR  (auxillary_expr);
	  
	  if (!lambda_vector_zerop (LLE_COEFFICIENTS (target_expr), depth))
	    {
	      LLE_CONSTANT (target_expr) = LLE_CONSTANT (target_expr) 
		* determinant;
	      lambda_vector_mult_const (LLE_INVARIANT_COEFFICIENTS (target_expr),
					LLE_INVARIANT_COEFFICIENTS (target_expr),
					invariants,
					determinant);
	      LLE_DENOMINATOR (target_expr) = LLE_DENOMINATOR (target_expr) 
		* determinant;
	    }
	  /* Find the gcd and divide by it here, rather than doing it
	     at the tree level.  */
	  gcd1 = gcd_vector (LLE_COEFFICIENTS (target_expr), depth);
	  gcd2 = gcd_vector (LLE_INVARIANT_COEFFICIENTS (target_expr), 
			     invariants);
	  gcd1 = gcd (gcd1, gcd2);
	  gcd1 = gcd (gcd1, LLE_CONSTANT (target_expr));
	  gcd1 = gcd (gcd1, LLE_DENOMINATOR (target_expr));
	  for (j = 0; j < depth; j++)
	    LLE_COEFFICIENTS (target_expr)[j] /= gcd1;
	  for (j = 0; j < invariants; j++)
	    LLE_INVARIANT_COEFFICIENTS (target_expr)[j] /=  gcd1;
	  LLE_CONSTANT (target_expr) /= gcd1;
	  LLE_DENOMINATOR (target_expr) /= gcd1;
	  /* Ignore if identical to existing bound.  */
	  if (!lle_equal (LL_LOWER_BOUND (target_loop), target_expr, depth,
			  invariants))
	    {
	      LLE_NEXT (target_expr) = LL_LOWER_BOUND (target_loop);
	      LL_LOWER_BOUND (target_loop) = target_expr;
	    }
	}      
      /* Now do the upper bound.  */
      auxillary_expr = LL_UPPER_BOUND (auxillary_loop);

      for (; auxillary_expr != NULL; auxillary_expr = LLE_NEXT (auxillary_expr))
	{
	  target_expr = lambda_linear_expression_new (depth, invariants);
	  lambda_vector_matrix_mult (LLE_COEFFICIENTS (auxillary_expr),
				     depth, inverse, depth,
				     LLE_COEFFICIENTS (target_expr));
	  lambda_vector_mult_const (LLE_COEFFICIENTS (target_expr),
				    LLE_COEFFICIENTS (target_expr), depth,
				    factor);
	  LLE_CONSTANT (target_expr) = LLE_CONSTANT (auxillary_expr) * factor;
	  lambda_vector_copy (LLE_INVARIANT_COEFFICIENTS (auxillary_expr),
			      LLE_INVARIANT_COEFFICIENTS (target_expr),
			      invariants);
	  lambda_vector_mult_const (LLE_INVARIANT_COEFFICIENTS (target_expr),
				    LLE_INVARIANT_COEFFICIENTS (target_expr), 
				    invariants, factor);
	  LLE_DENOMINATOR (target_expr) = LLE_DENOMINATOR  (auxillary_expr);
	  
	  if (!lambda_vector_zerop (LLE_COEFFICIENTS (target_expr), depth))
	    {
	      LLE_CONSTANT (target_expr) = LLE_CONSTANT (target_expr) 
		* determinant;
	      lambda_vector_mult_const (LLE_INVARIANT_COEFFICIENTS (target_expr),
					LLE_INVARIANT_COEFFICIENTS (target_expr),
					invariants,
					determinant);
	      LLE_DENOMINATOR (target_expr) = LLE_DENOMINATOR (target_expr) 
		* determinant;
	    }
	  /* Find the gcd and divide by it here, instead of at the
	     tree level.  */
	  gcd1 = gcd_vector (LLE_COEFFICIENTS (target_expr), depth);
	  gcd2 = gcd_vector (LLE_INVARIANT_COEFFICIENTS (target_expr), 
			     invariants);
	  gcd1 = gcd (gcd1, gcd2);
	  gcd1 = gcd (gcd1, LLE_CONSTANT (target_expr));
	  gcd1 = gcd (gcd1, LLE_DENOMINATOR (target_expr));
	  for (j = 0; j < depth; j++)
	    LLE_COEFFICIENTS (target_expr)[j] /= gcd1;
	  for (j = 0; j < invariants; j++)
	    LLE_INVARIANT_COEFFICIENTS (target_expr)[j] /=  gcd1;
	  LLE_CONSTANT (target_expr) /= gcd1;
	  LLE_DENOMINATOR (target_expr) /= gcd1;
	  /* Ignore if equal to existing bound.  */
	  if (!lle_equal (LL_UPPER_BOUND (target_loop), target_expr, depth,
			  invariants))
	    {
	      LLE_NEXT (target_expr) = LL_UPPER_BOUND (target_loop);
	      LL_UPPER_BOUND (target_loop) = target_expr;
	    }
	}
    }
  for (i = 0; i < depth; i++)
    {
      target_loop = LN_LOOPS (target_nest)[i];
      /* If necessary, exchange the upper and lower bounds and negate
	 the step size.  */
      if (stepsigns[i] < 0)
	{
	  LL_STEP (target_loop) *= -1;
	  tmp_expr = LL_LOWER_BOUND (target_loop);
	  LL_LOWER_BOUND (target_loop) = LL_UPPER_BOUND (target_loop);
	  LL_UPPER_BOUND (target_loop) = tmp_expr;
	}
    }
  return target_nest;
}

/* Compute the step signs.  */

static lambda_vector
lambda_compute_step_signs (lambda_trans_matrix trans,
			   lambda_vector stepsigns)
{
  lambda_matrix matrix, H;
  int size;
  lambda_vector newsteps;
  int i, j, factor, minimum_column;
  int temp;
  
  matrix = LTM_MATRIX (trans);
  size = LTM_ROWSIZE (trans);
  H = lambda_matrix_new (size, size);
  
  newsteps = lambda_vector_new (size);
  lambda_vector_copy (stepsigns, newsteps, size);
  
  lambda_matrix_copy (matrix, H, size, size);
  
  for (j = 0; j < size; j++)
    {
      lambda_vector row;
      row = H[j];
      for (i = j; i < size; i++)
	if (row[i] < 0)
	  lambda_matrix_col_negate (H, size, i);
      while (lambda_vector_first_nz (row, size, j + 1) < size)
	{
	  minimum_column = lambda_vector_min_nz (row, size, j);
	  lambda_matrix_col_exchange (H, size, j, minimum_column);
	  
	  temp = newsteps[j];
	  newsteps[j] = newsteps[minimum_column];
	  newsteps[minimum_column] = temp;
	  
	  for (i = j + 1; i < size; i++)
	    {
	      factor = row[i] / row[j];
	      lambda_matrix_col_add (H, size, j, i, -1 * factor);
	    }
	}
    }
  return newsteps;
}

/* Transform NEST according to TRANS, and return the new loop nest.  */

lambda_loopnest
lambda_loopnest_transform (lambda_loopnest nest, lambda_trans_matrix trans)
{
  lambda_loopnest auxillary_nest, target_nest;
  
  int depth, invariants;
  int i, j;
  lambda_lattice lattice;
  lambda_trans_matrix trans1, H, U;
  lambda_loop loop;
  lambda_linear_expression expression;
  lambda_vector origin;
  lambda_matrix origin_invariants;
  lambda_vector stepsigns;
  int f;
  
  depth = LN_DEPTH (nest);
  invariants = LN_INVARIANTS (nest);

  /* Keep track of the signs of the loop steps.  */  
  stepsigns = lambda_vector_new (depth);
  for (i = 0; i < depth; i++)
    {
      if (LL_STEP (LN_LOOPS(nest)[i]) > 0)
	stepsigns[i] = 1;
      else
	stepsigns[i] = -1;
    }

  /* Compute the lattice base.  */
  lattice = lambda_lattice_compute_base (nest);
  trans1 = lambda_trans_matrix_new (depth, depth);
  
  /* Multiply the transformation matrix by the lattice base.  */

  lambda_matrix_mult (LTM_MATRIX (trans), LATTICE_BASE (lattice), 
		      LTM_MATRIX (trans1),
		      depth, depth, depth);

  /* Compute the hermite normal form for the new transformation matrix.  */
  H = lambda_trans_matrix_new (depth, depth);
  U = lambda_trans_matrix_new (depth, depth);
  lambda_matrix_hermite (LTM_MATRIX (trans1), depth, LTM_MATRIX (H), LTM_MATRIX (U));
  
  /* Compute the auxillary loop nest's space from the unimodular
     portion.  */
  auxillary_nest = lambda_compute_auxillary_space (nest, U);
  

  /* Compute the loop step signs from the old step signs and the
     transformation matrix.  */
  stepsigns = lambda_compute_step_signs (trans1, stepsigns);
  
  /* Compute the target loop nest space from the auxillary nest and
     the lower triangular matrix H.  */
  target_nest = lambda_compute_target_space (auxillary_nest, H, stepsigns);  
  origin = lambda_vector_new (depth);
  origin_invariants = lambda_matrix_new (depth, invariants);
  lambda_matrix_vector_mult (LTM_MATRIX (trans), depth, depth, 
			     LATTICE_ORIGIN (lattice), origin);
  lambda_matrix_mult (LTM_MATRIX (trans), LATTICE_ORIGIN_INVARIANTS (lattice),
		      origin_invariants, depth, depth, invariants);
  for (i = 0; i < depth; i++)
    {
      loop = LN_LOOPS (target_nest)[i];
      expression = LL_LINEAR_OFFSET (loop);
      if (lambda_vector_zerop (LLE_COEFFICIENTS (expression), depth))
	f = 1;
      else
	f = LLE_DENOMINATOR (expression);
      LLE_CONSTANT (expression) += f * origin[i];
      
      for (j = 0; j < invariants; j++)
	LLE_INVARIANT_COEFFICIENTS(expression)[j] += f * origin_invariants[i][j];
    }


  return target_nest;
  
}


/* Convert a gcc tree expression to a linear expression.  
   This is a trivial implementation.  */

static lambda_linear_expression
gcc_tree_to_linear_expression (int depth, tree expr, 
			       varray_type outerinductionvars,
			       varray_type invariants,
			       int extra)
{
  lambda_linear_expression lle = NULL;
  switch (TREE_CODE (expr))
    {
    case INTEGER_CST:
      {
	lle = lambda_linear_expression_new (depth, 2 * depth);
	LLE_CONSTANT (lle) = TREE_INT_CST_LOW (expr);
	if (extra != 0) 
	  LLE_CONSTANT (lle) = extra;
	
	LLE_DENOMINATOR (lle) = 1;
      }
      break;
    case SSA_NAME:
      {
	size_t i;
	for (i = 0; i < VARRAY_ACTIVE_SIZE (outerinductionvars); i++)
	  if (VARRAY_TREE (outerinductionvars, i) != NULL)
	    {
	      tree iv = VARRAY_TREE (outerinductionvars, i);
	      if (SSA_NAME_VAR (iv) == SSA_NAME_VAR (expr))
		{
		  lle = lambda_linear_expression_new (depth, 2 * depth);
		  LLE_COEFFICIENTS (lle)[i] = 1;
		  if (extra != 0)
		    LLE_CONSTANT (lle) = extra;
		  
		  LLE_DENOMINATOR (lle) = 1;
		}
	    }
	for (i = 0; i < VARRAY_ACTIVE_SIZE (invariants); i++)
	  if (VARRAY_TREE (invariants, i) != NULL)
	    {
	      tree invar = VARRAY_TREE (invariants, i);
	      if (SSA_NAME_VAR (invar) == SSA_NAME_VAR (expr))
		{
		  lle = lambda_linear_expression_new (depth, 2 * depth);
		  LLE_INVARIANT_COEFFICIENTS (lle)[i] = 1;
		  if (extra != 0)
		    LLE_CONSTANT (lle) = extra;
		  LLE_DENOMINATOR (lle) = 1;
		}
	    }	
      }
      break;
    default:
      return NULL;
    }
  
  return lle;
}

/* Return true if OP is invariant in LOOP.  */
static bool
invariant_in_loop (struct loop *loop, tree op)
{
  if (TREE_CODE (op) == SSA_NAME)
    {
      if (TREE_CODE (SSA_NAME_VAR (op)) == PARM_DECL
	  && IS_EMPTY_STMT (SSA_NAME_DEF_STMT (op)))
	return true;
      if (IS_EMPTY_STMT (SSA_NAME_DEF_STMT (op)))
	return false;
      return !flow_bb_inside_loop_p (loop, 
				     bb_for_stmt (SSA_NAME_DEF_STMT (op)));
    }
  return false;
}

    

/* Generate a lambda loop from a gcc loop.  */

static lambda_loop
gcc_loop_to_lambda_loop (struct loop *loop, int depth,
			 varray_type *invariants,
			 tree *ourinductionvar,
			 varray_type outerinductionvars)
{
  tree phi;
  tree exit_cond;
  tree access_fn, inductionvar;
  tree step;
  lambda_loop lloop = NULL;
  lambda_linear_expression lbound, ubound;
  tree test;
  int stepint;
  int extra = 0;
#if 0
  tree ev;
  tree nb_iter;
  tree base;
  tree final;
#endif 
  
  use_optype uses;
  
  
  /* Find out induction var and set the pointer so that the caller can
     append it to the outerinductionvars array later.  */
  
  inductionvar = find_induction_var_from_exit_cond (loop);
  *ourinductionvar = inductionvar;
  
  exit_cond = get_loop_exit_condition (loop);

  if (inductionvar == NULL || exit_cond == NULL)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "Unable to convert loop: Cannot determine exit condition or induction variable for loop.\n");
      return NULL;
    }
  
  
  
  test = TREE_OPERAND (exit_cond, 0);
  if (TREE_CODE (test) != LE_EXPR 
      && TREE_CODE (test) != LT_EXPR
      && TREE_CODE (test) != NE_EXPR)
    {
      
      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "Unable to convert loop: Loop exit test uses unhandled test condition:");    
	  print_generic_stmt (dump_file, test, 0);
	  fprintf (dump_file, "\n");
	}
      return NULL;
    }
#if 0  
  ev = analyze_scalar_evolution (loop, inductionvar);
  
  if (!evolution_function_is_affine_or_constant_p (ev))
    return NULL;
  
  nb_iter = number_of_iterations_in_loop (loop);
  nb_iter = chrec_fold_minus (chrec_type (nb_iter), nb_iter, 
			      convert (chrec_type (nb_iter), integer_one_node));
  base = CHREC_LEFT (ev);
  step = CHREC_RIGHT (ev);
  final = chrec_apply (loop->num, ev, nb_iter);
#endif

  /* XXX - MUST BE BETTER WAY TO GET PHI NODE FOR INDUCTION
     VARIABLES.  */
  if (SSA_NAME_DEF_STMT (inductionvar) == NULL_TREE)
    {
      
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "Unable to convert loop: Cannot find PHI node for induction variable\n");
      
      return NULL;
    }
  
  
  phi = SSA_NAME_DEF_STMT (inductionvar);
  if (TREE_CODE (phi) != PHI_NODE)
    {
      get_stmt_operands (phi);
      uses = STMT_USE_OPS (phi);

      if (!uses)
	{
	  
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "Unable to convert loop: Cannot find PHI node for induction variable\n");
	  
	  return NULL;
	}
      
      phi = USE_OP (uses, 0);
      phi = SSA_NAME_DEF_STMT (phi);
      if (TREE_CODE (phi) != PHI_NODE)
	{
	  
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "Unable to convert loop: Cannot find PHI node for induction variable\n");  
	  return NULL;
	}
      
    }
  
  access_fn = instantiate_parameters
    (loop,
     analyze_scalar_evolution (loop, PHI_RESULT (phi)));
  if (!access_fn)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "Unable to convert loop: Access function for induction variable phi is NULL\n");
      
      return NULL;
    }
  
  step = evolution_part_in_loop_num (access_fn, loop_num (loop));
  if (!step)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "Unable to convert loop: Cannot determine step of loop.\n");
      
      return NULL;
    }
  if (TREE_CODE (step) != INTEGER_CST)
    {
     
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "Unable to convert loop: Step of loop is not integer.\n");
      return NULL;
    }
  
  stepint = TREE_INT_CST_LOW (step);

  /* Only want phis for induction vars, which will have two
     arguments.  */
  if (PHI_NUM_ARGS (phi) != 2)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "Unable to convert loop: PHI node for induction variable has >2 arguments\n");
      return NULL;
    }

  /* Another induction variable check. One argument's source should be
     in the loop, one outside the loop.  */
  if (flow_bb_inside_loop_p (loop, PHI_ARG_EDGE (phi, 0)->src)
      && flow_bb_inside_loop_p (loop, PHI_ARG_EDGE (phi, 1)->src))
    {
      
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "Unable to convert loop: PHI edges both inside loop, or both outside loop.\n");
      
      return NULL;
    }
  
  

  if (flow_bb_inside_loop_p (loop, PHI_ARG_EDGE (phi, 0)->src))

    lbound = gcc_tree_to_linear_expression (depth, PHI_ARG_DEF (phi, 1),
					    outerinductionvars, *invariants,
					    0);
  else
    lbound = gcc_tree_to_linear_expression (depth, PHI_ARG_DEF (phi, 0),
					    outerinductionvars, *invariants,
					    0);
  if (!lbound)
    {
      
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "Unable to convert loop: Cannot convert lower bound to linear expression\n");
      
      return NULL;
    }
  if (TREE_CODE (TREE_OPERAND (test, 1)) == SSA_NAME)
    if (invariant_in_loop (loop, TREE_OPERAND (test, 1)))
      VARRAY_PUSH_TREE (*invariants, TREE_OPERAND (test, 1));
  
  /* We only size the vectors assuming we have, at max, 2 times as many
     invariants as we do loops (one for each bound).  */
  if (VARRAY_ACTIVE_SIZE (*invariants) > (unsigned int)(2 * depth))
    abort ();

  /* We might have some leftover. */
  if (TREE_CODE (test) == LT_EXPR)
    extra = -1 * stepint;
  else if (TREE_CODE (test) == NE_EXPR)
    extra = -1 * stepint;
  
  ubound = gcc_tree_to_linear_expression (depth, 
					  TREE_OPERAND (test, 1),
					  outerinductionvars,
					  *invariants,
					  extra);
  
  if (!ubound)
    {
      
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "Unable to convert loop: Cannot convert upper bound to linear expression\n");
      return NULL;
    }
  

  lloop = lambda_loop_new ();
  LL_STEP (lloop) = stepint;
  LL_LOWER_BOUND (lloop) = lbound;
  LL_UPPER_BOUND (lloop) = ubound;
  return lloop;
}

/* Given an exit condition, find the induction variable it is testing
   against.  */

static tree
find_induction_var_from_exit_cond (struct loop *loop)
{
  tree expr = get_loop_exit_condition (loop);
  tree test;
  if (expr == NULL_TREE)
    return NULL_TREE;
  if (TREE_CODE (expr) != COND_EXPR)
    return NULL_TREE; 
  test = TREE_OPERAND (expr, 0);
  if (TREE_CODE_CLASS (TREE_CODE (test)) != '<')
    return NULL_TREE;
  if (TREE_CODE (TREE_OPERAND (test, 0)) != SSA_NAME)
    return NULL_TREE;
  return TREE_OPERAND (test, 0);
}

/* Generate a lambda loopnest from a gcc loopnest. */

lambda_loopnest 
gcc_loopnest_to_lambda_loopnest (struct loop *loop_nest,
				 varray_type *inductionvars,
				 varray_type *invariants)
{
  lambda_loopnest ret;
  struct loop *temp;
  int depth = 0;
  size_t i;
  varray_type loops;
  lambda_loop newloop;
  tree inductionvar = NULL;
  

  temp = loop_nest;
  while (temp)
    {
      depth++;
      temp = temp->inner;
    }
  VARRAY_GENERIC_PTR_INIT (loops, 1, "Loop nest");
  VARRAY_GENERIC_PTR_INIT (*inductionvars, 1, "induction vars");
  VARRAY_GENERIC_PTR_INIT (*invariants, 1, "Invariants");
  temp = loop_nest;
  while (temp)
    {
      newloop = gcc_loop_to_lambda_loop (temp, depth, invariants,
					 &inductionvar, *inductionvars);
      if (!newloop)
	return NULL;
      VARRAY_PUSH_GENERIC_PTR (*inductionvars, inductionvar);
      VARRAY_PUSH_GENERIC_PTR (loops, newloop);
      temp = temp->inner;
    }
  ret = lambda_loopnest_new (depth, 2 * depth);
  for (i = 0; i < VARRAY_ACTIVE_SIZE (loops); i++)
    LN_LOOPS(ret)[i] = VARRAY_GENERIC_PTR (loops, i);
  

  return ret;
  
}

/* Convert a lambda body vector to a gcc tree.  */

static tree
lbv_to_gcc_expression (lambda_body_vector lbv,
		       varray_type induction_vars,
		       tree *stmts_to_insert)
{
  tree stmts, stmt, resvar, name;
  size_t i;
  tree_stmt_iterator tsi;
  
  /* Create a statement list and a linear expression temporary.  */
  stmts = alloc_stmt_list ();
  resvar = create_tmp_var (integer_type_node, "lletmp");
  add_referenced_tmp_var (resvar);

  /* Start at 0.  */
  stmt = build (MODIFY_EXPR, void_type_node, resvar, integer_zero_node);
  name = make_ssa_name (resvar, stmt);
  TREE_OPERAND (stmt, 0) = name;
  tsi = tsi_last (stmts);
  tsi_link_after (&tsi, stmt, TSI_CONTINUE_LINKING);
 
  for (i = 0; i < VARRAY_ACTIVE_SIZE (induction_vars); i++)
    {
      if (LBV_COEFFICIENTS (lbv)[i] != 0)
	{
	  tree newname;
	  
	  /* newname = coefficient * induction_variable */
	  stmt = build (MODIFY_EXPR, void_type_node, resvar, 
			fold (build (MULT_EXPR, integer_type_node,
				     VARRAY_TREE (induction_vars, i), 
				     build_int_cst (integer_type_node,
						    LBV_COEFFICIENTS (lbv)[i]))));
	  newname = make_ssa_name (resvar, stmt);
	  TREE_OPERAND (stmt, 0) = newname;
	  tsi = tsi_last (stmts);
	  tsi_link_after (&tsi, stmt, TSI_CONTINUE_LINKING);
	  /* name = name + newname */
	  stmt = build (MODIFY_EXPR, void_type_node, resvar,
			build (PLUS_EXPR, integer_type_node, 
			       name, newname));
	  name = make_ssa_name (resvar, stmt);
	  TREE_OPERAND (stmt, 0) = name;
	  tsi = tsi_last (stmts);
	  tsi_link_after (&tsi, stmt, TSI_CONTINUE_LINKING);
	}     
    }

  /* Handle any denominator that occurs.  */
  if (LBV_DENOMINATOR (lbv) != 1)
    {
#if 0
      if (wrap == MAX_EXPR)
#endif
	stmt = build (MODIFY_EXPR, void_type_node, resvar,
		      build (CEIL_DIV_EXPR, integer_type_node,
			     name, build_int_cst (integer_type_node,
						  LBV_DENOMINATOR (lbv))));
#if 0
      else if (wrap == MIN_EXPR)
	stmt = build (MODIFY_EXPR, void_type_node, resvar,
		      build (FLOOR_DIV_EXPR, integer_type_node,
			     name, build_int_cst (integer_type_node,
						  LBV_DENOMINATOR (lbv))));
      else
	abort ();
#endif
      name = make_ssa_name (resvar, stmt);
      TREE_OPERAND (stmt, 0) = name;
      tsi = tsi_last (stmts);
      tsi_link_after (&tsi, stmt, TSI_CONTINUE_LINKING);
    }
  *stmts_to_insert = stmts;
  return name;
}

		       
/* Convert a linear expression from coefficient and constant form to a
   gcc tree.  This will likely generate trees that are badly in need
   of constant and copy propagation.  */

static tree
lle_to_gcc_expression (lambda_linear_expression lle,
		       lambda_linear_expression offset,
		       varray_type induction_vars,
		       varray_type invariants,
		       enum tree_code wrap, 
		       tree *stmts_to_insert)
{  
  tree stmts, stmt, resvar, name;
  size_t i;
  tree_stmt_iterator tsi;
  varray_type results;
  
  name = NULL_TREE;
  /* Create a statement list and a linear expression temporary.  */
  stmts = alloc_stmt_list ();
  resvar = create_tmp_var (integer_type_node, "lletmp");
  add_referenced_tmp_var (resvar);
  VARRAY_TREE_INIT (results, 1, "Results");
  
  for (; lle != NULL; lle = LLE_NEXT (lle))
    {    
      /* Start at 0.  */
      stmt = build (MODIFY_EXPR, void_type_node, resvar, integer_zero_node);
      name = make_ssa_name (resvar, stmt);
      TREE_OPERAND (stmt, 0) = name;
      tsi = tsi_last (stmts);
      tsi_link_after (&tsi, stmt, TSI_CONTINUE_LINKING);
    /* First do the induction variables.  
       name = name + (coeff * induction variable)  */
      for (i = 0; i < VARRAY_ACTIVE_SIZE (induction_vars); i++)
	{
	  if (LLE_COEFFICIENTS (lle)[i] != 0)
	    {
	      tree newname;
	      tree mult;
	      tree coeff;
	      coeff = build_int_cst (integer_type_node, 
				     LLE_COEFFICIENTS (lle)[i]);
	      mult = fold (build (MULT_EXPR, integer_type_node,
				  VARRAY_TREE (induction_vars, i), coeff));
	      /* newname = coefficient * induction_variable */
	      stmt = build (MODIFY_EXPR, void_type_node, resvar, mult);
	      newname = make_ssa_name (resvar, stmt);
	      TREE_OPERAND (stmt, 0) = newname;
	      tsi = tsi_last (stmts);
	      tsi_link_after (&tsi, stmt, TSI_CONTINUE_LINKING);
	      /* name = name + newname */
	      stmt = build (MODIFY_EXPR, void_type_node, resvar,
			    build (PLUS_EXPR, integer_type_node, 
				   name, newname));
	      name = make_ssa_name (resvar, stmt);
	      TREE_OPERAND (stmt, 0) = name;
	      tsi = tsi_last (stmts);
	      tsi_link_after (&tsi, stmt, TSI_CONTINUE_LINKING);
	    }     
	}
      /* Handle our invariants.
      name = name + (coeff * invariant).  */
      for (i = 0; i < VARRAY_ACTIVE_SIZE (invariants); i++)
	{
	  if (LLE_INVARIANT_COEFFICIENTS (lle)[i] != 0)
	    {
	      tree newname;
	      tree mult;
	      tree coeff;
	      coeff = build_int_cst (integer_type_node, 
				     LLE_INVARIANT_COEFFICIENTS (lle)[i]);
	      mult = fold (build (MULT_EXPR, integer_type_node,
				  VARRAY_TREE (invariants, i), coeff));
	      /* newname = coefficient * invariant */
	      stmt = build (MODIFY_EXPR, void_type_node, resvar, mult);
	      newname = make_ssa_name (resvar, stmt);
	      TREE_OPERAND (stmt, 0) = newname;
	      tsi = tsi_last (stmts);
	      tsi_link_after (&tsi, stmt, TSI_CONTINUE_LINKING);
	      /* name = name + newname */
	      stmt = build (MODIFY_EXPR, void_type_node, resvar,
			    build (PLUS_EXPR, integer_type_node, 
				   name, newname));
	      name = make_ssa_name (resvar, stmt);
	      TREE_OPERAND (stmt, 0) = name;
	      tsi = tsi_last (stmts);
	      tsi_link_after (&tsi, stmt, TSI_CONTINUE_LINKING);
	    }     
	}
      
      /* Now handle the constant.
	 name = name + constant.  */
      if (LLE_CONSTANT (lle) != 0)
	{
	  stmt = build (MODIFY_EXPR, void_type_node, resvar,
			build (PLUS_EXPR, integer_type_node, 
			       name, build_int_cst (integer_type_node,
						    LLE_CONSTANT (lle))));
	  name = make_ssa_name (resvar, stmt);
	  TREE_OPERAND (stmt, 0) = name;
	  tsi = tsi_last (stmts);
	  tsi_link_after (&tsi, stmt, TSI_CONTINUE_LINKING);
	}

      /* Now handle the offset.
	 name = name + linear offset.  */
      if (LLE_CONSTANT (offset) != 0)
	{
	  stmt = build (MODIFY_EXPR, void_type_node, resvar,
			build (PLUS_EXPR, integer_type_node, 
			       name, build_int_cst (integer_type_node,
						    LLE_CONSTANT (offset))));
	  name = make_ssa_name (resvar, stmt);
	  TREE_OPERAND (stmt, 0) = name;
	  tsi = tsi_last (stmts);
	  tsi_link_after (&tsi, stmt, TSI_CONTINUE_LINKING);
	}
      
      /* Handle any denominator that occurs.  */
      if (LLE_DENOMINATOR (lle) != 1)
	{
	  if (wrap == MAX_EXPR)
	    stmt = build (MODIFY_EXPR, void_type_node, resvar,
			  build (CEIL_DIV_EXPR, integer_type_node,
				 name, build_int_cst (integer_type_node,
						      LLE_DENOMINATOR (lle))));
	  else if (wrap == MIN_EXPR)
	    stmt = build (MODIFY_EXPR, void_type_node, resvar,
			  build (FLOOR_DIV_EXPR, integer_type_node,
				 name, build_int_cst (integer_type_node,
						      LLE_DENOMINATOR (lle))));
	  else
	    abort ();
	  name = make_ssa_name (resvar, stmt);
	  TREE_OPERAND (stmt, 0) = name;
	  tsi = tsi_last (stmts);
	  tsi_link_after (&tsi, stmt, TSI_CONTINUE_LINKING);
	}
      VARRAY_PUSH_TREE (results, name);
    }

  /* Again, out of laziness, we don't handle this case yet.  It's not
     hard, it just hasn't occurred.  */
  if (VARRAY_ACTIVE_SIZE (results) > 2)
    abort ();
  
  /* We may need to wrap the results in a MAX_EXPR or MIN_EXPR.  */
  if (VARRAY_ACTIVE_SIZE (results) > 1)
    {
      tree op1 = VARRAY_TREE (results, 0);
      tree op2 = VARRAY_TREE (results, 1);
      stmt = build (MODIFY_EXPR, void_type_node, resvar,
		    build (wrap, integer_type_node, op1, op2));
      name = make_ssa_name (resvar, stmt);
      TREE_OPERAND (stmt, 0) = name;
      tsi = tsi_last (stmts);
      tsi_link_after (&tsi, stmt, TSI_CONTINUE_LINKING);
    }

  *stmts_to_insert = stmts;
  return name;
}

/* Transform a lambda loopnest back into code.  This changes the
   loops, their induction variables, and their bodies, so that they
   match the transformed loopnest.  */
void 
lambda_loopnest_to_gcc_loopnest (struct loop *old_loopnest, 
				 varray_type old_ivs,
				 varray_type invariants,
				 lambda_loopnest new_loopnest,
				 lambda_trans_matrix transform)
{
 
  struct loop *temp;
  size_t i = 0;
  size_t depth = 0;
  varray_type new_ivs;
  block_stmt_iterator bsi;
  basic_block *bbs;
  
  if (dump_file)
    {
      transform = lambda_trans_matrix_inverse (transform);
      fprintf (dump_file, "Inverse of transformation matrix:\n");
      print_lambda_trans_matrix (dump_file, transform);
    }
  temp = old_loopnest;
  VARRAY_GENERIC_PTR_INIT (new_ivs, 1, "New induction variables");
  while (temp)
    {
      temp = temp->inner;
      depth++;
    }
  temp = old_loopnest;
  
  while (temp)
    {
      lambda_loop newloop;
      basic_block bb;
      tree ivvar, ivvarinced, exitcond, stmts;
      enum tree_code testtype;
      tree newupperbound, newlowerbound;
      lambda_linear_expression offset;
      /* First, build the new induction variable temporary  */
      
      ivvar = create_tmp_var (integer_type_node, "lnivtmp");
      add_referenced_tmp_var (ivvar);
      
      VARRAY_PUSH_GENERIC_PTR (new_ivs, ivvar);
    
      newloop = LN_LOOPS (new_loopnest)[i];
      
      /* Linear offset is a bit tricky to handle.  */
      offset = LL_LINEAR_OFFSET (newloop);
      
      if (LLE_DENOMINATOR (offset) != 1
	  || !lambda_vector_zerop (LLE_COEFFICIENTS (offset), depth))
	abort ();
      
      /* Now build the  new lower bounds, and insert the statements
	 necessary to generate it on the loop preheader. */
      newlowerbound = lle_to_gcc_expression (LL_LOWER_BOUND (newloop), 
					     LL_LINEAR_OFFSET (newloop),
					     new_ivs,
					     invariants,
					     MAX_EXPR, &stmts);
      bsi_insert_on_edge_immediate (loop_preheader_edge (temp), stmts);

      /* Build the new upper bound and insert its statements in the
	 basic block of the exit condition */ 
      newupperbound = lle_to_gcc_expression (LL_UPPER_BOUND (newloop),
					     LL_LINEAR_OFFSET (newloop),
					     new_ivs,
					     invariants,
					     MIN_EXPR, &stmts);
      /* XXX Is this right, or do we want before the first statement? */
      exitcond = get_loop_exit_condition (temp);
      bb = bb_for_stmt (exitcond);
      bsi = bsi_start (bb);
      bsi_insert_after (&bsi, stmts, BSI_NEW_STMT);
      
      /* Create the new iv, and insert it's increment on the latch
	 block.  */

      bb = temp->latch->pred->src;
      bsi = bsi_last (bb);
      create_iv (newlowerbound,
		 build_int_cst (integer_type_node, LL_STEP (newloop)), 
		 ivvar, temp, &bsi, false, &ivvar, &ivvarinced);

      /* Replace the exit condition with the new upper bound
	 comparison.  */
      testtype = LL_STEP (newloop) >= 0 ? LE_EXPR : GE_EXPR;
      COND_EXPR_COND (exitcond) = build (testtype,
					 boolean_type_node, 
					 ivvarinced, newupperbound);
      modify_stmt (exitcond);
      VARRAY_GENERIC_PTR (new_ivs, i) = ivvar;

      i++;      
      temp = temp->inner;
    }

  /* Go through the loop and make iv replacements.  */
  bbs = get_loop_body (old_loopnest);
  for (i = 0; i < old_loopnest->num_nodes; i++)
    for (bsi = bsi_start (bbs[i]); !bsi_end_p (bsi); bsi_next (&bsi))
      {
	tree stmt = bsi_stmt (bsi);
	use_optype uses;
	size_t j;
	
	get_stmt_operands (stmt);
	uses = STMT_USE_OPS (stmt);
	for (j = 0; j < NUM_USES (uses); j++)
	  {
	    size_t k;
	    tree *use = USE_OP_PTR (uses, j);
	    for (k = 0; k < VARRAY_ACTIVE_SIZE (old_ivs); k++)
	      {
		tree oldiv = VARRAY_TREE (old_ivs, k);
		if (SSA_NAME_VAR (*use) == SSA_NAME_VAR (oldiv))
		  {
		    tree newiv, stmts;
		    lambda_body_vector lbv;

		    /* Compute the new expression for the induction
		       variable.  */
		    depth = VARRAY_ACTIVE_SIZE (new_ivs);
		    lbv = lambda_body_vector_new (depth);
		    LBV_COEFFICIENTS (lbv)[k] = 1;
		    lbv = lambda_body_vector_compute_new (transform, lbv);
		    newiv = lbv_to_gcc_expression (lbv, new_ivs, &stmts);

		    /* Insert the statements to build that
		       expression.  */
		    bsi_insert_before (&bsi, stmts, BSI_SAME_STMT);
		    
		    /* Replace the use with the result of that
		       expression.  */
		    if (dump_file)
		      {
			fprintf (dump_file, 
				 "Replacing induction variable use of ");
			print_generic_stmt (dump_file, *use, 0);
			fprintf (dump_file, " with ");
			print_generic_stmt (dump_file, newiv, 0);
			fprintf (dump_file, "\n");
		      }
		    *use = newiv;
		  }
	      }
	    
	  }
      }
}
