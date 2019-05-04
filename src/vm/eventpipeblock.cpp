// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

#include "common.h"
#include "eventpipeblock.h"
#include "eventpipeeventinstance.h"
#include "fastserializableobject.h"
#include "fastserializer.h"

#ifdef FEATURE_PERFTRACING



DWORD GetBlockVersion(EventPipeSerializationFormat format)
{
    LIMITED_METHOD_CONTRACT;
    switch (format)
    {
    case EventPipeNetPerfFormatV3:
        return 1;
    case EventPipeNetTraceFormatV4:
        return 2;
    }
    _ASSERTE(!"Unrecognized EventPipeSerializationFormat");
    return 0;
}

DWORD GetBlockMinVersion(EventPipeSerializationFormat format)
{
    LIMITED_METHOD_CONTRACT;
    switch (format)
    {
    case EventPipeNetPerfFormatV3:
        return 0;
    case EventPipeNetTraceFormatV4:
        return 2;
    }
    _ASSERTE(!"Unrecognized EventPipeSerializationFormat");
    return 0;
}

EventPipeBlock::EventPipeBlock(unsigned int maxBlockSize, EventPipeSerializationFormat format) :
    FastSerializableObject(GetBlockVersion(format), GetBlockMinVersion(format))
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_ANY;
    }
    CONTRACTL_END;

    m_pBlock = new (nothrow) BYTE[maxBlockSize];
    if (m_pBlock == NULL)
    {
        return;
    }

    memset(m_pBlock, 0, maxBlockSize);
    m_pWritePointer = m_pBlock;
    m_pEndOfTheBuffer = m_pBlock + maxBlockSize;
    m_format = format;
    m_threadId = -1;
    m_flags = 0;
}

EventPipeBlock::~EventPipeBlock()
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_ANY;
    }
    CONTRACTL_END;

    if(m_pBlock != NULL)
    {
        m_pEndOfTheBuffer = NULL;
        m_pWritePointer = NULL;
        delete[] m_pBlock;
        m_pBlock = NULL;
    }
}

bool EventPipeBlock::WriteEvent(EventPipeEventInstance &instance)
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_ANY;
    }
    CONTRACTL_END;

    if (m_pBlock == NULL)
    {
        return false;
    }

    // In NetTraceV4 all events in the block belong to the same thread. If this block has
    // already recorded events from another thread it needs to be flushed and we start a
    // new block. If this block has never logged an event then we assign it to the thread
    // from this event.
    if (m_format == EventPipeNetTraceFormatV4 && m_threadId != instance.GetThreadId())
    {
        
        if (m_threadId == -1)
        {
            m_threadId = instance.GetThreadId();
        }
        else
        {
            // TODO: assert this once we aren't putting metadata in the same block
            //_ASSERTE(!"In EventPipe V4 format it is illegal to reuse a block for events on different threads");
            return false;
        }
    }


    unsigned int totalSize = instance.GetAlignedTotalSize(m_format);
    if (m_pWritePointer + totalSize >= m_pEndOfTheBuffer)
    {
        return false;
    }

    BYTE* alignedEnd = m_pWritePointer + totalSize + sizeof(totalSize);

    memcpy(m_pWritePointer, &totalSize, sizeof(totalSize));
    m_pWritePointer += sizeof(totalSize);

    unsigned int metadataId = instance.GetMetadataId();
    memcpy(m_pWritePointer, &metadataId, sizeof(metadataId));
    m_pWritePointer += sizeof(metadataId);

    if (m_format == EventPipeNetPerfFormatV3)
    {
        DWORD threadId = instance.GetThreadId();
        memcpy(m_pWritePointer, &threadId, sizeof(threadId));
        m_pWritePointer += sizeof(threadId);
    }

    const LARGE_INTEGER* timeStamp = instance.GetTimeStamp();
    memcpy(m_pWritePointer, timeStamp, sizeof(*timeStamp));
    m_pWritePointer += sizeof(*timeStamp);

    const GUID* activityId = instance.GetActivityId();
    memcpy(m_pWritePointer, activityId, sizeof(*activityId));
    m_pWritePointer += sizeof(*activityId);

    const GUID* relatedActivityId = instance.GetRelatedActivityId();
    memcpy(m_pWritePointer, relatedActivityId, sizeof(*relatedActivityId));
    m_pWritePointer += sizeof(*relatedActivityId);

    unsigned int dataLength = instance.GetDataLength();
    memcpy(m_pWritePointer, &dataLength, sizeof(dataLength));
    m_pWritePointer += sizeof(dataLength);

    if (dataLength > 0)
    {
        memcpy(m_pWritePointer, instance.GetData(), dataLength);
        m_pWritePointer += dataLength;
    }

    unsigned int stackSize = instance.GetStackSize();
    memcpy(m_pWritePointer, &stackSize, sizeof(stackSize));
    m_pWritePointer += sizeof(stackSize);

    if (stackSize > 0)
    {
        memcpy(m_pWritePointer, instance.GetStack(), stackSize);
        m_pWritePointer += stackSize;
    }

    while (m_pWritePointer < alignedEnd)
    {
        *m_pWritePointer++ = (BYTE)0; // put padding at the end to get 4 bytes alignment of the payload
    }

    return true;
}

void EventPipeBlock::SetSequencePointBit()
{
    LIMITED_METHOD_CONTRACT;
    m_flags |= IsSequencePoint;
}

void EventPipeBlock::Clear()
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_ANY;
    }
    CONTRACTL_END;

    if (m_pBlock == NULL)
    {
        return;
    }

    memset(m_pBlock, 0, GetSize());
    m_pWritePointer = m_pBlock;
    m_threadId = -1;
    m_flags = 0;
}

#endif // FEATURE_PERFTRACING
