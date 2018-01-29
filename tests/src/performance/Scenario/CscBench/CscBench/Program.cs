using System;
using System.Collections.Generic;
using System.Diagnostics.Tracing;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Diagnostics.Tracing;
using Microsoft.Diagnostics.Tracing.Parsers;
using Microsoft.Diagnostics.Tracing.Session;
using Microsoft.Dotnet.TestUtilities;
using Mono.Cecil;
using Mono.Cecil.Cil;
using Xunit.Abstractions;

namespace CscBench
{
    class Program
    {
        static void Main(string[] args)
        {
            AsyncMain(args).Wait();
        }

        public static async Task AsyncMain(string[] args)
        {
            ConsoleTestOutputHelper output = new ConsoleTestOutputHelper();
            CscTestSetup setup = await PrepareSetup(output);
            CscTestConfiguration[] configs = DetermineTestConfigurations(setup);

            foreach(var config in configs)
            {
                await MeasureCscIterations(config, output);
            }
            
            ExportResults(configs, output);
        }

        public class CscTestConfiguration
        {
            public string Name { get; set; }
            public CscTestSetup Setup { get; set; }
            public Dictionary<string, string> Environment { get; set; }
            public int[] IterationTimings { get; set; }
        }

        public class CscTestSetup
        {
            public string DotnetExePath { get; set; }
            public string CscBinaryPath { get; set; }
            public string CscWorkingDirPath { get; set; }
            public string CscCommandLineArgs { get; set; }
        }

        static async Task<CscTestSetup> PrepareSetup(ITestOutputHelper output)
        {
            using (IndentedTestOutputHelper setupOutput = new IndentedTestOutputHelper("Setup", output))
            {
                string workingDirectory = Path.Combine(Directory.GetCurrentDirectory(), @"bin\Debug\netcoreapp2.0");

                //acquire dotnet
                var downloadWork = AcquireDotNetWorkItem.DownloadSDK("2.0.0", RuntimeInformation.OSArchitecture, Path.Combine(workingDirectory, ".dotnet"));
                //await downloadWork.Run(setupOutput);

                //acquire csc source
                string cscSourceDownloadLink = "https://roslyninfra.blob.core.windows.net/perf-artifacts/CodeAnalysisRepro" +
                    (RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? ".zip" : ".tar.gz");
                string localCscSourceDir = Path.Combine(workingDirectory, "cscSource");
                //await FileTasks.DownloadAndUnzip(cscSourceDownloadLink, localCscSourceDir, output);
                string dotnetPath = downloadWork.LocalDotNetPath;
                string cscSourceDir = Path.Combine(localCscSourceDir, "CodeAnalysisRepro");

                // acquire csc binary
                // TODO: rewrite runtineconfig.json as needed
                string cscBinaryDirPath = Path.Combine(Path.GetDirectoryName(downloadWork.LocalDotNetPath), @"sdk\2.0.0\Roslyn");
                string localCscDir = Path.Combine(workingDirectory, "csc");
                //FileTasks.DirectoryCopy(cscBinaryDirPath, localCscDir, setupOutput);
                string cscPath = Path.Combine(localCscDir, "csc.exe");

                return new CscTestSetup()
                {
                    DotnetExePath = downloadWork.LocalDotNetPath,
                    CscBinaryPath = cscPath,
                    CscWorkingDirPath = cscSourceDir,
                    CscCommandLineArgs = "@repro.rsp"
                };
            }
        }

        public static CscTestConfiguration[] DetermineTestConfigurations(CscTestSetup setup)
        {
            Func<string, string, Dictionary<string, string>> EnvDictionary = (k, v) => {
                Dictionary<string, string> dict = new Dictionary<string, string>();
                dict.Add(k, v);
                return dict;
            };
            return new CscTestConfiguration[]
            {
                new CscTestConfiguration()
                {
                    Name = "Baseline",
                    Setup = setup
                },
                new CscTestConfiguration()
                {
                    Name = "Tiered",
                    Setup = setup,
                    Environment = EnvDictionary("COMPLUS_EXPERIMENTAL_TieredCompilation", "1")
                }
            };
        }

        static async Task MeasureCscIterations(CscTestConfiguration config, ITestOutputHelper output)
        {
            using (var runCscOutput = new IndentedTestOutputHelper("Run " + config.Name + " Csc Iterations", output))
            {
                config.IterationTimings = new int[10];
                for (int i = 0; i < config.IterationTimings.Length; i++)
                {
                    var cscProcess = new ProcessRunner(config.Setup.DotnetExePath, config.Setup.CscBinaryPath + " " + config.Setup.CscCommandLineArgs).
                          WithWorkingDirectory(config.Setup.CscWorkingDirPath).
                          WithEnvironment(config.Environment).
                          WithLog(runCscOutput);
                    await cscProcess.Run();
                    config.IterationTimings[i] = (int)(DateTime.Now - cscProcess.StartTime).TotalMilliseconds;
                }
            }
        }

        static void ExportResults(CscTestConfiguration[] configs, ITestOutputHelper output)
        {
            using (var resultsOutput = new IndentedTestOutputHelper("Results", output))
            {
                foreach(CscTestConfiguration config in configs)
                {
                    using (var configOutput = new IndentedTestOutputHelper(config.Name, resultsOutput))
                    {
                        for (int i = 0; i < config.IterationTimings.Length; i++)
                        {
                            configOutput.WriteLine(string.Format("Iteration {0,2}: {1,5}", i, config.IterationTimings[i]));
                        }
                    }
                }
            }
        }
    }
}
