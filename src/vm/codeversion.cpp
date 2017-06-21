// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.
// ===========================================================================
// File: CodeVersion.cpp
//
// ===========================================================================

#include "common.h"
#include "codeversion.h"

#ifdef FEATURE_CODE_VERSIONING
#include "threadsuspend.h"
#include "methoditer.h"
#include "../debug/ee/debugger.h"
#include "../debug/ee/walker.h"
#include "../debug/ee/controller.h"
#endif // FEATURE_CODE_VERSIONING

#ifndef FEATURE_CODE_VERSIONING

//
// When not using code versioning we've got a minimal implementation of 
// NativeCodeVersion + ILCodeVersion that simply wraps a MethodDesc* with
// no additional versioning information
//

NativeCodeVersion::NativeCodeVersion(const NativeCodeVersion & rhs) : m_pMethod(rhs.m_pMethod) {}
NativeCodeVersion::NativeCodeVersion(PTR_MethodDesc pMethod) : m_pMethod(pMethod) {}
BOOL NativeCodeVersion::IsNull() const { return m_pMethod == NULL; }
PTR_MethodDesc NativeCodeVersion::GetMethodDesc() const { return m_pMethod; }
PCODE NativeCodeVersion::GetNativeCode() const { return m_pMethod->GetNativeCode(); }
NativeCodeVersionId NativeCodeVersion::GetVersionId() const { return 0; }
ILCodeVersion NativeCodeVersion::GetILCodeVersion() const { return ILCodeVersion(m_pMethod); }
#ifndef DACCESS_COMPILE
BOOL NativeCodeVersion::SetNativeCodeInterlocked(PCODE pCode, PCODE pExpected) { return m_pMethod->SetNativeCodeInterlocked(pCode, pExpected); }
#endif
bool NativeCodeVersion::operator==(const NativeCodeVersion & rhs) const { return m_pMethod == rhs.m_pMethod; }
bool NativeCodeVersion::operator!=(const NativeCodeVersion & rhs) const { return !operator==(rhs); }


ILCodeVersion::ILCodeVersion() : m_pMethod(NULL) {}
ILCodeVersion::ILCodeVersion(const ILCodeVersion & ilCodeVersion) : m_pMethod(ilCodeVersion.m_pMethod) {}
BOOL ILCodeVersion::IsNull() const { return m_pMethod == NULL }
PTR_Module ILCodeVersion::GetModule() { return m_pMethod->GetModule() }
mdMethodDef ILCodeVersion::GetMethodDef() { return m_pMethod->GetMethodDef(); }
PTR_COR_ILMETHOD ILCodeVersion::GetIL() const { return m_pMethod->GetILHeader(TRUE); }
DWORD ILCodeVersion::GetJitFlags() const { return 0; }
bool ILCodeVersion::operator==(const ILCodeVersion & rhs) const { return m_pMethod == rhs.m_pMethod; }
bool ILCodeVersion::operator!=(const ILCodeVersion & rhs) const { return m_pMethod != rhs.m_pMethod; }



#else // FEATURE_CODE_VERSIONING


// This HRESULT is only used as a private implementation detail. If it escapes through public APIS
// it is a bug. Corerror.xml has a comment in it reserving this value for our use but it doesn't
// appear in the public headers.

#define CORPROF_E_RUNTIME_SUSPEND_REQUIRED 0x80131381

#ifndef DACCESS_COMPILE
NativeCodeVersionNode::NativeCodeVersionNode(NativeCodeVersionId id, MethodDesc* pMethodDesc, ReJITID parentId) :
    m_id(id),
    m_pMethodDesc(pMethodDesc),
    m_parentId(parentId),
    m_pNextMethodDescSibling(NULL),
    m_pNativeCode(NULL),
    m_optTier(NativeCodeVersion::OptimizationTier0),
    m_flags(0)
{}
#endif

PTR_MethodDesc NativeCodeVersionNode::GetMethodDesc() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return m_pMethodDesc;
}

PCODE NativeCodeVersionNode::GetNativeCode() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return m_pNativeCode;
}

ReJITID NativeCodeVersionNode::GetILVersionId() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return m_parentId;
}

ILCodeVersion NativeCodeVersionNode::GetILCodeVersion() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    PTR_MethodDesc pMD = GetMethodDesc();
    return pMD->GetCodeVersionManager()->GetILCodeVersion(pMD, GetILVersionId());
}

NativeCodeVersionId NativeCodeVersionNode::GetVersionId() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return m_id;
}

#ifndef DACCESS_COMPILE
BOOL NativeCodeVersionNode::SetNativeCodeInterlocked(PCODE pCode, PCODE pExpected)
{
    LIMITED_METHOD_CONTRACT;
    return FastInterlockCompareExchangePointer(&m_pNativeCode,
        (TADDR&)pCode, (TADDR&)pExpected) == (TADDR&)pExpected;
}
#endif

BOOL NativeCodeVersionNode::IsActiveChildVersion() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return (m_flags & IsActiveChildFlag) != 0;
}

#ifndef DACCESS_COMPILE
void NativeCodeVersionNode::SetActiveChildFlag(BOOL isActive)
{
    LIMITED_METHOD_CONTRACT;
    if (isActive)
    {
        m_flags |= IsActiveChildFlag;
    }
    else
    {
        m_flags &= ~IsActiveChildFlag;
    }
}
#endif


#ifdef FEATURE_TIERED_COMPILATION
NativeCodeVersion::OptimizationTier NativeCodeVersionNode::GetOptimizationTier() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return m_optTier;
}
#ifndef DACCESS_COMPILE
void NativeCodeVersionNode::SetOptimizationTier(NativeCodeVersion::OptimizationTier tier)
{
    LIMITED_METHOD_DAC_CONTRACT;
    m_optTier = tier;
}
#endif
#endif // FEATURE_TIERED_COMPILATION

NativeCodeVersion::NativeCodeVersion() :
    m_storageKind(StorageKind::Unknown)
{}

NativeCodeVersion::NativeCodeVersion(const NativeCodeVersion & rhs) :
    m_storageKind(rhs.m_storageKind),
    m_pVersionNode(rhs.m_pVersionNode),
    m_synthetic(rhs.m_synthetic)
{}

NativeCodeVersion::NativeCodeVersion(PTR_NativeCodeVersionNode pVersionNode) :
    m_storageKind(pVersionNode != NULL ? StorageKind::Explicit : StorageKind::Unknown),
    m_pVersionNode(pVersionNode)
{}

NativeCodeVersion::NativeCodeVersion(PTR_MethodDesc pMethod) :
    m_storageKind(pMethod != NULL ? StorageKind::Synthetic : StorageKind::Unknown)
{
    LIMITED_METHOD_DAC_CONTRACT;
    m_synthetic.m_pMethodDesc = pMethod;
}

BOOL NativeCodeVersion::IsNull() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return m_storageKind == StorageKind::Unknown;
}

BOOL NativeCodeVersion::IsDefaultVersion() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return m_storageKind == StorageKind::Synthetic;
}

PTR_MethodDesc NativeCodeVersion::GetMethodDesc() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    if (m_storageKind == StorageKind::Explicit)
    {
        return AsNode()->GetMethodDesc();
    }
    else
    {
        return m_synthetic.m_pMethodDesc;
    }
}

PCODE NativeCodeVersion::GetNativeCode() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    if (m_storageKind == StorageKind::Explicit)
    {
        return AsNode()->GetNativeCode();
    }
    else
    {
        return GetMethodDesc()->GetNativeCode();
    }
}

ILCodeVersion NativeCodeVersion::GetILCodeVersion() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    if (m_storageKind == StorageKind::Explicit)
    {
        return AsNode()->GetILCodeVersion();
    }
    else
    {
        PTR_MethodDesc pMethod = GetMethodDesc();
        return ILCodeVersion(dac_cast<PTR_Module>(pMethod->GetModule()), pMethod->GetMemberDef());
    }
}

NativeCodeVersionId NativeCodeVersion::GetVersionId() const 
{
    LIMITED_METHOD_DAC_CONTRACT;
    if (m_storageKind == StorageKind::Explicit)
    {
        return AsNode()->GetVersionId();
    }
    else
    {
        return 0;
    }
}

#ifndef DACCESS_COMPILE
BOOL NativeCodeVersion::SetNativeCodeInterlocked(PCODE pCode, PCODE pExpected)
{
    LIMITED_METHOD_CONTRACT;
    if (m_storageKind == StorageKind::Explicit)
    {
        return AsNode()->SetNativeCodeInterlocked(pCode, pExpected);
    }
    else
    {
        return GetMethodDesc()->SetNativeCodeInterlocked(pCode, pExpected);
    }
}
#endif

BOOL NativeCodeVersion::IsActiveChildVersion() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    if (m_storageKind == StorageKind::Explicit)
    {
        return AsNode()->IsActiveChildVersion();
    }
    else
    {
        MethodDescVersioningState* pMethodVersioningState = GetMethodDescVersioningState();
        if (pMethodVersioningState == NULL)
        {
            return TRUE;
        }
        return pMethodVersioningState->IsDefaultVersionActiveChild();
    }
}

PTR_MethodDescVersioningState NativeCodeVersion::GetMethodDescVersioningState() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    PTR_MethodDesc pMethodDesc = GetMethodDesc();
    CodeVersionManager* pCodeVersionManager = pMethodDesc->GetCodeVersionManager();
    return pCodeVersionManager->GetMethodDescVersioningState(pMethodDesc);
}

#ifndef DACCESS_COMPILE
void NativeCodeVersion::SetActiveChildFlag(BOOL isActive)
{
    LIMITED_METHOD_DAC_CONTRACT;
    if (m_storageKind == StorageKind::Explicit)
    {
        AsNode()->SetActiveChildFlag(isActive);
    }
    else
    {
        MethodDescVersioningState* pMethodVersioningState = GetMethodDescVersioningState();
        pMethodVersioningState->SetDefaultVersionActiveChildFlag(isActive);
    }
}

MethodDescVersioningState* NativeCodeVersion::GetMethodDescVersioningState()
{
    LIMITED_METHOD_DAC_CONTRACT;
    MethodDesc* pMethodDesc = GetMethodDesc();
    CodeVersionManager* pCodeVersionManager = pMethodDesc->GetCodeVersionManager();
    return pCodeVersionManager->GetMethodDescVersioningState(pMethodDesc);
}
#endif

#ifdef FEATURE_TIERED_COMPILATION
NativeCodeVersion::OptimizationTier NativeCodeVersion::GetOptimizationTier() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    if (m_storageKind == StorageKind::Explicit)
    {
        return AsNode()->GetOptimizationTier();
    }
    else
    {
        return NativeCodeVersion::OptimizationTier0;
    }
}

#ifndef DACCESS_COMPILE
void NativeCodeVersion::SetOptimizationTier(NativeCodeVersion::OptimizationTier tier)
{
    LIMITED_METHOD_CONTRACT;
    if (m_storageKind == StorageKind::Explicit)
    {
        AsNode()->SetOptimizationTier(tier);
    }
    else
    {
        _ASSERTE(!"Do not call SetOptimizationTier on default code versions - these versions are immutable");
    }
}
#endif
#endif

PTR_NativeCodeVersionNode NativeCodeVersion::AsNode() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    if (m_storageKind == StorageKind::Explicit)
    {
        return m_pVersionNode;
    }
    else
    {
        return NULL;
    }
}

#ifndef DACCESS_COMPILE
PTR_NativeCodeVersionNode NativeCodeVersion::AsNode()
{
    LIMITED_METHOD_CONTRACT;
    if (m_storageKind == StorageKind::Explicit)
    {
        return m_pVersionNode;
    }
    else
    {
        return NULL;
    }
}
#endif

bool NativeCodeVersion::operator==(const NativeCodeVersion & rhs) const
{
    LIMITED_METHOD_DAC_CONTRACT;
    if (m_storageKind == StorageKind::Explicit)
    {
        return (rhs.m_storageKind == StorageKind::Explicit) &&
            (rhs.AsNode() == AsNode());
    }
    else if (m_storageKind == StorageKind::Synthetic)
    {
        return (rhs.m_storageKind == StorageKind::Synthetic) &&
            (m_synthetic.m_pMethodDesc == rhs.m_synthetic.m_pMethodDesc);
    }
    else
    {
        return rhs.m_storageKind == StorageKind::Unknown;
    }
}
bool NativeCodeVersion::operator!=(const NativeCodeVersion & rhs) const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return !operator==(rhs);
}

NativeCodeVersionCollection::NativeCodeVersionCollection(PTR_MethodDesc pMethodDescFilter, ILCodeVersion ilCodeFilter) :
    m_pMethodDescFilter(pMethodDescFilter),
    m_ilCodeFilter(ilCodeFilter)
{
}

NativeCodeVersionIterator NativeCodeVersionCollection::Begin()
{
    LIMITED_METHOD_DAC_CONTRACT;
    return NativeCodeVersionIterator(this);
}
NativeCodeVersionIterator NativeCodeVersionCollection::End()
{
    LIMITED_METHOD_DAC_CONTRACT;
    return NativeCodeVersionIterator(NULL);
}

NativeCodeVersionIterator::NativeCodeVersionIterator(NativeCodeVersionCollection* pNativeCodeVersionCollection) :
    m_stage(IterationStage::Initial),
    m_pCollection(pNativeCodeVersionCollection),
    m_pLinkedListCur(dac_cast<PTR_NativeCodeVersionNode>(NULL))
{
    LIMITED_METHOD_DAC_CONTRACT;
    First();
}
void NativeCodeVersionIterator::First()
{
    LIMITED_METHOD_DAC_CONTRACT;
    if (m_pCollection == NULL)
    {
        m_stage = IterationStage::End;
    }
    Next();
}
void NativeCodeVersionIterator::Next()
{
    LIMITED_METHOD_DAC_CONTRACT;
    if (m_stage == IterationStage::Initial)
    {
        ILCodeVersion ilCodeFilter = m_pCollection->m_ilCodeFilter;
        m_stage = IterationStage::ImplicitCodeVersion;
        if (ilCodeFilter.IsNull() || ilCodeFilter.IsDefaultVersion())
        {
            m_cur = NativeCodeVersion(m_pCollection->m_pMethodDescFilter);
            return;
        }
    }
    if (m_stage == IterationStage::ImplicitCodeVersion)
    {
        m_stage = IterationStage::LinkedList;
        CodeVersionManager* pCodeVersionManager = m_pCollection->m_pMethodDescFilter->GetCodeVersionManager();
        MethodDescVersioningState* pMethodDescVersioningState = pCodeVersionManager->GetMethodDescVersioningState(m_pCollection->m_pMethodDescFilter);
        if (pMethodDescVersioningState == NULL)
        {
            m_pLinkedListCur = NULL;
        }
        else
        {
            ILCodeVersion ilCodeFilter = m_pCollection->m_ilCodeFilter;
            m_pLinkedListCur = pMethodDescVersioningState->GetFirstVersionNode();
            while (m_pLinkedListCur != NULL && !ilCodeFilter.IsNull() && ilCodeFilter.GetVersionId() != m_pLinkedListCur->GetILVersionId())
            {
                m_pLinkedListCur = m_pLinkedListCur->m_pNextMethodDescSibling;
            }
        }
        if (m_pLinkedListCur != NULL)
        {
            m_cur = NativeCodeVersion(m_pLinkedListCur);
            return;
        }
    }
    if (m_stage == IterationStage::LinkedList)
    {
        if (m_pLinkedListCur != NULL)
        {
            ILCodeVersion ilCodeFilter = m_pCollection->m_ilCodeFilter;
            do
            {
                m_pLinkedListCur = m_pLinkedListCur->m_pNextMethodDescSibling;
            } while (m_pLinkedListCur != NULL && !ilCodeFilter.IsNull() && ilCodeFilter.GetVersionId() != m_pLinkedListCur->GetILVersionId());
        }
        if (m_pLinkedListCur != NULL)
        {
            m_cur = NativeCodeVersion(m_pLinkedListCur);
            return;
        }
        else
        {
            m_stage = IterationStage::End;
            m_cur = NativeCodeVersion();
        }
    }
}
const NativeCodeVersion & NativeCodeVersionIterator::Get() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return m_cur;
}
bool NativeCodeVersionIterator::Equal(const NativeCodeVersionIterator &i) const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return m_cur == i.m_cur;
}

ILCodeVersionNode::ILCodeVersionNode() :
    m_pModule((TADDR)NULL),
    m_methodDef(0),
    m_rejitId(0),
    m_pNextILVersionNode(dac_cast<PTR_ILCodeVersionNode>(NULL)),
    m_rejitState(ILCodeVersion::kStateRequested),
    m_pIL((TADDR)NULL),
    m_jitFlags(0)
{}

#ifndef DACCESS_COMPILE
ILCodeVersionNode::ILCodeVersionNode(Module* pModule, mdMethodDef methodDef, ReJITID id) :
    m_pModule(pModule),
    m_methodDef(methodDef),
    m_rejitId(id),
    m_pNextILVersionNode(dac_cast<PTR_ILCodeVersionNode>(NULL)),
    m_rejitState(ILCodeVersion::kStateRequested),
    m_pIL(NULL),
    m_jitFlags(0)
{}
#endif

PTR_Module ILCodeVersionNode::GetModule() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return m_pModule;
}

mdMethodDef ILCodeVersionNode::GetMethodDef() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return m_methodDef;
}

ReJITID ILCodeVersionNode::GetVersionId() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return m_rejitId;
}

ILCodeVersion::RejitFlags ILCodeVersionNode::GetRejitState() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return m_rejitState;
}

PTR_COR_ILMETHOD ILCodeVersionNode::GetIL() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return m_pIL;
}

DWORD ILCodeVersionNode::GetJitFlags() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return m_jitFlags;
}

const InstrumentedILOffsetMapping* ILCodeVersionNode::GetInstrumentedILMap() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return &m_instrumentedILMap;
}

PTR_ILCodeVersionNode ILCodeVersionNode::GetNextILVersionNode() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return m_pNextILVersionNode;
}

#ifndef DACCESS_COMPILE
void ILCodeVersionNode::SetRejitState(ILCodeVersion::RejitFlags newState)
{
    LIMITED_METHOD_CONTRACT;
    m_rejitState = newState;
}

void ILCodeVersionNode::SetIL(COR_ILMETHOD* pIL)
{
    LIMITED_METHOD_CONTRACT;
    m_pIL = pIL;
}

void ILCodeVersionNode::SetJitFlags(DWORD flags)
{
    LIMITED_METHOD_CONTRACT;
    m_jitFlags = flags;
}

void ILCodeVersionNode::SetInstrumentedILMap(SIZE_T cMap, COR_IL_MAP * rgMap)
{
    LIMITED_METHOD_CONTRACT;
    m_instrumentedILMap.SetMappingInfo(cMap, rgMap);
}

void ILCodeVersionNode::SetNextILVersionNode(ILCodeVersionNode* pNextILVersionNode)
{
    LIMITED_METHOD_CONTRACT;
    m_pNextILVersionNode = pNextILVersionNode;
}
#endif

ILCodeVersion::ILCodeVersion() :
    m_storageKind(StorageKind::Unknown)
{}

ILCodeVersion::ILCodeVersion(const ILCodeVersion & ilCodeVersion) :
    m_storageKind(ilCodeVersion.m_storageKind),
    m_pVersionNode(ilCodeVersion.m_pVersionNode),
    m_synthetic(ilCodeVersion.m_synthetic)
{}

ILCodeVersion::ILCodeVersion(PTR_ILCodeVersionNode pILCodeVersionNode) :
    m_storageKind(pILCodeVersionNode != NULL ? StorageKind::Explicit : StorageKind::Unknown),
    m_pVersionNode(pILCodeVersionNode)
{}

ILCodeVersion::ILCodeVersion(PTR_Module pModule, mdMethodDef methodDef) :
    m_storageKind(pModule != NULL ? StorageKind::Synthetic : StorageKind::Unknown)
{
    LIMITED_METHOD_DAC_CONTRACT;
    m_synthetic.m_pModule = pModule;
    m_synthetic.m_methodDef = methodDef;
}

bool ILCodeVersion::operator==(const ILCodeVersion & rhs) const
{
    LIMITED_METHOD_DAC_CONTRACT;
    if (m_storageKind == StorageKind::Explicit)
    {
        return (rhs.m_storageKind == StorageKind::Explicit) &&
            (AsNode() == rhs.AsNode());
    }
    else if (m_storageKind == StorageKind::Synthetic)
    {
        return (rhs.m_storageKind == StorageKind::Synthetic) &&
            (m_synthetic.m_pModule == rhs.m_synthetic.m_pModule) &&
            (m_synthetic.m_methodDef == rhs.m_synthetic.m_methodDef);
    }
    else
    {
        return rhs.m_storageKind == StorageKind::Unknown;
    }
}

BOOL ILCodeVersion::IsNull() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return m_storageKind == StorageKind::Unknown;
}

BOOL ILCodeVersion::IsDefaultVersion() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return m_storageKind == StorageKind::Synthetic;
}

PTR_Module ILCodeVersion::GetModule() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    if (m_storageKind == StorageKind::Explicit)
    {
        return AsNode()->GetModule();
    }
    else
    {
        return m_synthetic.m_pModule;
    }
}

mdMethodDef ILCodeVersion::GetMethodDef() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    if (m_storageKind == StorageKind::Explicit)
    {
        return AsNode()->GetMethodDef();
    }
    else
    {
        return m_synthetic.m_methodDef;
    }
}

ReJITID ILCodeVersion::GetVersionId() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    if (m_storageKind == StorageKind::Explicit)
    {
        return AsNode()->GetVersionId();
    }
    else
    {
        return 0;
    }
}

NativeCodeVersionCollection ILCodeVersion::GetNativeCodeVersions(PTR_MethodDesc pClosedMethodDesc) const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return NativeCodeVersionCollection(pClosedMethodDesc, *this);
}

NativeCodeVersion ILCodeVersion::GetActiveNativeCodeVersion(PTR_MethodDesc pClosedMethodDesc) const
{
    LIMITED_METHOD_DAC_CONTRACT;
    NativeCodeVersionCollection versions = GetNativeCodeVersions(pClosedMethodDesc);
    for (NativeCodeVersionIterator cur = versions.Begin(), end = versions.End(); cur != end; cur++)
    {
        if (cur->IsActiveChildVersion())
        {
            return *cur;
        }
    }
    return NativeCodeVersion();
}

ILCodeVersion::RejitFlags ILCodeVersion::GetRejitState() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    if (m_storageKind == StorageKind::Explicit)
    {
        return AsNode()->GetRejitState();
    }
    else
    {
        return ILCodeVersion::kStateActive;
    }
}

PTR_COR_ILMETHOD ILCodeVersion::GetIL() const
{
    CONTRACTL
    {
        THROWS; //GetILHeader throws
        GC_NOTRIGGER;
        FORBID_FAULT;
        MODE_ANY;
    }
    CONTRACTL_END

    if (m_storageKind == StorageKind::Explicit)
    {
        return AsNode()->GetIL();
    }
    else
    {
        PTR_Module pModule = GetModule();
        PTR_MethodDesc pMethodDesc = dac_cast<PTR_MethodDesc>(pModule->LookupMethodDef(GetMethodDef()));
        if (pMethodDesc == NULL)
        {
            return NULL;
        }
        else
        {
            return dac_cast<PTR_COR_ILMETHOD>(pMethodDesc->GetILHeader(TRUE));
        }
    }
}

DWORD ILCodeVersion::GetJitFlags() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    if (m_storageKind == StorageKind::Explicit)
    {
        return AsNode()->GetJitFlags();
    }
    else
    {
        return 0;
    }
}

const InstrumentedILOffsetMapping* ILCodeVersion::GetInstrumentedILMap() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return AsNode()->GetInstrumentedILMap();
}

#ifndef DACCESS_COMPILE
void ILCodeVersion::SetRejitState(RejitFlags newState)
{
    LIMITED_METHOD_CONTRACT;
    AsNode()->SetRejitState(newState);
}

void ILCodeVersion::SetIL(COR_ILMETHOD* pIL)
{
    LIMITED_METHOD_CONTRACT;
    AsNode()->SetIL(pIL);
}

void ILCodeVersion::SetJitFlags(DWORD flags)
{
    LIMITED_METHOD_CONTRACT;
    AsNode()->SetJitFlags(flags);
}

void ILCodeVersion::SetInstrumentedILMap(SIZE_T cMap, COR_IL_MAP * rgMap)
{
    LIMITED_METHOD_CONTRACT;
    AsNode()->SetInstrumentedILMap(cMap, rgMap);
}

HRESULT ILCodeVersion::AddNativeCodeVersion(MethodDesc* pClosedMethodDesc, NativeCodeVersion* pNativeCodeVersion)
{
    LIMITED_METHOD_CONTRACT;
    CodeVersionManager* pManager = GetModule()->GetCodeVersionManager();
    HRESULT hr = pManager->AddNativeCodeVersion(*this, pClosedMethodDesc, pNativeCodeVersion);
    if (FAILED(hr))
    {
        _ASSERTE(hr == E_OUTOFMEMORY);
        return hr;
    }
    return S_OK;
}

HRESULT ILCodeVersion::GetOrCreateActiveNativeCodeVersion(MethodDesc* pClosedMethodDesc, NativeCodeVersion* pActiveNativeCodeVersion)
{
    LIMITED_METHOD_CONTRACT;
    HRESULT hr = S_OK;
    NativeCodeVersion activeNativeChild = GetActiveNativeCodeVersion(pClosedMethodDesc);
    if (activeNativeChild.IsNull())
    {
        if (FAILED(hr = AddNativeCodeVersion(pClosedMethodDesc, &activeNativeChild)))
        {
            _ASSERTE(hr == E_OUTOFMEMORY);
            return hr;
        }
    }
    // The first added child should automatically become active
    _ASSERTE(GetActiveNativeCodeVersion(pClosedMethodDesc) == activeNativeChild);
    *pActiveNativeCodeVersion = activeNativeChild;
    return S_OK;
}

HRESULT ILCodeVersion::SetActiveNativeCodeVersion(NativeCodeVersion activeNativeCodeVersion, BOOL fEESuspended)
{
    LIMITED_METHOD_CONTRACT;
    HRESULT hr = S_OK;
    MethodDesc* pMethodDesc = activeNativeCodeVersion.GetMethodDesc();
    NativeCodeVersion prevActiveVersion = GetActiveNativeCodeVersion(pMethodDesc);
    if (prevActiveVersion == activeNativeCodeVersion)
    {
        //nothing to do, this version is already active
        return S_OK;
    }

    if (!prevActiveVersion.IsNull())
    {
        prevActiveVersion.SetActiveChildFlag(FALSE);
    }
    activeNativeCodeVersion.SetActiveChildFlag(TRUE);

    // If needed update the published code body for this method
    CodeVersionManager* pCodeVersionManager = GetModule()->GetCodeVersionManager();
    if (pCodeVersionManager->GetActiveILCodeVersion(GetModule(), GetMethodDef()) == *this)
    {
        if (FAILED(hr = pCodeVersionManager->PublishNativeCodeVersion(pMethodDesc, activeNativeCodeVersion, fEESuspended)))
        {
            return hr;
        }
    }

    return S_OK;
}

ILCodeVersionNode* ILCodeVersion::AsNode()
{
    LIMITED_METHOD_CONTRACT;
    return m_pVersionNode;
}
#endif //DACCESS_COMPILE

PTR_ILCodeVersionNode ILCodeVersion::AsNode() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return m_pVersionNode;
}

ILCodeVersionCollection::ILCodeVersionCollection(PTR_Module pModule, mdMethodDef methodDef) :
    m_pModule(pModule),
    m_methodDef(methodDef)
{}

ILCodeVersionIterator ILCodeVersionCollection::Begin()
{
    LIMITED_METHOD_DAC_CONTRACT;
    return ILCodeVersionIterator(this);
}

ILCodeVersionIterator ILCodeVersionCollection::End()
{
    LIMITED_METHOD_DAC_CONTRACT;
    return ILCodeVersionIterator(NULL);
}

ILCodeVersionIterator::ILCodeVersionIterator(const ILCodeVersionIterator & iter) :
    m_stage(iter.m_stage),
    m_pCollection(iter.m_pCollection),
    m_pLinkedListCur(iter.m_pLinkedListCur),
    m_cur(iter.m_cur)
{}

ILCodeVersionIterator::ILCodeVersionIterator(ILCodeVersionCollection* pCollection) :
    m_stage(pCollection != NULL ? IterationStage::Initial : IterationStage::End),
    m_pCollection(pCollection),
    m_pLinkedListCur(dac_cast<PTR_ILCodeVersionNode>(NULL))
{
    LIMITED_METHOD_DAC_CONTRACT;
    First();
}

const ILCodeVersion & ILCodeVersionIterator::Get() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return m_cur;
}

void ILCodeVersionIterator::First()
{
    LIMITED_METHOD_DAC_CONTRACT;
    Next();
}

void ILCodeVersionIterator::Next()
{
    LIMITED_METHOD_DAC_CONTRACT;
    if (m_stage == IterationStage::Initial)
    {
        m_stage = IterationStage::ImplicitCodeVersion;
        m_cur = ILCodeVersion(m_pCollection->m_pModule, m_pCollection->m_methodDef);
        return;
    }
    if (m_stage == IterationStage::ImplicitCodeVersion)
    {
        CodeVersionManager* pCodeVersionManager = m_pCollection->m_pModule->GetCodeVersionManager();
        _ASSERTE(pCodeVersionManager->LockOwnedByCurrentThread());
        PTR_ILCodeVersioningState pILCodeVersioningState = pCodeVersionManager->GetILCodeVersioningState(m_pCollection->m_pModule, m_pCollection->m_methodDef);
        if (pILCodeVersioningState != NULL)
        {
            m_pLinkedListCur = pILCodeVersioningState->GetFirstVersionNode();
        }
        m_stage = IterationStage::LinkedList;
        if (m_pLinkedListCur != NULL)
        {
            m_cur = ILCodeVersion(m_pLinkedListCur);
            return;
        }
    }
    if (m_stage == IterationStage::LinkedList)
    {
        if (m_pLinkedListCur != NULL)
        {
            m_pLinkedListCur = m_pLinkedListCur->GetNextILVersionNode();
        }
        if (m_pLinkedListCur != NULL)
        {
            m_cur = ILCodeVersion(m_pLinkedListCur);
            return;
        }
        else
        {
            m_stage = IterationStage::End;
            m_cur = ILCodeVersion();
            return;
        }
    }
}

bool ILCodeVersionIterator::Equal(const ILCodeVersionIterator &i) const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return m_cur == i.m_cur;
}

MethodDescVersioningState::MethodDescVersioningState(PTR_MethodDesc pMethodDesc) :
    m_pMethodDesc(pMethodDesc),
    m_nextId(1),
    m_flags(IsDefaultVersionActiveChildFlag),
    m_pFirstVersionNode()
{
    LIMITED_METHOD_DAC_CONTRACT;
    ZeroMemory(m_rgSavedCode, JumpStubSize);
}

PTR_MethodDesc MethodDescVersioningState::GetMethodDesc() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return m_pMethodDesc;
}

#ifndef DACCESS_COMPILE
NativeCodeVersionId MethodDescVersioningState::AllocateVersionId()
{
    LIMITED_METHOD_CONTRACT;
    return m_nextId++;
}
#endif

PTR_NativeCodeVersionNode MethodDescVersioningState::GetFirstVersionNode() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return m_pFirstVersionNode;
}

MethodDescVersioningState::JumpStampFlags MethodDescVersioningState::GetJumpStampState()
{
    LIMITED_METHOD_DAC_CONTRACT;
    return (JumpStampFlags)(m_flags & JumpStampMask);
}

#ifndef DACCESS_COMPILE
void MethodDescVersioningState::SetJumpStampState(JumpStampFlags newState)
{
    LIMITED_METHOD_CONTRACT;
    m_flags = (m_flags & ~JumpStampMask) | (BYTE)newState;
}
#endif


//---------------------------------------------------------------------------------------
//
// Simple, thin abstraction of debugger breakpoint patching. Given an address and a
// previously procured DebuggerControllerPatch governing the code address, this decides
// whether the code address is patched. If so, it returns a pointer to the debugger's
// buffer (of what's "underneath" the int 3 patch); otherwise, it returns the code
// address itself.
//
// Arguments:
//      * pbCode - Code address to return if unpatched
//      * dbgpatch - DebuggerControllerPatch to test
//
// Return Value:
//      Either pbCode or the debugger's patch buffer, as per description above.
//
// Assumptions:
//      Caller must manually grab (and hold) the ControllerLockHolder and get the
//      DebuggerControllerPatch before calling this helper.
//      
// Notes:
//     pbCode need not equal the code address governed by dbgpatch, but is always
//     "related" (and sometimes really is equal). For example, this helper may be used
//     when writing a code byte to an internal rejit buffer (e.g., in preparation for an
//     eventual 64-bit interlocked write into the code stream), and thus pbCode would
//     point into the internal rejit buffer whereas dbgpatch governs the corresponding
//     code byte in the live code stream. This function would then be used to determine
//     whether a byte should be written into the internal rejit buffer OR into the
//     debugger controller's breakpoint buffer.
//

LPBYTE FirstCodeByteAddr(LPBYTE pbCode, DebuggerControllerPatch * dbgpatch)
{
    LIMITED_METHOD_CONTRACT;

    if (dbgpatch != NULL && dbgpatch->IsActivated())
    {
        // Debugger has patched the code, so return the address of the buffer
        return LPBYTE(&(dbgpatch->opcode));
    }

    // no active patch, just return the direct code address
    return pbCode;
}


#ifdef _DEBUG
BOOL MethodDescVersioningState::CodeIsSaved()
{
    LIMITED_METHOD_CONTRACT;

    for (size_t i = 0; i < sizeof(m_rgSavedCode); i++)
    {
        if (m_rgSavedCode[i] != 0)
            return TRUE;
    }
    return FALSE;
}
#endif //_DEBUG

//---------------------------------------------------------------------------------------
//
// Do the actual work of stamping the top of originally-jitted-code with a jmp that goes
// to the prestub. This can be called in one of three ways:
//     * Case 1: By RequestReJIT against an already-jitted function, in which case the
//         PCODE may be inferred by the MethodDesc, and our caller will have suspended
//         the EE for us, OR
//     * Case 2: By the prestub worker after jitting the original code of a function
//         (i.e., the "pre-rejit" scenario). In this case, the EE is not suspended. But
//         that's ok, because the PCODE has not yet been published to the MethodDesc, and
//         no thread can be executing inside the originally JITted function yet.
//     * Case 3: At type/method restore time for an NGEN'ed assembly. This is also the pre-rejit
//         scenario because we are guaranteed to do this before the code in the module
//         is executable. EE suspend is not required.
//
// Arguments:
//    * pCode - Case 1 (above): will be NULL, and we can infer the PCODE from the
//        MethodDesc; Case 2+3 (above, pre-rejit): will be non-NULL, and we'll need to use
//        this to find the code to stamp on top of.
//
// Return Value:
//    * S_OK: Either we successfully did the jmp-stamp, or a racing thread took care of
//        it for us.
//    * Else, HRESULT indicating failure.
//
// Assumptions:
//     The caller will have suspended the EE if necessary (case 1), before this is
//     called.
//
#ifndef DACCESS_COMPILE
HRESULT MethodDescVersioningState::JumpStampNativeCode(PCODE pCode /* = NULL */)
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        // It may seem dangerous to be stamping jumps over code while a GC is going on,
        // but we're actually safe. As we assert below, either we're holding the thread
        // store lock (and thus preventing a GC) OR we're stamping code that has not yet
        // been published (and will thus not be executed by managed therads or examined
        // by the GC).
        MODE_ANY;
    }
    CONTRACTL_END;

    PCODE pCodePublished = GetMethodDesc()->GetNativeCode();

    _ASSERTE((pCode != NULL) || (pCodePublished != NULL));
    _ASSERTE(GetMethodDesc()->GetCodeVersionManager()->LockOwnedByCurrentThread());

    HRESULT hr = S_OK;

    // We'll jump-stamp over pCode, or if pCode is NULL, jump-stamp over the published
    // code for this's MethodDesc.
    LPBYTE pbCode = (LPBYTE)pCode;
    if (pbCode == NULL)
    {
        // If caller didn't specify a pCode, just use the one that was published after
        // the original JIT.  (A specific pCode would be passed in the pre-rejit case,
        // to jump-stamp the original code BEFORE the PCODE gets published.)
        pbCode = (LPBYTE)pCodePublished;
    }
    _ASSERTE(pbCode != NULL);

    // The debugging API may also try to write to the very top of this function (though
    // with an int 3 for breakpoint purposes). Coordinate with the debugger so we know
    // whether we can safely patch the actual code, or instead write to the debugger's
    // buffer.
    DebuggerController::ControllerLockHolder lockController;

    // We could be in a race. Either two threads simultaneously JITting the same
    // method for the first time or two threads restoring NGEN'ed code.
    // Another thread may (or may not) have jump-stamped its copy of the code already
    _ASSERTE((GetJumpStampState() == JumpStampNone) || (GetJumpStampState() == JumpStampToPrestub));

    if (GetJumpStampState() == JumpStampToPrestub)
    {
        // The method has already been jump stamped so nothing left to do
        _ASSERTE(CodeIsSaved());
        return S_OK;
    }

    // Remember what we're stamping our jump on top of, so we can replace it during a
    // revert.
    for (int i = 0; i < sizeof(m_rgSavedCode); i++)
    {
        m_rgSavedCode[i] = *FirstCodeByteAddr(pbCode + i, DebuggerController::GetPatchTable()->GetPatch((CORDB_ADDRESS_TYPE *)(pbCode + i)));
    }

    EX_TRY
    {
        AllocMemTracker amt;

        // This guy might throw on out-of-memory, so rely on the tracker to clean-up
        Precode * pPrecode = Precode::Allocate(PRECODE_STUB, GetMethodDesc(), GetMethodDesc()->GetLoaderAllocator(), &amt);
        PCODE target = pPrecode->GetEntryPoint();

#if defined(_X86_) || defined(_AMD64_)

        // Normal unpatched code never starts with a jump
        // so make sure this code isn't already patched
        _ASSERTE(*FirstCodeByteAddr(pbCode, DebuggerController::GetPatchTable()->GetPatch((CORDB_ADDRESS_TYPE *)pbCode)) != X86_INSTR_JMP_REL32);

        INT64 i64OldCode = *(INT64*)pbCode;
        INT64 i64NewCode = i64OldCode;
        LPBYTE pbNewValue = (LPBYTE)&i64NewCode;
        *pbNewValue = X86_INSTR_JMP_REL32;
        INT32 UNALIGNED * pOffset = reinterpret_cast<INT32 UNALIGNED *>(&pbNewValue[1]);
        // This will throw for out-of-memory, so don't write anything until
        // after he succeeds
        // This guy will leak/cache/reuse the jumpstub
        *pOffset = rel32UsingJumpStub(reinterpret_cast<INT32 UNALIGNED *>(pbCode + 1), target, GetMethodDesc(), GetMethodDesc()->GetLoaderAllocator());

        // If we have the EE suspended or the code is unpublished there won't be contention on this code
        hr = UpdateJumpStampHelper(pbCode, i64OldCode, i64NewCode, FALSE);
        if (FAILED(hr))
        {
            ThrowHR(hr);
        }

        //
        // No failure point after this!
        //
        amt.SuppressRelease();

#else // _X86_ || _AMD64_
#error "Need to define a way to jump-stamp the prolog in a safe way for this platform"
#endif // _X86_ || _AMD64_

        SetJumpStampState(JumpStampToPrestub);
    }
    EX_CATCH_HRESULT(hr);
    _ASSERT(hr == S_OK || hr == E_OUTOFMEMORY);

    if (SUCCEEDED(hr))
    {
        _ASSERTE(GetJumpStampState() == JumpStampToPrestub);
        _ASSERTE(m_rgSavedCode[0] != 0); // saved code should not start with 0
    }

    return hr;
}


//---------------------------------------------------------------------------------------
//
// After code has been rejitted, this is called to update the jump-stamp to go from
// pointing to the prestub, to pointing to the newly rejitted code.
//
// Arguments:
//     fEESuspended - TRUE if the caller keeps the EE suspended during this call
//     pRejittedCode - jitted code for the updated IL this method should execute
//
// Assumptions:
//      This rejit manager's table crst should be held by the caller
//
// Returns - S_OK if the jump target is updated
//           CORPROF_E_RUNTIME_SUSPEND_REQUIRED if the ee isn't suspended and it
//             will need to be in order to do the update safely
HRESULT MethodDescVersioningState::UpdateJumpTarget(BOOL fEESuspended, PCODE pRejittedCode)
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_PREEMPTIVE;
    }
    CONTRACTL_END;

    MethodDesc * pMD = GetMethodDesc();
    _ASSERTE(pMD->GetCodeVersionManager()->LockOwnedByCurrentThread());
    _ASSERTE(GetJumpStampState() == JumpStampToPrestub);

    // Beginning of originally JITted code containing the jmp that we will redirect.
    BYTE * pbCode = (BYTE*)pMD->GetNativeCode();

#if defined(_X86_) || defined(_AMD64_)

    HRESULT hr = S_OK;
    {
        DebuggerController::ControllerLockHolder lockController;

        // This will throw for out-of-memory, so don't write anything until
        // after he succeeds
        // This guy will leak/cache/reuse the jumpstub
        INT32 offset = 0;
        EX_TRY
        {
            offset = rel32UsingJumpStub(
            reinterpret_cast<INT32 UNALIGNED *>(&pbCode[1]),    // base of offset
            pRejittedCode,                                      // target of jump
            pMD,
            pMD->GetLoaderAllocator());
        }
        EX_CATCH_HRESULT(hr);
        _ASSERT(hr == S_OK || hr == E_OUTOFMEMORY);
        if (FAILED(hr))
        {
            return hr;
        }
        // For validation later, remember what pbCode is right now
        INT64 i64OldValue = *(INT64 *)pbCode;

        // Assemble the INT64 of the new code bytes to write.  Start with what's there now
        INT64 i64NewValue = i64OldValue;
        LPBYTE pbNewValue = (LPBYTE)&i64NewValue;

        // First byte becomes a rel32 jmp instruction (should be a no-op as asserted
        // above, but can't hurt)
        *pbNewValue = X86_INSTR_JMP_REL32;
        // Next 4 bytes are the jmp target (offset to jmp stub)
        INT32 UNALIGNED * pnOffset = reinterpret_cast<INT32 UNALIGNED *>(&pbNewValue[1]);
        *pnOffset = offset;

        hr = UpdateJumpStampHelper(pbCode, i64OldValue, i64NewValue, !fEESuspended);
        _ASSERTE(hr == S_OK || (hr == CORPROF_E_RUNTIME_SUSPEND_REQUIRED && !fEESuspended));
    }
    if (FAILED(hr))
    {
        return hr;
    }

#else // _X86_ || _AMD64_
#error "Need to define a way to jump-stamp the prolog in a safe way for this platform"
#endif // _X86_ || _AMD64_

    // State transition
    SetJumpStampState(JumpStampToActiveVersion);
    return S_OK;
}


//---------------------------------------------------------------------------------------
//
// Poke the JITted code to satsify a revert request (or to perform an implicit revert as
// part of a second, third, etc. rejit request). Reinstates the originally JITted code
// that had been jump-stamped over to perform a prior rejit.
//
// Arguments
//     fEESuspended - TRUE if the caller keeps the EE suspended during this call
//
//
// Return Value:
//     S_OK to indicate the revert succeeded,
//     CORPROF_E_RUNTIME_SUSPEND_REQUIRED to indicate the jumpstamp hasn't been reverted
//       and EE suspension will be needed for success
//     other failure HRESULT indicating what went wrong.
//
// Assumptions:
//     Caller must be holding the owning ReJitManager's table crst.
//
HRESULT MethodDescVersioningState::UndoJumpStampNativeCode(BOOL fEESuspended)
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_ANY;
    }
    CONTRACTL_END;

    _ASSERTE(GetMethodDesc()->GetCodeVersionManager()->LockOwnedByCurrentThread());
    _ASSERTE((GetJumpStampState() == JumpStampToPrestub) || (GetJumpStampState() == JumpStampToActiveVersion));
    _ASSERTE(m_rgSavedCode[0] != 0); // saved code should not start with 0 (see above test)

    BYTE * pbCode = (BYTE*)GetMethodDesc()->GetNativeCode();
    DebuggerController::ControllerLockHolder lockController;

#if defined(_X86_) || defined(_AMD64_)
    _ASSERTE(m_rgSavedCode[0] != X86_INSTR_JMP_REL32);
    _ASSERTE(*FirstCodeByteAddr(pbCode, DebuggerController::GetPatchTable()->GetPatch((CORDB_ADDRESS_TYPE *)pbCode)) == X86_INSTR_JMP_REL32);
#else
#error "Need to define a way to jump-stamp the prolog in a safe way for this platform"
#endif // _X86_ || _AMD64_

    // For the interlocked compare, remember what pbCode is right now
    INT64 i64OldValue = *(INT64 *)pbCode;
    // Assemble the INT64 of the new code bytes to write.  Start with what's there now
    INT64 i64NewValue = i64OldValue;
    memcpy(LPBYTE(&i64NewValue), m_rgSavedCode, sizeof(m_rgSavedCode));
    HRESULT hr = UpdateJumpStampHelper(pbCode, i64OldValue, i64NewValue, !fEESuspended);
    _ASSERTE(hr == S_OK || (hr == CORPROF_E_RUNTIME_SUSPEND_REQUIRED && !fEESuspended));
    if (hr != S_OK)
        return hr;

    // Transition state of this ReJitInfo to indicate the MD no longer has any jump stamp
    SetJumpStampState(JumpStampNone);
    return S_OK;
}
#endif

//---------------------------------------------------------------------------------------
//
// This is called to modify the jump-stamp area, the first ReJitInfo::JumpStubSize bytes
// in the method's code. 
//
// Notes:
//      Callers use this method in a variety of circumstances:
//      a) when the code is unpublished (fContentionPossible == FALSE)
//      b) when the caller has taken the ThreadStoreLock and suspended the EE 
//         (fContentionPossible == FALSE)
//      c) when the code is published, the EE isn't suspended, and the jumpstamp
//         area consists of a single 5 byte long jump instruction
//         (fContentionPossible == TRUE)
//      This method will attempt to alter the jump-stamp even if the caller has not prevented
//      contention, but there is no guarantee it will be succesful. When the caller has prevented
//      contention, then success is assured. Callers may oportunistically try without
//      EE suspension, and then upgrade to EE suspension if the first attempt fails. 
//
// Assumptions:
//      This rejit manager's table crst should be held by the caller or fContentionPossible==FALSE
//      The debugger patch table lock should be held by the caller
//
// Arguments:
//      pbCode - pointer to the code where the jump stamp is placed
//      i64OldValue - the bytes which should currently be at the start of the method code
//      i64NewValue - the new bytes which should be written at the start of the method code
//      fContentionPossible - See the Notes section above.
//
// Returns:
//      S_OK => the jumpstamp has been succesfully updated.
//      CORPROF_E_RUNTIME_SUSPEND_REQUIRED => the jumpstamp remains unchanged (preventing contention will be necessary)
//      other failing HR => VirtualProtect failed, the jumpstamp remains unchanged
//
#ifndef DACCESS_COMPILE
HRESULT MethodDescVersioningState::UpdateJumpStampHelper(BYTE* pbCode, INT64 i64OldValue, INT64 i64NewValue, BOOL fContentionPossible)
{
    CONTRACTL
    {
        NOTHROW;
    GC_NOTRIGGER;
    MODE_ANY;
    }
    CONTRACTL_END;

    MethodDesc * pMD = GetMethodDesc();
    _ASSERTE(pMD->GetCodeVersionManager()->LockOwnedByCurrentThread() || !fContentionPossible);

    // When ReJIT is enabled, method entrypoints are always at least 8-byte aligned (see
    // code:EEJitManager::allocCode), so we can do a single 64-bit interlocked operation
    // to update the jump target.  However, some code may have gotten compiled before
    // the profiler had a chance to enable ReJIT (e.g., NGENd code, or code JITted
    // before a profiler attaches).  In such cases, we cannot rely on a simple
    // interlocked operation, and instead must suspend the runtime to ensure we can
    // safely update the jmp instruction.
    //
    // This method doesn't verify that the method is actually safe to rejit, we expect
    // callers to do that. At the moment NGEN'ed code is safe to rejit even if
    // it is unaligned, but code generated before the profiler attaches is not.
    if (fContentionPossible && !(IS_ALIGNED(pbCode, sizeof(INT64))))
    {
        return CORPROF_E_RUNTIME_SUSPEND_REQUIRED;
    }

    // The debugging API may also try to write to this function (though
    // with an int 3 for breakpoint purposes). Coordinate with the debugger so we know
    // whether we can safely patch the actual code, or instead write to the debugger's
    // buffer.
    if (fContentionPossible)
    {
        for (CORDB_ADDRESS_TYPE* pbProbeAddr = pbCode; pbProbeAddr < pbCode + MethodDescVersioningState::JumpStubSize; pbProbeAddr++)
        {
            if (NULL != DebuggerController::GetPatchTable()->GetPatch(pbProbeAddr))
            {
                return CORPROF_E_RUNTIME_SUSPEND_REQUIRED;
            }
        }
    }

#if defined(_X86_) || defined(_AMD64_)

    DWORD oldProt;
    if (!ClrVirtualProtect((LPVOID)pbCode, 8, PAGE_EXECUTE_READWRITE, &oldProt))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    if (fContentionPossible)
    {
        INT64 i64InterlockReportedOldValue = FastInterlockCompareExchangeLong((INT64 *)pbCode, i64NewValue, i64OldValue);
        // Since changes to these bytes are protected by this rejitmgr's m_crstTable, we
        // shouldn't have two writers conflicting.
        _ASSERTE(i64InterlockReportedOldValue == i64OldValue);
    }
    else
    {
        // In this path the caller ensures:
        //   a) no thread will execute through the prologue area we are modifying
        //   b) no thread is stopped in a prologue such that it resumes in the middle of code we are modifying
        //   c) no thread is doing a debugger patch skip operation in which an unmodified copy of the method's 
        //      code could be executed from a patch skip buffer.

        // PERF: we might still want a faster path through here if we aren't debugging that doesn't do
        // all the patch checks
        for (int i = 0; i < MethodDescVersioningState::JumpStubSize; i++)
        {
            *FirstCodeByteAddr(pbCode + i, DebuggerController::GetPatchTable()->GetPatch(pbCode + i)) = ((BYTE*)&i64NewValue)[i];
        }
    }

    if (oldProt != PAGE_EXECUTE_READWRITE)
    {
        // The CLR codebase in many locations simply ignores failures to restore the page protections
        // Its true that it isn't a problem functionally, but it seems a bit sketchy?
        // I am following the convention for now.
        ClrVirtualProtect((LPVOID)pbCode, 8, oldProt, &oldProt);
    }

    FlushInstructionCache(GetCurrentProcess(), pbCode, MethodDescVersioningState::JumpStubSize);
    return S_OK;

#else // _X86_ || _AMD64_
#error "Need to define a way to jump-stamp the prolog in a safe way for this platform"
#endif // _X86_ || _AMD64_
}
#endif

BOOL MethodDescVersioningState::IsDefaultVersionActiveChild() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return (m_flags & IsDefaultVersionActiveChildFlag) != 0;
}
#ifndef DACCESS_COMPILE
void MethodDescVersioningState::SetDefaultVersionActiveChildFlag(BOOL isActive)
{
    LIMITED_METHOD_CONTRACT;
    if (isActive)
    {
        m_flags |= IsDefaultVersionActiveChildFlag;
    }
    else
    {
        m_flags &= ~IsDefaultVersionActiveChildFlag;
    }
}

void MethodDescVersioningState::LinkNativeCodeVersionNode(NativeCodeVersionNode* pNativeCodeVersionNode)
{
    LIMITED_METHOD_CONTRACT;
    pNativeCodeVersionNode->m_pNextMethodDescSibling = m_pFirstVersionNode;
    m_pFirstVersionNode = pNativeCodeVersionNode;
}
#endif

ILCodeVersioningState::ILCodeVersioningState(PTR_Module pModule, mdMethodDef methodDef) :
    m_pModule(pModule),
    m_methodDef(methodDef),
    m_activeVersion(ILCodeVersion(pModule,methodDef)),
    m_pFirstVersionNode(dac_cast<PTR_ILCodeVersionNode>(NULL))
{}


ILCodeVersioningState::Key::Key() :
    m_pModule(dac_cast<PTR_Module>(NULL)),
    m_methodDef(0)
{}

ILCodeVersioningState::Key::Key(PTR_Module pModule, mdMethodDef methodDef) :
    m_pModule(pModule),
    m_methodDef(methodDef)
{}

size_t ILCodeVersioningState::Key::Hash() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return (size_t)(dac_cast<TADDR>(m_pModule) ^ m_methodDef);
}

bool ILCodeVersioningState::Key::operator==(const Key & rhs) const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return (m_pModule == rhs.m_pModule) && (m_methodDef == rhs.m_methodDef);
}

ILCodeVersioningState::Key ILCodeVersioningState::GetKey() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return Key(m_pModule, m_methodDef);
}

ILCodeVersion ILCodeVersioningState::GetActiveVersion() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return m_activeVersion;
}

PTR_ILCodeVersionNode ILCodeVersioningState::GetFirstVersionNode() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return m_pFirstVersionNode;
}

#ifndef DACCESS_COMPILE
void ILCodeVersioningState::SetActiveVersion(ILCodeVersion ilActiveCodeVersion)
{
    LIMITED_METHOD_CONTRACT;
    m_activeVersion = ilActiveCodeVersion;
}

void ILCodeVersioningState::LinkILCodeVersionNode(ILCodeVersionNode* pILCodeVersionNode)
{
    LIMITED_METHOD_CONTRACT;
    pILCodeVersionNode->SetNextILVersionNode(m_pFirstVersionNode);
    m_pFirstVersionNode = pILCodeVersionNode;
}
#endif

CodeVersionManager::CodeVersionManager()
{}

//---------------------------------------------------------------------------------------
//
// Called from BaseDomain::BaseDomain to do any constructor-time initialization.
// Presently, this takes care of initializing the Crst, choosing the type based on
// whether this ReJitManager belongs to the SharedDomain.
//
// Arguments:
//    * fSharedDomain - nonzero iff this ReJitManager belongs to the SharedDomain.
//    

void CodeVersionManager::PreInit(BOOL fSharedDomain)
{
    CONTRACTL
    {
        THROWS;
        GC_TRIGGERS;
        CAN_TAKE_LOCK;
        MODE_ANY;
    }
    CONTRACTL_END;

#ifndef DACCESS_COMPILE
    m_crstTable.Init(
        fSharedDomain ? CrstReJITSharedDomainTable : CrstReJITDomainTable,
        CrstFlags(CRST_UNSAFE_ANYMODE | CRST_DEBUGGER_THREAD | CRST_REENTRANCY | CRST_TAKEN_DURING_SHUTDOWN));
#endif // DACCESS_COMPILE
}

CodeVersionManager::TableLockHolder::TableLockHolder(CodeVersionManager* pCodeVersionManager) :
    CrstHolder(&pCodeVersionManager->m_crstTable)
{
}
#ifndef DACCESS_COMPILE
void CodeVersionManager::EnterLock()
{
    m_crstTable.Enter();
}
void CodeVersionManager::LeaveLock()
{
    m_crstTable.Leave();
}
#endif

#ifdef DEBUG
BOOL CodeVersionManager::LockOwnedByCurrentThread() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return const_cast<CrstExplicitInit &>(m_crstTable).OwnedByCurrentThread();
}
#endif

PTR_ILCodeVersioningState CodeVersionManager::GetILCodeVersioningState(PTR_Module pModule, mdMethodDef methodDef) const
{
    LIMITED_METHOD_DAC_CONTRACT;
    ILCodeVersioningState::Key key = ILCodeVersioningState::Key(pModule, methodDef);
    return m_ilCodeVersioningStateMap.Lookup(key);
}

PTR_MethodDescVersioningState CodeVersionManager::GetMethodDescVersioningState(PTR_MethodDesc pClosedMethodDesc) const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return m_methodDescVersioningStateMap.Lookup(pClosedMethodDesc);
}

#ifndef DACCESS_COMPILE
HRESULT CodeVersionManager::GetOrCreateILCodeVersioningState(Module* pModule, mdMethodDef methodDef, ILCodeVersioningState** ppILCodeVersioningState)
{
    LIMITED_METHOD_CONTRACT;
    HRESULT hr = S_OK;
    ILCodeVersioningState* pILCodeVersioningState = GetILCodeVersioningState(pModule, methodDef);
    if (pILCodeVersioningState == NULL)
    {
        pILCodeVersioningState = new (nothrow) ILCodeVersioningState(pModule, methodDef);
        if (pILCodeVersioningState == NULL)
        {
            return E_OUTOFMEMORY;
        }
        EX_TRY
        {
            // This throws when out of memory, but remains internally
            // consistent (without adding the new element)
            m_ilCodeVersioningStateMap.Add(pILCodeVersioningState);
        }
        EX_CATCH_HRESULT(hr);
        if (FAILED(hr))
        {
            delete pILCodeVersioningState;
            return hr;
        }
    }
    *ppILCodeVersioningState = pILCodeVersioningState;
    return S_OK;
}

HRESULT CodeVersionManager::GetOrCreateMethodDescVersioningState(MethodDesc* pMethod, MethodDescVersioningState** ppMethodVersioningState)
{
    LIMITED_METHOD_CONTRACT;
    HRESULT hr = S_OK;
    MethodDescVersioningState* pMethodVersioningState = m_methodDescVersioningStateMap.Lookup(pMethod);
    if (pMethodVersioningState == NULL)
    {
        pMethodVersioningState = new (nothrow) MethodDescVersioningState(pMethod);
        if (pMethodVersioningState == NULL)
        {
            return E_OUTOFMEMORY;
        }
        EX_TRY
        {
            // This throws when out of memory, but remains internally
            // consistent (without adding the new element)
            m_methodDescVersioningStateMap.Add(pMethodVersioningState);
        }
        EX_CATCH_HRESULT(hr);
        if (FAILED(hr))
        {
            delete pMethodVersioningState;
            return hr;
        }
    }
    *ppMethodVersioningState = pMethodVersioningState;
    return S_OK;
}
#endif // DACCESS_COMPILE

DWORD CodeVersionManager::GetNonDefaultILVersionCount()
{
    LIMITED_METHOD_DAC_CONTRACT;

    //This function is legal to call WITHOUT taking the lock
    //It is used to do a quick check if work might be needed without paying the overhead
    //of acquiring the lock and doing dictionary lookups
    return m_ilCodeVersioningStateMap.GetCount();
}

ILCodeVersionCollection CodeVersionManager::GetILCodeVersions(PTR_MethodDesc pMethod)
{
    LIMITED_METHOD_DAC_CONTRACT;
    _ASSERTE(LockOwnedByCurrentThread());
    return GetILCodeVersions(dac_cast<PTR_Module>(pMethod->GetModule()), pMethod->GetMemberDef());
}

ILCodeVersionCollection CodeVersionManager::GetILCodeVersions(PTR_Module pModule, mdMethodDef methodDef)
{
    LIMITED_METHOD_DAC_CONTRACT;
    _ASSERTE(LockOwnedByCurrentThread());
    return ILCodeVersionCollection(pModule, methodDef);
}

ILCodeVersion CodeVersionManager::GetActiveILCodeVersion(PTR_MethodDesc pMethod)
{
    LIMITED_METHOD_DAC_CONTRACT;
    _ASSERTE(LockOwnedByCurrentThread());
    return GetActiveILCodeVersion(dac_cast<PTR_Module>(pMethod->GetModule()), pMethod->GetMemberDef());
}

ILCodeVersion CodeVersionManager::GetActiveILCodeVersion(PTR_Module pModule, mdMethodDef methodDef)
{
    LIMITED_METHOD_DAC_CONTRACT;
    _ASSERTE(LockOwnedByCurrentThread());
    ILCodeVersioningState* pILCodeVersioningState = GetILCodeVersioningState(pModule, methodDef);
    if (pILCodeVersioningState == NULL)
    {
        return ILCodeVersion(pModule, methodDef);
    }
    else
    {
        return pILCodeVersioningState->GetActiveVersion();
    }
}

ILCodeVersion CodeVersionManager::GetILCodeVersion(PTR_MethodDesc pMethod, ReJITID rejitId)
{
    LIMITED_METHOD_DAC_CONTRACT;
    _ASSERTE(LockOwnedByCurrentThread());

#ifdef FEATURE_REJIT
    ILCodeVersionCollection collection = GetILCodeVersions(pMethod);
    for (ILCodeVersionIterator cur = collection.Begin(), end = collection.End(); cur != end; cur++)
    {
        if (cur->GetVersionId() == rejitId)
        {
            return *cur;
        }
    }
    return ILCodeVersion();
#else // FEATURE_REJIT
    _ASSERTE(rejitId == 0);
    return ILCodeVersion(pModule, methodDef);
#endif // FEATURE_REJIT
}

NativeCodeVersionCollection CodeVersionManager::GetNativeCodeVersions(PTR_MethodDesc pMethod) const
{
    LIMITED_METHOD_DAC_CONTRACT;
    _ASSERTE(LockOwnedByCurrentThread());
    return NativeCodeVersionCollection(pMethod, ILCodeVersion());
}

NativeCodeVersion CodeVersionManager::GetNativeCodeVersion(PTR_MethodDesc pMethod, PCODE codeStartAddress) const
{
    LIMITED_METHOD_DAC_CONTRACT;
    _ASSERTE(LockOwnedByCurrentThread());

    NativeCodeVersionCollection nativeCodeVersions = GetNativeCodeVersions(pMethod);
    for (NativeCodeVersionIterator cur = nativeCodeVersions.Begin(), end = nativeCodeVersions.End(); cur != end; cur++)
    {
        if (cur->GetNativeCode() == codeStartAddress)
        {
            return *cur;
        }
    }
    return NativeCodeVersion();
}

#ifndef DACCESS_COMPILE
HRESULT CodeVersionManager::AddILCodeVersion(Module* pModule, mdMethodDef methodDef, ReJITID rejitId, ILCodeVersion* pILCodeVersion)
{
    LIMITED_METHOD_CONTRACT;
    _ASSERTE(LockOwnedByCurrentThread());

    ILCodeVersioningState* pILCodeVersioningState;
    HRESULT hr = GetOrCreateILCodeVersioningState(pModule, methodDef, &pILCodeVersioningState);
    if (FAILED(hr))
    {
        _ASSERTE(hr == E_OUTOFMEMORY);
        return hr;
    }

    ILCodeVersionNode* pILCodeVersionNode = new (nothrow) ILCodeVersionNode(pModule, methodDef, rejitId);
    if (pILCodeVersionNode == NULL)
    {
        return E_OUTOFMEMORY;
    }
    pILCodeVersioningState->LinkILCodeVersionNode(pILCodeVersionNode);
    *pILCodeVersion = ILCodeVersion(pILCodeVersionNode);
    return S_OK;
}
    {
    }
    {
    }
    return S_OK;
}

HRESULT CodeVersionManager::AddNativeCodeVersion(ILCodeVersion ilCodeVersion, MethodDesc* pClosedMethodDesc, NativeCodeVersion* pNativeCodeVersion)
{
    LIMITED_METHOD_CONTRACT;
    _ASSERTE(LockOwnedByCurrentThread());

    MethodDescVersioningState* pMethodVersioningState;
    HRESULT hr = GetOrCreateMethodDescVersioningState(pClosedMethodDesc, &pMethodVersioningState);
    if (FAILED(hr))
    {
        _ASSERTE(hr == E_OUTOFMEMORY);
        return hr;
    }

    NativeCodeVersionId newId = pMethodVersioningState->AllocateVersionId();
    NativeCodeVersionNode* pNativeCodeVersionNode = new (nothrow) NativeCodeVersionNode(newId, pClosedMethodDesc, ilCodeVersion.GetVersionId());
    if (pNativeCodeVersionNode == NULL)
    {
        return E_OUTOFMEMORY;
    }

    pMethodVersioningState->LinkNativeCodeVersionNode(pNativeCodeVersionNode);

    // the first child added is automatically considered the active one.
    if (ilCodeVersion.GetActiveNativeCodeVersion(pClosedMethodDesc).IsNull())
    {
        pNativeCodeVersionNode->SetActiveChildFlag(TRUE);
        _ASSERTE(!ilCodeVersion.GetActiveNativeCodeVersion(pClosedMethodDesc).IsNull());

        // the new child shouldn't have any native code. If it did we might need to
        // publish that code as part of adding the node which would require callers
        // to pay attention to GC suspension and we'd need to report publishing errors
        // back to them.
        _ASSERTE(pNativeCodeVersionNode->GetNativeCode() == NULL);
    }
    *pNativeCodeVersion = NativeCodeVersion(pNativeCodeVersionNode);
    return S_OK;
}
    {
        return hr;
    }

    return S_OK;
}
#endif // DACCESS_COMPILE

//---------------------------------------------------------------------------------------
//
// Helper used by ReJitManager::RequestReJIT to jump stamp all the methods that were
// specified by the caller. Also used by RejitManager::DoJumpStampForAssemblyIfNecessary
// when rejitting a batch of generic method instantiations in a newly loaded NGEN assembly.
// 
// This method is responsible for calling ReJITError on the profiler if anything goes
// wrong.
//
// Arguments:
//    * pUndoMethods - array containing the methods that need the jump stamp removed
//    * pPreStubMethods - array containing the methods that need to be jump stamped to prestub
//    * pErrors - any errors will be appended to this array
//
// Returns:
//    S_OK - all methods are updated or added an error to the pErrors array
//    E_OUTOFMEMORY - some methods neither updated nor added an error to pErrors array
//                    ReJitInfo state remains consistent
//
// Assumptions:
//         1) Caller prevents contention by either:
//            a) Suspending the runtime
//            b) Ensuring all methods being updated haven't been published
//
#ifndef DACCESS_COMPILE
HRESULT CodeVersionManager::BatchUpdateJumpStamps(CDynArray<NativeCodeVersion> * pUndoMethods, CDynArray<NativeCodeVersion> * pPreStubMethods, CDynArray<CodePublishError> * pErrors)
{
    CONTRACTL
    {
        NOTHROW;
    GC_NOTRIGGER;
    MODE_PREEMPTIVE;
    PRECONDITION(CheckPointer(pUndoMethods));
    PRECONDITION(CheckPointer(pPreStubMethods));
    PRECONDITION(CheckPointer(pErrors));
    }
    CONTRACTL_END;

    _ASSERTE(LockOwnedByCurrentThread());
    HRESULT hr = S_OK;

    
    NativeCodeVersion * pInfoEnd = pUndoMethods->Ptr() + pUndoMethods->Count();
    for (NativeCodeVersion * pInfoCur = pUndoMethods->Ptr(); pInfoCur < pInfoEnd; pInfoCur++)
    {
        // If we are undoing jumpstamps they have been published already
        // and our caller is holding the EE suspended
        _ASSERTE(ThreadStore::HoldingThreadStore());
        MethodDescVersioningState* pMethodVersioningState = m_methodDescVersioningStateMap.Lookup(pInfoCur->GetMethodDesc());
        _ASSERTE(pMethodVersioningState != NULL);
        if (FAILED(hr = pMethodVersioningState->UndoJumpStampNativeCode(TRUE)))
        {
            if (FAILED(hr = AddCodePublishError(*pInfoCur, hr, pErrors)))
            {
                _ASSERTE(hr == E_OUTOFMEMORY);
                return hr;
            }
        }
    }

    pInfoEnd = pPreStubMethods->Ptr() + pPreStubMethods->Count();
    for (NativeCodeVersion * pInfoCur = pPreStubMethods->Ptr(); pInfoCur < pInfoEnd; pInfoCur++)
    {
        MethodDescVersioningState* pMethodVersioningState = m_methodDescVersioningStateMap.Lookup(pInfoCur->GetMethodDesc());
        _ASSERTE(pMethodVersioningState != NULL);
        if (FAILED(hr = pMethodVersioningState->JumpStampNativeCode()))
        {
            if (FAILED(hr = AddCodePublishError(*pInfoCur, hr, pErrors)))
            {
                _ASSERTE(hr == E_OUTOFMEMORY);
                return hr;
            }
        }
    }
    return S_OK;
}
#endif

#ifndef DACCESS_COMPILE
//static
void CodeVersionManager::OnAppDomainExit(AppDomain * pAppDomain)
{
    // This would clean up all the allocations we have done and synchronize with any threads that might
    // still be using the data
    _ASSERTE(!".Net Core shouldn't be doing app domain shutdown - if we start doing so this needs to be implemented");
}
#endif

//---------------------------------------------------------------------------------------
//
// Helper that inits a new ReJitReportErrorWorkItem and adds it to the pErrors array
//
// Arguments:
//      * pModule - The module in the module/MethodDef identifier pair for the method which
//                  had an error during rejit
//      * methodDef - The MethodDef in the module/MethodDef identifier pair for the method which
//                  had an error during rejit
//      * pMD - If available, the specific method instance which had an error during rejit
//      * hrStatus - HRESULT for the rejit error that occurred
//      * pErrors - the list of error records that this method will append to
//
// Return Value:
//      * S_OK: error was appended
//      * E_OUTOFMEMORY: Not enough memory to create the new error item. The array is unchanged.
//

//static
#ifndef DACCESS_COMPILE
HRESULT CodeVersionManager::AddCodePublishError(Module* pModule, mdMethodDef methodDef, MethodDesc* pMD, HRESULT hrStatus, CDynArray<CodePublishError> * pErrors)
{
    CONTRACTL
    {
        NOTHROW;
    GC_NOTRIGGER;
    MODE_ANY;
    }
    CONTRACTL_END;

    CodePublishError* pError = pErrors->Append();
    if (pError == NULL)
    {
        return E_OUTOFMEMORY;
    }
    pError->pModule = pModule;
    pError->methodDef = methodDef;
    pError->pMethodDesc = pMD;
    pError->hrStatus = hrStatus;
    return S_OK;
}
#endif

//---------------------------------------------------------------------------------------
//
// Helper that inits a new ReJitReportErrorWorkItem and adds it to the pErrors array
//
// Arguments:
//      * pReJitInfo - The method which had an error during rejit
//      * hrStatus - HRESULT for the rejit error that occurred
//      * pErrors - the list of error records that this method will append to
//
// Return Value:
//      * S_OK: error was appended
//      * E_OUTOFMEMORY: Not enough memory to create the new error item. The array is unchanged.
//

//static
#ifndef DACCESS_COMPILE
HRESULT CodeVersionManager::AddCodePublishError(NativeCodeVersion nativeCodeVersion, HRESULT hrStatus, CDynArray<CodePublishError> * pErrors)
{
    CONTRACTL
    {
        NOTHROW;
    GC_NOTRIGGER;
    MODE_ANY;
    }
    CONTRACTL_END;

    return E_NOTIMPL;
    //TODO
    /*

    Module * pModule = NULL;
    mdMethodDef methodDef = mdTokenNil;
    pReJitInfo->GetModuleAndTokenRegardlessOfKeyType(&pModule, &methodDef);
    return AddReJITError(pModule, methodDef, pReJitInfo->GetMethodDesc(), hrStatus, pErrors);
    */
}
#endif // DACCESS_COMPILE

#endif // FEATURE_CODE_VERSIONING

