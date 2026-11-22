using System.Numerics;
using System.Security.Cryptography;
using System.Text;

namespace DishonoredVR;

public static partial class PoseRender
{
    /// <summary>A note-hand character: the file's character id (src/note_hand_mesh.hpp identities), its arm folder in the game's resources and
    /// the logical arm resources (the md6 decls its arm entity defs name; the mod compares them with the live arm's descriptor+8).</summary>
    public sealed record NoteHandCharacter(string Name, uint Id, string Folder, string LeftArm, string RightArm);

    // Dishonored 2: Corvo and Emily. Death of the Outsider: Billie, whose arm md6s inherit Emily's (same 69-joint skeleton) with her own meshes.
    // Her void right arm (player_rightarm_void_billie) has no file: the mod refuses it and the note keeps its controller attachment.
    static readonly NoteHandCharacter[] D2Hands = {
        new("corvo", 0, "models/characters/player/corvo/", "left_arm.md6", "right_arm.md6"),
        new("emily", 1, "models/characters/player/emily/", "left_arm.md6", "right_arm.md6") };
    static readonly NoteHandCharacter[] DotoHands = {
        new("billie", 2, "models/characters/dlc01/player/billie/", "player_leftarm_billie.md6", "player_rightarm_billie.md6") };
    public static IReadOnlyList<NoteHandCharacter> NoteHandCharacters(GameProfile? profile = null) => profile == null || profile.IsD2 ? D2Hands : DotoHands;

    /// <summary>True when a note-hand file of the profile's game (default Dishonored 2) is missing or not the current format in directory.</summary>
    public static bool NoteHandMeshesMissing(string directory, GameProfile? profile = null)
    {
        foreach (var character in NoteHandCharacters(profile))
        foreach (string side in new[] { "left", "right" })
        {
            string path = Path.Combine(directory, $"note_hand_{character.Name}_{side}.bin");
            try
            {
                using var reader = new BinaryReader(File.OpenRead(path));
                if (reader.BaseStream.Length < 3400 || reader.BaseStream.Length > 144 * 1024 * 1024 ||
                    !reader.ReadBytes(8).AsSpan().SequenceEqual("D2VRNH01"u8) || reader.ReadUInt32() != 2 ||
                    reader.ReadUInt32() != character.Id || reader.ReadUInt32() != (side == "left" ? 0u : 1u)) return true;
            }
            catch (IOException) { return true; }
        }
        return false;
    }

    // Private install-derived data. Export before Avatar's guide-only ring reweighting.
    // game is the profile's install (default Dishonored 2), directory that game's mod home's artifacts\game-art.
    public static void ExportNoteHandMeshes(string game, string directory, GameProfile? profile = null)
    {
        Directory.CreateDirectory(directory);
        var containers = new ArtImport.Containers(Path.GetFullPath(game), n => n.StartsWith("generated/") && n.Contains("models/characters/"), (profile ?? GameProfile.D2).ResourceContainers);
        foreach (var hand in NoteHandCharacters(profile))
        for (uint side = 0; side < 2; ++side)
        {
            string character = hand.Name, limb = side == 0 ? "leftarm" : "rightarm";
            string resource = hand.Folder + (side == 0 ? hand.LeftArm : hand.RightArm);
            byte[] bytes = containers.Read($"generated/basemodel/{hand.Folder}player_{limb}_{character}.bmd6model");
            var mesh = ParseMesh(bytes);
            var skeleton = ParseSkeleton(containers.Read($"generated/skeleton/models/characters/player/emily/player_{limb}_emily.edgeskl"));
            if (skeleton.Bind.Length != 69 || !mesh.Skeleton.EndsWith($"/player_{limb}_emily.md6skl")) throw new InvalidDataException("Unverified note hand rig");
            string target = Path.Combine(directory, $"note_hand_{character}_{(side == 0 ? "left" : "right")}.bin"), temporary = target + ".tmp";
            using (var writer = new BinaryWriter(File.Create(temporary)))
            {
                writer.Write("D2VRNH01"u8); writer.Write(2u); writer.Write(hand.Id); writer.Write(side);
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
                    // "glove": D2's player_*_gloves and Billie's ply_billie_glove (no D2 arm material has "glove" without "gloves").
                    writer.Write(materialName.Contains("hands") || materialName.Contains("glove") || materialName.Contains("ring") ? 1u : 0u);
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
