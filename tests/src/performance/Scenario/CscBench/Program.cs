using System;
using System.IO;
using System.Runtime.InteropServices;
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
            string workingDirectory = Directory.GetCurrentDirectory();
            
            //acquire dotnet
            var downloadWork = AcquireDotNetWorkItem.DownloadRuntime("2.0.0", RuntimeInformation.OSArchitecture, Path.Combine(workingDirectory, ".dotnet"));
            await downloadWork.Run(output);

            //acquire csc
            string source = "https://roslyninfra.blob.core.windows.net/perf-artifacts/CodeAnalysisRepro" +
                (RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? ".zip" : ".tar.gz");
            string localCscDir = Path.Combine(workingDirectory, "csc");
            await FileTasks.DownloadAndUnzip(source, localCscDir, output);


        }
    }
}
