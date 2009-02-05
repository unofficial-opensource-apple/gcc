;; ARM VFP coprocessor Machine Description
;; Copyright (C) 2003 Free Software Foundation, Inc.
;; Written by CodeSourcery, LLC.
;;
;; This file is part of GCC.
;;
;; GCC is free software; you can redistribute it and/or modify it
;; under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 2, or (at your option)
;; any later version.
;;
;; GCC is distributed in the hope that it will be useful, but
;; WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;; General Public License for more details.
;;
;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING.  If not, write to the Free
;; Software Foundation, 59 Temple Place - Suite 330, Boston, MA
;; 02111-1307, USA.  */

;; Additional register numbers
(define_constants
  [(VFPCC_REGNUM 95)]
)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Pipeline description
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define_automaton "vfp11")

;; There are 3 pipelines in the VFP11 unit.
;;
;; - A 8-stage FMAC pipeline (7 execute + writeback) with forward from
;;   fourth stage for simple operations.
;;
;; - A 5-stage DS pipeline (4 execute + writeback) for divide/sqrt insns.
;;   These insns also uses first execute stage of FMAC pipeline.
;;
;; - A 4-stage LS pipeline (execute + 2 memory + writeback) with forward from
;;   second memory stage for loads.

;; We do not model Write-After-Read hazards.
;; We do not do write scheduling with the arm core, so it is only necessary
;; to model the first stage of each pipeline
;; ??? Need to model LS pipeline properly for load/store multiple?
;; We do not model fmstat properly.  This could be done by modeling pipelines
;; properly and defining an absence set between a dummy fmstat unit and all
;; other vfp units.

(define_cpu_unit "fmac" "vfp11")

(define_cpu_unit "ds" "vfp11")

(define_cpu_unit "vfp_ls" "vfp11")

;; The VFP "type" attributes differ from those used in the FPA model.
;; ffarith	Fast floating point insns, eg. abs, neg, cpy, cmp.
;; farith	Most arithmetic insns.
;; fmul		Double precision multiply.
;; fdivs	Single precision sqrt or division.
;; fdivd	Double precision sqrt or division.
;; f_load	Floating point load from memory.
;; f_store	Floating point store to memory.
;; f_2_r	Transfer vfp to arm reg.
;; r_2_f	Transfer arm to vfp reg.

(define_insn_reservation "vfp_ffarith" 4
 (and (eq_attr "fpu" "vfp")
      (eq_attr "type" "ffarith"))
 "fmac")

(define_insn_reservation "vfp_farith" 8
 (and (eq_attr "fpu" "vfp")
      (eq_attr "type" "farith"))
 "fmac")

(define_insn_reservation "vfp_fmul" 9
 (and (eq_attr "fpu" "vfp")
      (eq_attr "type" "fmul"))
 "fmac*2")

(define_insn_reservation "vfp_fdivs" 19
 (and (eq_attr "fpu" "vfp")
      (eq_attr "type" "fdivs"))
 "ds*15")

(define_insn_reservation "vfp_fdivd" 33
 (and (eq_attr "fpu" "vfp")
      (eq_attr "type" "fdivd"))
 "fmac+ds*29")

;; Moves to/from arm regs also use the load/store pipeline.
(define_insn_reservation "vfp_fload" 4
 (and (eq_attr "fpu" "vfp")
      (eq_attr "type" "f_load,r_2_f"))
 "vfp_ls")

(define_insn_reservation "vfp_fstore" 4
 (and (eq_attr "fpu" "vfp")
      (eq_attr "type" "f_load,f_2_r"))
 "vfp_ls")


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Insn pattern
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; SImode moves
;; ??? For now do not allow loading constants into vfp regs.  This causes
;; problems because small constants get converted into adds.
(define_insn "*arm_movsi_vfp"
  [(set (match_operand:SI 0 "nonimmediate_operand" "=r,r,r ,m,!w,r,!w,!w,  Uv")
      (match_operand:SI 1 "general_operand"	   "rI,K,mi,r,r,!w,!w,Uvi,!w"))]
  "TARGET_ARM && TARGET_VFP && TARGET_HARD_FLOAT
   && (   s_register_operand (operands[0], SImode)
       || s_register_operand (operands[1], SImode))"
  "@
  mov%?\\t%0, %1
  mvn%?\\t%0, #%B1
  ldr%?\\t%0, %1
  str%?\\t%1, %0
  fmsr%?\\t%0, %1\\t%@ int
  fmrs%?\\t%0, %1\\t%@ int
  fcpys%?\\t%0, %1\\t%@ int
  flds%?\\t%0, %1\\t%@ int
  fsts%?\\t%1, %0\\t%@ int"
  [(set_attr "predicable" "yes")
   (set_attr "type" "*,*,load1,store1,r_2_f,f_2_r,ffarith,f_load,f_store")
   (set_attr "pool_range"     "*,*,4096,*,*,*,*,1020,*")
   (set_attr "neg_pool_range" "*,*,4084,*,*,*,*,1008,*")]
)


;; DImode moves

(define_insn "*arm_movdi_vfp"
  [(set (match_operand:DI 0 "nonimmediate_di_operand" "=r, r,m,w,r,w,w, Uv")
	(match_operand:DI 1 "di_operand"              "rIK,mi,r,r,w,w,Uvi,w"))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "*
  switch (which_alternative)
    {
    case 0: case 1: case 2:
      return (output_move_double (operands));
    case 3:
      return \"fmdrr%?\\t%P0, %1\\t%@ int\";
    case 4:
      return \"fmrrd%?\\t%0, %1\\t%@ int\";
    case 5:
      return \"fcpyd%?\\t%P0, %P1\\t%@ int\";
    case 6:
      return \"fldd%?\\t%P0, %1\\t%@ int\";
    case 7:
      return \"fstd%?\\t%P1, %0\\t%@ int\";
    default:
      abort ();
    }
  "
  [(set_attr "type" "*,load2,store2,r_2_f,f_2_r,ffarith,f_load,f_store")
   (set_attr "length" "8,8,8,4,4,4,4,4")
   (set_attr "pool_range"     "*,1020,*,*,*,*,1020,*")
   (set_attr "neg_pool_range" "*,1008,*,*,*,*,1008,*")]
)


;; SFmode moves

(define_insn "*movsf_vfp"
  [(set (match_operand:SF 0 "nonimmediate_operand" "=w,r,w  ,Uv,r ,m,w,r")
	(match_operand:SF 1 "general_operand"	   " r,w,UvE,w, mE,r,w,r"))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP
   && (   s_register_operand (operands[0], SFmode)
       || s_register_operand (operands[1], SFmode))"
  "@
  fmsr%?\\t%0, %1
  fmrs%?\\t%0, %1
  flds%?\\t%0, %1
  fsts%?\\t%1, %0
  ldr%?\\t%0, %1\\t%@ float
  str%?\\t%1, %0\\t%@ float
  fcpys%?\\t%0, %1
  mov%?\\t%0, %1\\t%@ float"
  [(set_attr "predicable" "yes")
   (set_attr "type" "r_2_f,f_2_r,ffarith,*,f_load,f_store,load1,store1")
   (set_attr "pool_range" "*,*,1020,*,4096,*,*,*")
   (set_attr "neg_pool_range" "*,*,1008,*,4080,*,*,*")]
)


;; DFmode moves

(define_insn "*movdf_vfp"
  [(set (match_operand:DF 0 "nonimmediate_soft_df_operand" "=w,r,r, m,w  ,Uv,w,r")
	(match_operand:DF 1 "soft_df_operand"		   " r,w,mF,r,UvF,w, w,r"))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "*
  {
    switch (which_alternative)
      {
      case 0:
	return \"fmdrr%?\\t%P0, %Q1, %R1\";
      case 1:
	return \"fmrrd%?\\t%Q0, %R0, %P1\";
      case 2: case 3: case 7:
	return output_move_double (operands);
      case 4:
	return \"fldd%?\\t%P0, %1\";
      case 5:
	return \"fstd%?\\t%P1, %0\";
      case 6:
	return \"fcpyd%?\\t%P0, %P1\";
      default:
	abort ();
      }
    }
  "
  [(set_attr "type" "r_2_f,f_2_r,ffarith,*,load2,store2,f_load,f_store")
   (set_attr "length" "4,4,8,8,4,4,4,8")
   (set_attr "pool_range" "*,*,1020,*,1020,*,*,*")
   (set_attr "neg_pool_range" "*,*,1008,*,1008,*,*,*")]
)


;; Conditional move patterns

(define_insn "*movsfcc_vfp"
  [(set (match_operand:SF   0 "s_register_operand" "=w,w,w,w,w,w,?r,?r,?r")
	(if_then_else:SF
	  (match_operator   3 "arm_comparison_operator"
	    [(match_operand 4 "cc_register" "") (const_int 0)])
	  (match_operand:SF 1 "s_register_operand" "0,w,w,0,?r,?r,0,w,w")
	  (match_operand:SF 2 "s_register_operand" "w,0,w,?r,0,?r,w,0,w")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "@
   fcpys%D3\\t%0, %2
   fcpys%d3\\t%0, %1
   fcpys%D3\\t%0, %2\;fcpys%d3\\t%0, %1
   fmsr%D3\\t%0, %2
   fmsr%d3\\t%0, %1
   fmsr%D3\\t%0, %2\;fmsr%d3\\t%0, %1
   fmrs%D3\\t%0, %2
   fmrs%d3\\t%0, %1
   fmrs%D3\\t%0, %2\;fmrs%d3\\t%0, %1"
   [(set_attr "conds" "use")
    (set_attr "length" "4,4,8,4,4,8,4,4,8")
    (set_attr "type" "ffarith,ffarith,ffarith,r_2_f,r_2_f,r_2_f,f_2_r,f_2_r,f_2_r")]
)

(define_insn "*movdfcc_vfp"
  [(set (match_operand:DF   0 "s_register_operand" "=w,w,w,w,w,w,?r,?r,?r")
	(if_then_else:DF
	  (match_operator   3 "arm_comparison_operator"
	    [(match_operand 4 "cc_register" "") (const_int 0)])
	  (match_operand:DF 1 "s_register_operand" "0,w,w,0,?r,?r,0,w,w")
	  (match_operand:DF 2 "s_register_operand" "w,0,w,?r,0,?r,w,0,w")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "@
   fcpyd%D3\\t%P0, %P2
   fcpyd%d3\\t%P0, %P1
   fcpyd%D3\\t%P0, %P2\;fcpyd%d3\\t%P0, %P1
   fmdrr%D3\\t%P0, %Q2, %R2
   fmdrr%d3\\t%P0, %Q1, %R1
   fmdrr%D3\\t%P0, %Q2, %R2\;fmdrr%d3\\t%P0, %Q1, %R1
   fmrrd%D3\\t%Q0, %R0, %P2
   fmrrd%d3\\t%Q0, %R0, %P1
   fmrrd%D3\\t%Q0, %R0, %P2\;fmrrd%d3\\t%Q0, %R0, %P1"
   [(set_attr "conds" "use")
    (set_attr "length" "4,4,8,4,4,8,4,4,8")
    (set_attr "type" "ffarith,ffarith,ffarith,r_2_f,r_2_f,r_2_f,f_2_r,f_2_r,f_2_r")]
)


;; Sign manipulation functions

(define_insn "*abssf2_vfp"
  [(set (match_operand:SF	  0 "s_register_operand" "=w")
	(abs:SF (match_operand:SF 1 "s_register_operand" "w")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "fabss%?\\t%0, %1"
  [(set_attr "predicable" "yes")
   (set_attr "type" "ffarith")]
)

(define_insn "*absdf2_vfp"
  [(set (match_operand:DF	  0 "s_register_operand" "=w")
	(abs:DF (match_operand:DF 1 "s_register_operand" "w")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "fabsd%?\\t%P0, %P1"
  [(set_attr "predicable" "yes")
   (set_attr "type" "ffarith")]
)

(define_insn "*negsf2_vfp"
  [(set (match_operand:SF	  0 "s_register_operand" "+w")
	(neg:SF (match_operand:SF 1 "s_register_operand" "w")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "fnegs%?\\t%0, %1"
  [(set_attr "predicable" "yes")
   (set_attr "type" "ffarith")]
)

(define_insn "*negdf2_vfp"
  [(set (match_operand:DF	  0 "s_register_operand" "+w")
	(neg:DF (match_operand:DF 1 "s_register_operand" "w")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "fnegd%?\\t%P0, %P1"
  [(set_attr "predicable" "yes")
   (set_attr "type" "ffarith")]
)


;; Arithmetic insns

(define_insn "*addsf3_vfp"
  [(set (match_operand:SF	   0 "s_register_operand" "=w")
	(plus:SF (match_operand:SF 1 "s_register_operand" "w")
		 (match_operand:SF 2 "s_register_operand" "w")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "fadds%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")
   (set_attr "type" "farith")]
)

(define_insn "*adddf3_vfp"
  [(set (match_operand:DF	   0 "s_register_operand" "=w")
	(plus:DF (match_operand:DF 1 "s_register_operand" "w")
		 (match_operand:DF 2 "s_register_operand" "w")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "faddd%?\\t%P0, %P1, %P2"
  [(set_attr "predicable" "yes")
   (set_attr "type" "farith")]
)


(define_insn "*subsf3_vfp"
  [(set (match_operand:SF	    0 "s_register_operand" "=w")
	(minus:SF (match_operand:SF 1 "s_register_operand" "w")
		  (match_operand:SF 2 "s_register_operand" "w")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "fsubs%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")
   (set_attr "type" "farith")]
)

(define_insn "*subdf3_vfp"
  [(set (match_operand:DF	    0 "s_register_operand" "=w")
	(minus:DF (match_operand:DF 1 "s_register_operand" "w")
		  (match_operand:DF 2 "s_register_operand" "w")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "fsubd%?\\t%P0, %P1, %P2"
  [(set_attr "predicable" "yes")
   (set_attr "type" "farith")]
)


;; Division insns

(define_insn "*divsf3_vfp"
  [(set (match_operand:SF	  0 "s_register_operand" "+w")
	(div:SF (match_operand:SF 1 "s_register_operand" "w")
		(match_operand:SF 2 "s_register_operand" "w")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "fdivs%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")
   (set_attr "type" "fdivs")]
)

(define_insn "*divdf3_vfp"
  [(set (match_operand:DF	  0 "s_register_operand" "+w")
	(div:DF (match_operand:DF 1 "s_register_operand" "w")
		(match_operand:DF 2 "s_register_operand" "w")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "fdivd%?\\t%P0, %P1, %P2"
  [(set_attr "predicable" "yes")
   (set_attr "type" "fdivd")]
)


;; Multiplication insns

(define_insn "*mulsf3_vfp"
  [(set (match_operand:SF	   0 "s_register_operand" "+w")
	(mult:SF (match_operand:SF 1 "s_register_operand" "w")
		 (match_operand:SF 2 "s_register_operand" "w")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "fmuls%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")
   (set_attr "type" "farith")]
)

(define_insn "*muldf3_vfp"
  [(set (match_operand:DF	   0 "s_register_operand" "+w")
	(mult:DF (match_operand:DF 1 "s_register_operand" "w")
		 (match_operand:DF 2 "s_register_operand" "w")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "fmuld%?\\t%P0, %P1, %P2"
  [(set_attr "predicable" "yes")
   (set_attr "type" "fmul")]
)


(define_insn "*mulsf3negsf_vfp"
  [(set (match_operand:SF		   0 "s_register_operand" "+w")
	(mult:SF (neg:SF (match_operand:SF 1 "s_register_operand" "w"))
		 (match_operand:SF	   2 "s_register_operand" "w")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "fnmuls%?\\t%0, %1, %2"
  [(set_attr "predicable" "yes")
   (set_attr "type" "farith")]
)

(define_insn "*muldf3negdf_vfp"
  [(set (match_operand:DF		   0 "s_register_operand" "+w")
	(mult:DF (neg:DF (match_operand:DF 1 "s_register_operand" "w"))
		 (match_operand:DF	   2 "s_register_operand" "w")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "fnmuld%?\\t%P0, %P1, %P2"
  [(set_attr "predicable" "yes")
   (set_attr "type" "fmul")]
)


;; Multiply-accumulate insns

;; 0 = 1 * 2 + 0
(define_insn "*mulsf3addsf_vfp"
  [(set (match_operand:SF		    0 "s_register_operand" "=w")
	(plus:SF (mult:SF (match_operand:SF 2 "s_register_operand" "w")
			  (match_operand:SF 3 "s_register_operand" "w"))
		 (match_operand:SF	    1 "s_register_operand" "0")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "fmacs%?\\t%0, %2, %3"
  [(set_attr "predicable" "yes")
   (set_attr "type" "farith")]
)

(define_insn "*muldf3adddf_vfp"
  [(set (match_operand:DF		    0 "s_register_operand" "=w")
	(plus:DF (mult:DF (match_operand:DF 2 "s_register_operand" "w")
			  (match_operand:DF 3 "s_register_operand" "w"))
		 (match_operand:DF	    1 "s_register_operand" "0")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "fmacd%?\\t%P0, %P2, %P3"
  [(set_attr "predicable" "yes")
   (set_attr "type" "fmul")]
)

;; 0 = 1 * 2 - 0
(define_insn "*mulsf3subsf_vfp"
  [(set (match_operand:SF		     0 "s_register_operand" "=w")
	(minus:SF (mult:SF (match_operand:SF 2 "s_register_operand" "w")
			   (match_operand:SF 3 "s_register_operand" "w"))
		  (match_operand:SF	     1 "s_register_operand" "0")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "fmscs%?\\t%0, %2, %3"
  [(set_attr "predicable" "yes")
   (set_attr "type" "farith")]
)

(define_insn "*muldf3subdf_vfp"
  [(set (match_operand:DF		     0 "s_register_operand" "=w")
	(minus:DF (mult:DF (match_operand:DF 2 "s_register_operand" "w")
			   (match_operand:DF 3 "s_register_operand" "w"))
		  (match_operand:DF	     1 "s_register_operand" "0")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "fmscd%?\\t%P0, %P2, %P3"
  [(set_attr "predicable" "yes")
   (set_attr "type" "fmul")]
)

;; 0 = -(1 * 2) + 0
(define_insn "*mulsf3negsfaddsf_vfp"
  [(set (match_operand:SF		     0 "s_register_operand" "=w")
	(minus:SF (match_operand:SF	     1 "s_register_operand" "0")
		  (mult:SF (match_operand:SF 2 "s_register_operand" "w")
			   (match_operand:SF 3 "s_register_operand" "w"))))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "fnmacs%?\\t%0, %2, %3"
  [(set_attr "predicable" "yes")
   (set_attr "type" "farith")]
)

(define_insn "*fmuldf3negdfadddf_vfp"
  [(set (match_operand:DF		     0 "s_register_operand" "=w")
	(minus:DF (match_operand:DF	     1 "s_register_operand" "0")
		  (mult:DF (match_operand:DF 2 "s_register_operand" "w")
			   (match_operand:DF 3 "s_register_operand" "w"))))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "fnmacd%?\\t%P0, %P2, %P3"
  [(set_attr "predicable" "yes")
   (set_attr "type" "fmul")]
)


;; 0 = -(1 * 2) - 0
(define_insn "*mulsf3negsfsubsf_vfp"
  [(set (match_operand:SF		      0 "s_register_operand" "=w")
	(minus:SF (mult:SF
		    (neg:SF (match_operand:SF 2 "s_register_operand" "w"))
		    (match_operand:SF	      3 "s_register_operand" "w"))
		  (match_operand:SF	      1 "s_register_operand" "0")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "fnmscs%?\\t%0, %2, %3"
  [(set_attr "predicable" "yes")
   (set_attr "type" "farith")]
)

(define_insn "*muldf3negdfsubdf_vfp"
  [(set (match_operand:DF		      0 "s_register_operand" "=w")
	(minus:DF (mult:DF
		    (neg:DF (match_operand:DF 2 "s_register_operand" "w"))
		    (match_operand:DF	      3 "s_register_operand" "w"))
		  (match_operand:DF	      1 "s_register_operand" "0")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "fnmscd%?\\t%P0, %P2, %P3"
  [(set_attr "predicable" "yes")
   (set_attr "type" "fmul")]
)


;; Conversion routines

(define_insn "*extendsfdf2_vfp"
  [(set (match_operand:DF		   0 "s_register_operand" "=w")
	(float_extend:DF (match_operand:SF 1 "s_register_operand" "w")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "fcvtds%?\\t%P0, %1"
  [(set_attr "predicable" "yes")
   (set_attr "type" "farith")]
)

(define_insn "*truncdfsf2_vfp"
  [(set (match_operand:SF		   0 "s_register_operand" "=w")
	(float_truncate:SF (match_operand:DF 1 "s_register_operand" "w")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "fcvtsd%?\\t%0, %P1"
  [(set_attr "predicable" "yes")
   (set_attr "type" "farith")]
)

(define_insn "*truncsisf2_vfp"
  [(set (match_operand:SI		  0 "s_register_operand" "=w")
	(fix:SI (fix:SF (match_operand:SF 1 "s_register_operand" "w"))))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "ftosizs%?\\t%0, %1"
  [(set_attr "predicable" "yes")
   (set_attr "type" "farith")]
)

(define_insn "*truncsidf2_vfp"
  [(set (match_operand:SI		  0 "s_register_operand" "=w")
	(fix:SI (fix:DF (match_operand:DF 1 "s_register_operand" "w"))))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "ftosizd%?\\t%0, %P1"
  [(set_attr "predicable" "yes")
   (set_attr "type" "farith")]
)


(define_insn "fixuns_truncsfsi2"
  [(set (match_operand:SI		  0 "s_register_operand" "=w")
	(unsigned_fix:SI (fix:SF (match_operand:SF 1 "s_register_operand" "w"))))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "ftouizs%?\\t%0, %1"
  [(set_attr "predicable" "yes")
   (set_attr "type" "farith")]
)

(define_insn "fixuns_truncdfsi2"
  [(set (match_operand:SI		  0 "s_register_operand" "=w")
	(unsigned_fix:SI (fix:DF (match_operand:DF 1 "s_register_operand" "w"))))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "ftouizd%?\\t%0, %P1"
  [(set_attr "predicable" "yes")
   (set_attr "type" "farith")]
)


(define_insn "*floatsisf2_vfp"
  [(set (match_operand:SF	    0 "s_register_operand" "=w")
	(float:SF (match_operand:SI 1 "s_register_operand" "w")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "fsitos%?\\t%0, %1"
  [(set_attr "predicable" "yes")
   (set_attr "type" "farith")]
)

(define_insn "*floatsidf2_vfp"
  [(set (match_operand:DF	    0 "s_register_operand" "=w")
	(float:DF (match_operand:SI 1 "s_register_operand" "w")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "fsitod%?\\t%P0, %1"
  [(set_attr "predicable" "yes")
   (set_attr "type" "farith")]
)


(define_insn "floatunssisf2"
  [(set (match_operand:SF	    0 "s_register_operand" "=w")
	(unsigned_float:SF (match_operand:SI 1 "s_register_operand" "w")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "fuitos%?\\t%0, %1"
  [(set_attr "predicable" "yes")
   (set_attr "type" "farith")]
)

(define_insn "floatunssidf2"
  [(set (match_operand:DF	    0 "s_register_operand" "=w")
	(unsigned_float:DF (match_operand:SI 1 "s_register_operand" "w")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "fuitod%?\\t%P0, %1"
  [(set_attr "predicable" "yes")
   (set_attr "type" "farith")]
)


;; Sqrt insns.

(define_insn "*sqrtsf2_vfp"
  [(set (match_operand:SF	   0 "s_register_operand" "=w")
	(sqrt:SF (match_operand:SF 1 "s_register_operand" "w")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "fsqrts%?\\t%0, %1"
  [(set_attr "predicable" "yes")
   (set_attr "type" "fdivs")]
)

(define_insn "*sqrtdf2_vfp"
  [(set (match_operand:DF	   0 "s_register_operand" "=w")
	(sqrt:DF (match_operand:DF 1 "s_register_operand" "w")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "fsqrtd%?\\t%P0, %P1"
  [(set_attr "predicable" "yes")
   (set_attr "type" "fdivd")]
)


;; Patterns to split/copy vfp condition flags.

(define_insn "*movcc_vfp"
  [(set (reg CC_REGNUM)
	(reg VFPCC_REGNUM))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "fmstat%?"
  [(set_attr "conds" "set")
   (set_attr "type" "ffarith")]
)

(define_insn_and_split "*cmpsf_split_vfp"
  [(set (reg:CCFP CC_REGNUM)
	(compare:CCFP (match_operand:SF 0 "s_register_operand"  "w")
		      (match_operand:SF 1 "vfp_compare_operand" "wG")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "#"
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  [(set (reg:CCFP VFPCC_REGNUM)
	(compare:CCFP (match_dup 0)
		      (match_dup 1)))
   (set (reg:CCFP CC_REGNUM)
	(reg:CCFP VFPCC_REGNUM))]
  ""
)

(define_insn_and_split "*cmpsf_trap_split_vfp"
  [(set (reg:CCFPE CC_REGNUM)
	(compare:CCFPE (match_operand:SF 0 "s_register_operand"  "w")
		       (match_operand:SF 1 "vfp_compare_operand" "wG")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "#"
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  [(set (reg:CCFPE VFPCC_REGNUM)
	(compare:CCFPE (match_dup 0)
		       (match_dup 1)))
   (set (reg:CCFPE CC_REGNUM)
	(reg:CCFPE VFPCC_REGNUM))]
  ""
)

(define_insn_and_split "*cmpdf_split_vfp"
  [(set (reg:CCFP CC_REGNUM)
	(compare:CCFP (match_operand:DF 0 "s_register_operand"  "w")
		      (match_operand:DF 1 "vfp_compare_operand" "wG")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "#"
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  [(set (reg:CCFP VFPCC_REGNUM)
	(compare:CCFP (match_dup 0)
		       (match_dup 1)))
   (set (reg:CCFP CC_REGNUM)
	(reg:CCFPE VFPCC_REGNUM))]
  ""
)

(define_insn_and_split "*cmpdf_trap_split_vfp"
  [(set (reg:CCFPE CC_REGNUM)
	(compare:CCFPE (match_operand:DF 0 "s_register_operand"  "w")
		       (match_operand:DF 1 "vfp_compare_operand" "wG")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "#"
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  [(set (reg:CCFPE VFPCC_REGNUM)
	(compare:CCFPE (match_dup 0)
		       (match_dup 1)))
   (set (reg:CCFPE CC_REGNUM)
	(reg:CCFPE VFPCC_REGNUM))]
  ""
)


;; Comparison patterns

(define_insn "*cmpsf_vfp"
  [(set (reg:CCFP VFPCC_REGNUM)
	(compare:CCFP (match_operand:SF 0 "s_register_operand"  "w,w")
		      (match_operand:SF 1 "vfp_compare_operand" "w,G")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "@
   fcmps%?\\t%0, %1
   fcmpzs%?\\t%0"
  [(set_attr "predicable" "yes")
   (set_attr "type" "ffarith")]
)

(define_insn "*cmpsf_trap_vfp"
  [(set (reg:CCFPE VFPCC_REGNUM)
	(compare:CCFPE (match_operand:SF 0 "s_register_operand"  "w,w")
		       (match_operand:SF 1 "vfp_compare_operand" "w,G")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "@
   fcmpes%?\\t%0, %1
   fcmpezs%?\\t%0"
  [(set_attr "predicable" "yes")
   (set_attr "type" "ffarith")]
)

(define_insn "*cmpdf_vfp"
  [(set (reg:CCFP VFPCC_REGNUM)
	(compare:CCFP (match_operand:DF 0 "s_register_operand"  "w,w")
		      (match_operand:DF 1 "vfp_compare_operand" "w,G")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "@
   fcmpd%?\\t%P0, %P1
   fcmpzd%?\\t%P0"
  [(set_attr "predicable" "yes")
   (set_attr "type" "ffarith")]
)

(define_insn "*cmpdf_trap_vfp"
  [(set (reg:CCFPE VFPCC_REGNUM)
	(compare:CCFPE (match_operand:DF 0 "s_register_operand"  "w,w")
		       (match_operand:DF 1 "vfp_compare_operand" "w,G")))]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "@
   fcmped%?\\t%P0, %P1
   fcmpezd%?\\t%P0"
  [(set_attr "predicable" "yes")
   (set_attr "type" "ffarith")]
)


;; Store multiple insn used in function prologue.

(define_insn "*push_multi_vfp"
  [(match_parallel 2 "multi_register_push"
    [(set (match_operand:BLK 0 "memory_operand" "=m")
	  (unspec:BLK [(match_operand:DF 1 "s_register_operand" "w")]
		      UNSPEC_PUSH_MULT))])]
  "TARGET_ARM && TARGET_HARD_FLOAT && TARGET_VFP"
  "* return vfp_output_fstmx (operands);"
  [(set_attr "type" "f_store")]
)


;; Unimplemented insns:
;; fldm*
;; fstm*
;; fmdhr et al (VFPv1)
;; Support for xD (single precision only) variants.
;; fmrrs, fmsrr