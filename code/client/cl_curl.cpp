/*
===========================================================================
Copyright (C) 2006 Tony J. White (tjw@tjw.org)

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

#ifdef USE_CURL
extern "C" {
#include "client.h"
}

#include "client_cpp.h"

#include <algorithm>
#include <array>
#include <memory>

using fnql::CloseFile;
using fnql::FileWrite;

cvar_t *cl_cURLLib = nullptr;

#define ALLOWED_PROTOCOLS ( CURLPROTO_HTTP | CURLPROTO_HTTPS | CURLPROTO_FTP | CURLPROTO_FTPS )

#define ALLOWED_PROTOCOLS_STR "http,https,ftp,ftps"

namespace {

struct CurlEscapedStringDeleter {
	download_t *download;

	void operator()( char *ptr ) const
	{
		if ( ptr ) {
			download->func.free( ptr );
		}
	}
};

using ScopedCurlEscapedString = std::unique_ptr<char, CurlEscapedStringDeleter>;

} // namespace

#ifdef USE_CURL_DLOPEN

char* (*qcurl_version)(void) = nullptr;

CURL* (*qcurl_easy_init)(void) = nullptr;
CURLcode (*qcurl_easy_setopt)(CURL *curl, CURLoption option, ...) = nullptr;
CURLcode (*qcurl_easy_perform)(CURL *curl) = nullptr;
void (*qcurl_easy_cleanup)(CURL *curl) = nullptr;
CURLcode (*qcurl_easy_getinfo)(CURL *curl, CURLINFO info, ...) = nullptr;
CURL* (*qcurl_easy_duphandle)(CURL *curl) = nullptr;
void (*qcurl_easy_reset)(CURL *curl) = nullptr;
const char *(*qcurl_easy_strerror)(CURLcode) = nullptr;

CURLM* (*qcurl_multi_init)(void) = nullptr;
CURLMcode (*qcurl_multi_add_handle)(CURLM *multi_handle,
                                                CURL *curl_handle) = nullptr;
CURLMcode (*qcurl_multi_remove_handle)(CURLM *multi_handle,
                                                CURL *curl_handle) = nullptr;
CURLMcode (*qcurl_multi_fdset)(CURLM *multi_handle,
                                                fd_set *read_fd_set,
                                                fd_set *write_fd_set,
                                                fd_set *exc_fd_set,
                                                int *max_fd) = nullptr;
CURLMcode (*qcurl_multi_perform)(CURLM *multi_handle,
                                                int *running_handles) = nullptr;
CURLMcode (*qcurl_multi_cleanup)(CURLM *multi_handle) = nullptr;
CURLMsg *(*qcurl_multi_info_read)(CURLM *multi_handle,
                                                int *msgs_in_queue) = nullptr;
const char *(*qcurl_multi_strerror)(CURLMcode) = nullptr;

static void *cURLLib = nullptr;

/*
=================
GPA
=================
*/
template <typename Fn>
static Fn GPA(const char *str)
{
	void *rv;

	rv = Sys_LoadFunction(cURLLib, str);
	if(!rv)
	{
		Com_Printf("Can't load symbol %s\n", str);
		clc.cURLEnabled = qfalse;
		return nullptr;
	}
	else
	{
		Com_DPrintf("Loaded symbol %s (0x%p)\n", str, rv);
		return reinterpret_cast<Fn>(rv);
	}
}

template <typename Fn>
static Fn LoadDownloadCurlFunction( download_t *dl, const char *name )
{
	return reinterpret_cast<Fn>( Sys_LoadFunction( dl->func.lib, name ) );
}
#endif /* USE_CURL_DLOPEN */

/*
=================
CL_cURL_Init
=================
*/
qboolean CL_cURL_Init( void )
{
#ifdef USE_CURL_DLOPEN
	if(cURLLib)
		return qtrue;


	Com_Printf("Loading \"%s\"...", cl_cURLLib->string);
	if( (cURLLib = Sys_LoadLibrary(cl_cURLLib->string)) == nullptr )
	{
#ifdef _WIN32
		return qfalse;
#else
		std::array<char, 1024> fn;

		Q_strncpyz( fn.data(), Sys_Pwd(), static_cast<int>( fn.size() ) );
		strncat( fn.data(), "/", fn.size() - strlen( fn.data() ) - 1 );
		strncat( fn.data(), cl_cURLLib->string, fn.size() - strlen( fn.data() ) - 1 );

		if((cURLLib = Sys_LoadLibrary(fn.data())) == nullptr)
		{
#ifdef ALTERNATE_CURL_LIB
			// On some linux distributions there is no libcurl.so.3, but only libcurl.so.4. That one works too.
			if( (cURLLib = Sys_LoadLibrary(ALTERNATE_CURL_LIB)) == nullptr )
			{
				return qfalse;
			}
#else
			return qfalse;
#endif
		}
#endif /* _WIN32 */
	}

	clc.cURLEnabled = qtrue;

	qcurl_version = GPA<decltype(qcurl_version)>("curl_version");

	qcurl_easy_init = GPA<decltype(qcurl_easy_init)>("curl_easy_init");
	qcurl_easy_setopt = GPA<decltype(qcurl_easy_setopt)>("curl_easy_setopt");
	qcurl_easy_perform = GPA<decltype(qcurl_easy_perform)>("curl_easy_perform");
	qcurl_easy_cleanup = GPA<decltype(qcurl_easy_cleanup)>("curl_easy_cleanup");
	qcurl_easy_getinfo = GPA<decltype(qcurl_easy_getinfo)>("curl_easy_getinfo");
	qcurl_easy_duphandle = GPA<decltype(qcurl_easy_duphandle)>("curl_easy_duphandle");
	qcurl_easy_reset = GPA<decltype(qcurl_easy_reset)>("curl_easy_reset");
	qcurl_easy_strerror = GPA<decltype(qcurl_easy_strerror)>("curl_easy_strerror");
	
	qcurl_multi_init = GPA<decltype(qcurl_multi_init)>("curl_multi_init");
	qcurl_multi_add_handle = GPA<decltype(qcurl_multi_add_handle)>("curl_multi_add_handle");
	qcurl_multi_remove_handle = GPA<decltype(qcurl_multi_remove_handle)>("curl_multi_remove_handle");
	qcurl_multi_fdset = GPA<decltype(qcurl_multi_fdset)>("curl_multi_fdset");
	qcurl_multi_perform = GPA<decltype(qcurl_multi_perform)>("curl_multi_perform");
	qcurl_multi_cleanup = GPA<decltype(qcurl_multi_cleanup)>("curl_multi_cleanup");
	qcurl_multi_info_read = GPA<decltype(qcurl_multi_info_read)>("curl_multi_info_read");
	qcurl_multi_strerror = GPA<decltype(qcurl_multi_strerror)>("curl_multi_strerror");

	if(!clc.cURLEnabled)
	{
		CL_cURL_Shutdown();
		Com_Printf("FAIL One or more symbols not found\n");
		return qfalse;
	}
	Com_Printf("OK\n");

	return qtrue;
#else
	clc.cURLEnabled = qtrue;
	return qtrue;
#endif /* USE_CURL_DLOPEN */
}

/*
=================
CL_cURL_Shutdown
=================
*/
void CL_cURL_Shutdown( void )
{
	CL_cURL_Cleanup();
#ifdef USE_CURL_DLOPEN
	if(cURLLib)
	{
		Sys_UnloadLibrary(cURLLib);
		cURLLib = nullptr;
	}
	qcurl_version = nullptr;

	qcurl_easy_init = nullptr;
	qcurl_easy_setopt = nullptr;
	qcurl_easy_perform = nullptr;
	qcurl_easy_cleanup = nullptr;
	qcurl_easy_getinfo = nullptr;
	qcurl_easy_duphandle = nullptr;
	qcurl_easy_reset = nullptr;
	qcurl_easy_strerror = nullptr;

	qcurl_multi_init = nullptr;
	qcurl_multi_add_handle = nullptr;
	qcurl_multi_remove_handle = nullptr;
	qcurl_multi_fdset = nullptr;
	qcurl_multi_perform = nullptr;
	qcurl_multi_cleanup = nullptr;
	qcurl_multi_info_read = nullptr;
	qcurl_multi_strerror = nullptr;
#endif /* USE_CURL_DLOPEN */
}

void CL_cURL_Cleanup()
{
	if(clc.downloadCURLM) {
		CURLMcode result;

		if(clc.downloadCURL) {
			result = qcurl_multi_remove_handle(clc.downloadCURLM,
				clc.downloadCURL);
			if(result != CURLM_OK) {
				Com_DPrintf("qcurl_multi_remove_handle failed: %s\n", qcurl_multi_strerror(result));
			}
			qcurl_easy_cleanup(clc.downloadCURL);
		}
		result = qcurl_multi_cleanup(clc.downloadCURLM);
		if(result != CURLM_OK) {
			Com_DPrintf("CL_cURL_Cleanup: qcurl_multi_cleanup failed: %s\n", qcurl_multi_strerror(result));
		}
		clc.downloadCURLM = nullptr;
		clc.downloadCURL = nullptr;
	}
	else if(clc.downloadCURL) {
		qcurl_easy_cleanup(clc.downloadCURL);
		clc.downloadCURL = nullptr;
	}
}

#if CURL_AT_LEAST_VERSION(7, 32, 0)
static int CL_cURL_CallbackProgress( void *dummy, curl_off_t dltotal, curl_off_t dlnow,
	curl_off_t ultotal, curl_off_t ulnow )
#else
static int CL_cURL_CallbackProgress( void *dummy, double dltotal, double dlnow,
	double ultotal, double ulnow )
#endif
{
	clc.downloadSize = static_cast<int>( dltotal );
	Cvar_SetIntegerValue( "cl_downloadSize", clc.downloadSize );
	clc.downloadCount = static_cast<int>( dlnow );
	Cvar_SetIntegerValue( "cl_downloadCount", clc.downloadCount );
	return 0;
}


static size_t CL_cURL_CallbackWrite( void *buffer, size_t size, size_t nmemb, void *stream )
{
	const size_t bytes = size * nmemb;

	if ( clc.download == FS_INVALID_HANDLE ) {
		if ( !CL_ValidPakSignature( static_cast<const byte *>( buffer ), static_cast<int>( bytes ) ) ) {
			Com_Error( ERR_DROP, "CL_cURL_CallbackWrite: invalid pak signature for %s", 
				clc.downloadName );
			return static_cast<size_t>( -1 );
		}
		clc.download = FS_SV_FOpenFileWrite( clc.downloadTempName );
		if ( clc.download == FS_INVALID_HANDLE ) {
			Com_Error( ERR_DROP, "CL_cURL_CallbackWrite: failed to open %s for writing", 
				clc.downloadTempName );
			return static_cast<size_t>( -1 );
		}
	}

	FileWrite( static_cast<fileHandle_t *>( stream )[0], buffer, bytes );
	return bytes;
}


static CURLcode qcurl_easy_setopt_warn(CURL *curl, int optionRaw, ...)
{
	CURLcode result;
	const CURLoption option = static_cast<CURLoption>( optionRaw );

	va_list argp;
	va_start(argp, optionRaw);

	if(option < CURLOPTTYPE_OBJECTPOINT) {
		long longValue = va_arg(argp, long);
		result = qcurl_easy_setopt(curl, option, longValue);
	} else if(option < CURLOPTTYPE_OFF_T) {
		void *pointerValue = va_arg(argp, void *);
		result = qcurl_easy_setopt(curl, option, pointerValue);
	} else {
		curl_off_t offsetValue = va_arg(argp, curl_off_t);
		result = qcurl_easy_setopt(curl, option, offsetValue);
	}

	if(result != CURLE_OK) {
		Com_DPrintf("qcurl_easy_setopt failed: %s\n", qcurl_easy_strerror(result));
	}
	va_end(argp);

	return result;
}

static void CL_cURL_CloseDownload( void ) 
{
	CloseFile( clc.download );
}

void CL_cURL_BeginDownload( const char *localName, const char *remoteURL )
{
	CURLMcode result;

	clc.cURLUsed = qtrue;
	Com_Printf("URL: %s\n", remoteURL);
	Com_DPrintf("***** CL_cURL_BeginDownload *****\n"
		"Localname: %s\n"
		"RemoteURL: %s\n"
		"****************************\n", localName, remoteURL);
	CL_cURL_Cleanup();
	Q_strncpyz(clc.downloadURL, remoteURL, sizeof(clc.downloadURL));
	Q_strncpyz(clc.downloadName, localName, sizeof(clc.downloadName));
	Com_sprintf(clc.downloadTempName, sizeof(clc.downloadTempName),
		"%s.tmp", localName);

	// Set so UI gets access to it
	Cvar_Set( "cl_downloadName", localName );
	Cvar_Set( "cl_downloadSize", "0" );
	Cvar_Set( "cl_downloadCount", "0" );
	Cvar_SetIntegerValue( "cl_downloadTime", cls.realtime );

	CL_cURL_CloseDownload();

	clc.downloadBlock = 0; // Starting new file
	clc.downloadCount = 0;

	clc.downloadCURL = qcurl_easy_init();
	if(!clc.downloadCURL) {
		Com_Error(ERR_DROP, "CL_cURL_BeginDownload: qcurl_easy_init() "
			"failed");
		return;
	}

	if ( com_developer->integer )
		qcurl_easy_setopt_warn( clc.downloadCURL, CURLOPT_VERBOSE, 1 );
	qcurl_easy_setopt_warn(clc.downloadCURL, CURLOPT_URL, clc.downloadURL);
	qcurl_easy_setopt_warn(clc.downloadCURL, CURLOPT_TRANSFERTEXT, 0);
	qcurl_easy_setopt_warn(clc.downloadCURL, CURLOPT_REFERER, va("ioQ3://%s",
		NET_AdrToString(&clc.serverAddress)));
	qcurl_easy_setopt_warn(clc.downloadCURL, CURLOPT_USERAGENT, Q3_VERSION);
	qcurl_easy_setopt_warn(clc.downloadCURL, CURLOPT_WRITEFUNCTION,
		CL_cURL_CallbackWrite);
	qcurl_easy_setopt_warn(clc.downloadCURL, CURLOPT_WRITEDATA, &clc.download);
	qcurl_easy_setopt_warn(clc.downloadCURL, CURLOPT_NOPROGRESS, 0);
#if CURL_AT_LEAST_VERSION(7, 32, 0)
	qcurl_easy_setopt_warn(clc.downloadCURL, CURLOPT_XFERINFOFUNCTION,
		CL_cURL_CallbackProgress);
	qcurl_easy_setopt_warn(clc.downloadCURL, CURLOPT_XFERINFODATA, nullptr);
#else
	qcurl_easy_setopt_warn(clc.downloadCURL, CURLOPT_PROGRESSFUNCTION,
		CL_cURL_CallbackProgress);
	qcurl_easy_setopt_warn(clc.downloadCURL, CURLOPT_PROGRESSDATA, nullptr);
#endif
	qcurl_easy_setopt_warn(clc.downloadCURL, CURLOPT_FAILONERROR, 1);
	qcurl_easy_setopt_warn(clc.downloadCURL, CURLOPT_FOLLOWLOCATION, 1);
	qcurl_easy_setopt_warn(clc.downloadCURL, CURLOPT_MAXREDIRS, 5);
#if CURL_AT_LEAST_VERSION(7, 85, 0)
	qcurl_easy_setopt_warn(clc.downloadCURL, CURLOPT_PROTOCOLS_STR, ALLOWED_PROTOCOLS_STR);
#else
	qcurl_easy_setopt_warn(clc.downloadCURL, CURLOPT_PROTOCOLS, ALLOWED_PROTOCOLS);
#endif

#ifdef CURL_MAX_READ_SIZE
	qcurl_easy_setopt_warn(clc.downloadCURL, CURLOPT_BUFFERSIZE, CURL_MAX_READ_SIZE);
#endif

	clc.downloadCURLM = qcurl_multi_init();	
	if( !clc.downloadCURLM ) {
		qcurl_easy_cleanup( clc.downloadCURL );
		clc.downloadCURL = nullptr;
		Com_Error( ERR_DROP, "CL_cURL_BeginDownload: qcurl_multi_init() "
			"failed");
		return;
	}

	result = qcurl_multi_add_handle( clc.downloadCURLM, clc.downloadCURL );
	if ( result != CURLM_OK ) {
		qcurl_easy_cleanup( clc.downloadCURL );
		clc.downloadCURL = nullptr;
		Com_Error( ERR_DROP, "CL_cURL_BeginDownload: qcurl_multi_add_handle() failed: %s",	
			qcurl_multi_strerror( result ) );
		return;
	}

	if(!(clc.sv_allowDownload & DLF_NO_DISCONNECT) &&
		!clc.cURLDisconnected) {

		CL_AddReliableCommand("disconnect", qtrue);
		CL_WritePacket( 2 );
		clc.cURLDisconnected = qtrue;
	}
}


void CL_cURL_PerformDownload( void )
{
	CURLMcode res;
	CURLMsg *msg;
	int c;
	int i = 0;

	res = qcurl_multi_perform(clc.downloadCURLM, &c);
	while(res == CURLM_CALL_MULTI_PERFORM && i < 100) {
		res = qcurl_multi_perform(clc.downloadCURLM, &c);
		i++;
	}
	if(res == CURLM_CALL_MULTI_PERFORM)
		return;
	msg = qcurl_multi_info_read(clc.downloadCURLM, &c);
	if(msg == nullptr) {
		return;
	}
	CL_cURL_CloseDownload();
	if ( msg->msg == CURLMSG_DONE && msg->data.result == CURLE_OK ) {
		FS_SV_Rename( clc.downloadTempName, clc.downloadName );
		clc.downloadRestart = qtrue;
	}
	else {
		long code;

		qcurl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE,
			&code);	
		Com_Error(ERR_DROP, "Download Error: %s Code: %ld URL: %s",
			qcurl_easy_strerror(msg->data.result),
			code, clc.downloadURL);
	}

	CL_NextDownload();
}


/*
==================================

Common CURL downloading functions

==================================
*/


/*
==================================
stristr

case-insensitive sub-string search
==================================
*/
static const char* stristr( const char *source, const char *target )
{
	const char *p0, *p1, *p2, *pn;
	char c1, c2;

	if ( *target == '\0' )
	{
		return source;
	}

	pn = source;
	p1 = source;
	p2 = target;

	while ( *++p2 )
	{
		pn++;
	}

	while ( *pn != '\0' ) 
	{

		p0 = p1;
		p2 = target;

		while ( (c1 = *p1) != '\0' && (c2 = *p2) != '\0' )
		{
				if ( c1 <= 'Z' && c1 >= 'A' )
					c1 += ('a' - 'A');

				if ( c2 <= 'Z' && c2 >= 'A' )
					c2 += ('a' - 'A');

				if ( c1 != c2 )
				{
					break;
				}

				p1++;
				p2++;
		}

		if ( *p2 == '\0' )
		{
			return p0;
		}

		p1 = p0 + 1;
		pn++;
	}

	return nullptr;
}


/*
==================================
replace1
==================================
*/
static int replace1( const char src, const char dst, char *str )
{
	int count;

	if ( !str ) 
		return 0;

	count = 0;

	while ( *str != '\0' )
	{
		if ( *str == src )
		{
			*str = dst;
			count++;
		}
		str++;
	}

	return count;
}

static const char *Com_DL_GetPackExtension( const char *name )
{
	if ( COM_CompareExtension( name, ".pak" ) ) {
		return ".pak";
	}

	if ( COM_CompareExtension( name, ".pk3" ) ) {
		return ".pk3";
	}

	return nullptr;
}

static void Com_DL_StripPackExtension( char *name )
{
	if ( !FS_StripExt( name, ".pk3" ) ) {
		FS_StripExt( name, ".pak" );
	}
}


/*
=================
Com_DL_Done
=================
*/
void Com_DL_Done( download_t *dl ) 
{
	if ( dl->func.lib )
		Sys_UnloadLibrary( dl->func.lib );
	dl->func = {};
}


/*
=================
Com_DL_Init
=================
*/
qboolean Com_DL_Init( download_t *dl )
{
#ifdef USE_CURL_DLOPEN
	Com_Printf( "Loading \"%s\"...", cl_cURLLib->string );
	if( ( dl->func.lib = Sys_LoadLibrary( cl_cURLLib->string ) ) == nullptr )
	{
#ifdef _WIN32
		return qfalse;
#else
		std::array<char, 1024> fn;

		Q_strncpyz( fn.data(), Sys_Pwd(), static_cast<int>( fn.size() ) );
		strncat( fn.data(), "/", fn.size() - strlen( fn.data() ) - 1 );
		strncat( fn.data(), cl_cURLLib->string, fn.size() - strlen( fn.data() ) - 1 );

		if ( ( dl->func.lib = Sys_LoadLibrary( fn.data() ) ) == nullptr )
		{
#ifdef ALTERNATE_CURL_LIB
			// On some linux distributions there is no libcurl.so.3, but only libcurl.so.4. That one works too.
			if( ( dl->func.lib = Sys_LoadLibrary( ALTERNATE_CURL_LIB ) ) == nullptr )
			{
				return qfalse;
			}
#else
			return qfalse;
#endif
		}
#endif /* _WIN32 */
	}

	Sys_LoadFunctionErrors(); // reset error count;

	dl->func.version = LoadDownloadCurlFunction<decltype(dl->func.version)>( dl, "curl_version" );
	dl->func.easy_escape = LoadDownloadCurlFunction<decltype(dl->func.easy_escape)>( dl, "curl_easy_escape" );
	dl->func.free = LoadDownloadCurlFunction<decltype(dl->func.free)>( dl, "curl_free" );

	dl->func.easy_init = LoadDownloadCurlFunction<decltype(dl->func.easy_init)>( dl, "curl_easy_init" );
	dl->func.easy_setopt = LoadDownloadCurlFunction<decltype(dl->func.easy_setopt)>( dl, "curl_easy_setopt" );
	dl->func.easy_perform = LoadDownloadCurlFunction<decltype(dl->func.easy_perform)>( dl, "curl_easy_perform" );
	dl->func.easy_cleanup = LoadDownloadCurlFunction<decltype(dl->func.easy_cleanup)>( dl, "curl_easy_cleanup" );
	dl->func.easy_getinfo = LoadDownloadCurlFunction<decltype(dl->func.easy_getinfo)>( dl, "curl_easy_getinfo" );
	dl->func.easy_strerror = LoadDownloadCurlFunction<decltype(dl->func.easy_strerror)>( dl, "curl_easy_strerror" );
	
	dl->func.multi_init = LoadDownloadCurlFunction<decltype(dl->func.multi_init)>( dl, "curl_multi_init" );
	dl->func.multi_add_handle = LoadDownloadCurlFunction<decltype(dl->func.multi_add_handle)>( dl, "curl_multi_add_handle" );
	dl->func.multi_remove_handle = LoadDownloadCurlFunction<decltype(dl->func.multi_remove_handle)>( dl, "curl_multi_remove_handle" );
	dl->func.multi_perform = LoadDownloadCurlFunction<decltype(dl->func.multi_perform)>( dl, "curl_multi_perform" );
	dl->func.multi_cleanup = LoadDownloadCurlFunction<decltype(dl->func.multi_cleanup)>( dl, "curl_multi_cleanup" );
	dl->func.multi_info_read = LoadDownloadCurlFunction<decltype(dl->func.multi_info_read)>( dl, "curl_multi_info_read" );
	dl->func.multi_strerror = LoadDownloadCurlFunction<decltype(dl->func.multi_strerror)>( dl, "curl_multi_strerror" );

	if ( Sys_LoadFunctionErrors() )
	{
		Com_DL_Done( dl );
		Com_Printf( "FAIL: One or more symbols not found\n" );
		return qfalse;
	}

	Com_Printf( "OK\n" );

	return qtrue;
#else

	dl->func.lib = nullptr;

	dl->func.version = curl_version;
	dl->func.easy_escape = curl_easy_escape;
	dl->func.free = reinterpret_cast<void (*)(char *)>( curl_free );

	dl->func.easy_init = curl_easy_init;
	dl->func.easy_setopt = curl_easy_setopt;
	dl->func.easy_perform = curl_easy_perform;
	dl->func.easy_cleanup = curl_easy_cleanup;
	dl->func.easy_getinfo = curl_easy_getinfo;
	dl->func.easy_strerror = curl_easy_strerror;
	
	dl->func.multi_init = curl_multi_init;
	dl->func.multi_add_handle = curl_multi_add_handle;
	dl->func.multi_remove_handle = curl_multi_remove_handle;
	dl->func.multi_perform = curl_multi_perform;
	dl->func.multi_cleanup = curl_multi_cleanup;
	dl->func.multi_info_read = curl_multi_info_read;
	dl->func.multi_strerror = curl_multi_strerror;

	return qtrue;
#endif /* USE_CURL_DLOPEN */
}


/*
=================
Com_DL_Cleanup
=================
*/
qboolean Com_DL_InProgress( const download_t *dl )
{
	if ( dl->cURL && dl->URL[0] )
		return qtrue;
	else
		return qfalse;
}


/*
=================
Com_DL_Cleanup
=================
*/
void Com_DL_Cleanup( download_t *dl )
{
	if( dl->cURLM )
	{
		if ( dl->cURL )
		{
			dl->func.multi_remove_handle( dl->cURLM, dl->cURL );
			dl->func.easy_cleanup( dl->cURL );
		}
		dl->func.multi_cleanup( dl->cURLM );
		dl->cURLM = nullptr;
		dl->cURL = nullptr;
	}
	else if( dl->cURL )
	{
		dl->func.easy_cleanup( dl->cURL );
		dl->cURL = nullptr;
	}
	if ( dl->fHandle != FS_INVALID_HANDLE )
	{
		CloseFile( dl->fHandle );
	}

	if ( dl->mapAutoDownload )
	{
		Cvar_Set( "cl_downloadName", "" );
		Cvar_Set( "cl_downloadSize", "0" );
		Cvar_Set( "cl_downloadCount", "0" );
		Cvar_Set( "cl_downloadTime", "0" );
	}

	dl->Size = 0;
	dl->Count = 0;

	dl->URL[0] = '\0';
	dl->Name[0] = '\0';
	dl->Ext[0] = '\0';
	if ( dl->TempName[0] )
	{
		FS_Remove( dl->TempName );
	}
	dl->TempName[0] = '\0';
	dl->progress[0] = '\0';
	dl->headerCheck = qfalse;
	dl->mapAutoDownload = qfalse;

	Com_DL_Done( dl );
}


static const char *sizeToString( int size )
{
	static std::array<char, 32> buf;
	if ( size < 1024 ) {
		Com_sprintf( buf.data(), static_cast<int>( buf.size() ), "%iB", size );
	} else if ( size < 1024*1024 ) {
		Com_sprintf( buf.data(), static_cast<int>( buf.size() ), "%iKB", size / 1024 );
	} else {
		Com_sprintf( buf.data(), static_cast<int>( buf.size() ), "%i.%iMB", size / (1024*1024), (size / (1024*1024/10 )) % 10 );
	}
	return buf.data();
}


/*
=================
Com_DL_CallbackProgress
=================
*/
#if CURL_AT_LEAST_VERSION(7, 32, 0)
static int Com_DL_CallbackProgress( void *data, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow )
#else
static int Com_DL_CallbackProgress( void *data, double dltotal, double dlnow, double ultotal, double ulnow )
#endif
{
#if CURL_AT_LEAST_VERSION(7, 55, 0)
	curl_off_t speed;
#else
	double speed;
#endif

#if CURL_AT_LEAST_VERSION(7, 32, 0)
	curl_off_t percentage;
#else
	double percentage;
#endif

	download_t *dl = static_cast<download_t *>( data );

	dl->Size = static_cast<int>( dltotal );
	dl->Count = static_cast<int>( dlnow );

	if ( dl->mapAutoDownload && cls.state == CA_CONNECTED )
	{
		if ( Key_IsDown( K_ESCAPE ) )
		{
			Com_Printf( "%s: aborted\n", dl->Name );
			return -1;
		}
		Cvar_SetIntegerValue( "cl_downloadSize", dl->Size );
		Cvar_SetIntegerValue( "cl_downloadCount", dl->Count );
	}

	if ( dl->Size ) {
#if CURL_AT_LEAST_VERSION(7, 32, 0)
		percentage = ( dlnow * 100 ) / dltotal;
#else
		percentage = ( dlnow / dltotal ) * 100.0;
#endif
		Com_sprintf( dl->progress, static_cast<int>( sizeof( dl->progress ) ), " downloading %s: %s (%i%%)",
			dl->Name, sizeToString( dl->Count ), static_cast<int>( percentage ) );
	} else {
		Com_sprintf( dl->progress, static_cast<int>( sizeof( dl->progress ) ), " downloading %s: %s",
			dl->Name, sizeToString( dl->Count ) );
	}

#if CURL_AT_LEAST_VERSION(7, 55, 0)
	if ( dl->func.easy_getinfo( dl->cURL, CURLINFO_SPEED_DOWNLOAD_T, &speed ) == CURLE_OK )
#else
	if ( dl->func.easy_getinfo( dl->cURL, CURLINFO_SPEED_DOWNLOAD, &speed ) == CURLE_OK )
#endif
	{
		Q_strcat( dl->progress, sizeof( dl->progress ), va( " %s/s", sizeToString( static_cast<int>( speed ) ) ) );
	}

	return 0;
}


/*
=================
Com_DL_CallbackWrite
=================
*/
static size_t Com_DL_CallbackWrite( void *ptr, size_t size, size_t nmemb, void *userdata )
{
	download_t *dl;
	const size_t bytes = size * nmemb;

	dl = static_cast<download_t *>( userdata );

	if ( dl->fHandle == FS_INVALID_HANDLE )
	{
		if ( !CL_ValidPakSignature( static_cast<const byte *>( ptr ), static_cast<int>( bytes ) ) ) 
		{
			Com_Printf( S_COLOR_YELLOW "Com_DL_CallbackWrite(): invalid pak signature for %s.\n",
				dl->Name );
			return static_cast<size_t>( -1 );
		}

		dl->fHandle = FS_SV_FOpenFileWrite( dl->TempName );
		if ( dl->fHandle == FS_INVALID_HANDLE ) 
		{
			return static_cast<size_t>( -1 );
		}
	}

	FileWrite( dl->fHandle, ptr, bytes );

	return bytes;
}


/*
=================
Com_DL_ValidFileName
=================
*/
qboolean Com_DL_ValidFileName( const char *fileName )
{
	int c;
	while ( (c = *fileName++) != '\0' )
	{
		if ( c == '/' || c == '\\' || c == ':' )
			return qfalse;
		if ( c < ' ' || c > '~' )
			return qfalse;
	}
	return qtrue;
}


/*
=================
Com_DL_HeaderCallback
=================
*/
static size_t Com_DL_HeaderCallback( void *ptr, size_t size, size_t nmemb, void *userdata )
{
	std::array<char, MAX_OSPATH> name;
	std::array<char, 1024> header;
	char *s, quote, *d;
	download_t *dl;
	int len;
	const size_t bytes = size * nmemb;

	if ( bytes >= header.size() )
	{
		Com_Printf( S_COLOR_RED "Com_DL_HeaderCallback: header is too large." );
		return static_cast<size_t>( -1 );
	}

	dl = static_cast<download_t *>( userdata );
	
	std::copy_n( static_cast<const char *>( ptr ), bytes, header.data() );
	header[ bytes ] = '\0';

	//Com_Printf( "h: %s\n--------------------------\n", header );

	s = const_cast<char *>( stristr( header.data(), "content-disposition:" ) );
	if ( s ) 
	{
		s += 20; // strlen( "content-disposition:" )
		s = const_cast<char *>( stristr( s, "filename=" ) );
		if ( s ) 
		{
			s += 9; // strlen( "filename=" )
			
			d = name.data();
			replace1( '\r', '\0', s );
			replace1( '\n', '\0', s );

			// prevent overflow
			if ( strlen( s ) >= name.size() )
				s[ name.size() - 1 ] = '\0';

			if ( *s == '\'' || *s == '"' )
				quote = *s++;
			else
				quote = '\0';

			// copy filename
			while ( *s != '\0' && *s != quote ) 
				*d++ = *s++;
			len = static_cast<int>( d - name.data() );
			*d++ = '\0';

			// validate
			if ( len < 5 || Com_DL_GetPackExtension( name.data() ) == nullptr || !Com_DL_ValidFileName( name.data() ) )
			{
				Com_Printf( S_COLOR_RED "Com_DL_HeaderCallback: bad file name '%s'\n", name.data() );
				return static_cast<size_t>( -1 );
			}

			// strip extension
			Q_strncpyz( dl->Ext, Com_DL_GetPackExtension( name.data() ), sizeof( dl->Ext ) );
			Com_DL_StripPackExtension( name.data() );

			// store in
			Q_strncpyz( dl->Name, name.data(), sizeof( dl->Name ) );
		}
	}

	return bytes;
}


/*
===============================================================
Com_DL_Begin()

Start downloading file from remoteURL and save it under fs_game/localName
==============================================================
*/
qboolean Com_DL_Begin( download_t *dl, const char *localName, const char *remoteURL, qboolean autoDownload )
{
	const char *s;

	if ( Com_DL_InProgress( dl ) )
	{
		Com_Printf( S_COLOR_YELLOW " already downloading %s\n", dl->Name );
		return qfalse;
	}

	Com_DL_Cleanup( dl );

	if ( !Com_DL_Init( dl ) ) 
	{
		Com_Printf( S_COLOR_YELLOW "Error initializing cURL library\n" );
		return qfalse;
	}

	dl->cURL = dl->func.easy_init();
	if ( !dl->cURL ) 
	{
		Com_Printf( S_COLOR_RED "Com_DL_Begin: easy_init() failed\n" );
		Com_DL_Cleanup( dl );
		return qfalse;
	}

	{
		ScopedCurlEscapedString escapedName( dl->func.easy_escape( dl->cURL, localName, 0 ),
			CurlEscapedStringDeleter{ dl } );
		if ( !escapedName ) 
		{
			Com_Printf( S_COLOR_RED "Com_DL_Begin: easy_escape() failed\n" );
			Com_DL_Cleanup( dl );
			return qfalse;
		}

		Q_strncpyz( dl->URL, remoteURL, sizeof( dl->URL ) );

		if ( !Q_replace( "%1", escapedName.get(), dl->URL, sizeof( dl->URL ) ) )
		{
			if ( dl->URL[strlen(dl->URL)] != '/' )
				Q_strcat( dl->URL, sizeof( dl->URL ), "/" );
			Q_strcat( dl->URL, sizeof( dl->URL ), escapedName.get() );
			dl->headerCheck = qfalse;
		}
		else
		{
			dl->headerCheck = qtrue;
		}
	}

	Com_Printf( "URL: %s\n", dl->URL );

	if ( cl_dlDirectory->integer ) {
		Q_strncpyz( dl->gameDir, FS_GetBaseGameDir(), sizeof( dl->gameDir ) );
	} else {
		Q_strncpyz( dl->gameDir, FS_GetCurrentGameDir(), sizeof( dl->gameDir ) );
	}

	// try to extract game path from localName
	// dl->Name should contain only pak name without game dir and extension
	s = strrchr( localName, '/' );
	if ( s ) 
		Q_strncpyz( dl->Name, s+1, sizeof( dl->Name ) );
	else
		Q_strncpyz( dl->Name, localName, sizeof( dl->Name ) );

	Q_strncpyz( dl->Ext, ".pk3", sizeof( dl->Ext ) );
	if ( Com_DL_GetPackExtension( dl->Name ) != nullptr ) {
		Q_strncpyz( dl->Ext, Com_DL_GetPackExtension( dl->Name ), sizeof( dl->Ext ) );
		Com_DL_StripPackExtension( dl->Name );
	}
	if ( !dl->Name[0] )
	{
		Com_Printf( S_COLOR_YELLOW " empty filename after extension strip.\n" );
		return qfalse;
	}

	Com_sprintf( dl->TempName, sizeof( dl->TempName ), 
		"%s%c%s.%08x.tmp", dl->gameDir, PATH_SEP, dl->Name, rand() | (rand() << 16) );

	if ( com_developer->integer )
		dl->func.easy_setopt( dl->cURL, CURLOPT_VERBOSE, 1 );

	dl->func.easy_setopt( dl->cURL, CURLOPT_URL, dl->URL );
	dl->func.easy_setopt( dl->cURL, CURLOPT_TRANSFERTEXT, 0 );
	//dl->func.easy_setopt( dl->cURL, CURLOPT_REFERER, "q3a://127.0.0.1" );
	dl->func.easy_setopt( dl->cURL, CURLOPT_REFERER, dl->URL );
	dl->func.easy_setopt( dl->cURL, CURLOPT_USERAGENT, Q3_VERSION );
	dl->func.easy_setopt( dl->cURL, CURLOPT_WRITEFUNCTION, Com_DL_CallbackWrite );
	dl->func.easy_setopt( dl->cURL, CURLOPT_WRITEDATA, dl );
	if ( dl->headerCheck ) 
	{
		dl->func.easy_setopt( dl->cURL, CURLOPT_HEADERFUNCTION, Com_DL_HeaderCallback );
		dl->func.easy_setopt( dl->cURL, CURLOPT_HEADERDATA, dl );
	}
	dl->func.easy_setopt( dl->cURL, CURLOPT_NOPROGRESS, 0 );
#if CURL_AT_LEAST_VERSION(7, 32, 0)
	dl->func.easy_setopt( dl->cURL, CURLOPT_XFERINFOFUNCTION, Com_DL_CallbackProgress );
	dl->func.easy_setopt( dl->cURL, CURLOPT_XFERINFODATA, dl );
#else
	dl->func.easy_setopt( dl->cURL, CURLOPT_PROGRESSFUNCTION, Com_DL_CallbackProgress );
	dl->func.easy_setopt( dl->cURL, CURLOPT_PROGRESSDATA, dl );
#endif
	dl->func.easy_setopt( dl->cURL, CURLOPT_FAILONERROR, 1 );
	dl->func.easy_setopt( dl->cURL, CURLOPT_FOLLOWLOCATION, 1 );
	dl->func.easy_setopt( dl->cURL, CURLOPT_MAXREDIRS, 5 );
#if CURL_AT_LEAST_VERSION(7, 85, 0)
	dl->func.easy_setopt( dl->cURL, CURLOPT_PROTOCOLS_STR, ALLOWED_PROTOCOLS_STR );
#else
	dl->func.easy_setopt( dl->cURL, CURLOPT_PROTOCOLS, ALLOWED_PROTOCOLS );
#endif

#ifdef CURL_MAX_READ_SIZE
	dl->func.easy_setopt( dl->cURL, CURLOPT_BUFFERSIZE, CURL_MAX_READ_SIZE );
#endif

	dl->cURLM = dl->func.multi_init();

	if ( !dl->cURLM )
	{
		Com_DL_Cleanup( dl );
		Com_Printf( S_COLOR_RED "Com_DL_Begin: multi_init() failed\n" );
		return qfalse;
	}

	if ( dl->func.multi_add_handle( dl->cURLM, dl->cURL ) != CURLM_OK ) 
	{
		Com_DL_Cleanup( dl );
		Com_Printf( S_COLOR_RED "Com_DL_Begin: multi_add_handle() failed\n" );
		return qfalse;
	}

	dl->mapAutoDownload = autoDownload;

	if ( dl->mapAutoDownload )
	{
		Cvar_Set( "cl_downloadName", va( "%s%s", dl->Name, dl->Ext ) );
		Cvar_Set( "cl_downloadSize", "0" );
		Cvar_Set( "cl_downloadCount", "0" );
		Cvar_SetIntegerValue( "cl_downloadTime", cls.realtime );
	}

	return qtrue;
}


qboolean Com_DL_Perform( download_t *dl )
{
	std::array<char, sizeof( dl->TempName )> name;
	CURLMcode res;
	CURLMsg *msg;
	long code;
	int c, n;
	int i;

	res = dl->func.multi_perform( dl->cURLM, &c );

	n = 128;

	i = 0;
	while( res == CURLM_CALL_MULTI_PERFORM && i < n )
	{
		res = dl->func.multi_perform( dl->cURLM, &c );
		i++;
	}
	if( res == CURLM_CALL_MULTI_PERFORM )
	{
		return qtrue;
	}

	msg = dl->func.multi_info_read( dl->cURLM, &c );
	if( msg == nullptr )
	{
		return qtrue;
	}

	if ( dl->fHandle != FS_INVALID_HANDLE )
	{
		CloseFile( dl->fHandle );
	}

	if ( msg->msg == CURLMSG_DONE && msg->data.result == CURLE_OK )
	{
		bool autoDownload = dl->mapAutoDownload != qfalse;

		Com_sprintf( name.data(), static_cast<int>( name.size() ), "%s%c%s%s", dl->gameDir, PATH_SEP, dl->Name, dl->Ext );

		if ( !FS_SV_FileExists( name.data() ) )
		{
			FS_SV_Rename( dl->TempName, name.data() );
		}
		else
		{
			n = FS_GetZipChecksum( name.data() );
			Com_sprintf( name.data(), static_cast<int>( name.size() ), "%s%c%s.%08x%s", dl->gameDir, PATH_SEP, dl->Name, n, dl->Ext );

			if ( FS_SV_FileExists( name.data() ) )
				FS_Remove( name.data() );

			FS_SV_Rename( dl->TempName, name.data() );
		}

		Com_DL_Cleanup( dl );
		FS_Reload(); //clc.downloadRestart = qtrue;
		Com_Printf( S_COLOR_GREEN "%s downloaded\n", name.data() );
		if ( autoDownload )
		{
			if ( cls.state == CA_CONNECTED && !clc.demoplaying )
			{
				CL_AddReliableCommand( "donedl", qfalse ); // get new gamestate info from server
			} 
			else if ( clc.demoplaying )
			{
				// FIXME: there might be better solution than vid_restart
				cls.startCgame = qtrue;
				Cbuf_ExecuteText( EXEC_APPEND, "vid_restart\n" );
			}
		}
		return qfalse;
	}
	else
	{
		bool autoDownload = dl->mapAutoDownload != qfalse;
		dl->func.easy_getinfo( msg->easy_handle, CURLINFO_RESPONSE_CODE, &code );
		Com_Printf( S_COLOR_RED "Download Error: %s Code: %ld\n",
			dl->func.easy_strerror( msg->data.result ), code );
		Q_strncpyz( name.data(), dl->TempName, static_cast<int>( name.size() ) );
		Com_DL_Cleanup( dl );
		FS_Remove( name.data() );
		if ( autoDownload )
		{
			if ( cls.state == CA_CONNECTED )
			{
				Com_Error( ERR_DROP, "%s\n", "download error" );
			}
		}
	}

	return qtrue;
}

#endif /* USE_CURL */
