// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.
// ===========================================================================
// File: TieredCompilation.h
//
// ===========================================================================


#ifndef TIERED_COMPILATION_H
#define TIERED_COMPILATION_H


class HotMethodProfilingInfo
{
public:
    class Key
    {
    public:
        Key();
        Key(PTR_MethodDesc pMethod, ReJITID codeVersion);
        size_t Hash() const;
        bool operator==(const Key & rhs) const;
    private:
        PTR_MethodDesc m_pMethod;
        ReJITID m_codeVersion;
    };

    enum Stage
    {
        CompilingInstrumentedCodeBody,
        CollectingProfile,
        CompilingOptimizedCodeBody,
        OptimizedCodeBodyRunning
    };

    HotMethodProfilingInfo(PTR_MethodDesc pMethod, ReJITID codeVersion);
    Key GetKey() const;

private:
    Key m_key;
    Stage m_stage;
};

typedef DPTR(class HotMethodProfilingInfo) PTR_HotMethodProfilingInfo;

class HotMethodProfilingInfoHashTraits : public NoRemoveSHashTraits<DefaultSHashTraits<PTR_HotMethodProfilingInfo>>
{
public:
    typedef typename DefaultSHashTraits<PTR_HotMethodProfilingInfo>::element_t element_t;
    typedef typename DefaultSHashTraits<PTR_HotMethodProfilingInfo>::count_t count_t;

    typedef HotMethodProfilingInfo::Key key_t;

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

    static element_t Null() { LIMITED_METHOD_CONTRACT; return dac_cast<PTR_HotMethodProfilingInfo>(nullptr); }
    static bool IsNull(const element_t &e) { LIMITED_METHOD_CONTRACT; return e == NULL; }
};

typedef SHash<HotMethodProfilingInfoHashTraits> HotMethodProfilingInfoHash;



// TieredCompilationManager determines which methods should be recompiled and
// how they should be recompiled to best optimize the running code. It then
// handles logistics of getting new code created and installed.
class TieredCompilationManager
{
#ifdef FEATURE_TIERED_COMPILATION

public:
#if defined(DACCESS_COMPILE) || defined(CROSSGEN_COMPILE)
    TieredCompilationManager() {}
#else
    TieredCompilationManager();
#endif

    void Init(ADID appDomainId);

#endif // FEATURE_TIERED_COMPILATION

public:
    static NativeCodeVersion::OptimizationTier GetInitialOptimizationTier(PTR_MethodDesc pMethodDesc);
    static NativeCodeVersion::InstrumentationLevel GetInitialInstrumentationLevel(PTR_MethodDesc pMethodDesc);

#ifdef FEATURE_TIERED_COMPILATION

public:
    static bool RequiresCallCounting(MethodDesc* pMethodDesc);
    void OnMethodCalled(MethodDesc* pMethodDesc, DWORD currentCallCount, BOOL* shouldStopCountingCallsRef, BOOL* wasPromotedToTier1Ref);
    void OnMethodCallCountingStoppedWithoutTier1Promotion(MethodDesc* pMethodDesc);

    static WORD GetCallRateThreshold(NativeCodeVersion::OptimizationTier tier);
    void OnMethodCallRateThresholdExceeded(MethodDesc* pMethodDesc, NativeCodeVersion::OptimizationTier tier);
    void AsyncPromoteMethod(MethodDesc* pMethodDesc, NativeCodeVersion::OptimizationTier tier);
    void Shutdown();
    static CORJIT_FLAGS GetJitFlags(NativeCodeVersion nativeCodeVersion);

private:
    enum TimerId
    {
        TieringDelayTimer = 1,
        InstrumentationMonitorTimer = 2
    };

    bool IsTieringDelayActive();
    bool TryInitiateTieringDelay();
    static void WINAPI TimerCallback(PVOID parameter, BOOLEAN timerFired);
    static void TimerCallbackInAppDomain(LPVOID parameter);
    void TieringDelayTimerCallbackWorker();
    static void ResumeCountingCalls(MethodDesc* pMethodDesc);

    void AsyncCompileAndActivate(MethodDesc* pMethodDesc,
        NativeCodeVersion::OptimizationTier tier,
        NativeCodeVersion::InstrumentationLevel instrumentationLevel);
    bool TryAsyncOptimizeMethods();
    static DWORD StaticOptimizeMethodsCallback(void* args);
    void OptimizeMethodsCallback();
    void OptimizeMethods();
    void OptimizeMethod(NativeCodeVersion nativeCodeVersion);
    NativeCodeVersion GetNextMethodToOptimize();
    BOOL CompileCodeVersion(NativeCodeVersion nativeCodeVersion);
    void ActivateCodeVersion(NativeCodeVersion nativeCodeVersion);

    bool IncrementWorkerThreadCountIfNeeded();
    void DecrementWorkerThreadCount();
#ifdef _DEBUG
    DWORD DebugGetWorkerThreadCount();
#endif

    void BeginMonitorInstrumentation(NativeCodeVersion nativeCodeVersion);
    void InstrumentationMonitorCallbackWorker();

    Crst m_lock;
    SList<SListElem<NativeCodeVersion>> m_methodsToOptimize;
    ADID m_domainId;
    BOOL m_isAppDomainShuttingDown;
    DWORD m_countOptimizationThreadsRunning;
    DWORD m_callCountOptimizationThreshhold;
    DWORD m_optimizationQuantumMs;
    SArray<MethodDesc*>* m_methodsPendingCountingForTier1;
    HANDLE m_tieringDelayTimerHandle;
    bool m_tier1CallCountingCandidateMethodRecentlyRecorded;


    SArray<NativeCodeVersion> m_instrumentationInProgressCodeBodies;
    DWORD m_instrumentationMonitorPeriodMs;
    DWORD m_minMonitorBBCount;
    HANDLE m_instrumentationTimerHandle;

    HotMethodProfilingInfoHash m_hotMethodProfilingInfoHash;

#endif // FEATURE_TIERED_COMPILATION
};

#endif // TIERED_COMPILATION_H
