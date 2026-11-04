from __future__ import annotations

import dataclasses
import struct


PE_I386 = 0x014C
IMAGE_FILE_DLL = 0x2000
FORBIDDEN_WINDOWS_RUNTIME_DLLS = frozenset(
    {
        "libgcc_s_dw2-1.dll",
        "libstdc++-6.dll",
        "libwinpthread-1.dll",
        "libz-1.dll",
        "z-1.dll",
        "zlib1.dll",
        "libzstd.dll",
        "msvcp140.dll",
        "msvcp140_1.dll",
        "msvcp140_2.dll",
        "vcruntime140.dll",
        "vcruntime140_1.dll",
    }
)


@dataclasses.dataclass(frozen=True)
class PeInfo:
    machine: int
    is_dll: bool
    imports: tuple[str, ...]


def _unpack_from(fmt: str, data: bytes, offset: int, name: str) -> tuple[int, ...]:
    size = struct.calcsize(fmt)
    if offset < 0 or offset + size > len(data):
        raise ValueError(f"{name} has a truncated PE structure")
    return struct.unpack_from(fmt, data, offset)


def _read_c_string(data: bytes, offset: int, name: str) -> str:
    if offset < 0 or offset >= len(data):
        raise ValueError(f"{name} has an invalid PE import name offset")
    end = data.find(b"\0", offset, min(len(data), offset + 512))
    if end < 0:
        raise ValueError(f"{name} has an unterminated PE import name")
    try:
        return data[offset:end].decode("ascii")
    except UnicodeDecodeError as exc:
        raise ValueError(f"{name} has a non-ASCII PE import name") from exc


MAX_IMPORT_DESCRIPTORS = 4096
MAX_IMPORTS_PER_DLL = 65536
IMPORT_ORDINAL_FLAG_32 = 0x8000_0000
IMPORT_ORDINAL_FLAG_64 = 0x8000_0000_0000_0000


def parse_pe(data: bytes, *, name: str = "binary") -> PeInfo | None:
    """Return bounded PE metadata, or None when data is not a PE image."""
    parsed = _parse_pe(data, name=name, collect_functions=False)
    return None if parsed is None else parsed[0]


def parse_pe_imported_functions(
    data: bytes, *, name: str = "binary"
) -> dict[str, tuple[str, ...]] | None:
    """Return named imports keyed by case-folded DLL name, or None for non-PE data.

    Ordinal imports carry no name and are omitted. This is separate from
    parse_pe so runtime-dependency validation never depends on thunk parsing.
    """
    parsed = _parse_pe(data, name=name, collect_functions=True)
    return None if parsed is None else parsed[1]


def _parse_pe(
    data: bytes, *, name: str, collect_functions: bool
) -> tuple[PeInfo, dict[str, tuple[str, ...]]] | None:
    if len(data) < 2 or data[:2] != b"MZ":
        return None
    if len(data) < 64:
        raise ValueError(f"{name} has a truncated DOS header")

    (pe_offset,) = _unpack_from("<I", data, 60, name)
    if pe_offset < 64 or pe_offset + 24 > len(data):
        raise ValueError(f"{name} has an invalid PE header offset")
    if data[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise ValueError(f"{name} has an invalid PE signature")

    machine, section_count, _timestamp, _symbol_table, _symbol_count, optional_size, characteristics = (
        _unpack_from("<HHIIIHH", data, pe_offset + 4, name)
    )
    optional_offset = pe_offset + 24
    if optional_offset + optional_size > len(data):
        raise ValueError(f"{name} has a truncated PE optional header")
    (optional_magic,) = _unpack_from("<H", data, optional_offset, name)
    if optional_magic == 0x10B:
        directory_count_offset = optional_offset + 92
        directory_offset = optional_offset + 96
        thunk_format = "<I"
        ordinal_flag = IMPORT_ORDINAL_FLAG_32
    elif optional_magic == 0x20B:
        directory_count_offset = optional_offset + 108
        directory_offset = optional_offset + 112
        thunk_format = "<Q"
        ordinal_flag = IMPORT_ORDINAL_FLAG_64
    else:
        raise ValueError(f"{name} has unsupported PE optional magic 0x{optional_magic:04x}")

    (directory_count,) = _unpack_from("<I", data, directory_count_offset, name)
    import_rva = 0
    import_size = 0
    if directory_count > 1 and directory_offset + 16 <= optional_offset + optional_size:
        import_rva, import_size = _unpack_from("<II", data, directory_offset + 8, name)

    section_offset = optional_offset + optional_size
    if section_count > 96 or section_offset + section_count * 40 > len(data):
        raise ValueError(f"{name} has an invalid PE section table")
    sections: list[tuple[int, int, int]] = []
    for index in range(section_count):
        current = section_offset + index * 40
        virtual_size, virtual_address, raw_size, raw_offset = _unpack_from(
            "<IIII", data, current + 8, name
        )
        sections.append((virtual_address, max(virtual_size, raw_size), raw_offset))

    def rva_to_offset(rva: int) -> int:
        for virtual_address, mapped_size, raw_offset in sections:
            if virtual_address <= rva < virtual_address + mapped_size:
                offset = raw_offset + (rva - virtual_address)
                if offset >= len(data):
                    break
                return offset
        if rva < section_offset:
            return rva
        raise ValueError(f"{name} has an unmapped PE RVA 0x{rva:x}")

    thunk_size = struct.calcsize(thunk_format)

    def read_named_imports(thunk_rva: int) -> tuple[str, ...]:
        functions: list[str] = []
        thunk_offset = rva_to_offset(thunk_rva)
        for _index in range(MAX_IMPORTS_PER_DLL):
            (thunk,) = _unpack_from(thunk_format, data, thunk_offset, name)
            if thunk == 0:
                return tuple(functions)
            if not thunk & ordinal_flag:
                # IMAGE_IMPORT_BY_NAME: a 16-bit hint followed by the name.
                functions.append(_read_c_string(data, rva_to_offset(thunk + 2), name))
            thunk_offset += thunk_size
        raise ValueError(f"{name} has an unterminated PE import thunk table")

    imports: list[str] = []
    imported_functions: dict[str, tuple[str, ...]] = {}
    if import_rva:
        descriptor_offset = rva_to_offset(import_rva)
        descriptor_limit = min(
            len(data),
            descriptor_offset + (import_size if import_size else 20 * MAX_IMPORT_DESCRIPTORS),
        )
        terminated = False
        for _index in range(MAX_IMPORT_DESCRIPTORS):
            if descriptor_offset + 20 > descriptor_limit:
                break
            descriptor = _unpack_from("<IIIII", data, descriptor_offset, name)
            if not any(descriptor):
                terminated = True
                break
            import_name_rva = descriptor[3]
            if not import_name_rva:
                raise ValueError(f"{name} has a PE import descriptor without a name")
            dll_name = _read_c_string(data, rva_to_offset(import_name_rva), name)
            imports.append(dll_name)
            if collect_functions:
                # Prefer the unbound lookup table; bound images may only keep
                # names reachable through it, while FirstThunk is the fallback.
                thunk_rva = descriptor[0] or descriptor[4]
                functions = read_named_imports(thunk_rva) if thunk_rva else ()
                key = dll_name.casefold()
                imported_functions[key] = imported_functions.get(key, ()) + functions
            descriptor_offset += 20
        if not terminated:
            raise ValueError(f"{name} has an unterminated PE import table")

    info = PeInfo(
        machine=machine,
        is_dll=bool(characteristics & IMAGE_FILE_DLL),
        imports=tuple(imports),
    )
    return info, imported_functions


def validate_windows_runtime_dependencies(data: bytes, *, name: str) -> PeInfo | None:
    info = parse_pe(data, name=name)
    if info is None:
        return None
    forbidden = sorted(
        {dependency.casefold() for dependency in info.imports}
        & FORBIDDEN_WINDOWS_RUNTIME_DLLS
    )
    if forbidden:
        raise ValueError(
            f"{name} depends on unshipped Windows runtime DLLs: " + ", ".join(forbidden)
        )
    return info
