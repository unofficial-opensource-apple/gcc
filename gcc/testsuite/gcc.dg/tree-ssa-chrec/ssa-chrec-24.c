/* { dg-do compile } */ 
/* { dg-options "-O1 -fscalar-evolutions -fdump-tree-scev-details" } */


int bar (void);

int foo ()
{
  int c;
  
  /* This exercises the initial condition propagator: 
     Interval Copy Constant Propagation (ICCP).  */
  if (bar ())
    c = 2;
  else
    c = 3;
  
  while (c)
    {
      c += 5;
    }
}

/* 
   c  ->  {[2, 3], +, 5}_1
*/

/* FIXME. */

