using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace Canvas.Server.VisualCore;

/// <summary>The C interface of visualcore's <c>vc_native</c> library (<c>vc/native.h</c>).</summary>
internal static partial class NativeMethods
{
    private const string Library = "vc_native";

    [LibraryImport(Library, EntryPoint = "vc_document_open", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial DocumentHandle Open(string? snapshot, out int rejected);

    [LibraryImport(Library, EntryPoint = "vc_document_close")]
    internal static partial void Close(nint document);

    [LibraryImport(Library, EntryPoint = "vc_document_apply", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial nint Apply(DocumentHandle document, string ops);

    [LibraryImport(Library, EntryPoint = "vc_document_snapshot")]
    internal static partial nint Snapshot(DocumentHandle document);

    [LibraryImport(Library, EntryPoint = "vc_document_layers")]
    internal static partial nint Layers(DocumentHandle document);

    [LibraryImport(Library, EntryPoint = "vc_document_node", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial nint Node(DocumentHandle document, string id, string? mode);

    [LibraryImport(Library, EntryPoint = "vc_document_tokens", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial nint Tokens(DocumentHandle document, string? mode);

    [LibraryImport(Library, EntryPoint = "vc_document_modes")]
    internal static partial nint Modes(DocumentHandle document);

    [LibraryImport(Library, EntryPoint = "vc_last_error")]
    internal static partial nint LastError();

    [LibraryImport(Library, EntryPoint = "vc_free")]
    private static partial void Free(nint text);

    /// <summary>Copies a string the library returned and releases it. Null stays null.</summary>
    internal static string? Take(nint text)
    {
        if (text == 0)
        {
            return null;
        }
        try
        {
            return Marshal.PtrToStringUTF8(text);
        }
        finally
        {
            Free(text);
        }
    }
}

internal sealed class DocumentHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    public DocumentHandle() : base(ownsHandle: true)
    {
    }

    protected override bool ReleaseHandle()
    {
        NativeMethods.Close(handle);
        return true;
    }
}
