// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

#include "common.h"
#include "eventpipe.h"
#include "eventpipeconfiguration.h"
#include "eventpipebuffer.h"
#include "eventpipebuffermanager.h"
#include "eventpipefile.h"
#include "eventpipesession.h"

#ifdef FEATURE_PERFTRACING

void ReleaseEventPipeThreadRef(EventPipeThread *pThread)
{
    LIMITED_METHOD_CONTRACT;
    pThread->Release();
}

void AcquireEventPipeThreadRef(EventPipeThread *pThread)
{
    LIMITED_METHOD_CONTRACT;
    pThread->AddRef();
}

EVENTPIPE_THREAD_LOCAL EventPipeThreadHolder EventPipeThread::gCurrentEventPipeThreadHolder;

EventPipeThread::EventPipeThread() : m_writingEventInProgress(UINT64_MAX)
{
    CONTRACTL
    {
        THROWS;
        GC_NOTRIGGER;
        MODE_ANY;
    }
    CONTRACTL_END;

    m_lock.Init(LOCK_TYPE_DEFAULT);
    m_refCount = 0;

    m_pWriteBuffers = new EventPipeWriteBuffers();
    m_pBufferLists = new EventPipeBufferLists();
}

EventPipeThread::~EventPipeThread()
{
    LIMITED_METHOD_CONTRACT;
    _ASSERTE(AllValuesAreNull(*m_pWriteBuffers));
    _ASSERTE(AllValuesAreNull(*m_pBufferLists));

    delete m_pWriteBuffers;
    delete m_pBufferLists;
}

/*static */ EventPipeThread *EventPipeThread::Get()
{
    LIMITED_METHOD_CONTRACT;
    return gCurrentEventPipeThreadHolder;
}

/*static */ EventPipeThread *EventPipeThread::GetOrCreate()
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_ANY;
    }
    CONTRACTL_END;

    if (gCurrentEventPipeThreadHolder == nullptr)
    {
        EX_TRY
        {
            gCurrentEventPipeThreadHolder = new EventPipeThread();
        }
        EX_CATCH
        {
        }
        EX_END_CATCH(SwallowAllExceptions);
    }
    return gCurrentEventPipeThreadHolder;
}

/*static */ void EventPipeThread::Set(EventPipeThread *pThread)
{
    LIMITED_METHOD_CONTRACT;
    gCurrentEventPipeThreadHolder = pThread;
}

void EventPipeThread::AddRef()
{
    LIMITED_METHOD_CONTRACT;
    FastInterlockIncrement(&m_refCount);
}

void EventPipeThread::Release()
{
    LIMITED_METHOD_CONTRACT;
    if (FastInterlockDecrement(&m_refCount) == 0)
    {
        // https://isocpp.org/wiki/faq/freestore-mgmt#delete-this
        // As long as you're careful, it's okay (not evil) for an object to commit suicide (delete this).
        delete this;
    }
}

SpinLock *EventPipeThread::GetLock()
{
    LIMITED_METHOD_CONTRACT;
    return &m_lock;
}

EventPipeBuffer *EventPipeThread::GetWriteBuffer(EventPipeBufferManager *pBufferManager)
{
    LIMITED_METHOD_CONTRACT;
    _ASSERTE(m_lock.OwnedByCurrentThread());
    _ASSERTE(pBufferManager != nullptr);

    EventPipeBuffer *pWriteBuffer = nullptr;
    m_pWriteBuffers->Lookup(pBufferManager, &pWriteBuffer);
    _ASSERTE((pWriteBuffer == nullptr) || (pWriteBuffer->GetVolatileState() == EventPipeBufferState::WRITABLE));
    return pWriteBuffer;
}

void EventPipeThread::SetWriteBuffer(EventPipeBufferManager *pBufferManager, EventPipeBuffer *pNewBuffer)
{
    LIMITED_METHOD_CONTRACT;
    _ASSERTE(m_lock.OwnedByCurrentThread());
    _ASSERTE(pBufferManager != nullptr);
    _ASSERTE((pNewBuffer == nullptr) || pNewBuffer->GetVolatileState() == EventPipeBufferState::WRITABLE);

    EventPipeBuffer *pWriteBuffer = nullptr;
    if (m_pWriteBuffers->Lookup(pBufferManager, &pWriteBuffer))
    {
        _ASSERTE((pWriteBuffer == nullptr) || (pWriteBuffer->GetVolatileState() == EventPipeBufferState::WRITABLE));
        if (pWriteBuffer != nullptr)
            pWriteBuffer->ConvertToReadOnly();
        m_pWriteBuffers->Remove(pBufferManager);
    }

    EX_TRY
    {
        m_pWriteBuffers->Add(pBufferManager, pNewBuffer);
    }
    EX_CATCH
    {
    }
    EX_END_CATCH(SwallowAllExceptions);
}

EventPipeBufferList *EventPipeThread::GetBufferList(EventPipeBufferManager *pBufferManager)
{
    LIMITED_METHOD_CONTRACT;
    _ASSERTE(pBufferManager != nullptr);
    _ASSERTE(pBufferManager->IsLockOwnedByCurrentThread());

    EventPipeBufferList *pBufferList = nullptr;
    m_pBufferLists->Lookup(pBufferManager, &pBufferList);
    return pBufferList;
}

void EventPipeThread::SetBufferList(EventPipeBufferManager *pBufferManager, EventPipeBufferList *pNewBufferList)
{
    LIMITED_METHOD_CONTRACT;
    _ASSERTE(pBufferManager != nullptr);
    _ASSERTE(pBufferManager->IsLockOwnedByCurrentThread());

    EventPipeBufferList *pBufferList = nullptr;
    if (m_pBufferLists->Lookup(pBufferManager, &pBufferList))
        m_pBufferLists->Remove(pBufferManager);

    EX_TRY
    {
        m_pBufferLists->Add(pBufferManager, pNewBufferList);
    }
    EX_CATCH
    {
    }
    EX_END_CATCH(SwallowAllExceptions);
}

void EventPipeThread::Remove(EventPipeBufferManager *pBufferManager)
{
    CONTRACTL
    {
        THROWS;
        GC_NOTRIGGER;
        MODE_ANY;
        PRECONDITION(pBufferManager != nullptr);
    }
    CONTRACTL_END;

    if (pBufferManager == nullptr)
        return;

    EventPipeBufferList *pBufferList = nullptr;
    if (m_pBufferLists->Lookup(pBufferManager, &pBufferList))
        m_pBufferLists->Remove(pBufferManager);
}

EventPipeBufferManager::EventPipeBufferManager()
{
    CONTRACTL
    {
        THROWS;
        GC_NOTRIGGER;
        MODE_ANY;
    }
    CONTRACTL_END;

    m_pPerThreadBufferList = new SList<SListElem<EventPipeBufferList *>>();
    m_sizeOfAllBuffers = 0;
    m_lock.Init(LOCK_TYPE_DEFAULT);
    m_writeEventSuspending = FALSE;

#ifdef _DEBUG
    m_numBuffersAllocated = 0;
    m_numBuffersStolen = 0;
    m_numBuffersLeaked = 0;
    m_numEventsStored = 0;
    m_numEventsDropped = 0;
    m_numEventsWritten = 0;
#endif // _DEBUG

    m_pCurrentEvent = nullptr;
    m_pCurrentBuffer = nullptr;
    m_pCurrentBufferList = nullptr;
}

EventPipeBufferManager::~EventPipeBufferManager()
{
    CONTRACTL
    {
        NOTHROW;
        GC_TRIGGERS;
        MODE_ANY;
    }
    CONTRACTL_END;

    // setting this true should have no practical effect other than satisfying asserts at this point.
    m_writeEventSuspending = TRUE;
    DeAllocateBuffers();
}

#ifdef DEBUG
bool EventPipeBufferManager::IsLockOwnedByCurrentThread()
{
    return m_lock.OwnedByCurrentThread();
}
#endif

EventPipeBuffer *EventPipeBufferManager::AllocateBufferForThread(EventPipeSession &session, unsigned int requestSize, BOOL &writeSuspended)
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_ANY;
        PRECONDITION(requestSize > 0);
    }
    CONTRACTL_END;

    // Allocating a buffer requires us to take the lock.
    SpinLockHolder _slh(&m_lock);

    // if we are deallocating then give up, see the comments in SuspendWriteEvents() for why this is important.
    if (m_writeEventSuspending.Load())
    {
        writeSuspended = TRUE;
        return NULL;
    }

    // Determine if the requesting thread has at least one buffer.
    // If not, we guarantee that each thread gets at least one (to prevent thrashing when the circular buffer size is too small).
    bool allocateNewBuffer = false;

    EventPipeThread *const pEventPipeThread = EventPipeThread::GetOrCreate();
    if (pEventPipeThread == NULL)
        return NULL;

    EventPipeBufferList *pThreadBufferList = pEventPipeThread->GetBufferList(this);
    if (pThreadBufferList == NULL)
    {
        pThreadBufferList = new (nothrow) EventPipeBufferList(this, pEventPipeThread);

        if (pThreadBufferList == NULL)
        {
            return NULL;
        }

        SListElem<EventPipeBufferList *> *pElem = new (nothrow) SListElem<EventPipeBufferList *>(pThreadBufferList);
        if (pElem == NULL)
        {
            return NULL;
        }

        m_pPerThreadBufferList->InsertTail(pElem);
        pEventPipeThread->SetBufferList(this, pThreadBufferList);
        allocateNewBuffer = true;
    }

    // Determine if policy allows us to allocate another buffer
    if (!allocateNewBuffer)
    {
        if (m_sizeOfAllBuffers < session.GetCircularBufferSize())
        {
            // We don't worry about the fact that a new buffer could put us over the circular buffer size.
            // This is OK, and we won't do it again if we actually go over.
            allocateNewBuffer = true;
        }
    }
    EventPipeBuffer *pNewBuffer = NULL;
    if (allocateNewBuffer)
    {
        // Pick a buffer size by multiplying the base buffer size by the number of buffers already allocated for this thread.
        unsigned int sizeMultiplier = pThreadBufferList->GetCount() + 1;

        // Pick the base buffer size based.  Debug builds have a smaller size to stress the allocate/steal path more.
        unsigned int baseBufferSize =
#ifdef _DEBUG
            30 * 1024; // 30K
#else
            100 * 1024; // 100K
#endif
        unsigned int bufferSize = baseBufferSize * sizeMultiplier;

        // Make sure that buffer size >= request size so that the buffer size does not
        // determine the max event size.
        if (bufferSize < requestSize)
        {
            bufferSize = requestSize;
        }

        // Don't allow the buffer size to exceed 1MB.
        const unsigned int maxBufferSize = 1024 * 1024;
        if (bufferSize > maxBufferSize)
        {
            bufferSize = maxBufferSize;
        }

        // EX_TRY is used here as opposed to new (nothrow) because
        // the constructor also allocates a private buffer, which
        // could throw, and cannot be easily checked
        EX_TRY
        {
            pNewBuffer = new EventPipeBuffer(bufferSize DEBUG_ARG(pEventPipeThread));
        }
        EX_CATCH
        {
            pNewBuffer = NULL;
        }
        EX_END_CATCH(SwallowAllExceptions);

        if (pNewBuffer == NULL)
        {
            return NULL;
        }

        m_sizeOfAllBuffers += bufferSize;
#ifdef _DEBUG
        m_numBuffersAllocated++;
#endif // _DEBUG
    }

    // Set the buffer on the thread.
    if (pNewBuffer != NULL)
    {
        pThreadBufferList->InsertTail(pNewBuffer);
        return pNewBuffer;
    }

    return NULL;
}

void EventPipeBufferManager::DeAllocateBuffer(EventPipeBuffer *pBuffer)
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_ANY;
    }
    CONTRACTL_END;

    if (pBuffer != NULL)
    {
        m_sizeOfAllBuffers -= pBuffer->GetSize();
        delete (pBuffer);
#ifdef _DEBUG
        m_numBuffersAllocated--;
#endif // _DEBUG
    }
}

bool EventPipeBufferManager::WriteEvent(Thread *pThread, EventPipeSession &session, EventPipeEvent &event, EventPipeEventPayload &payload, LPCGUID pActivityId, LPCGUID pRelatedActivityId, Thread *pEventThread, StackContents *pStack)
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_ANY;
        // The input thread must match the current thread because no lock is taken on the buffer.
        PRECONDITION(pThread == GetThread());
    }
    CONTRACTL_END;

    // Check to see an event thread was specified.  If not, then use the current thread.
    if (pEventThread == NULL)
    {
        pEventThread = pThread;
    }

    // Before we pick a buffer, make sure the event is enabled.
    if (!event.IsEnabled())
    {
        return false;
    }

    StackContents stackContents;
    if (pStack == NULL && event.NeedStack() && !session.RundownEnabled())
    {
        EventPipe::WalkManagedStackForCurrentThread(stackContents);
        pStack = &stackContents;
    }

    // See if the thread already has a buffer to try.
    bool allocNewBuffer = false;
    EventPipeBuffer *pBuffer = NULL;

    EventPipeThread *pEventPipeThread = EventPipeThread::Get();

    if (pEventPipeThread == NULL)
    {
        allocNewBuffer = true;
    }
    else
    {
        SpinLockHolder _slh(pEventPipeThread->GetLock());
        pBuffer = pEventPipeThread->GetWriteBuffer(this);

        if (pBuffer == NULL)
        {
            allocNewBuffer = true;
        }
        else
        {
            // Attempt to write the event to the buffer.  If this fails, we should allocate a new buffer.
            allocNewBuffer = !pBuffer->WriteEvent(pEventThread, session, event, payload, pActivityId, pRelatedActivityId, pStack);
        }
    }

    // Check to see if we need to allocate a new buffer, and if so, do it here.
    if (allocNewBuffer)
    {
        // We previously switched to preemptive mode here, however, this is not safe and can cause deadlocks.
        // When a GC is started, and background threads are created (for the first BGC), a thread creation event is fired.
        // When control gets here the buffer is allocated, but then the thread hangs waiting for the GC to complete
        // (it was marked as started before creating threads) so that it can switch back to cooperative mode.
        // However, the GC is waiting on this call to return so that it can make forward progress.  Thus it is not safe
        // to switch to preemptive mode here.

        unsigned int requestSize = sizeof(EventPipeEventInstance) + payload.GetSize();
        BOOL writeSuspended = FALSE;
        pBuffer = AllocateBufferForThread(session, requestSize, writeSuspended);
        if (pBuffer == NULL)
        {
            // We treat this as the WriteEvent() call occurring after this session stopped listening for events, effectively the
            // same as if event.IsEnabled() test above returned false.
            if (writeSuspended)
                return false;
        }
        else
        {
            pEventPipeThread = EventPipeThread::Get();
            _ASSERTE(pEventPipeThread != NULL);
            {
                SpinLockHolder _slh(pEventPipeThread->GetLock());
                if (m_writeEventSuspending.Load())
                {
                    // After leaving the manager's lock in AllocateBufferForThread some other thread decided to suspend writes.
                    // We need to immediately return the buffer we just took without storing it or writing to it.
                    // SuspendWriteEvent() is spinning waiting for this buffer to be relinquished.
                    pBuffer->ConvertToReadOnly();

                    // We treat this as the WriteEvent() call occurring after this session stopped listening for events, effectively the
                    // same as if event.IsEnabled() returned false.
                    return false;
                }
                else
                {
                    pEventPipeThread->SetWriteBuffer(this, pBuffer);

                    // Try to write the event after we allocated a buffer.
                    // This is the first time if the thread had no buffers before the call to this function.
                    // This is the second time if this thread did have one or more buffers, but they were full.
                    allocNewBuffer = !pBuffer->WriteEvent(pEventThread, session, event, payload, pActivityId, pRelatedActivityId, pStack);
                }
            }
        }
    }

#ifdef _DEBUG
    if (!allocNewBuffer)
    {
        InterlockedIncrement(&m_numEventsStored);
    }
    else
    {
        InterlockedIncrement(&m_numEventsDropped);
    }
#endif // _DEBUG
    return !allocNewBuffer;
}

void EventPipeBufferManager::WriteAllBuffersToFile(EventPipeFile *pFile, LARGE_INTEGER stopTimeStamp)
{
    CONTRACTL
    {
        THROWS;
        GC_TRIGGERS;
        MODE_PREEMPTIVE;
        PRECONDITION(pFile != nullptr);
        PRECONDITION(GetCurrentEvent() == nullptr);
    }
    CONTRACTL_END;

    // Naively walk the circular buffer, writing the event stream in timestamp order.
    bool eventsWritten = false;
    MoveNextEventAnyThread(stopTimeStamp);
    while (GetCurrentEvent() != nullptr)
    {
        pFile->WriteEvent(*GetCurrentEvent());
        MoveNextEventAnyThread(stopTimeStamp);
        eventsWritten = true;
    }
    if (eventsWritten)
    {
        pFile->Flush();
    }
}

EventPipeEventInstance* EventPipeBufferManager::GetNextEvent()
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_ANY;
        PRECONDITION(!EventPipe::IsLockOwnedByCurrentThread());
    }
    CONTRACTL_END;

    // PERF: This may be too aggressive? If this method is being called frequently enough to keep pace with the
    // writing threads we could be in a state of high lock contention and lots of churning buffers. Each writer
    // would take several locks, allocate a new buffer, write one event into it, then the reader would take the
    // lock, convert the buffer to read-only and read the single event out of it. Allowing more events to accumulate
    // in the buffers before converting between writable and read-only amortizes a lot of the overhead. One way 
    // to achieve that would be picking a stopTimeStamp that was Xms in the past. This would let Xms of events
    // to accumulate in the write buffer before we converted it and forced the writer to allocate another. Other more
    // sophisticated approaches would probably build a low overhead synchronization mechanism to read and write the 
    // buffer at the same time.
    LARGE_INTEGER stopTimeStamp;
    QueryPerformanceCounter(&stopTimeStamp);
    MoveNextEventAnyThread(stopTimeStamp);
    return GetCurrentEvent();
}

EventPipeEventInstance* EventPipeBufferManager::GetCurrentEvent()
{
    LIMITED_METHOD_CONTRACT;
    return m_pCurrentEvent;
}

void EventPipeBufferManager::MoveNextEventAnyThread(LARGE_INTEGER stopTimeStamp)
{
    CONTRACTL
    {
        THROWS;
        GC_NOTRIGGER;
        MODE_PREEMPTIVE;
        PRECONDITION(!m_lock.OwnedByCurrentThread());
    }
    CONTRACTL_END;

    if (m_pCurrentEvent != nullptr)
    {
        m_pCurrentBuffer->MoveNextReadEvent();
    }
    m_pCurrentEvent = nullptr;
    m_pCurrentBuffer = nullptr;
    m_pCurrentBufferList = nullptr;

    // We need to do this in two steps because we can't hold m_lock and EventPipeThread::m_lock
    // at the same time.

    // Step 1 - while holding m_lock get the oldest buffer from each thread
    CQuickArrayList<EventPipeBuffer *> bufferList;
    CQuickArrayList<EventPipeBufferList *> bufferListList;
    {
        SpinLockHolder _slh(&m_lock);
        SListElem<EventPipeBufferList *> *pElem = m_pPerThreadBufferList->GetHead();
        while (pElem != NULL)
        {
            EventPipeBufferList *pBufferList = pElem->GetValue();
            EventPipeBuffer *pBuffer = pBufferList->GetHead();
            if (pBuffer != nullptr &&
                pBuffer->GetCreationTimeStamp().QuadPart < stopTimeStamp.QuadPart)
            {
                bufferListList.Push(pBufferList);
                bufferList.Push(pBuffer);
            }
            pElem = m_pPerThreadBufferList->GetNext(pElem);
        }
    }

    // Step 2 - iterate the cached list to find the one with the oldest event. This may require
    // converting some of the buffers from writable to readable, and that in turn requires
    // taking the associated EventPipeThread::m_lock for thread that was writing to that buffer.
    LARGE_INTEGER curOldestTime = stopTimeStamp;
    for (size_t i = 0; i < bufferList.Size(); i++)
    {
        EventPipeBufferList *pBufferList = bufferListList[i];
        EventPipeBuffer *pHeadBuffer = bufferList[i];
        EventPipeBuffer *pBuffer = AdvanceToNonEmptyBuffer(pBufferList, pHeadBuffer, stopTimeStamp);
        if (pBuffer == nullptr)
        {
            // there weren't any non-empty buffers in that list prior to stopTimeStamp
            continue;
        }
        // Peek the next event out of the buffer.
        EventPipeEventInstance *pNext = pBuffer->GetCurrentReadEvent();
        if (pNext != NULL)
        {
            // If it's the oldest event we've seen, then save it.
            if (pNext->GetTimeStamp()->QuadPart < curOldestTime.QuadPart)
            {
                m_pCurrentEvent = pNext;
                m_pCurrentBuffer = pBuffer;
                m_pCurrentBufferList = pBufferList;
                curOldestTime = *(m_pCurrentEvent->GetTimeStamp());
            }
        }
    }
}

void EventPipeBufferManager::MoveNextEventSameThread(LARGE_INTEGER beforeTimeStamp)
{
    CONTRACTL
    {
        THROWS;
        GC_NOTRIGGER;
        MODE_PREEMPTIVE;
        PRECONDITION(m_pCurrentEvent != nullptr);
        PRECONDITION(m_pCurrentBuffer != nullptr);
        PRECONDITION(m_pCurrentBufferList != nullptr);
        PRECONDITION(!m_lock.OwnedByCurrentThread());
    }
    CONTRACTL_END;

    //advance past the current event
    m_pCurrentEvent = nullptr;
    m_pCurrentBuffer->MoveNextReadEvent();

    // Find the first buffer in the list, if any, which has an event in it
    m_pCurrentBuffer = AdvanceToNonEmptyBuffer(m_pCurrentBufferList, m_pCurrentBuffer, beforeTimeStamp);
    if (m_pCurrentBuffer == nullptr)
    {
        // no more buffers prior to stopTimeStamp
        _ASSERTE(m_pCurrentEvent == nullptr);
        _ASSERTE(m_pCurrentBuffer == nullptr);
        m_pCurrentBufferList = nullptr;
        return;
    }

    // get the event from that buffer
    EventPipeEventInstance* pNextEvent = m_pCurrentBuffer->GetCurrentReadEvent();
    LARGE_INTEGER nextTimeStamp = *pNextEvent->GetTimeStamp();
    if (nextTimeStamp.QuadPart >= beforeTimeStamp.QuadPart)
    {
        // event exists, but isn't early enough
        m_pCurrentEvent = nullptr;
        m_pCurrentBuffer = nullptr;
        m_pCurrentBufferList = nullptr;
    }
    else
    {
        // event is early enough, set the new cursor
        m_pCurrentEvent = pNextEvent;
        _ASSERTE(m_pCurrentBuffer != nullptr);
        _ASSERTE(m_pCurrentBufferList != nullptr);
    }
}

EventPipeBuffer* EventPipeBufferManager::AdvanceToNonEmptyBuffer(EventPipeBufferList* pBufferList,
    EventPipeBuffer* pBuffer,
    LARGE_INTEGER beforeTimeStamp)
{
    CONTRACTL
    {
        THROWS;
        GC_NOTRIGGER;
        MODE_PREEMPTIVE;
        PRECONDITION(!m_lock.OwnedByCurrentThread());
        PRECONDITION(pBufferList != nullptr);
        PRECONDITION(pBuffer != nullptr);
        PRECONDITION(pBufferList->GetHead() == pBuffer);
    }
    CONTRACTL_END;

    EventPipeBuffer* pCurrentBuffer = pBuffer;
    while (pCurrentBuffer->GetCurrentReadEvent() == nullptr)
    {
        {
            SpinLockHolder _slh(&m_lock);

            // delete the empty buffer
            EventPipeBuffer *pRemoved = pBufferList->GetAndRemoveHead();
            _ASSERTE(pCurrentBuffer == pRemoved);
            DeAllocateBuffer(pRemoved);

            // get the next buffer
            pCurrentBuffer = pBufferList->GetHead();
            if (pCurrentBuffer == nullptr ||
                pCurrentBuffer->GetCreationTimeStamp().QuadPart >= beforeTimeStamp.QuadPart)
            {
                // no more buffers in the list before this timestamp, we're done
                return nullptr;
            }
        }
        if (!TryConvertBufferToReadOnly(pCurrentBuffer))
        {
            // the writer thread hasn't yet stored this buffer into the m_pWriteBuffer
            // field (there is a small time window after allocation in this state).
            // This should be the only buffer remaining in the list and it has no
            // events written into it so we are done iterating.
            return nullptr;

        }
    }
    return pCurrentBuffer;
}

bool EventPipeBufferManager::TryConvertBufferToReadOnly(EventPipeBuffer* pNewReadBuffer)
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_PREEMPTIVE;
        PRECONDITION(pNewReadBuffer != nullptr);
        PRECONDITION(!m_lock.OwnedByCurrentThread());
    }
    CONTRACTL_END;

    // if already readable, nothing to do
    if (pNewReadBuffer->GetVolatileState() == EventPipeBufferState::READ_ONLY)
    {
        return true;
    }

    // if not yet readable, disable the thread from writing to it which causes
    // it to become readable
    {
        EventPipeThread* pThread = pNewReadBuffer->GetWriterThread();
        SpinLockHolder _slh(pThread->GetLock());
        if (pThread->GetWriteBuffer(this) == pNewReadBuffer)
        {
            pThread->SetWriteBuffer(this, nullptr);
            _ASSERTE(pNewReadBuffer->GetVolatileState() == EventPipeBufferState::READ_ONLY);
            return true;
        }
    }

    // It is possible that EventPipeBufferList::TryGetBuffer(...) returns a writable buffer
    // yet it is not returned as EventPipeThread::GetWriteBuffer(...). This is because 
    // EventPipeBufferManager::AllocateBufferForThread() insert the new writable buffer into 
    // the EventPipeBufferList first, and then it is added to the writable buffer hash table
    // by EventPipeThread::SetWriteBuffer() next. The two operations are not atomic so it is possible 
    // to observe this partial state.
    return pNewReadBuffer->GetVolatileState() == EventPipeBufferState::READ_ONLY;
}

void EventPipeBufferManager::SuspendWriteEvent(EventPipeSessionID sessionId)
{
    CONTRACTL
    {
        THROWS;
        GC_NOTRIGGER;
        MODE_ANY;
        PRECONDITION(EnsureConsistency());
        // All calls to this method must be synchronized by our caller
        PRECONDITION(EventPipe::IsLockOwnedByCurrentThread());
    }
    CONTRACTL_END;

    CQuickArrayList<EventPipeThread *> threadList;
    {
        SpinLockHolder _slh(&m_lock);
        m_writeEventSuspending.Store(TRUE);
        // From this point until m_writeEventSuspending is reset to FALSE it is impossible
        // for new EventPipeBufferLists to be added to the m_pPerThreadBufferList. The only
        // way AllocateBufferForThread is allowed to add one is by:
        // 1) take m_lock - AllocateBufferForThread can't own it now because this thread owns it,
        //                  but after this thread gives it up lower in this function it could be acquired.
        // 2) observe m_writeEventSuspending = False - that won't happen, acquiring m_lock
        //                  guarantees AllocateBufferForThread will observe all the memory changes this
        //                  thread made prior to releasing m_lock and we've already set it TRUE.
        // This ensures that we iterate over the list of threads below we've got the complete list.
        SListElem<EventPipeBufferList *> *pElem = m_pPerThreadBufferList->GetHead();
        while (pElem != NULL)
        {
            threadList.Push(pElem->GetValue()->GetThread());
            pElem = m_pPerThreadBufferList->GetNext(pElem);
        }
    }

    // Iterate through all the threads, forcing them to finish writes in progress inside EventPipeThread::m_lock,
    // relinquish any buffers stored in EventPipeThread::m_pWriteBuffer and prevent storing new ones.
    for (size_t i = 0; i < threadList.Size(); i++)
    {
        EventPipeThread *pThread = threadList[i];
        {
            SpinLockHolder _slh(pThread->GetLock());
            pThread->SetWriteBuffer(this, nullptr);

            // From this point until m_writeEventSuspending is reset to FALSE it is impossible
            // for new EventPipeBufferLists to be added to the m_pPerThreadBufferList. The only
            // way AllocateBufferForThread is allowed to add one is by:
            // 1) take m_lock - AllocateBufferForThread can't own it now because this thread owns it,
            //                  but after this thread gives it up lower in this function it could be acquired.
            // 2) observe m_writeEventSuspending = False - that won't happen, acquiring m_lock
            //                  guarantees AllocateBufferForThread will observe all the memory changes this
            //                  thread made prior to releasing m_lock and we've already set it TRUE.
        }
    }

    // Wait for any straggler WriteEvent threads that may have already allocated a buffer but
    // hadn't yet relinquished it.
    {
        SpinLockHolder _slh(&m_lock);
        SListElem<EventPipeBufferList *> *pElem = m_pPerThreadBufferList->GetHead();
        while (pElem != NULL)
        {
            // Get the list and remove it from the thread.
            EventPipeBufferList *const pBufferList = pElem->GetValue();
            if (pBufferList != nullptr)
            {
                EventPipeThread *const pEventPipeThread = pBufferList->GetThread();
                if (pEventPipeThread != nullptr)
                {
                    YIELD_WHILE(pEventPipeThread->GetSessionWriteInProgress() == sessionId);
                    // It still guarantees that the thread has returned its buffer, but it also now guarantees that
                    // that the thread has returned from Session::WriteEvent() and has relinquished the session pointer
                    // This yield is guaranteed to eventually finish because threads will eventually exit WriteEvent()
                    // setting the flag back to -1. If the thread could quickly re-enter WriteEvent and set the flag
                    // back to this_session_id we could theoretically get unlucky and never observe the gap, but
                    // setting s_pSessions[this_session_id] = NULL above guaranteed that can't happen indefinately.
                    // Sooner or later the thread is going to see the NULL value and once it does it won't store
                    // this_session_id into the flag again.
                }
            }

            pElem = m_pPerThreadBufferList->GetNext(pElem);
        }
    }

    // Iterate through all the threads, and remove this buffer manager.
    for (size_t i = 0; i < threadList.Size(); i++)
    {
        EventPipeThread *pThread = threadList[i];
        {
            SpinLockHolder _slh(pThread->GetLock());
            pThread->Remove(this);
        }
    }
}

void EventPipeBufferManager::DeAllocateBuffers()
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_ANY;
    }
    CONTRACTL_END;

    _ASSERTE(EnsureConsistency());
    _ASSERTE(m_writeEventSuspending);

    // Take the buffer manager manipulation lock
    SpinLockHolder _slh(&m_lock);

    SListElem<EventPipeBufferList *> *pElem = m_pPerThreadBufferList->GetHead();
    while (pElem != NULL)
    {
        // Get the list and determine if we can free it.
        EventPipeBufferList *pBufferList = pElem->GetValue();
        EventPipeThread *pThread = pBufferList->GetThread();
        pThread->SetBufferList(this, nullptr);

        // Iterate over all nodes in the list and deallocate them.
        EventPipeBuffer *pBuffer = pBufferList->GetAndRemoveHead();
        while (pBuffer != NULL)
        {
            DeAllocateBuffer(pBuffer);
            pBuffer = pBufferList->GetAndRemoveHead();
        }

        // Remove the buffer list from the per-thread buffer list.
        pElem = m_pPerThreadBufferList->FindAndRemove(pElem);
        _ASSERTE(pElem != NULL);

        SListElem<EventPipeBufferList *> *pCurElem = pElem;
        pElem = m_pPerThreadBufferList->GetNext(pElem);
        delete (pCurElem);

        // Now that all the list elements have been freed, free the list itself.
        delete (pBufferList);
        pBufferList = NULL;
    }
}

#ifdef _DEBUG
bool EventPipeBufferManager::EnsureConsistency()
{
    LIMITED_METHOD_CONTRACT;

    SListElem<EventPipeBufferList *> *pElem = m_pPerThreadBufferList->GetHead();
    while (pElem != NULL)
    {
        EventPipeBufferList *pBufferList = pElem->GetValue();

        _ASSERTE(pBufferList->EnsureConsistency());

        pElem = m_pPerThreadBufferList->GetNext(pElem);
    }

    return true;
}
#endif // _DEBUG

EventPipeBufferList::EventPipeBufferList(EventPipeBufferManager *pManager, EventPipeThread *pThread)
{
    LIMITED_METHOD_CONTRACT;
    _ASSERTE(pManager != nullptr);
    _ASSERTE(pThread != nullptr); // TODO: Is this the correct assumption?

    m_pManager = pManager;
    m_pThread = pThread;
    m_pHeadBuffer = NULL;
    m_pTailBuffer = NULL;
    m_bufferCount = 0;
}

EventPipeBuffer *EventPipeBufferList::GetHead()
{
    LIMITED_METHOD_CONTRACT;
    return m_pHeadBuffer;
}

EventPipeBuffer *EventPipeBufferList::GetTail()
{
    LIMITED_METHOD_CONTRACT;
    return m_pTailBuffer;
}

void EventPipeBufferList::InsertTail(EventPipeBuffer *pBuffer)
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_ANY;
        PRECONDITION(pBuffer != NULL);
        PRECONDITION(EnsureConsistency());
        // Ensure that the input buffer didn't come from another list that was improperly cleaned up.
        PRECONDITION((pBuffer->GetNext() == NULL) && (pBuffer->GetPrevious() == NULL));
    }
    CONTRACTL_END;

    // First node in the list.
    if (m_pTailBuffer == NULL)
    {
        m_pHeadBuffer = m_pTailBuffer = pBuffer;
    }
    else
    {
        // Set links between the old and new tail nodes.
        m_pTailBuffer->SetNext(pBuffer);
        pBuffer->SetPrevious(m_pTailBuffer);

        // Set the new tail node.
        m_pTailBuffer = pBuffer;
    }

    m_bufferCount++;

    _ASSERTE(EnsureConsistency());
}

EventPipeBuffer *EventPipeBufferList::GetAndRemoveHead()
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_ANY;
    }
    CONTRACTL_END;

    _ASSERTE(EnsureConsistency());

    EventPipeBuffer *pRetBuffer = NULL;
    if (m_pHeadBuffer != NULL)
    {
        // Save the head node.
        pRetBuffer = m_pHeadBuffer;

        // Set the new head node.
        m_pHeadBuffer = m_pHeadBuffer->GetNext();

        // Update the head node's previous pointer.
        if (m_pHeadBuffer != NULL)
        {
            m_pHeadBuffer->SetPrevious(NULL);
        }
        else
        {
            // We just removed the last buffer from the list.
            // Make sure both head and tail pointers are NULL.
            m_pTailBuffer = NULL;
        }

        // Clear the next pointer of the old head node.
        pRetBuffer->SetNext(NULL);

        // Ensure that the old head node has no dangling references.
        _ASSERTE((pRetBuffer->GetNext() == NULL) && (pRetBuffer->GetPrevious() == NULL));

        // Decrement the count of buffers in the list.
        m_bufferCount--;
    }

    _ASSERTE(EnsureConsistency());

    return pRetBuffer;
}

unsigned int EventPipeBufferList::GetCount() const
{
    LIMITED_METHOD_CONTRACT;
    return m_bufferCount;
}

EventPipeThread *EventPipeBufferList::GetThread()
{
    LIMITED_METHOD_CONTRACT;
    return m_pThread;
}

#ifdef _DEBUG
bool EventPipeBufferList::EnsureConsistency()
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_ANY;
    }
    CONTRACTL_END;

    // Either the head and tail nodes are both NULL or both are non-NULL.
    _ASSERTE((m_pHeadBuffer == NULL && m_pTailBuffer == NULL) || (m_pHeadBuffer != NULL && m_pTailBuffer != NULL));

    // If the list is NULL, check the count and return.
    if (m_pHeadBuffer == NULL)
    {
        _ASSERTE(m_bufferCount == 0);
        return true;
    }

    // If the list is non-NULL, walk the list forward until we get to the end.
    unsigned int nodeCount = (m_pHeadBuffer != NULL) ? 1 : 0;
    EventPipeBuffer *pIter = m_pHeadBuffer;
    while (pIter->GetNext() != NULL)
    {
        pIter = pIter->GetNext();
        nodeCount++;

        // Check for consistency of the buffer itself.
        // NOTE: We can't check the last buffer because the owning thread could
        // be writing to it, which could result in false asserts.
        if (pIter->GetNext() != NULL)
        {
            _ASSERTE(pIter->EnsureConsistency());
        }

        // Check for cycles.
        _ASSERTE(nodeCount <= m_bufferCount);
    }

    // When we're done with the walk, pIter must point to the tail node.
    _ASSERTE(pIter == m_pTailBuffer);

    // Node count must equal the buffer count.
    _ASSERTE(nodeCount == m_bufferCount);

    // Now, walk the list in reverse.
    pIter = m_pTailBuffer;
    nodeCount = (m_pTailBuffer != NULL) ? 1 : 0;
    while (pIter->GetPrevious() != NULL)
    {
        pIter = pIter->GetPrevious();
        nodeCount++;

        // Check for cycles.
        _ASSERTE(nodeCount <= m_bufferCount);
    }

    // When we're done with the reverse walk, pIter must point to the head node.
    _ASSERTE(pIter == m_pHeadBuffer);

    // Node count must equal the buffer count.
    _ASSERTE(nodeCount == m_bufferCount);

    // We're done.
    return true;
}
#endif // _DEBUG

#ifdef DEBUG
bool EventPipeBufferList::IsBufferManagerLockOwnedByCurrentThread()
{
    return m_pManager->IsLockOwnedByCurrentThread();
}
#endif

#endif // FEATURE_PERFTRACING
