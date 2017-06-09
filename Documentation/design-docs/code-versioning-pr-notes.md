# Code Versioning PR Notes #

This is information that I hope is useful to reviewers while feature work is in progress, but at the end I plan to delete it. If you think there is information here of useful historical significance let me know and I'll preserve it, perhaps as an appendix to the feature spec.

## Progress so far ##

The current PR is incomplete so I wouldn't bother pointing out bug level issues or obvious gaps in functionality as long as you don't forsee significant issues in filling them. Hopefully it gives an idea where I am going, primarily for issue #2 in the design doc "There is no mechanism to record the aggregate configuration settings which generated a code body". Please do point out any issues you anticipate with the broader design. I'm trying to balance enough concrete info so that you can see some usage patterns and relation to other code while not doing so much work that I've wasted major effort if we need to make a course correction. I'm going to continue coding under the optimistic assumption no major course correction will be needed.

Because the change is going to be large in aggregate I'm attempting to structure as a set of incremental steps that should be reasonably understandable in isolation. I think it will be easier to conclude the result is correct when viewing it as a series of deltas from current state.

The first commit is a relatively uninteresting minor refactor and the 2nd commit just lays down some boilerplate infrastructure for a code versioning feature. The 3rd commit is where it starts getting interesting...

You should be able to see a pretty clear parallel between the new data structure in codeversion.h and previous datastructure in rejit.h. Roughly SharedReJitInfo -> ILCodeVersion[Node] and ReJitInfo -> NativeCodeVersion[Node]. However there are more changes than merely the rename.

1. The ReJit feature was roughly sliced in half. All state that Rejit maintained about the versions has now been shifted into the CodeVersion tree and ReJitManager is now a static class. There is also a partial migration of the JumpStamping code that begins building the solution for issue #3 "There is no unified mechanism for switching runtime dispatch between different versions of the code"

2. The Rejit feature used ReJitInfo in two different ways, one as a representation of a concrete native code body and the other as a representation of future rejitting work that might be needed for a given IL body. This dual usage doesn't appear to fit well in a more generalized tree (its not clear it was even that great just for ReJit, but it works). That second form corresponded to any ReJitInfos that used IL/Metadata token as their key or that used open generic methodDescs as the key. Typically the usage pattern of those would be to enumerate them and then immediately indirect to the corresponding SharedReJitInfo for each. Rather than do that I converted the code to directly enumerate ILCodeVersion instances. If necessary the ILCodeVersion can then be lowered to a set generic instantiations/native code bodies that all share that IL version. A few examples of this type of change:

	- ReJitManager::BindILVersion instead of previous ReJitManager::MarkForReJitHelper
	- Top of ReJitManager::DoJumpStampIfNecessary
	- DacDbiInterfaceImpl::GetActiveRejitILCodeVersionNode (you can see the previous lack of canonicalization force two separate searches into the old ReJitInfo based data structure)

    ILCodeVersion is always canonically indexed by Module/metadata token pair. Any search by MethodDesc is merely shorthand for searching by MethodDesc->GetModule()/MethodDesc->GetMemberDef()

3. NativeCodeVersion/ILCodeVersion aren't allocated on the loader heap as ReJitInfo/SharedReJitInfo were. This is intentional to prepare for their eventual collectibility. The loader heap doesn't appear to have any good support for fine-grained alloc/delete and its not clear there would be any significant benefit to adding that functionality vs. using standard new/delete.
4. The synchronization that ReJit did has largely been shifted/replicated on CodeVersionManager. The general pattern is if you want to read/write/add/remove anything in CodeVersionManager/ILCodeVersion/NativeCodeVersion you need to own the corresponding CodeVersionManager lock. As the code updating/code generation paths are fleshed out more I certainly think refinements are on the table, but if possible my goal is still to handle everything with one of: the big lock, narrow usage of interlocked, immutable write once data

5. NativeCodeVersion doesn't track the state of the JumpStamp within each instance as ReJitInfo used to do. If you consider the set of ReJitInfo's that all map to the same MethodDesc instantiation, all but one of them is always in the state m\_dwInternalFlags = kJumpNone. Likewise all but one of them has a zero-filled m\_rgSavedCode. Conceptually there is only method entry point that can be patched so only one set of these fields is needed per entrypoint. I created MethodDescVersioningState to directly track that data rather than cloning it in every NativeCodeVersion. The choice of which ReJitInfo had the flag  set different than none is interesting data - it represented which version was currently active. However we can efficiently mark it with a single bit per NativeCodeVersion or single pointer in MethodDescVersioningState, not the 12 bytes per ReJitInfo it used before.

## Work still to come ##

Assuming no major course correction, this is where I anticipate things will go (not necessarily in this order):

1. The NativeCodeVersion/ILCodeVersion only support the explicitly backed node case. For the case where these structures represent the default version there will be no node and the code needs to bifurcate and fetch the data from other runtime data structures. So far rejit doesn't really need that because it mostly avoids manipulating a default version, but a consolidated prestub codepath will certainly be referencing default code versions.

2. The special case jitting paths in ReJitManager::DoReJitIfNecessary will get merged with the general case jitting paths.

3. The low level interaction ReJit currently has with jump-stamping will get raised to an abstraction level where it just indicates to the code version manager which IL version of the code should be active.

4. The code version manager will automatically use FixupPrecode updates instead of JumpStamps whenever the method being versioned can support a fixup precode. With tiered compilation on, all methods jitted at runtime will support a fixup precode.

5. Tiered compilation will get embedded into this scheme with new NativeCodeVersion nodes being generated when the tiered compilation manager wants to shift from tier0 (initial default code) to tier1 (more optimized code). Whenever rejit creates a new IL version the tier will reset back to 0 for that IL versioning branch, then upgrade to tier1 after a given number of invocations.

6. Using the hash tables to enumerate NativeCodeVersions for the same MethodDesc or ILCodeVersions for the same Module/metadata token doesn't appear space efficient. I'm thinking to add a linked list of all NativeCodeVersions for a MethodDesc hanging off MethodDescVersioningState, and then introduce a similar ILVersioningState with linked list of ILCodeVersions. 

7. A variety of functional and stress testing needs to be done. 

8. Diagnostics interactions with ETW, SOS, and the debugger need some updates with tiered compilation enabled.


## Checkin bar ##

I don't anticipate completing all the above work before checkin. I think the reasonable bar is that:

1. No regressions on profiler rejit behavior, or any other supported runtime feature (CI passes + additional manual testing where CI has insufficient coverage) assuming that tiered compilation is still in its default OFF configuration.
2. Code reviewers are happy with the chunk of code being submitted + overall direction
3. The changes don't interfere with anyone else doing work around the same area.

We can discuss exactly how much of the above changes that will be in each checkin, but tentatively I'm thinking next checkin might include a good portion, perhaps (1), (2), (3), (5), and some of (7)