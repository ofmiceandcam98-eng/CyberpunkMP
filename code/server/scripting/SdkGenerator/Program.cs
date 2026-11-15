using CppSharp;
using CppSharp.AST;
using CppSharp.Generators;
using System.Runtime.InteropServices;

internal class CyberpunkSdk : ILibrary
{
    private string Module { get; set; }
    private string OutputPath { get; set; }
    private string SourcePath { get; set; }
    private string[] FileNames { get; set; }

    internal CyberpunkSdk(string module, string outputPath, string sourcePath, string[] fileNames)
    {
        Module = module;
        OutputPath = outputPath;
        SourcePath = sourcePath;
        FileNames = fileNames;
    }

    public void Postprocess(Driver driver, ASTContext ctx)
    {
    }

    public void Preprocess(Driver driver, ASTContext ctx)
    {
    }

    public void Setup(Driver driver)
    {
        var options = driver.Options;
        options.OutputDir = OutputPath;
        options.GeneratorKind = GeneratorKind.CSharp;

        var parserOptions = driver.ParserOptions;
        parserOptions.AddDefines("WIN32");
        parserOptions.AddDefines("CPPSHARP_GENERATOR");

        var module = options.AddModule(Module);
        if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
            module.SharedLibraryName = "Server.Native.dll";
        else
            module.SharedLibraryName = "Server.Native";

        module.IncludeDirs.Add(SourcePath);

        foreach (var file in FileNames)
        {
            module.Headers.Add(file);
        }
    }

    public void SetupPasses(Driver driver)
    {

    }
}

internal class Program
{
    private static string GetGitRoot()
    {
        string gitFolderPath = LibGit2Sharp.Repository.Discover(Directory.GetCurrentDirectory());
        if (gitFolderPath == null)
            throw new Exception("This code should be ran from a Git repository!");

        // Normalize directory separators to match the current environment
        gitFolderPath = gitFolderPath.Replace('/', Path.DirectorySeparatorChar)
                                     .Replace('\\', Path.DirectorySeparatorChar);

        // A linked worktree keeps its git folder INSIDE the main checkout, at
        // <main>/.git/worktrees/<name>, and names its own root in that folder's "gitdir"
        // file (the path of the worktree's .git). Cutting ".git" off the path, as below,
        // answered with the MAIN checkout: the generator read that checkout's headers,
        // wrote the bindings there, and the worktree's own build then failed with
        // "namespace 'Internal' does not exist". An ordinary clone has no such file and
        // takes the cut below exactly as before.
        string worktreeLink = Path.Combine(gitFolderPath, "gitdir");
        if (File.Exists(worktreeLink))
        {
            string? worktreeRoot = Path.GetDirectoryName(File.ReadAllText(worktreeLink).Trim());
            if (!string.IsNullOrEmpty(worktreeRoot) && Directory.Exists(worktreeRoot))
                return worktreeRoot + Path.DirectorySeparatorChar;
        }

        // Remove the .git part from the path
        string repositoryRoot = gitFolderPath.TrimEnd(Path.DirectorySeparatorChar)
                                             .Remove(gitFolderPath.LastIndexOf(".git", StringComparison.Ordinal));

        return repositoryRoot;
    }

    private static void Main(string[] args)
    {
        var fileNames = args.Select(arg => Path.GetFileName(arg)).ToArray();

        try
        {
            string root = GetGitRoot();
            string output = Path.Combine(root, @"code/server/scripting/CyberpunkSdk");
            string outputLoader = Path.Combine(root, @"code/server/loader");
            string source = Path.Combine(root, @"code/server/native/Scripting");

            ConsoleDriver.Run(new CyberpunkSdk("CyberpunkSdk.Internal", output, source, fileNames));
            ConsoleDriver.Run(new CyberpunkSdk("CyberpunkMp", outputLoader, source, new[] { "ServerAPI.h" }));

            //string sdkFile = Path.Combine(output, "CyberpunkSdk.Internal.cs");

            //string text = File.ReadAllText(sdkFile);

            /*if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
                text = text.Replace("Server.exe.dll", "Server.exe");
            else
                text = text.Replace("DllImport(\"Server\"", "DllImport(\"../Server\"");*/
           
            //File.WriteAllText(sdkFile, text);
        }
        catch(Exception e)
        {
            Console.WriteLine(e);
        }
    }
}