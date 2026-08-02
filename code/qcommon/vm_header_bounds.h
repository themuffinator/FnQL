/*
===========================================================================
Copyright (C) 2026 FnQL contributors

This file is part of FnQL and is distributed under the terms of the GNU
General Public License version 2 or (at your option) any later version.
===========================================================================
*/

#ifndef VM_HEADER_BOUNDS_H
#define VM_HEADER_BOUNDS_H

#include "vm_local.h"

#include <limits.h>

/*
 * VM_LoadQVM rounds the data image up to a power of two and reserves an
 * additional compatibility stack when the rounded tail is too small.  Keep
 * the accepted image below the largest value that can complete both steps
 * without reaching the signed-address boundary used by the VM runtimes.
 */
#define VM_MAX_EXACT_DATA_LENGTH ( ( 1U << 30 ) - PROGRAM_STACK_EXTRA )

typedef enum {
	VM_HEADER_VALID = 0,
	VM_HEADER_BAD_CODE_OFFSET,
	VM_HEADER_BAD_CODE_LENGTH,
	VM_HEADER_BAD_INSTRUCTION_COUNT,
	VM_HEADER_BAD_DATA_OFFSET,
	VM_HEADER_BAD_DATA_LENGTH,
	VM_HEADER_BAD_LITERAL_LENGTH,
	VM_HEADER_BAD_JUMP_TARGET_LENGTH,
	VM_HEADER_BAD_BSS_LENGTH,
	VM_HEADER_DATA_IMAGE_TOO_LARGE
} vmHeaderValidationResult_t;

/*
 * Validate an already byte-swapped QVM header.  All file ranges are proven
 * with subtraction so hostile signed fields cannot overflow an intermediate
 * sum.  The helper is header-only so the parser contract can be tested without
 * linking the engine VM and allocator subsystems into the regression binary.
 */
static ID_INLINE vmHeaderValidationResult_t VM_ValidateHeaderLayout(
	const vmHeader_t *header, int fileSize, int headerSize )
{
	int remaining;
	unsigned int initializedLength;
	const unsigned int maxInstructionCount =
		( (unsigned int)INT_MAX / (unsigned int)sizeof( instruction_t ) ) - 8U;

	if ( !header || fileSize < 0 || headerSize < 0 || headerSize > fileSize ||
		header->codeOffset < headerSize || header->codeOffset > fileSize ) {
		return VM_HEADER_BAD_CODE_OFFSET;
	}

	remaining = fileSize - header->codeOffset;
	if ( header->codeLength <= 0 || header->codeLength > remaining ) {
		return VM_HEADER_BAD_CODE_LENGTH;
	}
	if ( header->instructionCount <= 0 ||
		header->instructionCount > header->codeLength ||
		(unsigned int)header->instructionCount > maxInstructionCount ) {
		return VM_HEADER_BAD_INSTRUCTION_COUNT;
	}

	remaining -= header->codeLength;
	if ( header->dataOffset != fileSize - remaining ) {
		return VM_HEADER_BAD_DATA_OFFSET;
	}
	if ( header->dataLength < 0 || header->dataLength > remaining ) {
		return VM_HEADER_BAD_DATA_LENGTH;
	}

	remaining -= header->dataLength;
	if ( header->litLength < 0 || header->litLength > remaining ) {
		return VM_HEADER_BAD_LITERAL_LENGTH;
	}
	remaining -= header->litLength;

	if ( header->vmMagic == VM_MAGIC_VER2 ) {
		if ( header->jtrgLength < 0 || ( header->jtrgLength & 3 ) != 0 ||
			header->jtrgLength != remaining ) {
			return VM_HEADER_BAD_JUMP_TARGET_LENGTH;
		}
	} else if ( remaining != 0 ) {
		return VM_HEADER_BAD_LITERAL_LENGTH;
	}

	if ( header->bssLength < 0 ) {
		return VM_HEADER_BAD_BSS_LENGTH;
	}

	initializedLength = (unsigned int)header->dataLength +
		(unsigned int)header->litLength;
	if ( initializedLength > VM_MAX_EXACT_DATA_LENGTH ||
		(unsigned int)header->bssLength >
			VM_MAX_EXACT_DATA_LENGTH - initializedLength ) {
		return VM_HEADER_DATA_IMAGE_TOO_LARGE;
	}

	return VM_HEADER_VALID;
}

#endif /* VM_HEADER_BOUNDS_H */
