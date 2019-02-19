# Evolving EventCounter

When EventCounter was first designed, it was tailored towards aggregating a set of events that can each be represented as a single number, and then summarizing that as a set of statistics made available to one client. It works well for that purpose, but now we need it to do more:

1. We'd like to use it from multiple clients. Right now when multiple clients try to use EventCounter the statistics get produced at whatever aggregation interval was specified by the most recent client to specify one. The ideal outcome is that each client is unaware of any other client. An acceptable outcome is that each client understands how to get the results it wants despite interference from other clients.
2. [Simple viewers] - These viewers only know how to display name-value pairs (textually or in a simple plot over time). Given a set of statistics per counter there must be a way to produce a single canonical value that gets displayed.
3. [Simple viewers] - It is useful to have both a simple name that is compact and has no spaces in it for manipulation on the command line as well as a more descriptive name that can be shown in the UI. Right now counters have only one name, and naming conventions for it aren't specified.
4. We want a rate counter, for example "Exceptions Thrown Per Second." The developer specifies the timescale but the counter-viewer specifies the aggregation interval so scaling needs to occur. For example the user could ask for hourly reports of the "Exceptions Thrown Per Second" counter and something needs to compute # of exceptions in that hour * 1/3600.
5. We want to render counters where there is no pre-existing control-flow that occurs at convenient discrete intervals. For example getting the % of CPU used is time-varying function, but there is no OnCpuUsageUpdated() API. A developer could always emulate one by polling a query function, but they wouldn't know what is an efficient rate to poll that balances counter accuracy vs. performance overhead.



## Assumptions that guide our solution design space:

1. We will preserve both source and binary compat for the EventCounter managed API.
2. We will preserve the correct operation of the PerfView counter viewer GUI. It is however acceptable to require users to update to a newer version of PerfView to achieve this.
3. We do not have to preserve protocol compatibility within the event stream.
4. All our event transports will support enumerating filter data from all clients. This isn't true today, but if we can't get there we should give up or scope back on our multi-client goal. Imagine client1 sends filter data that enables counters and client2 enables the same provider without any filter data. If client1's filter data becomes unavailable because of client2's action then the client1 will not receive any counter events as it was supposed to.

## Design


### Multi-client support Options ###

The multi-client goal has probably been our thorniest problem (maybe for me at least, Vance didn't seem so concerned by it ;) The original EventCounter design allows clients to request the time interval over which counter data is aggregated. For example client A could request to receive # of exceptions per second, aggregated at 10 second intervals. The reported value 6 would mean that 60 exceptions had occured in the 10 second window since the last report. Imagine client B now requests to get events aggregated once per hour. Switching to report values every hour instead of every 10 seconds would deprive A of the data it needs. What could we do instead?

**Emit data at the smallest interval requested by any client** - This one is doable, but it has some minor issues at least:

1. It adds complexity to all clients. No client can be assured it will get the rate it asked for, thus it must observe the rate the data is being generated and post-process it. This also requires the runtime to include the aggregation function in the event stream metadata if we support counters that don't all implicitly use the same function.
2. There is some necessary approximation error added to a client side aggregation if the fine grain interval does not precisely divide the course grain interval. For example if one client asks for data every 60 seconds and another asks for it every 45 seconds, the 45 second chunks will need to be split into 15 second chunks that are presumed uniform and then combined into a 60 second chunk.
3. Not all statistical functions are aggregable. The most common ones we are targetting now are (sums, averages, maybe min/max), but there are other commonly used performance metrics such as the Nth percentile of a group that can not be accurately computed this way. We've defined these fancier statistics as out-of-scope for now, but we wouldn't be able to get there in the future without abandoning this technique and picking a different one.

**Emit data at a fixed interval (example 1 second)** - This avoids some of the complexity of configurable intervals in both the runtime and in viewers, and avoids any issues with intervals that don't divide each other. However the more I've thought about it I'm bothered that it creates a minimum threshold of activity in a process that might otherwise be completely inactive. Picking larger numbers mitigate that issue, but then make interactive viewer tools feel very unresponsive.

**Emit data per-session at exactly the rate each client asked for it** - This one would be ideal for clients, but it is unclear if the OS tracing APIs provide enough capability to do it. EventPipe/EventListener we own end-to-end so its just work, but my experiments with ETW have not been fruitful so far. In particular the EventWriteEx() function has a filter parameter that is demonstrably capable of sending per-session events, but I hit a dead-end trying to determine how to calculate which bit vector corresponds to which session. Its possible inspecting windows source will reveal a path, but looking at some of the weird registry lookups we are doing to get the filter data and session ID in the first place didn't inspire confidence that we were on solid ground.

**Emit data to all sessions at the rates requested by all clients** - This requires a little extra complexity in the runtime to maintain potentially multiple concurrent aggregations, and it is more verbose in the event stream if that is occuring. Clients need to filter out responses that don't match their requested rate, which is a little more complex than ideal, but still much simpler than needing to synthesize any statistics.


### Multi-client support Proposal ###

My current proposal is that we do the last option - emit the super-set of all requested aggregation rates into the event stream. My expectation is that in the common case there will only be one client which has no overhead. In the case of multiple clients we can still encourage people to use a few canonical rates such as per-second, per-10 seconds, per-minute, per-hour which makes it likely that similar use cases will be able to share the exact same set of events. In the worst case that a few different aggregations are happening in parallel the overhead of our common counter aggregations shouldn't be that high, otherwise they weren't very suitable for lightweight monitoring in the first place. In terms of runtime code complexity I think the difference between supporting 1 aggregation and N aggregations is probably <50 lines per counter type and we only have a few counter types.


### API design options ###

There are bunch of things above that touch the API, so I am bundling them all here. A few requirements:
Goal 3 - to have multiple names - requires that we add an extra string somewhere
Goal 4 - to have rate counters - requires that the developer can specify which kind of counter it is and what the rate is (per-second, per-hour, etc)
Goal 5 - we need a way for the counter infrastructure to poll at an appropriate rate


**Add everything to EventCounter** - The path we were on was to keep adding things onto the constructor which might now look like this:

    EventCounter(EventSource eventSource, 
                 string simpleName,
                 string descriptiveName,
                 bool isRateCounter,
                 TimeSpan rateCounterTimeScale,
                 Func<float> getMetricFunction)

I think that would be unwieldy for several reasons:

1. We can't easily add more types of counters in the future. Adding a bool for every option doesn't scale. Even if we replace bool with enumeration it wouldn't have handled rate counter case which needed another parameter to specify the time scale.
2. The getMetricFunction is defined as Func<float\> to handle the case where we need to report a floating point value (example % CPU usage), but that is a bit of an odd type to use for something that is dealing with integer counts.
3. Its unclear what to do with rateCounterTimeScale when the EventCounter isn't a rate counter.
4. Assuming you use the counter as a rate counter, WriteMetric(1) feels like an odd name for a method that increments a counter.
5. getMetricFunction is mutually exclusive with WriteMetric(), but they are both available on the same type. 

Ultimately we've got one object representing 4 different workflows (increment counter, poll counter, add number to a set, poll a number) and the only part that is common between them is specifying the names+EventSource.


 **More top-level types**

Instead of putting everything into EventCounter, we could have other types of counters that still follow the same publishing rules and likely share a base class:

    class EventCounter {
        EventCounter(EventSource eventSource,
                     string simpleName,
                     string descriptiveName);
        WriteMetric(float metric);
    }

    class PollingEventCounter {
        PollingEventCounter(EventSource eventSource,
                            string simpleName,
                            string descriptiveName,
                            Func<float> getMetricFunction);
    }

    class RateCounter {
        RateCounter(EventSource eventSource,
                    string simpleName,
                    string descriptiveName,
                    TimeSpan timeScale);
        Increment(long additionalCount = 1);
    }

    class PollingRateCounter {
        PollingRateCounter(EventSource eventSource,
                            string simpleName,
                            string descriptiveName,
                            TimeSpan timeScale,
                            Func<long> getCountFunction);
    }
    
Although the API appears a bit more verbose, I'd argue it has the same number of concepts, just now distinctly named and grouped. It also provides an easy path to creating MinCounter, MaxCounter, NthPercentileCounter, or any other esoteric counter we might desire in the future.


### Canonicalizing a single value output per counter ###

Define the Mean to be the canonical value of the set of statistics. For most of the counter cases we've only got 1 value X being inefficiently recorded as Min=X, Max=X, Mean=X, Sum=X, Count=1, StdDev=0.


### EventStream format

We need to add "DescriptiveName" to the payload, however that shouldn't break any tools that were searching the fields by name. It feels like a bit of a waste to encode single values as a set of six stats, but the data rates are low enough that it shouldn't matter. If it ever did we could add some marker to the filter data indicating the client understood a more compact encoding and the runtime could  compact it when all clients indicated support.
