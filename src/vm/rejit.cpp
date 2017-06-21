// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.
//
// ReJit.cpp
//

// 
// This module implements the tracking and execution of rejit requests. In order to avoid
// any overhead on the non-profiled case we don't intrude on any 'normal' data structures
// except one member on the AppDomain to hold our main hashtable and crst (the
// ReJitManager). See comments in rejit.h to understand relationships between ReJitInfo,
// SharedReJitInfo, and ReJitManager, particularly SharedReJitInfo::InternalFlags which
// capture the state of a rejit request, and ReJitInfo::InternalFlags which captures the
// state of a particular MethodDesc from a rejit request.
// 
// A ReJIT request (tracked via SharedReJitInfo) is made at the level of a (Module *,
// methodDef) pair, and thus affects all instantiations of a generic. Each MethodDesc
// affected by a ReJIT request has its state tracked via a ReJitInfo instance. A
// ReJitInfo can represent a rejit request against an already-jitted MethodDesc, or a
// rejit request against a not-yet-jitted MethodDesc (called a "pre-rejit" request). A
// Pre-ReJIT request happens when a profiler specifies a (Module *, methodDef) pair that
// has not yet been JITted, or that represents a generic function which always has the
// potential to JIT new instantiations in the future.
// 
// Top-level functions in this file of most interest are:
// 
// * (static) code:ReJitManager::RequestReJIT:
// Profiling API just delegates all rejit requests directly to this function. It is
// responsible for recording the request into the appropriate ReJITManagers and for
// jump-stamping any already-JITted functions affected by the request (so that future
// calls hit the prestub)
// 
// * code:ReJitManager::DoReJitIfNecessary:
// MethodDesc::DoPrestub calls this to determine whether it's been invoked to do a rejit.
// If so, ReJitManager::DoReJitIfNecessary is responsible for (indirectly) gathering the
// appropriate IL and codegen flags, calling UnsafeJitFunction(), and redirecting the
// jump-stamp from the prestub to the newly-rejitted code.
// 
// * code:PublishMethodHolder::PublishMethodHolder
// MethodDesc::MakeJitWorker() calls this to determine if there's an outstanding
// "pre-rejit" request for a MethodDesc that has just been jitted for the first time. We
// also call this from MethodDesc::CheckRestore when restoring generic methods.
// The holder applies the jump-stamp to the
// top of the originally JITted code, with the jump target being the prestub.
// When ReJIT is enabled this holder enters the ReJIT
// lock to enforce atomicity of doing the pre-rejit-jmp-stamp & publishing/restoring
// the PCODE, which is required to avoid races with a profiler that calls RequestReJIT
// just as the method finishes compiling/restoring.
//
// * code:PublishMethodTableHolder::PublishMethodTableHolder
// Does the same thing as PublishMethodHolder except iterating over every
// method in the MethodTable. This is called from MethodTable::SetIsRestored.
// 
// * code:ReJitManager::GetCurrentReJitFlags:
// CEEInfo::canInline() calls this as part of its calculation of whether it may inline a
// given method. (Profilers may specify on a per-rejit-request basis whether the rejit of
// a method may inline callees.)
// 
//
// #Invariants:
//
// For a given Module/MethodDef there is at most 1 SharedReJitInfo that is not Reverted,
// though there may be many that are in the Reverted state. If a method is rejitted
// multiple times, with multiple versions actively in use on the stacks, then all but the
// most recent are put into the Reverted state even though they may not yet be physically
// reverted and pitched yet.
//
// For a given MethodDesc there is at most 1 ReJitInfo in the kJumpToPrestub or kJumpToRejittedCode
// state.
// 
// The ReJitManager::m_crstTable lock is held whenever reading or writing to that
// ReJitManager instance's table (including state transitions applied to the ReJitInfo &
// SharedReJitInfo instances stored in that table).
//
// The ReJitManager::m_crstTable lock is never held during callbacks to the profiler
// such as GetReJITParameters, ReJITStarted, JITComplete, ReportReJITError
//
// Any thread holding the ReJitManager::m_crstTable lock can't block during runtime suspension
// therefore it can't call any GC_TRIGGERS functions
//
// Transitions between SharedRejitInfo states happen only in the following cicumstances:
//   1) New SharedRejitInfo added to table (Requested State)
//      Inside RequestRejit
//      Global Crst held, table Crst held
//
//   2) Requested -> GettingReJITParameters
//      Inside DoRejitIfNecessary
//      Global Crst NOT held, table Crst held
//
//   3) GettingReJITParameters -> Active
//      Inside DoRejitIfNecessary
//      Global Crst NOT held, table Crst held
//
//   4) * -> Reverted
//      Inside RequestRejit or RequestRevert
//      Global Crst held, table Crst held
//
//
// Transitions between RejitInfo states happen only in the following circumstances:
//   1) New RejitInfo added to table (kJumpNone state)
//      Inside RequestRejit, DoJumpStampIfNecessary
//      Global Crst MAY/MAY NOT be held, table Crst held
//      Allowed SharedReJit states: Requested, GettingReJITParameters, Active
//
//   2) kJumpNone -> kJumpToPrestub
//      Inside RequestRejit, DoJumpStampIfNecessary
//      Global Crst MAY/MAY NOT be held, table Crst held
//      Allowed SharedReJit states: Requested, GettingReJITParameters, Active
//
//   3) kJumpToPreStub -> kJumpToRejittedCode
//      Inside DoReJitIfNecessary
//      Global Crst NOT held, table Crst held
//      Allowed SharedReJit states: Active
//
//   4) * -> kJumpNone
//      Inside RequestRevert, RequestRejit
//      Global Crst held, table crst held
//      Allowed SharedReJit states: Reverted
//
//
// #Beware Invariant misconceptions - don't make bad assumptions!
//   Even if a SharedReJitInfo is in the Reverted state:
//     a) RejitInfos may still be in the kJumpToPreStub or kJumpToRejittedCode state
//        Reverted really just means the runtime has started reverting, but it may not
//        be complete yet on the thread executing Revert or RequestRejit.
//     b) The code for this version of the method may be executing on any number of
//        threads. Even after transitioning all rejit infos to kJumpNone state we
//        have no power to abort or hijack threads already running the rejitted code.
//
//   Even if a SharedReJitInfo is in the Active state:
//     a) The corresponding ReJitInfos may not be jump-stamped yet.
//        Some thread is still in the progress of getting this thread jump-stamped
//        OR it is a place-holder ReJitInfo.
//     b) An older ReJitInfo linked to a reverted SharedReJitInfo could still be
//        in kJumpToPreStub or kJumpToReJittedCode state. RequestRejit is still in
//        progress on some thread.
//
//
// #Known issues with REJIT at this time:
//   NGEN inlined methods will not be properly rejitted
//   Exception callstacks through rejitted code do not produce correct StackTraces
//   Live debugging is not supported when rejit is enabled
//   Rejit leaks rejitted methods, RejitInfos, and SharedRejitInfos until AppDomain unload
//   Dump debugging doesn't correctly locate RejitInfos that are keyed by MethodDesc
//   Metadata update creates large memory increase switching to RW (not specifically a rejit issue)
// 
// ======================================================================================

#include "common.h"
#include "rejit.h"
#include "method.hpp"
#include "eeconfig.h"
#include "methoditer.h"
#include "dbginterface.h"
#include "threadsuspend.h"

#ifdef FEATURE_REJIT
#ifdef FEATURE_CODE_VERSIONING

#include "../debug/ee/debugger.h"
#include "../debug/ee/walker.h"
#include "../debug/ee/controller.h"
#include "codeversion.h"

// This HRESULT is only used as a private implementation detail. Corerror.xml has a comment in it
//  reserving this value for our use but it doesn't appear in the public headers.
#define CORPROF_E_RUNTIME_SUSPEND_REQUIRED 0x80131381

// This is just used as a unique id. Overflow is OK. If we happen to have more than 4+Billion rejits
// and somehow manage to not run out of memory, we'll just have to redefine ReJITID as size_t.
/* static */
static ReJITID s_GlobalReJitId = 1;

/* static */
CrstStatic ReJitManager::s_csGlobalRequest;


//---------------------------------------------------------------------------------------
// Helpers

//static
CORJIT_FLAGS ReJitManager::JitFlagsFromProfCodegenFlags(DWORD dwCodegenFlags)
{
    LIMITED_METHOD_DAC_CONTRACT;

    CORJIT_FLAGS jitFlags;
    if ((dwCodegenFlags & COR_PRF_CODEGEN_DISABLE_ALL_OPTIMIZATIONS) != 0)
    {
        jitFlags.Set(CORJIT_FLAGS::CORJIT_FLAG_DEBUG_CODE);
    }
    if ((dwCodegenFlags & COR_PRF_CODEGEN_DISABLE_INLINING) != 0)
    {
        jitFlags.Set(CORJIT_FLAGS::CORJIT_FLAG_NO_INLINING);
    }

    // In the future more flags may be added that need to be converted here (e.g.,
    // COR_PRF_CODEGEN_ENTERLEAVE / CORJIT_FLAG_PROF_ENTERLEAVE)

    return jitFlags;
}

//---------------------------------------------------------------------------------------
// ProfilerFunctionControl implementation

ProfilerFunctionControl::ProfilerFunctionControl(LoaderHeap * pHeap) :
    m_refCount(1),
    m_pHeap(pHeap),
    m_dwCodegenFlags(0),
    m_cbIL(0),
    m_pbIL(NULL),
    m_cInstrumentedMapEntries(0),
    m_rgInstrumentedMapEntries(NULL)
{
    LIMITED_METHOD_CONTRACT;
}

ProfilerFunctionControl::~ProfilerFunctionControl()
{
    LIMITED_METHOD_CONTRACT;

    // Intentionally not deleting m_pbIL or m_rgInstrumentedMapEntries, as its ownership gets transferred to the
    // SharedReJitInfo that manages that rejit request.
}


HRESULT ProfilerFunctionControl::QueryInterface(REFIID id, void** pInterface)
{
    LIMITED_METHOD_CONTRACT;

    if ((id != IID_IUnknown) &&
        (id != IID_ICorProfilerFunctionControl))
    {
        *pInterface = NULL;
        return E_NOINTERFACE;
    }

    *pInterface = this;
    this->AddRef();
    return S_OK;
}

ULONG ProfilerFunctionControl::AddRef()
{
    LIMITED_METHOD_CONTRACT;

    return InterlockedIncrement(&m_refCount);
}

ULONG ProfilerFunctionControl::Release()
{
    LIMITED_METHOD_CONTRACT;

    ULONG refCount = InterlockedDecrement(&m_refCount);

    if (0 == refCount)
    {
        delete this;
    }

    return refCount;
}

//---------------------------------------------------------------------------------------
//
// Profiler calls this to specify a set of flags from COR_PRF_CODEGEN_FLAGS
// to control rejitting a particular methodDef.
//
// Arguments:
//    * flags - set of flags from COR_PRF_CODEGEN_FLAGS
//
// Return Value:
//    Always S_OK;
//

HRESULT ProfilerFunctionControl::SetCodegenFlags(DWORD flags)
{
    LIMITED_METHOD_CONTRACT;

    m_dwCodegenFlags = flags;
    return S_OK;
}

//---------------------------------------------------------------------------------------
//
// Profiler calls this to specify the IL to use when rejitting a particular methodDef.
//
// Arguments:
//    * cbNewILMethodHeader - Size in bytes of pbNewILMethodHeader
//    * pbNewILMethodHeader - Pointer to beginning of IL header + IL bytes.
//
// Return Value:
//    HRESULT indicating success or failure.
//
// Notes:
//    Caller owns allocating and freeing pbNewILMethodHeader as expected. 
//    SetILFunctionBody copies pbNewILMethodHeader into a separate buffer.
//

HRESULT ProfilerFunctionControl::SetILFunctionBody(ULONG cbNewILMethodHeader, LPCBYTE pbNewILMethodHeader)
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_ANY;
    }
    CONTRACTL_END;

    if (cbNewILMethodHeader == 0)
    {
        return E_INVALIDARG;
    }

    if (pbNewILMethodHeader == NULL)
    {
        return E_INVALIDARG;
    }

    _ASSERTE(m_cbIL == 0);
    _ASSERTE(m_pbIL == NULL);

#ifdef DACCESS_COMPILE
    m_pbIL = new (nothrow) BYTE[cbNewILMethodHeader];
#else
    // IL is stored on the appropriate loader heap, and its memory will be owned by the
    // SharedReJitInfo we copy the pointer to.
    m_pbIL = (LPBYTE) (void *) m_pHeap->AllocMem_NoThrow(S_SIZE_T(cbNewILMethodHeader));
#endif
    if (m_pbIL == NULL)
    {
        return E_OUTOFMEMORY;
    }

    m_cbIL = cbNewILMethodHeader;
    memcpy(m_pbIL, pbNewILMethodHeader, cbNewILMethodHeader);

    return S_OK;
}

HRESULT ProfilerFunctionControl::SetILInstrumentedCodeMap(ULONG cILMapEntries, COR_IL_MAP * rgILMapEntries)
{
#ifdef DACCESS_COMPILE
    // I'm not sure why any of these methods would need to be compiled in DAC? Could we remove the
    // entire class from the DAC'ized code build?
    _ASSERTE(!"This shouldn't be called in DAC");
    return E_NOTIMPL;
#else

    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_ANY;
    }
    CONTRACTL_END;

    if (cILMapEntries >= (MAXULONG / sizeof(COR_IL_MAP)))
    {
        // Too big!  The allocation below would overflow when calculating the size.
        return E_INVALIDARG;
    }

    if (g_pDebugInterface == NULL)
    {
        return CORPROF_E_DEBUGGING_DISABLED;
    }


    // copy the il map and il map entries into the corresponding fields.
    m_cInstrumentedMapEntries = cILMapEntries;

    // IL is stored on the appropriate loader heap, and its memory will be owned by the
    // SharedReJitInfo we copy the pointer to.
    m_rgInstrumentedMapEntries = (COR_IL_MAP*) (void *) m_pHeap->AllocMem_NoThrow(S_SIZE_T(cILMapEntries * sizeof(COR_IL_MAP)));

    if (m_rgInstrumentedMapEntries == NULL)
        return E_OUTOFMEMORY;


    memcpy_s(m_rgInstrumentedMapEntries, sizeof(COR_IL_MAP) * cILMapEntries, rgILMapEntries, sizeof(COR_IL_MAP) * cILMapEntries);

    return S_OK;
#endif // DACCESS_COMPILE
}

//---------------------------------------------------------------------------------------
//
// ReJitManager may use this to access the codegen flags the profiler had set on this
// ICorProfilerFunctionControl.
//
// Return Value:
//     * codegen flags previously set via SetCodegenFlags; 0 if none were set.
//
DWORD ProfilerFunctionControl::GetCodegenFlags()
{
    return m_dwCodegenFlags;
}

//---------------------------------------------------------------------------------------
//
// ReJitManager may use this to access the IL header + instructions the
// profiler had set on this ICorProfilerFunctionControl via SetIL
//
// Return Value:
//     * Pointer to ProfilerFunctionControl-allocated buffer containing the
//         IL header and instructions the profiler had provided.
//
LPBYTE ProfilerFunctionControl::GetIL()
{
    return m_pbIL;
}

//---------------------------------------------------------------------------------------
//
// ReJitManager may use this to access the count of instrumented map entry flags the 
// profiler had set on this ICorProfilerFunctionControl.
//
// Return Value:
//    * size of the instrumented map entry array
//
ULONG ProfilerFunctionControl::GetInstrumentedMapEntryCount()
{
    return m_cInstrumentedMapEntries;
}

//---------------------------------------------------------------------------------------
//
// ReJitManager may use this to access the instrumented map entries the 
// profiler had set on this ICorProfilerFunctionControl.
//
// Return Value:
//    * the array of instrumented map entries
//
COR_IL_MAP* ProfilerFunctionControl::GetInstrumentedMapEntries()
{
    return m_rgInstrumentedMapEntries;
}

//---------------------------------------------------------------------------------------
// ReJitManager implementation

// All the state-changey stuff is kept up here in the !DACCESS_COMPILE block.
// The more read-only inspection-y stuff follows the block.

#ifndef DACCESS_COMPILE

//---------------------------------------------------------------------------------------
//
// ICorProfilerInfo4::RequestReJIT calls into this guy to do most of the
// work. Takes care of finding the appropriate ReJitManager instances to
// record the rejit requests and perform jmp-stamping.
//
// Arguments:
//    * cFunctions - Element count of rgModuleIDs & rgMethodDefs
//    * rgModuleIDs - Parallel array of ModuleIDs to rejit
//    * rgMethodDefs - Parallel array of methodDefs to rejit
//
// Return Value:
//      HRESULT indicating success or failure of the overall operation.  Each
//      individual methodDef (or MethodDesc associated with the methodDef)
//      may encounter its own failure, which is reported by the ReJITError()
//      callback, which is called into the profiler directly.
//

// static
HRESULT ReJitManager::RequestReJIT(
    ULONG       cFunctions,
    ModuleID    rgModuleIDs[],
    mdMethodDef rgMethodDefs[])
{
    CONTRACTL
    {
        NOTHROW;
        GC_TRIGGERS;
        CAN_TAKE_LOCK;
        MODE_PREEMPTIVE;
    }
    CONTRACTL_END;

    // Serialize all RequestReJIT() and Revert() calls against each other (even across AppDomains)
    CrstHolder ch(&(s_csGlobalRequest));

    HRESULT hr = S_OK;

    // Request at least 1 method to reJIT!
    _ASSERTE ((cFunctions != 0) && (rgModuleIDs != NULL) && (rgMethodDefs != NULL));

    // Temporary storage to batch up all the ReJitInfos that will get jump stamped
    // later when the runtime is suspended.
    //
    //BUGBUG: Its not clear to me why it is safe to hold ReJitInfo* lists
    // outside the table locks. If an AppDomain unload occurred I don't see anything
    // that prevents them from being deleted. If this is a bug it is a pre-existing
    // condition and nobody has reported it as an issue yet. AppDomainExit probably
    // needs to synchronize with something.
    // Jan also pointed out the ModuleIDs have the same issue, in order to use this
    // function safely the profiler needs prevent the AppDomain which contains the
    // modules from being unloaded. I doubt any profilers are doing this intentionally
    // but calling from within typical callbacks like ModuleLoadFinished or
    // JIT events would do it for the current domain I think. Of course RequestRejit
    // could always be called with ModuleIDs in some other AppDomain.
    //END BUGBUG
    SHash<CodeVersionManager::JumpStampBatchTraits> mgrToJumpStampBatch;
    CDynArray<CodeVersionManager::CodePublishError> errorRecords;
    for (ULONG i = 0; i < cFunctions; i++)
    {
        Module * pModule = reinterpret_cast< Module * >(rgModuleIDs[i]);
        if (pModule == NULL || TypeFromToken(rgMethodDefs[i]) != mdtMethodDef)
        {
            ReportReJITError(pModule, rgMethodDefs[i], NULL, E_INVALIDARG);
            continue;
        }

        if (pModule->IsBeingUnloaded())
        {
            ReportReJITError(pModule, rgMethodDefs[i], NULL, CORPROF_E_DATAINCOMPLETE);
            continue;
        }

        if (pModule->IsReflection())
        {
            ReportReJITError(pModule, rgMethodDefs[i], NULL, CORPROF_E_MODULE_IS_DYNAMIC);
            continue;
        }

        if (!pModule->GetMDImport()->IsValidToken(rgMethodDefs[i]))
        {
            ReportReJITError(pModule, rgMethodDefs[i], NULL, E_INVALIDARG);
            continue;
        }

        MethodDesc * pMD = pModule->LookupMethodDef(rgMethodDefs[i]);

        if (pMD != NULL)
        {
            _ASSERTE(!pMD->IsNoMetadata());

            // Weird, non-user functions can't be rejitted
            if (!pMD->IsIL())
            {
                // Intentionally not reporting an error in this case, to be consistent
                // with the pre-rejit case, as we have no opportunity to report an error
                // in a pre-rejit request for a non-IL method, since the rejit manager
                // never gets a call from the prestub worker for non-IL methods.  Thus,
                // since pre-rejit requests silently ignore rejit requests for non-IL
                // methods, regular rejit requests will also silently ignore rejit requests for
                // non-IL methods to be consistent.
                continue;
            }
        }

        CodeVersionManager * pCodeVersionManager = pModule->GetCodeVersionManager();
        _ASSERTE(pCodeVersionManager != NULL);
        CodeVersionManager::JumpStampBatch * pJumpStampBatch = mgrToJumpStampBatch.Lookup(pCodeVersionManager);
        if (pJumpStampBatch == NULL)
        {
            pJumpStampBatch = new (nothrow)CodeVersionManager::JumpStampBatch(pCodeVersionManager);
            if (pJumpStampBatch == NULL)
            {
                return E_OUTOFMEMORY;
            }

            hr = S_OK;
            EX_TRY
            {
                // This guy throws when out of memory, but remains internally
                // consistent (without adding the new element)
                mgrToJumpStampBatch.Add(pJumpStampBatch);
            }
            EX_CATCH_HRESULT(hr);

            _ASSERT(hr == S_OK || hr == E_OUTOFMEMORY);
            if (FAILED(hr))
            {
                return hr;
            }
        }


        // At this stage, pMD may be NULL or non-NULL, and the specified function may or
        // may not be a generic (or a function on a generic class).  The operations
        // below depend on these conditions as follows:
        // 
        // (1) In all cases, bind to an ILCodeVersion
        // This serves as a pre-rejit request for any code that has yet to be generated
        // and will also hold the modified IL + REJITID that tracks the request generally
        // 
        // (2) IF pMD != NULL, but not generic (or function on generic class)
        // Do a REAL REJIT (add a real ReJitInfo that points to pMD and jump-stamp)
        // 
        // (3) IF pMD != NULL, and is a generic (or function on generic class)
        // Do a real rejit (including jump-stamp) for all already-jitted instantiations.

        BaseDomain * pBaseDomainFromModule = pModule->GetDomain();
        ILCodeVersion ilCodeVersion;
        {
            CodeVersionManager::TableLockHolder lock(pCodeVersionManager);

            // Bind the il code version
            hr = ReJitManager::BindILVersion(pCodeVersionManager, pJumpStampBatch, pModule, rgMethodDefs[i], &ilCodeVersion);
            if (FAILED(hr))
            {
                _ASSERTE(hr == E_OUTOFMEMORY);
                return hr;
            }

            if (pMD == NULL)
            {
                // nothing is loaded yet so only the pre-rejit placeholder is needed. We're done for this method.
                continue;
            }

            if (!pMD->HasClassOrMethodInstantiation() && pMD->HasNativeCode())
            {
                // We have a JITted non-generic. Easy case. Just mark the JITted method
                // desc as needing to be rejitted
                hr = ReJitManager::MarkForReJit(
                    pCodeVersionManager,
                    pMD,
                    ilCodeVersion,
                    pJumpStampBatch,
                    &errorRecords);

                if (FAILED(hr))
                {
                    _ASSERTE(hr == E_OUTOFMEMORY);
                    return hr;
                }
            }
            
            if (!pMD->HasClassOrMethodInstantiation())
            {
                // not generic, we're done for this method
                continue;
            }

            // Ok, now the case of a generic function (or function on generic class), which
            // is loaded, and may thus have compiled instantiations.
            // It's impossible to get to any other kind of domain from the profiling API
            _ASSERTE(pBaseDomainFromModule->IsAppDomain() ||
                pBaseDomainFromModule->IsSharedDomain());

            if (pBaseDomainFromModule->IsSharedDomain())
            {
                // Iterate through all modules loaded into the shared domain, to
                // find all instantiations living in the shared domain. This will
                // include orphaned code (i.e., shared code used by ADs that have
                // all unloaded), which is good, because orphaned code could get
                // re-adopted if a new AD is created that can use that shared code
                hr = ReJitManager::MarkAllInstantiationsForReJit(
                    pCodeVersionManager,
                    ilCodeVersion,
                    NULL,  // NULL means to search SharedDomain instead of an AD
                    pModule,
                    rgMethodDefs[i],
                    pJumpStampBatch,
                    &errorRecords);
            }
            else
            {
                // Module is unshared, so just use the module's domain to find instantiations.
                hr = ReJitManager::MarkAllInstantiationsForReJit(
                    pCodeVersionManager,
                    ilCodeVersion,
                    pBaseDomainFromModule->AsAppDomain(),
                    pModule,
                    rgMethodDefs[i],
                    pJumpStampBatch,
                    &errorRecords);
            }
            if (FAILED(hr))
            {
                _ASSERTE(hr == E_OUTOFMEMORY);
                return hr;
            }
        }

        // We want to iterate through all compilations of existing instantiations to
        // ensure they get marked for rejit.  Note: There may be zero instantiations,
        // but we won't know until we try.
        if (pBaseDomainFromModule->IsSharedDomain())
        {
            // Iterate through all real domains, to find shared instantiations.
            AppDomainIterator appDomainIterator(TRUE);
            while (appDomainIterator.Next())
            {
                AppDomain * pAppDomain = appDomainIterator.GetDomain();
                if (pAppDomain->IsUnloading())
                {
                    continue;
                }
                CodeVersionManager::TableLockHolder lock(pCodeVersionManager);
                hr = ReJitManager::MarkAllInstantiationsForReJit(
                    pCodeVersionManager,
                    ilCodeVersion,
                    pAppDomain,
                    pModule,
                    rgMethodDefs[i],
                    pJumpStampBatch,
                    &errorRecords);
                if (FAILED(hr))
                {
                    _ASSERTE(hr == E_OUTOFMEMORY);
                    return hr;
                }
            }
        }
    }   // for (ULONG i = 0; i < cFunctions; i++)

    // For each code versioning mgr, if there's work to do, suspend EE if needed,
    // enter the code versioning mgr's crst, and do the batched work.
    BOOL fEESuspended = FALSE;
    SHash<CodeVersionManager::JumpStampBatchTraits>::Iterator beginIter = mgrToJumpStampBatch.Begin();
    SHash<CodeVersionManager::JumpStampBatchTraits>::Iterator endIter = mgrToJumpStampBatch.End();
    for (SHash<CodeVersionManager::JumpStampBatchTraits>::Iterator iter = beginIter; iter != endIter; iter++)
    {
        CodeVersionManager::JumpStampBatch * pJumpStampBatch = *iter;
        CodeVersionManager * pCodeVersionManager = pJumpStampBatch->pCodeVersionManager;

        int cBatchedPreStubMethods = pJumpStampBatch->preStubMethods.Count();
        if (cBatchedPreStubMethods == 0)
        {
            continue;
        }
        if(!fEESuspended)
        {
            // As a potential future optimization we could speculatively try to update the jump stamps without
            // suspending the runtime. That needs to be plumbed through BatchUpdateJumpStamps though.
            
            ThreadSuspend::SuspendEE(ThreadSuspend::SUSPEND_FOR_REJIT);
            fEESuspended = TRUE;
        }

        CodeVersionManager::TableLockHolder lock(pCodeVersionManager);
        _ASSERTE(ThreadStore::HoldingThreadStore());
        hr = pCodeVersionManager->BatchUpdateJumpStamps(&(pJumpStampBatch->undoMethods), &(pJumpStampBatch->preStubMethods), &errorRecords);
        if (FAILED(hr))
            break;
    }
    if (fEESuspended)
    {
        ThreadSuspend::RestartEE(FALSE, TRUE);
    }

    if (FAILED(hr))
    {
        _ASSERTE(hr == E_OUTOFMEMORY);
        return hr;
    }

    // Report any errors that were batched up
    for (int i = 0; i < errorRecords.Count(); i++)
    {
        ReportReJITError(&(errorRecords[i]));
    }

    // We got through processing everything, but profiler will need to see the individual ReJITError
    // callbacks to know what, if anything, failed.
    return S_OK;
}

//---------------------------------------------------------------------------------------
//
// Helper used by ReJitManager::RequestReJIT to iterate through any generic
// instantiations of a function in a given AppDomain, and to create the corresponding
// ReJitInfos for those MethodDescs. This also adds corresponding entries to a temporary
// dynamic array created by our caller for batching up the jump-stamping we'll need to do
// later.
// 
// This method is responsible for calling ReJITError on the profiler if anything goes
// wrong.
//
// Arguments:
//    * pSharedForAllGenericInstantiations - The SharedReJitInfo for this mdMethodDef's
//        rejit request. This is what we must associate any newly-created ReJitInfo with.
//    * pAppDomainToSearch - AppDomain in which to search for generic instantiations
//        matching the specified methodDef. If it is NULL, then we'll search for all
//        MethodDescs whose metadata definition appears in a Module loaded into the
//        SharedDomain (regardless of which ADs--if any--are using those MethodDescs).
//        This captures the case of domain-neutral code that was in use by an AD that
//        unloaded, and may come into use again once a new AD loads that can use the
//        shared code.
//    * pModuleContainingMethodDef - Module* containing the specified methodDef token.
//    * methodDef - Token for the method for which we're searching for MethodDescs.
//    * pJumpStampBatch - Batch we're responsible for placing ReJitInfo's into, on which
//        the caller will update the jump stamps.
//    * pRejitErrors - Dynamic array we're responsible for adding error records into.
//        The caller will report them to the profiler outside the table lock
//   
// Returns:
//    S_OK - all methods were either marked for rejit OR have appropriate error records
//           in pRejitErrors
//    E_OUTOFMEMORY - some methods weren't marked for rejit AND we didn't have enough
//           memory to create the error records
//
// Assumptions:
//     * This function should only be called on the ReJitManager that owns the (generic)
//         definition of methodDef
//     * If pModuleContainingMethodDef is loaded into the SharedDomain, then
//         pAppDomainToSearch may be NULL (to search all instantiations loaded shared),
//         or may be non-NULL (to search all instantiations loaded into
//         pAppDomainToSearch)
//     * If pModuleContainingMethodDef is not loaded domain-neutral, then
//         pAppDomainToSearch must be non-NULL (and, indeed, must be the very AD that
//         pModuleContainingMethodDef is loaded into).
//

HRESULT ReJitManager::MarkAllInstantiationsForReJit(
    CodeVersionManager* pCodeVersionManager,
    ILCodeVersion ilCodeVersion,
    AppDomain * pAppDomainToSearch,
    PTR_Module pModuleContainingMethodDef,
    mdMethodDef methodDef,
    CodeVersionManager::JumpStampBatch* pJumpStampBatch,
    CDynArray<CodeVersionManager::CodePublishError> * pRejitErrors)
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_PREEMPTIVE;
        CAN_TAKE_LOCK;
        PRECONDITION(CheckPointer(pCodeVersionManager));
        PRECONDITION(CheckPointer(pAppDomainToSearch, NULL_OK));
        PRECONDITION(CheckPointer(pModuleContainingMethodDef));
        PRECONDITION(CheckPointer(pJumpStampBatch));
    }
    CONTRACTL_END;

    _ASSERTE(pCodeVersionManager->LockOwnedByCurrentThread());
    _ASSERTE(methodDef != mdTokenNil);
    _ASSERTE(pJumpStampBatch->pCodeVersionManager == pCodeVersionManager);

    HRESULT hr;

    BaseDomain * pDomainContainingGenericDefinition = pModuleContainingMethodDef->GetDomain();

#ifdef _DEBUG
    _ASSERTE(pCodeVersionManager == pDomainContainingGenericDefinition->GetCodeVersionManager());

    // If the generic definition is not loaded domain-neutral, then all its
    // instantiations will also be non-domain-neutral and loaded into the same
    // domain as the generic definition.  So the caller may only pass the
    // domain containing the generic definition as pAppDomainToSearch
    if (!pDomainContainingGenericDefinition->IsSharedDomain())
    {
        _ASSERTE(pDomainContainingGenericDefinition == pAppDomainToSearch);
    }
#endif //_DEBUG

    // If pAppDomainToSearch is NULL, iterate through all existing 
    // instantiations loaded into the SharedDomain. If pAppDomainToSearch is non-NULL, 
    // iterate through all existing instantiations in pAppDomainToSearch, and only consider
    // instantiations in non-domain-neutral assemblies (as we already covered domain 
    // neutral assemblies when we searched the SharedDomain).
    LoadedMethodDescIterator::AssemblyIterationMode mode = LoadedMethodDescIterator::kModeSharedDomainAssemblies;
    // these are the default flags which won't actually be used in shared mode other than
    // asserting they were specified with their default values
    AssemblyIterationFlags assemFlags = (AssemblyIterationFlags) (kIncludeLoaded | kIncludeExecution);
    ModuleIterationOption moduleFlags = (ModuleIterationOption) kModIterIncludeLoaded;
    if (pAppDomainToSearch != NULL)
    {
        mode = LoadedMethodDescIterator::kModeUnsharedADAssemblies;
        assemFlags = (AssemblyIterationFlags)(kIncludeAvailableToProfilers | kIncludeExecution);
        moduleFlags = (ModuleIterationOption)kModIterIncludeAvailableToProfilers;
    }
    LoadedMethodDescIterator it(
        pAppDomainToSearch, 
        pModuleContainingMethodDef, 
        methodDef,
        mode,
        assemFlags,
        moduleFlags);
    CollectibleAssemblyHolder<DomainAssembly *> pDomainAssembly;
    while (it.Next(pDomainAssembly.This()))
    {
        MethodDesc * pLoadedMD = it.Current();

        if (!pLoadedMD->HasNativeCode())
        {
            // Skip uninstantiated MethodDescs. The placeholder added by our caller
            // is sufficient to ensure they'll eventually be rejitted when they get
            // compiled.
            continue;
        }

        if (FAILED(hr = IsMethodSafeForReJit(pLoadedMD)))
        {
            if (FAILED(hr = CodeVersionManager::AddCodePublishError(pModuleContainingMethodDef, methodDef, pLoadedMD, hr, pRejitErrors)))
            {
                _ASSERTE(hr == E_OUTOFMEMORY);
                return hr;
            }
            continue;
        }

#ifdef _DEBUG
        if (!pDomainContainingGenericDefinition->IsSharedDomain())
        {
            // Method is defined outside of the shared domain, so its instantiation must
            // be defined in the AD we're iterating over (pAppDomainToSearch, which, as
            // asserted above, must be the same domain as the generic's definition)
            _ASSERTE(pLoadedMD->GetDomain() == pAppDomainToSearch);
        }
#endif // _DEBUG

        // This will queue up the MethodDesc for rejitting and create all the
        // look-aside tables needed.
        hr = MarkForReJit(
            pCodeVersionManager,
            pLoadedMD, 
            ilCodeVersion, 
            pJumpStampBatch,
            pRejitErrors);
        if (FAILED(hr))
        {
            _ASSERTE(hr == E_OUTOFMEMORY);
            return hr;
        }
    }

    return S_OK;
}

// static
HRESULT ReJitManager::BindILVersion(
    CodeVersionManager* pCodeVersionManager,
    CodeVersionManager::JumpStampBatch* pJumpStampBatch,
    PTR_Module pModule,
    mdMethodDef methodDef,
    ILCodeVersion *pILCodeVersion)
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_PREEMPTIVE;
        CAN_TAKE_LOCK;
        PRECONDITION(CheckPointer(pCodeVersionManager));
        PRECONDITION(CheckPointer(pJumpStampBatch));
        PRECONDITION(CheckPointer(pModule));
        PRECONDITION(CheckPointer(pILCodeVersion));
    }
    CONTRACTL_END;

    _ASSERTE(pCodeVersionManager->LockOwnedByCurrentThread());
    _ASSERTE((pModule != NULL) && (methodDef != mdTokenNil));

    // Check if there was there a previous rejit request for this method that hasn't been exposed back
    // to the profiler yet
    ILCodeVersion ilCodeVersion = pCodeVersionManager->GetActiveILCodeVersion(pModule, methodDef);

    if (ilCodeVersion.GetRejitState() == ILCodeVersion::kStateRequested)
    {
        // We can 'reuse' this instance because the profiler doesn't know about
        // it yet. (This likely happened because a profiler called RequestReJIT
        // twice in a row, without us having a chance to jmp-stamp the code yet OR
        // while iterating through instantiations of a generic, the iterator found
        // duplicate entries for the same instantiation.)
        _ASSERTE(ilCodeVersion.GetIL() == NULL);

        *pILCodeVersion = ilCodeVersion;
        return S_FALSE;
    }

    // Either there was no ILCodeVersion yet for this MethodDesc OR whatever we've found
    // couldn't be reused (and needed to be reverted).  Create a new ILCodeVersion to return
    // to the caller.
    return pCodeVersionManager->AddILCodeVersion(pModule, methodDef, InterlockedIncrement(reinterpret_cast<LONG*>(&s_GlobalReJitId)), pILCodeVersion);
}

//---------------------------------------------------------------------------------------
//
// Helper used by ReJitManager::MarkAllInstantiationsForReJit and
// ReJitManager::RequestReJIT to do the actual ReJitInfo allocation and
// placement inside m_table.
//
// Arguments:
//    * pMD - MethodDesc for which to find / create ReJitInfo. Only used if
//        we're creating a regular ReJitInfo
//    * ilCodeVersion - ILCodeVersion to associate any newly created
//        ReJitInfo with.
//    * pJumpStampBatch - a batch of methods that need to have jump stamps added
//        or removed. This method will add new ReJitInfos to the batch as needed.
//    * pRejitErrors - An array of rejit errors that this call will append to
//        if there is an error marking
//
// Return Value:
//    * S_OK: Successfully created a new ReJitInfo to manage this request
//    * S_FALSE: An existing ReJitInfo was already available to manage this
//        request, so we didn't need to create a new one.
//    * E_OUTOFMEMORY
//    * Else, a failure HRESULT indicating what went wrong.
//

HRESULT ReJitManager::MarkForReJit(
    CodeVersionManager* pCodeVersionManager,
    PTR_MethodDesc pMD, 
    ILCodeVersion ilCodeVersion, 
    CodeVersionManager::JumpStampBatch* pJumpStampBatch,
    CDynArray<CodeVersionManager::CodePublishError> * pRejitErrors)
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_PREEMPTIVE;
        CAN_TAKE_LOCK;
        PRECONDITION(CheckPointer(pCodeVersionManager));
        PRECONDITION(CheckPointer(pMD));
        PRECONDITION(CheckPointer(pJumpStampBatch));
        PRECONDITION(CheckPointer(pRejitErrors));
    }
    CONTRACTL_END;

    _ASSERTE(pCodeVersionManager->LockOwnedByCurrentThread());
    _ASSERTE(pJumpStampBatch->pCodeVersionManager == pCodeVersionManager);

    HRESULT hr = S_OK;


    // ReJitInfos with MethodDesc's need to be jump-stamped,
    NativeCodeVersion * pNativeCodeVersion = pJumpStampBatch->preStubMethods.Append();
    if (pNativeCodeVersion == NULL)
    {
        return E_OUTOFMEMORY;
    }
    hr = ilCodeVersion.AddNativeCodeVersion(pMD, pNativeCodeVersion);
    if (FAILED(hr))
    {
        _ASSERTE(hr == E_OUTOFMEMORY);
        return hr;
    }

    return S_OK;
}


// ICorProfilerInfo4::RequestRevert calls into this guy to do most of the
// work. Takes care of finding the appropriate ReJitManager instances to
// perform the revert
//
// Arguments:
//    * cFunctions - Element count of rgModuleIDs & rgMethodDefs
//    * rgModuleIDs - Parallel array of ModuleIDs to revert
//    * rgMethodDefs - Parallel array of methodDefs to revert
//    * rgHrStatuses - [out] Parallel array of HRESULTs indicating success/failure
//        of reverting each (ModuleID, methodDef).
//
// Return Value:
//      HRESULT indicating success or failure of the overall operation.  Each
//      individual methodDef (or MethodDesc associated with the methodDef)
//      may encounter its own failure, which is reported by the rgHrStatuses
//      [out] parameter.
//

// static
HRESULT ReJitManager::RequestRevert(
    ULONG       cFunctions,
    ModuleID    rgModuleIDs[],
    mdMethodDef rgMethodDefs[],
    HRESULT     rgHrStatuses[])
{
    CONTRACTL
    {
        NOTHROW;
        GC_TRIGGERS;
        CAN_TAKE_LOCK;
        MODE_PREEMPTIVE;
    }
    CONTRACTL_END;

    // Serialize all RequestReJIT() and Revert() calls against each other (even across AppDomains)
    CrstHolder ch(&(s_csGlobalRequest));

    // Request at least 1 method to revert!
    _ASSERTE ((cFunctions != 0) && (rgModuleIDs != NULL) && (rgMethodDefs != NULL));

    ThreadSuspend::SuspendEE(ThreadSuspend::SUSPEND_FOR_REJIT);
    for (ULONG i = 0; i < cFunctions; i++)
    {
        HRESULT hr = E_UNEXPECTED;
        Module * pModule = reinterpret_cast< Module * >(rgModuleIDs[i]);
        if (pModule == NULL || TypeFromToken(rgMethodDefs[i]) != mdtMethodDef)
        {
            hr = E_INVALIDARG;
        }
        else if (pModule->IsBeingUnloaded())
        {
            hr = CORPROF_E_DATAINCOMPLETE;
        }
        else if (pModule->IsReflection())
        {
            hr = CORPROF_E_MODULE_IS_DYNAMIC;
        }
        else
        {
            hr = ReJitManager::RequestRevertByToken(pModule, rgMethodDefs[i]);
        }
        
        if (rgHrStatuses != NULL)
        {
            rgHrStatuses[i] = hr;
        }
    }

    ThreadSuspend::RestartEE(FALSE /* bFinishedGC */, TRUE /* SuspendSucceded */);

    return S_OK;
}

// static

//---------------------------------------------------------------------------------------
//
// Given a methodDef token, finds the corresponding ReJitInfo, and asks the
// ReJitInfo to perform a revert.
//
// Arguments:
//    * pModule - Module to revert
//    * methodDef - methodDef token to revert
//
// Return Value:
//      HRESULT indicating success or failure.  If the method was never
//      rejitted in the first place, this method returns a special error code
//      (CORPROF_E_ACTIVE_REJIT_REQUEST_NOT_FOUND).
//      E_OUTOFMEMORY
//

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4702) // Disable bogus unreachable code warning
#endif // _MSC_VER
HRESULT ReJitManager::RequestRevertByToken(PTR_Module pModule, mdMethodDef methodDef)
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        CAN_TAKE_LOCK;
        MODE_PREEMPTIVE;
    }
    CONTRACTL_END;

    _ASSERTE(ThreadStore::HoldingThreadStore());
    return E_NOTIMPL;
    /*
    CrstHolder ch(&m_crstTable);

    _ASSERTE(pModule != NULL);
    _ASSERTE(methodDef != mdTokenNil);

    ReJitInfo * pInfo = NULL;
    MethodDesc * pMD = NULL;

    pInfo = FindNonRevertedReJitInfo(pModule, methodDef);
    if (pInfo == NULL)
    {
        pMD = pModule->LookupMethodDef(methodDef);
        pInfo = FindNonRevertedReJitInfo(pMD);
        if (pInfo == NULL)
            return CORPROF_E_ACTIVE_REJIT_REQUEST_NOT_FOUND;
    }

    _ASSERTE (pInfo != NULL);
    _ASSERTE (pInfo->m_pShared != NULL);
    ReJitManagerJumpStampBatch batch(this);
    HRESULT hr = Revert(pInfo->m_pShared, &batch);
    if (FAILED(hr))
    {
        _ASSERTE(hr == E_OUTOFMEMORY);
        return hr;
    }
    CDynArray<ReJitReportErrorWorkItem> errorRecords;
    hr = BatchUpdateJumpStamps(&(batch.undoMethods), &(batch.preStubMethods), &errorRecords);
    if (FAILED(hr))
    {
        _ASSERTE(hr == E_OUTOFMEMORY);
        return hr;
    }

    // If there were any errors, return the first one. This matches previous error handling
    // behavior that only returned the first error encountered within Revert().
    for (int i = 0; i < errorRecords.Count(); i++)
    {
        _ASSERTE(FAILED(errorRecords[i].hrStatus));
        return errorRecords[i].hrStatus;
    }
    return S_OK;*/
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER



HRESULT ReJitManager::ConfigureILCodeVersion(ILCodeVersion ilCodeVersion)
{
    STANDARD_VM_CONTRACT;

    CodeVersionManager* pCodeVersionManager = ilCodeVersion.GetModule()->GetCodeVersionManager();
    _ASSERTE(!pCodeVersionManager->LockOwnedByCurrentThread());


    HRESULT hr = S_OK;
    Module* pModule = ilCodeVersion.GetModule();
    mdMethodDef methodDef = ilCodeVersion.GetMethodDef();
    BOOL fNeedsParameters = FALSE;
    BOOL fWaitForParameters = FALSE;

    {
        // Serialize access to the rejit state
        CodeVersionManager::TableLockHolder lock(pCodeVersionManager);
        switch (ilCodeVersion.GetRejitState())
        {
        case ILCodeVersion::kStateRequested:
            ilCodeVersion.SetRejitState(ILCodeVersion::kStateGettingReJITParameters);
            fNeedsParameters = TRUE;
            break;

        case ILCodeVersion::kStateGettingReJITParameters:
            fWaitForParameters = TRUE;
            break;

        default:
            return S_OK;
        }
    }

    if (fNeedsParameters)
    {
        // Here's where we give a chance for the rejit requestor to
        // examine and modify the IL & codegen flags before it gets to
        // the JIT. This allows one to add probe calls for things like
        // code coverage, performance, or whatever. These will be
        // stored in pShared.
        _ASSERTE(pModule != NULL);
        _ASSERTE(methodDef != mdTokenNil);
        ReleaseHolder<ProfilerFunctionControl> pFuncControl =
            new (nothrow)ProfilerFunctionControl(pModule->GetLoaderAllocator()->GetLowFrequencyHeap());
        HRESULT hr = S_OK;
        if (pFuncControl == NULL)
        {
            hr = E_OUTOFMEMORY;
        }
        else
        {
            BEGIN_PIN_PROFILER(CORProfilerPresent());
            hr = g_profControlBlock.pProfInterface->GetReJITParameters(
                (ModuleID)pModule,
                methodDef,
                pFuncControl);
            END_PIN_PROFILER();
        }

        if (FAILED(hr))
        {
            {
                // Historically on failure we would revert to the kRequested state and fall-back
                // to the initial code gen. The next time the method ran it would try again.
                //
                // Preserving that behavior is possible, but a bit awkward now that we have
                // Precode swapping as well. Instead of doing that I am acting as if GetReJITParameters
                // had succeeded, using the original IL, no jit flags, and no modified IL mapping.
                // This is similar to a fallback except the profiler won't get any further attempts
                // to provide the parameters correctly. If the profiler wants another attempt it would
                // need to call RequestRejit again.
                CodeVersionManager::TableLockHolder lock(pCodeVersionManager);
                if (ilCodeVersion.GetRejitState() == ILCodeVersion::kStateGettingReJITParameters)
                {
                    ilCodeVersion.SetRejitState(ILCodeVersion::kStateActive);
                    ilCodeVersion.SetIL(ILCodeVersion(pModule, methodDef).GetIL());
                }
            }
            ReportReJITError(pModule, methodDef, pModule->LookupMethodDef(methodDef), hr);
            return S_OK;
        }
        else
        {
            CodeVersionManager::TableLockHolder lock(pCodeVersionManager);
            if (ilCodeVersion.GetRejitState() == ILCodeVersion::kStateGettingReJITParameters)
            {
                // Inside the above call to ICorProfilerCallback4::GetReJITParameters, the profiler
                // will have used the specified pFuncControl to provide its IL and codegen flags. 
                // So now we transfer it out to the SharedReJitInfo.
                ilCodeVersion.SetJitFlags(pFuncControl->GetCodegenFlags());
                ilCodeVersion.SetIL((COR_ILMETHOD*)pFuncControl->GetIL());
                // ilCodeVersion is now the owner of the memory for the IL buffer
                ilCodeVersion.SetInstrumentedILMap(pFuncControl->GetInstrumentedMapEntryCount(),
                    pFuncControl->GetInstrumentedMapEntries());
                ilCodeVersion.SetRejitState(ILCodeVersion::kStateActive);
            }
        }
    }
    else if (fWaitForParameters)
    {
        // This feels lame, but it doesn't appear like we have the good threading primitves
        // for this. What I would like is an AutoResetEvent that atomically exits the table
        // Crst when I wait on it. From what I can tell our AutoResetEvent doesn't have
        // that atomic transition which means this ordering could occur:
        // [Thread 1] detect kStateGettingParameters and exit table lock
        // [Thread 2] enter table lock, transition kStateGettingParameters -> kStateActive
        // [Thread 2] signal AutoResetEvent
        // [Thread 2] exit table lock
        // [Thread 1] wait on AutoResetEvent (which may never be signaled again)
        //
        // Another option would be ManualResetEvents, one for each SharedReJitInfo, but
        // that feels like a lot of memory overhead to handle a case which occurs rarely.
        // A third option would be dynamically creating ManualResetEvents in a side
        // dictionary on demand, but that feels like a lot of complexity for an event 
        // that occurs rarely.
        //
        // I just ended up with this simple polling loop. Assuming profiler
        // writers implement GetReJITParameters performantly we will only iterate
        // this loop once, and even then only in the rare case of threads racing
        // to JIT the same IL. If this really winds up causing performance issues
        // We can build something more sophisticated.
        while (true)
        {
            {
                CodeVersionManager::TableLockHolder lock(pCodeVersionManager);
                if (ilCodeVersion.GetRejitState() == ILCodeVersion::kStateActive)
                {
                    break; // the other thread got the parameters succesfully, go race to rejit
                }
            }
            ClrSleepEx(1, FALSE);
        }
    }
    

//---------------------------------------------------------------------------------------
//
// Transition SharedReJitInfo to Reverted state and add all associated ReJitInfos to the
// undo list in the method batch
//
// Arguments:
//      pShared - SharedReJitInfo to revert
//      pJumpStampBatch - a batch of methods that need their jump stamps reverted. This method
//                        is responsible for adding additional ReJitInfos to the list.
//
// Return Value:
//      S_OK if all MDs are batched and the SharedReJitInfo is marked reverted 
//      E_OUTOFMEMORY (MDs couldn't be added to batch, SharedReJitInfo is not reverted)
//
// Assumptions:
//      Caller must be holding this ReJitManager's table crst.
//

HRESULT ReJitManager::Revert(ILCodeVersion ilCodeVersion, CodeVersionManager::JumpStampBatch* pJumpStampBatch)
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_ANY;
    }
    CONTRACTL_END;

    CodeVersionManager* pCodeVersionManager = ilCodeVersion.GetModule()->GetCodeVersionManager();

    _ASSERTE(pCodeVersionManager->LockOwnedByCurrentThread());
    _ASSERTE((ilCodeVersion.GetRejitState() == ILCodeVersion::kStateRequested) ||
             (ilCodeVersion.GetRejitState() == ILCodeVersion::kStateGettingReJITParameters) ||
             (ilCodeVersion.GetRejitState() == ILCodeVersion::kStateActive));
    _ASSERTE(pJumpStampBatch->pCodeVersionManager == pCodeVersionManager);

    //TODO
    return E_NOTIMPL;
    /*
    HRESULT hrReturn = S_OK;
    for (ReJitInfo * pInfo = pShared->GetMethods(); pInfo != NULL; pInfo = pInfo->m_pNext)
    {
        if (pInfo->GetState() == ReJitInfo::kJumpNone)
        {
            // Nothing to revert for this MethodDesc / instantiation.
            continue;
        }

        ReJitInfo** ppInfo = pJumpStampBatch->undoMethods.Append();
        if (ppInfo == NULL)
        {
            return E_OUTOFMEMORY;
        }
        *ppInfo = pInfo;
    }

    pShared->m_dwInternalFlags &= ~SharedReJitInfo::kStateMask;
    pShared->m_dwInternalFlags |= SharedReJitInfo::kStateReverted;
    return S_OK;
    */
}


#endif // DACCESS_COMPILE
// The rest of the ReJitManager methods are safe to compile for DAC


//---------------------------------------------------------------------------------------
//
// ReJitManager instance constructor--for now, does nothing
//

ReJitManager::ReJitManager()
{
    LIMITED_METHOD_DAC_CONTRACT;
}

//---------------------------------------------------------------------------------------
//
// Used by profiler to get the ReJITID corrseponding to a (MethodDesc *, PCODE) pair. 
// Can also be used to determine whether (MethodDesc *, PCODE) corresponds to a rejit
// (vs. a regular JIT) for the purposes of deciding whether to notify the debugger about
// the rejit (and building the debugger JIT info structure).
//
// Arguments:
//      * pMD - MethodDesc * of interestg
//      * pCodeStart - PCODE of the particular interesting JITting of that MethodDesc *
//
// Return Value:
//      0 if no such ReJITID found (e.g., PCODE is from a JIT and not a rejit), else the
//      ReJITID requested.
//
// static
ReJITID ReJitManager::GetReJitId(PTR_MethodDesc pMD, PCODE pCodeStart)
{
    CONTRACTL
    {
        NOTHROW;
        CAN_TAKE_LOCK;
        GC_TRIGGERS;
        PRECONDITION(CheckPointer(pMD));
        PRECONDITION(pCodeStart != NULL);
    }
    CONTRACTL_END;

    // Fast-path: If the rejit map is empty, no need to look up anything. Do this outside
    // of a lock to impact our caller (the prestub worker) as little as possible. If the
    // map is nonempty, we'll acquire the lock at that point and do the lookup for real.
    CodeVersionManager* pCodeVersionManager = pMD->GetCodeVersionManager();
    if (pCodeVersionManager->GetNonDefaultILVersionCount() == 0)
    {
        return 0;
    }

    CodeVersionManager::TableLockHolder ch(pCodeVersionManager);
    return ReJitManager::GetReJitIdNoLock(pMD, pCodeStart);
}

//---------------------------------------------------------------------------------------
//
// See comment above code:ReJitManager::GetReJitId for main details of what this does.
// 
// This function is basically the same as GetReJitId, except caller is expected to take
// the ReJitManager lock directly (via ReJitManager::TableLockHolder). This exists so
// that ETW can explicitly take the triggering ReJitManager lock up front, and in the
// proper order, to avoid lock leveling issues, and triggering issues with other locks it
// takes that are CRST_UNSAFE_ANYMODE
// 

ReJITID ReJitManager::GetReJitIdNoLock(PTR_MethodDesc pMD, PCODE pCodeStart)
{
    CONTRACTL
    {
        NOTHROW;
        CANNOT_TAKE_LOCK;
        GC_NOTRIGGER;
        PRECONDITION(CheckPointer(pMD));
        PRECONDITION(pCodeStart != NULL);
    }
    CONTRACTL_END;

    // Caller must ensure this lock is taken!
    CodeVersionManager* pCodeVersionManager = pMD->GetCodeVersionManager();
    _ASSERTE(pCodeVersionManager->LockOwnedByCurrentThread());

    /* TODO
    ReJitInfo * pInfo = FindReJitInfo(pMD, pCodeStart, 0);
    if (pInfo == NULL)
    {
        return 0;
    }

    return pInfo->m_pShared->GetId();
    */
    return 0;

}

//---------------------------------------------------------------------------------------
//
// Called by profiler to retrieve an array of ReJITIDs corresponding to a MethodDesc *
//
// Arguments:
//      * pMD - MethodDesc * to look up
//      * cReJitIds - Element count capacity of reJitIds
//      * pcReJitIds - [out] Place total count of ReJITIDs found here; may be more than
//          cReJitIds if profiler passed an array that's too small to hold them all
//      * reJitIds - [out] Place ReJITIDs found here. Count of ReJITIDs returned here is
//          min(cReJitIds, *pcReJitIds)
//
// Return Value:
//      * S_OK: ReJITIDs successfully returned, array is big enough
//      * S_FALSE: ReJITIDs successfully found, but array was not big enough. Only
//          cReJitIds were returned and cReJitIds < *pcReJitId (latter being the total
//          number of ReJITIDs available).
//
// static
HRESULT ReJitManager::GetReJITIDs(PTR_MethodDesc pMD, ULONG cReJitIds, ULONG * pcReJitIds, ReJITID reJitIds[])
{
    CONTRACTL
    {
        NOTHROW;
        CAN_TAKE_LOCK;
        GC_NOTRIGGER;
        PRECONDITION(CheckPointer(pMD));
        PRECONDITION(pcReJitIds != NULL);
        PRECONDITION(reJitIds != NULL);
    }
    CONTRACTL_END;

    CodeVersionManager* pCodeVersionManager = pMD->GetCodeVersionManager();
    CodeVersionManager::TableLockHolder lock(pCodeVersionManager);

    ULONG cnt = 0;

    ILCodeVersionCollection ilCodeVersions = pCodeVersionManager->GetILCodeVersions(pMD);
    for (ILCodeVersionIterator iter = ilCodeVersions.Begin(), end = ilCodeVersions.End();
        iter != end; 
        iter++)
    {
        ILCodeVersion curILVersion = *iter;

        if (curILVersion.GetRejitState() == ILCodeVersion::kStateActive)
        {
            if (cnt < cReJitIds)
            {
                reJitIds[cnt] = curILVersion.GetVersionId();
            }
            ++cnt;

            // no overflow
            _ASSERTE(cnt != 0);
        }
    }
    *pcReJitIds = cnt;

    return (cnt > cReJitIds) ? S_FALSE : S_OK;
}

#endif // FEATURE_CODE_VERSIONING
#else // FEATURE_REJIT

// On architectures that don't support rejit, just keep around some do-nothing
// stubs so the rest of the VM doesn't have to be littered with #ifdef FEATURE_REJIT

// static
HRESULT ReJitManager::RequestReJIT(
    ULONG       cFunctions,
    ModuleID    rgModuleIDs[],
    mdMethodDef rgMethodDefs[])
{
    return E_NOTIMPL;
}

// static
HRESULT ReJitManager::RequestRevert(
        ULONG       cFunctions,
        ModuleID    rgModuleIDs[],
        mdMethodDef rgMethodDefs[],
        HRESULT     rgHrStatuses[])
{
    return E_NOTIMPL;
}

ReJitManager::ReJitManager()
{
}

void ReJitManager::PreInit(BOOL fSharedDomain)
{
}

ReJITID ReJitManager::GetReJitId(PTR_MethodDesc pMD, PCODE pCodeStart)
{
    return 0;
}

ReJITID ReJitManager::GetReJitIdNoLock(PTR_MethodDesc pMD, PCODE pCodeStart)
{
    return 0;
}

HRESULT ReJitManager::GetReJITIDs(PTR_MethodDesc pMD, ULONG cReJitIds, ULONG * pcReJitIds, ReJITID reJitIds[])
{
    return E_NOTIMPL;
}

#endif // FEATURE_REJIT
