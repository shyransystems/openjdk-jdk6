#ifdef USE_PRAGMA_IDENT_HDR
#pragma ident "@(#)globals_i486.hpp	1.46 07/05/05 17:04:16 JVM"
#endif
/*
 * Copyright 2000-2006 Sun Microsystems, Inc.  All Rights Reserved.
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

//
// Sets the default values for platform dependent flags used by the runtime system.
// (see globals.hpp)
//

define_pd_global(bool,  ConvertSleepToYield,      true);
define_pd_global(bool,  ShareVtableStubs,         true);
define_pd_global(bool,  CountInterpCalls,         true);

define_pd_global(bool, ImplicitNullChecks,          true);  // Generate code for implicit null checks
define_pd_global(bool, UncommonNullCast,            true);  // Uncommon-trap NULLs past to check cast

// See 4827828 for this change. There is no globals_core_i486.hpp. I can't
// assign a different value for C2 without touching a number of files. Use 
// #ifdef to minimize the change as it's late in Mantis. -- FIXME.
// c1 doesn't have this problem because the fix to 4858033 assures us
// the the vep is aligned at CodeEntryAlignment whereas c2 only aligns
// the uep and the vep doesn't get real alignment but just slops on by
// only assured that the entry instruction meets the 5 byte size requirement.
#ifdef COMPILER2
define_pd_global(intx,  CodeEntryAlignment,       32); 
#else
define_pd_global(intx,  CodeEntryAlignment,       16); 
#endif // COMPILER2

define_pd_global(bool, NeedsDeoptSuspend,           false); // only register window machines need this

define_pd_global(uintx, TLABSize,                 0); 
define_pd_global(uintx, NewSize,                  1024 * K);
define_pd_global(intx,  InlineFrequencyCount,     100);
define_pd_global(intx,  PreInflateSpin,		  10);

define_pd_global(intx, StackYellowPages, 2);
define_pd_global(intx, StackRedPages, 1);
define_pd_global(intx, StackShadowPages, 3 DEBUG_ONLY(+1));

define_pd_global(bool, RewriteBytecodes,     true);
define_pd_global(bool, RewriteFrequentPairs, true);
