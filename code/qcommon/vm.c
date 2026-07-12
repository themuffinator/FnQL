/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2012-2020 Quake3e project

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// vm.c -- virtual machine

/*


intermix code and data
symbol table

a dll has one imported function: VM_SystemCall
and one exported function: Perform


*/

#include "vm_local.h"
#include "../game/g_public.h"
#include "../ui/ui_public.h"
#include "../renderercommon/tr_types.h"
#ifndef USE_DEDICATED
#include "../cgame/cg_public.h"
#endif

opcode_info_t ops[ OP_MAX ] =
{
	// size, stack, nargs, flags
	{ 0, 0, 0, 0 }, // undef
	{ 0, 0, 0, 0 }, // ignore
	{ 0, 0, 0, 0 }, // break

	{ 4, 0, 0, 0 }, // enter
	{ 4,-4, 0, 0 }, // leave
	{ 0, 0, 1, 0 }, // call
	{ 0, 4, 0, 0 }, // push
	{ 0,-4, 1, 0 }, // pop

	{ 4, 4, 0, 0 }, // const
	{ 4, 4, 0, 0 }, // local
	{ 0,-4, 1, 0 }, // jump

	{ 4,-8, 2, JUMP }, // eq
	{ 4,-8, 2, JUMP }, // ne

	{ 4,-8, 2, JUMP }, // lti
	{ 4,-8, 2, JUMP }, // lei
	{ 4,-8, 2, JUMP }, // gti
	{ 4,-8, 2, JUMP }, // gei

	{ 4,-8, 2, JUMP }, // ltu
	{ 4,-8, 2, JUMP }, // leu
	{ 4,-8, 2, JUMP }, // gtu
	{ 4,-8, 2, JUMP }, // geu

	{ 4,-8, 2, JUMP|FPU }, // eqf
	{ 4,-8, 2, JUMP|FPU }, // nef

	{ 4,-8, 2, JUMP|FPU }, // ltf
	{ 4,-8, 2, JUMP|FPU }, // lef
	{ 4,-8, 2, JUMP|FPU }, // gtf
	{ 4,-8, 2, JUMP|FPU }, // gef

	{ 0, 0, 1, 0 }, // load1
	{ 0, 0, 1, 0 }, // load2
	{ 0, 0, 1, 0 }, // load4
	{ 0,-8, 2, 0 }, // store1
	{ 0,-8, 2, 0 }, // store2
	{ 0,-8, 2, 0 }, // store4
	{ 1,-4, 1, 0 }, // arg
	{ 4,-8, 2, 0 }, // bcopy

	{ 0, 0, 1, 0 }, // sex8
	{ 0, 0, 1, 0 }, // sex16

	{ 0, 0, 1, 0 }, // negi
	{ 0,-4, 3, 0 }, // add
	{ 0,-4, 3, 0 }, // sub
	{ 0,-4, 3, 0 }, // divi
	{ 0,-4, 3, 0 }, // divu
	{ 0,-4, 3, 0 }, // modi
	{ 0,-4, 3, 0 }, // modu
	{ 0,-4, 3, 0 }, // muli
	{ 0,-4, 3, 0 }, // mulu

	{ 0,-4, 3, 0 }, // band
	{ 0,-4, 3, 0 }, // bor
	{ 0,-4, 3, 0 }, // bxor
	{ 0, 0, 1, 0 }, // bcom

	{ 0,-4, 3, 0 }, // lsh
	{ 0,-4, 3, 0 }, // rshi
	{ 0,-4, 3, 0 }, // rshu

	{ 0, 0, 1, FPU }, // negf
	{ 0,-4, 3, FPU }, // addf
	{ 0,-4, 3, FPU }, // subf
	{ 0,-4, 3, FPU }, // divf
	{ 0,-4, 3, FPU }, // mulf

	{ 0, 0, 1, 0 },   // cvif
	{ 0, 0, 1, FPU }  // cvfi
};

const char *opname[ 256 ] = {
	"OP_UNDEF",

	"OP_IGNORE",

	"OP_BREAK",

	"OP_ENTER",
	"OP_LEAVE",
	"OP_CALL",
	"OP_PUSH",
	"OP_POP",

	"OP_CONST",

	"OP_LOCAL",

	"OP_JUMP",

	//-------------------

	"OP_EQ",
	"OP_NE",

	"OP_LTI",
	"OP_LEI",
	"OP_GTI",
	"OP_GEI",

	"OP_LTU",
	"OP_LEU",
	"OP_GTU",
	"OP_GEU",

	"OP_EQF",
	"OP_NEF",

	"OP_LTF",
	"OP_LEF",
	"OP_GTF",
	"OP_GEF",

	//-------------------

	"OP_LOAD1",
	"OP_LOAD2",
	"OP_LOAD4",
	"OP_STORE1",
	"OP_STORE2",
	"OP_STORE4",
	"OP_ARG",

	"OP_BLOCK_COPY",

	//-------------------

	"OP_SEX8",
	"OP_SEX16",

	"OP_NEGI",
	"OP_ADD",
	"OP_SUB",
	"OP_DIVI",
	"OP_DIVU",
	"OP_MODI",
	"OP_MODU",
	"OP_MULI",
	"OP_MULU",

	"OP_BAND",
	"OP_BOR",
	"OP_BXOR",
	"OP_BCOM",

	"OP_LSH",
	"OP_RSHI",
	"OP_RSHU",

	"OP_NEGF",
	"OP_ADDF",
	"OP_SUBF",
	"OP_DIVF",
	"OP_MULF",

	"OP_CVIF",
	"OP_CVFI"
};

cvar_t	*vm_rtChecks;

#ifdef DEBUG
int		vm_debugLevel;
#endif

// used by Com_Error to get rid of running vm's before longjmp
static int forced_unload;

static struct vm_s vmTable[ VM_COUNT ];

// Pure Quake Live servers still require the retail native UI and cgame.  Keep
// a bounded copy of modules which the local installation opened successfully
// before a server could influence filesystem search order.  A pure restart may
// reload only these exact bytes; it never searches for a new native module.
#define VM_PINNED_NATIVE_MAX_BYTES ( 16 * 1024 * 1024 )
typedef struct {
	byte	*bytes;
	int	length;
	char	filename[MAX_QPATH];
} vmPinnedNative_t;

static vmPinnedNative_t vmPinnedNative[VM_COUNT];
static char vmNativeCacheNonce[33];

static const char *vmName[ VM_COUNT ] = {
	"qagame",
	"cgame",
	"ui"
};

static void VM_VmInfo_f( void );
static void VM_VmProfile_f( void );

#ifdef DEBUG
void VM_Debug( int level ) {
	vm_debugLevel = level;
}
#endif

static qboolean VM_NormalizeQbooleanArg( intptr_t value ) {
	return value ? qtrue : qfalse;
}

static intptr_t VM_NormalizeQbooleanResult( qboolean value ) {
	return value ? qtrue : qfalse;
}

static int VM_GetExpectedNativeApiVersion( vmIndex_t index ) {
	if ( index == VM_GAME ) {
		return GAME_NATIVE_API_VERSION;
	}

#ifndef USE_DEDICATED
	if ( index == VM_UI ) {
		return UI_QL_API_VERSION;
	}

	if ( index == VM_CGAME ) {
		return CGAME_NATIVE_API_VERSION;
	}
#endif

	return 0;
}

static int VM_GetExpectedNativeExportCount( vmIndex_t index ) {
	if ( index == VM_GAME ) {
		return GAME_NATIVE_EXPORT_COUNT;
	}

#ifndef USE_DEDICATED
	if ( index == VM_UI ) {
		return UI_NATIVE_EXPORT_COUNT;
	}

	if ( index == VM_CGAME ) {
		return CG_NATIVE_EXPORT_COUNT;
	}
#endif

	return 0;
}

static qboolean VM_NativeExportSlotIsRequired( const vm_t *vm, int slot ) {
#ifndef USE_DEDICATED
	if ( vm && vm->index == VM_CGAME && slot == CG_NATIVE_EXPORT_RESERVED_NULL ) {
		return qfalse;
	}
#endif

	return qtrue;
}

static qboolean VM_ValidateNativeDllInterface( vm_t *vm ) {
	int expectedApiVersion;
	int expectedExportCount;
	int i;
	void **dllExports;

	if ( !vm || !vm->dllExports || !vm->dllImports ) {
		return qtrue;
	}

	expectedApiVersion = VM_GetExpectedNativeApiVersion( vm->index );
	expectedExportCount = VM_GetExpectedNativeExportCount( vm->index );
	if ( !expectedApiVersion || !expectedExportCount ) {
		return qtrue;
	}

	if ( vm->dllApiVersion != expectedApiVersion ) {
		Com_Printf( "Rejected DLL '%s': native API %d does not match expected %d for %s\n",
			vm->name, vm->dllApiVersion, expectedApiVersion, vm->name );
		return qfalse;
	}

	dllExports = (void **)vm->dllExports;
	for ( i = 0; i < expectedExportCount; i++ ) {
		if ( !VM_NativeExportSlotIsRequired( vm, i ) ) {
			continue;
		}

		if ( !dllExports[i] ) {
			Com_Printf( "Rejected DLL '%s': missing native export slot %d for %s\n",
				vm->name, i, vm->name );
			return qfalse;
		}
	}

	return qtrue;
}

/*
==============
VM_CheckBounds
==============
*/
void VM_CheckBounds( const vm_t *vm, unsigned int address, unsigned int length )
{
	if ( vm && ( vm->entryPoint || vm->dllExports ) ) {
		return;
	}

	{
		if ( (address | length) > vm->dataMask || (address + length) > vm->dataMask )
		{
			Com_Error( ERR_DROP, "program tried to bypass data segment bounds" );
		}
	}
}


/*
==============
VM_CheckBounds2
==============
*/
void VM_CheckBounds2( const vm_t *vm, unsigned int addr1, unsigned int addr2, unsigned int length )
{
	if ( vm && ( vm->entryPoint || vm->dllExports ) ) {
		return;
	}

	{
		if ( (addr1 | addr2 | length) > vm->dataMask || (addr1 + length) > vm->dataMask || (addr2+length) > vm->dataMask )
		{
			Com_Error( ERR_DROP, "program tried to bypass data segment bounds" );
		}
	}
}


/*
==============
VM_Init
==============
*/
void VM_Init( void ) {
#ifndef DEDICATED
	Cvar_Get( "vm_ui", "0", CVAR_ARCHIVE | CVAR_PROTECTED );	// Quake Live ships native UI modules by default.
	Cvar_Get( "vm_cgame", "0", CVAR_ARCHIVE | CVAR_PROTECTED );	// Quake Live ships native cgame by default.
#endif
	Cvar_Get( "vm_game", "0", CVAR_ARCHIVE | CVAR_PROTECTED );

	Cmd_AddCommand( "vmprofile", VM_VmProfile_f );
	Cmd_AddCommand( "vminfo", VM_VmInfo_f );

	Com_Memset( vmTable, 0, sizeof( vmTable ) );
	Com_Memset( vmPinnedNative, 0, sizeof( vmPinnedNative ) );
	vmNativeCacheNonce[0] = '\0';
}


/*
===============
VM_ValueToSymbol

Assumes a program counter value
===============
*/
const char *VM_ValueToSymbol( vm_t *vm, int value ) {
	vmSymbol_t	*sym;
	static char		text[MAX_TOKEN_CHARS];

	sym = vm->symbols;
	if ( !sym ) {
		return "NO SYMBOLS";
	}

	// find the symbol
	while ( sym->next && sym->next->symValue <= value ) {
		sym = sym->next;
	}

	if ( value == sym->symValue ) {
		return sym->symName;
	}

	Com_sprintf( text, sizeof( text ), "%s+%i", sym->symName, value - sym->symValue );

	return text;
}


/*
===============
VM_ValueToFunctionSymbol

For profiling, find the symbol behind this value
===============
*/
vmSymbol_t *VM_ValueToFunctionSymbol( vm_t *vm, int value ) {
	vmSymbol_t	*sym;
	static vmSymbol_t	nullSym;

	sym = vm->symbols;
	if ( !sym ) {
		return &nullSym;
	}

	while ( sym->next && sym->next->symValue <= value ) {
		sym = sym->next;
	}

	return sym;
}


/*
===============
VM_SymbolToValue
===============
*/
int VM_SymbolToValue( vm_t *vm, const char *symbol ) {
	vmSymbol_t	*sym;

	for ( sym = vm->symbols ; sym ; sym = sym->next ) {
		if ( !strcmp( symbol, sym->symName ) ) {
			return sym->symValue;
		}
	}
	return 0;
}


/*
=====================
VM_SymbolForCompiledPointer
=====================
*/
#if 0 // 64bit!
const char *VM_SymbolForCompiledPointer( vm_t *vm, void *code ) {
	int			i;

	if ( code < (void *)vm->codeBase.ptr ) {
		return "Before code block";
	}
	if ( code >= (void *)(vm->codeBase.ptr + vm->codeLength) ) {
		return "After code block";
	}

	// find which original instruction it is after
	for ( i = 0 ; i < vm->codeLength ; i++ ) {
		if ( (void *)vm->instructionPointers[i] > code ) {
			break;
		}
	}
	i--;

	// now look up the bytecode instruction pointer
	return VM_ValueToSymbol( vm, i );
}
#endif


/*
===============
ParseHex
===============
*/
static int	ParseHex( const char *text ) {
	int		value;
	int		c;

	value = 0;
	while ( ( c = *text++ ) != 0 ) {
		if ( c >= '0' && c <= '9' ) {
			value = value * 16 + c - '0';
			continue;
		}
		if ( c >= 'a' && c <= 'f' ) {
			value = value * 16 + 10 + c - 'a';
			continue;
		}
		if ( c >= 'A' && c <= 'F' ) {
			value = value * 16 + 10 + c - 'A';
			continue;
		}
	}

	return value;
}


/*
===============
VM_LoadSymbols
===============
*/
static void VM_LoadSymbols( vm_t *vm ) {
	union {
		char	*c;
		void	*v;
	} mapfile;
	const char *text_p, *token;
	char	name[MAX_QPATH];
	char	symbols[MAX_QPATH];
	vmSymbol_t	**prev, *sym;
	int		count;
	int		value;
	int		chars;
	int		segment;
	int		numInstructions;

	// don't load symbols if not developer
	if ( !com_developer->integer ) {
		return;
	}

	COM_StripExtension(vm->name, name, sizeof(name));
	Com_sprintf( symbols, sizeof( symbols ), "vm/%s.map", name );
	FS_ReadFile( symbols, &mapfile.v );
	if ( !mapfile.c ) {
		Com_Printf( "Couldn't load symbol file: %s\n", symbols );
		return;
	}

	numInstructions = vm->instructionCount;

	// parse the symbols
	text_p = mapfile.c;
	prev = &vm->symbols;
	count = 0;

	while ( 1 ) {
		token = COM_Parse( &text_p );
		if ( !token[0] ) {
			break;
		}
		segment = ParseHex( token );
		if ( segment ) {
			COM_Parse( &text_p );
			COM_Parse( &text_p );
			continue;		// only load code segment values
		}

		token = COM_Parse( &text_p );
		if ( !token[0] ) {
			Com_Printf( "WARNING: incomplete line at end of file\n" );
			break;
		}
		value = ParseHex( token );

		token = COM_Parse( &text_p );
		if ( !token[0] ) {
			Com_Printf( "WARNING: incomplete line at end of file\n" );
			break;
		}
		chars = strlen( token );
		sym = Hunk_Alloc( sizeof( *sym ) + chars, h_high );
		*prev = sym;
		prev = &sym->next;
		sym->next = NULL;

		// convert value from an instruction number to a code offset
		if ( vm->instructionPointers && value >= 0 && value < numInstructions ) {
			value = vm->instructionPointers[value];
		}

		sym->symValue = value;
		Q_strncpyz( sym->symName, token, chars + 1 );

		count++;
	}

	vm->numSymbols = count;
	Com_Printf( "%i symbols parsed from %s\n", count, symbols );
	FS_FreeFile( mapfile.v );
}


/*
============
VM_DllSyscall

Dlls will call this directly

 rcg010206 The horror; the horror.

  The syscall mechanism relies on stack manipulation to get its args.
   This is likely due to C's inability to pass "..." parameters to
   a function in one clean chunk. On PowerPC Linux, these parameters
   are not necessarily passed on the stack, so while (&arg[0] == arg)
   is true, (&arg[1] == 2nd function parameter) is not necessarily
   accurate, as arg's value might have been stored to the stack or
   other piece of scratch memory to give it a valid address, but the
   next parameter might still be sitting in a register.

  Quake's syscall system also assumes that the stack grows downward,
   and that any needed types can be squeezed, safely, into a signed int.

  This hack below copies all needed values for an argument to a
   array in memory, so that Quake can get the correct values. This can
   also be used on systems where the stack grows upwards, as the
   presumably standard and safe stdargs.h macros are used.

  As for having enough space in a signed int for your datatypes, well,
   it might be better to wait for DOOM 3 before you start porting.  :)

  The original code, while probably still inherently dangerous, seems
   to work well enough for the platforms it already works on. Rather
   than add the performance hit for those platforms, the original code
   is still in use there.

  For speed, we just grab 15 arguments, and don't worry about exactly
   how many the syscall actually needs; the extra is thrown away.

============
*/
#if 0 // - disabled because now is different for each module
intptr_t QDECL VM_DllSyscall( intptr_t arg, ... ) {
#if !id386 || defined __clang__
  // rcg010206 - see commentary above
  intptr_t	args[16];
  va_list	ap;
  int i;

  args[0] = arg;

  va_start( ap, arg );
  for (i = 1; i < ARRAY_LEN( args ); i++ )
    args[ i ] = va_arg( ap, intptr_t );
  va_end( ap );

  return currentVM->systemCall( args );
#else // original id code
	return currentVM->systemCall( &arg );
#endif
}
#endif


static void VM_SwapLongs( void *data, int length )
{
#ifndef Q3_LITTLE_ENDIAN
	int32_t *ptr;
	int i;
	ptr = (int32_t *) data;
	length /= sizeof( int32_t );
	for ( i = 0; i < length; i++ ) {
		ptr[ i ] = LittleLong( ptr[ i ] );
	}
#endif
}


static int Load_JTS( vm_t *vm, uint32_t crc32, void *data, int vmPakIndex ) {
	char		filename[MAX_QPATH];
	int			header[2];
	int			length;
	fileHandle_t fh;

	// load the image
	Com_sprintf( filename, sizeof(filename), "vm/%s.jts", vm->name );
	if ( data )
		Com_Printf( "Loading jts file %s...\n", filename );

	length = FS_FOpenFileRead( filename, &fh, qtrue );

	if ( fh == FS_INVALID_HANDLE ) {
		if ( data )
			Com_Printf( " not found.\n" );
		return -1;
	}

	if ( fs_lastPakIndex != vmPakIndex ) {
		Com_DPrintf( " invalid pak index %i (expecting %i) for %s.\n", fs_lastPakIndex, vmPakIndex, filename );
		FS_FCloseFile( fh );
		return -1;
	}

	if ( length < sizeof( header ) ) {
		if ( data )
			Com_Printf( " bad filesize %i for %s.\n", length, filename );
		FS_FCloseFile( fh );
		return -1;
	}

	if ( FS_Read( header, sizeof( header ), fh ) != sizeof( header ) ) {
		if ( data )
			Com_Printf( " error reading header of %s.\n", filename );
		FS_FCloseFile( fh );
		return -1;
	}

	// byte swap the header
	VM_SwapLongs( header, sizeof( header  ) );

	if ( (unsigned int)header[0] != crc32 ) {
		if ( data )
			Com_Printf( " crc32 mismatch: %08X <-> %08X.\n", header[0], crc32 );
		FS_FCloseFile( fh );
		return -1;
	}

	if ( header[1] < 0 || header[1] != (length - (int)sizeof( header ) ) ) {
		if ( data )
			Com_Printf( " bad file header.\n" );
		FS_FCloseFile( fh );
		return -1;
	}

	length -= sizeof( header ); // skip header and filesize

	// we need just filesize
	if ( !data ) {
		FS_FCloseFile( fh );
		return length;
	}

	FS_Read( data, length, fh );
	FS_FCloseFile( fh );

	// byte swap the data
	VM_SwapLongs( data, length );

	return length;
}


/*
=================
VM_ValidateHeader
=================
*/
static char *VM_ValidateHeader( vmHeader_t *header, int fileSize )
{
	static char errMsg[128];
	int n;

	// truncated
	if ( fileSize < ( sizeof( vmHeader_t ) - sizeof( int32_t ) ) ) {
		sprintf( errMsg, "truncated image header (%i bytes long)", fileSize );
		return errMsg;
	}

	// bad magic
	if ( LittleLong( header->vmMagic ) != VM_MAGIC && LittleLong( header->vmMagic ) != VM_MAGIC_VER2 ) {
		sprintf( errMsg, "bad file magic %08x", LittleLong( header->vmMagic ) );
		return errMsg;
	}

	// truncated
	if ( fileSize < sizeof( vmHeader_t ) && LittleLong( header->vmMagic ) != VM_MAGIC_VER2 ) {
		sprintf( errMsg, "truncated image header (%i bytes long)", fileSize );
		return errMsg;
	}

	if ( LittleLong( header->vmMagic ) == VM_MAGIC_VER2 )
		n = sizeof( vmHeader_t );
	else
		n = ( sizeof( vmHeader_t ) - sizeof( int32_t ) );

	// byte swap the header
	VM_SwapLongs( header, n );

	// bad code offset
	if ( header->codeOffset >= fileSize ) {
		sprintf( errMsg, "bad code segment offset %i", header->codeOffset );
		return errMsg;
	}

	// bad code length
	if ( header->codeLength <= 0 || header->codeOffset + header->codeLength > fileSize ) {
		sprintf( errMsg, "bad code segment length %i", header->codeLength );
		return errMsg;
	}

	// bad data offset
	if ( header->dataOffset >= fileSize || header->dataOffset != header->codeOffset + header->codeLength ) {
		sprintf( errMsg, "bad data segment offset %i", header->dataOffset );
		return errMsg;
	}

	// bad data length
	if ( header->dataOffset + header->dataLength > fileSize )  {
		sprintf( errMsg, "bad data segment length %i", header->dataLength );
		return errMsg;
	}

	if ( header->vmMagic == VM_MAGIC_VER2 ) {
		// bad lit/jtrg length
		if ( header->dataOffset + header->dataLength + header->litLength + header->jtrgLength != fileSize ) {
			sprintf( errMsg, "bad lit/jtrg segment length" );
			return errMsg;
		}
	}
	// bad lit length
	else if ( header->dataOffset + header->dataLength + header->litLength != fileSize ) {
		sprintf( errMsg, "bad lit segment length %i", header->litLength );
		return errMsg;
	}

	return NULL;
}


/*
=================
VM_LoadQVM

Load a .qvm file

if ( alloc )
 - Validate header, swap data
 - Alloc memory for data/instructions
 - Alloc memory for instructionPointers - NOT NEEDED
 - Load instructions
 - Clear/load data
else
 - Check for header changes
 - Clear/load data

=================
*/
static vmHeader_t *VM_LoadQVM( vm_t *vm, qboolean alloc ) {
	int					length;
	unsigned int		dataLength;
	unsigned int		dataAlloc;
	char				filename[MAX_QPATH], *errorMsg;
	unsigned int		crc32sum;
	qboolean			tryjts;
	vmHeader_t			*header;
	int					vmPakIndex;

	// load the image
	Com_sprintf( filename, sizeof(filename), "vm/%s.qvm", vm->name );
	Com_Printf( "Loading vm file %s...\n", filename );
	length = FS_ReadFile( filename, (void **)&header );
	if ( !header ) {
		Com_Printf( "Failed.\n" );
		VM_Free( vm );
		return NULL;
	}

	vmPakIndex = fs_lastPakIndex;

	crc32sum = crc32_buffer( (const byte*) header, length );

	// will also swap header
	errorMsg = VM_ValidateHeader( header, length );
	if ( errorMsg ) {
		VM_Free( vm );
		FS_FreeFile( header );
		Com_Printf( S_COLOR_RED "%s\n", errorMsg );
		return NULL;
	}

	vm->crc32sum = crc32sum;
	tryjts = qfalse;

	if( header->vmMagic == VM_MAGIC_VER2 ) {
		Com_Printf( "...which has vmMagic VM_MAGIC_VER2\n" );
	} else {
		tryjts = qtrue;
	}

	vm->exactDataLength = header->dataLength + header->litLength + header->bssLength;

	dataLength = vm->exactDataLength;
	if ( dataLength < PROGRAM_STACK_SIZE ) {
		dataLength = PROGRAM_STACK_SIZE;
	}

	vm->programStackExtra = PROGRAM_STACK_EXTRA;

	// if rounding difference is larger than extra space we need then reuse it
	if ( log2pad( dataLength, 1 ) - dataLength >= PROGRAM_STACK_EXTRA ) {
#ifdef _DEBUG
		// keep exact size for debug purposes
#else
		// reuse it all for release builds
		vm->programStackExtra = log2pad( dataLength, 1 ) - dataLength;
		// Com_DPrintf( S_COLOR_CYAN "%s: reuse %i bytes for pStack\n", vm->name, vm->programStackExtra );
#endif
	} else {
		dataLength += vm->programStackExtra;
	}

	vm->dataLength = dataLength;

	// round up to next power of 2 so all data operations can be mask protected
	dataLength = log2pad( dataLength, 1 );

	// reserve some space for effective LOCAL+LOAD* checks
	dataAlloc = dataLength + VM_DATA_GUARD_SIZE;

	if ( dataLength >= (1U<<31) || dataAlloc >= (1U<<31) ) {
		// dataLenth is negative int32
		VM_Free( vm );
		FS_FreeFile( header );
		Com_Printf( S_COLOR_RED "%s: data segment is too large\n", __func__ );
		return NULL;
	}

	if ( alloc ) {
		// allocate zero filled space for initialized and uninitialized data
		vm->dataBase = Hunk_Alloc( dataAlloc, h_high );
		vm->dataMask = dataLength - 1;
		vm->dataAlloc = dataAlloc;
	} else {
		// clear the data, but make sure we're not clearing more than allocated
		if ( vm->dataAlloc != dataAlloc ) {
			VM_Free( vm );
			FS_FreeFile( header );
			Com_Printf( S_COLOR_YELLOW "Warning: Data region size of %s not matching after"
					"VM_Restart()\n", filename );
			return NULL;
		}
		Com_Memset( vm->dataBase, 0x0, vm->dataAlloc );
	}

	// copy the intialized data
	Com_Memcpy( vm->dataBase, (byte *)header + header->dataOffset, header->dataLength + header->litLength );

	// byte swap the longs
	VM_SwapLongs( vm->dataBase, header->dataLength );

	if( header->vmMagic == VM_MAGIC_VER2 ) {
		int previousNumJumpTableTargets = vm->numJumpTableTargets;

		header->jtrgLength &= ~0x03;

		vm->numJumpTableTargets = header->jtrgLength >> 2;
		Com_Printf( "Loading %d jump table targets\n", vm->numJumpTableTargets );

		if ( alloc ) {
			vm->jumpTableTargets = (int32_t *) Hunk_Alloc( header->jtrgLength, h_high );
		} else {
			if ( vm->numJumpTableTargets != previousNumJumpTableTargets ) {
				VM_Free( vm );
				FS_FreeFile( header );

				Com_Printf( S_COLOR_YELLOW "Warning: Jump table size of %s not matching after "
					"VM_Restart()\n", filename );
				return NULL;
			}

			Com_Memset( vm->jumpTableTargets, 0, header->jtrgLength );
		}

		Com_Memcpy( vm->jumpTableTargets, (byte *)header + header->dataOffset +
				header->dataLength + header->litLength, header->jtrgLength );

		// byte swap the longs
		VM_SwapLongs( vm->jumpTableTargets, header->jtrgLength );
	}

	if ( tryjts == qtrue && (length = Load_JTS( vm, crc32sum, NULL, vmPakIndex )) >= 0 ) {
		// we are trying to load newer file?
		if ( vm->jumpTableTargets && vm->numJumpTableTargets != length >> 2 ) {
			Com_Printf( S_COLOR_YELLOW "Reload jts file\n" );
			vm->jumpTableTargets = NULL;
			alloc = qtrue;
		}
		vm->numJumpTableTargets = length >> 2;
		Com_Printf( "Loading %d external jump table targets\n", vm->numJumpTableTargets );
		if ( alloc == qtrue ) {
			vm->jumpTableTargets = (int32_t *) Hunk_Alloc( length, h_high );
		} else {
			Com_Memset( vm->jumpTableTargets, 0, length );
		}
		Load_JTS( vm, crc32sum, vm->jumpTableTargets, vmPakIndex );
	}

	return header;
}


static void VM_IgnoreInstructions( instruction_t *buf, const int count ) {
	int i;

	for ( i = 0; i < count; i++ ) {
		Com_Memset( buf + i, 0, sizeof( *buf ) );
		buf[i].op = OP_IGNORE;
	}

	buf[0].value = count > 0 ? count - 1 : 0;
}


static int InvertCondition( int op )
{
	switch ( op ) {
		case OP_EQ: return OP_NE;   // == -> !=
		case OP_NE: return OP_EQ;   // != -> ==

		case OP_LTI: return OP_GEI;	// <  -> >=
		case OP_LEI: return OP_GTI;	// <= -> >
		case OP_GTI: return OP_LEI; // >  -> <=
		case OP_GEI: return OP_LTI; // >= -> <

		case OP_LTU: return OP_GEU;
		case OP_LEU: return OP_GTU;
		case OP_GTU: return OP_LEU;
		case OP_GEU: return OP_LTU;

		case OP_EQF: return OP_NEF;
		case OP_NEF: return OP_EQF;

		case OP_LTF: return OP_GEF;
		case OP_LEF: return OP_GTF;
		case OP_GTF: return OP_LEF;
		case OP_GEF: return OP_LTF;

		default: 
			Com_Error( ERR_DROP, "incorrect condition opcode %i", op );
			return op;
	}
}


/*
=================
VM_FindLocal

search for specified local variable until end of function
=================
*/
static qboolean VM_FindLocal( int32_t addr, const instruction_t *buf, const instruction_t *end, int32_t *back_addr ) {
	int32_t curr_addr = *back_addr;
	while ( buf < end ) {
		if ( buf->op == OP_LOCAL ) {
			if ( buf->value == addr ) {
				return qtrue;
			}
			++buf; continue;
		}
		if ( ops[ buf->op ].flags & JUMP ) {
			if ( buf->value < curr_addr ) {
				curr_addr = buf->value;
			}
			++buf; continue;
		}
		if ( buf->op == OP_JUMP ) {
			if ( buf->value && buf->value < curr_addr ) {
				curr_addr = buf->value;
			}
			++buf; continue;
		}
		if ( buf->op == OP_PUSH && (buf+1)->op == OP_LEAVE ) {
			break;
		}
		++buf;
	}
	*back_addr = curr_addr;
	return qfalse;
}


/*
=================
VM_Fixup

Do some corrections to fix known Q3LCC flaws
=================
*/
static void VM_Fixup( instruction_t *buf, int instructionCount )
{
	int n;
	instruction_t *i;

	i = buf;
	n = 0;

	while ( n < instructionCount )
	{
		if ( i->op == OP_LOCAL ) {

			// skip useless sequences
			if ( (i+1)->op == OP_LOCAL && (i+0)->value == (i+1)->value && (i+2)->op == OP_LOAD4 && (i+3)->op == OP_STORE4 ) {
				VM_IgnoreInstructions( i, 4 );
				i += 4; n += 4;
				continue;
			}

			// [0]OP_LOCAL + [1]OP_CONST + [2]OP_CALL + [3]OP_STORE4
			if ( (i+1)->op == OP_CONST && (i+2)->op == OP_CALL && (i+3)->op == OP_STORE4 && !(i+4)->jused ) {
				// [4]OP_CONST|OP_LOCAL (dest) + [5]OP_LOCAL(temp) + [6]OP_LOAD4 + [7]OP_STORE4
				if ( (i+4)->op == OP_CONST || (i+4)->op == OP_LOCAL ) {
					if ( (i+5)->op == OP_LOCAL && (i+5)->value == (i+0)->value && (i+6)->op == OP_LOAD4 && (i+7)->op == OP_STORE4 ) {
						int32_t back_addr = n;
						int32_t curr_addr = n;
						qboolean do_break = qfalse;

						// make sure that address of (potentially) temporary variable is not referenced further in this function
						if ( VM_FindLocal( i->value, i + 8, buf + instructionCount, &back_addr ) ) {
							i++; n++;
							continue;
						}

						// we have backward jumps in code then check for references before current position
						while ( back_addr < curr_addr ) {
							curr_addr = back_addr;
							if ( VM_FindLocal( i->value, buf + back_addr, i, &back_addr ) ) {
								do_break = qtrue;
								break;
							}
						}
						if ( do_break ) {
							i++; n++;
							continue;
						}

						(i+0)->op = (i+4)->op;
						(i+0)->value = (i+4)->value;
						VM_IgnoreInstructions( i + 4, 4 );
						i += 8;
						n += 8;
						continue;
					}
				}
			}
		}

		if ( i->op == OP_LEAVE && !i->endp ) {
			if ( !(i+1)->jused && (i+1)->op == OP_CONST && (i+2)->op == OP_JUMP ) {
				int v = (i+1)->value;
				if ( buf[ v ].op == OP_PUSH && buf[ v+1 ].op == OP_LEAVE && buf[ v+1 ].endp ) {
					VM_IgnoreInstructions( i + 1, 2 );
					i += 3;
					n += 3;
					continue;
				}
			}
		}

		//n + 0: if ( cond ) goto label1;
		//n + 2: goto label2;
		//n + 3: label1:
		// ...
		//n + x: label2:
		if ( ( ops[i->op].flags & (JUMP | FPU) ) == JUMP && !(i+1)->jused && (i+1)->op == OP_CONST && (i+2)->op == OP_JUMP ) {
			if ( i->value == n + 3 && (i+1)->value >= n + 3 ) {
				i->op = InvertCondition( i->op );
				i->value = ( i + 1 )->value;
				VM_IgnoreInstructions( i + 1, 2 );
				i += 3;
				n += 3;
				continue;
			}
		}
		i++;
		n++;
	}
}


/*
=================
VM_LoadInstructions

loads instructions in structured format
=================
*/
const char *VM_LoadInstructions( const byte *code_pos, int codeLength, int instructionCount, instruction_t *buf )
{
	static char errBuf[ 128 ];
	const byte *code_start, *code_end;
	int i, n, op0, op1, opStack;
	instruction_t *ci;

	code_start = code_pos; // for printing
	code_end = code_pos + codeLength;

	ci = buf;
	opStack = 0;
	op1 = OP_UNDEF;

	// load instructions and perform some initial calculations/checks
	for ( i = 0; i < instructionCount; i++, ci++, op1 = op0 ) {
		op0 = *code_pos;
		if ( op0 < 0 || op0 >= OP_MAX ) {
			sprintf( errBuf, "bad opcode %02X at offset %d", op0, (int)(code_pos - code_start) );
			return errBuf;
		}
		n = ops[ op0 ].size;
		if ( code_pos + 1 + n  > code_end ) {
			sprintf( errBuf, "code_pos > code_end" );
			return errBuf;
		}
		code_pos++;
		ci->op = op0;
		if ( n == 4 ) {
			ci->value = LittleLong( *((int32_t*)code_pos) );
			code_pos += 4;
		} else if ( n == 1 ) {
			ci->value = *((unsigned char*)code_pos);
			code_pos += 1;
		} else {
			ci->value = 0;
		}

		if ( ops[ op0 ].flags & FPU ) {
			ci->fpu = 1;
		}

		// setup jump value from previous const
		if ( op0 == OP_JUMP && op1 == OP_CONST ) {
			ci->value = (ci-1)->value;
		}

		ci->opStack = opStack;
		opStack += ops[ op0 ].stack;
	}

	return NULL;
}


static qboolean safe_address( instruction_t *ci, instruction_t *proc, int dataLength )
{
	if ( ci->op == OP_LOCAL ) {
		// local address can't exceed programStack frame plus 256 bytes of passed arguments
		if ( ci->value < 8 || ( proc && ci->value >= proc->value + 256 ) )
			return qfalse;
		return qtrue;
	}

	if ( ci->op == OP_CONST ) {
		// constant address can't exceed data segment
		if ( ci->value >= dataLength || ci->value < 0 )
			return qfalse;
		return qtrue;
	}

	return qfalse;
}


/*
===============================
VM_CheckInstructions

performs additional consistency and security checks
===============================
*/
const char *VM_CheckInstructions( instruction_t *buf,
								int instructionCount,
								const int32_t *jumpTableTargets,
								int numJumpTableTargets,
								int dataLength )
{
	static char errBuf[ 128 ];
	instruction_t *opStackPtr[ PROC_OPSTACK_SIZE ];
	int i, m, n, v, op0, op1, opStack, pstack;
	instruction_t *ci, *proc;
	int startp, endp;
	int safe_stores;
	int unsafe_stores;

	ci = buf;
	opStack = 0;

	// opstack checks
	for ( i = 0; i < instructionCount; i++, ci++ ) {
		opStack += ops[ ci->op ].stack;
		if ( opStack < 0 ) {
			sprintf( errBuf, "opStack underflow at %i", i );
			return errBuf;
		}
		if ( opStack >= PROC_OPSTACK_SIZE * 4 ) {
			sprintf( errBuf, "opStack overflow at %i", i );
			return errBuf;
		}
	}

	ci = buf;
	pstack = 0;
	opStack = 0;
	safe_stores = 0;
	unsafe_stores = 0;
	op1 = OP_UNDEF;
	proc = NULL;
	Com_Memset( opStackPtr, 0, sizeof( opStackPtr ) );

	startp = 0;
	endp = instructionCount - 1;

	// Additional security checks

	for ( i = 0; i < instructionCount; i++, ci++, op1 = op0 ) {
		op0 = ci->op;

		m = ops[ ci->op ].stack;
		opStack += m;
		if ( m >= 0 ) {
			// do some FPU type promotion for more efficient loads
			if ( ci->fpu && ci->op != OP_CVIF ) {
				opStackPtr[ opStack / 4 ]->fpu = 1;
			}
			opStackPtr[ opStack >> 2 ] = ci;
		} else {
			if ( ci->fpu ) {
				if ( m <= -8 ) {
					opStackPtr[ opStack / 4 + 1 ]->fpu = 1;
					opStackPtr[ opStack / 4 + 2 ]->fpu = 1;
				} else {
					opStackPtr[ opStack / 4 + 0 ]->fpu = 1;
					opStackPtr[ opStack / 4 + 1 ]->fpu = 1;
				}
			} else {
				if ( m <= -8 ) {
					//
				} else {
					opStackPtr[ opStack / 4 + 0 ] = ci;
				}
			}
		}

		// function entry
		if ( op0 == OP_ENTER ) {
			// missing block end
			if ( proc || ( pstack && op1 != OP_LEAVE ) ) {
				sprintf( errBuf, "missing proc end before %i", i );
				return errBuf;
			}
			if ( ci->opStack != 0 ) {
				v = ci->opStack;
				sprintf( errBuf, "bad entry opstack %i at %i", v, i );
				return errBuf;
			}
			v = ci->value;
			if ( v < 0 || v >= PROGRAM_STACK_SIZE || (v & 3) ) {
				sprintf( errBuf, "bad entry programStack %i at %i", v, i );
				return errBuf;
			}

			pstack = ci->value;

			// mark jump target
			ci->jused = 1;
			proc = ci;
			startp = i + 1;

			// locate endproc
			for ( endp = 0, n = i+1 ; n < instructionCount; n++ ) {
				if ( buf[n].op == OP_PUSH && buf[n+1].op == OP_LEAVE ) {
					buf[n+1].endp = 1;
					endp = n;
					break;
				}
			}

			if ( endp == 0 ) {
				sprintf( errBuf, "missing end proc for %i", i );
				return errBuf;
			}

			continue;
		}

		// proc opstack will carry max.possible opstack value
		if ( proc && ci->opStack > proc->opStack )
			proc->opStack = ci->opStack;

		// function return
		if ( op0 == OP_LEAVE ) {
			// bad return programStack
			if ( pstack != ci->value ) {
				v = ci->value;
				sprintf( errBuf, "bad programStack %i at %i", v, i );
				return errBuf;
			}
			// bad opStack before return
			if ( ci->opStack != 4 ) {
				v = ci->opStack;
				sprintf( errBuf, "bad opStack %i at %i", v, i );
				return errBuf;
			}
			v = ci->value;
			if ( v < 0 || v >= PROGRAM_STACK_SIZE || (v & 3) ) {
				sprintf( errBuf, "bad return programStack %i at %i", v, i );
				return errBuf;
			}
			if ( op1 == OP_PUSH ) {
				if ( proc == NULL ) {
					sprintf( errBuf, "unexpected proc end at %i", i );
					return errBuf;
				}
				proc = NULL;
				startp = i + 1; // next instruction
				endp = instructionCount - 1; // end of the image
			}
			continue;
		}

		// conditional jumps
		if ( ops[ ci->op ].flags & JUMP ) {
			v = ci->value;
			// conditional jumps should have opStack >= 8
			if ( ci->opStack < 8 ) {
				sprintf( errBuf, "bad jump opStack %i at %i", ci->opStack, i );
				return errBuf;
			}
			//if ( v >= header->instructionCount ) {
			// allow only local proc jumps
			if ( v < startp || v > endp ) {
				sprintf( errBuf, "jump target %i at %i is out of range (%i,%i)", v, i-1, startp, endp );
				return errBuf;
			}
			if ( buf[v].opStack != ci->opStack - 8 ) {
				n = buf[v].opStack;
				sprintf( errBuf, "jump target %i has bad opStack %i", v, n );
				return errBuf;
			}
			// mark jump target
			buf[v].jused = 1;
			continue;
		}

		// unconditional jumps
		if ( op0 == OP_JUMP ) {
			// jumps should have opStack >= 4
			if ( ci->opStack < 4 ) {
				sprintf( errBuf, "bad jump opStack %i at %i", ci->opStack, i );
				return errBuf;
			}
			if ( op1 == OP_CONST ) {
				v = buf[i-1].value;
				// allow only local jumps
				if ( v < startp || v > endp ) {
					sprintf( errBuf, "jump target %i at %i is out of range (%i,%i)", v, i-1, startp, endp );
					return errBuf;
				}
				if ( buf[v].opStack != ci->opStack - 4 ) {
					n = buf[v].opStack;
					sprintf( errBuf, "jump target %i has bad opStack %i", v, n );
					return errBuf;
				}
				if ( buf[v].op == OP_ENTER ) {
					n = buf[v].op;
					sprintf( errBuf, "jump target %i has bad opcode %s", v, opname[ n ] );
					return errBuf;
				}
				if ( v == (i-1) ) {
					sprintf( errBuf, "self loop at %i", v );
					return errBuf;
				}
				// mark jump target
				buf[v].jused = 1;
			} else {
				if ( proc )
					proc->swtch = 1;
				else
					ci->swtch = 1;
			}
			continue;
		}

		if ( op0 == OP_CALL ) {
			if ( ci->opStack < 4 ) {
				sprintf( errBuf, "bad call opStack at %i", i );
				return errBuf;
			}
			if ( op1 == OP_CONST ) {
				v = buf[i-1].value;
				// analyse only local function calls
				if ( v >= 0 ) {
					if ( v >= instructionCount ) {
						sprintf( errBuf, "call target %i is out of range", v );
						return errBuf;
					}
					if ( buf[v].op != OP_ENTER ) {
						n = buf[v].op;
						sprintf( errBuf, "call target %i has bad opcode %s", v, opname[ n ] );
						return errBuf;
					}
					if ( v == 0 ) {
						sprintf( errBuf, "explicit vmMain call inside VM at %i", i );
						return errBuf;
					}
					// mark jump target
					buf[v].jused = 1;
				}
			}
			continue;
		}

		if ( ci->op == OP_ARG ) {
			v = ci->value & 255;
			if ( proc == NULL ) {
				sprintf( errBuf, "missing proc frame for %s %i at %i", opname[ ci->op ], v, i );
				return errBuf;
			}
			// argument can't exceed programStack frame
			if ( v < 8 || v > pstack - 4 || (v & 3) ) {
				sprintf( errBuf, "bad argument address %i at %i", v, i );
				return errBuf;
			}
			continue;
		}

		if ( ci->op == OP_LOCAL ) {
			v = ci->value;
			if ( proc == NULL ) {
				sprintf( errBuf, "missing proc frame for %s %i at %i", opname[ ci->op ], v, i );
				return errBuf;
			}
			if ( (ci+1)->op == OP_LOAD4 || (ci+1)->op == OP_LOAD2 || (ci+1)->op == OP_LOAD1 ) {
				if ( !safe_address( ci, proc, dataLength ) ) {
					sprintf( errBuf, "bad %s address %i at %i", opname[ ci->op ], v, i );
					return errBuf;
				}
			}
			continue;
		}

		if ( ci->op == OP_LOAD4 && op1 == OP_CONST ) {
			v = (ci-1)->value;
			if ( v < 0 || v > dataLength - 4 ) {
				sprintf( errBuf, "bad %s address %i at %i", opname[ ci->op ], v, i - 1 );
				return errBuf;
			}
			continue;
		}

		if ( ci->op == OP_LOAD2 && op1 == OP_CONST ) {
			v = (ci-1)->value;
			if ( v < 0 || v > dataLength - 2 ) {
				sprintf( errBuf, "bad %s address %i at %i", opname[ ci->op ], v, i - 1 );
				return errBuf;
			}
			continue;
		}

		if ( ci->op == OP_LOAD1 && op1 == OP_CONST ) {
			v =  (ci-1)->value;
			if ( v < 0 || v > dataLength - 1 ) {
				sprintf( errBuf, "bad %s address %i at %i", opname[ ci->op ], v, i - 1 );
				return errBuf;
			}
			continue;
		}

		if ( ci->op == OP_STORE4 || ci->op == OP_STORE2 || ci->op == OP_STORE1 ) {
			instruction_t *x = opStackPtr[ opStack / 4 + 1 ];
			if ( x->op == OP_CONST || x->op == OP_LOCAL ) {
				if ( safe_address( x, proc, dataLength ) ) {
					ci->safe = 1;
					safe_stores++;
					continue;
				} else {
					sprintf( errBuf, "bad %s address %i at %i", opname[ ci->op ], x->value, (int)(x - buf) );
					return errBuf;
				}
			}
			unsafe_stores++;
			continue;
		}

		if ( ci->op == OP_BLOCK_COPY ) {
			instruction_t *src = opStackPtr[ opStack / 4 + 2 ];
			instruction_t *dst = opStackPtr[ opStack / 4 + 1 ];
			int safe = 0;
			v = ci->value;
			if ( v >= dataLength ) {
				sprintf( errBuf, "bad count %i for block copy at %i", v, i - 1 );
				return errBuf;
			}
			if ( src->op == OP_LOCAL || src->op == OP_CONST ) {
				if ( !safe_address( src, proc, dataLength ) ) {
					sprintf( errBuf, "bad src for block copy at %i", (int)(dst - buf) );
					return errBuf;
				}
				src->safe = 1;
				safe++;
			}
			if ( dst->op == OP_LOCAL || dst->op == OP_CONST ) {
				if ( !safe_address( dst, proc, dataLength ) ) {
					sprintf( errBuf, "bad dst for block copy at %i", (int)(dst - buf) );
					return errBuf;
				}
				dst->safe = 1;
				safe++;
			}
			if ( safe == 2 ) {
				ci->safe = 1;
			}
		}
//		op1 = op0;
//		ci++;
	}

	if ( ( safe_stores + unsafe_stores ) > 0 ) {
		Com_DPrintf( "%s: safe stores - %i (%i%%)\n", __func__, safe_stores, safe_stores * 100 / ( safe_stores + unsafe_stores ) );
	}

	if ( op1 != OP_UNDEF && op1 != OP_LEAVE ) {
		sprintf( errBuf, "missing return instruction at the end of the image" );
		return errBuf;
	}

	// ensure that the optimization pass knows about all the jump table targets
	if ( jumpTableTargets ) {
		// first pass - validate
		for( i = 0; i < numJumpTableTargets; i++ ) {
			n = jumpTableTargets[ i ];
			if ( n < 0 || n >= instructionCount ) {
				Com_Printf( S_COLOR_YELLOW "jump target %i set on instruction %i that is out of range [0..%i]",
					i, n, instructionCount - 1 );
				break;
			}
			if ( buf[n].opStack != 0 ) {
				Com_Printf( S_COLOR_YELLOW "jump target %i set on instruction %i (%s) with bad opStack %i\n",
					i, n, opname[ buf[n].op ], buf[n].opStack );
				break;
			}
		}
		if ( i != numJumpTableTargets ) {
			// we may trap this on buggy VM_MAGIC_VER2 images
			// but we can safely optimize code even without JTRGSEG
			// so just switch to VM_MAGIC path here
			goto __noJTS;
		}
		// second pass - apply
		for( i = 0; i < numJumpTableTargets; i++ ) {
			n = jumpTableTargets[ i ];
			buf[ n ].jused = 1;
		}
	} else {
__noJTS:
		v = 0;
		// instructions with opStack > 0 can't be jump labels so it is safe to optimize/merge
		for ( i = 0, ci = buf; i < instructionCount; i++, ci++ ) {
			if ( ci->op == OP_ENTER ) {
				v = ci->swtch;
				continue;
			}
			// if there is a switch statement in function -
			// mark all potential jump labels
			if ( ci->swtch )
				v = ci->swtch;
			if ( ci->opStack > 0 )
				ci->jused = 0;
			else if ( v )
				ci->jused = 1;
		}
	}

	VM_Fixup( buf, instructionCount );

	return NULL;
}


/*
=================
VM_ReplaceInstructions
=================
*/
void VM_ReplaceInstructions( vm_t *vm, instruction_t *buf ) {
	instruction_t *ip;

	//Com_Printf( S_COLOR_GREEN "VMINFO [%s] crc: %08X, ic: %i, dl: %i\n", vm->name, vm->crc32sum, vm->instructionCount, vm->exactDataLength );

	if ( vm->index == VM_CGAME ) {
		if ( vm->crc32sum == 0x3E93FC1A && vm->instructionCount == 123596 && vm->exactDataLength == 2007536 ) {
			ip = buf + 110190;
			if ( ip->op == OP_ENTER && (ip+183)->op == OP_LEAVE && ip->value == (ip+183)->value ) {
				ip++;
				ip->op = OP_CONST;	ip->value = 110372; ip++;
				ip->op = OP_JUMP;	ip->value = 0; ip++;
				ip->op = OP_IGNORE; ip->value = 0;
			}
			if ( buf[4358].op == OP_LOCAL && buf[4358].value == 308 && buf[4359].op == OP_CONST && !buf[4359].value ) {
				buf[4359].value++;
			}
		} else
		if ( vm->crc32sum == 0xF0F1AE90 && vm->instructionCount == 123552 && vm->exactDataLength == 2007520 ) {
			ip = buf + 110177;
			if ( ip->op == OP_ENTER && (ip+183)->op == OP_LEAVE && ip->value == (ip+183)->value ) {
				ip++;
				ip->op = OP_CONST;	ip->value = 110359; ip++;
				ip->op = OP_JUMP;	ip->value = 0; ip++;
				ip->op = OP_IGNORE; ip->value = 0;
			}
			if ( buf[4358].op == OP_LOCAL && buf[4358].value == 308 && buf[4359].op == OP_CONST && !buf[4359].value ) {
				buf[4359].value++;
			}
		} else
		if ( vm->crc32sum == 0x051D4668 && vm->instructionCount == 267812 && vm->exactDataLength == 38064376 ) {
			ip = buf + 235;
			if ( ip->value == 70943 ) {
				VM_IgnoreInstructions( ip, 8 );
			}
		}
	}

	if ( vm->index == VM_GAME ) {
		if ( vm->crc32sum == 0x5AAE0ACC && vm->instructionCount == 251521 && vm->exactDataLength == 1872720 ) {
			vm->forceDataMask = qtrue; // OSP server doing some bad things with memory
		} else {
			vm->forceDataMask = qfalse;
		}
	}

	if ( vm->index == VM_UI ) {
		// fix OSP demo UI
		if ( vm->crc32sum == 0xCA84F31D && vm->instructionCount == 78585 && vm->exactDataLength == 542180 ) {
			if ( memcmp( vm->dataBase + 0x3D2E, "dm_67", 5 ) == 0 ) {
				memcpy( vm->dataBase + 0x3D2E, "dm_??", 5 );
			}
			if ( memcmp( vm->dataBase + 0x3D50, "\"%s.%s\"\n", 8 ) == 0 ) {
				memcpy( vm->dataBase + 0x3D50, "\"%s\"\n", 6 );
			}
		}
		// fix defrag-1.91.25 demo UI - masked Q_strupr() calls for directories and filenames
		if ( vm->crc32sum == 0x6E51985F && vm->instructionCount == 125942 && vm->exactDataLength == 1334788 ) {
			ip = buf + 60150;
			if ( ip[0].op == OP_LOCAL && ip[0].value == 28 && ip[1].op == OP_LOAD4 && ip[2].op == OP_ARG && ip[3].value == 124325 ) {
				VM_IgnoreInstructions( ip, 6 );
				ip = buf + 60438;
				VM_IgnoreInstructions( ip, 6 );
			}
		}
	}
}


/*
=================
VM_Restart

Reload the data, but leave everything else in place
This allows a server to do a map_restart without changing memory allocation
=================
*/
vm_t *VM_Restart( vm_t *vm ) {
	vmHeader_t	*header;

	// DLL's can't be restarted in place
	if ( vm->dllHandle ) {
		syscall_t		systemCall;
		dllSyscall_t	dllSyscall;
		vmIndex_t		index;
		void			*dllImports;
		int				dllApiVersion;

		index = vm->index;
		systemCall = vm->systemCall;
		dllSyscall = vm->dllSyscall;
		dllImports = vm->dllImports;
		dllApiVersion = vm->dllApiVersion;

		VM_Free( vm );

		vm = VM_CreateNative( index, systemCall, dllSyscall, VMI_NATIVE, dllImports, dllApiVersion );
		return vm;
	}

	// load the image
	if( ( header = VM_LoadQVM( vm, qfalse ) ) == NULL ) {
		Com_Printf( S_COLOR_RED "VM_Restart() failed\n" );
		return NULL;
	}

	Com_Printf( "VM_Restart()\n" );

	// free the original file
	FS_FreeFile( header );

	return vm;
}


/*
=================
Sys_LoadDll

Used to load a development dll instead of a virtual machine

TTimo: added some verbosity in debug
=================
*/
static qboolean VM_CreatePathForFile( const char *OSPath ) {
	char	path[MAX_OSPATH * 2 + 1];
	char	*ofs;

	if ( !OSPath || !OSPath[0] ) {
		return qfalse;
	}
	if ( strstr( OSPath, "../" ) || strstr( OSPath, "..\\" ) || strstr( OSPath, "::" ) ) {
		Com_Printf( "WARNING: refusing to create relative path \"%s\"\n", OSPath );
		return qfalse;
	}

	Q_strncpyz( path, OSPath, sizeof( path ) );
	for ( ofs = path; *ofs; ofs++ ) {
		if ( *ofs == '/' || *ofs == '\\' ) {
			*ofs = PATH_SEP;
		}
	}
	for ( ofs = path + 1; *ofs; ofs++ ) {
		if ( *ofs == PATH_SEP ) {
			*ofs = '\0';
			Sys_Mkdir( path );
			*ofs = PATH_SEP;
		}
	}

	return qtrue;
}

static qboolean VM_WriteNativeCacheFile( const char *path, const void *buffer, int size ) {
	FILE	*file;
	size_t	written;

	if ( !path || !path[0] || !buffer || size <= 0 ) {
		return qfalse;
	}
	if ( !VM_CreatePathForFile( path ) ) {
		return qfalse;
	}

	file = Sys_FOpen( path, "wb" );
	if ( !file ) {
		Com_Printf( "VM_LoadDll: failed to open native cache '%s'\n", path );
		return qfalse;
	}

	written = fwrite( buffer, 1, size, file );
	if ( written != (size_t)size ) {
		fclose( file );
		Com_Printf( "VM_LoadDll: short write to native cache '%s'\n", path );
		return qfalse;
	}

	if ( fclose( file ) == EOF ) {
		Com_Printf( "VM_LoadDll: failed to close native cache '%s'\n", path );
		return qfalse;
	}

	return qtrue;
}

static const char *VM_NativeCachePath( const char *filename, char *path, size_t pathSize ) {
	byte	nonce[16];
	char	cacheName[MAX_QPATH];
	const char	*basePath;
	int	i;

	if ( !filename || !filename[0] || !path || pathSize == 0 ) {
		return NULL;
	}

	if ( !vmNativeCacheNonce[0] ) {
		Com_RandomBytes( nonce, sizeof( nonce ) );
		for ( i = 0; i < (int)sizeof( nonce ); ++i ) {
			Com_sprintf( vmNativeCacheNonce + i * 2,
				sizeof( vmNativeCacheNonce ) - (size_t)i * 2, "%02x", nonce[i] );
		}
	}

	basePath = Cvar_VariableString( "fs_homepath" );
	if ( !basePath || !basePath[0] ) {
		basePath = Sys_Pwd();
	}

	Com_sprintf( cacheName, sizeof( cacheName ), "native/%s/%s",
		vmNativeCacheNonce, filename );
	Q_strncpyz( path, FS_BuildOSPath( basePath, ".tmp", cacheName ), pathSize );
	return path;
}

static void VM_PinNativeModule( vmIndex_t index, const char *filename,
	const void *buffer, int length ) {
	vmPinnedNative_t	*pinned;
	byte	*copy;

	if ( (unsigned)index >= VM_COUNT || !filename || !filename[0] ||
		!buffer || length <= 0 || length > VM_PINNED_NATIVE_MAX_BYTES ) {
		return;
	}

	pinned = &vmPinnedNative[index];
	if ( pinned->bytes && pinned->length == length &&
		!Q_stricmp( pinned->filename, filename ) &&
		!memcmp( pinned->bytes, buffer, length ) ) {
		return;
	}

	copy = Z_Malloc( length );
	Com_Memcpy( copy, buffer, length );
	if ( pinned->bytes ) {
		Z_Free( pinned->bytes );
	}
	pinned->bytes = copy;
	pinned->length = length;
	Q_strncpyz( pinned->filename, filename, sizeof( pinned->filename ) );
}

qboolean VM_HasPinnedNativeModule( vmIndex_t index ) {
	if ( (unsigned)index >= VM_COUNT ) {
		return qfalse;
	}

	return vmPinnedNative[index].bytes && vmPinnedNative[index].length > 0
		? qtrue : qfalse;
}

typedef enum {
	VM_DLL_ENTRY_INVALID,
	VM_DLL_ENTRY_LEGACY,
	VM_DLL_ENTRY_STRUCTURED
} vmDllEntryAbi_t;

/*
=================
VM_SelectDllEntryAbi

Retail Quake Live game modules expose dllEntry without vmMain and receive the
structured export/import tables.  Classic Quake III modules expose vmMain and
use the one-argument syscall registration entry point.  Select from the export
shape before invoking dllEntry so a legacy module is never called once with an
incompatible argument list merely as a native-ABI probe.
=================
*/
static vmDllEntryAbi_t VM_SelectDllEntryAbi( dllEntry_t dllEntry, vmMainFunc_t entryPoint,
	void **dllExports, void *dllImports, int *dllApiVersion ) {
	if ( !dllEntry ) {
		return VM_DLL_ENTRY_INVALID;
	}
	if ( entryPoint ) {
		return VM_DLL_ENTRY_LEGACY;
	}
	if ( dllExports && dllImports && dllApiVersion ) {
		return VM_DLL_ENTRY_STRUCTURED;
	}

	return VM_DLL_ENTRY_INVALID;
}

static void *VM_LoadDllFromPakCache( vmIndex_t index, const char *filename ) {
	void	*buffer;
	int		length;
	char	cachePath[MAX_OSPATH * 3 + 1];
	void	*libHandle;

	buffer = NULL;
	length = FS_ReadFile( filename, &buffer );
	if ( length <= 0 || !buffer ) {
		if ( buffer ) {
			FS_FreeFile( buffer );
		}
		return NULL;
	}

	if ( !VM_NativeCachePath( filename, cachePath, sizeof( cachePath ) ) ) {
		FS_FreeFile( buffer );
		return NULL;
	}
	if ( !VM_WriteNativeCacheFile( cachePath, buffer, length ) ) {
		FS_FreeFile( buffer );
		return NULL;
	}

	libHandle = Sys_LoadLibrary( cachePath );
	if ( libHandle ) {
		VM_PinNativeModule( index, filename, buffer, length );
		Com_Printf( "VM_LoadDll: loaded native module cache '%s'\n", cachePath );
	}
	FS_FreeFile( buffer );

	return libHandle;
}

static void *VM_LoadPinnedDll( vmIndex_t index, const char *filename ) {
	const vmPinnedNative_t	*pinned;
	char	cachePath[MAX_OSPATH * 3 + 1];
	void	*libHandle;

	if ( !VM_HasPinnedNativeModule( index ) ) {
		return NULL;
	}

	pinned = &vmPinnedNative[index];
	if ( Q_stricmp( pinned->filename, filename ) ) {
		return NULL;
	}
	if ( !VM_NativeCachePath( filename, cachePath, sizeof( cachePath ) ) ||
		 !VM_WriteNativeCacheFile( cachePath, pinned->bytes, pinned->length ) ) {
		return NULL;
	}

	libHandle = Sys_LoadLibrary( cachePath );
	if ( libHandle ) {
		Com_Printf( "VM_LoadDll: reloaded pinned native module '%s'\n", cachePath );
	}
	return libHandle;
}

static qboolean VM_AssignDllFunction( void *destination, size_t destinationSize,
	void *symbol, const char *symbolName ) {
	if ( destinationSize != sizeof( symbol ) ) {
		Com_Printf( "VM_LoadDll: function pointer for '%s' has an incompatible size\n", symbolName );
		Com_Memset( destination, 0, destinationSize );
		return qfalse;
	}

	/* Dynamic loader APIs expose symbols as data pointers even when the symbol
	 * is a function. Copying the representation avoids a non-portable C cast. */
	Com_Memcpy( destination, &symbol, destinationSize );
	return qtrue;
}

static void * QDECL VM_LoadDll( vmIndex_t index, const char *name,
		vmMainFunc_t *entryPoint, dllSyscall_t systemcalls, void **dllExports,
		void *dllImports, int *dllApiVersion, qboolean pinnedOnly ) {

	char		filename[ MAX_QPATH ];
	void		*libHandle;
	dllEntry_t	dllEntry;
	dllEntryNative_t dllEntryNative;
	vmDllEntryAbi_t entryAbi;
	void		*dllEntrySymbol;
	void		*vmMainSymbol;

	Com_sprintf( filename, sizeof( filename ), "%s" ARCH_STRING DLL_EXT, name );

	if ( pinnedOnly ) {
		libHandle = VM_LoadPinnedDll( index, filename );
	} else {
		// Materialize the virtual-filesystem selection first so the bytes
		// executed now are exactly the bytes a later pure restart can pin.
		libHandle = VM_LoadDllFromPakCache( index, filename );
		if ( !libHandle ) {
			// Preserve development/system layouts whose library is visible to
			// the OS loader but intentionally not readable as a virtual file.
			libHandle = FS_LoadLibrary( filename );
		}
	}

	if ( !libHandle ) {
		if ( pinnedOnly ) {
			Com_Printf( "VM_LoadDLL '%s' has no pinned native image\n", filename );
		} else {
			Com_Printf( "VM_LoadDLL '%s' failed\n", filename );
		}
		return NULL;
	}

	Com_Printf( "VM_LoadDLL '%s' ok\n", filename );

	dllEntrySymbol = Sys_LoadFunction( libHandle, "dllEntry" );
	vmMainSymbol = Sys_LoadFunction( libHandle, "vmMain" );
	if ( !VM_AssignDllFunction( &dllEntry, sizeof( dllEntry ), dllEntrySymbol, "dllEntry" ) ||
		 !VM_AssignDllFunction( &dllEntryNative, sizeof( dllEntryNative ), dllEntrySymbol, "dllEntry" ) ||
		 !VM_AssignDllFunction( entryPoint, sizeof( *entryPoint ), vmMainSymbol, "vmMain" ) ) {
		Sys_UnloadLibrary( libHandle );
		return NULL;
	}
	if ( dllExports ) {
		*dllExports = NULL;
	}
	if ( dllApiVersion ) {
		*dllApiVersion = 0;
	}

	entryAbi = VM_SelectDllEntryAbi( dllEntry, *entryPoint, dllExports, dllImports, dllApiVersion );
	if ( entryAbi == VM_DLL_ENTRY_STRUCTURED ) {
		dllEntryNative( dllExports, dllImports, dllApiVersion );
		if ( !*dllExports ) {
			Com_Printf( "VM_LoadDll(%s) did not publish native exports\n", name );
			Sys_UnloadLibrary( libHandle );
			return NULL;
		}

		Com_Printf( "VM_LoadDll(%s) found native exports at %p (api %d)\n",
			name, *dllExports, *dllApiVersion );
		return libHandle;
	}

	if ( entryAbi == VM_DLL_ENTRY_LEGACY ) {
		Com_Printf( "VM_LoadDll(%s) found legacy vmMain at %p\n", name, vmMainSymbol );
		dllEntry( systemcalls );
		Com_Printf( "VM_LoadDll(%s) succeeded!\n", name );
		return libHandle;
	}

	Sys_UnloadLibrary( libHandle );
	return NULL;
}


/*
================
VM_Create

If image ends in .qvm it will be interpreted, otherwise
it will attempt to load as a system dll
================
*/
vm_t *VM_Create( vmIndex_t index, syscall_t systemCalls, dllSyscall_t dllSyscalls, vmInterpret_t interpret ) {
	return VM_CreateNative( index, systemCalls, dllSyscalls, interpret, NULL, 0 );
}

vm_t *VM_CreateNative( vmIndex_t index, syscall_t systemCalls, dllSyscall_t dllSyscalls, vmInterpret_t interpret, void *dllImports, int dllApiVersion ) {
	int			remaining;
	const char	*name;
	vmHeader_t	*header;
	vm_t		*vm;

	if ( !systemCalls ) {
		Com_Error( ERR_FATAL, "VM_Create: bad parms" );
	}

	if ( (unsigned)index >= VM_COUNT ) {
		Com_Error( ERR_DROP, "VM_Create: bad vm index %i", index );
	}

	remaining = Hunk_MemoryRemaining();

	vm = &vmTable[ index ];

	// see if we already have the VM
	if ( vm->name ) {
		if ( vm->index != index ) {
			Com_Error( ERR_DROP, "VM_Create: bad allocated vm index %i", vm->index );
			return NULL;
		}
		return vm;
	}

	name = vmName[ index ];

	vm->name = name;
	vm->index = index;
	vm->systemCall = systemCalls;
	vm->dllSyscall = dllSyscalls;
	vm->dllImports = dllImports;
	vm->dllApiVersion = dllApiVersion;
	vm->privateFlag = CVAR_PRIVATE;

	// never allow dll loading with a demo
	if ( interpret == VMI_NATIVE ) {
		if ( Cvar_VariableIntegerValue( "fs_restrict" ) ) {
			interpret = VMI_COMPILED;
		}
	}

	if ( interpret == VMI_NATIVE || interpret == VMI_PINNED_NATIVE ) {
		// try to load as a system dll
		Com_Printf( "Loading dll file %s.\n", name );
		vm->dllHandle = VM_LoadDll( index, name, &vm->entryPoint, dllSyscalls,
			&vm->dllExports, vm->dllImports, vm->dllImports ? &vm->dllApiVersion : NULL,
			interpret == VMI_PINNED_NATIVE ? qtrue : qfalse );
		if ( vm->dllHandle ) {
			if ( vm->dllExports && !VM_ValidateNativeDllInterface( vm ) ) {
				Sys_UnloadLibrary( vm->dllHandle );
				vm->dllHandle = NULL;
				vm->dllExports = NULL;
				vm->entryPoint = NULL;
			} else {
				vm->privateFlag = 0; // allow reading private cvars
				vm->dataAlloc = ~0U;
				vm->dataMask = ~0U;
				vm->dataBase = 0;
				return vm;
			}
		}

		Com_Printf( "Failed to load dll, looking for qvm.\n" );
		interpret = VMI_COMPILED;
	}

	// load the image
	if( ( header = VM_LoadQVM( vm, qtrue ) ) == NULL ) {
		VM_Free( vm );
		return NULL;
	}

	// allocate space for the jump targets, which will be filled in by the compile/prep functions
	vm->instructionCount = header->instructionCount;
	//vm->instructionPointers = Hunk_Alloc(vm->instructionCount * sizeof(*vm->instructionPointers), h_high);
	vm->instructionPointers = NULL;

	// copy or compile the instructions
	vm->codeLength = header->codeLength;

	// the stack is implicitly at the end of the image
	vm->programStack = vm->dataMask + 1;
	vm->stackBottom = vm->programStack - PROGRAM_STACK_SIZE - vm->programStackExtra;

	vm->compiled = qfalse;

#ifdef NO_VM_COMPILED
	if ( interpret >= VMI_COMPILED ) {
		Com_Printf( "Architecture doesn't have a bytecode compiler, using interpreter\n" );
		interpret = VMI_BYTECODE;
	}
#else
	if ( interpret >= VMI_COMPILED ) {
		if ( VM_Compile( vm, header ) ) {
			vm->compiled = qtrue;
		}
	}
#endif
	// VM_Compile may have reset vm->compiled if compilation failed
	if ( !vm->compiled ) {
		if ( !VM_PrepareInterpreter2( vm, header ) ) {
			FS_FreeFile( header );	// free the original file
			VM_Free( vm );
			return NULL;
		}
	}

	// free the original file
	FS_FreeFile( header );

	// load the map file
	VM_LoadSymbols( vm );

	Com_Printf( "%s loaded in %d bytes on the hunk\n", vm->name, remaining - Hunk_MemoryRemaining() );

	return vm;
}


/*
==============
VM_Free
==============
*/
void VM_Free( vm_t *vm ) {

	if( !vm ) {
		return;
	}

	if ( vm->callLevel ) {
		if ( !forced_unload ) {
			Com_Error( ERR_FATAL, "VM_Free(%s) on running vm", vm->name );
			return;
		} else {
			Com_Printf( "forcefully unloading %s vm\n", vm->name );
		}
	}

	if ( vm->destroy )
		vm->destroy( vm );

	if ( vm->dllHandle )
		Sys_UnloadLibrary( vm->dllHandle );

#if 0	// now automatically freed by hunk
	if ( vm->codeBase.ptr ) {
		Z_Free( vm->codeBase.ptr );
	}
	if ( vm->dataBase ) {
		Z_Free( vm->dataBase );
	}
	if ( vm->instructionPointers ) {
		Z_Free( vm->instructionPointers );
	}
#endif
	Com_Memset( vm, 0, sizeof( *vm ) );
}


void VM_Clear( void ) {
	int i;
	for ( i = 0; i < VM_COUNT; i++ ) {
		VM_Free( &vmTable[ i ] );
	}
}


void VM_Forced_Unload_Start(void) {
	forced_unload = 1;
}


void VM_Forced_Unload_Done(void) {
	forced_unload = 0;
}


/*
==============
VM_Call


Upon a system call, the stack will look like:

sp+32	parm1
sp+28	parm0
sp+24	return value
sp+20	return address
sp+16	local1
sp+14	local0
sp+12	arg1
sp+8	arg0
sp+4	return stack
sp		return address

An interpreted function will immediately execute
an OP_ENTER instruction, which will subtract space for
locals from sp
==============
*/

static intptr_t VM_CallNativeExports( vm_t *vm, int callnum, const intptr_t *args ) {
	void **dllExports;
	void *exportFunc;

	if ( !vm->dllExports ) {
		Com_Error( ERR_FATAL, "VM_CallNativeExports: no exports for %s", vm->name );
	}

	dllExports = (void **)vm->dllExports;

	if ( vm->index == VM_GAME ) {
		switch ( callnum ) {
		case GAME_INIT:
			exportFunc = dllExports[GAME_NATIVE_EXPORT_INIT];
			if ( !exportFunc ) {
				break;
			}
			((void (QDECL *)( int, int, qboolean ))exportFunc)(
				(int)args[0], (int)args[1], VM_NormalizeQbooleanArg( args[2] ) );
			return 0;
		case GAME_SHUTDOWN:
			exportFunc = dllExports[GAME_NATIVE_EXPORT_SHUTDOWN];
			if ( !exportFunc ) {
				break;
			}
			((void (QDECL *)( qboolean ))exportFunc)( VM_NormalizeQbooleanArg( args[0] ) );
			return 0;
		case GAME_CLIENT_CONNECT:
			exportFunc = dllExports[GAME_NATIVE_EXPORT_CLIENT_CONNECT];
			if ( !exportFunc ) {
				break;
			}
			return (intptr_t)((const char *(QDECL *)( int, qboolean, qboolean ))exportFunc)(
				(int)args[0], VM_NormalizeQbooleanArg( args[1] ), VM_NormalizeQbooleanArg( args[2] ) );
		case GAME_CLIENT_BEGIN:
			exportFunc = dllExports[GAME_NATIVE_EXPORT_CLIENT_BEGIN];
			if ( !exportFunc ) {
				break;
			}
			((void (QDECL *)( int ))exportFunc)( (int)args[0] );
			return 0;
		case GAME_CLIENT_USERINFO_CHANGED:
			exportFunc = dllExports[GAME_NATIVE_EXPORT_CLIENT_USERINFO_CHANGED];
			if ( !exportFunc ) {
				break;
			}
			((void (QDECL *)( int ))exportFunc)( (int)args[0] );
			return 0;
		case GAME_CLIENT_DISCONNECT:
			exportFunc = dllExports[GAME_NATIVE_EXPORT_CLIENT_DISCONNECT];
			if ( !exportFunc ) {
				break;
			}
			((void (QDECL *)( int ))exportFunc)( (int)args[0] );
			return 0;
		case GAME_CLIENT_COMMAND:
			exportFunc = dllExports[GAME_NATIVE_EXPORT_CLIENT_COMMAND];
			if ( !exportFunc ) {
				break;
			}
			((void (QDECL *)( int ))exportFunc)( (int)args[0] );
			return 0;
		case GAME_CLIENT_THINK:
			exportFunc = dllExports[GAME_NATIVE_EXPORT_CLIENT_THINK];
			if ( !exportFunc ) {
				break;
			}
			((void (QDECL *)( int ))exportFunc)( (int)args[0] );
			return 0;
		case GAME_RUN_FRAME:
			exportFunc = dllExports[GAME_NATIVE_EXPORT_RUN_FRAME];
			if ( !exportFunc ) {
				break;
			}
			((void (QDECL *)( int ))exportFunc)( (int)args[0] );
			return 0;
		case GAME_CONSOLE_COMMAND:
			exportFunc = dllExports[GAME_NATIVE_EXPORT_CONSOLE_COMMAND];
			if ( !exportFunc ) {
				break;
			}
			return VM_NormalizeQbooleanResult( ((qboolean (QDECL *)( void ))exportFunc)() );
		case BOTAI_START_FRAME:
			exportFunc = dllExports[GAME_NATIVE_EXPORT_BOTAI_START_FRAME];
			if ( !exportFunc ) {
				break;
			}
			return ((int (QDECL *)( int ))exportFunc)( (int)args[0] );
		case GAME_CAN_CLIENT_SEE_CLIENT:
			exportFunc = dllExports[GAME_NATIVE_EXPORT_CAN_CLIENT_SEE_CLIENT];
			if ( !exportFunc ) {
				break;
			}
			return VM_NormalizeQbooleanResult(
				((qboolean (QDECL *)( int, int ))exportFunc)( (int)args[0], (int)args[1] ) );
		case GAME_FREEZE_CAN_SEE_THAW_PROGRESS_EVENT:
			exportFunc = dllExports[GAME_NATIVE_EXPORT_FREEZE_CAN_SEE_THAW_PROGRESS_EVENT];
			if ( !exportFunc ) {
				break;
			}
			return VM_NormalizeQbooleanResult(
				((qboolean (QDECL *)( int, int ))exportFunc)( (int)args[0], (int)args[1] ) );
		case GAME_IS_OBJECTIVE_ENTITY:
			exportFunc = dllExports[GAME_NATIVE_EXPORT_IS_OBJECTIVE_ENTITY];
			if ( !exportFunc ) {
				break;
			}
			return VM_NormalizeQbooleanResult( ((qboolean (QDECL *)( int ))exportFunc)( (int)args[0] ) );
		case GAME_SHOULD_SUPPRESS_VOICE_TO_CLIENT:
			exportFunc = dllExports[GAME_NATIVE_EXPORT_SHOULD_SUPPRESS_VOICE_TO_CLIENT];
			if ( !exportFunc ) {
				break;
			}
			return VM_NormalizeQbooleanResult(
				((qboolean (QDECL *)( int, int ))exportFunc)( (int)args[0], (int)args[1] ) );
		case GAME_IS_CLIENT_ADMIN:
			exportFunc = dllExports[GAME_NATIVE_EXPORT_IS_CLIENT_ADMIN];
			if ( !exportFunc ) {
				break;
			}
			return VM_NormalizeQbooleanResult( ((qboolean (QDECL *)( int ))exportFunc)( (int)args[0] ) );
		case GAME_ARE_ENEMY_CLIENTS:
			exportFunc = dllExports[GAME_NATIVE_EXPORT_ARE_ENEMY_CLIENTS];
			if ( !exportFunc ) {
				break;
			}
			return VM_NormalizeQbooleanResult(
				((qboolean (QDECL *)( int, int ))exportFunc)( (int)args[0], (int)args[1] ) );
		case GAME_GET_CLIENT_SCORE:
			exportFunc = dllExports[GAME_NATIVE_EXPORT_GET_CLIENT_SCORE];
			if ( !exportFunc ) {
				break;
			}
			return ((int (QDECL *)( int ))exportFunc)( (int)args[0] );
		default:
			Com_Error( ERR_DROP, "VM_CallNativeExports: bad game call %i", callnum );
		}

		Com_Error( ERR_DROP, "VM_CallNativeExports: missing game export %i", callnum );
	}

#ifndef USE_DEDICATED
	if ( vm->index == VM_UI ) {
		int uiExportIndex = callnum;

		if ( vm->dllApiVersion > UI_API_VERSION ) {
			if ( callnum == UI_GETAPIVERSION ) {
				return vm->dllApiVersion;
			}
			uiExportIndex = callnum - 1;
		}

		if ( uiExportIndex < 0 || uiExportIndex >= UI_NATIVE_EXPORT_COUNT ) {
			Com_Error( ERR_DROP, "VM_CallNativeExports: bad ui export index %i for call %i", uiExportIndex, callnum );
		}

		switch ( callnum ) {
		case UI_GETAPIVERSION:
			exportFunc = dllExports[uiExportIndex];
			if ( !exportFunc ) {
				break;
			}
			return ((int (QDECL *)( void ))exportFunc)();
		case UI_INIT:
			exportFunc = dllExports[uiExportIndex];
			if ( !exportFunc ) {
				break;
			}
			((void (QDECL *)( qboolean ))exportFunc)( VM_NormalizeQbooleanArg( args[0] ) );
			return 0;
		case UI_SHUTDOWN:
			exportFunc = dllExports[uiExportIndex];
			if ( !exportFunc ) {
				break;
			}
			((void (QDECL *)( void ))exportFunc)();
			return 0;
		case UI_KEY_EVENT:
			exportFunc = dllExports[uiExportIndex];
			if ( !exportFunc ) {
				break;
			}
			((void (QDECL *)( int, qboolean, int ))exportFunc)( (int)args[0], VM_NormalizeQbooleanArg( args[1] ), (int)args[2] );
			return 0;
		case UI_MOUSE_EVENT:
			exportFunc = dllExports[uiExportIndex];
			if ( !exportFunc ) {
				break;
			}
			((void (QDECL *)( int, int ))exportFunc)( (int)args[0], (int)args[1] );
			return 0;
		case UI_REFRESH:
			exportFunc = dllExports[uiExportIndex];
			if ( !exportFunc ) {
				break;
			}
			((void (QDECL *)( int ))exportFunc)( (int)args[0] );
			return 0;
		case UI_IS_FULLSCREEN:
			exportFunc = dllExports[uiExportIndex];
			if ( !exportFunc ) {
				break;
			}
			return VM_NormalizeQbooleanResult( ((qboolean (QDECL *)( void ))exportFunc)() );
		case UI_SET_ACTIVE_MENU:
			exportFunc = dllExports[uiExportIndex];
			if ( !exportFunc ) {
				break;
			}
			((void (QDECL *)( uiMenuCommand_t ))exportFunc)( (uiMenuCommand_t)args[0] );
			return 0;
		case UI_CONSOLE_COMMAND:
			exportFunc = dllExports[uiExportIndex];
			if ( !exportFunc ) {
				break;
			}
			return VM_NormalizeQbooleanResult( ((qboolean (QDECL *)( int ))exportFunc)( (int)args[0] ) );
		case UI_DRAW_CONNECT_SCREEN:
			exportFunc = dllExports[uiExportIndex];
			if ( !exportFunc ) {
				break;
			}
			((void (QDECL *)( qboolean ))exportFunc)( VM_NormalizeQbooleanArg( args[0] ) );
			return 0;
		case UI_HASUNIQUECDKEY:
			exportFunc = dllExports[uiExportIndex];
			if ( !exportFunc ) {
				break;
			}
			return VM_NormalizeQbooleanResult( ((qboolean (QDECL *)( void ))exportFunc)() );
		case UI_REFRESH_DISPLAY_CONTEXT:
			exportFunc = dllExports[uiExportIndex];
			if ( !exportFunc ) {
				break;
			}
			return ((int (QDECL *)( void ))exportFunc)();
		case UI_MENUS_ANY_VISIBLE:
			exportFunc = dllExports[uiExportIndex];
			if ( !exportFunc ) {
				break;
			}
			return VM_NormalizeQbooleanResult( ((qboolean (QDECL *)( void ))exportFunc)() );
		case UI_FOR_EACH_ARENA_NAME:
			exportFunc = dllExports[uiExportIndex];
			if ( !exportFunc ) {
				break;
			}
			((void (QDECL *)( uiArenaNameCallback_t ))exportFunc)( (uiArenaNameCallback_t)args[0] );
			return 0;
		case UI_DRAW_ADVERTISEMENT_WAIT_SCREEN:
			exportFunc = dllExports[uiExportIndex];
			if ( !exportFunc ) {
				break;
			}
			((void (QDECL *)( const char * ))exportFunc)( (const char *)args[0] );
			return 0;
		default:
			Com_Error( ERR_DROP, "VM_CallNativeExports: bad ui call %i", callnum );
		}

		Com_Error( ERR_DROP, "VM_CallNativeExports: missing ui export %i", callnum );
	}

	if ( vm->index == VM_CGAME ) {
		switch ( callnum ) {
		case CG_INIT:
			exportFunc = dllExports[CG_NATIVE_EXPORT_INIT];
			if ( !exportFunc ) {
				break;
			}
			((void (QDECL *)( int, int, int ))exportFunc)( (int)args[0], (int)args[1], (int)args[2] );
			return 0;
		case CG_SHUTDOWN:
			exportFunc = dllExports[CG_NATIVE_EXPORT_SHUTDOWN];
			if ( !exportFunc ) {
				break;
			}
			((void (QDECL *)( void ))exportFunc)();
			return 0;
		case CG_CONSOLE_COMMAND:
			exportFunc = dllExports[CG_NATIVE_EXPORT_CONSOLE_COMMAND];
			if ( !exportFunc ) {
				break;
			}
			return VM_NormalizeQbooleanResult( ((qboolean (QDECL *)( void ))exportFunc)() );
		case CG_DRAW_ACTIVE_FRAME:
			exportFunc = dllExports[CG_NATIVE_EXPORT_DRAW_ACTIVE_FRAME];
			if ( !exportFunc ) {
				break;
			}
			((void (QDECL *)( int, stereoFrame_t, qboolean ))exportFunc)(
				(int)args[0], (stereoFrame_t)args[1], VM_NormalizeQbooleanArg( args[2] ) );
			return 0;
		case CG_CROSSHAIR_PLAYER:
			exportFunc = dllExports[CG_NATIVE_EXPORT_CROSSHAIR_PLAYER];
			if ( !exportFunc ) {
				break;
			}
			return ((int (QDECL *)( void ))exportFunc)();
		case CG_LAST_ATTACKER:
			exportFunc = dllExports[CG_NATIVE_EXPORT_LAST_ATTACKER];
			if ( !exportFunc ) {
				break;
			}
			return ((int (QDECL *)( void ))exportFunc)();
		case CG_KEY_EVENT:
			exportFunc = dllExports[CG_NATIVE_EXPORT_KEY_EVENT];
			if ( !exportFunc ) {
				break;
			}
			((void (QDECL *)( int, qboolean ))exportFunc)( (int)args[0], VM_NormalizeQbooleanArg( args[1] ) );
			return 0;
		case CG_MOUSE_EVENT:
			exportFunc = dllExports[CG_NATIVE_EXPORT_MOUSE_EVENT];
			if ( !exportFunc ) {
				break;
			}
			((void (QDECL *)( int, int ))exportFunc)( (int)args[0], (int)args[1] );
			return 0;
		case CG_EVENT_HANDLING:
			exportFunc = dllExports[CG_NATIVE_EXPORT_EVENT_HANDLING];
			if ( !exportFunc ) {
				break;
			}
			((void (QDECL *)( int ))exportFunc)( (int)args[0] );
			return 0;
		case CG_CHAT_DOWN:
			exportFunc = dllExports[CG_NATIVE_EXPORT_CHAT_DOWN];
			if ( !exportFunc ) {
				break;
			}
			((void (QDECL *)( void ))exportFunc)();
			return 0;
		case CG_CHAT_UP:
			exportFunc = dllExports[CG_NATIVE_EXPORT_CHAT_UP];
			if ( !exportFunc ) {
				break;
			}
			((void (QDECL *)( void ))exportFunc)();
			return 0;
		case CG_SHOW_1ST_TRACKED_PLAYER:
			exportFunc = dllExports[CG_NATIVE_EXPORT_SHOW_1ST_TRACKED_PLAYER];
			if ( !exportFunc ) {
				break;
			}
			((void (QDECL *)( void ))exportFunc)();
			return 0;
		case CG_SHOW_2ND_TRACKED_PLAYER:
			exportFunc = dllExports[CG_NATIVE_EXPORT_SHOW_2ND_TRACKED_PLAYER];
			if ( !exportFunc ) {
				break;
			}
			((void (QDECL *)( void ))exportFunc)();
			return 0;
		case CG_COPY_CLIENT_IDENTITY:
			exportFunc = dllExports[CG_NATIVE_EXPORT_COPY_CLIENT_IDENTITY];
			if ( !exportFunc ) {
				break;
			}
			return VM_NormalizeQbooleanResult(
				((qboolean (QDECL *)( int, void * ))exportFunc)( (int)args[0], (void *)args[1] ) );
		case CG_GET_CHAT_FIELD_Y:
			exportFunc = dllExports[CG_NATIVE_EXPORT_GET_CHAT_FIELD_Y];
			if ( !exportFunc ) {
				break;
			}
			return ((int (QDECL *)( void ))exportFunc)();
		case CG_GET_CHAT_FIELD_PIXEL_WIDTH:
			exportFunc = dllExports[CG_NATIVE_EXPORT_GET_CHAT_FIELD_PIXEL_WIDTH];
			if ( !exportFunc ) {
				break;
			}
			return ((int (QDECL *)( void ))exportFunc)();
		case CG_GET_CHAT_FIELD_WIDTH_IN_CHARS:
			exportFunc = dllExports[CG_NATIVE_EXPORT_GET_CHAT_FIELD_WIDTH_IN_CHARS];
			if ( !exportFunc ) {
				break;
			}
			return ((int (QDECL *)( void ))exportFunc)();
		case CG_SET_CLIENT_SPEAKING_STATE:
			exportFunc = dllExports[CG_NATIVE_EXPORT_SET_CLIENT_SPEAKING_STATE];
			if ( !exportFunc ) {
				break;
			}
			return ((int (QDECL *)( int, int ))exportFunc)( (int)args[0], (int)args[1] );
		case CG_GET_PHYSICS_TIME:
			exportFunc = dllExports[CG_NATIVE_EXPORT_GET_PHYSICS_TIME];
			if ( !exportFunc ) {
				break;
			}
			return ((int (QDECL *)( void ))exportFunc)();
		default:
			Com_Error( ERR_DROP, "VM_CallNativeExports: bad cgame call %i", callnum );
		}

		Com_Error( ERR_DROP, "VM_CallNativeExports: missing cgame export %i", callnum );
	}
#endif

	Com_Error( ERR_DROP, "VM_CallNativeExports: unknown module %s", vm->name );
	return 0;
}

qboolean VM_CallGameRegisterCvars( vm_t *vm ) {
	void **dllExports;
	void *exportFunc;

	if ( !vm ) {
		Com_Error( ERR_FATAL, "VM_CallGameRegisterCvars with NULL vm" );
	}

	if ( vm->index != VM_GAME || !vm->dllExports ) {
		return qfalse;
	}

	dllExports = (void **)vm->dllExports;
	exportFunc = dllExports[GAME_NATIVE_EXPORT_REGISTER_CVARS];
	if ( !exportFunc ) {
		Com_Error( ERR_DROP, "VM_CallGameRegisterCvars: missing game export %i",
			GAME_NATIVE_EXPORT_REGISTER_CVARS );
	}

	if ( vm->callLevel == 0 ) {
		vm->syscallCount = 0;
	}
	++vm->callLevel;
	((void (QDECL *)( void ))exportFunc)();
	--vm->callLevel;

	return qtrue;
}

#ifndef USE_DEDICATED
qboolean VM_CallCGameRegisterCvars( vm_t *vm ) {
	void **dllExports;
	void *exportFunc;

	if ( !vm ) {
		Com_Error( ERR_FATAL, "VM_CallCGameRegisterCvars with NULL vm" );
	}

	if ( vm->index != VM_CGAME || !vm->dllExports ) {
		return qfalse;
	}

	dllExports = (void **)vm->dllExports;
	exportFunc = dllExports[CG_NATIVE_EXPORT_REGISTER_CVARS];
	if ( !exportFunc ) {
		Com_Error( ERR_DROP, "VM_CallCGameRegisterCvars: missing cgame export %i",
			CG_NATIVE_EXPORT_REGISTER_CVARS );
	}

	if ( vm->callLevel == 0 ) {
		vm->syscallCount = 0;
	}
	++vm->callLevel;
	((void (QDECL *)( void ))exportFunc)();
	--vm->callLevel;

	return qtrue;
}
#endif

intptr_t QDECL VM_Call( vm_t *vm, int nargs, int callnum, ... )
{
	//vm_t	*oldVM;
	intptr_t r;
	int i;

	if ( !vm ) {
		Com_Error( ERR_FATAL, "VM_Call with NULL vm" );
	}

#ifdef DEBUG
	if ( vm_debugLevel ) {
	  Com_Printf( "VM_Call( %d )\n", callnum );
	}

	if ( nargs >= MAX_VMMAIN_CALL_ARGS ) {
		Com_Error( ERR_DROP, "VM_Call: nargs >= MAX_VMMAIN_CALL_ARGS" );
	}
#endif

	// reset syscall counter for top-level calls to detect infinite loops
	if ( vm->callLevel == 0 ) {
		vm->syscallCount = 0;
	}

	++vm->callLevel;

	// if we have a dll loaded, call it directly
	if ( vm->dllExports )
	{
		intptr_t args[MAX_VMMAIN_CALL_ARGS] = { 0 };
		va_list ap;
		va_start( ap, callnum );
		for ( i = 0; i < nargs; i++ ) {
			args[i] = va_arg( ap, intptr_t );
		}
		va_end( ap );

		r = VM_CallNativeExports( vm, callnum, args );
	}
	else if ( vm->entryPoint )
	{
		//rcg010207 -  see dissertation at top of VM_DllSyscall() in this file.
		int32_t args[MAX_VMMAIN_CALL_ARGS-1] = { 0 };
		va_list ap;
		va_start( ap, callnum );
		for ( i = 0; i < nargs; i++ ) {
			args[i] = va_arg( ap, int32_t );
		}
		va_end( ap );

		r = vm->entryPoint(
			callnum,
			args[0], args[1], args[2], args[3], args[4],
			args[5], args[6], args[7], args[8], args[9],
			args[10], args[11], args[12], args[13], args[14] );
	} else {
#if id386 && !defined __clang__ // calling convention doesn't need conversion in some cases
#ifndef NO_VM_COMPILED
		if ( vm->compiled )
			r = VM_CallCompiled( vm, nargs+1, (int32_t*)&callnum );
		else
#endif
			r = VM_CallInterpreted2( vm, nargs+1, (int32_t*)&callnum );
#else
		int32_t args[MAX_VMMAIN_CALL_ARGS];
		va_list ap;

		args[0] = callnum;
		va_start( ap, callnum );
		for ( i = 0; i < nargs; i++ ) {
			args[i+1] = va_arg( ap, int32_t );
		}
		va_end(ap);
#ifndef NO_VM_COMPILED
		if ( vm->compiled )
			r = VM_CallCompiled( vm, nargs+1, &args[0] );
		else
#endif
			r = VM_CallInterpreted2( vm, nargs+1, &args[0] );
#endif
	}
	--vm->callLevel;

	return r;
}


//=================================================================

static int QDECL VM_ProfileSort( const void *a, const void *b ) {
	vmSymbol_t	*sa, *sb;

	sa = *(vmSymbol_t **)a;
	sb = *(vmSymbol_t **)b;

	if ( sa->profileCount < sb->profileCount ) {
		return -1;
	}
	if ( sa->profileCount > sb->profileCount ) {
		return 1;
	}
	return 0;
}


/*
==============
VM_NameToVM
==============
*/
static vm_t *VM_NameToVM( const char *name )
{
	vmIndex_t index;

	if ( !Q_stricmp( name, "game" ) )
		index = VM_GAME;
	else if ( !Q_stricmp( name, "cgame" ) )
		index = VM_CGAME;
	else if ( !Q_stricmp( name, "ui" ) )
		index = VM_UI;
	else {
		Com_Printf( " unknown VM name '%s'\n", name );
		return NULL;
	}

	if ( !vmTable[ index ].name ) {
		Com_Printf( " %s is not running.\n", name );
		return NULL;
	}

	return &vmTable[ index ];
}


/*
==============
VM_VmProfile_f

==============
*/
static void VM_VmProfile_f( void ) {
	vm_t		*vm;
	vmSymbol_t	**sorted, *sym;
	int			i;
	double		total;

	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "usage: %s <game|cgame|ui>\n", Cmd_Argv( 0 ) );
		return;
	}

	vm = VM_NameToVM( Cmd_Argv( 1 ) );
	if ( vm == NULL ) {
		return;
	}

	if ( !vm->numSymbols ) {
		return;
	}

	sorted = Z_Malloc( vm->numSymbols * sizeof( *sorted ) );
	sorted[0] = vm->symbols;
	total = sorted[0]->profileCount;
	for ( i = 1 ; i < vm->numSymbols ; i++ ) {
		sorted[i] = sorted[i-1]->next;
		total += sorted[i]->profileCount;
	}

	qsort( sorted, vm->numSymbols, sizeof( *sorted ), VM_ProfileSort );

	for ( i = 0 ; i < vm->numSymbols ; i++ ) {
		int		perc;

		sym = sorted[i];

		perc = 100 * (float) sym->profileCount / total;
		Com_Printf( "%2i%% %9i %s\n", perc, sym->profileCount, sym->symName );
		sym->profileCount = 0;
	}

	Com_Printf("    %9.0f total\n", total );

	Z_Free( sorted );
}


/*
==============
VM_VmInfo_f
==============
*/
static void VM_VmInfo_f( void ) {
	const vm_t	*vm;
	int		i;

	Com_Printf( "Registered virtual machines:\n" );
	for ( i = 0 ; i < VM_COUNT ; i++ ) {
		vm = &vmTable[i];
		if ( !vm->name ) {
			continue;
		}
		Com_Printf( "%s : ", vm->name );
		if ( vm->dllHandle ) {
			Com_Printf( "native\n" );
			continue;
		}
		if ( vm->compiled ) {
			Com_Printf( "compiled on load\n" );
		} else {
			Com_Printf( "interpreted\n" );
		}
		Com_Printf( "    code length : %7i\n", vm->codeLength );
		Com_Printf( "    table length: %7i\n", vm->instructionCount*4 );
		Com_Printf( "    data length : %7i\n", vm->dataMask + 1 );
	}
}


/*
===============
VM_LogSyscalls

Insert calls to this while debugging the vm compiler
===============
*/
void VM_LogSyscalls( int *args ) {
#if 0
	static	int		callnum;
	static	FILE	*f;

	if ( !f ) {
		f = Sys_FOpen( "syscalls.log", "w" );
		if ( !f ) {
			return;
		}
	}
	callnum++;
	fprintf( f, "%i: %p (%i) = %i %i %i %i\n", callnum, (void*)(args - (int *)currentVM->dataBase),
		args[0], args[1], args[2], args[3], args[4] );
#endif
}
