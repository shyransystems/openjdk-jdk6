#ifdef USE_PRAGMA_IDENT_SRC
#pragma ident "@(#)threadCritical_linux.cpp	1.16 07/05/05 17:04:37 JVM"
#endif
/*
 * Copyright 2001-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#include "incls/_threadCritical_linux.cpp.incl"

// put OS-includes here
# include <pthread.h>

//
// See threadCritical.hpp for details of this class.
//

static pthread_t             tc_owner = 0;
static pthread_mutex_t       tc_mutex = PTHREAD_MUTEX_INITIALIZER;
static int                   tc_count = 0;

void ThreadCritical::initialize() {
}

void ThreadCritical::release() {
}

ThreadCritical::ThreadCritical() {
  pthread_t self = pthread_self();
  if (self != tc_owner) {
    int ret = pthread_mutex_lock(&tc_mutex);
    guarantee(ret == 0, "fatal error with pthread_mutex_lock()");
    assert(tc_count == 0, "Lock acquired with illegal reentry count.");
    tc_owner = self;
  }
  tc_count++;
}

ThreadCritical::~ThreadCritical() {
  assert(tc_owner == pthread_self(), "must have correct owner");
  assert(tc_count > 0, "must have correct count");

  tc_count--;
  if (tc_count == 0) {
    tc_owner = 0;
    int ret = pthread_mutex_unlock(&tc_mutex);
    guarantee(ret == 0, "fatal error with pthread_mutex_unlock()");
  }
}

