// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.
// 
// ===========================================================================
// File: REJIT.H
//

// 
// REJIT.H defines the class and structures used to store info about rejitted
// methods.  See comment at top of rejit.cpp for more information on how
// rejit works.
// 
// ===========================================================================
#ifndef _REJIT_H_
#define _REJIT_H_

#include "common.h"
#include "contractimpl.h"
#include "shash.h"
#include "corprof.h"
#include "codeversion.h"

class ReJitManager;
class MethodDesc;
class ClrDataAccess;

#ifdef FEATURE_REJIT

//---------------------------------------------------------------------------------------
// The CLR's implementation of ICorProfilerFunctionControl, which is passed
// to the profiler.  The profiler calls methods on this to specify the IL and
// codegen flags for a given rejit request.
// 
class ProfilerFunctionControl : public ICorProfilerFunctionControl
{
public:
    ProfilerFunctionControl(LoaderHeap * pHeap);
    virtual ~ProfilerFunctionControl();

    // IUnknown functions
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** pInterface);
    virtual ULONG STDMETHODCALLTYPE AddRef();
    virtual ULONG STDMETHODCALLTYPE Release();

    // ICorProfilerFunctionControl functions
    virtual HRESULT STDMETHODCALLTYPE SetCodegenFlags(DWORD flags);
    virtual HRESULT STDMETHODCALLTYPE SetILFunctionBody(ULONG cbNewILMethodHeader, LPCBYTE pbNewILMethodHeader);
    virtual HRESULT STDMETHODCALLTYPE SetILInstrumentedCodeMap(ULONG cILMapEntries, COR_IL_MAP * rgILMapEntries);

    // Accessors
    DWORD GetCodegenFlags();
    LPBYTE GetIL();
    ULONG GetInstrumentedMapEntryCount();
    COR_IL_MAP* GetInstrumentedMapEntries();


protected:
    Volatile<LONG> m_refCount;
    LoaderHeap * m_pHeap;
    DWORD m_dwCodegenFlags;
    ULONG m_cbIL;

    // This pointer will get copied into SharedReJitInfo::m_pbIL and owned there.
    LPBYTE m_pbIL;
    ULONG m_cInstrumentedMapEntries;
    COR_IL_MAP * m_rgInstrumentedMapEntries;
};

#endif  // FEATURE_REJIT



//---------------------------------------------------------------------------------------
// The big honcho.  One of these per AppDomain, plus one for the
// SharedDomain.  Contains the hash table of ReJitInfo structures to manage
// every rejit and revert request for its owning domain.
// 
class ReJitManager
{
    friend class ClrDataAccess;
    friend class DacDbiInterfaceImpl;

private:

#ifdef FEATURE_REJIT

    // One global crst (for the entire CLR instance) to synchronize
    // cross-ReJitManager operations, such as batch calls to RequestRejit and
    // RequestRevert (which modify multiple ReJitManager instances).
    static CrstStatic s_csGlobalRequest;

#endif //FEATURE_REJIT

public:

    static void InitStatic();

    static BOOL IsReJITEnabled();

    static HRESULT RequestReJIT(
        ULONG       cFunctions,
        ModuleID    rgModuleIDs[],
        mdMethodDef rgMethodDefs[]);
    
    static HRESULT RequestRevert(
        ULONG       cFunctions,
        ModuleID    rgModuleIDs[],
        mdMethodDef rgMethodDefs[],
        HRESULT     rgHrStatuses[]);

    static PCODE DoReJitIfNecessary(PTR_MethodDesc pMD);  // Invokes the jit, or returns previously rejitted code
    
    static CORJIT_FLAGS JitFlagsFromProfCodegenFlags(DWORD dwCodegenFlags);

    ReJitManager();

    static ReJITID GetReJitId(PTR_MethodDesc pMD, PCODE pCodeStart);

    static ReJITID GetReJitIdNoLock(PTR_MethodDesc pMD, PCODE pCodeStart);

    static HRESULT GetReJITIDs(PTR_MethodDesc pMD, ULONG cReJitIds, ULONG * pcReJitIds, ReJITID reJitIds[]);

#ifdef FEATURE_REJIT

#ifndef DACCESS_COMPILE
    static void ReportReJITError(CodeVersionManager::CodePublishError* pErrorRecord);
    static void ReportReJITError(Module* pModule, mdMethodDef methodDef, MethodDesc* pMD, HRESULT hrStatus);
#endif

    static PCODE DoReJitIfNecessaryWorker(MethodDesc* pMD);  // Invokes the jit, or returns previously rejitted code
private:

    static HRESULT BindILVersion(
        CodeVersionManager* pCodeVersionManager,
        CodeVersionManager::JumpStampBatch* pJumpStampBatch,
        PTR_Module pModule,
        mdMethodDef methodDef,
        ILCodeVersion *pILCodeVersion);

    static HRESULT MarkAllInstantiationsForReJit(
        CodeVersionManager* pCodeVersionManager,
        ILCodeVersion ilCodeVersion,
        AppDomain * pAppDomainToSearch,
        PTR_Module pModuleContainingGenericDefinition,
        mdMethodDef methodDef,
        CodeVersionManager::JumpStampBatch* pJumpStampBatch,
        CDynArray<CodeVersionManager::CodePublishError> * pRejitErrors);



    static HRESULT MarkForReJit(CodeVersionManager* pCodeVersionManager,
                                PTR_MethodDesc pMD,
                                ILCodeVersion ilCodeVersion,
                                CodeVersionManager::JumpStampBatch* pJumpStampBatch,
                                CDynArray<CodeVersionManager::CodePublishError> * pRejitErrors);

    static HRESULT RequestRevertByToken(PTR_Module pModule, mdMethodDef methodDef);
    static HRESULT Revert(ILCodeVersion ilCodeVersion, CodeVersionManager::JumpStampBatch* pJumpStampBatch);
    static PCODE   DoReJit(ILCodeVersion ilCodeVersion, MethodDesc* pMethod);

#endif // FEATURE_REJIT

};

#include "rejit.inl"

#endif // _REJIT_H_
