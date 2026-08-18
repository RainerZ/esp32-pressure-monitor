from pathlib import Path

Import("env")

build_dir = Path(env.subst("$BUILD_DIR"))
generated_sections = build_dir / "sections.ld"

# ESP-IDF requires .flash.appdesc and .flash.rodata to be adjacent, and
# esptool requires data sharing a flash MMU page to reside in one ELF segment.
# Generate a project-local shadow of sections.ld which collects XCPlite's
# runtime descriptors inside .flash.rodata and keeps named build metadata
# adjacent in the same mapped flash region.
source_sections = next(
    (
        Path(path) / "sections.ld"
        for path in env["LIBPATH"]
        if (Path(path) / "sections.ld").is_file() and (Path(path) / "sections.ld") != generated_sections
    ),
    None,
)
if source_sections is None:
    raise RuntimeError("Could not locate ESP-IDF sections.ld")

marker = "    _flash_rodata_start = ABSOLUTE(.);\n"
xcp_sections = """    __start_xcp_cals = ABSOLUTE(.);
    KEEP(*(xcp_cals))
    __stop_xcp_cals = ABSOLUTE(.);
    __start_xcp_evts = ABSOLUTE(.);
    KEEP(*(xcp_evts))
    __stop_xcp_evts = ABSOLUTE(.);
"""

# xcpclient finds these sections by name when reading the ELF. Place them
# immediately after .flash.rodata so they remain in the same mapped flash
# region while retaining their individual ELF section identities.
#
# The trailing ALIGN(8) inside each output section is load bearing. esptool
# only merges ELF sections into one image segment when they are contiguous.
# Without the padding, xcp_epk (5 bytes) ends unaligned and the ALIGN(8) of
# xcp_meta leaves a 7 byte hole, which splits the flash rodata into two DROM
# segments. The ESP-IDF bootloader then maps only the last of them and the
# application crashes immediately on boot:
#   E boot: Image contains multiple DROM segments. Only the last one will be mapped.
named_sections_marker = "  _flash_rodata_align = ALIGNOF(.flash.rodata);\n"
named_sections = """  xcp_epk : ALIGN(1)
  {
    KEEP(*(xcp_epk))
    . = ALIGN(8);
  } > default_rodata_seg

  xcp_meta : ALIGN(8)
  {
    KEEP(*(xcp_meta))
    . = ALIGN(8);
  } > default_rodata_seg

"""

sections_text = source_sections.read_text()
if sections_text.count(marker) != 1:
    raise RuntimeError(f"Unexpected ESP-IDF sections.ld layout: {source_sections}")
if sections_text.count(named_sections_marker) != 1:
    raise RuntimeError(f"Unexpected ESP-IDF sections.ld layout: {source_sections}")
generated_text = sections_text.replace(marker, marker + xcp_sections)
generated_text = generated_text.replace(named_sections_marker, named_sections + named_sections_marker)

generated_sections.parent.mkdir(parents=True, exist_ok=True)
if not generated_sections.exists() or generated_sections.read_text() != generated_text:
    generated_sections.write_text(generated_text)

env.Depends("$BUILD_DIR/${PROGNAME}.elf", str(generated_sections))


# The gap that this script must not reintroduce is invisible until the board
# fails to boot, so assert contiguity on every build. Sections are read straight
# out of the ELF, which avoids depending on a toolchain binary being on PATH.
DROM_SECTIONS = [".flash.appdesc", ".flash.rodata", "xcp_epk", "xcp_meta"]


def _elf_sections(path):
    data = Path(path).read_bytes()
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        return None  # not a 32 bit little endian ELF, skip the check
    e_shoff = int.from_bytes(data[0x20:0x24], "little")
    e_shentsize = int.from_bytes(data[0x2E:0x30], "little")
    e_shnum = int.from_bytes(data[0x30:0x32], "little")
    e_shstrndx = int.from_bytes(data[0x32:0x34], "little")
    if not e_shoff or not e_shnum:
        return None

    def header(index):
        off = e_shoff + index * e_shentsize
        return (
            int.from_bytes(data[off + 0x00:off + 0x04], "little"),  # sh_name
            int.from_bytes(data[off + 0x0C:off + 0x10], "little"),  # sh_addr
            int.from_bytes(data[off + 0x14:off + 0x18], "little"),  # sh_size
            int.from_bytes(data[off + 0x04:off + 0x08], "little"),  # sh_type
        )

    shstr = e_shoff + e_shstrndx * e_shentsize
    strtab_off = int.from_bytes(data[shstr + 0x10:shstr + 0x14], "little")  # sh_offset

    sections = {}
    for i in range(e_shnum):
        sh_name, sh_addr, sh_size, sh_type = header(i)
        end = data.index(b"\0", strtab_off + sh_name)
        name = data[strtab_off + sh_name:end].decode("ascii", "replace")
        if name in DROM_SECTIONS and sh_type != 8:  # skip SHT_NOBITS
            sections[name] = (sh_addr, sh_size)
    return sections


def check_drom_contiguous(target, source, env):
    sections = _elf_sections(str(target[0]))
    if not sections:
        return

    present = [name for name in DROM_SECTIONS if name in sections]
    for current, following in zip(present, present[1:]):
        addr, size = sections[current]
        next_addr = sections[following][0]
        if addr + size != next_addr:
            raise RuntimeError(
                "Gap of {} bytes between {} (ends 0x{:08x}) and {} (starts 0x{:08x}).\n"
                "esptool would emit separate DROM segments, the ESP-IDF bootloader would map\n"
                "only the last one, and the firmware would crash and reset on every boot.\n"
                "Pad the preceding output section in extra_linker_script.py.".format(
                    next_addr - (addr + size), current, addr + size, following, next_addr))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", check_drom_contiguous)
