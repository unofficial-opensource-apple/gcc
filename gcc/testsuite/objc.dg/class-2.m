/* Test super classes.  */
/* { dg-do compile } */

@interface supclass1
@end

@interface supclass2
@end

@interface class1 : supclass1
@end

/* APPLE LOCAL begin test suite */
@implementation class1 : supclass2 /* { dg-error "conflicting super class name" } */
@end /* { dg-error "previous declaration" "" { target *-*-* } 14 } */
/* APPLE LOCAL end test suite */
