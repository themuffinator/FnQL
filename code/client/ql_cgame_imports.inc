// Auto-generated cgame import wrappers for Quake Live native DLLs.
// Generated from cg_syscalls.c; do not edit manually.

/*
==============
QL_CG_PASSFLOAT
==============
*/
static int QDECL QL_CG_PASSFLOAT( float x ) {
	float	floatTemp;
	floatTemp = x;
	return *(int *)&floatTemp;
}

/*
==============
QL_CG_trap_Print
==============
*/
static void QDECL QL_CG_trap_Print( const char *fmt ) {
	CG_Import_Syscall( CG_PRINT, fmt );
}

/*
==============
QL_CG_trap_Error
==============
*/
static void QDECL QL_CG_trap_Error( const char *fmt ) {
	CG_Import_Syscall( CG_ERROR, fmt );
}

/*
==============
QL_CG_trap_Milliseconds
==============
*/
static int QDECL QL_CG_trap_Milliseconds( void ) {
	return CG_Import_Syscall( CG_MILLISECONDS );
}

/*
==============
QL_CG_trap_Cvar_Register
==============
*/
static void QDECL QL_CG_trap_Cvar_Register( vmCvar_t *vmCvar, const char *varName, const char *defaultValue, int flags ) {
	CG_Import_Syscall( CG_CVAR_REGISTER, vmCvar, varName, defaultValue, flags );
}

/*
==============
QL_CG_trap_Cvar_Update
==============
*/
static void QDECL QL_CG_trap_Cvar_Update( vmCvar_t *vmCvar ) {
	CG_Import_Syscall( CG_CVAR_UPDATE, vmCvar );
}

/*
==============
QL_CG_trap_Cvar_Set
==============
*/
static void QDECL QL_CG_trap_Cvar_Set( const char *var_name, const char *value ) {
	CG_Import_Syscall( CG_CVAR_SET, var_name, value );
}

/*
==============
QL_CG_trap_Cvar_VariableStringBuffer
==============
*/
static void QDECL QL_CG_trap_Cvar_VariableStringBuffer( const char *var_name, char *buffer, int bufsize ) {
	CG_Import_Syscall( CG_CVAR_VARIABLESTRINGBUFFER, var_name, buffer, bufsize );
}

/*
==============
QL_CG_trap_Argc
==============
*/
static int QDECL QL_CG_trap_Argc( void ) {
	return CG_Import_Syscall( CG_ARGC );
}

/*
==============
QL_CG_trap_Argv
==============
*/
static void QDECL QL_CG_trap_Argv( int n, char *buffer, int bufferLength ) {
	CG_Import_Syscall( CG_ARGV, n, buffer, bufferLength );
}

/*
==============
QL_CG_trap_Args
==============
*/
static void QDECL QL_CG_trap_Args( char *buffer, int bufferLength ) {
	CG_Import_Syscall( CG_ARGS, buffer, bufferLength );
}

/*
==============
QL_CG_trap_FS_FOpenFile
==============
*/
static int QDECL QL_CG_trap_FS_FOpenFile( const char *qpath, fileHandle_t *f, fsMode_t mode ) {
	return CG_Import_Syscall( CG_FS_FOPENFILE, qpath, f, mode );
}

/*
==============
QL_CG_trap_FS_Read
==============
*/
static void QDECL QL_CG_trap_FS_Read( void *buffer, int len, fileHandle_t f ) {
	CG_Import_Syscall( CG_FS_READ, buffer, len, f );
}

/*
==============
QL_CG_trap_FS_Write
==============
*/
static void QDECL QL_CG_trap_FS_Write( const void *buffer, int len, fileHandle_t f ) {
	CG_Import_Syscall( CG_FS_WRITE, buffer, len, f );
}

/*
==============
QL_CG_trap_FS_FCloseFile
==============
*/
static void QDECL QL_CG_trap_FS_FCloseFile( fileHandle_t f ) {
	CG_Import_Syscall( CG_FS_FCLOSEFILE, f );
}

/*
==============
QL_CG_trap_FS_Seek
==============
*/
static int QDECL QL_CG_trap_FS_Seek( fileHandle_t f, long offset, int origin ) {
	return CG_Import_Syscall( CG_FS_SEEK, f, offset, origin );
}

/*
==============
QL_CG_trap_SendConsoleCommand
==============
*/
static void QDECL QL_CG_trap_SendConsoleCommand( const char *text ) {
	CG_Import_Syscall( CG_SENDCONSOLECOMMAND, text );
}

/*
==============
QL_CG_trap_AddCommand
==============
*/
static void QDECL QL_CG_trap_AddCommand( const char *cmdName ) {
	CG_Import_Syscall( CG_ADDCOMMAND, cmdName );
}

/*
==============
QL_CG_trap_RemoveCommand
==============
*/
static void QDECL QL_CG_trap_RemoveCommand( const char *cmdName ) {
	CG_Import_Syscall( CG_REMOVECOMMAND, cmdName );
}

/*
==============
QL_CG_trap_SendClientCommand
==============
*/
static void QDECL QL_CG_trap_SendClientCommand( const char *s ) {
	CG_Import_Syscall( CG_SENDCLIENTCOMMAND, s );
}

/*
==============
QL_CG_trap_UpdateScreen
==============
*/
static void QDECL QL_CG_trap_UpdateScreen( void ) {
	CG_Import_Syscall( CG_UPDATESCREEN );
}

/*
==============
QL_CG_trap_CM_LoadMap
==============
*/
static void QDECL QL_CG_trap_CM_LoadMap( const char *mapname ) {
	CG_Import_Syscall( CG_CM_LOADMAP, mapname );
}

/*
==============
QL_CG_trap_CM_NumInlineModels
==============
*/
static int QDECL QL_CG_trap_CM_NumInlineModels( void ) {
	return CG_Import_Syscall( CG_CM_NUMINLINEMODELS );
}

/*
==============
QL_CG_trap_CM_InlineModel
==============
*/
static clipHandle_t QDECL QL_CG_trap_CM_InlineModel( int index ) {
	return CG_Import_Syscall( CG_CM_INLINEMODEL, index );
}

/*
==============
QL_CG_trap_CM_TempBoxModel
==============
*/
static clipHandle_t QDECL QL_CG_trap_CM_TempBoxModel( const vec3_t mins, const vec3_t maxs ) {
	return CG_Import_Syscall( CG_CM_TEMPBOXMODEL, mins, maxs );
}

/*
==============
QL_CG_trap_CM_TempCapsuleModel
==============
*/
static clipHandle_t QDECL QL_CG_trap_CM_TempCapsuleModel( const vec3_t mins, const vec3_t maxs ) {
	return CG_Import_Syscall( CG_CM_TEMPCAPSULEMODEL, mins, maxs );
}

/*
==============
QL_CG_trap_CM_PointContents
==============
*/
static int QDECL QL_CG_trap_CM_PointContents( const vec3_t p, clipHandle_t model ) {
	return CG_Import_Syscall( CG_CM_POINTCONTENTS, p, model );
}

/*
==============
QL_CG_trap_CM_TransformedPointContents
==============
*/
static int QDECL QL_CG_trap_CM_TransformedPointContents( const vec3_t p, clipHandle_t model, const vec3_t origin, const vec3_t angles ) {
	return CG_Import_Syscall( CG_CM_TRANSFORMEDPOINTCONTENTS, p, model, origin, angles );
}

/*
==============
QL_CG_trap_CM_BoxTrace
==============
*/
static void QDECL QL_CG_trap_CM_BoxTrace( trace_t *results, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs, clipHandle_t model, int brushmask ) {
	CG_Import_Syscall( CG_CM_BOXTRACE, results, start, end, mins, maxs, model, brushmask );
}

/*
==============
QL_CG_trap_CM_CapsuleTrace
==============
*/
static void QDECL QL_CG_trap_CM_CapsuleTrace( trace_t *results, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs, clipHandle_t model, int brushmask ) {
	CG_Import_Syscall( CG_CM_CAPSULETRACE, results, start, end, mins, maxs, model, brushmask );
}

/*
==============
QL_CG_trap_CM_TransformedBoxTrace
==============
*/
static void QDECL QL_CG_trap_CM_TransformedBoxTrace( trace_t *results, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs, clipHandle_t model, int brushmask, const vec3_t origin, const vec3_t angles ) {
	CG_Import_Syscall( CG_CM_TRANSFORMEDBOXTRACE, results, start, end, mins, maxs, model, brushmask, origin, angles );
}

/*
==============
QL_CG_trap_CM_TransformedCapsuleTrace
==============
*/
static void QDECL QL_CG_trap_CM_TransformedCapsuleTrace( trace_t *results, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs, clipHandle_t model, int brushmask, const vec3_t origin, const vec3_t angles ) {
	CG_Import_Syscall( CG_CM_TRANSFORMEDCAPSULETRACE, results, start, end, mins, maxs, model, brushmask, origin, angles );
}

/*
==============
QL_CG_trap_CM_MarkFragments
==============
*/
static int QDECL QL_CG_trap_CM_MarkFragments( int numPoints, const vec3_t *points, const vec3_t projection, int maxPoints, vec3_t pointBuffer, int maxFragments, markFragment_t *fragmentBuffer ) {
	return CG_Import_Syscall( CG_CM_MARKFRAGMENTS, numPoints, points, projection, maxPoints, pointBuffer, maxFragments, fragmentBuffer );
}

/*
==============
QL_CG_trap_S_StartSound
==============
*/
static void QDECL QL_CG_trap_S_StartSound( vec3_t origin, int entityNum, int entchannel, sfxHandle_t sfx ) {
	CG_Import_Syscall( CG_S_STARTSOUND, origin, entityNum, entchannel, sfx );
}

/*
==============
QL_CG_trap_S_StartLocalSound
==============
*/
static void QDECL QL_CG_trap_S_StartLocalSound( sfxHandle_t sfx, int channelNum ) {
	CG_Import_Syscall( CG_S_STARTLOCALSOUND, sfx, channelNum );
}

/*
==============
QL_CG_trap_S_ClearLoopingSounds
==============
*/
static UNUSED_VAR void QDECL QL_CG_trap_S_ClearLoopingSounds( qboolean killall ) {
	CG_Import_Syscall( CG_S_CLEARLOOPINGSOUNDS, killall );
}

/*
==============
QL_CG_trap_S_AddLoopingSound
==============
*/
static void QDECL QL_CG_trap_S_AddLoopingSound( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx ) {
	CG_Import_Syscall( CG_S_ADDLOOPINGSOUND, entityNum, origin, velocity, sfx );
}

/*
==============
QL_CG_trap_S_AddRealLoopingSound
==============
*/
static void QDECL QL_CG_trap_S_AddRealLoopingSound( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx ) {
	CG_Import_Syscall( CG_S_ADDREALLOOPINGSOUND, entityNum, origin, velocity, sfx );
}

/*
==============
QL_CG_trap_S_StopLoopingSound
==============
*/
static void QDECL QL_CG_trap_S_StopLoopingSound( int entityNum ) {
	CG_Import_Syscall( CG_S_STOPLOOPINGSOUND, entityNum );
}

/*
==============
QL_CG_trap_S_UpdateEntityPosition
==============
*/
static void QDECL QL_CG_trap_S_UpdateEntityPosition( int entityNum, const vec3_t origin ) {
	CG_Import_Syscall( CG_S_UPDATEENTITYPOSITION, entityNum, origin );
}

/*
==============
QL_CG_trap_S_Respatialize
==============
*/
static void QDECL QL_CG_trap_S_Respatialize( int entityNum, const vec3_t origin, vec3_t axis[3], int inwater ) {
	CG_Import_Syscall( CG_S_RESPATIALIZE, entityNum, origin, axis, inwater );
}

/*
==============
QL_CG_trap_S_RegisterSound
==============
*/
static sfxHandle_t QDECL QL_CG_trap_S_RegisterSound( const char *sample, qboolean compressed ) {
	return CG_Import_Syscall( CG_S_REGISTERSOUND, sample, compressed );
}

/*
==============
QL_CG_trap_S_StartBackgroundTrack
==============
*/
static void QDECL QL_CG_trap_S_StartBackgroundTrack( const char *intro, const char *loop ) {
	CG_Import_Syscall( CG_S_STARTBACKGROUNDTRACK, intro, loop );
}

/*
==============
QL_CG_trap_R_LoadWorldMap
==============
*/
static void QDECL QL_CG_trap_R_LoadWorldMap( const char *mapname ) {
	CG_Import_Syscall( CG_R_LOADWORLDMAP, mapname );
}

/*
==============
QL_CG_trap_R_RegisterModel
==============
*/
static qhandle_t QDECL QL_CG_trap_R_RegisterModel( const char *name ) {
	return CG_Import_Syscall( CG_R_REGISTERMODEL, name );
}

/*
==============
QL_CG_trap_R_RegisterSkin
==============
*/
static qhandle_t QDECL QL_CG_trap_R_RegisterSkin( const char *name ) {
	return CG_Import_Syscall( CG_R_REGISTERSKIN, name );
}

/*
==============
QL_CG_trap_R_RegisterShader
==============
*/
static qhandle_t QDECL QL_CG_trap_R_RegisterShader( const char *name ) {
	return CG_Import_Syscall( CG_R_REGISTERSHADER, name );
}

/*
==============
QL_CG_trap_R_RegisterShaderNoMip
==============
*/
static qhandle_t QDECL QL_CG_trap_R_RegisterShaderNoMip( const char *name ) {
	return CG_Import_Syscall( CG_R_REGISTERSHADERNOMIP, name );
}

/*
==============
QL_CG_trap_R_RegisterFont
==============
*/
static void QDECL QL_CG_trap_R_RegisterFont( const char *fontName, int pointSize, fontInfo_t *font ) {
	CG_Import_Syscall(CG_R_REGISTERFONT, fontName, pointSize, font );
}

/*
==============
QL_CG_trap_R_ClearScene
==============
*/
static void QDECL QL_CG_trap_R_ClearScene( void ) {
	CG_Import_Syscall( CG_R_CLEARSCENE );
}

/*
==============
QL_CG_trap_R_AddRefEntityToScene
==============
*/
static void QDECL QL_CG_trap_R_AddRefEntityToScene( const refEntity_t *re ) {
	CG_Import_Syscall( CG_R_ADDREFENTITYTOSCENE, re );
}

/*
==============
QL_CG_trap_R_AddPolyToScene
==============
*/
static void QDECL QL_CG_trap_R_AddPolyToScene( qhandle_t hShader , int numVerts, const polyVert_t *verts ) {
	CG_Import_Syscall( CG_R_ADDPOLYTOSCENE, hShader, numVerts, verts );
}

/*
==============
QL_CG_trap_R_AddPolysToScene
==============
*/
static void QDECL QL_CG_trap_R_AddPolysToScene( qhandle_t hShader , int numVerts, const polyVert_t *verts, int num ) {
	CG_Import_Syscall( CG_R_ADDPOLYSTOSCENE, hShader, numVerts, verts, num );
}

/*
==============
QL_CG_trap_R_LightForPoint
==============
*/
static int QDECL QL_CG_trap_R_LightForPoint( vec3_t point, vec3_t ambientLight, vec3_t directedLight, vec3_t lightDir ) {
	return CG_Import_Syscall( CG_R_LIGHTFORPOINT, point, ambientLight, directedLight, lightDir );
}

/*
==============
QL_CG_trap_R_AddLightToScene
==============
*/
static void QDECL QL_CG_trap_R_AddLightToScene( const vec3_t org, float intensity, float r, float g, float b ) {
	CG_Import_Syscall( CG_R_ADDLIGHTTOSCENE, org, QL_CG_PASSFLOAT(intensity), QL_CG_PASSFLOAT(r), QL_CG_PASSFLOAT(g), QL_CG_PASSFLOAT(b) );
}

/*
==============
QL_CG_trap_R_AddAdditiveLightToScene
==============
*/
static void QDECL QL_CG_trap_R_AddAdditiveLightToScene( const vec3_t org, float intensity, float r, float g, float b ) {
	CG_Import_Syscall( CG_R_ADDADDITIVELIGHTTOSCENE, org, QL_CG_PASSFLOAT(intensity), QL_CG_PASSFLOAT(r), QL_CG_PASSFLOAT(g), QL_CG_PASSFLOAT(b) );
}

/*
==============
QL_CG_trap_R_RenderScene
==============
*/
static void QDECL QL_CG_trap_R_RenderScene( const refdef_t *fd ) {
	CG_Import_Syscall( CG_R_RENDERSCENE, fd );
}

/*
==============
QL_CG_trap_R_SetColor
==============
*/
static UNUSED_VAR void QDECL QL_CG_trap_R_SetColor( const float *rgba ) {
	CG_Import_Syscall( CG_R_SETCOLOR, rgba );
}

/*
==============
QL_CG_trap_R_DrawStretchPic
==============
*/
static void QDECL QL_CG_trap_R_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t hShader ) {
	CG_Import_Syscall( CG_R_DRAWSTRETCHPIC, QL_CG_PASSFLOAT(x), QL_CG_PASSFLOAT(y), QL_CG_PASSFLOAT(w), QL_CG_PASSFLOAT(h), QL_CG_PASSFLOAT(s1), QL_CG_PASSFLOAT(t1), QL_CG_PASSFLOAT(s2), QL_CG_PASSFLOAT(t2), hShader );
}

/*
==============
QL_CG_trap_R_ModelBounds
==============
*/
static void QDECL QL_CG_trap_R_ModelBounds( clipHandle_t model, vec3_t mins, vec3_t maxs ) {
	CG_Import_Syscall( CG_R_MODELBOUNDS, model, mins, maxs );
}

/*
==============
QL_CG_trap_R_LerpTag
==============
*/
static int QDECL QL_CG_trap_R_LerpTag( orientation_t *tag, clipHandle_t mod, int startFrame, int endFrame, float frac, const char *tagName ) {
	return CG_Import_Syscall( CG_R_LERPTAG, tag, mod, startFrame, endFrame, QL_CG_PASSFLOAT(frac), tagName );
}

/*
==============
QL_CG_trap_R_RemapShader
==============
*/
static void QDECL QL_CG_trap_R_RemapShader( const char *oldShader, const char *newShader, const char *timeOffset ) {
	CG_Import_Syscall( CG_R_REMAP_SHADER, oldShader, newShader, timeOffset );
}

/*
==============
QL_CG_trap_GetGlconfig
==============
*/
static void QDECL QL_CG_trap_GetGlconfig( glconfig_t *glconfig ) {
	CG_Import_Syscall( CG_GETGLCONFIG, glconfig );
}

/*
==============
QL_CG_trap_GetGameState
==============
*/
static void QDECL QL_CG_trap_GetGameState( gameState_t *gamestate ) {
	CG_Import_Syscall( CG_GETGAMESTATE, gamestate );
}

/*
==============
QL_CG_trap_GetCurrentSnapshotNumber
==============
*/
static void QDECL QL_CG_trap_GetCurrentSnapshotNumber( int *snapshotNumber, int *serverTime ) {
	CG_Import_Syscall( CG_GETCURRENTSNAPSHOTNUMBER, snapshotNumber, serverTime );
}

/*
==============
QL_CG_trap_GetSnapshot
==============
*/
static qboolean QDECL QL_CG_trap_GetSnapshot( int snapshotNumber, snapshot_t *snapshot ) {
	return CG_Import_Syscall( CG_GETSNAPSHOT, snapshotNumber, snapshot ) ? qtrue : qfalse;
}

/*
==============
QL_CG_trap_GetServerCommand
==============
*/
static qboolean QDECL QL_CG_trap_GetServerCommand( int serverCommandNumber ) {
	return CG_Import_Syscall( CG_GETSERVERCOMMAND, serverCommandNumber ) ? qtrue : qfalse;
}

/*
==============
QL_CG_trap_GetCurrentCmdNumber
==============
*/
static int QDECL QL_CG_trap_GetCurrentCmdNumber( void ) {
	return CG_Import_Syscall( CG_GETCURRENTCMDNUMBER );
}

/*
==============
QL_CG_trap_GetUserCmd
==============
*/
static qboolean QDECL QL_CG_trap_GetUserCmd( int cmdNumber, usercmd_t *ucmd ) {
	return CG_Import_Syscall( CG_GETUSERCMD, cmdNumber, ucmd ) ? qtrue : qfalse;
}

/*
==============
QL_CG_trap_SetUserCmdValue
==============
*/
static void QDECL QL_CG_trap_SetUserCmdValue( int stateValue, int primaryValue, float sensitivityScale, int fov ) {
	CG_Import_Syscall( CG_SETUSERCMDVALUE, stateValue, primaryValue, QL_CG_PASSFLOAT(sensitivityScale), fov );
}

/*
==============
QL_CG_trap_MemoryRemaining
==============
*/
static int QDECL QL_CG_trap_MemoryRemaining( void ) {
	return CG_Import_Syscall( CG_MEMORY_REMAINING );
}

/*
==============
QL_CG_trap_Key_IsDown
==============
*/
static qboolean QDECL QL_CG_trap_Key_IsDown( int keynum ) {
	return CG_Import_Syscall( CG_KEY_ISDOWN, keynum ) ? qtrue : qfalse;
}

/*
==============
QL_CG_trap_Key_GetCatcher
==============
*/
static int QDECL QL_CG_trap_Key_GetCatcher( void ) {
	return CG_Import_Syscall( CG_KEY_GETCATCHER );
}

/*
==============
QL_CG_trap_Key_SetCatcher
==============
*/
static void QDECL QL_CG_trap_Key_SetCatcher( int catcher ) {
	CG_Import_Syscall( CG_KEY_SETCATCHER, catcher );
}

/*
==============
QL_CG_trap_Key_GetKey
==============
*/
static int QDECL QL_CG_trap_Key_GetKey( const char *binding ) {
	return CG_Import_Syscall( CG_KEY_GETKEY, binding );
}

/*
==============
QL_CG_trap_Cmd_ExecuteText
==============
*/
static void QDECL QL_CG_trap_Cmd_ExecuteText( int exec_when, const char *text ) {
	CG_Import_Syscall( CG_CMD_EXECUTETEXT, exec_when, text );
}

/*
==============
QL_CG_trap_Key_KeynumToStringBuf
==============
*/
static void QDECL QL_CG_trap_Key_KeynumToStringBuf( int keynum, char *buf, int buflen ) {
	CG_Import_Syscall( CG_KEY_KEYNUMTOSTRINGBUF, keynum, buf, buflen );
}

/*
==============
QL_CG_trap_Key_GetBindingBuf
==============
*/
static void QDECL QL_CG_trap_Key_GetBindingBuf( int keynum, char *buf, int buflen ) {
	CG_Import_Syscall( CG_KEY_GETBINDINGBUF, keynum, buf, buflen );
}

/*
==============
QL_CG_trap_Key_SetBinding
==============
*/
static void QDECL QL_CG_trap_Key_SetBinding( int keynum, const char *binding ) {
	CG_Import_Syscall( CG_KEY_SETBINDING, keynum, binding );
}

/*
==============
QL_CG_trap_Key_GetOverstrikeMode
==============
*/
static qboolean QDECL QL_CG_trap_Key_GetOverstrikeMode( void ) {
	return CG_Import_Syscall( CG_KEY_GETOVERSTRIKEMODE ) ? qtrue : qfalse;
}

/*
==============
QL_CG_trap_Key_SetOverstrikeMode
==============
*/
static void QDECL QL_CG_trap_Key_SetOverstrikeMode( qboolean state ) {
	CG_Import_Syscall( CG_KEY_SETOVERSTRIKEMODE, state );
}

/*
==============
QL_CG_trap_PC_AddGlobalDefine
==============
*/
static int QDECL QL_CG_trap_PC_AddGlobalDefine( char *define ) {
	return CG_Import_Syscall( CG_PC_ADD_GLOBAL_DEFINE, define );
}

/*
==============
QL_CG_trap_PC_LoadSource
==============
*/
static int QDECL QL_CG_trap_PC_LoadSource( const char *filename ) {
	return CG_Import_Syscall( CG_PC_LOAD_SOURCE, filename );
}

/*
==============
QL_CG_trap_PC_FreeSource
==============
*/
static int QDECL QL_CG_trap_PC_FreeSource( int handle ) {
	return CG_Import_Syscall( CG_PC_FREE_SOURCE, handle );
}

/*
==============
QL_CG_trap_PC_ReadToken
==============
*/
static int QDECL QL_CG_trap_PC_ReadToken( int handle, pc_token_t *pc_token ) {
	return CG_Import_Syscall( CG_PC_READ_TOKEN, handle, pc_token );
}

/*
==============
QL_CG_trap_PC_SourceFileAndLine
==============
*/
static int QDECL QL_CG_trap_PC_SourceFileAndLine( int handle, char *filename, int *line ) {
	return CG_Import_Syscall( CG_PC_SOURCE_FILE_AND_LINE, handle, filename, line );
}

/*
==============
QL_CG_trap_S_StopBackgroundTrack
==============
*/
static void QDECL QL_CG_trap_S_StopBackgroundTrack( void ) {
	CG_Import_Syscall( CG_S_STOPBACKGROUNDTRACK );
}

/*
==============
QL_CG_trap_RealTime
==============
*/
static int QDECL QL_CG_trap_RealTime( qtime_t *qtime ) {
	return CG_Import_Syscall( CG_REAL_TIME, qtime );
}

/*
==============
QL_CG_trap_SnapVector
==============
*/
static void QDECL QL_CG_trap_SnapVector( float *v ) {
	CG_Import_Syscall( CG_SNAPVECTOR, v );
}

/*
==============
QL_CG_trap_CIN_PlayCinematic
==============
*/
static int QDECL QL_CG_trap_CIN_PlayCinematic( const char *arg0, int xpos, int ypos, int width, int height, int bits ) {
	return CG_Import_Syscall( CG_CIN_PLAYCINEMATIC, arg0, xpos, ypos, width, height, bits );
}

/*
==============
QL_CG_trap_CIN_StopCinematic
==============
*/
static e_status QDECL QL_CG_trap_CIN_StopCinematic( int handle ) {
	return static_cast<e_status>( CG_Import_Syscall( CG_CIN_STOPCINEMATIC, handle ) );
}

/*
==============
QL_CG_trap_CIN_RunCinematic
==============
*/
static e_status QDECL QL_CG_trap_CIN_RunCinematic( int handle ) {
	return static_cast<e_status>( CG_Import_Syscall( CG_CIN_RUNCINEMATIC, handle ) );
}

/*
==============
QL_CG_trap_CIN_DrawCinematic
==============
*/
static void QDECL QL_CG_trap_CIN_DrawCinematic( int handle ) {
	CG_Import_Syscall( CG_CIN_DRAWCINEMATIC, handle );
}

/*
==============
QL_CG_trap_CIN_SetExtents
==============
*/
static void QDECL QL_CG_trap_CIN_SetExtents( int handle, int x, int y, int w, int h ) {
	CG_Import_Syscall( CG_CIN_SETEXTENTS, handle, x, y, w, h );
}

/*
==============
QL_CG_trap_GetEntityToken
==============
*/
static qboolean QDECL QL_CG_trap_GetEntityToken( char *buffer, int bufferSize ) {
	return CG_Import_Syscall( CG_GET_ENTITY_TOKEN, buffer, bufferSize ) ? qtrue : qfalse;
}

/*
==============
QL_CG_trap_R_inPVS
==============
*/
static qboolean QDECL QL_CG_trap_R_inPVS( const vec3_t p1, const vec3_t p2 ) {
	return CG_Import_Syscall( CG_R_INPVS, p1, p2 ) ? qtrue : qfalse;
}

/*
==============
QL_CG_trap_CM_LoadModel
==============
*/
static int QDECL QL_CG_trap_CM_LoadModel( const char *name ) {
	(void)name;
	return 0;
}

/*
==============
QL_CG_trap_Memset
==============
*/
static void *QDECL QL_CG_trap_Memset( void *dest, int c, int count ) {
	return memset( dest, c, count );
}

/*
==============
QL_CG_trap_Memcpy
==============
*/
static void *QDECL QL_CG_trap_Memcpy( void *dest, const void *src, int count ) {
	return memcpy( dest, src, count );
}

/*
==============
QL_CG_trap_Strncpy
==============
*/
static char *QDECL QL_CG_trap_Strncpy( char *dest, const char *src, int count ) {
	return strncpy( dest, src, count );
}

/*
==============
QL_CG_trap_Sin
==============
*/
static float QDECL QL_CG_trap_Sin( float x ) {
	return (float)sin( x );
}

/*
==============
QL_CG_trap_Cos
==============
*/
static float QDECL QL_CG_trap_Cos( float x ) {
	return (float)cos( x );
}

/*
==============
QL_CG_trap_Atan2
==============
*/
static float QDECL QL_CG_trap_Atan2( float y, float x ) {
	return (float)atan2( y, x );
}

/*
==============
QL_CG_trap_Sqrt
==============
*/
static float QDECL QL_CG_trap_Sqrt( float x ) {
	return (float)sqrt( x );
}

/*
==============
QL_CG_trap_Floor
==============
*/
static float QDECL QL_CG_trap_Floor( float x ) {
	return (float)floor( x );
}

/*
==============
QL_CG_trap_Ceil
==============
*/
static float QDECL QL_CG_trap_Ceil( float x ) {
	return (float)ceil( x );
}

/*
==============
QL_CG_trap_TestPrintInt
==============
*/
static void QDECL QL_CG_trap_TestPrintInt( int value ) {
	(void)value;
}

/*
==============
QL_CG_trap_TestPrintFloat
==============
*/
static void QDECL QL_CG_trap_TestPrintFloat( float value ) {
	(void)value;
}

/*
==============
QL_CG_trap_ACos
==============
*/
static float QDECL QL_CG_trap_ACos( float x ) {
	return Q_acos( x );
}

/*
==============
QL_CG_trap_AdvertisementBridge_InitCGame
==============
*/
static void QDECL QL_CG_trap_AdvertisementBridge_InitCGame( void ) {
	CG_Import_Syscall( CG_ADVERTISEMENTBRIDGE_INITCGAME );
}

/*
==============
QL_CG_trap_AdvertisementBridge_ShutdownCGame
==============
*/
static void QDECL QL_CG_trap_AdvertisementBridge_ShutdownCGame( void ) {
	CG_Import_Syscall( CG_ADVERTISEMENTBRIDGE_SHUTDOWNCGAME );
}

/*
==============
QL_CG_trap_AdvertisementBridge_UpdateLoadingViewParameters
==============
*/
static void QDECL QL_CG_trap_AdvertisementBridge_UpdateLoadingViewParameters( void ) {
	CG_Import_Syscall( CG_ADVERTISEMENTBRIDGE_UPDATELOADINGVIEWPARAMETERS );
}

/*
==============
QL_CG_trap_AdvertisementBridge_SetFrameTime
==============
*/
static void QDECL QL_CG_trap_AdvertisementBridge_SetFrameTime( int frameTime ) {
	CG_Import_Syscall( CG_ADVERTISEMENTBRIDGE_SETFRAMETIME, frameTime );
}
