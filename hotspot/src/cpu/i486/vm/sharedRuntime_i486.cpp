#ifdef USE_PRAGMA_IDENT_SRC
#pragma ident "@(#)sharedRuntime_i486.cpp	1.52 07/06/28 16:50:04 JVM"
#endif
/*
 * Copyright 2003-2007 Sun Microsystems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *  
 */

#include "incls/_precompiled.incl"
#include "incls/_sharedRuntime_i486.cpp.incl"

#define __ masm->
#ifdef COMPILER2
UncommonTrapBlob   *SharedRuntime::_uncommon_trap_blob;
#endif // COMPILER2

DeoptimizationBlob *SharedRuntime::_deopt_blob;
SafepointBlob      *SharedRuntime::_polling_page_safepoint_handler_blob;
SafepointBlob      *SharedRuntime::_polling_page_return_handler_blob;
RuntimeStub*       SharedRuntime::_wrong_method_blob;
RuntimeStub*       SharedRuntime::_ic_miss_blob;
RuntimeStub*       SharedRuntime::_resolve_opt_virtual_call_blob;
RuntimeStub*       SharedRuntime::_resolve_virtual_call_blob;
RuntimeStub*       SharedRuntime::_resolve_static_call_blob;

class RegisterSaver {
  enum { FPU_regs_live = 8 /*for the FPU stack*/+8/*eight more for XMM registers*/ };
  // Capture info about frame layout
  enum layout { 
                fpu_state_off = 0,
                fpu_state_end = fpu_state_off+FPUStateSizeInWords-1,
                st0_off, st0H_off,
                st1_off, st1H_off,
                st2_off, st2H_off,
                st3_off, st3H_off,
                st4_off, st4H_off,
                st5_off, st5H_off,
                st6_off, st6H_off,
                st7_off, st7H_off, 

                xmm0_off, xmm0H_off, 
                xmm1_off, xmm1H_off, 
                xmm2_off, xmm2H_off, 
                xmm3_off, xmm3H_off, 
                xmm4_off, xmm4H_off, 
                xmm5_off, xmm5H_off, 
                xmm6_off, xmm6H_off, 
                xmm7_off, xmm7H_off, 
                flags_off,
                edi_off,         
                esi_off,
                ignore_off,  // extra copy of ebp
                esp_off,
                ebx_off,
                edx_off,
                ecx_off,
                eax_off,            
                // The frame sender code expects that rbp will be in the "natural" place and
                // will override any oopMap setting for it. We must therefore force the layout
                // so that it agrees with the frame sender code.
		ebp_off, 
                return_off,      // slot for return address
	        reg_save_size };

  
  public: 

  static OopMap* save_live_registers(MacroAssembler* masm, int additional_frame_words,
                                     int* total_frame_words, bool verify_fpu = true);
  static void restore_live_registers(MacroAssembler* masm);

  static int eax_offset() { return eax_off; }
  static int ebx_offset() { return ebx_off; }

  // Offsets into the register save area
  // Used by deoptimization when it is managing result register
  // values on its own

  static int eaxOffset(void) { return eax_off; }
  static int edxOffset(void) { return edx_off; }
  static int ebxOffset(void) { return ebx_off; }
  static int xmm0Offset(void) { return xmm0_off; }
  // This really returns a slot in the fp save area, which one is not important
  static int fpResultOffset(void) { return st0_off; }

  // During deoptimization only the result register need to be restored
  // all the other values have already been extracted.

  static void restore_result_registers(MacroAssembler* masm);

};

OopMap* RegisterSaver::save_live_registers(MacroAssembler* masm, int additional_frame_words,
                                           int* total_frame_words, bool verify_fpu) {

  int frame_size_in_bytes =  (reg_save_size + additional_frame_words) * wordSize;
  int frame_words = frame_size_in_bytes / wordSize;
  *total_frame_words = frame_words;

  assert(FPUStateSizeInWords == 27, "update stack layout");

  // save registers, fpu state, and flags  
  // We assume caller has already has return address slot on the stack
  // We push epb twice in this sequence because we want the real ebp
  // to be under the return like a normal enter and we want to use pushad
  // We push by hand instead of pusing push
  __ enter();
  __ pushad();
  __ pushfd();        
  __ subl(esp,FPU_regs_live*sizeof(jdouble)); // Push FPU registers space
  __ push_FPU_state();          // Save FPU state & init

  if (verify_fpu) {
    // Some stubs may have non standard FPU control word settings so
    // only check and reset the value when it required to be the
    // standard value.  The safepoint blob in particular can be used
    // in methods which are using the 24 bit control word for
    // optimized float math.

#ifdef ASSERT
    // Make sure the control word has the expected value
    Label ok;
    __ cmpw(Address(esp, 0), StubRoutines::fpu_cntrl_wrd_std());
    __ jccb(Assembler::equal, ok);
    __ stop("corrupted control word detected");
    __ bind(ok);
#endif

    // Reset the control word to guard against exceptions being unmasked
    // since fstp_d can cause FPU stack underflow exceptions.  Write it
    // into the on stack copy and then reload that to make sure that the
    // current and future values are correct.
    __ movw(Address(esp, 0), StubRoutines::fpu_cntrl_wrd_std());
  }

  __ frstor(Address(esp, 0));
  if (!verify_fpu) {
    // Set the control word so that exceptions are masked for the
    // following code.
    __ fldcw(Address((int) StubRoutines::addr_fpu_cntrl_wrd_std(), relocInfo::none));
  }

  // Save the FPU registers in de-opt-able form 

  __ fstp_d(Address(esp, st0_off*wordSize)); // st(0)
  __ fstp_d(Address(esp, st1_off*wordSize)); // st(1)
  __ fstp_d(Address(esp, st2_off*wordSize)); // st(2)
  __ fstp_d(Address(esp, st3_off*wordSize)); // st(3)
  __ fstp_d(Address(esp, st4_off*wordSize)); // st(4)
  __ fstp_d(Address(esp, st5_off*wordSize)); // st(5)
  __ fstp_d(Address(esp, st6_off*wordSize)); // st(6)
  __ fstp_d(Address(esp, st7_off*wordSize)); // st(7)

  if( UseSSE == 1 ) {           // Save the XMM state
    __ movflt(Address(esp,xmm0_off*wordSize),xmm0);
    __ movflt(Address(esp,xmm1_off*wordSize),xmm1);
    __ movflt(Address(esp,xmm2_off*wordSize),xmm2);
    __ movflt(Address(esp,xmm3_off*wordSize),xmm3);
    __ movflt(Address(esp,xmm4_off*wordSize),xmm4);
    __ movflt(Address(esp,xmm5_off*wordSize),xmm5);
    __ movflt(Address(esp,xmm6_off*wordSize),xmm6);
    __ movflt(Address(esp,xmm7_off*wordSize),xmm7);
  } else if( UseSSE >= 2 ) {
    __ movdbl(Address(esp,xmm0_off*wordSize),xmm0);
    __ movdbl(Address(esp,xmm1_off*wordSize),xmm1);
    __ movdbl(Address(esp,xmm2_off*wordSize),xmm2);
    __ movdbl(Address(esp,xmm3_off*wordSize),xmm3);
    __ movdbl(Address(esp,xmm4_off*wordSize),xmm4);
    __ movdbl(Address(esp,xmm5_off*wordSize),xmm5);
    __ movdbl(Address(esp,xmm6_off*wordSize),xmm6);
    __ movdbl(Address(esp,xmm7_off*wordSize),xmm7);
  }

  // Set an oopmap for the call site.  This oopmap will map all
  // oop-registers and debug-info registers as callee-saved.  This
  // will allow deoptimization at this safepoint to find all possible
  // debug-info recordings, as well as let GC find all oops.

  OopMapSet *oop_maps = new OopMapSet();
  OopMap* map =  new OopMap( frame_words, 0 );  

#define STACK_OFFSET(x) VMRegImpl::stack2reg((x) + additional_frame_words)

  map->set_callee_saved(STACK_OFFSET( eax_off), eax->as_VMReg());
  map->set_callee_saved(STACK_OFFSET( ecx_off), ecx->as_VMReg());
  map->set_callee_saved(STACK_OFFSET( edx_off), edx->as_VMReg());
  map->set_callee_saved(STACK_OFFSET( ebx_off), ebx->as_VMReg());
  // ebp location is known implicitly, no oopMap
  map->set_callee_saved(STACK_OFFSET( esi_off), esi->as_VMReg());
  map->set_callee_saved(STACK_OFFSET( edi_off), edi->as_VMReg());
  map->set_callee_saved(STACK_OFFSET(st0_off), as_FloatRegister(0)->as_VMReg());
  map->set_callee_saved(STACK_OFFSET(st1_off), as_FloatRegister(1)->as_VMReg());
  map->set_callee_saved(STACK_OFFSET(st2_off), as_FloatRegister(2)->as_VMReg());
  map->set_callee_saved(STACK_OFFSET(st3_off), as_FloatRegister(3)->as_VMReg());
  map->set_callee_saved(STACK_OFFSET(st4_off), as_FloatRegister(4)->as_VMReg());
  map->set_callee_saved(STACK_OFFSET(st5_off), as_FloatRegister(5)->as_VMReg());
  map->set_callee_saved(STACK_OFFSET(st6_off), as_FloatRegister(6)->as_VMReg());
  map->set_callee_saved(STACK_OFFSET(st7_off), as_FloatRegister(7)->as_VMReg());
  map->set_callee_saved(STACK_OFFSET(xmm0_off), xmm0->as_VMReg());
  map->set_callee_saved(STACK_OFFSET(xmm1_off), xmm1->as_VMReg());
  map->set_callee_saved(STACK_OFFSET(xmm2_off), xmm2->as_VMReg());
  map->set_callee_saved(STACK_OFFSET(xmm3_off), xmm3->as_VMReg());
  map->set_callee_saved(STACK_OFFSET(xmm4_off), xmm4->as_VMReg());
  map->set_callee_saved(STACK_OFFSET(xmm5_off), xmm5->as_VMReg());
  map->set_callee_saved(STACK_OFFSET(xmm6_off), xmm6->as_VMReg());
  map->set_callee_saved(STACK_OFFSET(xmm7_off), xmm7->as_VMReg());
  // %%% This is really a waste but we'll keep things as they were for now
  if (true) {
#define NEXTREG(x) (x)->as_VMReg()->next()
    map->set_callee_saved(STACK_OFFSET(st0H_off), NEXTREG(as_FloatRegister(0)));
    map->set_callee_saved(STACK_OFFSET(st1H_off), NEXTREG(as_FloatRegister(1)));
    map->set_callee_saved(STACK_OFFSET(st2H_off), NEXTREG(as_FloatRegister(2)));
    map->set_callee_saved(STACK_OFFSET(st3H_off), NEXTREG(as_FloatRegister(3)));
    map->set_callee_saved(STACK_OFFSET(st4H_off), NEXTREG(as_FloatRegister(4)));
    map->set_callee_saved(STACK_OFFSET(st5H_off), NEXTREG(as_FloatRegister(5)));
    map->set_callee_saved(STACK_OFFSET(st6H_off), NEXTREG(as_FloatRegister(6)));
    map->set_callee_saved(STACK_OFFSET(st7H_off), NEXTREG(as_FloatRegister(7)));
    map->set_callee_saved(STACK_OFFSET(xmm0H_off), NEXTREG(xmm0));
    map->set_callee_saved(STACK_OFFSET(xmm1H_off), NEXTREG(xmm1));
    map->set_callee_saved(STACK_OFFSET(xmm2H_off), NEXTREG(xmm2));
    map->set_callee_saved(STACK_OFFSET(xmm3H_off), NEXTREG(xmm3));
    map->set_callee_saved(STACK_OFFSET(xmm4H_off), NEXTREG(xmm4));
    map->set_callee_saved(STACK_OFFSET(xmm5H_off), NEXTREG(xmm5));
    map->set_callee_saved(STACK_OFFSET(xmm6H_off), NEXTREG(xmm6));
    map->set_callee_saved(STACK_OFFSET(xmm7H_off), NEXTREG(xmm7));
#undef NEXTREG
#undef STACK_OFFSET
  }

  return map;

}

void RegisterSaver::restore_live_registers(MacroAssembler* masm) {

  // Recover XMM & FPU state
  if( UseSSE == 1 ) {
    __ movflt(xmm0,Address(esp,xmm0_off*wordSize));
    __ movflt(xmm1,Address(esp,xmm1_off*wordSize));
    __ movflt(xmm2,Address(esp,xmm2_off*wordSize));
    __ movflt(xmm3,Address(esp,xmm3_off*wordSize));
    __ movflt(xmm4,Address(esp,xmm4_off*wordSize));
    __ movflt(xmm5,Address(esp,xmm5_off*wordSize));
    __ movflt(xmm6,Address(esp,xmm6_off*wordSize));
    __ movflt(xmm7,Address(esp,xmm7_off*wordSize));
  } else if( UseSSE >= 2 ) {
    __ movdbl(xmm0,Address(esp,xmm0_off*wordSize));
    __ movdbl(xmm1,Address(esp,xmm1_off*wordSize));
    __ movdbl(xmm2,Address(esp,xmm2_off*wordSize));
    __ movdbl(xmm3,Address(esp,xmm3_off*wordSize));
    __ movdbl(xmm4,Address(esp,xmm4_off*wordSize));
    __ movdbl(xmm5,Address(esp,xmm5_off*wordSize));
    __ movdbl(xmm6,Address(esp,xmm6_off*wordSize));
    __ movdbl(xmm7,Address(esp,xmm7_off*wordSize));
  }
  __ pop_FPU_state();
  __ addl(esp,FPU_regs_live*sizeof(jdouble)); // Pop FPU registers

  __ popfd();
  __ popad();
  // Get the ebp described implicitly by the frame sender code (no oopMap)
  __ popl(ebp);

}

void RegisterSaver::restore_result_registers(MacroAssembler* masm) {

  // Just restore result register. Only used by deoptimization. By
  // now any callee save register that needs to be restore to a c2
  // caller of the deoptee has been extracted into the vframeArray
  // and will be stuffed into the c2i adapter we create for later
  // restoration so only result registers need to be restored here.
  // 

  __ frstor(Address(esp));      // Restore fpu state 

  // Recover XMM & FPU state
  if( UseSSE == 1 ) {
    __ movflt(xmm0, Address(esp, xmm0_off*wordSize));
  } else if( UseSSE >= 2 ) {
    __ movdbl(xmm0, Address(esp, xmm0_off*wordSize));
  }
  __ movl(eax, Address(esp, eax_off*wordSize));
  __ movl(edx, Address(esp, edx_off*wordSize));
  // Pop all of the register save are off the stack except the return address
  __ addl(esp, return_off * wordSize); 
}

// The java_calling_convention describes stack locations as ideal slots on
// a frame with no abi restrictions. Since we must observe abi restrictions
// (like the placement of the register window) the slots must be biased by
// the following value.
static int reg2offset_in(VMReg r) { 
  // Account for saved ebp and return address
  // This should really be in_preserve_stack_slots
  return (r->reg2stack() + 2) * VMRegImpl::stack_slot_size;
}

static int reg2offset_out(VMReg r) { 
  return (r->reg2stack() + SharedRuntime::out_preserve_stack_slots()) * VMRegImpl::stack_slot_size;
}

// ---------------------------------------------------------------------------
// Read the array of BasicTypes from a signature, and compute where the
// arguments should go.  Values in the VMRegPair regs array refer to 4-byte
// quantities.  Values less than SharedInfo::stack0 are registers, those above
// refer to 4-byte stack slots.  All stack slots are based off of the stack pointer
// as framesizes are fixed.
// VMRegImpl::stack0 refers to the first slot 0(sp).
// and VMRegImpl::stack0+1 refers to the memory word 4-byes higher.  Register
// up to RegisterImpl::number_of_registers) are the 32-bit
// integer registers.  

// Pass first two oop/int args in registers ECX and EDX.
// Pass first two float/double args in registers XMM0 and XMM1.
// Doubles have precedence, so if you pass a mix of floats and doubles
// the doubles will grab the registers before the floats will.

// Note: the INPUTS in sig_bt are in units of Java argument words, which are
// either 32-bit or 64-bit depending on the build.  The OUTPUTS are in 32-bit
// units regardless of build. Of course for i486 there is no 64 bit build


// ---------------------------------------------------------------------------
// The compiled Java calling convention.  
// Pass first two oop/int args in registers ECX and EDX.
// Pass first two float/double args in registers XMM0 and XMM1.
// Doubles have precedence, so if you pass a mix of floats and doubles
// the doubles will grab the registers before the floats will.
int SharedRuntime::java_calling_convention(const BasicType *sig_bt,
                                           VMRegPair *regs,
                                           int total_args_passed,
                                           int is_outgoing) {
  uint    stack = 0;          // Starting stack position for args on stack
  
  
  // Pass first two oop/int args in registers ECX and EDX.
  uint reg_arg0 = 9999;
  uint reg_arg1 = 9999;
  
  // Pass first two float/double args in registers XMM0 and XMM1.
  // Doubles have precedence, so if you pass a mix of floats and doubles
  // the doubles will grab the registers before the floats will.
  // CNC - TURNED OFF FOR non-SSE.
  //       On Intel we have to round all doubles (and most floats) at
  //       call sites by storing to the stack in any case.
  // UseSSE=0 ==> Don't Use ==> 9999+0
  // UseSSE=1 ==> Floats only ==> 9999+1
  // UseSSE>=2 ==> Floats or doubles ==> 9999+2
  enum { fltarg_dontuse = 9999+0, fltarg_float_only = 9999+1, fltarg_flt_dbl = 9999+2 };
  uint fargs = (UseSSE>=2) ? 2 : UseSSE;
  uint freg_arg0 = 9999+fargs;
  uint freg_arg1 = 9999+fargs;
  
  // Pass doubles & longs aligned on the stack.  First count stack slots for doubles
  int i;
  for( i = 0; i < total_args_passed; i++) {
    if( sig_bt[i] == T_DOUBLE ) {
      // first 2 doubles go in registers
      if( freg_arg0 == fltarg_flt_dbl ) freg_arg0 = i;
      else if( freg_arg1 == fltarg_flt_dbl ) freg_arg1 = i;
      else // Else double is passed low on the stack to be aligned.
        stack += 2;
    } else if( sig_bt[i] == T_LONG ) {
      stack += 2;
    }
  }
  int dstack = 0;             // Separate counter for placing doubles
  
  // Now pick where all else goes.
  for( i = 0; i < total_args_passed; i++) {
    // From the type and the argument number (count) compute the location
    switch( sig_bt[i] ) {
    case T_SHORT:
    case T_CHAR:
    case T_BYTE:
    case T_BOOLEAN:
    case T_INT:
    case T_ARRAY:
    case T_OBJECT:
    case T_ADDRESS:
      if( reg_arg0 == 9999 )  {
        reg_arg0 = i;
        regs[i].set1(ecx->as_VMReg());
      } else if( reg_arg1 == 9999 )  {
        reg_arg1 = i;
        regs[i].set1(edx->as_VMReg());
      } else {
        regs[i].set1(VMRegImpl::stack2reg(stack++));
      }
      break;
    case T_FLOAT:
      if( freg_arg0 == fltarg_flt_dbl || freg_arg0 == fltarg_float_only ) {
        freg_arg0 = i;
        regs[i].set1(xmm0->as_VMReg());
      } else if( freg_arg1 == fltarg_flt_dbl || freg_arg1 == fltarg_float_only ) {
        freg_arg1 = i;
        regs[i].set1(xmm1->as_VMReg());
      } else {
        regs[i].set1(VMRegImpl::stack2reg(stack++));
      }
      break;
    case T_LONG:      
      assert(sig_bt[i+1] == T_VOID, "missing Half" ); 
      regs[i].set2(VMRegImpl::stack2reg(dstack));
      dstack += 2;
      break;
    case T_DOUBLE:
      assert(sig_bt[i+1] == T_VOID, "missing Half" ); 
      if( freg_arg0 == (uint)i ) {
        regs[i].set2(xmm0->as_VMReg());
      } else if( freg_arg1 == (uint)i ) {
        regs[i].set2(xmm1->as_VMReg());
      } else {
        regs[i].set2(VMRegImpl::stack2reg(dstack));
        dstack += 2;
      }
      break;
    case T_VOID: regs[i].set_bad(); break;
      break;
    default:
      ShouldNotReachHere();
      break;
    }
  }

  // return value can be odd number of VMRegImpl stack slots make multiple of 2
  return round_to(stack, 2);
}

// Patch the callers callsite with entry to compiled code if it exists.
static void patch_callers_callsite(MacroAssembler *masm) {
  Label L;
  __ verify_oop(ebx);
  __ cmpl(Address(ebx, in_bytes(methodOopDesc::code_offset())), NULL_WORD);
  __ jcc(Assembler::equal, L);
  // Schedule the branch target address early.
  // Call into the VM to patch the caller, then jump to compiled callee
  // eax isn't live so capture return address while we easily can
  __ movl(eax, Address(esp, 0));
  __ pushad();
  __ pushfd();

  if (UseSSE == 1) {
    __ subl(esp, 2*wordSize);
    __ movflt(Address(esp, 0), xmm0);
    __ movflt(Address(esp, wordSize), xmm1);
  }
  if (UseSSE >= 2) {
    __ subl(esp, 4*wordSize);
    __ movdbl(Address(esp, 0), xmm0);
    __ movdbl(Address(esp, 2*wordSize), xmm1);
  }
#ifdef COMPILER2
  // C2 may leave the stack dirty if not in SSE2+ mode
  if (UseSSE >= 2) {
    __ verify_FPU(0, "c2i transition should have clean FPU stack");
  } else {
    __ empty_FPU_stack();
  }
#endif /* COMPILER2 */

  // VM needs caller's callsite
  __ pushl(eax);
  // VM needs target method
  __ pushl(ebx);
  __ verify_oop(ebx);
  __ call(CAST_FROM_FN_PTR(address, SharedRuntime::fixup_callers_callsite), relocInfo::runtime_call_type);
  __ addl(esp, 2*wordSize);

  if (UseSSE == 1) {
    __ movflt(xmm0, Address(esp, 0));
    __ movflt(xmm1, Address(esp, wordSize));
    __ addl(esp, 2*wordSize);
  }
  if (UseSSE >= 2) {
    __ movdbl(xmm0, Address(esp, 0));
    __ movdbl(xmm1, Address(esp, 2*wordSize));
    __ addl(esp, 4*wordSize);
  }

  __ popfd();
  __ popad();
  __ bind(L);
}


// Helper function to put tags in interpreter stack.
static void  tag_stack(MacroAssembler *masm, const BasicType sig, int st_off) {
  if (TaggedStackInterpreter) {
    int tag_offset = st_off + Interpreter::expr_tag_offset_in_bytes(0);
    if (sig == T_OBJECT || sig == T_ARRAY) {
      __ movl(Address(esp, tag_offset), frame::TagReference);
    } else if (sig == T_LONG || sig == T_DOUBLE) {
      int next_tag_offset = st_off + Interpreter::expr_tag_offset_in_bytes(1);
      __ movl(Address(esp, next_tag_offset), frame::TagValue);
      __ movl(Address(esp, tag_offset), frame::TagValue);
    } else {
      __ movl(Address(esp, tag_offset), frame::TagValue);
    }
  }
}

// Double and long values with Tagged stacks are not contiguous.
static void move_c2i_double(MacroAssembler *masm, XMMRegister r, int st_off) {
  int next_off = st_off - Interpreter::stackElementSize();
  if (TaggedStackInterpreter) {
   __ movdbl(Address(esp, next_off), r);
   // Move top half up and put tag in the middle.
   __ movl(edi, Address(esp, next_off+wordSize));
   __ movl(Address(esp, st_off), edi);
   tag_stack(masm, T_DOUBLE, next_off);
  } else {
   __ movdbl(Address(esp, next_off), r);
  }
}

static void gen_c2i_adapter(MacroAssembler *masm,
                            int total_args_passed,
                            int comp_args_on_stack,
                            const BasicType *sig_bt,
                            const VMRegPair *regs,
                            Label& skip_fixup) {
  // Before we get into the guts of the C2I adapter, see if we should be here
  // at all.  We've come from compiled code and are attempting to jump to the
  // interpreter, which means the caller made a static call to get here
  // (vcalls always get a compiled target if there is one).  Check for a
  // compiled target.  If there is one, we need to patch the caller's call.
  patch_callers_callsite(masm);

  __ bind(skip_fixup);

#ifdef COMPILER2
  // C2 may leave the stack dirty if not in SSE2+ mode
  if (UseSSE >= 2) {
    __ verify_FPU(0, "c2i transition should have clean FPU stack");
  } else {
    __ empty_FPU_stack();
  }
#endif /* COMPILER2 */

  // Since all args are passed on the stack, total_args_passed * interpreter_
  // stack_element_size  is the
  // space we need.
  int extraspace = total_args_passed * Interpreter::stackElementSize();

  // Get return address
  __ popl(eax);

  // set senderSP value
  __ movl(esi, esp);

  __ subl(esp, extraspace);

  // Now write the args into the outgoing interpreter space
  for (int i = 0; i < total_args_passed; i++) {
    if (sig_bt[i] == T_VOID) {
      assert(i > 0 && (sig_bt[i-1] == T_LONG || sig_bt[i-1] == T_DOUBLE), "missing half");
      continue;
    }

    // st_off points to lowest address on stack.
    int st_off = ((total_args_passed - 1) - i) * Interpreter::stackElementSize();
    // Say 4 args:
    // i   st_off
    // 0   12 T_LONG
    // 1    8 T_VOID
    // 2    4 T_OBJECT
    // 3    0 T_BOOL
    VMReg r_1 = regs[i].first();
    VMReg r_2 = regs[i].second();
    if (!r_1->is_valid()) {
      assert(!r_2->is_valid(), "");
      continue;
    }

    if (r_1->is_stack()) { 
      // memory to memory use fpu stack top
      int ld_off = r_1->reg2stack() * VMRegImpl::stack_slot_size + extraspace;

      if (!r_2->is_valid()) {
        __ movl(edi, Address(esp, ld_off));
        __ movl(Address(esp, st_off), edi);
        tag_stack(masm, sig_bt[i], st_off);
      } else {

        // ld_off == LSW, ld_off+VMRegImpl::stack_slot_size == MSW
        // st_off == MSW, st_off-wordSize == LSW

        int next_off = st_off - Interpreter::stackElementSize();
        __ movl(edi, Address(esp, ld_off));
        __ movl(Address(esp, next_off), edi);
        __ movl(edi, Address(esp, ld_off + wordSize));
        __ movl(Address(esp, st_off), edi);
        tag_stack(masm, sig_bt[i], next_off);
      }
    } else if (r_1->is_Register()) {
      Register r = r_1->as_Register();
      if (!r_2->is_valid()) {
        __ movl(Address(esp, st_off), r);
        tag_stack(masm, sig_bt[i], st_off);
      } else {
        // long/double in gpr
        ShouldNotReachHere();
      }
    } else {
      assert(r_1->is_XMMRegister(), "");
      if (!r_2->is_valid()) {
        __ movflt(Address(esp, st_off), r_1->as_XMMRegister());
        tag_stack(masm, sig_bt[i], st_off);
      } else {
        assert(sig_bt[i] == T_DOUBLE || sig_bt[i] == T_LONG, "wrong type");
        move_c2i_double(masm, r_1->as_XMMRegister(), st_off);
      }
    }
  }

  // Schedule the branch target address early.
  __ movl(ecx, Address(ebx, in_bytes(methodOopDesc::interpreter_entry_offset())));
  // And repush original return address
  __ pushl(eax);
  __ jmp(ecx);
}


// For tagged stacks, double or long value aren't contiguous on the stack
// so get them contiguous for the xmm load
static void move_i2c_double(MacroAssembler *masm, XMMRegister r, Register saved_sp, int ld_off) {
  int next_val_off = ld_off - Interpreter::stackElementSize();
  if (TaggedStackInterpreter) {
    // use tag slot temporarily for MSW
    __ movl(esi, Address(saved_sp, ld_off));
    __ movl(Address(saved_sp, next_val_off+wordSize), esi);
    __ movdbl(r, Address(saved_sp, next_val_off));
    // restore tag
    __ movl(Address(saved_sp, next_val_off+wordSize), frame::TagValue);
  } else {
    __ movdbl(r, Address(saved_sp, next_val_off));
  }
}

static void gen_i2c_adapter(MacroAssembler *masm,
                            int total_args_passed,
                            int comp_args_on_stack,
                            const BasicType *sig_bt,
                            const VMRegPair *regs) {
  // we're being called from the interpreter but need to find the
  // compiled return entry point.  The return address on the stack
  // should point at it and we just need to pull the old value out.
  // load up the pointer to the compiled return entry point and
  // rewrite our return pc. The code is arranged like so:
  //
  // .word Interpreter::return_sentinel
  // .word address_of_compiled_return_point
  // return_entry_point: blah_blah_blah
  // 
  // So we can find the appropriate return point by loading up the word
  // just prior to the current return address we have on the stack.
  //
  // We will only enter here from an interpreted frame and never from after
  // passing thru a c2i. Azul allowed this but we do not. If we lose the
  // race and use a c2i we will remain interpreted for the race loser(s).
  // This removes all sorts of headaches on the x86 side and also eliminates
  // the possibility of having c2i -> i2c -> c2i -> ... endless transitions.


  // Note: esi contains the senderSP on entry. We must preserve it since
  // we may do a i2c -> c2i transition if we lose a race where compiled
  // code goes non-entrant while we get args ready.

  // Pick up the return address
  __ movl(eax, Address(esp));

  // If UseSSE >= 2 then no cleanup is needed on the return to the
  // interpreter so skip fixing up the return entry point unless
  // VerifyFPU is enabled.
  if (UseSSE < 2 || VerifyFPU) {
    Label skip, chk_int;
    // If we were called from the call stub we need to do a little bit different
    // cleanup than if the interpreter returned to the call stub.

    __ cmpl(eax, (int)StubRoutines::_call_stub_return_address);
    __ jcc(Assembler::notEqual, chk_int);
    assert(StubRoutines::i486::get_call_stub_compiled_return() != NULL, "must be set");
    __ movl(eax, (intptr_t) StubRoutines::i486::get_call_stub_compiled_return());
    __ jmp(skip);

    // It must be the interpreter since we never get here via a c2i (unlike Azul)

    __ bind(chk_int);
#ifdef ASSERT
    {
      Label ok;
      __ cmpl(Address(eax, -8), Interpreter::return_sentinel);
      __ jcc(Assembler::equal, ok);
      __ int3();
      __ bind(ok);
    }
#endif
    __ movl(eax, Address(eax, -4));
    __ bind(skip);
  }

  // eax now contains the compiled return entry point which will do an
  // cleanup needed for the return from compiled to interpreted.

  // Must preserve original SP for loading incoming arguments because
  // we need to align the outgoing SP for compiled code.
  __ movl(edi, esp);

  // Cut-out for having no stack args.  Since up to 2 int/oop args are passed
  // in registers, we will occasionally have no stack args.
  int comp_words_on_stack = 0;
  if (comp_args_on_stack) {
    // Sig words on the stack are greater-than VMRegImpl::stack0.  Those in
    // registers are below.  By subtracting stack0, we either get a negative
    // number (all values in registers) or the maximum stack slot accessed.
    // int comp_args_on_stack = VMRegImpl::reg2stack(max_arg);
    // Convert 4-byte stack slots to words.
    comp_words_on_stack = round_to(comp_args_on_stack*4, wordSize)>>LogBytesPerWord;
    // Round up to miminum stack alignment, in wordSize
    comp_words_on_stack = round_to(comp_words_on_stack, 2);
    __ subl(esp, comp_words_on_stack * wordSize);
  }

  // Align the outgoing SP
  __ andl(esp, -(StackAlignmentInBytes));

  // push the return address on the stack (note that pushing, rather
  // than storing it, yields the correct frame alignment for the callee)
  __ pushl(eax);

  // Put saved SP in another register
  const Register saved_sp = eax;
  __ movl(saved_sp, edi);


  // Will jump to the compiled code just as if compiled code was doing it.
  // Pre-load the register-jump target early, to schedule it better.
  __ movl(edi, Address(ebx, in_bytes(methodOopDesc::from_compiled_offset())));

  // Now generate the shuffle code.  Pick up all register args and move the
  // rest through the floating point stack top.
  for (int i = 0; i < total_args_passed; i++) {
    if (sig_bt[i] == T_VOID) {
      // Longs and doubles are passed in native word order, but misaligned
      // in the 32-bit build.
      assert(i > 0 && (sig_bt[i-1] == T_LONG || sig_bt[i-1] == T_DOUBLE), "missing half");
      continue;
    }

    // Pick up 0, 1 or 2 words from SP+offset.  
    
    assert(!regs[i].second()->is_valid() || regs[i].first()->next() == regs[i].second(),
            "scrambled load targets?");
    // Load in argument order going down.
    int ld_off = (total_args_passed - i)*Interpreter::stackElementSize() + Interpreter::value_offset_in_bytes();
    // Point to interpreter value (vs. tag)
    int next_off = ld_off - Interpreter::stackElementSize();
    //
    //  
    //
    VMReg r_1 = regs[i].first();
    VMReg r_2 = regs[i].second();
    if (!r_1->is_valid()) {
      assert(!r_2->is_valid(), "");
      continue;
    }
    if (r_1->is_stack()) { 
      // Convert stack slot to an SP offset (+ wordSize to account for return address )
      int st_off = regs[i].first()->reg2stack()*VMRegImpl::stack_slot_size + wordSize;

      // We can use esi as a temp here because compiled code doesn't need esi as an input
      // and if we end up going thru a c2i because of a miss a reasonable value of esi
      // we be generated. 
      if (!r_2->is_valid()) {
        // __ fld_s(Address(saved_sp, ld_off));
        // __ fstp_s(Address(esp, st_off));
        __ movl(esi, Address(saved_sp, ld_off));
        __ movl(Address(esp, st_off), esi);
      } else {
        // Interpreter local[n] == MSW, local[n+1] == LSW however locals
        // are accessed as negative so LSW is at LOW address

        // ld_off is MSW so get LSW
        // st_off is LSW (i.e. reg.first())
        // __ fld_d(Address(saved_sp, next_off));
        // __ fstp_d(Address(esp, st_off));
        __ movl(esi, Address(saved_sp, next_off));
        __ movl(Address(esp, st_off), esi);
        __ movl(esi, Address(saved_sp, ld_off));
        __ movl(Address(esp, st_off + wordSize), esi);
      }
    } else if (r_1->is_Register()) {  // Register argument
      Register r = r_1->as_Register();
      assert(r != eax, "must be different");
      if (r_2->is_valid()) {
        assert(r_2->as_Register() != eax, "need another temporary register");
        // Remember r_1 is low address (and LSB on x86)
        // So r_2 gets loaded from high address regardless of the platform
        __ movl(r_2->as_Register(), Address(saved_sp, ld_off));
        __ movl(r, Address(saved_sp, next_off));
      } else {
        __ movl(r, Address(saved_sp, ld_off));
      }
    } else {
      assert(r_1->is_XMMRegister(), "");
      if (!r_2->is_valid()) {
        __ movflt(r_1->as_XMMRegister(), Address(saved_sp, ld_off));
      } else {
        move_i2c_double(masm, r_1->as_XMMRegister(), saved_sp, ld_off);
      }
    }
  }

  // 6243940 We might end up in handle_wrong_method if
  // the callee is deoptimized as we race thru here. If that
  // happens we don't want to take a safepoint because the
  // caller frame will look interpreted and arguments are now
  // "compiled" so it is much better to make this transition
  // invisible to the stack walking code. Unfortunately if
  // we try and find the callee by normal means a safepoint
  // is possible. So we stash the desired callee in the thread
  // and the vm will find there should this case occur.

  __ get_thread(eax);
  __ movl(Address(eax, JavaThread::callee_target_offset()), ebx);

  // move methodOop to eax in case we end up in an c2i adapter.
  // the c2i adapters expect methodOop in eax (c2) because c2's
  // resolve stubs return the result (the method) in eax.
  // I'd love to fix this. 
  __ movl(eax, ebx);

  __ jmp(edi);
}

// ---------------------------------------------------------------
AdapterHandlerEntry* SharedRuntime::generate_i2c2i_adapters(MacroAssembler *masm,
                                                            int total_args_passed,
                                                            int comp_args_on_stack,
                                                            const BasicType *sig_bt,
                                                            const VMRegPair *regs) {
  address i2c_entry = __ pc();

  gen_i2c_adapter(masm, total_args_passed, comp_args_on_stack, sig_bt, regs);

  // -------------------------------------------------------------------------
  // Generate a C2I adapter.  On entry we know ebx holds the methodOop during calls
  // to the interpreter.  The args start out packed in the compiled layout.  They
  // need to be unpacked into the interpreter layout.  This will almost always
  // require some stack space.  We grow the current (compiled) stack, then repack
  // the args.  We  finally end in a jump to the generic interpreter entry point.
  // On exit from the interpreter, the interpreter will restore our SP (lest the
  // compiled code, which relys solely on SP and not EBP, get sick).

  address c2i_unverified_entry = __ pc();
  Label skip_fixup;

  Register holder = eax;
  Register receiver = ecx;
  Register temp = ebx;

  {
    address ic_miss = SharedRuntime::get_ic_miss_stub();

    Label missed;

    __ verify_oop(holder);
    __ movl(temp, Address(receiver, oopDesc::klass_offset_in_bytes()));
    __ verify_oop(temp);
    
    __ cmpl(temp, Address(holder, compiledICHolderOopDesc::holder_klass_offset()));
    __ movl(ebx, Address(holder, compiledICHolderOopDesc::holder_method_offset()));
    __ jcc(Assembler::notEqual, missed);
    // Method might have been compiled since the call site was patched to
    // interpreted if that is the case treat it as a miss so we can get
    // the call site corrected.
    __ cmpl(Address(ebx, in_bytes(methodOopDesc::code_offset())), NULL_WORD);
    __ jcc(Assembler::equal, skip_fixup);

    __ bind(missed);
    __ jmp(ic_miss, relocInfo::runtime_call_type);
  }

  address c2i_entry = __ pc();

  gen_c2i_adapter(masm, total_args_passed, comp_args_on_stack, sig_bt, regs, skip_fixup);

  __ flush();
  return new AdapterHandlerEntry(i2c_entry, c2i_entry, c2i_unverified_entry);
}

int SharedRuntime::c_calling_convention(const BasicType *sig_bt, 
                                         VMRegPair *regs,
                                         int total_args_passed) {
// We return the amount of VMRegImpl stack slots we need to reserve for all
// the arguments NOT counting out_preserve_stack_slots. 

  uint    stack = 0;        // All arguments on stack

  for( int i = 0; i < total_args_passed; i++) {
    // From the type and the argument number (count) compute the location
    switch( sig_bt[i] ) {
    case T_BOOLEAN:
    case T_CHAR:
    case T_FLOAT:
    case T_BYTE:
    case T_SHORT:
    case T_INT:
    case T_OBJECT:
    case T_ARRAY:
    case T_ADDRESS:
      regs[i].set1(VMRegImpl::stack2reg(stack++));
      break;
    case T_LONG:
    case T_DOUBLE: // The stack numbering is reversed from Java
      // Since C arguments do not get reversed, the ordering for
      // doubles on the stack must be opposite the Java convention
      assert(sig_bt[i+1] == T_VOID, "missing Half" ); 
      regs[i].set2(VMRegImpl::stack2reg(stack));
      stack += 2;
      break;
    case T_VOID: regs[i].set_bad(); break;
    default:
      ShouldNotReachHere();
      break;
    }
  }
  return stack;
}

// A simple move of integer like type
static void simple_move32(MacroAssembler* masm, VMRegPair src, VMRegPair dst) {
  if (src.first()->is_stack()) {
    if (dst.first()->is_stack()) {
      // stack to stack
      // __ ld(FP, reg2offset(src.first()) + STACK_BIAS, L5);
      // __ st(L5, SP, reg2offset(dst.first()) + STACK_BIAS);
      __ movl(eax, Address(ebp, reg2offset_in(src.first())));
      __ movl(Address(esp, reg2offset_out(dst.first())), eax);
    } else {
      // stack to reg
      __ movl(dst.first()->as_Register(),  Address(ebp, reg2offset_in(src.first())));
    }
  } else if (dst.first()->is_stack()) {
    // reg to stack
    __ movl(Address(esp, reg2offset_out(dst.first())), src.first()->as_Register());
  } else {
    __ movl(dst.first()->as_Register(), src.first()->as_Register());
  }
}

// An oop arg. Must pass a handle not the oop itself
static void object_move(MacroAssembler* masm, 
                        OopMap* map,
                        int oop_handle_offset,
                        int framesize_in_slots,
                        VMRegPair src,
                        VMRegPair dst,
                        bool is_receiver,
                        int* receiver_offset) {

  // Because of the calling conventions we know that src can be a 
  // register or a stack location. dst can only be a stack location.

  assert(dst.first()->is_stack(), "must be stack");
  // must pass a handle. First figure out the location we use as a handle

  if (src.first()->is_stack()) {
    // Oop is already on the stack as an argument
    Register rHandle = eax;
    Label nil;
    __ xorl(rHandle, rHandle);
    __ cmpl(Address(ebp, reg2offset_in(src.first())), NULL_WORD);
    __ jcc(Assembler::equal, nil);
    __ leal(rHandle, Address(ebp, reg2offset_in(src.first())));
    __ bind(nil);
    __ movl(Address(esp, reg2offset_out(dst.first())), rHandle);

    int offset_in_older_frame = src.first()->reg2stack() + SharedRuntime::out_preserve_stack_slots();
    map->set_oop(VMRegImpl::stack2reg(offset_in_older_frame + framesize_in_slots));
    if (is_receiver) {
      *receiver_offset = (offset_in_older_frame + framesize_in_slots) * VMRegImpl::stack_slot_size;
    }
  } else {
    // Oop is in an a register we must store it to the space we reserve
    // on the stack for oop_handles
    const Register rOop = src.first()->as_Register();
    const Register rHandle = eax;
    int oop_slot = (rOop == ecx ? 0 : 1) * VMRegImpl::slots_per_word + oop_handle_offset;
    int offset = oop_slot*VMRegImpl::stack_slot_size;
    Label skip;
    __ movl(Address(esp, offset), rOop);
    map->set_oop(VMRegImpl::stack2reg(oop_slot));
    __ xorl(rHandle, rHandle);
    __ cmpl(rOop, NULL_WORD);
    __ jcc(Assembler::equal, skip);
    __ leal(rHandle, Address(esp, offset));
    __ bind(skip);
    // Store the handle parameter
    __ movl(Address(esp, reg2offset_out(dst.first())), rHandle);
    if (is_receiver) {
      *receiver_offset = offset;
    }
  }
}

// A float arg may have to do float reg int reg conversion
static void float_move(MacroAssembler* masm, VMRegPair src, VMRegPair dst) {
  assert(!src.second()->is_valid() && !dst.second()->is_valid(), "bad float_move");

  // Because of the calling convention we know that src is either a stack location
  // or an xmm register. dst can only be a stack location.

  assert(dst.first()->is_stack() && ( src.first()->is_stack() || src.first()->is_XMMRegister()), "bad parameters");

  if (src.first()->is_stack()) {
    __ movl(eax, Address(ebp, reg2offset_in(src.first())));
    __ movl(Address(esp, reg2offset_out(dst.first())), eax);
  } else {
    // reg to stack
    __ movflt(Address(esp, reg2offset_out(dst.first())), src.first()->as_XMMRegister());
  }
}

// A long move
static void long_move(MacroAssembler* masm, VMRegPair src, VMRegPair dst) {

  // The only legal possibility for a long_move VMRegPair is:
  // 1: two stack slots (possibly unaligned)
  // as neither the java  or C calling convention will use registers
  // for longs.

  if (src.first()->is_stack() && dst.first()->is_stack()) {
    assert(src.second()->is_stack() && dst.second()->is_stack(), "must be all stack");
    __ movl(eax, Address(ebp, reg2offset_in(src.first())));
    __ movl(ebx, Address(ebp, reg2offset_in(src.second())));
    __ movl(Address(esp, reg2offset_out(dst.first())), eax);
    __ movl(Address(esp, reg2offset_out(dst.second())), ebx);
  } else {
    ShouldNotReachHere();
  }
}

// A double move
static void double_move(MacroAssembler* masm, VMRegPair src, VMRegPair dst) {

  // The only legal possibilities for a double_move VMRegPair are:
  // The painful thing here is that like long_move a VMRegPair might be

  // Because of the calling convention we know that src is either
  //   1: a single physical register (xmm registers only)
  //   2: two stack slots (possibly unaligned)
  // dst can only be a pair of stack slots.

  assert(dst.first()->is_stack() && (src.first()->is_XMMRegister() || src.first()->is_stack()), "bad args");

  if (src.first()->is_stack()) {
    // source is all stack
    __ movl(eax, Address(ebp, reg2offset_in(src.first())));
    __ movl(ebx, Address(ebp, reg2offset_in(src.second())));
    __ movl(Address(esp, reg2offset_out(dst.first())), eax);
    __ movl(Address(esp, reg2offset_out(dst.second())), ebx);
  } else {
    // reg to stack
    // No worries about stack alignment
    __ movdbl(Address(esp, reg2offset_out(dst.first())), src.first()->as_XMMRegister());
  }
}


void SharedRuntime::save_native_result(MacroAssembler *masm, BasicType ret_type, int frame_slots) {
  // We always ignore the frame_slots arg and just use the space just below frame pointer
  // which by this time is free to use
  switch (ret_type) {
  case T_FLOAT:
    __ fstp_s(Address(ebp, -wordSize));
    break;
  case T_DOUBLE:
    __ fstp_d(Address(ebp, -2*wordSize));
    break;
  case T_VOID:  break;
  case T_LONG:
    __ movl(Address(ebp, -wordSize), eax);
    __ movl(Address(ebp, -2*wordSize), edx);
    break;
  default: {
    __ movl(Address(ebp, -wordSize), eax);
    }
  }
}

void SharedRuntime::restore_native_result(MacroAssembler *masm, BasicType ret_type, int frame_slots) {
  // We always ignore the frame_slots arg and just use the space just below frame pointer
  // which by this time is free to use
  switch (ret_type) {
  case T_FLOAT:
    __ fld_s(Address(ebp, -wordSize));
    break;
  case T_DOUBLE:
    __ fld_d(Address(ebp, -2*wordSize));
    break;
  case T_LONG:
    __ movl(eax, Address(ebp, -wordSize));
    __ movl(edx, Address(ebp, -2*wordSize));
    break;
  case T_VOID:  break;
  default: {
    __ movl(eax, Address(ebp, -wordSize));
    }
  }
}

// ---------------------------------------------------------------------------
// Generate a native wrapper for a given method.  The method takes arguments
// in the Java compiled code convention, marshals them to the native
// convention (handlizes oops, etc), transitions to native, makes the call,
// returns to java state (possibly blocking), unhandlizes any result and
// returns.
nmethod *SharedRuntime::generate_native_wrapper(MacroAssembler *masm,
                                                methodHandle method,
                                                int total_in_args,
                                                int comp_args_on_stack,
                                                BasicType *in_sig_bt,
                                                VMRegPair *in_regs,
                                                BasicType ret_type) {

  // An OopMap for lock (and class if static)
  OopMapSet *oop_maps = new OopMapSet();

  // We have received a description of where all the java arg are located
  // on entry to the wrapper. We need to convert these args to where
  // the jni function will expect them. To figure out where they go
  // we convert the java signature to a C signature by inserting
  // the hidden arguments as arg[0] and possibly arg[1] (static method)

  int total_c_args = total_in_args + 1;
  if (method->is_static()) {
    total_c_args++;
  }

  BasicType* out_sig_bt = NEW_RESOURCE_ARRAY(BasicType, total_c_args);
  VMRegPair* out_regs   = NEW_RESOURCE_ARRAY(VMRegPair,   total_c_args);

  int argc = 0;
  out_sig_bt[argc++] = T_ADDRESS;
  if (method->is_static()) {
    out_sig_bt[argc++] = T_OBJECT;
  }

  int i;
  for (i = 0; i < total_in_args ; i++ ) {
    out_sig_bt[argc++] = in_sig_bt[i];
  }


  // Now figure out where the args must be stored and how much stack space
  // they require (neglecting out_preserve_stack_slots but space for storing
  // the 1st six register arguments). It's weird see int_stk_helper.
  // 
  int out_arg_slots;
  out_arg_slots = c_calling_convention(out_sig_bt, out_regs, total_c_args);

  // Compute framesize for the wrapper.  We need to handlize all oops in
  // registers a max of 2 on x86. 

  // Calculate the total number of stack slots we will need.

  // First count the abi requirement plus all of the outgoing args
  int stack_slots = SharedRuntime::out_preserve_stack_slots() + out_arg_slots;

  // Now the space for the inbound oop handle area

  int oop_handle_offset = stack_slots;
  stack_slots += 2*VMRegImpl::slots_per_word;

  // Now any space we need for handlizing a klass if static method

  int klass_slot_offset = 0;
  int klass_offset = -1;
  int lock_slot_offset = 0;
  bool is_static = false;
  int oop_temp_slot_offset = 0;

  if (method->is_static()) {
    klass_slot_offset = stack_slots;
    stack_slots += VMRegImpl::slots_per_word;
    klass_offset = klass_slot_offset * VMRegImpl::stack_slot_size;
    is_static = true;
  }

  // Plus a lock if needed

  if (method->is_synchronized()) {
    lock_slot_offset = stack_slots;
    stack_slots += VMRegImpl::slots_per_word;
  }

  // Now a place (+2) to save return values or temp during shuffling
  // + 2 for return address (which we own) and saved ebp
  stack_slots += 4;

  // Ok The space we have allocated will look like:
  //
  //
  // FP-> |                     |
  //      |---------------------|
  //      | 2 slots for moves   |
  //      |---------------------| 
  //      | lock box (if sync)  |
  //      |---------------------| <- lock_slot_offset  (-lock_slot_ebp_offset)
  //      | klass (if static)   |
  //      |---------------------| <- klass_slot_offset
  //      | oopHandle area      |
  //      |---------------------| <- oop_handle_offset (a max of 2 registers)
  //      | outbound memory     |
  //      | based arguments     |
  //      |                     |
  //      |---------------------| 
  //      |                     |
  // SP-> | out_preserved_slots |
  //
  //
  // ****************************************************************************
  // WARNING - on Windows Java Natives use pascal calling convention and pop the
  // arguments off of the stack after the jni call. Before the call we can use
  // instructions that are SP relative. After the jni call we switch to FP
  // relative instructions instead of re-adjusting the stack on windows.
  // ****************************************************************************


  // Now compute actual number of stack words we need rounding to make 
  // stack properly aligned.
  stack_slots = round_to(stack_slots, 2 * VMRegImpl::slots_per_word);

  int stack_size = stack_slots * VMRegImpl::stack_slot_size;

  intptr_t start = (intptr_t)__ pc();

  // First thing make an ic check to see if we should even be here
  address ic_miss = SharedRuntime::get_ic_miss_stub();

  // We are free to use all registers as temps without saving them and
  // restoring them except ebp. ebp is the only callee save register
  // as far as the interpreter and the compiler(s) are concerned.


  const Register ic_reg = eax;
  const Register receiver = ecx;
  Label hit;
  Label exception_pending;


  __ verify_oop(receiver);
  __ cmpl(ic_reg, Address(receiver, oopDesc::klass_offset_in_bytes()));
  __ jcc(Assembler::equal, hit);
  __ jmp(ic_miss, relocInfo::runtime_call_type);

  // verified entry must be aligned for code patching.
  // and the first 5 bytes must be in the same cache line
  // if we align at 8 then we will be sure 5 bytes are in the same line
  __ align(8);

  __ bind(hit);

  int vep_offset = ((intptr_t)__ pc()) - start;

#ifdef COMPILER1
  if (InlineObjectHash && method->intrinsic_id() == vmIntrinsics::_hashCode) {
    // Object.hashCode can pull the hashCode from the header word
    // instead of doing a full VM transition once it's been computed.
    // Since hashCode is usually polymorphic at call sites we can't do
    // this optimization at the call site without a lot of work.
    Label slowCase;
    Register receiver = ecx;
    Register result = eax;
    __ movl(result, Address(receiver, oopDesc::mark_offset_in_bytes()));
    
    // check if locked
    __ testl (result, markOopDesc::unlocked_value);
    __ jcc (Assembler::zero, slowCase);
    
    if (UseBiasedLocking) {
      // Check if biased and fall through to runtime if so
      __ testl (result, markOopDesc::biased_lock_bit_in_place);
      __ jcc (Assembler::notZero, slowCase);
    }

    // get hash
    __ andl (result, markOopDesc::hash_mask_in_place);
    // test if hashCode exists
    __ jcc  (Assembler::zero, slowCase);
    __ shrl (result, markOopDesc::hash_shift);
    __ ret(0);
    __ bind (slowCase);
  }
#endif // COMPILER1

  // The instruction at the verified entry point must be 5 bytes or longer
  // because it can be patched on the fly by make_non_entrant. The stack bang
  // instruction fits that requirement. 

  // Generate stack overflow check
  
  if (UseStackBanging) {
    __ bang_stack_with_offset(StackShadowPages*os::vm_page_size());
  } else {
    // need a 5 byte instruction to allow MT safe patching to non-entrant
    __ fat_nop();
  }

  // Generate a new frame for the wrapper.
  __ enter();
  // -2 because return address is already present and so is saved ebp
  __ subl(esp, stack_size - 2*wordSize);

  // Frame is now completed as far a size and linkage.

  int frame_complete = ((intptr_t)__ pc()) - start;

  // Calculate the difference between esp and ebp. We need to know it
  // after the native call because on windows Java Natives will pop
  // the arguments and it is painful to do esp relative addressing
  // in a platform independent way. So after the call we switch to
  // ebp relative addressing.

  int fp_adjustment = stack_size - 2*wordSize;

#ifdef COMPILER2
  // C2 may leave the stack dirty if not in SSE2+ mode
  if (UseSSE >= 2) {
    __ verify_FPU(0, "c2i transition should have clean FPU stack");
  } else {
    __ empty_FPU_stack();
  }
#endif /* COMPILER2 */

  // Compute the ebp offset for any slots used after the jni call

  int lock_slot_ebp_offset = (lock_slot_offset*VMRegImpl::stack_slot_size) - fp_adjustment;
  int oop_temp_slot_ebp_offset = (oop_temp_slot_offset*VMRegImpl::stack_slot_size) - fp_adjustment;

  // We use edi as a thread pointer because it is callee save and
  // if we load it once it is usable thru the entire wrapper
  const Register thread = edi;

  // We use esi as the oop handle for the receiver/klass
  // It is callee save so it survives the call to native

  const Register oop_handle_reg = esi;

  __ get_thread(thread);


  //
  // We immediately shuffle the arguments so that any vm call we have to
  // make from here on out (sync slow path, jvmti, etc.) we will have
  // captured the oops from our caller and have a valid oopMap for
  // them.

  // -----------------
  // The Grand Shuffle 
  //
  // Natives require 1 or 2 extra arguments over the normal ones: the JNIEnv*
  // and, if static, the class mirror instead of a receiver.  This pretty much
  // guarantees that register layout will not match (and x86 doesn't use reg
  // parms though amd does).  Since the native abi doesn't use register args
  // and the java conventions does we don't have to worry about collisions.
  // All of our moved are reg->stack or stack->stack.
  // We ignore the extra arguments during the shuffle and handle them at the
  // last moment. The shuffle is described by the two calling convention
  // vectors we have in our possession. We simply walk the java vector to
  // get the source locations and the c vector to get the destinations.

  int c_arg = method->is_static() ? 2 : 1 ;

  // Record esp-based slot for receiver on stack for non-static methods
  int receiver_offset = -1;

  // This is a trick. We double the stack slots so we can claim
  // the oops in the caller's frame. Since we are sure to have
  // more args than the caller doubling is enough to make
  // sure we can capture all the incoming oop args from the
  // caller. 
  //
  OopMap* map = new OopMap(stack_slots * 2, 0 /* arg_slots*/);

  // Mark location of ebp
  // map->set_callee_saved(VMRegImpl::stack2reg( stack_slots - 2), stack_slots * 2, 0, ebp->as_VMReg());

  // We know that we only have args in at most two integer registers (ecx, edx). So eax, ebx
  // Are free to temporaries if we have to do  stack to steck moves.
  // All inbound args are referenced based on ebp and all outbound args via esp.

  for (i = 0; i < total_in_args ; i++, c_arg++ ) {
    switch (in_sig_bt[i]) {
      case T_ARRAY:
      case T_OBJECT:
        object_move(masm, map, oop_handle_offset, stack_slots, in_regs[i], out_regs[c_arg],
                    ((i == 0) && (!is_static)),
                    &receiver_offset);
        break;
      case T_VOID:
        break;

      case T_FLOAT:
        float_move(masm, in_regs[i], out_regs[c_arg]);
          break;

      case T_DOUBLE:
        assert( i + 1 < total_in_args && 
                in_sig_bt[i + 1] == T_VOID &&
                out_sig_bt[c_arg+1] == T_VOID, "bad arg list");
        double_move(masm, in_regs[i], out_regs[c_arg]);
        break;

      case T_LONG :
        long_move(masm, in_regs[i], out_regs[c_arg]);
        break;

      case T_ADDRESS: assert(false, "found T_ADDRESS in java args");

      default:
        simple_move32(masm, in_regs[i], out_regs[c_arg]);
    }
  }

  // Pre-load a static method's oop into esi.  Used both by locking code and
  // the normal JNI call code.
  if (method->is_static()) {

    //  load opp into a register
    __ movl(oop_handle_reg, JNIHandles::make_local(Klass::cast(method->method_holder())->java_mirror()));

    // Now handlize the static class mirror it's known not-null.
    __ movl(Address(esp, klass_offset), oop_handle_reg);
    map->set_oop(VMRegImpl::stack2reg(klass_slot_offset));

    // Now get the handle
    __ leal(oop_handle_reg, Address(esp, klass_offset));
    // store the klass handle as second argument
    __ movl(Address(esp, wordSize), oop_handle_reg);
  }

  // Change state to native (we save the return address in the thread, since it might not
  // be pushed on the stack when we do a a stack traversal). It is enough that the pc()
  // points into the right code segment. It does not have to be the correct return pc.
  // We use the same pc/oopMap repeatedly when we call out

  intptr_t the_pc = (intptr_t) __ pc();
  oop_maps->add_gc_map(the_pc - start, map);

  __ set_last_Java_frame(thread, esp, noreg, (address)the_pc);


  // We have all of the arguments setup at this point. We must not touch any register
  // argument registers at this point (what if we save/restore them there are no oop?

  { 
    SkipIfEqual skip_if(masm, &DTraceMethodProbes, 0);
    __ movl(eax, JNIHandles::make_local(method()));
    __ call_VM_leaf(
         CAST_FROM_FN_PTR(address, SharedRuntime::dtrace_method_entry), 
         thread, eax);
  }


  // These are register definitions we need for locking/unlocking 
  const Register swap_reg = eax;  // Must use eax for cmpxchg instruction
  const Register obj_reg  = ecx;  // Will contain the oop
  const Register lock_reg = edx;  // Address of compiler lock object (BasicLock)

  Label slow_path_lock;
  Label lock_done;

  // Lock a synchronized method
  if (method->is_synchronized()) {


    const int mark_word_offset = BasicLock::displaced_header_offset_in_bytes();

    // Get the handle (the 2nd argument)
    __ movl(oop_handle_reg, Address(esp, wordSize));

    // Get address of the box

    __ leal(lock_reg, Address(ebp, lock_slot_ebp_offset));

    // Load the oop from the handle 
    __ movl(obj_reg, Address(oop_handle_reg, 0));

    if (UseBiasedLocking) {
      // Note that oop_handle_reg is trashed during this call
      __ biased_locking_enter(lock_reg, obj_reg, swap_reg, oop_handle_reg, false, lock_done, &slow_path_lock);
    }

    // Load immediate 1 into swap_reg %eax
    __ movl(swap_reg, 1);

    // Load (object->mark() | 1) into swap_reg %eax
    __ orl(swap_reg, Address(obj_reg));

    // Save (object->mark() | 1) into BasicLock's displaced header
    __ movl(Address(lock_reg, mark_word_offset), swap_reg);

    if (os::is_MP()) {
      __ lock();
    }

    // src -> dest iff dest == eax else eax <- dest
    // *obj_reg = lock_reg iff *obj_reg == eax else eax = *(obj_reg) 
    __ cmpxchg(lock_reg, Address(obj_reg));
    __ jcc(Assembler::equal, lock_done);

    // Test if the oopMark is an obvious stack pointer, i.e.,
    //  1) (mark & 3) == 0, and
    //  2) esp <= mark < mark + os::pagesize()
    // These 3 tests can be done by evaluating the following
    // expression: ((mark - esp) & (3 - os::vm_page_size())),
    // assuming both stack pointer and pagesize have their
    // least significant 2 bits clear.
    // NOTE: the oopMark is in swap_reg %eax as the result of cmpxchg

    __ subl(swap_reg, esp);
    __ andl(swap_reg, 3 - os::vm_page_size());

    // Save the test result, for recursive case, the result is zero
    __ movl(Address(lock_reg, mark_word_offset), swap_reg);
    __ jcc(Assembler::notEqual, slow_path_lock);
    // Slow path will re-enter here
    __ bind(lock_done);

    if (UseBiasedLocking) {
      // Re-fetch oop_handle_reg as we trashed it above
      __ movl(oop_handle_reg, Address(esp, wordSize));
    }
  }


  // Finally just about ready to make the JNI call


  // get JNIEnv* which is first argument to native

  __ leal(edx, Address(thread, in_bytes(JavaThread::jni_environment_offset())));
  __ movl(Address(esp, 0), edx);

  // Now set thread in native
  __ movl(Address(thread, JavaThread::thread_state_offset()), _thread_in_native);    

  __ call(method->native_function(), relocInfo::runtime_call_type);

  // WARNING - on Windows Java Natives use pascal calling convention and pop the
  // arguments off of the stack. We could just re-adjust the stack pointer here
  // and continue to do SP relative addressing but we instead switch to FP
  // relative addressing.

  // Unpack native results.  
  switch (ret_type) {
  case T_BOOLEAN: __ c2bool(eax);            break;
  case T_CHAR   : __ andl(eax, 0xFFFF);      break;
  case T_BYTE   : __ sign_extend_byte (eax); break;
  case T_SHORT  : __ sign_extend_short(eax); break;
  case T_INT    : /* nothing to do */        break;
  case T_DOUBLE :
  case T_FLOAT  :
    // Result is in st0 we'll save as needed
    break;
  case T_ARRAY:                 // Really a handle
  case T_OBJECT:                // Really a handle
      break; // can't de-handlize until after safepoint check
  case T_VOID: break;
  case T_LONG: break;
  default       : ShouldNotReachHere();
  }

  // Switch thread to "native transition" state before reading the synchronization state.
  // This additional state is necessary because reading and testing the synchronization
  // state is not atomic w.r.t. GC, as this scenario demonstrates:
  //     Java thread A, in _thread_in_native state, loads _not_synchronized and is preempted.
  //     VM thread changes sync state to synchronizing and suspends threads for GC.
  //     Thread A is resumed to finish this native method, but doesn't block here since it
  //     didn't see any synchronization is progress, and escapes.
  __ movl(Address(thread, JavaThread::thread_state_offset()), _thread_in_native_trans);    

  if(os::is_MP()) { 
    if (UseMembar) {
      __ membar(); // Force this write out before the read below
    } else {
      // Write serialization page so VM thread can do a pseudo remote membar.
      // We use the current thread pointer to calculate a thread specific
      // offset to write to within the page. This minimizes bus traffic
      // due to cache line collision.
      __ serialize_memory(thread, ecx);
    }
  }

  if (AlwaysRestoreFPU) {
    // Make sure the control word is correct. 
    __ fldcw(Address((int) StubRoutines::addr_fpu_cntrl_wrd_std(), relocInfo::none));
  }

  // check for safepoint operation in progress and/or pending suspend requests
  { Label Continue;

    __ cmpl(Address((int)SafepointSynchronize::address_of_state(), relocInfo::none),
            SafepointSynchronize::_not_synchronized);

    Label L;
    __ jcc(Assembler::notEqual, L);
    __ cmpl(Address(thread, JavaThread::suspend_flags_offset()), 0);
    __ jcc(Assembler::equal, Continue);
    __ bind(L);

    // Don't use call_VM as it will see a possible pending exception and forward it
    // and never return here preventing us from clearing _last_native_pc down below.
    // Also can't use call_VM_leaf either as it will check to see if esi & edi are
    // preserved and correspond to the bcp/locals pointers. So we do a runtime call
    // by hand.
    //
    save_native_result(masm, ret_type, stack_slots);
    __ pushl(thread);
    __ call(CAST_FROM_FN_PTR(address, JavaThread::check_special_condition_for_native_trans), relocInfo::runtime_call_type);
    __ increment(esp, wordSize);
    // Restore any method result value
    restore_native_result(masm, ret_type, stack_slots);

    __ bind(Continue);
  }

  // change thread state
  __ movl(Address(thread, JavaThread::thread_state_offset()), _thread_in_Java);

  Label reguard;
  Label reguard_done;
  __ cmpl(Address(thread, JavaThread::stack_guard_state_offset()), JavaThread::stack_guard_yellow_disabled);
  __ jcc(Assembler::equal, reguard);

  // slow path reguard  re-enters here
  __ bind(reguard_done);

  // Handle possible exception (will unlock if necessary)

  // native result if any is live 

  // Unlock
  Label slow_path_unlock;
  Label unlock_done;
  if (method->is_synchronized()) {

    Label done;

    // Get locked oop from the handle we passed to jni
    __ movl(obj_reg, Address(oop_handle_reg, 0));

    if (UseBiasedLocking) {
      __ biased_locking_exit(obj_reg, ebx, done);
    }

    // Simple recursive lock?

    __ cmpl(Address(ebp, lock_slot_ebp_offset), NULL_WORD);
    __ jcc(Assembler::equal, done);

    // Must save eax if if it is live now because cmpxchg must use it
    if (ret_type != T_FLOAT && ret_type != T_DOUBLE && ret_type != T_VOID) {
      save_native_result(masm, ret_type, stack_slots);
    }

    //  get old displaced header
    __ movl(ebx, Address(ebp, lock_slot_ebp_offset));

    // get address of the stack lock
    __ leal(eax, Address(ebp, lock_slot_ebp_offset));

    // Atomic swap old header if oop still contains the stack lock
    if (os::is_MP()) {
    __ lock();
    }

    // src -> dest iff dest == eax else eax <- dest
    // *obj_reg = ebx iff *obj_reg == eax else eax = *(obj_reg) 
    __ cmpxchg(ebx, Address(obj_reg));
    __ jcc(Assembler::notEqual, slow_path_unlock);

    // slow path re-enters here
    __ bind(unlock_done);
    if (ret_type != T_FLOAT && ret_type != T_DOUBLE && ret_type != T_VOID) {
      restore_native_result(masm, ret_type, stack_slots);
    }

    __ bind(done);

  }

  { 
    SkipIfEqual skip_if(masm, &DTraceMethodProbes, 0);
    // Tell dtrace about this method exit
    save_native_result(masm, ret_type, stack_slots);
    __ movl(eax, JNIHandles::make_local(method()));
    __ call_VM_leaf(
         CAST_FROM_FN_PTR(address, SharedRuntime::dtrace_method_exit), 
         thread, eax);
    restore_native_result(masm, ret_type, stack_slots);
  }

  // We can finally stop using that last_Java_frame we setup ages ago

  __ reset_last_Java_frame(thread, false, true);

  // Unpack oop result
  if (ret_type == T_OBJECT || ret_type == T_ARRAY) {
      Label L;
      __ cmpl(eax, NULL_WORD);
      __ jcc(Assembler::equal, L);
      __ movl(eax, Address(eax));
      __ bind(L);
      __ verify_oop(eax);
  }

  // reset handle block
  __ movl(ecx, Address(thread, JavaThread::active_handles_offset()));

  __ movl(Address(ecx, JNIHandleBlock::top_offset_in_bytes()), 0);

  // Any exception pending?
  __ cmpl(Address(thread, in_bytes(Thread::pending_exception_offset())), NULL_WORD);
  __ jcc(Assembler::notEqual, exception_pending);


  // no exception, we're almost done

  // check that only result value is on FPU stack
  __ verify_FPU(ret_type == T_FLOAT || ret_type == T_DOUBLE ? 1 : 0, "native_wrapper normal exit");

  // Fixup floating pointer results so that result looks like a return from a compiled method
  if (ret_type == T_FLOAT) {
    if (UseSSE >= 1) {
      // Pop st0 and store as float and reload into xmm register
      __ fstp_s(Address(ebp, -4));
      __ movflt(xmm0, Address(ebp, -4));
    }
  } else if (ret_type == T_DOUBLE) {
    if (UseSSE >= 2) {
      // Pop st0 and store as double and reload into xmm register
      __ fstp_d(Address(ebp, -8));
      __ movdbl(xmm0, Address(ebp, -8));
    }
  }

  // Return

  __ leave();
  __ ret(0);

  // Unexpected paths are out of line and go here

  // Slow path locking & unlocking
  if (method->is_synchronized()) {

    // BEGIN Slow path lock

    __ bind(slow_path_lock);

    // has last_Java_frame setup. No exceptions so do vanilla call not call_VM
    // args are (oop obj, BasicLock* lock, JavaThread* thread)
    __ pushl(thread);
    __ pushl(lock_reg);
    __ pushl(obj_reg);
    __ call(CAST_FROM_FN_PTR(address, SharedRuntime::complete_monitor_locking_C), relocInfo::runtime_call_type);
    __ addl(esp, 3*wordSize);

#ifdef ASSERT
    { Label L;
    __ cmpl(Address(thread, in_bytes(Thread::pending_exception_offset())), (int)NULL_WORD);
    __ jcc(Assembler::equal, L);
    __ stop("no pending exception allowed on exit from monitorenter");
    __ bind(L);
    }
#endif
    __ jmp(lock_done);

    // END Slow path lock

    // BEGIN Slow path unlock
    __ bind(slow_path_unlock);

    // Slow path unlock

    if (ret_type == T_FLOAT || ret_type == T_DOUBLE ) {
      save_native_result(masm, ret_type, stack_slots);
    }
    // Save pending exception around call to VM (which contains an EXCEPTION_MARK)

    __ pushl(Address(thread, in_bytes(Thread::pending_exception_offset())));
    __ movl(Address(thread, in_bytes(Thread::pending_exception_offset())), NULL_WORD);


    // should be a peal
    // +wordSize because of the push above
    __ leal(eax, Address(ebp, lock_slot_ebp_offset));
    __ pushl(eax);

    __ pushl(obj_reg);
    __ call(CAST_FROM_FN_PTR(address, SharedRuntime::complete_monitor_unlocking_C), relocInfo::runtime_call_type);
    __ addl(esp, 2*wordSize);
#ifdef ASSERT
    {
      Label L;
      __ cmpl(Address(thread, in_bytes(Thread::pending_exception_offset())), NULL_WORD);
      __ jcc(Assembler::equal, L);
      __ stop("no pending exception allowed on exit complete_monitor_unlocking_C");
      __ bind(L);
    }
#endif /* ASSERT */

    __ popl(Address(thread, in_bytes(Thread::pending_exception_offset())));

    if (ret_type == T_FLOAT || ret_type == T_DOUBLE ) {
      restore_native_result(masm, ret_type, stack_slots);
    }
    __ jmp(unlock_done);
    // END Slow path unlock

  }

  // SLOW PATH Reguard the stack if needed

  __ bind(reguard);
  save_native_result(masm, ret_type, stack_slots);
  __ call(CAST_FROM_FN_PTR(address, SharedRuntime::reguard_yellow_pages), relocInfo::runtime_call_type);
  restore_native_result(masm, ret_type, stack_slots);
  __ jmp(reguard_done);


  // BEGIN EXCEPTION PROCESSING

  // Forward  the exception
  __ bind(exception_pending);

  // remove possible return value from FPU register stack
  __ empty_FPU_stack();

  // pop our frame
  __ leave();
  // and forward the exception
  __ jmp(StubRoutines::forward_exception_entry(), relocInfo::runtime_call_type);

  __ flush();

  nmethod *nm = nmethod::new_native_nmethod(method,
                                            masm->code(),
                                            vep_offset,
                                            frame_complete,
                                            stack_slots / VMRegImpl::slots_per_word,
                                            (is_static ? in_ByteSize(klass_offset) : in_ByteSize(receiver_offset)),
                                            in_ByteSize(lock_slot_offset*VMRegImpl::stack_slot_size),
                                            oop_maps);
  return nm;

}

// this function returns the adjust size (in number of words) to a c2i adapter
// activation for use during deoptimization
int Deoptimization::last_frame_adjust(int callee_parameters, int callee_locals ) {
  return (callee_locals - callee_parameters) * Interpreter::stackElementWords();
}


uint SharedRuntime::out_preserve_stack_slots() {
  return 0;
}


//------------------------------generate_deopt_blob----------------------------
void SharedRuntime::generate_deopt_blob() {
  // allocate space for the code
  ResourceMark rm;
  // setup code generation tools
  CodeBuffer   buffer("deopt_blob", 1024, 1024);
  MacroAssembler* masm = new MacroAssembler(&buffer);
  int frame_size_in_words;
  OopMap* map = NULL;
  // Account for the extra args we place on the stack
  // by the time we call fetch_unroll_info
  const int additional_words = 2; // deopt kind, thread

  OopMapSet *oop_maps = new OopMapSet();

  // -------------
  // This code enters when returning to a de-optimized nmethod.  A return
  // address has been pushed on the the stack, and return values are in
  // registers. 
  // If we are doing a normal deopt then we were called from the patched 
  // nmethod from the point we returned to the nmethod. So the return
  // address on the stack is wrong by NativeCall::instruction_size
  // We will adjust the value to it looks like we have the original return
  // address on the stack (like when we eagerly deoptimized).
  // In the case of an exception pending with deoptimized then we enter
  // with a return address on the stack that points after the call we patched
  // into the exception handler. We have the following register state:
  //    eax: exception
  //    ebx: exception handler
  //    edx: throwing pc
  // So in this case we simply jam edx into the useless return address and
  // the stack looks just like we want.
  //
  // At this point we need to de-opt.  We save the argument return
  // registers.  We call the first C routine, fetch_unroll_info().  This
  // routine captures the return values and returns a structure which
  // describes the current frame size and the sizes of all replacement frames.
  // The current frame is compiled code and may contain many inlined
  // functions, each with their own JVM state.  We pop the current frame, then
  // push all the new frames.  Then we call the C routine unpack_frames() to
  // populate these frames.  Finally unpack_frames() returns us the new target
  // address.  Notice that callee-save registers are BLOWN here; they have
  // already been captured in the vframeArray at the time the return PC was
  // patched.
  address start = __ pc();
  Label cont;

  // Prolog for non exception case!

  // Save everything in sight.

  map = RegisterSaver::save_live_registers(masm, additional_words, &frame_size_in_words);
  // Normal deoptimization
  __ pushl(Deoptimization::Unpack_deopt);
  __ jmp(cont);

  int reexecute_offset = __ pc() - start;

  // Reexecute case
  // return address is the pc describes what bci to do re-execute at

  // No need to update map as each call to save_live_registers will produce identical oopmap
  (void) RegisterSaver::save_live_registers(masm, additional_words, &frame_size_in_words);

  __ pushl(Deoptimization::Unpack_reexecute);
  __ jmp(cont);
  
  int exception_offset = __ pc() - start;

  // Prolog for exception case

  // all registers are dead at this entry point, except for eax and
  // edx which contain the exception oop and exception pc
  // respectively.  Set them in TLS and fall thru to the
  // unpack_with_exception_in_tls entry point.

  __ get_thread(edi);
  __ movl(Address(edi, JavaThread::exception_pc_offset()), edx);
  __ movl(Address(edi, JavaThread::exception_oop_offset()), eax);

  int exception_in_tls_offset = __ pc() - start;

  // new implementation because exception oop is now passed in JavaThread

  // Prolog for exception case
  // All registers must be preserved because they might be used by LinearScan
  // Exceptiop oop and throwing PC are passed in JavaThread
  // tos: stack at point of call to method that threw the exception (i.e. only
  // args are on the stack, no return address)

  // make room on stack for the return address
  // It will be patched later with the throwing pc. The correct value is not 
  // available now because loading it from memory would destroy registers.
  __ pushl(0);

  // Save everything in sight.

  // No need to update map as each call to save_live_registers will produce identical oopmap
  (void) RegisterSaver::save_live_registers(masm, additional_words, &frame_size_in_words);

  // Now it is safe to overwrite any register

  // store the correct deoptimization type
  __ pushl(Deoptimization::Unpack_exception); 

  // load throwing pc from JavaThread and patch it as the return address 
  // of the current frame. Then clear the field in JavaThread
  __ get_thread(edi);
  __ movl(edx, Address(edi, JavaThread::exception_pc_offset()));
  __ movl(Address(ebp, wordSize), edx); 
  __ movl(Address(edi, JavaThread::exception_pc_offset()), NULL_WORD);

#ifdef ASSERT
  // verify that there is really an exception oop in JavaThread
  __ movl(eax, Address(edi, JavaThread::exception_oop_offset()));
  __ verify_oop(eax);

  // verify that there is no pending exception
  Label no_pending_exception;
  __ movl(eax, Address(edi, Thread::pending_exception_offset()));
  __ testl(eax, eax);
  __ jcc(Assembler::zero, no_pending_exception);
  __ stop("must not have pending exception here");
  __ bind(no_pending_exception);
#endif

  __ bind(cont);

  // Compiled code leaves the floating point stack dirty, empty it.
  __ empty_FPU_stack();


  // Call C code.  Need thread and this frame, but NOT official VM entry
  // crud.  We cannot block on this call, no GC can happen.  
  __ get_thread(ecx);
  __ pushl(ecx);
  // fetch_unroll_info needs to call last_java_frame()
  __ set_last_Java_frame(ecx, noreg, noreg, NULL);

  __ call( CAST_FROM_FN_PTR(address, Deoptimization::fetch_unroll_info), relocInfo::runtime_call_type );

  // Need to have an oopmap that tells fetch_unroll_info where to
  // find any register it might need.

  oop_maps->add_gc_map( __ pc()-start, map);

  // Discard arg to fetch_unroll_info
  __ popl(ecx);

  __ get_thread(ecx);
  __ reset_last_Java_frame(ecx, false, false);

  // Load UnrollBlock into EDI
  __ movl(edi, eax);

  // Move the unpack kind to a safe place in the UnrollBlock because
  // we are very short of registers

  Address unpack_kind(edi, Deoptimization::UnrollBlock::unpack_kind_offset_in_bytes());
  // retrieve the deopt kind from where we left it.
  __ popl(eax);
  __ movl(unpack_kind, eax);                      // save the unpack_kind value

   Label noException;
  __ cmpl(eax, Deoptimization::Unpack_exception);   // Was exception pending?
  __ jcc(Assembler::notEqual, noException);
  __ movl(eax, Address(ecx, JavaThread::exception_oop_offset()));
  __ movl(edx, Address(ecx, JavaThread::exception_pc_offset()));
  __ movl(Address(ecx, JavaThread::exception_oop_offset()), NULL_WORD);
  __ movl(Address(ecx, JavaThread::exception_pc_offset()), NULL_WORD);

  __ verify_oop(eax);

  // Overwrite the result registers with the exception results.
  __ movl(Address(esp, RegisterSaver::eaxOffset()*wordSize), eax);
  __ movl(Address(esp, RegisterSaver::edxOffset()*wordSize), edx);

  __ bind(noException);

  // Stack is back to only having register save data on the stack.
  // Now restore the result registers. Everything else is either dead or captured
  // in the vframeArray.

  RegisterSaver::restore_result_registers(masm);

  // All of the register save area has been popped of the stack. Only the
  // return address remains.

  // Pop all the frames we must move/replace. 
  // 
  // Frame picture (youngest to oldest)
  // 1: self-frame (no frame link)
  // 2: deopting frame  (no frame link)
  // 3: caller of deopting frame (could be compiled/interpreted). 
  // 
  // Note: by leaving the return address of self-frame on the stack
  // and using the size of frame 2 to adjust the stack
  // when we are done the return to frame 3 will still be on the stack.

  // Pop deoptimized frame
  __ addl(esp,Address(edi,Deoptimization::UnrollBlock::size_of_deoptimized_frame_offset_in_bytes()));

  // sp should be pointing at the return address to the caller (3)

  // Stack bang to make sure there's enough room for these interpreter frames.
  if (UseStackBanging) {
    __ movl(ebx, Address(edi ,Deoptimization::UnrollBlock::total_frame_sizes_offset_in_bytes())); 
    __ bang_stack_size(ebx, ecx);
  }

  // Load array of frame pcs into ECX
  __ movl(ecx,Address(edi,Deoptimization::UnrollBlock::frame_pcs_offset_in_bytes()));

  __ popl(esi); // trash the old pc

  // Load array of frame sizes into ESI
  __ movl(esi,Address(edi,Deoptimization::UnrollBlock::frame_sizes_offset_in_bytes()));

  Address counter(edi, Deoptimization::UnrollBlock::counter_temp_offset_in_bytes());

  __ movl(ebx, Address(edi, Deoptimization::UnrollBlock::number_of_frames_offset_in_bytes()));
  __ movl(counter, ebx);

  // Pick up the initial fp we should save
  __ movl(ebp, Address(edi, Deoptimization::UnrollBlock::initial_fp_offset_in_bytes()));

  // Now adjust the caller's stack to make up for the extra locals
  // but record the original sp so that we can save it in the skeletal interpreter
  // frame and the stack walking of interpreter_sender will get the unextended sp
  // value and not the "real" sp value.

  Address sp_temp(edi, Deoptimization::UnrollBlock::sender_sp_temp_offset_in_bytes());
  __ movl(sp_temp, esp);
  __ subl(esp, Address(edi, Deoptimization::UnrollBlock::caller_adjustment_offset_in_bytes()));

  // Push interpreter frames in a loop
  Label loop;
  __ bind(loop);
  __ movl(ebx, Address(esi));           // Load frame size
  __ subl(ebx, 2*wordSize);             // we'll push pc and ebp by hand
  __ pushl(Address(ecx));               // save return address
  __ enter();                           // save old & set new ebp
  __ subl(esp, ebx);                    // Prolog!
  __ movl(ebx, sp_temp);                // sender's sp
  // This value is corrected by layout_activation_impl 
  __ movl(Address(ebp, frame::interpreter_frame_last_sp_offset * wordSize), NULL_WORD );
  __ movl(Address(ebp, frame::interpreter_frame_sender_sp_offset * wordSize), ebx); // Make it walkable
  __ movl(sp_temp, esp);                // pass to next frame
  __ addl(esi, 4);                      // Bump array pointer (sizes)
  __ addl(ecx, 4);                      // Bump array pointer (pcs)
  __ decrement(counter);                // decrement counter
  __ jcc(Assembler::notZero, loop);
  __ pushl(Address(ecx));               // save final return address

  // Re-push self-frame
  __ enter();                           // save old & set new ebp

  //  Return address and ebp are in place
  // We'll push additional args later. Just allocate a full sized
  // register save area 
  __ subl(esp, (frame_size_in_words-additional_words - 2) * wordSize);

  // Restore frame locals after moving the frame
  __ movl(Address(esp, RegisterSaver::eaxOffset()*wordSize), eax);
  __ movl(Address(esp, RegisterSaver::edxOffset()*wordSize), edx);
  __ fstp_d(Address(esp, RegisterSaver::fpResultOffset()*wordSize));   // Pop float stack and store in local
  if( UseSSE>=2 ) __ movdbl(Address(esp, RegisterSaver::xmm0Offset()*wordSize), xmm0);
  if( UseSSE==1 ) __ movflt(Address(esp, RegisterSaver::xmm0Offset()*wordSize), xmm0);

  // Set up the args to unpack_frame

  __ pushl(unpack_kind);                     // get the unpack_kind value
  __ get_thread(ecx);
  __ pushl(ecx);

  // set last_Java_sp, last_Java_fp
  __ set_last_Java_frame(ecx, noreg, ebp, NULL);

  // Call C code.  Need thread but NOT official VM entry
  // crud.  We cannot block on this call, no GC can happen.  Call should
  // restore return values to their stack-slots with the new SP.
  __ call( CAST_FROM_FN_PTR(address, Deoptimization::unpack_frames), relocInfo::runtime_call_type );
  // Set an oopmap for the call site
  oop_maps->add_gc_map( __ pc()-start, new OopMap( frame_size_in_words, 0 ));

  // eax contains the return result type
  __ pushl(eax);

  __ get_thread(ecx);
  __ reset_last_Java_frame(ecx, false, false);

  // Collect return values
  __ movl(eax,Address(esp, (RegisterSaver::eaxOffset() + additional_words + 1)*wordSize));
  __ movl(edx,Address(esp, (RegisterSaver::edxOffset() + additional_words + 1)*wordSize));

  // Clear floating point stack before returning to interpreter
  __ empty_FPU_stack();

  // Check if we should push the float or double return value.
  Label results_done, yes_double_value;
  __ cmpl(Address(esp, 0), T_DOUBLE);
  __ jcc (Assembler::zero, yes_double_value);
  __ cmpl(Address(esp, 0), T_FLOAT);
  __ jcc (Assembler::notZero, results_done);

  // return float value as expected by interpreter
  if( UseSSE>=1 ) __ movflt(xmm0, Address(esp, (RegisterSaver::xmm0Offset() + additional_words + 1)*wordSize));
  else            __ fld_d(Address(esp, (RegisterSaver::fpResultOffset() + additional_words + 1)*wordSize));
  __ jmp(results_done);

  // return double value as expected by interpreter
  __ bind(yes_double_value);
  if( UseSSE>=2 ) __ movdbl(xmm0, Address(esp, (RegisterSaver::xmm0Offset() + additional_words + 1)*wordSize));
  else            __ fld_d(Address(esp, (RegisterSaver::fpResultOffset() + additional_words + 1)*wordSize));

  __ bind(results_done);

  // Pop self-frame.
  __ leave();                              // Epilog!

  // Jump to interpreter
  __ ret(0);
  
  // -------------
  // make sure all code is generated
  masm->flush();

  _deopt_blob = DeoptimizationBlob::create( &buffer, oop_maps, 0, exception_offset, reexecute_offset, frame_size_in_words);
  _deopt_blob->set_unpack_with_exception_in_tls_offset(exception_in_tls_offset);
}


#ifdef COMPILER2
//------------------------------generate_uncommon_trap_blob--------------------
static UncommonTrapBlob* generate_uncommon_trap_blob() {
  // allocate space for the code
  ResourceMark rm;
  // setup code generation tools
  CodeBuffer   buffer("uncommon_trap_blob", 512, 512);
  MacroAssembler* masm = new MacroAssembler(&buffer);

  enum frame_layout {
    arg0_off,      // thread                     sp + 0 // Arg location for 
    arg1_off,      // unloaded_class_index       sp + 1 // calling C
    // The frame sender code expects that rbp will be in the "natural" place and
    // will override any oopMap setting for it. We must therefore force the layout
    // so that it agrees with the frame sender code.
    ebp_off,       // callee saved register      sp + 2
    return_off,    // slot for return address    sp + 3
    framesize
  };
  
  address start = __ pc();
  // Push self-frame.
  __ subl(esp,return_off*wordSize);     // Epilog!

  // ebp is an implicitly saved callee saved register (i.e. the calling
  // convention will save restore it in prolog/epilog) Other than that
  // there are no callee save registers no that adapter frames are gone.
  __ movl(Address(esp,ebp_off  *wordSize),ebp);

  // Clear the floating point exception stack
  __ empty_FPU_stack();

  // set last_Java_sp
  __ get_thread(edx);
  __ set_last_Java_frame(edx, noreg, noreg, NULL);

  // Call C code.  Need thread but NOT official VM entry
  // crud.  We cannot block on this call, no GC can happen.  Call should
  // capture callee-saved registers as well as return values.
  __ movl(Address(esp, arg0_off*wordSize),edx);
  // argument already in ECX 
  __ movl(Address(esp, arg1_off*wordSize),ecx);
  __ call(CAST_FROM_FN_PTR(address, Deoptimization::uncommon_trap), relocInfo::runtime_call_type);

  // Set an oopmap for the call site
  OopMapSet *oop_maps = new OopMapSet();
  OopMap* map =  new OopMap( framesize, 0 );
  // No oopMap for ebp, it is known implicitly

  oop_maps->add_gc_map( __ pc()-start, map);

  __ get_thread(ecx);

  __ reset_last_Java_frame(ecx, false, false);

  // Load UnrollBlock into EDI
  __ movl(edi, eax);

  // Pop all the frames we must move/replace. 
  // 
  // Frame picture (youngest to oldest)
  // 1: self-frame (no frame link)
  // 2: deopting frame  (no frame link)
  // 3: caller of deopting frame (could be compiled/interpreted).

  // Pop self-frame.  We have no frame, and must rely only on EAX and ESP.
  __ addl(esp,(framesize-1)*wordSize);     // Epilog!

  // Pop deoptimized frame
  __ addl(esp,Address(edi,Deoptimization::UnrollBlock::size_of_deoptimized_frame_offset_in_bytes()));

  // sp should be pointing at the return address to the caller (3)
  
  // Stack bang to make sure there's enough room for these interpreter frames.
  if (UseStackBanging) {
    __ movl(ebx, Address(edi ,Deoptimization::UnrollBlock::total_frame_sizes_offset_in_bytes())); 
    __ bang_stack_size(ebx, ecx);
  }


  // Load array of frame pcs into ECX
  __ movl(ecx,Address(edi,Deoptimization::UnrollBlock::frame_pcs_offset_in_bytes()));

  __ popl(esi); // trash the pc

  // Load array of frame sizes into ESI
  __ movl(esi,Address(edi,Deoptimization::UnrollBlock::frame_sizes_offset_in_bytes()));

  Address counter(edi, Deoptimization::UnrollBlock::counter_temp_offset_in_bytes());

  __ movl(ebx, Address(edi, Deoptimization::UnrollBlock::number_of_frames_offset_in_bytes()));
  __ movl(counter, ebx);

  // Pick up the initial fp we should save
  __ movl(ebp, Address(edi, Deoptimization::UnrollBlock::initial_fp_offset_in_bytes()));

  // Now adjust the caller's stack to make up for the extra locals
  // but record the original sp so that we can save it in the skeletal interpreter
  // frame and the stack walking of interpreter_sender will get the unextended sp
  // value and not the "real" sp value.

  Address sp_temp(edi, Deoptimization::UnrollBlock::sender_sp_temp_offset_in_bytes());
  __ movl(sp_temp, esp);
  __ subl(esp, Address(edi, Deoptimization::UnrollBlock::caller_adjustment_offset_in_bytes()));

  // Push interpreter frames in a loop
  Label loop;
  __ bind(loop);
  __ movl(ebx, Address(esi));           // Load frame size
  __ subl(ebx, 2*wordSize);             // we'll push pc and ebp by hand
  __ pushl(Address(ecx));               // save return address
  __ enter();                           // save old & set new ebp
  __ subl(esp, ebx);                    // Prolog!
  __ movl(ebx, sp_temp);                // sender's sp
  // This value is corrected by layout_activation_impl 
  __ movl(Address(ebp, frame::interpreter_frame_last_sp_offset * wordSize), NULL_WORD );
  __ movl(Address(ebp, frame::interpreter_frame_sender_sp_offset * wordSize), ebx); // Make it walkable
  __ movl(sp_temp, esp);                // pass to next frame
  __ addl(esi, 4);                      // Bump array pointer (sizes)
  __ addl(ecx, 4);                      // Bump array pointer (pcs)
  __ decrement(counter);                // decrement counter
  __ jcc(Assembler::notZero, loop);
  __ pushl(Address(ecx));               // save final return address

  // Re-push self-frame
  __ enter();                           // save old & set new ebp
  __ subl(esp, (framesize-2) * wordSize);   // Prolog!


  // set last_Java_sp, last_Java_fp
  __ get_thread(edi);
  __ set_last_Java_frame(edi, noreg, ebp, NULL);

  // Call C code.  Need thread but NOT official VM entry
  // crud.  We cannot block on this call, no GC can happen.  Call should
  // restore return values to their stack-slots with the new SP.
  __ movl(Address(esp,arg0_off*wordSize),edi);
  __ movl(Address(esp,arg1_off*wordSize), Deoptimization::Unpack_uncommon_trap); 
  __ call(CAST_FROM_FN_PTR(address, Deoptimization::unpack_frames), relocInfo::runtime_call_type);
  // Set an oopmap for the call site
  oop_maps->add_gc_map( __ pc()-start, new OopMap( framesize, 0 ) );

  __ get_thread(edi);
  __ reset_last_Java_frame(edi, true, false);

  // Pop self-frame.
  __ leave();     // Epilog!

  // Jump to interpreter
  __ ret(0);

  // -------------
  // make sure all code is generated
  masm->flush();

  return UncommonTrapBlob::create(&buffer, oop_maps, framesize);
}
#endif // COMPILER2

//------------------------------generate_handler_blob------
//
// Generate a special Compile2Runtime blob that saves all registers, 
// setup oopmap, and calls safepoint code to stop the compiled code for
// a safepoint.
//
static SafepointBlob* generate_handler_blob(address call_ptr, bool cause_return) {

  // Account for thread arg in our frame
  const int additional_words = 1; 
  int frame_size_in_words;

  assert (StubRoutines::forward_exception_entry() != NULL, "must be generated before");  

  ResourceMark rm;
  OopMapSet *oop_maps = new OopMapSet();
  OopMap* map;

  // allocate space for the code
  // setup code generation tools  
  CodeBuffer   buffer("handler_blob", 1024, 512);
  MacroAssembler* masm = new MacroAssembler(&buffer);
  
  const Register java_thread = edi; // callee-saved for VC++
  address start   = __ pc();  
  address call_pc = NULL;  

  // If cause_return is true we are at a poll_return and there is
  // the return address on the stack to the caller on the nmethod
  // that is safepoint. We can leave this return on the stack and
  // effectively complete the return and safepoint in the caller.
  // Otherwise we push space for a return address that the safepoint
  // handler will install later to make the stack walking sensible.
  if( !cause_return )
    __ pushl(ebx);                // Make room for return address (or push it again)

  map = RegisterSaver::save_live_registers(masm, additional_words, &frame_size_in_words, false);
  
  // The following is basically a call_VM. However, we need the precise
  // address of the call in order to generate an oopmap. Hence, we do all the
  // work ourselves.

  // Push thread argument and setup last_Java_sp
  __ get_thread(java_thread);
  __ pushl(java_thread);
  __ set_last_Java_frame(java_thread, noreg, noreg, NULL);

  // if this was not a poll_return then we need to correct the return address now. 
  if( !cause_return ) {
    __ movl(eax, Address(java_thread, JavaThread::saved_exception_pc_offset()));
    __ movl(Address(ebp, wordSize), eax);
  }

  // do the call
  __ call(call_ptr, relocInfo::runtime_call_type);

  // Set an oopmap for the call site.  This oopmap will map all
  // oop-registers and debug-info registers as callee-saved.  This
  // will allow deoptimization at this safepoint to find all possible
  // debug-info recordings, as well as let GC find all oops.

  oop_maps->add_gc_map( __ pc() - start, map);

  // Discard arg
  __ popl(ecx);

  Label noException;

  // Clear last_Java_sp again
  __ get_thread(java_thread);
  __ reset_last_Java_frame(java_thread, false, false);

  __ cmpl(Address(java_thread, Thread::pending_exception_offset()), NULL_WORD);
  __ jcc(Assembler::equal, noException);

  // Exception pending

  RegisterSaver::restore_live_registers(masm);

  __ jmp(StubRoutines::forward_exception_entry(), relocInfo::runtime_call_type);

  __ bind(noException);

  // Normal exit, register restoring and exit  
  RegisterSaver::restore_live_registers(masm);

  __ ret(0);
  
  // make sure all code is generated
  masm->flush();  

  // Fill-out other meta info
  return SafepointBlob::create(&buffer, oop_maps, frame_size_in_words);    
}

//
// generate_resolve_blob - call resolution (static/virtual/opt-virtual/ic-miss
//
// Generate a stub that calls into vm to find out the proper destination
// of a java call. All the argument registers are live at this point
// but since this is generic code we don't know what they are and the caller
// must do any gc of the args.
//
static RuntimeStub* generate_resolve_blob(address destination, const char* name) {
  assert (StubRoutines::forward_exception_entry() != NULL, "must be generated before");  

  // allocate space for the code
  ResourceMark rm;

  CodeBuffer buffer(name, 1000, 512);
  MacroAssembler* masm                = new MacroAssembler(&buffer);

  int frame_size_words;
  enum frame_layout { 
                thread_off,
                extra_words };
  
  OopMapSet *oop_maps = new OopMapSet();
  OopMap* map = NULL;
  
  int start = __ offset();

  map = RegisterSaver::save_live_registers(masm, extra_words, &frame_size_words);

  int frame_complete = __ offset();

  const Register thread = edi;
  __ get_thread(edi);

  __ pushl(thread);
  __ set_last_Java_frame(thread, noreg, ebp, NULL);

  __ call(destination, relocInfo::runtime_call_type);


  // Set an oopmap for the call site.
  // We need this not only for callee-saved registers, but also for volatile
  // registers that the compiler might be keeping live across a safepoint.

  oop_maps->add_gc_map( __ offset() - start, map);

  // eax contains the address we are going to jump to assuming no exception got installed

  __ addl(esp, wordSize);

  // clear last_Java_sp
  __ reset_last_Java_frame(thread, true, false);
  // check for pending exceptions
  Label pending;
  __ cmpl(Address(thread, Thread::pending_exception_offset()), NULL_WORD);
  __ jcc(Assembler::notEqual, pending);

  // get the returned methodOop
  __ movl(ebx, Address(thread, JavaThread::vm_result_offset()));
  __ movl(Address(esp, RegisterSaver::ebx_offset() * wordSize), ebx);

  __ movl(Address(esp, RegisterSaver::eax_offset() * wordSize), eax);

  RegisterSaver::restore_live_registers(masm);

  // We are back the the original state on entry and ready to go.

  __ jmp(eax);

  // Pending exception after the safepoint

  __ bind(pending);

  RegisterSaver::restore_live_registers(masm);

  // exception pending => remove activation and forward to exception handler

  __ get_thread(thread);
  __ movl(Address(thread, JavaThread::vm_result_offset()), NULL_WORD);
  __ movl(eax, Address(thread, Thread::pending_exception_offset()));
  __ jmp(StubRoutines::forward_exception_entry(), relocInfo::runtime_call_type);

  // -------------
  // make sure all code is generated
  masm->flush();  

  // return the  blob
  // frame_size_words or bytes??
  return RuntimeStub::new_runtime_stub(name, &buffer, frame_complete, frame_size_words, oop_maps, true);
}

void SharedRuntime::generate_stubs() {

  _wrong_method_blob = generate_resolve_blob(CAST_FROM_FN_PTR(address, SharedRuntime::handle_wrong_method),
                                        "wrong_method_stub");

  _ic_miss_blob      = generate_resolve_blob(CAST_FROM_FN_PTR(address, SharedRuntime::handle_wrong_method_ic_miss),
                                        "ic_miss_stub");

  _resolve_opt_virtual_call_blob = generate_resolve_blob(CAST_FROM_FN_PTR(address, SharedRuntime::resolve_opt_virtual_call_C),
                                        "resolve_opt_virtual_call");

  _resolve_virtual_call_blob = generate_resolve_blob(CAST_FROM_FN_PTR(address, SharedRuntime::resolve_virtual_call_C),
                                        "resolve_virtual_call");

  _resolve_static_call_blob = generate_resolve_blob(CAST_FROM_FN_PTR(address, SharedRuntime::resolve_static_call_C),
                                        "resolve_static_call");

  _polling_page_safepoint_handler_blob =
    generate_handler_blob(CAST_FROM_FN_PTR(address, 
                   SafepointSynchronize::handle_polling_page_exception), false);

  _polling_page_return_handler_blob =
    generate_handler_blob(CAST_FROM_FN_PTR(address,
                   SafepointSynchronize::handle_polling_page_exception), true);
                  
  generate_deopt_blob();
#ifdef COMPILER2
  _uncommon_trap_blob = generate_uncommon_trap_blob();
#endif // COMPILER2
}
