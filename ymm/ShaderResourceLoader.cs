using System.IO;

namespace SaAohueYmm;

internal static class ShaderResourceLoader
{
    public static byte[] Get(string name)
    {
        using var stream = typeof(ShaderResourceLoader).Assembly.GetManifestResourceStream($"SaAohueYmm.{name}.cso")
            ?? throw new InvalidOperationException($"シェーダー リソースがありません: {name}");
        using var output = new MemoryStream();
        stream.CopyTo(output);
        return output.ToArray();
    }
}
