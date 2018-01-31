using System;
using System.IO;
using System.IO.Compression;
using System.Net;
using System.Runtime.InteropServices;
using System.Threading.Tasks;

namespace Microsoft.Dotnet.TestUtilities
{


    public class DotNetSetup
    {
        public DotNetSetup(string dotNetDirPath)
        {
            DotNetDirPath = dotNetDirPath;
            RuntimeVersion = DefaultRuntimeVersion;
            OS = DefaultOSPlatform;
            Architecture = RuntimeInformation.OSArchitecture;
            AzureFeed = DefaultAzureFeed;
        }

        public string DotNetDirPath { get; set; }
        public string RuntimeVersion { get; set; }
        public string SdkVersion { get; set; }
        public OSPlatform OS { get; set; }
        public Architecture Architecture { get; set; }
        public string AzureFeed { get; set; }
        public string PrivateRuntimeBinaryDirPath { get; set; }

        public DotNetSetup WithRuntimeVersion(string version)
        {
            RuntimeVersion = version;
            return this;
        }

        public DotNetSetup WithSdkVersion(string version)
        {
            SdkVersion = version;
            return this;
        }

        public DotNetSetup WithArchitecture(Architecture architecture)
        {
            Architecture = architecture;
            return this;
        }

        public DotNetSetup WithOS(OSPlatform os)
        {
            OS = os;
            return this;
        }

        public DotNetSetup WithPrivateRuntimeBinaryOverlay(string privateRuntimeBinaryDirPath)
        {
            PrivateRuntimeBinaryDirPath = privateRuntimeBinaryDirPath;
            return this;
        }

        public string GetRuntimeDownloadLink()
        {
            return GetRuntimeDownloadLink(AzureFeed, RuntimeVersion, OS, Architecture);
        }

        public string GetSDKDownloadLink()
        {
            if(SdkVersion == null)
            {
                return null;
            }
            else
            {
                return GetSDKDownloadLink(AzureFeed, SdkVersion, OS, Architecture);
            }
        }

        async public Task<DotNetSetupResult> Run(ITestOutputHelper output)
        {
            using (var acquireOutput = new IndentedTestOutputHelper("Acquiring DotNet", output))
            {
                string remoteSdkPath = GetSDKDownloadLink();
                if(remoteSdkPath != null)
                {
                    await FileTasks.DownloadAndUnzip(remoteSdkPath, DotNetDirPath, acquireOutput);
                }

                string remoteRuntimePath = GetRuntimeDownloadLink();
                if(remoteRuntimePath != null)
                {
                    await FileTasks.DownloadAndUnzip(remoteRuntimePath, DotNetDirPath, acquireOutput);
                }

                DotNetSetupResult result = new DotNetSetupResult();
                result.DotNetDirPath = DotNetDirPath;
                result.DotNetExePath = Path.Combine(DotNetDirPath, "dotnet" + (OS == OSPlatform.Windows ? ".exe" : ""));
                acquireOutput.WriteLine("Dotnet path: " + result.DotNetExePath);
                if (!File.Exists(result.DotNetExePath))
                {
                    throw new FileNotFoundException(result.DotNetExePath + " not found");
                }

                result.SdkVersion = SdkVersion;
                if (result.SdkVersion != null)
                {
                    result.SdkDirPath = Path.Combine(DotNetDirPath, "sdk", result.SdkVersion);
                    if (!Directory.Exists(result.SdkDirPath))
                    {
                        throw new DirectoryNotFoundException("Sdk directory " + result.SdkDirPath + " not found");
                    }
                }

                result.RuntimeVersion = RuntimeVersion;
                if (result.RuntimeVersion != null)
                {
                    result.RuntimeDirPath = Path.Combine(DotNetDirPath, "shared", "Microsoft.NETCore.App", result.RuntimeVersion);
                    if(!Directory.Exists(result.RuntimeDirPath))
                    {
                        throw new DirectoryNotFoundException("Runtime directory " + result.RuntimeDirPath + " not found");
                    }

                    //overlay private binaries if needed
                    if (PrivateRuntimeBinaryDirPath != null)
                    {
                        foreach (string fileName in GetPrivateRuntimeOverlayBinaryNames(OS))
                        {
                            string backupPath = Path.Combine(result.RuntimeDirPath, fileName + ".original");
                            string overwritePath = Path.Combine(result.RuntimeDirPath, fileName);
                            string privateBinPath = Path.Combine(PrivateRuntimeBinaryDirPath, fileName);
                            if (!File.Exists(backupPath))
                            {
                                File.Copy(overwritePath, backupPath);
                            }
                            if (!File.Exists(privateBinPath))
                            {
                                throw new FileNotFoundException("Private binary " + privateBinPath + " not found");
                            }
                            File.Copy(privateBinPath, overwritePath, true);
                        }
                    }
                }


                return result;
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

        public static string[] GetPrivateRuntimeOverlayBinaryNames(OSPlatform os)
        {
            return new string[]
            {
                GetNativeDllNameConvention("coreclr", os),
                GetNativeDllNameConvention("clrjit", os),
                GetNativeDllNameConvention("mscordaccore", os),
                GetNativeDllNameConvention("mscordbi", os),
                GetNativeDllNameConvention("sos", os),
                "sos.NETCore.dll",
                GetNativeDllNameConvention("clretwrc", os),
                "System.Private.CoreLib.dll",
                "mscorrc.debug.dll",
                "mscorrc.dll"
            };
        }

        private static string GetNativeDllNameConvention(string baseName, OSPlatform os)
        {
            if(os == OSPlatform.Windows)
            {
                return baseName + ".dll";
            }
            else
            {
                return "lib" + baseName;
            }
        }
    }

    public class DotNetSetupResult
    {
        public string DotNetExePath { get; set; }
        public string DotNetDirPath { get; set; }
        public string RuntimeDirPath { get; set; }
        public string RuntimeVersion { get; set; }
        public string SdkDirPath { get; set; }
        public string SdkVersion { get; set; }
    }

}
