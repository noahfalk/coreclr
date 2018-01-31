using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Net;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace Microsoft.Dotnet.TestUtilities
{
    public static class FileTasks
    {
        public async static Task DownloadAndUnzip(string remotePath, string localExpandedDirPath, ITestOutputHelper output, bool deleteTempFiles=true)
        {
            string tempFileNameBase = Guid.NewGuid().ToString();
            string tempDownloadPath = Path.Combine(Path.GetTempPath(), tempFileNameBase + Path.GetExtension(remotePath));
            await Download(remotePath, tempDownloadPath, output);
            await Unzip(tempDownloadPath, localExpandedDirPath, output, true);
        }

        public async static Task Download(string remotePath, string localPath, ITestOutputHelper output)
        {
            output.WriteLine("Downloading: " + remotePath + " -> " + localPath);
            Directory.CreateDirectory(Path.GetDirectoryName(localPath));
            WebRequest request = HttpWebRequest.Create(remotePath);
            WebResponse response = await request.GetResponseAsync();
            using (FileStream localZipStream = File.OpenWrite(localPath))
            {
                // TODO: restore the CopyToAsync code after System.Net.Http.dll is 
                // updated to a newer version. The current old version has a bug 
                // where the copy never finished.
                // await response.GetResponseStream().CopyToAsync(localZipStream);
                byte[] buffer = new byte[16 * 1024];
                long bytesLeft = response.ContentLength;

                while (bytesLeft > 0)
                {
                    int read = response.GetResponseStream().Read(buffer, 0, buffer.Length);
                    if (read == 0)
                        break;
                    localZipStream.Write(buffer, 0, read);
                    bytesLeft -= read;
                }
            }
        }

        public static async Task Unzip(string zipPath, string expandedDirPath, ITestOutputHelper output, bool deleteZippedFiles=true, string tempTarPath=null)
        {
            if (zipPath.EndsWith(".zip"))
            {
                await FileTasks.UnWinZip(zipPath, expandedDirPath, output);
                if (deleteZippedFiles)
                {
                    File.Delete(zipPath);
                }
            }
            else if (zipPath.EndsWith(".tar.gz"))
            {
                bool deleteTar = deleteZippedFiles;
                if(tempTarPath == null)
                {
                    string tempFileNameBase = Guid.NewGuid().ToString();
                    tempTarPath = Path.Combine(Path.GetTempPath(), tempFileNameBase + ".tar");
                    deleteTar = true;
                }
                await UnGZip(zipPath, tempTarPath, output);
                await UnTar(tempTarPath, expandedDirPath, output);
                if(deleteZippedFiles)
                {
                    File.Delete(zipPath);
                }
                if(deleteTar)
                {
                    File.Delete(tempTarPath);
                }
            }
            else
            {
                output.WriteLine("Unsupported compression format: " + zipPath);
                throw new NotSupportedException("Unsupported compression format: " + zipPath);
            }
        }

        public static async Task UnWinZip(string zipPath, string expandedDirPath, ITestOutputHelper output)
        {
            output.WriteLine("Unziping: " + zipPath + " -> " + expandedDirPath);
            using (FileStream zipStream = File.OpenRead(zipPath))
            {
                ZipArchive zip = new ZipArchive(zipStream);
                foreach (ZipArchiveEntry entry in zip.Entries)
                {
                    string extractedFilePath = Path.Combine(expandedDirPath, entry.FullName);
                    Directory.CreateDirectory(Path.GetDirectoryName(extractedFilePath));
                    using (Stream zipFileStream = entry.Open())
                    {
                        using (FileStream extractedFileStream = File.OpenWrite(extractedFilePath))
                        {
                            await zipFileStream.CopyToAsync(extractedFileStream);
                        }
                    }
                }
            }
        }

        public async static Task UnGZip(string gzipPath, string expandedFilePath, ITestOutputHelper output)
        {
            output.WriteLine("Unziping: " + gzipPath + " -> " + expandedFilePath);
            using (FileStream gzipStream = File.OpenRead(gzipPath))
            {
                using (GZipStream expandedStream = new GZipStream(gzipStream, CompressionMode.Decompress))
                {
                    using (FileStream targetFileStream = File.OpenWrite(expandedFilePath))
                    {
                        await expandedStream.CopyToAsync(targetFileStream);
                    }
                }
            }
        }

        public async static Task UnTar(string tarPath, string expandedDirPath, ITestOutputHelper output)
        {
            Directory.CreateDirectory(expandedDirPath);
            string tarToolPath = null;
            if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
            {
                tarToolPath = "/bin/tar";
            }
            else if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
            {
                tarToolPath = "/usr/bin/tar";
            }
            else
            {
                throw new NotSupportedException("Unknown where this OS stores the tar executable");
            }

            await new ProcessRunner(tarToolPath, "-xf " + tarPath).
                   WithWorkingDirectory(expandedDirPath).
                   WithLog(output).
                   WithExpectedExitCode(0).
                   Run();
        }

        public static void DirectoryCopy(string sourceDir, string destDir, ITestOutputHelper output = null, bool overwrite = true)
        {
            if(output != null)
            {
                output.WriteLine("Copying " + sourceDir + " -> " + destDir);
            }
            
            DirectoryInfo dir = new DirectoryInfo(sourceDir);

            DirectoryInfo[] dirs = dir.GetDirectories();
            if (!Directory.Exists(destDir))
            {
                Directory.CreateDirectory(destDir);
            }

            FileInfo[] files = dir.GetFiles();
            foreach (FileInfo file in files)
            {
                string temppath = Path.Combine(destDir, file.Name);
                file.CopyTo(temppath, overwrite);
            }

            foreach (DirectoryInfo subdir in dirs)
            {
                string temppath = Path.Combine(destDir, subdir.Name);
                DirectoryCopy(subdir.FullName, temppath, null, overwrite);
            }
        }
    }
}
