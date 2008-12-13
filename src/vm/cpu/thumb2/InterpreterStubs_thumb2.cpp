/*
 *   
 *
 * Copyright  1990-2008 Sun Microsystems, Inc. All Rights Reserved.
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
#include "incls/_InterpreterStubs_thumb2.cpp.incl"

#if ENABLE_INTERPRETER_GENERATOR

void InterpreterStubs::generate() {
  generate_interpreter_throw_exceptions();
  generate_interpreter_deoptimization_entry();

  generate_primordial_to_current_thread();
  generate_current_thread_to_primordial();
}

void InterpreterStubs::generate_interpreter_throw_exceptions() {
  Segment seg(this, code_segment, "Interpreter exception throwers");

  bind_global("interpreter_throw_ArrayIndexOutOfBoundsException");
  interpreter_call_vm("array_index_out_of_bounds_exception", T_ILLEGAL);

  bind_global("interpreter_throw_NullPointerException_tos_cached");
  push(tos);
  bind_global("interpreter_throw_NullPointerException");
  interpreter_call_vm("null_pointer_exception", T_ILLEGAL);

  bind_global("interpreter_throw_IncompatibleClassChangeError");
  interpreter_call_vm("incompatible_class_change_error", T_ILLEGAL);
}

#if ENABLE_EMBEDDED_CALLINFO
void InterpreterStubs::generate_interpreter_fill_in_tags() {
#if 0 // BEGIN_CONVERT_TO_T2
  Segment seg(this, code_segment, "Interpreter fill in tags");

  Register param_size = tmp0; // must be preserved
  Register callinfo   = tmp1;

  {
      bind_local("interpreter_fill_in_tags");
      comment("%s: size of parameters",       reg_name(tmp0));
      comment("%s: call info from call size", reg_name(tmp1));
      comment("");
      comment("Must preserve lr, %s (method), and %s (parameter size)", 
              reg_name(r0), reg_name(tmp0));

      Label loop_entry;

      // tos_val = r0 must be preserved
      Register arg_index  = tmp2;
      Register one_reg    = tmp3;
      Register tag_address = JavaStackDirection < 0 ? tmp4 : jsp;

      mov_imm(one_reg, 1 << CallInfo::format1_tag_start);
      sub(arg_index, param_size, one, set_CC);
      report_fatal("shouldn't be called on no arguments", lt);

      if (JavaStackDirection < 0) { 
        comment("Tag address of last argument");
        add(tag_address, jsp, imm(BytesPerWord));
      } else { 
        comment("jsp points to tag address of last argument");
      }

    bind(loop_entry);
      comment("test the bit in the call info");
      tst(callinfo, reg_shift(one_reg, lsl, arg_index));

      mov(tos_tag, imm(obj_tag), ne);
      mov(tos_tag, imm(int_tag), eq);
      if (JavaStackDirection < 0) { 
        str(tos_tag, add_index(tag_address, arg_index, lsl, 3));
      } else {
        str(tos_tag, sub_index(tag_address, arg_index, lsl, 3));
      }
      sub(arg_index, arg_index, one, set_CC);
      b(loop_entry, ge);
      mov(pc, reg(locals));
  }
  {
    Register bit_offset  = tmp1; // callinfo not needed
    Register one_reg     = tmp2;
    Register tag_address = tmp3;
    Register x1          = tmp4;
    Register x2          = tmp5;
    Register index       = tos_tag;
    Label loop;

    bind_local("interpreter_fill_in_extended_tags");
       comment("Total number of tags");
       if (HARDWARE_LITTLE_ENDIAN) {
         ldrh(bit_offset, imm_index3(lr, -2 * BytesPerWord));
       } else {
         ldrh(bit_offset, imm_index3(lr, -2 * BytesPerWord + 2));
       }

       comment("Tag address of first argument");
       if (JavaStackDirection < 0) {
         add(tag_address, jsp, imm_shift(param_size, lsl, 3));
       } else { 
         sub(tag_address, jsp, imm_shift(param_size, lsl, 3));
       }
       // tag_address points to the last address of the previous stack
       add_imm(tag_address, tag_address,
               JavaFrame::arg_offset_from_sp(-1) + BytesPerWord);

       comment("Index of last argument");
       sub(index, param_size, one);    

       comment("Bit number of first argument");
       sub(bit_offset, bit_offset, reg(param_size));
       mov(bit_offset, imm_shift(bit_offset, lsl, 2));
       add(bit_offset, bit_offset, imm(32 + 32 + 16));

       comment("A useful constant");
       mov(one_reg, one);

    bind(loop);
       comment("Get the bit offset for this argument");
       add(x1, bit_offset, imm_shift(index, lsl, 2));

       comment("Get the appropriate word");
       mov(x2, imm_shift(x1, lsr, 5));
       ldr(x2, sub_index(lr, x2, lsl, 2));

       comment("Pick out the nybble");
       andr(x1, x1, imm(31));
       mov(x2, reg_shift(x2, lsr, x1));
       andr(x2, x2, imm(15), set_CC);

       comment("Convert the nybble into a stack type");
       sub(x2, x2, one,                     ne);
       mov(x2, reg_shift(one_reg, lsl, x2), ne);
       if (JavaStackDirection < 0) {
         str(x2, sub_index(tag_address, index, lsl, 3));
       } else {
         str(x2, add_index(tag_address, index, lsl, 3));
       }
       comment("Update the info");
       sub(index, index, one, set_CC);
       b(loop, ge);
       mov(pc, reg(locals));
  }
#endif // END_CONVERT_TO_T2
}
#endif // ENABLE_EMBEDDED_CALLINFO

void InterpreterStubs::generate_interpreter_deoptimization_entry() {
  Segment seg(this, code_segment, "Interpreter deoptimization entry");
  define_call_info();
  bind("interpreter_deoptimization_entry");
  restore_interpreter_state();
  prefetch(0);
  dispatch(tos_on_stack);
}

void InterpreterStubs::generate_primordial_to_current_thread() {
  Segment seg(this, code_segment, "Primordial to current thread");

bind_global("primordial_to_current_thread");
  comment("save permanent registers (including return address)");

  enterx();

  stmdb_post_w(sp, range(r3, r11));
  str_pre_imm12_w(lr, sp, (PreIndex)-BytesPerWord);

  comment("Set up global pointer");
  ldr_gp_base(gp, r3);

#if !ENABLE_ARM_V7
  ldr_label(r3, "persistent_handles");
  set_persistent_handles_addr(r3);
#endif

#if ENABLE_ARM_V7
  comment("Set up HandlerBase");
  ldr_label(r3, "handler_base");
  mcr_w(p14, 6, r3, c1, c0);
#endif

  comment("Get current thread");
  get_thread(r1);

  comment("Save primordial stack pointer");
  mov(r3, sp);
  set_primordial_sp(r3); // str.w sp, [...] is illegal, so use another register

  comment("Get new stack pointer");
  ldr(jsp, imm_index(r1, Thread::stack_pointer_offset()));

  comment("Go to code");
  ldr(lr, imm_index(jsp, -JavaStackDirection * BytesPerWord));
  Macros::ldr(fp, jsp, PostIndex(-JavaStackDirection * 2 * BytesPerWord));
  bx(lr);

bind_global("start_lightweight_thread_asm");
  Label testing_compiler; 
  const int SignedBytesPerWord = JavaStackDirection * BytesPerWord;

  comment("Set up global pointer");
  ldr_gp_base(gp, r0);
  // jsp       => Thread::lightweight_thread_exit
  // jsp +- 4   => Thread::lightweight_thread_uncaught_exception
  // jsp +- 8   => Thread::finish
  // jsp +- 12  => force_terminated
  // jsp +- 16  => TestCompiler

  comment("Invoke pending entries unless the thread is being terminated");
  get_thread(r0);
  comment("r1 = THREAD->status();");
  ldr(r1, imm_index(r0, Thread::status_offset()));
  mov(r2, zero);
  comment("if ((r1 & THREAD_TERMINATING) != 0) {");
  tst(r1, THREAD_TERMINATING);
  comment("  THREAD->pending_entries = NULL;");
  it(ne, ELSE);
  str(r2, imm_index(r0, Thread::pending_entries_offset()));
  comment("} else {");
  comment("  invoke_pending_entries(THREAD)");
  bl("invoke_pending_entries");
  comment("}");

  comment("if (!TestCompiler) {");
  ldr(r0, imm_index(jsp, -4 * SignedBytesPerWord));
  get_current_pending_exception(r1);
  cmp(r0, imm12(zero));
  b(testing_compiler, ne);

  comment("  if (Thread::current_has_pending_exception()) {");
  comment("    call_on_primordial_stack(lightweight_thread_uncaught_exception);");
  comment("  }");
  cmp(r1, imm12(zero));
  it(ne, THEN);
  ldr(r0, imm_index(jsp, -1 * SignedBytesPerWord));
  bl("call_on_primordial_stack");

  comment("  call_on_primordial_stack(finish);");
  ldr(r0, imm_index(jsp, -2 * SignedBytesPerWord));
  bl("call_on_primordial_stack");   

  comment("  invoke_pending_entries(THREAD);");
  get_thread(r0);
  bl("invoke_pending_entries");

  comment("  force_terminated()");
  ldr(r0, imm_index(jsp, -3 * SignedBytesPerWord));  
  bl("call_on_primordial_stack");
  comment("}");  
#if ENABLE_ISOLATES
  comment("Terminate the task if no other threads on it");
  ldr_label(r0, "thread_task_cleanup");
  bl("call_on_primordial_stack");   
  comment("  invoke_pending_entries(THREAD);");
  get_thread(r0);
  bl("invoke_pending_entries");
#endif

bind_global(testing_compiler);
  comment("call_on_primordial_stack(lightweight_thread_exit);");
  ldr(r0, jsp);
  bl("call_on_primordial_stack");

  comment("GUARANTEE(Scheduler::_next_runnable_thread == NULL");

  ldr_label(r0, "_next_runnable_thread");
  ldr(r0, r0);
  cmp(r0, imm12(zero));
  breakpoint(ne);

  comment("current_thread_to_primordial();");
  bl("current_thread_to_primordial_fast");
}

void InterpreterStubs::generate_current_thread_to_primordial() {
  Segment seg(this, code_segment, "Current thread to primordial");
  bind_global("current_thread_to_primordial");
  
  comment("Set up global pointer, as we can be called from C code");
  ldr_gp_base(gp, r0);
  
  bind_global("current_thread_to_primordial_fast");

  // We're never going to return to this thread, so it doesn't matter if
  // it doesn't look like a stopped Java thread anymore.
  get_primordial_sp(r3);
  mov(sp, r3);

  comment("restore permanent registers (including return address)");
  Macros::ldr(lr, sp, (PostIndex)BytesPerWord);
  ldmia_post_w(sp, range(r3, r11));
  leavex();
  bx(lr);

  if (GenerateDebugAssembly) {
    bind_local("interpreter_bkpt");
    get_gp_bytecode_counter(tmp3);
    add(tmp3, tmp3, imm(1));
    set_gp_bytecode_counter(tmp3);
    mov(pc, reg(tmp0));
  }
}

#endif // ENABLE_INTERPRETER_GENERATOR
