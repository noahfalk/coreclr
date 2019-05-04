// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

#ifndef __EVENTPIPE_BLOCK_H__
#define __EVENTPIPE_BLOCK_H__

#ifdef FEATURE_PERFTRACING

#include "eventpipeeventinstance.h"
#include "fastserializableobject.h"
#include "fastserializer.h"

class EventPipeBlock final : public FastSerializableObject
{
public:
    EventPipeBlock(unsigned int maxBlockSize, EventPipeSerializationFormat format);
    ~EventPipeBlock();

    // Write an event to the block.
    // Returns:
    //  - true: The write succeeded.
    //  - false: The write failed.  In this case, the block should be considered full.
    bool WriteEvent(EventPipeEventInstance &instance);
    void SetSequencePointBit();
    void Clear();

    const char *GetTypeName() override
    {
        LIMITED_METHOD_CONTRACT;
        return "EventBlock";
    }

    unsigned int GetHeaderSize() const
    {
        if (m_format == EventPipeNetPerfFormatV3)
        {
            return 0;
        }
        else if (m_format == EventPipeNetTraceFormatV4)
        {
            return 12;
        }
        else
        {
            _ASSERTE(!"Invalid EventPipe serialization format");
            return 0;
        }
    }

    void FastSerializeHeader(FastSerializer *pSerializer)
    {
        // Write the V4 EventPipeBlock header
        // This needs to match the layout that was described in EventPipeFile
        // See EventPipeFile::FastSerialize
        if (m_format == EventPipeNetTraceFormatV4)
        {
            pSerializer->WriteBuffer((BYTE*)&m_threadId, sizeof(m_threadId)); //offset 0
            pSerializer->WriteBuffer((BYTE*)&m_flags, sizeof(m_flags));       //offset 8
        }
    }

    void FastSerialize(FastSerializer *pSerializer) override
    {
        CONTRACTL
        {
            NOTHROW;
            GC_NOTRIGGER;
            MODE_PREEMPTIVE;
            PRECONDITION(pSerializer != NULL);
        }
        CONTRACTL_END;

        if (m_pBlock == NULL)
            return;

        unsigned int eventsSize = (unsigned int)(m_pWritePointer - m_pBlock);
        unsigned int headerSize = GetHeaderSize();
        if (eventsSize == 0)
        {
            _ASSERTE(!"We shouldn't be asked to write 0 size EventBlocks");
            pSerializer->WriteBuffer((BYTE *)&eventsSize, sizeof(eventsSize));
            return;
        }
        else
        {
            unsigned int bufferSize = headerSize + eventsSize;
            pSerializer->WriteBuffer((BYTE *)&bufferSize, sizeof(bufferSize));
        }

        size_t currentPosition = pSerializer->GetCurrentPosition();
        if (currentPosition % ALIGNMENT_SIZE != 0)
        {
            BYTE maxPadding[ALIGNMENT_SIZE - 1] = {}; // it's longest possible padding, we are going to use only part of it
            unsigned int paddingLength = ALIGNMENT_SIZE - (currentPosition % ALIGNMENT_SIZE);
            pSerializer->WriteBuffer(maxPadding, paddingLength); // we write zeros here, the reader is going to always read from the first aligned address of the serialized content

            _ASSERTE(pSerializer->HasWriteErrors() || (pSerializer->GetCurrentPosition() % ALIGNMENT_SIZE == 0));
        }

        //Write the header if needed
        FastSerializeHeader(pSerializer);

        pSerializer->WriteBuffer(m_pBlock, eventsSize);
    }

private:
    BYTE *m_pBlock;
    BYTE *m_pWritePointer;
    BYTE *m_pEndOfTheBuffer;
    EventPipeSerializationFormat m_format;
    ULONGLONG m_threadId;

    enum EventPipeBlockFlags
    {
        IsSequencePoint = 0x1
    };
    DWORD m_flags;

    unsigned int GetSize() const
    {
        LIMITED_METHOD_CONTRACT;
        return m_pBlock == nullptr ? 0 : (unsigned int)(m_pEndOfTheBuffer - m_pBlock);
    }
};

#endif // FEATURE_PERFTRACING

#endif // __EVENTPIPE_BLOCK_H__
