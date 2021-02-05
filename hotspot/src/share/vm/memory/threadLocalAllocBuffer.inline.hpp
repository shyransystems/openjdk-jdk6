#ifdef USE_PRAGMA_IDENT_HDR
#pragma ident "@(#)threadLocalAllocBuffer.inline.hpp	1.29 07/05/05 17:05:56 JVM"
#endif
/*
 * Copyright 1999-2007 Sun Microsystems, Inc.  All Rights Reserved.
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

inline HeapWord* ThreadLocalAllocBuffer::allocate(size_t size) {
  invariants();
  HeapWord* obj = top();
  if (pointer_delta(end(), obj) >= size) {
    // successful thread-local allocation
    
    DEBUG_ONLY(Copy::fill_to_words(obj, size, badHeapWordVal));
    // This addition is safe because we know that top is
    // at least size below end, so the add can't wrap.
    set_top(obj + size);
    
    invariants();
    return obj;
  }
  return NULL;
}

inline size_t ThreadLocalAllocBuffer::compute_size(size_t obj_size) {
  const size_t aligned_obj_size = align_object_size(obj_size);

  // Compute the size for the new TLAB.
  // The "last" tlab may be smaller to reduce fragmentation.
  // unsafe_max_tlab_alloc is just a hint.
  const size_t available_size = Universe::heap()->unsafe_max_tlab_alloc(myThread()) /
                                                  HeapWordSize;
  size_t new_tlab_size = MIN2(available_size, desired_size() + aligned_obj_size);

  // Make sure there's enough room for object and filler int[].
  const size_t obj_plus_filler_size = aligned_obj_size + alignment_reserve();
  if (new_tlab_size < obj_plus_filler_size) {
    // If there isn't enough room for the allocation, return failure.
    if (PrintTLAB && Verbose) {
      gclog_or_tty->print_cr("ThreadLocalAllocBuffer::compute_size(" SIZE_FORMAT ")"
                    " returns failure",
                    obj_size);
    }
    return 0;
  }
  if (PrintTLAB && Verbose) {
    gclog_or_tty->print_cr("ThreadLocalAllocBuffer::compute_size(" SIZE_FORMAT ")"
                  " returns " SIZE_FORMAT,
                  obj_size, new_tlab_size);
  }
  return new_tlab_size;
}


void ThreadLocalAllocBuffer::record_slow_allocation(size_t obj_size) {
  // Raise size required to bypass TLAB next time. Why? Else there's
  // a risk that a thread that repeatedly allocates objects of one
  // size will get stuck on this slow path.

  set_refill_waste_limit(refill_waste_limit() + refill_waste_limit_increment());

  _slow_allocations++;

  if (PrintTLAB && Verbose) {
    Thread* thrd = myThread();
    gclog_or_tty->print("TLAB: %s thread: "INTPTR_FORMAT" [id: %2d]"
                        " obj: "SIZE_FORMAT
                        " free: "SIZE_FORMAT
                        " waste: "SIZE_FORMAT"\n",
                        "slow", thrd, thrd->osthread()->thread_id(),
                        obj_size, free(), refill_waste_limit());
  }
}
