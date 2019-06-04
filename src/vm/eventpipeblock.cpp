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
    FastSerializableObject(GetBlockVersion(format), GetBlockMinVersion(format), format >= EventPipeNetTraceFormatV4)
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

    delete[] m_pBlock;
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
}

EventPipeEventBlockBase::EventPipeEventBlockBase(unsigned int maxBlockSize, EventPipeSerializationFormat format) :
    EventPipeBlock(maxBlockSize, format)
{}

bool EventPipeEventBlockBase::WriteEvent(EventPipeEventInstance &instance, ULONGLONG captureThreadId, unsigned int sequenceNumber, BOOL isSortedEvent)
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_ANY;
        PRECONDITION(isSortedEvent || m_format >= EventPipeNetTraceFormatV4);
    }
    CONTRACTL_END;

    if (m_pBlock == NULL)
    {
        return false;
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
    _ASSERTE((metadataId & (1 << 31)) == 0);
    metadataId |= (!isSortedEvent ? 1 << 31 : 0);
    memcpy(m_pWritePointer, &metadataId, sizeof(metadataId));
    m_pWritePointer += sizeof(metadataId);

    if (m_format == EventPipeNetPerfFormatV3)
    {
        DWORD threadId = instance.GetThreadId32();
        memcpy(m_pWritePointer, &threadId, sizeof(threadId));
        m_pWritePointer += sizeof(threadId);
    }
    else if (m_format == EventPipeNetTraceFormatV4)
    {
        memcpy(m_pWritePointer, &sequenceNumber, sizeof(sequenceNumber));
        m_pWritePointer += sizeof(sequenceNumber);

        ULONGLONG threadId = instance.GetThreadId64();
        memcpy(m_pWritePointer, &threadId, sizeof(threadId));
        m_pWritePointer += sizeof(threadId);

        memcpy(m_pWritePointer, &captureThreadId, sizeof(captureThreadId));
        m_pWritePointer += sizeof(captureThreadId);
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

EventPipeEventBlock::EventPipeEventBlock(unsigned int maxBlockSize, EventPipeSerializationFormat format) :
    EventPipeEventBlockBase(maxBlockSize, format)
{}


EventPipeMetadataBlock::EventPipeMetadataBlock(unsigned int maxBlockSize) :
    EventPipeEventBlockBase(maxBlockSize, EventPipeNetTraceFormatV4)
{}

unsigned int GetSequencePointBlockSize(EventPipeSequencePoint* pSequencePoint)
{
    const unsigned int sizeOfSequenceNumber =
        sizeof(ULONGLONG) +    // thread id
        sizeof(unsigned int);  // sequence number
    return sizeof(pSequencePoint->TimeStamp) +
        pSequencePoint->ThreadSequenceNumbers.GetCount() * sizeOfSequenceNumber;
}

EventPipeSequencePointBlock::EventPipeSequencePointBlock(EventPipeSequencePoint* pSequencePoint) :
    EventPipeBlock(GetSequencePointBlockSize(pSequencePoint))
{
    const LARGE_INTEGER* timeStamp = &pSequencePoint->TimeStamp;
    memcpy(m_pWritePointer, timeStamp, sizeof(*timeStamp));
    m_pWritePointer += sizeof(*timeStamp);

    for (ThreadSequenceNumberMap::Iterator pCur = pSequencePoint->ThreadSequenceNumbers.Begin();
        pCur != pSequencePoint->ThreadSequenceNumbers.End();
        pCur++)
    {
        const ULONGLONG threadId = pCur->Key()->GetThread()->GetOSThreadId();
        memcpy(m_pWritePointer, &threadId, sizeof(threadId));
        m_pWritePointer += sizeof(threadId);

        const unsigned int sequenceNumber = pCur->Value();
        memcpy(m_pWritePointer, &sequenceNumber, sizeof(sequenceNumber));
        m_pWritePointer += sizeof(sequenceNumber);
    }
}


#endif // FEATURE_PERFTRACING
