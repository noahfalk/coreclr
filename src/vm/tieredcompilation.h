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

    void Init(ADID appDomainId);

public:
    void OnMethodCalled(MethodDesc* pMethodDesc, DWORD currentCallCount, BOOL* shouldStopCountingCallsRef, BOOL* wasPromotedToTier1Ref);
    void OnMethodCallCountingStoppedWithoutTier1Promotion(MethodDesc* pMethodDesc);
private:
    BOOL TryInitiateTier1CountingDelay();
public:
    void AsyncPromoteMethodToTier1(MethodDesc* pMethodDesc);
    void Shutdown();
    static CORJIT_FLAGS GetJitFlags(NativeCodeVersion nativeCodeVersion);

private:

    static VOID WINAPI Tier1DelayTimerCallback(PVOID parameter, BOOLEAN timerFired);
    static void Tier1DelayTimerCallbackInAppDomain(LPVOID parameter);
    void Tier1DelayTimerCallbackWorker();
    BOOL PauseTieredCompilationWork(BOOL *pWasAlreadyPaused);
    void ResumeTieredCompilationWork(BOOL doSynchronousMethodOptimization);
    BOOL IsPaused();
    static void ResumeCountingCalls(MethodDesc* pMethodDesc);

    VOID EnrollOptimizeThreadIfNeeded();
    BOOL AsyncOptimizeMethods();
    static DWORD StaticOptimizeMethodsCallback(void* args);
    void OptimizeMethodsCallback();
    void OptimizeMethods();
    void OptimizeMethod(NativeCodeVersion nativeCodeVersion);
    NativeCodeVersion GetNextMethodToOptimize();
    BOOL CompileCodeVersion(NativeCodeVersion nativeCodeVersion);
    void ActivateCodeVersion(NativeCodeVersion nativeCodeVersion);

    BOOL IncrementWorkerThreadCountIfNeeded();
    void IncrementWorkerThreadCount();
    void DecrementWorkerThreadCount();

    SpinLock m_lock;
    SList<SListElem<NativeCodeVersion>> m_methodsToOptimize;
    ADID m_domainId;
    BOOL m_isAppDomainShuttingDown;
    DWORD m_countOptimizationThreadsRunning;
    DWORD m_callCountOptimizationThreshhold;
    DWORD m_optimizationQuantumMs;
    SArray<MethodDesc*>* m_methodsPendingCountingForTier1;
    HANDLE m_tier1CountingDelayTimerHandle;
    bool m_tier1CallCountingCandidateMethodRecentlyRecorded;

    CLREvent m_asyncWorkDoneEvent;
};

#endif // FEATURE_TIERED_COMPILATION

#endif // TIERED_COMPILATION_H
