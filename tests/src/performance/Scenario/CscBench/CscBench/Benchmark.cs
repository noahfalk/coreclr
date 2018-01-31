using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;
using Microsoft.Dotnet.TestUtilities;

namespace CscBench
{
    public abstract class Benchmark
    {
        public Benchmark(string name)
        {
            Name = name;
            IterationTimings = new Dictionary<BenchmarkConfiguration, int[]>();
        }

        public string Name { get; private set; }
        public string ExePath { get; protected set; }
        public string WorkingDirPath { get; protected set; }
        public string CommandLineArguments { get; protected set; }

        public Dictionary<BenchmarkConfiguration, int[]> IterationTimings { get; private set; }

        public async Task Run(DotNetSetupResult dotnetInstall, string intermediateOutputDir, IEnumerable<BenchmarkConfiguration> configs, int iterations, ITestOutputHelper output)
        {
            await Setup(dotnetInstall, intermediateOutputDir, output);
            await MeasureIterations(dotnetInstall.DotNetExePath, configs, iterations, output);
        }

        protected abstract Task Setup(DotNetSetupResult dotnetInstall, string intermediateOutputDir, ITestOutputHelper output);


        async Task MeasureIterations(string dotNetExePath, IEnumerable<BenchmarkConfiguration> configs, int iterations, ITestOutputHelper output)
        {
            foreach(BenchmarkConfiguration config in configs)
            {
                int[] timings = await MeasureIterations(dotNetExePath, config, iterations, output);
                IterationTimings.Add(config, timings);
            }
        }

        async Task<int[]> MeasureIterations(string dotNetExePath, BenchmarkConfiguration config, int iterations, ITestOutputHelper output)
        {
            int[] iterationTimings = new int[iterations];
            int warmupIterations = 1;
            using (var runOutput = new IndentedTestOutputHelper("Run " + Name + " [" + config.Name + "] Iterations", output))
            {
                for (int i = 0; i < warmupIterations; i++)
                {
                    await MeasureIteration(dotNetExePath, config, output);
                }
                for (int i = 0; i < iterationTimings.Length; i++)
                {
                    iterationTimings[i] = await MeasureIteration(dotNetExePath, config, output);
                }
            }
            return iterationTimings;
        }

        async Task<int> MeasureIteration(string dotnetExePath, BenchmarkConfiguration config, ITestOutputHelper output)
        {
            var benchmarkProcess = new ProcessRunner(dotnetExePath, ExePath + " " + CommandLineArguments).
                          WithWorkingDirectory(WorkingDirPath).
                          WithEnvironment(config.EnvironmentVariables).
                          WithLog(output);
            await benchmarkProcess.Run();
            return (int)(DateTime.Now - benchmarkProcess.StartTime).TotalMilliseconds;
        }
    }
}
