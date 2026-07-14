/*
===========================================================================

FnQL synthetic native cgame ABI probe

This is test-only code.  It intentionally implements the structured Quake
Live cgame dllEntry contract without reconstructing any game behavior.  The
runtime test builds it as an x86 DLL and verifies a real host-table float
return plus engine-to-cgame active-frame dispatch.

===========================================================================
*/

#define CGAME_NATIVE_API_VERSION 8
#define CG_NATIVE_EXPORT_COUNT 21

enum {
	CG_QL_IMPORT_PRINT = 0,
	CG_QL_IMPORT_CVAR_REGISTER = 4,
	CG_QL_IMPORT_CVAR_REGISTER_RANGE = 5,
	CG_QL_IMPORT_CVAR_UPDATE = 6,
	CG_QL_IMPORT_CVAR_SET_VALUE = 8,
	CG_QL_IMPORT_CVAR_VARIABLEVALUE = 10,
	CG_QL_IMPORT_ARGC = 11,
	CG_QL_IMPORT_ARGV = 12,
	CG_QL_IMPORT_FS_FOPENFILE = 14,
	CG_QL_IMPORT_FS_READ = 15,
	CG_QL_IMPORT_FS_WRITE = 16,
	CG_QL_IMPORT_FS_FCLOSEFILE = 17,
	CG_QL_IMPORT_FS_SEEK = 18,
	CG_QL_IMPORT_ADDCOMMAND = 21,
	CG_QL_IMPORT_GETGLCONFIG = 84,
	CG_QL_IMPORT_GETGAMESTATE = 85,
	CG_QL_IMPORT_GETCURRENTSNAPSHOTNUMBER = 86,
	CG_QL_IMPORT_GETSNAPSHOT = 87,
	CG_QL_IMPORT_GETCURRENTCMDNUMBER = 89,
	CG_QL_IMPORT_GETUSERCMD = 90,
	CG_QL_IMPORT_SETUSERCMDVALUE = 91,
	CG_QL_IMPORT_MEMORY_REMAINING = 92,
	CG_QL_IMPORT_KEY_ISDOWN = 94,
	CG_QL_IMPORT_KEY_GETCATCHER = 95,
	CG_QL_IMPORT_KEY_GETKEY = 97,
	CG_QL_IMPORT_KEY_KEYNUMTOSTRINGBUF = 98,
	CG_QL_IMPORT_KEY_GETBINDINGBUF = 99,
	CG_QL_IMPORT_PC_ADD_GLOBAL_DEFINE = 107,
	CG_QL_IMPORT_PC_LOAD_SOURCE = 108,
	CG_QL_IMPORT_PC_FREE_SOURCE = 109,
	CG_QL_IMPORT_PC_READ_TOKEN = 110,
	CG_QL_IMPORT_PC_SOURCE_FILE_AND_LINE = 111,
	CG_QL_IMPORT_IS_CLIENT_MUTED = 125,
	CG_QL_IMPORT_TOGGLE_CLIENT_MUTE = 126
};

enum {
	CG_NATIVE_EXPORT_INIT = 0,
	CG_NATIVE_EXPORT_REGISTER_CVARS = 1,
	CG_NATIVE_EXPORT_SHUTDOWN = 2,
	CG_NATIVE_EXPORT_CONSOLE_COMMAND = 3,
	CG_NATIVE_EXPORT_DRAW_ACTIVE_FRAME = 4,
	CG_NATIVE_EXPORT_CROSSHAIR_PLAYER = 5,
	CG_NATIVE_EXPORT_LAST_ATTACKER = 6,
	CG_NATIVE_EXPORT_KEY_EVENT = 7,
	CG_NATIVE_EXPORT_MOUSE_EVENT = 8,
	CG_NATIVE_EXPORT_EVENT_HANDLING = 9,
	CG_NATIVE_EXPORT_SHOW_1ST_TRACKED_PLAYER = 10,
	CG_NATIVE_EXPORT_SHOW_2ND_TRACKED_PLAYER = 11,
	CG_NATIVE_EXPORT_CHAT_DOWN = 12,
	CG_NATIVE_EXPORT_CHAT_UP = 13,
	CG_NATIVE_EXPORT_GET_PHYSICS_TIME = 14,
	CG_NATIVE_EXPORT_COPY_CLIENT_IDENTITY = 15,
	CG_NATIVE_EXPORT_RESERVED_NULL = 16,
	CG_NATIVE_EXPORT_GET_CHAT_FIELD_Y = 17,
	CG_NATIVE_EXPORT_GET_CHAT_FIELD_PIXEL_WIDTH = 18,
	CG_NATIVE_EXPORT_GET_CHAT_FIELD_WIDTH_IN_CHARS = 19,
	CG_NATIVE_EXPORT_SET_CLIENT_SPEAKING_STATE = 20
};

typedef void (__cdecl *cgamePrint_t)( const char *text );
typedef void (__cdecl *cgameCvarRegister_t)( void *cvar, const char *name, const char *defaultValue, int flags );
typedef void (__cdecl *cgameCvarRegisterRange_t)( void *cvar, const char *name, const char *defaultValue,
	const char *minimumValue, const char *maximumValue, int flags );
typedef void (__cdecl *cgameCvarUpdate_t)( void *cvar );
typedef void (__cdecl *cgameCvarSetValue_t)( const char *name, float value );
typedef float (__cdecl *cgameCvarVariableValue_t)( const char *name );
typedef int (__cdecl *cgameArgc_t)( void );
typedef void (__cdecl *cgameArgv_t)( int index, char *buffer, int bufferLength );
typedef void (__cdecl *cgameAddCommand_t)( const char *name );
typedef int (__cdecl *cgameFsOpenFile_t)( const char *path, int *handle, int mode );
typedef void (__cdecl *cgameFsReadWrite_t)( void *buffer, int length, int handle );
typedef void (__cdecl *cgameFsCloseFile_t)( int handle );
typedef int (__cdecl *cgameFsSeek_t)( int handle, long offset, int origin );
typedef int (__cdecl *cgameClientMuted_t)( unsigned int identityLow, unsigned int identityHigh );
typedef void (__cdecl *cgameGetGlconfig_t)( void *config );
typedef void (__cdecl *cgameGetGameState_t)( void *gameState );
typedef void (__cdecl *cgameGetCurrentSnapshotNumber_t)( int *snapshotNumber, int *serverTime );
typedef int (__cdecl *cgameGetSnapshot_t)( int snapshotNumber, void *snapshot );
typedef int (__cdecl *cgameGetCurrentCmdNumber_t)( void );
typedef int (__cdecl *cgameGetUserCmd_t)( int commandNumber, void *command );
typedef void (__cdecl *cgameSetUserCmdValue_t)( int weapon, int weaponPrimary, float sensitivity, int fov );
typedef int (__cdecl *cgameIntVoid_t)( void );
typedef int (__cdecl *cgameIntInt_t)( int value );
typedef int (__cdecl *cgameIntString_t)( const char *value );
typedef void (__cdecl *cgameKeyBuffer_t)( int keynum, char *buffer, int bufferLength );
typedef int (__cdecl *cgamePcAddGlobalDefine_t)( char *define );
typedef int (__cdecl *cgamePcLoadSource_t)( const char *filename );
typedef int (__cdecl *cgamePcFreeSource_t)( int handle );
typedef int (__cdecl *cgamePcReadToken_t)( int handle, void *token );
typedef int (__cdecl *cgamePcSourceFileAndLine_t)( int handle, char *filename, int *line );

typedef struct {
	int serverTime;
	int angles[3];
	int buttons;
	unsigned char weapon;
	unsigned char weaponPrimary;
	unsigned char fov;
	signed char forwardmove;
	signed char rightmove;
	signed char upmove;
} probeUsercmd_t;

typedef struct {
	int handle;
	int modificationCount;
	float value;
	int integer;
	int flags;
	char string[256];
} probeVmCvar_t;

static void **cgameImports;
static int drewActiveFrame;
static int drawFrameCount;
static int usercmdChecked;
static int snapshotChecked;
static unsigned char gameState[65536];
static unsigned char snapshot[1024 * 1024];
static probeVmCvar_t rangeCvar;

static void Probe_Print( const char *text ) {
	if ( cgameImports && cgameImports[CG_QL_IMPORT_PRINT] ) {
		((cgamePrint_t)cgameImports[CG_QL_IMPORT_PRINT])( text );
	}
}

static int Probe_StringEquals( const char *left, const char *right ) {
	while ( *left && *left == *right ) {
		++left;
		++right;
	}

	return *left == *right;
}

static void __cdecl Probe_Init( int serverMessageNum, int serverCommandSequence, int clientNum ) {
	float value = 0.0f;
	int muteRoundTrip = 0;
	int cvarRangeImport = 0;
	unsigned char glconfig[0x2c44];
	int glconfigImport = 0;
	int gameStateImport = 0;
	int filesystemImport = 0;
	int parserImport = 0;
	int fileHandle = 0;
	int fileLength;
	char fileContents[5] = { 0, 0, 0, 0, 0 };
	const char fileProbeText[] = "fnql";
	char parserDefine[] = "FNQL_CGAME_PROBE 1";
	const char parserText[] = "FNQL_CGAME_PROBE\n";
	unsigned char parserToken[1040];
	char parserFilename[64];
	int parserLine = -1;
	int keyImport = 0;
	char keyName[64];
	char keyBinding[64];
	int index;
	(void)serverMessageNum;
	(void)serverCommandSequence;
	(void)clientNum;

	if ( cgameImports && cgameImports[CG_QL_IMPORT_CVAR_VARIABLEVALUE] ) {
		value = ((cgameCvarVariableValue_t)cgameImports[CG_QL_IMPORT_CVAR_VARIABLEVALUE])(
			"fnql_cgame_probe_float" );
	}

	if ( value > 13.49f && value < 13.51f ) {
		Probe_Print( "FnQL cgame ABI probe: native float import ok\n" );
	} else {
		Probe_Print( "FnQL cgame ABI probe: native float import failed\n" );
	}

	if ( cgameImports && cgameImports[CG_QL_IMPORT_CVAR_REGISTER] &&
		cgameImports[CG_QL_IMPORT_CVAR_REGISTER_RANGE] && cgameImports[CG_QL_IMPORT_CVAR_UPDATE] &&
		cgameImports[CG_QL_IMPORT_CVAR_SET_VALUE] && cgameImports[CG_QL_IMPORT_CVAR_VARIABLEVALUE] ) {
		((cgameCvarRegisterRange_t)cgameImports[CG_QL_IMPORT_CVAR_REGISTER_RANGE])(
			&rangeCvar, "fnql_cgame_probe_range", "0.75", "0.25", "1.50", 0 );
		((cgameCvarSetValue_t)cgameImports[CG_QL_IMPORT_CVAR_SET_VALUE])(
			"fnql_cgame_probe_range", 2.0f );
		((cgameCvarUpdate_t)cgameImports[CG_QL_IMPORT_CVAR_UPDATE])( &rangeCvar );
		value = ((cgameCvarVariableValue_t)cgameImports[CG_QL_IMPORT_CVAR_VARIABLEVALUE])(
			"fnql_cgame_probe_range" );
		cvarRangeImport = value > 1.49f && value < 1.51f &&
			rangeCvar.value > 1.49f && rangeCvar.value < 1.51f && rangeCvar.string[0] == '1';
	}
	Probe_Print( cvarRangeImport
		? "FnQL cgame ABI probe: native cvar range imports ok\n"
		: "FnQL cgame ABI probe: native cvar range imports failed\n" );

	if ( cgameImports && cgameImports[CG_QL_IMPORT_FS_FOPENFILE] &&
		cgameImports[CG_QL_IMPORT_FS_READ] && cgameImports[CG_QL_IMPORT_FS_WRITE] &&
		cgameImports[CG_QL_IMPORT_FS_FCLOSEFILE] && cgameImports[CG_QL_IMPORT_FS_SEEK] ) {
		cgameFsOpenFile_t openFile = (cgameFsOpenFile_t)cgameImports[CG_QL_IMPORT_FS_FOPENFILE];
		cgameFsReadWrite_t readWrite = (cgameFsReadWrite_t)cgameImports[CG_QL_IMPORT_FS_READ];
		cgameFsCloseFile_t closeFile = (cgameFsCloseFile_t)cgameImports[CG_QL_IMPORT_FS_FCLOSEFILE];
		cgameFsSeek_t seekFile = (cgameFsSeek_t)cgameImports[CG_QL_IMPORT_FS_SEEK];

		(void)openFile( "fnql_cgame_probe_io.txt", &fileHandle, 1 );
		if ( fileHandle > 0 ) {
			readWrite = (cgameFsReadWrite_t)cgameImports[CG_QL_IMPORT_FS_WRITE];
			readWrite( (void *)fileProbeText, 4, fileHandle );
			closeFile( fileHandle );
			fileHandle = 0;
			fileLength = openFile( "fnql_cgame_probe_io.txt", &fileHandle, 0 );
			if ( fileLength == 4 && fileHandle > 0 && seekFile( fileHandle, 0, 2 ) == 0 ) {
				readWrite = (cgameFsReadWrite_t)cgameImports[CG_QL_IMPORT_FS_READ];
				readWrite( fileContents, 4, fileHandle );
				filesystemImport = fileContents[0] == 'f' && fileContents[1] == 'n' &&
					fileContents[2] == 'q' && fileContents[3] == 'l';
			}
			if ( fileHandle > 0 ) {
				closeFile( fileHandle );
			}
		}
	}
	Probe_Print( filesystemImport
		? "FnQL cgame ABI probe: native filesystem imports ok\n"
		: "FnQL cgame ABI probe: native filesystem imports failed\n" );

	for ( index = 0; index < (int)sizeof( parserToken ); ++index ) {
		parserToken[index] = 0xa5;
	}
	for ( index = 0; index < (int)sizeof( parserFilename ); ++index ) {
		parserFilename[index] = (char)0xa5;
	}
	if ( cgameImports && cgameImports[CG_QL_IMPORT_FS_FOPENFILE] &&
		cgameImports[CG_QL_IMPORT_FS_WRITE] && cgameImports[CG_QL_IMPORT_FS_FCLOSEFILE] &&
		cgameImports[CG_QL_IMPORT_PC_ADD_GLOBAL_DEFINE] && cgameImports[CG_QL_IMPORT_PC_LOAD_SOURCE] &&
		cgameImports[CG_QL_IMPORT_PC_FREE_SOURCE] && cgameImports[CG_QL_IMPORT_PC_READ_TOKEN] &&
		cgameImports[CG_QL_IMPORT_PC_SOURCE_FILE_AND_LINE] ) {
		cgameFsOpenFile_t openFile = (cgameFsOpenFile_t)cgameImports[CG_QL_IMPORT_FS_FOPENFILE];
		cgameFsReadWrite_t writeFile = (cgameFsReadWrite_t)cgameImports[CG_QL_IMPORT_FS_WRITE];
		cgameFsCloseFile_t closeFile = (cgameFsCloseFile_t)cgameImports[CG_QL_IMPORT_FS_FCLOSEFILE];
		int parserHandle;
		int globalDefineResult;

		(void)openFile( "fnql_cgame_probe_parser.cfg", &fileHandle, 1 );
		if ( fileHandle > 0 ) {
			writeFile( (void *)parserText, sizeof( parserText ) - 1, fileHandle );
			closeFile( fileHandle );
			fileHandle = 0;
			globalDefineResult = ((cgamePcAddGlobalDefine_t)cgameImports[CG_QL_IMPORT_PC_ADD_GLOBAL_DEFINE])(
				parserDefine );
			parserHandle = ((cgamePcLoadSource_t)cgameImports[CG_QL_IMPORT_PC_LOAD_SOURCE])(
				"fnql_cgame_probe_parser.cfg" );
			if ( parserHandle > 0 ) {
				parserImport = globalDefineResult && ((cgamePcReadToken_t)cgameImports[CG_QL_IMPORT_PC_READ_TOKEN])(
					parserHandle, parserToken ) &&
					((cgamePcSourceFileAndLine_t)cgameImports[CG_QL_IMPORT_PC_SOURCE_FILE_AND_LINE])(
						parserHandle, parserFilename, &parserLine ) &&
					parserToken[0] != 0xa5 && (unsigned char)parserFilename[0] != 0xa5 && parserLine >= 1;
				(void)((cgamePcFreeSource_t)cgameImports[CG_QL_IMPORT_PC_FREE_SOURCE])( parserHandle );
			}
		}
	}
	Probe_Print( parserImport
		? "FnQL cgame ABI probe: native parser imports ok\n"
		: "FnQL cgame ABI probe: native parser imports failed\n" );

	if ( cgameImports && cgameImports[CG_QL_IMPORT_IS_CLIENT_MUTED] &&
		cgameImports[CG_QL_IMPORT_TOGGLE_CLIENT_MUTE] ) {
		cgameClientMuted_t isMuted = (cgameClientMuted_t)cgameImports[CG_QL_IMPORT_IS_CLIENT_MUTED];
		cgameClientMuted_t toggleMuted = (cgameClientMuted_t)cgameImports[CG_QL_IMPORT_TOGGLE_CLIENT_MUTE];
		const unsigned int identityLow = 0x464e514cU;
		const unsigned int identityHigh = 0x50524f42U;

		muteRoundTrip = !isMuted( identityLow, identityHigh ) &&
			toggleMuted( identityLow, identityHigh ) &&
			isMuted( identityLow, identityHigh ) &&
			!toggleMuted( identityLow, identityHigh ) &&
			!isMuted( identityLow, identityHigh );
	}

	Probe_Print( muteRoundTrip
		? "FnQL cgame ABI probe: native mute imports ok\n"
		: "FnQL cgame ABI probe: native mute imports failed\n" );

	for ( index = 0; index < (int)sizeof( glconfig ); ++index ) {
		glconfig[index] = 0xa5;
	}
	if ( cgameImports && cgameImports[CG_QL_IMPORT_GETGLCONFIG] ) {
		((cgameGetGlconfig_t)cgameImports[CG_QL_IMPORT_GETGLCONFIG])( glconfig );
		glconfigImport = glconfig[0] != 0xa5;
	}
	Probe_Print( glconfigImport
		? "FnQL cgame ABI probe: native glconfig import ok\n"
		: "FnQL cgame ABI probe: native glconfig import failed\n" );

	for ( index = 0; index < (int)sizeof( gameState ); ++index ) {
		gameState[index] = 0xa5;
	}
	if ( cgameImports && cgameImports[CG_QL_IMPORT_GETGAMESTATE] ) {
		((cgameGetGameState_t)cgameImports[CG_QL_IMPORT_GETGAMESTATE])( gameState );
		gameStateImport = gameState[0] != 0xa5;
	}
	Probe_Print( gameStateImport
		? "FnQL cgame ABI probe: native gamestate import ok\n"
		: "FnQL cgame ABI probe: native gamestate import failed\n" );

	for ( index = 0; index < (int)sizeof( keyName ); ++index ) {
		keyName[index] = (char)0xa5;
		keyBinding[index] = (char)0xa5;
	}
	if ( cgameImports && cgameImports[CG_QL_IMPORT_MEMORY_REMAINING] &&
		cgameImports[CG_QL_IMPORT_KEY_ISDOWN] && cgameImports[CG_QL_IMPORT_KEY_GETCATCHER] &&
		cgameImports[CG_QL_IMPORT_KEY_GETKEY] && cgameImports[CG_QL_IMPORT_KEY_KEYNUMTOSTRINGBUF] &&
		cgameImports[CG_QL_IMPORT_KEY_GETBINDINGBUF] ) {
		const int memoryRemaining = ((cgameIntVoid_t)cgameImports[CG_QL_IMPORT_MEMORY_REMAINING])();

		(void)((cgameIntInt_t)cgameImports[CG_QL_IMPORT_KEY_ISDOWN])( 0 );
		(void)((cgameIntVoid_t)cgameImports[CG_QL_IMPORT_KEY_GETCATCHER])();
		(void)((cgameIntString_t)cgameImports[CG_QL_IMPORT_KEY_GETKEY])( "fnql_cgame_probe_unbound" );
		((cgameKeyBuffer_t)cgameImports[CG_QL_IMPORT_KEY_KEYNUMTOSTRINGBUF])( 0, keyName, sizeof( keyName ) );
		((cgameKeyBuffer_t)cgameImports[CG_QL_IMPORT_KEY_GETBINDINGBUF])( 0, keyBinding, sizeof( keyBinding ) );
		keyImport = memoryRemaining > 0 && (unsigned char)keyName[0] != 0xa5 &&
			(unsigned char)keyBinding[0] != 0xa5;
	}
	Probe_Print( keyImport
		? "FnQL cgame ABI probe: native key imports ok\n"
		: "FnQL cgame ABI probe: native key imports failed\n" );

	if ( cgameImports && cgameImports[CG_QL_IMPORT_SETUSERCMDVALUE] ) {
		((cgameSetUserCmdValue_t)cgameImports[CG_QL_IMPORT_SETUSERCMDVALUE])( 7, 8, 1.25f, 110 );
	}
}

static void __cdecl Probe_RegisterCvars( void ) {
	if ( cgameImports && cgameImports[CG_QL_IMPORT_ADDCOMMAND] ) {
		((cgameAddCommand_t)cgameImports[CG_QL_IMPORT_ADDCOMMAND])( "fnql_cgame_probe_command" );
	}
}

static void __cdecl Probe_Shutdown( void ) {
}

static int __cdecl Probe_ConsoleCommand( void ) {
	char command[64] = { 0 };

	if ( cgameImports && cgameImports[CG_QL_IMPORT_ARGC] && cgameImports[CG_QL_IMPORT_ARGV] &&
		((cgameArgc_t)cgameImports[CG_QL_IMPORT_ARGC])() > 0 ) {
		((cgameArgv_t)cgameImports[CG_QL_IMPORT_ARGV])( 0, command, sizeof( command ) );
		if ( Probe_StringEquals( command, "fnql_cgame_probe_command" ) ) {
			Probe_Print( "FnQL cgame ABI probe: native console command dispatch ok\n" );
			return 1;
		}
	}

	return 0;
}

static void __cdecl Probe_DrawActiveFrame( int serverTime, int stereoFrame, int demoPlayback ) {
	probeUsercmd_t command;
	int snapshotNumber = 0;
	int snapshotServerTime = 0;
	(void)serverTime;
	(void)stereoFrame;
	(void)demoPlayback;

	if ( !drewActiveFrame ) {
		drewActiveFrame = 1;
		Probe_Print( "FnQL cgame ABI probe: native draw dispatch ok\n" );
	}

	++drawFrameCount;
	if ( !usercmdChecked && cgameImports &&
		cgameImports[CG_QL_IMPORT_GETCURRENTCMDNUMBER] && cgameImports[CG_QL_IMPORT_GETUSERCMD] ) {
		int commandNumber = ((cgameGetCurrentCmdNumber_t)cgameImports[CG_QL_IMPORT_GETCURRENTCMDNUMBER])();
		if ( commandNumber > 0 &&
			((cgameGetUserCmd_t)cgameImports[CG_QL_IMPORT_GETUSERCMD])( commandNumber, &command ) &&
			command.weapon == 7 && command.weaponPrimary == 8 && command.fov == 110 ) {
			usercmdChecked = 1;
			Probe_Print( "FnQL cgame ABI probe: native usercmd imports ok\n" );
		}
	}

	if ( !usercmdChecked && drawFrameCount == 30 ) {
		usercmdChecked = 1;
		Probe_Print( "FnQL cgame ABI probe: native usercmd imports failed\n" );
	}

	if ( !snapshotChecked && cgameImports &&
		cgameImports[CG_QL_IMPORT_GETCURRENTSNAPSHOTNUMBER] && cgameImports[CG_QL_IMPORT_GETSNAPSHOT] ) {
		snapshot[0] = 0xa5;
		snapshot[1] = 0xa5;
		snapshot[2] = 0xa5;
		snapshot[3] = 0xa5;
		((cgameGetCurrentSnapshotNumber_t)cgameImports[CG_QL_IMPORT_GETCURRENTSNAPSHOTNUMBER])(
			&snapshotNumber, &snapshotServerTime );
		if ( snapshotNumber > 0 &&
			((cgameGetSnapshot_t)cgameImports[CG_QL_IMPORT_GETSNAPSHOT])( snapshotNumber, snapshot ) &&
			(snapshot[0] != 0xa5 || snapshot[1] != 0xa5 || snapshot[2] != 0xa5 || snapshot[3] != 0xa5) ) {
			snapshotChecked = 1;
			Probe_Print( "FnQL cgame ABI probe: native snapshot imports ok\n" );
		}
	}

	if ( !snapshotChecked && drawFrameCount == 30 ) {
		snapshotChecked = 1;
		Probe_Print( "FnQL cgame ABI probe: native snapshot imports failed\n" );
	}
}

static int __cdecl Probe_IntResult( void ) {
	return 0;
}

static void __cdecl Probe_KeyEvent( int key, int down ) {
	(void)key;
	(void)down;
}

static void __cdecl Probe_MouseEvent( int dx, int dy ) {
	(void)dx;
	(void)dy;
}

static void __cdecl Probe_EventHandling( int type ) {
	(void)type;
}

static int __cdecl Probe_CopyClientIdentity( int clientNum, void *identity ) {
	(void)clientNum;
	(void)identity;
	return 0;
}

static int __cdecl Probe_SetClientSpeakingState( int clientNum, int speaking ) {
	(void)clientNum;
	(void)speaking;
	return 0;
}

static void *cgameExports[CG_NATIVE_EXPORT_COUNT] = {
	(void *)Probe_Init,
	(void *)Probe_RegisterCvars,
	(void *)Probe_Shutdown,
	(void *)Probe_ConsoleCommand,
	(void *)Probe_DrawActiveFrame,
	(void *)Probe_IntResult,
	(void *)Probe_IntResult,
	(void *)Probe_KeyEvent,
	(void *)Probe_MouseEvent,
	(void *)Probe_EventHandling,
	(void *)Probe_Shutdown,
	(void *)Probe_Shutdown,
	(void *)Probe_Shutdown,
	(void *)Probe_Shutdown,
	(void *)Probe_IntResult,
	(void *)Probe_CopyClientIdentity,
	0,
	(void *)Probe_IntResult,
	(void *)Probe_IntResult,
	(void *)Probe_IntResult,
	(void *)Probe_SetClientSpeakingState
};

__declspec( dllexport ) void __cdecl dllEntry( void ***exports, void *imports, int *apiVersion ) {
	cgameImports = (void **)imports;
	*exports = cgameExports;
	*apiVersion = CGAME_NATIVE_API_VERSION;
}
