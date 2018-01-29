using System;
using System.Diagnostics.Tracing;
using System.Threading;

namespace CscIterator
{
    class Program
    {
        static int Main(string[] args)
        {
            int ret = 0;
            MainIteratorEventSource evtSource = new MainIteratorEventSource();
            for (int i = 0; i < 100; i++)
            {
                evtSource.BeginMainIteration(i);
                Console.WriteLine("Begin iteration " + i);
                ret = Microsoft.CodeAnalysis.CSharp.CommandLine.Program.Main(args);
                evtSource.EndMainIteration(i);
                Console.WriteLine("End iteration " + i);
            }
            return ret;
        }
    }


    [EventSource(Name = "MainIterator")]
    public class MainIteratorEventSource : EventSource
    {
        [Event(1)]
        public void BeginMainIteration(int index)
        {
            WriteEvent(1, index);
        }

        [Event(2)]
        public void EndMainIteration(int index)
        {
            WriteEvent(2, index);
        }
    }
}
