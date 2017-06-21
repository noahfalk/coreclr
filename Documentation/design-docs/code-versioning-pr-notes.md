# Code Versioning PR Notes #

This is information that I hope is useful to reviewers while feature work is in progress, but at the end I plan to delete it. If you think there is information here of useful historical significance let me know and I'll preserve it, perhaps as an appendix to the feature spec.

## Progress so far ##

I re-created a rough commit history as a set of incremental steps that should be reasonably understandable in isolation. Hopefully it will be easier to conclude the result is correct when viewing it as a series of deltas from current state, though even some of the individual deltas are still sizable.

I did a variety of testing intermixed with fixes and cleanup near the end. Its possible I've regressed things during the cleanup, but there will be at least one more round of final testing to detect that if so. At the time a tested it results were:



- CoreCLR test run with tiered jitting OFF: 0 failures
- CoreCLR test run with tiered jitting ON: 6 failures (all jit specific tests that appear to be testing inlining on what are now non-inlined initial code bodies)
- Manual smoke test interpreter + tiered compilation: working
- Manual smoke test MulticoreJit, tiered jitting OFF: working
- Profiler and debugger tests, tiered jitting OFF: 0 failures (not counting baseline failures)
- Profiler tests, tiered jitting ON, test fixes applied to mitigate product breaking changes: a few failures (I was re-running chunks of tests against a changing product baseline so I don't have an exact number for all tests on the identical product build yet)
- Initial performance analysis, tiered jitting OFF: At worst a small regression, but inconclusive without some further analysis/matching pogo data/larger sample sizes. I ran music store app on a .Net Core official daily build vs ret build on my machine without updated Pogo data. Official build vs my build both jit 18259+-3 methods (source of variance uninvestigated)
	- Average total time in Prestub:              1141ms(baseline) vs. 1182ms(my build)
		- Largest difference in time was inside UnsafeJitFunction, but I don't believe my change should have had any significant impact within UnsafeJitFunction.
	- Average total time in Prestub excluding UnsafeJitFunction/ETW events: 14ms(baseline) vs. 16ms(my build)
		- All the heavy lifting in my changes should have shown up here if they were going to.

- Performance analysis, tiered jitting ON: Not yet investigated


### Commit Notes ###


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

After commit #3 I did a little better job breaking things up and putting notes in the commit history itself so I won't reiterate them here.

## Work still to come ##

Assuming no major course correction, this is where I anticipate things will go (not necessarily in this order):

1. Functional testing mentioned above with tiered jitting OFF needs to be iterated again before checkin to catch issues introduced during final cleanup and PR/CI updates that are inevitably coming.

2. More perf investigation to ensure there is no regression with tiered jitting OFF

3. Update feature doc to be more concrete/explanatory based on work done.

4. Get feedback from profiler authors on the prefered form of breaking changes when tiered jitting is ON / mitigations for those changes.

5. Stress testing

6. Perf investigation to assess where we are with tiered jitting ON

7. Diagnostics interactions with ETW, SOS, and the debugger need some updates with tiered compilation enabled.

8. Fix some remaining known issues when tiered jitting is ON


## Checkin bar ##

I don't anticipate completing all the above work before checkin. I think the reasonable bar is that:

1. No regressions on profiler rejit behavior, or any other supported runtime feature (CI passes + additional manual testing where CI has insufficient coverage) assuming that tiered compilation is still in its default OFF configuration.
2. Code reviewers are happy with the chunk of code being submitted + overall direction
3. The changes don't interfere with anyone else doing work around the same area.

We can discuss exactly how much of the above changes that will be in each checkin, but tentatively I'm thinking next checkin would include only (1) and (2) above.