/*
 *   
 *
 * Copyright  1990-2006 Sun Microsystems, Inc. All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 only, as published by the Free Software Foundation. 
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details (a copy is
 * included at /legal/license.txt). 
 * 
 * You should have received a copy of the GNU General Public License
 * version 2 along with this work; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA 
 * 
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 or visit www.sun.com if you need additional
 * information or have any questions. 
 */

#include "incls/_precompiled.incl"

#if ENABLE_INTERPRETER_GENERATOR
#include "incls/_TemplateTable_thumb2.cpp.incl"

void Template::null_check(Register object, bool tos_cached) {
  comment("null check");
  eol_comment("is object zero?");
  cmp(object, imm12(zero));
  // In case the tos is cached in a register make sure
  // to push it onto the stack before calling into the vm
  const char *name = tos_cached ?
      "interpreter_throw_NullPointerException_tos_cached" :
      "interpreter_throw_NullPointerException";
  // IMPL_NOTE: this should be changed to a 32-bit beq instruction, but it's
  // not supported by GCC
  it(eq); ldr_using_gp(pc, name);
}

void Template::branch(bool is_jsr, bool is_wide) {
  GUARANTEE(!is_jsr, "jsr not supported by CLDC");
  if (is_wide) {
    ldrb_imm12_w(tmp0, imm_index(bcp, 1));
    ldrb_imm12_w(tmp1, imm_index(bcp, 2));
    ldrb_imm12_w(tmp2, imm_index(bcp, 3));
    ldrb_imm12_w(tmp3, imm_index(bcp, 4));
    add(tmp0, tmp1, imm_shift(tmp0, lsl, BitsPerByte));
    add(tmp0, tmp2, imm_shift(tmp0, lsl, BitsPerByte));
    add(tmp0, tmp3, imm_shift(tmp0, lsl, BitsPerByte));
    prefetch(tmp0);
  } else {
    ldrsb_imm12_w(tmp1, bcp, 1);   // branchbyte1
    ldrb_imm12_w(tmp0,  bcp, 2);   // branchbyte2
    add(bcp, bcp, imm_shift(tmp1, lsl, BitsPerByte));
    prefetch(tmp0);
  }
  dispatch(is_jsr ? 1 : 0);
}


void Template::fast_access_field(bool is_static, bool is_put, BasicType type,
                                 int param_bytes, bool extended, int offset) {  
  Register A_LOW  = low_from_pair(tos_val, tmp0);
  Register A_HIGH = high_from_pair(tos_val, tmp0);

  int entry_size = (is_static ? 0 : 1) + (is_put ? word_size_for(type) : 0);
  bool with_aload_0 = extended && !is_static;

  if (with_aload_0) {
    GUARANTEE(!is_static && !is_put && entry_size == 1, "Sanity");
    entry_size = 0;
  }

  pop_arguments(entry_size);

  Label    npe;
  Register object       = tos_tag;
  Register field_offset = tmp1;

  if (is_static) {
    GUARANTEE(param_bytes == 2, "1-byte param not supported");
    // object:         class receiver
    // field_offset:   offset into class of field
    // tmp2 and tmp3 are temporaries

    // tmp0 gets the constant pool entry
    ldr_cp_entry_A(object, tmp2);
    get_class_list_base(tmp3);
    ldr_cp_entry_B(object, tmp2);

    comment("Get field offset from high sixteen bits of entry");
    mov_w(field_offset, object, lsr_shift, 16);

    comment("Get class_id from low sixteen bits of entry");
    sub(object, object, imm_shift(field_offset, lsl, 16));

#if ENABLE_ISOLATES
    Label class_is_initialized, need_init;
    //  load instanceClassDesc in tmp2
    ldr_class_from_index_and_base(tmp2, object, tmp3);
    comment("Get TaskMirror from class_id");
    get_mirror_list_base(tmp3);
#if ENABLE_ARM_V7
    Assembler::ldr(object, tmp3, reg(object));
#else
    ldr_w(object, tmp3, object, times_4);
#endif
    cmp(object, zero);
    comment("It will almost never be zero, so preload tmp3");
    get_task_class_init_marker(tmp3);
    b(need_init, eq);
    cmp(object, reg(tmp3));
    b(class_is_initialized, ne);
bind(need_init);
    push_results(entry_size);
    set_stack_state_to(tos_on_stack);
    // get instanceclass into extra arg
    comment("move instanceclass obj into extra arg");
    mov(r1, reg(tmp2));
    interpreter_call_vm("task_barrier", T_OBJECT);
    comment("reload registers");
    ldr_cp_entry_A(object, tmp2);
    ldr_cp_entry_B(object, tmp2);
    comment("Get field offset from high sixteen bits of entry");
    lsr_imm5(field_offset, object, 16);
    comment("get pointer to task mirror into object");
    mov(object, reg(r0));
    
    restore_stack_state_from(tos_on_stack);
    pop_arguments(entry_size);

bind(class_is_initialized);
#else
    comment("Get the JavaClass from class_id");
    ldr_class_from_index_and_base(object, object, tmp3);
#endif
  } else { 
    object = is_put ? (word_size_for(type) == 1 ? tmp0 : tmp2) : tos_val;

    if (with_aload_0) {
      ldr(tos_val, local_addr_at(0, 0));
    }

    if (param_bytes == 2) {
      ldrb_imm12_w(tmp3, imm_index(bcp, 1));
      ldrb_imm12_w(tmp4, imm_index(bcp, 2));
    } else if (param_bytes == 1) {
      ldrb_imm12_w(field_offset, imm_index(bcp, 1));
    } else {
      GUARANTEE(type == T_INT || type == T_OBJECT, "assumptions");
      comment("Implicit offset = %d", offset);
      mov(field_offset, imm12(offset/BytesPerWord)); // is multipled x4 later
    }

    cmp(object, imm12(zero));
    b(npe, eq);
    if (param_bytes == 2) {
      //  ENABLE_NATIVE_ORDER_REWRITING  HARDWARE_LITTLE_ENDIAN   little-endian
      //  ENABLE_NATIVE_ORDER_REWRITING !HARDWARE_LITTLE_ENDIAN   big-endian
      // !ENABLE_NATIVE_ORDER_REWRITING  HARDWARE_LITTLE_ENDIAN   big-endian
      // !ENABLE_NATIVE_ORDER_REWRITING !HARDWARE_LITTLE_ENDIAN   big-endian
      if (ENABLE_NATIVE_ORDER_REWRITING && HARDWARE_LITTLE_ENDIAN) {
        // Byte offset is little endian
        orr_w(field_offset, tmp3, imm_shift(tmp4, lsl, BitsPerByte));
      } else {
        // Byte offset is big endian
        orr_w(field_offset, tmp4, imm_shift(tmp3, lsl, BitsPerByte));
      }
    }
  }

  if (param_bytes == 2) {
    prefetch(3);
  } else if (param_bytes == 1) {
    prefetch(2);
  } else {
    prefetch(1);
  }

  int multiplier = is_static ? times_1 : LogBytesPerWord;
  if (is_put) {
    eol_comment("%s[%s] := %s", 
            reg_name(object), reg_name(field_offset), reg_name(tos_val));
    switch (type) {
    case T_BYTE  :
      strb_w(tos_val, object, field_offset, 0);
      break;
    case T_SHORT :
      strh_w(tos_val, object, field_offset, 0);
      break;
    case T_OBJECT:
      add(object, object, field_offset, lsl_shift, multiplier);
      str_imm12_w(tos_val, object);
      oop_write_barrier(object, tos_val, tmp1, tmp3, false);
      break;
    case T_DOUBLE: // Fall through
    case T_LONG  :
      //str(A_LOW,
      //    add_index(object, field_offset, lsl, multiplier, pre_indexed));
      add(object, object, field_offset, lsl_shift, multiplier);
      str_imm12_w(A_LOW, object);
      str_imm12_w(A_HIGH, imm_index(object, BytesPerWord));
      break;
    case T_INT   : // Fall through
    case T_FLOAT :
      //str(tos_val, add_index(object, field_offset, lsl, multiplier));
      add(object, object, field_offset, lsl_shift, multiplier);
      str_imm12_w(tos_val, object);
      break;
    default      :
      SHOULD_NOT_REACH_HERE();
    }
    dispatch(0);
  } else { 
    eol_comment("%s := %s[%s]", 
                reg_name(tos_val), reg_name(object), reg_name(field_offset));
    switch (type) {
    case T_BYTE:
      GUARANTEE(!is_static, "only non-static fields are packed");
      ldrsb(tos_val, object, field_offset, 0);
      break;
    case T_SHORT:
      GUARANTEE(!is_static, "only non-static fields are packed");
      ldrsh(tos_val, object, field_offset, 0);
      break;
    case T_CHAR:
      GUARANTEE(!is_static, "only non-static fields are packed");
      ldrh(tos_val, object, field_offset, 0);
      break;
    case T_OBJECT  : // fall through
    case T_FLOAT   : // fall through
    case T_INT:
      //ldr(tos_val, add_index(object, field_offset, lsl, multiplier));
      add(object, object, field_offset, lsl_shift, multiplier);
      ldr_imm12_w(tos_val, object);
      break;
    case T_LONG: // fall through
    case T_DOUBLE:
      if (object != A_LOW) { 
        //ldr(A_LOW,
        //     add_index(object, field_offset, lsl, multiplier, pre_indexed));
        add(object, object, field_offset, lsl_shift, multiplier);
        ldr_imm12_w(A_LOW, object);
        ldr_imm12_w(A_HIGH, imm_index(object, BytesPerWord));
      } else if (multiplier == 1) { 
        //ldr(A_LOW,  add_index(field_offset, object, lsl, 1, pre_indexed));
        add(tos_val, object, field_offset);
        ldr_imm12_w(A_LOW, object);
        ldr_imm12_w(A_HIGH, imm_index(field_offset, BytesPerWord));
      } else { 
        add(field_offset, object, field_offset, lsl_shift, multiplier);
        ldr_imm12_w(A_LOW, imm_index(field_offset, 0));
        ldr_imm12_w(A_HIGH, imm_index(field_offset, BytesPerWord));
      }
    }
    dispatch(word_size_for(type));
  }
bind(npe);
  if (!is_static) { 
    comment("Handle null pointer exception");
    push_results(entry_size);
    set_stack_state_to(tos_on_stack);
    ldr_using_gp(pc, "interpreter_throw_NullPointerException");
  }
}

SourceAssembler::Condition Template::j_not(j_condition cc) {
  // This routine is not required for ARM
  // - use not(as_ARM_Cond(cc)) instead!
  SHOULD_NOT_REACH_HERE();
  return nv;
}

void bc_unimplemented::generate() {
  comment("unimplemented bytecode");
  report_fatal("trying to execute unimplemented bytecode");
}

#if ENABLE_JAVA_DEBUGGER
void bc_breakpoint::generate() {
  breakpoint();
#if 0 // BEGIN_CONVERT_TO_T2
  set_stack_state_to(tos_on_stack);
  comment("breakpoint processing");
  comment("call JavaDebugger code to notify debugger");
  interpreter_call_vm("handle_breakpoint", T_INT);
  comment("bytecode to interpret is in eax");
  comment("Dispatch to the opcode that would have been here");
  and(tmp0, return_register, imm(0xff));
  // Make the stack look appropriate
  ldr(tmp0, bytecode_impl(tmp0));
  restore_stack_state_from(tos_on_stack);
  mov(pc, reg(tmp0));
#endif // END_CONVERT_TO_T2
}

#endif

void bc_nop::generate() {
  comment("do nothing");
  prefetch(1);
  dispatch(tos_interpreter_basic);
}

void bc_aconst_null::generate() {
  prefetch(1);
  pop_arguments(0);
  mov(tos_val, zero);
  dispatch(1);
}

void bc_iconst::generate(jint arg) {
  prefetch(1);
  pop_arguments(0);
  mov(tos_val, arg);
  dispatch(1);
}

void bc_lconst::generate(jlong arg) {
  prefetch(1);
  pop_arguments(0);
  mov(lsw_from_long_pair(tos_val, tmp0), lsw(arg));
  mov(msw_from_long_pair(tos_val, tmp0), msw(arg));
  dispatch(2);
}

void bc_fconst::generate(jfloat arg) {
  prefetch(1);
  pop_arguments(0);
  mov(tos_val, *(int*)&arg);
  dispatch(1);
}

void bc_dconst::generate(jdouble arg) {
  const jlong l = *(jlong *)&arg;
  pop_arguments(0);
  prefetch(1);
  GUARANTEE(lsw(l) == 0 || msw(l) == 0, "small floating point number");
  mov(lsw_from_double_pair(tos_val, tmp0), 0);
  mov(msw_from_double_pair(tos_val, tmp0), lsw(l) | msw(l));
  dispatch(2);
}

void bc_bipush::generate() {
  pop_arguments(0);
  prefetch(2);
  ldrsb_imm12_w(tos_val, bcp, 1 - 2); // We've already prefetched
  dispatch(1);
}

void bc_sipush::generate() {
  pop_arguments(0);
  ldrsb_imm12_w(tmp1, imm_index(bcp, 1));
  ldrb_imm12_w (tmp2, imm_index(bcp, 2));
  prefetch(3);
  orr_w(tos_val, tmp2, imm_shift(tmp1, lsl, BitsPerByte));
  dispatch(1);
}

void bc_call_vm_redo::generate(const char *name) {
  set_stack_state_to(tos_on_stack);
  interpreter_call_vm(name, T_VOID);

  if (ENABLE_BYTECODE_FLUSHING) {
    mov(r0, reg(bcp));
    // For merged bytecodes, make sure we account for the longest one
    mov(r1, max(3, Bytecodes::length_for(bytecode())));
    fast_c_call("arm_flush_icache", tmp4);
  }

  prefetch(0);
#if ENABLE_JAVA_DEBUGGER
  Label no_debugger;
  comment("Check to see if we are connected to a debugger");
  get_debugger_active(tmp0);
  tst(tmp0, imm(DEBUGGER_ACTIVE));
  b(no_debugger, eq);
  comment("We're debugging, see if the bytecode at bcp is a breakpoint");
  cmp(bcode, imm(Bytecodes::_breakpoint));
  b(no_debugger, ne);
  comment("A breakpoint is there so just vector to the bytecode returned");
  mov(bcode, reg(r0));
  bind(no_debugger);
#endif
  dispatch(tos_on_stack);
}

void bc_call_vm_dispatch::generate(const char*name) {
  set_stack_state_to(tos_on_stack);
  interpreter_call_vm(name, T_INT);

  if (ENABLE_BYTECODE_FLUSHING) {
    mov(tmp2, reg(r0));
    mov(r0, reg(bcp));
    // For merged bytecodes, make sure we account for the longest one
    mov(r1, max(3, Bytecodes::length_for(bytecode())));
    fast_c_call("arm_flush_icache", tmp4);
    sub(bcode, gp, tmp2, lsl_shift, 2);
  } else {
    sub(bcode, gp, r0, lsl_shift, 2);
  }

  ldr(bcode, bcode);
  restore_stack_state_from(tos_on_stack);
  mov(pc, bcode);
}

void bc_load_n::generate(BasicType type, int index) { 
  pop_arguments(0);
  prefetch(1);
  if (!is_two_word(type)) { 
    ldr(tos_val, local_addr_at(index, 0));
  } else { 
    ldr(tmp0,    local_addr_at(index, 0));
    ldr(tos_val, local_addr_at(index, 
                               JavaStackDirection*WordsPerStackElement));
  }
  dispatch(word_size_for(type));
}

void bc_store_n::generate(BasicType type, int index) { 
  pop_arguments(word_size_for(type));
  prefetch(1);
  if (!is_two_word(type)) {
    str(tos_val, local_addr_at(index, 0));
  } else {
    str(tos_val, local_addr_at(index, 
                                 JavaStackDirection*WordsPerStackElement));
    str(tmp0,    local_addr_at(index, 0));
  }
  dispatch(0);
}


void bc_load::generate(BasicType type, bool is_wide) {
  pop_arguments(0);

  int length = is_wide ? Bytecodes::wide_length_for(Bytecodes::_iload)
                       : Bytecodes::length_for(Bytecodes::_iload);
  if (is_wide) {
    ldrh_at_bcp_A(tmp3, tmp0, 2);
    prefetch(length);
    ldrh_at_bcp_B(tmp3, tmp0, 2);
  } else {
    ldrb_at_bcp(tmp3, 1);
    prefetch(length);
  }

  if (!is_two_word(type)) { 
    ldr_local_at(tos_val, tmp3, tmp2);
  } else { 
    int tos_offset = JavaStackDirection * BytesPerStackElement;
    get_local_addr(tmp2, tmp3);
    ldr(tos_val, imm_index(tmp2, tos_offset)); // low bits
    ldr(tmp0,    imm_index(tmp2, 0));
  }
  dispatch(word_size_for(type));
}

void bc_store::generate(BasicType type, bool is_wide) {
  pop_arguments(word_size_for(type));
  int length = is_wide ? Bytecodes::wide_length_for(Bytecodes::_istore)
                       : Bytecodes::length_for(Bytecodes::_istore);
  if (is_wide) {
    ldrh_at_bcp_A(tmp3, tmp2, 2);
    prefetch(length);
    ldrh_at_bcp_B(tmp3, tmp2, 2);
  } else {
    ldrb_at_bcp(tmp3, 1);
    prefetch(length);
  }
  if (!is_two_word(type)) { 
    str_local_at(tos_val, tmp3, tmp2);
  } else {
    get_local_addr(tmp2, tmp3);
    int tos_offset = JavaStackDirection * BytesPerStackElement;
    str(tos_val, imm_index(tmp2, tos_offset));
    str(tmp0,    imm_index(tmp2, 0));
  }
  dispatch(0);
}

void bc_array_load::generate(BasicType type) {
  pop_arguments(2);
  Label npe, aiob;
  Register array = tmp0;
  Register index = tos_val;
  cmp(array, zero);
  it(ne); ldr(tmp1, imm_index(array, Array::length_offset()));
  b(npe, eq);
  cmp(index, reg(tmp1));
  b(aiob, hs);

  prefetch(1); // bci must be incremented *after* test for exceptions!

  eol_comment("load array element");
  switch (type) {
    case T_BYTE  :
      add(array, array, imm_shift(index, lsl, LogBytesPerByte));
      ldrsb_imm12_w(tos_val, imm_index(array, Array::base_offset()));
      break;
    case T_CHAR  :
      add(array, array, imm_shift(index, lsl, LogBytesPerShort));
      ldrh_imm12_w(tos_val, imm_index(array, Array::base_offset()));
      break;
    case T_SHORT :
      add(array, array, imm_shift(index, lsl, LogBytesPerShort));
      ldrsh_imm12_w(tos_val, imm_index(array, Array::base_offset()));
      break;
    case T_INT   : // fall through
    case T_FLOAT : // fall through
    case T_OBJECT:
      add(array, array, imm_shift(index, lsl, LogBytesPerWord));
      ldr(tos_val, imm_index(array, Array::base_offset()));
      break;
    case T_LONG  : // fall through
    case T_DOUBLE: {
      Register A_LOW  = low_from_pair(tos_val, tmp0);
      Register A_HIGH = high_from_pair(tos_val, tmp0);
      add(A_HIGH, array, imm_shift(index, lsl, LogBytesPerLong));
      ldr(A_LOW,   imm_index(A_HIGH, Array::base_offset()));
      ldr(A_HIGH,  imm_index(A_HIGH, Array::base_offset() + BytesPerWord));
      break;
    }
    default      : SHOULD_NOT_REACH_HERE();
  }
  dispatch(word_size_for(type));

bind(npe);
  comment("Handle null pointer exception"); 
  push_results(2);                // push arguments back on stack
  set_stack_state_to(tos_on_stack);
  ldr_using_gp(pc, "interpreter_throw_NullPointerException");

bind(aiob);
  comment("Handle array index out of bounds exception"); 
  push_results(2);                // push arguments back on stack
  set_stack_state_to(tos_on_stack);
  ldr_using_gp(pc, "interpreter_throw_ArrayIndexOutOfBoundsException");
}

void bc_array_store::generate(BasicType type) {
  pop_arguments(2 + word_size_for(type));

  Register index = word_size_for(type) == 2 ? tmp2 : tmp0;
  Register array = word_size_for(type) == 2 ? tmp4 : tmp2;

  Label npe, aiob;
  Label slow_type_check, done_type_check;

  cmp(array, zero);
  it(ne); ldr(tmp1, imm_index(array, Array::length_offset()));
  b(npe, eq);
  cmp(index, reg(tmp1));
  b(aiob, hs);

  if (type == T_OBJECT) { 
    Register object = tos_val;
    cmp(object, zero);
    b(done_type_check, eq);
    // Get the class of the object and the element class of the array
    eol_comment("array near");
    ldr(tmp4, imm_index(array, 0));
    eol_comment("object near");
    ldr(tmp5, imm_index(object, 0));
    eol_comment("array klass");
    ldr(tmp4, imm_index(tmp4, 0));
    eol_comment("object klass");
    ldr(tmp5, imm_index(tmp5, 0));
    eol_comment("array element type");
    ldr(tmp4, imm_index(tmp4, ObjArrayClass::element_class_offset()));

    // Fast check against the subtype check caches.
    eol_comment("subtype cache 1");
    ldr(tos_tag, imm_index(tmp5, JavaClass::subtype_cache_1_offset()));
    eol_comment("subtype cache 2");
    ldr(tmp5,    imm_index(tmp5, JavaClass::subtype_cache_2_offset()));
    cmp(tmp4, reg(tos_tag));
    it(ne);
    cmp(tmp4, reg(tmp5));
    b(slow_type_check,   ne);
  }

bind(done_type_check);
  prefetch(1); // bci must be incremented *after* test for exceptions!

  comment("store array element");
  switch (type) {
    case T_BYTE  :
      add(array, array, imm_shift(index, lsl, LogBytesPerByte));
      strb_imm12_w(tos_val, imm_index(array, Array::base_offset()));
      break;
    case T_CHAR  : // fall through
    case T_SHORT :
      add(array, array, imm_shift(index, lsl, LogBytesPerShort));
      strh_imm12_w(tos_val, imm_index(array, Array::base_offset()));
      break;
    case T_INT   : // fall through
    case T_FLOAT :
      add(array, array, imm_shift(index, lsl, LogBytesPerWord));
      str_imm12_w(tos_val, imm_index(array, Array::base_offset()));
      break;

    case T_OBJECT: // fall through
      add(array, array, imm_shift(index, lsl, LogBytesPerWord));
      Macros::str(tos_val, array, PreIndex(Array::base_offset()));
      oop_write_barrier(array, index, tmp1, tmp3, false);
      break;

    case T_LONG  : // fall through
    case T_DOUBLE: {
      Register A_LOW  = low_from_pair(tos_val, tmp0);
      Register A_HIGH = high_from_pair(tos_val, tmp0);
      add(array, array, imm_shift(index, lsl, LogBytesPerLong));
      str(A_LOW,  imm_index(array, Array::base_offset()));
      str(A_HIGH, imm_index(array, Array::base_offset() + BytesPerWord));
      break;
    }
    default      :
        SHOULD_NOT_REACH_HERE();
  }
  dispatch(0);

bind(npe);
  comment("Handle null pointer exception"); 
  push_results(2 + word_size_for(type));
  set_stack_state_to(tos_on_stack);
  ldr_using_gp(pc, "interpreter_throw_NullPointerException");

bind(aiob);
  comment("Handle array index out of bounds exception");     
  push_results(2 + word_size_for(type));
  set_stack_state_to(tos_on_stack);
  ldr_using_gp(pc, "interpreter_throw_ArrayIndexOutOfBoundsException");

bind(slow_type_check);
  if (type == T_OBJECT) { 
    comment("Handle slow type checking");
    push_results(3);
    set_stack_state_to(tos_on_stack);
    interpreter_call_vm("array_store_type_check", T_VOID);
    restore_stack_state_from(tos_on_stack);
    pop_arguments(3);
    b(done_type_check);
  }
}

void bc_pop::generate() {
  prefetch(1);
  pop_arguments(-1);
  dispatch(0);
}

void bc_pop_and_npe_if_null::generate() {
  set_stack_state_to(tos_in_regs);
  null_check(tos_val);
  prefetch(1);
  dispatch(tos_on_stack);
}

#if !ENABLE_CPU_VARIANT
void bc_init_static_array::generate() {
  ldrb(tmp1, bcp, PreIndex(1));
  ldrb(tmp4, bcp, PreIndex(1));
  ldrb(tmp2, bcp, PreIndex(1));

  cmp(tos_val, zero);
  it(eq);
  ldr_using_gp(pc, "interpreter_throw_NullPointerException");

  ldr(tmp3, imm_index(tos_val, Array::length_offset()));  // load array size
  add(tmp0, tos_val, imm(Array::base_offset())); // load array base address

  if (HARDWARE_LITTLE_ENDIAN) {
    orr_w(tmp2, tmp4, imm_shift(tmp2, lsl, BitsPerByte));
  } else {
    orr_w(tmp2, tmp2, imm_shift(tmp4, lsl, BitsPerByte));
  }
  cmp(tmp2, reg(tmp3));
  it(gt);
  ldr_using_gp(pc, "interpreter_throw_ArrayIndexOutOfBoundsException");

  lsl_w(tmp2, tmp2, tmp1);
  add(tmp2, tmp2, reg(bcp)); // tmp2 now points to the last item

  Label copy_byte;
  // we cannot use ldrh and ldr due to addresses could be unaligned
  bind(copy_byte);
  ldrb(tmp1, bcp, PreIndex(1));
  strb(tmp1, tmp0, PostIndex(1));
  cmp(tmp2, reg(bcp));
  b(copy_byte, gt);  

  prefetch(1);
  dispatch(tos_interpreter_basic);
}
#endif //!ENABLE_CPU_VARIANT

void bc_pop2::generate() {
  prefetch(1);
  pop_arguments(-2); 
  dispatch(0);
}

void bc_dup::generate() {
  if (ENABLE_TOS_CACHING) { 
    prefetch(1);
    pop_arguments(1);
    dispatch(2, tos, tos);
  } else { 
    // Slight optimization.  Grab the argument without popping it off stack
    eol_comment("Grab tos without popping it");
    StackTypeMode mode = ENABLE_FULL_STACK ? full : empty;
    prefetch(1);
    pop_arguments(0);
    pop(tos, al, mode, no_writeback);
    dispatch(1, tos);
  }
}

// ... tmp01, tos -> ... tos, tmp01, tos
void bc_dup_x1::generate() {
  pop_arguments(2);
  comment("dup_x1 (...ba => ...aba)");
  prefetch(1);
  dispatch(3, tos, tmp01, tos);
}

// .. tmp23, tmp01, tos -> ..tos, tmp23, tmp01, tos
void bc_dup_x2::generate() {
  pop_arguments(3);
  comment("dup_x2 (...cba => ...acba");
  prefetch(1);
  dispatch(4, tos, tmp01, tmp23, tos);
}

void bc_dup2::generate() {
  StackTypeMode mode = ENABLE_FULL_STACK ? full : empty;
  pop_arguments(1);
  eol_comment("Grab second element without popping it");
  pop(tmp01, al, mode, no_writeback);

  comment("dup2 (...ba => ...baba");
  prefetch(1);
  dispatch(3, tos, tmp01, tos);
}

void bc_dup2_x1::generate() {
  pop_arguments(3);
  comment("dup2_x1 (...cba => ...bacba)");
  prefetch(1);
  dispatch(5, tos, tmp01, tmp23, tos, tmp01);
}

void bc_dup2_x2::generate() {
  StackTypeMode mode = ENABLE_FULL_STACK ? full : empty;
  pop_arguments(4);
  GUARANTEE(tmp5 == bcode, "Code assumption"); // See comment below
  comment("dup_x2 (...dcba => ...badcba)");
  // We have to push tmp45 before we do the prefetch because prefetch uses tmp5
  push(tmp01, al, mode);
  push(tos,   al, mode);
  push(tmp45, al, mode);
  prefetch(1);
  dispatch(3, tos, tmp01, tmp23);
}

void bc_swap::generate() {
  prefetch(1);
  pop_arguments(2);
  dispatch(2, tmp01, tos);
}

void bc_iop2::generate(j_operation op) {
  pop_arguments(2);
  prefetch(1);
  switch (op) {
    case j_add:
      add(tos_val, tmp0, reg(tos_val));
      break;

    case j_sub:
      sub(tos_val, tmp0, reg(tos_val));
      break;

    case j_mul:
      mul_w(tos_val, tmp0,     tos_val );
      break;

    case j_and:
      and_w(tos_val, tmp0, reg(tos_val));
      break;

    case j_or :
      orr_w(tos_val, tmp0, reg(tos_val));
      break;

    case j_xor:
      eor_w(tos_val, tmp0, reg(tos_val));
      break;

    // shifts
    case j_shl:
      and_imm12_w(tmp1, tos_val, imm12(0x1f));
      lsl(tmp0, tmp1);
      mov(tos_val, tmp0);
      break;

    case j_shr:
      and_imm12_w(tmp1, tos_val, imm12(0x1f));
      asr(tmp0, tmp1);
      mov(tos_val, tmp0);
      break;

    case j_ushr:
      and_imm12_w(tmp1, tos_val, imm12(0x1f));
      lsr(tmp0, tmp1);
      mov(tos_val, tmp0);
      break;

    default:
      SHOULD_NOT_REACH_HERE();
  }
  // no need to set tag since it is already correct
  dispatch(1);
}

void bc_lop2::generate(j_operation op) {
  pop_arguments(4);
  Register A_MSW = msw_from_long_pair(r0, tmp0);
  Register A_LSW = lsw_from_long_pair(r0, tmp0);

  Register B_MSW = msw_from_long_pair(tmp2, tmp4);
  Register B_LSW = lsw_from_long_pair(tmp2, tmp4);

  prefetch(1);
  switch (op) {
    case j_add:
      add(A_LSW, B_LSW, reg(A_LSW), lsl_shift, 0, set_CC);
      adc_w(A_MSW, B_MSW, reg(A_MSW));
      break;
    case j_sub:
      sub(A_LSW, B_LSW, reg(A_LSW), lsl_shift, 0, set_CC);
      sbc_w(A_MSW, B_MSW, reg(A_MSW));
      break;
    case j_and:
      and_w(A_LSW, B_LSW, reg(A_LSW));
      and_w(A_MSW, B_MSW, reg(A_MSW));
      break;
    case j_or:
      orr_w(A_LSW, B_LSW, reg(A_LSW));
      orr_w(A_MSW, B_MSW, reg(A_MSW));
      break;
    case j_xor:
      eor_w(A_LSW, B_LSW, reg(A_LSW));
      eor_w(A_MSW, B_MSW, reg(A_MSW));
      break;
    default:
      SHOULD_NOT_REACH_HERE();
  }
  // No need to set tag
  dispatch(2);
}


void bc_iinc::generate(bool is_wide) {
  // We preserve the state of the stack, w/out ever touching it
  if (is_wide) { 
    ldrb_imm12_w(tmp0,  imm_index(bcp, 2));
    ldrb_imm12_w(tmp1,  imm_index(bcp, 3));
    ldrsb_imm12_w(tmp2, imm_index(bcp, 4));
    ldrb_imm12_w(tmp3,  imm_index(bcp, 5));
    eol_comment("register index");
    add(tmp0, tmp1, imm_shift(tmp0, lsl, 1 * BitsPerByte));
  } else { 
    eol_comment("register index");
    ldrb_imm12_w(tmp0,  imm_index(bcp, 1));
    eol_comment("increment");
    ldrsb_imm12_w(tmp2, imm_index(bcp, 2));
  } 
  eol_comment("get local");
  ldr_local_at(tmp1, tmp0, tmp4);

  prefetch(is_wide ? Bytecodes::wide_length_for(Bytecodes::_iinc)
                   : Bytecodes::length_for(Bytecodes::_iinc));

  eol_comment("increment local");
  if (is_wide) { 
    add(tmp1, tmp1, imm_shift(tmp2, lsl, 1 * BitsPerByte));
    add(tmp1, tmp1, reg(tmp3));
  } else {
    add(tmp1, tmp1, reg(tmp2));
  }

  eol_comment("store local");
  str_local_at(tmp1, tmp0, tmp4);
  dispatch(tos_interpreter_basic);
}

void bc_neg::generate(BasicType type) {
  Register A_LOW  = JavaStackDirection < 0 ? r0   : tmp3;
  Register A_HIGH = JavaStackDirection < 0 ? tmp3 : r0;

  switch (type) {
    case T_INT   :
      pop_arguments(1);
      prefetch(1);
      eol_comment("negate");
      rsb_imm12_w(tos_val, tos_val, imm12(zero));
      dispatch(1);
      break;

    case T_LONG  : {
      pop_arguments(2);
      Register A_MSW = msw_from_long_pair(tos_val, tmp0);
      Register A_LSW = lsw_from_long_pair(tos_val, tmp0);
      prefetch(1);
      eol_comment("negate");
      mov(tmp1, zero);
      sub(A_LSW, tmp1, A_LSW, lsl_shift, 0, set_CC);
      sbc_w(A_MSW, tmp1, A_MSW);
      dispatch(2);
      break;
    }

    case T_FLOAT :
      pop_arguments(1);
      prefetch(1);
      eol_comment("flip sign bit");
      eor_imm12_w(tos_val, tos_val, modified_imm12(0x80000000)); // IMPL_NOTE: macro needed
      dispatch(1);
      break;

    case T_DOUBLE: {
      Register A_MSW = msw_from_double_pair(tos_val, tmp0);
      Register A_LSW = lsw_from_double_pair(tos_val, tmp0);

      if (A_MSW == tos_val) {
        pop_arguments(1);
        prefetch(1);
        eol_comment("flip sign bit");
        eor_imm12_w(tos_val, tos_val, modified_imm12(0x80000000));
        dispatch(1);
      } else {
        int offset = JavaFrame::arg_offset_from_sp(ENABLE_TOS_CACHING ? 0 : 1)
                 + (ENABLE_FULL_STACK ? 0 : -JavaStackDirection * BytesPerWord);
        eol_comment("get word containing sign bit");
        ldr(A_MSW, jsp, offset);
        prefetch(1);
        eol_comment("flip sign bit");
        eor_imm12_w(A_MSW, A_MSW, modified_imm12(0x80000000));
        eol_comment("and put it back");
        str(A_MSW, jsp, offset);
        dispatch(tos_interpreter_basic);
      }
      break;
    }

    default:
      SHOULD_NOT_REACH_HERE();
  }
}

void bc_lcmp::generate() {
  pop_arguments(4);

  Register B_MSW = msw_from_long_pair(tos_val, tmp0);
  Register B_LSW = lsw_from_long_pair(tos_val, tmp0);

  Register A_MSW = msw_from_long_pair(tmp2, tmp4);
  Register A_LSW = lsw_from_long_pair(tmp2, tmp4);

  prefetch(1);

  eol_comment("subtract arguments");
  sub(A_LSW, A_LSW, reg(B_LSW), lsl_shift, 0, set_CC);
  sbc_w(A_MSW, A_MSW, reg(B_MSW), lsl_shift, 0, set_CC);

  eol_comment("handle *only* for < and >=");
  it(lt);
  mvn_imm12_w(tos_val, modified_imm12(0)); // we store -1 this way
  it(ge);
  mov(tos_val, +1);
  eol_comment("correction for ==");
  orr_w(A_LSW, A_LSW, reg(A_MSW), lsl_shift, 0, set_CC);
  it(eq);
  mov(tos_val, +0);

  dispatch(1);
}

void bc_irem::generate() {
#if ENABLE_ARM_V7 && 0 // sdiv is not supported in ARMv7l
  pop_arguments(2);
  prefetch(1);
  sdiv_w(tos_tag, tmp0, tos_val);
  Assembler::mul(tos_tag, tos_val);
  Assembler::sub(tos_val, tmp0, tos_tag);
  dispatch(1);
#else
  // WARNING:  idiv_irem knows that the saved bcp is in tmp2
  // Every call to interpreter_idiv_irem must followed
  // by the global label which is checked in compiler_idiv_irem
  mov(tmp2, reg(bcp));
  call_from_interpreter("interpreter_idiv_irem", true);
bind("called_from_bc_irem");
  mov(bcp, reg(tmp2));
  // Remainder is in r0.  Quotient in r1
  prefetch(1);
  simple_c_store_result(T_INT);
  dispatch(tos_interpreter_basic);
#endif
}

void bc_idiv::generate() {
#if ENABLE_ARM_V7 && 0 // sdiv is not supported in ARMv7l
  pop_arguments(2);
  prefetch(1);
  sdiv_w(tos_val, tmp0, tos_val);
  dispatch(1);
#else
  // WARNING:  idiv_irem knows that the saved bcp is in tmp2
  // Every call to interpreter_idiv_irem must followed
  // by the global label which is checked in compiler_idiv_irem
  mov(tmp2, reg(bcp));
  call_from_interpreter("interpreter_idiv_irem", true);
bind("called_from_bc_idiv");
  mov(bcp, reg(tmp2));
  // Remainder is in r0.  Quotient in r1
  prefetch(1);
  mov(tos_val, reg(r1));
  simple_c_store_result(T_INT);
  dispatch(tos_interpreter_basic);
#endif
}

void bc_lrem::generate() {
  simple_c_setup_arguments(T_LONG, T_LONG);
  orr_w(tmp2, r2, reg(r3), lsl_shift, 0, set_CC);

  // b("long_divide_by_zero", eq); IMPL_NOTE: this doesn't work yet
  breakpoint(eq);

  fast_c_call("jvm_lrem");

  prefetch(1);
  simple_c_store_result(T_LONG);
  dispatch(tos_interpreter_basic);
}

void bc_ldiv::generate() {
  simple_c_setup_arguments(T_LONG, T_LONG);
  orr_w(tmp2, r2, reg(r3), lsl_shift, 0, set_CC);

  // b("long_divide_by_zero", eq); IMPL_NOTE: this doesn't work yet
  breakpoint(eq);

  fast_c_call("jvm_ldiv");

  prefetch(1);
  simple_c_store_result(T_LONG);
  dispatch(tos_interpreter_basic);

bind_local("long_divide_by_zero");
  Register A_1 = pair1_from_low_high(r2, r3);
  Register A_2 = pair2_from_low_high(r2, r3);
  Register B_1 = pair1_from_low_high(r0, r1);
  Register B_2 = pair2_from_low_high(r0, r1);
  push_results(4, (RegisterSet)set(A_1), (RegisterSet)set(A_2),
                  (RegisterSet)set(B_1), (RegisterSet)set(B_2));
  set_stack_state_to(tos_on_stack);
  interpreter_call_vm("division_by_zero_exception", T_ILLEGAL);
}

void bc_lmul::generate() {
  pop_arguments(4);
  Register A_MSW = msw_from_long_pair(tos_val, tmp0);
  Register A_LSW = lsw_from_long_pair(tos_val, tmp0);

  Register B_MSW = msw_from_long_pair(tmp2, tmp4);
  Register B_LSW = lsw_from_long_pair(tmp2, tmp4);

  Register R_MSW = msw_from_long_pair(tmp1, tmp3);
  Register R_LSW = lsw_from_long_pair(tmp1, tmp3);

  prefetch(1);
  // (xhi*B + xlo)*(yhi*B + ylo) = xhi*yhi*B^2 + xhi*yloB + xlo*yhi*B + xlo*ylo
  // ignore B^2 term (larger than 64 bit)
  umull_w(R_LSW, R_MSW, A_LSW, B_LSW       );
  mla_w  (       R_MSW, A_LSW, B_MSW, R_MSW);
  mla_w  (       R_MSW, A_MSW, B_LSW, R_MSW);

  dispatch(2, (RegisterSet)set(tmp1), (RegisterSet)set(tmp3));
}

void bc_lshl::generate() {
  pop_arguments(3);
  // These aliases make the code below easier to understand
  Register shift = tos_val;
  Register arg_msw = msw_from_long_pair(tmp0, tmp2);
  Register arg_lsw = lsw_from_long_pair(tmp0, tmp2);

  Register unshift = tos_tag;
  Register res_msw = msw_from_long_pair(tmp1, tmp3);
  Register res_lsw = lsw_from_long_pair(tmp1, tmp3);
  Label no_shift;

  comment("Normalize shift amount");
  and_imm12_w(shift, shift, imm12(63), set_CC);
  prefetch(1);
  it(eq, THEN_THEN);
  mov(res_msw, reg(arg_msw));
  mov(res_lsw, reg(arg_lsw));  
  b(no_shift);

  comment("Get long argument from the stack");
  comment("Calculate 32-shift");
  rsb_imm12_w(unshift, shift, imm12(32), set_CC);

  comment("condition code le means that shift is >= 32");
  it(le, THEN_THEN);
  sub(shift, shift, 32);
  lsl_w(res_msw, arg_lsw, shift);
  mov(res_lsw, 0);

  comment("condition code gt means that shift is < 32");
  it(gt, THEN_THEN_THEN);
  lsl_w(res_msw, arg_msw, shift);
  lsl_w(res_lsw, arg_lsw, shift);
  lsr(arg_lsw, unshift);
  orr_w(res_msw, res_msw, arg_lsw);

bind(no_shift);
  dispatch(2, (RegisterSet)set(tmp1), (RegisterSet)set(tmp3));
}

void bc_lshr::generate(bool is_signed) {
  pop_arguments(3);
  Register shift = tos_val;
  Register arg_msw = msw_from_long_pair(tmp0, tmp2);
  Register arg_lsw = lsw_from_long_pair(tmp0, tmp2);

  Register unshift = tos_tag;
  Register res_msw = msw_from_long_pair(tmp1, tmp3);
  Register res_lsw = lsw_from_long_pair(tmp1, tmp3);

  Shift shift_type = is_signed ? asr_shift : lsr_shift;
  Label no_shift;

  comment("Normalize shift amount");
  and_imm12_w(shift, shift, modified_imm12(63), set_CC);
  prefetch(1);
  it(eq, THEN_THEN);
  mov(res_msw, reg(arg_msw));
  mov(res_lsw, reg(arg_lsw));
  b(no_shift);

  comment("Calculate 32-shift");
  rsb_imm12_w(unshift, shift, modified_imm12(32), set_CC);

  comment("condition code le means that shift is >= 32");
  it(le, THEN_THEN);
  sub(shift, shift, 32);
  if (is_signed) {
    asr_w(res_lsw, arg_msw, shift);
    mov_w(res_msw, arg_msw, asr_shift, 31);
  } else {
    lsr_w(res_lsw, arg_msw, shift);
    mov_imm12_w(res_msw, modified_imm12(0));
  }

  comment("condition code gt means that shift is < 32");
  it(gt, THEN_THEN_THEN);
  lsr_w(res_lsw, arg_lsw, shift);
  lsl_w(unshift, arg_msw, unshift);
  orr(res_lsw, unshift);
  if (is_signed) {
    asr_w(res_msw, arg_msw, shift);
  } else {
    lsr_w(res_msw, arg_msw, shift);
  }

bind(no_shift);
  dispatch(2, (RegisterSet)set(tmp1), (RegisterSet)set(tmp3));
}

static Assembler::Condition as_ARM_cond(j_condition j_cond) {
  switch (j_cond) {
    case j_equal        : return Assembler::eq;
    case j_not_equal    : return Assembler::ne;
    case j_less         : return Assembler::lt;
    case j_less_equal   : return Assembler::le;
    case j_greater      : return Assembler::gt;
    case j_greater_equal: return Assembler::ge;
  }
  return Assembler::nv;
}

void bc_if_0cmp::generate(j_condition condition) {
  check_timer_tick(tos_interpreter_basic);

  pop_arguments(1);
  Label not_taken;
  cmp(tos_val, zero);
  b(not_taken, not_cond(as_ARM_cond(condition)));
  branch(false, false);
  bind(not_taken);
  prefetch(3);
  dispatch(0);
}

void bc_if_icmp::generate(j_condition condition) {
  check_timer_tick(tos_interpreter_basic);

  pop_arguments(2);
  Label not_taken;
  cmp(tmp0, tos_val);
  b(not_taken, not_cond(as_ARM_cond(condition)));
  branch(false, false);
  bind(not_taken);
  prefetch(3);
  dispatch(0);
}

void bc_if_acmp::generate(j_condition condition) {
  check_timer_tick(tos_interpreter_basic);

  pop_arguments(2);
  Label not_taken;
  cmp(tmp0, tos_val);
  b(not_taken, not_cond(as_ARM_cond(condition)));
  branch(false, false);
  bind(not_taken);
  prefetch(3);
  dispatch(0);
}

void bc_if_nullcmp::generate(j_condition condition) {
  check_timer_tick(tos_interpreter_basic);

  pop_arguments(1);
  Label not_taken;
  cmp(tos_val, 0);
  b(not_taken, not_cond(as_ARM_cond(condition)));
  branch(false, false);
  bind(not_taken);
  prefetch(3);
  dispatch(0);
}

// Branch routines.
void bc_goto::generate(bool is_wide) {
  check_timer_tick(tos_interpreter_basic);
  pop_arguments(0);
  branch(false, is_wide);
}

void bc_return::generate(BasicType type) {
  unlock_activation();
  if (type == T_VOID) { 
    if (sp == jsp) {
     set_stack_state_to(tos_on_stack);
    }
  } else { 
    // This gets the result into r0/r1.  
    simple_c_setup_arguments(type);
  }
  if (GenerateDebugAssembly) { 
    ldr(tmp0, imm_index(fp, JavaFrame::locals_pointer_offset()));
    cmp(tmp0, reg(locals));
    breakpoint(ne);
  }
  ldr(lr,     imm_index(fp, JavaFrame::return_address_offset()));
  ldr(fp,     imm_index(fp, JavaFrame::caller_fp_offset()));
  sub(jsp, locals, JavaFrame::arg_offset_from_sp(-1));
  set_return_type(type);
  bx(lr);
}

void bc_tableswitch::generate() {
  // branch table can have negative offsets
  check_timer_tick(tos_interpreter_basic);
  pop_arguments(1);

  comment("Have tos_tag point to lower limit");
  add(tos_tag, bcp, imm(2 * BytesPerWord));
  bic_imm12_w(tos_tag, tos_tag, imm12(BytesPerWord - 1));

  if (!ENABLE_NATIVE_ORDER_REWRITING && HARDWARE_LITTLE_ENDIAN) { 
    swap_mask(tmp0);
  }

  comment("tmp2 gets the lower limit");
  comment("tmp3 gets the upper limit");
  comment("tos_tag is updated to point to the start of the offsets");
  // IMPL_NOTE: replace with ldmia_w(tos_tag, set(tmp2, tmp3), writeback);
  Macros::ldr(tmp2, tos_tag, PostIndex(4));
  Macros::ldr(tmp3, tos_tag, PostIndex(4));

  if (!ENABLE_NATIVE_ORDER_REWRITING && HARDWARE_LITTLE_ENDIAN) { 
    swap_bytes(tmp2, tmp1, tmp0);
    swap_bytes(tmp3, tmp1, tmp0);
  }

  cmp(tmp3, reg(tos_val));
  it(ge);
  sub(tos_val, tos_val, tmp2, lsl_shift, 0, set_CC);
  comment("Offset of branch target (swapped?) if value is in the table");
  it(ge);
  ldr_w(tmp1, tos_tag, tos_val, 2);
  comment("Offset of branch target (swapped?) if value is outside of table");
  it(lt);
  ldr(tmp1, imm_index(tos_tag, -3 * BytesPerWord));
  if (!ENABLE_NATIVE_ORDER_REWRITING && HARDWARE_LITTLE_ENDIAN) { 
    swap_bytes(tmp1, tmp2, tmp0);
  }
  prefetch(tmp1);
  dispatch(0);
}

void bc_lookupswitch::generate() {
  // branch table can have negative offsets
  check_timer_tick(tos_interpreter_basic);
  pop_arguments(1);
  // linear search - we may want to choose between linear search
  // and binary search depending on the table size, eventually.

  if (!ENABLE_NATIVE_ORDER_REWRITING && HARDWARE_LITTLE_ENDIAN) {
    swap_mask(tos_tag);
    swap_bytes(tos_val, tmp2, tos_tag);
  }

  comment("compute aligned table ptr");
  bic_imm12_w(tmp0, bcp, imm12(BytesPerWord - 1));

  comment("get number of table entries + 1");
  ldr(tmp1, imm_index(tmp0, 2*BytesPerWord));
  if (!ENABLE_NATIVE_ORDER_REWRITING && HARDWARE_LITTLE_ENDIAN) { 
    swap_bytes(tmp1, tmp2, tos_tag);
  }
  add(tmp1, tmp1, one);
  if (GenerateDebugAssembly) {
    cmp(tmp1, zero);
    report_fatal("number of entries + 1 must be > 0", ls);
  }

  comment("set table ptr to default entry");
  add(tmp0, tmp0, imm(BytesPerWord));

  // The following loop is optimized for compactness/speed:
  // In order to avoid extra branches/tests, the key check
  // and end of loop test are combined. Also, there is al-
  // ways an extra iteration (i.e. one word access beyond
  // the table) in the (hopefully) uncommon case that the
  // key is not found in the table.
  comment("search the table");
  { Label loop;
    bind(loop);
    Macros::ldr(tmp2, tmp0, PreIndex(2*BytesPerWord));
    sub(tmp1, tmp1, imm12(1), set_CC);
    it(ne);
    cmp(tos_val, reg(tmp2));
    b(loop, ne);
  }

  comment("reset table ptr if not found");
  cmp(tmp1, zero);
  it(eq);
  bic_imm12_w(tmp0, bcp, imm12(BytesPerWord - 1));

  comment("get table entry or default offset");
  ldr(tmp1, imm_index(tmp0, BytesPerWord));
  if (!ENABLE_NATIVE_ORDER_REWRITING && HARDWARE_LITTLE_ENDIAN) { 
    swap_bytes(tmp1, tmp2, tos_tag);
  }

  comment("get bytecode at destination");
  add(bcp, bcp, tmp1);
  ldrb_imm12_w(bcode, bcp);
  dispatch(0);
}

void bc_i2l::generate() {
  pop_arguments(1);
  prefetch(1);

  Register R_MSW = msw_from_long_pair(tmp0, tmp2);
  Register R_LSW = lsw_from_long_pair(tmp0, tmp2);

  mov_w(R_MSW, imm_shift(tos_val, asr, BitsPerWord - 1)); 
  if (R_LSW == tmp0) {
    dispatch(2, tos, tmp23);
  } else { 
    dispatch(2, tmp01, tos);
  }
}

void bc_i2b::generate() {
  prefetch(1);
  pop_arguments(1);
  // IMPL_NOTE: replace with one 32bit instruction
  mov_w(tos_val, imm_shift(tos_val, lsl, BitsPerWord - BitsPerByte));
  mov_w(tos_val, imm_shift(tos_val, asr, BitsPerWord - BitsPerByte));
  dispatch(1);
}

void bc_i2c::generate() {
  prefetch(1);
  pop_arguments(1);
  // IMPL_NOTE: replace with one 32bit instruction
  mov_w(tos_val, imm_shift(tos_val, lsl, BitsPerWord - BitsPerShort));
  mov_w(tos_val, imm_shift(tos_val, lsr, BitsPerWord - BitsPerShort));
  dispatch(1);
}

void bc_i2s::generate() {
  prefetch(1);
  pop_arguments(1);
  // // IMPL_NOTE: replace with one 32bit instruction
  mov_w(tos_val, imm_shift(tos_val, lsl, BitsPerWord - BitsPerShort));
  mov_w(tos_val, imm_shift(tos_val, asr, BitsPerWord - BitsPerShort));
  dispatch(1);
}

void bc_l2i::generate() {
  Register A_MSW = msw_from_long_pair(tos_val, tmp0);
  Register A_LSW = lsw_from_long_pair(tos_val, tmp0);

  if (A_LSW == tos_val) { 
    pop_arguments(1);
    prefetch(1);
    sub(jsp, jsp, JavaStackDirection * BytesPerStackElement);
    dispatch(1);
  } else { 
    comment("This is a pop, with a change of tag");
    pop_arguments(-1);
    prefetch(1);
    dispatch(0);
  }
}    

void bc_athrow::generate() {
  set_stack_state_to(tos_in_regs);
  null_check(tos_val);
  save_interpreter_state();
  mov(r1, reg(r0));
  call_from_interpreter("interpreter_call_vm_exception", true);
}

void bc_new::generate() {
  set_stack_state_to(tos_on_stack);
  interpreter_call_vm("newobject", T_OBJECT);
  prefetch(3);
  dispatch(tos_in_regs);
}

void bc_anewarray::generate() {
  set_stack_state_to(tos_on_stack);
  interpreter_call_vm("anewarray", T_ARRAY);
  prefetch(3);
  sub(jsp, jsp, JavaStackDirection * BytesPerStackElement);
  dispatch(tos_in_regs);
}

void bc_multianewarray::generate() {
  set_stack_state_to(tos_on_stack);
  interpreter_call_vm("multianewarray", T_ARRAY);
  ldrb_at_bcp(tmp0, 3); // get dimensions
  // remove parameters
  prefetch(4);
  if (JavaStackDirection < 0) {
    add(jsp, jsp, imm_shift(tmp0, lsl, LogBytesPerStackElement));
  } else {
    sub(jsp, jsp, imm_shift(tmp0, lsl, LogBytesPerStackElement));
  }

  dispatch(tos_in_regs);
}

void bc_arraylength::generate() {
  Label npe;
  pop_arguments(1);
  cmp(tos_val, zero);
  b(npe, eq);
  prefetch(1);
  ldr(tos_val, imm_index(tos_val, Array::length_offset()));
  dispatch(1);
bind(npe);
  push_results(1); 
  set_stack_state_to(tos_on_stack);
  ldr_using_gp(pc, "interpreter_throw_NullPointerException");
}

void bc_monitorenter::generate() {
  set_stack_state_to(tos_in_regs);
  null_check(tos_val);

  add(bcp, bcp, 1);
  save_interpreter_state();
  call_from_interpreter("shared_monitor_enter", false);
  restore_interpreter_state();
  comment("Dispatch to the next bytecode");
  prefetch(0);
  dispatch(tos_on_stack);
}

void bc_monitorexit::generate() {
  set_stack_state_to(tos_in_regs);
  // object to lock is in tos_val
  null_check(tos_val);

  save_interpreter_state();
  call_from_interpreter("shared_monitor_exit", false);
  restore_interpreter_state();

  prefetch(1);
  dispatch(tos_on_stack);
}

void bc_instanceof::generate() {
  set_stack_state_to(tos_on_stack);
  interpreter_call_vm("instanceof", T_INT);
  sub(jsp, jsp, JavaStackDirection * BytesPerStackElement);
  prefetch(3);
  mov(tos_tag, int_tag);
  dispatch(tos_in_regs);
}

void bc_checkcast::generate() {
  set_stack_state_to(tos_on_stack);
  interpreter_call_vm("checkcast", T_VOID);
  prefetch(3);
  dispatch(tos_on_stack);
}

void bc_wide::generate() {
  breakpoint();
#if 0 // BEGIN_CONVERT_TO_T2
  ldrb_at_bcp(bcode, 1);
  ldr(tmp1, bytecode_impl(bcode));
  ldr(tmp2, imm_index(tmp1, -BytesPerWord));
  mov(pc, reg(tmp2));
#endif // END_CONVERT_TO_T2
}

void bc_fast_ldc::generate(BasicType type, bool is_wide) {
  int prefetch_step = is_wide ? 3 : 2;

  pop_arguments(0);

  switch (type) {
  case T_INT    :
  case T_FLOAT  :
  case T_OBJECT :
    ldr_cp_entry_A(tos_val, tmp0, addr_dont_care, is_wide ? 2 : 1);
    prefetch(prefetch_step);
    ldr_cp_entry_B(tos_val, tmp0, addr_dont_care, is_wide ? 2 : 1);
    break;
  case T_LONG   :
  case T_DOUBLE : {
    Register A_LOW  = low_from_pair(tos_val, tmp0);
    Register A_HIGH = high_from_pair(tos_val, tmp0);
    GUARANTEE(is_wide, "No short load of long or double");
    // A_LOW gets the value, tmp0 gets its address
    ldr_cp_entry_A(A_LOW, tmp3, addr_cpool_index, 2);
    prefetch(prefetch_step);
    ldr_cp_entry_B(A_LOW, tmp3, addr_cpool_index, 2);
    ldr(A_HIGH, imm_index(tmp3, BytesPerWord));
    break;
  }
  default:
    SHOULD_NOT_REACH_HERE();
  }
  dispatch(word_size_for(type));
}

void bc_fast_putstatic::generate(BasicType type, bool with_init) {
  fast_access_field(true, true, type, 2, with_init);
}

void bc_fast_getstatic::generate(BasicType type, bool with_init) {
  fast_access_field(true, false, type, 2, with_init);
}

void bc_fast_putfield::generate(BasicType type) {
  fast_access_field(false, true, type, 2);
}

void bc_fast_getfield::generate(BasicType type, int param_bytes) {
  fast_access_field(false, false, type, param_bytes);
}

void bc_aload_0_fast_getfield_1::generate(BasicType type) {
  fast_access_field(false, false, type, 1, true);
}

void bc_aload_0_fast_getfield_n::generate(BasicType type, int offset) {
  fast_access_field(false, false, type, 0, true, offset);
}

void bc_fast_invokespecial::generate() {
  set_stack_state_to(tos_on_stack);

  save_interpreter_state();

  // get the cpool entry
  ldr_cp_entry_A(tmp3, tmp1);
  get_class_list_base(tmp2);
  ldr_cp_entry_B(tmp3, tmp1);

  // get vtable index (bits [0..15]) of the cpool entry
  eol_comment("vtable index is low 16 bits of the cpool entry");
  mov_w(tmp4, tmp3, lsl_shift, 16);
  mov_w(tmp4, tmp4, lsr_shift, 16);

  // get class id (bits [16..31]) of the cpool entry
  eol_comment("class id is high 16 bits of the cpool entry");
  mov_w(tmp3, tmp3, asr_shift, 16);

  // tmp2:  class_list_base
  // tmp3 : class id
  // tmp4 : vtable index

  // get the class with the class_id
  ldr_class_from_index_and_base(tmp3, tmp3, tmp2);

  // tmp3 : fixed cpool class
  // tmp4 : vtable index of method to call

  comment("Get ClassInfo from class");
  ldr(tmp3, imm_index(tmp3, JavaClass::class_info_offset()));

  // tmp3 : ClassInfo
  // tmp4 : vtable index of method to call
  comment("Get method from vtable of the ClassInfo");
  ldr_indexed(tos_val, tmp3, tmp4, times_4, ClassInfoDesc::header_size());

  comment("Get the number of parameters");
  get_method_parameter_size(tmp2, tos_val);

  // tmp2   : number of parameters
  // tos_val : fixed cpool method to call

  comment("Get receiver object");
  ldr_receiver(tmp3, tmp2);
  null_check(tmp3, false);

  // tos_val : fixed cpool method to call

  comment("Call method");
  ldr(tmp0, imm_index(tos_val, Method::variable_part_offset()));
  ldr(tmp0, imm_index(tmp0, 0));

  invoke_method(tos_val, tmp0, tmp2, 3, "invoke3_deoptimization_entry");
}

void bc_fast_invoke::generate(bool has_fixed_target_method) {
  set_stack_state_to(tos_on_stack);
  Bytecodes::Code bc = bytecode();
  bool must_do_null_check = (bc != Bytecodes::_fast_invokestatic &&
                             bc != Bytecodes::_fast_init_invokestatic);

  save_interpreter_state();

  if (has_fixed_target_method) {
    // load the fixed target method from the constant pool
    ldr_cp_entry_A(tos_val, tmp0);
    ldr_cp_entry_B(tos_val, tmp0);
    if (must_do_null_check) { 
      eol_comment("number of parameters");
      get_method_parameter_size(tmp2, tos_val);
      eol_comment("get receiver object");
      ldr_receiver(tmp3, tmp2);
      // tmp2: number of parameters
      null_check(tmp3, false);
#if ENABLE_ISOLATES
    } else {
      Label class_is_initialized, need_init;
      // Get task mirror for the current class
      // First, load offset to appropriate  entries of task mirror table
      comment("get holder of this method");
      get_class_list_base(tmp1);
      ldrh(tmp0, tos_val, Method::holder_id_offset());
      ldr_class_from_index_and_base(tmp0, tmp0, tmp1);
      get_mirror_list_base(tmp1);
      ldrh(tmp2, tos_val, Method::holder_id_offset());
#if ENABLE_ARM_V7
      Assembler::ldr(tmp1, tmp1, reg(tmp2));
#else
      ldr_w(tmp1, tmp1, tmp2, times_4);
#endif
      cmp(tmp1, zero);
      comment("It will almost never be zero, so preload tmp2");
      get_task_class_init_marker(tmp2);
      b(need_init, eq);  
      cmp(tmp1, reg(tmp2));
      b(class_is_initialized, ne);
bind(need_init);
      comment("move instanceclass obj into extra arg");
      mov(r1, reg(tmp0));
      interpreter_call_vm("task_barrier", T_OBJECT);
      comment("reload registers");
      ldr_cp_entry_A(tos_val, tmp0);
      ldr_cp_entry_B(tos_val, tmp0);
bind(class_is_initialized);
#endif
    }
  } else {
    // get the cpool entry
    ldr_cp_entry_A(tmp3, tmp1);
    get_class_list_base(tmp2);
    ldr_cp_entry_B(tmp3, tmp1);
    eol_comment("vtable index is low bits of cpool entry");
    mov_w(tmp4, tmp3, lsl_shift, 16);
    mov_w(tmp4, tmp4, lsr_shift, 16);

    eol_comment("class id is high bits of cpool entry");
    mov_w(tmp3, tmp3, asr_shift, 16);

    // tmp2 : class list base
    // tmp3 : class id
    // tmp4 : vtable index

    // get the class with the given class id
    ldr_class_from_index_and_base(tmp3, tmp3, tmp2);

    // tmp3 : fixed cpool class
    // tmp4 : vtable index of method

    comment("Get fixed method from vtable of the class");
    ldr(tmp3, imm_index(tmp3, JavaClass::class_info_offset()));
    ldr_indexed(tmp1, tmp3, tmp4, times_4, ClassInfoDesc::header_size());

    // tmp1 : fixed cpool method
    // tmp4 : vtable index of method

    comment("Get the number of parameters");
    get_method_parameter_size(tmp2, tmp1);

    // tmp2 : number of parameters
    // tmp4 : vtable index of method

    comment("Get receiver object");
    ldr_receiver(tmp3, tmp2);

    comment("Check if it is null");
    null_check(tmp3, false);

    comment("Get java near");
    ldr(tmp3, imm_index(tmp3, 0));

    comment("Get classinfo of receiver object");
    ldr(tmp3, imm_index(tmp3, JavaNear::class_info_offset()));

    comment("Get method from vtable of the class");
    ldr_indexed(tos_val, tmp3, tmp4, times_4, ClassInfoDesc::header_size());
  }

  comment("Call method");
  // tos_val must contain the method
  ldr(tmp0, imm_index(tos_val, Method::variable_part_offset()));
  ldr(tmp0, imm_index(tmp0, 0));
  invoke_method(tos_val, tmp0, tmp2, 3, NULL);
}

 void bc_fast_invokeinterface::generate() {
  set_stack_state_to(tos_on_stack);

  // tos registers are free now
  Register tmp6 = tos_val;
  Register tmp7 = tos_tag;

  eol_comment("get bytes of constant pool index");
  ldrh_at_bcp_A(tmp0, tmp3, 1);

  eol_comment("get count");
  ldrb_at_bcp(tmp1, 3);

  eol_comment("combine bytes");
  ldrh_at_bcp_B(tmp0, tmp3, 1);

  save_interpreter_state();

  // tmp0 = constant pool index for class id and itable index.
  // tmp1 = numbers of parameters.

  comment("Get the class id from resolved constant pool entry");
  ldr_indexed(tmp2, cpool, tmp0, times_4, 0);

  // tmp1 = numbers of parameters.
  // tmp2 = (class_id << 16) + method_table_index

  comment("Get receiver object");
  ldr_receiver(tmp6, tmp1);
  mov_w(tmp3, tmp2, lsr_shift, 16);
  null_check(tmp6, false);

  // tmp2 = (class_id << 16) + method_table_index
  // tmp3 = class_id of interface
  // tmp6 = receiver object.

  comment("Get class of receiver object");
  ldr(tmp6, imm_index(tmp6, 0));
  sub(tmp2, tmp2, imm_shift(tmp3, lsl, 16));
  ldr(tmp6, imm_index(tmp6, 0));

  // tmp2 = method_table_index
  // tmp3 = class_id of interface.
  // tmp6 = receiver class.

  comment("Get ClassInfo of receiver class");
  ldr(tmp6, imm_index(tmp6, JavaClass::class_info_offset()));

  // tmp2 = method_table_index
  // tmp3 = class id of interface.
  // tmp6 = receiver classInfo.

  comment("Get the itable from the ClassInfo of the receiver object");
  ldrh_imm12_w(tmp1, tmp6, ClassInfo::vtable_length_offset());
  ldrh_imm12_w(tmp0, tmp6, ClassInfo::itable_length_offset());
  // tmp7 = ClassInfo + header + vtable_length << 2
  add(tmp7, tmp6, ClassInfoDesc::header_size() - 2 * BytesPerWord);
  add(tmp7, tmp7, imm_shift(tmp1, lsl, times_4));

  // tmp0 = itable_length
  // tmp2 = method_table_index
  // tmp3 = class_id of interface.
  // tmp6 = receiver classInfo.
  // tmp7 = start of itable.

  Label lookup;
  comment("Lookup interface method table by linear search");
bind(lookup);
  sub(tmp0, tmp0, imm12(one), set_CC); 
  it(ge); Macros::ldr(tmp4, tmp7, PreIndex(2 * BytesPerWord));
  it(lt); ldr_using_gp(pc, "interpreter_throw_IncompatibleClassChangeError");

  cmp(tmp3, reg(tmp4));
  b(lookup, ne);

  // tmp2 = method_table_index
  // tmp6 = receiver ClassInfo.
  // tmp7 = found itable entry.

  comment("Found the itable entry; now get the method table offset from there");
  ldr(tmp1, imm_index(tmp7, BytesPerWord));
  // tmp1 = offset of method table (relative to receiver ClassInfo).
  // tmp2 = method_table_index
  // tmp6 = receiver ClassInfo.

  comment("Get the method from the method table");
  add(tos_val, tmp6, imm_shift(tmp2, lsl, times_4));
  ldr_w(tos_val, tos_val, tmp1, 0);

  comment("Call method");
  // tos_val must contain the method
  ldr(tmp0, imm_index(tos_val, Method::variable_part_offset()));
  ldr(tmp0, imm_index(tmp0, 0));
  invoke_method(tos_val, tmp0, tmp2, 5, "invoke5_deoptimization_entry");
}

void bc_fast_invokenative::generate() {
  set_stack_state_to(tos_on_stack);
  Label redo;

#if ENABLE_PROFILER
  if (UseProfiler) {
    comment("Inform Profiler we're in native method");

    mov(tmp1, 1);
    ldr_label(tmp0, "_jvm_profiler_in_native_method");
    str(tmp1, imm_index(tmp0, 0));
  }
#endif

bind(redo);

#if ENABLE_TTY_TRACE
  if (GenerateDebugAssembly) {
    // This is ugly. We can't use shared_call_vm, not because there's
    // anything wrong with using it here, but because we can't use it
    // for quick_native entries. See generate_quick_native_method_entry()
    Label skip;

    ldr_label(tmp0, "TraceNativeCalls");
    ldr(tmp0, imm_index(tmp0, 0));
    cmp(tmp0, zero);
    b(skip, eq);

    ldr(tmp1, imm_index(bcp, Method::native_code_offset_from_bcp()));
    ldr_label(tmp0, "_current_native_function");
    str(tmp1, imm_index(tmp0, 0));

    save_interpreter_state();
    ldr_label(r0, "trace_native_call");
    bl("call_on_primordial_stack");   
    restore_interpreter_state();
  bind(skip);
  }
#endif //ENABLE_TTY_TRACE

  // tmp1 = addr of first param
  comment("Get address of first paramater");
  get_method(tmp0);
  ldrh_imm12_w(tmp0, imm_index(tmp0, Method::access_flags_offset()));
  mov(tmp1, reg(locals));
  ldrb_at_bcp(tmp2, 1);         // the kind

  comment("Set space for fake parameter for static method (KNI-ism)");
  tst(tmp0, JVM_ACC_STATIC);
  it(ne); sub(tmp1, tmp1, JavaStackDirection * BytesPerStackElement);
  comment("Point _kni_parameter_base to the first parameter");
  set_kni_parameter_base(tmp1);

  // The redo is necessary for implementing async native methods for
  // LWT. If a native method (e.g., socket.read()) must block, the
  // method calls Scheduler::set_thread_blocked(), which sets
  // Thread.async_redo to true. The native method then returns
  // immediately.
  //
  // We'll come to the check of Thread.async_redo below when the
  // native methods becomes ready for execution (by code inside
  // Scheduler::check_async_threads()). We call the native method
  // again and then return normally.
  //
  // If a method is redone, any previous return values are ignored.

  eol_comment("get the pointer from the bytecode");
  ldr_imm12_w(tmp1, imm_index(bcp, Method::native_code_offset_from_bcp()));

  comment("call the native code");
  save_interpreter_state();
  cmp(tmp2, T_OBJECT);
  it(eq, ELSE);
  ldr_using_gp(tmp2, "shared_call_vm_oop");
  ldr_using_gp(tmp2, "shared_call_vm");
  blx(tmp2);

  restore_interpreter_state();

  get_thread(tmp1);
  ldr(tmp2, imm_index(tmp1, Thread::async_redo_offset()));
  comment("Clear Thread.async_redo so that we won't loop indefinitely");
  mov_imm12_w(tmp3, imm12(zero));
  str(tmp3, imm_index(tmp1, Thread::async_redo_offset()));

  comment("loop if need to redo the native method invocation");
  cmp(tmp2, imm12(zero));
  b(redo, ne);

#if ENABLE_PROFILER
  if (UseProfiler) {
    comment("Inform Profiler we're out of native method");

    mov(tmp1, 0);
    ldr_label(tmp0, "_jvm_profiler_in_native_method");
    str(tmp1, imm_index(tmp0, 0));
  }
#endif

  eol_comment("Get kind");
  ldrb_at_bcp(tmp2, 1);   

  eol_comment("Clear Thread.async_info");
  str(tmp3, imm_index(tmp1, Thread::async_info_offset()));

  // This is a bit ugly.  We want to make the stack look like we have just
  // returned from a simple c function.  This means that the stack is
  // appropriately full or empty.
  if (!ENABLE_FULL_STACK) {
    add(jsp, jsp, JavaStackDirection * BytesPerWord);
  } 

  Label native_int, native_long, native_void, native_object, native_illegal;
  Label native_float, native_double;

#if 1
  comment("IMPL_NOTE: change this to a proper table switch");
  cmp(tmp2, T_INT);    b(native_int,    eq);
  cmp(tmp2, T_LONG);   b(native_long,   eq);
  cmp(tmp2, T_VOID);   b(native_void,   eq);
  cmp(tmp2, T_OBJECT); b(native_object, eq);
  cmp(tmp2, T_FLOAT);  b(native_float,  eq);
  cmp(tmp2, T_DOUBLE); b(native_double, eq);
  breakpoint();
#else
  sub(tmp2, tmp2, 6);
  ldr_w(tmp2, pc, tmp2, /*shift=*/2);
  mov(pc, reg(tmp2));

  // Build a table with entries from 6 to 14.  This switch lets us build the
  // table without actually knowing what goes where.
  for (int i = 6; i <= 14; i++) {
    switch(i) {
      case T_INT:     define_long(native_int);    break;
      case T_LONG:    define_long(native_long);   break;
      case T_VOID:    define_long(native_void);   break;
      case T_OBJECT:  define_long(native_object); break;
      case T_FLOAT:   define_long(native_float);  break;
      case T_DOUBLE:  define_long(native_double); break;
      default:        define_long(native_illegal);break;
    }
  }
#endif

  bind(native_int);
    simple_c_store_result(T_INT);
    ldr_label(tmp0, "bc_impl_ireturn_internal", false);

  bind(native_long);
    simple_c_store_result(T_LONG);
    ldr_label(tmp0, "bc_impl_lreturn_internal", false);
    mov(pc, tmp0);

  bind(native_object);
    simple_c_store_result(T_OBJECT);
    ldr_label(tmp0, "bc_impl_areturn_internal", false);
    mov(pc, tmp0);

  bind(native_void);
    simple_c_store_result(T_VOID);
    ldr_label(tmp0, "bc_impl_return_internal", false);
    mov(pc, tmp0);

#if ENABLE_FLOAT
  bind(native_float);
    simple_c_store_result(T_FLOAT);
    ldr_label(tmp0, "bc_impl_freturn_internal", false);
    mov(pc, tmp0);

  bind(native_double);
    simple_c_store_result(T_DOUBLE);
    ldr_label(tmp0, "bc_impl_dreturn_internal", false);
    mov(pc, tmp0);
#endif

#if !ENABLE_FLOAT
  bind(native_float);
  bind(native_double);
#endif
  bind(native_illegal);
  breakpoint();
}

void bc_fast_new::generate() {
  set_stack_state_to(tos_on_stack);
  Assembler::Register  result   = tos_val;
  Assembler::Register  size     = tos_tag;
  Assembler::Register  class_id = tos_tag; // caution overlap with previous reg
  Assembler::Register  clazz    = tmp0;

  Label slow_case;

  ldr_cp_entry_A(class_id, tmp2);
  get_class_list_base(tmp1);
  ldr_cp_entry_B(class_id, tmp2);
  ldr_class_from_index_and_base(clazz, class_id, tmp1);
#if ENABLE_ISOLATES
  Label class_is_initialized, need_init;
  get_mirror_list_base(tmp2);
#if ENABLE_ARM_V7
  Assembler::ldr(tmp1, tmp2, reg(class_id));
#else
  ldr_w(tmp1, tmp2, class_id, times_4);
#endif
  get_task_class_init_marker(tmp2);
  cmp(tmp1, reg(tmp2));
  b(class_is_initialized, ne);
bind(need_init);
  comment("move instanceclass obj into extra arg");
  mov(r1, reg(clazz));
  interpreter_call_vm("task_barrier", T_OBJECT);
  comment("reload registers");
  ldr_cp_entry_A(clazz, tmp2);
  get_class_list_base(tmp1);
  ldr_cp_entry_B(clazz, tmp2);
  ldr_class_from_index_and_base(clazz, clazz, tmp1);
bind(class_is_initialized);
#endif

  comment("get _inline_allocation_top");
  get_inline_allocation_top(result);

  comment("get instance size (in bytes) from class");
  ldrsh_imm12_w(size, clazz, FarClass::instance_size_offset());

  comment("get _inline_allocation_end, and check for overflow");
  get_inline_allocation_end(tmp3);
  add(tmp2, result, size, lsl_shift, 0, set_CC);
  b(slow_case, cs);
  cmp(tmp2, tmp3);
  b(slow_case, hi);

  comment("allocation succeeded, set _inline_allocation_top");
  set_inline_allocation_top(tmp2);

  comment("get prototypical near from class");
  ldr(clazz, imm_index(clazz, FarClass::prototypical_near_offset()));

  prefetch(3);

  comment("set prototypical near in object; no need for write barrier");
  str(clazz, imm_index(result, 0));

  comment("initialize object fields to zero");
  zero_region(result, size, tmp2, tmp3, BytesPerWord, true, true);

  dispatch(tos_in_regs);

bind(slow_case);
  restore_stack_state_from(tos_on_stack);
  ldr_label(r3, "bc_impl_new_internal", false);
  bx(r3);
}

void bc_fast_checkcast::generate() {
  Label done, slow;
  pop_arguments(1);

  comment("Check for NULL object");
  cmp(tos_val, zero);
  b(done, eq);

  comment("Get the class for the object, and the constant pool entry");
  // interleaved to avoid pipeline stalling
  ldr(tmp0, imm_index(tos_val, 0));
  ldr_cp_entry_A(tmp1, tmp2);
  get_class_list_base(tmp3);
  ldr_cp_entry_B(tmp1, tmp2);
  ldr(tmp0, imm_index(tmp0, 0));
  ldr_class_from_index_and_base(tmp1, tmp1, tmp3);

  comment("Compare the classes");
  cmp(tmp1, reg(tmp0));

  comment("Check the subtype caches");
  it(ne, THEN_THEN_THEN);
  ldr(tmp2, imm_index(tmp0, JavaClass::subtype_cache_1_offset()));
  ldr(tmp3, imm_index(tmp0, JavaClass::subtype_cache_2_offset()));
  cmp(tmp1, reg(tmp2));
  cmp(tmp1, reg(tmp3));
  b(slow, ne);

bind(done);
  prefetch(3);
  dispatch(1);

bind(slow);
  push_results(1); 
  ldr_label(r3, "bc_impl_checkcast", false);
  bx(r3);
}

void bc_fast_instanceof::generate() {
  pop_arguments(1);

  Label done;
  Label slow;

  comment("Get the object to check cast for");

  comment("Check for NULL object");
  cmp(tos_val, zero);
  b(done, eq); // leave the object on TOS (if NULL return int(0))

  // tmp0: class of the object
  // tmp1: class to test against
  // tmp2: constant pool index
  // tos_val: object to test and result

  comment("Get the class for the object, and the constant pool entry");
  // interleaved to avoid pipeline stalling
  ldr(tmp0, imm_index(tos_val, 0));
  ldr_cp_entry_A(tmp1, tmp2);
  get_class_list_base(tmp3);
  ldr_cp_entry_B(tmp1, tmp2);
  ldr(tmp0, imm_index(tmp0, 0));
  ldr_class_from_index_and_base(tmp1, tmp1, tmp3);

  comment("Compare the classes");
  cmp(tmp1, reg(tmp0));

  comment("Check the subtype caches");
  it(ne, THEN_THEN_THEN);
  ldr(tmp2, imm_index(tmp0, JavaClass::subtype_cache_1_offset()));
  ldr(tmp3, imm_index(tmp0, JavaClass::subtype_cache_2_offset()));
  cmp(tmp1, reg(tmp2));
  cmp(tmp1, reg(tmp3));
  b(slow, ne);

  comment("Set the result to 1");
  mov(tos_val, one);

bind(done);
  prefetch(3);
  dispatch(1);

bind(slow);
  push_results(1); 
  ldr_label(r3, "bc_impl_instanceof", false);
  bx(r3);
}

void bc_newarray::generate() {
  pop_arguments(1);

  Label slow_case;

  Assembler::Register  result = tos_val;
  Assembler::Register  size   = tos_tag;
  Assembler::Register  length = tmp0;
  Assembler::Register  tag    = tmp1;  // make sure tag and clazz don't overlap
  Assembler::Register  clazz  = tmp1;

  mov(length, reg(tos_val));

  comment("get array type");
  ldrb_at_bcp(tag, 1);

  comment("Handle slowly if length too large or negative");
  cmp(length, maximum_safe_array_length);
  b(slow_case, hi);

  comment("compute array size in words");
  and_imm12_w(size, tag, imm12(3));
  lsl_w(size, length, size);
  add(size, size, Array::base_offset() + BytesPerWord - 1);
  lsr_imm5(size, size, LogBytesPerWord);

  comment("get _inline_allocation_top");
  get_inline_allocation_top(result);

  comment("get _inline_allocation_end, and check for overflow");
  get_inline_allocation_end(tmp3);
  add(tmp2, result, imm_shift(size, lsl, LogBytesPerWord), set_CC);
  b(slow_case, cs);
  cmp_w(tmp2, tmp3);
  b(slow_case, hi);

#if ENABLE_ARM_V7
  ldr_label_offset(tmp3, "persistent_handles",
            (Universe::bool_array_class_index - T_BOOLEAN) * BytesPerWord);
#else
  get_persistent_handles_addr(tmp3);
  add(tmp3, tmp3, 
        imm12((Universe::bool_array_class_index - T_BOOLEAN) * BytesPerWord));
#endif

  comment("allocation succeeded, set _inline_allocation_top");
  set_inline_allocation_top(tmp2);

  comment("Get array class");
  //ldr(clazz, add_index(tmp3, tag, lsl, LogBytesPerWord));
  add(clazz, tmp3, tag, lsl_shift, LogBytesPerWord);
  ldr_imm12_w(clazz, clazz);

  comment("set array length");
  str_imm12_w(length, imm_index(result, Array::length_offset()));

  comment("Get array class near");
  ldr_imm12_w(clazz, imm_index(clazz, FarClass::prototypical_near_offset()));

  prefetch(2);

  comment("initialize array elements to zero");
  zero_region(result, size, tmp2, tmp3, Array::base_offset(), true, false);

  comment("set prototypical near in array; no need for write barrier");
  str_imm12_w(clazz, imm_index(tos_val, Oop::klass_offset()));

  dispatch(1);

bind(slow_case);
  // _newarray(Thread*, tag, length)
  if (!ENABLE_FULL_STACK) {  
    sub(jsp, jsp, JavaStackDirection* BytesPerWord);
  }
  GUARANTEE(length != r1, "Register clash");
  mov_w(r1, tag);
  mov_w(r2, length);
  interpreter_call_vm("_newarray", T_ARRAY);
  if (!ENABLE_FULL_STACK) {  
    add(jsp, jsp, JavaStackDirection* BytesPerWord);
  }
  prefetch(2);
  dispatch(1);
}

void bc_fast_anewarray::generate() {
  pop_arguments(1);

  Label slow_case;
  Assembler::Register  result = tos_val;
  Assembler::Register  size   = tos_tag;
  Assembler::Register  length = tmp0;
  Assembler::Register  clazz  = tmp1;

  mov(length, reg(tos_val));

  comment("go slow case if array length is larger than maximum safe array length or below zero");
  cmp(length, maximum_safe_array_length);
  b(slow_case, hi);

  comment("compute array size in bytes");  
  mov_w(size, length, lsl_shift, LogBytesPerWord);
  add(size, size, imm(Array::base_offset()));

  comment("get _inline_allocation_top");
  get_inline_allocation_top(result);

  comment("get _inline_allocation_end, and check for overflow");
  get_inline_allocation_end(tmp3);
  add(tmp2, result, reg(size), lsl_shift, 0, set_CC);
  b(slow_case, cs);
  cmp(tmp2, reg(tmp3));
  b(slow_case, hi);

  comment("allocation succeeded, set _inline_allocation_top");
  set_inline_allocation_top(tmp2);

  comment("get constant pool entry");
  ldr_cp_entry_A(clazz, tmp2);
  ldr_cp_entry_B(clazz, tmp2);
#if ENABLE_ISOLATES
  Label done;
  comment("If arrayclass pointer is non-zero we're done");
  get_class_list_base(tmp3);
  ldr_class_from_index_and_base(tmp2, clazz, tmp3);
  ldr(tmp2, tmp2, JavaClass::array_class_offset());
  cmp(tmp2, zero);
  it(ne);
  mov(clazz, reg(tmp2));
  b(done, ne);
  comment("get task mirror");
  get_mirror_list_base(tmp3);
  ldr_class_from_index_and_base(clazz, clazz, tmp3);
  comment("get array class from class");
  ldr(clazz, clazz, TaskMirror::array_class_offset());
  cmp(clazz, zero);
  b(slow_case, eq);
bind(done);
#else
  get_class_list_base(tmp3);
  ldr_class_from_index_and_base(clazz, clazz, tmp3);
  ldr(clazz, imm_index(clazz, JavaClass::array_class_offset()));
#endif

  prefetch(3);

  comment("set array length");
  str(length, imm_index(tos_val, Array::length_offset()));

  comment("get prototypical near from array class");
  ldr(clazz, imm_index(clazz, FarClass::prototypical_near_offset()));

  comment("initialize array elements to zero");
  zero_region(result, length, tmp2, tmp3, Array::base_offset(), false, false);

  comment("set prototypical near in array; no need for write barrier");
  str(clazz, imm_index(result, Oop::klass_offset()));

  dispatch(1);

  // Slow-case: Use bc_anewarray template.
bind(slow_case);
  mov(tos_val, reg(length));
  push_results(1);
  ldr_label(r3, "bc_impl_anewarray_internal", false);
  bx(r3);
}

#if ENABLE_FLOAT

void bc_fadd::generate() {
  simple_c_bytecode("jvm_fadd", T_FLOAT, T_FLOAT, T_FLOAT, true);
}

void bc_fsub::generate() {
  simple_c_bytecode("jvm_fsub", T_FLOAT, T_FLOAT, T_FLOAT, false);
}

void bc_fmul::generate() {
  simple_c_bytecode("jvm_fmul", T_FLOAT, T_FLOAT, T_FLOAT, true);
}

void bc_fdiv::generate() {
  simple_c_bytecode("jvm_fdiv", T_FLOAT, T_FLOAT, T_FLOAT, false);
}

void bc_frem::generate() {
  simple_c_bytecode("jvm_frem", T_FLOAT, T_FLOAT, T_FLOAT, false);
}

void bc_l2d::generate() {
  simple_c_bytecode("jvm_l2d", T_DOUBLE, T_LONG);
}

void bc_l2f::generate() {
  simple_c_bytecode("jvm_l2f", T_FLOAT, T_LONG);
}

void bc_f2d::generate() {
  simple_c_bytecode("jvm_f2d", T_DOUBLE, T_FLOAT);
}

void bc_f2l::generate() {
  simple_c_bytecode("jvm_f2l", T_LONG, T_FLOAT);
}

void bc_f2i::generate() {
  simple_c_bytecode("jvm_f2i", T_INT, T_FLOAT);
}

void bc_d2f::generate() {
  simple_c_bytecode("jvm_d2f", T_FLOAT, T_DOUBLE);
}

void bc_d2l::generate() {
  simple_c_bytecode("jvm_d2l", T_LONG, T_DOUBLE);
}

void bc_d2i::generate() {
  simple_c_bytecode("jvm_d2i", T_INT, T_DOUBLE);
}

void bc_i2d::generate() {
  simple_c_bytecode("jvm_i2d", T_DOUBLE, T_INT);
}

void bc_i2f::generate() {
  simple_c_bytecode("jvm_i2f", T_FLOAT, T_INT);
}

void bc_dcmp::generate(int arg) {
  char* name;
  switch(arg) {
    case -1:  name = "jvm_dcmpl"; break;
    case 1:   name = "jvm_dcmpg"; break;
    default:  SHOULD_NOT_REACH_HERE(); name = "undefined"; break;
  }
  simple_c_bytecode(name, T_INT, T_DOUBLE, T_DOUBLE);
}

void bc_fcmp::generate(int arg) {
  char* name;
  switch(arg) {
    case -1:  name = "jvm_fcmpl"; break;
    case 1:   name = "jvm_fcmpg"; break;
    default:  SHOULD_NOT_REACH_HERE(); name = "undefined"; break;
  }
  simple_c_bytecode(name, T_INT, T_FLOAT, T_FLOAT);
}

void bc_dadd::generate() {
  simple_c_bytecode("jvm_dadd", T_DOUBLE, T_DOUBLE, T_DOUBLE, true);
}

void bc_dsub::generate() {
  simple_c_bytecode("jvm_dsub", T_DOUBLE, T_DOUBLE, T_DOUBLE, false);
}

void bc_dmul::generate() {
  simple_c_bytecode("jvm_dmul", T_DOUBLE, T_DOUBLE, T_DOUBLE, true);
}

void bc_ddiv::generate() {
  simple_c_bytecode("jvm_ddiv", T_DOUBLE, T_DOUBLE, T_DOUBLE, false);
}

void bc_drem::generate() {
  simple_c_bytecode("jvm_drem", T_DOUBLE, T_DOUBLE, T_DOUBLE, false);
}

#endif // ENABLE_FLOAT

#endif // ENABLE_INTERPRETER_GENERATOR
