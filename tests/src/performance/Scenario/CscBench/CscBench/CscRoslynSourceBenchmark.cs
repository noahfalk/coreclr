using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using Microsoft.Dotnet.TestUtilities;

namespace CscBench
{
    class CscRoslynSourceBenchmark : CscBenchmark
    {
        public CscRoslynSourceBenchmark() : base("Csc Roslyn Source")
        {
        }

        protected override async Task SetupSourceToCompile(string intermediateOutputDir, string runtimeDirPath, ITestOutputHelper output)
        {
            string cscSourceDownloadLink = "https://roslyninfra.blob.core.windows.net/perf-artifacts/CodeAnalysisRepro" +
                    (RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? ".zip" : ".tar.gz");
            string sourceDownloadDir = Path.Combine(intermediateOutputDir, "roslynSource");
            await FileTasks.DownloadAndUnzip(cscSourceDownloadLink, sourceDownloadDir, output);
            string sourceDir = Path.Combine(sourceDownloadDir, "CodeAnalysisRepro");

            CommandLineArguments = "@repro.rsp";
            WorkingDirPath = sourceDir;
        }
    }
}
