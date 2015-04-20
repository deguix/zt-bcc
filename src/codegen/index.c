#include "phase.h"

#define MAX_MAP_LOCATIONS 128

struct local_alloc {
   int index;
   int func_size;
};

struct func_alloc {
   struct local_alloc* local;
   int start_index;
   int size;
};

struct alloc {
   struct codegen* codegen;
   struct func_alloc* func;
};

static void alloc_mapvars_index( struct codegen* codegen );
static void visit_tree( struct codegen* codegen );
static void visit_script( struct codegen* codegen, struct script* script );
static void assign_nestedcalls_id( struct codegen* codegen,
   struct func* nested_funcs );
static void visit_func( struct codegen* codegen, struct func* func );
static void visit_block( struct alloc* alloc, struct block* block );
static void visit_block_item( struct alloc* alloc, struct node* node );
static void init_func_alloc( struct func_alloc* alloc, int start_index );
static void init_alloc( struct alloc* alloc, struct codegen* codegen,
   struct func_alloc* func_alloc );
static void visit_stmt( struct alloc* alloc, struct node* node );
static void visit_for( struct alloc* alloc, struct for_stmt* stmt );
static void visit_nested_func( struct alloc* alloc, struct func* func );
static void visit_nested_userfunc( struct alloc* alloc, struct func* func );
static void visit_nested_builtinfunc( struct alloc* alloc, struct func* func );
static void visit_var( struct alloc* alloc, struct var* var );
static void visit_format_item( struct alloc* alloc, struct format_item* item );
static int alloc_scriptvar( struct alloc* alloc );
static void dealloc_lastscriptvar( struct alloc* alloc );
static void visit_packed_expr( struct alloc* alloc,
   struct packed_expr* packed );
static void visit_expr( struct alloc* alloc, struct node* node );
static void visit_call( struct alloc* alloc, struct call* call );
static void visit_paltrans( struct alloc* alloc, struct paltrans* trans );
static void visit_strcpy( struct alloc* alloc, struct strcpy_call* call );

void c_alloc_indexes( struct codegen* codegen ) {
   alloc_mapvars_index( codegen );
   visit_tree( codegen );
}

void alloc_mapvars_index( struct codegen* codegen ) {
   // Variables:
   // 
   // Order of allocation:
   // - arrays
   // - scalars, with-no-value
   // - scalars, with-value
   // - scalars, with-value, hidden
   // - scalars, with-no-value, hidden
   // - arrays, hidden
   // - imported
   //
   // -----------------------------------------------------------------------
   // Arrays.
   int index = 0;
   list_iter_t i;
   list_iter_init( &i, &codegen->task->library_main->vars );
   while ( ! list_end( &i ) ) {
      struct var* var = list_data( &i );
      if ( var->storage == STORAGE_MAP &&
         ( var->dim || ! var->type->primitive ) && ! var->hidden ) {
         var->index = index;
         ++index;
      }
      list_next( &i );
   }
   // Scalars, with-no-value.
   list_iter_init( &i, &codegen->task->library_main->vars );
   while ( ! list_end( &i ) ) {
      struct var* var = list_data( &i );
      if ( var->storage == STORAGE_MAP && ! var->dim &&
         var->type->primitive && ! var->hidden &&
         ( ! var->value || ! var->value->expr->value ) ) {
         var->index = index;
         ++index;
      }
      list_next( &i );
   }
   // Scalars, with-value.
   list_iter_init( &i, &codegen->task->library_main->vars );
   while ( ! list_end( &i ) ) {
      struct var* var = list_data( &i );
      if ( var->storage == STORAGE_MAP && ! var->dim &&
         var->type->primitive && ! var->hidden && var->value &&
         var->value->expr->value ) {
         var->index = index;
         ++index;
      }
      list_next( &i );
   }
   // Scalars, with-value, hidden.
   list_iter_init( &i, &codegen->task->library_main->vars );
   while ( ! list_end( &i ) ) {
      struct var* var = list_data( &i );
      if ( var->storage == STORAGE_MAP && ! var->dim &&
         var->type->primitive && var->hidden && var->value &&
         var->value->expr->value ) {
         var->index = index;
         ++index;
      }
      list_next( &i );
   }
   // Scalars, with-no-value, hidden.
   list_iter_init( &i, &codegen->task->library_main->vars );
   while ( ! list_end( &i ) ) {
      struct var* var = list_data( &i );
      if ( var->storage == STORAGE_MAP && ! var->dim &&
         var->type->primitive && var->hidden &&
         ( ! var->value || ! var->value->expr->value ) ) {
         var->index = index;
         ++index;
      }
      list_next( &i );
   }
   // Arrays, hidden.
   list_iter_init( &i, &codegen->task->library_main->vars );
   while ( ! list_end( &i ) ) {
      struct var* var = list_data( &i );
      if ( var->storage == STORAGE_MAP &&
         ( var->dim || ! var->type->primitive ) && var->hidden ) {
         var->index = index;
         ++index;
      }
      list_next( &i );
   }
   // Imported.
   list_iter_init( &i, &codegen->task->library_main->dynamic );
   while ( ! list_end( &i ) ) {
      struct library* lib = list_data( &i );
      list_iter_t k;
      list_iter_init( &k, &lib->vars );
      while ( ! list_end( &k ) ) {
         struct var* var = list_data( &k );
         if ( var->storage == STORAGE_MAP && var->used ) {
            var->index = index;
            ++index;
         }
         list_next( &k );
      }
      list_next( &i );
   }
   // Don't go over the variable limit.
   if ( index > MAX_MAP_LOCATIONS ) {
      t_diag( codegen->task, DIAG_ERR | DIAG_FILE,
         &codegen->task->library_main->file_pos,
         "library uses over maximum %d variables", MAX_MAP_LOCATIONS );
      t_bail( codegen->task );
   }
   // Functions:
   // -----------------------------------------------------------------------
   index = 0;
   // Imported functions:
   list_iter_init( &i, &codegen->task->library_main->dynamic );
   while ( ! list_end( &i ) ) {
      struct library* lib = list_data( &i );
      list_iter_t k;
      list_iter_init( &k, &lib->funcs );
      while ( ! list_end( &k ) ) {
         struct func* func = list_data( &k );
         struct func_user* impl = func->impl;
         if ( impl->usage ) {
            impl->index = index;
            ++index;
         }
         list_next( &k );
      }
      list_next( &i );
   }
   // Functions:
   list_iter_init( &i, &codegen->task->library_main->funcs );
   while ( ! list_end( &i ) ) {
      struct func* func = list_data( &i );
      if ( ! func->hidden ) {
         struct func_user* impl = func->impl;
         impl->index = index;
         ++index;
      }
      list_next( &i );
   }
   // In Little-E, the field of the function-call instruction that stores the
   // index of the function is a byte in size, allowing up to 256 different
   // functions to be called.
   // NOTE: Maybe automatically switch to the Big-E format? 
   if ( codegen->task->library_main->format == FORMAT_LITTLE_E && index > 256 ) {
      t_diag( codegen->task, DIAG_ERR | DIAG_FILE,
         &codegen->task->library_main->file_pos,
         "library uses over maximum 256 functions" );
      t_diag( codegen->task, DIAG_FILE, &codegen->task->library_main->file_pos,
         "to use more functions, try using the #nocompact directive" );
      t_bail( codegen->task );
   }
}

// - Allocates index for local variables.
// - Determines which strings need to be present at runtime.
//   ------------------------------------------------------------------------
//   Counting the usage of strings is done so only strings that are used are
//   outputted into the object file. There is no need to output the default
//   arguments of the MorphActor() function if it's never called, say.
void visit_tree( struct codegen* codegen ) {
   list_iter_t i;
   list_iter_init( &i, &codegen->task->library_main->scripts );
   while ( ! list_end( &i ) ) {
      struct script* script = list_data( &i );
      visit_script( codegen, script );
      list_next( &i );
   }
   // Functions.
   list_iter_init( &i, &codegen->task->library_main->funcs );
   while ( ! list_end( &i ) ) {
      visit_func( codegen, list_data( &i ) );
      list_next( &i );
   }
   // Variables.
   list_iter_init( &i, &codegen->task->library_main->vars );
   while ( ! list_end( &i ) ) {
      struct alloc alloc;
      init_alloc( &alloc, codegen, NULL );
      visit_var( &alloc, list_data( &i ) );
      list_next( &i );
   }
}

void assign_nestedcalls_id( struct codegen* codegen,
   struct func* nested_funcs ) {
   struct func* nested_func = nested_funcs;
   while ( nested_func ) {
      struct func_user* nested_impl = nested_func->impl;
      struct call* call = nested_impl->nested_calls;
      int id = 0;
      while ( call ) {
         call->nested_call->id = id;
         ++id;
         call = call->nested_call->next;
      }
      nested_func = nested_impl->next_nested;
   }
}

void visit_script( struct codegen* codegen, struct script* script ) {
   struct func_alloc func_alloc;
   init_func_alloc( &func_alloc, 0 );
   struct alloc alloc;
   init_alloc( &alloc, codegen, &func_alloc );
   struct param* param = script->params;
   while ( param ) {
      param->index = func_alloc.start_index;
      ++func_alloc.start_index;
      ++func_alloc.size;
      param = param->next;
   }
   visit_stmt( &alloc, script->body );
   script->size = func_alloc.size;
   if ( script->nested_funcs ) {
      assign_nestedcalls_id( codegen, script->nested_funcs );
   }
}

void visit_func( struct codegen* codegen, struct func* func ) {
   struct func_alloc func_alloc;
   init_func_alloc( &func_alloc, 0 );
   struct alloc alloc;
   init_alloc( &alloc, codegen, &func_alloc );
   struct param* param = func->params;
   while ( param ) {
      param->index = func_alloc.start_index;
      ++func_alloc.start_index;
      ++func_alloc.size;
      param = param->next;
   }
   struct func_user* impl = func->impl;
   visit_block( &alloc, impl->body );
   impl->size = func_alloc.size;
   if ( impl->nested_funcs ) {
      assign_nestedcalls_id( codegen, impl->nested_funcs );
   }
}

void init_func_alloc( struct func_alloc* alloc, int start_index ) {
   alloc->local = NULL;
   alloc->start_index = start_index;
   alloc->size = 0;
}

void init_alloc( struct alloc* alloc, struct codegen* codegen,
   struct func_alloc* func_alloc ) {
   alloc->codegen = codegen;
   alloc->func = func_alloc;
}

void visit_block( struct alloc* alloc, struct block* block ) {
   struct local_alloc* parent = alloc->func->local;
   struct local_alloc local;
   if ( parent ) {
      local.index = parent->index;
      local.func_size = parent->func_size;
   }
   else {
      local.index = alloc->func->start_index;
      local.func_size = alloc->func->size;
   }
   alloc->func->local = &local;
   list_iter_t i;
   list_iter_init( &i, &block->stmts );
   while ( ! list_end( &i ) ) {
      visit_block_item( alloc, list_data( &i ) );
      list_next( &i );
   }
   alloc->func->local = parent;
}

void visit_block_item( struct alloc* alloc, struct node* node ) {
   switch ( node->type ) {
   case NODE_FUNC:
      visit_nested_func( alloc, ( struct func* ) node );
      break;
   case NODE_CASE: {
      struct case_label* label = ( struct case_label* ) node;
      visit_expr( alloc, &label->number->node );
      break; }
   case NODE_VAR:
      visit_var( alloc, ( struct var* ) node );
      break;
   case NODE_CASE_DEFAULT:
   case NODE_GOTO_LABEL:
   case NODE_IMPORT:
      // Ignored.
      break;
   default:
      visit_stmt( alloc, node );
      break;
   }
}

void visit_stmt( struct alloc* alloc, struct node* node ) {
   switch ( node->type ) {
   case NODE_IF: {
      struct if_stmt* stmt = ( struct if_stmt* ) node;
      visit_stmt( alloc, stmt->body );
      visit_expr( alloc, &stmt->cond->node );
      if ( stmt->else_body ) {
         visit_stmt( alloc, stmt->else_body );
      }
      break; }
   case NODE_WHILE: {
      struct while_stmt* stmt = ( struct while_stmt* ) node;
      visit_expr( alloc, &stmt->cond->node );
      visit_stmt( alloc, stmt->body );
      break; }
   case NODE_FOR:
      visit_for( alloc, ( struct for_stmt* ) node );
      break;
   case NODE_FORMAT_ITEM:
      visit_format_item( alloc, ( struct format_item* ) node );
      break;
   case NODE_SWITCH: {
      struct switch_stmt* stmt = ( struct switch_stmt* ) node;
      visit_expr( alloc, &stmt->cond->node );
      visit_stmt( alloc, stmt->body );
      break; }
   case NODE_BLOCK:
      visit_block( alloc, ( struct block* ) node );
      break;
   case NODE_RETURN: {
      struct return_stmt* stmt = ( struct return_stmt* ) node;
      if ( stmt->return_value ) {
         visit_packed_expr( alloc, ( struct packed_expr* )
            stmt->return_value );
      }
      break; }
   case NODE_PACKED_EXPR: {
      visit_packed_expr( alloc, ( struct packed_expr* ) node );
      break; }
   case NODE_PALTRANS:
      visit_paltrans( alloc, ( struct paltrans* ) node );
      break;
   default:
      break;
   }
}

void visit_for( struct alloc* alloc, struct for_stmt* stmt ) {
   list_iter_t i;
   list_iter_init( &i, &stmt->init );
   while ( ! list_end( &i ) ) {
      struct node* node = list_data( &i );
      if ( node->type == NODE_EXPR ) {
         visit_expr( alloc, list_data( &i ) );
      }
      else if ( node->type == NODE_VAR ) {
         visit_var( alloc, ( struct var* ) node );
      }
      list_next( &i );
   }
   if ( stmt->cond ) {
      visit_expr( alloc, &stmt->cond->node );
   }
   list_iter_init( &i, &stmt->post );
   while ( ! list_end( &i ) ) {
      visit_expr( alloc, list_data( &i ) );
      list_next( &i );
   }
   visit_stmt( alloc, stmt->body );
}

void visit_nested_func( struct alloc* alloc, struct func* func ) {
   if ( func->type == FUNC_USER ) {
      visit_nested_userfunc( alloc, func );
   }
   else {
      visit_nested_builtinfunc( alloc, func );
   }
}

void visit_nested_userfunc( struct alloc* alloc, struct func* func ) {
   struct func_user* impl = func->impl;
   struct func_alloc* parent = alloc->func;
   impl->index_offset = parent->local->index;
   struct func_alloc func_alloc;
   init_func_alloc( &func_alloc, impl->index_offset );
   // Allocate index for each parameters.
   struct param* param = func->params;
   while ( param ) {
      param->index = func_alloc.start_index;
      ++func_alloc.start_index;
      ++func_alloc.size;
      param = param->next;
   }
   alloc->func = &func_alloc;
   visit_block( alloc, impl->body );
   impl->size = func_alloc.size;
   alloc->func = parent;
   int new_size = parent->local->func_size + func_alloc.size;
   if ( parent->size < new_size ) {
      parent->size = new_size;
   }
}

void visit_nested_builtinfunc( struct alloc* alloc, struct func* func ) {
   // Allocate a script variable only for a parameter that is used as part of
   // the default value of a later parameter.
   int used = 0;
   struct param* param = func->params;
   while ( param ) {
      if ( param->used ) {
         param->index = alloc_scriptvar( alloc );
         ++used;
      }
      param = param->next;
   }
}

void visit_var( struct alloc* alloc, struct var* var ) {
   struct value* value = var->value;
   while ( value ) {
      if ( ! value->string_initz ) {
         visit_expr( alloc, &value->expr->node );
      }
      value = value->next;
   }
   if ( var->storage == STORAGE_LOCAL ) {
      var->index = alloc_scriptvar( alloc );
   }
}

void visit_format_item( struct alloc* alloc, struct format_item* item ) {
   while ( item ) {
      visit_expr( alloc, &item->value->node );
      item = item->next;
   }
}

// Increases the space size of local variables by one, returning the index of
// the space slot.
int alloc_scriptvar( struct alloc* alloc ) {
   struct local_alloc* local = alloc->func->local;
   int index = local->index;
   ++local->index;
   ++local->func_size;
   if ( local->func_size > alloc->func->size ) {
      alloc->func->size = local->func_size;
   }
   return index;
}

// Decreases the space size of local variables by one.
void dealloc_lastscriptvar( struct alloc* alloc ) {
   --alloc->func->local->index;
   --alloc->func->local->func_size;
}

void visit_packed_expr( struct alloc* alloc, struct packed_expr* packed ) {
   visit_expr( alloc, &packed->expr->node );
   if ( packed->block ) {
      visit_block( alloc, packed->block );
   }
}

void visit_expr( struct alloc* alloc, struct node* node ) {
   switch ( node->type ) {
   case NODE_EXPR: {
      struct expr* expr = ( struct expr* ) node;
      visit_expr( alloc, expr->root );
      break; }
   case NODE_UNARY: {
      struct unary* unary = ( struct unary* ) node;
      visit_expr( alloc, unary->operand );
      break; }
   case NODE_BINARY: {
      struct binary* binary = ( struct binary* ) node;
      visit_expr( alloc, binary->lside );
      visit_expr( alloc, binary->rside );
      break; }
   case NODE_INDEXED_STRING_USAGE: {
      struct indexed_string_usage* usage =
         ( struct indexed_string_usage* ) node;
      usage->string->used = true;
      break; }
   case NODE_CALL:
      visit_call( alloc, ( struct call* ) node );
      break;
   case NODE_FORMAT_ITEM:
      visit_format_item( alloc, ( struct format_item* ) node );
      break;
   case NODE_ACCESS: {
      struct access* access = ( struct access* ) node;
      visit_expr( alloc, access->lside );
      visit_expr( alloc, access->rside );
      break; }
   case NODE_PAREN: {
      struct paren* paren = ( struct paren* ) node;
      visit_expr( alloc, paren->inside );
      break; }
   case NODE_SUBSCRIPT: {
      struct subscript* sub = ( struct subscript* ) node;
      visit_expr( alloc, sub->lside );
      visit_expr( alloc, &sub->index->node );
      break; }
   case NODE_ASSIGN: {
      struct assign* assign = ( struct assign* ) node;
      visit_expr( alloc, assign->lside );
      visit_expr( alloc, assign->rside );
      break; }
   case NODE_CONSTANT: {
      struct constant* constant = ( struct constant* ) node;
      if ( constant->value_node ) {
         visit_expr( alloc, &constant->value_node->node );
      }
      break; }
   case NODE_NAME_USAGE: {
      struct name_usage* usage = ( struct name_usage* ) node;
      visit_expr( alloc, usage->object );
      break; }
   case NODE_PARAM: {
      struct param* param = ( struct param* ) node;
      if ( param->default_value ) {
         visit_expr( alloc, &param->default_value->node );
      }
      break; }
   case NODE_CONDITIONAL: {
      struct conditional* cond = ( struct conditional* ) node;
      visit_expr( alloc, cond->left );
      if ( cond->middle ) {
         visit_expr( alloc, cond->middle );
      }
      visit_expr( alloc, cond->right );
      break; }
   case NODE_STRCPY:
      visit_strcpy( alloc, ( struct strcpy_call* ) node );
      break;
   default:
      break;
   }
}

void visit_call( struct alloc* alloc, struct call* call ) {
   visit_expr( alloc, call->operand );
   if ( call->func->type == FUNC_USER ) {
      struct func_user* impl = call->func->impl;
      impl->usage = 1;
   }
   // Arguments.
   list_iter_t i;
   list_iter_init( &i, &call->args );
   struct param* param = call->func->params;
   while ( ! list_end( &i ) ) {
      visit_expr( alloc, list_data( &i ) );
      if ( param ) {
         param = param->next;
      }
      list_next( &i );
   }
   // Default arguments.
   while ( param ) {
      visit_expr( alloc, &param->default_value->node );
      param = param->next;
   }
}

void visit_paltrans( struct alloc* alloc, struct paltrans* trans ) {
   visit_expr( alloc, &trans->number->node );
   struct palrange* range = trans->ranges;
   while ( range ) {
      visit_expr( alloc, &range->begin->node );
      visit_expr( alloc, &range->end->node );
      if ( range->rgb ) {
         visit_expr( alloc, &range->value.rgb.red1->node );
         visit_expr( alloc, &range->value.rgb.green1->node );
         visit_expr( alloc, &range->value.rgb.blue1->node );
         visit_expr( alloc, &range->value.rgb.red2->node );
         visit_expr( alloc, &range->value.rgb.green2->node );
         visit_expr( alloc, &range->value.rgb.blue2->node );
      }
      else {
         visit_expr( alloc, &range->value.ent.begin->node );
         visit_expr( alloc, &range->value.ent.end->node );
      }
      range = range->next;
   }
}

void visit_strcpy( struct alloc* alloc, struct strcpy_call* call ) {
   visit_expr( alloc, &call->array->node );
   if ( call->array_offset ) {
      visit_expr( alloc, &call->array_offset->node );
      if ( call->array_length ) {
         visit_expr( alloc, &call->array_length->node );
      }
   }
   visit_expr( alloc, &call->string->node );
   if ( call->offset ) {
      visit_expr( alloc, &call->offset->node );
   }
}