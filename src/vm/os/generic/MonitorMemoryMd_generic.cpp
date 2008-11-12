/*
 *   
 *
 * Copyright  1990-2007 Sun Microsystems, Inc. All Rights Reserved.
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

/*
 * MonitorMemoryMd_generic.cpp:
 */

#include "incls/_precompiled.incl"
#include "incls/_MonitorMemoryMd_generic.cpp.incl"

#if ENABLE_MEMORY_MONITOR

void MonitorMemoryMd::startup() {}
void MonitorMemoryMd::shutdown() {}
void MonitorMemoryMd::startFlushThread() {}
void MonitorMemoryMd::stopFlushThread() {}
void MonitorMemoryMd::lock() {}
void MonitorMemoryMd::unlock() {}
u_long MonitorMemoryMd::htonl_m(u_long hostlong) {
    return hostlong;
}

#ifdef __cplusplus
extern "C" {
#endif

void DeleteLimeFunction(LimeFunction *f){}

LimeFunction *NewLimeFunction(const char *packageName,
                              const char *className,
                              const char *methodName) {
    return NULL;
}

int isSocketInitialized(void) {
  return 0;
}

void socketConnect(int port, const char *server) {}
void socketDisconnect(void) {}
void socketSendCommandData(int numCommands, int sendOffset, char* sendBuffer) {}

#ifdef __cplusplus
}
#endif

#endif // ENABLE_MEMORY_MONITOR