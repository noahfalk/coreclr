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
                await SetupSourceToCompile(intermediateOutputDir, dotNetInstall.RuntimeDirPath, setupSection);
            }
        }

        protected void SetupCscBinDir(string sdkDirPath, string runtimeVersion, string intermediateOutputDir, ITestOutputHelper output)
        {
            // copy the SDK version of csc into a private directory so we can safely retarget it
            string cscBinaryDirPath = Path.Combine(sdkDirPath, "Roslyn", "bincore");
            string localCscDir = Path.Combine(intermediateOutputDir, "csc");
            FileTasks.DirectoryCopy(cscBinaryDirPath, localCscDir, output);
            ExePath = Path.Combine(localCscDir, "csc.dll");

            //overwrite csc.runtimeconfig.json to point at the runtime version we want to use
            string runtimeConfigPath = Path.Combine(localCscDir, "csc.runtimeconfig.json");
            File.Delete(runtimeConfigPath);
            File.WriteAllLines(runtimeConfigPath, new string[] {
                "{",
                "  \"runtimeOptions\": {",
                "    \"tfm\": \"netcoreapp2.0\",",
                "    \"framework\": {",
                "        \"name\": \"Microsoft.NETCore.App\",",
                "        \"version\": \"" + runtimeVersion + "\"",
                "    }",
                "  }",
                "}"
            });
        }

        protected abstract Task SetupSourceToCompile(string intermediateOutputDir, string runtimeDirPath, ITestOutputHelper output);
    }
}
