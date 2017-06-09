// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.
// ===========================================================================
// File: CodeVersion.h
//
// ===========================================================================


#ifndef CODE_VERSION_H
#define CODE_VERSION_H

#ifdef FEATURE_CODE_VERSIONING

class NativeCodeVersionNode;
typedef DPTR(class NativeCodeVersionNode) PTR_NativeCodeVersionNode;
class NativeCodeVersion;
class NativeCodeVersionCollection;
class NativeCodeVersionIterator;
class ILCodeVersionNode;
typedef DPTR(class ILCodeVersionNode) PTR_ILCodeVersionNode;
class ILCodeVersion;
class ILCodeVersionCollection;
class ILCodeVersionIterator;
class MethodDescVersioningState;
typedef DPTR(class MethodDescVersioningState) PTR_MethodDescVersioningState;
class CodeVersionManager;



typedef DWORD NativeCodeVersionId;

class NativeCodeVersionNode
{
    friend NativeCodeVersionIterator;
    friend MethodDescVersioningState;
    friend ILCodeVersionNode;
public:
#ifndef DACCESS_COMPILE
    NativeCodeVersionNode(NativeCodeVersionId id, MethodDesc* pMethod, ReJITID parentId);
#endif
    PTR_MethodDesc GetMethodDesc() const;
    NativeCodeVersionId GetVersionId() const;
    PCODE GetNativeCode() const;
    BOOL SetNativeCodeInterlocked(PCODE pCode, PCODE pExpected);
    ReJITID GetILVersionId() const;
    ILCodeVersion GetILCodeVersion() const;
//#ifdef FEATURE_TIERED_COMPILATION
//    TieredCompilationCodeConfiguration* GetTieredCompilationConfig();
//    const TieredCompilationCodeConfiguration* GetTieredCompilationConfig() const;
//#endif


private:
    //union
    //{
    PCODE m_pNativeCode;
    PTR_MethodDesc m_pMethodDesc;
    //};

    //union
    //{
    PTR_NativeCodeVersionNode m_pNextILVersionSibling;
    ReJITID m_parentId;
    //};

    NativeCodeVersionId m_id;
//#ifdef FEATURE_TIERED_COMPILATION
//    TieredCompilationCodeConfiguration m_tieredCompilationConfig;
//#endif
};

class NativeCodeVersion
{
    friend MethodDescVersioningState;

public:
    NativeCodeVersion();
    NativeCodeVersion(const NativeCodeVersion & rhs);
    NativeCodeVersion(PTR_NativeCodeVersionNode pVersionNode);
    NativeCodeVersion(PTR_MethodDesc pMethod);
    BOOL IsNull() const;
    PTR_MethodDesc GetMethodDesc() const;
    NativeCodeVersionId GetVersionId() const;
    BOOL IsDefaultVersion() const;
    PCODE GetNativeCode() const;
    BOOL SetNativeCodeInterlocked(PCODE pCode, PCODE pExpected = NULL);
    ILCodeVersion GetILCodeVersion() const;
//#ifdef FEATURE_TIERED_COMPILATION
//    TieredCompilationCodeConfiguration* GetTieredCompilationConfig();
//    const TieredCompilationCodeConfiguration* GetTieredCompilationConfig() const;
//#endif

    bool operator==(const NativeCodeVersion & rhs) const;
    bool operator!=(const NativeCodeVersion & rhs) const;

#ifdef DACCESS_COMPILE
    // The DAC is privy to the backing node abstraction
    PTR_NativeCodeVersionNode AsNode() const;
#endif

private:

#ifndef DACCESS_COMPILE
    PTR_NativeCodeVersionNode AsNode() const;
    PTR_NativeCodeVersionNode AsNode();
#endif

    enum StorageKind
    {
        Unknown,
        Explicit,
        Synthetic
    };

    StorageKind m_storageKind;
    union
    {
        PTR_NativeCodeVersionNode m_pVersionNode;
        struct SyntheticStorage
        {
            MethodDesc* m_pMethodDesc;
            //TieredCompilationCodeConfiguration m_tieredCompilationConfig;
        } m_synthetic;
    };
};

class NativeCodeVersionNodeHashTraits : public DefaultSHashTraits<PTR_NativeCodeVersionNode>
{
public:
    typedef typename DefaultSHashTraits<PTR_NativeCodeVersionNode>::element_t element_t;
    typedef typename DefaultSHashTraits<PTR_NativeCodeVersionNode>::count_t count_t;

    typedef const PTR_MethodDesc key_t;

    static key_t GetKey(element_t e)
    {
        LIMITED_METHOD_CONTRACT;
        return e->GetMethodDesc();
    }
    static BOOL Equals(key_t k1, key_t k2)
    {
        LIMITED_METHOD_CONTRACT;
        return k1 == k2;
    }
    static count_t Hash(key_t k)
    {
        LIMITED_METHOD_CONTRACT;
        return (count_t)(size_t)dac_cast<TADDR>(k);
    }
    static const element_t Null() { LIMITED_METHOD_CONTRACT; return element_t(); }
    static bool IsNull(const element_t &e) { LIMITED_METHOD_CONTRACT; return e == NULL; }
};

typedef SHash<NativeCodeVersionNodeHashTraits> NativeCodeVersionNodeHash;

class NativeCodeVersionCollection
{
    friend class NativeCodeVersionIterator;
public:
    NativeCodeVersionCollection();
    NativeCodeVersionIterator Begin();
    NativeCodeVersionIterator End();

private:
};

class NativeCodeVersionIterator : public Enumerator<const NativeCodeVersion, NativeCodeVersionIterator>
{
    friend class Enumerator<const NativeCodeVersion, NativeCodeVersionIterator>;

public:
    NativeCodeVersionIterator(NativeCodeVersionCollection* pCollection);
    CHECK Check() const { CHECK_OK; }

protected:
    const NativeCodeVersion & Get() const;
    void First();
    void Next();
    bool Equal(const NativeCodeVersionIterator &i) const;

    CHECK DoCheck() const { CHECK_OK; }

private:
    NativeCodeVersion m_cur;
};



class ILCodeVersion
{
public:
    ILCodeVersion();
    ILCodeVersion(const ILCodeVersion & ilCodeVersion);
    ILCodeVersion(PTR_ILCodeVersionNode pILCodeVersionNode);
    bool operator==(const ILCodeVersion & rhs) const;
    BOOL IsNull() const;
    PTR_Module GetModule();
    mdMethodDef GetMethodDef();
    ReJITID GetVersionId();
    NativeCodeVersionCollection GetNativeCodeVersions(PTR_MethodDesc pClosedMethodDesc);
    NativeCodeVersion GetActiveNativeCodeVersion(PTR_MethodDesc pClosedMethodDesc);

    PTR_COR_ILMETHOD GetIL() const;
    DWORD GetJitFlags() const;
    const InstrumentedILOffsetMapping* GetInstrumentedILMap() const;
#ifndef DACCESS_COMPILE
    void SetIL(COR_ILMETHOD* pIL);
    void SetJitFlags(DWORD flags);
    void SetInstrumentedILMap(SIZE_T cMap, COR_IL_MAP * rgMap);
    HRESULT AddNativeCodeVersion(MethodDesc* pClosedMethodDesc, NativeCodeVersion* pNativeCodeVersion);
    void LinkNativeCodeNode(NativeCodeVersionNode* pNativeCodeVersionNode);
#endif

    enum RejitFlags
    {
        // The profiler has requested a ReJit, so we've allocated stuff, but we haven't
        // called back to the profiler to get any info or indicate that the ReJit has
        // started. (This Info can be 'reused' for a new ReJit if the
        // profiler calls RequestRejit again before we transition to the next state.)
        kStateRequested = 0x00000000,

        // The CLR has initiated the call to the profiler's GetReJITParameters() callback
        // but it hasn't completed yet. At this point we have to assume the profiler has
        // commited to a specific IL body, even if the CLR doesn't know what it is yet.
        // If the profiler calls RequestRejit we need to allocate a new ILCodeVersion
        // and call GetReJITParameters() again.
        kStateGettingReJITParameters = 0x00000001,

        // We have asked the profiler about this method via ICorProfilerFunctionControl,
        // and have thus stored the IL and codegen flags the profiler specified. Can only
        // transition to kStateReverted from this state.
        kStateActive = 0x00000002,

        // The methoddef has been reverted, but not freed yet. It (or its instantiations
        // for generics) *MAY* still be active on the stack someplace or have outstanding
        // memory references.
        kStateReverted = 0x00000003,


        kStateMask = 0x0000000F,
    };

    RejitFlags GetRejitState() const;
    void SetRejitState(RejitFlags newState);

#ifdef DACCESS_COMPILE
    // The DAC is privy to the backing node abstraction
    PTR_ILCodeVersionNode AsNode() const;
#endif

private:

#ifndef DACCESS_COMPILE
    PTR_ILCodeVersionNode AsNode();
    PTR_ILCodeVersionNode AsNode() const;
#endif

    enum StorageKind
    {
        Unknown,
        Explicit,
        Synthetic
    };

    StorageKind m_storageKind;
    //union
    //{
        PTR_ILCodeVersionNode m_pVersionNode;
    //    struct SyntheticStorage
    //    {
    //        PTR_Module m_pModule;
    //        mdMethodDef m_methodDef;
    //    } m_synthetic;
    //};
};

class ILCodeVersionNode
{
public:
    ILCodeVersionNode();
#ifndef DACCESS_COMPILE
    ILCodeVersionNode(Module* pModule, mdMethodDef methodDef, ReJITID id);
#endif
    PTR_Module GetModule();
    mdMethodDef GetMethodDef();
    ReJITID GetVersionId();
    PTR_NativeCodeVersionNode GetActiveNativeCodeVersion(PTR_MethodDesc pClosedMethodDesc);
    PTR_COR_ILMETHOD GetIL() const;
    DWORD GetJitFlags() const;
    const InstrumentedILOffsetMapping* GetInstrumentedILMap() const;
    ILCodeVersion::RejitFlags GetRejitState() const;
#ifndef DACCESS_COMPILE
    void SetIL(COR_ILMETHOD* pIL);
    void SetJitFlags(DWORD flags);
    void SetInstrumentedILMap(SIZE_T cMap, COR_IL_MAP * rgMap);
    void SetRejitState(ILCodeVersion::RejitFlags newState);
    void LinkNativeCodeNode(NativeCodeVersionNode* pNativeCodeVersionNode);
#endif

    struct Key
    {
    public:
        Key();
        Key(PTR_Module pModule, mdMethodDef methodDef);
        size_t Hash() const;
        bool operator==(const Key & rhs) const;
    private:
        PTR_Module m_pModule;
        mdMethodDef m_methodDef;
    };

    Key GetKey() const;

private:
    PTR_Module m_pModule;
    mdMethodDef m_methodDef;
    ReJITID m_rejitId;
    PTR_NativeCodeVersionNode m_pFirstChild;
    ILCodeVersion::RejitFlags m_rejitState;
    PTR_COR_ILMETHOD m_pIL;
    DWORD m_jitFlags;
    InstrumentedILOffsetMapping m_instrumentedILMap;
};

class ILCodeVersionNodeHashTraits : public DefaultSHashTraits<PTR_ILCodeVersionNode>
{
public:
    typedef typename DefaultSHashTraits<PTR_ILCodeVersionNode>::element_t element_t;
    typedef typename DefaultSHashTraits<PTR_ILCodeVersionNode>::count_t count_t;

    typedef ILCodeVersionNode::Key key_t;

    static key_t GetKey(element_t e)
    {
        LIMITED_METHOD_CONTRACT;
        return e->GetKey();
    }
    static BOOL Equals(key_t k1, key_t k2)
    {
        LIMITED_METHOD_CONTRACT;
        return k1 == k2;
    }
    static count_t Hash(key_t k)
    {
        LIMITED_METHOD_CONTRACT;
        return (count_t)k.Hash();
    }
    static const element_t Null() { LIMITED_METHOD_CONTRACT; return element_t(); }
    static bool IsNull(const element_t &e) { LIMITED_METHOD_CONTRACT; return e == NULL; }
};

typedef SHash<ILCodeVersionNodeHashTraits> ILCodeVersionNodeHash;

class ILCodeVersionCollection
{
    friend class ILCodeVersionIterator;

public:
    ILCodeVersionCollection(PTR_Module pModule, mdMethodDef methodDef);
    ILCodeVersionIterator Begin();
    ILCodeVersionIterator End();

private:
    PTR_Module m_pModule;
    mdMethodDef m_methodDef;
};

class ILCodeVersionIterator : public Enumerator<const ILCodeVersion, ILCodeVersionIterator>
{
    friend class Enumerator<const ILCodeVersion, ILCodeVersionIterator>;

public:
    ILCodeVersionIterator();
    ILCodeVersionIterator(const ILCodeVersionIterator & iter);
    ILCodeVersionIterator(ILCodeVersionNodeHash::KeyIterator tableStartIter, ILCodeVersionNodeHash::KeyIterator tableEndIter);
    CHECK Check() const { CHECK_OK; }

protected:
    const ILCodeVersion & Get() const;
    void First();
    void Next();
    bool Equal(const ILCodeVersionIterator &i) const;

    CHECK DoCheck() const { CHECK_OK; }

private:
    ILCodeVersion m_cur;

    //TODO: storing all the versions in a table doesn't seem that efficient
    //We've got a density factor of 0.75 so we only get 3 versions for every 4
    //pointers
    //What about one ILVersioningState in a table then a linked list from there?
    ILCodeVersionNodeHash::KeyIterator m_tableCurIter;
    ILCodeVersionNodeHash::KeyIterator m_tableEndIter;
};

class MethodDescVersioningState
{
public:
    // The size of the code used to jump stamp the prolog
    static const size_t JumpStubSize =
#if defined(_X86_) || defined(_AMD64_)
        5;
#else
#error "Need to define size of rejit jump-stamp for this platform"
        1;
#endif

    MethodDescVersioningState(PTR_MethodDesc pMethodDesc);
    PTR_MethodDesc GetMethodDesc() const;
    NativeCodeVersionId AllocateVersionId();

#ifndef DACCESS_COMPILE
    HRESULT UpdateJumpTarget(BOOL fEESuspended, PCODE pRejittedCode);
    HRESULT UndoJumpStampNativeCode(BOOL fEESuspended);
    HRESULT JumpStampNativeCode(PCODE pCode = NULL);
#endif

    enum JumpStampFlags
    {
        // There is no jump stamp in place on this method (Either because
        // there is no code at all, or there is code that hasn't been
        // overwritten with a jump)
        JumpStampNone = 0x0,

        // The method code has the jump stamp written in, and it points to the Prestub
        JumpStampToPrestub = 0x1,

        // The method code has the jump stamp written in, and it points to the currently
        // active code version
        JumpStampToActiveVersion = 0x2,
    };

    JumpStampFlags GetJumpStampState();
    void SetJumpStampState(JumpStampFlags newState);

private:
    INDEBUG(BOOL CodeIsSaved();)
#ifndef DACCESS_COMPILE
    HRESULT UpdateJumpStampHelper(BYTE* pbCode, INT64 i64OldValue, INT64 i64NewValue, BOOL fContentionPossible);
#endif
    PTR_MethodDesc m_pMethodDesc;
    BYTE m_flags;
    NativeCodeVersionId m_nextId;

    // The originally JITted code that was overwritten with the jmp stamp.
    BYTE m_rgSavedCode[JumpStubSize];
};

class MethodDescVersioningStateHashTraits : public NoRemoveSHashTraits<DefaultSHashTraits<PTR_MethodDescVersioningState>>
{
public:
    typedef typename DefaultSHashTraits<PTR_MethodDescVersioningState>::element_t element_t;
    typedef typename DefaultSHashTraits<PTR_MethodDescVersioningState>::count_t count_t;

    typedef const PTR_MethodDesc key_t;

    static key_t GetKey(element_t e)
    {
        LIMITED_METHOD_CONTRACT;
        return e->GetMethodDesc();
    }
    static BOOL Equals(key_t k1, key_t k2)
    {
        LIMITED_METHOD_CONTRACT;
        return k1 == k2;
    }
    static count_t Hash(key_t k)
    {
        LIMITED_METHOD_CONTRACT;
        return (count_t)(size_t)dac_cast<TADDR>(k);
    }

    static const element_t Null() { LIMITED_METHOD_CONTRACT; return element_t(); }
    static bool IsNull(const element_t &e) { LIMITED_METHOD_CONTRACT; return e == NULL; }
};

typedef SHash<MethodDescVersioningStateHashTraits> MethodDescVersioningStateHash;


class CodeVersionManager
{
    friend class ILCodeVersion;

public:
    CodeVersionManager();

    void PreInit(BOOL fSharedDomain);

    class TableLockHolder : public CrstHolder
    {
    public:
        TableLockHolder(CodeVersionManager * pCodeVersionManager);
    };
    //Using the holder is preferable, but in some cases the holder can't be used
#ifndef DACCESS_COMPILE
    void EnterLock();
    void LeaveLock();
#endif
    //only intended for debug assertions
    BOOL LockOwnedByCurrentThread();

#ifndef DACCESS_COMPILE
    HRESULT AddILCodeVersion(Module* pModule, mdMethodDef methodDef, ReJITID rejitId, ILCodeVersion* pILCodeVersion);
    HRESULT AddNativeCodeVersion(ILCodeVersion ilCodeVersion, MethodDesc* pClosedMethodDesc, NativeCodeVersion* pNativeCodeVersion);
#endif

    DWORD GetNonDefaultILVersionCount();
    ILCodeVersionCollection GetILCodeVersions(PTR_MethodDesc pMethod);
    ILCodeVersionCollection GetILCodeVersions(PTR_Module pModule, mdMethodDef methodDef);
    ILCodeVersionIterator GetILCodeVersionIterator(PTR_Module pModule, mdMethodDef methodDef);
    ILCodeVersion GetActiveILCodeVersion(PTR_MethodDesc pMethod);
    ILCodeVersion GetActiveILCodeVersion(PTR_Module pModule, mdMethodDef methodDef);
    ILCodeVersion GetILCodeVersion(PTR_MethodDesc pMethod, ReJITID rejitId);

    NativeCodeVersion GetNativeCodeVersion(PTR_MethodDesc pMethod, PCODE codeStartAddress);

    struct JumpStampBatch
    {
        JumpStampBatch(CodeVersionManager * pCodeVersionManager) : undoMethods(), preStubMethods()
        {
            LIMITED_METHOD_CONTRACT;
            this->pCodeVersionManager = pCodeVersionManager;
        }

        CodeVersionManager* pCodeVersionManager;
        CDynArray<NativeCodeVersion> undoMethods;
        CDynArray<NativeCodeVersion> preStubMethods;
    };

    class JumpStampBatchTraits : public DefaultSHashTraits<JumpStampBatch *>
    {
    public:

        // explicitly declare local typedefs for these traits types, otherwise 
        // the compiler may get confused
        typedef DefaultSHashTraits<JumpStampBatch *> PARENT;
        typedef PARENT::element_t element_t;
        typedef PARENT::count_t count_t;

        typedef CodeVersionManager * key_t;

        static key_t GetKey(const element_t &e)
        {
            return e->pCodeVersionManager;
        }

        static BOOL Equals(key_t k1, key_t k2)
        {
            return (k1 == k2);
        }

        static count_t Hash(key_t k)
        {
            return (count_t)k;
        }

        static bool IsNull(const element_t &e)
        {
            return (e == NULL);
        }
    };

    struct CodePublishError
    {
        Module* pModule;
        mdMethodDef methodDef;
        MethodDesc* pMethodDesc;
        HRESULT hrStatus;
    };

#ifndef DACCESS_COMPILE
    HRESULT BatchUpdateJumpStamps(CDynArray<NativeCodeVersion> * pUndoMethods,
        CDynArray<NativeCodeVersion> * pPreStubMethods,
        CDynArray<CodePublishError> * pErrors);
#endif

#ifndef DACCESS_COMPILE
    static HRESULT AddCodePublishError(Module* pModule, mdMethodDef methodDef, MethodDesc* pMD, HRESULT hrStatus, CDynArray<CodePublishError> * pErrors);
    static HRESULT AddCodePublishError(NativeCodeVersion nativeCodeVersion, HRESULT hrStatus, CDynArray<CodePublishError> * pErrors);
#endif

    PTR_MethodDescVersioningState GetMethodVersioningState(PTR_MethodDesc pMethod);
#ifndef DACCESS_COMPILE
    HRESULT GetOrCreateMethodVersioningState(MethodDesc* pMethod, MethodDescVersioningState** ppMethodDescVersioningState);
#endif

#ifndef DACCESS_COMPILE
    static void OnAppDomainExit(AppDomain* pAppDomain);
#endif

private:

    BOOL PublishMethodCodeIfNeeded(NativeCodeVersionNode* pNativeCodeVersion);

    //Module,MethodDef -> ILCodeVersions
    ILCodeVersionNodeHash m_ilCodeVersionNodeMap;
    //closed MethodDesc -> NativeCodeVersions
    NativeCodeVersionNodeHash m_nativeCodeVersionNodeMap;
    //closed MethodDesc -> MethodDescVersioningState
    MethodDescVersioningStateHash m_methodDescVersioningStateMap;

    CrstExplicitInit m_crstTable;
};

class CodeVersionManagerLockHolder
{
public:
    CodeVersionManagerLockHolder(CodeVersionManager* pCodeVersionManager) {}
};

#endif // FEATURE_CODE_VERSIONING

#endif // CODE_VERSION_H
