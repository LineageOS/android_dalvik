/* Copyright (c) 2012, The Linux Foundation. All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef DALVIK_VM_COMPILER_POSTOPTIMIZATION_H_
#define DALVIK_VM_COMPILER_POSTOPTIMIZATION_H_

#include "Dalvik.h"
#include "libdex/DexOpcodes.h"
#include "compiler/codegen/arm/ArmLIR.h"

//#include "compiler/codegen/Ralloc.h"

//#include "compiler/codegen/arm/ArmLIR.h"
/*
#include "InlineNative.h"
#include "vm/Globals.h"
#include "vm/compiler/Loop.h"
#include "vm/compiler/Compiler.h"
#include "vm/compiler/CompilerInternals.h"
#include "vm/compiler/codegen/arm/ArmLIR.h"
#include "libdex/OpCodeNames.h"
#include "vm/compiler/codegen/arm/CalloutHelper.h"
#include "vm/compiler/codegen/arm/Ralloc.h"
*/


/* Forward declarations */
struct CompilationUnit;
struct LIR;
struct MIR;
//struct RegLocation;
struct ArmLIR;

/*
enum OpCode; 
enum RegisterClass;
////enum   ArmOpCode;
enum ArmOpcode;
enum ArmConditionCode;
enum TemplateOpCode;
*/

void dvmCompilerApplyLocalOptimizations(struct CompilationUnit *cUnit,
                                        struct LIR *head,
                                        struct LIR *tail);

void dvmCompilerApplyGlobalOptimizations(struct CompilationUnit *cUnit);

bool dvmArithLocalOptimization(struct CompilationUnit *cUnit,
                             struct MIR *mir,
                             struct RegLocation rlDest,
                             struct RegLocation rlSrc1,
                             struct RegLocation rlSrc2);

typedef struct LocalOptsFuncMap{

    bool (*handleEasyDivide) (struct CompilationUnit *cUnit,
                              enum Opcode dalvikOpCode,
                              struct RegLocation rlSrc,
                              struct RegLocation rlDest,
                              int lit);
    bool (*handleEasyMultiply) (struct CompilationUnit *cUnit,
                                struct RegLocation rlSrc,
                                struct RegLocation rlDest,
                                int lit);
    bool (*handleExecuteInline) (struct CompilationUnit *cUnit,
                                 struct MIR *mir);
    void (*handleExtendedMIR) (struct CompilationUnit *cUnit,
                               struct MIR *mir);
    void (*insertChainingSwitch) (struct CompilationUnit *cUnit);
    bool (*isPopCountLE2) (unsigned int x);
    bool (*isPowerOfTwo) (int x);
    int (*lowestSetBit) (unsigned int x);
    void (*markCard) (struct CompilationUnit *cUnit,
                      int valReg,
                      int tgtAddrReg);
    void (*setupLoopEntryBlock) (struct CompilationUnit *cUnit,
                                 struct  BasicBlock *entry,
                                 struct ArmLIR *bodyLabel);
    void (*genInterpSingleStep) (struct CompilationUnit *cUnit,
                                 struct  MIR *mir);
    void (*setMemRefType) (struct ArmLIR *lir,
                           bool isLoad,
                           int memType);
    void (*annotateDalvikRegAccess) (struct ArmLIR *lir,
                                     int regId,
                                     bool isLoad);
    void (*setupResourceMasks) (struct ArmLIR *lir);
    struct ArmLIR *(*newLIR0) (struct CompilationUnit *cUnit,
                               enum ArmOpcode opCode);
    struct ArmLIR *(*newLIR1) (struct CompilationUnit *cUnit,
                               enum ArmOpcode opCode,
                               int dest);
    struct ArmLIR *(*newLIR2) (struct CompilationUnit *cUnit,
                               enum ArmOpcode opCode,
                               int dest,
                               int src1);
    struct ArmLIR *(*newLIR3) (struct CompilationUnit *cUnit,
                               enum ArmOpcode opCode,
                               int dest,
                               int src1,
                               int src2);
#if defined(_ARMV7_A) || defined(_ARMV7_A_NEON)
    struct ArmLIR *(*newLIR4) (struct CompilationUnit *cUnit,
                               enum ArmOpcode opCode,
                               int dest,
                               int src1,
                               int src2,
                               int info);
#endif
    struct RegLocation (*inlinedTarget) (struct CompilationUnit *cUnit,
                                         struct MIR *mir,
                                         bool fpHint);
    struct ArmLIR *(*genCheckCommon) (struct CompilationUnit *cUnit,
                                      int dOffset,
                                      struct ArmLIR *branch,
                                      struct ArmLIR *pcrLabel);
    struct ArmLIR *(*loadWordDisp) (struct CompilationUnit *cUnit,
                                    int rBase,
                                    int displacement,
                                    int rDest);
    struct ArmLIR *(*storeWordDisp) (struct CompilationUnit *cUnit,
                                     int rBase,
                                     int displacement,
                                     int rSrc);
    void (*loadValueDirect) (struct CompilationUnit *cUnit,
                             struct RegLocation rlSrc,
                             int reg1);
    void (*loadValueDirectFixed) (struct CompilationUnit *cUnit,
                                  struct RegLocation rlSrc,
                                  int reg1);
    void (*loadValueDirectWide) (struct CompilationUnit *cUnit,
                                 struct RegLocation rlSrc,
                                 int regLo,
                                 int regHi);
    void (*loadValueDirectWideFixed) (struct CompilationUnit *cUnit,
                                      struct RegLocation rlSrc,
                                      int regLo,
                                      int regHi);
    struct RegLocation (*loadValue) (struct CompilationUnit *cUnit,
                                     struct RegLocation rlSrc,
                                     enum RegisterClass opKind);
    void (*storeValue) (struct CompilationUnit *cUnit,
                        struct  RegLocation rlDest,
                        struct RegLocation rlSrc);
    struct RegLocation (*loadValueWide) (struct CompilationUnit *cUnit,
                                         struct RegLocation rlSrc,
                                         enum RegisterClass opKind);
    struct ArmLIR *(*genNullCheck) (struct CompilationUnit *cUnit,
                                    int sReg,
                                    int mReg,
                                    int dOffset,
                                    struct ArmLIR *pcrLabel);
    struct ArmLIR *(*genRegRegCheck) (struct CompilationUnit *cUnit,
                                      enum ArmConditionCode cond,
                                      int reg1,
                                      int reg2,
                                      int dOffset,
                                      struct ArmLIR *pcrLabel);
    struct ArmLIR *(*genZeroCheck) (struct CompilationUnit *cUnit,
                                    int mReg,
                                    int dOffset,
                                    struct ArmLIR *pcrLabel);
    struct ArmLIR *(*genBoundsCheck) (struct CompilationUnit *cUnit,
                                      int rIndex,
                                      int rBound,
                                      int dOffset,
                                      struct ArmLIR *pcrLabel);
    struct ArmLIR *(*loadConstantNoClobber) (struct CompilationUnit *cUnit,
                                             int rDest,
                                             int value);
    struct ArmLIR *(*loadConstant) (struct CompilationUnit *cUnit,
                                    int rDest,
                                    int value);
    void (*storeValueWide) (struct CompilationUnit *cUnit,
                            struct  RegLocation rlDest,
                            struct RegLocation rlSrc);
    void (*genSuspendPoll) (struct CompilationUnit *cUnit, struct MIR *mir);
    struct ArmLIR *(*storeBaseDispWide)(struct CompilationUnit *cUnit,
                                        int rBase,
                                        int displacement,
                                        int rSrcLo,
                                        int rSrcHi);
    struct ArmLIR *(*storeBaseDisp)(struct CompilationUnit *cUnit,
                                        int rBase,
                                        int displacement,
                                        int rSrc,
                                        OpSize size);
    struct ArmLIR *(*loadBaseDispWide)(struct CompilationUnit *cUnit,
                                        MIR *mir,
                                        int rBase,
                                        int displacement,
                                        int rDestLo,
                                        int rDestHi,
                                        int sReg);
    struct ArmLIR *(*opRegRegImm)(struct CompilationUnit *cUnit,
                                    enum OpKind op,
                                    int rDest,
                                    int rSrc1,
                                    int value);
    struct ArmLIR *(*opRegRegReg)(struct CompilationUnit *cUnit,
                                    enum OpKind op,
                                    int rDest,
                                    int rSrc1,
                                    int rSrc2);
    struct ArmLIR *(*loadBaseIndexed)(struct CompilationUnit *cUnit,
                                        int rBase,
                                        int rIndex,
                                        int rDest,
                                        int scale,
                                        enum OpSize size);
    struct ArmLIR *(*storeBaseIndexed)(struct CompilationUnit *cUnit,
                                        int rBase,
                                        int rIndex,
                                        int rSrc,
                                        int scale,
                                        enum OpSize size);
    enum RegisterClass (*dvmCompilerRegClassBySize)(enum OpSize size);
    int (*encodeShift)(int code, int amount);
    struct ArmLIR *(*opRegReg)(struct CompilationUnit *cUnit,
                                enum OpKind op,
                                int rDestSrc1,
                                int rSrc2);
    struct ArmLIR *(*opCondBranch)(struct CompilationUnit *cUnit,
                                    enum ArmConditionCode cc);
    struct ArmLIR *(*genIT)(struct CompilationUnit *cUnit,
                            enum ArmConditionCode code,
                            const char *guide);
    void (*genBarrier)(struct CompilationUnit *cUnit);
    int (*modifiedImmediate)(u4 value);
    struct ArmLIR *(*genRegImmCheck)(struct CompilationUnit *cUnit,
                                    enum ArmConditionCode cond,
                                    int reg,
                                    int checkValue,
                                    int dOffset,
                                    ArmLIR *pcrLabel);
} LocalOptsFuncMap;

extern LocalOptsFuncMap localOptsFunMap;

#endif  // DALVIK_VM_COMPILER_POSTOPTIMIZATION_H_

