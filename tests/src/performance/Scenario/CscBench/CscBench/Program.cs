using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Dotnet.TestUtilities;

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
            CommandLineOptions options = CommandLineOptions.Parse(args);
            TestRun testRun = ConfigureTestRun(options, output);
            await testRun.Run(output);
        }

        static TestRun ConfigureTestRun(CommandLineOptions options, ITestOutputHelper output)
        {
            TestRun run = new TestRun()
            {
                IntermediateOutputDirPath = Directory.GetCurrentDirectory(),
                DotnetFrameworkVersion = TieredJitBench.VersioningConstants.MicrosoftNetCoreAppPackageVersion,
                Iterations = 10
            };

            if(options.CoreCLRBinaryDir != null)
            {
                if(!Directory.Exists(options.CoreCLRBinaryDir))
                {
                    throw new Exception("coreclr-bin-dir directory " + options.CoreCLRBinaryDir + " does not exist");
                }
                run.PrivateCoreCLRBinDirPath = options.CoreCLRBinaryDir;
            }
            else
            {
                string coreRootEnv = Environment.GetEnvironmentVariable("CORE_ROOT");
                if (coreRootEnv != null)
                {
                    if (!Directory.Exists(coreRootEnv))
                    {
                        throw new Exception("CORE_ROOT directory " + coreRootEnv + " does not exist");
                    }
                    run.PrivateCoreCLRBinDirPath = coreRootEnv;
                }
                else
                {
                    //maybe we've got private coreclr binaries in our current directory? Use those if so.
                    string currentDirectory = Directory.GetCurrentDirectory();
                    if(File.Exists(Path.Combine(currentDirectory, "System.Private.CoreLib.dll")))
                    {
                        run.PrivateCoreCLRBinDirPath = currentDirectory;
                    }
                    else
                    {
                        // don't use private CoreCLR binaries
                    }
                }
            }

            if(options.DotnetFrameworkVersion != null)
            {
                run.DotnetFrameworkVersion = options.DotnetFrameworkVersion;
            }

            if(options.DotnetSdkVersion != null)
            {
                run.DotnetSdkVersion = options.DotnetSdkVersion;
            }
            run.DotnetSdkVersion = DotNetSetup.GetCompatibleDefaultSDKVersionForRuntimeVersion(run.DotnetFrameworkVersion);

            if(options.TargetArchitecture != null)
            {
                if(options.TargetArchitecture.Equals("x64", StringComparison.OrdinalIgnoreCase))
                {
                    run.Architecture = Architecture.X64;
                }
                else if(options.TargetArchitecture.Equals("x86", StringComparison.OrdinalIgnoreCase))
                {
                    run.Architecture = Architecture.X86;
                }
                else
                {
                    throw new Exception("Unrecognized architecture " + options.TargetArchitecture);
                }
            }
            else
            {
                run.Architecture = RuntimeInformation.ProcessArchitecture;
            }

            if(options.Iterations > 0)
            {
                run.Iterations = (int)options.Iterations;
            }

            run.Benchmarks.AddRange(GetBenchmarks(options));
            run.Configurations.AddRange(GetBenchmarkConfigurations(options));

            using (var runConfigSection = new IndentedTestOutputHelper("Test Run Configuration", output))
            {
                runConfigSection.WriteLine("DotnetFrameworkVersion:    " + run.DotnetFrameworkVersion);
                runConfigSection.WriteLine("DotnetSdkVersion:          " + run.DotnetSdkVersion);
                runConfigSection.WriteLine("PrivateCoreCLRBinDirPath:  " + run.PrivateCoreCLRBinDirPath);
                runConfigSection.WriteLine("Architecture:              " + run.Architecture);
                runConfigSection.WriteLine("IntermediateOutputDirPath: " + run.IntermediateOutputDirPath);
                runConfigSection.WriteLine("Iterations:                " + run.Iterations);
            }

            return run;
        }

        static IEnumerable<Benchmark> GetBenchmarks(CommandLineOptions options)
        {
            yield return new CscRoslynSourceBenchmark();
        }

        static IEnumerable<BenchmarkConfiguration> GetBenchmarkConfigurations(CommandLineOptions options)
        {
            if(options.EnableTiering || options.Minopts || options.DisableR2R || options.DisableNgen)
            {
                BenchmarkConfiguration config = new BenchmarkConfiguration();
                if(options.EnableTiering)
                {
                    config.WithTiering();
                }
                if(options.Minopts)
                {
                    config.WithMinOpts();
                }
                if(options.DisableR2R)
                {
                    config.WithNoR2R();
                }
                if(options.DisableNgen)
                {
                    config.WithNoNgen();
                }
                yield return config;
            }
            else
            {
                yield return new BenchmarkConfiguration();
                yield return new BenchmarkConfiguration().WithTiering();
                yield return new BenchmarkConfiguration().WithMinOpts();
                yield return new BenchmarkConfiguration().WithNoR2R();
            }
        }
    }
}
