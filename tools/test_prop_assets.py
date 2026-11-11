"""The game's own props (tools/prop_assets.py): tag_props.bin is what
port_tag_profiles_props_open unpacks, as every build ships it."""

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from tools import prop_assets  # noqa: E402

PACKED = ROOT / "port/assets/props/tag_props.bin"


def archive_files():
    data = PACKED.read_bytes()
    assert data[:8] == b"OCETAGS1"
    count = struct.unpack_from("<I", data, 8)[0]
    offset = 12
    entries = []
    for _ in range(count):
        length = struct.unpack_from("<I", data, offset)[0]
        name = data[offset + 4:offset + 4 + length].decode()
        size = struct.unpack_from("<I", data, offset + 4 + length)[0]
        offset += 8 + length
        entries.append((name, size))
    files = {}
    for name, size in entries:
        files[name] = data[offset:offset + size]
        offset += size
    assert offset == len(data)
    return files


def test_the_archive_is_its_profile_its_tags_and_their_pack():
    files = archive_files()
    assert set(files) == {"profile.toml", "tags.pack", "tags.index"} | {prop[2] for prop in prop_assets.PROPS}
    assert files["profile.toml"].decode() == prop_assets.profile_text()


def test_the_pack_and_index_are_read_and_written_byte_for_byte():
    source = (prop_assets.SOURCE / "tags.pack").read_bytes()
    pack = prop_assets.Pack(source)
    assert pack.pack_bytes() == source
    assert pack.index_bytes() == (prop_assets.SOURCE / "tags.index").read_bytes()
    # (each region's key as port_tag_store.c makes it)
    assert all(prop_assets.key_of(pack.read(key)) == key for key in pack.records)


def test_every_region_a_tag_file_names_is_in_the_pack():
    files = archive_files()
    pack = prop_assets.Pack(files["tags.pack"])
    assert pack.index_bytes() == files["tags.index"]
    for _, _, file, _, _ in prop_assets.PROPS:
        for region in prop_assets.TagFile(files[file]).regions:
            assert tuple(region[1:5]) in pack.regions


def test_the_capsule_is_its_model_alone_and_laid_out_as_before():
    files = archive_files()
    pack = prop_assets.Pack(files["tags.pack"])
    original = prop_assets.TagFile((prop_assets.SOURCE / "capsule.scen.tagref").read_bytes())
    stripped = prop_assets.TagFile(files["capsule.scen.tagref"])
    # (a bundle is laid out exactly as tag_bundle.c writes one: nothing moves)
    assert len(stripped.tags) == len(original.tags)
    root = stripped.root(0)
    assert struct.unpack_from("<4I", stripped.tags, root + prop_assets.OBJECT_CREATION_EFFECT)[1:] == (
        prop_assets.NONE, 0, prop_assets.NONE)
    count, address = stripped.block(root, prop_assets.OBJECT_ATTACHMENTS)
    assert count == 1
    assert struct.unpack_from("<4I", stripped.tags, address)[1:] == (prop_assets.NONE, 0, prop_assets.NONE)
    differing = [index for index in range(len(original.tags)) if original.tags[index] != stripped.tags[index]]
    assert differing and all(root + 0xA0 <= index < root + 0xB0 or address <= index < address + 16
                             for index in differing)
    # (the health pack's parts, a strip of one index over and over; the rest
    # as they were)
    changed = [(old, new) for old, new in zip(original.regions, stripped.regions) if old != new]
    assert len(changed) == 2
    for old, new in changed:
        assert old[0] == new[0] and old[1] == new[1]
        assert pack.read(tuple(new[1:5])) == bytes(new[1])


def test_the_firefight_copies_differ_from_their_props_by_their_name_alone():
    files = archive_files()
    for _, tag, file, _, copy_of in prop_assets.PROPS:
        if copy_of is None:
            continue
        copy = prop_assets.TagFile(files[file])
        source = prop_assets.TagFile(files[copy_of])
        assert tag.startswith("firefight\\") and copy.name(0) == tag
        assert copy.regions == source.regions
        renames = dict(prop_assets.COPY_RENAMES.get(file, {}), **{source.name(0): tag})
        changed = [index for index in range(copy.count) if copy.name(index) != source.name(index)]
        assert changed and all(renames[source.name(index)] == copy.name(index) for index in changed)
        # (only the names' bytes)
        renamed_bytes = set()
        for index in changed:
            start = copy.name_offset(index)
            renamed_bytes |= set(range(start, start + len(copy.name(index))))
        assert all(copy.tags[index] == source.tags[index] for index in range(len(copy.tags)) if index not in renamed_bytes)


def test_the_drop_areas_model_is_the_props_own_on_every_map():
    copy = prop_assets.TagFile(archive_files()["drop area.scen.tagref"])
    names = [copy.name(index) for index in range(copy.count)]
    assert "altis\\scenery\\capsule2\\capsule2" not in names
    assert names.count("firefight\\props\\drop_area_model") == 2


def test_the_game_names_the_firefight_copies():
    spawner = (ROOT / "port/linux/game/firefight_spawner.c").read_text()
    racks = (ROOT / "port/linux/game/weapon_racks.c").read_text()
    names = {prop[1].replace("\\", "\\\\") for prop in prop_assets.PROPS}
    # (the pods are the drop area's tag, the props' own on every map)
    assert "#define FF_POD_TAG \"firefight\\\\props\\\\drop_area_pod\"" in spawner
    assert "#define FF_DROP_AREA_TAG FF_POD_TAG" in spawner
    assert "#define FORGE_DROP_AREA_TAG \"firefight\\\\props\\\\drop_area_pod\"" in (
        ROOT / "port/forge/src/forge_3d_mode.c").read_text()
    assert "#define DROP_AREA_TAG \"firefight\\\\props\\\\drop_area_pod\"" in racks
    assert "#define WEAPON_RACK_TAG \"firefight\\\\props\\\\weapon_rack_resupply\"" in racks
    assert {"firefight\\\\props\\\\drop_area_pod", "firefight\\\\props\\\\weapon_rack_resupply"} <= names


def test_forge_has_a_firefight_category_last():
    engine = (ROOT / "port/forge/include/forge_engine.h").read_text()
    tags = (ROOT / "port/forge/src/forge_runtime_tags.c").read_text()
    assert "_forge_cat_ai_squads,\n" in engine
    assert engine.index("_forge_cat_ai_squads,") < engine.index("_forge_cat_firefight,") < engine.index(
        "NUMBER_OF_FORGE_PALETTE_CATEGORIES")
    assert "\"ACTORS\",\n\t\t\"FIREFIGHT\"\n\t};" in (ROOT / "port/forge/src/forge_3d_mode.c").read_text()
    assert "add_catalog_entry('ffsp', _forge_cat_firefight," in tags
    assert "\"objective\\\\firefight_generator\"" in tags
    assert "add_catalog_entry('flag', _forge_cat_firefight, 1, \"objective\\\\firefight_generator\"" in tags


def test_every_build_ships_it():
    assert "Path(\"port/assets/props/tag_props.bin\")" in (ROOT / "tools/linux_build.py").read_text()
    assert "tag_props.bin" in (ROOT / "tools/switch_deploy.py").read_text()
    assert "'tag_props.bin'" in (ROOT / "port/web/site/app.js").read_text()
    assert "\"tag_props.bin\"" in (ROOT / "port/android/host/host_main.c").read_text()
    assert "copy_title_file(\"tag_props.bin\")" in (ROOT / "port/ps5/host/host_net_ps5.c").read_text()
    assert "#define UPDATE_PROPS \"tag_props.bin\"" in (ROOT / "port/switch/host/host_update.c").read_text()
