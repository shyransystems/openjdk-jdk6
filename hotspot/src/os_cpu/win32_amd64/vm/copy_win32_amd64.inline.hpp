#ifdef USE_PRAGMA_IDENT_HDR
#pragma ident "@(#)copy_win32_amd64.inline.hpp	1.7 07/05/05 17:04:54 JVM"
#endif
/*
 * Copyright 2004 Sun Microsystems, Inc.  All Rights Reserved.
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

static void pd_conjoint_bytes(void* from, void* to, size_t count) {
  (void)memmove(to, from, count);
}

static void pd_conjoint_bytes_atomic(void* from, void* to, size_t count) {
  (void)memmove(to, from, count);
}

static void pd_conjoint_jshorts_atomic(jshort* from, jshort* to, size_t count) {
  // FIXME
  (void)memmove(to, from, count << LogBytesPerShort);
}

static void pd_conjoint_jints_atomic(jint* from, jint* to, size_t count) {
  // FIXME
  (void)memmove(to, from, count << LogBytesPerInt);
}

static void pd_conjoint_jlongs_atomic(jlong* from, jlong* to, size_t count) {
  assert(BytesPerLong == BytesPerOop, "jlongs and oops must be the same size");
  pd_conjoint_oops_atomic((oop*)from, (oop*)to, count);
}

static void pd_conjoint_oops_atomic(oop* from, oop* to, size_t count) {
  // Do better than this: inline memmove body  NEEDS CLEANUP
  if (from > to) {
    while (count-- > 0) {
      // Copy forwards
      *to++ = *from++;
    }
  } else {
    from += count - 1;
    to   += count - 1;
    while (count-- > 0) {
      // Copy backwards
      *to-- = *from--;
    }
  }
}

static void pd_arrayof_conjoint_bytes(HeapWord* from, HeapWord* to, size_t count) {
  pd_conjoint_bytes_atomic(from, to, count);
}

static void pd_arrayof_conjoint_jshorts(HeapWord* from, HeapWord* to, size_t count) {
  pd_conjoint_jshorts_atomic((jshort*)from, (jshort*)to, count);
}

static void pd_arrayof_conjoint_jints(HeapWord* from, HeapWord* to, size_t count) {
  pd_conjoint_jints_atomic((jint*)from, (jint*)to, count);
}

static void pd_arrayof_conjoint_jlongs(HeapWord* from, HeapWord* to, size_t count) {
  pd_conjoint_jlongs_atomic((jlong*)from, (jlong*)to, count);
}

static void pd_arrayof_conjoint_oops(HeapWord* from, HeapWord* to, size_t count) {
  pd_conjoint_oops_atomic((oop*)from, (oop*)to, count);
}
