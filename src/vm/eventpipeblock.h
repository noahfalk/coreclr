// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

#ifndef __EVENTPIPE_BLOCK_H__
#define __EVENTPIPE_BLOCK_H__

#ifdef FEATURE_PERFTRACING

#include "eventpipeeventinstance.h"
#include "fastserializableobject.h"
#include "fastserializer.h"

// The base class for all file blocks in the Nettrace file format
// This class handles memory management to buffer the block data,
// bookkeeping, block version numbers, and serializing the data 
// to the file with correct alignment.
// Sub-classes decide the format of the block contents and how
// the blocks are named.
class EventPipeBlock : public FastSerializableObject
{
public:
    EventPipeBlock(unsigned int maxBlockSize, EventPipeSerializationFormat format = EventPipeNetTraceFormatV4);
    ~EventPipeBlock();

    void Clear();

    unsigned int GetBytesWritten() const
    {
        return m_pBlock == nullptr ? 0 : (unsigned int)(m_pWritePointer - m_pBlock);
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

        unsigned int dataSize = GetBytesWritten();
        pSerializer->WriteBuffer((BYTE *)&dataSize, sizeof(dataSize));

        if (dataSize == 0)
            return;

        unsigned int requiredPadding = pSerializer->GetRequiredPadding();
        if (requiredPadding != 0)
        {
            BYTE maxPadding[ALIGNMENT_SIZE - 1] = {}; // it's longest possible padding, we are going to use only part of it
            pSerializer->WriteBuffer(maxPadding, requiredPadding); // we write zeros here, the reader is going to always read from the first aligned address of the serialized content

            _ASSERTE(pSerializer->HasWriteErrors() || (pSerializer->GetRequiredPadding() == 0));
        }

        pSerializer->WriteBuffer(m_pBlock, dataSize);
    }

protected:
    BYTE *m_pBlock;
    BYTE *m_pWritePointer;
    BYTE *m_pEndOfTheBuffer;
    EventPipeSerializationFormat m_format;

    unsigned int GetSize() const
    {
        LIMITED_METHOD_CONTRACT;
        return m_pBlock == nullptr ? 0 : (unsigned int)(m_pEndOfTheBuffer - m_pBlock);
    }
};

// The base type for blocks that contain events (EventBlock and EventMetadataBlock)
class EventPipeEventBlockBase : public EventPipeBlock
{
public:
    EventPipeEventBlockBase(unsigned int maxBlockSize, EventPipeSerializationFormat format);

    // Write an event to the block.
    // Returns:
    //  - true: The write succeeded.
    //  - false: The write failed.  In this case, the block should be considered full.
    bool WriteEvent(EventPipeEventInstance &instance);

};

class EventPipeEventBlock : public EventPipeEventBlockBase
{
public:
    EventPipeEventBlock(unsigned int maxBlockSize, EventPipeSerializationFormat format);

    const char *GetTypeName() override
    {
        LIMITED_METHOD_CONTRACT;
        return "EventBlock";
    }
};

class EventPipeMetadataBlock : public EventPipeEventBlockBase
{
public:
    EventPipeMetadataBlock(unsigned int maxBlockSize);

    const char *GetTypeName() override
    {
        LIMITED_METHOD_CONTRACT;
        return "MetadataBlock";
    }
};

#endif // FEATURE_PERFTRACING

#endif // __EVENTPIPE_BLOCK_H__
