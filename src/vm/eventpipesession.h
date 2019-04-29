// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

#ifndef __EVENTPIPE_SESSION_H__
#define __EVENTPIPE_SESSION_H__

#ifdef FEATURE_PERFTRACING

class EventPipeSessionProviderList;
class EventPipeSessionProvider;

enum class EventPipeSessionType
{
    File,       // EventToFile
    Streaming,  // EventToEventListener
    IpcStream   // EventToIpc
};

enum EventPipeSerializationFormat
{
    // Default format used in .Net Core 2.0-3.0 Preview 5
    // Remains the default format .Net Core 3.0 when used with
    // private EventPipe managed API via reflection.
    // This format had limited official exposure in documented
    // end-user scenarios, but it is supported by PerfView,
    // TraceEvent, and was used by AI profiler
    EventPipeNetPerfFormatV3,

    // Default format used in .Net Core 3 Preview6+
    //
    EventPipeNetTraceFormatV4,

    EventPipeFormatCount
};

class EventPipeSession
{
private:
    // The set of configurations for each provider in the session.
    EventPipeSessionProviderList *m_pProviderList;

    // The configured size of the circular buffer.
    size_t m_circularBufferSizeInBytes;

    // True if rundown is enabled.
    Volatile<bool> m_rundownEnabled;

    // The type of the session.
    // This determines behavior within the system (e.g. policies around which events to drop, etc.)
    EventPipeSessionType m_sessionType;

    // For file/IPC sessions this controls the format emitted. For in-proc EventListener it is
    // irrelevant.
    EventPipeSerializationFormat m_format;

    // Start date and time in UTC.
    FILETIME m_sessionStartTime;

    // Start timestamp.
    LARGE_INTEGER m_sessionStartTimeStamp;

public:

    // TODO: This needs to be exposed via EventPipe::CreateSession() and EventPipe::DeleteSession() to avoid memory ownership issues.
    EventPipeSession(
        EventPipeSessionType sessionType,
        EventPipeSerializationFormat format,
        unsigned int circularBufferSizeInMB,
        const EventPipeProviderConfiguration *pProviders,
        uint32_t numProviders);
    ~EventPipeSession();

    // Determine if the session is valid or not.  Invalid sessions can be detected before they are enabled.
    bool IsValid() const;

    // Get the session type.
    EventPipeSessionType GetSessionType() const
    {
        LIMITED_METHOD_CONTRACT;
        return m_sessionType;
    }

    // Get the format version used by the file/IPC serializer
    EventPipeSerializationFormat GetSerializationFormat() const
    {
        LIMITED_METHOD_CONTRACT;
        return m_format;
    }

    // Get the configured size of the circular buffer.
    size_t GetCircularBufferSize() const
    {
        LIMITED_METHOD_CONTRACT;
        return m_circularBufferSizeInBytes;
    }

    // Determine if rundown is enabled.
    bool RundownEnabled() const
    {
        LIMITED_METHOD_CONTRACT;
        return m_rundownEnabled;
    }

    // Set the rundown enabled flag.
    void SetRundownEnabled(bool value)
    {
        LIMITED_METHOD_CONTRACT;
        m_rundownEnabled = value;
    }

    // Get the session start time in UTC.
    FILETIME GetStartTime() const
    {
        LIMITED_METHOD_CONTRACT;
        return m_sessionStartTime;
    }

    // Get the session start timestamp.
    LARGE_INTEGER GetStartTimeStamp() const
    {
        LIMITED_METHOD_CONTRACT;
        return m_sessionStartTimeStamp;
    }

    // Add a new provider to the session.
    void AddSessionProvider(EventPipeSessionProvider *pProvider);

    // Get the session provider for the specified provider if present.
    EventPipeSessionProvider* GetSessionProvider(EventPipeProvider *pProvider);
};

#endif // FEATURE_PERFTRACING

#endif // __EVENTPIPE_SESSION_H__
