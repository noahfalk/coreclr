using System;
using System.IO;
using System.IO.Compression;
using System.Net;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Xunit.Abstractions;

namespace Microsoft.Dotnet.TestUtilities
{
    /// <summary>
    /// Acquires the CLI tools from a web endpoint, a local zip/tar.gz, or directly from a local path
    /// </summary>
    public class AcquireDotNetWorkItem
    {
        public static AcquireDotNetWorkItem DownloadRuntime(string runtimeVersion, Architecture arch, string localDotNetDirPath)
        {
            return Download(runtimeVersion, arch, localDotNetDirPath, false);
        }

        public static AcquireDotNetWorkItem DownloadSDK(string sdkVersion, Architecture arch, string localDotNetDirPath)
        {
            return Download(sdkVersion, arch, localDotNetDirPath, true);
        }

        public static AcquireDotNetWorkItem Download(string dotnetVersion, Architecture arch,  string localDotNetDirPath, bool includeSdk)
        {
            string remotePath = includeSdk ? GetSDKDownloadLink(dotnetVersion, arch) : GetRuntimeDownloadLink(dotnetVersion, arch);
            string localDotNetPath = Path.Combine(localDotNetDirPath, DefaultOSPlatform == OSPlatform.Windows ? "dotnet.exe" : "dotnet");
            return new AcquireDotNetWorkItem(remotePath, null, localDotNetDirPath, localDotNetPath);
        }



        

        

        /// <summary>
        /// Create a new AcquireDotNetTestStep
        /// </summary>
        /// <param name="remoteDotNetZipPath">
        /// If non-null, the CLI tools will be downloaded from this web endpoint.
        /// The path should use an http or https scheme and the remote file should be in .zip or .tar.gz format.
        /// localDotNetZipPath must also be non-null to indicate where the downloaded archive will be cached</param>
        /// <param name="localDotNetZipPath">
        /// If non-null, the location of a .zip or .tar.gz compressed folder containing the CLI tools. This
        /// must be a local file system or network file system path. 
        /// localDotNetZipExpandDirPath must also be non-null to indicate where the expanded folder will be
        /// stored.
        /// localDotNetTarPath must be non-null if localDotNetZip points to a .tar.gz format archive, in order
        /// to indicate where the .tar file will be cached</param>
        /// <param name="localDotNetZipExpandDirPath">
        /// If localDotNetZipPath is non-null, this path will be used to store the expanded version of the
        /// archive. Otherwise this path is unused.</param>
        /// <param name="localDotNetPath">
        /// The path to the dotnet binary. When the CLI tools are being acquired from a compressed archive
        /// this will presumably be a path inside the localDotNetZipExpandDirPath directory, otherwise
        /// it can be any local file system path where the dotnet binary can be found.</param>
        public AcquireDotNetWorkItem(string remoteDotNetZipPath,
                                    string localDotNetZipPath,
                                    string localDotNetZipExpandDirPath,
                                    string localDotNetPath)
        {
            RemoteDotNetPath = remoteDotNetZipPath;
            LocalDotNetZipPath = localDotNetZipPath;
            LocalDotNetZipExpandDirPath = localDotNetZipExpandDirPath;
            LocalDotNetPath = localDotNetPath;
        }

        /// <summary>
        /// If non-null, the CLI tools will be downloaded from this web endpoint.
        /// The path should use an http or https scheme and the remote file should be in .zip or .tar.gz format.
        /// </summary>
        public string RemoteDotNetPath { get; private set; }

        /// <summary>
        /// If non-null, the location of a .zip or .tar.gz compressed folder containing the CLI tools. This
        /// is a local file system or network file system path. 
        /// </summary>
        public string LocalDotNetZipPath { get; private set; }

        /// <summary>
        /// If localDotNetZipPath is non-null, this path will be used to store the expanded version of the
        /// archive. Otherwise null.
        /// </summary>
        public string LocalDotNetZipExpandDirPath { get; private set; }

        /// <summary>
        /// The path to the dotnet binary when the test step is complete.
        /// </summary>
        public string LocalDotNetPath { get; private set; }

        
    }


    public enum SdkVersionPolicy
    {
        /// <summary>
        /// Don't install any SDK version
        /// </summary>
        NoSdk,

        /// <summary>
        /// Install the version which exactly matches the runtime version
        /// </summary>
        MatchRuntimeVersion,

        /// <summary>
        /// Pick an arbitrary stable build of the SDK that is believed to
        /// work compatibly with the runtime
        /// </summary>
        CompatibleDefaultForRuntimeVersion,

        /// <summary>
        /// Use an explicitly specified SDK version
        /// </summary>
        ExplicitVersion
    }

    public class AcquireDotNetJob
    {
        public AcquireDotNetJob(string dotNetDirPath)
        {
            DotNetDirPath = dotNetDirPath;
            RuntimeVersion = DefaultRuntimeVersion;
            SdkVersionPolicy = SdkVersionPolicy.NoSdk;
            OS = DefaultOSPlatform;
            Architecture = RuntimeInformation.OSArchitecture;
            AzureFeed = DefaultAzureFeed;
        }

        public string DotNetDirPath { get; set; }
        public string RuntimeVersion { get; set; }
        public string SdkVersion { get; set; }
        public SdkVersionPolicy SdkVersionPolicy { get; set; }
        public OSPlatform OS { get; set; }
        public Architecture Architecture { get; set; }
        public string AzureFeed { get; set; }
        public string PrivateRuntimeBinaryDirPath { get; set; }

        public AcquireDotNetJob WithRuntimeVersion(string version)
        {
            RuntimeVersion = version;
            return this;
        }

        public AcquireDotNetJob WithSdkVersion(string version)
        {
            SdkVersionPolicy = SdkVersionPolicy.ExplicitVersion;
            SdkVersion = version;
            return this;
        }

        public AcquireDotNetJob WithStableRuntimeCompatibleSdkVersion()
        {
            SdkVersionPolicy = SdkVersionPolicy.CompatibleDefaultForRuntimeVersion;
            return this;
        }

        public AcquireDotNetJob WithSdk()
        {
            SdkVersionPolicy = SdkVersionPolicy.MatchRuntimeVersion;
            return this;
        }

        public AcquireDotNetJob WithArchitecture(Architecture architecture)
        {
            Architecture = architecture;
            return this;
        }

        public AcquireDotNetJob WithOS(OSPlatform os)
        {
            OS = os;
            return this;
        }

        public string GetRuntimeDownloadLink()
        {
            return GetRuntimeDownloadLink(AzureFeed, RuntimeVersion, OS, Architecture);
        }

        public string ResolveSDKVersion()
        {
            if (SdkVersionPolicy == SdkVersionPolicy.NoSdk)
            {
                return null;
            }
            else if (SdkVersionPolicy == SdkVersionPolicy.MatchRuntimeVersion)
            {
                return RuntimeVersion;
            }
            else if (SdkVersionPolicy == SdkVersionPolicy.CompatibleDefaultForRuntimeVersion)
            {
                return GetCompatibleDefaultSDKVersionForRuntimeVersion(RuntimeVersion);
            }
            else
            {
                return SdkVersion;
            }
        }

        public string GetSDKDownloadLink()
        {
            string sdkVersion = ResolveSDKVersion();
            if(sdkVersion == null)
            {
                return null;
            }
            else
            {
                return GetSDKDownloadLink(AzureFeed, ResolveSDKVersion(), OS, Architecture);
            }
        }

        public bool HasMatchingRuntimeAndSdkVersion()
        {
            return RuntimeVersion == ResolveSDKVersion();
        }

        async public Task Run(ITestOutputHelper output)
        {
            using (var indent = new IndentedTestOutputHelper("Acquiring DotNet", output))
            {
                string remoteSdkPath = GetSDKDownloadLink();
                if(remoteSdkPath != null)
                {
                    await FileTasks.DownloadAndUnzip(RemoteDotNetPath, LocalDotNetZipExpandDirPath, indent);
                }

                if(!HasMatchingRuntimeAndSdkVersion())
                {
                    await FileTasks.DownloadAndUnzip(RemoteDotNetPath, LocalDotNetZipExpandDirPath, indent);
                }
                indent.WriteLine("Dotnet path: " + LocalDotNetPath);
                if (!File.Exists(LocalDotNetPath))
                {
                    throw new FileNotFoundException(LocalDotNetPath + " not found");
                }
            }
        }

        public static string DefaultRuntimeVersion {  get { return "2.0.0"; } }

        public static string DefaultAzureFeed { get { return "https://dotnetcli.azureedge.net/dotnet"; } }

        public static OSPlatform DefaultOSPlatform
        {
            get
            {
                if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
                    return OSPlatform.Windows;
                if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
                    return OSPlatform.Linux;
                if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
                    return OSPlatform.OSX;
                throw new Exception("Unable to detect current OS");
            }
        }

        public static string GetNormalizedOSName(OSPlatform os)
        {
            if (os == OSPlatform.Windows)
            {
                return "win";
            }
            else if (os == OSPlatform.Linux)
            {
                return "linux";
            }
            else if (os == OSPlatform.OSX)
            {
                return "osx";
            }
            else
            {
                throw new Exception("OS " + os + " wasn't recognized. No normalized name for dotnet download is available");
            }
        }

        public static string GetNormalizedArchitectureName(Architecture arch)
        {
            if (arch == Architecture.X64)
            {
                return "x64";
            }
            else
            {
                throw new Exception("Architecture " + arch + " wasn't recognized. No normalized name for dotnet download is available");
            }
        }

        public static string GetRuntimeDownloadLink(string version, Architecture arch)
        {
            return GetRuntimeDownloadLink(DefaultAzureFeed, version, DefaultOSPlatform, arch);
        }

        public static string GetRuntimeDownloadLink(string azureFeed, string version, OSPlatform os, Architecture arch)
        {
            return GetRuntimeDownloadLink(azureFeed, version, GetNormalizedOSName(os), GetNormalizedArchitectureName(arch));
        }

        public static string GetRuntimeDownloadLink(string azureFeed, string version, string os, string arch)
        {
            return string.Format("{0}/Runtime/{1}/dotnet-runtime-{1}-{2}-{3}.zip", azureFeed, version, os, arch);
        }

        public static string GetSDKDownloadLink(string version, Architecture arch)
        {
            return GetSDKDownloadLink(DefaultAzureFeed, version, DefaultOSPlatform, arch);
        }

        public static string GetSDKDownloadLink(string azureFeed, string version, OSPlatform os, Architecture arch)
        {
            return GetSDKDownloadLink(azureFeed, version, GetNormalizedOSName(os), GetNormalizedArchitectureName(arch));
        }

        public static string GetSDKDownloadLink(string azureFeed, string version, string os, string arch)
        {
            return string.Format("{0}/Sdk/{1}/dotnet-sdk-{1}-{2}-{3}.zip", azureFeed, version, os, arch);
        }

        public static string GetTargetFrameworkMonikerForRuntimeVersion(string runtimeVersion)
        {
            if(runtimeVersion.StartsWith("2.0"))
            {
                return "netcoreapp2.0";
            }
            else if(runtimeVersion.StartsWith("2.1"))
            {
                return "netcoreapp2.1";
            }
            else
            {
                throw new NotSupportedException("Version " + runtimeVersion + " doesn't have a known TFM");
            }
        }

        public static string GetCompatibleDefaultSDKVersionForRuntimeVersion(string runtimeVersion)
        {
            return GetCompatibleDefaultSDKVersionForRuntimeTFM(
                GetTargetFrameworkMonikerForRuntimeVersion(runtimeVersion));
        }

        public static string GetCompatibleDefaultSDKVersionForRuntimeTFM(string targetFrameworkMoniker)
        {
            if (targetFrameworkMoniker == "netcoreapp2.0")
            {
                return "2.0.0";
            }
            else if (targetFrameworkMoniker == "netcoreapp2.1")
            {
                return "2.2.0-preview1-007558";
            }
            else
            {
                throw new Exception("No compatible SDK version has been designated for TFM: " + targetFrameworkMoniker);
            }
        }
    }

    public class DotNetInstallation
    {
        public string DotNetExePath { get; }
        public string DotNetDirPath { get; }
        public string RuntimeDirPath { get; }
        public string RuntimeVersion { get; }
        public string SdkDirPath { get; }
        public string SdkVersion { get; }
    }

}
