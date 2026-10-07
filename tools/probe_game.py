"""Read-only Dishonored 2 install/PE probe. A camera match is NOT VR support."""
from __future__ import annotations

import argparse
import bisect
from datetime import datetime, timezone
import hashlib
import json
import mmap
from pathlib import Path
import re
import struct
import sys

# Published IGCS camera/Ansel research, copyright 2017 Frans Bouma (BSD-2-Clause).
# Source commit and retained notice: ../THIRD_PARTY_NOTICES.md.
SIGNATURES = {
    "ansel_start_session": "48 83 EC 28 48 8B 0D ?? ?? ?? ?? 48 85 C9 0F 84 ?? ?? ?? ?? 48 8B 01 FF 90 ?? ?? ?? ?? 84 C0",
    "ansel_stop_session": "48 83 EC 28 8B 15 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? 41 B0 01 E8",
    "ansel_camera_xyz_store": "F3 0F 11 05 ?? ?? ?? ?? F3 0F 10 8F ?? 3A 00 00 F3 0F 11 0D ?? ?? ?? ?? F3 0F 10 87 ?? 3A 00 00",
    "pause_on_focus_loss_reference": "83 3D ?? ?? ?? ?? 00 74 4E 48 8B 0D ?? ?? ?? ?? 48 8B 01",
}

RESEARCH_STRINGS = (
    "Ansel_PlayerVisible", "pm_thirdPerson", "pm_thirdPersonFocusJoint",
    "stereoRender_separation", "stereoRender_screenSeparation", "stereoRender_swapEyes",
    "cpnt_poolsize_arkCpntRenderView", "com_skipGameRenderView",
)

RESEARCH_TYPES = ("arkCpntPlayerCamera", "arkCpntRenderView", "arkCpntPlayerCameraFactory",
                  "arkCpntRenderViewFactory", "endJoint@arkFabrIK_Toolkit",
                  "transformsBridge@arkFabrIK")


def vdf(text: str) -> dict:
    """Parse Steam's quoted key/value objects; reject damaged manifests."""
    tokens = [token for token in re.findall(r'//[^\r\n]*|"(?:\\.|[^"\\])*"|[{}]|\S+', text)
              if not token.startswith("//")]
    index = 0

    def quoted(token: str) -> str:
        if not (len(token) >= 2 and token[0] == token[-1] == '"'):
            raise ValueError("Unquoted Steam manifest token")
        return re.sub(r'\\([\\"])', r'\1', token[1:-1])

    def obj(nested: bool = False, depth: int = 0) -> dict:
        nonlocal index
        if depth > 32:
            raise ValueError("Manifest nesting exceeds limit")
        result = {}
        while index < len(tokens):
            if tokens[index] == "}":
                if not nested:
                    raise ValueError("Unexpected manifest closing brace")
                index += 1
                return result
            key = quoted(tokens[index])
            index += 1
            if index == len(tokens):
                raise ValueError("Missing manifest value")
            token = tokens[index]
            index += 1
            value = obj(True, depth + 1) if token == "{" else quoted(token)
            if key in result:
                raise ValueError("Duplicate manifest key: " + key)
            result[key] = value
        if nested:
            raise ValueError("Unclosed manifest object")
        return result

    return obj()


class PE:
    def __init__(self, data):
        self.data = data
        if self.bytes(0, 2) != b"MZ":
            raise ValueError("Not a Windows PE executable")
        pe = self.unpack("<I", 0x3C)[0]
        if self.bytes(pe, 4) != b"PE\0\0":
            raise ValueError("Invalid PE signature")
        self.machine, count, self.timestamp, _, _, optional_size, _ = self.unpack("<HHIIIHH", pe + 4)
        optional = pe + 24
        self.bytes(optional, optional_size)
        if optional_size < 112 or self.unpack("<H", optional)[0] != 0x20B:
            raise ValueError("Expected a PE32+ executable")
        if not 1 <= count <= 96:
            raise ValueError("Invalid section count")
        self.image_base = self.unpack("<Q", optional + 24)[0]
        self.image_size = self.unpack("<I", optional + 56)[0]
        self.header_size = self.unpack("<I", optional + 60)[0]
        directory_count = self.unpack("<I", optional + 108)[0]
        if self.header_size > len(data) or self.image_size < self.header_size:
            raise ValueError("Invalid PE image/header sizes")
        if directory_count * 8 > optional_size - 112:
            raise ValueError("Truncated PE directory table")
        self.import_directory = self.unpack("<II", optional + 120) if directory_count > 1 else (0, 0)
        self.export_directory = self.unpack("<II", optional + 112) if directory_count > 0 else (0, 0)
        self.exception_directory = self.unpack("<II", optional + 136) if directory_count > 3 else (0, 0)
        self.sections = []
        for i in range(count):
            at = optional + optional_size + i * 40
            name, virtual_size, rva, raw_size, raw_offset, _, _, _, _, flags = self.unpack("<8sIIIIIIHHI", at)
            self.bytes(raw_offset, raw_size)
            if rva + max(virtual_size, raw_size) > self.image_size:
                raise ValueError("Section exceeds PE image")
            self.sections.append(dict(name=name.rstrip(b"\0").decode("ascii", errors="replace"),
                                      rva=rva, virtual_size=virtual_size, raw_size=raw_size,
                                      raw_offset=raw_offset, executable=bool(flags & 0x20000000)))
        self._functions = None

    def bytes(self, offset: int, size: int) -> bytes:
        if offset < 0 or size < 0 or offset + size > len(self.data):
            raise ValueError("PE field lies outside file")
        return self.data[offset:offset + size]

    def unpack(self, fmt: str, offset: int):
        return struct.unpack(fmt, self.bytes(offset, struct.calcsize(fmt)))

    def offset(self, rva: int, size: int = 1) -> int:
        if size <= 0:
            raise ValueError("Invalid requested RVA size")
        if 0 <= rva and rva + size <= self.header_size:
            return rva
        matches = [s["raw_offset"] + rva - s["rva"] for s in self.sections
                   if s["rva"] <= rva and rva + size <= s["rva"] + s["raw_size"]]
        if len(matches) != 1:
            raise ValueError("Unmapped or ambiguous file-backed RVA")
        return matches[0]

    def imports(self) -> list[str]:
        rva, size = self.import_directory
        if not rva:
            return []
        names = []
        # Descriptors, names, and their terminators must be fully file-backed.
        for relative in range(0, min(size, 20 * 4096) - 19, 20):
            descriptor = self.unpack("<IIIII", self.offset(rva + relative, 20))
            if not any(descriptor):
                return sorted(set(names))
            name_rva = descriptor[3]
            name = bytearray()
            for i in range(512):
                char = self.bytes(self.offset(name_rva + i), 1)
                if char == b"\0":
                    break
                name.extend(char)
            else:
                raise ValueError("Unterminated imported DLL name")
            names.append(name.decode("ascii").lower())
        raise ValueError("Unterminated PE import directory")

    def code_export(self, name: str) -> int:
        """Return one direct executable export RVA, rejecting forwarders."""
        rva, size = self.export_directory
        if not rva or size < 40:
            raise ValueError("Missing export directory")
        directory = self.unpack("<IIHHIIIIIII", self.offset(rva, 40))
        functions, names, function_table, name_table, ordinal_table = directory[6:]
        if not 0 < functions <= 65536 or not 0 < names <= 65536:
            raise ValueError("Invalid export table count")
        self.offset(function_table, functions*4)
        self.offset(name_table, names*4)
        self.offset(ordinal_table, names*2)
        found = []
        for index in range(names):
            name_rva = self.unpack("<I", self.offset(name_table+index*4, 4))[0]
            export_name = bytearray()
            for offset in range(512):
                char = self.bytes(self.offset(name_rva+offset), 1)
                if char == b"\0":
                    break
                export_name.extend(char)
            else:
                raise ValueError("Unterminated export name")
            if export_name.decode("ascii") == name:
                ordinal = self.unpack("<H", self.offset(ordinal_table+index*2, 2))[0]
                if ordinal >= functions:
                    raise ValueError("Export ordinal outside function table")
                target = self.unpack("<I", self.offset(function_table+ordinal*4, 4))[0]
                if rva <= target < rva+size or not any(
                        s["executable"] and s["rva"] <= target < s["rva"]+s["raw_size"] for s in self.sections):
                    raise ValueError("Export is forwarded or not executable")
                found.append(target)
        if len(found) != 1:
            raise ValueError("Expected exactly one code export: "+name)
        return found[0]

    def signature_matches(self, pattern: str) -> list[dict]:
        tokens = pattern.split()
        expression = b"".join(b"." if token == "??" else re.escape(bytes([int(token, 16)]))
                              for token in tokens)
        regex = re.compile(b"(?=(" + expression + b"))", re.DOTALL)
        found = []
        for section in self.sections:
            if not section["executable"]:
                continue
            start = section["raw_offset"]
            # Limit each scan to one section so matches cannot straddle padding.
            for match in regex.finditer(self.data, start, start + section["raw_size"]):
                found.append(dict(file_offset=match.start(),
                                  rva=section["rva"] + match.start() - start,
                                  section=section["name"]))
                if len(found) == 32:
                    return found  # 32 matches are enough to reject uniqueness.
        return found

    def string_matches(self, text: str) -> list[dict]:
        needle = text.encode("ascii") + b"\0"
        found = []
        for section in self.sections:
            start = section["raw_offset"]
            end = start + section["raw_size"]
            at = self.data.find(needle, start, end)
            while at >= 0 and len(found) < 32:
                found.append(dict(file_offset=at, rva=section["rva"] + at-start,
                                  section=section["name"]))
                at = self.data.find(needle, at + 1, end)
        return found

    def function_at(self, rva: int) -> dict | None:
        """x64 runtime-function boundaries are research aids, not safe hook lengths."""
        if self._functions is None:
            start, size = self.exception_directory
            self._functions = []
            if start:
                if size % 12:
                    raise ValueError("Malformed x64 runtime-function table")
                data = self.bytes(self.offset(start, size), size)
                for begin, end, unwind in struct.iter_unpack("<III", data):
                    if not 0 <= begin < end <= self.image_size:
                        raise ValueError("Invalid runtime-function bounds")
                    self._functions.append((begin, end, unwind))
                self._functions.sort()
            self._function_starts = [entry[0] for entry in self._functions]
        index = bisect.bisect_right(self._function_starts, rva) - 1
        if index >= 0:
            begin, end, unwind = self._functions[index]
            if begin <= rva < end:
                return dict(begin_rva=begin, end_rva=end, unwind_rva=unwind)
        return None

    def relative_call_candidates(self, target_rva: int) -> list[dict]:
        found = []
        for section in self.sections:
            if not section["executable"]:
                continue
            start, end = section["raw_offset"], section["raw_offset"]+section["raw_size"]
            at = self.data.find(b"\xe8", start, end)
            while at >= 0:
                if at+5 <= end:
                    rva = section["rva"]+at-start
                    if rva+5+self.unpack("<i", at+1)[0] == target_rva:
                        found.append(dict(rva=rva, file_offset=at, containing_unwind_entry=self.function_at(rva),
                                          instruction_boundary_verified=False))
                at = self.data.find(b"\xe8", at+1, end)
        return found

    def rtti_vtables(self, name: str) -> list[dict]:
        """Locate MSVC x64 RTTI/vtable candidates using rttidata.h's REV1 layout."""
        found = []
        for typename in self.string_matches(".?AV" + name + "@@"):
            descriptor_rva = typename["rva"] - 16
            if descriptor_rva < 0:
                continue
            reference = struct.pack("<I", descriptor_rva)
            for section in self.sections:
                if section["executable"]:
                    continue
                start, end = section["raw_offset"], section["raw_offset"] + section["raw_size"]
                at = self.data.find(reference, start, end)
                while at >= 0:
                    locator_offset = at - 12
                    if start <= locator_offset <= end - 24:
                        signature, object_offset, constructor_offset, type_rva, _, self_rva = self.unpack("<IIIIII", locator_offset)
                        locator_rva = section["rva"] + locator_offset - start
                        if signature == 1 and type_rva == descriptor_rva and self_rva == locator_rva:
                            pointer = struct.pack("<Q", self.image_base + locator_rva)
                            table_header = self.data.find(pointer, start, end)
                            while table_header >= 0:
                                table_rva = section["rva"] + table_header + 8 - start
                                if table_rva % 8 == 0:
                                    methods = []
                                    for slot in range(64):
                                        entry = table_header + 8 + slot * 8
                                        if entry + 8 > end:
                                            break
                                        method_rva = self.unpack("<Q", entry)[0] - self.image_base
                                        if not any(s["executable"] and s["rva"] <= method_rva <
                                                   s["rva"] + s["virtual_size"] for s in self.sections):
                                            break
                                        methods.append(dict(slot=slot, rva=method_rva,
                                                            function=self.function_at(method_rva)))
                                    if methods:
                                        found.append(dict(type_descriptor_rva=descriptor_rva,
                                            locator_rva=locator_rva, vtable_rva=table_rva,
                                            subobject_offset=object_offset, constructor_offset=constructor_offset,
                                            first_virtual_methods=methods, live_object_verified=False))
                                table_header = self.data.find(pointer, table_header + 1, end)
                    at = self.data.find(reference, at + 1, end)
        return found


def steam_root() -> Path | None:
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as key:
            return Path(winreg.QueryValueEx(key, "SteamPath")[0])
    except (ImportError, OSError):
        return None


def openxr_runtime() -> dict:
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Khronos\OpenXR\1",
                            0, winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as key:
            path = Path(winreg.QueryValueEx(key, "ActiveRuntime")[0])
        result = dict(manifest=str(path), registered=True, headset_tested=False)
        manifest = json.loads(path.read_text(encoding="utf-8-sig"))
        library = Path(manifest["runtime"]["library_path"])
        if not library.is_absolute():
            library = path.parent / library
        result.update(library=str(library.resolve()), library_exists=library.is_file())
        return result
    except (ImportError, OSError, ValueError, KeyError) as error:
        return dict(registered=False, headset_tested=False, error=str(error))


def installation(game: Path) -> dict:
    # Standard Steam library layout, with no reads from account/login files.
    manifest_path = game.parent.parent / "appmanifest_403640.acf"
    result = dict(folder=str(game), exists=game.is_dir(), manifest=str(manifest_path))
    if manifest_path.is_file():
        state = vdf(manifest_path.read_text(encoding="utf-8-sig"))["AppState"]
        if state.get("appid") != "403640":
            raise ValueError("Unexpected app ID in Dishonored 2 manifest")
        for key in ("StateFlags", "buildid", "TargetBuildID", "BytesToDownload", "BytesDownloaded", "BytesToStage", "BytesStaged"):
            if key in state:
                result[key] = int(state[key])
        result["steam_install_complete"] = result.get("StateFlags") == 4
    return result


def inspect_exe(path: Path) -> dict:
    before = path.stat()
    with path.open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
        pe = PE(data)
        if pe.machine != 0x8664:
            raise ValueError("Dishonored 2 research requires an x64 executable")
        signatures = {}
        for name, pattern in SIGNATURES.items():
            matches = pe.signature_matches(pattern)
            for match in matches:
                match["containing_function"] = pe.function_at(match["rva"])
            signatures[name] = dict(matches=matches, unique_candidate=len(matches) == 1,
                                    scan_limit=32, validated_for_patching=False)
        camera = signatures["ansel_camera_xyz_store"]
        if camera["unique_candidate"]:
            match = camera["matches"][0]
            displacement = pe.unpack("<i", match["file_offset"] + 4)[0]
            target = match["rva"] + 8 + displacement
            try:
                offset = pe.offset(target, 32)
                camera["candidate_camera_struct"] = dict(rva=target, file_offset=offset,
                    position_offset=0, quaternion_offset=12, fov_offset=28,
                    role="Ansel/photo camera candidate; gameplay role unverified")
            except ValueError:
                camera["candidate_camera_struct"] = dict(rva=target,
                    role="RIP target is not a file-backed 32-byte struct; runtime inspection needed")
        imports = pe.imports()
        report = dict(path=str(path), size=before.st_size, sha256=hashlib.sha256(data).hexdigest(),
                      architecture="x64", preferred_image_base=pe.image_base,
                      image_size=pe.image_size, coff_timestamp=pe.timestamp, sections=pe.sections,
                      imported_dlls=imports, static_d3d11_import="d3d11.dll" in imports,
                      camera_research=signatures,
                      research_strings={text: pe.string_matches(text) for text in RESEARCH_STRINGS},
                      engine_type_candidates={name: pe.rtti_vtables(name) for name in RESEARCH_TYPES},
                      compatibility="unverified", live_process_read=False)
    after = path.stat()
    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
        raise ValueError("Executable changed during scan; wait for Steam to finish and rerun")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, help="Installed Dishonored2 folder")
    parser.add_argument("--exe", type=Path, help="Explicit executable for offline binary research")
    parser.add_argument("--output", type=Path, help="Save JSON inside the workspace; never in the game folder")
    args = parser.parse_args()
    root = steam_root()
    game = args.game or (root / "steamapps/common/Dishonored2" if root else None)
    report = dict(time_utc=datetime.now(timezone.utc).isoformat(),
                  status="waiting_for_game", vr_mod_ready=False, game_writes=False,
                  openxr=openxr_runtime(), blockers=[])
    code = 3
    try:
        if game:
            game = game.resolve()
            report["installation"] = installation(game)
        executable = args.exe or (game / "Dishonored2.exe" if game else None)
        if executable and executable.is_file():
            if args.exe or report.get("installation", {}).get("steam_install_complete"):
                report["executable"] = inspect_exe(executable.resolve())
                report["status"] = "binary_research_available"
                report["blockers"] = ["Gameplay camera, stereo frame path, player rig and input hooks are unverified."]
                code = 0
            else:
                report["blockers"].append("Steam installation/update is incomplete; executable scan deferred.")
        else:
            report["blockers"].append("Dishonored2.exe is not installed at the specified location.")
    except (OSError, ValueError, KeyError, UnicodeError) as error:
        report["status"] = "probe_failed"
        report["blockers"].append(str(error))
        code = 1
    payload = json.dumps(report, indent=2)
    if args.output:
        workspace = Path(__file__).resolve().parent.parent
        output = args.output.resolve()
        if not output.is_relative_to(workspace) or (game and output.is_relative_to(game)):
            parser.error("--output must be inside the project workspace and outside the game installation")
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(payload + "\n", encoding="utf-8")
    print(payload)
    return code


if __name__ == "__main__":
    sys.exit(main())
