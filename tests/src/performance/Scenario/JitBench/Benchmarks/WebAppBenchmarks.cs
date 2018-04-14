﻿using System;
using System.Collections.Generic;
using System.IO;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using Microsoft.Xunit.Performance.Api;

namespace JitBench
{
    class MusicStoreBenchmark : WebAppBenchmark
    {
        public MusicStoreBenchmark() : base("MusicStore", "MusicStore.dll") { }

        protected override string GetJitBenchRepoRootDir(string outputDir)
        {
            return Path.Combine(outputDir, "K");
        }

        protected override string GetWebAppSrcDirectory(string outputDir)
        {
            return Path.Combine(GetJitBenchRepoRootDir(outputDir), "src", "MusicStore");
        }
    }

    class AllReadyBenchmark : WebAppBenchmark
    {
        public AllReadyBenchmark() : base("AllReady", "AllReady.dll") { }

        protected override string GetJitBenchRepoRootDir(string outputDir)
        {
            return Path.Combine(outputDir, "J");
        }

        protected override string GetWebAppSrcDirectory(string outputDir)
        {
            return Path.Combine(GetJitBenchRepoRootDir(outputDir), "src", "AllReady");
        }
    }

    abstract class WebAppBenchmark : Benchmark
    {
        private static readonly HashSet<int> DefaultExitCodes = new HashSet<int>(new[] { 0 });

        public WebAppBenchmark(string name, string executableName) : base(name)
        {
            ExePath = executableName;
        }

        public override async Task Setup(DotNetInstallation dotNetInstall, string outputDir, bool useExistingSetup, ITestOutputHelper output)
        {
            if(!useExistingSetup)
            {
                using (var setupSection = new IndentedTestOutputHelper("Setup " + Name, output))
                {
                    await CloneAspNetJitBenchRepo(outputDir, setupSection);

                    //we need aspnetcore-dev feed to install asp.net daily builds. We could push this change upstream into the JitBench repo
                    //but it was logistically easier to just overwrite NuGet.config for now.
                    if(dotNetInstall.MicrosoftAspNetCoreVersion.Contains("preview"))
                    {
                        OverwriteNuGetConfig(outputDir, setupSection);
                    }
                    await Publish(dotNetInstall, outputDir, setupSection);
                }
            }

            string tfm = DotNetSetup.GetTargetFrameworkMonikerForFrameworkVersion(dotNetInstall.MicrosoftNetCoreAppVersion);
            WorkingDirPath = GetWebAppPublishDirectory(dotNetInstall, outputDir, tfm);
        }

        async Task CloneAspNetJitBenchRepo(string outputDir, ITestOutputHelper output)
        {
            // If the repo already exists, we delete it and extract it again.
            string jitBenchRepoRootDir = GetJitBenchRepoRootDir(outputDir);
            FileTasks.DeleteDirectory(jitBenchRepoRootDir, output);

            await ExecuteGitCommand($"clone {JitBenchRepoUrl} {jitBenchRepoRootDir}", output);
            await ExecuteGitCommand($"checkout {JitBenchCommitSha1Id}", output, workingDirectory: jitBenchRepoRootDir);
            await ExecuteGitCommand($"submodule update --init --recursive", output, workingDirectory: jitBenchRepoRootDir);
        }

        async Task ExecuteGitCommand(string arguments, ITestOutputHelper output, string workingDirectory = null)
        {
            int exitCode = await new ProcessRunner("git", arguments).WithLog(output).WithWorkingDirectory(workingDirectory).Run();

            if (!DefaultExitCodes.Contains(exitCode))
                throw new Exception($"git {arguments} has failed, the exit code was {exitCode}");
        }

        void OverwriteNuGetConfig(string outputDir, ITestOutputHelper output)
        {
            string srcDir = GetWebAppSrcDirectory(outputDir);
            if(!Directory.Exists(srcDir))
            {
                throw new Exception("Unable to find " + srcDir);
            }
            string nugetConfigPath = Path.Combine(srcDir, "NuGet.config");
            string nugetConfigText =
@"<?xml version=""1.0"" encoding=""utf-8""?>
  <configuration>
    <packageSources>
      <clear/>
      <add key=""aspnet core dev"" value=""https://dotnet.myget.org/F/aspnetcore-dev/api/v3/index.json"" />
      <add key=""dotnet core"" value=""https://dotnet.myget.org/F/dotnet-core/api/v3/index.json"" />
      <add key=""NuGet"" value=""https://api.nuget.org/v3/index.json""/>
    </packageSources>
  </configuration>
";
            output.WriteLine("Overwriting " + nugetConfigPath + " to add aspnetcore-dev feed");
            File.WriteAllText(nugetConfigPath, nugetConfigText);
        }

        private async Task<string> Publish(DotNetInstallation dotNetInstall, string outputDir, ITestOutputHelper output)
        {
            string tfm = DotNetSetup.GetTargetFrameworkMonikerForFrameworkVersion(dotNetInstall.MicrosoftNetCoreAppVersion);
            string publishDir = GetWebAppPublishDirectory(dotNetInstall, outputDir, tfm);
            if (publishDir != null)
            {
                FileTasks.DeleteDirectory(publishDir, output);
            }
            string dotNetExePath = dotNetInstall.DotNetExe;
            await new ProcessRunner(dotNetExePath, $"publish -c Release -f {tfm}")
                .WithWorkingDirectory(GetWebAppSrcDirectory(outputDir))
                .WithEnvironmentVariable("DOTNET_MULTILEVEL_LOOKUP", "0")
                .WithEnvironmentVariable("JITBENCH_ASPNET_VERSION", dotNetInstall.MicrosoftAspNetCoreVersion)
                .WithEnvironmentVariable("JITBENCH_TARGET_FRAMEWORK_MONIKER", tfm)
                .WithEnvironmentVariable("JITBENCH_FRAMEWORK_VERSION", dotNetInstall.MicrosoftNetCoreAppVersion)
                .WithEnvironmentVariable("UseSharedCompilation", "false")
                .WithLog(output)
                .Run();

            publishDir = GetWebAppPublishDirectory(dotNetInstall, outputDir, tfm);
            if (publishDir == null)
            {
                throw new DirectoryNotFoundException("Could not find 'publish' directory");
            }
            return publishDir;
        }

        public override Metric[] GetDefaultDisplayMetrics()
        {
            return new Metric[]
            {
                StartupMetric,
                FirstRequestMetric,
                MedianResponseMetric
            };
        }

        protected override IterationResult RecordIterationMetrics(ScenarioExecutionResult scenarioIteration, string stdout, string stderr, ITestOutputHelper output)
        {
            IterationResult result = base.RecordIterationMetrics(scenarioIteration, stdout, stderr, output);
            AddConsoleMetrics(result, stdout, output);
            return result;
        }

        void AddConsoleMetrics(IterationResult result, string stdout, ITestOutputHelper output)
        {
            output.WriteLine("Processing iteration results.");

            double? startupTime = null;
            double? firstRequestTime = null;
            double? steadyStateMedianTime = null;

            using (var reader = new StringReader(stdout))
            {
                string line;
                while ((line = reader.ReadLine()) != null)
                {
                    Match match = Regex.Match(line, @"^Server start \(ms\): \s*(\d+)\s*$");
                    if (match.Success && match.Groups.Count == 2)
                    {
                        startupTime = Convert.ToDouble(match.Groups[1].Value);
                        continue;
                    }

                    match = Regex.Match(line, @"^1st Request \(ms\): \s*(\d+)\s*$");
                    if (match.Success && match.Groups.Count == 2)
                    {
                        firstRequestTime = Convert.ToDouble(match.Groups[1].Value);
                        continue;
                    }

                    //the steady state output chart looks like:
                    //   Requests    Aggregate Time(ms)    Req/s   Req Min(ms)   Req Mean(ms)   Req Median(ms)   Req Max(ms)   SEM(%)
                    // ----------    ------------------    -----   -----------   ------------   --------------   -----------   ------
                    //    2-  100                 5729   252.60          3.01           3.96             3.79          9.81     1.86
                    //  101-  250                 6321   253.76          3.40           3.94             3.84          5.25     0.85
                    //  ... many more rows ...

                    //                              Requests       Agg     req/s        min          mean           median         max          SEM
                    match = Regex.Match(line, @"^\s*\d+-\s*\d+ \s* \d+ \s* \d+\.\d+ \s* \d+\.\d+ \s* (\d+\.\d+) \s* (\d+\.\d+) \s* \d+\.\d+ \s* \d+\.\d+$");
                    if (match.Success && match.Groups.Count == 3)
                    {
                        //many lines will match, but the final values of these variables will be from the last batch which is presumably the
                        //best measurement of steady state performance
                        steadyStateMedianTime = Convert.ToDouble(match.Groups[2].Value);
                        continue;
                    }
                }
            }

            if (!startupTime.HasValue)
                throw new FormatException("Startup time was not found.");
            if (!firstRequestTime.HasValue)
                throw new FormatException("First Request time was not found.");
            if (!steadyStateMedianTime.HasValue)
                throw new FormatException("Steady state median response time not found.");
                

            result.Measurements.Add(StartupMetric, startupTime.Value);
            result.Measurements.Add(FirstRequestMetric, firstRequestTime.Value);
            result.Measurements.Add(MedianResponseMetric, steadyStateMedianTime.Value);

            output.WriteLine($"Server started in {startupTime}ms");
            output.WriteLine($"Request took {firstRequestTime}ms");
            output.WriteLine($"Median steady state response {steadyStateMedianTime.Value}ms");
        }

        /// <summary>
        /// When serializing the result data to benchview this is called to determine if any of the metrics should be reported differently
        /// than they were collected. Both web apps use this to collect several measurements in each iteration, then present those measurements
        /// to benchview as if each was the Duration metric of a distinct scenario test with its own set of iterations.
        /// </summary>
        public override bool TryGetBenchviewCustomMetricReporting(Metric originalMetric, out Metric newMetric, out string newScenarioModelName)
        {
            if(originalMetric.Equals(StartupMetric))
            {
                newScenarioModelName = "Startup";
            }
            else if (originalMetric.Equals(FirstRequestMetric))
            {
                newScenarioModelName = "First Request";
            }
            else if (originalMetric.Equals(MedianResponseMetric))
            {
                newScenarioModelName = "Median Response";
            }
            else
            {
                return base.TryGetBenchviewCustomMetricReporting(originalMetric, out newMetric, out newScenarioModelName);
            }
            newMetric = Metric.ElapsedTimeMilliseconds;
            return true;
        }

        protected abstract string GetJitBenchRepoRootDir(string outputDir);

        protected abstract string GetWebAppSrcDirectory(string outputDir);

        string GetWebAppPublishDirectory(DotNetInstallation dotNetInstall, string outputDir, string tfm)
        {
            string dir = Path.Combine(GetWebAppSrcDirectory(outputDir), "bin", dotNetInstall.Architecture, "Release", tfm, "publish");
            if (Directory.Exists(dir))
            {
                return dir;
            }

            dir = Path.Combine(GetWebAppSrcDirectory(outputDir), "bin", "Release", tfm, "publish");
            if (Directory.Exists(dir))
            {
                return dir;
            }

            return null;
        }

        string GetWebAppStoreDir(string outputDir)
        {
            return Path.Combine(GetJitBenchRepoRootDir(outputDir), StoreDirName);
        }

        private const string JitBenchRepoUrl = "https://github.com/aspnet/JitBench";
        private const string JitBenchCommitSha1Id = "6bee730486f272d31f23f1033225090511f856f3";
        private const string StoreDirName = ".store";
        private readonly Metric StartupMetric = new Metric("Startup", "ms");
        private readonly Metric FirstRequestMetric = new Metric("First Request", "ms");
        private readonly Metric MedianResponseMetric = new Metric("Median Response", "ms");
    }
}
