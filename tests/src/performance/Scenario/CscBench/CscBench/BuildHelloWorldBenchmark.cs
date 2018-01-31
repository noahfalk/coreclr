using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using Microsoft.Dotnet.TestUtilities;

namespace CscBench
{
    public class BuildHelloWorldBenchmark : Benchmark
    {
        public BuildHelloWorldBenchmark() : base("Dotnet Build HelloWorld") { }

        protected override async Task Setup(DotNetSetupResult dotNetInstall, string intermediateOutputDir, ITestOutputHelper output)
        {
            using (var setupSection = new IndentedTestOutputHelper("Setup " + Name, output))
            {
                await SetupHelloWorldProject(dotNetInstall.DotNetExePath, intermediateOutputDir, setupSection);
            }
        }

        protected async Task SetupHelloWorldProject(string dotNetExePath, string intermediateOutputDir, ITestOutputHelper output)
        {
            string helloWorldProjectDir = Path.Combine(intermediateOutputDir, "helloworld");
            if(!Directory.Exists(helloWorldProjectDir))
            {
                Directory.CreateDirectory(helloWorldProjectDir);
            }
            await new ProcessRunner(dotNetExePath, "new console")
                .WithWorkingDirectory(helloWorldProjectDir)
                .WithLog(output)
                .Run();

            //the 'exePath' gets passed as an argument to dotnet.exe
            //in this case it isn't an executable at all, its a CLI command
            //a little cheap, but it works
            ExePath = "build";
            WorkingDirPath = helloWorldProjectDir;
        }
    }
}
