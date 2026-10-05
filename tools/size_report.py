#!/usr/bin/env python3
# Size report of an ESP-IDF build, read out of the linker map.
#
# The hardware abstraction has to answer one question over and over: what does
# this change cost. A driver swap, a board profile, a font generated at build
# time - each is a number in kilobytes. The plan that motivated this tool
# carried estimates where it could have carried measurements, and the numbers
# were read out of a map file by hand, which is why nobody could repeat them.
#
# So the map file is the source and this script reads it. Nothing is estimated,
# nothing is hard-coded per project. What it prints:
#
#   Flash and RAM per area, ours separated from the framework, because the
#   framework is the larger half of the flash and "the build grew by 60 kB" means
#   something very different depending on whose 60 kB it was.
#   PSRAM buffers on their own line. They are heap, they are not static RAM, and
#   they are not in the map either - a pair of 153 kB line buffers is exactly the
#   kind of thing that decides whether a board fits.
#
# Two details of the map format that cost an afternoon each, written down so the
# next reader does not pay for them again:
#
#   The file lists every object twice. Before "Linker script and memory map" is
#   the per-object input section table, after it the output sections that were
#   actually allocated. Summing both counts every byte twice and gives a flash
#   figure three times too small. Only the second half is real.
#   A section whose name is long enough does not fit on its line: the name
#   stands alone and the address, size and object follow on the next line. Such
#   a line matches no section pattern at all, and dropping it loses real code -
#   .literal.diagPhaseC is one of them, and it is one of ours.
#
# Usage:
#   tools/size_report.py                        # the usual env, default map
#   tools/size_report.py --env esp32-s3          # a named PlatformIO env
#   tools/size_report.py --map path/to/firmware.map
#   tools/size_report.py --save build/size-before.txt
#   tools/size_report.py --baseline build/size-before.txt
#
# The report ends in a machine-readable block, which is what --baseline reads.
# Save a report before a change, compare after it, and the estimate becomes a
# measurement.
#
# SPDX-License-Identifier: MIT

import argparse
import os
import re
import subprocess
import sys

# Which area an object of ours belongs to. Matched against the object path, so
# this is a list of directory names, not a list of files.
OUR_AREAS = {
    "gui": ("gui", "font"),
    "web": ("web",),
    "storage": ("storage", "sdlog"),
    "device": ("device", "rct", "oig"),
    "output": ("output", "relay"),
    "display": ("display", "touch", "backlight"),
    "config": ("config",),
    "i18n": ("i18n",),
}

# Objects that belong to no area of ours because they only share a name with
# one: the toolchain lives under .platformio/packages, which contains "platform"
# in every single path and would otherwise swallow the whole map. A path of ours
# always carries /src/ or lib<N>/.
NOT_OURS = ("/.platformio/", "/packages/", "/toolchain-", "/libc", "/libgcc",
            "/libstdc++")

# Output sections that occupy flash and the ones that occupy static RAM.
FLASH_SECTIONS = (".text", ".literal", ".rodata", ".irom", ".flash")
RAM_SECTIONS = (".data", ".bss", ".dram", ".noinit", ".group")

# Internal RAM of the S3, for the percentage. drom/iram from the map would be the
# linker script's maximum, not the chip's.
DRAM_TOTAL = 320 * 1024

# A section name alone on its line, no address yet.
NAME_ONLY = re.compile(r"^\s{1,2}(\.[\w.\-]+)\s*$")
# Address, size, object - with the name either present or carried over.
FULL = re.compile(
    r"^\s{1,2}(\.[\w.\-]+)?\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)\s*$"
)
MEM_REGION = re.compile(
    r"^(\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+([rwx]+)\s*$"
)
PARTITION = re.compile(
    r"^\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*,\s*(0x[0-9a-f]+)\s*,\s*(0x[0-9a-f]+)",
    re.I,
)

MAP_MARKER = "Linker script and memory map"


def human(n):
    if n >= 1024 * 1024:
        return "%.2f MB" % (n / (1024.0 * 1024))
    if n >= 1024:
        return "%.1f kB" % (n / 1024.0)
    return "%d B" % n


def find_map(project, env):
    base = os.path.join(project, ".pio", "build", env)
    for name in ("firmware.map", "rct-panel.map"):
        path = os.path.join(base, name)
        if os.path.isfile(path):
            return path
    return None


def app_partition_size(project):
    """The size of the app partition, from the CSV if it can be found.

    This is the flash budget that matters: OTA means two app slots, so the
    figure a build has to fit into is the slot, not the whole chip.
    """
    best = 0
    candidates = []
    if os.path.isdir(os.path.join(project, "partitions")):
        for fn in sorted(os.listdir(os.path.join(project, "partitions"))):
            if fn.endswith(".csv"):
                candidates.append(os.path.join(project, "partitions", fn))
    else:
        for fn in sorted(os.listdir(project)):
            if fn.endswith(".csv") and "partition" in fn:
                candidates.append(os.path.join(project, fn))
    for path in candidates:
        try:
            with open(path, "r", errors="replace") as fh:
                for line in fh:
                    m = PARTITION.match(line)
                    if m and m.group(2).lower() == "app":
                        best = max(best, int(m.group(5), 16))
        except OSError:
            continue
    return best


def parse_sections(lines):
    """[(section, size, object path)] of the allocated output sections only."""
    try:
        start = next(i for i, l in enumerate(lines) if MAP_MARKER in l)
    except StopIteration:
        sys.exit(
            "Kein Abschnitt 'Linker script and memory map' in der Map-Datei. "
            "Ist das wirklich eine GNU-ld-Map?"
        )
    out = []
    pending = None
    for line in lines[start:]:
        m = NAME_ONLY.match(line)
        if m:
            pending = m.group(1)
            continue
        m = FULL.match(line)
        if not m:
            continue
        name = m.group(1) or pending
        if m.group(1):
            pending = None
        if not name:
            continue
        obj = m.group(4)
        # The fourth field is the object for a section line and something else
        # (a symbol, a comment) for everything else. Only objects are counted.
        if not (obj.endswith(".o") or obj.endswith(")")):
            continue
        out.append((name, int(m.group(3), 16), obj))
    return out


def parse_regions(lines):
    """region name -> length, from the memory configuration block."""
    regions = {}
    in_block = False
    for line in lines:
        if line.strip().lower().startswith("memory configuration"):
            in_block = True
            continue
        if in_block:
            if not line.strip():
                continue
            m = MEM_REGION.match(line.strip())
            if m and m.group(1) != "*default*":
                regions[m.group(1)] = int(m.group(3), 16)
            elif not m:
                break
    return regions


def object_area(path):
    """The area an object belongs to: ours by name, the framework grouped."""
    norm = path.replace("\\", "/")
    low = norm.lower()
    if any(bad in low for bad in NOT_OURS):
        return "framework"
    for area, hints in OUR_AREAS.items():
        for hint in hints:
            # The area has to be a path element of ours, not any substring:
            # "display" must not match a framework file called "display.c".
            if ("/src/" + hint) in low or low.startswith("src/" + hint):
                return area
    if "/src/" in norm or norm.startswith("src/"):
        return "ours-other"
    return "framework"


def collect(sections):
    """area -> {flash, ram, objects}"""
    areas = {}

    def slot(name):
        return areas.setdefault(name, {"flash": 0, "ram": 0, "objects": set()})

    for name, size, obj in sections:
        if name.startswith(".debug"):
            # Debug info is 18 MB of this map and none of it ships.
            continue
        s = slot(object_area(obj))
        s["objects"].add(obj)
        if name.startswith(FLASH_SECTIONS):
            s["flash"] += size
        elif name.startswith(RAM_SECTIONS):
            s["ram"] += size
    return areas


# The sizeof() of the types that show up in a size expression. These are not
# guessed: a lv_color_t is two bytes on every ESP32, and the sizes below are
# checked against the firmware, which prints the runtime figure for the queue
# buffer in its log ("SD: Puffer 288 Zeilen / 24 h (85248 bytes, PSRAM)").
SIZEOF = {
    # LV_COLOR_DEPTH 16 in include/lv_conf.h: an lv_color_t is two bytes. Read
    # from the lv_conf.h in the project when there is one, so a panel that moves
    # to 32-bit colour does not leave this tool quietly wrong.
    "lv_color_t": 2,
    "uint16_t": 2,
    "uint8_t": 1,
    "uint32_t": 4,
    "int": 4,
    "size_t": 4,
    "float": 4,
    "rowq::Slot": None,     # taken from the struct's two arrays
}

# A constant written down in the project: #define X 480, constexpr int X = 480,
# static const size_t X = 480. The value may sit behind an "=", behind a "(" for
# a function-like define, or simply after whitespace - all three spellings
# occur here, and a scanner that only knows one of them reports the display
# resolution as unknown.
CONST_RE = re.compile(
    r"^\s*(?:#\s*define|constexpr\s+\w+|static\s+const\s+\w+"
    r"|const\s+\w+)\s+(\w+)\s*(?:=\s*|\(\s*)?(\d+)\s*(?:[uUlL]*)\s*[,;)]?"
    r"(?:\s*//.*)?$"
)
# The allocation, with the variable it lands in. Two shapes have to match, and
# the second one is the common one in this code base:
#
#     s_buf = (uint16_t *)heap_caps_malloc(need, MALLOC_CAP_SPIRAM);
#     lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(...);
#
# In the second the declaration with its pointer type stands in front of the
# name, so a pattern that wants the name right before the "=" finds nothing -
# and it finds nothing for exactly the two largest buffers there are.
ALLOC_RE = re.compile(
    r"([\w\.\->\[\]]+)\s*=\s*"
    r"(?:\(\s*[\w:]+\s*\*+\s*\)\s*)?"
    r"(?:pvPortMalloc|heap_caps_malloc|ps_malloc|heap_caps_aligned_alloc|malloc)"
    r"\s*\(\s*([^;]*?)\)\s*;"
)


def scan_constants(root):
    """Numeric constants out of the project's own sources.

    The display resolution and the buffer sizes live in the code, not in a
    build script, so they are read from there. A value the tool cannot resolve is
    reported as unresolved rather than assumed - a wrong size in this report
    would be worse than a missing one, because the whole point is that the
    numbers can be believed.
    """
    consts = {}
    structs = {}
    skip_dirs = {".pio", ".git", "tools", "docs", "node_modules"}
    files = []
    for dirpath, dirs, names in os.walk(root):
        dirs[:] = [d for d in dirs if d not in skip_dirs]
        for fn in sorted(names):
            if fn.endswith((".cpp", ".c", ".h", ".hpp")):
                files.append(os.path.join(dirpath, fn))

    texts = {}
    for path in files:
        try:
            with open(path, "r", errors="replace") as fh:
                texts[path] = fh.read()
        except OSError:
            continue

    # Constants first, in a pass of its own: a struct member can name a
    # constant from a header that is read later, so the two passes cannot be
    # interleaved per file.
    for text in texts.values():
        for line in text.splitlines():
            m = CONST_RE.match(line)
            if m:
                consts.setdefault(m.group(1), int(m.group(2)))

    # A struct of plain char arrays has a size the tool can work out: the sum
    # of its array members. rowq::Slot is two of them, a line and a path, so
    # the queue buffer of the SD logger gets a real number instead of an
    # "unknown". The member's namespace prefix is kept out of the way, since
    # csvrow::kLineCap is found under the name kLineCap.
    for text in texts.values():
        for sm in re.finditer(r"struct\s+(\w+)\s*\{([^}]*)\}", text, re.S):
            members = re.findall(r"char\s+\w+\[([\w:]+)\]", sm.group(2))
            if not members:
                continue
            total = 0
            ok = True
            for cap in members:
                if cap.isdigit():
                    total += int(cap)
                elif cap.split("::")[-1] in consts:
                    total += consts[cap.split("::")[-1]]
                else:
                    ok = False
            if ok:
                structs.setdefault(sm.group(1), total)

    sizes = dict(SIZEOF)

    # lv_color_t follows LV_COLOR_DEPTH: 16 means two bytes, 32 means four.
    # Read from the project's own lv_conf.h, so a panel that moves to 32-bit
    # colour does not leave this tool quietly wrong.
    conf = os.path.join(root, "include", "lv_conf.h")
    try:
        with open(conf, "r", errors="replace") as fh:
            m = re.search(r"#define\s+LV_COLOR_DEPTH\s+(\d+)", fh.read())
        if m:
            depth = int(m.group(1))
            if depth in (1, 8, 16, 32):
                sizes["lv_color_t"] = max(1, depth // 8)
    except OSError:
        pass

    sizes.update(structs)
    # A qualified name in the source, e.g. sizeof(rowq::Slot), is looked up
    # under the bare struct name.
    for qualified, size in structs.items():
        sizes.setdefault(qualified.split("::")[-1], size)
    return consts, sizes


EXPR_TOKEN = re.compile(
    r"(\d+)|(\w+::\w+)|(\w+)|(\*)|(\+)|(\-)|(\()|(\))|(\s+)"
)


def eval_size(expr, consts, sizes):
    """Evaluate a size expression, or return None if anything is unknown."""
    src = expr.strip()
    # Only arithmetic on numbers, names and sizeof is attempted. Anything else
    # (a function call, a pointer, a member access) is out of reach. sizeof is
    # the one call that is in reach, so it is cut out of the check first.
    probe = re.sub(r"sizeof\s*\(\s*[\w:]+\s*\)", "", src)
    if re.search(r"[a-zA-Z_]\w*\s*\(", probe):
        return None

    out = []
    pos = 0
    while pos < len(src):
        m = EXPR_TOKEN.match(src, pos)
        if not m:
            return None
        pos = m.end()
        if m.group(1):
            out.append(str(int(m.group(1))))
        elif m.group(2) or m.group(3):
            name = m.group(2) or m.group(3)
            if name == "sizeof":
                # sizeof(type) is replaced by the type's size; the brackets
                # around it are consumed with it, or the name of the type ends
                # up in the arithmetic.
                # sizeof ( type ) - three tokens, and the type has to be one the tool knows
                open_b = EXPR_TOKEN.match(src, pos)
                if not open_b or not open_b.group(7):
                    return None
                ty_tok = EXPR_TOKEN.match(src, open_b.end())
                if not ty_tok or not (ty_tok.group(2) or ty_tok.group(3)):
                    return None
                bare = (ty_tok.group(2) or ty_tok.group(3)).split("::")[-1]
                close_b = EXPR_TOKEN.match(src, ty_tok.end())
                if not close_b or not close_b.group(8):
                    return None
                if bare not in sizes:
                    return None
                out.append(str(sizes[bare]))
                pos = close_b.end()
                continue
            elif name in consts:
                out.append(str(consts[name]))
            else:
                return None
        elif m.group(4):
            out.append("*")
        elif m.group(5):
            out.append("+")
        elif m.group(6):
            out.append("-")
        elif m.group(7):
            out.append("(")
        elif m.group(8):
            out.append(")")
        # whitespace ignored

    # sizeof(type) -> the number, wherever it stands
    py = "".join(out)
    py = re.sub(r"__SIZEOF__\(\s*([\w:]+)\s*\)",
                lambda m: str(sizes.get(m.group(1), 0)) if
                m.group(1) in sizes else "__NO__", py)
    if "__NO__" in py:
        return None
    if not re.fullmatch(r"[\d\s()+\-*]+", py):
        return None
    try:
        val = eval(py, {"__builtins__": {}}, {})
    except Exception:
        return None
    return int(val) if isinstance(val, (int, float)) else None


# Sizes that are read back from the display at runtime instead of being written
# down at the call site: the screenshot buffer asks the display how big it is,
# which is the right thing for the code to do. For a report the same value has
# to come from somewhere, and the chain that makes it available is short: the
# display is created with LCD_H_RES x LCD_V_RES in src/display/Display.cpp. So
# the two queries are mapped back to those constants - and any number that comes
# through this way is printed as derived, because it is one indirection away
# from the thing the code actually asks.
DISPLAY_QUERIES = {
    "lv_display_get_horizontal_resolution": "LCD_H_RES",
    "lv_display_get_vertical_resolution": "LCD_V_RES",
    "lv_disp_get_hor_res": "LCD_H_RES",
    "lv_disp_get_ver_res": "LCD_V_RES",
}


def _substitute_queries(expr, consts):
    """Replace a runtime display query by the build's resolution constant.

    Returns (expression, derived) - derived is True if a query was replaced, so
    the caller can label the result instead of passing it off as a literal.
    """
    derived = False
    # A cast in front of the query or of a name adds nothing to a product but
    # confuses the tokenizer, so it goes. Only these exact casts, and only when
    # they stand on their own: "(int)" may not be reduced to a bracket, or the
    # expression behind it loses its grouping. "(int)lv_display_..." has to go
    # before the query is replaced, because the cast's bracket and the call's
    # own bracket cannot be told apart afterwards.
    expr = re.sub(r"\(\s*(?:u?int\d+_t|size_t|int|unsigned|long)\s*\)"
                  r"(?=\s*[A-Za-z_])", "", expr)
    for call, konst in DISPLAY_QUERIES.items():
        if call + "(" in expr:
            derived = True
            expr = re.sub(re.escape(call) + r"\s*\([^)]*\)", konst, expr)
    return expr, derived


def resolve_local(name, text, upto, consts, sizes, depth=0):
    """Follow a size variable back to its assignment: need -> w*h*2.

    A size that lives in a local is not a constant, and a tool that says
    "unknown" for every such line is a tool nobody reads. The search walks back
    over the lines above the call and evaluates the first assignment it finds,
    with names resolved recursively up to a small depth. A name it cannot
    resolve stays unknown rather than becoming a guess.

    Returns (bytes, derived) - derived is True when the value came from the
    display resolution rather than from a literal in the expression.
    """
    if depth > 5:
        return None, False
    lines = text[:upto].splitlines()
    for cand in reversed(lines[-10:]):
        m = re.search(r"\b" + re.escape(name) + r"\s*=\s*([^;]+);", cand)
        if not m:
            continue
        expr, derived = _substitute_queries(m.group(1), consts)
        val = eval_size(expr, consts, sizes)
        if val is not None:
            return val, derived
        # The assignment may refer to locals of its own - "need" is w*h*2, and
        # both w and h have to be resolved before the product means anything.
        # Resolving them one at a time and giving up after the first is how a
        # tool ends up reporting "unknown" for the one size everybody knows.
        work = expr
        deep_derived = False
        for inner in re.findall(r"[A-Za-z_]\w*", expr):
            if inner in sizes or inner in consts:
                continue
            if inner in ("sizeof", "int", "float", "unsigned", "long"):
                continue
            deep, inner_derived = resolve_local(inner, text, upto,
                                                 consts, sizes, depth + 1)
            if deep is None:
                continue
            work = re.sub(r"\b" + re.escape(inner) + r"\b", str(deep), work)
            deep_derived = deep_derived or inner_derived
        val = eval_size(work, consts, sizes)
        if val is not None:
            return val, derived or deep_derived
    if name in consts:
        return consts[name], False
    return None, False


def elf_sections(project, env, elf_name="firmware.elf"):
    """The section sizes the toolchain's own "size -A" reports, or None.

    The map is the better source for the split by area, and the worse source for
    the total: it only knows what belongs to an object, so the vectors, the app
    descriptor and whatever the linker script places on its own are in the
    binary but not in the map's per-object lines. The difference is a few
    kilobytes and it lands in the framework's direction, which is the wrong way
    for a report to be wrong.

    So the totals come from the ELF and the areas from the map, and the report
    says which is which.
    """
    elf = os.path.join(project, ".pio", "build", env, elf_name)
    if not os.path.isfile(elf):
        return None
    binary = find_size_tool()
    if not binary:
        return None
    try:
        out = subprocess.run([binary, "-A", elf], capture_output=True,
                             text=True, timeout=120).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    sections = {}
    for line in out.splitlines():
        parts = line.split()
        # "size -A" prints name, size, address - three fields, and the name
        # padded with spaces. Expecting two finds nothing at all, which is how
        # the total came out as zero for a while.
        if len(parts) < 2 or not parts[1].isdigit():
            continue
        if not parts[0].startswith("."):
            continue
        sections[parts[0]] = int(parts[1])
    return sections or None


def find_size_tool():
    """Look for the toolchain's size in the PlatformIO packages directory."""
    base = os.path.expanduser("~/.platformio/packages")
    if not os.path.isdir(base):
        return None
    for pkg in sorted(os.listdir(base)):
        if not pkg.startswith("toolchain-"):
            continue
        bindir = os.path.join(base, pkg, "bin")
        if not os.path.isdir(bindir):
            continue
        for fn in sorted(os.listdir(bindir)):
            if fn.endswith("-elf-size"):
                return os.path.join(bindir, fn)
    return None


# The sections that occupy the app partition, and the sections that make up the
# static RAM. The dummy sections are zero-byte placeholders and are left out.
FLASH_SECTION_NAMES = (".flash.text", ".flash.rodata", ".flash.rodata_noload",
                       ".flash.appdesc", ".iram0.vectors", ".iram0.text",
                       ".iram0.data", ".iram0.bss")
RAM_SECTION_NAMES = (".dram0.data", ".dram0.bss", ".noinit", ".rtc_noinit",
                     ".iram0.data", ".iram0.bss")


def totals_from_elf(sections):
    """(flash, ram) out of an ELF section table, or (None, None)."""
    if not sections:
        return None, None
    flash = sum(sections.get(n, 0) for n in FLASH_SECTION_NAMES)
    ram = sum(sections.get(n, 0) for n in RAM_SECTION_NAMES)
    return flash, ram


def psram_from_source(root):
    """The PSRAM allocations, found in the code that asks for them.

    These are heap allocations: not in the map file, not in the static RAM
    total, and on a board without PSRAM they are what decides whether the
    display comes up at all. They are read out of the source with the constants
    resolved, and an expression that stays unresolved is printed as such - a
    number that cannot be believed is worse than a gap that can be seen.
    """
    consts, sizes = scan_constants(root)
    found = []
    skip_dirs = {".pio", ".git", "tools", "docs", "node_modules"}
    for dirpath, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in skip_dirs]
        for fn in sorted(files):
            if not fn.endswith((".cpp", ".c")):
                continue
            path = os.path.join(dirpath, fn)
            try:
                with open(path, "r", errors="replace") as fh:
                    text = fh.read()
            except OSError:
                continue
            # The whole file at once, not line by line: a size expression of two
            # or three lines is how clang-format leaves it, and a call only found
            # when it fits on one line is a call that is not found. The two LVGL
            # line buffers are written that way.
            allocs = []
            for m in ALLOC_RE.finditer(text):
                line = text.count("\n", 0, m.start()) + 1
                expr = " ".join(m.group(2).split())
                expr = re.sub(r"MALLOC_CAP_\w+\s*\|?\s*", "", expr)
                expr = expr.strip().rstrip(",").strip()
                note = expr[:46]
                derived = False
                size = eval_size(expr, consts, sizes)
                if size is None and re.fullmatch(r"[\w:]+", expr):
                    bare = expr.split("::")[-1]
                    size, derived = resolve_local(bare, text, m.start(),
                                                  consts, sizes)
                if derived:
                    note = "%s  [aus der Anzeigeauflösung]" % note
                allocs.append([path, line, size, note, derived, expr, False,
                               m.group(1).strip()])

            # Two allocation calls do not always mean two buffers. The usual
            # second call is the fallback for when the first one failed - a
            # request for PSRAM that is repeated against internal memory, or a
            # buffer guarded by "if (!buf)". Adding those up would double the
            # two largest lines in this report and make the total a fiction.
            #
            # What tells the two apart is the variable, not the size: buf1 and
            # buf2 of the same size are two live buffers, while the same
            # variable assigned twice is one buffer with a second attempt. The
            # size expression alone cannot decide it, and guessing it wrong
            # costs 450 kB in either direction.
            by_target = {}
            for a in allocs:
                prev = by_target.get((a[0], a[7]))
                if prev is not None:
                    a[6] = True            # the later call is the fallback
                else:
                    by_target[(a[0], a[7])] = a

            for a in allocs:
                path, line, size, note, derived, _expr, is_fallback, _target = a
                if is_fallback:
                    note = "%s  (2. Versuch)" % note
                if size is None:
                    found.append((path, line, None,
                                  "nicht aufloesbar: %s" % note))
                    continue
                found.append((path, line, size,
                              "%s = %s" % (note, human(size))))
    return found


def parse_baseline(text):
    """Read a previous report back in."""
    vals = {}
    for line in text.splitlines():
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "FLASH_TOTAL" and len(parts) > 1:
            vals["flash"] = int(parts[1])
        elif parts[0] == "RAM_TOTAL" and len(parts) > 1:
            vals["ram"] = int(parts[1])
        elif parts[0] == "AREA" and len(parts) > 3:
            vals["area:" + parts[1]] = (int(parts[2]), int(parts[3]))
    return vals


def print_table(keys, areas, field, title):
    shown = [k for k in keys if areas.get(k, {}).get(field, 0) > 0]
    if not shown:
        return 0
    total = sum(areas[k][field] for k in shown)
    print("%s  -  %s gesamt" % (title, human(total)))
    for k in shown:
        v = areas[k]
        print("  %-26s %9s   %4d Objekte" % (k, human(v[field]),
                                             len(v["objects"])))
    print()
    return total


def main():
    ap = argparse.ArgumentParser(
        description="Groessenbericht eines Builds aus der Linker-Map")
    ap.add_argument("--env", default="esp32-s3",
                    help="PlatformIO-Umgebung (Vorgabe esp32-s3)")
    ap.add_argument("--map", help="Pfad zur .map-Datei")
    ap.add_argument("--project", default=".", help="Projektwurzel")
    ap.add_argument("--baseline", help="ein alter Bericht zum Vergleich")
    ap.add_argument("--save", help="den Bericht zusaetzlich in eine Datei")
    ap.add_argument("--no-psram", action="store_true",
                    help="die PSRAM-Suche im Quelltext ueberspringen")
    args = ap.parse_args()

    map_path = args.map or find_map(args.project, args.env)
    if not map_path or not os.path.isfile(map_path):
        sys.exit(
            "Map-Datei nicht gefunden (%s).\n"
            "Erwartet wird eine von PlatformIO erzeugte .map-Datei; "
            "Pfad mit --map uebergeben." % (map_path or "nicht ermittelt")
        )

    with open(map_path, "r", errors="replace") as fh:
        lines = fh.read().splitlines()

    sections = parse_sections(lines)
    regions = parse_regions(lines)
    areas = collect(sections)

    flash = sum(a["flash"] for a in areas.values())
    ram = sum(a["ram"] for a in areas.values())
    debug = sum(s for n, s, _ in sections if n.startswith(".debug"))

    ours = sorted(k for k in areas if not k.startswith("framework"))
    fw = sorted(k for k in areas if k.startswith("framework"))

    print("Groessenbericht")
    print("  Map     %s" % map_path)
    print("  Build   %s" % args.env)
    print("  Zeilen  %d, davon %d Sektionen nach dem Merge, %d debug"
          % (len(lines), len(sections), sum(1 for n, _, _ in sections
                                             if n.startswith(".debug"))))
    print()

    print_table(ours, areas, "flash", "Eigener Flash")
    print_table(ours, areas, "ram", "Eigener RAM")
    fw_flash = print_table(fw, areas, "flash", "Framework-Flash")

    if flash:
        print("Davon Framework: %.1f %% des gelinkten Flash\n"
              % (100.0 * fw_flash / flash))

    # The map's per-object lines miss what belongs to no object, so the totals
    # come from the ELF where there is one. flash_map and ram_map stay for the
    # note underneath: the gap between them is what a reader of the map alone
    # silently leaves out.
    elf = elf_sections(args.project, args.env)
    flash_elf, ram_elf = totals_from_elf(elf)
    quelle = "ELF (size -A)" if flash_elf is not None else "Map"
    flash_total = flash_elf if flash_elf is not None else flash
    ram_total = ram_elf if ram_elf is not None else ram

    slot = app_partition_size(args.project)
    print("Flash gelinkt    %9s   (Summe aus der %s)"
          % (human(flash_total), quelle))
    if slot:
        print("Flash je App-Part %9s   (%4.1f %% belegt, %s frei)"
              % (human(slot), 100.0 * flash_total / slot,
                 human(slot - flash_total)))
    else:
        print("Flash je App-Part       keine Partitionstabelle gefunden")
    print("RAM statisch     %9s von %8s  (%4.1f %%)   (Summe aus der %s)"
          % (human(ram_total), human(DRAM_TOTAL),
             100.0 * ram_total / DRAM_TOTAL, quelle))
    print("Debug im Build   %9s   (gehoert nicht ins Geraet)"
          % human(debug))
    if flash_elf is not None and abs(flash_elf - flash) >= 1024:
        print("Hinweis: die Objektsektionen der Map summieren sich auf %s, das "
              "ELF auf %s." % (human(flash), human(flash_elf)))
        print("         Fuer die Gesamtzahl gilt das ELF: was keinem Objekt "
              "gehoert,")
        print("         liegt nur dort - der Resetvektor und die "
              "App-Beschreibung.")

    if regions:
        print("\nRegionen aus der Map")
        for name in sorted(regions):
            print("  %-18s %10s" % (name, human(regions[name])))

    if not args.no_psram:
        found = psram_from_source(args.project)
        if found:
            print("\nPSRAM - aus dem Quelltext, nicht aus der Map")
            for path, line, size, note in found:
                rel = os.path.relpath(path, args.project)
                print("  %-46s  %s:%d" % (note, rel, line))
            known = [s for _, _, s, n in found
                     if s and "2. Versuch" not in n]
            if known:
                print("  %-46s  %s" % ("Summe der Puffer", human(sum(known))))
            if any("nicht aufloesbar" in n for _, _, _, n in found):
                print("  Die als nicht aufloesbar markierten Zeilen sind kein "
                      "Fehler im Build, sondern eine Lücke dieses Werkzeugs.")

    out = []
    out.append("FLASH_TOTAL %d" % flash_total)
    out.append("RAM_TOTAL %d" % ram_total)
    for k in sorted(areas):
        out.append("AREA\t%s\t%d\t%d" % (k, areas[k]["flash"], areas[k]["ram"]))
    protokoll = "\n".join(out)

    print("\n--- Protokoll ---")
    print(protokoll)

    if args.baseline:
        try:
            with open(args.baseline, "r", errors="replace") as fh:
                old = parse_baseline(fh.read())
        except OSError as exc:
            print("\nBasis nicht lesbar: %s" % exc)
            return 0
        print("\n--- Vergleich mit %s ---" % args.baseline)
        for key, now, label in (("flash", flash_total, "Flash gelinkt"),
                                ("ram", ram_total, "RAM statisch")):
            if key not in old:
                continue
            delta = now - old[key]
            # A difference that rounds away in kB should still be visible as a
            # byte count: "0 B" next to two megabyte figures looks like a bug
            # in the comparison, not like the truth.
            print("%-16s %+10s (%+d B)   %s -> %s"
                  % (label, human(delta), delta, human(old[key]), human(now)))
        for k in sorted(areas):
            prev = old.get("area:" + k)
            if not prev:
                continue
            for idx, label in ((0, "Flash"), (1, "RAM")):
                field = ("flash", "ram")[idx]
                d = areas[k][field] - prev[idx]
                if abs(d) >= 512:
                    print("  %-26s %-5s %+10s" % (k, label, human(d)))
        for k in sorted(set(k[5:] for k in old if k.startswith("area:")) -
                        set(areas)):
            print("  %-26s %-5s %+10s  (Bereich entfallen)" % (k, "Flash",
                                                                "?"))

    if args.save:
        d = os.path.dirname(args.save)
        if d:
            os.makedirs(d, exist_ok=True)
        with open(args.save, "w") as fh:
            fh.write(protokoll + "\n")
        print("\nProtokoll gesichert: %s" % args.save)
    return 0


if __name__ == "__main__":
    sys.exit(main())