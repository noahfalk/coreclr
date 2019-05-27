// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

#ifndef __EVENTPIPE_BUFFERMANAGER_H__
#define __EVENTPIPE_BUFFERMANAGER_H__

#ifdef FEATURE_PERFTRACING

#include "eventpipe.h"
#include "spinlock.h"

class EventPipeBuffer;
class EventPipeBufferList;
class EventPipeBufferManager;
class EventPipeFile;
class EventPipeSession;
class EventPipeThread;

void ReleaseEventPipeThreadRef(EventPipeThread* pThread);
void AcquireEventPipeThreadRef(EventPipeThread* pThread);
typedef Wrapper<EventPipeThread*, AcquireEventPipeThreadRef, ReleaseEventPipeThreadRef> EventPipeThreadHolder;

typedef MapSHashWithRemove<EventPipeBufferManager *, EventPipeBuffer *> EventPipeWriteBuffers;
typedef MapSHashWithRemove<EventPipeBufferManager *, EventPipeBufferList *> EventPipeBufferLists;

#ifndef __GNUC__
  #define EVENTPIPE_THREAD_LOCAL __declspec(thread)
#else  // !__GNUC__
  #define EVENTPIPE_THREAD_LOCAL thread_local
#endif // !__GNUC__

class EventPipeThread
{
    static EVENTPIPE_THREAD_LOCAL EventPipeThreadHolder gCurrentEventPipeThreadHolder;

    ~EventPipeThread();

    // The EventPipeThreadHolder maintains one count while the thread is alive
    // and each session's EventPipeBufferList maintains one count while it
    // exists
    LONG m_refCount;

    // this is a dictionary of { buffer-manager, buffer } this thread is
    // allowed to write to if exists or non-null, it must match the tail of the
    // m_bufferList
    // this pointer is protected by m_lock
    EventPipeWriteBuffers *m_pWriteBuffers = nullptr;

    // this is a dictionary of { buffer-manager, list of buffers } that were
    // written to by this thread
    // it is protected by EventPipeBufferManager::m_lock
    EventPipeBufferLists *m_pBufferLists = nullptr;

    // This lock is designed to have low contention. Normally it is only taken by this thread,
    // but occasionally it may also be taken by another thread which is trying to collect and drain
    // buffers from all threads.
    SpinLock m_lock;

    //
    EventPipeSession *m_pRundownSession = nullptr;

#ifdef DEBUG
    template <typename T>
    static bool AllValuesAreNull(T &map)
    {
        LIMITED_METHOD_CONTRACT;
        for (typename T::Iterator iter = map.Begin(); iter != map.End(); ++iter)
            if (iter->Value() != nullptr)
                return false;
        return true;
    }
#endif // DEBUG

public:
    static EventPipeThread *Get();
    static EventPipeThread *GetOrCreate();
    static void Set(EventPipeThread *pThread);

    bool IsRundownThread() const
    {
        LIMITED_METHOD_CONTRACT;
        return (m_pRundownSession != nullptr);
    }

    void SetAsRundownThread(EventPipeSession *pSession)
    {
        LIMITED_METHOD_CONTRACT;
        m_pRundownSession = pSession;
    }

    EventPipeSession *GetRundownSession() const
    {
        LIMITED_METHOD_CONTRACT;
        return m_pRundownSession;
    }

    EventPipeThread();
    void AddRef();
    void Release();
    SpinLock *GetLock();
    Volatile<EventPipeSessionID> m_writingEventInProgress;

    EventPipeBuffer *GetWriteBuffer(EventPipeBufferManager *pBufferManager);
    void SetWriteBuffer(EventPipeBufferManager *pBufferManager, EventPipeBuffer *pNewBuffer);
    EventPipeBufferList *GetBufferList(EventPipeBufferManager *pBufferManager);
    void SetBufferList(EventPipeBufferManager *pBufferManager, EventPipeBufferList *pBufferList);
    void Remove(EventPipeBufferManager *pBufferManager);

    void SetSessionWriteInProgress(uint64_t index)
    {
        LIMITED_METHOD_CONTRACT;
        m_writingEventInProgress.Store((index < 64) ? (1ULL << index) : UINT64_MAX);
    }

    EventPipeSessionID GetSessionWriteInProgress() const
    {
        LIMITED_METHOD_CONTRACT;
        return m_writingEventInProgress.Load();
    }
};

class EventPipeBufferManager
{

    // Declare friends.
    friend class EventPipeBufferList;

private:

    // A list of linked-lists of buffer objects.
    // Each entry in this list represents a set of buffers owned by a single thread.
    // The actual Thread object has a pointer to the object contained in this list.  This ensures that
    // each thread can access its own list, while at the same time, ensuring that when
    // a thread is destroyed, we keep the buffers around without having to perform any
    // migration or book-keeping.
    SList<SListElem<EventPipeBufferList*>> *m_pPerThreadBufferList;

    // The total allocation size of buffers under management.
    size_t m_sizeOfAllBuffers;

    // Lock to protect access to the per-thread buffer list and total allocation size.
    SpinLock m_lock;
    Volatile<BOOL> m_writeEventSuspending;

    // Iterator state for reader thread
    // These are not protected by m_lock and expected to only be used on the reader thread
    EventPipeEventInstance* m_pCurrentEvent;
    EventPipeBuffer* m_pCurrentBuffer;
    EventPipeBufferList* m_pCurrentBufferList;

#ifdef _DEBUG
    // For debugging purposes.
    unsigned int m_numBuffersAllocated;
    unsigned int m_numBuffersStolen;
    unsigned int m_numBuffersLeaked;
    Volatile<LONG> m_numEventsStored;
    Volatile<LONG> m_numEventsDropped;
    unsigned long m_numEventsWritten;
#endif // _DEBUG

    // Allocate a new buffer for the specified thread.
    // This function will store the buffer in the thread's buffer list for future use and also return it here.
    // A NULL return value means that a buffer could not be allocated.
    EventPipeBuffer* AllocateBufferForThread(EventPipeSession &session, unsigned int requestSize, BOOL & writeSuspended);

    // Add a buffer to the thread buffer list.
    void AddBufferToThreadBufferList(EventPipeBufferList *pThreadBuffers, EventPipeBuffer *pBuffer);

    // De-allocates the input buffer.
    void DeAllocateBuffer(EventPipeBuffer *pBuffer);

    // Detaches this buffer from an active writer thread and marks it read-only so that the reader
    // thread can use it. If the writer thread has not yet stored the buffer into its thread-local
    // slot it will not be converted, but such buffers have no events in them so there is no reason
    // to read them.
    bool TryConvertBufferToReadOnly(EventPipeBuffer* pNewReadBuffer);

    // Finds the first buffer in EventPipeBufferList that has a readable event prior to beforeTimeStamp,
    // starting with pBuffer
    EventPipeBuffer* AdvanceToNonEmptyBuffer(EventPipeBufferList* pBufferList,
                                             EventPipeBuffer* pBuffer,
                                             LARGE_INTEGER beforeTimeStamp);

    //  -------------- Reader Iteration API ----------------
    // An iterator that can enumerate all the events which have been written into this buffer manager.
    // Initially the iterator starts uninitialized and GetCurrentEvent() returns NULL. Calling MoveNextXXX()
    // attempts to advance the cursor to the next event. If there is no event prior to stopTimeStamp then
    // the GetCurrentEvent() again returns NULL, otherwise it returns that event. The event pointer returned
    // by GetCurrentEvent() is valid until MoveNextXXX() is called again. Once all events in a buffer have 
    // been read the iterator will delete that buffer from the pool.

    // Moves to the next oldest event searching across all threads. If there is no event older than
    // stopTimeStamp then GetCurrentEvent() will return NULL.
    void MoveNextEventAnyThread(LARGE_INTEGER stopTimeStamp);

    // Moves to the next oldest event from the same thread as the current event. If there is no event 
    // older than stopTimeStamp then GetCurrentEvent() will return NULL. This should only be called
    // when GetCurrentEvent() is non-null (because we need to know what thread's events to iterate)
    void MoveNextEventSameThread(LARGE_INTEGER stopTimeStamp);

    // Returns the current event the iteration cursor is on, or NULL if the iteration is unitialized/
    // the last call to MoveNextXXX() didn't find any suitable event.
    EventPipeEventInstance* GetCurrentEvent();

public:

    EventPipeBufferManager();
    ~EventPipeBufferManager();

    // Write an event to the input thread's current event buffer.
    // An optional eventThread can be provided for sample profiler events.
    // This is because the thread that writes the events is not the same as the "event thread".
    // An optional stack trace can be provided for sample profiler events.
    // Otherwise, if a stack trace is needed, one will be automatically collected.
    bool WriteEvent(Thread *pThread, EventPipeSession &session, EventPipeEvent &event, EventPipeEventPayload &payload, LPCGUID pActivityId, LPCGUID pRelatedActivityId, Thread *pEventThread = NULL, StackContents *pStack = NULL);

    // Suspends all WriteEvent activity. All existing buffers will be in the
    // READ_ONLY state and no new EventPipeBuffers or EventPipeBufferLists can be created. Calls to
    // WriteEvent that start during the suspension period or were in progress but hadn't yet recorded
    // their event into a buffer before the start of the suspension period will return false and the
    // event will not be recorded. Any events that not recorded as a result of this suspension will be
    // treated the same as events that were not recorded due to configuration.
    // EXPECTED USAGE: First the caller will disable all events via configuration, then call
    // SuspendWriteEvent() to force any WriteEvent calls that may still be in progress to either
    // finish or cancel. After that all BufferLists and Buffers can be safely drained and/or deleted.
    void SuspendWriteEvent(EventPipeSessionID sessionId);

    // Write the contents of the managed buffers to the specified file.
    // The stopTimeStamp is used to determine when tracing was stopped to ensure that we
    // skip any events that might be partially written due to races when tracing is stopped.
    void WriteAllBuffersToFile(EventPipeFile *pFile, LARGE_INTEGER stopTimeStamp);

    // Attempt to de-allocate resources as best we can.  It is possible for some buffers to leak because
    // threads can be in the middle of a write operation and get blocked, and we may not get an opportunity
    // to free their buffer for a very long time.
    void DeAllocateBuffers();

    // Get next event.  This is used to dispatch events to EventListener.
    EventPipeEventInstance* GetNextEvent();

#ifdef _DEBUG
    bool EnsureConsistency();
    bool IsLockOwnedByCurrentThread();
#endif // _DEBUG
};

// Represents a list of buffers associated with a specific thread.
class EventPipeBufferList
{
private:

    // The buffer manager that owns this list.
    EventPipeBufferManager *m_pManager;

    // The thread which writes to the buffers in this list
    EventPipeThreadHolder m_pThread;

    // Buffers are stored in an intrusive linked-list from oldest to newest.
    // Head is the oldest buffer.  Tail is the newest (and currently used) buffer.
    EventPipeBuffer *m_pHeadBuffer;
    EventPipeBuffer *m_pTailBuffer;

    // The number of buffers in the list.
    unsigned int m_bufferCount;

public:

    EventPipeBufferList(EventPipeBufferManager *pManager, EventPipeThread* pThread);

    // Get the head node of the list.
    EventPipeBuffer* GetHead();

    // Get the tail node of the list.
    EventPipeBuffer* GetTail();

    // Insert a new buffer at the tail of the list.
    void InsertTail(EventPipeBuffer *pBuffer);

    // Remove the head node of the list.
    EventPipeBuffer* GetAndRemoveHead();

    // Get the count of buffers in the list.
    unsigned int GetCount() const;

    // Get the thread associated with this list.
    EventPipeThread* GetThread();

#ifdef _DEBUG
    // Validate the consistency of the list.
    // This function will assert if the list is in an inconsistent state.
    bool EnsureConsistency();
#endif // _DEBUG

#ifdef DEBUG
    bool IsBufferManagerLockOwnedByCurrentThread();
#endif // DEBUG
};


#endif // FEATURE_PERFTRACING

#endif // __EVENTPIPE_BUFFERMANAGER_H__
