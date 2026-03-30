#!/usr/bin/env python3
"""Parse a UE5 .umap file and extract actor/component properties as JSON.

Reads the UPackage binary format (names table, imports, exports) and dumps
all serialized properties for actors matching a filter list. Outputs JSON
to stdout.

Usage:
    python parse_umap.py <path_to.umap> [--filter Actor1,Actor2,...]
"""

import struct
import sys
import json
import os
from dataclasses import dataclass, field
from typing import List, Dict, Any, Optional, Tuple

# ─────────────────────────────────────────────────────────────────────────────
# UPackage header (UE4/5 format)
# ─────────────────────────────────────────────────────────────────────────────

PACKAGE_MAGIC = 0x9E2A83C1

@dataclass
class FNameEntry:
    name: str
    non_case_preserving_hash: int = 0
    case_preserving_hash: int = 0

@dataclass
class FObjectImport:
    class_package: int  # name index
    class_name: int     # name index
    outer_index: int
    object_name: int    # name index

@dataclass
class FObjectExport:
    class_index: int
    super_index: int
    outer_index: int
    object_name: int    # name index
    object_flags: int
    serial_size: int
    serial_offset: int
    template_index: int = 0

class UPackageReader:
    def __init__(self, filepath: str):
        with open(filepath, 'rb') as f:
            self.data = f.read()
        self.pos = 0
        self.names: List[FNameEntry] = []
        self.imports: List[FObjectImport] = []
        self.exports: List[FObjectExport] = []
        self.file_version = 0
        self.licensee_version = 0

    def read_u8(self) -> int:
        v = self.data[self.pos]
        self.pos += 1
        return v

    def read_i32(self) -> int:
        v = struct.unpack_from('<i', self.data, self.pos)[0]
        self.pos += 4
        return v

    def read_u32(self) -> int:
        v = struct.unpack_from('<I', self.data, self.pos)[0]
        self.pos += 4
        return v

    def read_i64(self) -> int:
        v = struct.unpack_from('<q', self.data, self.pos)[0]
        self.pos += 8
        return v

    def read_u64(self) -> int:
        v = struct.unpack_from('<Q', self.data, self.pos)[0]
        self.pos += 8
        return v

    def read_f32(self) -> float:
        v = struct.unpack_from('<f', self.data, self.pos)[0]
        self.pos += 4
        return v

    def read_f64(self) -> float:
        v = struct.unpack_from('<d', self.data, self.pos)[0]
        self.pos += 8
        return v

    def read_u16(self) -> int:
        v = struct.unpack_from('<H', self.data, self.pos)[0]
        self.pos += 2
        return v

    def read_i16(self) -> int:
        v = struct.unpack_from('<h', self.data, self.pos)[0]
        self.pos += 2
        return v

    def read_fstring(self) -> str:
        """Read a UE FString (length-prefixed, null-terminated)."""
        length = self.read_i32()
        if length == 0:
            return ""
        if length < 0:
            # UTF-16
            count = -length
            raw = self.data[self.pos:self.pos + count * 2]
            self.pos += count * 2
            return raw.decode('utf-16-le').rstrip('\0')
        else:
            raw = self.data[self.pos:self.pos + length]
            self.pos += length
            return raw.decode('utf-8', errors='replace').rstrip('\0')

    def read_fname(self) -> Tuple[int, int]:
        """Read an FName reference (index + number)."""
        idx = self.read_i32()
        num = self.read_i32()
        return (idx, num)

    def get_name(self, index: int) -> str:
        if 0 <= index < len(self.names):
            return self.names[index].name
        return f"<invalid_name_{index}>"

    def get_fname_str(self, idx: int, num: int) -> str:
        name = self.get_name(idx)
        if num > 0:
            name += f"_{num - 1}"
        return name

    def parse_header(self):
        """Parse the UPackage file header."""
        self.pos = 0
        magic = self.read_u32()
        if magic != PACKAGE_MAGIC:
            raise ValueError(f"Not a UPackage file (magic={magic:#x}, expected={PACKAGE_MAGIC:#x})")

        legacy_version = self.read_i32()
        if legacy_version < 0:
            # UE4+ versioning
            if legacy_version != -4:
                legacy_ue3_version = self.read_i32()
            self.file_version = self.read_i32()  # UE4 file version (usually negative or enum)
            self.licensee_version = self.read_i32()

            # Custom versions array
            custom_version_count = self.read_i32()
            for _ in range(custom_version_count):
                # FGuid (16 bytes) + int32 version
                self.pos += 16 + 4
        else:
            raise ValueError(f"Legacy UE3 format not supported (version={legacy_version})")

        total_header_size = self.read_i32()
        folder_name = self.read_fstring()
        package_flags = self.read_u32()

        name_count = self.read_i32()
        name_offset = self.read_i32()

        # Soft object paths (UE5)
        if self.file_version >= 0:
            self.pos += 8  # SoftObjectPathsCount + SoftObjectPathsOffset

        # Localization ID
        localization_id = self.read_fstring() if package_flags & 0x00000200 else ""

        # Gatherable text data
        gatherable_count = self.read_i32()
        gatherable_offset = self.read_i32()

        export_count = self.read_i32()
        export_offset = self.read_i32()
        import_count = self.read_i32()
        import_offset = self.read_i32()
        depends_offset = self.read_i32()

        # String asset references
        string_ref_count = self.read_i32()
        string_ref_offset = self.read_i32()

        # Searchable names (UE4.14+)
        searchable_names_offset = self.read_i32()

        # Thumbnail table
        thumbnail_offset = self.read_i32()

        # GUID
        self.pos += 16  # Package GUID

        # Generations
        gen_count = self.read_i32()
        for _ in range(gen_count):
            self.pos += 8  # ExportCount + NameCount

        # Engine version
        self.pos += 4 + 4 + 4 + 4  # Major.Minor.Patch.Changelist
        engine_branch = self.read_fstring()

        # Compatible engine version (UE4.26+)
        self.pos += 4 + 4 + 4 + 4
        compatible_branch = self.read_fstring()

        compression_flags = self.read_u32()

        # Compressed chunks
        compressed_chunk_count = self.read_i32()
        for _ in range(compressed_chunk_count):
            self.pos += 16  # Offset + Size + CompressedOffset + CompressedSize

        package_source = self.read_u32()

        # Additional packages to cook
        additional_count = self.read_i32()
        for _ in range(additional_count):
            self.read_fstring()

        # Asset registry data offset
        asset_registry_data_offset = self.read_i32()

        # Bulk data start offset
        bulk_data_start_offset = self.read_i64()

        # World tile info data offset
        world_tile_info_offset = self.read_i32()

        # Chunk IDs
        chunk_id_count = self.read_i32()
        for _ in range(chunk_id_count):
            self.pos += 4

        # Preload dependency count/offset
        preload_dep_count = self.read_i32()
        preload_dep_offset = self.read_i32()

        # Parse names table
        self.pos = name_offset
        self.names = []
        for _ in range(name_count):
            name_str = self.read_fstring()
            # Non-case-preserving hash and case-preserving hash
            if self.pos + 4 <= len(self.data):
                ncph = self.read_u16()
                cph = self.read_u16()
            else:
                ncph = cph = 0
            self.names.append(FNameEntry(name_str, ncph, cph))

        # Parse imports
        self.pos = import_offset
        self.imports = []
        for _ in range(import_count):
            class_pkg = self.read_fname()
            class_name = self.read_fname()
            outer = self.read_i32()
            obj_name = self.read_fname()
            # UE5 has extra fields
            if self.file_version >= 0:
                self.pos += 4  # bImportOptional or PackageIndex
            self.imports.append(FObjectImport(
                class_pkg[0], class_name[0], outer, obj_name[0]
            ))

        # Parse exports
        self.pos = export_offset
        self.exports = []
        for _ in range(export_count):
            class_idx = self.read_i32()
            super_idx = self.read_i32()
            template_idx = self.read_i32() if self.file_version >= 0 else 0
            outer_idx = self.read_i32()
            obj_name = self.read_fname()
            obj_flags = self.read_u32()

            serial_size = self.read_i64()
            serial_offset = self.read_i64()

            # Skip remaining fields we don't need
            b_forced_export = self.read_i32()
            b_not_for_client = self.read_i32()
            b_not_for_server = self.read_i32()

            # Package GUID
            self.pos += 16

            # Package flags
            pkg_flags = self.read_u32()

            # UE4.14+ fields
            b_not_always_loaded_for_editor_game = self.read_i32()
            b_is_asset = self.read_i32()

            # First export dependency
            first_dep = self.read_i32()
            serialization_before_serialization = self.read_i32()
            create_before_serialization = self.read_i32()
            serialization_before_create = self.read_i32()
            create_before_create = self.read_i32()

            self.exports.append(FObjectExport(
                class_idx, super_idx, outer_idx, obj_name[0],
                obj_flags, int(serial_size), int(serial_offset), template_idx
            ))

    def resolve_class_name(self, class_index: int) -> str:
        """Resolve the class name from a class index (imports use negative-1 indexing)."""
        if class_index < 0:
            import_idx = -class_index - 1
            if 0 <= import_idx < len(self.imports):
                return self.get_name(self.imports[import_idx].class_name)
        elif class_index > 0:
            export_idx = class_index - 1
            if 0 <= export_idx < len(self.exports):
                return self.get_name(self.exports[export_idx].object_name)
        return "<null>"

    def resolve_object_name(self, idx: int) -> str:
        """Resolve object name from import/export index."""
        if idx < 0:
            import_idx = -idx - 1
            if 0 <= import_idx < len(self.imports):
                return self.get_name(self.imports[import_idx].object_name)
        elif idx > 0:
            export_idx = idx - 1
            if 0 <= export_idx < len(self.exports):
                return self.get_name(self.exports[export_idx].object_name)
        return "<null>"

    def parse_properties(self, offset: int, size: int) -> Dict[str, Any]:
        """Parse UProperty tags from serialized export data."""
        props = {}
        end = offset + size
        self.pos = offset

        # Skip potential UObject header bytes (flags, etc.)
        # Try to find the first property tag
        max_search = min(64, size)
        found_start = False

        for attempt in range(max_search):
            saved_pos = self.pos
            try:
                name_idx, name_num = self.read_fname()
                name = self.get_name(name_idx)
                if name == "None":
                    self.pos = saved_pos + 8  # skip "None" and continue looking
                    continue
                # Check if this looks like a valid property name
                if name and name[0].isalpha() and len(name) < 256:
                    # Peek at type name
                    type_idx, type_num = self.read_fname()
                    type_name = self.get_name(type_idx)
                    if type_name in ('FloatProperty', 'DoubleProperty', 'IntProperty',
                                     'BoolProperty', 'NameProperty', 'StrProperty',
                                     'ObjectProperty', 'StructProperty', 'ArrayProperty',
                                     'EnumProperty', 'ByteProperty', 'TextProperty',
                                     'SoftObjectProperty', 'UInt32Property', 'Int8Property',
                                     'UInt64Property', 'MapProperty', 'SetProperty',
                                     'MulticastInlineDelegateProperty',
                                     'MulticastSparseDelegateProperty'):
                        # Valid property found, rewind to start of this tag
                        self.pos = saved_pos
                        found_start = True
                        break
                self.pos = saved_pos + 1  # Try next byte
            except (struct.error, IndexError):
                self.pos = saved_pos + 1

        if not found_start:
            return props

        # Now parse property tags
        while self.pos < end:
            try:
                tag_start = self.pos
                name_idx, name_num = self.read_fname()
                name = self.get_name(name_idx)

                if name == "None":
                    break  # End of properties

                type_idx, type_num = self.read_fname()
                type_name = self.get_name(type_idx)

                prop_size = self.read_i32()
                array_index = self.read_i32()

                if type_name == 'StructProperty':
                    struct_name_idx, struct_name_num = self.read_fname()
                    struct_name = self.get_name(struct_name_idx)
                    self.pos += 16  # Struct GUID
                    has_custom_override = self.read_u8()
                    struct_data = self._parse_struct_value(struct_name, prop_size)
                    props[name] = struct_data

                elif type_name == 'BoolProperty':
                    value = self.read_u8()
                    has_custom_override = self.read_u8()
                    props[name] = bool(value)

                elif type_name == 'FloatProperty':
                    has_custom_override = self.read_u8()
                    props[name] = round(self.read_f32(), 6)

                elif type_name == 'DoubleProperty':
                    has_custom_override = self.read_u8()
                    props[name] = round(self.read_f64(), 6)

                elif type_name == 'IntProperty':
                    has_custom_override = self.read_u8()
                    props[name] = self.read_i32()

                elif type_name == 'UInt32Property':
                    has_custom_override = self.read_u8()
                    props[name] = self.read_u32()

                elif type_name == 'NameProperty':
                    has_custom_override = self.read_u8()
                    n_idx, n_num = self.read_fname()
                    props[name] = self.get_fname_str(n_idx, n_num)

                elif type_name == 'StrProperty':
                    has_custom_override = self.read_u8()
                    props[name] = self.read_fstring()

                elif type_name == 'EnumProperty':
                    enum_name_idx, enum_name_num = self.read_fname()
                    has_custom_override = self.read_u8()
                    if prop_size > 0:
                        val_idx, val_num = self.read_fname()
                        props[name] = self.get_fname_str(val_idx, val_num)
                    else:
                        props[name] = "<empty_enum>"

                elif type_name == 'ByteProperty':
                    enum_name_idx, enum_name_num = self.read_fname()
                    has_custom_override = self.read_u8()
                    enum_name = self.get_name(enum_name_idx)
                    if enum_name == "None":
                        props[name] = self.read_u8()
                    else:
                        val_idx, val_num = self.read_fname()
                        props[name] = self.get_fname_str(val_idx, val_num)

                elif type_name == 'ObjectProperty':
                    has_custom_override = self.read_u8()
                    obj_idx = self.read_i32()
                    props[name] = self.resolve_object_name(obj_idx)

                elif type_name == 'SoftObjectProperty':
                    has_custom_override = self.read_u8()
                    asset_path = self.read_fstring()
                    sub_path = self.read_fstring()
                    props[name] = asset_path

                elif type_name == 'ArrayProperty':
                    inner_type_idx, inner_type_num = self.read_fname()
                    has_custom_override = self.read_u8()
                    # Skip array data for now — too complex to parse generically
                    self.pos += prop_size
                    props[name] = f"<array[{self.get_name(inner_type_idx)}], {prop_size} bytes>"

                elif type_name == 'TextProperty':
                    has_custom_override = self.read_u8()
                    self.pos += prop_size
                    props[name] = "<text>"

                elif type_name in ('MulticastInlineDelegateProperty',
                                   'MulticastSparseDelegateProperty',
                                   'MapProperty', 'SetProperty'):
                    has_custom_override = self.read_u8()
                    self.pos += prop_size
                    props[name] = f"<{type_name}, {prop_size} bytes>"

                else:
                    # Unknown property type — skip
                    self.pos = tag_start + 8  # skip name
                    break

            except (struct.error, IndexError):
                break

        return props

    def _parse_struct_value(self, struct_name: str, size: int) -> Any:
        """Parse the value of a struct property."""
        start = self.pos

        # Known fixed-size structs
        if struct_name in ('Vector', 'FVector'):
            x = self.read_f64() if size >= 24 else self.read_f32()
            y = self.read_f64() if size >= 24 else self.read_f32()
            z = self.read_f64() if size >= 24 else self.read_f32()
            return {"X": round(x, 4), "Y": round(y, 4), "Z": round(z, 4)}

        elif struct_name in ('Rotator', 'FRotator'):
            p = self.read_f64() if size >= 24 else self.read_f32()
            y = self.read_f64() if size >= 24 else self.read_f32()
            r = self.read_f64() if size >= 24 else self.read_f32()
            return {"Pitch": round(p, 4), "Yaw": round(y, 4), "Roll": round(r, 4)}

        elif struct_name == 'LinearColor':
            r = self.read_f32()
            g = self.read_f32()
            b = self.read_f32()
            a = self.read_f32()
            return {"R": round(r, 6), "G": round(g, 6), "B": round(b, 6), "A": round(a, 6)}

        elif struct_name == 'Color':
            b = self.read_u8()
            g = self.read_u8()
            r = self.read_u8()
            a = self.read_u8()
            return {"R": r, "G": g, "B": b, "A": a}

        elif struct_name == 'Guid':
            self.pos += 16
            return "<guid>"

        elif struct_name in ('Vector2D', 'FVector2D'):
            x = self.read_f64() if size >= 16 else self.read_f32()
            y = self.read_f64() if size >= 16 else self.read_f32()
            return {"X": round(x, 4), "Y": round(y, 4)}

        elif struct_name in ('Vector4', 'FVector4'):
            x = self.read_f64() if size >= 32 else self.read_f32()
            y = self.read_f64() if size >= 32 else self.read_f32()
            z = self.read_f64() if size >= 32 else self.read_f32()
            w = self.read_f64() if size >= 32 else self.read_f32()
            return {"X": round(x, 4), "Y": round(y, 4), "Z": round(z, 4), "W": round(w, 4)}

        elif struct_name == 'Quat':
            x = self.read_f64() if size >= 32 else self.read_f32()
            y = self.read_f64() if size >= 32 else self.read_f32()
            z = self.read_f64() if size >= 32 else self.read_f32()
            w = self.read_f64() if size >= 32 else self.read_f32()
            return {"X": round(x, 4), "Y": round(y, 4), "Z": round(z, 4), "W": round(w, 4)}

        else:
            # Generic struct — try to parse as nested properties
            nested = {}
            end = start + size
            while self.pos < end - 8:
                try:
                    n_idx, n_num = self.read_fname()
                    n = self.get_name(n_idx)
                    if n == "None":
                        break
                    t_idx, t_num = self.read_fname()
                    t = self.get_name(t_idx)
                    ps = self.read_i32()
                    ai = self.read_i32()

                    if t == 'FloatProperty':
                        self.read_u8()
                        nested[n] = round(self.read_f32(), 6)
                    elif t == 'DoubleProperty':
                        self.read_u8()
                        nested[n] = round(self.read_f64(), 6)
                    elif t == 'BoolProperty':
                        nested[n] = bool(self.read_u8())
                        self.read_u8()
                    elif t == 'IntProperty':
                        self.read_u8()
                        nested[n] = self.read_i32()
                    elif t == 'StructProperty':
                        sn_idx, sn_num = self.read_fname()
                        sn = self.get_name(sn_idx)
                        self.pos += 16
                        self.read_u8()
                        nested[n] = self._parse_struct_value(sn, ps)
                    elif t == 'EnumProperty':
                        self.read_fname()
                        self.read_u8()
                        if ps > 0:
                            vi, vn = self.read_fname()
                            nested[n] = self.get_fname_str(vi, vn)
                        else:
                            nested[n] = "<empty>"
                    elif t == 'NameProperty':
                        self.read_u8()
                        vi, vn = self.read_fname()
                        nested[n] = self.get_fname_str(vi, vn)
                    elif t == 'ObjectProperty':
                        self.read_u8()
                        oi = self.read_i32()
                        nested[n] = self.resolve_object_name(oi)
                    elif t == 'ByteProperty':
                        self.read_fname()
                        self.read_u8()
                        if ps == 1:
                            nested[n] = self.read_u8()
                        else:
                            vi, vn = self.read_fname()
                            nested[n] = self.get_fname_str(vi, vn)
                    else:
                        self.read_u8()
                        self.pos += ps
                        nested[n] = f"<{t}>"
                except (struct.error, IndexError):
                    break

            if nested:
                return nested
            # Fallback: skip
            self.pos = start + size
            return f"<struct:{struct_name}, {size} bytes>"

    def dump_actors(self, filter_classes: Optional[List[str]] = None) -> List[Dict]:
        """Extract actors and their properties."""
        results = []

        for i, export in enumerate(self.exports):
            obj_name = self.get_name(export.object_name)
            class_name = self.resolve_class_name(export.class_index)

            # Resolve actual class name from import
            actual_class = class_name
            if export.class_index < 0:
                imp_idx = -export.class_index - 1
                if 0 <= imp_idx < len(self.imports):
                    actual_class = self.get_name(self.imports[imp_idx].object_name)

            if filter_classes and not any(fc.lower() in actual_class.lower() for fc in filter_classes):
                continue

            if export.serial_size <= 0 or export.serial_offset <= 0:
                continue

            try:
                props = self.parse_properties(export.serial_offset, export.serial_size)
            except Exception as e:
                props = {"_error": str(e)}

            # Get outer name for hierarchy
            outer_name = ""
            if export.outer_index > 0:
                outer_name = self.get_name(self.exports[export.outer_index - 1].object_name)
            elif export.outer_index < 0:
                outer_name = self.resolve_object_name(export.outer_index)

            if props:  # Only include if we got some properties
                results.append({
                    "export_index": i,
                    "object_name": obj_name,
                    "class": actual_class,
                    "outer": outer_name,
                    "properties": props
                })

        return results


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <path_to.umap> [--filter Class1,Class2,...]")
        sys.exit(1)

    filepath = sys.argv[1]
    filter_classes = None

    for i, arg in enumerate(sys.argv[2:], 2):
        if arg == '--filter' and i + 1 < len(sys.argv):
            filter_classes = sys.argv[i + 1].split(',')

    if not filter_classes:
        # Default: extract lighting/atmosphere/fog actors
        filter_classes = [
            'ExponentialHeightFog',
            'SkyAtmosphere',
            'SkyLight',
            'DirectionalLight',
            'HeightFogComponent',
            'SkyAtmosphereComponent',
            'SkyLightComponent',
            'DirectionalLightComponent',
            'LightComponent',
            'PostProcess',
        ]

    reader = UPackageReader(filepath)
    reader.parse_header()

    print(f"// Names: {len(reader.names)}, Imports: {len(reader.imports)}, Exports: {len(reader.exports)}", file=sys.stderr)

    actors = reader.dump_actors(filter_classes)

    output = {
        "file": os.path.basename(filepath),
        "names_count": len(reader.names),
        "imports_count": len(reader.imports),
        "exports_count": len(reader.exports),
        "actors": actors
    }

    print(json.dumps(output, indent=2, ensure_ascii=False))


if __name__ == '__main__':
    main()
