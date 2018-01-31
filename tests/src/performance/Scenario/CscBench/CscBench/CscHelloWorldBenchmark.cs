using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using Microsoft.Dotnet.TestUtilities;

namespace CscBench
{
    class CscHelloWorldBenchmark : CscBenchmark
    {
        public CscHelloWorldBenchmark() : base("Csc Hello World")
        {
        }

        protected override async Task SetupSourceToCompile(string intermediateOutputDir, string runtimeDirPath, ITestOutputHelper output)
        {
            string helloWorldDir = Path.Combine(intermediateOutputDir, "helloWorldSource");
            if (!Directory.Exists(helloWorldDir))
            {
                Directory.CreateDirectory(helloWorldDir);
            }
            string helloWorldPath = Path.Combine(helloWorldDir, "hello.cs");
            File.WriteAllLines(helloWorldPath, new string[]
            {
                "using System;",
                "public static class Program",
                "{",
                "    public static void Main(string[] args)",
                "    {",
                "        Console.WriteLine(\"Hello World!\");",
                "    }",
                "}"
            });

            string systemPrivateCoreLibPath = Path.Combine(runtimeDirPath, "System.Private.CoreLib.dll");
            string systemRuntimePath = Path.Combine(runtimeDirPath, "System.Runtime.dll");
            string systemConsolePath = Path.Combine(runtimeDirPath, "System.Console.dll");
            CommandLineArguments = "hello.cs /nostdlib /r:" + systemPrivateCoreLibPath + " /r:" + systemRuntimePath + " /r:" + systemConsolePath;
            WorkingDirPath = helloWorldDir;
        }


    }
}
