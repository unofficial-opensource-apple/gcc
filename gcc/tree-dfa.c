/* Data flow functions for trees.
   Copyright (C) 2001, 2002, 2003, 2004 Free Software Foundation, Inc.
   Contributed by Diego Novillo <dnovillo@redhat.com>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "hashtab.h"
#include "tree.h"
#include "rtl.h"
#include "tm_p.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "output.h"
#include "errors.h"
#include "timevar.h"
#include "expr.h"
#include "ggc.h"
#include "langhooks.h"
#include "flags.h"
#include "function.h"
#include "diagnostic.h"
#include "tree-dump.h"
#include "tree-gimple.h"
#include "tree-flow.h"
#include "tree-inline.h"
#include "tree-alias-common.h"
#include "tree-pass.h"
#include "convert.h"
#include "params.h"

/* Build and maintain data flow information for trees.  */

/* Counters used to display DFA and SSA statistics.  */
struct dfa_stats_d
{
  long num_stmt_anns;
  long num_var_anns;
  long num_defs;
  long num_uses;
  long num_phis;
  long num_phi_args;
  int max_num_phi_args;
  long num_vdefs;
  long num_vuses;
};


/* State information for find_vars_r.  */
struct walk_state
{
  /* Hash table used to avoid adding the same variable more than once.  */
  htab_t vars_found;
};


/* Local functions.  */
static void collect_dfa_stats (struct dfa_stats_d *);
static tree collect_dfa_stats_r (tree *, int *, void *);
static void add_immediate_use (tree, tree);
static tree find_vars_r (tree *, int *, void *);
static void add_referenced_var (tree, struct walk_state *);
static void compute_immediate_uses_for_phi (tree, bool (*)(tree));
static void compute_immediate_uses_for_stmt (tree, int, bool (*)(tree));
static void find_hidden_use_vars (tree);
static tree find_hidden_use_vars_r (tree *, int *, void *);


/* Global declarations.  */

/* Array of all variables referenced in the function.  */
varray_type referenced_vars;


/*---------------------------------------------------------------------------
			Dataflow analysis (DFA) routines
---------------------------------------------------------------------------*/
/* Find all the variables referenced in the function.  This function
   builds the global arrays REFERENCED_VARS and CALL_CLOBBERED_VARS.

   Note that this function does not look for statement operands, it simply
   determines what variables are referenced in the program and detects
   various attributes for each variable used by alias analysis and the
   optimizer.  */

static void
find_referenced_vars (void)
{
  htab_t vars_found;
  basic_block bb;
  block_stmt_iterator si;
  struct walk_state walk_state;
  tree block;

  /* This is the very first pass in preparation for building the SSA
     form of the function, so initialize internal data structures now.  */
  init_tree_ssa ();

  /* Walk the lexical blocks in the function looking for variables that may
     have been used to declare VLAs and for nested functions.  Both
     constructs create hidden uses of variables. 

     Note that at this point we may have multiple blocks hung off
     DECL_INITIAL chained through the BLOCK_CHAIN field due to
     how inlining works.  Egad.  */
  block = DECL_INITIAL (current_function_decl);
  while (block)
    {
      find_hidden_use_vars (block);
      block = BLOCK_CHAIN (block);
    }

  vars_found = htab_create (50, htab_hash_pointer, htab_eq_pointer, NULL);
  memset (&walk_state, 0, sizeof (walk_state));
  walk_state.vars_found = vars_found;

  FOR_EACH_BB (bb)
    for (si = bsi_start (bb); !bsi_end_p (si); bsi_next (&si))
      {
	tree *stmt_p = bsi_stmt_ptr (si);
	walk_tree (stmt_p, find_vars_r, &walk_state, NULL);
      }

  htab_delete (vars_found);
}

struct tree_opt_pass pass_referenced_vars =
{
  NULL,					/* name */
  NULL,					/* gate */
  find_referenced_vars,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  0,					/* tv_id */
  PROP_gimple_leh | PROP_cfg,		/* properties_required */
  PROP_referenced_vars,			/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  0,					/* todo_flags_finish */
};


/* Compute immediate uses.  
   
   CALC_FOR is an optional function pointer which indicates whether
      immediate uses information should be calculated for a given SSA
      variable.  If NULL, then information is computed for all
      variables.

   FLAGS is one of {TDFA_USE_OPS, TDFA_USE_VOPS}.  It is used by
      compute_immediate_uses_for_stmt to determine whether to look at
      virtual and/or real operands while computing def-use chains.  */

void
compute_immediate_uses (int flags, bool (*calc_for)(tree))
{
  basic_block bb;
  block_stmt_iterator si;

  FOR_EACH_BB (bb)
    {
      tree phi;

      for (phi = phi_nodes (bb); phi; phi = TREE_CHAIN (phi))
	compute_immediate_uses_for_phi (phi, calc_for);

      for (si = bsi_start (bb); !bsi_end_p (si); bsi_next (&si))
        {
	  tree stmt = bsi_stmt (si);
	  get_stmt_operands (stmt);
	  compute_immediate_uses_for_stmt (stmt, flags, calc_for);
	}
    }
}


/* Invalidates dataflow information for a statement STMT.  */

static void
free_df_for_stmt (tree stmt)
{
  stmt_ann_t ann = stmt_ann (stmt);

  if (ann && ann->df)
    {
      /* If we have a varray of immediate uses, then go ahead and release
	 it for re-use.  */
      if (ann->df->immediate_uses)
	ggc_free (ann->df->immediate_uses);

      /* Similarly for the main dataflow structure.  */
      ggc_free (ann->df);
      ann->df = NULL;
    }
}


/* Invalidate dataflow information for the whole function.  */

void
free_df (void)
{
  basic_block bb;
  block_stmt_iterator si;

  FOR_EACH_BB (bb)
    {
      tree phi;

      for (phi = phi_nodes (bb); phi; phi = TREE_CHAIN (phi))
	free_df_for_stmt (phi);

      for (si = bsi_start (bb); !bsi_end_p (si); bsi_next (&si))
        {
	  tree stmt = bsi_stmt (si);
	  free_df_for_stmt (stmt);
	}
    }
}


/* Helper for compute_immediate_uses.  Check all the USE and/or VUSE
   operands in phi node PHI and add a def-use edge between their
   defining statement and PHI.  CALC_FOR is as in
   compute_immediate_uses.

   PHI nodes are easy, we only need to look at their arguments.  */

static void
compute_immediate_uses_for_phi (tree phi, bool (*calc_for)(tree))
{
  int i;

#ifdef ENABLE_CHECKING
  if (TREE_CODE (phi) != PHI_NODE)
    abort ();
#endif

  for (i = 0; i < PHI_NUM_ARGS (phi); i++)
    {
      tree arg = PHI_ARG_DEF (phi, i);

      if (TREE_CODE (arg) == SSA_NAME && (!calc_for || calc_for (arg)))
	{ 
	  tree imm_rdef_stmt = SSA_NAME_DEF_STMT (PHI_ARG_DEF (phi, i));
	  if (!IS_EMPTY_STMT (imm_rdef_stmt))
	    add_immediate_use (imm_rdef_stmt, phi);
	}
    }
}


/* Another helper for compute_immediate_uses.  Depending on the value
   of FLAGS, check all the USE and/or VUSE operands in STMT and add a
   def-use edge between their defining statement and STMT.  CALC_FOR
   is as in compute_immediate_uses.  */

static void
compute_immediate_uses_for_stmt (tree stmt, int flags, bool (*calc_for)(tree))
{
  size_t i;
  use_optype uses;
  vuse_optype vuses;
  vdef_optype vdefs;
  stmt_ann_t ann;

#ifdef ENABLE_CHECKING
  /* PHI nodes are handled elsewhere.  */
  if (TREE_CODE (stmt) == PHI_NODE)
    abort ();
#endif

  /* Look at USE_OPS or VUSE_OPS according to FLAGS.  */
  ann = stmt_ann (stmt);
  if (flags & TDFA_USE_OPS)
    {
      uses = USE_OPS (ann);
      for (i = 0; i < NUM_USES (uses); i++)
	{
	  tree use = USE_OP (uses, i);
	  tree imm_stmt = SSA_NAME_DEF_STMT (use);
	  if (!IS_EMPTY_STMT (imm_stmt) && (!calc_for || calc_for (use)))
	    add_immediate_use (imm_stmt, stmt);
	}
    }

  if (flags & TDFA_USE_VOPS)
    {
      vuses = VUSE_OPS (ann);
      for (i = 0; i < NUM_VUSES (vuses); i++)
	{
	  tree vuse = VUSE_OP (vuses, i);
	  tree imm_rdef_stmt = SSA_NAME_DEF_STMT (vuse);
	  if (!IS_EMPTY_STMT (imm_rdef_stmt) && (!calc_for || calc_for (vuse)))
	    add_immediate_use (imm_rdef_stmt, stmt);
	}

      vdefs = VDEF_OPS (ann);
      for (i = 0; i < NUM_VDEFS (vdefs); i++)
	{
	  tree vuse = VDEF_OP (vdefs, i);
	  tree imm_rdef_stmt = SSA_NAME_DEF_STMT (vuse);
	  if (!IS_EMPTY_STMT (imm_rdef_stmt) && (!calc_for || calc_for (vuse)))
	    add_immediate_use (imm_rdef_stmt, stmt);
	}
    }
}


/* Add statement USE_STMT to the list of statements that use definitions
    made by STMT.  */

static void
add_immediate_use (tree stmt, tree use_stmt)
{
  stmt_ann_t ann = get_stmt_ann (stmt);
  struct dataflow_d *df;

  df = ann->df;
  if (df == NULL)
    {
      df = ann->df = ggc_alloc (sizeof (struct dataflow_d));
      memset ((void *) df, 0, sizeof (struct dataflow_d));
      df->uses[0] = use_stmt;
      return;
    }

  if (!df->uses[1])
    {
      df->uses[1] = use_stmt;
      return;
    }

  if (ann->df->immediate_uses == NULL)
    VARRAY_TREE_INIT (ann->df->immediate_uses, 4, "immediate_uses");

  VARRAY_PUSH_TREE (ann->df->immediate_uses, use_stmt);
}


/* If the immediate use of USE points to OLD, then redirect it to NEW.  */
 
static void
redirect_immediate_use (tree use, tree old, tree new)
{
  tree imm_stmt = SSA_NAME_DEF_STMT (use);
  struct dataflow_d *df = get_stmt_ann (imm_stmt)->df;
  unsigned int num_uses = num_immediate_uses (df);
  unsigned int i;

  for (i = 0; i < num_uses; i++)
    {
      if (immediate_use (df, i) == old)
	{
	  if (i == 0 || i == 1)
	    df->uses[i] = new;
	  else
	    VARRAY_TREE (df->immediate_uses, i - 2) = new;
	}
    }
}


/* Redirect all immediate uses for operands in OLD so that they point
   to NEW.  This routine should have no knowledge of how immediate
   uses are stored.  */

void
redirect_immediate_uses (tree old, tree new)
{
  stmt_ann_t ann = get_stmt_ann (old);
  use_optype uses = USE_OPS (ann);
  vuse_optype vuses = VUSE_OPS (ann);
  vdef_optype vdefs = VDEF_OPS (ann);
  unsigned int i;

  /* Look at USE_OPS or VUSE_OPS according to FLAGS.  */
  for (i = 0; i < NUM_USES (uses); i++)
    redirect_immediate_use (USE_OP (uses, i), old, new); 

  for (i = 0; i < NUM_VUSES (vuses); i++)
    redirect_immediate_use (VUSE_OP (vuses, i), old, new);

  for (i = 0; i < NUM_VDEFS (vdefs); i++)
    redirect_immediate_use (VDEF_OP (vdefs, i), old, new);
}


/*---------------------------------------------------------------------------
			    Manage annotations
---------------------------------------------------------------------------*/
/* Create a new annotation for a _DECL node T.  */

var_ann_t
create_var_ann (tree t)
{
  var_ann_t ann;

#if defined ENABLE_CHECKING
  if (t == NULL_TREE
      || !DECL_P (t)
      || (t->common.ann
	  && t->common.ann->common.type != VAR_ANN))
    abort ();
#endif

  ann = ggc_alloc (sizeof (*ann));
  memset ((void *) ann, 0, sizeof (*ann));

  ann->common.type = VAR_ANN;

  t->common.ann = (tree_ann) ann;

  return ann;
}


/* Create a new annotation for a statement node T.  */

stmt_ann_t
create_stmt_ann (tree t)
{
  stmt_ann_t ann;

#if defined ENABLE_CHECKING
  if ((!is_gimple_stmt (t) && !is_essa_node (t))
      || (t->common.ann
	  && t->common.ann->common.type != STMT_ANN))
    abort ();
#endif

  ann = ggc_alloc (sizeof (*ann));
  memset ((void *) ann, 0, sizeof (*ann));

  ann->common.type = STMT_ANN;

  /* Since we just created the annotation, mark the statement modified.  */
  ann->modified = true;

  t->common.ann = (tree_ann) ann;

  return ann;
}


/* Create a new annotation for an SSA name T.  */

ssa_name_ann_t
create_ssa_name_ann (tree t)
{
  ssa_name_ann_t ann;

#if defined ENABLE_CHECKING
  if (t == NULL_TREE
      || (t->common.ann
	  && t->common.ann->common.type != SSA_NAME_ANN))
    abort ();
#endif

  ann = ggc_alloc (sizeof (*ann));
  memset ((void *) ann, 0, sizeof (*ann));

  ann->common.type = SSA_NAME_ANN;
  t->common.ann = (tree_ann) ann;

  return ann;
}


/* Build a temporary.  Make sure and register it to be renamed.  */

tree
make_rename_temp (tree type, const char *prefix)
{
  tree t = create_tmp_var (type, prefix);
  add_referenced_tmp_var (t);
  bitmap_set_bit (vars_to_rename, var_ann (t)->uid);
  return t;
}



/*---------------------------------------------------------------------------
			      Debugging functions
---------------------------------------------------------------------------*/
/* Dump the list of all the referenced variables in the current function to
   FILE.  */

void
dump_referenced_vars (FILE *file)
{
  size_t i;

  fprintf (file, "\nReferenced variables in %s: %u\n\n", 
	   get_name (current_function_decl), (unsigned) num_referenced_vars);

  for (i = 0; i < num_referenced_vars; i++)
    {
      tree var = referenced_var (i);
      fprintf (file, "Variable: ");
      dump_variable (file, var);
      fprintf (file, "\n");
    }
}


/* Dump the list of all the referenced variables to stderr.  */

void
debug_referenced_vars (void)
{
  dump_referenced_vars (stderr);
}


/* Dump variable VAR and its may-aliases to FILE.  */

void
dump_variable (FILE *file, tree var)
{
  var_ann_t ann;

  if (var == NULL_TREE)
    {
      fprintf (file, "<nil>");
      return;
    }

  print_generic_expr (file, var, dump_flags);
  
  if (TREE_CODE (var) == SSA_NAME)
    var = SSA_NAME_VAR (var);

  ann = var_ann (var);

  fprintf (file, ", UID %u", (unsigned) ann->uid);

  if (ann->has_hidden_use)
    fprintf (file, ", has hidden uses");

  if (ann->type_mem_tag)
    {
      fprintf (file, ", type memory tag: ");
      print_generic_expr (file, ann->type_mem_tag, dump_flags);
    }

  if (ann->is_alias_tag)
    fprintf (file, ", is an alias tag");

  if (needs_to_live_in_memory (var))
    fprintf (file, ", is %s", TREE_STATIC (var) ? "static" : "global");

  if (is_call_clobbered (var))
    fprintf (file, ", call clobbered");

  if (ann->default_def)
    {
      fprintf (file, ", default def: ");
      print_generic_expr (file, ann->default_def, dump_flags);
    }

  if (ann->may_aliases)
    {
      fprintf (file, ", may aliases: ");
      dump_may_aliases_for (file, var);
    }

  fprintf (file, "\n");
}


/* Dump variable VAR and its may-aliases to stderr.  */

void
debug_variable (tree var)
{
  dump_variable (stderr, var);
}


/* Dump def-use edges on FILE.  */

void
dump_immediate_uses (FILE *file)
{
  basic_block bb;
  block_stmt_iterator si;
  const char *funcname
    = lang_hooks.decl_printable_name (current_function_decl, 2);

  fprintf (file, "\nDef-use edges for function %s\n", funcname);

  FOR_EACH_BB (bb)
    {
      tree phi;

      for (phi = phi_nodes (bb); phi; phi = TREE_CHAIN (phi))
	dump_immediate_uses_for (file, phi);

      for (si = bsi_start (bb); !bsi_end_p (si); bsi_next (&si))
	dump_immediate_uses_for (file, bsi_stmt (si));
    }

  fprintf (file, "\n");
}


/* Dump def-use edges on stderr.  */

void
debug_immediate_uses (void)
{
  dump_immediate_uses (stderr);
}


/* Dump all immediate uses for STMT on FILE.  */

void
dump_immediate_uses_for (FILE *file, tree stmt)
{
  dataflow_t df = get_immediate_uses (stmt);
  int num_imm_uses = num_immediate_uses (df);

  if (num_imm_uses > 0)
    {
      int i;

      fprintf (file, "-> ");
      print_generic_stmt (file, stmt, TDF_SLIM);
      fprintf (file, "\n");

      for (i = 0; i < num_imm_uses; i++)
	{
	  /* APPLE LOCAL begin AV if-conversion -dpatel  */
	  /* Print basic block number also.  */
	  basic_block bb =  bb_for_stmt (immediate_use (df, i));
	  fprintf (file, "\t[%d]\t", bb->index);
	  /* APPLE LOCAL end AV if-conversion -dpatel  */
	  print_generic_stmt (file, immediate_use (df, i), TDF_SLIM);
	  fprintf (file, "\n");
	}

      fprintf (file, "\n");
    }
}


/* Dump immediate uses for STMT on stderr.  */

void
debug_immediate_uses_for (tree stmt)
{
  dump_immediate_uses_for (stderr, stmt);
}


/* Dump various DFA statistics to FILE.  */

void
dump_dfa_stats (FILE *file)
{
  struct dfa_stats_d dfa_stats;

  unsigned long size, total = 0;
  const char * const fmt_str   = "%-30s%-13s%12s\n";
  const char * const fmt_str_1 = "%-30s%13lu%11lu%c\n";
  const char * const fmt_str_3 = "%-43s%11lu%c\n";
  const char *funcname
    = lang_hooks.decl_printable_name (current_function_decl, 2);

  collect_dfa_stats (&dfa_stats);

  fprintf (file, "\nDFA Statistics for %s\n\n", funcname);

  fprintf (file, "---------------------------------------------------------\n");
  fprintf (file, fmt_str, "", "  Number of  ", "Memory");
  fprintf (file, fmt_str, "", "  instances  ", "used ");
  fprintf (file, "---------------------------------------------------------\n");

  size = num_referenced_vars * sizeof (tree);
  total += size;
  fprintf (file, fmt_str_1, "Referenced variables", num_referenced_vars, 
	   SCALE (size), LABEL (size));

  size = dfa_stats.num_stmt_anns * sizeof (struct stmt_ann_d);
  total += size;
  fprintf (file, fmt_str_1, "Statements annotated", dfa_stats.num_stmt_anns,
	   SCALE (size), LABEL (size));

  size = dfa_stats.num_var_anns * sizeof (struct var_ann_d);
  total += size;
  fprintf (file, fmt_str_1, "Variables annotated", dfa_stats.num_var_anns,
	   SCALE (size), LABEL (size));

  size = dfa_stats.num_uses * sizeof (tree *);
  total += size;
  fprintf (file, fmt_str_1, "USE operands", dfa_stats.num_uses,
	   SCALE (size), LABEL (size));

  size = dfa_stats.num_defs * sizeof (tree *);
  total += size;
  fprintf (file, fmt_str_1, "DEF operands", dfa_stats.num_defs,
	   SCALE (size), LABEL (size));

  size = dfa_stats.num_vuses * sizeof (tree *);
  total += size;
  fprintf (file, fmt_str_1, "VUSE operands", dfa_stats.num_vuses,
	   SCALE (size), LABEL (size));

  size = dfa_stats.num_vdefs * sizeof (tree *);
  total += size;
  fprintf (file, fmt_str_1, "VDEF operands", dfa_stats.num_vdefs,
	   SCALE (size), LABEL (size));

  size = dfa_stats.num_phis * sizeof (struct tree_phi_node);
  total += size;
  fprintf (file, fmt_str_1, "PHI nodes", dfa_stats.num_phis,
	   SCALE (size), LABEL (size));

  size = dfa_stats.num_phi_args * sizeof (struct phi_arg_d);
  total += size;
  fprintf (file, fmt_str_1, "PHI arguments", dfa_stats.num_phi_args,
 	   SCALE (size), LABEL (size));

  fprintf (file, "---------------------------------------------------------\n");
  fprintf (file, fmt_str_3, "Total memory used by DFA/SSA data", SCALE (total),
	   LABEL (total));
  fprintf (file, "---------------------------------------------------------\n");
  fprintf (file, "\n");

  if (dfa_stats.num_phis)
    fprintf (file, "Average number of arguments per PHI node: %.1f (max: %d)\n",
	     (float) dfa_stats.num_phi_args / (float) dfa_stats.num_phis,
	     dfa_stats.max_num_phi_args);

  fprintf (file, "\n");
}


/* Dump DFA statistics on stderr.  */

void
debug_dfa_stats (void)
{
  dump_dfa_stats (stderr);
}


/* Collect DFA statistics and store them in the structure pointed by
   DFA_STATS_P.  */

static void
collect_dfa_stats (struct dfa_stats_d *dfa_stats_p)
{
  htab_t htab;
  basic_block bb;
  block_stmt_iterator i;

  if (dfa_stats_p == NULL)
    abort ();

  memset ((void *)dfa_stats_p, 0, sizeof (struct dfa_stats_d));

  /* Walk all the trees in the function counting references.  Start at
     basic block 0, but don't stop at block boundaries.  */
  htab = htab_create (30, htab_hash_pointer, htab_eq_pointer, NULL);

  for (i = bsi_start (BASIC_BLOCK (0)); !bsi_end_p (i); bsi_next (&i))
    walk_tree (bsi_stmt_ptr (i), collect_dfa_stats_r, (void *) dfa_stats_p,
	       (void *) htab);

  htab_delete (htab);

  FOR_EACH_BB (bb)
    {
      tree phi;
      for (phi = phi_nodes (bb); phi; phi = TREE_CHAIN (phi))
	{
	  dfa_stats_p->num_phis++;
	  dfa_stats_p->num_phi_args += PHI_NUM_ARGS (phi);
	  if (PHI_NUM_ARGS (phi) > dfa_stats_p->max_num_phi_args)
	    dfa_stats_p->max_num_phi_args = PHI_NUM_ARGS (phi);
	}
    }
}


/* Callback for walk_tree to collect DFA statistics for a tree and its
   children.  */

static tree
collect_dfa_stats_r (tree *tp, int *walk_subtrees ATTRIBUTE_UNUSED,
		     void *data)
{
  tree t = *tp;
  struct dfa_stats_d *dfa_stats_p = (struct dfa_stats_d *)data;

  if (t->common.ann)
    {
      switch (ann_type (t->common.ann))
	{
	case STMT_ANN:
	  {
	    stmt_ann_t ann = (stmt_ann_t) t->common.ann;
	    dfa_stats_p->num_stmt_anns++;
	    dfa_stats_p->num_defs += NUM_DEFS (DEF_OPS (ann));
	    dfa_stats_p->num_uses += NUM_USES (USE_OPS (ann));
	    dfa_stats_p->num_vdefs += NUM_VDEFS (VDEF_OPS (ann));
	    dfa_stats_p->num_vuses += NUM_VUSES (VUSE_OPS (ann));
	    break;
	  }

	case VAR_ANN:
	  dfa_stats_p->num_var_anns++;
	  break;

	default:
	  break;
	}
    }

  return NULL;
}


/*---------------------------------------------------------------------------
			     Miscellaneous helpers
---------------------------------------------------------------------------*/
/* Callback for walk_tree.  Used to collect variables referenced in
   the function.  */

static tree
find_vars_r (tree *tp, int *walk_subtrees, void *data)
{
  tree t = *tp;
  struct walk_state *walk_state = (struct walk_state *)data;

  if (SSA_VAR_P (t))
    {
      /* If T is a regular variable that the optimizers are interested
	 in, add it to the list of variables.  */
      add_referenced_var (t, walk_state);
    }
  else if (DECL_P (t)
	   || TYPE_P (t)
	   || TREE_CODE_CLASS (TREE_CODE (t)) == 'c')
    {
      /* Type, _DECL and constant nodes have no interesting children.
	 Ignore them.  */
      *walk_subtrees = 0;
    }


  return NULL_TREE;
}


/* Add VAR to the list of dereferenced variables.

   WALK_STATE contains a hash table used to avoid adding the same
      variable more than once. Note that this function assumes that
      VAR is a valid SSA variable.  If WALK_STATE is NULL, no
      duplicate checking is done.  */

static void
add_referenced_var (tree var, struct walk_state *walk_state)
{
  void **slot;
  var_ann_t v_ann;

  v_ann = get_var_ann (var);

  if (walk_state)
    slot = htab_find_slot (walk_state->vars_found, (void *) var, INSERT);
  else
    slot = NULL;

  if (slot == NULL || *slot == NULL)
    {
      /* This is the first time we find this variable, add it to the
         REFERENCED_VARS array and annotate it with attributes that are
	 intrinsic to the variable.  */
      if (slot)
	*slot = (void *) var;
      v_ann->uid = num_referenced_vars;
      VARRAY_PUSH_TREE (referenced_vars, var);

      /* Global and static variables are call-clobbered, always.  */
      if (needs_to_live_in_memory (var))
	mark_call_clobbered (var);

      /* DECL_NONLOCAL variables should not be removed, as they are needed
	 to emit nested functions.  */
      if (DECL_NONLOCAL (var))
	v_ann->used = 1;
    }
}


/* Return the virtual variable associated to the non-scalar variable VAR.  */

tree
get_virtual_var (tree var)
{
  enum tree_code code;

  STRIP_NOPS (var);

  if (TREE_CODE (var) == SSA_NAME)
    var = SSA_NAME_VAR (var);

  code = TREE_CODE (var);

  while (code == ARRAY_REF
         || code == COMPONENT_REF
	 || code == REALPART_EXPR
	 || code == IMAGPART_EXPR)
    {
      var = TREE_OPERAND (var, 0);
      code = TREE_CODE (var);
    }

#ifdef ENABLE_CHECKING
  /* Treating GIMPLE registers as virtual variables makes no sense.
     Also complain if we couldn't extract a _DECL out of the original
     expression.  */
  if (!SSA_VAR_P (var)
      || is_gimple_reg (var))
    abort ();
#endif

  return var;
}


/* Mark variables in BLOCK that have hidden uses.  A hidden use can
   occur due to VLA declarations or nested functions.   */

static void
find_hidden_use_vars (tree block)
{
  tree sub, decl, tem;

  /* Check all the arrays declared in the block for VLAs.
     While scanning the block's variables, also see if there is
     a nested function at this scope.  */
  for (decl = BLOCK_VARS (block); decl; decl = TREE_CHAIN (decl))
    {
      int inside_vla = 0;
      walk_tree (&decl, find_hidden_use_vars_r, &inside_vla, NULL);
    }

  /* Now repeat the search in any sub-blocks.  */
  for (sub = BLOCK_SUBBLOCKS (block); sub; sub = TREE_CHAIN (sub))
    find_hidden_use_vars (sub);

  /* A VLA parameter may use a variable which as set from another
     parameter to declare the size of the VLA.  We need to mark the
     variable as having a hidden use since it is used to declare the
     VLA parameter and that declaration is not seen by the SSA code. 

     Note get_pending_sizes clears the PENDING_SIZES chain, so we
     must restore it. */
  tem = get_pending_sizes ();
  put_pending_sizes (tem);
  for (; tem; tem = TREE_CHAIN (tem))
    {
      int inside_vla = 1;
      walk_tree (&TREE_VALUE (tem), find_hidden_use_vars_r, &inside_vla, NULL);
    }
}


/* Callback for walk_tree used by find_hidden_use_vars to analyze each 
   variable in a lexical block.  If the variable's size has a variable
   size, then mark all objects needed to compute the variable's size
   as having hidden uses.  */

static tree
find_hidden_use_vars_r (tree *tp, int *walk_subtrees ATTRIBUTE_UNUSED,
			void *data ATTRIBUTE_UNUSED)
{
  int *inside_vla = (int *) data;

  /* We need to look for hidden uses due to VLAs in variable
     definitions.  We originally used to look for these hidden
     uses in the variable's type, but that's unreliable if the
     type's size contains a SAVE_EXPR for a different function
     context than the variable is used within.  */
  if (SSA_VAR_P (*tp)
      && ((DECL_SIZE (*tp)
	   && ! really_constant_p (DECL_SIZE (*tp)))
	  || (DECL_SIZE_UNIT (*tp)
	      && ! really_constant_p (DECL_SIZE_UNIT (*tp)))))
    {
      int save = *inside_vla;

      *inside_vla = 1;
      walk_tree (&DECL_SIZE (*tp), find_hidden_use_vars_r, inside_vla, NULL);
      walk_tree (&DECL_SIZE_UNIT (*tp), find_hidden_use_vars_r,
		 inside_vla, NULL);
      *inside_vla = save;
    }
  else if (*inside_vla && SSA_VAR_P (*tp))
    set_has_hidden_use (*tp);

  return NULL_TREE;
}


/* Add a temporary variable to REFERENCED_VARS.  This is similar to
   add_referenced_var, but is used by passes that need to add new temps to
   the REFERENCED_VARS array after the program has been scanned for
   variables.  The variable will just receive a new UID and be added
   to the REFERENCED_VARS array without checking for duplicates.  */

void
add_referenced_tmp_var (tree var)
{
  add_referenced_var (var, NULL);
}


/* Return true if VDEFS_AFTER contains fewer entries than VDEFS_BEFORE.
   Note that this assumes that both varrays are VDEF operands for the same
   statement.  */

static inline bool
vdefs_disappeared_p (vdef_optype vdefs_before, vdef_optype vdefs_after)
{
  /* If there was nothing before, nothing could've disappeared.  */
  if (vdefs_before == NULL)
    return false;
     
  /* All/some of them gone.  */
  if (vdefs_after == NULL
      || NUM_VDEFS (vdefs_before) > NUM_VDEFS (vdefs_after))
    return true;

  return false;
}


/* Add all the non-SSA variables found in STMT's operands to the bitmap
   VARS_TO_RENAME.  */

void
mark_new_vars_to_rename (tree stmt, bitmap vars_to_rename)
{
  def_optype defs;
  use_optype uses;
  vdef_optype vdefs;
  vuse_optype vuses;
  size_t i;
  bitmap vars_in_vops_to_rename;
  bool found_exposed_symbol = false;
  vdef_optype vdefs_before, vdefs_after;
  stmt_ann_t ann;

  vars_in_vops_to_rename = BITMAP_XMALLOC ();

  /* Before re-scanning the statement for operands, mark the existing
     virtual operands to be renamed again.  We do this because when new
     symbols are exposed, the virtual operands that were here before due to
     aliasing will probably be removed by the call to get_stmt_operand.
     Therefore, we need to flag them to be renamed beforehand.

     We flag them in a separate bitmap because we don't really want to
     rename them if there are not any newly exposed symbols in the
     statement operands.  */
  ann = stmt_ann (stmt);
  vdefs_before = vdefs = VDEF_OPS (ann);
  for (i = 0; i < NUM_VDEFS (vdefs); i++)
    {
      tree var = VDEF_RESULT (vdefs, i);
      if (!DECL_P (var))
	var = SSA_NAME_VAR (var);
      bitmap_set_bit (vars_in_vops_to_rename, var_ann (var)->uid);
    }

  vuses = VUSE_OPS (ann);
  for (i = 0; i < NUM_VUSES (vuses); i++)
    {
      tree var = VUSE_OP (vuses, i);
      if (!DECL_P (var))
	var = SSA_NAME_VAR (var);
      bitmap_set_bit (vars_in_vops_to_rename, var_ann (var)->uid);
    }

  /* Now force an operand re-scan on the statement and mark any newly
     exposed variables.  */
  modify_stmt (stmt);
  get_stmt_operands (stmt);

  defs = DEF_OPS (ann);
  for (i = 0; i < NUM_DEFS (defs); i++)
    {
      tree var = DEF_OP (defs, i);
      if (DECL_P (var))
	{
	  found_exposed_symbol = true;
	  bitmap_set_bit (vars_to_rename, var_ann (var)->uid);
	}
    }

  uses = USE_OPS (ann);
  for (i = 0; i < NUM_USES (uses); i++)
    {
      tree var = USE_OP (uses, i);
      if (DECL_P (var))
	{
	  found_exposed_symbol = true;
	  bitmap_set_bit (vars_to_rename, var_ann (var)->uid);
	}
    }

  vdefs_after = vdefs = VDEF_OPS (ann);
  for (i = 0; i < NUM_VDEFS (vdefs); i++)
    {
      tree var = VDEF_RESULT (vdefs, i);
      if (DECL_P (var))
	{
	  found_exposed_symbol = true;
	  bitmap_set_bit (vars_to_rename, var_ann (var)->uid);
	}
    }

  vuses = VUSE_OPS (ann);
  for (i = 0; i < NUM_VUSES (vuses); i++)
    {
      tree var = VUSE_OP (vuses, i);
      if (DECL_P (var))
	{
	  found_exposed_symbol = true;
	  bitmap_set_bit (vars_to_rename, var_ann (var)->uid);
	}
    }

  /* If we found any newly exposed symbols, or if there are fewer VDEF
     operands in the statement, add the variables we had set in
     VARS_IN_VOPS_TO_RENAME to VARS_TO_RENAME.  We need to check for
     vanishing VDEFs because in those cases, the names that were formerly
     generated by this statement are not going to be available anymore.  */
  if (found_exposed_symbol
      || vdefs_disappeared_p (vdefs_before, vdefs_after))
    bitmap_a_or_b (vars_to_rename, vars_to_rename, vars_in_vops_to_rename);

  BITMAP_XFREE (vars_in_vops_to_rename);
}