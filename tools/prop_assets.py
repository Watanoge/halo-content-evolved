#!/usr/bin/env python3
"""Packs the game's own props (port/assets/props) for Firefight and Forge:

    python3 tools/prop_assets.py

The props are Tag Injector bundles taken out of a Custom Edition map
(bigass_open_v1.1), kept in source/ as a profile's folder keeps them: a tag
file each (OCTR) and the pack (OCTP) and index (OCTI) their pixels and
vertices are in (port/linux/src/port_tag_store.c):

    capsule.scen.tagref      altis\\scenery\\capsule\\capsule: Firefight's
                             ordnance pod (firefight_spawner.c)
    weapon rack.scen.tagref  altis\\scenery\\weaponrack\\weapon rack

Forge places both as scenery that does nothing; each also goes in the
archive a second time, a copy named for Firefight (FIREFIGHT: Forge's
FIREFIGHT category), which is what has the behaviour: the capsule's copy
marks a drop area the ordnance pods come down at (firefight_spawner.c), the
rack's refills the ammunition of whoever uses it (weapon_racks.c). A copy is
its tag file with its first tag's path changed for one of the same length,
so nothing else in it moves; the capsule's copy's model and collision are
renamed so too, as the map they came from has those of the old names (its
capsule's, with the health pack) and a level keeps its own of a name. The
pods are the capsule's copy, so that they are the props' on every map.

The capsule's scenery came with its map's logic: a creation effect that
spawns a Gauss rifle, grenades, a trip mine and a health pack round it with a
scorch and particles, and a green light attached. Its tag file is written
here with that creation effect none and each attachment's tag none (which
objects.c's attachments_new makes nothing of), so it is its model alone.
Nothing else moves: a bundle must be laid out exactly as tag_bundle.c writes
one, every block where its walk finds it, so the blocks keep their elements
and the tags they named stay in it (its predicted resources still name them,
which only says what to have ready). Its model has a health pack inside it
too, a part of each of its geometries drawn with the health pack's shader:
that part's triangle strip is written again with every index the same, so it
draws nothing, as a new region of the pack (by its contents, as
port_tag_store.c keys one) that the tag file names instead.

This writes, as one archive the game unpacks into tag_injector/.props and
plays as a profile of its own (port_tag_profiles_props_open):

    port/assets/props/tag_props.bin   (tools/linux_build.py's GAME_DATA_FILES)

Its layout is a shared profile's archive (port_tag_profiles.c): "OCETAGS1", a
count (little-endian, 4 bytes), and of each file its name's length (4
bytes), its name and its size (4 bytes); then each file's bytes, in that
order. The builds do not run this, as what it writes is committed.
"""

import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "port/assets/props"
SOURCE = ASSETS / "source"
PACKED = ASSETS / "tag_props.bin"

MAP = "bigass_open_v1.1"
# (group, tag, file, name in Forge's palette, the source's file it is a copy
# of: None for its own)
PROPS = [
    ("scen", "altis\\scenery\\capsule\\capsule", "capsule.scen.tagref", "Ordnance Capsule", None),
    ("scen", "altis\\scenery\\weaponrack\\weapon rack", "weapon rack.scen.tagref", "Weapon Rack", None),
    ("scen", "firefight\\props\\drop_area_pod", "drop area.scen.tagref", "Drop Area", "capsule.scen.tagref"),
    ("scen", "firefight\\props\\weapon_rack_resupply", "resupply rack.scen.tagref", "Weapon Rack (Resupply)",
     "weapon rack.scen.tagref"),
]
# the tags of a copy named for Firefight too (by their paths), so that a level
# that has the props' own tags (bigass_open_v1.1: the map they were taken
# from, whose capsule's model has its health pack) plays the copy's, not its
# own: a level keeps its own tag of a bundle's tag's group and path
COPY_RENAMES = {
    "drop area.scen.tagref": {"altis\\scenery\\capsule2\\capsule2": "firefight\\props\\drop_area_model"},
}
# tags whose scenery is to be its model alone, and the shaders whose parts
# of its model are drawn as nothing
STRIPPED = {"capsule.scen.tagref"}
HIDDEN_SHADERS = ("cmt\\powerups\\healthpack\\shaders\\healthpack",)

# the tag file's header (port_tag_store.c): 'OCTR', version, the bundle's
# size, its tags' size, their stored size, their flags, the region count, 0;
# a region is its offset in file data, size, hash low, high, CRC, 0
TAG_FILE_SIGNATURE = b"OCTR"
TAG_FILE_HEADER = 32
TAG_FILE_REGION = 24
STORED_DEFLATED = 1
# the pack: 'OCTP', version, generation, 0; each region a record (size, hash
# low, high, CRC, stored size, flags, 0, 0) and its stored bytes. The index:
# 'OCTI', version, the pack's generation, the count; each region (size, hash
# low, high, CRC, where its stored bytes are, stored size, flags, 0) in their
# keys' order (hash high, low, CRC, size)
PACK_HEADER = 16
PACK_RECORD = 32
# a bundle (tag_bundle.h): its tag count at 0x0C, tags at 0x10, names at
# 0x14, data at 0x1C; a tag entry is 0x20 bytes, its group at 0, its name's
# offset in names at 0x0C, its root's offset in data at 0x10
BUNDLE_SIGNATURE = b"OCEB"
BUNDLE_TAG_BYTES = 0x20
NONE = 0xFFFFFFFF
# an object's definition (source/objects/object_definitions.h): its model
# (a reference: group, name, name length, index; none is the group, NONE, 0,
# NONE), its creation effect and its attachments (a block: count, address in
# data, definition), each an object_attachment_definition whose tag (a
# reference) is first
OBJECT_MODEL = 0x28
OBJECT_CREATION_EFFECT = 0xA0
OBJECT_ATTACHMENTS = 0x140
ATTACHMENT_BYTES = 0x48
# a gbxmodel (source/models/model_definitions.h; tag_schema_models.c): its
# geometries and shaders (blocks), a geometry's parts (a block at 0x24, 0x30
# each), a part's shader index (0x04), strip count (0x48) and strip offset
# in file data (0x4C), 0x84 each; a shader reference is 0x20
MODEL_GEOMETRIES = 0xD0
MODEL_SHADERS = 0xDC
GEOMETRY_BYTES = 0x30
GEOMETRY_PARTS = 0x24
PART_BYTES = 0x84
PART_SHADER = 0x04
PART_STRIP_COUNT = 0x48
PART_STRIP_OFFSET = 0x4C
SHADER_BYTES = 0x20


def toml_string(text: str) -> str:
    return text.replace("\\", "\\\\").replace("\"", "\\\"")


def profile_text() -> str:
    lines = ["# the game's own props (tools/prop_assets.py): Firefight's and Forge's", "name = \"Props\"", "format = 2"]
    for group, tag, file, name, _ in PROPS:
        lines += ["", "[[tags]]", f"map = \"{MAP}\"", f"group = \"{group}\"", f"tag = \"{toml_string(tag)}\"",
                  f"file = \"{toml_string(file)}\"", "enabled = true", f"name = \"{toml_string(name)}\""]
    return "\n".join(lines) + "\n"


def key_of(content: bytes):
    """a region's key, as port_tag_store.c's key_make makes it: its size, its
    64-bit FNV-1a hash (low, high) and its CRC-32"""
    value = 14695981039346656037
    for byte in content:
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return (len(content), value & 0xFFFFFFFF, value >> 32, zlib.crc32(content) & 0xFFFFFFFF)


class Pack:
    """a folder's pack: its regions' stored bytes by key"""

    def __init__(self, content: bytes):
        if content[:4] != b"OCTP":
            raise SystemExit("not a pack")
        self.header = content[:PACK_HEADER]
        self.records = []
        self.regions = {}
        offset = PACK_HEADER
        while offset < len(content):
            size, low, high, crc, stored_size, flags, _, _ = struct.unpack_from("<8I", content, offset)
            stored = content[offset + PACK_RECORD:offset + PACK_RECORD + stored_size]
            self.add((size, low, high, crc), stored, flags)
            offset += PACK_RECORD + stored_size

    def add(self, key, stored: bytes, flags: int = 0) -> None:
        if key in self.regions:
            return
        self.regions[key] = (stored, flags)
        self.records.append(key)

    def read(self, key) -> bytes:
        stored, flags = self.regions[key]
        return zlib.decompress(stored) if flags & STORED_DEFLATED else stored

    def pack_bytes(self) -> bytes:
        out = bytearray(self.header)
        for key in self.records:
            stored, flags = self.regions[key]
            out += struct.pack("<8I", *key, len(stored), flags, 0, 0) + stored
        return bytes(out)

    def index_bytes(self) -> bytes:
        generation = struct.unpack_from("<I", self.header, 8)[0]
        places = {}
        offset = PACK_HEADER
        for key in self.records:
            stored, flags = self.regions[key]
            places[key] = (offset + PACK_RECORD, len(stored), flags)
            offset += PACK_RECORD + len(stored)
        ordered = sorted(self.records, key=lambda key: (key[2], key[1], key[3], key[0]))
        out = bytearray(b"OCTI" + struct.pack("<3I", 1, generation, len(ordered)))
        for key in ordered:
            out += struct.pack("<8I", *key, *places[key], 0)
        return bytes(out)


class TagFile:
    """a tag file: its bundle's tags (inflated) and its regions of file data"""

    def __init__(self, content: bytes):
        if content[:4] != TAG_FILE_SIGNATURE:
            raise SystemExit("not a tag file")
        (self.signature, self.version, self.size, tags_size, stored_size, flags, count,
         self.zero) = struct.unpack_from("<4s7I", content, 0)
        self.regions = [list(struct.unpack_from("<6I", content, TAG_FILE_HEADER + index * TAG_FILE_REGION))
                        for index in range(count)]
        start = TAG_FILE_HEADER + count * TAG_FILE_REGION
        stored = content[start:start + stored_size]
        self.tags = bytearray(zlib.decompress(stored) if flags & STORED_DEFLATED else stored)
        if len(self.tags) != tags_size or self.tags[:4] != BUNDLE_SIGNATURE:
            raise SystemExit("the tag file's tags are not a bundle's")
        self.count, self.entries, self.names = struct.unpack_from("<3I", self.tags, 0x0C)
        self.data = struct.unpack_from("<I", self.tags, 0x1C)[0]

    def field(self, tag: int, offset: int) -> int:
        return struct.unpack_from("<I", self.tags, self.entries + tag * BUNDLE_TAG_BYTES + offset)[0]

    def root(self, tag: int) -> int:
        return self.data + self.field(tag, 0x10)

    def name_offset(self, tag: int) -> int:
        return self.names + self.field(tag, 0x0C)

    def name(self, tag: int) -> str:
        start = self.name_offset(tag)
        return self.tags[start:self.tags.index(0, start)].decode()

    def block(self, base: int, offset: int):
        count, address = struct.unpack_from("<2I", self.tags, base + offset)
        return count, self.data + address

    def file_bytes(self) -> bytes:
        packed = zlib.compress(bytes(self.tags), 6)
        out = bytearray(struct.pack("<4s7I", self.signature, self.version, self.size, len(self.tags), len(packed),
                                    STORED_DEFLATED, len(self.regions), self.zero))
        for region in self.regions:
            out += struct.pack("<6I", *region)
        return bytes(out + packed)


def reference_none(tags: bytearray, offset: int) -> None:
    group = struct.unpack_from("<I", tags, offset)[0]
    struct.pack_into("<4I", tags, offset, group, NONE, 0, NONE)


def model_alone(tag_file: TagFile, pack: Pack) -> None:
    """its first tag's (an object's) creation effect and attachments' tags
    none, and its model's parts of HIDDEN_SHADERS drawn as nothing"""
    tags = tag_file.tags
    root = tag_file.root(0)
    reference_none(tags, root + OBJECT_CREATION_EFFECT)
    attachments, address = tag_file.block(root, OBJECT_ATTACHMENTS)
    for attachment in range(attachments):
        reference_none(tags, address + attachment * ATTACHMENT_BYTES)
    model = struct.unpack_from("<I", tags, root + OBJECT_MODEL + 12)[0]
    if model == NONE:
        return
    model_root = tag_file.root(model)
    shader_count, shaders = tag_file.block(model_root, MODEL_SHADERS)
    hidden = set()
    for shader in range(shader_count):
        index = struct.unpack_from("<I", tags, shaders + shader * SHADER_BYTES + 12)[0]
        if index != NONE and tag_file.name(index) in HIDDEN_SHADERS:
            hidden.add(shader)
    geometry_count, geometries = tag_file.block(model_root, MODEL_GEOMETRIES)
    for geometry in range(geometry_count):
        part_count, parts = tag_file.block(geometries + geometry * GEOMETRY_BYTES, GEOMETRY_PARTS)
        for part in range(part_count):
            base = parts + part * PART_BYTES
            if struct.unpack_from("<h", tags, base + PART_SHADER)[0] not in hidden:
                continue
            strip_count, strip_offset = struct.unpack_from("<2I", tags, base + PART_STRIP_COUNT)
            region = next(region for region in tag_file.regions if region[0] == strip_offset)
            strip = pack.read(tuple(region[1:5]))
            if len(strip) != (strip_count + 2) * 2:
                raise SystemExit("a part's strip is not where its tag file says")
            empty = bytes(len(strip))
            key = key_of(empty)
            pack.add(key, zlib.compress(empty, 6), STORED_DEFLATED)
            region[1:5] = key


def renamed(tag_file: TagFile, tag: int, name: str) -> None:
    """a tag named `name`, of the same length (nothing moves)"""
    old = tag_file.name(tag)
    if len(name) != len(old):
        raise SystemExit(f"{name} is not as long as {old}")
    start = tag_file.name_offset(tag)
    tag_file.tags[start:start + len(name)] = name.encode()


def main() -> None:
    pack = Pack((SOURCE / "tags.pack").read_bytes())
    sources = {}
    for _, _, file, _, copy_of in PROPS:
        if copy_of is None:
            sources[file] = TagFile((SOURCE / file).read_bytes())
            if file in STRIPPED:
                model_alone(sources[file], pack)
    files = [("profile.toml", profile_text().encode())]
    for _, tag, file, _, copy_of in PROPS:
        if copy_of is None:
            files.append((file, sources[file].file_bytes()))
            continue
        copy = TagFile(sources[copy_of].file_bytes())
        renames = COPY_RENAMES.get(file, {})
        for index in range(copy.count):
            if copy.name(index) in renames:
                renamed(copy, index, renames[copy.name(index)])
        renamed(copy, 0, tag)
        files.append((file, copy.file_bytes()))
    files += [("tags.pack", pack.pack_bytes()), ("tags.index", pack.index_bytes())]
    out = bytearray(b"OCETAGS1" + struct.pack("<I", len(files)))
    for name, content in files:
        encoded = name.encode()
        out += struct.pack("<I", len(encoded)) + encoded + struct.pack("<I", len(content))
    for _, content in files:
        out += content
    PACKED.write_bytes(bytes(out))
    print(f"{PACKED.relative_to(ROOT)}: {len(files)} files, {len(out)} bytes")


if __name__ == "__main__":
    main()
