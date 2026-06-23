extern "C" {
#include "client.h"

#if defined( __clang__ )
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined( __GNUC__ )
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define JSON_IMPLEMENTATION
#include "../qcommon/json.h"
#undef JSON_IMPLEMENTATION
#if defined( __clang__ )
#pragma clang diagnostic pop
#elif defined( __GNUC__ )
#pragma GCC diagnostic pop
#endif
}

#include "client_cpp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

using fnql::FileWrite;
using fnql::ScopedFileHandle;
using fnql::ScopedReadFile;

namespace {

constexpr char kHudScriptFile[] = "fnql-hud.json";
constexpr char kHudDumpFile[] = "fnql-hud-dump.json";

constexpr int kHudMaxRules = 256;
constexpr int kHudMaxShaderNames = 1024;
constexpr int kHudMaxFrameDraws = 4096;
constexpr int kHudMaxFrameGroups = 512;
constexpr int kHudMaxDumpGroups = 1024;

enum class HudTransformMode {
	Stretch,
	Uniform
};

enum class HudAlignX {
	Left,
	Center,
	Right
};

enum class HudAlignY {
	Top,
	Middle,
	Bottom
};

struct HudShaderName {
	qhandle_t handle = 0;
	std::array<char, MAX_QPATH> name{};
};

struct HudRule {
	std::array<char, 64> name{};
	bool hasShader = false;
	std::array<char, MAX_QPATH> shader{};
	bool hasTextLike = false;
	bool textLike = false;
	bool hasRefdef = false;
	bool refdef = false;
	bool hasRegion = false;
	float regionX = 0.0f;
	float regionY = 0.0f;
	float regionW = 0.0f;
	float regionH = 0.0f;
	bool hasAlignX = false;
	HudAlignX alignX = HudAlignX::Center;
	bool hasAlignY = false;
	HudAlignY alignY = HudAlignY::Middle;
	HudTransformMode mode = HudTransformMode::Uniform;
};

struct HudDrawCapture {
	float x = 0.0f;
	float y = 0.0f;
	float w = 0.0f;
	float h = 0.0f;
	float s1 = 0.0f;
	float t1 = 0.0f;
	float s2 = 0.0f;
	float t2 = 0.0f;
	qhandle_t shader = 0;
	std::array<char, MAX_QPATH> shaderName{};
	bool textLike = false;
	bool refdef = false;
};

struct HudDumpGroup {
	float x1 = 0.0f;
	float y1 = 0.0f;
	float x2 = 0.0f;
	float y2 = 0.0f;
	std::array<char, MAX_QPATH> shaderName{};
	bool textLike = false;
	bool refdef = false;
	int drawCount = 0;
	int samples = 0;
	bool likelyAligned = false;
	HudTransformMode mode = HudTransformMode::Uniform;
	HudAlignX alignX = HudAlignX::Center;
	HudAlignY alignY = HudAlignY::Middle;
};

std::array<HudShaderName, kHudMaxShaderNames> hudShaderNames{};
int hudNumShaderNames = 0;

std::array<HudRule, kHudMaxRules> hudRules{};
int hudNumRules = 0;
bool hudScriptLoaded = false;
bool hudScriptWarned = false;
int hudLastAspectMode = 0;

bool hudFrameActive = false;
std::array<HudDrawCapture, kHudMaxFrameDraws> hudFrameDraws{};
int hudFrameDrawCount = 0;

std::array<HudDumpGroup, kHudMaxDumpGroups> hudDumpGroups{};
int hudDumpGroupCount = 0;
bool hudDumpDirty = false;
int hudDumpPrevValue = 0;

const HudRule *CL_HudFindRule( const HudDrawCapture *draw );

qboolean QBool( bool value ) {
	return value ? qtrue : qfalse;
}

const char *CL_HudAlignXName( HudAlignX align ) {
	switch ( align ) {
	case HudAlignX::Left:
		return "left";
	case HudAlignX::Right:
		return "right";
	default:
		return "center";
	}
}

const char *CL_HudAlignYName( HudAlignY align ) {
	switch ( align ) {
	case HudAlignY::Top:
		return "top";
	case HudAlignY::Bottom:
		return "bottom";
	default:
		return "center";
	}
}

qboolean CL_HudParseAlignX( const char *value, HudAlignX *align ) {
	if ( !value || !*value ) {
		return qfalse;
	}

	if ( !Q_stricmp( value, "left" ) ) {
		*align = HudAlignX::Left;
		return qtrue;
	}

	if ( !Q_stricmp( value, "right" ) ) {
		*align = HudAlignX::Right;
		return qtrue;
	}

	if ( !Q_stricmp( value, "center" ) || !Q_stricmp( value, "middle" ) ) {
		*align = HudAlignX::Center;
		return qtrue;
	}

	return qfalse;
}

qboolean CL_HudParseAlignY( const char *value, HudAlignY *align ) {
	if ( !value || !*value ) {
		return qfalse;
	}

	if ( !Q_stricmp( value, "top" ) ) {
		*align = HudAlignY::Top;
		return qtrue;
	}

	if ( !Q_stricmp( value, "bottom" ) ) {
		*align = HudAlignY::Bottom;
		return qtrue;
	}

	if ( !Q_stricmp( value, "center" ) || !Q_stricmp( value, "middle" ) ) {
		*align = HudAlignY::Middle;
		return qtrue;
	}

	return qfalse;
}

qboolean CL_HudParseMode( const char *value, HudTransformMode *mode ) {
	if ( !value || !*value ) {
		return qfalse;
	}

	if ( !Q_stricmp( value, "stretch" ) ) {
		*mode = HudTransformMode::Stretch;
		return qtrue;
	}

	if ( !Q_stricmp( value, "uniform" ) ) {
		*mode = HudTransformMode::Uniform;
		return qtrue;
	}

	return qfalse;
}

qboolean CL_HudShaderMatches( const char *pattern, const char *shaderName ) {
	if ( !pattern || !*pattern || !shaderName ) {
		return qfalse;
	}

	const char *wildcard = std::strchr( pattern, '*' );
	if ( !wildcard || wildcard[1] != '\0' ) {
		return QBool( !Q_stricmp( pattern, shaderName ) );
	}

	return QBool( !Q_stricmpn( pattern, shaderName, static_cast<int>( wildcard - pattern ) ) );
}

void CL_HudJsonWriteString( fileHandle_t file, const char *text ) {
	FileWrite( file, "\"", 1 );

	for ( const char *s = text; s && *s; s++ ) {
		switch ( *s ) {
		case '\\':
		case '"': {
			const char ch = '\\';
			FileWrite( file, &ch, 1 );
			FileWrite( file, s, 1 );
			break;
		}
		case '\n':
			FileWrite( file, "\\n", 2 );
			break;
		case '\r':
			FileWrite( file, "\\r", 2 );
			break;
		case '\t':
			FileWrite( file, "\\t", 2 );
			break;
		default:
			FileWrite( file, s, 1 );
			break;
		}
	}

	FileWrite( file, "\"", 1 );
}

void CL_HudClearRules() {
	hudNumRules = 0;
	hudScriptLoaded = false;
}

qboolean CL_HudLoadRules( qboolean verbose ) {
	CL_HudClearRules();

	ScopedReadFile scriptFile = ScopedReadFile::Read( kHudScriptFile );
	if ( !scriptFile ) {
		hudScriptLoaded = true;
		if ( verbose || ( !hudScriptWarned && cl_hudAspect && cl_hudAspect->integer > 0 ) ) {
			Com_Printf( S_COLOR_YELLOW "HUD: no script loaded from %s, using centered uniform placement.\n", kHudScriptFile );
			hudScriptWarned = true;
		}
		return qfalse;
	}

	const char *json = scriptFile.as<const char>();
	const char *jsonEnd = json + scriptFile.length();
	const char *rulesJson = nullptr;

	if ( JSON_ValueGetType( json, jsonEnd ) == JSONTYPE_OBJECT ) {
		rulesJson = JSON_ObjectGetNamedValue( json, jsonEnd, "rules" );
	} else if ( JSON_ValueGetType( json, jsonEnd ) == JSONTYPE_ARRAY ) {
		rulesJson = json;
	}

	if ( !rulesJson || JSON_ValueGetType( rulesJson, jsonEnd ) != JSONTYPE_ARRAY ) {
		hudScriptLoaded = true;
		if ( verbose || !hudScriptWarned ) {
			Com_Printf( S_COLOR_YELLOW "HUD: invalid script format in %s.\n", kHudScriptFile );
			hudScriptWarned = true;
		}
		return qfalse;
	}

	for ( const char *ruleJson = JSON_ArrayGetFirstValue( rulesJson, jsonEnd );
		ruleJson && hudNumRules < kHudMaxRules;
		ruleJson = JSON_ArrayGetNextValue( ruleJson, jsonEnd ) ) {
		const char *valueJson;
		const char *matchJson;
		const char *regionJson;
		const char *alignJson;
		std::array<char, 128> text;

		if ( JSON_ValueGetType( ruleJson, jsonEnd ) != JSONTYPE_OBJECT ) {
			continue;
		}

		HudRule *rule = &hudRules[hudNumRules];
		*rule = HudRule{};

		valueJson = JSON_ObjectGetNamedValue( ruleJson, jsonEnd, "name" );
		if ( valueJson ) {
			JSON_ValueGetString( valueJson, jsonEnd, rule->name.data(), static_cast<int>( rule->name.size() ) );
		}

		valueJson = JSON_ObjectGetNamedValue( ruleJson, jsonEnd, "mode" );
		if ( valueJson && JSON_ValueGetString( valueJson, jsonEnd, text.data(), static_cast<int>( text.size() ) ) ) {
			CL_HudParseMode( text.data(), &rule->mode );
		}

		alignJson = JSON_ObjectGetNamedValue( ruleJson, jsonEnd, "align" );
		if ( alignJson && JSON_ValueGetType( alignJson, jsonEnd ) == JSONTYPE_OBJECT ) {
			valueJson = JSON_ObjectGetNamedValue( alignJson, jsonEnd, "x" );
			if ( valueJson && JSON_ValueGetString( valueJson, jsonEnd, text.data(), static_cast<int>( text.size() ) ) ) {
				rule->hasAlignX = CL_HudParseAlignX( text.data(), &rule->alignX );
			}

			valueJson = JSON_ObjectGetNamedValue( alignJson, jsonEnd, "y" );
			if ( valueJson && JSON_ValueGetString( valueJson, jsonEnd, text.data(), static_cast<int>( text.size() ) ) ) {
				rule->hasAlignY = CL_HudParseAlignY( text.data(), &rule->alignY );
			}
		}

		matchJson = JSON_ObjectGetNamedValue( ruleJson, jsonEnd, "match" );
		if ( matchJson && JSON_ValueGetType( matchJson, jsonEnd ) == JSONTYPE_OBJECT ) {
			valueJson = JSON_ObjectGetNamedValue( matchJson, jsonEnd, "shader" );
			if ( valueJson && JSON_ValueGetString( valueJson, jsonEnd, rule->shader.data(), static_cast<int>( rule->shader.size() ) ) ) {
				rule->hasShader = true;
			}

			valueJson = JSON_ObjectGetNamedValue( matchJson, jsonEnd, "textLike" );
			if ( valueJson ) {
				rule->hasTextLike = true;
				rule->textLike = JSON_ValueGetInt( valueJson, jsonEnd ) != 0;
			}

			valueJson = JSON_ObjectGetNamedValue( matchJson, jsonEnd, "refdef" );
			if ( valueJson ) {
				rule->hasRefdef = true;
				rule->refdef = JSON_ValueGetInt( valueJson, jsonEnd ) != 0;
			}

			regionJson = JSON_ObjectGetNamedValue( matchJson, jsonEnd, "region" );
			if ( regionJson && JSON_ValueGetType( regionJson, jsonEnd ) == JSONTYPE_OBJECT ) {
				rule->regionX = JSON_ValueGetFloat( JSON_ObjectGetNamedValue( regionJson, jsonEnd, "x" ), jsonEnd );
				rule->regionY = JSON_ValueGetFloat( JSON_ObjectGetNamedValue( regionJson, jsonEnd, "y" ), jsonEnd );
				rule->regionW = JSON_ValueGetFloat( JSON_ObjectGetNamedValue( regionJson, jsonEnd, "w" ), jsonEnd );
				rule->regionH = JSON_ValueGetFloat( JSON_ObjectGetNamedValue( regionJson, jsonEnd, "h" ), jsonEnd );
				rule->hasRegion = true;
			}
		}

		hudNumRules++;
	}

	hudScriptLoaded = true;
	hudScriptWarned = false;
	if ( verbose ) {
		Com_Printf( "HUD: loaded %i rule%s from %s.\n", hudNumRules, hudNumRules == 1 ? "" : "s", kHudScriptFile );
	}
	return qtrue;
}

void CL_HudEnsureRules() {
	if ( !cl_hudAspect || cl_hudAspect->integer <= 0 ) {
		return;
	}

	if ( hudLastAspectMode != cl_hudAspect->integer ) {
		hudLastAspectMode = cl_hudAspect->integer;
		hudScriptLoaded = false;
		hudScriptWarned = false;
	}

	if ( !hudScriptLoaded ) {
		CL_HudLoadRules( qfalse );
	}
}

void HudReload() {
	hudLastAspectMode = -1;
	hudScriptWarned = false;
	CL_HudLoadRules( qtrue );
}

const char *CL_HudLookupShaderName( qhandle_t shader ) {
	if ( shader == cls.charSetShader ) {
		return "charset";
	}

	if ( shader == cls.whiteShader ) {
		return "white";
	}

	if ( shader == cls.consoleShader ) {
		return "console";
	}

	for ( int i = 0; i < hudNumShaderNames; i++ ) {
		if ( hudShaderNames[i].handle == shader ) {
			return hudShaderNames[i].name.data();
		}
	}

	return "";
}

qboolean CL_HudIsTextLike( qhandle_t shader, const char *shaderName, float w, float h ) {
	if ( shader == cls.charSetShader ) {
		return qtrue;
	}

	if ( shaderName && *shaderName && !Q_stricmp( shaderName, "charset" ) ) {
		return qtrue;
	}

	return QBool( w <= BIGCHAR_WIDTH * 2.0f && h <= GIANTCHAR_HEIGHT * 1.5f );
}

qboolean CL_HudIsFullscreenLike( float x, float y, float w, float h ) {
	return QBool( x <= 1.0f && y <= 1.0f && w >= SCREEN_WIDTH - 1.0f && h >= SCREEN_HEIGHT - 1.0f );
}

void CL_HudApplyStretch( float *x, float *y, float *w, float *h ) {
	const float xscale = cls.glconfig.vidWidth / 640.0f;
	const float yscale = cls.glconfig.vidHeight / 480.0f;

	*x *= xscale;
	*y *= yscale;
	*w *= xscale;
	*h *= yscale;
}

void CL_HudUnstretch( float x, float y, float w, float h, float *outX, float *outY, float *outW, float *outH ) {
	const float xscale = cls.glconfig.vidWidth / 640.0f;
	const float yscale = cls.glconfig.vidHeight / 480.0f;

	if ( xscale == 0.0f || yscale == 0.0f ) {
		*outX = x;
		*outY = y;
		*outW = w;
		*outH = h;
		return;
	}

	*outX = x / xscale;
	*outY = y / yscale;
	*outW = w / xscale;
	*outH = h / yscale;
}

void CL_HudApplyUniform( float *x, float *y, float *w, float *h, HudAlignX alignX, HudAlignY alignY ) {
	float originX;
	float originY;

	switch ( alignX ) {
	case HudAlignX::Left:
		originX = 0.0f;
		break;
	case HudAlignX::Right:
		originX = cls.biasX * 2.0f;
		break;
	default:
		originX = cls.biasX;
		break;
	}

	switch ( alignY ) {
	case HudAlignY::Top:
		originY = 0.0f;
		break;
	case HudAlignY::Bottom:
		originY = cls.biasY * 2.0f;
		break;
	default:
		originY = cls.biasY;
		break;
	}

	*x = *x * cls.scale + originX;
	*y = *y * cls.scale + originY;
	*w *= cls.scale;
	*h *= cls.scale;
}

void CL_HudTransformRect( const HudDrawCapture *draw, float *x, float *y, float *w, float *h ) {
	HudTransformMode mode = CL_HudIsFullscreenLike( draw->x, draw->y, draw->w, draw->h ) ? HudTransformMode::Stretch : HudTransformMode::Uniform;
	HudAlignX alignX = HudAlignX::Center;
	HudAlignY alignY = HudAlignY::Middle;

	if ( hudScriptLoaded && hudNumRules > 0 ) {
		const HudRule *rule = CL_HudFindRule( draw );
		if ( rule ) {
			mode = rule->mode;
			if ( rule->hasAlignX ) {
				alignX = rule->alignX;
			}
			if ( rule->hasAlignY ) {
				alignY = rule->alignY;
			}
		}
	}

	*x = draw->x;
	*y = draw->y;
	*w = draw->w;
	*h = draw->h;

	if ( mode == HudTransformMode::Stretch ) {
		CL_HudApplyStretch( x, y, w, h );
	} else {
		CL_HudApplyUniform( x, y, w, h, alignX, alignY );
	}
}

const HudRule *CL_HudFindRule( const HudDrawCapture *draw ) {
	for ( int i = 0; i < hudNumRules; i++ ) {
		const HudRule *rule = &hudRules[i];
		const float cx = draw->x + draw->w * 0.5f;
		const float cy = draw->y + draw->h * 0.5f;

		if ( rule->hasShader && !CL_HudShaderMatches( rule->shader.data(), draw->shaderName.data() ) ) {
			continue;
		}

		if ( rule->hasTextLike && rule->textLike != draw->textLike ) {
			continue;
		}

		if ( rule->hasRefdef && rule->refdef != draw->refdef ) {
			continue;
		}

		if ( rule->hasRegion ) {
			if ( cx < rule->regionX || cy < rule->regionY ||
				cx > rule->regionX + rule->regionW || cy > rule->regionY + rule->regionH ) {
				continue;
			}
		}

		return rule;
	}

	return nullptr;
}

void CL_HudCaptureDraw( const HudDrawCapture *draw ) {
	if ( !hudFrameActive || !cl_hudDump || !cl_hudDump->integer ) {
		return;
	}

	if ( hudFrameDrawCount >= kHudMaxFrameDraws ) {
		return;
	}

	hudFrameDraws[hudFrameDrawCount++] = *draw;
}

bool CL_HudDrawLess( const HudDrawCapture &a, const HudDrawCapture &b ) {
	if ( a.y < b.y ) {
		return true;
	}
	if ( a.y > b.y ) {
		return false;
	}
	if ( a.x < b.x ) {
		return true;
	}
	if ( a.x > b.x ) {
		return false;
	}
	return Q_stricmp( a.shaderName.data(), b.shaderName.data() ) < 0;
}

qboolean CL_HudCanMergeGroup( const HudDrawCapture *draw, const HudDumpGroup *group ) {
	const float gapX = draw->textLike ? 24.0f : 8.0f;
	const float gapY = draw->textLike ? 6.0f : 8.0f;
	const float drawX2 = draw->x + draw->w;
	const float drawY2 = draw->y + draw->h;

	if ( draw->textLike != group->textLike ) {
		return qfalse;
	}

	if ( draw->refdef != group->refdef ) {
		return qfalse;
	}

	if ( Q_stricmp( draw->shaderName.data(), group->shaderName.data() ) ) {
		return qfalse;
	}

	if ( drawY2 < group->y1 - gapY || draw->y > group->y2 + gapY ) {
		return qfalse;
	}

	if ( drawX2 < group->x1 - gapX || draw->x > group->x2 + gapX ) {
		return qfalse;
	}

	return qtrue;
}

void CL_HudSuggestPlacement( HudDumpGroup *group ) {
	const float width = group->x2 - group->x1;
	const float height = group->y2 - group->y1;
	const float centerX = group->x1 + width * 0.5f;
	const float centerY = group->y1 + height * 0.5f;

	if ( CL_HudIsFullscreenLike( group->x1, group->y1, width, height ) ) {
		group->mode = HudTransformMode::Stretch;
		group->alignX = HudAlignX::Center;
		group->alignY = HudAlignY::Middle;
		group->likelyAligned = false;
		return;
	}

	group->mode = HudTransformMode::Uniform;

	if ( group->x1 <= 96.0f || centerX <= 160.0f ) {
		group->alignX = HudAlignX::Left;
	} else if ( group->x2 >= SCREEN_WIDTH - 96.0f || centerX >= SCREEN_WIDTH - 160.0f ) {
		group->alignX = HudAlignX::Right;
	} else {
		group->alignX = HudAlignX::Center;
	}

	if ( group->y1 <= 72.0f || centerY <= 120.0f ) {
		group->alignY = HudAlignY::Top;
	} else if ( group->y2 >= SCREEN_HEIGHT - 72.0f || centerY >= SCREEN_HEIGHT - 120.0f ) {
		group->alignY = HudAlignY::Bottom;
	} else {
		group->alignY = HudAlignY::Middle;
	}

	group->likelyAligned = group->alignX != HudAlignX::Center || group->alignY != HudAlignY::Middle;
}

qboolean CL_HudSameDumpGroup( const HudDumpGroup *left, const HudDumpGroup *right ) {
	const float leftW = left->x2 - left->x1;
	const float leftH = left->y2 - left->y1;
	const float rightW = right->x2 - right->x1;
	const float rightH = right->y2 - right->y1;
	const float leftCx = left->x1 + leftW * 0.5f;
	const float leftCy = left->y1 + leftH * 0.5f;
	const float rightCx = right->x1 + rightW * 0.5f;
	const float rightCy = right->y1 + rightH * 0.5f;

	if ( left->textLike != right->textLike ) {
		return qfalse;
	}

	if ( left->refdef != right->refdef ) {
		return qfalse;
	}

	if ( left->mode != right->mode || left->alignX != right->alignX || left->alignY != right->alignY ) {
		return qfalse;
	}

	if ( Q_stricmp( left->shaderName.data(), right->shaderName.data() ) ) {
		return qfalse;
	}

	if ( std::fabs( leftCx - rightCx ) > 24.0f || std::fabs( leftCy - rightCy ) > 24.0f ) {
		return qfalse;
	}

	if ( std::fabs( leftW - rightW ) > 96.0f || std::fabs( leftH - rightH ) > 48.0f ) {
		return qfalse;
	}

	return qtrue;
}

void CL_HudAccumulateDumpGroup( const HudDumpGroup *group ) {
	for ( int i = 0; i < hudDumpGroupCount; i++ ) {
		HudDumpGroup *existing = &hudDumpGroups[i];

		if ( !CL_HudSameDumpGroup( existing, group ) ) {
			continue;
		}

		if ( group->x1 < existing->x1 ) {
			existing->x1 = group->x1;
		}
		if ( group->y1 < existing->y1 ) {
			existing->y1 = group->y1;
		}
		if ( group->x2 > existing->x2 ) {
			existing->x2 = group->x2;
		}
		if ( group->y2 > existing->y2 ) {
			existing->y2 = group->y2;
		}

		existing->drawCount += group->drawCount;
		existing->samples++;
		hudDumpDirty = true;
		return;
	}

	if ( hudDumpGroupCount >= kHudMaxDumpGroups ) {
		return;
	}

	hudDumpGroups[hudDumpGroupCount] = *group;
	hudDumpGroups[hudDumpGroupCount].samples = 1;
	hudDumpGroupCount++;
	hudDumpDirty = true;
}

void CL_HudWriteDumpFile() {
	if ( !hudDumpDirty ) {
		return;
	}

	ScopedFileHandle file( FS_FOpenFileWrite( kHudDumpFile ) );
	if ( !file ) {
		Com_Printf( S_COLOR_YELLOW "HUD: failed to write dump file %s.\n", kHudDumpFile );
		return;
	}
	const fileHandle_t dumpFile = file.get();

	FS_Printf( dumpFile, "{\n  \"version\": 1,\n  \"rules\": [\n" );

	for ( int i = 0; i < hudDumpGroupCount; i++ ) {
		const HudDumpGroup *group = &hudDumpGroups[i];

		if ( i ) {
			FS_Printf( dumpFile, ",\n" );
		}

		FS_Printf( dumpFile, "    {\n" );
		FS_Printf( dumpFile, "      \"name\": " );
		CL_HudJsonWriteString( dumpFile, va( "hud_%04i", i ) );
		FS_Printf( dumpFile, ",\n      \"match\": {\n" );

		if ( group->shaderName[0] ) {
			FS_Printf( dumpFile, "        \"shader\": " );
			CL_HudJsonWriteString( dumpFile, group->shaderName.data() );
			FS_Printf( dumpFile, ",\n" );
		}

		if ( group->refdef ) {
			FS_Printf( dumpFile, "        \"refdef\": 1,\n" );
		}

		FS_Printf( dumpFile, "        \"textLike\": %i,\n", group->textLike ? 1 : 0 );
		FS_Printf( dumpFile, "        \"region\": { \"x\": %.3f, \"y\": %.3f, \"w\": %.3f, \"h\": %.3f }\n",
			group->x1, group->y1, group->x2 - group->x1, group->y2 - group->y1 );
		FS_Printf( dumpFile, "      },\n" );

		FS_Printf( dumpFile, "      \"mode\": " );
		CL_HudJsonWriteString( dumpFile, group->mode == HudTransformMode::Stretch ? "stretch" : "uniform" );
		FS_Printf( dumpFile, ",\n      \"align\": {\n        \"x\": " );
		CL_HudJsonWriteString( dumpFile, CL_HudAlignXName( group->alignX ) );
		FS_Printf( dumpFile, ",\n        \"y\": " );
		CL_HudJsonWriteString( dumpFile, CL_HudAlignYName( group->alignY ) );
		FS_Printf( dumpFile, "\n      },\n" );
		FS_Printf( dumpFile, "      \"likelyAligned\": %i,\n", group->likelyAligned ? 1 : 0 );
		FS_Printf( dumpFile, "      \"samples\": %i,\n", group->samples );
		FS_Printf( dumpFile, "      \"drawCount\": %i\n", group->drawCount );
		FS_Printf( dumpFile, "    }" );
	}

	FS_Printf( dumpFile, "\n  ]\n}\n" );
	hudDumpDirty = false;
}

void CL_HudDumpFrame() {
	if ( hudFrameDrawCount <= 0 ) {
		return;
	}

	std::sort( hudFrameDraws.begin(), hudFrameDraws.begin() + hudFrameDrawCount, CL_HudDrawLess );

	std::array<HudDumpGroup, kHudMaxFrameGroups> frameGroups{};
	int frameGroupCount = 0;

	for ( int i = 0; i < hudFrameDrawCount; i++ ) {
		const HudDrawCapture *draw = &hudFrameDraws[i];
		const float drawX2 = draw->x + draw->w;
		const float drawY2 = draw->y + draw->h;
		bool merged = false;

		for ( int j = 0; j < frameGroupCount; j++ ) {
			HudDumpGroup *group = &frameGroups[j];

			if ( !CL_HudCanMergeGroup( draw, group ) ) {
				continue;
			}

			if ( draw->x < group->x1 ) {
				group->x1 = draw->x;
			}
			if ( draw->y < group->y1 ) {
				group->y1 = draw->y;
			}
			if ( drawX2 > group->x2 ) {
				group->x2 = drawX2;
			}
			if ( drawY2 > group->y2 ) {
				group->y2 = drawY2;
			}
			group->drawCount++;
			merged = true;
			break;
		}

		if ( merged || frameGroupCount >= kHudMaxFrameGroups ) {
			continue;
		}

		HudDumpGroup *group = &frameGroups[frameGroupCount];
		*group = HudDumpGroup{};
		group->x1 = draw->x;
		group->y1 = draw->y;
		group->x2 = drawX2;
		group->y2 = drawY2;
		Q_strncpyz( group->shaderName.data(), draw->shaderName.data(), static_cast<int>( group->shaderName.size() ) );
		group->textLike = draw->textLike;
		group->refdef = draw->refdef;
		group->drawCount = 1;
		frameGroupCount++;
	}

	for ( int i = 0; i < frameGroupCount; i++ ) {
		CL_HudSuggestPlacement( &frameGroups[i] );
		CL_HudAccumulateDumpGroup( &frameGroups[i] );
	}

	CL_HudWriteDumpFile();
}

} // namespace

extern "C" void CL_HudReload_f( void ) {
	HudReload();
}

extern "C" void CL_HudInit( void ) {
	hudLastAspectMode = -1;
	hudDumpPrevValue = cl_hudDump ? cl_hudDump->integer : 0;
	hudNumShaderNames = 0;
	hudNumRules = 0;
	hudScriptLoaded = false;
	hudScriptWarned = false;
	hudFrameActive = false;
	hudFrameDrawCount = 0;
	hudDumpGroupCount = 0;
	hudDumpDirty = false;
	Cmd_AddCommand( "hud_reload", CL_HudReload_f );
}

extern "C" void CL_HudShutdown( void ) {
	Cmd_RemoveCommand( "hud_reload" );
}

extern "C" void CL_HudResetCGame( void ) {
	hudNumShaderNames = 0;
	hudFrameActive = false;
	hudFrameDrawCount = 0;
}

extern "C" void CL_HudBeginFrame( void ) {
	if ( cl_hudDump && cl_hudDump->integer != hudDumpPrevValue ) {
		if ( cl_hudDump->integer ) {
			hudDumpGroupCount = 0;
			hudDumpDirty = true;
		}
		hudDumpPrevValue = cl_hudDump->integer;
	}

	hudFrameActive = true;
	hudFrameDrawCount = 0;
	CL_HudEnsureRules();
}

extern "C" void CL_HudEndFrame( void ) {
	if ( hudFrameActive && cl_hudDump && cl_hudDump->integer ) {
		CL_HudDumpFrame();
	}

	hudFrameActive = false;
	hudFrameDrawCount = 0;
}

extern "C" void CL_HudRegisterShaderName( qhandle_t shader, const char *name ) {
	if ( shader <= 0 || !name || !*name ) {
		return;
	}

	for ( int i = 0; i < hudNumShaderNames; i++ ) {
		if ( hudShaderNames[i].handle == shader ) {
			Q_strncpyz( hudShaderNames[i].name.data(), name, static_cast<int>( hudShaderNames[i].name.size() ) );
			return;
		}
	}

	if ( hudNumShaderNames >= kHudMaxShaderNames ) {
		return;
	}

	hudShaderNames[hudNumShaderNames].handle = shader;
	Q_strncpyz( hudShaderNames[hudNumShaderNames].name.data(), name, static_cast<int>( hudShaderNames[hudNumShaderNames].name.size() ) );
	hudNumShaderNames++;
}

extern "C" void CL_HudAdjustRefdef( refdef_t *refdef ) {
	HudDrawCapture draw;
	float x;
	float y;
	float w;
	float h;

	if ( !refdef || ( refdef->rdflags & RDF_NOWORLDMODEL ) == 0 ) {
		return;
	}

	CL_HudUnstretch( static_cast<float>( refdef->x ), static_cast<float>( refdef->y ),
		static_cast<float>( refdef->width ), static_cast<float>( refdef->height ),
		&draw.x, &draw.y, &draw.w, &draw.h );
	draw.textLike = false;
	draw.refdef = true;

	CL_HudCaptureDraw( &draw );

	refdef->rdflags |= RDF_NOFOVCORRECTION;

	if ( !cl_hudAspect || cl_hudAspect->integer <= 0 ) {
		return;
	}

	CL_HudTransformRect( &draw, &x, &y, &w, &h );
	refdef->x = static_cast<int>( x + 0.5f );
	refdef->y = static_cast<int>( y + 0.5f );
	refdef->width = static_cast<int>( w + 0.5f );
	refdef->height = static_cast<int>( h + 0.5f );
}

extern "C" void CL_HudDrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t shader ) {
	HudDrawCapture draw;
	float pixelX = x;
	float pixelY = y;
	float pixelW = w;
	float pixelH = h;

	if ( cl_captureActive && cl_captureActive->integer && r_levelshotHideHud && r_levelshotHideHud->integer ) {
		return;
	}

	CL_HudUnstretch( pixelX, pixelY, pixelW, pixelH, &draw.x, &draw.y, &draw.w, &draw.h );
	draw.s1 = s1;
	draw.t1 = t1;
	draw.s2 = s2;
	draw.t2 = t2;
	draw.shader = shader;
	Q_strncpyz( draw.shaderName.data(), CL_HudLookupShaderName( shader ), static_cast<int>( draw.shaderName.size() ) );
	draw.textLike = CL_HudIsTextLike( shader, draw.shaderName.data(), w, h );

	CL_HudCaptureDraw( &draw );

	if ( !cl_hudAspect || cl_hudAspect->integer <= 0 ) {
		re.DrawStretchPic( pixelX, pixelY, pixelW, pixelH, s1, t1, s2, t2, shader );
		return;
	}

	CL_HudTransformRect( &draw, &x, &y, &w, &h );
	re.DrawStretchPic( x, y, w, h, s1, t1, s2, t2, shader );
}
