using CommandLine;
using CommandLine.Text;
using System;
using System.IO;
using System.Linq;
using System.Reflection;

namespace CscBench
{
    // Licensed to the .NET Foundation under one or more agreements.
    // The .NET Foundation licenses this file to you under the MIT license.
    // See the LICENSE file in the project root for more information.



    /// <summary>
    /// Provides an interface to parse the command line arguments passed to the TieredJitBench harness.
    /// </summary>
    internal sealed class CommandLineOptions
    {
        public CommandLineOptions()
        {
            _tempDirectory = Directory.GetCurrentDirectory();
            DotnetFrameworkVersion = TieredJitBench.VersioningConstants.MicrosoftNetCoreAppPackageVersion;
        }

        [Option("use-existing-setup", Required = false, HelpText = "Use existing setup.")]
        public Boolean UseExistingSetup { get; set; }

        [Option("coreclr-bin-dir", Required = false, HelpText = "Copy private CoreCLR binaries from this directory. (The binaries must match target-architecture)")]
        public string CoreCLRBinaryDir { get; set; }

        [Option("dotnet-framework-version", Required = false, HelpText = "The version of dotnet on which private CoreCLR binaries will be overlayed")]
        public string DotnetFrameworkVersion { get; set; }

        [Option("dotnet-sdk-version", Required = false, HelpText = "The version of dotnet SDK to install for this test")]
        public string DotnetSdkVersion { get; set; }

        [Option("tiering", Required = false, HelpText = "Enable tiered jit.")]
        public Boolean EnableTiering { get; set; }

        [Option("minopts", Required = false, HelpText = "Force jit to use minopt codegen.")]
        public Boolean Minopts { get; set; }

        [Option("disable-r2r", Required = false, HelpText = "Disable loading of R2R images.")]
        public Boolean DisableR2R { get; set; }

        [Option("disable-ngen", Required = false, HelpText = "Disable loading of ngen images.")]
        public Boolean DisableNgen { get; set; }

        [Option("iterations", Required = false, HelpText = "Number of iterations to run.")]
        public uint Iterations { get; set; }

        [Option('o', Required = false, HelpText = "Specifies the intermediate output directory name.")]
        public string IntermediateOutputDirectory
        {
            get { return _tempDirectory; }

            set
            {
                if (string.IsNullOrWhiteSpace(value))
                    throw new InvalidOperationException("The intermediate output directory name cannot be null, empty or white space.");

                if (value.Any(c => Path.GetInvalidPathChars().Contains(c)))
                    throw new InvalidOperationException("Specified intermediate output directory name contains invalid path characters.");

                _tempDirectory = Path.IsPathRooted(value) ? value : Path.GetFullPath(value);
                Directory.CreateDirectory(_tempDirectory);
            }
        }

        [Option("target-architecture", Required = false, HelpText = "The architecture of the binaries being tested.")]
        public string TargetArchitecture { get; set; }

        public static CommandLineOptions Parse(string[] args)
        {
            using (var parser = new Parser((settings) =>
            {
                settings.CaseInsensitiveEnumValues = true;
                settings.CaseSensitive = false;
                settings.HelpWriter = new StringWriter();
                settings.IgnoreUnknownArguments = true;
            }))
            {
                CommandLineOptions options = null;
                parser.ParseArguments<CommandLineOptions>(args)
                    .WithParsed(parsed => options = parsed)
                    .WithNotParsed(errors =>
                    {
                        foreach (Error error in errors)
                        {
                            switch (error.Tag)
                            {
                                case ErrorType.MissingValueOptionError:
                                    throw new ArgumentException(
                                            $"Missing value option for command line argument '{(error as MissingValueOptionError).NameInfo.NameText}'");
                                case ErrorType.HelpRequestedError:
                                    Console.WriteLine(Usage());
                                    Environment.Exit(0);
                                    break;
                                case ErrorType.VersionRequestedError:
                                    Console.WriteLine(new AssemblyName(typeof(CommandLineOptions).GetTypeInfo().Assembly.FullName).Version);
                                    Environment.Exit(0);
                                    break;
                                case ErrorType.BadFormatTokenError:
                                case ErrorType.UnknownOptionError:
                                case ErrorType.MissingRequiredOptionError:
                                    throw new ArgumentException(
                                            $"Missing required  command line argument '{(error as MissingRequiredOptionError).NameInfo.NameText}'");
                                case ErrorType.MutuallyExclusiveSetError:
                                case ErrorType.BadFormatConversionError:
                                case ErrorType.SequenceOutOfRangeError:
                                case ErrorType.RepeatedOptionError:
                                case ErrorType.NoVerbSelectedError:
                                case ErrorType.BadVerbSelectedError:
                                case ErrorType.HelpVerbRequestedError:
                                    break;
                            }
                        }
                    });
                return options;
            }
        }

        public static string Usage()
        {
            var parser = new Parser((parserSettings) =>
            {
                parserSettings.CaseInsensitiveEnumValues = true;
                parserSettings.CaseSensitive = false;
                parserSettings.EnableDashDash = true;
                parserSettings.HelpWriter = new StringWriter();
                parserSettings.IgnoreUnknownArguments = true;
            });

            var helpTextString = new HelpText
            {
                AddDashesToOption = true,
                AddEnumValuesToHelpText = true,
                AdditionalNewLineAfterOption = false,
                Heading = "TieredJitBench",
                MaximumDisplayWidth = 80,
            }.AddOptions(parser.ParseArguments<CommandLineOptions>(new string[] { "--help" })).ToString();
            return helpTextString;
        }

        private string _tempDirectory;
    }
}
