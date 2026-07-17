/*
===========================================================================

FnQL global-fog sidecar format.

This header contains the strict, renderer-independent text grammar shared by
the OpenGL-lineage and Vulkan backends.  Global fog is a visual-only renderer
layer: it never changes BSP fog assignment, visibility, collision, game state,
demo data, protocol data, or VM/native-module interfaces.

===========================================================================
*/

#ifndef TR_GLOBAL_FOG_H
#define TR_GLOBAL_FOG_H

#define GLOBAL_FOG_SIDECAR_MAX_BYTES 16384
#define GLOBAL_FOG_TOKEN_MAX_BYTES 64

typedef enum {
	GLOBAL_FOG_EXP = 0,
	GLOBAL_FOG_EXP2,
	GLOBAL_FOG_LINEAR
} globalFogMode_t;

typedef struct {
	qboolean		loaded;
	globalFogMode_t	mode;
	vec3_t			color;
	float			density;
	float			start;
	float			end;
	float			opacity;
	qboolean		sky;
} globalFog_t;

static ID_INLINE void R_GlobalFogClear( globalFog_t *fog )
{
	Com_Memset( fog, 0, sizeof( *fog ) );
	fog->mode = GLOBAL_FOG_EXP2;
	fog->opacity = 1.0f;
	fog->sky = qtrue;
}


static ID_INLINE qboolean R_GlobalFogWhitespace( unsigned char c )
{
	return ( c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
		c == '\v' || c == '\f' ) ? qtrue : qfalse;
}


/* Returns 1 for a token, 0 at EOF, and -1 for malformed input.  The explicit
 * end pointer prevents an embedded NUL or a missing terminator from widening
 * parsing beyond the FS_ReadFile byte count. */
static ID_INLINE int R_GlobalFogNextToken( const char **cursor, const char *end,
	char *token, int tokenSize )
{
	const char *p;
	const char *start;
	int length;

	if ( !cursor || !*cursor || !end || !token || tokenSize < 2 || *cursor > end ) {
		return -1;
	}

	p = *cursor;
	for ( ;; ) {
		while ( p < end && R_GlobalFogWhitespace( (unsigned char)*p ) ) {
			p++;
		}
		if ( p >= end ) {
			*cursor = p;
			token[0] = '\0';
			return 0;
		}
		if ( *p == '\0' ) {
			return -1;
		}
		if ( p + 1 < end && p[0] == '/' && p[1] == '/' ) {
			p += 2;
			while ( p < end && *p != '\n' && *p != '\r' ) {
				if ( *p == '\0' ) {
					return -1;
				}
				p++;
			}
			continue;
		}
		break;
	}

	start = p;
	while ( p < end && !R_GlobalFogWhitespace( (unsigned char)*p ) &&
		!( p + 1 < end && p[0] == '/' && p[1] == '/' ) ) {
		if ( *p == '\0' ) {
			return -1;
		}
		p++;
	}

	length = (int)( p - start );
	if ( length <= 0 || length >= tokenSize ) {
		return -1;
	}
	Com_Memcpy( token, start, length );
	token[length] = '\0';
	*cursor = p;
	return 1;
}


static ID_INLINE qboolean R_GlobalFogParseFloat( const char *token, float *out )
{
	char *end;
	double value;

	if ( !token || !token[0] || !out ) {
		return qfalse;
	}
	value = strtod( token, &end );
	if ( end == token || *end || value != value ||
		value > 3.402823466e+38 || value < -3.402823466e+38 ) {
		return qfalse;
	}
	*out = (float)value;
	return qtrue;
}


static ID_INLINE qboolean R_GlobalFogParseBoolean( const char *token, qboolean *out )
{
	if ( !token || !out ) {
		return qfalse;
	}
	if ( !Q_stricmp( token, "1" ) || !Q_stricmp( token, "true" ) ||
		!Q_stricmp( token, "yes" ) ) {
		*out = qtrue;
		return qtrue;
	}
	if ( !Q_stricmp( token, "0" ) || !Q_stricmp( token, "false" ) ||
		!Q_stricmp( token, "no" ) ) {
		*out = qfalse;
		return qtrue;
	}
	return qfalse;
}


static ID_INLINE void R_GlobalFogSetError( char *error, int errorSize,
	const char *message )
{
	if ( error && errorSize > 0 ) {
		Q_strncpyz( error, message, errorSize );
	}
}


/*
====================
R_GlobalFogParse

Grammar (one keyword followed by its values; ASCII whitespace and // comments
are accepted):

  color <red> <green> <blue>    normalized RGB, each in [0, 1]
  mode <exp|exp2|linear>        defaults to exp2
  density <value>               required, in world-unit^-1
  start <world units>           optional, defaults to zero
  end <world units>             required for linear mode
  opacity <value>               optional maximum blend amount, defaults to 1
  sky <0|1>                     whether clear-depth sky pixels receive fog
====================
*/
static ID_INLINE qboolean R_GlobalFogParse( globalFog_t *fog, const char *text,
	int textLength, char *error, int errorSize )
{
	const char *cursor;
	const char *end;
	char token[GLOBAL_FOG_TOKEN_MAX_BYTES];
	qboolean colorSeen = qfalse;
	qboolean densitySeen = qfalse;
	qboolean modeSeen = qfalse;
	qboolean startSeen = qfalse;
	qboolean endSeen = qfalse;
	qboolean opacitySeen = qfalse;
	qboolean skySeen = qfalse;
	int tokenResult;

	if ( !fog ) {
		R_GlobalFogSetError( error, errorSize, "missing output" );
		return qfalse;
	}
	R_GlobalFogClear( fog );
	if ( !text || textLength <= 0 || textLength > GLOBAL_FOG_SIDECAR_MAX_BYTES ) {
		R_GlobalFogSetError( error, errorSize, "empty or oversized file" );
		return qfalse;
	}

	cursor = text;
	end = text + textLength;
	while ( ( tokenResult = R_GlobalFogNextToken( &cursor, end, token,
		sizeof( token ) ) ) > 0 ) {
		if ( !Q_stricmp( token, "color" ) ) {
			int i;
			if ( colorSeen ) {
				R_GlobalFogSetError( error, errorSize, "duplicate color directive" );
				return qfalse;
			}
			for ( i = 0; i < 3; i++ ) {
				if ( R_GlobalFogNextToken( &cursor, end, token, sizeof( token ) ) != 1 ||
					!R_GlobalFogParseFloat( token, &fog->color[i] ) ||
					fog->color[i] < 0.0f || fog->color[i] > 1.0f ) {
					R_GlobalFogSetError( error, errorSize,
						"color must contain three normalized values" );
					return qfalse;
				}
			}
			colorSeen = qtrue;
		} else if ( !Q_stricmp( token, "mode" ) ) {
			if ( modeSeen ) {
				R_GlobalFogSetError( error, errorSize, "duplicate mode directive" );
				return qfalse;
			}
			if ( R_GlobalFogNextToken( &cursor, end, token, sizeof( token ) ) != 1 ) {
				R_GlobalFogSetError( error, errorSize, "mode requires a value" );
				return qfalse;
			}
			if ( !Q_stricmp( token, "exp" ) ) {
				fog->mode = GLOBAL_FOG_EXP;
			} else if ( !Q_stricmp( token, "exp2" ) ) {
				fog->mode = GLOBAL_FOG_EXP2;
			} else if ( !Q_stricmp( token, "linear" ) ) {
				fog->mode = GLOBAL_FOG_LINEAR;
			} else {
				R_GlobalFogSetError( error, errorSize, "mode must be exp, exp2, or linear" );
				return qfalse;
			}
			modeSeen = qtrue;
		} else if ( !Q_stricmp( token, "density" ) ) {
			if ( densitySeen ) {
				R_GlobalFogSetError( error, errorSize, "duplicate density directive" );
				return qfalse;
			}
			if ( R_GlobalFogNextToken( &cursor, end, token, sizeof( token ) ) != 1 ||
				!R_GlobalFogParseFloat( token, &fog->density ) ||
				fog->density <= 0.0f || fog->density > 0.1f ) {
				R_GlobalFogSetError( error, errorSize,
					"density must be greater than zero and no greater than 0.1" );
				return qfalse;
			}
			densitySeen = qtrue;
		} else if ( !Q_stricmp( token, "start" ) ) {
			if ( startSeen ) {
				R_GlobalFogSetError( error, errorSize, "duplicate start directive" );
				return qfalse;
			}
			if ( R_GlobalFogNextToken( &cursor, end, token, sizeof( token ) ) != 1 ||
				!R_GlobalFogParseFloat( token, &fog->start ) || fog->start < 0.0f ) {
				R_GlobalFogSetError( error, errorSize, "start must be a non-negative distance" );
				return qfalse;
			}
			startSeen = qtrue;
		} else if ( !Q_stricmp( token, "end" ) ) {
			if ( endSeen ) {
				R_GlobalFogSetError( error, errorSize, "duplicate end directive" );
				return qfalse;
			}
			if ( R_GlobalFogNextToken( &cursor, end, token, sizeof( token ) ) != 1 ||
				!R_GlobalFogParseFloat( token, &fog->end ) || fog->end <= 0.0f ) {
				R_GlobalFogSetError( error, errorSize, "end must be a positive distance" );
				return qfalse;
			}
			endSeen = qtrue;
		} else if ( !Q_stricmp( token, "opacity" ) ) {
			if ( opacitySeen ) {
				R_GlobalFogSetError( error, errorSize, "duplicate opacity directive" );
				return qfalse;
			}
			if ( R_GlobalFogNextToken( &cursor, end, token, sizeof( token ) ) != 1 ||
				!R_GlobalFogParseFloat( token, &fog->opacity ) ||
				fog->opacity < 0.0f || fog->opacity > 1.0f ) {
				R_GlobalFogSetError( error, errorSize, "opacity must be in [0, 1]" );
				return qfalse;
			}
			opacitySeen = qtrue;
		} else if ( !Q_stricmp( token, "sky" ) ) {
			if ( skySeen ) {
				R_GlobalFogSetError( error, errorSize, "duplicate sky directive" );
				return qfalse;
			}
			if ( R_GlobalFogNextToken( &cursor, end, token, sizeof( token ) ) != 1 ||
				!R_GlobalFogParseBoolean( token, &fog->sky ) ) {
				R_GlobalFogSetError( error, errorSize,
					"sky must be 0/1, true/false, or yes/no" );
				return qfalse;
			}
			skySeen = qtrue;
		} else {
			if ( error && errorSize > 0 ) {
				Com_sprintf( error, errorSize, "unknown directive '%s'", token );
			}
			return qfalse;
		}
	}

	if ( tokenResult < 0 ) {
		R_GlobalFogSetError( error, errorSize, "malformed or overlong token" );
		return qfalse;
	}
	if ( !colorSeen || !densitySeen ) {
		R_GlobalFogSetError( error, errorSize, "color and density are required" );
		return qfalse;
	}
	if ( fog->mode == GLOBAL_FOG_LINEAR && ( !endSeen || fog->end <= fog->start ) ) {
		R_GlobalFogSetError( error, errorSize, "linear fog requires end greater than start" );
		return qfalse;
	}

	fog->loaded = qtrue;
	return qtrue;
}

#endif // TR_GLOBAL_FOG_H
