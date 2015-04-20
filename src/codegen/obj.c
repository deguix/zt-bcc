#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "phase.h"
#include "pcode.h"

static void add_buffer( struct codegen* codegen );
static struct buffer* alloc_buffer( void );
static void write_opc( struct codegen* codegen, int );
static void write_arg( struct codegen* codegen, int );
static void write_args( struct codegen* codegen );
static void add_immediate( struct codegen* codegen, int );
static void remove_immediate( struct codegen* codegen );
static void push_immediate( struct codegen* codegen, int );
static bool is_byte_value( int );

void c_init_obj( struct codegen* codegen ) {
   codegen->buffer_head = NULL;
   codegen->buffer = NULL;
   add_buffer( codegen );
   codegen->opc = PCD_NONE;
   codegen->opc_args = 0;
   codegen->immediate = NULL;
   codegen->immediate_tail = NULL;
   codegen->free_immediate = NULL;
   codegen->immediate_count = 0;
   codegen->push_immediate = false;
}

void add_buffer( struct codegen* codegen ) {
   if ( ! codegen->buffer || ! codegen->buffer->next ) {
      struct buffer* buffer = alloc_buffer();
      if ( codegen->buffer ) {
         codegen->buffer->next = buffer;
         codegen->buffer = buffer;
      }
      else {
         codegen->buffer_head = buffer;
         codegen->buffer = buffer;
      }
   }
   else {
      codegen->buffer = codegen->buffer->next;
   }
}

struct buffer* alloc_buffer( void ) {
   struct buffer* buffer = mem_alloc( sizeof( *buffer ) );
   buffer->next = NULL;
   buffer->used = 0;
   buffer->pos = 0;
   return buffer;
}

void c_add_sized( struct codegen* codegen, const void* data, int length ) {
   if ( BUFFER_SIZE - codegen->buffer->pos < length ) {
      add_buffer( codegen );
   }
   memcpy( codegen->buffer->data + codegen->buffer->pos, data, length );
   codegen->buffer->pos += length;
   if ( codegen->buffer->pos > codegen->buffer->used ) {
      codegen->buffer->used = codegen->buffer->pos;
   }
}

void c_add_byte( struct codegen* codegen, char value ) {
   c_add_sized( codegen, &value, sizeof( value ) );
}

void c_add_short( struct codegen* codegen, short value ) {
   c_add_sized( codegen, &value, sizeof( value ) );
}

void c_add_int( struct codegen* codegen, int value ) {
   c_add_sized( codegen, &value, sizeof( value ) );
}

void c_add_int_zero( struct codegen* codegen, int amount ) {
   while ( amount ) {
      c_add_int( codegen, 0 );
      --amount;
   }
}

void c_add_str( struct codegen* codegen, const char* value ) {
   c_add_sized( codegen, value, strlen( value ) );
}

void c_add_opc( struct codegen* codegen, int code ) {
   // Number-stacking instructions:
   // -----------------------------------------------------------------------
   {
      switch ( code ) {
      case PCD_PUSHNUMBER:
      case PCD_PUSHBYTE:
      case PCD_PUSH2BYTES:
      case PCD_PUSH3BYTES:
      case PCD_PUSH4BYTES:
      case PCD_PUSH5BYTES:
      case PCD_PUSHBYTES:
         codegen->push_immediate = true;
         goto finish;
      default:
         codegen->push_immediate = false;
         goto fold;
      }
   }
   // Unary constant folding:
   // -----------------------------------------------------------------------
   fold: {
      switch ( code ) {
      case PCD_UNARYMINUS:
      case PCD_NEGATELOGICAL:
      case PCD_NEGATEBINARY:
         if ( codegen->immediate_count ) {
            break;
         }
         // FALLTHROUGH
      default:
         goto binary_fold;
      }
      switch ( code ) {
      case PCD_UNARYMINUS:
         codegen->immediate_tail->value = ( - codegen->immediate_tail->value );
         break;
      case PCD_NEGATELOGICAL:
         codegen->immediate_tail->value = ( ! codegen->immediate_tail->value );
         break;
      case PCD_NEGATEBINARY:
         codegen->immediate_tail->value = ( ~ codegen->immediate_tail->value );
         break;
      default:
         break;
      }
      goto finish;
   }
   // Binary constant folding:
   // -----------------------------------------------------------------------
   binary_fold: {
      switch ( code ) {
      case PCD_ORLOGICAL:
      case PCD_ANDLOGICAL:
      case PCD_ORBITWISE:
      case PCD_EORBITWISE:
      case PCD_ANDBITWISE:
      case PCD_EQ:
      case PCD_NE:
      case PCD_LT:
      case PCD_LE:
      case PCD_GT:
      case PCD_GE:
      case PCD_LSHIFT:
      case PCD_RSHIFT:
      case PCD_ADD:
      case PCD_SUBTRACT:
      case PCD_MULIPLY:
      case PCD_DIVIDE:
      case PCD_MODULUS:
         if ( codegen->immediate_count >= 2 ) {
            break;
         }
         // FALLTHROUGH
      default:
         goto direct;
      }
      struct immediate* second_last = codegen->immediate;
      struct immediate* last = second_last->next;
      while ( last->next ) {
         second_last = last;
         last = last->next;
      }
      int l = second_last->value;
      int r = last->value;
      last->next = codegen->free_immediate;
      codegen->free_immediate = last;
      --codegen->immediate_count;
      last = second_last;
      last->next = NULL;
      codegen->immediate_tail = last;
      switch ( code ) {
      case PCD_ORLOGICAL: last->value = ( l || r ); break;
      case PCD_ANDLOGICAL: last->value = ( l && r ); break;
      case PCD_ORBITWISE: last->value = l | r; break;
      case PCD_EORBITWISE: last->value = l ^ r; break;
      case PCD_ANDBITWISE: last->value = l & r; break;
      case PCD_EQ: last->value = ( l == r ); break;
      case PCD_NE: last->value = ( l != r ); break;
      case PCD_LT: last->value = ( l < r ); break;
      case PCD_LE: last->value = ( l <= r ); break;
      case PCD_GT: last->value = ( l > r ); break;
      case PCD_GE: last->value = ( l >= r ); break;
      case PCD_LSHIFT: last->value = l << r; break;
      case PCD_RSHIFT: last->value = l >> r; break;
      case PCD_ADD: last->value = l + r; break;
      case PCD_SUBTRACT: last->value = l - r; break;
      case PCD_MULIPLY: last->value = l * r; break;
      case PCD_DIVIDE: last->value = l / r; break;
      case PCD_MODULUS: last->value = l % r; break;
      default: break;
      }
      goto finish;
   }
   // Direct instructions:
   // -----------------------------------------------------------------------
   direct: {
      if ( ! codegen->immediate ) {
         goto other;
      }
      static const struct entry {
         int code;
         int count;
         int direct;
      } entries[] = {
         { PCD_LSPEC1, 1, PCD_LSPEC1DIRECT },
         { PCD_LSPEC2, 2, PCD_LSPEC2DIRECT },
         { PCD_LSPEC3, 3, PCD_LSPEC3DIRECT },
         { PCD_LSPEC4, 4, PCD_LSPEC4DIRECT },
         { PCD_LSPEC5, 5, PCD_LSPEC5DIRECT },
         { PCD_DELAY, 1, PCD_DELAYDIRECT },
         { PCD_RANDOM, 2, PCD_RANDOMDIRECT },
         { PCD_THINGCOUNT, 2, PCD_THINGCOUNTDIRECT },
         { PCD_TAGWAIT, 1, PCD_TAGWAITDIRECT },
         { PCD_POLYWAIT, 1, PCD_POLYWAITDIRECT },
         { PCD_CHANGEFLOOR, 2, PCD_CHANGEFLOORDIRECT },
         { PCD_CHANGECEILING, 2, PCD_CHANGECEILINGDIRECT },
         { PCD_SCRIPTWAIT, 1, PCD_SCRIPTWAITDIRECT },
         { PCD_CONSOLECOMMAND, 3, PCD_CONSOLECOMMANDDIRECT },
         { PCD_SETGRAVITY, 1, PCD_SETGRAVITYDIRECT },
         { PCD_SETAIRCONTROL, 1, PCD_SETAIRCONTROLDIRECT },
         { PCD_GIVEINVENTORY, 2, PCD_GIVEINVENTORYDIRECT },
         { PCD_TAKEINVENTORY, 2, PCD_TAKEINVENTORYDIRECT },
         { PCD_CHECKINVENTORY, 1, PCD_CHECKINVENTORYDIRECT },
         { PCD_SPAWN, 6, PCD_SPAWNDIRECT },
         { PCD_SPAWNSPOT, 4, PCD_SPAWNSPOTDIRECT },
         { PCD_SETMUSIC, 3, PCD_SETMUSICDIRECT },
         { PCD_LOCALSETMUSIC, 3, PCD_LOCALSETMUSIC },
         { PCD_SETFONT, 1, PCD_SETFONTDIRECT },
         { PCD_NONE, 0, PCD_NONE } };
      const struct entry* entry = entries;
      while ( entry->code != code ) {
         if ( entry->code == PCD_NONE ) {
            goto other;
         }
         ++entry;
      }
      if ( entry->count > codegen->immediate_count ) {
         goto other;
      }
      else if ( entry->count < codegen->immediate_count ) {
         push_immediate( codegen, codegen->immediate_count - entry->count );
      }
      int count = 0;
      struct immediate* immediate = codegen->immediate;
      while ( immediate && is_byte_value( immediate->value ) ) {
         immediate = immediate->next;
         ++count;
      }
      int direct = entry->direct;
      if ( codegen->compress && count == entry->count ) {
         switch ( code ) {
         case PCD_LSPEC1: direct = PCD_LSPEC1DIRECTB; break;
         case PCD_LSPEC2: direct = PCD_LSPEC2DIRECTB; break;
         case PCD_LSPEC3: direct = PCD_LSPEC3DIRECTB; break;
         case PCD_LSPEC4: direct = PCD_LSPEC4DIRECTB; break;
         case PCD_LSPEC5: direct = PCD_LSPEC5DIRECTB; break;
         case PCD_DELAY: direct = PCD_DELAYDIRECTB; break;
         case PCD_RANDOM: direct = PCD_RANDOMDIRECTB; break;
         default: break;
         }
      }
      write_opc( codegen, direct );
      // Some instructions have other arguments that need to be written
      // before outputting the queued immediates.
      switch ( code ) {
      case PCD_LSPEC1:
      case PCD_LSPEC2:
      case PCD_LSPEC3:
      case PCD_LSPEC4:
      case PCD_LSPEC5:
         break;
      default:
         write_args( codegen );
      }
      goto finish;
   }
   // Other instructions:
   // -----------------------------------------------------------------------
   other: {
      if ( codegen->immediate_count ) {
         push_immediate( codegen, codegen->immediate_count );
      }
      write_opc( codegen, code );
   }
   finish: ;
}

void c_add_arg( struct codegen* codegen, int arg ) {
   if ( codegen->push_immediate ) {
      add_immediate( codegen, arg );
   }
   else {
      write_arg( codegen, arg );
      switch ( codegen->opc ) {
      case PCD_LSPEC1DIRECT:
      case PCD_LSPEC2DIRECT:
      case PCD_LSPEC3DIRECT:
      case PCD_LSPEC4DIRECT:
      case PCD_LSPEC5DIRECT:
      case PCD_LSPEC1DIRECTB:
      case PCD_LSPEC2DIRECTB:
      case PCD_LSPEC3DIRECTB:
      case PCD_LSPEC4DIRECTB:
      case PCD_LSPEC5DIRECTB:
         if ( codegen->opc_args == 1 ) {
            write_args( codegen );
         }
         break;
      default:
         break;
      }
   }
}

void write_opc( struct codegen* codegen, int code ) {
   if ( codegen->compress ) {
      // I don't know how the compression algorithm works exactly, but at this
      // time, the following works with all of the available opcodes as of this
      // writing. Also, now that the opcode is shrinked, the 4-byte arguments
      // that follow may no longer be 4-byte aligned.
      if ( code >= 240 ) {
         c_add_byte( codegen, ( char ) 240 );
         c_add_byte( codegen, ( char ) code - 240 );
      }
      else {
         c_add_byte( codegen, ( char ) code );
      }
   }
   else {
      c_add_int( codegen, code );
   }
   codegen->opc = code;
   codegen->opc_args = 0;
}

void write_arg( struct codegen* codegen, int arg ) {
   switch ( codegen->opc ) {
   case PCD_LSPEC1:
   case PCD_LSPEC2:
   case PCD_LSPEC3:
   case PCD_LSPEC4:
   case PCD_LSPEC5:
   case PCD_LSPEC5RESULT:
   case PCD_LSPEC1DIRECT:
   case PCD_LSPEC2DIRECT:
   case PCD_LSPEC3DIRECT:
   case PCD_LSPEC4DIRECT:
   case PCD_LSPEC5DIRECT:
      if ( codegen->opc_args == 0 && codegen->compress ) {
         c_add_byte( codegen, arg );
      }
      else {
         c_add_int( codegen, arg );
      }
      break;
   case PCD_PUSHBYTE:
   case PCD_PUSH2BYTES:
   case PCD_PUSH3BYTES:
   case PCD_PUSH4BYTES:
   case PCD_PUSH5BYTES:
   case PCD_PUSHBYTES:
   case PCD_LSPEC1DIRECTB:
   case PCD_LSPEC2DIRECTB:
   case PCD_LSPEC3DIRECTB:
   case PCD_LSPEC4DIRECTB:
   case PCD_LSPEC5DIRECTB:
   case PCD_DELAYDIRECTB:
   case PCD_RANDOMDIRECTB:
      c_add_byte( codegen, arg );
      break;
   case PCD_PUSHSCRIPTVAR:
   case PCD_PUSHMAPVAR:
   case PCD_PUSHMAPARRAY:
   case PCD_PUSHWORLDVAR:
   case PCD_PUSHWORLDARRAY:
   case PCD_PUSHGLOBALVAR:
   case PCD_PUSHGLOBALARRAY:
   case PCD_ASSIGNSCRIPTVAR:
   case PCD_ASSIGNMAPVAR:
   case PCD_ASSIGNWORLDVAR:
   case PCD_ASSIGNGLOBALVAR:
   case PCD_ASSIGNMAPARRAY:
   case PCD_ASSIGNWORLDARRAY:
   case PCD_ASSIGNGLOBALARRAY:
   case PCD_ADDSCRIPTVAR:
   case PCD_ADDMAPVAR:
   case PCD_ADDWORLDVAR:
   case PCD_ADDGLOBALVAR:
   case PCD_ADDMAPARRAY:
   case PCD_ADDWORLDARRAY:
   case PCD_ADDGLOBALARRAY:
   case PCD_SUBSCRIPTVAR:
   case PCD_SUBMAPVAR:
   case PCD_SUBWORLDVAR:
   case PCD_SUBGLOBALVAR:
   case PCD_SUBMAPARRAY:
   case PCD_SUBWORLDARRAY:
   case PCD_SUBGLOBALARRAY:
   case PCD_MULSCRIPTVAR:
   case PCD_MULMAPVAR:
   case PCD_MULWORLDVAR:
   case PCD_MULGLOBALVAR:
   case PCD_MULMAPARRAY:
   case PCD_MULWORLDARRAY:
   case PCD_MULGLOBALARRAY:
   case PCD_DIVSCRIPTVAR:
   case PCD_DIVMAPVAR:
   case PCD_DIVWORLDVAR:
   case PCD_DIVGLOBALVAR:
   case PCD_DIVMAPARRAY:
   case PCD_DIVWORLDARRAY:
   case PCD_DIVGLOBALARRAY:
   case PCD_MODSCRIPTVAR:
   case PCD_MODMAPVAR:
   case PCD_MODWORLDVAR:
   case PCD_MODGLOBALVAR:
   case PCD_MODMAPARRAY:
   case PCD_MODWORLDARRAY:
   case PCD_MODGLOBALARRAY:
   case PCD_LSSCRIPTVAR:
   case PCD_LSMAPVAR:
   case PCD_LSWORLDVAR:
   case PCD_LSGLOBALVAR:
   case PCD_LSMAPARRAY:
   case PCD_LSWORLDARRAY:
   case PCD_LSGLOBALARRAY:
   case PCD_RSSCRIPTVAR:
   case PCD_RSMAPVAR:
   case PCD_RSWORLDVAR:
   case PCD_RSGLOBALVAR:
   case PCD_RSMAPARRAY:
   case PCD_RSWORLDARRAY:
   case PCD_RSGLOBALARRAY:
   case PCD_ANDSCRIPTVAR:
   case PCD_ANDMAPVAR:
   case PCD_ANDWORLDVAR:
   case PCD_ANDGLOBALVAR:
   case PCD_ANDMAPARRAY:
   case PCD_ANDWORLDARRAY:
   case PCD_ANDGLOBALARRAY:
   case PCD_EORSCRIPTVAR:
   case PCD_EORMAPVAR:
   case PCD_EORWORLDVAR:
   case PCD_EORGLOBALVAR:
   case PCD_EORMAPARRAY:
   case PCD_EORWORLDARRAY:
   case PCD_EORGLOBALARRAY:
   case PCD_ORSCRIPTVAR:
   case PCD_ORMAPVAR:
   case PCD_ORWORLDVAR:
   case PCD_ORGLOBALVAR:
   case PCD_ORMAPARRAY:
   case PCD_ORWORLDARRAY:
   case PCD_ORGLOBALARRAY:
   case PCD_INCSCRIPTVAR:
   case PCD_INCMAPVAR:
   case PCD_INCWORLDVAR:
   case PCD_INCGLOBALVAR:
   case PCD_INCMAPARRAY:
   case PCD_INCWORLDARRAY:
   case PCD_INCGLOBALARRAY:
   case PCD_DECSCRIPTVAR:
   case PCD_DECMAPVAR:
   case PCD_DECWORLDVAR:
   case PCD_DECGLOBALVAR:
   case PCD_DECMAPARRAY:
   case PCD_DECWORLDARRAY:
   case PCD_DECGLOBALARRAY:
      if ( codegen->compress ) {
         c_add_byte( codegen, arg );
      }
      else {
         c_add_int( codegen, arg );
      }
      break;
   case PCD_CALL:
   case PCD_CALLDISCARD:
      if ( codegen->compress ) {
         c_add_byte( codegen, arg );
      }
      else {
         c_add_int( codegen, arg );
      }
      break;
   case PCD_CALLFUNC:
      if ( codegen->compress ) {
         // Argument-count field.
         if ( codegen->opc_args == 0 ) {
            c_add_byte( codegen, arg );
         }
         // Function-index field.
         else {
            c_add_short( codegen, arg );
         }
      }
      else {
         c_add_int( codegen, arg );
      }
      break;
   case PCD_CASEGOTOSORTED:
      // The arguments need to be 4-byte aligned.
      if ( codegen->opc_args == 0 ) {
         int i = c_tell( codegen ) % 4;
         if ( i ) {
            while ( i < 4 ) {
               c_add_byte( codegen, 0 );
               ++i;
            }
         }
      }
      c_add_int( codegen, arg );
      break;
   default:
      c_add_int( codegen, arg );
      break;
   }
   ++codegen->opc_args;
}

void write_args( struct codegen* codegen ) {
   while ( codegen->immediate_count ) {
      write_arg( codegen, codegen->immediate->value );
      remove_immediate( codegen );
   }
}

void add_immediate( struct codegen* codegen, int value ) {
   struct immediate* immediate = NULL;
   if ( codegen->free_immediate ) {
      immediate = codegen->free_immediate;
      codegen->free_immediate = immediate->next;
   }
   else {
      immediate = mem_alloc( sizeof( *immediate ) );
   }
   immediate->value = value;
   immediate->next = NULL;
   if ( codegen->immediate ) {
      codegen->immediate_tail->next = immediate;
   }
   else {
      codegen->immediate = immediate;
   }
   codegen->immediate_tail = immediate;
   ++codegen->immediate_count;
}

void remove_immediate( struct codegen* codegen ) {
   struct immediate* immediate = codegen->immediate;
   codegen->immediate = immediate->next;
   immediate->next = codegen->free_immediate;
   codegen->free_immediate = immediate;
   --codegen->immediate_count;
   if ( ! codegen->immediate_count ) {
      codegen->immediate = NULL;
   }
}

// NOTE: It is assumed that the count passed is always equal to, or is less
// than, the number of immediates in the queue.
void push_immediate( struct codegen* codegen, int count ) {
   int left = count;
   struct immediate* immediate = codegen->immediate;
   while ( left ) {
      int i = 0;
      struct immediate* temp = immediate;
      while ( i < left && is_byte_value( temp->value ) ) {
         temp = temp->next;
         ++i;
      }
      if ( i ) {
         left -= i;
         if ( codegen->compress ) {
            int code = PCD_PUSHBYTES;
            switch ( i ) {
            case 1: code = PCD_PUSHBYTE; break;
            case 2: code = PCD_PUSH2BYTES; break;
            case 3: code = PCD_PUSH3BYTES; break;
            case 4: code = PCD_PUSH4BYTES; break;
            case 5: code = PCD_PUSH5BYTES; break;
            default: break;
            }
            write_opc( codegen, code );
            if ( code == PCD_PUSHBYTES ) {
               write_arg( codegen, i );
            }
            while ( i ) {
               write_arg( codegen, immediate->value );
               immediate = immediate->next;
               --i;
            }
         }
         else {
            // Optimization: Pack four byte-values into a single 4-byte integer.
            // The instructions following this one will still be 4-byte aligned.
            while ( i >= 4 ) {
               write_opc( codegen, PCD_PUSH4BYTES );
               write_arg( codegen, immediate->value );
               immediate = immediate->next;
               write_arg( codegen, immediate->value );
               immediate = immediate->next;
               write_arg( codegen, immediate->value );
               immediate = immediate->next;
               write_arg( codegen, immediate->value );
               immediate = immediate->next;
               i -= 4;
            }
            while ( i ) {
               write_opc( codegen, PCD_PUSHNUMBER );
               write_arg( codegen, immediate->value );
               immediate = immediate->next;
               --i;
            }
         }
      }
      else {
         write_opc( codegen, PCD_PUSHNUMBER );
         write_arg( codegen, immediate->value );
         immediate = immediate->next;
         --left;
      }
   }
   left = count;
   while ( left ) {
      remove_immediate( codegen );
      --left;
   }
}

bool is_byte_value( int value ) {
   return ( ( unsigned int ) value <= 255 );
}

int c_tell( struct codegen* codegen ) {
   // Make sure to flush any queued immediates before acquiring the position.
   if ( codegen->immediate_count ) {
      push_immediate( codegen, codegen->immediate_count );
   }
   int pos = 0;
   struct buffer* buffer = codegen->buffer_head;
   while ( true ) {
      if ( buffer == codegen->buffer ) {
         pos += buffer->pos;
         break;
      }
      else if ( buffer->next ) {
         pos += BUFFER_SIZE;
         buffer = buffer->next;
      }
      else {
         pos += buffer->pos;
         break;
      }
   }
   return pos;
}

void c_seek( struct codegen* codegen, int pos ) {
   struct buffer* buffer = codegen->buffer_head;
   while ( buffer ) {
      if ( pos < BUFFER_SIZE ) {
         codegen->buffer = buffer;
         codegen->buffer->pos = pos;
         break;
      }
      pos -= BUFFER_SIZE;
      buffer = buffer->next;
   }
}

void c_seek_end( struct codegen* codegen ) {
   while ( codegen->buffer->next ) {
      codegen->buffer = codegen->buffer->next;
   }
   codegen->buffer->pos = codegen->buffer->used;
}

void c_flush( struct codegen* phase ) {
   // Don't overwrite the object file unless it's an ACS object file, or the
   // file doesn't exist. This is a basic check done to help prevent the user
   // from accidently killing their other files. Maybe add a command-line
   // argument to force the overwrite, like --force-object-overwrite?
   FILE* fh = fopen( phase->task->options->object_file, "rb" );
   if ( fh ) {
      char id[ 4 ];
      bool proceed = false;
      if ( fread( id, 1, 4, fh ) == 4 &&
         id[ 0 ] == 'A' && id[ 1 ] == 'C' && id[ 2 ] == 'S' &&
         ( id[ 3 ] == '\0' || id[ 3 ] == 'E' || id[ 3 ] == 'e' ) ) {
         proceed = true;
      }
      fclose( fh );
      if ( ! proceed ) {
         t_diag( phase->task, DIAG_ERR, "trying to overwrite unknown file: %s",
            phase->task->options->object_file );
         t_bail( phase->task );
      }
   }
   bool failure = false;
   fh = fopen( phase->task->options->object_file, "wb" );
   if ( fh ) {
      struct buffer* buffer = phase->buffer_head;
      while ( buffer ) {
         int written = fwrite( buffer->data, 1, buffer->used, fh );
         if ( written != buffer->used ) {
            failure = true;
            break;
         }
         buffer = buffer->next;
      }
      fclose( fh );
   }
   else {
      failure = true;
   }
   if ( failure ) {
      t_diag( phase->task, DIAG_ERR, "failed to write object file: %s",
         phase->task->options->object_file );
      t_bail( phase->task );
   }
}