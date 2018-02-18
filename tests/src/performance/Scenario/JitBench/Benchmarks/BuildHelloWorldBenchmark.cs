﻿using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Threading.Tasks;

namespace JitBench
{
    public class BuildHelloWorldBenchmark : Benchmark
    {
        public BuildHelloWorldBenchmark() : base("Dotnet_Build_HelloWorld") { }

        public override async Task Setup(DotNetInstallation dotNetInstall, string intermediateOutputDir, bool useExistingSetup, ITestOutputHelper output)
        {
            using (var setupSection = new IndentedTestOutputHelper("Setup " + Name, output))
            {
                await SetupHelloWorldProject(dotNetInstall.DotNetExe, intermediateOutputDir, useExistingSetup, setupSection);
            }
        }

        protected async Task SetupHelloWorldProject(string dotNetExePath, string intermediateOutputDir, bool useExistingSetup, ITestOutputHelper output)
        {
            string helloWorldProjectDir = Path.Combine(intermediateOutputDir, "helloworld");
            //the 'exePath' gets passed as an argument to dotnet.exe
            //in this case it isn't an executable at all, its a CLI command
            //a little cheap, but it works
            ExePath = "build";
            WorkingDirPath = helloWorldProjectDir;

            if(!useExistingSetup)
            {
                FileTasks.DeleteDirectory(helloWorldProjectDir, output);
                FileTasks.CreateDirectory(helloWorldProjectDir, output);
                await new ProcessRunner(dotNetExePath, "new console")
                    .WithWorkingDirectory(helloWorldProjectDir)
                    .WithLog(output)
                    .Run();
            }
        }
    }
}
