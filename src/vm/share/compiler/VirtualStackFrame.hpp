/*
 *
 *
 * Portions Copyright  2000-2007 Sun Microsystems, Inc. All Rights
 * Reserved.  Use is subject to license terms.
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
 *
 *!c<
 * Copyright 2006 Intel Corporation. All rights reserved.
 *!c>
 */

#if ENABLE_COMPILER

/**
 * This structure describes the internal layout of one location inside
 * the VirtualStackFrame. It's good for quick access of the VirtualStackFrame,
 * but it works only when GC doesn't occur. For the general case, where
 * GC may occur, use the Location class instead (see Location.hpp)
 */
class RawLocationData {
protected:
  jubyte _type;
  jubyte _where;
  jubyte _status;
  jubyte _flags;
  juint  _value;
  juint  _length;
#if ENABLE_COMPILER_TYPE_INFO
  jushort _class_id;
#endif
#if ENABLE_CSE
  jushort _push_bci; // bci at which the value was pushed to this location
#endif
#if (ENABLE_COMPILER_TYPE_INFO + ENABLE_CSE) == 1
  jushort _padding;
#endif
#ifndef PRODUCT
public:
  void print_on(Stream *);
  void p();
#endif
};

class RawLocation;

#if ENABLE_CSE

#define FOR_VISIBLE_EXPRESSION_FIELDS(template) \
  template(jshort,  start_bci, 1)          \
  template(jshort,  expr_length, 0)        \
  template(jint,    locals_map, 0)         \
  template(jint,    fields_map, 0)         \
  template(jint,    array_types_map, 0)    \
  template(jubyte,  value_type, 0)

#define FOR_HIDDEN_EXPRESSION_FIELDS(template) \
  template(jubyte,  value_flags, 0)        \
  template(jushort, value_class_id, 0)     \
  template(jint,    value_length, 0)

#define FOR_ALL_EXPRESSION_FIELDS(template) \
  FOR_VISIBLE_EXPRESSION_FIELDS(template)   \
  FOR_HIDDEN_EXPRESSION_FIELDS(template)

#define DECLARE_FIELD(type, name, off) \
    type _##name;
#define DEFINE_ACCESSOR(type, name, off)       \
    type name() const { return _##name - off; } \
    void set_##name(type name) { _##name = name + off; }

class Expression {
 private:

  FOR_ALL_EXPRESSION_FIELDS(DECLARE_FIELD)

  FOR_HIDDEN_EXPRESSION_FIELDS(DEFINE_ACCESSOR)

 public:
  FOR_VISIBLE_EXPRESSION_FIELDS(DEFINE_ACCESSOR)

  void copy_value_flags_from(const Value & value) {
    GUARANTEE(value_type() == value.type(), "Types mismatch");
    set_value_flags(value.flags());
#if ENABLE_COMPILER_TYPE_INFO
    set_value_class_id(value.class_id());
#endif
    set_value_length(value.length());
  }

  void copy_value_flags_to(Value & value) const {
    GUARANTEE(value_type() == value.type(), "Types mismatch");
    value.set_flags(value_flags());
#if ENABLE_COMPILER_TYPE_INFO
    value.set_class_id(value_class_id());
#endif
    value.set_length(value_length());
  }
};

#undef DECLARE_FIELD
#undef DEFINE_ACCESSOR

class ExpressionAttributeClosure : public BytecodeClosure {
 public:
  ExpressionAttributeClosure() : 
   _locals_map(0), _fields_map(0), _array_types_map(0) {}

  virtual void load_local(BasicType kind, int index JVM_TRAPS);

  virtual void load_array(BasicType kind JVM_TRAPS);

  virtual void get_field(int index JVM_TRAPS);
    
  virtual void get_static(int index JVM_TRAPS);

  virtual void fast_get_field(BasicType field_type, int field_offset JVM_TRAPS);

  jint locals_map() const {
    return _locals_map;
  }

  jint fields_map() const {
    return _fields_map;
  }

  jint array_types_map() const {
    return _array_types_map;
  }

 private:
  jint   _locals_map;
  jint   _fields_map;
  jint   _array_types_map;
};

class KillExpressionClosure : public BytecodeClosure {
 public:
  KillExpressionClosure(VirtualStackFrame* f) {
    _frame = f;
  }

  virtual void store_local(BasicType kind, int index JVM_TRAPS);

  virtual void store_array(BasicType kind JVM_TRAPS);

  virtual void put_field(int index JVM_TRAPS);
    
  virtual void put_static(int index JVM_TRAPS);

  virtual void fast_put_field(BasicType field_type, int field_offset JVM_TRAPS);

 private: 
  VirtualStackFrame* frame() const { return _frame; }

  VirtualStackFrame* _frame;
};

#endif

class VirtualStackFrame: public CompilerObject {
 private:
  int            _saved_stack_pointer;
  // Fields to copy start here
  int            _real_stack_pointer;
  int            _virtual_stack_pointer;
  int            _flush_count;
#if ENABLE_REMEMBER_ARRAY_LENGTH
  //bitmap of _bound_mask
  //31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10 9 8 7 6 5 4 3 2 1 0
  //|flag       |                                       |base      |boundary  |local index                    |
  //flag:  whether is valid
  //base:  store the index of array base register
  //boundary:  store the the index of array length register
  //local index: store the location index(in the Virstual Stack Frame) of a array boundary checked local variable
  //We use 4bits to represent the index of ARM register(16 registers). So we use other approach to represent the case
  //there's no register assigned for base or boundary.
  //we don't adjust register reference when one is cached.
  int            _bound_mask;
#endif

#if ENABLE_CSE
  Expression _expressions_map[Assembler::number_of_registers];
  // If inside a bytecode segment that represents a valid expression,
  // keeps the first bci of the most recent expression being calculated on
  // stack. Otherwise, -1.
  jshort     _expression_start_bci;
#endif

#if USE_COMPILER_FPU_MAP
  FPURegisterMap _fpu_register_map;
#endif

#if ENABLE_ARM_VFP
  // Literals mask is necessary to distingish zero literals from non-literals
  // in literals map.  If literal map[reg] == 0 && literals_mask & 1 << reg
  // then reg contains zero literal otherwise reg does not contain a literal.
  int            _literals_mask[2];
#endif  // ENABLE_ARM_VFP

#if USE_COMPILER_LITERALS_MAP
  enum { literals_map_size = Assembler::number_of_registers };
  int _literals_map[ literals_map_size ];
#endif

  static int _location_map_size;

  // Followed by _location_map_size ints
  static Method* method( void );

  static CodeGenerator* code_generator( void ) {
    return (CodeGenerator*) _compiler_state;
  }

  static int num_stack_lock_words( void ) {
    return jvm_fast_globals.num_stack_lock_words;
  }

 public:
  // copy this virtual stack frame to dst
  void copy_to(VirtualStackFrame* dst) const {
    // Need to copy only the locations that are actually in use. I.e.,
    //
    //     virtual_stack_pointer()---+
    //                               v
    //     [literals_map_size][Y][Y][Y][n][n]
    //
    // Note that virtual_stack_pointer() is a "full stack": it points to
    // the current top of stack, so the number of used stack elements are
    // virtual_stack_pointer()+1
    jvm_memcpy( &dst->_real_stack_pointer, &_real_stack_pointer,
                DISTANCE(&_real_stack_pointer, raw_location_end() ) );
  }

  // Allocate a new instance of VirtualStackFrame.
  static VirtualStackFrame* allocate(JVM_SINGLE_ARG_TRAPS) {
    return (VirtualStackFrame*)
      CompilerObject::allocate( CompilerObject::VirtualStackFrame_type,
        _location_map_size + sizeof(VirtualStackFrame) JVM_NO_CHECK );
  }

  // Construct a new virtual stack frame for the given method.
  static VirtualStackFrame* create(Method* method JVM_TRAPS);

  // clone this virtual stack frame
  VirtualStackFrame* clone(JVM_SINGLE_ARG_TRAPS);

  // clone this virtual stack frame, but adjust stack for exception
  VirtualStackFrame* clone_for_exception(int handler_bci JVM_TRAPS);

  // clear this virtual stack frame
  void clear( void );

  // clear a specific location
  void clear( const int location );

  // adjust the virtual stack frame for an invoke.
  // - pop the parameter block and
  // - push the result
  void adjust_for_invoke(int parameter_size, BasicType return_type);

  // Flush all cached or changed locations to memory. After this operation
  // no locations will be mapped in registers anymore.
  void flush(JVM_SINGLE_ARG_TRAPS);

  // A platform-dependent way of flushing the stack quickly. Currently
  // we do this on ARM only by using post/pre-indexing addressing modes
  bool flush_quick();

  // flush all cached locations that are in the FPU
  void flush_fpu();

#if ENABLE_EMBEDDED_CALLINFO
  // flush all tags in this virtual stack frame
  ReturnOop generate_callinfo_stackmap(JVM_SINGLE_ARG_TRAPS);
#endif

  void mark_as_flushed();

  // write all changed locations to memory
  void write_changes();

  // update all cached locations by reading from memory
  void update_caches();

  // Spill a specific register into all locations it maps. After this
  // operation, all locations that previously mapped to this register will
  // not map to any register.
  void spill_register(Assembler::Register reg);

  // For all locations that uses this register, make sure that any changes
  // in the register are committed to memory.
  void commit_changes(Assembler::Register reg);

  // Make sure a frame doesn't use a register, either by spilling it or by
  // moving it to a free register.
  void unuse_register(Assembler::Register reg);

  // Check if this frame is conformant to the other frame, so you can
  // branch from this frame to the other frame w/o merging.
  bool is_conformant_to(VirtualStackFrame* other);

  // generate code for merging the states of the two virtual stack frames
  void conform_to(VirtualStackFrame* other) {
    verify_conform_to(other);
    conform_to_impl(other);
  }

  void verify_conform_to(VirtualStackFrame* /*other*/) PRODUCT_RETURN;

#ifndef PRODUCT
  // IMPL_NOTE: temporary workaround for the cases when optimized VSF merge
  // cannot be applied. Need to revisit these cases.
  bool can_do_optimized_merge(VirtualStackFrame* other);
#endif

  void conform_to_impl(VirtualStackFrame* other) {
#ifndef PRODUCT
    if (UseVSFMergeOptimization && can_do_optimized_merge(other)) {
      conform_to_optimized_impl(other);
    } else {
#else
    {
#endif
      conform_to_reference_impl(other);
    }
  }

  // make sure any other virtual stack frame can be made conformant with this
  // one
  void conformance_entry(bool merging);

  // make sure a frame types conform to the stack map, and discard any
  // frame types that don't match
  void conform_to_stack_map(int bci);

  // returns if this virtual stack frame fits into the compiled compact
  // format (lazy tagging)
  bool fits_compiled_compact_format() const;

#if ENABLE_EMBEDDED_CALLINFO
  void fill_in_tags(CallInfo& info, int parameters_size);
#endif

#if ENABLE_APPENDED_CALLINFO
  void fill_callinfo_record(CallInfoWriter* callinfo);
#endif

  // stack manipulation operations
  void swap();
  void pop();
  void pop2();
  void dup();
  void dup2();
  void dup_x1();
  void dup_x2();
  void dup2_x1();
  void dup2_x2();

  // type-specific stack manipulation operations
  void pop(BasicType kind);
  void pop(Value& result);
  void push(const Value& value);

  void value_at(Value& result, int location);
  void value_at(ExtendedValue& result, int location);
  void value_at_put(int location, const Value& value);

  // Set the (real) stack pointer to a specific value.
  void set_stack_pointer(jint location);

  // Dirtify a given register by invalidating all mappings of this register.
  void dirtify(Assembler::Register reg);

  bool is_mapping_something(const Assembler::Register reg) const {
    return is_mapping_location(reg) || is_annotated(reg);
  }
  bool is_mapping_location(const Assembler::Register reg) const;
  bool is_annotated(const Assembler::Register reg) const;

 public:
  // Accessors for the virtual stack pointer variable.
  jint virtual_stack_pointer( void ) const {
    return _virtual_stack_pointer;
  }
  void set_virtual_stack_pointer( const jint value ) {
    _virtual_stack_pointer = value;
  }
  void increment_virtual_stack_pointer( void ) {
    _virtual_stack_pointer++;
  }
  void decrement_virtual_stack_pointer( void ) {
    _virtual_stack_pointer--;
  }

#if ENABLE_ARM_VFP
  const jint* literals_mask( void ) const { return _literals_mask; }
  jint*       literals_mask( void )       { return _literals_mask; }
#endif

#if USE_COMPILER_LITERALS_MAP
  const jint* literals_map ( void ) const { return _literals_map;  }
  jint* literals_map ( void ) { return _literals_map;  }
#endif

  static void set_location_map_size( const int value ) {
    _location_map_size = value;
  }

  const RawLocationData* raw_location_base( void  ) const {
    return (const RawLocationData*)(this+1);
  }
  RawLocationData* raw_location_base( void ) {
    return (RawLocationData*)(this+1);
  }

  const RawLocation* raw_location_at(const int index) const {
    GUARANTEE( unsigned(index*sizeof(RawLocationData)) < unsigned(_location_map_size),
               "Location index out of bounds" );
    return (const RawLocation*) (raw_location_base() + index);
  }
  RawLocation* raw_location_at(const int index) {
    GUARANTEE( unsigned(index*sizeof(RawLocationData)) < unsigned(_location_map_size),
               "Location index out of bounds" );
    return (RawLocation*) (raw_location_base() + index);
  }

  // Marks the exclusive end of the end of the stack. To iterate all locations:
  // for (raw_loc = raw_location_at(0);
  //      raw < raw_location_end(raw_location_at(0));) {
  //     ....;
  //     raw_loc += is_two_word(raw_loc->type()) ? 2 : 1;
  // }
  const RawLocation* raw_location_end(const RawLocation* start) const {
    GUARANTEE(start == raw_location_at(0), "sanity");
    const RawLocationData *p = (const RawLocationData*) ((void*)start);
    return (const RawLocation*) (p + (virtual_stack_pointer() + 1));
  }
  RawLocation* raw_location_end(RawLocation* start) const {
    GUARANTEE(start == raw_location_at(0), "sanity");
    RawLocationData *p = (RawLocationData*) ((void*)start);
    return (RawLocation*) (p + (virtual_stack_pointer() + 1));
  }

  const RawLocation* raw_location_end( void ) const {
    return raw_location_end( raw_location_at(0) );
  }
  RawLocation* raw_location_end( void ) {
    return raw_location_end( raw_location_at(0) );
  }

  void clear_stack( void ) {
    set_virtual_stack_pointer(method()->max_locals() - 1);
  }

#if USE_COMPILER_FPU_MAP
  const FPURegisterMap& fpu_register_map( void ) const {
    return _fpu_register_map;
  }
  FPURegisterMap& fpu_register_map( void ) {
    return _fpu_register_map;
  }
#endif  // USE_COMPILER_FPU_MAP

  // Accessors for the stack pointer variable.
  jint stack_pointer( void ) const {
    return _real_stack_pointer;
  }
  void set_real_stack_pointer( const jint value) {
    _real_stack_pointer = value;
  }

  jint flush_count( void ) const {
    return _flush_count;
  }
  void set_flush_count( const jint value ) {
    _flush_count = value;
  }
  void clear_flush_count( void ) {
    set_flush_count( 0 );
  }

 private:
  jubyte receiver_flags(const int size_of_parameters) const;

 public:
  // These two helper methods are used to determine if we need a null check for
  // receiver without loading the receiver value into a register
  bool receiver_must_be_nonnull(int size_of_parameters) const {
    return (receiver_flags(size_of_parameters) & Value::F_MUST_BE_NONNULL) != 0;
  }

  bool receiver_must_be_null(int size_of_parameters) const {
    return (receiver_flags(size_of_parameters) & Value::F_MUST_BE_NULL) != 0;
  }

  void receiver(Value& value, int size_of_parameters) {
    value_at(value, virtual_stack_pointer() - size_of_parameters + 1);
  }

#if USE_COMPILER_LITERALS_MAP
  Assembler::Register get_literal(int imm32, const LiteralAccessor& la);

  void set_has_literal_value(Assembler::Register reg, const int imm32) {
    GUARANTEE((int)reg >= 0 && (int)reg < literals_map_size, "range");
    set_literal(reg, imm32);
#if ENABLE_ARM_VFP
    jint* p = literals_mask();
    if( reg >= BitsPerInt ) {
      p++;
      reg = Assembler::Register(reg - BitsPerInt);
    }
    const jint mask = 1 << reg;
    if(imm32 == 0) {
      *p |= mask;
    } else {
      *p &=~mask;
    }
#endif
  }

  jint has_literal(Assembler::Register reg) const {
#if ENABLE_ARM_VFP
    jint imm32 = get_literal(reg);
    if(imm32 == 0) {
      const jint* p = literals_mask();
      if( reg >= BitsPerInt ) {
        p++;
        reg = Assembler::Register(reg - BitsPerInt);
      }
      imm32 = (*p >> reg) & 1;
    }
    return imm32;
#else
    return get_literal(reg);
#endif
  }

  void clear_literals(void);

  bool has_no_literals(void) const;

  jint get_literal(const Assembler::Register reg) const {
    return _literals_map [reg];
  }

  void set_literal(const Assembler::Register reg, const jint imm32) {
    _literals_map [reg] = imm32;
  }

  void clear_literal(Assembler::Register reg) {
    set_literal( reg, 0 );
#if ENABLE_ARM_VFP
    jint* p = literals_mask();
    if( reg >= BitsPerInt ) {
      p++;
      reg = Assembler::Register(reg - BitsPerInt);
    }
    *p &=~ (1 << reg);
#endif
  }

#if ENABLE_ARM_VFP
  Assembler::Register find_zero                 (void) const;
  Assembler::Register find_non_NaN              (void) const;
  Assembler::Register find_double_non_NaN       (void) const;
  Assembler::Register find_double_vfp_literal(const jint lo, const jint hi) const;

  static bool is_non_NaN(const jint imm32) {
    enum { NaN_mask = 0x7F800000 };
    return imm32 && (imm32 & NaN_mask) != NaN_mask;
  }

  bool result_register_contains(const jint imm32) const {
    const jint s0_value = get_literal(Assembler::s0);
    return imm32 == s0_value &&
      (s0_value || (literals_mask()[0] & (1 << Assembler::s0)));
  }

  bool result_register_contains(const jint lo, const jint hi) const {
    const jint mask = ~literals_mask()[0];
    {
      const jint s0_value = get_literal(Assembler::s0);
      if (lo != s0_value) {
        return false;
      }
      if (s0_value == 0 && (mask & (1 << Assembler::s0)) ) {
        return false;
      }
    }
    {
      const jint s1_value = get_literal(Assembler::s1);
      if (hi != s1_value) {
        return false;
      }
      if (s1_value == 0 && (mask & (1 << Assembler::s1)) ) {
        return false;
      }
    }
    return true;
  }

#endif

#else // !USE_COMPILER_FPU_MAP
  void clear_literal(const Assembler::Register) {}
  void clear_literals(void) {}
  bool has_no_literals(void) const { return true; }
#endif

#if ENABLE_REMEMBER_ARRAY_LENGTH
  enum BoundMaskElement {
    index_bits = 0xfff, // store the location index of a array bound checked variable
    max_value_of_index = 0xfff,
    bound_bits = (0xf <<12), // store the array length
    bound_shift = 12,
    base_bits = (0xf << 16), // store the array base
    base_shift = 16,
    flag_bits = (0xf<<28), // store the array access count
    flag_shift = 28,
    max_value_of_count = 0xf,
  };

  //return the bound_mask bitmap
  jint bound_mask( void ) const {
    return _bound_mask;
  }

  //set the bound_mask bitmap
  void set_bound_mask( const jint value) {
    _bound_mask = value;
  }

  //get the register which hold the array base address from
  //bound_mask
  Assembler::Register base_register (void) const;

  //get the flag of bound_mask
  int  bound_flag(void) const;

  //get the register which hold the length of the array whose
  //address is cached in the base_register();
  Assembler::Register length_register(void) const;

  //index of local variable which did boundary check against
  //the value of length_register()
  jint bound_index(void) const ;

  //try to cache the array length into the bound mask bitmap
  //the array base address is passwd by base_reg
  Assembler::Register cached_array_length(
       Assembler::Register base_reg, bool first_time,
       Assembler::Condition cond);

  //check whether the array whose base register is hold in "reg" is
  //cached by boundary mask bitmap
  bool is_cached_array_bound_of(Assembler::Register reg) const;

  //whether there's array cached in the bound mask
  bool is_bound_mask_valid(void) const;

  //clear the bound mask bitmap of current VSF
  void clear_bound();

  //free the registers allocted for caching of array boundary
  Assembler::Register free_length_register();

#if  ENABLE_REMEMBER_ARRAY_CHECK && ENABLE_NPCE
  //clear the must_be_index_checked status of all the values in current VSF
  void clear_must_be_index_checked_status_of_values(void);

  //whether the length is cached in bound mask and
  //value is mask as must_be_index_checked
  bool is_value_must_be_index_checked(
        Assembler::Register length, Value &value);

  //mark the value as index_checked,
  //the "length" must have been cached in
  //bound mask
  void set_value_must_be_index_checked(
        Assembler::Register length, Value &value);

  //whether need to skip the array boundary check.
  //And set must_be_index_checked if the local value is not checked before.
  bool try_to_set_must_be_index_checked(Assembler::Register length, Value& value);
#else
  void clear_must_be_index_checked_status_of_values(void){}
  bool is_value_must_be_index_checked(
        Assembler::Register, Value &value) {return false;}
  void set_value_must_be_index_checked(
        Assembler::Register, Value &value) {}
  bool try_to_set_must_be_index_checked(Assembler::Register length, Value& value) {return false;}
#endif

  //set the flag bound mask.
  void set_boundary_flag(void);

  //try to recache the same array base and length into the bound mask
  //with the same base_reg and bound_reg
  void recache_array_length(Assembler::Register base_reg,
          Assembler::Register bound_reg);

  //cache the index of local variable into the bound mask.
  //the value should have done boundary check against the array
  //length cached in the bound mask
  void set_boundary_index(jint index);

  //update the base register and length register of
  //the bound mask with the value of parameters
  void set_boundary_value(Assembler::Register bound_reg,
                                       Assembler::Register base_reg);

  //mark the array holding by a local variable isn't first time accessed.
  //so the compiler can omit  the loading of array length in next time.
  //We won't cache non local variable.
  bool set_is_not_first_time_access(Assembler::Register base_reg);

  //during register allocator, try to free the register used for boundary caching
  //if exists.
  Assembler::Register try_to_free_length_register();

  //check is there any location occupied that register.
  //we must make sure the base register is allocated in the frame
  //before we do the preload of array length
  bool is_allocated(const Assembler::Register base_reg) const;
#else
  void clear_must_be_index_checked_status_of_values(void) {}
  bool is_value_must_be_index_checked(
        Assembler::Register, Value &value) {return false;}
  void set_value_must_be_index_checked(
        Assembler::Register, Value &value) {}
  bool try_to_set_must_be_index_checked(Assembler::Register length, Value& value) {return false;}
  jint bound_mask() const { return 0;}
  Assembler::Register base_register (void) const {return Assembler::no_reg;}
  int  bound_flag(void) const {return 0;}
  Assembler::Register length_register(void) const {return Assembler::no_reg;}
  void clear_bound() {}
  bool is_bound_mask_valid(void) const {return false;}
  void recache_array_length(Assembler::Register base_reg,
          Assembler::Register bound_reg) {};
  void set_bound_mask(jint value) {}
  Assembler::Register try_to_free_length_register() {return Assembler::no_reg;}
#endif

  void set_value_must_be_nonnull(Value &value);
  void set_value_has_known_min_length(Value &value, int length);
#if ENABLE_COMPILER_TYPE_INFO
  void set_value_class(Value & value, JavaClass * java_class);
#endif

#if ENABLE_CSE
  void push_expression();
  void pop_expression();

  void set_expr_start_bci(int bci) {
    _expression_start_bci = bci;
  }

  int expr_start_bci() const {
    return _expression_start_bci;
  }

  void reset_expr_start_bci() {
    _expression_start_bci = -1;
  }

  bool in_expression_segment() const {
    return expr_start_bci() >= 0;
  }

  void reset_expression_segment();

  bool has_expression(const Assembler::Register reg) const {
    GUARANTEE((int)reg >= 0 && (int)reg < Assembler::number_of_registers, 
              "Invalid register");
    return _expressions_map[reg].expr_length() > 0;
  }

  const Expression & expression(const Assembler::Register reg) const {
    GUARANTEE((int)reg >= 0 && (int)reg < Assembler::number_of_registers, 
              "Invalid register");
    return _expressions_map[reg];
  }

  Expression & expression(const Assembler::Register reg) {
    GUARANTEE((int)reg >= 0 && (int)reg < Assembler::number_of_registers, 
              "Invalid register");
    return _expressions_map[reg];
  }

  void clear_expression(const Assembler::Register reg) {
    GUARANTEE((int)reg >= 0 && (int)reg < Assembler::number_of_registers, 
              "Invalid register");
    jvm_memset(_expressions_map + reg, 0, sizeof(Expression));
  }

  void clear_expressions(void);
#else
  void push_expression() {}
  void pop_expression() {}
  bool has_expression(const Assembler::Register reg) const { return false; }
  void reset_expression_segment() {}
  void clear_expression(const Assembler::Register) {}
  void clear_expressions(void) {}
#endif

  // Debug dump the state of the virtual stack frame.
  void dump_fp_registers(bool /*as_comment*/) const PRODUCT_RETURN;
  void dump(bool /*as_comment*/)                    PRODUCT_RETURN;
  void print( void )                                PRODUCT_RETURN;

 private:
  void conform_to_reference_impl(VirtualStackFrame* other);

#ifndef PRODUCT
  void conform_to_optimized_impl(VirtualStackFrame* other);
#endif

  // VSF merge helper routines.

  // Prologue - misc stuff to be done before the merge:
  //  - flush float and double locations if using compiler FPU map;
  //  - clear compiler literals
  //  - adjust stack pointer
  //  - discard locations with type conflict
  //  - AZZERT_ONLY: verify that if a target location contains an immediate
  //    value, the corresponding source location has the same value.
  void conform_to_prologue(VirtualStackFrame* other);
  // Epilogue - misc stuff to be done after the merge:
  //  - adjust stack pointer.
  void conform_to_epilogue(VirtualStackFrame* other);

#ifndef PRODUCT
  // First phase - process all required register->memory and immediate->memory
  // transfers.
  // Returns true iff a second phase is needed (some location requires
  // register->register transfer).
  bool conform_to_phase_one(VirtualStackFrame* other);
  // Second phase - process all required register->register transfers.
  void conform_to_phase_two(VirtualStackFrame* other);
  // Third phase - process all required memory->register and immediate->register
  // transfers.
  void conform_to_phase_three(VirtualStackFrame* other);
#endif

  // Get the number of locals.
  int locals() const { return method()->max_locals(); }

  // Get the total amount of locations.
  int locations() const {
    return method()->max_execution_stack_count() - num_stack_lock_words();
  }

  // Check if a given location satisfies some criteria.
  bool is_local (int location) const {
    return location < locals();
  }

  bool is_mapped_by(const RawLocation* raw_location,
                    const Assembler::Register reg) const;

  // Check if a given location is mapped by a given register.
  bool is_mapped_by( const int index, const Assembler::Register reg ) const {
    return is_mapped_by(raw_location_at(index), reg);
  }

  // Returns the type of a expression stack element
  BasicType expression_stack_type(int index);

  // Expression stack manipulation used for implementing all
  // dup and swap operations.
  void stack_2_1_to_1_2(void);
  void stack_1_to_1_1(void);
  void stack_2_1_to_2_1_2_1(void);
  void stack_2_1_to_1_2_1(void);
  void stack_3_2_1_to_1_3_2_1(void);
  void stack_3_2_1_to_2_1_3_2_1(void);
  void stack_4_3_2_1_to_2_1_4_3_2_1(void);

  friend class OSRStub;
};

// This class is used to save the virtual stack frame state before
// VM calls and to restore it before returning.
class PreserveVirtualStackFrameState: public StackObj {
 public:
  PreserveVirtualStackFrameState(VirtualStackFrame* vsf JVM_TRAPS): _frame(vsf){
    save(JVM_SINGLE_ARG_NO_CHECK_AT_BOTTOM);
  }
  ~PreserveVirtualStackFrameState( void ) {
    restore();
  }

 private:
  void save( JVM_SINGLE_ARG_TRAPS );
  void restore( void );

  VirtualStackFrame*  frame      ( void ) const { return _frame; }
  VirtualStackFrame*  saved_frame( void ) const { return _saved_frame; }

  VirtualStackFrame* _frame;
  VirtualStackFrame* _saved_frame;
};

#if USE_COMPILER_LITERALS_MAP
class LiteralElementStream {
public:
  LiteralElementStream(VirtualStackFrame* vsf) {
    _vsf = vsf;
    _index = -1;
    next();
  }
  void next();
  bool eos() {
    return _index >= Assembler::number_of_registers;
  }
  void reset() {
    _index = -1; next();
  }
  Assembler::Register reg() {
    return (Assembler::Register)_index;
  }
  int value( void ) const {
    return _vsf->literals_map()[_index];
  }

private:
  VirtualStackFrame* _vsf;
  int                _index;
};
#endif

#if ENABLE_CSE
class ExpressionStream {
public:
  ExpressionStream(VirtualStackFrame* vsf) {
    _vsf = vsf;
    _index = -1;
    next();
  }
  void next();
  bool eos() {
    return _index >= Assembler::number_of_registers;
  }
  void reset() {
    _index = -1; next();
  }

  Assembler::Register reg() const {
    return (Assembler::Register)_index;
  }

#define DEFINE_EXPR_ACCESSOR(type, name, off) \
  type name() const { return frame()->expression(reg()).name(); }

  FOR_VISIBLE_EXPRESSION_FIELDS(DEFINE_EXPR_ACCESSOR);

  bool has_local_index(int index) const {
    GUARANTEE(index >= 0 && index < BitsPerWord, "Invalid index");
    return locals_map() & (1 << index);
  }
  bool has_field_offset(int offset) const {
    GUARANTEE(offset >= 0 && offset < BitsPerWord, "Invalid offset");
    return fields_map() & (1 << offset);
  }
  bool has_array_type(BasicType type) const {
    GUARANTEE(type >= T_BOOLEAN && type <= T_ARRAY, "Invalid type");
    return array_types_map() & (1 << type);
  }

  void kill() {
    frame()->clear_expression((Assembler::Register)_index);
  }
private:
  VirtualStackFrame* frame() const { return _vsf; }

  VirtualStackFrame*  _vsf;
  jint                _index;
};
#endif

#ifdef PRODUCT
#define REGISTER_REFERENCES_CHECKER
#else
#define REGISTER_REFERENCES_CHECKER \
  RegisterReferenceChecker __register_reference_checker

class RegisterReferenceChecker {
public:
  RegisterReferenceChecker() {
    for (int i = 0; i < Assembler::number_of_registers; i++) {
      Assembler::Register reg = (Assembler::Register)i;
      _register_references[i] = RegisterAllocator::references(reg);
    }
  }
  ~RegisterReferenceChecker() {
#ifdef AZZERT
    bool ok = true;
#endif
    for (int i = 0; i < Assembler::number_of_registers; i++) {
      Assembler::Register reg = (Assembler::Register)i;
      if (_register_references[i] != RegisterAllocator::references(reg)) {
#if ARM | defined(HITACHI_SH)
        const char* name = Disassembler::register_name(reg);
#else
        const char* name = Assembler::name_for_long_register(reg);
#endif
        tty->print_cr("register_references[%s] = %d -> %d", name,
                      _register_references[i],
                      RegisterAllocator::references(reg));
#ifdef AZZERT
        ok = false;
#endif
      }
    }
    GUARANTEE(ok, "Register references changed");
  }
private:
  int _register_references[Assembler::number_of_registers];
};

#endif

#endif /* ENABLE_COMPILER */
