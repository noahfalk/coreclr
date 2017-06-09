# Code Versioning #

The current runtime prototype for tiered compilation doesn't work properly when trying to use profiler
rejit at the same time. Although this may seem initially seem like a corner case intersection, diagnostic monitoring profilers are increasingly becoming standard operating procedure with rejit usage increasing fast in that domain. Likewise the goal is for tiered compilation to be used as an always on (or nearly always on) feature. Rejit can also be viewed as a canonical example of a broader problem that the runtime doesn't have a foundational concept of code versioning and lifetime management. Rejit has one conception of it, the tiered jitting prototype adds another, Edit and Continue adds a 3rd, LCG a 4th, the IL interpreter a 5th, Samsung's in progress code-pitching a 6th, and so on. Using any of those features is mutually exclusive with every other at method level granularity.

I don't aim to produce in one effort a grand unified implementation, I do want to achieve two things:

1. A roadmap that allows all those features to interoperate if we made the effort to do so in the future.
2. Implement that design far enough so that Rejit + Tiered Compilation interoperate now.

For now tiered compilation can reasonably co-exist with the other features by disabling it only on the methods that are eligible for one of the other techniques.



### Code Versions and the Build Pipeline ###

Code versions are an abstract concept not currently embodied precisely in the runtime implementation. Conceptually they include two things:

1. A native code implementation with corresponding side tables for EH, GC, debug data, etc.
2. Various identifiers and configuration information that describe how this code version was generated and how it differs from other versions.

Given a single IL method def token loaded with a .Net process we may have N different 'code versions'.
To create a particular code version there is a code generation pipeline operating inside the runtime which gives several different components an opportunity to modify the final result. Again not defined in a very structured way within our code, the current effective pipeline looks somewhat like this:

1. IL code loaded from an assembly
2. IL code can be replaced at module load time by a profiler
3. IL code can be replaced by Edit And Continue
4. IL code can be replaced by profiler rejit
5. Jit settings can be modified by debugger
6. Jit settings can be modified by the profiler
7. Jit settings can be modified by the tiered compilation manager
8. Final native code is produced by the Jit

A complete accounting of a code version includes the configuration choices we made at every stage and then the final result of jitting the code. If any stage changes its configuration, the new combination of settings and the resulting code represents a new code version.


### Current barriers to interoperability ###

Among the runtime features that have notions of code versioning or code lifetime management, no single change is going to make them all work together, but we can do some centralized work to provide a standard for interoperation. The issues to resolve are: 

1. Runtime features don't agree on a common in-process build pipeline. ReJit is probably the biggest offender but the general issue that some feature will duplicate a portion of the build/configuration logic that handles the versioning needs of that one feature as a special case, but excludes the requirements from other features.

2. There is no mechanism to record the aggregate configuration settings which will generate a code body. This is useful for diagnostic purposes, to memoize the resulting code, and to track the configuration of the active code-body (the one that should be run when the method is next called). When all the configuration is static it is easy enough to infer the configuration used, and when one configuration stage is dynamic we usually add a dictionary somewhere which tracks N configurations of one stage to N corresponding code bodies. However once two stages vary it is no longer sufficient to have feature specific dictionaries that assume all other configuration stages are held constant.

3. There is no unified mechanism for switching runtime dispatch between different versions of the code. ReJit uses a JumpStamp, EnC and tiered compilation use FixupPrecode, and IL interpreter uses a custom stub. 

4. There is no unified mechanism for lifetime management of different code versions. Most of the runtime assumes code has AppDomain lifetime, LCG has a customized garbage collection scheme, and Samsung's code pitching uses a different garbage collection scheme.

### Proposed Solutions ###

**1. Runtime features don't agree on a common in-process build pipeline.**

We can define that the code path through PreStub, PreStubWorker, DoPrestub, JitMethodWorker, UnsafeJitFunction, etc. is the standard path. This code path isn't quite as flexible as needs to be to accomodate all the configuration stage options so I propose we do a little refactoring. Currently that path uses a collection of flags to control behavior, but the number of ifdefs and inlined special cases makes the code hard to follow and some features deliberately avoided attempting to integrate because the perceived complexity of correctly running in that code path seemed overly risky. I believe refactoring using a configuration object argument that has a few polymorphic callbacks will provide a good balance between performance, code maintainability and extensibility. Given typical jit times on the order of 100k+ cycles, the overhead of a few extra virtual calls during setup seems neglible. I do however want to avoid any significant performance costs to make the callbacks so both the caller and the callee should keep the current EH model, not do GC mode or app domain transitions, use simple shared data types, not add defensive synchronization locks, etc. 
  ReJIT then needs to be refactored to use this path rather than its own custom re-implementation of MakeJitWorker.

**2. There is no mechanism to record the aggregate configuration settings which generated a code body.**

We need to define a common data structure that represents a code version, and then a manager that is capable of storing and enumerating them. A tree of versions appears to be the best conceptual fit with
both pre-existing code and the kinds of operations new potential build pipeline stages want to do.
The leaves of the tree represent distinct code versions, and each level in the tree represents configuration that corresponds to one stage in the build pipeline.

A simple hypothetical tree that grows left to right (apologies there is probably a better way to format this):

Level 0 IL assembly + method Def -> Level 1 IL instrumentation from rejit  -> Level 2 Generic instantiation

(List<T\>.Add)   ->    List<T>.Add original IL -> List<int\>.Add

...................................................-> List<char\>.Add

...................... ->    List<T>.Add new IL -> List<int\>.Add

...................................................-> List<char\>.Add


I propose we do build this tree-like API but with three levels of branched configuration:

1. ReJIT modifications (IL instrumentation, jit flags, IL mapping tables)
2. Generic instantiation
3. Jitted code flags for tiered compilation

In the future we could add other stages as we want to allow for more dynamically varying configuration options. For example a future candidate is Edit and Continue, which if added would likely become the new level 1 shifting all the other levels right.

Each level of the tree will be represented with an object model node (excepting some pragmatic considerations discussed below). The object model should support basic tree operations such as navigation to parent, enumeration of children, and adding/removing children. Each node also allows read/write access to relevant configuration data/versioning identifiers/code information, though most of the data should become immutable after it is written initially.

Other pragmatic considerations that will cause our tree to diverge from an idealized form:

1. The majority of code will likely only have one version, and this configuration data is already stored in other runtime data structures. Creating additional memory allocations to store the data a 2nd time would not be efficient. To avoid this the main trick up my sleave is to add one layer of indirection. Users of the version tree interact with copy-by-value structures that represent logical tree nodes. Internally those structures either have a pointer to a heap allocated physical tree node, or have some identity data indicating what logical node they are supposed to represent. In the case of physically backed nodes all the method calls on the structure are just proxied to the backing store and implemented as nieve field lookups there. In the case of the unbacked nodes, the method calls on the structure would lookup or compute the configuration information on demand from disparate CLR data structures that store it. For example the API to get the native code from a leaf code version might look like this:

```
	PCODE NativeCodeVersion::GetNativeCode() const
	{
	    if (m_storageKind == StorageKind::Explicit)
	    {
	        return m_pNativeCodeVersionStorage->GetNativeCode();
	    }
	    else
	    {
	        return GetMethodDesc()->GetNativeCode();
	    }
	}
```

2. Its not clear we'll have a strong need to create an object model representation of versions at the generic instance level of the versioning tree. If it winds up being useful we can certainly do so, but if the difference to other components is minimal as I suspect it will be then we may get away with simply identifying a generic instance version at level 2 as the pair <level 1 version, closed  MethodDesc>. This allows for various parts of the API surface to be ellided. For example instead of having:

```
    GenericInstanceCodeVersionCollection ILCodeVersion::GetGenericVersions();
```
```
    NativeCodeVersionCollection GenericInstanceCodeVersion::GetTieredCompilationVersions();
```

We get:

    NativeCodeVersionCollection ILCodeVersion::GetNativeCodeVersions(MethodDesc* pClosedMethodDesc);

3. Although I'm not planning to have any form of garbage collection in the initial version, I think we should assume that eventually we will collect code versions that aren't in active use. The tree might become temporarily very branchy, but over time it would decay back down to 1 branch per generic instantiation. Ultimately non-generic methods might become a single path from root to a leaf, and generic methods look like palm trees with long skinny trunks and then one level with greater branching. In these cases rather than fret too much about maximal sharing of common configuration data between similar children in a branchy tree we would instead worry about compactly encoding the single trunk and the leaves after the generic instantiation branch.

 The result looks similar to the existing tree in rejit.h. The rejit tree has a first level branching node SharedRejitInfo that represents a unique IL body, and then each of those has child ReJitInfo nodes that are 1:1 with native code. I am renaming the types ILCodeVersion and NativeCodeVersion in an attempt to break the direct associations rejit and instead refer to the kind of configuration present at each stage. However if we add more stages that makes this terminology ambiguous (for example adding Edit and Continue IL modification as a stage) then we might need to adjust naming again to better distinguish.
 
 One aspect of the rejit tree I don't plan to bring forward is the use of ReJitInfo nodes as placeholders for an unspecified set of yet to be loaded generic instantiations. In many cases I find it actually complicates the the rejit code to use ReJitInfo that way and it appears that SharedReJitInfo on its own (or the ILVersion equivalent in the new data structure) is a perfectly viable representation of that concept.

For more detailed view of some proposed API, see src\vm\codeversion.h

**3. There is no unified mechanism for switching runtime dispatch between different versions of the code.**

I propose we create a general purpose concept of swapping to a new method body implementation inside the implementation of MethodDesc and the to be created Code Version Manager. The resulting API might look something like:

    CodeVersionManager::SetActiveCodeVersion(NativeCodeVersion someCodeVersion);

Then the next time the method that this version pertains to is called, we guarantee it will run this version of the code. Internally this involves:

a) If the method is already dispatching to certain code, modify the indirections not to point at that old version

b) If the new version already has generated code, modify the indirections to point at the new code instead.

c) If the new version does not already have generated code, record that this is the version we want to run eventually, and modify the various indirections to point to the PreStub. When the PreStub runs later it will generate the code for the version we indicated and fixup the indirections.

In terms of which indirections we use, I think we need to support two of our pre-existing techniques for now: FixupPrecode and JumpStamp. FixupPrestub has much better perf characteristics in some cases, but JumpStamp is the only one that works on precompiled images. JumpStamp is effectively required to prevent ReJit from regressing, but without a FixupPrestub option I don't think tiered compilation performance will be reasonable. The caller of the API should be ignorant of what technique we use, which means we are free to revisit these choices in the future.

**4. There is no unified mechanism for lifetime management of different code versions.**

This one isn't an issue for ReJit+tiered compilation so I have given it the least attention, but it will be concern in the future for being able to do any sort of garbage collection over code versions. I'd like to ensure we have a plausible path forward without investing significant time trying to solve any details now. My thoughts:

Only versionable code is in scope for lifetime management. Versionable code is code that is represented in the code version manager, and that can swapped in/out using the SetActiveCodeVersion() API proposed above. This greatly simplifies the problem because most of the runtime doesn't ever hold a pointer directly to this code, it holds a pointer to a jmp instruction and the target of that jmp is the actual code. At any time the jmp can be re-directed back to the PreStub, the code collected, and pointers to the jmp remain valid. Only a tiny subset of the runtime will be allowed to manipulate the actual jitted code pointers, and this subset can use a basic reference counting scheme to ensure that code versions aren't collected while they are in use. Code pointers could also exist in registers or on the stack and we probably won't be able to use explicit reference counting to handle those cases. For references on the stack and in registers I'm going to handwave and say the stackwalker will identify them when we suspend and sweep for a code garbage collection. There is certainly more detail to getting that exactly right, but we've already got LCG so I'm assuming the prior art makes it a reasonably tractable problem.

I'm not making any attempt to define policy about how often we sweep the code versions, what the scope of those sweeps are, or what code/version information gets deleted when we detect it is unreferenced. All of those would be performance policy decisions for another day. Samsung's jit pitching PR in progress now does make one stab at defining such policies.
