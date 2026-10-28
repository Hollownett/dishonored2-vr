using System.Numerics;
using System.Security.Cryptography;
using System.Text;

namespace DishonoredVR;

public static partial class PoseRender
{
    public static bool NoteHandMeshesMissing(string directory)
    {
        foreach (string character in new[] { "corvo", "emily" })
        foreach (string side in new[] { "left", "right" })
        {
            string path = Path.Combine(directory, $"note_hand_{character}_{side}.bin");
            try
            {
                using var reader = new BinaryReader(File.OpenRead(path));
                if (reader.BaseStream.Length < 3400 || reader.BaseStream.Length > 144 * 1024 * 1024 ||
                    !reader.ReadBytes(8).AsSpan().SequenceEqual("D2VRNH01"u8) || reader.ReadUInt32() != 2 ||
                    reader.ReadUInt32() != (character == "corvo" ? 0u : 1u) || reader.ReadUInt32() != (side == "left" ? 0u : 1u)) return true;
            }
            catch (IOException) { return true; }
        }
        return false;
    }

    // Private install-derived data. Export before Avatar's guide-only ring reweighting.
    public static void ExportNoteHandMeshes(string game, string directory)
    {
        Directory.CreateDirectory(directory);
        var containers = new ArtImport.Containers(Path.GetFullPath(game), n => n.StartsWith("generated/") && n.Contains("models/characters/"));
        foreach (string character in new[] { "corvo", "emily" })
        for (uint side = 0; side < 2; ++side)
        {
            string limb = side == 0 ? "leftarm" : "rightarm";
            string resource = $"models/characters/player/{character}/{(side == 0 ? "left_arm" : "right_arm")}.md6";
            byte[] bytes = containers.Read($"generated/basemodel/models/characters/player/{character}/player_{limb}_{character}.bmd6model");
            var mesh = ParseMesh(bytes);
            var skeleton = ParseSkeleton(containers.Read($"generated/skeleton/models/characters/player/emily/player_{limb}_emily.edgeskl"));
            if (skeleton.Bind.Length != 69 || !mesh.Skeleton.EndsWith($"/player_{limb}_emily.md6skl")) throw new InvalidDataException("Unverified note hand rig");
            string target = Path.Combine(directory, $"note_hand_{character}_{(side == 0 ? "left" : "right")}.bin"), temporary = target + ".tmp";
            using (var writer = new BinaryWriter(File.Create(temporary)))
            {
                writer.Write("D2VRNH01"u8); writer.Write(2u); writer.Write(character == "corvo" ? 0u : 1u); writer.Write(side);
                writer.Write(SHA256.HashData(bytes));
                byte[] name = Encoding.UTF8.GetBytes(resource); writer.Write((uint)name.Length); writer.Write(name);
                foreach (var bind in skeleton.Bind)
                {
                    var inverse = bind.Inverse(); var x = Vector3.Transform(Vector3.UnitX, inverse.Q); var y = Vector3.Transform(Vector3.UnitY, inverse.Q); var z = Vector3.Transform(Vector3.UnitZ, inverse.Q);
                    foreach (float value in new[] { x.X, y.X, z.X, inverse.P.X, x.Y, y.Y, z.Y, inverse.P.Y, x.Z, y.Z, z.Z, inverse.P.Z }) writer.Write(value);
                }
                writer.Write((uint)mesh.Surfaces.Count);
                foreach (var surface in mesh.Surfaces)
                {
                    writer.Write(surface.Kind); writer.Write((uint)surface.VertexCount); writer.Write((uint)surface.Count);
                    for (int v = surface.VertexStart; v < surface.VertexStart + surface.VertexCount; ++v)
                    {
                        var p = mesh.P[v]; writer.Write(p.X); writer.Write(p.Y); writer.Write(p.Z);
                        writer.Write(mesh.W, v * 8, 8); writer.Write(mesh.J, v * 8, 8);
                    }
                    for (int i = surface.Start; i < surface.Start + surface.Count; ++i) writer.Write(checked((ushort)(mesh.Idx[i] - surface.VertexStart)));
                    string materialName = surface.Material.ToLowerInvariant();
                    writer.Write(materialName.Contains("hands") || materialName.Contains("gloves") || materialName.Contains("ring") ? 1u : 0u);
                    for (int v = surface.VertexStart; v < surface.VertexStart + surface.VertexCount; ++v)
                    { writer.Write(mesh.Uv[v].X); writer.Write(mesh.Uv[v].Y); }
                    // Use the installed material declaration rather than guide-only material guesses.
                    var material = Encoding.UTF8.GetString(containers.Read("generated/decls/material/" + surface.Material + ".decl"));
                    var diffuse = System.Text.RegularExpressions.Regex.Match(material, @"^\s*diffusemap\s+(\S+)", System.Text.RegularExpressions.RegexOptions.Multiline | System.Text.RegularExpressions.RegexOptions.IgnoreCase);
                    if (!diffuse.Success) throw new InvalidDataException("No native note hand diffuse map: " + surface.Material);
                    string map = diffuse.Groups[1].Value.Trim('"').ToLowerInvariant();
                    if (map.EndsWith(".tga")) map = map[..^4];
                    var texture = LoadTex(containers, map);
                    writer.Write((uint)texture.W); writer.Write((uint)texture.H); writer.Write(texture.Rgba);
                }
            }
            File.Move(temporary, target, true);
        }
    }
}
