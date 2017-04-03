// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.
// ===========================================================================
// File: TieredCompilation.h
//
// ===========================================================================


#ifndef TIERED_COMPILATION_H
#define TIERED_COMPILATION_H




#ifdef FEATURE_TIERED_COMPILATION

// TieredCompilationManager determines which methods should be recompiled and
// how they should be recompiled to best optimize the running code. It then
// handles logistics of getting new code created and installed.
class TieredCompilationManager
{
public:
#if defined(DACCESS_COMPILE) || defined(CROSSGEN_COMPILE)
    TieredCompilationManager() {}
#else
    TieredCompilationManager();
#endif

    void Init(ADID appDomainId, MethodCodeUpdater* pMethodCodeUpdater);
    BOOL OnMethodCalled(MethodDesc* pMethodDesc, DWORD currentCallCount);
    void OnAppDomainShutdown();

private:

    static DWORD StaticOptimizeMethodsCallback(void* args);
    void OptimizeMethodsCallback();
    void OptimizeMethod(MethodDesc* pMethod);
    MethodDesc* GetNextMethodToOptimize();
    PCODE CompileMethod(MethodDesc* pMethod);
    void InstallMethodCode(MethodDesc* pMethod, PCODE pCode);

    SpinLock m_lock;
    SList<SListElem<NativeCodeVersionHandle>> m_methodsToOptimize;
    ADID m_domainId;
    BOOL m_isAppDomainShuttingDown;
    DWORD m_countOptimizationThreadsRunning;
    DWORD m_callCountOptimizationThreshhold;
    DWORD m_optimizationQuantumMs;
    MethodCodeUpdater* m_pMethodCodeUpdater;
};


struct TieredCompilationCodeConfiguration
{
public:
    enum OptimizationTier
    {
        Tier0 = 0,
        Tier1 = 1
    };

    OptimizationTier GetOptimizationTier() { return m_tier; }
    void SetOptimizationTier(OptimizationTier tier) { m_tier = tier; }
private:
    OptimizationTier m_tier;
};

#endif // FEATURE_TIERED_COMPILATION

#endif // TIERED_COMPILATION_H
