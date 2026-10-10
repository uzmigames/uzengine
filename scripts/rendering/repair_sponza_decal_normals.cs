using System;
using System.IO;
using System.Numerics;
using System.Collections.Generic;
using System.Globalization;

public static class SponzaDecalNormals
{
    private sealed class Mesh
    {
        public byte[] Bytes;
        public int Count, IndexCount, VertexOffset, IndexOffset;
        public Mesh(string path)
        {
            Bytes = File.ReadAllBytes(path);
            if (BitConverter.ToUInt32(Bytes, 0) != 0x484d5a55 || BitConverter.ToInt32(Bytes, 4) != 2
                || BitConverter.ToInt32(Bytes, 8) != 0 || BitConverter.ToInt32(Bytes, 12) != 48
                || BitConverter.ToInt32(Bytes, 20) != 1)
                throw new InvalidDataException("Expected UZMH v2 standard48-byte vertices and32-bit indices.");
            Count = BitConverter.ToInt32(Bytes, 16);
            IndexCount = BitConverter.ToInt32(Bytes, 24);
            VertexOffset = checked((int)BitConverter.ToInt64(Bytes, 56));
            IndexOffset = checked((int)BitConverter.ToInt64(Bytes, 64));
            if (IndexCount % 3 != 0 || VertexOffset + Count * 48 > IndexOffset
                || IndexOffset + IndexCount * 4 > Bytes.Length)
                throw new InvalidDataException("Invalid mesh ranges.");
        }
        public int Index(int i) { return BitConverter.ToInt32(Bytes, IndexOffset + i * 4); }
        public float Scalar(int vertex, int component) { return BitConverter.ToSingle(Bytes, VertexOffset + vertex * 48 + component * 4); }
        public Vector3 Vector(int vertex, int component) { return new Vector3(Scalar(vertex, component), Scalar(vertex, component + 1), Scalar(vertex, component + 2)); }
        public void Write(int vertex, int component, float value) { Array.Copy(BitConverter.GetBytes(value), 0, Bytes, VertexOffset + vertex * 48 + component * 4, 4); }
        public void WriteVector(int vertex, int component, Vector3 value) { Write(vertex, component, value.X); Write(vertex, component + 1, value.Y); Write(vertex, component + 2, value.Z); }
    }
    private static bool Finite(Vector3 v) { return !float.IsNaN(v.X + v.Y + v.Z) && !float.IsInfinity(v.X + v.Y + v.Z); }
    private static float Angle(Vector3 a, Vector3 b)
    {
        return (float)Math.Acos(Math.Max(-1.0, Math.Min(1.0, Vector3.Dot(a, b) / (a.Length() * b.Length()))));
    }
    public static void Repair(string path)
    {
        var mesh = new Mesh(path);
        var normals = new Vector3[mesh.Count];
        var tangents = new Vector3[mesh.Count];
        var bitangents = new Vector3[mesh.Count];
        var used = new bool[mesh.Count];
        for (int i = 0; i < mesh.IndexCount; i += 3)
        {
            int a = mesh.Index(i), b = mesh.Index(i + 1), c = mesh.Index(i + 2);
            var p = mesh.Vector(a, 0); var q = mesh.Vector(b, 0); var r = mesh.Vector(c, 0);
            var edge1 = q - p; var edge2 = r - p;
            var outwardArea = -Vector3.Cross(edge1, edge2);
            float area2 = outwardArea.Length();
            if (area2 <= 1e-8f) continue;
            normals[a] += outwardArea * Angle(edge1, edge2);
            normals[b] += outwardArea * Angle(p - q, r - q);
            normals[c] += outwardArea * Angle(p - r, q - r);
            used[a] = used[b] = used[c] = true;
            float du1 = mesh.Scalar(b, 6) - mesh.Scalar(a, 6), dv1 = mesh.Scalar(b, 7) - mesh.Scalar(a, 7);
            float du2 = mesh.Scalar(c, 6) - mesh.Scalar(a, 6), dv2 = mesh.Scalar(c, 7) - mesh.Scalar(a, 7);
            float determinant = du1 * dv2 - du2 * dv1;
            if (Math.Abs(determinant) <= 1e-12f) continue;
            var tangent = (edge1 * dv2 - edge2 * dv1) * (area2 / determinant);
            var bitangent = (edge2 * du1 - edge1 * du2) * (area2 / determinant);
            foreach (int vertex in new int[] { a, b, c }) { tangents[vertex] += tangent; bitangents[vertex] += bitangent; }
        }
        for (int i = 0; i < mesh.Count; ++i)
        {
            if (!used[i] || normals[i].LengthSquared() <= 1e-12f)
                throw new InvalidDataException("Vertex has no unambiguous LOD0 normal: " + i);
            var normal = Vector3.Normalize(normals[i]);
            var tangent = tangents[i] - normal * Vector3.Dot(normal, tangents[i]);
            if (tangent.LengthSquared() <= 1e-12f)
                tangent = Vector3.Cross(Math.Abs(normal.Z) < 0.9f ? Vector3.UnitZ : Vector3.UnitY, normal);
            tangent = Vector3.Normalize(tangent);
            float sign = Vector3.Dot(Vector3.Cross(normal, tangent), bitangents[i]) < 0 ? -1.0f : 1.0f;
            if (!Finite(normal) || !Finite(tangent)) throw new InvalidDataException("Nonfinite reconstructed frame.");
            mesh.WriteVector(i, 3, normal);
            mesh.WriteVector(i, 8, tangent);
            mesh.Write(i, 11, sign);
        }
        File.WriteAllBytes(path, mesh.Bytes);
    }
    public static int Audit(string path, string baseline)
    {
        var mesh = new Mesh(path);
        int badGeometry = 0, badFrames = 0, invariantErrors = 0;
        float minimumCosine = 1;
        for (int i = 0; i < mesh.IndexCount; i += 3)
        {
            int a = mesh.Index(i), b = mesh.Index(i + 1), c = mesh.Index(i + 2);
            var outward = -Vector3.Cross(mesh.Vector(b, 0) - mesh.Vector(a, 0), mesh.Vector(c, 0) - mesh.Vector(a, 0));
            if (outward.LengthSquared() <= 1e-16f) continue;
            outward = Vector3.Normalize(outward);
            var normal = Vector3.Normalize(mesh.Vector(a, 3) + mesh.Vector(b, 3) + mesh.Vector(c, 3));
            float cosine = Vector3.Dot(outward, normal);
            minimumCosine = Math.Min(minimumCosine, cosine);
            if (!Finite(normal) || cosine <= 0) ++badGeometry;
        }
        for (int i = 0; i < mesh.Count; ++i)
        {
            var n = mesh.Vector(i, 3); var t = mesh.Vector(i, 8); float w = mesh.Scalar(i, 11);
            if (!Finite(n) || !Finite(t) || Math.Abs(n.Length() - 1) > 1e-4f
                || Math.Abs(t.Length() - 1) > 1e-4f || Math.Abs(Vector3.Dot(n, t)) > 1e-4f || Math.Abs(w) != 1)
                ++badFrames;
        }
        if (!String.IsNullOrEmpty(baseline))
        {
            byte[] before = File.ReadAllBytes(baseline);
            if (before.Length != mesh.Bytes.Length) ++invariantErrors;
            for (int i = 0; i < Math.Min(before.Length, mesh.Bytes.Length); ++i)
            {
                int within = i - mesh.VertexOffset;
                bool vertex = within >= 0 && within < mesh.Count * 48;
                int component = vertex ? within % 48 : -1;
                bool allowed = vertex && ((component >= 12 && component < 24) || component >= 32);
                if (!allowed && before[i] != mesh.Bytes[i]) ++invariantErrors;
            }
        }
        bool plane = true;
        foreach (int vertex in new int[] { 1444, 1446, 1447 })
            plane &= mesh.Vector(vertex, 3).X > 0.99f && Math.Abs(mesh.Vector(vertex, 3).Z) < 0.02f;
        Console.WriteLine(String.Format(CultureInfo.InvariantCulture,
            "vertices={0} LOD0triangles={1} wrongHemisphere={2} minCosine={3:F9} invalidFrames={4} changedProtectedBytes={5} confirmedPlaneCorrect={6}",
            mesh.Count, mesh.IndexCount / 3, badGeometry, minimumCosine, badFrames, invariantErrors, plane));
        return badGeometry == 0 && badFrames == 0 && invariantErrors == 0 && plane ? 0 : 1;
    }
}
