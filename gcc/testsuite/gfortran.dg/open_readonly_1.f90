! { dg-do run }
! PR19451
! Writing to a non-empty readonly file caused a segfault.
! We were still trying to write the EOR after an error ocurred
program prog
  open (unit=10, file='PR19451.dat')
  write (10,*) "Hello World"
  close (10)
  open (unit=10, file='PR19451.dat', action="read")
  write (10,*,err=20) "Hello World"
  call abort()
  20 continue
end program

