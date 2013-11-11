/**
 * Copyright (c) 2012, The Linux Foundation. All rights reserved.
 * Not a Contribution, Apache license notifications and license are retained
 * for attribution purposes only.
 *
 * Copyright (c) 2005-2008, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you
 * may not use this file except in compliance with the License.  You may
 * obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * Support files for dump Dalvik info during crash from debuggerd
 **/

#include <Dalvik.h>
#include "DalvikCrashDump.h"

/* Add hook to dump dalvik information */
__attribute__ ((weak))
void dump_dalvik(ptrace_context_t* context, log_t* log, pid_t tid, bool at_fault)
{
    ALOGE("[Dalvik] No information available \n");
}
