# Code Versioning Profiler Breaking Changes #

The runtime changes done as part of the code versioning feature will cause some (hopefully minor) breaking changes to be visible via the profiler API. My goal is to advertise these coming changes and solicit feedback about what will be easiest for profiler writers to absorb. Currently this feature is only under development in the .Net Core version of the runtime. If you solely support a profiler on full .Net Framework this change doesn't affect you.

## Underlying issue ##

Code versioning, and in particular its use for tiered compilation means that the runtime will be invoking the JIT more than it did in the past. Historically there was a 1:1 relationship between a FunctionID and a single JITCompilationFinished event. For those profilers using ReJIT APIs the 1:1 relationship was between a (FunctionID,ReJITID) pair and a single [Re]JITCompilationFinished event. [Re]JitCompilationStarted events were usually 1:1 as well, but not guaranteed in all cases. Tiered compilation will break these invariants by invoking the JIT potentially multiple times per method.

## Likely ways you will see behavior change ##

1. There will be more JITCompilation events, and potentially more ReJIT compilation events than there were before.
  - **Question:** When an IL code body that has been instrumented by the profiler using RequestReJIT is jitted multiple times by the runtime, precedence suggests the first JIT compilation should emit ReJITCompilationStarted/ReJITCompilationFinished. However when the same instrumented IL code body is jitted again by the runtime should the runtime emit JITCompilation events, ReJITCompilation events, or something else? The easiest/fastest performing option within the runtime is to generate ReJITCompilation events.
  - **Question 2:** Same thing, but for methods that have not been instrumented by RequestReJIT. When these methods are jitted multiple times the easiest/most performant runtime option would be to emit more JITCompilation events, but would some other event type be preferred?

2. These JIT events may originate from a background worker thread that may different from the thread which ulimately runs the jitted code. 

3. Calls to ICorProfiler4::GetCodeInfo3 will only return information about the first jitted code body for a given FunctionID,rejitID pair. If profiler scenarios require an enumeration of every jitted code body a new API will need to be created and consumed.

4. A by-product of the current runtime changes is that IL supplied during the JITCompilationStarted callback is now verified the same as if you had provided it during ModuleLoadFinished. This verification might be beneficial, but if there was a useful reason to continue not verifying it I could restore the original no-verification behavior.


## Obscure ways you might see behavior change ##

1. If tiered compilation fails to publish an updated code body on a method that has already been instrumented with RequestReJIT and jitted, the profiler could receive a rejit error callback reporting the problem. This should only occur on OOM or process memory corruption.

2. The timing of ReJITCompilationFinished has been adjusted to be slightly earlier (after the new code body is generated, but prior to updating the previous jitted code to modify control flow). This raises a slim possibility for a ReJIT error to be reported after ReJITCompilationFinished in the case of OOM or process memory corruption.


There are likely some other variations of the changed behavior I haven't thought of yet, but if further testing, code review, or discussion brings it to the surface I'll add it here. Feel free to get in touch on github (@noahfalk), or if you have anything you want to discuss in private you can email me at noahfalk AT microsoft.com