// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

#include "common.h"
#include "eventpipeblock.h"
#include "eventpipeeventinstance.h"
#include "eventpipefile.h"
#include "sampleprofiler.h"

#ifdef FEATURE_PERFTRACING

DWORD GetFileVersion(EventPipeSerializationFormat format)
{
    LIMITED_METHOD_CONTRACT;
    switch(format)
    {
    case EventPipeNetPerfFormatV3:
        return 3;
    case EventPipeNetTraceFormatV4:
        return 4;
    }
    _ASSERTE(!"Unrecognized EventPipeSerializationFormat");
    return 0;
}

DWORD GetFileMinVersion(EventPipeSerializationFormat format)
{
    LIMITED_METHOD_CONTRACT;
    switch (format)
    {
    case EventPipeNetPerfFormatV3:
        return 0;
    case EventPipeNetTraceFormatV4:
        return 4;
    }
    _ASSERTE(!"Unrecognized EventPipeSerializationFormat");
    return 0;
}

EventPipeFile::EventPipeFile(StreamWriter *pStreamWriter, EventPipeSerializationFormat format) :
    FastSerializableObject(GetFileVersion(format), GetFileMinVersion(format), format >= EventPipeNetTraceFormatV4)
{
    CONTRACTL
    {
        THROWS;
        GC_TRIGGERS;
        MODE_PREEMPTIVE;
    }
    CONTRACTL_END;

    m_format = format;
    m_pBlock = new EventPipeEventBlock(100 * 1024, format);
    m_pMetadataBlock = new EventPipeMetadataBlock(100 * 1024);

    // File start time information.
    GetSystemTime(&m_fileOpenSystemTime);
    QueryPerformanceCounter(&m_fileOpenTimeStamp);
    QueryPerformanceFrequency(&m_timeStampFrequency);

    m_pointerSize = TARGET_POINTER_SIZE;

    m_currentProcessId = GetCurrentProcessId();

    SYSTEM_INFO sysinfo = {};
    GetSystemInfo(&sysinfo);
    m_numberOfProcessors = sysinfo.dwNumberOfProcessors;

    m_samplingRateInNs = SampleProfiler::GetSamplingRate();

    // Create the file stream and write the header.
    m_pSerializer = new FastSerializer(pStreamWriter);

    m_serializationLock.Init(LOCK_TYPE_DEFAULT);

    m_pMetadataIds = new MapSHashWithRemove<EventPipeEvent*, unsigned int>();

    // Start and 0 - The value is always incremented prior to use, so the first ID will be 1.
    m_metadataIdCounter = 0;

#ifdef DEBUG
    QueryPerformanceCounter(&m_lastSortedTimestamp);
#endif

    // Write the first object to the file.
    m_pSerializer->WriteObject(this);
}

EventPipeFile::~EventPipeFile()
{
    CONTRACTL
    {
        NOTHROW;
        GC_TRIGGERS;
        MODE_ANY;
    }
    CONTRACTL_END;

    if (m_pBlock != NULL && m_pSerializer != NULL)
        WriteEnd();

    delete m_pBlock;
    delete m_pMetadataBlock;
    delete m_pSerializer;
    delete m_pMetadataIds;
}

EventPipeSerializationFormat EventPipeFile::GetSerializationFormat() const
{
    LIMITED_METHOD_CONTRACT;
    return m_format;
}

bool EventPipeFile::HasErrors() const
{
    LIMITED_METHOD_CONTRACT;
    return (m_pSerializer == nullptr) || m_pSerializer->HasWriteErrors();
}

void EventPipeFile::WriteEvent(EventPipeEventInstance &instance, ULONGLONG captureThreadId, unsigned int sequenceNumber, BOOL isSortedEvent)
{
    CONTRACTL
    {
        THROWS;
        GC_NOTRIGGER;
        MODE_ANY;
    }
    CONTRACTL_END;

#ifdef DEBUG
    _ASSERTE(instance.GetTimeStamp()->QuadPart > m_lastSortedTimestamp.QuadPart);
    if (isSortedEvent)
    {
        m_lastSortedTimestamp = *(instance.GetTimeStamp());
    }
#endif

    // Check to see if we've seen this event type before.
    // If not, then write the event metadata to the event stream first.
    unsigned int metadataId = GetMetadataId(*instance.GetEvent());
    if(metadataId == 0)
    {
        metadataId = GenerateMetadataId();

        EventPipeEventInstance* pMetadataInstance = EventPipe::BuildEventMetadataEvent(instance, metadataId);

        WriteToBlock(*pMetadataInstance, 0, 0, 0, TRUE); // metadataId=0 breaks recursion and represents the metadata event.

        SaveMetadataId(*instance.GetEvent(), metadataId);

        delete[] pMetadataInstance->GetData();
        delete pMetadataInstance;
    }

    WriteToBlock(instance, metadataId, captureThreadId, sequenceNumber, isSortedEvent);
}

void EventPipeFile::WriteSequencePoint(EventPipeSequencePoint* pSequencePoint)
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_ANY;
    }
    CONTRACTL_END;

    Flush(FlushAllBlocks);
    EventPipeSequencePointBlock sequencePointBlock(pSequencePoint);
    m_pSerializer->WriteObject(&sequencePointBlock);
}

void EventPipeFile::Flush(FlushFlags flags)
{
    // Write existing buffer to the stream/file regardless of whether it is full or not.
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_ANY;
    }
    CONTRACTL_END;
    // we write current blocks to the disk, whether they are full or not
    if ((m_pMetadataBlock->GetBytesWritten() != 0) && ((flags & FlushMetadataBlock) != 0))
    {
        m_pSerializer->WriteObject(m_pMetadataBlock);
        m_pMetadataBlock->Clear();
    }
    if ((m_pBlock->GetBytesWritten() != 0) && ((flags & FlushEventBlock) != 0))
    {
        m_pSerializer->WriteObject(m_pBlock);
        m_pBlock->Clear();
    }
}

void EventPipeFile::WriteEnd()
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_ANY;
    }
    CONTRACTL_END;

    Flush();

    // "After the last EventBlock is emitted, the stream is ended by emitting a NullReference Tag which indicates that there are no more objects in the stream to read."
    // see https://github.com/Microsoft/perfview/blob/master/src/TraceEvent/EventPipe/EventPipeFormat.md for more
    m_pSerializer->WriteTag(FastSerializerTags::NullReference);
}

void EventPipeFile::WriteToBlock(EventPipeEventInstance &instance, 
                                 unsigned int metadataId,
                                 ULONGLONG captureThreadId,
                                 unsigned int sequenceNumber,
                                 BOOL isSortedEvent)
{
    CONTRACTL
    {
        THROWS;
        GC_NOTRIGGER;
        MODE_ANY;
    }
    CONTRACTL_END;

    instance.SetMetadataId(metadataId);

    // If we are writing events we need to flush metadata as well because the
    // metadata we want to refer to might be in the pending metadata block
    FlushFlags flags = (metadataId == 0) ? FlushMetadataBlock : FlushAllBlocks;
    EventPipeEventBlockBase* pBlock = (metadataId == 0) ? 
        (EventPipeEventBlockBase*) m_pMetadataBlock : (EventPipeEventBlockBase*) m_pBlock;

    if (pBlock->WriteEvent(instance, captureThreadId, sequenceNumber, isSortedEvent))
        return; // the block is not full, we added the event and continue

    // we can't write this event to the current block (it's full)
    // so we write what we have in the block to the serializer
    Flush(flags);

    bool result = pBlock->WriteEvent(instance, captureThreadId, sequenceNumber, isSortedEvent);

    _ASSERTE(result == true); // we should never fail to add event to a clear block (if we do the max size is too small)
}

unsigned int EventPipeFile::GenerateMetadataId()
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_ANY;
    }
    CONTRACTL_END;

    // PAL does not support 32 bit InterlockedIncrement, so we are using the LONG version and cast to int
    // https://github.com/dotnet/coreclr/blob/master/src/pal/inc/pal.h#L4159
    // it's ok because the metadataId will never be bigger than 32 bit
    return (unsigned int)InterlockedIncrement(&m_metadataIdCounter);
}

unsigned int EventPipeFile::GetMetadataId(EventPipeEvent &event)
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_ANY;
    }
    CONTRACTL_END;

    unsigned int metadataId;
    if(m_pMetadataIds->Lookup(&event, &metadataId))
    {
        _ASSERTE(metadataId != 0);
        return metadataId;
    }

    return 0;
}

void EventPipeFile::SaveMetadataId(EventPipeEvent &event, unsigned int metadataId)
{
    CONTRACTL
    {
        THROWS;
        GC_NOTRIGGER;
        MODE_ANY;
        PRECONDITION(metadataId > 0);
    }
    CONTRACTL_END;

    // If a pre-existing metadata label exists, remove it.
    unsigned int oldId;
    if(m_pMetadataIds->Lookup(&event, &oldId))
        m_pMetadataIds->Remove(&event);

    // Add the metadata label.
    m_pMetadataIds->Add(&event, metadataId);
}

#endif // FEATURE_PERFTRACING
