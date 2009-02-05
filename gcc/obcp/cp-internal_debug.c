/* gcc/obcp/cp-internal_debug.c
   (to be used in conjunction with gcc/internal_debug.c)
   
  TREE and RTL debugging macro functions.  What we do
  here is to instantiate each macro as a function *BY
  THE SAME NAME*.  Depends on the macro not being
  expanded when it is surrounded by parens.

  Note that this one includes the C++ stuff; it might make
  sense to separate that from the C-only stuff.  But I no
  longer think it makes sense to separate the RTL from the
  TREE stuff, nor to put those in print-rtl.c, print-tree.c,
  and cp/ptree.c.   */

#include "config.h"
#include "system.h"
#include "tree.h"
#include "rtl.h" 
#include "cp-tree.h"   

/* get Objective-C stuff also */
#include "obcp/objc-act.h"

#define fn_1(name,rt,pt)       rt (name) (pt a)           { return name(a); }
#define fn_2(name,rt,p1,p2)    rt (name) (p1 a,p2 b)      { return name(a,b); }
#define fn_3(name,rt,p1,p2,p3) rt (name) (p1 a,p2 b,p3 c) { return name(a,b,c); }

/* MACROS from tree.h (single-parameter ones) */

/* C++  TREE debugging macro functions.  From cp-tree.h.
   I made special-case meta-macros for the most common
   one-parameter ones, that take a node and return either
   a node or an int.  */

#define fn_noden( m ) fn_1(m, tree, tree)
#define fn_nodei( m ) fn_1(m, int, tree)

// Some of them need to be pre-declared...
#ifdef MI_MATRIX
    fn_nodei(BINFO_CID)
    fn_nodei(CLASSTYPE_CID)
    fn_1(CLASSTYPE_MI_MATRIX, char*, tree)
#endif

// And here are the real ones.
fn_nodei(ANON_AGGRNAME_P)
fn_nodei(ANON_PARMNAME_P)
fn_nodei(ANON_UNION_P)
fn_nodei(ANON_UNION_TYPE_P)
fn_nodei(ARITHMETIC_TYPE_P)
fn_noden(ASM_CLOBBERS)
fn_noden(ASM_CV_QUAL)
fn_noden(ASM_INPUTS)
fn_noden(ASM_OUTPUTS)
fn_noden(ASM_STRING)
fn_noden(BINDING_SCOPE)
fn_noden(BINDING_TYPE)
fn_noden(BINDING_VALUE)
fn_nodei(BINFO_BASEINIT_MARKED)
fn_nodei(BINFO_FIELDS_MARKED)
fn_nodei(BINFO_MARKED)
fn_nodei(BINFO_NEW_VTABLE_MARKED)
fn_nodei(BINFO_VBASE_INIT_MARKED)
fn_nodei(BINFO_VBASE_MARKED)
fn_nodei(BINFO_VIA_PUBLIC)
fn_nodei(BINFO_VTABLE_PATH_MARKED)
fn_nodei(C_DECLARED_LABEL_FLAG)
fn_nodei(C_EXP_ORIGINAL_CODE)
fn_nodei(C_PROMOTING_INTEGER_TYPE_P)
fn_nodei(C_TYPE_FIELDS_READONLY)
fn_nodei(C_TYPEDEF_EXPLICITLY_SIGNED)
fn_noden(CASE_HIGH)
fn_noden(CASE_LOW)
fn_noden(CLASSTYPE_ABSTRACT_VIRTUALS)
fn_nodei(CLASSTYPE_ALIGN)
fn_noden(CLASSTYPE_AS_LIST)
fn_noden(CLASSTYPE_BASE_INIT_LIST)
fn_noden(CLASSTYPE_BASELINK_VEC)
fn_noden(CLASSTYPE_BINFO_AS_LIST)
fn_nodei(CLASSTYPE_DEBUG_REQUESTED)
fn_nodei(CLASSTYPE_DECLARED_CLASS)
fn_nodei(CLASSTYPE_EXPLICIT_INSTANTIATION)
fn_noden(CLASSTYPE_FIRST_CONVERSION)
fn_noden(CLASSTYPE_FRIEND_CLASSES)
fn_nodei(CLASSTYPE_GOT_SEMICOLON)
fn_noden(CLASSTYPE_ID_AS_LIST)
fn_nodei(CLASSTYPE_IMPLICIT_INSTANTIATION)
fn_noden(CLASSTYPE_INLINE_FRIENDS)
fn_nodei(CLASSTYPE_INTERFACE_KNOWN)
fn_nodei(CLASSTYPE_INTERFACE_ONLY)
fn_nodei(CLASSTYPE_INTERFACE_UNKNOWN)
fn_nodei(CLASSTYPE_IS_TEMPLATE)
fn_nodei(CLASSTYPE_LOCAL_TYPEDECLS)
fn_nodei(CLASSTYPE_MARKED)
fn_nodei(CLASSTYPE_MARKED2)
fn_nodei(CLASSTYPE_MARKED3)
fn_nodei(CLASSTYPE_MARKED4)
fn_nodei(CLASSTYPE_MARKED5)
fn_nodei(CLASSTYPE_MARKED6)
fn_nodei(CLASSTYPE_MAX_DEPTH)
fn_noden(CLASSTYPE_METHOD_VEC)
fn_1(CLASSTYPE_MTABLE_ENTRY, char*, tree)
fn_nodei(CLASSTYPE_N_BASECLASSES)
fn_nodei(CLASSTYPE_N_SUPERCLASSES)
fn_nodei(CLASSTYPE_N_VBASECLASSES)
fn_nodei(CLASSTYPE_NEEDS_VIRTUAL_REINIT)
fn_nodei(CLASSTYPE_NON_AGGREGATE)
fn_nodei(CLASSTYPE_READONLY_FIELDS_NEED_INIT)
fn_nodei(CLASSTYPE_REF_FIELDS_NEED_INIT)
fn_noden(CLASSTYPE_RTTI)
fn_noden(CLASSTYPE_SEARCH_SLOT)
fn_noden(CLASSTYPE_SIGNATURE)
fn_noden(CLASSTYPE_SIZE)
fn_nodei(CLASSTYPE_SOURCE_LINE)
fn_noden(CLASSTYPE_TAGS)
fn_noden(CLASSTYPE_TEMPLATE_INFO)
fn_nodei(CLASSTYPE_TEMPLATE_INSTANTIATION)
fn_nodei(CLASSTYPE_TEMPLATE_LEVEL)
fn_nodei(CLASSTYPE_TEMPLATE_SPECIALIZATION)
fn_noden(CLASSTYPE_TI_ARGS)
fn_noden(CLASSTYPE_TI_SPEC_INFO)
fn_noden(CLASSTYPE_TI_TEMPLATE)
fn_nodei(CLASSTYPE_USE_TEMPLATE)
fn_noden(CLASSTYPE_VBASECLASSES)
fn_noden(CLASSTYPE_VFIELD)
fn_nodei(CLASSTYPE_VFIELD_PARENT)
fn_noden(CLASSTYPE_VFIELDS)
fn_nodei(CLASSTYPE_VSIZE)
fn_nodei(CLASSTYPE_VTABLE_NEEDS_WRITING)
fn_noden(COMPOUND_BODY)
fn_nodei(COMPOUND_STMT_NO_SCOPE)
fn_noden(CP_DECL_CONTEXT)
fn_nodei(CP_TYPE_QUALS)
fn_nodei(CP_TYPE_CONST_P)
fn_nodei(CP_TYPE_VOLATILE_P)
fn_nodei(CP_TYPE_RESTRICT_P)
fn_nodei(DECL_ABSTRACT_VIRTUAL_P)
fn_noden(DECL_ACCESS)
fn_noden(DECL_CLASS_CONTEXT)
fn_nodei(DECL_CLASS_SCOPE_P)
fn_nodei(DECL_CLASS_TEMPLATE_P)
fn_nodei(DECL_COMDAT)
fn_nodei(DECL_CONST_MEMFUNC_P)
fn_nodei(DECL_CONSTRUCTOR_FOR_VBASE_P)
fn_nodei(DECL_CONSTRUCTOR_P)
fn_nodei(DECL_DEAD_FOR_LOCAL)
fn_nodei(DECL_DECLARES_TYPE_P)
fn_nodei(DECL_DESTRUCTOR_P)
fn_nodei(DECL_ERROR_REPORTED)
fn_nodei(DECL_EXPLICIT_INSTANTIATION)
fn_nodei(DECL_FRIEND_P)
fn_noden(DECL_FRIENDLIST)
fn_nodei(DECL_FUNCTION_MEMBER_P)
fn_nodei(DECL_FUNCTION_TEMPLATE_P)
fn_nodei(DECL_IMPLICIT_INSTANTIATION)
fn_nodei(DECL_IN_AGGR_P)
fn_nodei(DECL_IN_MEMORY_P)
fn_noden(DECL_INNERMOST_TEMPLATE_PARMS)
fn_nodei(DECL_INTERFACE_KNOWN)
fn_nodei(DECL_LANGUAGE)
fn_nodei(DECL_MAIN_P)
fn_noden(DECL_MAIN_VARIANT)
fn_nodei(DECL_MAYBE_TEMPLATE)
fn_noden(DECL_MEMFUNC_POINTER_TO)
fn_noden(DECL_MEMFUNC_POINTING_TO)
fn_nodei(DECL_MUTABLE_P)
fn_noden(DECL_NAMESPACE_ALIAS)
fn_nodei(DECL_NAMESPACE_SCOPE_P)
fn_noden(DECL_NAMESPACE_USERS)
fn_noden(DECL_NAMESPACE_USING)
fn_nodei(DECL_NEEDS_FINAL_OVERRIDER_P)
fn_nodei(DECL_NONCONVERTING_P)
fn_nodei(DECL_NONSTATIC_MEMBER_FUNCTION_P)
fn_nodei(DECL_NOT_REALLY_EXTERN)
fn_nodei(DECL_NTPARMS)
fn_nodei(DECL_OPERATOR)
fn_1(DECL_PENDING_INLINE_INFO, struct pending_inline *, tree )
fn_nodei(DECL_PRESERVES_THIS)
fn_noden(DECL_PRIMARY_TEMPLATE)
fn_noden(DECL_REAL_CONTEXT)
fn_nodei(DECL_REALLY_EXTERN)
fn_noden(DECL_REFERENCE_SLOT)
fn_nodei(DECL_RETURNS_FIRST_ARG)
fn_nodei(DECL_SAVED_INLINE)
fn_noden(DECL_SAVED_TREE)
fn_noden(DECL_SHADOWED_FOR_VAR)
fn_nodei(DECL_STATIC_FUNCTION_P)
fn_noden(DECL_TEMPLATE_INFO)
fn_noden(DECL_TEMPLATE_INJECT)
fn_nodei(DECL_TEMPLATE_INSTANTIATION)
fn_noden(DECL_TEMPLATE_INSTANTIATIONS)
fn_noden(DECL_TEMPLATE_PARMS)
fn_noden(DECL_TEMPLATE_RESULT)
fn_nodei(DECL_TEMPLATE_SPECIALIZATION)
fn_noden(DECL_TEMPLATE_SPECIALIZATIONS)
fn_nodei(DECL_TEMPLATE_TEMPLATE_PARM_P)
fn_nodei(DECL_THIS_EXTERN)
fn_nodei(DECL_THIS_INLINE)
fn_nodei(DECL_THIS_STATIC)
fn_noden(DECL_TI_ARGS)
fn_noden(DECL_TI_TEMPLATE)
fn_nodei(DECL_USE_TEMPLATE)
fn_nodei(DECL_VOLATILE_MEMFUNC_P)
fn_nodei(DEFARG_LENGTH)
fn_noden(DEFARG_NODE_CHECK)
fn_1(DEFARG_POINTER, char*, tree)
fn_nodei(DELETE_EXPR_USE_GLOBAL)
fn_nodei(DELETE_EXPR_USE_VEC)
fn_noden(DELTA2_FROM_PTRMEMFUNC)
fn_noden(DELTA_FROM_VTABLE_ENTRY)
fn_nodei(DESTRUCTOR_NAME_P)
fn_noden(DO_BODY)
fn_noden(DO_COND)
fn_noden(ELSE_CLAUSE)
fn_nodei(EMPTY_CONSTRUCTOR_P)
fn_noden(EXPR_STMT_EXPR)
fn_noden(FNADDR_FROM_VTABLE_ENTRY)
fn_noden(FOR_BODY)
fn_noden(FOR_COND)
fn_noden(FOR_EXPR)
fn_noden(FOR_INIT_STMT)
fn_noden(FRIEND_DECLS)
fn_noden(FRIEND_NAME)
fn_noden(FROB_CONTEXT)
fn_noden(FUNCTION_ARG_CHAIN)
fn_noden(GOTO_DESTINATION)
fn_noden(HANDLER_BODY)
fn_noden(HANDLER_PARMS)
fn_noden(IDENTIFIER_AS_DESC)
fn_noden(IDENTIFIER_AS_LIST)
fn_noden(IDENTIFIER_BINDING)
fn_noden(IDENTIFIER_CLASS_VALUE)
fn_noden(IDENTIFIER_ERROR_LOCUS)
fn_noden(IDENTIFIER_GLOBAL_VALUE)
fn_nodei(IDENTIFIER_HAS_TYPE_VALUE)
fn_noden(IDENTIFIER_IMPLICIT_DECL)
fn_noden(IDENTIFIER_LABEL_VALUE)
fn_noden(IDENTIFIER_NAMESPACE_BINDINGS)
fn_noden(IDENTIFIER_NAMESPACE_VALUE)
fn_nodei(IDENTIFIER_OPNAME_P)
fn_noden(IDENTIFIER_TEMPLATE)
fn_noden(IDENTIFIER_TYPE_VALUE)
fn_nodei(IDENTIFIER_TYPENAME_P)
fn_nodei(IDENTIFIER_VIRTUAL_P)
fn_noden(IF_COND)
fn_noden(INNERMOST_TEMPLATE_PARMS)
fn_1(INTEGRAL_CODE_P,int,int)
fn_nodei(IS_AGGR_TYPE)
fn_1(IS_AGGR_TYPE_CODE,int,int)
fn_nodei(IS_DEFAULT_IMPLEMENTATION)
fn_nodei(IS_OVERLOAD_TYPE)
fn_nodei(IS_SIGNATURE)
fn_nodei(IS_SIGNATURE_POINTER)
fn_nodei(IS_SIGNATURE_REFERENCE)
fn_1(LANG_DECL_PERMANENT, int, struct lang_decl*)
fn_nodei(LOOKUP_EXPR_GLOBAL)
fn_nodei(MAIN_NAME_P)
fn_1(NAMESPACE_LEVEL, struct binding_level*, tree)
fn_nodei(NEW_EXPR_USE_GLOBAL)
fn_noden(ORIGINAL_NAMESPACE)
fn_noden(OVL_CHAIN)
fn_noden(OVL_CURRENT)
fn_noden(OVL_FUNCTION)
fn_noden(OVL_NEXT)
fn_nodei(OVL_USED)
fn_nodei(PARM_DECL_EXPR)
fn_noden(PFN_FROM_PTRMEMFUNC)
fn_nodei(PRIMARY_TEMPLATE_P)
fn_noden(REAL_IDENTIFIER_TYPE_VALUE)
fn_nodei(SHARED_MEMBER_P)
fn_nodei(SIGNATURE_HAS_OPAQUE_TYPEDECLS)
fn_noden(SIGNATURE_METHOD_VEC)
fn_noden(SIGNATURE_POINTER_TO)
fn_noden(SIGNATURE_REFERENCE_TO)
fn_noden(SIGNATURE_TYPE)
fn_nodei(SIGTABLE_HAS_BEEN_GENERATED)
fn_1(SRCLOC_FILE, char*, tree)
fn_nodei(SRCLOC_LINE)
fn_noden(SWITCH_BODY)
fn_noden(SWITCH_COND)
fn_nodei(TEMP_NAME_P)
fn_noden(TEMPLATE_PARM_DECL)
fn_noden(TEMPLATE_PARM_DESCENDANTS)
fn_nodei(TEMPLATE_PARM_IDX)
fn_nodei(TEMPLATE_PARM_LEVEL)
fn_nodei(TEMPLATE_PARM_ORIG_LEVEL)
fn_nodei(TEMPLATE_PARMS_FOR_INLINE)
fn_noden(TEMPLATE_TYPE_DECL)
fn_nodei(TEMPLATE_TYPE_IDX)
fn_nodei(TEMPLATE_TYPE_LEVEL)
fn_nodei(TEMPLATE_TYPE_ORIG_LEVEL)
fn_noden(TEMPLATE_TYPE_PARM_INDEX)
fn_noden(THEN_CLAUSE)
fn_nodei(THIS_NAME_P)
fn_nodei(THUNK_DELTA)
fn_noden(TI_ARGS)
fn_nodei(TI_PENDING_SPECIALIZATION_FLAG)
fn_nodei(TI_PENDING_TEMPLATE_FLAG)
fn_noden(TI_SPEC_INFO)
fn_noden(TI_TEMPLATE)
fn_nodei(TI_USES_TEMPLATE_PARMS)
fn_nodei(TREE_HAS_CONSTRUCTOR)
fn_nodei(TREE_INDIRECT_USING)
fn_nodei(TREE_NEGATED_INT)
fn_nodei(TREE_NONLOCAL_FLAG)
fn_nodei(TREE_PARMLIST)
fn_nodei(TREE_READONLY_DECL_P)
fn_noden(TRY_HANDLERS)
fn_noden(TRY_STMTS)
fn_nodei(TYPE_ASSEMBLER_NAME_LENGTH)
fn_1(TYPE_ASSEMBLER_NAME_STRING, char*, tree)
fn_nodei(TYPE_BEING_DEFINED)
fn_nodei(TYPE_BUILT_IN)
fn_nodei(TYPE_FOR_JAVA)
fn_noden(TYPE_GET_PTRMEMFUNC_TYPE)
fn_nodei(TYPE_GETS_DELETE)
fn_nodei(TYPE_GETS_NEW)
fn_nodei(TYPE_GETS_REG_DELETE)
fn_nodei(TYPE_HAS_ABSTRACT_ASSIGN_REF)
fn_nodei(TYPE_HAS_ASSIGN_REF)
fn_nodei(TYPE_HAS_ASSIGNMENT)
fn_nodei(TYPE_HAS_COMPLEX_ASSIGN_REF)
fn_nodei(TYPE_HAS_COMPLEX_INIT_REF)
fn_nodei(TYPE_HAS_CONST_ASSIGN_REF)
fn_nodei(TYPE_HAS_CONST_INIT_REF)
fn_nodei(TYPE_HAS_CONSTRUCTOR)
fn_nodei(TYPE_HAS_CONVERSION)
fn_nodei(TYPE_HAS_DEFAULT_CONSTRUCTOR)
fn_nodei(TYPE_HAS_DESTRUCTOR)
fn_nodei(TYPE_HAS_INIT_REF)
fn_nodei(TYPE_HAS_NONPUBLIC_ASSIGN_REF)
fn_nodei(TYPE_HAS_NONPUBLIC_CTOR)
fn_nodei(TYPE_HAS_REAL_ASSIGN_REF)
fn_nodei(TYPE_HAS_REAL_ASSIGNMENT)
fn_nodei(TYPE_HAS_TRIVIAL_ASSIGN_REF)
fn_nodei(TYPE_HAS_TRIVIAL_INIT_REF)
fn_noden(TYPE_IDENTIFIER)
fn_noden(TYPE_MAIN_DECL)
fn_nodei(TYPE_NAME_LENGTH)
fn_1(TYPE_NAME_STRING, char*, tree)
fn_nodei(TYPE_NEEDS_DESTRUCTOR)
fn_nodei(TYPE_NON_AGGREGATE_CLASS)
fn_nodei(TYPE_OVERLOADS_ARRAY_REF)
fn_nodei(TYPE_OVERLOADS_ARROW)
fn_nodei(TYPE_OVERLOADS_CALL_EXPR)
fn_nodei(TYPE_PTR_P)
fn_nodei(TYPE_PTRFN_P)
fn_nodei(TYPE_PTRMEM_P)
fn_nodei(TYPE_PTRMEMFUNC_FLAG)
fn_noden(TYPE_PTRMEMFUNC_FN_TYPE)
fn_noden(TYPE_PTRMEMFUNC_OBJECT_TYPE)
fn_nodei(TYPE_PTRMEMFUNC_P)
fn_nodei(TYPE_PTROB_P)
fn_nodei(TYPE_PTROBV_P)
fn_noden(TYPE_RAISES_EXCEPTIONS)
fn_nodei(TYPE_REDEFINED)
fn_nodei(TYPE_USES_COMPLEX_INHERITANCE)
fn_nodei(TYPE_USES_MULTIPLE_INHERITANCE)
fn_nodei(TYPE_USES_VIRTUAL_BASECLASSES)
fn_nodei(TYPE_VEC_DELETE_TAKES_SIZE)
fn_nodei(TYPE_VEC_NEW_USES_COOKIE)
fn_nodei(TYPE_VIRTUAL_P)
fn_nodei(TYPE_WAS_ANONYMOUS)
fn_noden(TYPENAME_TYPE_FULLNAME)
fn_noden(UPT_PARMS)
fn_noden(UPT_TEMPLATE)
fn_nodei(VBASE_NAME_P)
fn_noden(VF_BASETYPE_VALUE)
fn_noden(VF_BINFO_VALUE)
fn_noden(VF_DERIVED_VALUE)
fn_noden(VF_NORMAL_VALUE)
fn_nodei(VFIELD_NAME_P)
fn_nodei(VPTR_NAME_P)
fn_nodei(VTABLE_NAME_P)
fn_noden(WHILE_BODY)
fn_noden(WHILE_COND)
fn_nodei(WRAPPER_INT)
fn_noden(WRAPPER_PTR)

fn_2( ACCESSIBLY_DERIVED_FROM_P, int, tree, tree )
fn_2( ACCESSIBLY_UNIQUELY_DERIVED_P, int, tree, tree )
fn_2( DERIVED_FROM_P, int, tree, tree )
fn_2( IS_AGGR_TYPE_2, int, tree, tree )
fn_2( PROMOTES_TO_AGGR_TYPE, int, tree, int )
fn_2( UNIQUELY_DERIVED_FROM_P, int, tree, tree )

/* Objective-C specific stuff */

fn_noden(KEYWORD_KEY_NAME)
fn_noden(KEYWORD_ARG_NAME)
fn_noden(METHOD_SEL_NAME)
fn_noden(METHOD_SEL_ARGS)
fn_noden(METHOD_ADD_ARGS)
fn_noden(METHOD_DEFINITION)
fn_noden(METHOD_ENCODING)
fn_noden(CLASS_NAME)
fn_noden(CLASS_SUPER_NAME)   
fn_noden(CLASS_IVARS)
fn_noden(CLASS_RAW_IVARS)
fn_noden(CLASS_NST_METHODS)
fn_noden(CLASS_CLS_METHODS)
fn_noden(CLASS_STATIC_TEMPLATE)
fn_noden(CLASS_CATEGORY_LIST)
fn_noden(CLASS_PROTOCOL_LIST)
fn_noden(PROTOCOL_NAME)
fn_noden(PROTOCOL_LIST)
fn_noden(PROTOCOL_NST_METHODS)
fn_noden(PROTOCOL_CLS_METHODS)
fn_noden(PROTOCOL_FORWARD_DECL)
fn_nodei(PROTOCOL_DEFINED)
fn_noden(TYPE_PROTOCOL_LIST)
fn_nodei(TREE_STATIC_TEMPLATE)
fn_nodei(IS_ID)
fn_nodei(IS_PROTOCOL_QUALIFIED_ID)
fn_nodei(IS_SUPER)

/* End of obcp/cp-internal_debug.c */