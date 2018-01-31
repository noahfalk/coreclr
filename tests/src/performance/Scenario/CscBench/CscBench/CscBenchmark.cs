using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using Microsoft.Dotnet.TestUtilities;

namespace CscBench
{
    public abstract class CscBenchmark : Benchmark
    {
        public CscBenchmark(string name) : base(name) { }

        protected override async Task Setup(DotNetSetupResult dotNetInstall, string intermediateOutputDir, ITestOutputHelper output)
        {
            using (var setupSection = new IndentedTestOutputHelper("Setup " + Name, output))
            {
                SetupCscBinDir(dotNetInstall.SdkDirPath, dotNetInstall.RuntimeVersion, intermediateOutputDir, setupSection);
                await SetupSourceToCompile(intermediateOutputDir, setupSection);
            }
        }

        protected void SetupCscBinDir(string sdkDirPath, string runtimeVersion, string intermediateOutputDir, ITestOutputHelper output)
        {
            // TODO: rewrite runtineconfig.json as needed
            string cscBinaryDirPath = Path.Combine(sdkDirPath, "Roslyn");
            string localCscDir = Path.Combine(intermediateOutputDir, "csc");
            FileTasks.DirectoryCopy(cscBinaryDirPath, localCscDir, output);
            ExePath = Path.Combine(localCscDir, "csc.exe");
        }

        protected abstract Task SetupSourceToCompile(string intermediateOutputDir, ITestOutputHelper output);
    }
}
