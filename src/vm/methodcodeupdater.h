// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.
// ===========================================================================
// File: MethodCodeUpdater.h
//
// ===========================================================================


#ifndef METHOD_CODE_UPDATER_H
#define METHOD_CODE_UPDATER_H

//Identifier for a particular version of native code in the scope of all native code generated
//for a particular MethodDesc in the process lifetime.
typedef USHORT NativeCodeVersionId;
struct NativeCodeVersionDesc;

struct ILCodeVersionHandle
{
public:
    NativeCodeVersionDesc* GetActiveNativeCodeVersion(MethodDesc* pClosedMethodDesc) { return NULL; }
    NativeCodeVersionDesc* AddNativeCodeVersion(MethodDesc* pClosedMethodDesc) { return NULL; }
    COR_ILMETHOD* GetILHeader() { return NULL; }
    CORJIT_FLAGS GetJitFlags() { return CORJIT_FLAGS(); }
    InstrumentedILOffsetMapping* GetILOffsetMapping() { return NULL; }

private:
    ReJITID m_rejitId;
};

struct NativeCodeVersionHandle
{
    BOOL IsNull();
    MethodDesc* GetMethod();

private:
    MethodDesc* m_pMethod;
    NativeCodeVersionId m_versionId;
};

class NativeCodeVersionDescIterator : Enumerator<NativeCodeVersionDesc, NativeCodeVersionDescIterator>
{
    friend class Enumerator<NativeCodeVersionDesc, NativeCodeVersionDescIterator>;

public:
    NativeCodeVersionDescIterator();
    
protected:
    const NativeCodeVersionDesc &Get() const { return NativeCodeVersionDesc(); }
    void First() { return; }
    void Next() { return; }
    bool Equal(const NativeCodeVersionDescIterator &i) const { return TRUE; }
    CHECK DoCheck() const { CHECK_OK; }
};

struct ILCodeVersionDesc
{
public:
    Module* GetModule() { return NULL; }
    mdMethodDef GetMethodDef() { return 0;  }
    MethodDesc* GetMethodDescIfLoaded() { return NULL; }
    ReJITID GetVersionId() { return 0; }
    NativeCodeVersionDescIterator GetActiveNativeCodeVersions() { return NativeCodeVersionDescIterator(); }
    NativeCodeVersionDescIterator GetAllNativeCodeVersions() { return NativeCodeVersionDescIterator(); }
    NativeCodeVersionDesc* GetActiveNativeCodeVersion(MethodDesc* pClosedMethodDesc) { return NULL; }
    NativeCodeVersionDesc* AddNativeCodeVersion(MethodDesc* pClosedMethodDesc) { return NULL; }
    void RemoveNativeCodeVersion(NativeCodeVersionDesc* pNativeCodeVersionDesc) {}
    COR_ILMETHOD* GetILHeader() { return NULL; }
    CORJIT_FLAGS GetJitFlags() { return CORJIT_FLAGS(); }
    InstrumentedILOffsetMapping* GetILOffsetMapping() { return NULL; }

private:
};

struct NativeCodeVersionDesc
{
public:
    NativeCodeVersionHandle GetHandle() { return NativeCodeVersionHandle(); }
    MethodDesc* GetMethod() { return NULL; }
    PCODE GetNativeCode() { return NULL; }
    BOOL IsPointingToPrestub() { return FALSE; }
    void SetNativeCode(PCODE pCode) { return; }
    ILCodeVersionDesc* GetILVersion() { return NULL; }
#ifdef FEATURE_TIERED_COMPILATION
    TieredCompilationCodeConfiguration* GetTieredCompilationConfig() { return &m_tieredCompilationConfig; }
#endif

private:
    union
    {
        PCODE* m_pNativeCode;
        MethodDesc* m_pMethodDesc;
    };

    union
    {
        NativeCodeVersionDesc* m_pNextNativeCode;
        ILCodeVersionDesc* m_pParent;
    };

    enum NativeCodeVersionDescFlags : BYTE
    {
        // This flag is set if m_pNativeCode/m_pMethodDesc union should be treated as holding
        // the native code pointer, otherwise it holds the MethodDesc pointer
        NativeCodeSet = 0x01,

        // This flag is set if this version of the code is not the active one for a given
        // (il version + generic instantiation). 
        Inactive = 0x02,

        // This flag is set if the m_pNextNativeCode/m_pParent union should be treated as holding
        // a m_pParent field
        ParentSet = 0x04
    };

    NativeCodeVersionId m_id;
    NativeCodeVersionDescFlags m_flags; 
#ifdef FEATURE_TIERED_COMPILATION
    TieredCompilationCodeConfiguration m_tieredCompilationConfig;
#endif
};


class MethodCodeUpdater
{
public:
#if defined(DACCESS_COMPILE) || defined(CROSSGEN_COMPILE)
    MethodCodeUpdater() {}
#else
    MethodCodeUpdater() {}
#endif

    ILCodeVersionHandle GetDefaultILVersionHandle(MethodDesc* pMethod) { return ILCodeVersionHandle(); }
    ILCodeVersionHandle GetDefaultILVersionHandle(Module* pModule, mdMethodDef methodDef) { return ILCodeVersionHandle(); }
    
    void EnterLock() {}
    void ReleaseLock() {}

    ILCodeVersionHandle GetActiveILVersionHandle(MethodDesc* pMethod) { return ILCodeVersionHandle(); }
    ILCodeVersionDesc* GetActiveILVersion(MethodDesc* pMethod) { return NULL; }
    ILCodeVersionDesc* AddILVersion(Module* pModule, mdMethodDef methodDef) { return NULL; }
private:

};

#endif // METHOD_CODE_UPDATER
