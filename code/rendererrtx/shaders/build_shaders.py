#!/usr/bin/env python3
"""Deterministic shader compiler and reflection generator for rendererrtx."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple


# SPIR-V opcode constants used by this script.
OP_NAME = 5
OP_MEMBER_NAME = 6
OP_DECORATE = 71
OP_MEMBER_DECORATE = 72
OP_VARIABLE = 59
OP_TYPE_INT = 21
OP_TYPE_FLOAT = 22
OP_TYPE_VECTOR = 23
OP_TYPE_MATRIX = 24
OP_TYPE_IMAGE = 25
OP_TYPE_SAMPLER = 26
OP_TYPE_SAMPLED_IMAGE = 27
OP_TYPE_ARRAY = 28
OP_TYPE_RUNTIME_ARRAY = 29
OP_TYPE_STRUCT = 30
OP_TYPE_POINTER = 32
OP_CONSTANT = 43
OP_SPEC_CONSTANT = 50

# Extension opcode for VK_KHR_acceleration_structure.
OP_TYPE_ACCELERATION_STRUCTURE_KHR = 5341

# SPIR-V storage class values used by reflection.
STORAGE_CLASS_UNIFORM_CONSTANT = 0
STORAGE_CLASS_UNIFORM = 2
STORAGE_CLASS_PUSH_CONSTANT = 9
STORAGE_CLASS_IMAGE = 11
STORAGE_CLASS_STORAGE_BUFFER = 12

# SPIR-V decoration values used by reflection.
DECORATION_BLOCK = 2
DECORATION_BUFFER_BLOCK = 3
DECORATION_ARRAY_STRIDE = 6
DECORATION_MATRIX_STRIDE = 7
DECORATION_BINDING = 33
DECORATION_DESCRIPTOR_SET = 34
DECORATION_OFFSET = 35


STORAGE_CLASS_NAMES = {
	STORAGE_CLASS_UNIFORM_CONSTANT: "UniformConstant",
	STORAGE_CLASS_UNIFORM: "Uniform",
	STORAGE_CLASS_PUSH_CONSTANT: "PushConstant",
	STORAGE_CLASS_IMAGE: "Image",
	STORAGE_CLASS_STORAGE_BUFFER: "StorageBuffer",
}


@dataclass(frozen=True)
class ShaderVariant:
	symbol: str
	source: str
	stage: str
	defines: Tuple[str, ...]


@dataclass
class SpirvModule:
	types: Dict[int, Dict[str, object]]
	names: Dict[int, str]
	decorations: Dict[int, Dict[int, List[int]]]
	member_names: Dict[int, Dict[int, str]]
	member_decorations: Dict[int, Dict[int, Dict[int, List[int]]]]
	variables: Dict[int, Dict[str, int]]
	constants: Dict[int, int]


def decode_spirv_string(words: List[int]) -> str:
	data = bytearray()
	for word in words:
		data.extend(struct.pack("<I", word))
	terminator = data.find(0)
	if terminator >= 0:
		data = data[:terminator]
	return data.decode("utf-8", errors="replace")


def literal_words_to_uint(words: List[int]) -> int:
	value = 0
	for index, word in enumerate(words):
		value |= word << (index * 32)
	return value


def first_literal(values: Optional[List[int]]) -> Optional[int]:
	if not values:
		return None
	return values[0]


def parse_spirv(binary: bytes) -> SpirvModule:
	if len(binary) < 20 or (len(binary) % 4) != 0:
		raise ValueError("Invalid SPIR-V payload size")

	words = list(struct.unpack("<" + ("I" * (len(binary) // 4)), binary))

	if words[0] != 0x07230203:
		raise ValueError("Invalid SPIR-V magic")

	types: Dict[int, Dict[str, object]] = {}
	names: Dict[int, str] = {}
	decorations: Dict[int, Dict[int, List[int]]] = {}
	member_names: Dict[int, Dict[int, str]] = {}
	member_decorations: Dict[int, Dict[int, Dict[int, List[int]]]] = {}
	variables: Dict[int, Dict[str, int]] = {}
	constants: Dict[int, int] = {}

	offset = 5
	while offset < len(words):
		opcode_word = words[offset]
		word_count = opcode_word >> 16
		opcode = opcode_word & 0xFFFF

		if word_count == 0:
			raise ValueError("Malformed SPIR-V: instruction with word_count=0")

		operands = words[offset + 1 : offset + word_count]
		offset += word_count

		if opcode == OP_NAME and len(operands) >= 1:
			names[operands[0]] = decode_spirv_string(operands[1:])
		elif opcode == OP_MEMBER_NAME and len(operands) >= 2:
			type_id = operands[0]
			member_index = operands[1]
			member_names.setdefault(type_id, {})[member_index] = decode_spirv_string(operands[2:])
		elif opcode == OP_DECORATE and len(operands) >= 2:
			target_id = operands[0]
			decoration = operands[1]
			decorations.setdefault(target_id, {})[decoration] = operands[2:]
		elif opcode == OP_MEMBER_DECORATE and len(operands) >= 3:
			type_id = operands[0]
			member_index = operands[1]
			decoration = operands[2]
			member_decorations.setdefault(type_id, {}).setdefault(member_index, {})[decoration] = operands[3:]
		elif opcode == OP_VARIABLE and len(operands) >= 3:
			variables[operands[1]] = {
				"type_id": operands[0],
				"storage_class": operands[2],
			}
		elif opcode == OP_TYPE_INT and len(operands) >= 3:
			types[operands[0]] = {
				"kind": "int",
				"width": operands[1],
				"signed": operands[2],
			}
		elif opcode == OP_TYPE_FLOAT and len(operands) >= 2:
			types[operands[0]] = {
				"kind": "float",
				"width": operands[1],
			}
		elif opcode == OP_TYPE_VECTOR and len(operands) >= 3:
			types[operands[0]] = {
				"kind": "vector",
				"component_type_id": operands[1],
				"component_count": operands[2],
			}
		elif opcode == OP_TYPE_MATRIX and len(operands) >= 3:
			types[operands[0]] = {
				"kind": "matrix",
				"column_type_id": operands[1],
				"column_count": operands[2],
			}
		elif opcode == OP_TYPE_IMAGE and len(operands) >= 7:
			types[operands[0]] = {
				"kind": "image",
				"sampled": operands[6],
			}
		elif opcode == OP_TYPE_SAMPLER and len(operands) >= 1:
			types[operands[0]] = {
				"kind": "sampler",
			}
		elif opcode == OP_TYPE_SAMPLED_IMAGE and len(operands) >= 2:
			types[operands[0]] = {
				"kind": "sampled_image",
				"image_type_id": operands[1],
			}
		elif opcode == OP_TYPE_ARRAY and len(operands) >= 3:
			types[operands[0]] = {
				"kind": "array",
				"element_type_id": operands[1],
				"length_id": operands[2],
			}
		elif opcode == OP_TYPE_RUNTIME_ARRAY and len(operands) >= 2:
			types[operands[0]] = {
				"kind": "runtime_array",
				"element_type_id": operands[1],
			}
		elif opcode == OP_TYPE_STRUCT and len(operands) >= 1:
			types[operands[0]] = {
				"kind": "struct",
				"member_type_ids": operands[1:],
			}
		elif opcode == OP_TYPE_POINTER and len(operands) >= 3:
			types[operands[0]] = {
				"kind": "pointer",
				"storage_class": operands[1],
				"pointee_type_id": operands[2],
			}
		elif opcode == OP_TYPE_ACCELERATION_STRUCTURE_KHR and len(operands) >= 1:
			types[operands[0]] = {
				"kind": "acceleration_structure",
			}
		elif opcode == OP_CONSTANT and len(operands) >= 3:
			constants[operands[1]] = literal_words_to_uint(operands[2:])
		elif opcode == OP_SPEC_CONSTANT and len(operands) >= 3:
			constants[operands[1]] = literal_words_to_uint(operands[2:])

	return SpirvModule(
		types=types,
		names=names,
		decorations=decorations,
		member_names=member_names,
		member_decorations=member_decorations,
		variables=variables,
		constants=constants,
	)


def storage_class_name(value: int) -> str:
	return STORAGE_CLASS_NAMES.get(value, f"Unknown({value})")


def resolve_constant(module: SpirvModule, constant_id: int) -> Optional[int]:
	return module.constants.get(constant_id)


def unwrap_array_type(module: SpirvModule, type_id: int) -> Tuple[Optional[int], int]:
	count: Optional[int] = 1
	current = type_id

	while True:
		type_info = module.types.get(current)
		if not type_info:
			break

		kind = type_info.get("kind")
		if kind == "array":
			length = resolve_constant(module, int(type_info["length_id"]))
			if length is None:
				count = None
			elif count is not None:
				count *= int(length)
			current = int(type_info["element_type_id"])
		elif kind == "runtime_array":
			count = None
			current = int(type_info["element_type_id"])
		else:
			break

	return count, current


def classify_descriptor(module: SpirvModule, storage_class: int, base_type_id: int) -> str:
	base_type = module.types.get(base_type_id, {})
	base_kind = base_type.get("kind")

	if storage_class == STORAGE_CLASS_UNIFORM_CONSTANT:
		if base_kind == "sampled_image":
			return "combined_image_sampler"
		if base_kind == "sampler":
			return "sampler"
		if base_kind == "image":
			sampled = int(base_type.get("sampled", 1))
			return "storage_image" if sampled == 2 else "sampled_image"
		if base_kind == "acceleration_structure":
			return "acceleration_structure"
		return "uniform_constant"

	if storage_class == STORAGE_CLASS_UNIFORM:
		decor = module.decorations.get(base_type_id, {})
		if DECORATION_BLOCK in decor:
			return "uniform_buffer"
		if DECORATION_BUFFER_BLOCK in decor:
			return "storage_buffer"
		return "uniform"

	if storage_class == STORAGE_CLASS_STORAGE_BUFFER:
		return "storage_buffer"

	if storage_class == STORAGE_CLASS_IMAGE:
		return "storage_image"

	return "unknown"


def calculate_member_size(
	module: SpirvModule,
	struct_type_id: int,
	member_index: int,
	member_type_id: int,
	memo: Dict[int, Optional[int]],
	stack: Optional[List[int]] = None,
) -> Optional[int]:
	member_type = module.types.get(member_type_id, {})
	member_decor = module.member_decorations.get(struct_type_id, {}).get(member_index, {})

	if member_type.get("kind") == "matrix":
		matrix_stride = first_literal(member_decor.get(DECORATION_MATRIX_STRIDE))
		if matrix_stride is not None:
			column_count = int(member_type.get("column_count", 0))
			return int(matrix_stride) * column_count

	return calculate_type_size(module, member_type_id, memo, stack)


def calculate_type_size(
	module: SpirvModule,
	type_id: int,
	memo: Dict[int, Optional[int]],
	stack: Optional[List[int]] = None,
) -> Optional[int]:
	if type_id in memo:
		return memo[type_id]

	if stack is None:
		stack = []
	if type_id in stack:
		return None

	stack.append(type_id)
	type_info = module.types.get(type_id)

	if not type_info:
		size = None
	else:
		kind = type_info.get("kind")
		if kind == "int" or kind == "float":
			size = int(type_info["width"]) // 8
		elif kind == "vector":
			component_size = calculate_type_size(module, int(type_info["component_type_id"]), memo, stack)
			size = None if component_size is None else component_size * int(type_info["component_count"])
		elif kind == "matrix":
			column_size = calculate_type_size(module, int(type_info["column_type_id"]), memo, stack)
			size = None if column_size is None else column_size * int(type_info["column_count"])
		elif kind == "array":
			element_type_id = int(type_info["element_type_id"])
			element_size = calculate_type_size(module, element_type_id, memo, stack)
			length = resolve_constant(module, int(type_info["length_id"]))
			stride = first_literal(module.decorations.get(type_id, {}).get(DECORATION_ARRAY_STRIDE))
			if length is None or (element_size is None and stride is None):
				size = None
			elif stride is not None:
				size = int(stride) * int(length)
			else:
				size = element_size * int(length)
		elif kind == "runtime_array":
			size = None
		elif kind == "struct":
			member_type_ids = list(type_info.get("member_type_ids", []))
			max_end = 0
			sequential_offset = 0
			for member_index, member_type_id in enumerate(member_type_ids):
				member_size = calculate_member_size(
					module,
					type_id,
					member_index,
					int(member_type_id),
					memo,
					stack,
				)
				if member_size is None:
					max_end = -1
					break
				member_decor = module.member_decorations.get(type_id, {}).get(member_index, {})
				member_offset = first_literal(member_decor.get(DECORATION_OFFSET))
				offset = sequential_offset if member_offset is None else int(member_offset)
				end = offset + member_size
				sequential_offset = end
				max_end = max(max_end, end)
			size = None if max_end < 0 else max_end
		else:
			size = None

	stack.pop()
	memo[type_id] = size
	return size


def extract_reflection(module: SpirvModule) -> Dict[str, object]:
	descriptor_bindings: List[Dict[str, object]] = []
	push_constants: List[Dict[str, object]] = []
	size_memo: Dict[int, Optional[int]] = {}

	for var_id in sorted(module.variables):
		variable = module.variables[var_id]
		storage_class = int(variable["storage_class"])
		var_type_id = int(variable["type_id"])
		type_info = module.types.get(var_type_id, {})

		if type_info.get("kind") != "pointer":
			continue

		pointee_type_id = int(type_info["pointee_type_id"])
		name = module.names.get(var_id, "").strip() or f"id_{var_id}"

		if storage_class in (STORAGE_CLASS_UNIFORM_CONSTANT, STORAGE_CLASS_UNIFORM, STORAGE_CLASS_STORAGE_BUFFER, STORAGE_CLASS_IMAGE):
			decor = module.decorations.get(var_id, {})
			set_index = first_literal(decor.get(DECORATION_DESCRIPTOR_SET))
			binding_index = first_literal(decor.get(DECORATION_BINDING))
			if set_index is None or binding_index is None:
				continue

			descriptor_count, descriptor_base_type_id = unwrap_array_type(module, pointee_type_id)
			descriptor_bindings.append(
				{
					"name": name,
					"set": int(set_index),
					"binding": int(binding_index),
					"storage_class": storage_class_name(storage_class),
					"descriptor_class": classify_descriptor(module, storage_class, descriptor_base_type_id),
					"descriptor_count": descriptor_count,
				}
			)

		if storage_class == STORAGE_CLASS_PUSH_CONSTANT:
			push_constants.append(
				{
					"name": name,
					"size_bytes": calculate_type_size(module, pointee_type_id, size_memo),
				}
			)

	descriptor_bindings.sort(key=lambda entry: (entry["set"], entry["binding"], entry["name"]))
	push_constants.sort(key=lambda entry: entry["name"])

	return {
		"descriptor_bindings": descriptor_bindings,
		"push_constants": push_constants,
	}


def discover_direct_variants(shader_dir: Path) -> List[ShaderVariant]:
	variants: List[ShaderVariant] = []

	direct_stages = ("vert", "frag", "rgen", "rmiss", "rchit", "rahit")

	for stage in direct_stages:
		for shader_path in sorted(shader_dir.glob(f"*.{stage}"), key=lambda path: path.name.lower()):
			variants.append(
				ShaderVariant(
					symbol=f"{shader_path.stem}_{stage}_spv",
					source=shader_path.name,
					stage=stage,
					defines=(),
				)
			)

	return variants


def append_env_fog_variants(
	output: List[ShaderVariant],
	source: str,
	symbol_prefix: str,
	base_defines: Tuple[str, ...],
) -> None:
	output.append(ShaderVariant(symbol=symbol_prefix, source=source, stage="vert", defines=base_defines))
	output.append(ShaderVariant(symbol=f"{symbol_prefix}_fog", source=source, stage="vert", defines=base_defines + ("USE_FOG",)))
	output.append(ShaderVariant(symbol=f"{symbol_prefix}_env", source=source, stage="vert", defines=base_defines + ("USE_ENV",)))
	output.append(
		ShaderVariant(
			symbol=f"{symbol_prefix}_env_fog",
			source=source,
			stage="vert",
			defines=base_defines + ("USE_FOG", "USE_ENV"),
		)
	)


def append_pair_variants(
	output: List[ShaderVariant],
	source: str,
	stage: str,
	symbol_prefix: str,
	base_defines: Tuple[str, ...],
) -> None:
	output.append(ShaderVariant(symbol=symbol_prefix, source=source, stage=stage, defines=base_defines))
	output.append(ShaderVariant(symbol=f"{symbol_prefix}_fog", source=source, stage=stage, defines=base_defines + ("USE_FOG",)))


def build_template_variants() -> List[ShaderVariant]:
	variants: List[ShaderVariant] = []

	# Lighting templates.
	variants.extend(
		[
			ShaderVariant("vert_light", "light_vert.tmpl", "vert", ()),
			ShaderVariant("vert_light_fog", "light_vert.tmpl", "vert", ("USE_FOG",)),
			ShaderVariant("frag_light", "light_frag.tmpl", "frag", ()),
			ShaderVariant("frag_light_fog", "light_frag.tmpl", "frag", ("USE_FOG",)),
			ShaderVariant("frag_light_line", "light_frag.tmpl", "frag", ("USE_LINE",)),
			ShaderVariant("frag_light_line_fog", "light_frag.tmpl", "frag", ("USE_LINE", "USE_FOG")),
		]
	)

	# Generic vertex template permutations.
	append_env_fog_variants(variants, "gen_vert.tmpl", "vert_tx0", ())
	append_env_fog_variants(variants, "gen_vert.tmpl", "vert_tx0_ident1", ("USE_CLX_IDENT",))
	append_env_fog_variants(variants, "gen_vert.tmpl", "vert_tx0_fixed", ("USE_FIXED_COLOR",))
	append_env_fog_variants(variants, "gen_vert.tmpl", "vert_tx1", ("USE_TX1",))
	append_env_fog_variants(variants, "gen_vert.tmpl", "vert_tx1_ident1", ("USE_CLX_IDENT", "USE_TX1"))
	append_env_fog_variants(variants, "gen_vert.tmpl", "vert_tx1_fixed", ("USE_FIXED_COLOR", "USE_TX1"))
	append_env_fog_variants(variants, "gen_vert.tmpl", "vert_tx1_cl", ("USE_CL1", "USE_TX1"))
	append_env_fog_variants(variants, "gen_vert.tmpl", "vert_tx2", ("USE_TX2",))
	append_env_fog_variants(variants, "gen_vert.tmpl", "vert_tx2_cl", ("USE_CL2", "USE_TX2"))

	# Generic fragment template permutations.
	append_pair_variants(variants, "gen_frag.tmpl", "frag", "frag_tx0", ("USE_ATEST",))
	append_pair_variants(variants, "gen_frag.tmpl", "frag", "frag_tx0_ident1", ("USE_CLX_IDENT", "USE_ATEST"))
	append_pair_variants(variants, "gen_frag.tmpl", "frag", "frag_tx0_fixed", ("USE_FIXED_COLOR", "USE_ATEST"))
	append_pair_variants(variants, "gen_frag.tmpl", "frag", "frag_tx0_ent", ("USE_ENT_COLOR", "USE_ATEST"))
	variants.append(
		ShaderVariant("frag_tx0_df", "gen_frag.tmpl", "frag", ("USE_CLX_IDENT", "USE_ATEST", "USE_DF"))
	)
	append_pair_variants(variants, "gen_frag.tmpl", "frag", "frag_tx1", ("USE_TX1",))
	append_pair_variants(variants, "gen_frag.tmpl", "frag", "frag_tx1_ident1", ("USE_CLX_IDENT", "USE_TX1"))
	append_pair_variants(variants, "gen_frag.tmpl", "frag", "frag_tx1_fixed", ("USE_FIXED_COLOR", "USE_TX1"))
	append_pair_variants(variants, "gen_frag.tmpl", "frag", "frag_tx1_cl", ("USE_CL1", "USE_TX1"))
	append_pair_variants(variants, "gen_frag.tmpl", "frag", "frag_tx2", ("USE_TX2",))
	append_pair_variants(variants, "gen_frag.tmpl", "frag", "frag_tx2_cl", ("USE_CL2", "USE_TX2"))

	return variants


def build_variant_list(shader_dir: Path) -> List[ShaderVariant]:
	variants = discover_direct_variants(shader_dir)
	variants.extend(build_template_variants())

	seen: Dict[str, ShaderVariant] = {}
	for variant in variants:
		if variant.symbol in seen:
			raise ValueError(
				f"Duplicate shader symbol '{variant.symbol}' from '{seen[variant.symbol].source}' and '{variant.source}'"
			)
		seen[variant.symbol] = variant
	return variants


def locate_glslang_validator(explicit_path: Optional[str]) -> Path:
	candidates: List[Path] = []

	if explicit_path:
		candidates.append(Path(explicit_path))

	env_glslang = os.environ.get("GLSLANG_VALIDATOR")
	if env_glslang:
		candidates.append(Path(env_glslang))

	vulkan_sdk = os.environ.get("VULKAN_SDK")
	if vulkan_sdk:
		bin_dir = Path(vulkan_sdk) / "Bin"
		candidates.append(bin_dir / "glslangValidator")
		candidates.append(bin_dir / "glslangValidator.exe")

	which_path = shutil.which("glslangValidator")
	if which_path:
		candidates.append(Path(which_path))

	which_path_exe = shutil.which("glslangValidator.exe")
	if which_path_exe:
		candidates.append(Path(which_path_exe))

	for candidate in candidates:
		if candidate.is_file():
			return candidate

	raise FileNotFoundError(
		"Unable to locate glslangValidator. Set --glslang, GLSLANG_VALIDATOR, or VULKAN_SDK."
	)


def compile_variant(glslang: Path, shader_dir: Path, temp_spv: Path, variant: ShaderVariant) -> bytes:
	command = [
		str(glslang),
		"-S",
		variant.stage,
		"-V",
		"-o",
		str(temp_spv),
		variant.source,
	]
	if variant.stage in {"rgen", "rmiss", "rchit", "rahit"}:
		command.extend(["--target-env", "spirv1.4"])
	command.extend(f"-D{macro}" for macro in variant.defines)

	result = subprocess.run(
		command,
		cwd=str(shader_dir),
		stdout=subprocess.PIPE,
		stderr=subprocess.STDOUT,
		text=True,
		check=False,
	)
	if result.returncode != 0:
		raise RuntimeError(
			"glslangValidator failed for "
			f"{variant.symbol} ({variant.source}, stage={variant.stage}, defines={variant.defines}):\n"
			f"{result.stdout}"
		)

	return temp_spv.read_bytes()


def generate_c_source(compiled: List[Tuple[ShaderVariant, bytes]]) -> str:
	output: List[str] = []
	for variant, data in compiled:
		output.append(f"const unsigned char {variant.symbol}[{len(data)}] = {{\n\t")
		for index, value in enumerate(data):
			output.append(f"0x{value:02X}")
			if index + 1 < len(data):
				if ((index + 1) % 16) == 0:
					output.append(",\n\t")
				else:
					output.append(", ")
		output.append("\n};\n")
	return "".join(output)


def generate_reflection_json(compiled: List[Tuple[ShaderVariant, bytes]]) -> str:
	shaders: List[Dict[str, object]] = []
	for variant, data in compiled:
		module = parse_spirv(data)
		reflection = extract_reflection(module)
		shaders.append(
			{
				"symbol": variant.symbol,
				"source": variant.source,
				"stage": variant.stage,
				"defines": list(variant.defines),
				"byte_size": len(data),
				"reflection": reflection,
			}
		)

	document = {
		"schema": "q3rtx.rendererrtx.shader_reflection.v1",
		"generator": "code/rendererrtx/shaders/build_shaders.py",
		"shader_count": len(shaders),
		"shaders": shaders,
	}
	return json.dumps(document, indent=2, sort_keys=False) + "\n"


def read_text_if_exists(path: Path) -> Optional[str]:
	if not path.is_file():
		return None
	return path.read_text(encoding="utf-8")


def write_text_if_changed(path: Path, text: str) -> bool:
	existing = read_text_if_exists(path)
	if existing == text:
		return False
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_text(text, encoding="utf-8", newline="\n")
	return True


def build_outputs(
	shader_dir: Path,
	glslang: Path,
	output_c: Optional[Path],
	output_reflection: Optional[Path],
) -> Tuple[Optional[str], Optional[str], int]:
	variants = build_variant_list(shader_dir)

	compiled: List[Tuple[ShaderVariant, bytes]] = []
	with tempfile.TemporaryDirectory(prefix="q3rtx_shader_build_") as temp_dir_name:
		temp_dir = Path(temp_dir_name)
		temp_spv = temp_dir / "shader.spv"

		for variant in variants:
			data = compile_variant(glslang, shader_dir, temp_spv, variant)
			compiled.append((variant, data))

	c_source = generate_c_source(compiled) if output_c else None
	reflection_json = generate_reflection_json(compiled) if output_reflection else None

	return c_source, reflection_json, len(compiled)


def parse_args() -> argparse.Namespace:
	default_shader_dir = Path(__file__).resolve().parent
	default_output_dir = default_shader_dir / "spirv"

	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--shader-dir", type=Path, default=default_shader_dir, help="Shader source directory.")
	parser.add_argument("--glslang", help="Path to glslangValidator executable.")
	parser.add_argument(
		"--output-c",
		type=Path,
		default=default_output_dir / "shader_data.c",
		help="Output C source path.",
	)
	parser.add_argument(
		"--output-reflection",
		type=Path,
		default=default_output_dir / "shader_reflection.json",
		help="Output reflection JSON path.",
	)
	parser.add_argument(
		"--no-c-output",
		action="store_true",
		help="Disable C output generation.",
	)
	parser.add_argument(
		"--no-reflection-output",
		action="store_true",
		help="Disable reflection JSON output generation.",
	)
	parser.add_argument(
		"--check",
		action="store_true",
		help="Validate output files are up to date without writing them.",
	)
	return parser.parse_args()


def main() -> int:
	args = parse_args()

	shader_dir = args.shader_dir.resolve()
	output_c = None if args.no_c_output else args.output_c
	output_reflection = None if args.no_reflection_output else args.output_reflection

	if output_c is None and output_reflection is None:
		print("No outputs requested. Enable --output-c and/or --output-reflection.", file=sys.stderr)
		return 2

	try:
		glslang = locate_glslang_validator(args.glslang)
	except FileNotFoundError as exc:
		print(str(exc), file=sys.stderr)
		return 2

	try:
		c_source, reflection_json, shader_count = build_outputs(shader_dir, glslang, output_c, output_reflection)
	except (RuntimeError, ValueError) as exc:
		print(str(exc), file=sys.stderr)
		return 1

	if args.check:
		outdated = False
		if output_c and c_source is not None and read_text_if_exists(output_c) != c_source:
			print(f"Out of date: {output_c}")
			outdated = True
		if output_reflection and reflection_json is not None and read_text_if_exists(output_reflection) != reflection_json:
			print(f"Out of date: {output_reflection}")
			outdated = True
		if outdated:
			return 1
		print(f"Shader outputs are up to date ({shader_count} variants).")
		return 0

	changed = False
	if output_c and c_source is not None:
		changed |= write_text_if_changed(output_c, c_source)
	if output_reflection and reflection_json is not None:
		changed |= write_text_if_changed(output_reflection, reflection_json)

	status = "updated" if changed else "no changes"
	print(f"Shader build complete: {shader_count} variants compiled, {status}.")
	if output_c:
		print(f"  C output: {output_c}")
	if output_reflection:
		print(f"  Reflection output: {output_reflection}")
	return 0


if __name__ == "__main__":
	sys.exit(main())
