using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using Microsoft.Dotnet.TestUtilities;

namespace CscBench
{
    public class TestRun
    {
        public TestRun()
        {
            Benchmarks = new List<Benchmark>();
            Configurations = new List<BenchmarkConfiguration>();
        }

        public string DotnetFrameworkVersion { get; set; }
        public string DotnetSdkVersion { get; set; }
        public string PrivateCoreCLRBinDirPath { get; set; }
        public Architecture Architecture { get; set; }
        public string IntermediateOutputDirPath { get; set; }
        public int Iterations { get; set; }
        public List<Benchmark> Benchmarks { get; }
        public List<BenchmarkConfiguration> Configurations { get; private set; }
        public DotNetSetupResult DotNetSetupResult { get; private set; }

        public async Task Run(ITestOutputHelper output)
        {
            await PrepareDotNet(output);
            await RunBenchmarks(output);
            DisplayBenchmarkResults(output);
        }


        async Task PrepareDotNet(ITestOutputHelper output)
        {
            DotNetSetup setup = new DotNetSetup(Path.Combine(IntermediateOutputDirPath, ".dotnet"))
                .WithRuntimeVersion(DotnetFrameworkVersion)
                .WithSdkVersion(DotnetSdkVersion)
                .WithArchitecture(Architecture);
            if(PrivateCoreCLRBinDirPath != null)
            {
                setup.WithPrivateRuntimeBinaryOverlay(PrivateCoreCLRBinDirPath);
            }
            DotNetSetupResult = await setup.Run(output);
        }

        async Task RunBenchmarks(ITestOutputHelper output)
        {
            foreach(Benchmark benchmark in Benchmarks)
            {
                await benchmark.Run(DotNetSetupResult, IntermediateOutputDirPath, Configurations, Iterations, output);
            }
        }
        
        void DisplayBenchmarkResults(ITestOutputHelper output)
        {
            StringBuilder columnHeader = new StringBuilder();
            columnHeader.Append("Benchmark                ");
            foreach(BenchmarkConfiguration config in Configurations)
            {
                columnHeader.AppendFormat("{0,15}", config.Name);
            }
            output.WriteLine(columnHeader.ToString());
            foreach(Benchmark benchmark in Benchmarks)
            {
                StringBuilder row = new StringBuilder();
                row.AppendFormat("{0,-25}", benchmark.Name);
                foreach(BenchmarkConfiguration config in Configurations)
                {
                    int[] timings = benchmark.IterationTimings[config];
                    int average = (int)timings.Average();
                    row.AppendFormat("{0,15}", average);
                }
                output.WriteLine(row.ToString());
            }
        }
    }
}
