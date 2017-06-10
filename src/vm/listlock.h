// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.
// ===========================================================================
// File: ListLock.h
// 

// 
// ===========================================================================
// This file decribes the list lock and deadlock aware list lock.
// ===========================================================================

#ifndef LISTLOCK_H
#define LISTLOCK_H

#include "vars.hpp"
#include "threads.h"
#include "crst.h"

template < typename ELEMENT >
class ListLockBase;
// This structure is used for running class init methods or JITing methods
// (m_pData points to a FunctionDesc). This class cannot have a destructor since it is used
// in function that also have EX_TRY's and the VC compiler doesn't allow classes with destructors
// to be allocated in a function that used SEH.
// <TODO>@FUTURE Keep a pool of these (e.g. an array), so we don't have to allocate on the fly</TODO>
// m_hInitException contains a handle to the exception thrown by the class init. This
// allows us to throw this information to the caller on subsequent class init attempts.
template < typename ELEMENT >
class ListLockEntryBase
{
    friend class ListLockBase<ELEMENT>;

public:
#ifdef _DEBUG
    bool Check()
    {
        WRAPPER_NO_CONTRACT;

        return m_dwRefCount != (DWORD)-1;
    }
#endif // DEBUG

    DeadlockAwareLock       m_deadlock;
    ListLockBase<ELEMENT> * m_pList;
    ELEMENT                 m_data;
    Crst                    m_Crst;
    const char *            m_pszDescription;
    ListLockEntryBase<ELEMENT> * m_pNext;
    DWORD                   m_dwRefCount;
    HRESULT                 m_hrResultCode;
    LOADERHANDLE            m_hInitException;
    PTR_LoaderAllocator     m_pLoaderAllocator;
#ifdef FEATURE_CORRUPTING_EXCEPTIONS
    // Field to maintain the corruption severity of the exception
    CorruptionSeverity      m_CorruptionSeverity;
#endif // FEATURE_CORRUPTING_EXCEPTIONS

    ListLockEntryBase(ListLockBase<ELEMENT> *pList, ELEMENT data, const char *description = NULL)
    : m_deadlock(description),
        m_pList(pList),
        m_data(data),
        m_Crst(CrstListLock,
        (CrstFlags)(CRST_REENTRANCY | (pList->IsHostBreakable() ? CRST_HOST_BREAKABLE : 0))),
        m_pszDescription(description),
        m_pNext(NULL),
        m_dwRefCount(1),
        m_hrResultCode(S_FALSE),
        m_hInitException(NULL),
        m_pLoaderAllocator(NULL)
#ifdef FEATURE_CORRUPTING_EXCEPTIONS
        ,
        m_CorruptionSeverity(NotCorrupting)
#endif // FEATURE_CORRUPTING_EXCEPTIONS
    {
        WRAPPER_NO_CONTRACT;
    }

    virtual ~ListLockEntryBase()
    {
    }

    DEBUG_NOINLINE void Enter()
    {
        WRAPPER_NO_CONTRACT;
        ANNOTATION_SPECIAL_HOLDER_CALLER_NEEDS_DYNAMIC_CONTRACT;

        m_deadlock.BeginEnterLock();
        DeadlockAwareLock::BlockingLockHolder dlLock;
        m_Crst.Enter();
        m_deadlock.EndEnterLock();
    }

    BOOL CanDeadlockAwareEnter()
    {
        WRAPPER_NO_CONTRACT;

        return m_deadlock.CanEnterLock();
    }

    DEBUG_NOINLINE BOOL DeadlockAwareEnter()
    {
        WRAPPER_NO_CONTRACT;
        ANNOTATION_SPECIAL_HOLDER_CALLER_NEEDS_DYNAMIC_CONTRACT;

        if (!m_deadlock.TryBeginEnterLock())
            return FALSE;

        DeadlockAwareLock::BlockingLockHolder dlLock;
        m_Crst.Enter();
        m_deadlock.EndEnterLock();

        return TRUE;
    }

    DEBUG_NOINLINE void Leave()
    {
        WRAPPER_NO_CONTRACT;
        ANNOTATION_SPECIAL_HOLDER_CALLER_NEEDS_DYNAMIC_CONTRACT;

        m_deadlock.LeaveLock();
        m_Crst.Leave();
    }

    static ListLockEntry *Find(ListLockBase<ELEMENT>* pLock, ELEMENT data, const char *description = NULL)
    {
        CONTRACTL
        {
            THROWS;
            GC_NOTRIGGER;
            MODE_ANY;
        }
        CONTRACTL_END;

        _ASSERTE(pLock->HasLock());

        ListLockEntryBase<ELEMENT> *pEntry = pLock->Find(data);
        if (pEntry == NULL)
        {
            pEntry = new ListLockEntryBase<ELEMENT>(pLock, data, description);
            pLock->AddElement(pEntry);
        }
        else
            pEntry->AddRef();

        return pEntry;
    };


    void AddRef()
    {
        CONTRACTL
        {
            NOTHROW;
            GC_NOTRIGGER;
            MODE_ANY;
            PRECONDITION(CheckPointer(this));
        }
        CONTRACTL_END;

        FastInterlockIncrement((LONG*)&m_dwRefCount);
    }

    void Release()
    {
        CONTRACTL
        {
            NOTHROW;
            GC_TRIGGERS;
            MODE_ANY;
            PRECONDITION(CheckPointer(this));
        }
        CONTRACTL_END;

        ListLockBase<ELEMENT>::LockHolder lock(m_pList);

        if (FastInterlockDecrement((LONG*)&m_dwRefCount) == 0)
        {
            // Remove from list
            m_pList->Unlink(this);
            delete this;
        }
    };

#ifdef _DEBUG
    BOOL HasLock()
    {
        WRAPPER_NO_CONTRACT;
        return(m_Crst.OwnedByCurrentThread());
    }
#endif

    // LockHolder holds the lock of the element, not the element itself

    DEBUG_NOINLINE static void LockHolderEnter(ListLockEntryBase<ELEMENT> *pThis) PUB
    {
        WRAPPER_NO_CONTRACT;
        ANNOTATION_SPECIAL_HOLDER_CALLER_NEEDS_DYNAMIC_CONTRACT;
        pThis->Enter();
    }

    DEBUG_NOINLINE static void LockHolderLeave(ListLockEntryBase<ELEMENT> *pThis) PUB
    {
        WRAPPER_NO_CONTRACT;
        ANNOTATION_SPECIAL_HOLDER_CALLER_NEEDS_DYNAMIC_CONTRACT;
        pThis->Leave();
    }

    DEBUG_NOINLINE void FinishDeadlockAwareEnter()
    {
        ANNOTATION_SPECIAL_HOLDER_CALLER_NEEDS_DYNAMIC_CONTRACT;
        DeadlockAwareLock::BlockingLockHolder dlLock;
        m_Crst.Enter();
        m_deadlock.EndEnterLock();
    }

    typedef Wrapper<ListLockEntryBase<ELEMENT> *, ListLockEntryBase<ELEMENT>::LockHolderEnter, ListLockEntryBase<ELEMENT>::LockHolderLeave> LockHolderBase;

    class LockHolder : public LockHolderBase
    {
    public:

        LockHolder() 
          : LockHolderBase(NULL, FALSE)
        {
        }

        LockHolder(ListLockEntryBase<ELEMENT> *value, BOOL take = TRUE) 
          : LockHolderBase(value, take)
        {
        }

        BOOL DeadlockAwareAcquire()
        {
            if (!m_acquired && m_value != NULL)
            {
                if (!m_value->m_deadlock.TryBeginEnterLock())
                    return FALSE;
                m_value->FinishDeadlockAwareEnter();
                m_acquired = TRUE;
            }
            return TRUE;
        }
    };
};

template < typename ELEMENT >
class ListLockBase
{
 protected:
    CrstStatic          m_Crst;
    BOOL                m_fInited;
    BOOL                m_fHostBreakable;        // Lock can be broken by a host for deadlock detection
    ListLockEntryBase<ELEMENT> * m_pHead;

 public:

    BOOL IsInitialized()
    {
		LIMITED_METHOD_CONTRACT;
        return m_fInited;
    }
    inline void PreInit()
    {
        LIMITED_METHOD_CONTRACT;
        memset(this, 0, sizeof(*this));
    }

    // DO NOT MAKE A CONSTRUCTOR FOR THIS CLASS - There are global instances
    void Init(CrstType crstType, CrstFlags flags, BOOL fHostBreakable = FALSE)
    {
	 WRAPPER_NO_CONTRACT;
        PreInit();
        m_Crst.Init(crstType, flags);
        m_fInited = TRUE;
        m_fHostBreakable = fHostBreakable;
    }

    void Destroy()
    {
        WRAPPER_NO_CONTRACT;
        // There should not be any of these around
        _ASSERTE(m_pHead == NULL || dbg_fDrasticShutdown || g_fInControlC);

        if (m_fInited)
        {
            m_fInited = FALSE;
            m_Crst.Destroy();
        }
    }

    BOOL IsHostBreakable () const
    {
        LIMITED_METHOD_CONTRACT;
        return m_fHostBreakable;
    }

    void AddElement(ListLockEntryBase<ELEMENT>* pElement)
    {
        WRAPPER_NO_CONTRACT;
        pElement->m_pNext = m_pHead;
        m_pHead = pElement;
    }


    DEBUG_NOINLINE void Enter()
    {
        CANNOT_HAVE_CONTRACT; // See below
        ANNOTATION_SPECIAL_HOLDER_CALLER_NEEDS_DYNAMIC_CONTRACT;
#if 0 // The cleanup logic contract will cause any forbid GC state from the Crst to 
      // get deleted.  This causes asserts from Leave.  We probably should make the contract
      // implementation tolerant of this pattern, or else ensure that the state the contract
      // modifies is not used by any other code.
        CONTRACTL
        {
            NOTHROW;
            UNCHECKED(GC_TRIGGERS); // May trigger or not based on Crst's type
            MODE_ANY;
            PRECONDITION(CheckPointer(this));
        }
        CONTRACTL_END;
#endif

        m_Crst.Enter();
    }

    DEBUG_NOINLINE void Leave()
    {
        WRAPPER_NO_CONTRACT;
        ANNOTATION_SPECIAL_HOLDER_CALLER_NEEDS_DYNAMIC_CONTRACT;
        m_Crst.Leave();
    }

    // Must own the lock before calling this or is ok if the debugger has
    // all threads stopped
    inline ListLockEntryBase<ELEMENT> *Find(ELEMENT data)
    {
        CONTRACTL
        {
            NOTHROW;
        GC_NOTRIGGER;
        PRECONDITION(CheckPointer(this));
#ifdef DEBUGGING_SUPPORTED
        PRECONDITION(m_Crst.OwnedByCurrentThread() ||
            CORDebuggerAttached() && g_pDebugInterface->IsStopped());
#else
        PRECONDITION(m_Crst.OwnedByCurrentThread());
#endif // DEBUGGING_SUPPORTED

        }
        CONTRACTL_END;

        ListLockEntryBase<ELEMENT> *pSearch;

        for (pSearch = m_pHead; pSearch != NULL; pSearch = pSearch->m_pNext)
        {
            if (pSearch->m_data == data)
                return pSearch;
        }

        return NULL;
    }

    // Must own the lock before calling this!
    ListLockEntryBase<ELEMENT>* Pop(BOOL unloading = FALSE) 
    {
        LIMITED_METHOD_CONTRACT;
#ifdef _DEBUG
        if(unloading == FALSE)
            _ASSERTE(m_Crst.OwnedByCurrentThread());
#endif

        if(m_pHead == NULL) return NULL;
        ListLockEntryBase<ELEMENT>* pEntry = m_pHead;
        m_pHead = m_pHead->m_pNext;
        return pEntry;
    }

    // Must own the lock before calling this!
    ListLockEntryBase<ELEMENT>* Peek() 
    {
		LIMITED_METHOD_CONTRACT;
        _ASSERTE(m_Crst.OwnedByCurrentThread());
        return m_pHead;
    }

    // Must own the lock before calling this!
    BOOL Unlink(ListLockEntryBase<ELEMENT> *pItem)
    {
		LIMITED_METHOD_CONTRACT;
        _ASSERTE(m_Crst.OwnedByCurrentThread());
        ListLockEntryBase<ELEMENT> *pSearch;
        ListLockEntryBase<ELEMENT> *pPrev;

        pPrev = NULL;

        for (pSearch = m_pHead; pSearch != NULL; pSearch = pSearch->m_pNext)
        {
            if (pSearch == pItem)
            {
                if (pPrev == NULL)
                    m_pHead = pSearch->m_pNext;
                else
                    pPrev->m_pNext = pSearch->m_pNext;

                return TRUE;
            }

            pPrev = pSearch;
        }

        // Not found
        return FALSE;
    }

#ifdef _DEBUG
    BOOL HasLock()
    {
        WRAPPER_NO_CONTRACT;
        STATIC_CONTRACT_SO_TOLERANT;
        return(m_Crst.OwnedByCurrentThread());
    }
#endif

    DEBUG_NOINLINE static void HolderEnter(ListLockBase<ELEMENT> *pThis)
    {
        WRAPPER_NO_CONTRACT;
        ANNOTATION_SPECIAL_HOLDER_CALLER_NEEDS_DYNAMIC_CONTRACT;
        pThis->Enter();
    }

    DEBUG_NOINLINE static void HolderLeave(ListLockBase<ELEMENT> *pThis)
    {
        WRAPPER_NO_CONTRACT;
        ANNOTATION_SPECIAL_HOLDER_CALLER_NEEDS_DYNAMIC_CONTRACT;
        pThis->Leave();
    }

    typedef Wrapper<ListLockBase<ELEMENT>*, ListLockBase<ELEMENT>::HolderEnter, ListLockBase<ELEMENT>::HolderLeave> LockHolder;
};

class WaitingThreadListElement
{
public:
    Thread *                   m_pThread;
    WaitingThreadListElement * m_pNext;
};

typedef class ListLockBase<void*> ListLock;
typedef class ListLockEntryBase<void*> ListLockEntry;

// Holds the lock of the ListLock
typedef ListLock::LockHolder ListLockHolder;

// Holds the ownership of the lock element
typedef ReleaseHolder<ListLockEntry> ListLockEntryHolder;

// Holds the lock of the lock element
typedef ListLockEntry::LockHolder ListLockEntryLockHolder;


#endif // LISTLOCK_H
