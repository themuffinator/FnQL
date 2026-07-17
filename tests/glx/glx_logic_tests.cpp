/*
===========================================================================
Copyright (C) 2026

This file is part of FnQL.

FnQL is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
===========================================================================
*/

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "glx_material_key.h"
#include "glx_caps_logic.h"
#include "glx_color_math.h"
#include "glx_post_output_reference.h"
#include "glx_post_shader_plan.h"
#include "glx_post_shader_source.h"
#include "glx_executor.h"
#include "glx_render_ir.h"
#include "glx_static_world_logic.h"
#include "glx_stream_logic.h"
#include "tr_cubemap.h"
#include "tr_lens_flare.h"
#include "tr_liquid.h"
#include "tr_motion_blur.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <limits>

namespace glx {

refimport_t *g_imports = nullptr;
int g_testDrawElementsCalls = 0;
int g_testDrawArraysCalls = 0;

refimport_t &RI()
{
	static refimport_t nullImports {};
	return g_imports ? *g_imports : nullImports;
}

qboolean ImportsReady()
{
	return g_imports ? qtrue : qfalse;
}

const char *BoolName( qboolean value )
{
	return value ? "yes" : "no";
}

qboolean ToQBool( bool value )
{
	return value ? qtrue : qfalse;
}

void GLX_Draw_Shutdown()
{
}

qboolean GLX_Draw_DrawElements( unsigned int mode, int count, unsigned int type,
	const void *indices )
{
	(void)mode;
	(void)count;
	(void)type;
	(void)indices;
	g_testDrawElementsCalls++;
	return qtrue;
}

qboolean GLX_Draw_DrawArrays( unsigned int mode, int first, int count )
{
	(void)mode;
	(void)first;
	(void)count;
	g_testDrawArraysCalls++;
	return qtrue;
}

void GLX_Test_ResetDrawStubs()
{
	g_testDrawElementsCalls = 0;
	g_testDrawArraysCalls = 0;
}

} // namespace glx

namespace {

bool Check( bool condition, const char *test, int line, const char *expression )
{
	if ( condition ) {
		return true;
	}

	std::fprintf( stderr, "%s:%d: check failed: %s\n", test, line, expression );
	return false;
}

#define CHECK( expression ) do { if ( !Check( ( expression ), __func__, __LINE__, #expression ) ) return false; } while ( 0 )

bool CubemapFaceAxesAreOrthonormalAndConsistent()
{
	const vec3_t baseAxis[3] = {
		{ 1.0f, 0.0f, 0.0f },
		{ 0.0f, 1.0f, 0.0f },
		{ 0.0f, 0.0f, 1.0f }
	};
	const vec3_t expected[6][3] = {
		{ {  1.0f,  0.0f,  0.0f }, {  0.0f,  1.0f, 0.0f }, {  0.0f, 0.0f, 1.0f } },
		{ { -1.0f,  0.0f,  0.0f }, {  0.0f, -1.0f, 0.0f }, {  0.0f, 0.0f, 1.0f } },
		{ {  0.0f,  1.0f,  0.0f }, { -1.0f,  0.0f, 0.0f }, {  0.0f, 0.0f, 1.0f } },
		{ {  0.0f, -1.0f,  0.0f }, {  1.0f,  0.0f, 0.0f }, {  0.0f, 0.0f, 1.0f } },
		{ {  0.0f,  0.0f,  1.0f }, {  0.0f,  1.0f, 0.0f }, { -1.0f, 0.0f, 0.0f } },
		{ {  0.0f,  0.0f, -1.0f }, {  0.0f,  1.0f, 0.0f }, {  1.0f, 0.0f, 0.0f } }
	};

	for ( int face = 0; face < 6; face++ ) {
		vec3_t axis[3];
		vec3_t cross;

		R_CubemapFaceAxis( baseAxis, face, axis );
		for ( int basis = 0; basis < 3; basis++ ) {
			for ( int component = 0; component < 3; component++ ) {
				CHECK( axis[basis][component] == expected[face][basis][component] );
			}
			CHECK( std::fabs( DotProduct( axis[basis], axis[basis] ) - 1.0f ) < 0.00001f );
		}
		CHECK( std::fabs( DotProduct( axis[0], axis[1] ) ) < 0.00001f );
		CHECK( std::fabs( DotProduct( axis[0], axis[2] ) ) < 0.00001f );
		CHECK( std::fabs( DotProduct( axis[1], axis[2] ) ) < 0.00001f );
		CrossProduct( axis[0], axis[1], cross );
		CHECK( VectorCompare( cross, axis[2] ) );
	}

	return true;
}

bool NearlyEqual( float lhs, float rhs, float epsilon )
{
	return std::fabs( lhs - rhs ) <= epsilon;
}

bool VecNearlyEqual( const glx::ColorMathVec3 &lhs, const glx::ColorMathVec3 &rhs,
	float epsilon )
{
	return NearlyEqual( lhs.r, rhs.r, epsilon ) &&
		NearlyEqual( lhs.g, rhs.g, epsilon ) &&
		NearlyEqual( lhs.b, rhs.b, epsilon );
}

glx::MaterialIR ProjectedDlightMdiTestMaterial()
{
	return glx::GLX_RenderIR_MakeMaterial(
		0, GLX_STAGE_DLIGHT_MAP, GLX_MATERIAL_STATE_DEPTHMASK_TRUE, 1 );
}

glx::DynamicDraw ProjectedDlightMdiTestDraw( uintptr_t indexOffsetBytes,
	int count = 6, int projectedRecordCount = 2 )
{
	glx::DynamicDraw draw {};

	draw.kind = glx::DynamicDrawKind::Indexed;
	draw.role = glx::DynamicDrawRole::DynamicLight;
	draw.pass = glx::FramePassKind::DynamicLights;
	draw.primitive = 0x0004u;
	draw.count = count;
	draw.indexType = 0x1403u;
	draw.indices = reinterpret_cast<const void *>( indexOffsetBytes );
	draw.legacyReason = GLX_LEGACY_DELEGATION_NONE;
	draw.profilerPath = GLX_DRAW_STREAM_GENERIC;
	draw.projectedDlights.firstRecord = 0;
	draw.projectedDlights.recordCount = projectedRecordCount;
	draw.material = ProjectedDlightMdiTestMaterial();
	draw.upload = glx::GLX_RenderIR_MakeUploadPlan(
		glx::UploadPlanKind::TransientStream,
		static_cast<int>( glx::StreamStrategy::PersistentMapped ), 160u, 96u, 48u );
	draw.upload.sync = glx::UploadSyncPolicy::PersistentFence;
	return draw;
}

bool MaterialKeysClassifyRcShapes()
{
	glx::MaterialProgramKey key {};

	CHECK( glx::GLX_Material_KeyForInputs( 0, 0, 0, 0, qfalse, &key ) == qtrue );
	CHECK( key.mode == glx::MaterialProgramMode::SingleTexture );
	CHECK( key.features == glx::GLX_MATERIAL_FEATURE_NONE );

	CHECK( glx::GLX_Material_KeyForInputs( GLX_STAGE_TEXMOD, 0, 2, 0, qfalse, &key ) == qtrue );
	CHECK( key.mode == glx::MaterialProgramMode::SingleTexture );
	CHECK( key.features == glx::GLX_MATERIAL_FEATURE_TEXMOD );

	CHECK( glx::GLX_Material_KeyForInputs( GLX_STAGE_ENVIRONMENT, 0, 0, 0, qfalse, &key ) == qtrue );
	CHECK( key.mode == glx::MaterialProgramMode::SingleTexture );
	CHECK( key.features == glx::GLX_MATERIAL_FEATURE_ENVIRONMENT );

	CHECK( glx::GLX_Material_KeyForInputs( GLX_STAGE_TEXMOD | GLX_STAGE_ENVIRONMENT,
		0, 1, 0, qfalse, &key ) == qtrue );
	CHECK( key.mode == glx::MaterialProgramMode::SingleTexture );
	CHECK( key.features == ( glx::GLX_MATERIAL_FEATURE_TEXMOD | glx::GLX_MATERIAL_FEATURE_ENVIRONMENT ) );

	CHECK( glx::GLX_Material_KeyForInputs( GLX_STAGE_DEPTH_FRAGMENT,
		0, 0, 0, qfalse, &key ) == qtrue );
	CHECK( key.mode == glx::MaterialProgramMode::SingleTexture );
	CHECK( key.features == glx::GLX_MATERIAL_FEATURE_DEPTH_FRAGMENT );

	CHECK( glx::GLX_Material_KeyForInputs( GLX_STAGE_DEPTH_FRAGMENT | GLX_STAGE_TEXMOD |
		GLX_STAGE_ENVIRONMENT, 0, 2, 0, qfalse, &key ) == qtrue );
	CHECK( key.mode == glx::MaterialProgramMode::SingleTexture );
	CHECK( key.features == ( glx::GLX_MATERIAL_FEATURE_DEPTH_FRAGMENT |
		glx::GLX_MATERIAL_FEATURE_TEXMOD | glx::GLX_MATERIAL_FEATURE_ENVIRONMENT ) );

	CHECK( glx::GLX_Material_KeyForInputs( GLX_STAGE_MULTITEXTURE,
		GLX_MATERIAL_COMBINE_MODULATE, 0, 0,
		qfalse, &key ) == qtrue );
	CHECK( key.mode == glx::MaterialProgramMode::MultiModulate );
	CHECK( key.features == glx::GLX_MATERIAL_FEATURE_NONE );

	CHECK( glx::GLX_Material_KeyForInputs( GLX_STAGE_MULTITEXTURE | GLX_STAGE_TEXMOD,
		GLX_MATERIAL_COMBINE_ADD, 0, 1, qfalse, &key ) == qtrue );
	CHECK( key.mode == glx::MaterialProgramMode::MultiAdd );
	CHECK( key.features == glx::GLX_MATERIAL_FEATURE_TEXMOD );

	CHECK( glx::GLX_Material_KeyForInputs( GLX_STAGE_MULTITEXTURE | GLX_STAGE_DEPTH_FRAGMENT,
		GLX_MATERIAL_COMBINE_MODULATE, 0, 0,
		qfalse, &key ) == qtrue );
	CHECK( key.mode == glx::MaterialProgramMode::MultiModulate );
	CHECK( key.features == glx::GLX_MATERIAL_FEATURE_DEPTH_FRAGMENT );

	CHECK( glx::GLX_Material_KeyForInputs( GLX_STAGE_MULTITEXTURE,
		GLX_MATERIAL_COMBINE_REPLACE, 0, 0,
		qfalse, &key ) == qtrue );
	CHECK( key.mode == glx::MaterialProgramMode::MultiReplace );

	CHECK( glx::GLX_Material_KeyForInputs( GLX_STAGE_MULTITEXTURE,
		GLX_MATERIAL_COMBINE_DECAL, 0, 0,
		qfalse, &key ) == qtrue );
	CHECK( key.mode == glx::MaterialProgramMode::MultiDecal );

	return true;
}

bool MaterialKeysRejectUnsupportedCombines()
{
	glx::MaterialProgramKey key {};

	CHECK( glx::GLX_Material_KeyForInputs( GLX_STAGE_MULTITEXTURE, 0x1234, 0, 0,
		qfalse, &key ) == qfalse );

	CHECK( glx::GLX_Material_KeyForInputs( GLX_STAGE_TEXMOD | GLX_STAGE_ENVIRONMENT,
		GLX_MATERIAL_COMBINE_ADD, 4, 4, qtrue, &key ) == qtrue );
	CHECK( key.mode == glx::MaterialProgramMode::Fog );
	CHECK( key.features == glx::GLX_MATERIAL_FEATURE_NONE );

	return true;
}

bool MaterialKeysTreatSpecialSceneFlagsAsGates()
{
	glx::MaterialProgramKey key {};

	CHECK( glx::GLX_Material_KeyForInputs( GLX_STAGE_DLIGHT_MAP | GLX_STAGE_SCREEN_MAP,
		0, 0, 0, qfalse, &key ) == qtrue );
	CHECK( key.mode == glx::MaterialProgramMode::SingleTexture );
	CHECK( key.features == glx::GLX_MATERIAL_FEATURE_NONE );

	CHECK( glx::GLX_Material_KeyForInputs( GLX_STAGE_VIDEO_MAP | GLX_STAGE_TEXMOD,
		0, 1, 0, qfalse, &key ) == qtrue );
	CHECK( key.mode == glx::MaterialProgramMode::SingleTexture );
	CHECK( key.features == glx::GLX_MATERIAL_FEATURE_TEXMOD );

	CHECK( glx::GLX_Material_KeyForInputs( GLX_STAGE_SHADOW_PASS | GLX_STAGE_BEAM_PASS |
		GLX_STAGE_POSTPROCESS_PASS,
		0, 0, 0, qfalse, &key ) == qtrue );
	CHECK( key.mode == glx::MaterialProgramMode::SingleTexture );
	CHECK( key.features == glx::GLX_MATERIAL_FEATURE_NONE );

	return true;
}

bool MaterialStageKeysCoverPreparedIdTech3StageLanguage()
{
	glx::MaterialStageKey key {};
	const int rgbGens[] = {
		GLX_MATERIAL_RGBGEN_BAD,
		GLX_MATERIAL_RGBGEN_IDENTITY_LIGHTING,
		GLX_MATERIAL_RGBGEN_IDENTITY,
		GLX_MATERIAL_RGBGEN_ENTITY,
		GLX_MATERIAL_RGBGEN_ONE_MINUS_ENTITY,
		GLX_MATERIAL_RGBGEN_EXACT_VERTEX,
		GLX_MATERIAL_RGBGEN_VERTEX,
		GLX_MATERIAL_RGBGEN_ONE_MINUS_VERTEX,
		GLX_MATERIAL_RGBGEN_WAVEFORM,
		GLX_MATERIAL_RGBGEN_LIGHTING_DIFFUSE,
		GLX_MATERIAL_RGBGEN_FOG,
		GLX_MATERIAL_RGBGEN_CONST
	};
	const int alphaGens[] = {
		GLX_MATERIAL_ALPHAGEN_IDENTITY,
		GLX_MATERIAL_ALPHAGEN_SKIP,
		GLX_MATERIAL_ALPHAGEN_ENTITY,
		GLX_MATERIAL_ALPHAGEN_ONE_MINUS_ENTITY,
		GLX_MATERIAL_ALPHAGEN_VERTEX,
		GLX_MATERIAL_ALPHAGEN_ONE_MINUS_VERTEX,
		GLX_MATERIAL_ALPHAGEN_LIGHTING_SPECULAR,
		GLX_MATERIAL_ALPHAGEN_WAVEFORM,
		GLX_MATERIAL_ALPHAGEN_PORTAL,
		GLX_MATERIAL_ALPHAGEN_CONST
	};
	const int tcGens[] = {
		GLX_MATERIAL_TCGEN_BAD,
		GLX_MATERIAL_TCGEN_IDENTITY,
		GLX_MATERIAL_TCGEN_LIGHTMAP,
		GLX_MATERIAL_TCGEN_TEXTURE,
		GLX_MATERIAL_TCGEN_ENVIRONMENT_MAPPED,
		GLX_MATERIAL_TCGEN_ENVIRONMENT_MAPPED_FP,
		GLX_MATERIAL_TCGEN_FOG,
		GLX_MATERIAL_TCGEN_VECTOR
	};
	const int waveFuncs[] = {
		GLX_MATERIAL_WAVEFUNC_NONE,
		GLX_MATERIAL_WAVEFUNC_SIN,
		GLX_MATERIAL_WAVEFUNC_SQUARE,
		GLX_MATERIAL_WAVEFUNC_TRIANGLE,
		GLX_MATERIAL_WAVEFUNC_SAWTOOTH,
		GLX_MATERIAL_WAVEFUNC_INVERSE_SAWTOOTH,
		GLX_MATERIAL_WAVEFUNC_NOISE
	};
	const unsigned int allTexMods = GLX_MATERIAL_TMOD_NONE_BIT |
		GLX_MATERIAL_TMOD_TRANSFORM_BIT |
		GLX_MATERIAL_TMOD_TURBULENT_BIT |
		GLX_MATERIAL_TMOD_SCROLL_BIT |
		GLX_MATERIAL_TMOD_SCALE_BIT |
		GLX_MATERIAL_TMOD_STRETCH_BIT |
		GLX_MATERIAL_TMOD_ROTATE_BIT |
		GLX_MATERIAL_TMOD_ENTITY_TRANSLATE_BIT |
		GLX_MATERIAL_TMOD_OFFSET_BIT |
		GLX_MATERIAL_TMOD_SCALE_OFFSET_BIT |
		GLX_MATERIAL_TMOD_OFFSET_SCALE_BIT;
	unsigned int fiveTexModSequence = 0;
	unsigned int fiveTexModMask;

	fiveTexModSequence = glx::GLX_Material_TexModSequenceSetSlot( fiveTexModSequence, 0,
		GLX_MATERIAL_TMOD_OPCODE_TRANSFORM );
	fiveTexModSequence = glx::GLX_Material_TexModSequenceSetSlot( fiveTexModSequence, 1,
		GLX_MATERIAL_TMOD_OPCODE_TURBULENT );
	fiveTexModSequence = glx::GLX_Material_TexModSequenceSetSlot( fiveTexModSequence, 2,
		GLX_MATERIAL_TMOD_OPCODE_SCROLL );
	fiveTexModSequence = glx::GLX_Material_TexModSequenceSetSlot( fiveTexModSequence, 3,
		GLX_MATERIAL_TMOD_OPCODE_SCALE );
	fiveTexModSequence = glx::GLX_Material_TexModSequenceSetSlot( fiveTexModSequence, 4,
		GLX_MATERIAL_TMOD_OPCODE_ROTATE );
	fiveTexModMask = glx::GLX_Material_TexModSequenceMask(
		glx::GLX_MATERIAL_MAX_TEXMODS_PER_BUNDLE, fiveTexModSequence );

	for ( int rgbGen : rgbGens ) {
		CHECK( glx::GLX_Material_StageKeyForInputs( 0, 0, 0,
			rgbGen, GLX_MATERIAL_ALPHAGEN_SKIP,
			GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_BAD,
			0, 0, 0, 0, qfalse, &key ) == qtrue );
		CHECK( key.rgbGen == rgbGen );
		CHECK( key.program.mode == glx::MaterialProgramMode::SingleTexture );
	}

	for ( int alphaGen : alphaGens ) {
		CHECK( glx::GLX_Material_StageKeyForInputs( 0, 0, 0,
			GLX_MATERIAL_RGBGEN_IDENTITY, alphaGen,
			GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_BAD,
			0, 0, 0, 0, qfalse, &key ) == qtrue );
		CHECK( key.alphaGen == alphaGen );
	}

	for ( int tcGen : tcGens ) {
		CHECK( glx::GLX_Material_StageKeyForInputs( 0, 0, 0,
			GLX_MATERIAL_RGBGEN_IDENTITY, GLX_MATERIAL_ALPHAGEN_SKIP,
			tcGen, GLX_MATERIAL_TCGEN_BAD,
			0, 0, 0, 0, qfalse, &key ) == qtrue );
		CHECK( key.tcGen0 == tcGen );
	}

	for ( int waveFunc : waveFuncs ) {
		CHECK( glx::GLX_Material_StageKeyForInputsFull( 0, 0, 0,
			GLX_MATERIAL_RGBGEN_WAVEFORM, GLX_MATERIAL_ALPHAGEN_SKIP,
			waveFunc, GLX_MATERIAL_WAVEFUNC_NONE,
			GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_BAD,
			0, 0, 0, 0, 0, 0, 0, 0,
			GLX_MATERIAL_FOG_ADJUST_NONE, qfalse, &key ) == qtrue );
		CHECK( key.rgbWaveFunc == waveFunc );

		CHECK( glx::GLX_Material_StageKeyForInputsFull( 0, 0, 0,
			GLX_MATERIAL_RGBGEN_IDENTITY, GLX_MATERIAL_ALPHAGEN_WAVEFORM,
			GLX_MATERIAL_WAVEFUNC_NONE, waveFunc,
			GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_BAD,
			0, 0, 0, 0, 0, 0, 0, 0,
			GLX_MATERIAL_FOG_ADJUST_NONE, qfalse, &key ) == qtrue );
		CHECK( key.alphaWaveFunc == waveFunc );
	}

	CHECK( glx::GLX_Material_StageKeyForInputsFull(
		GLX_STAGE_TEXMOD | GLX_STAGE_ENVIRONMENT | GLX_STAGE_DEPTH_FRAGMENT,
		0x1234, 0,
		GLX_MATERIAL_RGBGEN_WAVEFORM, GLX_MATERIAL_ALPHAGEN_PORTAL,
		GLX_MATERIAL_WAVEFUNC_SAWTOOTH, GLX_MATERIAL_WAVEFUNC_NONE,
		GLX_MATERIAL_TCGEN_VECTOR, GLX_MATERIAL_TCGEN_LIGHTMAP,
		glx::GLX_MATERIAL_MAX_TEXMODS_PER_BUNDLE,
		glx::GLX_MATERIAL_MAX_TEXMODS_PER_BUNDLE,
		fiveTexModMask, fiveTexModMask, fiveTexModSequence, fiveTexModSequence,
		0, 0, GLX_MATERIAL_FOG_ADJUST_MODULATE_RGB, qfalse, &key ) == qtrue );
	CHECK( key.flags == ( GLX_STAGE_TEXMOD | GLX_STAGE_ENVIRONMENT | GLX_STAGE_DEPTH_FRAGMENT ) );
	CHECK( key.stateBits == 0x1234u );
	CHECK( key.rgbWaveFunc == GLX_MATERIAL_WAVEFUNC_SAWTOOTH );
	CHECK( key.alphaWaveFunc == GLX_MATERIAL_WAVEFUNC_NONE );
	CHECK( key.texModTypes0 == fiveTexModMask );
	CHECK( key.texModTypes1 == fiveTexModMask );
	CHECK( key.texModSequence0 == fiveTexModSequence );
	CHECK( key.texModSequence1 == fiveTexModSequence );
	CHECK( key.fogAdjust == GLX_MATERIAL_FOG_ADJUST_MODULATE_RGB );
	CHECK( key.program.features == ( glx::GLX_MATERIAL_FEATURE_TEXMOD |
		glx::GLX_MATERIAL_FEATURE_ENVIRONMENT | glx::GLX_MATERIAL_FEATURE_DEPTH_FRAGMENT ) );
	CHECK( glx::GLX_Material_StageKeyEquals( key, key ) == qtrue );

	glx::MaterialStageKey entityColorKey {};
	glx::MaterialStageKey vertexColorKey {};
	CHECK( glx::GLX_Material_StageKeyForInputs( 0, 0, 0,
		GLX_MATERIAL_RGBGEN_ENTITY, GLX_MATERIAL_ALPHAGEN_SKIP,
		GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_BAD,
		0, 0, 0, 0, qfalse, &entityColorKey ) == qtrue );
	CHECK( glx::GLX_Material_StageKeyForInputs( 0, 0, 0,
		GLX_MATERIAL_RGBGEN_VERTEX, GLX_MATERIAL_ALPHAGEN_SKIP,
		GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_BAD,
		0, 0, 0, 0, qfalse, &vertexColorKey ) == qtrue );
	CHECK( glx::GLX_Material_KeyEquals( entityColorKey.program, vertexColorKey.program ) == qtrue );
	CHECK( glx::GLX_Material_StageKeyEquals( entityColorKey, vertexColorKey ) == qfalse );

	glx::MaterialStageKey scrollKey {};
	glx::MaterialStageKey rotateKey {};
	CHECK( glx::GLX_Material_StageKeyForInputs( GLX_STAGE_TEXMOD, 0, 0,
		GLX_MATERIAL_RGBGEN_IDENTITY, GLX_MATERIAL_ALPHAGEN_SKIP,
		GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_BAD,
		1, 0, GLX_MATERIAL_TMOD_SCROLL_BIT, 0, qfalse, &scrollKey ) == qtrue );
	CHECK( glx::GLX_Material_StageKeyForInputs( GLX_STAGE_TEXMOD, 0, 0,
		GLX_MATERIAL_RGBGEN_IDENTITY, GLX_MATERIAL_ALPHAGEN_SKIP,
		GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_BAD,
		1, 0, GLX_MATERIAL_TMOD_ROTATE_BIT, 0, qfalse, &rotateKey ) == qtrue );
	CHECK( glx::GLX_Material_KeyEquals( scrollKey.program, rotateKey.program ) == qtrue );
	CHECK( glx::GLX_Material_StageKeyEquals( scrollKey, rotateKey ) == qfalse );

	glx::MaterialStageKey scrollScaleKey {};
	glx::MaterialStageKey scaleScrollKey {};
	unsigned int scrollScaleSequence = 0;
	unsigned int scaleScrollSequence = 0;
	const unsigned int scrollScaleMask = GLX_MATERIAL_TMOD_SCROLL_BIT |
		GLX_MATERIAL_TMOD_SCALE_BIT;

	scrollScaleSequence = glx::GLX_Material_TexModSequenceSetSlot( scrollScaleSequence, 0,
		GLX_MATERIAL_TMOD_OPCODE_SCROLL );
	scrollScaleSequence = glx::GLX_Material_TexModSequenceSetSlot( scrollScaleSequence, 1,
		GLX_MATERIAL_TMOD_OPCODE_SCALE );
	scaleScrollSequence = glx::GLX_Material_TexModSequenceSetSlot( scaleScrollSequence, 0,
		GLX_MATERIAL_TMOD_OPCODE_SCALE );
	scaleScrollSequence = glx::GLX_Material_TexModSequenceSetSlot( scaleScrollSequence, 1,
		GLX_MATERIAL_TMOD_OPCODE_SCROLL );
	CHECK( glx::GLX_Material_StageKeyForInputsFull( GLX_STAGE_TEXMOD, 0, 0,
		GLX_MATERIAL_RGBGEN_IDENTITY, GLX_MATERIAL_ALPHAGEN_SKIP,
		GLX_MATERIAL_WAVEFUNC_NONE, GLX_MATERIAL_WAVEFUNC_NONE,
		GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_BAD,
		2, 0, scrollScaleMask, 0, scrollScaleSequence, 0,
		0, 0, GLX_MATERIAL_FOG_ADJUST_NONE, qfalse,
		&scrollScaleKey ) == qtrue );
	CHECK( glx::GLX_Material_StageKeyForInputsFull( GLX_STAGE_TEXMOD, 0, 0,
		GLX_MATERIAL_RGBGEN_IDENTITY, GLX_MATERIAL_ALPHAGEN_SKIP,
		GLX_MATERIAL_WAVEFUNC_NONE, GLX_MATERIAL_WAVEFUNC_NONE,
		GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_BAD,
		2, 0, scrollScaleMask, 0, scaleScrollSequence, 0,
		0, 0, GLX_MATERIAL_FOG_ADJUST_NONE, qfalse,
		&scaleScrollKey ) == qtrue );
	CHECK( glx::GLX_Material_KeyEquals( scrollScaleKey.program, scaleScrollKey.program ) == qtrue );
	CHECK( scrollScaleKey.texModTypes0 == scaleScrollKey.texModTypes0 );
	CHECK( glx::GLX_Material_StageKeyEquals( scrollScaleKey, scaleScrollKey ) == qfalse );
	CHECK( glx::GLX_Material_StageKeyForInputsFull( GLX_STAGE_TEXMOD, 0, 0,
		GLX_MATERIAL_RGBGEN_IDENTITY, GLX_MATERIAL_ALPHAGEN_SKIP,
		GLX_MATERIAL_WAVEFUNC_NONE, GLX_MATERIAL_WAVEFUNC_NONE,
		GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_BAD,
		2, 0, scrollScaleMask, 0, scrollScaleSequence | ( 1u << 12 ), 0,
		0, 0, GLX_MATERIAL_FOG_ADJUST_NONE, qfalse, &scrollScaleKey ) == qfalse );

	glx::MaterialStageKey sineRgbWaveKey {};
	glx::MaterialStageKey squareRgbWaveKey {};
	CHECK( glx::GLX_Material_StageKeyForInputsFull( 0, 0, 0,
		GLX_MATERIAL_RGBGEN_WAVEFORM, GLX_MATERIAL_ALPHAGEN_SKIP,
		GLX_MATERIAL_WAVEFUNC_SIN, GLX_MATERIAL_WAVEFUNC_NONE,
		GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_BAD,
		0, 0, 0, 0, 0, 0, 0, 0,
		GLX_MATERIAL_FOG_ADJUST_NONE, qfalse, &sineRgbWaveKey ) == qtrue );
	CHECK( glx::GLX_Material_StageKeyForInputsFull( 0, 0, 0,
		GLX_MATERIAL_RGBGEN_WAVEFORM, GLX_MATERIAL_ALPHAGEN_SKIP,
		GLX_MATERIAL_WAVEFUNC_SQUARE, GLX_MATERIAL_WAVEFUNC_NONE,
		GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_BAD,
		0, 0, 0, 0, 0, 0, 0, 0,
		GLX_MATERIAL_FOG_ADJUST_NONE, qfalse, &squareRgbWaveKey ) == qtrue );
	CHECK( glx::GLX_Material_KeyEquals( sineRgbWaveKey.program, squareRgbWaveKey.program ) == qtrue );
	CHECK( glx::GLX_Material_StageKeyEquals( sineRgbWaveKey, squareRgbWaveKey ) == qfalse );

	glx::MaterialStageKey sineStretchKey {};
	glx::MaterialStageKey triangleStretchKey {};
	unsigned int stretchSequence = 0;
	unsigned int sineStretchWave = 0;
	unsigned int triangleStretchWave = 0;

	stretchSequence = glx::GLX_Material_TexModSequenceSetSlot( stretchSequence, 0,
		GLX_MATERIAL_TMOD_OPCODE_STRETCH );
	sineStretchWave = glx::GLX_Material_TexModWaveFuncSetSlot( sineStretchWave, 0,
		GLX_MATERIAL_WAVEFUNC_SIN );
	triangleStretchWave = glx::GLX_Material_TexModWaveFuncSetSlot( triangleStretchWave, 0,
		GLX_MATERIAL_WAVEFUNC_TRIANGLE );
	CHECK( glx::GLX_Material_StageKeyForInputsFull( GLX_STAGE_TEXMOD, 0, 0,
		GLX_MATERIAL_RGBGEN_IDENTITY, GLX_MATERIAL_ALPHAGEN_SKIP,
		GLX_MATERIAL_WAVEFUNC_NONE, GLX_MATERIAL_WAVEFUNC_NONE,
		GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_BAD,
		1, 0, GLX_MATERIAL_TMOD_STRETCH_BIT, 0, stretchSequence, 0,
		sineStretchWave, 0, GLX_MATERIAL_FOG_ADJUST_NONE, qfalse,
		&sineStretchKey ) == qtrue );
	CHECK( glx::GLX_Material_StageKeyForInputsFull( GLX_STAGE_TEXMOD, 0, 0,
		GLX_MATERIAL_RGBGEN_IDENTITY, GLX_MATERIAL_ALPHAGEN_SKIP,
		GLX_MATERIAL_WAVEFUNC_NONE, GLX_MATERIAL_WAVEFUNC_NONE,
		GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_BAD,
		1, 0, GLX_MATERIAL_TMOD_STRETCH_BIT, 0, stretchSequence, 0,
		triangleStretchWave, 0, GLX_MATERIAL_FOG_ADJUST_NONE, qfalse,
		&triangleStretchKey ) == qtrue );
	CHECK( glx::GLX_Material_KeyEquals( sineStretchKey.program, triangleStretchKey.program ) == qtrue );
	CHECK( glx::GLX_Material_StageKeyEquals( sineStretchKey, triangleStretchKey ) == qfalse );
	CHECK( glx::GLX_Material_StageKeyForInputsFull( GLX_STAGE_TEXMOD, 0, 0,
		GLX_MATERIAL_RGBGEN_IDENTITY, GLX_MATERIAL_ALPHAGEN_SKIP,
		GLX_MATERIAL_WAVEFUNC_NONE, GLX_MATERIAL_WAVEFUNC_NONE,
		GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_BAD,
		1, 0, GLX_MATERIAL_TMOD_SCROLL_BIT, 0,
		glx::GLX_Material_TexModSequenceSetSlot( 0, 0, GLX_MATERIAL_TMOD_OPCODE_SCROLL ),
		0, sineStretchWave, 0, GLX_MATERIAL_FOG_ADJUST_NONE, qfalse,
		&sineStretchKey ) == qfalse );

	glx::MaterialStageKey noDetailKey {};
	glx::MaterialStageKey detailKey {};
	CHECK( glx::GLX_Material_StageKeyForInputs( 0, 0, 0,
		GLX_MATERIAL_RGBGEN_IDENTITY, GLX_MATERIAL_ALPHAGEN_SKIP,
		GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_BAD,
		0, 0, 0, 0, qfalse, &noDetailKey ) == qtrue );
	CHECK( glx::GLX_Material_StageKeyForInputs( GLX_STAGE_DETAIL, 0, 0,
		GLX_MATERIAL_RGBGEN_IDENTITY, GLX_MATERIAL_ALPHAGEN_SKIP,
		GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_BAD,
		0, 0, 0, 0, qfalse, &detailKey ) == qtrue );
	CHECK( glx::GLX_Material_KeyEquals( noDetailKey.program, detailKey.program ) == qtrue );
	CHECK( glx::GLX_Material_StageKeyEquals( noDetailKey, detailKey ) == qfalse );

	glx::MaterialStageKey fogRgbAdjustKey {};
	glx::MaterialStageKey fogAlphaAdjustKey {};
	CHECK( glx::GLX_Material_StageKeyForInputsFull( 0, 0, 0,
		GLX_MATERIAL_RGBGEN_IDENTITY, GLX_MATERIAL_ALPHAGEN_SKIP,
		GLX_MATERIAL_WAVEFUNC_NONE, GLX_MATERIAL_WAVEFUNC_NONE,
		GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_BAD,
		0, 0, 0, 0, 0, 0, 0, 0,
		GLX_MATERIAL_FOG_ADJUST_MODULATE_RGB, qfalse, &fogRgbAdjustKey ) == qtrue );
	CHECK( glx::GLX_Material_StageKeyForInputsFull( 0, 0, 0,
		GLX_MATERIAL_RGBGEN_IDENTITY, GLX_MATERIAL_ALPHAGEN_SKIP,
		GLX_MATERIAL_WAVEFUNC_NONE, GLX_MATERIAL_WAVEFUNC_NONE,
		GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_BAD,
		0, 0, 0, 0, 0, 0, 0, 0,
		GLX_MATERIAL_FOG_ADJUST_MODULATE_ALPHA, qfalse, &fogAlphaAdjustKey ) == qtrue );
	CHECK( glx::GLX_Material_KeyEquals( fogRgbAdjustKey.program, fogAlphaAdjustKey.program ) == qtrue );
	CHECK( glx::GLX_Material_StageKeyEquals( fogRgbAdjustKey, fogAlphaAdjustKey ) == qfalse );

	glx::MaterialStageKey blendStateKey {};
	glx::MaterialStageKey alphaStateKey {};
	CHECK( glx::GLX_Material_StageKeyForInputs( GLX_STAGE_BLEND,
		GLX_MATERIAL_STATE_SRCBLEND_SRC_ALPHA |
			GLX_MATERIAL_STATE_DSTBLEND_ONE_MINUS_SRC_ALPHA,
		0, GLX_MATERIAL_RGBGEN_IDENTITY, GLX_MATERIAL_ALPHAGEN_SKIP,
		GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_BAD,
		0, 0, 0, 0, qfalse, &blendStateKey ) == qtrue );
	CHECK( glx::GLX_Material_StageKeyForInputs( GLX_STAGE_ALPHA_TEST,
		GLX_MATERIAL_STATE_ATEST_GE_80,
		0, GLX_MATERIAL_RGBGEN_IDENTITY, GLX_MATERIAL_ALPHAGEN_SKIP,
		GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_BAD,
		0, 0, 0, 0, qfalse, &alphaStateKey ) == qtrue );
	CHECK( glx::GLX_Material_KeyEquals( blendStateKey.program, alphaStateKey.program ) == qtrue );
	CHECK( glx::GLX_Material_StageKeyEquals( blendStateKey, alphaStateKey ) == qfalse );
	CHECK( glx::GLX_Material_StateUnknownBits( blendStateKey.stateBits ) == 0 );
	CHECK( glx::GLX_Material_StateUnknownBits( 0x80000000u ) == 0x80000000u );

	CHECK( glx::GLX_Material_StageKeyForInputs(
		GLX_STAGE_MULTITEXTURE | GLX_STAGE_DEPTH_FRAGMENT | GLX_STAGE_BLEND |
			GLX_STAGE_ALPHA_TEST | GLX_STAGE_DEPTH_WRITE | GLX_STAGE_LIGHTMAP |
			GLX_STAGE_ANIMATED_IMAGE | GLX_STAGE_VIDEO_MAP | GLX_STAGE_SCREEN_MAP |
			GLX_STAGE_DLIGHT_MAP | GLX_STAGE_DETAIL | GLX_STAGE_ST0 | GLX_STAGE_ST1,
		0xabcd, GLX_MATERIAL_COMBINE_MODULATE,
		GLX_MATERIAL_RGBGEN_LIGHTING_DIFFUSE, GLX_MATERIAL_ALPHAGEN_LIGHTING_SPECULAR,
		GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_LIGHTMAP,
		0, 0, 0, 0, qfalse, &key ) == qtrue );
	CHECK( key.program.mode == glx::MaterialProgramMode::MultiModulate );
	CHECK( key.program.features == glx::GLX_MATERIAL_FEATURE_DEPTH_FRAGMENT );
	CHECK( ( key.flags & GLX_STAGE_DLIGHT_MAP ) != 0 );
	CHECK( ( key.flags & GLX_STAGE_DETAIL ) != 0 );
	CHECK( ( key.flags & GLX_STAGE_SCREEN_MAP ) != 0 );
	CHECK( ( key.flags & GLX_STAGE_VIDEO_MAP ) != 0 );

	CHECK( glx::GLX_Material_StageKeyForInputs( 0, 0, 0,
		999, GLX_MATERIAL_ALPHAGEN_SKIP, GLX_MATERIAL_TCGEN_TEXTURE,
		GLX_MATERIAL_TCGEN_BAD, 0, 0, 0, 0, qfalse, &key ) == qfalse );
	CHECK( glx::GLX_Material_StageKeyForInputs( 0, 0, 0,
		GLX_MATERIAL_RGBGEN_IDENTITY, 999, GLX_MATERIAL_TCGEN_TEXTURE,
		GLX_MATERIAL_TCGEN_BAD, 0, 0, 0, 0, qfalse, &key ) == qfalse );
	CHECK( glx::GLX_Material_StageKeyForInputs( 0, 0, 0,
		GLX_MATERIAL_RGBGEN_IDENTITY, GLX_MATERIAL_ALPHAGEN_SKIP, 999,
		GLX_MATERIAL_TCGEN_BAD, 0, 0, 0, 0, qfalse, &key ) == qfalse );
	CHECK( glx::GLX_Material_StageKeyForInputs( GLX_STAGE_TEXMOD, 0, 0,
		GLX_MATERIAL_RGBGEN_IDENTITY, GLX_MATERIAL_ALPHAGEN_SKIP,
		GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_BAD,
		glx::GLX_MATERIAL_MAX_TEXMODS_PER_BUNDLE + 1, 0,
		allTexMods, 0, qfalse, &key ) == qfalse );
	CHECK( glx::GLX_Material_StageKeyForInputs( GLX_STAGE_TEXMOD, 0, 0,
		GLX_MATERIAL_RGBGEN_IDENTITY, GLX_MATERIAL_ALPHAGEN_SKIP,
		GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_BAD,
		1, 0, 0, 0, qfalse, &key ) == qfalse );
	CHECK( glx::GLX_Material_StageKeyForInputs( GLX_STAGE_TEXMOD, 0, 0,
		GLX_MATERIAL_RGBGEN_IDENTITY, GLX_MATERIAL_ALPHAGEN_SKIP,
		GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_BAD,
		1, 0, GLX_MATERIAL_TMOD_UNKNOWN_BIT, 0, qfalse, &key ) == qfalse );
	CHECK( glx::GLX_Material_StageKeyForInputsFull( 0, 0, 0,
		GLX_MATERIAL_RGBGEN_IDENTITY, GLX_MATERIAL_ALPHAGEN_SKIP,
		GLX_MATERIAL_WAVEFUNC_SIN, GLX_MATERIAL_WAVEFUNC_NONE,
		GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_BAD,
		0, 0, 0, 0, 0, 0, 0, 0,
		GLX_MATERIAL_FOG_ADJUST_NONE, qfalse, &key ) == qfalse );
	CHECK( glx::GLX_Material_StageKeyForInputsFull( 0, 0, 0,
		GLX_MATERIAL_RGBGEN_WAVEFORM, GLX_MATERIAL_ALPHAGEN_SKIP,
		99, GLX_MATERIAL_WAVEFUNC_NONE,
		GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_BAD,
		0, 0, 0, 0, 0, 0, 0, 0,
		GLX_MATERIAL_FOG_ADJUST_NONE, qfalse, &key ) == qfalse );
	CHECK( glx::GLX_Material_StageKeyForInputsFull( 0, 0, 0,
		GLX_MATERIAL_RGBGEN_IDENTITY, GLX_MATERIAL_ALPHAGEN_SKIP,
		GLX_MATERIAL_WAVEFUNC_NONE, GLX_MATERIAL_WAVEFUNC_NONE,
		GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_BAD,
		0, 0, 0, 0, 0, 0, 0, 0,
		99, qfalse, &key ) == qfalse );

	return true;
}

bool MaterialIRCompilesToProgramStatePlans()
{
	glx::MaterialIR material = glx::GLX_RenderIR_MakeMaterial(
		17,
		GLX_STAGE_MULTITEXTURE | GLX_STAGE_TEXMOD | GLX_STAGE_ENVIRONMENT |
			GLX_STAGE_BLEND | GLX_STAGE_ST0 | GLX_STAGE_ST1,
		GLX_MATERIAL_STATE_SRCBLEND_SRC_ALPHA |
			GLX_MATERIAL_STATE_DSTBLEND_ONE_MINUS_SRC_ALPHA,
		2 );
	glx::MaterialStatePlan plan {};
	unsigned int reasons = 0;
	unsigned int texModSequence0 = 0;
	unsigned int texModSequence1 = 0;

	texModSequence0 = glx::GLX_Material_TexModSequenceSetSlot( texModSequence0, 0,
		GLX_MATERIAL_TMOD_OPCODE_SCROLL );
	texModSequence0 = glx::GLX_Material_TexModSequenceSetSlot( texModSequence0, 1,
		GLX_MATERIAL_TMOD_OPCODE_SCALE );
	texModSequence1 = glx::GLX_Material_TexModSequenceSetSlot( texModSequence1, 0,
		GLX_MATERIAL_TMOD_OPCODE_ROTATE );

	material.rgbGen = GLX_MATERIAL_RGBGEN_WAVEFORM;
	material.alphaGen = GLX_MATERIAL_ALPHAGEN_VERTEX;
	material.rgbWaveFunc = GLX_MATERIAL_WAVEFUNC_SIN;
	material.alphaWaveFunc = GLX_MATERIAL_WAVEFUNC_NONE;
	material.tcGen0 = GLX_MATERIAL_TCGEN_TEXTURE;
	material.tcGen1 = GLX_MATERIAL_TCGEN_LIGHTMAP;
	material.texMods0 = 2;
	material.texMods1 = 1;
	material.texModTypes0 = GLX_MATERIAL_TMOD_SCROLL_BIT | GLX_MATERIAL_TMOD_SCALE_BIT;
	material.texModTypes1 = GLX_MATERIAL_TMOD_ROTATE_BIT;
	material.texModSequence0 = texModSequence0;
	material.texModSequence1 = texModSequence1;
	material.fogAdjust = GLX_MATERIAL_FOG_ADJUST_MODULATE_RGB;
	material.materialCombine = GLX_MATERIAL_COMBINE_MODULATE;

	CHECK( glx::GLX_Material_StatePlanForIR( material, &plan, &reasons ) == qtrue );
	CHECK( reasons == glx::GLX_MATERIAL_UNSUPPORTED_NONE );
	CHECK( plan.tier == glx::RenderProductTier::GL2X );
	CHECK( plan.programmable == qtrue );
	CHECK( plan.sort == 17 );
	CHECK( plan.stage.program.mode == glx::MaterialProgramMode::MultiModulate );
	CHECK( plan.stage.program.features == ( glx::GLX_MATERIAL_FEATURE_TEXMOD |
		glx::GLX_MATERIAL_FEATURE_ENVIRONMENT ) );
	CHECK( plan.stage.stateBits == material.stateBits );
	CHECK( plan.stage.rgbGen == GLX_MATERIAL_RGBGEN_WAVEFORM );
	CHECK( plan.stage.rgbWaveFunc == GLX_MATERIAL_WAVEFUNC_SIN );
	CHECK( plan.stage.texModSequence0 == texModSequence0 );
	CHECK( plan.stage.texModSequence1 == texModSequence1 );

	CHECK( glx::GLX_Material_StatePlanForTierAndIR( glx::RenderProductTier::GL12,
		material, &plan, &reasons ) == qtrue );
	CHECK( plan.tier == glx::RenderProductTier::GL12 );
	CHECK( plan.programmable == qfalse );

	glx::MaterialIR badCombine = material;
	badCombine.materialCombine = 99;
	CHECK( glx::GLX_Material_StatePlanForIR( badCombine, &plan, &reasons ) == qfalse );
	CHECK( ( reasons & glx::GLX_MATERIAL_UNSUPPORTED_INVALID_COMBINE ) != 0 );
	CHECK( std::strcmp( glx::GLX_Material_UnsupportedReasonName( reasons ),
		"invalid multitexture combine" ) == 0 );

	glx::MaterialIR badSequence = material;
	badSequence.texModSequence0 = glx::GLX_Material_TexModSequenceSetSlot(
		badSequence.texModSequence0, 2, GLX_MATERIAL_TMOD_OPCODE_ROTATE );
	CHECK( glx::GLX_Material_StatePlanForIR( badSequence, &plan, &reasons ) == qfalse );
	CHECK( ( reasons & glx::GLX_MATERIAL_UNSUPPORTED_TEXMOD_SEQUENCE ) != 0 );

	glx::MaterialIR badWave = material;
	badWave.rgbWaveFunc = 99;
	CHECK( glx::GLX_Material_StatePlanForIR( badWave, &plan, &reasons ) == qfalse );
	CHECK( ( reasons & glx::GLX_MATERIAL_UNSUPPORTED_RGB_WAVE ) != 0 );

	return true;
}

bool MaterialParameterBlocksMirrorNativeRenderInputs()
{
	glx::MaterialIR material = glx::GLX_RenderIR_MakeMaterial(
		9,
		GLX_STAGE_MULTITEXTURE | GLX_STAGE_TEXMOD | GLX_STAGE_ENVIRONMENT |
			GLX_STAGE_DEPTH_FRAGMENT | GLX_STAGE_SCREEN_MAP,
		GLX_MATERIAL_STATE_SRCBLEND_SRC_ALPHA |
			GLX_MATERIAL_STATE_DSTBLEND_ONE_MINUS_SRC_ALPHA,
		3 );
	unsigned int texModSequence = 0;

	texModSequence = glx::GLX_Material_TexModSequenceSetSlot( texModSequence, 0,
		GLX_MATERIAL_TMOD_OPCODE_SCROLL );
	material.rgbGen = GLX_MATERIAL_RGBGEN_VERTEX;
	material.alphaGen = GLX_MATERIAL_ALPHAGEN_WAVEFORM;
	material.rgbWaveFunc = GLX_MATERIAL_WAVEFUNC_NONE;
	material.alphaWaveFunc = GLX_MATERIAL_WAVEFUNC_SIN;
	material.tcGen0 = GLX_MATERIAL_TCGEN_TEXTURE;
	material.tcGen1 = GLX_MATERIAL_TCGEN_LIGHTMAP;
	material.texMods0 = 1;
	material.texModTypes0 = GLX_MATERIAL_TMOD_SCROLL_BIT;
	material.texModSequence0 = texModSequence;
	material.fogAdjust = GLX_MATERIAL_FOG_ADJUST_MODULATE_RGB;
	material.materialCombine = GLX_MATERIAL_COMBINE_MODULATE;

	glx::MaterialParameterBlock block =
		glx::GLX_RenderIR_MakeMaterialParameterBlock( material );
	const unsigned int hash = glx::GLX_RenderIR_HashMaterialParameterBlock( block );
	CHECK( glx::GLX_RenderIR_ValidateMaterialParameterBlock( block ) == qtrue );
	CHECK( hash != 0 );
	CHECK( block.frame.sort == 9 );
	CHECK( block.frame.shaderStagePasses == 3 );
	CHECK( ( block.frame.featureMask & GLX_STAGE_MULTITEXTURE ) != 0 );
	CHECK( ( block.frame.featureMask & GLX_STAGE_TEXMOD ) != 0 );
	CHECK( ( block.frame.featureMask & GLX_STAGE_SCREEN_MAP ) != 0 );
	CHECK( block.object.rgbGen == GLX_MATERIAL_RGBGEN_VERTEX );
	CHECK( block.object.alphaGen == GLX_MATERIAL_ALPHAGEN_WAVEFORM );
	CHECK( block.object.alphaWaveFunc == GLX_MATERIAL_WAVEFUNC_SIN );
	CHECK( block.object.tcGen1 == GLX_MATERIAL_TCGEN_LIGHTMAP );
	CHECK( block.material.flags == material.flags );
	CHECK( block.material.stateBits == material.stateBits );
	CHECK( block.material.texModSequence0 == texModSequence );
	CHECK( block.material.fogAdjust == GLX_MATERIAL_FOG_ADJUST_MODULATE_RGB );
	CHECK( block.material.materialCombine == GLX_MATERIAL_COMBINE_MODULATE );

	glx::MaterialParameterBlock changedBlock = block;
	changedBlock.object.tcGen1 = GLX_MATERIAL_TCGEN_TEXTURE;
	CHECK( glx::GLX_RenderIR_HashMaterialParameterBlock( changedBlock ) != hash );

	glx::MaterialStatePlan irPlan {};
	glx::MaterialStatePlan blockPlan {};
	unsigned int irReasons = 0;
	unsigned int blockReasons = 0;
	CHECK( glx::GLX_Material_StatePlanForTierAndIR( glx::RenderProductTier::GL2X,
		material, &irPlan, &irReasons ) == qtrue );
	CHECK( glx::GLX_Material_StatePlanForTierAndParameterBlock( glx::RenderProductTier::GL2X,
		block, &blockPlan, &blockReasons ) == qtrue );
	CHECK( irReasons == glx::GLX_MATERIAL_UNSUPPORTED_NONE );
	CHECK( blockReasons == glx::GLX_MATERIAL_UNSUPPORTED_NONE );
	CHECK( blockPlan.sort == material.sort );
	CHECK( blockPlan.programmable == qtrue );
	CHECK( glx::GLX_Material_StageKeyEquals( irPlan.stage, blockPlan.stage ) == qtrue );

	block.material.texMods0 = -1;
	CHECK( glx::GLX_RenderIR_ValidateMaterialParameterBlock( block ) == qfalse );
	blockReasons = glx::GLX_MATERIAL_UNSUPPORTED_NONE;
	CHECK( glx::GLX_Material_StatePlanForTierAndParameterBlock( glx::RenderProductTier::GL2X,
		block, &blockPlan, &blockReasons ) == qfalse );
	CHECK( ( blockReasons & glx::GLX_MATERIAL_UNSUPPORTED_INVALID_IR ) != 0 );

	return true;
}

bool StreamGatesMatchRcAllowlist()
{
	glx::StreamMaterialGateConfig rc {};
	glx::StreamMaterialGateResult result;

	rc.keyMode = 0;
	rc.multitexture = qtrue;
	rc.depthFragment = qtrue;
	rc.texMods = qtrue;
	rc.environment = qtrue;
	rc.dynamicLights = qfalse;
	rc.screenMaps = qfalse;
	rc.videoMaps = qfalse;

	result = glx::GLX_Stream_EvaluateMaterialGate( 0, 0, 0, rc );
	CHECK( result.allowed == qtrue );

	result = glx::GLX_Stream_EvaluateMaterialGate( GLX_STAGE_TEXMOD, 1, 0, rc );
	CHECK( result.allowed == qtrue );
	CHECK( result.hasTexMods == qtrue );
	CHECK( result.texModsGateAllowed == qtrue );

	result = glx::GLX_Stream_EvaluateMaterialGate( GLX_STAGE_ENVIRONMENT, 0, 0, rc );
	CHECK( result.allowed == qtrue );
	CHECK( result.hasEnvironment == qtrue );
	CHECK( result.environmentGateAllowed == qtrue );

	result = glx::GLX_Stream_EvaluateMaterialGate( GLX_STAGE_MULTITEXTURE | GLX_STAGE_ST1,
		0, 0, rc );
	CHECK( result.allowed == qtrue );
	CHECK( result.hasMultitexture == qtrue );
	CHECK( result.multitextureGateAllowed == qtrue );
	CHECK( result.hasSecondTexcoord == qtrue );
	CHECK( result.secondTexcoordGateAllowed == qtrue );

	result = glx::GLX_Stream_EvaluateMaterialGate( GLX_STAGE_DEPTH_FRAGMENT, 0, 0, rc );
	CHECK( result.allowed == qtrue );
	CHECK( result.hasDepthFragment == qtrue );
	CHECK( result.depthFragmentGateAllowed == qtrue );

	result = glx::GLX_Stream_EvaluateMaterialGate(
		GLX_STAGE_DEPTH_FRAGMENT | GLX_STAGE_MULTITEXTURE | GLX_STAGE_ST1, 0, 0, rc );
	CHECK( result.allowed == qtrue );
	CHECK( result.hasDepthFragment == qtrue );
	CHECK( result.depthFragmentGateAllowed == qtrue );
	CHECK( result.hasMultitexture == qtrue );
	CHECK( result.multitextureGateAllowed == qtrue );
	CHECK( result.hasSecondTexcoord == qtrue );
	CHECK( result.secondTexcoordGateAllowed == qtrue );

	result = glx::GLX_Stream_EvaluateMaterialGate( GLX_STAGE_ST1, 0, 0, rc );
	CHECK( result.allowed == qfalse );
	CHECK( result.secondTexcoordGateAllowed == qfalse );

	result = glx::GLX_Stream_EvaluateMaterialGate( GLX_STAGE_DLIGHT_MAP, 0, 0, rc );
	CHECK( result.allowed == qfalse );
	CHECK( result.dynamicLightGateAllowed == qfalse );

	result = glx::GLX_Stream_EvaluateMaterialGate( GLX_STAGE_SCREEN_MAP, 0, 0, rc );
	CHECK( result.allowed == qfalse );
	CHECK( result.screenMapGateAllowed == qfalse );

	result = glx::GLX_Stream_EvaluateMaterialGate( GLX_STAGE_VIDEO_MAP, 0, 0, rc );
	CHECK( result.allowed == qfalse );
	CHECK( result.videoMapGateAllowed == qfalse );

	return true;
}

bool StreamBroadKeyModeRemainsDeveloperEscapeHatch()
{
	glx::StreamMaterialGateConfig config {};
	glx::StreamMaterialGateResult result;

	config.keyMode = 2;

	result = glx::GLX_Stream_EvaluateMaterialGate(
		GLX_STAGE_ENVIRONMENT | GLX_STAGE_DLIGHT_MAP | GLX_STAGE_SCREEN_MAP |
			GLX_STAGE_VIDEO_MAP,
		2, 3, config );

	CHECK( result.allowed == qtrue );
	CHECK( result.hasEnvironment == qtrue );
	CHECK( result.environmentGateAllowed == qtrue );
	CHECK( result.hasDynamicLight == qtrue );
	CHECK( result.dynamicLightGateAllowed == qtrue );
	CHECK( result.hasScreenMap == qtrue );
	CHECK( result.screenMapGateAllowed == qtrue );
	CHECK( result.hasVideoMap == qtrue );
	CHECK( result.videoMapGateAllowed == qtrue );

	result = glx::GLX_Stream_EvaluateMaterialGate( GLX_STAGE_MULTITEXTURE | GLX_STAGE_ST1,
		0, 0, config );
	CHECK( result.allowed == qfalse );
	CHECK( result.hasMultitexture == qtrue );
	CHECK( result.multitextureGateAllowed == qfalse );
	CHECK( result.hasSecondTexcoord == qtrue );
	CHECK( result.secondTexcoordGateAllowed == qfalse );

	result = glx::GLX_Stream_EvaluateMaterialGate( GLX_STAGE_DEPTH_FRAGMENT, 0, 0, config );
	CHECK( result.allowed == qfalse );
	CHECK( result.hasDepthFragment == qtrue );
	CHECK( result.depthFragmentGateAllowed == qfalse );

	return true;
}

bool StreamSpecialSceneGatesAreExplicit()
{
	glx::StreamMaterialGateConfig config {};
	glx::StreamMaterialGateResult result;

	config.keyMode = 0;
	config.dynamicLights = qtrue;

	result = glx::GLX_Stream_EvaluateMaterialGate( GLX_STAGE_MULTITEXTURE, 0, 0, config );
	CHECK( result.allowed == qfalse );
	CHECK( result.hasMultitexture == qtrue );
	CHECK( result.multitextureGateAllowed == qfalse );

	config.multitexture = qtrue;
	result = glx::GLX_Stream_EvaluateMaterialGate( GLX_STAGE_MULTITEXTURE, 0, 0, config );
	CHECK( result.allowed == qtrue );
	CHECK( result.hasMultitexture == qtrue );
	CHECK( result.multitextureGateAllowed == qtrue );

	result = glx::GLX_Stream_EvaluateMaterialGate( GLX_STAGE_DEPTH_FRAGMENT, 0, 0, config );
	CHECK( result.allowed == qfalse );
	CHECK( result.hasDepthFragment == qtrue );
	CHECK( result.depthFragmentGateAllowed == qfalse );

	config.depthFragment = qtrue;
	result = glx::GLX_Stream_EvaluateMaterialGate( GLX_STAGE_DEPTH_FRAGMENT, 0, 0, config );
	CHECK( result.allowed == qtrue );
	CHECK( result.hasDepthFragment == qtrue );
	CHECK( result.depthFragmentGateAllowed == qtrue );

	result = glx::GLX_Stream_EvaluateMaterialGate( GLX_STAGE_DLIGHT_MAP, 0, 0, config );
	CHECK( result.allowed == qtrue );
	CHECK( result.hasDynamicLight == qtrue );
	CHECK( result.dynamicLightGateAllowed == qtrue );

	result = glx::GLX_Stream_EvaluateMaterialGate( GLX_STAGE_DLIGHT_MAP | GLX_STAGE_SCREEN_MAP,
		0, 0, config );
	CHECK( result.allowed == qfalse );
	CHECK( result.hasDynamicLight == qtrue );
	CHECK( result.dynamicLightGateAllowed == qtrue );
	CHECK( result.hasScreenMap == qtrue );
	CHECK( result.screenMapGateAllowed == qfalse );

	config.screenMaps = qtrue;
	result = glx::GLX_Stream_EvaluateMaterialGate( GLX_STAGE_DLIGHT_MAP | GLX_STAGE_SCREEN_MAP,
		0, 0, config );
	CHECK( result.allowed == qtrue );
	CHECK( result.dynamicLightGateAllowed == qtrue );
	CHECK( result.screenMapGateAllowed == qtrue );

	result = glx::GLX_Stream_EvaluateMaterialGate( GLX_STAGE_VIDEO_MAP, 0, 0, config );
	CHECK( result.allowed == qfalse );
	CHECK( result.hasVideoMap == qtrue );
	CHECK( result.videoMapGateAllowed == qfalse );

	config.videoMaps = qtrue;
	result = glx::GLX_Stream_EvaluateMaterialGate( GLX_STAGE_VIDEO_MAP, 0, 0, config );
	CHECK( result.allowed == qtrue );
	CHECK( result.videoMapGateAllowed == qtrue );

	config.keyMode = 1;
	config.dynamicLights = qfalse;
	config.screenMaps = qfalse;
	config.videoMaps = qfalse;
	result = glx::GLX_Stream_EvaluateMaterialGate(
		GLX_STAGE_DLIGHT_MAP | GLX_STAGE_SCREEN_MAP | GLX_STAGE_VIDEO_MAP,
		0, 0, config );
	CHECK( result.allowed == qfalse );
	CHECK( result.hasDynamicLight == qtrue );
	CHECK( result.dynamicLightGateAllowed == qfalse );
	CHECK( result.hasScreenMap == qtrue );
	CHECK( result.screenMapGateAllowed == qfalse );
	CHECK( result.hasVideoMap == qtrue );
	CHECK( result.videoMapGateAllowed == qfalse );

	result = glx::GLX_Stream_EvaluateMaterialGate( GLX_STAGE_TEXMOD | GLX_STAGE_DLIGHT_MAP,
		2, 0, config );
	CHECK( result.allowed == qfalse );
	CHECK( result.hasTexMods == qtrue );
	CHECK( result.texModsGateAllowed == qtrue );
	CHECK( result.hasDynamicLight == qtrue );
	CHECK( result.dynamicLightGateAllowed == qfalse );

	return true;
}

bool StreamDynamicLightAutoGateIsTierAndReadinessBound()
{
	CHECK( glx::GLX_Stream_ParseDynamicLightGateMode( "auto", 0 ) ==
		glx::StreamDynamicLightGateMode::Auto );
	CHECK( glx::GLX_Stream_ParseDynamicLightGateMode( "AUTO", 0 ) ==
		glx::StreamDynamicLightGateMode::Auto );
	CHECK( glx::GLX_Stream_ParseDynamicLightGateMode( "1", 1 ) ==
		glx::StreamDynamicLightGateMode::On );
	CHECK( glx::GLX_Stream_ParseDynamicLightGateMode( "2", 2 ) ==
		glx::StreamDynamicLightGateMode::On );
	CHECK( glx::GLX_Stream_ParseDynamicLightGateMode( "true", 0 ) ==
		glx::StreamDynamicLightGateMode::On );
	CHECK( glx::GLX_Stream_ParseDynamicLightGateMode( "off", 1 ) ==
		glx::StreamDynamicLightGateMode::Off );
	CHECK( glx::GLX_Stream_ParseDynamicLightGateMode( nullptr, 0 ) ==
		glx::StreamDynamicLightGateMode::Off );

	glx::StreamDynamicLightGateConfig config {};
	config.streamDraw = qtrue;
	config.streamReady = qtrue;
	config.mode = glx::StreamDynamicLightGateMode::Auto;

	config.tier = glx::RenderProductTier::GL12;
	CHECK( glx::GLX_Stream_EvaluateDynamicLightGate( config ) == qfalse );

	config.tier = glx::RenderProductTier::GL2X;
	CHECK( glx::GLX_Stream_EvaluateDynamicLightGate( config ) == qfalse );

	config.tier = glx::RenderProductTier::GL3X;
	CHECK( glx::GLX_Stream_EvaluateDynamicLightGate( config ) == qtrue );

	config.tier = glx::RenderProductTier::GL41;
	CHECK( glx::GLX_Stream_EvaluateDynamicLightGate( config ) == qtrue );

	config.tier = glx::RenderProductTier::GL46;
	CHECK( glx::GLX_Stream_EvaluateDynamicLightGate( config ) == qtrue );

	config.streamReady = qfalse;
	CHECK( glx::GLX_Stream_EvaluateDynamicLightGate( config ) == qfalse );

	config.streamReady = qtrue;
	config.streamDraw = qfalse;
	CHECK( glx::GLX_Stream_EvaluateDynamicLightGate( config ) == qfalse );

	config.streamDraw = qtrue;
	config.streamReady = qfalse;
	config.tier = glx::RenderProductTier::GL12;
	config.mode = glx::StreamDynamicLightGateMode::On;
	CHECK( glx::GLX_Stream_EvaluateDynamicLightGate( config ) == qfalse );

	config.streamReady = qtrue;
	CHECK( glx::GLX_Stream_EvaluateDynamicLightGate( config ) == qtrue );

	config.mode = glx::StreamDynamicLightGateMode::Off;
	CHECK( glx::GLX_Stream_EvaluateDynamicLightGate( config ) == qfalse );

	return true;
}

bool StreamShadowGateIsExplicit()
{
	glx::StreamSpecialDrawGateConfig config {};

	config.streamDraw = qtrue;
	config.shadows = qfalse;
	CHECK( glx::GLX_Stream_EvaluateShadowDrawGate( config ) == qfalse );

	config.streamDraw = qfalse;
	config.shadows = qtrue;
	CHECK( glx::GLX_Stream_EvaluateShadowDrawGate( config ) == qfalse );

	config.streamDraw = qtrue;
	config.shadows = qtrue;
	CHECK( glx::GLX_Stream_EvaluateShadowDrawGate( config ) == qtrue );

	return true;
}

bool StreamBeamGateIsExplicit()
{
	glx::StreamSpecialDrawGateConfig config {};

	config.streamDraw = qtrue;
	config.beams = qfalse;
	CHECK( glx::GLX_Stream_EvaluateBeamDrawGate( config ) == qfalse );

	config.streamDraw = qfalse;
	config.beams = qtrue;
	CHECK( glx::GLX_Stream_EvaluateBeamDrawGate( config ) == qfalse );

	config.streamDraw = qtrue;
	config.beams = qtrue;
	CHECK( glx::GLX_Stream_EvaluateBeamDrawGate( config ) == qtrue );

	return true;
}

bool StreamPostProcessGateIsExplicit()
{
	glx::StreamSpecialDrawGateConfig config {};

	config.streamDraw = qtrue;
	config.postprocess = qfalse;
	CHECK( glx::GLX_Stream_EvaluatePostProcessDrawGate( config ) == qfalse );

	config.streamDraw = qfalse;
	config.postprocess = qtrue;
	CHECK( glx::GLX_Stream_EvaluatePostProcessDrawGate( config ) == qfalse );

	config.streamDraw = qtrue;
	config.postprocess = qtrue;
	CHECK( glx::GLX_Stream_EvaluatePostProcessDrawGate( config ) == qtrue );

	return true;
}

bool StreamDynamicCategoriesNormalizeToSceneProducts()
{
	CHECK( glx::GLX_Stream_NormalizeDynamicCategoryMask(
		GLX_DYNAMIC_CATEGORY_MASK_ENTITY, 0 ) == GLX_DYNAMIC_CATEGORY_MASK_ENTITY );
	CHECK( glx::GLX_Stream_NormalizeDynamicCategoryMask(
		GLX_DYNAMIC_CATEGORY_MASK_PARTICLE | GLX_DYNAMIC_CATEGORY_MASK_MARK, 0 ) ==
		( GLX_DYNAMIC_CATEGORY_MASK_PARTICLE | GLX_DYNAMIC_CATEGORY_MASK_MARK ) );
	CHECK( glx::GLX_Stream_NormalizeDynamicCategoryMask( 0,
		GLX_STAGE_BEAM_PASS ) == GLX_DYNAMIC_CATEGORY_MASK_BEAM );
	CHECK( glx::GLX_Stream_NormalizeDynamicCategoryMask( 0,
		GLX_STAGE_SHADOW_PASS ) == GLX_DYNAMIC_CATEGORY_MASK_SPECIAL );
	CHECK( glx::GLX_Stream_NormalizeDynamicCategoryMask( 0,
		GLX_STAGE_POSTPROCESS_PASS ) == GLX_DYNAMIC_CATEGORY_MASK_SPECIAL );
	CHECK( glx::GLX_Stream_NormalizeDynamicCategoryMask( 0,
		GLX_STAGE_DLIGHT_MAP ) == GLX_DYNAMIC_CATEGORY_MASK_DLIGHT );
	CHECK( glx::GLX_Stream_NormalizeDynamicCategoryMask(
		GLX_DYNAMIC_CATEGORY_MASK_ENTITY, GLX_STAGE_DLIGHT_MAP ) ==
		( GLX_DYNAMIC_CATEGORY_MASK_ENTITY | GLX_DYNAMIC_CATEGORY_MASK_DLIGHT ) );
	CHECK( glx::GLX_Stream_NormalizeDynamicCategoryMask(
		GLX_DYNAMIC_CATEGORY_MASK_ENTITY, GLX_STAGE_SHADOW_PASS ) ==
		( GLX_DYNAMIC_CATEGORY_MASK_ENTITY | GLX_DYNAMIC_CATEGORY_MASK_SPECIAL ) );
	CHECK( glx::GLX_Stream_NormalizeDynamicCategoryMask( 0, 0 ) ==
		GLX_DYNAMIC_CATEGORY_MASK_SPECIAL );

	return true;
}

bool CapabilityLogicClassifiesTiersAndExtensions()
{
	int major = 0;
	int minor = 0;

	glx::GLX_Caps_ParseVersionString( "OpenGL 4.6.0 Compatibility Profile", &major, &minor );
	CHECK( major == 4 );
	CHECK( minor == 6 );

	glx::GLX_Caps_ParseVersionString( "driver-without-version", &major, &minor );
	CHECK( major == 0 );
	CHECK( minor == 0 );

	CHECK( glx::GLX_Caps_ExtensionListHas( "GL_ARB_sync GL_ARB_timer_query", "GL_ARB_sync" ) == qtrue );
	CHECK( glx::GLX_Caps_ExtensionListHas( "GL_ARB_sync2 GL_ARB_timer_query", "GL_ARB_sync" ) == qfalse );

	glx::FeatureSet gl12 = glx::GLX_Caps_FeaturesForVersionAndExtensions( 1, 2, "" );
	CHECK( glx::GLX_Caps_TierForVersionAndFeatures( 1, 2, gl12 ) == glx::RenderProductTier::GL12 );
	CHECK( glx::GLX_Caps_HintForTierAndFeatures( glx::RenderProductTier::GL12, gl12 ) == glx::CapabilityHint::FixedFunction );

	glx::FeatureSet gl2x = glx::GLX_Caps_FeaturesForVersionAndExtensions( 2, 1, "" );
	CHECK( glx::GLX_Caps_TierForVersionAndFeatures( 2, 1, gl2x ) == glx::RenderProductTier::GL2X );
	CHECK( glx::GLX_Caps_HintForTierAndFeatures( glx::RenderProductTier::GL2X, gl2x ) == glx::CapabilityHint::Programmable );

	glx::FeatureSet gl3x = glx::GLX_Caps_FeaturesForVersionAndExtensions( 3, 3, "" );
	CHECK( gl3x.mapBufferRange == qtrue );
	CHECK( gl3x.timerQuery == qtrue );
	CHECK( glx::GLX_Caps_TierForVersionAndFeatures( 3, 3, gl3x ) == glx::RenderProductTier::GL3X );
	CHECK( glx::GLX_Caps_HintForTierAndFeatures( glx::RenderProductTier::GL3X, gl3x ) == glx::CapabilityHint::Modern );

	glx::FeatureSet extensionCore = glx::GLX_Caps_FeaturesForVersionAndExtensions( 2, 1,
		"GL_ARB_map_buffer_range GL_ARB_uniform_buffer_object GL_ARB_instanced_arrays" );
	CHECK( glx::GLX_Caps_TierForVersionAndFeatures( 2, 1, extensionCore ) == glx::RenderProductTier::GL2X );
	CHECK( glx::GLX_Caps_HintForTierAndFeatures( glx::RenderProductTier::GL2X, extensionCore ) == glx::CapabilityHint::Modern );

	glx::FeatureSet gl41 = glx::GLX_Caps_FeaturesForVersionAndExtensions( 4, 1, "" );
	CHECK( glx::GLX_Caps_TierForVersionAndFeatures( 4, 1, gl41 ) == glx::RenderProductTier::GL41 );
	CHECK( glx::GLX_Caps_HintForTierAndFeatures( glx::RenderProductTier::GL41, gl41 ) == glx::CapabilityHint::Modern );

	glx::FeatureSet gl46 = glx::GLX_Caps_FeaturesForVersionAndExtensions( 4, 6, "" );
	CHECK( gl46.drawIndirect == qtrue );
	CHECK( gl46.multiDrawIndirect == qtrue );
	CHECK( gl46.bufferStorage == qtrue );
	CHECK( glx::GLX_Caps_TierForVersionAndFeatures( 4, 6, gl46 ) == glx::RenderProductTier::GL46 );
	CHECK( glx::GLX_Caps_HintForTierAndFeatures( glx::RenderProductTier::GL46, gl46 ) == glx::CapabilityHint::HighEnd );
	CHECK( std::strcmp( glx::GLX_RenderProductTierName( glx::RenderProductTier::GL46 ), "GL46" ) == 0 );
	CHECK( std::strcmp( glx::GLX_CapabilityHintName( glx::CapabilityHint::HighEnd ), "high-end" ) == 0 );

	return true;
}

bool StreamStrategySelectionFollowsFallbackLadder()
{
	glx::FeatureSet features {};
	glx::StreamStrategySelection selection;
	glx::StreamRuntimeFallback runtime;

	selection = glx::GLX_Stream_SelectStrategy( "auto", features );
	CHECK( selection.knownMode == qtrue );
	CHECK( selection.strategy == glx::StreamStrategy::OrphanSubData );
	CHECK( selection.fallbackCount == 0 );

	features.mapBufferRange = qtrue;
	selection = glx::GLX_Stream_SelectStrategy( "auto", features );
	CHECK( selection.strategy == glx::StreamStrategy::MapBufferRange );
	CHECK( selection.fallbackCount == 0 );

	selection = glx::GLX_Stream_SelectStrategy( "persistent", features );
	CHECK( selection.strategy == glx::StreamStrategy::MapBufferRange );
	CHECK( selection.fallbackCount == 1 );

	selection = glx::GLX_Stream_SelectStrategy( "maprange", glx::FeatureSet {} );
	CHECK( selection.strategy == glx::StreamStrategy::OrphanSubData );
	CHECK( selection.fallbackCount == 1 );

	features.bufferStorage = qtrue;
	features.syncObjects = qtrue;
	selection = glx::GLX_Stream_SelectStrategy( "auto", features );
	CHECK( selection.strategy == glx::StreamStrategy::PersistentMapped );
	CHECK( selection.fallbackCount == 0 );

	selection = glx::GLX_Stream_SelectStrategy( "mystery", features );
	CHECK( selection.knownMode == qfalse );
	CHECK( selection.strategy == glx::StreamStrategy::PersistentMapped );

	runtime = glx::GLX_Stream_ApplyRuntimeFunctionFallbacks( {
		glx::StreamStrategy::PersistentMapped,
		qtrue,
		qfalse,
		qtrue,
		qtrue,
		qtrue
	} );
	CHECK( runtime.strategy == glx::StreamStrategy::MapBufferRange );
	CHECK( runtime.ready == qtrue );
	CHECK( runtime.fallbackCount == 1 );

	runtime = glx::GLX_Stream_ApplyRuntimeFunctionFallbacks( {
		glx::StreamStrategy::MapBufferRange,
		qfalse,
		qfalse,
		qtrue,
		qfalse,
		qtrue
	} );
	CHECK( runtime.strategy == glx::StreamStrategy::OrphanSubData );
	CHECK( runtime.ready == qtrue );
	CHECK( runtime.fallbackCount == 1 );

	runtime = glx::GLX_Stream_ApplyRuntimeFunctionFallbacks( {
		glx::StreamStrategy::OrphanSubData,
		qfalse,
		qfalse,
		qfalse,
		qfalse,
		qfalse
	} );
	CHECK( runtime.strategy == glx::StreamStrategy::OrphanSubData );
	CHECK( runtime.ready == qfalse );

	return true;
}

bool StaticWorldPacketLogicClassifiesRunsAndPolicies()
{
	const glx::StaticWorldPacketView packet {
		"textures/base_floor/test",
		7,
		100,
		4,
		200,
		24
	};
	glx::StaticWorldRunPacket result;

	result = glx::GLX_StaticWorld_ClassifyRunAgainstPacketView( packet, 3,
		200, 24, 100, 4, "textures/base_floor/test", 7 );
	CHECK( result.match == glx::StaticWorldPacketMatch::Full );
	CHECK( result.packetIndex == 3 );

	result = glx::GLX_StaticWorld_ClassifyRunAgainstPacketView( packet, 3,
		204, 8, 101, 2, "textures/base_floor/test", 7 );
	CHECK( result.match == glx::StaticWorldPacketMatch::Partial );

	result = glx::GLX_StaticWorld_ClassifyRunAgainstPacketView( packet, 3,
		204, 8, 99, 2, "textures/base_floor/test", 7 );
	CHECK( result.match == glx::StaticWorldPacketMatch::ItemMismatch );

	result = glx::GLX_StaticWorld_ClassifyRunAgainstPacketView( packet, 3,
		200, 24, 100, 4, "textures/other", 7 );
	CHECK( result.match == glx::StaticWorldPacketMatch::NoMatch );

	result = glx::GLX_StaticWorld_ClassifyRunAgainstPacketView( packet, 3,
		200, 24, 100, 4, "textures/base_floor/test", 8 );
	CHECK( result.match == glx::StaticWorldPacketMatch::NoMatch );

	CHECK( glx::GLX_StaticWorld_DrawPolicyFromString( "full" ) == glx::StaticWorldDrawPolicy::FullPackets );
	CHECK( glx::GLX_StaticWorld_DrawPolicyFromString( "packet" ) == glx::StaticWorldDrawPolicy::ContainedPackets );
	CHECK( glx::GLX_StaticWorld_DrawPolicyFromString( "legacy" ) == glx::StaticWorldDrawPolicy::AllRuns );
	CHECK( glx::GLX_StaticWorld_DrawPolicyFromString( "unknown" ) == glx::StaticWorldDrawPolicy::FullPackets );

	CHECK( glx::GLX_StaticWorld_DrawPolicyAllows( glx::StaticWorldDrawPolicy::FullPackets,
		glx::StaticWorldPacketMatch::Full ) == qtrue );
	CHECK( glx::GLX_StaticWorld_DrawPolicyAllows( glx::StaticWorldDrawPolicy::FullPackets,
		glx::StaticWorldPacketMatch::Partial ) == qfalse );
	CHECK( glx::GLX_StaticWorld_DrawPolicyAllows( glx::StaticWorldDrawPolicy::ContainedPackets,
		glx::StaticWorldPacketMatch::Partial ) == qtrue );
	CHECK( glx::GLX_StaticWorld_DrawPolicyAllows( glx::StaticWorldDrawPolicy::ContainedPackets,
		glx::StaticWorldPacketMatch::ItemMismatch ) == qfalse );
	CHECK( glx::GLX_StaticWorld_DrawPolicyAllows( glx::StaticWorldDrawPolicy::AllRuns,
		glx::StaticWorldPacketMatch::NoMatch ) == qtrue );

	return true;
}

bool RenderIRDefaultPassScheduleIsDeterministic()
{
	glx::FramePass passes[glx::GLX_RENDER_IR_PASS_COUNT];
	char schedule[glx::GLX_RENDER_IR_PASS_SCHEDULE_TEXT_BYTES];
	int count = 0;

	CHECK( glx::GLX_RenderIR_DefaultPassSchedule( passes, glx::GLX_RENDER_IR_PASS_COUNT, &count ) == qtrue );
	CHECK( count == glx::GLX_RENDER_IR_PASS_COUNT );
	CHECK( glx::GLX_RenderIR_ValidatePassSchedule( passes, count ) == qtrue );
	CHECK( glx::GLX_RenderIR_FormatPassSchedule( passes, count, schedule,
		glx::GLX_RENDER_IR_PASS_SCHEDULE_TEXT_BYTES ) > 0 );
	CHECK( std::strcmp( schedule,
		"frame-setup>sky-opaque-world>opaque-entities>dynamic-lights>dynamic-scene>transparent-layers>"
		"first-person-weapon>hud-2d>postprocess>output-export" ) == 0 );
	CHECK( glx::GLX_RenderIR_PassScheduleHash( passes, count ) != 0 );
	CHECK( passes[0].kind == glx::FramePassKind::FrameSetup );
	CHECK( passes[1].kind == glx::FramePassKind::SkyAndOpaqueWorld );
	CHECK( passes[3].kind == glx::FramePassKind::DynamicLights );
	CHECK( passes[4].kind == glx::FramePassKind::DynamicScene );
	CHECK( passes[8].kind == glx::FramePassKind::PostProcess );
	CHECK( passes[9].kind == glx::FramePassKind::OutputExport );

	CHECK( glx::GLX_RenderIR_DefaultPassSchedule( nullptr, 0, &count ) == qfalse );
	CHECK( count == glx::GLX_RENDER_IR_PASS_COUNT );

	passes[3].sequence = 4;
	CHECK( glx::GLX_RenderIR_ValidatePassSchedule( passes, glx::GLX_RENDER_IR_PASS_COUNT ) == qfalse );

	return true;
}

bool RenderIRProductsValidate()
{
	glx::UploadPlan upload = glx::GLX_RenderIR_MakeUploadPlan(
		glx::UploadPlanKind::TransientStream, 1, 128, 64, 32 );
	upload.texcoordBytes = 16;
	upload.alignment = 64;
	upload.sync = glx::UploadSyncPolicy::FrameFence;

	glx::MaterialIR material = glx::GLX_RenderIR_MakeMaterial(
		7, GLX_STAGE_ST0 | GLX_STAGE_TEXMOD, GLX_MATERIAL_STATE_DEPTHMASK_TRUE, 2 );
	material.rgbGen = GLX_MATERIAL_RGBGEN_VERTEX;
	material.alphaGen = GLX_MATERIAL_ALPHAGEN_VERTEX;
	material.tcGen0 = GLX_MATERIAL_TCGEN_TEXTURE;
	material.texMods0 = 1;

	glx::WorldPacket packet {};
	packet.packetIndex = 3;
	packet.pass = glx::FramePassKind::SkyAndOpaqueWorld;
	packet.surfaces = 4;
	packet.vertexes = 64;
	packet.indexes = 96;
	packet.firstItem = 10;
	packet.itemCount = 4;
	packet.vertexOffset = 128;
	packet.indexOffset = 256;
	packet.material = material;
	packet.upload = upload;

	glx::DynamicDraw draw {};
	draw.kind = glx::DynamicDrawKind::Indexed;
	draw.role = glx::DynamicDrawRole::DynamicLight;
	draw.pass = glx::GLX_RenderIR_DefaultPassForDynamicDrawRole( draw.role );
	draw.primitive = 0x0004;
	draw.count = 96;
	draw.indexType = 0x1403;
	draw.indices = reinterpret_cast<const void *>( static_cast<uintptr_t>( 0x40 ) );
	draw.legacyReason = GLX_LEGACY_DELEGATION_NONE;
	draw.profilerPath = GLX_DRAW_STREAM_GENERIC;
	draw.material = material;
	draw.upload = upload;

	glx::OutputTransform output = glx::GLX_RenderIR_DefaultOutputTransform();
	CHECK( output.sceneColorSpace == glx::SceneColorSpace::DisplayReferredSdr );
	CHECK( output.toneMap == glx::ToneMapOperator::Legacy );
	CHECK( output.grade == glx::ColorGradeMode::Disabled );
	CHECK( output.outputPrimaries == glx::OutputPrimaries::SrgbBt709 );
	CHECK( output.gamutMap == glx::GamutMapMode::Disabled );
	CHECK( output.exposureAlgorithm == glx::ExposureReductionAlgorithm::Manual );
	CHECK( output.autoExposure == qfalse );
	CHECK( output.requestedPrecisionMode == 0 );
	CHECK( output.precisionMode == 8 );
	CHECK( output.bloomThreshold == 0.75f );
	CHECK( output.bloomSoftKnee == 0.0f );
	CHECK( output.paperWhiteNits == 203.0f );
	CHECK( output.maxOutputNits == 203.0f );
	CHECK( output.gradeLift[0] == 0.0f );
	CHECK( output.gradeLift[1] == 0.0f );
	CHECK( output.gradeLift[2] == 0.0f );
	CHECK( output.gradeGamma[0] == 1.0f );
	CHECK( output.gradeGamma[1] == 1.0f );
	CHECK( output.gradeGamma[2] == 1.0f );
	CHECK( output.gradeGain[0] == 1.0f );
	CHECK( output.gradeGain[1] == 1.0f );
	CHECK( output.gradeGain[2] == 1.0f );
	CHECK( output.whitePointSourceKelvin == 6504.0f );
	CHECK( output.whitePointTargetKelvin == 6504.0f );
	CHECK( output.lutSize == 0.0f );
	CHECK( output.lutScale == 4.0f );
	CHECK( output.requestedBackend == ROUTPUT_REQUEST_AUTO );
	CHECK( output.selectedBackend == ROUTPUT_BACKEND_SDR_SRGB );
	CHECK( output.nativeBackend == ROUTPUT_BACKEND_SDR_SRGB );
	CHECK( output.outputHardwareActive == qfalse );
	CHECK( output.displayHdrHeadroom == 1.0f );
	CHECK( output.displaySdrWhiteNits == 203.0f );
	CHECK( output.displayMaxNits == 203.0f );
	CHECK( std::strcmp( RendererOutputRequestName( ROUTPUT_REQUEST_HDR10_PQ ), "hdr10-pq" ) == 0 );
	CHECK( std::strcmp( RendererOutputBackendName( ROUTPUT_BACKEND_MACOS_EDR ), "macos-edr" ) == 0 );
	CHECK( std::strcmp( glx::GLX_RenderIR_SceneColorSpaceName( output.sceneColorSpace ), "display-referred-sdr" ) == 0 );
	CHECK( glx::ToneMapOperator::Reinhard == glx::ToneMapOperator::ReinhardSimple );
	CHECK( glx::ToneMapOperator::Aces == glx::ToneMapOperator::AcesFitted );
	CHECK( std::strcmp( glx::GLX_RenderIR_ToneMapName( glx::ToneMapOperator::ReinhardSimple ), "reinhard-simple" ) == 0 );
	CHECK( std::strcmp( glx::GLX_RenderIR_ToneMapLegacyAliasName( glx::ToneMapOperator::Reinhard ), "reinhard" ) == 0 );
	CHECK( std::strcmp( glx::GLX_RenderIR_ToneMapName( glx::ToneMapOperator::AcesFitted ), "aces-fitted" ) == 0 );
	CHECK( std::strcmp( glx::GLX_RenderIR_ToneMapLegacyAliasName( glx::ToneMapOperator::Aces ), "aces" ) == 0 );
	CHECK( std::strcmp( glx::GLX_RenderIR_ExposureReductionName( glx::ExposureReductionAlgorithm::Manual ), "manual" ) == 0 );
	CHECK( std::strcmp( glx::GLX_RenderIR_ExposureReductionName( glx::ExposureReductionAlgorithm::HistogramPercentile ), "histogram-percentile" ) == 0 );
	CHECK( std::strcmp( glx::GLX_RenderIR_ColorGradeName( glx::ColorGradeMode::LiftGammaGainLut3D ), "lgg-lut3d" ) == 0 );
	CHECK( std::strcmp( glx::GLX_RenderIR_OutputPrimariesName( glx::OutputPrimaries::Bt2020 ), "bt2020" ) == 0 );
	CHECK( std::strcmp( glx::GLX_RenderIR_OutputPrimariesContractName( glx::OutputPrimaries::DisplayP3 ), "display-p3-matrix" ) == 0 );
	CHECK( std::strcmp( glx::GLX_RenderIR_OutputPrimariesContractName( glx::OutputPrimaries::Native ), "native-pass-through" ) == 0 );
	CHECK( glx::GLX_RenderIR_OutputPrimariesImplemented( glx::OutputPrimaries::Unknown ) == qfalse );
	CHECK( std::strcmp( glx::GLX_RenderIR_GamutMapName( glx::GamutMapMode::CompressToOutput ), "compress" ) == 0 );
	CHECK( std::strcmp( glx::GLX_RenderIR_DynamicDrawRoleName( glx::DynamicDrawRole::Generic ), "generic" ) == 0 );
	CHECK( std::strcmp( glx::GLX_RenderIR_DynamicDrawRoleName( glx::DynamicDrawRole::DynamicLight ), "dlight" ) == 0 );
	CHECK( glx::GLX_RenderIR_DefaultPassForDynamicDrawRole( glx::DynamicDrawRole::Generic ) ==
		glx::FramePassKind::DynamicScene );
	CHECK( glx::GLX_RenderIR_DefaultPassForDynamicDrawRole( glx::DynamicDrawRole::DynamicLight ) ==
		glx::FramePassKind::DynamicLights );
	CHECK( glx::GLX_RenderIR_DefaultPassForDynamicDrawRole( glx::DynamicDrawRole::PostProcess ) ==
		glx::FramePassKind::PostProcess );
	CHECK( glx::GLX_RenderIR_ClassifyDynamicDrawRole( 0, 0 ) == glx::DynamicDrawRole::Generic );
	CHECK( glx::GLX_RenderIR_ClassifyDynamicDrawRole( GLX_STAGE_DLIGHT_MAP, 0 ) ==
		glx::DynamicDrawRole::DynamicLight );
	CHECK( glx::GLX_RenderIR_ClassifyDynamicDrawRole( 0, GLX_DYNAMIC_CATEGORY_MASK_DLIGHT ) ==
		glx::DynamicDrawRole::DynamicLight );
	CHECK( glx::GLX_RenderIR_ClassifyDynamicDrawRole( GLX_STAGE_SHADOW_PASS, 0 ) ==
		glx::DynamicDrawRole::Shadow );
	CHECK( glx::GLX_RenderIR_ClassifyDynamicDrawRole( 0, GLX_DYNAMIC_CATEGORY_MASK_BEAM ) ==
		glx::DynamicDrawRole::Beam );
	CHECK( glx::GLX_RenderIR_ClassifyDynamicDrawRole( GLX_STAGE_POSTPROCESS_PASS, 0 ) ==
		glx::DynamicDrawRole::PostProcess );

	glx::PostNode post {};
	post.kind = glx::PostNodeKind::BloomFinal;
	post.pass = glx::FramePassKind::PostProcess;
	post.sequence = 1;
	post.output = output;

	CHECK( glx::GLX_RenderIR_ValidateUploadPlan( upload ) == qtrue );
	CHECK( glx::GLX_RenderIR_ValidateMaterial( material ) == qtrue );
	CHECK( glx::GLX_RenderIR_ValidateWorldPacket( packet ) == qtrue );
	CHECK( glx::GLX_RenderIR_ValidateDynamicDraw( draw ) == qtrue );
	CHECK( glx::GLX_RenderIR_ValidateOutputTransform( output ) == qtrue );
	CHECK( glx::GLX_RenderIR_ValidatePostNode( post ) == qtrue );
	const unsigned int outputHash = glx::GLX_RenderIR_HashOutputTransform( output );
	const unsigned int postHash = glx::GLX_RenderIR_HashPostNode( post );
	CHECK( outputHash != 0 );
	CHECK( postHash != 0 );
	glx::OutputTransform changedOutput = output;
	changedOutput.exposure = 1.25f;
	CHECK( glx::GLX_RenderIR_HashOutputTransform( changedOutput ) != outputHash );
	changedOutput = output;
	changedOutput.exposureAlgorithm = glx::ExposureReductionAlgorithm::HistogramPercentile;
	changedOutput.autoExposure = qtrue;
	CHECK( glx::GLX_RenderIR_HashOutputTransform( changedOutput ) != outputHash );
	glx::PostNode changedPost = post;
	changedPost.sequence++;
	CHECK( glx::GLX_RenderIR_HashPostNode( changedPost ) != postHash );

	CHECK( glx::GLX_RenderIR_ValidateUploadPlan(
		glx::GLX_RenderIR_MakeUploadPlan( glx::UploadPlanKind::ClientMemory, -1, 0, 0, 0 ) ) == qtrue );
	upload.bytes = 0;
	CHECK( glx::GLX_RenderIR_ValidateUploadPlan( upload ) == qfalse );
	draw.count = 0;
	CHECK( glx::GLX_RenderIR_ValidateDynamicDraw( draw ) == qfalse );
	draw.count = 96;
	draw.role = static_cast<glx::DynamicDrawRole>( 99 );
	CHECK( glx::GLX_RenderIR_ValidateDynamicDraw( draw ) == qfalse );
	output.exposure = -1.0f;
	CHECK( glx::GLX_RenderIR_ValidateOutputTransform( output ) == qfalse );
	output = glx::GLX_RenderIR_DefaultOutputTransform();
	output.autoExposure = qtrue;
	CHECK( glx::GLX_RenderIR_ValidateOutputTransform( output ) == qfalse );
	output = glx::GLX_RenderIR_DefaultOutputTransform();
	output.precisionMode = 0;
	CHECK( glx::GLX_RenderIR_ValidateOutputTransform( output ) == qfalse );
	output = glx::GLX_RenderIR_DefaultOutputTransform();
	output.requestedPrecisionMode = 4;
	CHECK( glx::GLX_RenderIR_ValidateOutputTransform( output ) == qfalse );
	output = glx::GLX_RenderIR_DefaultOutputTransform();
	output.bloomSoftKnee = 1.1f;
	CHECK( glx::GLX_RenderIR_ValidateOutputTransform( output ) == qfalse );
	output = glx::GLX_RenderIR_DefaultOutputTransform();
	output.sceneColorSpace = glx::SceneColorSpace::SceneLinear;
	CHECK( glx::GLX_RenderIR_ValidateOutputTransform( output ) == qfalse );
	output.hdrMode = 1;
	output.precisionMode = 16;
	output.toneMap = glx::ToneMapOperator::AcesFitted;
	CHECK( glx::GLX_RenderIR_ValidateOutputTransform( output ) == qtrue );
	output.selectedBackend = ROUTPUT_BACKEND_HDR10_PQ;
	output.outputHardwareActive = qtrue;
	CHECK( glx::GLX_RenderIR_ValidateOutputTransform( output ) == qfalse );
	output.transfer = glx::OutputTransfer::Hdr10Pq;
	output.outputPrimaries = glx::OutputPrimaries::Bt2020;
	CHECK( glx::GLX_RenderIR_ValidateOutputTransform( output ) == qtrue );
	output.grade = glx::ColorGradeMode::LiftGammaGainLut3D;
	output.gradeGamma[0] = 1.1f;
	output.whitePointTargetKelvin = 6000.0f;
	output.lutSize = 16.0f;
	output.lutScale = 4.0f;
	CHECK( glx::GLX_RenderIR_ValidateOutputTransform( output ) == qtrue );
	output.gradeGamma[1] = 0.0f;
	CHECK( glx::GLX_RenderIR_ValidateOutputTransform( output ) == qfalse );
	output.gradeGamma[1] = 1.0f;
	output.lutSize = -1.0f;
	CHECK( glx::GLX_RenderIR_ValidateOutputTransform( output ) == qfalse );
	output = glx::GLX_RenderIR_DefaultOutputTransform();
	output.outputHardwareActive = qtrue;
	CHECK( glx::GLX_RenderIR_ValidateOutputTransform( output ) == qfalse );

	return true;
}

bool RenderIRProjectedDlightRecordsValidate()
{
	const float origin[3] = { 128.0f, -64.0f, 32.0f };
	const float color[3] = { 1.0f, 0.5f, 0.25f };
	glx::ProjectedDlightRecord records[2] = {
		glx::GLX_RenderIR_MakeProjectedDlightRecord(
			origin, 320.0f, color, glx::GLX_PROJECTED_DLIGHT_ADDITIVE ),
		glx::GLX_RenderIR_MakeProjectedDlightRecord(
			origin, 96.0f, color, glx::GLX_PROJECTED_DLIGHT_LINEAR )
	};

	CHECK( glx::GLX_RenderIR_ValidateProjectedDlightRecord( records[0] ) == qtrue );
	CHECK( glx::GLX_RenderIR_ValidateProjectedDlightRecords( records, 2 ) == qtrue );
	CHECK( glx::GLX_RenderIR_ValidateProjectedDlightRecords( nullptr, 0 ) == qtrue );
	CHECK( glx::GLX_RenderIR_ValidateProjectedDlightRecords( nullptr, 1 ) == qfalse );

	glx::ProjectedDlightRecord invalid = records[0];
	invalid.radius = 0.0f;
	CHECK( glx::GLX_RenderIR_ValidateProjectedDlightRecord( invalid ) == qfalse );
	invalid = records[0];
	invalid.color[1] = -0.1f;
	CHECK( glx::GLX_RenderIR_ValidateProjectedDlightRecord( invalid ) == qfalse );
	invalid = records[0];
	invalid.flags = 0x80000000u;
	CHECK( glx::GLX_RenderIR_ValidateProjectedDlightRecord( invalid ) == qfalse );

	glx::ProjectedDlightListRef noLights {};
	CHECK( glx::GLX_RenderIR_ValidateProjectedDlightListRef( noLights ) == qtrue );
	glx::ProjectedDlightListRef packetLights {};
	packetLights.firstRecord = 1;
	packetLights.recordCount = 1;
	CHECK( glx::GLX_RenderIR_ValidateProjectedDlightListRef( packetLights ) == qtrue );
	packetLights.firstRecord = glx::GLX_RENDER_IR_PROJECTED_DLIGHT_RECORD_LIMIT;
	CHECK( glx::GLX_RenderIR_ValidateProjectedDlightListRef( packetLights ) == qtrue );
	packetLights.firstRecord = glx::GLX_RENDER_IR_PROJECTED_DLIGHT_LIST_RECORD_LIMIT;
	CHECK( glx::GLX_RenderIR_ValidateProjectedDlightListRef( packetLights ) == qfalse );
	packetLights.firstRecord = 2;
	packetLights.recordCount = glx::GLX_RENDER_IR_PROJECTED_DLIGHT_RECORD_LIMIT;
	CHECK( glx::GLX_RenderIR_ValidateProjectedDlightListRef( packetLights ) == qtrue );
	packetLights.firstRecord = glx::GLX_RENDER_IR_PROJECTED_DLIGHT_LIST_RECORD_LIMIT -
		glx::GLX_RENDER_IR_PROJECTED_DLIGHT_RECORD_LIMIT + 1;
	CHECK( glx::GLX_RenderIR_ValidateProjectedDlightListRef( packetLights ) == qfalse );
	packetLights.firstRecord = 4;
	packetLights.recordCount = 0;
	CHECK( glx::GLX_RenderIR_ValidateProjectedDlightListRef( packetLights ) == qfalse );

	glx::ProjectedDlightRecord arena[4] {};
	glx::ProjectedDlightListBuildResult list = glx::GLX_RenderIR_BuildProjectedDlightList(
		records, 2, 0x3u, arena, 4, 1 );
	CHECK( list.complete == qtrue );
	CHECK( list.ref.firstRecord == 1 );
	CHECK( list.ref.recordCount == 2 );
	CHECK( list.copiedMask == 0x3u );
	CHECK( list.droppedMask == 0u );
	CHECK( arena[1].radius == records[0].radius );
	CHECK( arena[2].radius == records[1].radius );
	CHECK( glx::GLX_RenderIR_ValidateProjectedDlightListRef( list.ref ) == qtrue );

	list = glx::GLX_RenderIR_BuildProjectedDlightList( records, 2, 0x3u, arena, 2, 1 );
	CHECK( list.complete == qfalse );
	CHECK( list.ref.firstRecord == 1 );
	CHECK( list.ref.recordCount == 1 );
	CHECK( list.copiedMask == 0x1u );
	CHECK( list.droppedMask == 0x2u );

	glx::ProjectedDlightRecord wideArena[glx::GLX_RENDER_IR_PROJECTED_DLIGHT_RECORD_LIMIT + 4] {};
	list = glx::GLX_RenderIR_BuildProjectedDlightList( records, 2, 0x3u, wideArena,
		glx::GLX_RENDER_IR_PROJECTED_DLIGHT_RECORD_LIMIT + 4,
		glx::GLX_RENDER_IR_PROJECTED_DLIGHT_RECORD_LIMIT );
	CHECK( list.complete == qtrue );
	CHECK( list.ref.firstRecord == glx::GLX_RENDER_IR_PROJECTED_DLIGHT_RECORD_LIMIT );
	CHECK( list.ref.recordCount == 2 );
	CHECK( list.copiedMask == 0x3u );
	CHECK( list.droppedMask == 0u );

	list = glx::GLX_RenderIR_BuildProjectedDlightList( records, 2, 0x4u, arena, 4, 0 );
	CHECK( list.complete == qfalse );
	CHECK( list.ref.firstRecord == 0 );
	CHECK( list.ref.recordCount == 0 );
	CHECK( list.droppedMask == 0x4u );

	records[1].color[0] = -1.0f;
	list = glx::GLX_RenderIR_BuildProjectedDlightList( records, 2, 0x3u, arena, 4, 0 );
	CHECK( list.complete == qfalse );
	CHECK( list.ref.firstRecord == 0 );
	CHECK( list.ref.recordCount == 1 );
	CHECK( list.copiedMask == 0x1u );
	CHECK( list.droppedMask == 0x2u );
	records[1].color[0] = color[0];

	glx::UploadPlan staticUpload = glx::GLX_RenderIR_MakeUploadPlan(
		glx::UploadPlanKind::StaticWorld, -1, 512, 384, 128 );
	glx::MaterialIR material = glx::GLX_RenderIR_MakeMaterial(
		0, GLX_STAGE_LIGHTMAP, GLX_MATERIAL_STATE_DEPTHMASK_TRUE, 1 );
	glx::WorldPacket packet {};
	packet.pass = glx::FramePassKind::SkyAndOpaqueWorld;
	packet.surfaces = 1;
	packet.vertexes = 8;
	packet.indexes = 12;
	packet.itemCount = 1;
	packet.projectedDlights.firstRecord = 0;
	packet.projectedDlights.recordCount = 2;
	packet.material = material;
	packet.upload = staticUpload;
	CHECK( glx::GLX_RenderIR_ValidateWorldPacket( packet ) == qtrue );
	CHECK( glx::GLX_RenderIR_TierSupportsWorldPacket( glx::RenderProductTier::GL46, packet ) == qtrue );
	packet.projectedDlights.firstRecord = glx::GLX_RENDER_IR_PROJECTED_DLIGHT_LIST_RECORD_LIMIT;
	CHECK( glx::GLX_RenderIR_ValidateWorldPacket( packet ) == qfalse );
	CHECK( glx::GLX_RenderIR_TierSupportsWorldPacket( glx::RenderProductTier::GL46, packet ) == qfalse );

	glx::DynamicDraw draw {};
	draw.kind = glx::DynamicDrawKind::Indexed;
	draw.role = glx::DynamicDrawRole::DynamicLight;
	draw.pass = glx::FramePassKind::DynamicLights;
	draw.primitive = 0x0004;
	draw.count = 6;
	draw.indexType = 0x1405;
	draw.projectedDlights.firstRecord = 0;
	draw.projectedDlights.recordCount = 1;
	draw.material = material;
	draw.upload = glx::GLX_RenderIR_MakeUploadPlan(
		glx::UploadPlanKind::ClientMemory, -1, 0, 0, 0 );
	CHECK( glx::GLX_RenderIR_ValidateDynamicDraw( draw ) == qtrue );
	glx::ProjectedDlightShaderInput shaderInput =
		glx::GLX_RenderIR_MakeProjectedDlightShaderInput( draw, qtrue );
	CHECK( glx::GLX_RenderIR_ValidateProjectedDlightShaderInput( shaderInput ) == qtrue );
	CHECK( shaderInput.programmable == qtrue );
	CHECK( shaderInput.target == glx::ProjectedDlightShaderTarget::DynamicDraw );
	CHECK( shaderInput.dynamicRole == glx::DynamicDrawRole::DynamicLight );
	glx::ProjectedDlightShaderUniformPlan uniformPlan =
		glx::GLX_RenderIR_PlanProjectedDlightUniformWindow( shaderInput, 3, qtrue );
	CHECK( uniformPlan.valid == qtrue );
	CHECK( uniformPlan.requestedRecords == 1 );
	CHECK( uniformPlan.uploadRecords == 1 );
	CHECK( uniformPlan.truncatedRecords == 0u );
	CHECK( uniformPlan.limitSuppressed == qfalse );
	CHECK( uniformPlan.execute == qtrue );
	glx::ProjectedDlightShaderExecutionPlan executionPlan =
		glx::GLX_RenderIR_PlanProjectedDlightShaderExecution(
			shaderInput, 3, qtrue, qfalse, nullptr, 32u, 64 );
	CHECK( executionPlan.valid == qtrue );
	CHECK( executionPlan.execute == qtrue );
	CHECK( executionPlan.backend == glx::ProjectedDlightShaderBackend::UniformWindow );
	CHECK( executionPlan.resourcePromoted == qfalse );
	CHECK( executionPlan.uniformRecords == 1 );
	CHECK( executionPlan.streamRecords == 0u );
	CHECK( executionPlan.limitSuppressed == qfalse );
	glx::ProjectedDlightShaderReplacementPlan replacementPlan =
		glx::GLX_RenderIR_PlanProjectedDlightShaderReplacement(
			shaderInput, qtrue, qtrue, executionPlan );
	CHECK( replacementPlan.requested == qtrue );
	CHECK( replacementPlan.replace == qtrue );
	CHECK( replacementPlan.legacyFallback == qfalse );
	replacementPlan = glx::GLX_RenderIR_PlanProjectedDlightShaderReplacement(
		shaderInput, qfalse, qtrue, executionPlan );
	CHECK( replacementPlan.requested == qfalse );
	CHECK( replacementPlan.replace == qfalse );
	CHECK( replacementPlan.legacyFallback == qfalse );
	glx::ProjectedDlightShaderExecutionPlan emptyExecutionPlan {};
	replacementPlan = glx::GLX_RenderIR_PlanProjectedDlightShaderReplacement(
		shaderInput, qtrue, qfalse, emptyExecutionPlan );
	CHECK( replacementPlan.requested == qtrue );
	CHECK( replacementPlan.replace == qfalse );
	CHECK( replacementPlan.legacyFallback == qtrue );
	glx::ProjectedDlightShaderStreamPlan streamPlan =
		glx::GLX_RenderIR_PlanProjectedDlightStreamUpload(
			shaderInput, glx::RenderProductTier::GL46, qtrue, 32u, 64 );
	CHECK( streamPlan.valid == qtrue );
	CHECK( streamPlan.eligible == qtrue );
	CHECK( streamPlan.upload == qtrue );
	CHECK( streamPlan.recordCount == 1 );
	CHECK( streamPlan.bytes == 32u );
	glx::ProjectedDlightDynamicMdiPlan mdiPlan =
		glx::GLX_RenderIR_PlanProjectedDlightDynamicMdi(
			draw, glx::RenderProductTier::GL46, qtrue, qtrue, 4u, 64 );
	CHECK( mdiPlan.valid == qtrue );
	CHECK( mdiPlan.eligible == qfalse );
	CHECK( mdiPlan.commandReady == qfalse );
	draw.indices = reinterpret_cast<const void *>( static_cast<uintptr_t>( 32 ) );
	draw.legacyReason = GLX_LEGACY_DELEGATION_NONE;
	draw.upload = glx::GLX_RenderIR_MakeUploadPlan(
		glx::UploadPlanKind::TransientStream,
		static_cast<int>( glx::StreamStrategy::PersistentMapped ), 96, 0, 24 );
	mdiPlan = glx::GLX_RenderIR_PlanProjectedDlightDynamicMdi(
		draw, glx::RenderProductTier::GL46, qtrue, qtrue, 4u, 64 );
	CHECK( mdiPlan.valid == qtrue );
	CHECK( mdiPlan.eligible == qtrue );
	CHECK( mdiPlan.resourceReady == qtrue );
	CHECK( mdiPlan.indexed == qtrue );
	CHECK( mdiPlan.commandReady == qtrue );
	CHECK( mdiPlan.drawCount == 1 );
	CHECK( mdiPlan.indexCount == 6u );
	CHECK( mdiPlan.projectedRecordCount == 1u );
	CHECK( mdiPlan.commandBytes == 20u );
	CHECK( mdiPlan.command.count == 6u );
	CHECK( mdiPlan.command.instanceCount == 1u );
	CHECK( mdiPlan.command.firstIndex == 8u );
	CHECK( mdiPlan.command.baseVertex == 0 );
	CHECK( mdiPlan.command.baseInstance == 0u );
	CHECK( mdiPlan.commandUploadPlan.kind == glx::UploadPlanKind::TransientStream );
	CHECK( mdiPlan.commandUploadPlan.strategy ==
		static_cast<int>( glx::StreamStrategy::PersistentMapped ) );
	CHECK( mdiPlan.commandUploadPlan.bytes == 20u );
	CHECK( mdiPlan.commandUploadPlan.indexBytes == 0u );
	CHECK( mdiPlan.commandUploadPlan.alignment == 64 );
	CHECK( mdiPlan.commandUploadPlan.sync == glx::UploadSyncPolicy::PersistentFence );
	glx::ProjectedDlightDynamicMdiCommandUpload commandUpload {};
	commandUpload.valid = qtrue;
	commandUpload.buffer = 7u;
	commandUpload.offset = 128u;
	commandUpload.bytes = 20u;
	glx::ProjectedDlightResourceRange resourceRange {};
	resourceRange.valid = qtrue;
	resourceRange.authoritative = qtrue;
	resourceRange.buffer = 13u;
	resourceRange.offset = 256u;
	resourceRange.bytes = 32u;
	glx::ProjectedDlightDynamicMdiSubmitPlan submitPlan =
		glx::GLX_RenderIR_PlanProjectedDlightDynamicMdiSubmit(
			mdiPlan, commandUpload, draw.primitive, draw.indexType, qfalse,
			&resourceRange, 11u );
	CHECK( submitPlan.valid == qtrue );
	CHECK( submitPlan.eligible == qfalse );
	CHECK( submitPlan.commandUploaded == qtrue );
	CHECK( submitPlan.projectedResourceBound == qtrue );
	CHECK( submitPlan.projectedResourceAuthoritative == qtrue );
	CHECK( submitPlan.drawCount == 1u );
	CHECK( submitPlan.indexCount == 6u );
	CHECK( submitPlan.projectedRecordCount == 1u );
	CHECK( submitPlan.primitive == draw.primitive );
	CHECK( submitPlan.indexType == draw.indexType );
	CHECK( submitPlan.indexBuffer == 11u );
	CHECK( submitPlan.projectedResourceBuffer == 13u );
	CHECK( submitPlan.projectedResourceOffset == 256u );
	CHECK( submitPlan.projectedResourceBytes == 32u );
	CHECK( submitPlan.commandBuffer == 7u );
	CHECK( submitPlan.commandOffset == 128u );
	CHECK( submitPlan.commandBytes == 20u );
	CHECK( submitPlan.commandStride == 20u );
	submitPlan = glx::GLX_RenderIR_PlanProjectedDlightDynamicMdiSubmit(
		mdiPlan, commandUpload, draw.primitive, draw.indexType, qtrue,
		&resourceRange, 11u );
	CHECK( submitPlan.valid == qtrue );
	CHECK( submitPlan.eligible == qtrue );
	glx::ProjectedDlightDynamicMdiSubmitPlan batchPlans[2] {};
	batchPlans[0] = submitPlan;
	batchPlans[1] = submitPlan;
	batchPlans[1].commandOffset = 148u;
	batchPlans[1].indexCount = 4u;
	batchPlans[1].projectedRecordCount = 2u;
	glx::ProjectedDlightDynamicMdiBatchPlan batchPlan =
		glx::GLX_RenderIR_PlanProjectedDlightDynamicMdiBatch(
			batchPlans, 2, 8u );
	CHECK( batchPlan.valid == qtrue );
	CHECK( batchPlan.eligible == qtrue );
	CHECK( batchPlan.projectedResourceAuthoritative == qtrue );
	CHECK( batchPlan.drawCount == 2u );
	CHECK( batchPlan.indexCount == 10u );
	CHECK( batchPlan.projectedRecordCount == 3u );
	CHECK( batchPlan.commandBuffer == 7u );
	CHECK( batchPlan.commandOffset == 128u );
	CHECK( batchPlan.commandBytes == 40u );
	CHECK( batchPlan.commandStride == 20u );
	CHECK( batchPlan.indexBuffer == 11u );
	CHECK( batchPlan.projectedResourceBuffer == 13u );
	CHECK( batchPlan.projectedResourceOffset == 256u );
	CHECK( batchPlan.projectedResourceBytes == 32u );
	CHECK( batchPlan.rejectReason ==
		glx::GLX_RENDER_IR_PROJECTED_DLIGHT_MDI_BATCH_REJECT_NONE );
	CHECK( batchPlan.rejectIndex == -1 );
	batchPlans[1].eligible = qfalse;
	batchPlan = glx::GLX_RenderIR_PlanProjectedDlightDynamicMdiBatch(
		batchPlans, 2, 8u );
	CHECK( batchPlan.valid == qtrue );
	CHECK( batchPlan.eligible == qfalse );
	batchPlans[1] = submitPlan;
	batchPlans[1].commandOffset = 192u;
	batchPlan = glx::GLX_RenderIR_PlanProjectedDlightDynamicMdiBatch(
		batchPlans, 2, 8u );
	CHECK( batchPlan.valid == qtrue );
	CHECK( batchPlan.drawCount == 1u );
	CHECK( batchPlan.rejectReason ==
		glx::GLX_RENDER_IR_PROJECTED_DLIGHT_MDI_BATCH_REJECT_NON_CONTIGUOUS_COMMAND );
	CHECK( batchPlan.rejectIndex == 1 );
	batchPlans[1] = submitPlan;
	batchPlans[1].commandOffset = 148u;
	batchPlans[1].projectedResourceOffset = 512u;
	batchPlan = glx::GLX_RenderIR_PlanProjectedDlightDynamicMdiBatch(
		batchPlans, 2, 8u );
	CHECK( batchPlan.valid == qtrue );
	CHECK( batchPlan.rejectReason ==
		glx::GLX_RENDER_IR_PROJECTED_DLIGHT_MDI_BATCH_REJECT_RESOURCE );
	batchPlans[1] = submitPlan;
	batchPlans[1].commandOffset = 148u;
	batchPlans[1].indexBuffer = 12u;
	batchPlan = glx::GLX_RenderIR_PlanProjectedDlightDynamicMdiBatch(
		batchPlans, 2, 8u );
	CHECK( batchPlan.valid == qtrue );
	CHECK( batchPlan.rejectReason ==
		glx::GLX_RENDER_IR_PROJECTED_DLIGHT_MDI_BATCH_REJECT_INDEX_BUFFER );
	batchPlans[1] = submitPlan;
	batchPlans[1].commandOffset = 148u;
	batchPlans[1].commandBuffer = 9u;
	batchPlan = glx::GLX_RenderIR_PlanProjectedDlightDynamicMdiBatch(
		batchPlans, 2, 8u );
	CHECK( batchPlan.valid == qtrue );
	CHECK( batchPlan.rejectReason ==
		glx::GLX_RENDER_IR_PROJECTED_DLIGHT_MDI_BATCH_REJECT_COMMAND_BUFFER );
	batchPlans[1] = submitPlan;
	batchPlans[1].commandOffset = 148u;
	batchPlans[1].indexType = 0x1403u;
	batchPlan = glx::GLX_RenderIR_PlanProjectedDlightDynamicMdiBatch(
		batchPlans, 2, 8u );
	CHECK( batchPlan.valid == qtrue );
	CHECK( batchPlan.rejectReason ==
		glx::GLX_RENDER_IR_PROJECTED_DLIGHT_MDI_BATCH_REJECT_STATE );
	batchPlans[1] = submitPlan;
	batchPlans[1].commandOffset = 148u;
	batchPlan = glx::GLX_RenderIR_PlanProjectedDlightDynamicMdiBatch(
		batchPlans, 2, 1u );
	CHECK( batchPlan.valid == qtrue );
	CHECK( batchPlan.rejectReason ==
		glx::GLX_RENDER_IR_PROJECTED_DLIGHT_MDI_BATCH_REJECT_CAPACITY );
	batchPlans[0].valid = qfalse;
	batchPlan = glx::GLX_RenderIR_PlanProjectedDlightDynamicMdiBatch(
		batchPlans, 2, 8u );
	CHECK( batchPlan.valid == qfalse );
	CHECK( batchPlan.rejectReason ==
		glx::GLX_RENDER_IR_PROJECTED_DLIGHT_MDI_BATCH_REJECT_INVALID_PLAN );
	commandUpload.offset = 130u;
	submitPlan = glx::GLX_RenderIR_PlanProjectedDlightDynamicMdiSubmit(
		mdiPlan, commandUpload, draw.primitive, draw.indexType, qtrue,
		&resourceRange, 11u );
	CHECK( submitPlan.valid == qfalse );
	CHECK( submitPlan.commandUploaded == qfalse );
	commandUpload.offset = 128u;
	submitPlan = glx::GLX_RenderIR_PlanProjectedDlightDynamicMdiSubmit(
		mdiPlan, commandUpload, draw.primitive, 0u, qtrue,
		&resourceRange, 11u );
	CHECK( submitPlan.valid == qfalse );
	resourceRange.offset = 258u;
	submitPlan = glx::GLX_RenderIR_PlanProjectedDlightDynamicMdiSubmit(
		mdiPlan, commandUpload, draw.primitive, draw.indexType, qtrue,
		&resourceRange, 11u );
	CHECK( submitPlan.valid == qfalse );
	CHECK( submitPlan.projectedResourceBound == qfalse );
	resourceRange.offset = 256u;
	resourceRange.authoritative = qfalse;
	submitPlan = glx::GLX_RenderIR_PlanProjectedDlightDynamicMdiSubmit(
		mdiPlan, commandUpload, draw.primitive, draw.indexType, qtrue,
		&resourceRange, 11u );
	CHECK( submitPlan.valid == qfalse );
	CHECK( submitPlan.projectedResourceBound == qtrue );
	CHECK( submitPlan.projectedResourceAuthoritative == qfalse );
	resourceRange.authoritative = qtrue;
	mdiPlan = glx::GLX_RenderIR_PlanProjectedDlightDynamicMdi(
		draw, glx::RenderProductTier::GL46, qfalse, qtrue, 4u, 64 );
	CHECK( mdiPlan.valid == qtrue );
	CHECK( mdiPlan.eligible == qfalse );
	CHECK( mdiPlan.resourceReady == qtrue );
	CHECK( mdiPlan.commandReady == qtrue );
	mdiPlan = glx::GLX_RenderIR_PlanProjectedDlightDynamicMdi(
		draw, glx::RenderProductTier::GL46, qtrue, qfalse, 4u, 64 );
	CHECK( mdiPlan.valid == qtrue );
	CHECK( mdiPlan.eligible == qfalse );
	CHECK( mdiPlan.resourceReady == qfalse );
	CHECK( mdiPlan.commandReady == qtrue );
	mdiPlan = glx::GLX_RenderIR_PlanProjectedDlightDynamicMdi(
		draw, glx::RenderProductTier::GL41, qtrue, qtrue, 4u, 64 );
	CHECK( mdiPlan.valid == qtrue );
	CHECK( mdiPlan.eligible == qfalse );
	draw.indices = reinterpret_cast<const void *>( static_cast<uintptr_t>( 34 ) );
	mdiPlan = glx::GLX_RenderIR_PlanProjectedDlightDynamicMdi(
		draw, glx::RenderProductTier::GL46, qtrue, qtrue, 4u, 64 );
	CHECK( mdiPlan.valid == qtrue );
	CHECK( mdiPlan.commandReady == qfalse );
	CHECK( mdiPlan.eligible == qfalse );
	streamPlan = glx::GLX_RenderIR_PlanProjectedDlightStreamUpload(
		shaderInput, glx::RenderProductTier::GL41, qtrue, 32u, 64 );
	CHECK( streamPlan.valid == qtrue );
	CHECK( streamPlan.eligible == qfalse );
	CHECK( streamPlan.upload == qfalse );
	shaderInput.projectedDlights.recordCount = 4;
	uniformPlan = glx::GLX_RenderIR_PlanProjectedDlightUniformWindow(
		shaderInput, 3, qtrue );
	CHECK( uniformPlan.valid == qtrue );
	CHECK( uniformPlan.requestedRecords == 4 );
	CHECK( uniformPlan.uploadRecords == 3 );
	CHECK( uniformPlan.truncatedRecords == 1u );
	CHECK( uniformPlan.limitSuppressed == qtrue );
	CHECK( uniformPlan.execute == qfalse );
	streamPlan = glx::GLX_RenderIR_PlanProjectedDlightStreamUpload(
		shaderInput, glx::RenderProductTier::GL46, qtrue, 32u, 64 );
	CHECK( streamPlan.valid == qtrue );
	CHECK( streamPlan.eligible == qtrue );
	CHECK( streamPlan.upload == qtrue );
	CHECK( streamPlan.recordCount == 4 );
	CHECK( streamPlan.bytes == 128u );
	resourceRange.valid = qtrue;
	resourceRange.authoritative = qtrue;
	resourceRange.buffer = 13u;
	resourceRange.offset = 256u;
	resourceRange.bytes = 128u;
	executionPlan = glx::GLX_RenderIR_PlanProjectedDlightShaderExecution(
		shaderInput, 3, qtrue, qtrue, &resourceRange, 32u, 64 );
	CHECK( executionPlan.valid == qtrue );
	CHECK( executionPlan.execute == qtrue );
	CHECK( executionPlan.backend == glx::ProjectedDlightShaderBackend::StreamResource );
	CHECK( executionPlan.resourcePromoted == qtrue );
	CHECK( executionPlan.limitSuppressed == qfalse );
	CHECK( executionPlan.truncatedRecords == 0u );
	CHECK( executionPlan.uniformRecords == 0 );
	CHECK( executionPlan.streamRecords == 4u );
	CHECK( executionPlan.projectedResourceBound == qtrue );
	CHECK( executionPlan.projectedResourceAuthoritative == qtrue );
	replacementPlan = glx::GLX_RenderIR_PlanProjectedDlightShaderReplacement(
		shaderInput, qtrue, qtrue, executionPlan );
	CHECK( replacementPlan.requested == qtrue );
	CHECK( replacementPlan.replace == qtrue );
	CHECK( replacementPlan.legacyFallback == qfalse );
	resourceRange.authoritative = qfalse;
	executionPlan = glx::GLX_RenderIR_PlanProjectedDlightShaderExecution(
		shaderInput, 3, qtrue, qtrue, &resourceRange, 32u, 64 );
	CHECK( executionPlan.valid == qtrue );
	CHECK( executionPlan.execute == qfalse );
	CHECK( executionPlan.backend == glx::ProjectedDlightShaderBackend::NoBackend );
	CHECK( executionPlan.resourcePromoted == qfalse );
	CHECK( executionPlan.limitSuppressed == qtrue );
	CHECK( executionPlan.projectedResourceBound == qtrue );
	CHECK( executionPlan.projectedResourceAuthoritative == qfalse );
	replacementPlan = glx::GLX_RenderIR_PlanProjectedDlightShaderReplacement(
		shaderInput, qtrue, qtrue, executionPlan );
	CHECK( replacementPlan.requested == qtrue );
	CHECK( replacementPlan.replace == qfalse );
	CHECK( replacementPlan.legacyFallback == qtrue );
	resourceRange.authoritative = qtrue;
	resourceRange.bytes = 64u;
	executionPlan = glx::GLX_RenderIR_PlanProjectedDlightShaderExecution(
		shaderInput, 3, qtrue, qtrue, &resourceRange, 32u, 64 );
	CHECK( executionPlan.valid == qtrue );
	CHECK( executionPlan.execute == qfalse );
	CHECK( executionPlan.backend == glx::ProjectedDlightShaderBackend::NoBackend );
	CHECK( executionPlan.resourcePromoted == qfalse );
	CHECK( executionPlan.limitSuppressed == qtrue );
	CHECK( executionPlan.projectedResourceBound == qfalse );
	CHECK( executionPlan.projectedResourceAuthoritative == qfalse );
	replacementPlan = glx::GLX_RenderIR_PlanProjectedDlightShaderReplacement(
		shaderInput, qtrue, qtrue, executionPlan );
	CHECK( replacementPlan.requested == qtrue );
	CHECK( replacementPlan.replace == qfalse );
	CHECK( replacementPlan.legacyFallback == qtrue );
	resourceRange.bytes = 128u;
	executionPlan = glx::GLX_RenderIR_PlanProjectedDlightShaderExecution(
		shaderInput, 3, qtrue, qfalse, &resourceRange, 32u, 64 );
	CHECK( executionPlan.valid == qtrue );
	CHECK( executionPlan.execute == qfalse );
	CHECK( executionPlan.backend == glx::ProjectedDlightShaderBackend::NoBackend );
	CHECK( executionPlan.resourcePromoted == qfalse );
	CHECK( executionPlan.limitSuppressed == qtrue );
	CHECK( executionPlan.projectedResourceBound == qtrue );
	CHECK( executionPlan.projectedResourceAuthoritative == qtrue );
	replacementPlan = glx::GLX_RenderIR_PlanProjectedDlightShaderReplacement(
		shaderInput, qtrue, qtrue, executionPlan );
	CHECK( replacementPlan.requested == qtrue );
	CHECK( replacementPlan.replace == qfalse );
	CHECK( replacementPlan.legacyFallback == qtrue );
	draw.projectedDlights.firstRecord = glx::GLX_RENDER_IR_PROJECTED_DLIGHT_LIST_RECORD_LIMIT;
	CHECK( glx::GLX_RenderIR_ValidateDynamicDraw( draw ) == qfalse );
	CHECK( glx::GLX_RenderIR_ValidateProjectedDlightShaderInput(
		glx::GLX_RenderIR_MakeProjectedDlightShaderInput( draw, qtrue ) ) == qfalse );

	packet.projectedDlights.firstRecord = 0;
	packet.projectedDlights.recordCount = 2;
	shaderInput = glx::GLX_RenderIR_MakeProjectedDlightShaderInput( packet, qtrue );
	executionPlan = glx::GLX_RenderIR_PlanProjectedDlightShaderExecution(
		shaderInput, 3, qtrue, qfalse, nullptr, 32u, 64 );
	replacementPlan = glx::GLX_RenderIR_PlanProjectedDlightShaderReplacement(
		shaderInput, qtrue,
		glx::GLX_RenderIR_TierSupportsWorldPacket( glx::RenderProductTier::GL46,
			packet ),
		executionPlan );
	CHECK( replacementPlan.requested == qtrue );
	CHECK( replacementPlan.replace == qtrue );
	CHECK( replacementPlan.legacyFallback == qfalse );
	shaderInput = glx::GLX_RenderIR_MakeProjectedDlightShaderInput( packet, qfalse );
	CHECK( glx::GLX_RenderIR_ValidateProjectedDlightShaderInput( shaderInput ) == qtrue );
	CHECK( shaderInput.programmable == qfalse );
	CHECK( shaderInput.target == glx::ProjectedDlightShaderTarget::WorldPacket );
	executionPlan = glx::GLX_RenderIR_PlanProjectedDlightShaderExecution(
		shaderInput, 3, qtrue, qfalse, nullptr, 32u, 64 );
	replacementPlan = glx::GLX_RenderIR_PlanProjectedDlightShaderReplacement(
		shaderInput, qtrue, qtrue, executionPlan );
	CHECK( replacementPlan.requested == qtrue );
	CHECK( replacementPlan.replace == qfalse );
	CHECK( replacementPlan.legacyFallback == qtrue );
	shaderInput.projectedDlights.recordCount = 0;
	CHECK( glx::GLX_RenderIR_ValidateProjectedDlightShaderInput( shaderInput ) == qfalse );

	glx::FrameProducts products {};
	products.projectedDlights = records;
	products.projectedDlightCount = 2;
	CHECK( glx::GLX_RenderIR_ValidateProjectedDlightRecords(
		products.projectedDlights, products.projectedDlightCount ) == qtrue );

	return true;
}

bool RenderIRProjectedDlightMdiBatchesValidateOffsetsAndRejects()
{
	const unsigned int commandStride =
		static_cast<unsigned int>( sizeof( glx::DrawElementsIndirectCommand ) );
	glx::DynamicDraw draw = ProjectedDlightMdiTestDraw( 64u, 6, 2 );
	glx::DrawElementsIndirectCommand command {};

	CHECK( glx::GLX_RenderIR_BuildDrawElementsIndirectCommand( draw, 2u,
		&command ) == qtrue );
	CHECK( command.count == 6u );
	CHECK( command.instanceCount == 1u );
	CHECK( command.firstIndex == 32u );
	CHECK( command.baseVertex == 0 );
	CHECK( command.baseInstance == 0u );
	draw.indices = reinterpret_cast<const void *>( static_cast<uintptr_t>( 66u ) );
	CHECK( glx::GLX_RenderIR_BuildDrawElementsIndirectCommand( draw, 4u,
		&command ) == qfalse );
	CHECK( command.count == 0u );
	draw = ProjectedDlightMdiTestDraw( 64u, 6, 2 );

	glx::ProjectedDlightDynamicMdiPlan mdiPlan =
		glx::GLX_RenderIR_PlanProjectedDlightDynamicMdi(
			draw, glx::RenderProductTier::GL46, qtrue, qtrue, 2u, 256 );
	CHECK( mdiPlan.valid == qtrue );
	CHECK( mdiPlan.eligible == qtrue );
	CHECK( mdiPlan.commandReady == qtrue );
	CHECK( mdiPlan.command.firstIndex == 32u );
	CHECK( mdiPlan.commandBytes == commandStride );
	CHECK( mdiPlan.commandUploadPlan.alignment == 256 );

	glx::ProjectedDlightDynamicMdiPlan gl3Plan =
		glx::GLX_RenderIR_PlanProjectedDlightDynamicMdi(
			draw, glx::RenderProductTier::GL3X, qtrue, qtrue, 2u, 256 );
	CHECK( gl3Plan.valid == qtrue );
	CHECK( gl3Plan.commandReady == qtrue );
	CHECK( gl3Plan.eligible == qfalse );
	glx::ProjectedDlightDynamicMdiPlan disabledPlan =
		glx::GLX_RenderIR_PlanProjectedDlightDynamicMdi(
			draw, glx::RenderProductTier::GL46, qfalse, qtrue, 2u, 256 );
	CHECK( disabledPlan.valid == qtrue );
	CHECK( disabledPlan.commandReady == qtrue );
	CHECK( disabledPlan.eligible == qfalse );

	glx::ProjectedDlightDynamicMdiCommandUpload commandUpload {};
	commandUpload.valid = qtrue;
	commandUpload.buffer = 5u;
	commandUpload.offset = 256u;
	commandUpload.bytes = commandStride;
	glx::ProjectedDlightResourceRange resourceRange {};
	resourceRange.valid = qtrue;
	resourceRange.authoritative = qtrue;
	resourceRange.buffer = 9u;
	resourceRange.offset = 512u;
	resourceRange.bytes = 64u;
	glx::ProjectedDlightDynamicMdiSubmitPlan submitPlan =
		glx::GLX_RenderIR_PlanProjectedDlightDynamicMdiSubmit(
			mdiPlan, commandUpload, draw.primitive, draw.indexType, qtrue,
			&resourceRange, 3u );
	CHECK( submitPlan.valid == qtrue );
	CHECK( submitPlan.eligible == qtrue );
	CHECK( submitPlan.commandUploaded == qtrue );
	CHECK( submitPlan.projectedResourceBound == qtrue );
	CHECK( submitPlan.projectedResourceAuthoritative == qtrue );
	CHECK( submitPlan.commandOffset == 256u );
	CHECK( submitPlan.commandBytes == commandStride );
	CHECK( submitPlan.commandStride == commandStride );

	glx::ProjectedDlightDynamicMdiSubmitPlan fallbackSubmitPlan =
		glx::GLX_RenderIR_PlanProjectedDlightDynamicMdiSubmit(
			mdiPlan, commandUpload, draw.primitive, draw.indexType, qfalse,
			&resourceRange, 3u );
	CHECK( fallbackSubmitPlan.valid == qtrue );
	CHECK( fallbackSubmitPlan.eligible == qfalse );

	glx::ProjectedDlightDynamicMdiSubmitPlan batchPlans[3] {};
	for ( unsigned int i = 0; i < 3u; i++ ) {
		batchPlans[i] = submitPlan;
		batchPlans[i].commandOffset = submitPlan.commandOffset + i * commandStride;
		batchPlans[i].indexCount = 6u + i * 2u;
		batchPlans[i].projectedRecordCount = 1u + i;
	}

	glx::ProjectedDlightDynamicMdiBatchPlan batchPlan =
		glx::GLX_RenderIR_PlanProjectedDlightDynamicMdiBatch(
			batchPlans, 3, 4u );
	CHECK( batchPlan.valid == qtrue );
	CHECK( batchPlan.eligible == qtrue );
	CHECK( batchPlan.drawCount == 3u );
	CHECK( batchPlan.indexCount == 24u );
	CHECK( batchPlan.projectedRecordCount == 6u );
	CHECK( batchPlan.commandOffset == submitPlan.commandOffset );
	CHECK( batchPlan.commandBytes == commandStride * 3u );
	CHECK( batchPlan.commandStride == commandStride );
	CHECK( batchPlan.rejectReason ==
		glx::GLX_RENDER_IR_PROJECTED_DLIGHT_MDI_BATCH_REJECT_NONE );
	CHECK( batchPlan.rejectIndex == -1 );

	batchPlans[1].eligible = qfalse;
	batchPlan = glx::GLX_RenderIR_PlanProjectedDlightDynamicMdiBatch(
		batchPlans, 3, 4u );
	CHECK( batchPlan.valid == qtrue );
	CHECK( batchPlan.eligible == qfalse );
	CHECK( batchPlan.rejectReason ==
		glx::GLX_RENDER_IR_PROJECTED_DLIGHT_MDI_BATCH_REJECT_NONE );

	CHECK( glx::GLX_RenderIR_PlanProjectedDlightDynamicMdiBatch(
		nullptr, 3, 4u ).rejectReason ==
		glx::GLX_RENDER_IR_PROJECTED_DLIGHT_MDI_BATCH_REJECT_INVALID_INPUT );
	batchPlans[0] = submitPlan;
	batchPlans[0].valid = qfalse;
	batchPlan = glx::GLX_RenderIR_PlanProjectedDlightDynamicMdiBatch(
		batchPlans, 3, 4u );
	CHECK( batchPlan.valid == qfalse );
	CHECK( batchPlan.rejectReason ==
		glx::GLX_RENDER_IR_PROJECTED_DLIGHT_MDI_BATCH_REJECT_INVALID_PLAN );
	CHECK( batchPlan.rejectIndex == 0 );

	batchPlans[0] = submitPlan;
	batchPlans[1] = submitPlan;
	batchPlans[1].commandOffset = submitPlan.commandOffset + commandStride * 2u;
	batchPlan = glx::GLX_RenderIR_PlanProjectedDlightDynamicMdiBatch(
		batchPlans, 2, 4u );
	CHECK( batchPlan.rejectReason ==
		glx::GLX_RENDER_IR_PROJECTED_DLIGHT_MDI_BATCH_REJECT_NON_CONTIGUOUS_COMMAND );
	CHECK( batchPlan.rejectIndex == 1 );

	batchPlans[1] = submitPlan;
	batchPlans[1].commandOffset = submitPlan.commandOffset + commandStride;
	batchPlans[1].primitive = 0x0005u;
	batchPlan = glx::GLX_RenderIR_PlanProjectedDlightDynamicMdiBatch(
		batchPlans, 2, 4u );
	CHECK( batchPlan.rejectReason ==
		glx::GLX_RENDER_IR_PROJECTED_DLIGHT_MDI_BATCH_REJECT_STATE );

	batchPlans[1] = submitPlan;
	batchPlans[1].commandOffset = submitPlan.commandOffset + commandStride;
	batchPlans[1].projectedResourceBytes += 16u;
	batchPlan = glx::GLX_RenderIR_PlanProjectedDlightDynamicMdiBatch(
		batchPlans, 2, 4u );
	CHECK( batchPlan.rejectReason ==
		glx::GLX_RENDER_IR_PROJECTED_DLIGHT_MDI_BATCH_REJECT_RESOURCE );

	batchPlans[1] = submitPlan;
	batchPlans[1].commandOffset = submitPlan.commandOffset + commandStride;
	batchPlans[1].indexBuffer = 4u;
	batchPlan = glx::GLX_RenderIR_PlanProjectedDlightDynamicMdiBatch(
		batchPlans, 2, 4u );
	CHECK( batchPlan.rejectReason ==
		glx::GLX_RENDER_IR_PROJECTED_DLIGHT_MDI_BATCH_REJECT_INDEX_BUFFER );

	batchPlans[1] = submitPlan;
	batchPlans[1].commandOffset = submitPlan.commandOffset + commandStride;
	batchPlans[1].commandBuffer = 6u;
	batchPlan = glx::GLX_RenderIR_PlanProjectedDlightDynamicMdiBatch(
		batchPlans, 2, 4u );
	CHECK( batchPlan.rejectReason ==
		glx::GLX_RENDER_IR_PROJECTED_DLIGHT_MDI_BATCH_REJECT_COMMAND_BUFFER );

	batchPlans[1] = submitPlan;
	batchPlans[1].commandOffset = submitPlan.commandOffset + commandStride;
	batchPlans[1].commandBytes = commandStride * 2u;
	batchPlan = glx::GLX_RenderIR_PlanProjectedDlightDynamicMdiBatch(
		batchPlans, 2, 4u );
	CHECK( batchPlan.rejectReason ==
		glx::GLX_RENDER_IR_PROJECTED_DLIGHT_MDI_BATCH_REJECT_NON_CONTIGUOUS_COMMAND );

	for ( unsigned int i = 0; i < 3u; i++ ) {
		batchPlans[i] = submitPlan;
		batchPlans[i].commandOffset = submitPlan.commandOffset + i * commandStride;
	}
	batchPlan = glx::GLX_RenderIR_PlanProjectedDlightDynamicMdiBatch(
		batchPlans, 3, 2u );
	CHECK( batchPlan.valid == qtrue );
	CHECK( batchPlan.drawCount == 2u );
	CHECK( batchPlan.rejectReason ==
		glx::GLX_RENDER_IR_PROJECTED_DLIGHT_MDI_BATCH_REJECT_CAPACITY );
	CHECK( batchPlan.rejectIndex == 2 );

	return true;
}

bool ExecutorConsumesProjectedDlightMdiPlansAndSubmittedDrawAccounting()
{
	glx::Capabilities caps {};
	caps.tier = glx::RenderProductTier::GL46;
	glx::ExecutorState state {};
	glx::GLX_Executor_Init( &state, caps );
	glx::DynamicDraw draw = ProjectedDlightMdiTestDraw( 96u, 12, 3 );
	glx::ProjectedDlightDynamicMdiPlan mdiPlan =
		glx::GLX_RenderIR_PlanProjectedDlightDynamicMdi(
			draw, glx::RenderProductTier::GL46, qtrue, qtrue, 2u, 64 );

	CHECK( mdiPlan.valid == qtrue );
	CHECK( mdiPlan.eligible == qtrue );
	CHECK( glx::GLX_Executor_ConsumeProjectedDlightDynamicMdiPlan(
		&state, mdiPlan ) == qtrue );
	CHECK( state.highEndProjectedDlightMdiCandidates == 1u );
	CHECK( state.highEndProjectedDlightMdiRecords == 3u );
	CHECK( state.highEndProjectedDlightMdiIndexes == 12u );
	CHECK( state.rejectedProducts == 0u );

	glx::GLX_Test_ResetDrawStubs();
	CHECK( glx::GLX_Executor_ConsumeDynamicDraw( &state, draw ) == qtrue );
	CHECK( glx::g_testDrawElementsCalls == 0 );
	CHECK( glx::g_testDrawArraysCalls == 0 );
	CHECK( state.dynamicDraws == 1u );
	CHECK( state.dynamicDrawsWithProjectedDlights == 1u );
	CHECK( state.dynamicDrawProjectedDlightRecords == 3u );
	CHECK( state.dynamicIndexes == 12u );
	CHECK( state.dynamicVertices == 0u );
	const int dlightRole = static_cast<int>( glx::DynamicDrawRole::DynamicLight );
	const int dlightPass = static_cast<int>( glx::FramePassKind::DynamicLights );
	CHECK( state.dynamicDrawRoleDraws[dlightRole] == 1u );
	CHECK( state.dynamicDrawRoleIndexes[dlightRole] == 12u );
	CHECK( state.dynamicDrawPassDraws[dlightPass] == 1u );
	CHECK( state.dynamicDrawPassIndexes[dlightPass] == 12u );
	CHECK( state.highEndDraws == 1u );
	CHECK( state.highEndPersistentUploads == 1u );
	CHECK( state.highEndSyncUploads == 1u );
	CHECK( state.highEndDynamicBufferProducts == 1u );
	CHECK( state.highEndDsaProducts == 1u );

	glx::GLX_Test_ResetDrawStubs();
	CHECK( glx::GLX_Executor_ExecuteDynamicDraw( &state, draw ) == qtrue );
	CHECK( glx::g_testDrawElementsCalls == 1 );
	CHECK( glx::g_testDrawArraysCalls == 0 );

	glx::ExecutorState gl41State {};
	caps.tier = glx::RenderProductTier::GL41;
	glx::GLX_Executor_Init( &gl41State, caps );
	CHECK( glx::GLX_Executor_ConsumeProjectedDlightDynamicMdiPlan(
		&gl41State, mdiPlan ) == qfalse );
	CHECK( gl41State.rejectedProducts == 1u );
	CHECK( gl41State.highEndProjectedDlightMdiCandidates == 0u );

	glx::ExecutorState fallbackState {};
	caps.tier = glx::RenderProductTier::GL46;
	glx::GLX_Executor_Init( &fallbackState, caps );
	glx::ProjectedDlightDynamicMdiPlan fallbackPlan =
		glx::GLX_RenderIR_PlanProjectedDlightDynamicMdi(
			draw, glx::RenderProductTier::GL46, qfalse, qtrue, 2u, 64 );
	CHECK( fallbackPlan.valid == qtrue );
	CHECK( fallbackPlan.commandReady == qtrue );
	CHECK( fallbackPlan.eligible == qfalse );
	CHECK( glx::GLX_Executor_ConsumeProjectedDlightDynamicMdiPlan(
		&fallbackState, fallbackPlan ) == qfalse );
	CHECK( fallbackState.rejectedProducts == 1u );
	CHECK( fallbackState.highEndProjectedDlightMdiCandidates == 0u );

	return true;
}

bool RenderIRTierMappingKeepsSingleProductContract()
{
	glx::FeatureSet features {};

	CHECK( glx::GLX_RenderIR_TierForVersionAndFeatures( 1, 2, features ) == glx::RenderProductTier::GL12 );
	CHECK( glx::GLX_RenderIR_TierForVersionAndFeatures( 2, 1, features ) == glx::RenderProductTier::GL2X );
	CHECK( glx::GLX_RenderIR_TierForVersionAndFeatures( 3, 3, features ) == glx::RenderProductTier::GL3X );
	CHECK( glx::GLX_RenderIR_TierForVersionAndFeatures( 4, 1, features ) == glx::RenderProductTier::GL41 );
	CHECK( glx::GLX_RenderIR_TierForVersionAndFeatures( 4, 6, features ) == glx::RenderProductTier::GL46 );

	features.bufferStorage = qtrue;
	features.syncObjects = qtrue;
	features.directStateAccess = qtrue;
	CHECK( glx::GLX_RenderIR_TierForVersionAndFeatures( 4, 6, features ) == glx::RenderProductTier::GL46 );

	const glx::RenderProductTier tiers[] = {
		glx::RenderProductTier::GL12,
		glx::RenderProductTier::GL2X,
		glx::RenderProductTier::GL3X,
		glx::RenderProductTier::GL41,
		glx::RenderProductTier::GL46
	};
	const glx::RenderProductKind products[] = {
		glx::RenderProductKind::FramePass,
		glx::RenderProductKind::WorldPacket,
		glx::RenderProductKind::DynamicDraw,
		glx::RenderProductKind::MaterialIR,
		glx::RenderProductKind::UploadPlan,
		glx::RenderProductKind::PostNode,
		glx::RenderProductKind::OutputTransform
	};

	for ( const glx::RenderProductTier tier : tiers ) {
		for ( const glx::RenderProductKind product : products ) {
			CHECK( glx::GLX_RenderIR_TierConsumesProduct( tier, product ) == qtrue );
		}
	}
	CHECK( glx::GLX_RenderIR_TierName( glx::RenderProductTier::GL46 )[0] == 'G' );

	return true;
}

bool GL12ExecutorPolicyIsFixedFunctionAndSdrOnly()
{
	const glx::TierExecutionPolicy policy =
		glx::GLX_RenderIR_TierExecutionPolicy( glx::RenderProductTier::GL12 );

	CHECK( std::strcmp( policy.executorName, "fixed-function" ) == 0 );
	CHECK( policy.fixedFunction == qtrue );
	CHECK( policy.clientMemoryDraws == qtrue );
	CHECK( policy.streamUploads == qfalse );
	CHECK( policy.materialCompiler == qfalse );
	CHECK( policy.modernPostChain == qfalse );
	CHECK( policy.sceneLinearOutput == qfalse );
	CHECK( policy.fboPostProcess == qfalse );
	CHECK( policy.uboFrameObjectConstants == qfalse );
	CHECK( policy.timerQueries == qfalse );
	CHECK( policy.syncAwareUploads == qfalse );
	CHECK( policy.staticBufferOwnership == qfalse );
	CHECK( policy.dynamicBufferOwnership == qfalse );
	CHECK( policy.persistentUploads == qfalse );
	CHECK( policy.indirectSubmission == qfalse );
	CHECK( policy.directStateAccess == qfalse );
	CHECK( policy.lightmaps == qtrue );
	CHECK( policy.multitexture == qtrue );
	CHECK( policy.fog == qtrue );
	CHECK( policy.sprites == qtrue );
	CHECK( policy.beams == qtrue );
	CHECK( policy.dynamicLights == qtrue );
	CHECK( policy.stencilShadowsIfAvailable == qtrue );
	CHECK( policy.screenshots == qtrue );
	CHECK( policy.demos == qtrue );
	CHECK( std::strstr( policy.unavailable, "GLSL material compiler" ) != nullptr );

	glx::UploadPlan clientUpload = glx::GLX_RenderIR_MakeUploadPlan(
		glx::UploadPlanKind::ClientMemory, -1, 0, 0, 0 );
	glx::UploadPlan streamUpload = glx::GLX_RenderIR_MakeUploadPlan(
		glx::UploadPlanKind::TransientStream, 1, 128, 64, 32 );
	glx::UploadPlan staticUpload = glx::GLX_RenderIR_MakeUploadPlan(
		glx::UploadPlanKind::StaticWorld, -1, 128, 64, 32 );

	CHECK( glx::GLX_RenderIR_TierSupportsUploadPlan( glx::RenderProductTier::GL12, clientUpload ) == qtrue );
	CHECK( glx::GLX_RenderIR_TierSupportsUploadPlan( glx::RenderProductTier::GL12, staticUpload ) == qtrue );
	CHECK( glx::GLX_RenderIR_TierSupportsUploadPlan( glx::RenderProductTier::GL12, streamUpload ) == qfalse );
	CHECK( glx::GLX_RenderIR_TierSupportsUploadPlan( glx::RenderProductTier::GL2X, streamUpload ) == qtrue );

	glx::MaterialIR material = glx::GLX_RenderIR_MakeMaterial(
		0, GLX_STAGE_LIGHTMAP | GLX_STAGE_MULTITEXTURE |
			GLX_STAGE_SHADOW_PASS | GLX_STAGE_BEAM_PASS,
		GLX_MATERIAL_STATE_DEPTHMASK_TRUE, 2 );
	material.fogPass = qtrue;

	glx::DynamicDraw draw {};
	draw.kind = glx::DynamicDrawKind::Indexed;
	draw.pass = glx::FramePassKind::DynamicScene;
	draw.primitive = 0x0004;
	draw.count = 6;
	draw.indexType = 0x1403;
	draw.indices = reinterpret_cast<const void *>( static_cast<uintptr_t>( 0x20 ) );
	draw.legacyReason = GLX_LEGACY_DELEGATION_GENERIC;
	draw.profilerPath = GLX_DRAW_GENERIC;
	draw.material = material;
	draw.upload = clientUpload;
	CHECK( glx::GLX_RenderIR_TierSupportsDynamicDraw( glx::RenderProductTier::GL12, draw ) == qtrue );
	draw.upload = streamUpload;
	CHECK( glx::GLX_RenderIR_TierSupportsDynamicDraw( glx::RenderProductTier::GL12, draw ) == qfalse );

	glx::WorldPacket packet {};
	packet.pass = glx::FramePassKind::SkyAndOpaqueWorld;
	packet.surfaces = 1;
	packet.vertexes = 4;
	packet.indexes = 6;
	packet.itemCount = 1;
	packet.material = material;
	packet.upload = staticUpload;
	CHECK( glx::GLX_RenderIR_TierSupportsWorldPacket( glx::RenderProductTier::GL12, packet ) == qtrue );

	glx::OutputTransform output = glx::GLX_RenderIR_DefaultOutputTransform();
	CHECK( glx::GLX_RenderIR_TierSupportsOutputTransform( glx::RenderProductTier::GL12, output ) == qtrue );
	output.transfer = glx::OutputTransfer::Hdr10Pq;
	CHECK( glx::GLX_RenderIR_TierSupportsOutputTransform( glx::RenderProductTier::GL12, output ) == qfalse );

	glx::PostNode post {};
	post.kind = glx::PostNodeKind::GammaDirect;
	post.pass = glx::FramePassKind::PostProcess;
	post.sequence = 0;
	post.output = glx::GLX_RenderIR_DefaultOutputTransform();
	CHECK( glx::GLX_RenderIR_TierSupportsPostNode( glx::RenderProductTier::GL12, post ) == qtrue );
	post.kind = glx::PostNodeKind::BloomFinal;
	CHECK( glx::GLX_RenderIR_TierSupportsPostNode( glx::RenderProductTier::GL12, post ) == qfalse );

	return true;
}

bool GL2XExecutorPolicyIsProgrammableAndAvoidsLaterRequirements()
{
	const glx::TierExecutionPolicy policy =
		glx::GLX_RenderIR_TierExecutionPolicy( glx::RenderProductTier::GL2X );

	CHECK( std::strcmp( policy.executorName, "programmable" ) == 0 );
	CHECK( policy.fixedFunction == qfalse );
	CHECK( policy.clientMemoryDraws == qtrue );
	CHECK( policy.streamUploads == qtrue );
	CHECK( policy.materialCompiler == qtrue );
	CHECK( policy.commonMaterials == qtrue );
	CHECK( policy.dynamicEntities == qtrue );
	CHECK( policy.postProcessLite == qtrue );
	CHECK( policy.modernPostChain == qfalse );
	CHECK( policy.sceneLinearOutput == qfalse );
	CHECK( policy.fboPostProcess == qfalse );
	CHECK( policy.uboFrameObjectConstants == qfalse );
	CHECK( policy.timerQueries == qfalse );
	CHECK( policy.syncAwareUploads == qfalse );
	CHECK( policy.staticBufferOwnership == qfalse );
	CHECK( policy.dynamicBufferOwnership == qfalse );
	CHECK( policy.persistentUploads == qfalse );
	CHECK( policy.indirectSubmission == qfalse );
	CHECK( policy.directStateAccess == qfalse );
	CHECK( policy.lightmaps == qtrue );
	CHECK( policy.multitexture == qtrue );
	CHECK( policy.fog == qtrue );
	CHECK( policy.sprites == qtrue );
	CHECK( policy.beams == qtrue );
	CHECK( policy.dynamicLights == qtrue );
	CHECK( policy.screenshots == qtrue );
	CHECK( policy.demos == qtrue );
	CHECK( std::strstr( policy.unavailable, "persistent uploads" ) != nullptr );

	glx::UploadPlan streamUpload = glx::GLX_RenderIR_MakeUploadPlan(
		glx::UploadPlanKind::TransientStream, static_cast<int>( glx::StreamStrategy::MapBufferRange ),
		192, 96, 48 );
	streamUpload.texcoordBytes = 24;
	streamUpload.sync = glx::UploadSyncPolicy::FrameFence;
	CHECK( glx::GLX_RenderIR_TierSupportsUploadPlan( glx::RenderProductTier::GL2X, streamUpload ) == qtrue );

	glx::UploadPlan persistentUpload = streamUpload;
	persistentUpload.strategy = static_cast<int>( glx::StreamStrategy::PersistentMapped );
	persistentUpload.sync = glx::UploadSyncPolicy::PersistentFence;
	CHECK( glx::GLX_RenderIR_TierSupportsUploadPlan( glx::RenderProductTier::GL2X, persistentUpload ) == qfalse );
	CHECK( glx::GLX_RenderIR_TierSupportsUploadPlan( glx::RenderProductTier::GL46, persistentUpload ) == qtrue );

	glx::MaterialIR material = glx::GLX_RenderIR_MakeMaterial(
		0, GLX_STAGE_LIGHTMAP | GLX_STAGE_MULTITEXTURE | GLX_STAGE_TEXMOD |
			GLX_STAGE_DEPTH_FRAGMENT | GLX_STAGE_ENVIRONMENT,
		GLX_MATERIAL_STATE_DEPTHMASK_TRUE | GLX_MATERIAL_STATE_ATEST_GE_80, 2 );
	material.rgbGen = GLX_MATERIAL_RGBGEN_VERTEX;
	material.alphaGen = GLX_MATERIAL_ALPHAGEN_VERTEX;
	material.tcGen0 = GLX_MATERIAL_TCGEN_TEXTURE;
	material.tcGen1 = GLX_MATERIAL_TCGEN_LIGHTMAP;
	material.texMods0 = 1;
	material.materialCombine = GLX_MATERIAL_COMBINE_MODULATE;
	CHECK( glx::GLX_RenderIR_TierSupportsMaterial( glx::RenderProductTier::GL2X, material ) == qtrue );

	glx::DynamicDraw draw {};
	draw.kind = glx::DynamicDrawKind::Indexed;
	draw.pass = glx::FramePassKind::DynamicScene;
	draw.primitive = 0x0004;
	draw.count = 96;
	draw.indexType = 0x1403;
	draw.indices = reinterpret_cast<const void *>( static_cast<uintptr_t>( 0x40 ) );
	draw.legacyReason = GLX_LEGACY_DELEGATION_NONE;
	draw.profilerPath = GLX_DRAW_STREAM_GENERIC;
	draw.material = material;
	draw.upload = streamUpload;
	CHECK( glx::GLX_RenderIR_TierSupportsDynamicDraw( glx::RenderProductTier::GL2X, draw ) == qtrue );
	draw.upload = persistentUpload;
	CHECK( glx::GLX_RenderIR_TierSupportsDynamicDraw( glx::RenderProductTier::GL2X, draw ) == qfalse );

	glx::PostNode post {};
	post.kind = glx::PostNodeKind::BloomFinal;
	post.pass = glx::FramePassKind::PostProcess;
	post.sequence = 1;
	post.output = glx::GLX_RenderIR_DefaultOutputTransform();
	CHECK( glx::GLX_RenderIR_TierSupportsPostNode( glx::RenderProductTier::GL2X, post ) == qtrue );
	post.kind = glx::PostNodeKind::ToneMap;
	CHECK( glx::GLX_RenderIR_TierSupportsPostNode( glx::RenderProductTier::GL2X, post ) == qfalse );

	glx::OutputTransform output = glx::GLX_RenderIR_DefaultOutputTransform();
	CHECK( glx::GLX_RenderIR_TierSupportsOutputTransform( glx::RenderProductTier::GL2X, output ) == qtrue );
	output.transfer = glx::OutputTransfer::Hdr10Pq;
	CHECK( glx::GLX_RenderIR_TierSupportsOutputTransform( glx::RenderProductTier::GL2X, output ) == qfalse );

	return true;
}

bool GL3XExecutorPolicyIsPerformanceAndAvoidsGL4OnlyRequirements()
{
	const glx::TierExecutionPolicy policy =
		glx::GLX_RenderIR_TierExecutionPolicy( glx::RenderProductTier::GL3X );

	CHECK( std::strcmp( policy.executorName, "performance" ) == 0 );
	CHECK( policy.fixedFunction == qfalse );
	CHECK( policy.streamUploads == qtrue );
	CHECK( policy.materialCompiler == qtrue );
	CHECK( policy.commonMaterials == qtrue );
	CHECK( policy.dynamicEntities == qtrue );
	CHECK( policy.modernPostChain == qtrue );
	CHECK( policy.sceneLinearOutput == qtrue );
	CHECK( policy.fboPostProcess == qtrue );
	CHECK( policy.uboFrameObjectConstants == qtrue );
	CHECK( policy.timerQueries == qtrue );
	CHECK( policy.syncAwareUploads == qtrue );
	CHECK( policy.staticBufferOwnership == qtrue );
	CHECK( policy.dynamicBufferOwnership == qtrue );
	CHECK( policy.highQualitySdrOutput == qtrue );
	CHECK( policy.optionalHardwareHdrOutput == qtrue );
	CHECK( policy.persistentUploads == qfalse );
	CHECK( policy.indirectSubmission == qfalse );
	CHECK( policy.directStateAccess == qfalse );
	CHECK( policy.screenshots == qtrue );
	CHECK( policy.demos == qtrue );
	CHECK( std::strstr( policy.unavailable, "persistent" ) != nullptr );
	CHECK( std::strstr( policy.unavailable, "direct-state access" ) != nullptr );

	glx::UploadPlan staticUpload = glx::GLX_RenderIR_MakeUploadPlan(
		glx::UploadPlanKind::StaticWorld, static_cast<int>( glx::StreamStrategy::MapBufferRange ),
		4096, 3072, 1024 );
	staticUpload.sync = glx::UploadSyncPolicy::FrameFence;
	CHECK( glx::GLX_RenderIR_TierSupportsUploadPlan( glx::RenderProductTier::GL3X, staticUpload ) == qtrue );

	glx::UploadPlan streamUpload = glx::GLX_RenderIR_MakeUploadPlan(
		glx::UploadPlanKind::TransientStream, static_cast<int>( glx::StreamStrategy::MapBufferRange ),
		512, 256, 128 );
	streamUpload.texcoordBytes = 64;
	streamUpload.sync = glx::UploadSyncPolicy::FrameFence;
	CHECK( glx::GLX_RenderIR_TierSupportsUploadPlan( glx::RenderProductTier::GL3X, streamUpload ) == qtrue );

	glx::UploadPlan postUpload = glx::GLX_RenderIR_MakeUploadPlan(
		glx::UploadPlanKind::PostProcess, static_cast<int>( glx::StreamStrategy::MapBufferRange ),
		128, 64, 32 );
	postUpload.sync = glx::UploadSyncPolicy::FrameFence;
	CHECK( glx::GLX_RenderIR_TierSupportsUploadPlan( glx::RenderProductTier::GL3X, postUpload ) == qtrue );

	glx::UploadPlan persistentUpload = streamUpload;
	persistentUpload.strategy = static_cast<int>( glx::StreamStrategy::PersistentMapped );
	persistentUpload.sync = glx::UploadSyncPolicy::PersistentFence;
	CHECK( glx::GLX_RenderIR_TierSupportsUploadPlan( glx::RenderProductTier::GL3X, persistentUpload ) == qfalse );
	CHECK( glx::GLX_RenderIR_TierSupportsUploadPlan( glx::RenderProductTier::GL46, persistentUpload ) == qtrue );

	glx::MaterialIR material = glx::GLX_RenderIR_MakeMaterial(
		0, GLX_STAGE_LIGHTMAP | GLX_STAGE_MULTITEXTURE | GLX_STAGE_TEXMOD |
			GLX_STAGE_DEPTH_FRAGMENT | GLX_STAGE_ENVIRONMENT,
		GLX_MATERIAL_STATE_DEPTHMASK_TRUE | GLX_MATERIAL_STATE_ATEST_GE_80, 2 );
	material.rgbGen = GLX_MATERIAL_RGBGEN_VERTEX;
	material.alphaGen = GLX_MATERIAL_ALPHAGEN_VERTEX;
	CHECK( glx::GLX_RenderIR_TierSupportsMaterial( glx::RenderProductTier::GL3X, material ) == qtrue );

	glx::WorldPacket packet {};
	packet.pass = glx::FramePassKind::SkyAndOpaqueWorld;
	packet.surfaces = 4;
	packet.vertexes = 128;
	packet.indexes = 192;
	packet.itemCount = 4;
	packet.material = material;
	packet.upload = staticUpload;
	CHECK( glx::GLX_RenderIR_TierSupportsWorldPacket( glx::RenderProductTier::GL3X, packet ) == qtrue );
	packet.upload = persistentUpload;
	CHECK( glx::GLX_RenderIR_TierSupportsWorldPacket( glx::RenderProductTier::GL3X, packet ) == qfalse );

	glx::DynamicDraw draw {};
	draw.kind = glx::DynamicDrawKind::Indexed;
	draw.pass = glx::FramePassKind::DynamicScene;
	draw.primitive = 0x0004;
	draw.count = 96;
	draw.indexType = 0x1403;
	draw.indices = reinterpret_cast<const void *>( static_cast<uintptr_t>( 0x80 ) );
	draw.legacyReason = GLX_LEGACY_DELEGATION_NONE;
	draw.profilerPath = GLX_DRAW_STREAM_GENERIC;
	draw.material = material;
	draw.upload = streamUpload;
	CHECK( glx::GLX_RenderIR_TierSupportsDynamicDraw( glx::RenderProductTier::GL3X, draw ) == qtrue );
	draw.upload = persistentUpload;
	CHECK( glx::GLX_RenderIR_TierSupportsDynamicDraw( glx::RenderProductTier::GL3X, draw ) == qfalse );

	glx::OutputTransform output = glx::GLX_RenderIR_DefaultOutputTransform();
	output.transfer = glx::OutputTransfer::LinearSrgb;
	output.sceneColorSpace = glx::SceneColorSpace::SceneLinear;
	output.hdrMode = 1;
	output.precisionMode = 16;
	output.toneMap = glx::ToneMapOperator::AcesFitted;
	output.bloomSoftKnee = 0.5f;
	CHECK( glx::GLX_RenderIR_TierSupportsOutputTransform( glx::RenderProductTier::GL3X, output ) == qtrue );
	CHECK( glx::GLX_RenderIR_TierSupportsOutputTransform( glx::RenderProductTier::GL2X, output ) == qfalse );

	glx::PostNode post {};
	post.kind = glx::PostNodeKind::ToneMap;
	post.pass = glx::FramePassKind::PostProcess;
	post.sequence = 2;
	post.output = output;
	CHECK( glx::GLX_RenderIR_TierSupportsPostNode( glx::RenderProductTier::GL3X, post ) == qtrue );
	CHECK( glx::GLX_RenderIR_TierSupportsPostNode( glx::RenderProductTier::GL2X, post ) == qfalse );
	post.kind = glx::PostNodeKind::Grade;
	CHECK( glx::GLX_RenderIR_TierSupportsPostNode( glx::RenderProductTier::GL3X, post ) == qtrue );

	return true;
}

bool PostOutputPlansSeparatePlannedAndExecutableOwnership()
{
	glx::OutputTransform output = glx::GLX_RenderIR_DefaultOutputTransform();
	output.sceneColorSpace = glx::SceneColorSpace::SceneLinear;
	output.transfer = glx::OutputTransfer::SdrSrgb;
	output.toneMap = glx::ToneMapOperator::AcesFitted;
	output.grade = glx::ColorGradeMode::LiftGammaGain;
	output.hdrMode = 1;
	output.precisionMode = 16;
	output.paperWhiteNits = 203.0f;
	output.maxOutputNits = 203.0f;

	glx::PostOutputPlanInputs inputs {};
	inputs.tier = glx::RenderProductTier::GL3X;
	inputs.output = output;
	inputs.fboReady = qtrue;
	inputs.programReady = qtrue;
	inputs.framebufferFnsReady = qtrue;
	inputs.outputContractValid = qtrue;
	inputs.bloomAvailable = qfalse;
	inputs.postShaderExecutorEnabled = qtrue;
	inputs.minimized = qfalse;
	inputs.windowAdjusted = qfalse;
	inputs.screenshotMask = 0;
	inputs.fboReadIndex = 2;
	inputs.sequenceBase = 7;
	inputs.flags = 0x2u;

	glx::PostOutputPlan plan = glx::GLX_RenderIR_BuildPostOutputPlan( inputs );
	CHECK( plan.glxOwned == qtrue );
	CHECK( plan.executorImplemented == qtrue );
	CHECK( plan.executableNodeCount == 3 );
	CHECK( plan.outputTransformExecutable == qtrue );
	CHECK( plan.fallbackReasons == glx::GLX_POST_OUTPUT_FALLBACK_NONE );
	CHECK( plan.outputTransformPresent == qtrue );
	CHECK( plan.predictedResult == GLX_POSTPROCESS_RESULT_GAMMA_DIRECT );
	CHECK( plan.nodeCount == 3 );
	CHECK( plan.nodes[0].kind == glx::PostNodeKind::Grade );
	CHECK( plan.nodes[1].kind == glx::PostNodeKind::ToneMap );
	CHECK( plan.nodes[2].kind == glx::PostNodeKind::GammaDirect );
	CHECK( plan.nodes[0].sequence == 7 );
	CHECK( plan.nodes[2].outputTarget == 0 );
	CHECK( plan.hash != 0u );
	for ( int i = 0; i < plan.nodeCount; i++ ) {
		CHECK( glx::GLX_RenderIR_TierSupportsPostNode( inputs.tier, plan.nodes[i] ) == qtrue );
	}
	CHECK( glx::GLX_RenderIR_TierSupportsOutputTransform( inputs.tier, plan.output ) == qtrue );

	inputs.tier = glx::RenderProductTier::GL2X;
	plan = glx::GLX_RenderIR_BuildPostOutputPlan( inputs );
	CHECK( plan.glxOwned == qfalse );
	CHECK( ( plan.fallbackReasons & glx::GLX_POST_OUTPUT_FALLBACK_TIER ) != 0u );
	CHECK( ( plan.fallbackReasons & glx::GLX_POST_OUTPUT_FALLBACK_EXECUTOR_NOT_IMPLEMENTED ) != 0u );
	CHECK( plan.predictedResult == GLX_POSTPROCESS_RESULT_GAMMA_DIRECT );

	inputs.tier = glx::RenderProductTier::GL46;
	inputs.minimized = qtrue;
	plan = glx::GLX_RenderIR_BuildPostOutputPlan( inputs );
	CHECK( plan.glxOwned == qfalse );
	CHECK( ( plan.fallbackReasons & glx::GLX_POST_OUTPUT_FALLBACK_MINIMIZED ) != 0u );
	CHECK( ( plan.fallbackReasons & glx::GLX_POST_OUTPUT_FALLBACK_EXECUTOR_NOT_IMPLEMENTED ) == 0u );
	CHECK( plan.predictedResult == GLX_POSTPROCESS_RESULT_MINIMIZED );

	inputs.minimized = qfalse;
	inputs.outputContractValid = qfalse;
	plan = glx::GLX_RenderIR_BuildPostOutputPlan( inputs );
	CHECK( plan.glxOwned == qfalse );
	CHECK( ( plan.fallbackReasons & glx::GLX_POST_OUTPUT_FALLBACK_OUTPUT_CONTRACT ) != 0u );
	CHECK( ( plan.fallbackReasons & glx::GLX_POST_OUTPUT_FALLBACK_EXECUTOR_NOT_IMPLEMENTED ) == 0u );

	inputs.outputContractValid = qtrue;
	inputs.postShaderExecutorEnabled = qfalse;
	plan = glx::GLX_RenderIR_BuildPostOutputPlan( inputs );
	CHECK( plan.glxOwned == qfalse );
	CHECK( plan.executorImplemented == qtrue );
	CHECK( ( plan.fallbackReasons & glx::GLX_POST_OUTPUT_FALLBACK_EXECUTOR_DISABLED ) != 0u );

	inputs.postShaderExecutorEnabled = qtrue;
	inputs.bloomAvailable = qtrue;
	inputs.windowAdjusted = qtrue;
	inputs.screenshotMask = 1;
	output.toneMap = glx::ToneMapOperator::Legacy;
	output.grade = glx::ColorGradeMode::Disabled;
	inputs.output = output;
	plan = glx::GLX_RenderIR_BuildPostOutputPlan( inputs );
	CHECK( plan.glxOwned == qtrue );
	CHECK( plan.executorImplemented == qtrue );
	CHECK( plan.executableNodeCount == 3 );
	CHECK( plan.fallbackReasons == glx::GLX_POST_OUTPUT_FALLBACK_NONE );
	CHECK( plan.predictedResult == GLX_POSTPROCESS_RESULT_GAMMA_BLIT );
	CHECK( plan.nodeCount == 3 );
	CHECK( plan.nodes[0].kind == glx::PostNodeKind::BloomPrefinal );
	CHECK( plan.nodes[1].kind == glx::PostNodeKind::GammaBlit );
	CHECK( plan.nodes[2].kind == glx::PostNodeKind::Screenshot );

	return true;
}

static rendererDisplayOutput_t TestDisplayOutputDefaults()
{
	rendererDisplayOutput_t output {};

	output.valid = qtrue;
	output.displayIndex = 1;
	std::snprintf( output.videoDriver, sizeof( output.videoDriver ), "%s", "sdl" );
	std::snprintf( output.displayName, sizeof( output.displayName ), "%s", "SDR Panel" );
	output.nativeBackend = ROUTPUT_BACKEND_SDR_SRGB;
	output.sdrWhiteNits = 203.0f;
	output.hdrHeadroom = 1.0f;
	output.maxLuminanceNits = 203.0f;
	output.maxFullFrameLuminanceNits = 203.0f;
	return output;
}

bool DisplayOutputStateHashTracksRuntimeHdrCapabilityChanges()
{
	rendererDisplayOutput_t sdr = TestDisplayOutputDefaults();
	glx::GLX_RenderIR_SanitizeDisplayOutput( &sdr );
	const unsigned int sdrHash = glx::GLX_RenderIR_HashDisplayOutput( sdr );
	CHECK( sdrHash != 0u );
	CHECK( glx::GLX_RenderIR_DisplayOutputChangeMask( sdr, sdr ) ==
		glx::GLX_DISPLAY_OUTPUT_CHANGE_NONE );

	rendererDisplayOutput_t jitter = sdr;
	jitter.hdrHeadroom = 1.004f;
	glx::GLX_RenderIR_SanitizeDisplayOutput( &jitter );
	CHECK( glx::GLX_RenderIR_HashDisplayOutput( jitter ) == sdrHash );
	CHECK( glx::GLX_RenderIR_DisplayOutputChangeMask( sdr, jitter ) ==
		glx::GLX_DISPLAY_OUTPUT_CHANGE_NONE );

	rendererDisplayOutput_t windowsHdr = sdr;
	std::snprintf( windowsHdr.displayName, sizeof( windowsHdr.displayName ), "%s", "HDR Panel" );
	windowsHdr.nativeBackend = ROUTPUT_BACKEND_WINDOWS_SCRGB;
	windowsHdr.hdrEnabled = qtrue;
	windowsHdr.hdrHeadroomValid = qtrue;
	windowsHdr.hdrHeadroom = 4.0f;
	windowsHdr.maxLuminanceNits = 812.0f;
	windowsHdr.maxFullFrameLuminanceNits = 812.0f;
	windowsHdr.iccProfileAvailable = qtrue;
	windowsHdr.iccProfileBytes = 2048;
	windowsHdr.windowsAdvancedColor = qtrue;
	windowsHdr.windowsScRgbSupported = qtrue;
	windowsHdr.windowsHdr10Supported = qtrue;
	glx::GLX_RenderIR_SanitizeDisplayOutput( &windowsHdr );
	const unsigned int windowsHash = glx::GLX_RenderIR_HashDisplayOutput( windowsHdr );
	const unsigned int windowsMask = glx::GLX_RenderIR_DisplayOutputChangeMask(
		sdr, windowsHdr );
	CHECK( windowsHash != 0u );
	CHECK( windowsHash != sdrHash );
	CHECK( ( windowsMask & glx::GLX_DISPLAY_OUTPUT_CHANGE_DISPLAY ) != 0u );
	CHECK( ( windowsMask & glx::GLX_DISPLAY_OUTPUT_CHANGE_BACKEND ) != 0u );
	CHECK( ( windowsMask & glx::GLX_DISPLAY_OUTPUT_CHANGE_HDR ) != 0u );
	CHECK( ( windowsMask & glx::GLX_DISPLAY_OUTPUT_CHANGE_HEADROOM ) != 0u );
	CHECK( ( windowsMask & glx::GLX_DISPLAY_OUTPUT_CHANGE_LUMINANCE ) != 0u );
	CHECK( ( windowsMask & glx::GLX_DISPLAY_OUTPUT_CHANGE_ICC ) != 0u );
	CHECK( ( windowsMask & glx::GLX_DISPLAY_OUTPUT_CHANGE_PLATFORM_CAPS ) != 0u );

	rendererDisplayOutput_t appleEdr = sdr;
	appleEdr.nativeBackend = ROUTPUT_BACKEND_MACOS_EDR;
	appleEdr.hdrHeadroomValid = qtrue;
	appleEdr.hdrHeadroom = 2.5f;
	appleEdr.maxLuminanceNits = 507.5f;
	appleEdr.maxFullFrameLuminanceNits = 507.5f;
	appleEdr.macosEdrSupported = qtrue;
	glx::GLX_RenderIR_SanitizeDisplayOutput( &appleEdr );
	const unsigned int appleMask = glx::GLX_RenderIR_DisplayOutputChangeMask(
		sdr, appleEdr );
	CHECK( ( appleMask & glx::GLX_DISPLAY_OUTPUT_CHANGE_BACKEND ) != 0u );
	CHECK( ( appleMask & glx::GLX_DISPLAY_OUTPUT_CHANGE_HEADROOM ) != 0u );
	CHECK( ( appleMask & glx::GLX_DISPLAY_OUTPUT_CHANGE_PLATFORM_CAPS ) != 0u );
	CHECK( ( appleMask & glx::GLX_DISPLAY_OUTPUT_CHANGE_HDR ) == 0u );

	rendererDisplayOutput_t bad = sdr;
	bad.nativeBackend = ROUTPUT_BACKEND_MACOS_EDR;
	bad.hdrHeadroomValid = qtrue;
	bad.hdrHeadroom = std::numeric_limits<float>::quiet_NaN();
	bad.sdrWhiteNits = -1.0f;
	bad.maxLuminanceNits = std::numeric_limits<float>::infinity();
	bad.iccProfileAvailable = qtrue;
	bad.iccProfileBytes = -4;
	glx::GLX_RenderIR_SanitizeDisplayOutput( &bad );
	CHECK( bad.nativeBackend == ROUTPUT_BACKEND_SDR_SRGB );
	CHECK( bad.hdrHeadroom == 1.0f );
	CHECK( bad.hdrHeadroomValid == qfalse );
	CHECK( bad.sdrWhiteNits == 80.0f );
	CHECK( bad.maxLuminanceNits >= bad.sdrWhiteNits );
	CHECK( bad.iccProfileAvailable == qfalse );
	CHECK( bad.iccProfileBytes == 0 );

	return true;
}

bool CaptureExportPoliciesStayExplicitAndSdrDefault()
{
	CHECK( glx::GLX_RenderIR_CaptureExportPolicyForCvar( 0 ) ==
		glx::CaptureExportPolicy::SdrSrgb );
	CHECK( glx::GLX_RenderIR_CaptureExportPolicyForCvar( 1 ) ==
		glx::CaptureExportPolicy::HdrSceneLinear );
	CHECK( glx::GLX_RenderIR_CaptureExportPolicyForCvar( 2 ) ==
		glx::CaptureExportPolicy::HdrOutput );
	CHECK( std::strcmp( glx::GLX_RenderIR_CaptureExportPolicyName(
		glx::CaptureExportPolicy::SdrSrgb ), "sdr-srgb" ) == 0 );
	CHECK( std::strcmp( glx::GLX_RenderIR_CaptureExportPolicyName(
		glx::CaptureExportPolicy::HdrSceneLinear ), "hdr-scene-linear" ) == 0 );
	CHECK( std::strcmp( glx::GLX_RenderIR_CaptureExportPolicyName(
		glx::CaptureExportPolicy::HdrOutput ), "hdr-output" ) == 0 );
	CHECK( glx::GLX_RenderIR_CaptureExportPolicyHdrAware(
		glx::CaptureExportPolicy::SdrSrgb ) == qfalse );
	CHECK( glx::GLX_RenderIR_CaptureExportPolicyHdrAware(
		glx::CaptureExportPolicy::HdrSceneLinear ) == qtrue );
	CHECK( glx::GLX_RenderIR_ResolveCaptureExportPolicy(
		glx::CaptureExportPolicy::HdrSceneLinear ) ==
		glx::CaptureExportPolicy::SdrSrgb );
	CHECK( glx::GLX_RenderIR_ResolveCaptureExportPolicy(
		glx::CaptureExportPolicy::HdrOutput ) ==
		glx::CaptureExportPolicy::SdrSrgb );
	CHECK( glx::GLX_RenderIR_CaptureExportPolicySupported(
		glx::CaptureExportPolicy::SdrSrgb ) == qtrue );
	CHECK( glx::GLX_RenderIR_CaptureExportPolicySupported(
		glx::CaptureExportPolicy::HdrSceneLinear ) == qfalse );
	CHECK( glx::GLX_RenderIR_CaptureExportPolicySupported(
		glx::CaptureExportPolicy::HdrOutput ) == qfalse );

	glx::OutputTransform display = glx::GLX_RenderIR_DefaultOutputTransform();
	display.sceneColorSpace = glx::SceneColorSpace::SceneLinear;
	display.transfer = glx::OutputTransfer::Hdr10Pq;
	display.hdrMode = 1;
	display.precisionMode = 16;
	display.toneMap = glx::ToneMapOperator::AcesFitted;
	display.outputPrimaries = glx::OutputPrimaries::Bt2020;
	display.selectedBackend = ROUTPUT_BACKEND_HDR10_PQ;
	display.outputHardwareActive = qtrue;
	display.maxOutputNits = 1000.0f;

	glx::OutputTransform capture = glx::GLX_RenderIR_CaptureOutputTransform(
		display, glx::CaptureExportPolicy::SdrSrgb );
	CHECK( capture.transfer == glx::OutputTransfer::ScreenshotSrgb );
	CHECK( capture.sceneColorSpace == glx::SceneColorSpace::DisplayReferredSdr );
	CHECK( capture.toneMap == glx::ToneMapOperator::Legacy );
	CHECK( capture.grade == glx::ColorGradeMode::Disabled );
	CHECK( capture.outputPrimaries == glx::OutputPrimaries::SrgbBt709 );
	CHECK( capture.selectedBackend == ROUTPUT_BACKEND_SDR_SRGB );
	CHECK( capture.outputHardwareActive == qfalse );
	CHECK( capture.hdrMode == 0 );
	CHECK( capture.precisionMode == 8 );
	CHECK( glx::GLX_RenderIR_ValidateOutputTransform( capture ) == qtrue );

	glx::PostOutputPlanInputs inputs {};
	inputs.tier = glx::RenderProductTier::GL46;
	inputs.output = display;
	inputs.captureRequest = glx::CaptureExportPolicy::HdrSceneLinear;
	inputs.fboReady = qtrue;
	inputs.programReady = qtrue;
	inputs.framebufferFnsReady = qtrue;
	inputs.outputContractValid = qtrue;
	inputs.postShaderExecutorEnabled = qtrue;
	inputs.screenshotMask = 1;
	inputs.fboReadIndex = 2;

	const glx::PostOutputPlan plan =
		glx::GLX_RenderIR_BuildPostOutputPlan( inputs );
	CHECK( plan.captureRequest == glx::CaptureExportPolicy::HdrSceneLinear );
	CHECK( plan.captureSelected == glx::CaptureExportPolicy::SdrSrgb );
	CHECK( plan.captureHdrAware == qtrue );
	CHECK( plan.captureSupported == qfalse );
	CHECK( plan.nodeCount > 0 );
	CHECK( plan.nodes[plan.nodeCount - 1].kind == glx::PostNodeKind::Screenshot );
	CHECK( plan.nodes[plan.nodeCount - 1].output.transfer ==
		glx::OutputTransfer::ScreenshotSrgb );
	CHECK( plan.nodes[plan.nodeCount - 1].output.sceneColorSpace ==
		glx::SceneColorSpace::DisplayReferredSdr );

	return true;
}

bool HdrColorMathReferencesCoverPipelineContracts()
{
	CHECK( NearlyEqual( glx::GLX_ColorMath_SrgbToLinear( 0.0f ), 0.0f, 0.000001f ) );
	CHECK( NearlyEqual( glx::GLX_ColorMath_SrgbToLinear( 1.0f ), 1.0f, 0.000001f ) );
	CHECK( NearlyEqual( glx::GLX_ColorMath_SrgbToLinear( 0.04045f ), 0.0031308f, 0.000001f ) );
	CHECK( NearlyEqual( glx::GLX_ColorMath_LinearToSrgb( 0.0031308f ), 0.04045f, 0.00005f ) );
	CHECK( NearlyEqual( glx::GLX_ColorMath_SrgbToLinear(
		glx::GLX_ColorMath_LinearToSrgb( 0.18f ) ), 0.18f, 0.0005f ) );

	const float quietNaN = std::numeric_limits<float>::quiet_NaN();
	const float positiveInf = std::numeric_limits<float>::infinity();
	const float negativeInf = -std::numeric_limits<float>::infinity();
	CHECK( glx::GLX_ColorMath_Clamp01( quietNaN ) == 0.0f );
	CHECK( glx::GLX_ColorMath_Clamp01( positiveInf ) == 1.0f );
	CHECK( glx::GLX_ColorMath_Clamp01( negativeInf ) == 0.0f );
	CHECK( glx::GLX_ColorMath_LinearToSrgb( quietNaN ) == 0.0f );
	CHECK( glx::GLX_ColorMath_ToneMapReinhardSimple( positiveInf ) == 1.0f );
	CHECK( glx::GLX_ColorMath_ToneMapReinhard( positiveInf ) == 1.0f );
	CHECK( glx::GLX_ColorMath_ToneMapAcesFitted( positiveInf ) == 1.0f );
	CHECK( glx::GLX_ColorMath_ToneMapAcesFitted(
		std::numeric_limits<float>::max() ) == 1.0f );

	CHECK( NearlyEqual( glx::GLX_ColorMath_ToneMapReinhardSimple( 0.0f ), 0.0f, 0.000001f ) );
	CHECK( NearlyEqual( glx::GLX_ColorMath_ToneMapReinhardSimple( 1.0f ), 0.5f, 0.000001f ) );
	CHECK( NearlyEqual( glx::GLX_ColorMath_ToneMapReinhardSimple( 4.0f ), 0.8f, 0.000001f ) );
	CHECK( glx::GLX_ColorMath_ToneMapReinhardSimple( 8.0f ) > glx::GLX_ColorMath_ToneMapReinhardSimple( 4.0f ) );

	const float aces1 = glx::GLX_ColorMath_ToneMapAcesFitted( 1.0f );
	const float aces2 = glx::GLX_ColorMath_ToneMapAcesFitted( 2.0f );
	CHECK( NearlyEqual( glx::GLX_ColorMath_ToneMapAcesFitted( 0.0f ), 0.0f, 0.000001f ) );
	CHECK( aces1 > 0.80f && aces1 < 0.81f );
	CHECK( aces2 > aces1 );
	CHECK( glx::GLX_ColorMath_ToneMapAcesFitted( 64.0f ) <= 1.0f );

	const float pq0 = glx::GLX_ColorMath_PqEncodeNits( 0.0f, 10000.0f );
	const float pq100 = glx::GLX_ColorMath_PqEncodeNits( 100.0f, 10000.0f );
	const float pq1000 = glx::GLX_ColorMath_PqEncodeNits( 1000.0f, 10000.0f );
	const float pq10000 = glx::GLX_ColorMath_PqEncodeNits( 10000.0f, 10000.0f );
	CHECK( pq0 >= 0.0f && pq0 < 0.00001f );
	CHECK( pq100 > pq0 );
	CHECK( pq1000 > pq100 );
	CHECK( NearlyEqual( pq10000, 1.0f, 0.00001f ) );

	const glx::ColorMathVec3 red { 1.0f, 0.0f, 0.0f };
	const glx::ColorMathVec3 green { 0.0f, 1.0f, 0.0f };
	const glx::ColorMathVec3 blue { 0.0f, 0.0f, 1.0f };
	const glx::ColorMathVec3 white { 1.0f, 1.0f, 1.0f };
	const glx::ColorMathVec3 bt2020Red = glx::GLX_ColorMath_LinearSrgbToBt2020( red );
	const glx::ColorMathVec3 p3Red = glx::GLX_ColorMath_LinearSrgbToDisplayP3( red );
	const glx::ColorMathVec3 p3Green = glx::GLX_ColorMath_LinearSrgbToDisplayP3( green );
	const glx::ColorMathVec3 p3Blue = glx::GLX_ColorMath_LinearSrgbToDisplayP3( blue );
	const glx::ColorMathVec3 p3White = glx::GLX_ColorMath_LinearSrgbToDisplayP3( white );
	CHECK( bt2020Red.r > bt2020Red.g && bt2020Red.r > bt2020Red.b );
	CHECK( p3Red.r > p3Red.g && p3Red.r > p3Red.b );
	CHECK( VecNearlyEqual( p3Red, { 0.8224621f, 0.0331941f, 0.0170827f }, 0.00001f ) );
	CHECK( VecNearlyEqual( p3Green, { 0.1775380f, 0.9668059f, 0.0723974f }, 0.00001f ) );
	CHECK( VecNearlyEqual( p3Blue, { 0.0f, 0.0f, 0.9105199f }, 0.00001f ) );
	CHECK( VecNearlyEqual( p3White, white, 0.00001f ) );
	CHECK( glx::GLX_ColorMath_Luma( green ) > glx::GLX_ColorMath_Luma( red ) );

	const glx::ColorMathVec3 dim { 0.70f, 0.20f, 0.10f };
	const glx::ColorMathVec3 bright { 1.20f, 0.10f, 0.05f };
	const glx::ColorMathVec3 threshold { 0.75f, 0.0f, 0.0f };
	CHECK( glx::GLX_ColorMath_BloomWeight( dim, 0, 0.75f, 1.0f, 0, 0.0f ) == 0.0f );
	CHECK( glx::GLX_ColorMath_BloomWeight( bright, 0, 0.75f, 1.0f, 0, 0.0f ) == 1.0f );
	CHECK( NearlyEqual( glx::GLX_ColorMath_BloomWeight( threshold, 0, 0.75f, 1.0f, 0, 0.5f ),
		0.5f, 0.000001f ) );
	CHECK( glx::GLX_ColorMath_BloomMetric( green, 2 ) > glx::GLX_ColorMath_BloomMetric( red, 2 ) );
	const glx::ColorMathVec3 flareDisplay { 0.5f, 0.25f, 0.0f };
	const glx::ColorMathVec3 flareLegacy =
		glx::GLX_ColorMath_FlareSceneColor( flareDisplay, true, true );
	const glx::ColorMathVec3 flareLinear =
		glx::GLX_ColorMath_FlareSceneColor( flareDisplay, true, false );
	CHECK( VecNearlyEqual( flareLegacy, flareDisplay, 0.000001f ) );
	CHECK( flareLinear.r < flareLegacy.r );
	CHECK( flareLinear.g < flareLegacy.g );
	CHECK( NearlyEqual( flareLinear.r, glx::GLX_ColorMath_SrgbToLinear( 0.5f ), 0.000001f ) );
	CHECK( glx::GLX_ColorMath_SceneLinearBloomWeight(
		{ 0.25f, 0.25f, 0.25f }, 2, 0.75f, 2.0f, 0.0f ) == 0.0f );
	CHECK( glx::GLX_ColorMath_SceneLinearBloomWeight(
		{ 0.50f, 0.50f, 0.50f }, 2, 0.75f, 2.0f, 0.0f ) == 1.0f );

	glx::ColorMathExposureHistogram exposureHistogram {};
	glx::GLX_ColorMath_ExposureHistogramReset( &exposureHistogram, -4.0f, 4.0f );
	CHECK( glx::GLX_ColorMath_ExposureHistogramAddLuma( &exposureHistogram, 0.25f ) == true );
	CHECK( glx::GLX_ColorMath_ExposureHistogramAddLuma( &exposureHistogram, 1.0f ) == true );
	CHECK( glx::GLX_ColorMath_ExposureHistogramAddLuma( &exposureHistogram, 4.0f ) == true );
	CHECK( exposureHistogram.sampleCount == 3u );
	const glx::ColorMathExposureResult exposureP50 =
		glx::GLX_ColorMath_ExposureHistogramPercentile( exposureHistogram,
			50.0f, 0.18f, 0.125f, 8.0f );
	const glx::ColorMathExposureResult exposureP90 =
		glx::GLX_ColorMath_ExposureHistogramPercentile( exposureHistogram,
			90.0f, 0.18f, 0.125f, 8.0f );
	const glx::ColorMathExposureResult exposureAverage =
		glx::GLX_ColorMath_ExposureSimpleAverage( exposureHistogram,
			0.18f, 0.125f, 8.0f );
	CHECK( exposureP50.valid == true );
	CHECK( exposureP90.valid == true );
	CHECK( exposureAverage.valid == true );
	CHECK( exposureP50.measuredLuma > 0.9f && exposureP50.measuredLuma < 1.2f );
	CHECK( exposureP90.measuredLuma > exposureP50.measuredLuma );
	CHECK( exposureP90.exposureScale < exposureP50.exposureScale );
	CHECK( exposureAverage.bin == -1 );

	int lutSize = 0;
	CHECK( glx::GLX_ColorMath_LutAtlasSize( 256, 16, &lutSize ) == true );
	CHECK( lutSize == 16 );
	CHECK( glx::GLX_ColorMath_LutAtlasSize( 1024, 32, &lutSize ) == true );
	CHECK( lutSize == 32 );
	CHECK( glx::GLX_ColorMath_LutAtlasSize( 4096, 64, &lutSize ) == true );
	CHECK( lutSize == 64 );
	CHECK( glx::GLX_ColorMath_LutAtlasSize( 128, 16, &lutSize ) == false );
	CHECK( glx::GLX_ColorMath_LutAtlasSize( 4225, 65, &lutSize ) == false );

	const glx::ColorMathXy d65 = glx::GLX_ColorMath_WhitePointXyFromKelvin( 6504.0f );
	CHECK( NearlyEqual( d65.x, 0.3134f, 0.0010f ) );
	CHECK( NearlyEqual( d65.y, 0.3236f, 0.0010f ) );
	const glx::ColorMathVec3 neutral { 0.18f, 0.18f, 0.18f };
	const glx::ColorMathVec3 sameWhite = glx::GLX_ColorMath_AdaptWhitePointBradford(
		neutral, 6504.0f, 6504.0f );
	CHECK( VecNearlyEqual( sameWhite, neutral, 0.00001f ) );
	float whitePointMatrix[9];
	glx::GLX_ColorMath_BuildBradfordAdaptationMatrix( 6504.0f, 6504.0f,
		whitePointMatrix );
	CHECK( NearlyEqual( whitePointMatrix[0], 1.0f, 0.00001f ) );
	CHECK( NearlyEqual( whitePointMatrix[4], 1.0f, 0.00001f ) );
	CHECK( NearlyEqual( whitePointMatrix[8], 1.0f, 0.00001f ) );
	const glx::ColorMathVec3 warmerWhite = glx::GLX_ColorMath_AdaptWhitePointBradford(
		neutral, 6504.0f, 6000.0f );
	CHECK( warmerWhite.r > neutral.r );
	CHECK( warmerWhite.b < neutral.b );

	const int identitySize = 4;
	static glx::ColorMathVec3 identityLut[identitySize * identitySize * identitySize];
	static glx::ColorMathVec3 solidLut[identitySize * identitySize * identitySize];
	const float identityScale = 4.0f;
	for ( int y = 0; y < identitySize; y++ ) {
		for ( int x = 0; x < identitySize * identitySize; x++ ) {
			identityLut[y * identitySize * identitySize + x] =
				glx::GLX_ColorMath_LutIdentityTexel( identitySize, x, y, identityScale );
			solidLut[y * identitySize * identitySize + x] =
				glx::ColorMathVec3 { 0.25f, 0.50f, 0.75f };
		}
	}
	const glx::ColorMathVec3 lutSample { 1.5f, 2.5f, 0.5f };
	CHECK( VecNearlyEqual( glx::GLX_ColorMath_SampleLutAtlas( identityLut,
		identitySize * identitySize, identitySize, lutSample, identityScale ),
		lutSample, 0.00001f ) );
	const glx::ColorMathVec3 lutClamped { 0.0f, identityScale, 2.0f };
	const glx::ColorMathVec3 lutOutOfRange { -1.0f, 8.0f, 2.0f };
	CHECK( VecNearlyEqual( glx::GLX_ColorMath_SampleLutAtlas( identityLut,
		identitySize * identitySize, identitySize, lutOutOfRange, identityScale ),
		lutClamped, 0.00001f ) );
	CHECK( VecNearlyEqual( glx::GLX_ColorMath_SampleLutAtlas( nullptr,
		identitySize * identitySize, identitySize, lutSample, identityScale ),
		lutSample, 0.00001f ) );

	glx::OutputTransform output = glx::GLX_RenderIR_DefaultOutputTransform();
	output.sceneColorSpace = glx::SceneColorSpace::SceneLinear;
	output.hdrMode = 1;
	output.precisionMode = 16;
	output.transfer = glx::OutputTransfer::LinearSrgb;
	output.grade = glx::ColorGradeMode::LiftGammaGainLut3D;
	output.toneMap = glx::ToneMapOperator::Legacy;
	output.lutSize = static_cast<float>( identitySize );
	output.lutScale = identityScale;
	output.maxOutputNits = output.paperWhiteNits * identityScale;
	const glx::PostOutputReferenceContract identityContract =
		glx::GLX_PostOutputReference_BuildContract( output,
			identitySize * identitySize, identitySize );
	CHECK( identityContract.lutActive == true );
	CHECK( identityContract.lutSize == identitySize );
	CHECK( VecNearlyEqual( glx::GLX_PostOutputReference_Evaluate( lutSample, output,
		identityLut, identitySize * identitySize, identitySize ),
		lutSample, 0.00001f ) );
	output.grade = glx::ColorGradeMode::Lut3D;
	const glx::ColorMathVec3 solidSample = glx::GLX_PostOutputReference_Evaluate(
		lutSample, output, solidLut, identitySize * identitySize, identitySize );
	CHECK( VecNearlyEqual( solidSample, { 0.25f, 0.50f, 0.75f }, 0.00001f ) );
	output.lutSize = static_cast<float>( identitySize + 1 );
	const glx::PostOutputReferenceContract mismatchedContract =
		glx::GLX_PostOutputReference_BuildContract( output,
			identitySize * identitySize, identitySize );
	CHECK( mismatchedContract.lutActive == false );
	CHECK( VecNearlyEqual( glx::GLX_PostOutputReference_Evaluate( lutSample, output,
		solidLut, identitySize * identitySize, identitySize ),
		lutSample, 0.00001f ) );
	output.grade = glx::ColorGradeMode::LiftGammaGainLut3D;
	output.lutSize = static_cast<float>( identitySize );

	output.grade = glx::ColorGradeMode::LiftGammaGain;
	output.lutSize = 0.0f;
	output.gradeLift[0] = 0.10f;
	const glx::ColorMathVec3 lifted = glx::GLX_PostOutputReference_Evaluate(
		neutral, output, nullptr, 0, 0 );
	CHECK( lifted.r > neutral.r );
	CHECK( NearlyEqual( lifted.g, neutral.g, 0.00001f ) );

	output.grade = glx::ColorGradeMode::Disabled;
	output.gradeLift[0] = 0.0f;
	output.toneMap = glx::ToneMapOperator::AcesFitted;
	output.transfer = glx::OutputTransfer::SdrSrgb;
	const glx::ColorMathVec3 acesSdr = glx::GLX_PostOutputReference_Evaluate(
		{ 1.0f, 0.5f, 0.25f }, output, nullptr, 0, 0 );
	CHECK( acesSdr.r > acesSdr.g && acesSdr.g > acesSdr.b );
	CHECK( acesSdr.r <= 1.0f );

	output.toneMap = glx::ToneMapOperator::Legacy;
	output.transfer = glx::OutputTransfer::LinearSrgb;
	output.outputPrimaries = glx::OutputPrimaries::DisplayP3;
	output.gamutMap = glx::GamutMapMode::Disabled;
	const glx::ColorMathVec3 p3Output = glx::GLX_PostOutputReference_Evaluate(
		red, output, nullptr, 0, 0 );
	CHECK( VecNearlyEqual( p3Output, glx::GLX_ColorMath_LinearSrgbToDisplayP3( red ),
		0.00001f ) );
	output.outputPrimaries = glx::OutputPrimaries::Bt2020;
	const glx::ColorMathVec3 bt2020Output = glx::GLX_PostOutputReference_Evaluate(
		red, output, nullptr, 0, 0 );
	CHECK( VecNearlyEqual( bt2020Output, glx::GLX_ColorMath_LinearSrgbToBt2020( red ),
		0.00001f ) );
	output.outputPrimaries = glx::OutputPrimaries::Native;
	CHECK( glx::GLX_RenderIR_ValidateOutputTransform( output ) == qfalse );
	const glx::PostOutputReferenceContract rejectedNativeContract =
		glx::GLX_PostOutputReference_BuildContract( output, 0, 0 );
	CHECK( rejectedNativeContract.outputPrimaries == glx::OutputPrimaries::SrgbBt709 );
	output.selectedBackend = ROUTPUT_BACKEND_LINUX_EXPERIMENTAL_HDR;
	output.outputHardwareActive = qtrue;
	output.outputExperimental = qtrue;
	CHECK( glx::GLX_RenderIR_ValidateOutputTransform( output ) == qtrue );
	const glx::PostOutputReferenceContract nativeContract =
		glx::GLX_PostOutputReference_BuildContract( output, 0, 0 );
	CHECK( nativeContract.outputPrimaries == glx::OutputPrimaries::Native );
	CHECK( VecNearlyEqual( glx::GLX_PostOutputReference_Evaluate( red, output,
		nullptr, 0, 0 ), red, 0.00001f ) );
	output.outputPrimaries = glx::OutputPrimaries::Unknown;
	CHECK( glx::GLX_RenderIR_ValidateOutputTransform( output ) == qfalse );
	const glx::PostOutputReferenceContract unknownContract =
		glx::GLX_PostOutputReference_BuildContract( output, 0, 0 );
	CHECK( unknownContract.outputPrimaries == glx::OutputPrimaries::SrgbBt709 );
	output.selectedBackend = ROUTPUT_BACKEND_SDR_SRGB;
	output.outputHardwareActive = qfalse;
	output.outputExperimental = qfalse;
	output.outputPrimaries = glx::OutputPrimaries::SrgbBt709;
	output.gamutMap = glx::GamutMapMode::Clip;
	CHECK( VecNearlyEqual( glx::GLX_PostOutputReference_Evaluate(
		{ 2.0f, -0.5f, 0.25f }, output, nullptr, 0, 0 ),
		{ 1.0f, 0.0f, 0.25f }, 0.00001f ) );
	output.gamutMap = glx::GamutMapMode::CompressToOutput;
	output.paperWhiteNits = 200.0f;
	output.maxOutputNits = 1000.0f;
	CHECK( VecNearlyEqual( glx::GLX_PostOutputReference_Evaluate(
		{ 8.0f, 2.0f, 0.25f }, output, nullptr, 0, 0 ),
		{ 5.0f, 2.0f, 0.25f }, 0.00001f ) );

	output.toneMap = glx::ToneMapOperator::Legacy;
	output.transfer = glx::OutputTransfer::Hdr10Pq;
	output.outputPrimaries = glx::OutputPrimaries::Bt2020;
	output.gamutMap = glx::GamutMapMode::CompressToOutput;
	output.paperWhiteNits = 203.0f;
	output.maxOutputNits = 1000.0f;
	const glx::ColorMathVec3 hdrDim = glx::GLX_PostOutputReference_Evaluate(
		{ 0.25f, 0.25f, 0.25f }, output, nullptr, 0, 0 );
	const glx::ColorMathVec3 hdrBright = glx::GLX_PostOutputReference_Evaluate(
		{ 1.0f, 1.0f, 1.0f }, output, nullptr, 0, 0 );
	CHECK( hdrBright.r > hdrDim.r );
	CHECK( hdrBright.g > hdrDim.g );
	CHECK( hdrBright.r <= 1.0f && hdrBright.g <= 1.0f && hdrBright.b <= 1.0f );

	glx::OutputTransform pathological = glx::GLX_RenderIR_DefaultOutputTransform();
	pathological.sceneColorSpace = glx::SceneColorSpace::SceneLinear;
	pathological.hdrMode = 1;
	pathological.precisionMode = 16;
	pathological.transfer = glx::OutputTransfer::LinearSrgb;
	pathological.grade = glx::ColorGradeMode::LiftGammaGainLut3D;
	pathological.gamutMap = glx::GamutMapMode::CompressToOutput;
	pathological.outputPrimaries = glx::OutputPrimaries::DisplayP3;
	pathological.exposure = quietNaN;
	pathological.paperWhiteNits = quietNaN;
	pathological.maxOutputNits = negativeInf;
	pathological.displayHdrHeadroom = positiveInf;
	pathological.gradeLift[0] = positiveInf;
	pathological.gradeLift[1] = quietNaN;
	pathological.gradeLift[2] = negativeInf;
	pathological.gradeGamma[0] = 0.0f;
	pathological.gradeGamma[1] = quietNaN;
	pathological.gradeGamma[2] = positiveInf;
	pathological.gradeGain[0] = positiveInf;
	pathological.gradeGain[1] = quietNaN;
	pathological.gradeGain[2] = -1.0f;
	pathological.whitePointSourceKelvin = quietNaN;
	pathological.whitePointTargetKelvin = positiveInf;
	pathological.lutSize = quietNaN;
	pathological.lutScale = negativeInf;
	const glx::PostOutputReferenceContract sanitizedPathological =
		glx::GLX_PostOutputReference_BuildContract( pathological, 0, 0 );
	CHECK( NearlyEqual( sanitizedPathological.exposure, 1.0f, 0.00001f ) );
	CHECK( NearlyEqual( sanitizedPathological.paperWhiteNits, 203.0f, 0.00001f ) );
	CHECK( NearlyEqual( sanitizedPathological.maxOutputNits, 203.0f, 0.00001f ) );
	CHECK( NearlyEqual( sanitizedPathological.displayHdrHeadroom, 1.0f, 0.00001f ) );
	CHECK( sanitizedPathological.lutActive == false );
	const glx::ColorMathVec3 pathologicalOut = glx::GLX_PostOutputReference_Evaluate(
		{ quietNaN, positiveInf, negativeInf }, pathological, nullptr, 0, 0 );
	CHECK( std::isfinite( pathologicalOut.r ) );
	CHECK( std::isfinite( pathologicalOut.g ) );
	CHECK( std::isfinite( pathologicalOut.b ) );
	CHECK( pathologicalOut.r >= 0.0f && pathologicalOut.g >= 0.0f &&
		pathologicalOut.b >= 0.0f );

	return true;
}

bool PostShaderPlansClassifyOutputTransformProgramShape()
{
	glx::OutputTransform output = glx::GLX_RenderIR_DefaultOutputTransform();
	glx::PostShaderPlan plan = glx::GLX_PostShader_BuildPlan( output );
	CHECK( plan.valid == qtrue );
	CHECK( plan.key.sceneLinear == qfalse );
	CHECK( ( plan.featureMask & glx::GLX_POST_SHADER_FEATURE_LEGACY_GAMMA ) != 0u );
	CHECK( ( plan.featureMask & glx::GLX_POST_SHADER_FEATURE_SCENE_LINEAR ) == 0u );
	CHECK( ( plan.featureMask & glx::GLX_POST_SHADER_FEATURE_OUTPUT_TRANSFORM ) != 0u );
	CHECK( plan.textureCount == 1 );
	CHECK( plan.uniformVec4Count == 5 );
	CHECK( plan.hash != 0u );

	output.sceneColorSpace = glx::SceneColorSpace::SceneLinear;
	output.hdrMode = 1;
	output.precisionMode = 16;
	output.transfer = glx::OutputTransfer::SdrSrgb;
	output.toneMap = glx::ToneMapOperator::AcesFitted;
	output.grade = glx::ColorGradeMode::LiftGammaGainLut3D;
	output.whitePointTargetKelvin = 6000.0f;
	output.lutSize = 16.0f;
	output.lutScale = 4.0f;
	plan = glx::GLX_PostShader_BuildPlan( output );
	CHECK( plan.valid == qtrue );
	CHECK( plan.key.sceneLinear == qtrue );
	CHECK( plan.key.lutActive == qtrue );
	CHECK( plan.key.whitePointAdaptation == qtrue );
	CHECK( ( plan.featureMask & glx::GLX_POST_SHADER_FEATURE_SCENE_LINEAR ) != 0u );
	CHECK( ( plan.featureMask & glx::GLX_POST_SHADER_FEATURE_LIFT_GAMMA_GAIN ) != 0u );
	CHECK( ( plan.featureMask & glx::GLX_POST_SHADER_FEATURE_WHITE_POINT ) != 0u );
	CHECK( ( plan.featureMask & glx::GLX_POST_SHADER_FEATURE_LUT_3D ) != 0u );
	CHECK( ( plan.featureMask & glx::GLX_POST_SHADER_FEATURE_TONEMAP_ACES_FITTED ) != 0u );
	CHECK( ( plan.featureMask & glx::GLX_POST_SHADER_FEATURE_ENCODE_SRGB ) != 0u );
	CHECK( ( plan.featureMask & glx::GLX_POST_SHADER_FEATURE_OUTPUT_TRANSFORM ) != 0u );
	CHECK( ( plan.featureMask & glx::GLX_POST_SHADER_FEATURE_BLOOM_COMBINE ) == 0u );
	CHECK( plan.textureCount == 2 );
	CHECK( plan.uniformVec4Count == 12 );
	const unsigned int acesLutHash = plan.hash;

	glx::PostShaderPlan bloomPlan = glx::GLX_PostShader_BuildPlanForOutput( output, qtrue );
	CHECK( bloomPlan.valid == qtrue );
	CHECK( ( bloomPlan.featureMask & glx::GLX_POST_SHADER_FEATURE_BLOOM_COMBINE ) != 0u );
	CHECK( bloomPlan.textureCount == 3 );
	CHECK( bloomPlan.uniformVec4Count == 13 );
	CHECK( bloomPlan.hash != acesLutHash );

	glx::PostShaderPlan bloomPrefinalPlan =
		glx::GLX_PostShader_BuildPlanForPass( output, qtrue, qfalse );
	CHECK( bloomPrefinalPlan.valid == qtrue );
	CHECK( ( bloomPrefinalPlan.featureMask & glx::GLX_POST_SHADER_FEATURE_BLOOM_COMBINE ) != 0u );
	CHECK( ( bloomPrefinalPlan.featureMask & glx::GLX_POST_SHADER_FEATURE_OUTPUT_TRANSFORM ) == 0u );
	CHECK( bloomPrefinalPlan.textureCount == 2 );
	CHECK( bloomPrefinalPlan.uniformVec4Count == 5 );

	output.greyscale = 0.75f;
	plan = glx::GLX_PostShader_BuildPlan( output );
	CHECK( ( plan.featureMask & glx::GLX_POST_SHADER_FEATURE_GREYSCALE ) != 0u );
	output.greyscale = 0.0f;

	glx::OutputTransform exposureOnly = output;
	exposureOnly.exposure = 1.5f;
	CHECK( glx::GLX_PostShader_BuildPlan( exposureOnly ).hash == acesLutHash );
	output.toneMap = glx::ToneMapOperator::ReinhardSimple;
	CHECK( glx::GLX_PostShader_BuildPlan( output ).hash != acesLutHash );

	output.toneMap = glx::ToneMapOperator::Legacy;
	output.grade = glx::ColorGradeMode::Disabled;
	output.lutSize = 0.0f;
	output.transfer = glx::OutputTransfer::Hdr10Pq;
	output.outputPrimaries = glx::OutputPrimaries::Bt2020;
	output.gamutMap = glx::GamutMapMode::CompressToOutput;
	plan = glx::GLX_PostShader_BuildPlan( output );
	CHECK( plan.valid == qtrue );
	CHECK( ( plan.featureMask & glx::GLX_POST_SHADER_FEATURE_ENCODE_HDR10_PQ ) != 0u );
	CHECK( ( plan.featureMask & glx::GLX_POST_SHADER_FEATURE_BT2020_OUTPUT ) != 0u );
	CHECK( ( plan.featureMask & glx::GLX_POST_SHADER_FEATURE_GAMUT_COMPRESS ) != 0u );
	CHECK( ( plan.featureMask & glx::GLX_POST_SHADER_FEATURE_HDR_HEADROOM_OUTPUT ) == 0u );
	CHECK( plan.textureCount == 1 );
	CHECK( plan.uniformVec4Count == 6 );

	output.transfer = glx::OutputTransfer::MacEdr;
	output.outputPrimaries = glx::OutputPrimaries::DisplayP3;
	output.gamutMap = glx::GamutMapMode::CompressToOutput;
	plan = glx::GLX_PostShader_BuildPlan( output );
	CHECK( plan.valid == qtrue );
	CHECK( ( plan.featureMask & glx::GLX_POST_SHADER_FEATURE_DISPLAY_P3_OUTPUT ) != 0u );
	CHECK( ( plan.featureMask & glx::GLX_POST_SHADER_FEATURE_HDR_HEADROOM_OUTPUT ) != 0u );
	CHECK( ( plan.featureMask & glx::GLX_POST_SHADER_FEATURE_GAMUT_COMPRESS ) != 0u );
	CHECK( plan.uniformVec4Count == 5 );

	output.gamutMap = glx::GamutMapMode::Clip;
	plan = glx::GLX_PostShader_BuildPlan( output );
	CHECK( plan.valid == qtrue );
	CHECK( ( plan.featureMask & glx::GLX_POST_SHADER_FEATURE_GAMUT_CLIP ) != 0u );
	CHECK( ( plan.featureMask & glx::GLX_POST_SHADER_FEATURE_GAMUT_COMPRESS ) == 0u );

	output.gamutMap = glx::GamutMapMode::CompressToOutput;
	output.grade = glx::ColorGradeMode::Lut3D;
	output.lutSize = 128.0f;
	output.lutScale = 4.0f;
	plan = glx::GLX_PostShader_BuildPlan( output );
	CHECK( plan.valid == qtrue );
	CHECK( plan.key.lutActive == qfalse );
	CHECK( ( plan.featureMask & glx::GLX_POST_SHADER_FEATURE_LUT_3D ) == 0u );

	output.precisionMode = 8;
	CHECK( glx::GLX_PostShader_BuildPlan( output ).valid == qfalse );
	return true;
}

bool PostShaderFinalEligibilityCoversLegacyGamma()
{
	glx::OutputTransform output = glx::GLX_RenderIR_DefaultOutputTransform();
	output.legacyGamma = 0.75f;
	output.legacyOverbright = 2.0f;
	glx::PostShaderPlan plan = glx::GLX_PostShader_BuildPlan( output );

	CHECK( plan.valid == qtrue );
	CHECK( plan.key.sceneLinear == qfalse );
	CHECK( plan.key.outputTransform == qtrue );
	CHECK( plan.key.transfer == glx::OutputTransfer::SdrSrgb );
	CHECK( ( plan.featureMask & glx::GLX_POST_SHADER_FEATURE_LEGACY_GAMMA ) != 0u );
	CHECK( glx::GLX_PostShader_FinalOutputDomainSupported( plan, output, qtrue ) == qtrue );
	CHECK( glx::GLX_PostShader_FinalCompatibilityRejectMask( plan, output,
		qfalse, qtrue ) == glx::GLX_POST_SHADER_DIRECT_REJECT_NONE );

	glx::PostShaderPlan bloomPlan =
		glx::GLX_PostShader_BuildPlanForOutput( output, qtrue );
	CHECK( bloomPlan.valid == qtrue );
	CHECK( ( bloomPlan.featureMask & glx::GLX_POST_SHADER_FEATURE_BLOOM_COMBINE ) != 0u );
	CHECK( glx::GLX_PostShader_FinalCompatibilityRejectMask( bloomPlan, output,
		qtrue, qtrue ) == glx::GLX_POST_SHADER_DIRECT_REJECT_NONE );

	glx::PostShaderPlan bloomPrefinalPlan =
		glx::GLX_PostShader_BuildPlanForPass( output, qtrue, qfalse );
	CHECK( bloomPrefinalPlan.valid == qtrue );
	CHECK( ( bloomPrefinalPlan.featureMask & glx::GLX_POST_SHADER_FEATURE_OUTPUT_TRANSFORM ) == 0u );
	CHECK( glx::GLX_PostShader_FinalCompatibilityRejectMask( bloomPrefinalPlan,
		output, qtrue, qfalse ) == glx::GLX_POST_SHADER_DIRECT_REJECT_NONE );

	glx::OutputTransform hdrTransferInSdr = output;
	hdrTransferInSdr.transfer = glx::OutputTransfer::Hdr10Pq;
	plan = glx::GLX_PostShader_BuildPlan( hdrTransferInSdr );
	CHECK( plan.valid == qtrue );
	const unsigned int reject = glx::GLX_PostShader_FinalCompatibilityRejectMask(
		plan, hdrTransferInSdr, qfalse, qtrue );
	CHECK( ( reject & glx::GLX_POST_SHADER_DIRECT_REJECT_NOT_SCENE_LINEAR ) != 0u );
	CHECK( ( reject & glx::GLX_POST_SHADER_DIRECT_REJECT_TRANSFER ) != 0u );

	output = glx::GLX_RenderIR_DefaultOutputTransform();
	output.sceneColorSpace = glx::SceneColorSpace::SceneLinear;
	output.hdrMode = 1;
	output.precisionMode = 16;
	output.transfer = glx::OutputTransfer::SdrSrgb;
	output.toneMap = glx::ToneMapOperator::AcesFitted;
	plan = glx::GLX_PostShader_BuildPlan( output );
	CHECK( plan.valid == qtrue );
	CHECK( plan.key.sceneLinear == qtrue );
	CHECK( ( plan.featureMask & glx::GLX_POST_SHADER_FEATURE_SCENE_LINEAR ) != 0u );
	CHECK( glx::GLX_PostShader_FinalCompatibilityRejectMask( plan, output,
		qfalse, qtrue ) == glx::GLX_POST_SHADER_DIRECT_REJECT_NONE );

	return true;
}

bool PostShaderSourcesEmitDeterministicProgramShape()
{
	char vertex[glx::GLX_POST_SHADER_VERTEX_SOURCE_BYTES];
	char fragment[glx::GLX_POST_SHADER_FRAGMENT_SOURCE_BYTES];
	char modernVertex[glx::GLX_POST_SHADER_VERTEX_SOURCE_BYTES];
	char modernFragment[glx::GLX_POST_SHADER_FRAGMENT_SOURCE_BYTES];
	int vertexBytes = 0;
	int fragmentBytes = 0;
	int modernVertexBytes = 0;
	int modernFragmentBytes = 0;

	glx::OutputTransform output = glx::GLX_RenderIR_DefaultOutputTransform();
	glx::PostShaderPlan plan = glx::GLX_PostShader_BuildPlan( output );
	CHECK( glx::GLX_PostShaderSource_TargetForTier(
		glx::RenderProductTier::GL2X, 2, 1 ) == glx::PostShaderSourceTarget::Glsl120 );
	CHECK( glx::GLX_PostShaderSource_TargetForTier(
		glx::RenderProductTier::GL3X, 3, 0 ) == glx::PostShaderSourceTarget::Glsl130 );
	CHECK( glx::GLX_PostShaderSource_TargetForTier(
		glx::RenderProductTier::GL3X, 3, 2 ) == glx::PostShaderSourceTarget::Glsl150Compatibility );
	CHECK( glx::GLX_PostShaderSource_TargetForTier(
		glx::RenderProductTier::GL3X, 3, 3 ) == glx::PostShaderSourceTarget::Glsl330Compatibility );
	CHECK( glx::GLX_PostShaderSource_TargetForTier(
		glx::RenderProductTier::GL41, 4, 1 ) == glx::PostShaderSourceTarget::Glsl410Compatibility );
	CHECK( glx::GLX_PostShaderSource_TargetForTier(
		glx::RenderProductTier::GL46, 4, 6 ) == glx::PostShaderSourceTarget::Glsl410Compatibility );
	CHECK( glx::GLX_PostShaderSource_WriteVertex( vertex, sizeof( vertex ), &vertexBytes ) == qtrue );
	CHECK( glx::GLX_PostShaderSource_WriteFragment( plan, fragment, sizeof( fragment ), &fragmentBytes ) == qtrue );
	CHECK( vertexBytes > 0 );
	CHECK( fragmentBytes > 0 );
	CHECK( std::strstr( vertex, "#version 120" ) != nullptr );
	CHECK( std::strstr( vertex, "gl_MultiTexCoord0" ) != nullptr );
	CHECK( std::strstr( vertex, "gl_ModelViewProjectionMatrix" ) != nullptr );
	CHECK( std::strstr( fragment, "#version 120" ) != nullptr );
	CHECK( std::strstr( fragment, "#define GLX_POST_LEGACY_GAMMA 1" ) != nullptr );
	CHECK( std::strstr( fragment, "#define GLX_POST_SCENE_LINEAR 0" ) != nullptr );
	CHECK( std::strstr( fragment, "#define GLX_POST_LUT_3D 0" ) != nullptr );
	CHECK( std::strstr( fragment, "#define GLX_POST_OUTPUT_TRANSFORM 1" ) != nullptr );
	CHECK( std::strstr( fragment, "#define GLX_POST_BLOOM_COMBINE 0" ) != nullptr );
	CHECK( std::strstr( fragment, "#define GLX_POST_GAMUT_CLIP 0" ) != nullptr );
	CHECK( std::strstr( fragment, "uniform vec4 u_OutputParams1" ) != nullptr );
	CHECK( std::strstr( fragment, "glxFiniteOr" ) != nullptr );
	CHECK( std::strstr( fragment, "glxExposure()" ) != nullptr );
	CHECK( std::strstr( fragment, "glxApplyOutputPrimaries" ) != nullptr );
	CHECK( std::strstr( fragment, "glxApplyGamutMap" ) != nullptr );
	CHECK( std::strstr( fragment, "glxEncodeTransfer" ) != nullptr );
	CHECK( std::strstr( fragment,
		"pow(glxNonNegativeVec3(color), vec3(glxLegacyGamma()))" ) != nullptr );
	CHECK( std::strstr( fragment, "uniform sampler2D u_Bloom" ) != nullptr );
	CHECK( std::strstr( fragment, "uniform sampler2D u_ColorGradeLut" ) != nullptr );

	const glx::PostShaderSourceSummary legacySummary =
		glx::GLX_PostShaderSource_BuildSummary( plan );
	CHECK( legacySummary.valid == qtrue );
	CHECK( legacySummary.truncated == qfalse );
	CHECK( legacySummary.target == glx::PostShaderSourceTarget::Glsl120 );
	CHECK( legacySummary.targetVersion == 120 );
	CHECK( legacySummary.sourceHash != 0u );
	CHECK( legacySummary.vertexBytes == vertexBytes );
	CHECK( legacySummary.fragmentBytes == fragmentBytes );

	CHECK( glx::GLX_PostShaderSource_WriteVertex(
		glx::PostShaderSourceTarget::Glsl410Compatibility, modernVertex,
		sizeof( modernVertex ), &modernVertexBytes ) == qtrue );
	CHECK( glx::GLX_PostShaderSource_WriteFragment( plan,
		glx::PostShaderSourceTarget::Glsl410Compatibility, modernFragment,
		sizeof( modernFragment ), &modernFragmentBytes ) == qtrue );
	CHECK( modernVertexBytes > vertexBytes );
	CHECK( modernFragmentBytes > fragmentBytes );
	CHECK( std::strstr( modernVertex, "#version 410 compatibility" ) != nullptr );
	CHECK( std::strstr( modernVertex, "out vec2 v_TexCoord" ) != nullptr );
	CHECK( std::strstr( modernVertex, "gl_MultiTexCoord0" ) != nullptr );
	CHECK( std::strstr( modernFragment, "#version 410 compatibility" ) != nullptr );
	CHECK( std::strstr( modernFragment, "in vec2 v_TexCoord" ) != nullptr );
	CHECK( std::strstr( modernFragment, "out vec4 glx_FragColor" ) != nullptr );
	CHECK( std::strstr( modernFragment, "#define GLX_POST_SAMPLE2D texture" ) != nullptr );
	CHECK( std::strstr( modernFragment, "glx_FragColor = vec4(color, 1.0)" ) != nullptr );
	const glx::PostShaderSourceSummary modernSummary =
		glx::GLX_PostShaderSource_BuildSummary( plan,
			glx::PostShaderSourceTarget::Glsl410Compatibility );
	CHECK( modernSummary.valid == qtrue );
	CHECK( modernSummary.truncated == qfalse );
	CHECK( modernSummary.target == glx::PostShaderSourceTarget::Glsl410Compatibility );
	CHECK( modernSummary.targetVersion == 410 );
	CHECK( modernSummary.sourceHash != legacySummary.sourceHash );
	CHECK( modernSummary.vertexBytes == modernVertexBytes );
	CHECK( modernSummary.fragmentBytes == modernFragmentBytes );

	output.sceneColorSpace = glx::SceneColorSpace::SceneLinear;
	output.hdrMode = 1;
	output.precisionMode = 16;
	output.transfer = glx::OutputTransfer::Hdr10Pq;
	output.toneMap = glx::ToneMapOperator::AcesFitted;
	output.grade = glx::ColorGradeMode::LiftGammaGainLut3D;
	output.whitePointTargetKelvin = 6000.0f;
	output.lutSize = 32.0f;
	output.lutScale = 8.0f;
	output.outputPrimaries = glx::OutputPrimaries::Bt2020;
	output.gamutMap = glx::GamutMapMode::CompressToOutput;
	plan = glx::GLX_PostShader_BuildPlan( output );
	CHECK( glx::GLX_PostShaderSource_WriteFragment( plan, fragment, sizeof( fragment ), &fragmentBytes ) == qtrue );
	CHECK( std::strstr( fragment, "#define GLX_POST_SCENE_LINEAR 1" ) != nullptr );
	CHECK( std::strstr( fragment, "#define GLX_POST_LIFT_GAMMA_GAIN 1" ) != nullptr );
	CHECK( std::strstr( fragment, "#define GLX_POST_WHITE_POINT 1" ) != nullptr );
	CHECK( std::strstr( fragment, "#define GLX_POST_LUT_3D 1" ) != nullptr );
	CHECK( std::strstr( fragment, "#define GLX_POST_TONEMAP_ACES_FITTED 1" ) != nullptr );
	CHECK( std::strstr( fragment, "#define GLX_POST_TONEMAP_ACES GLX_POST_TONEMAP_ACES_FITTED" ) != nullptr );
	CHECK( std::strstr( fragment, "#define GLX_POST_ENCODE_HDR10_PQ 1" ) != nullptr );
	CHECK( std::strstr( fragment, "#define GLX_POST_BT2020_OUTPUT 1" ) != nullptr );
	CHECK( std::strstr( fragment, "#define GLX_POST_GAMUT_COMPRESS 1" ) != nullptr );
	CHECK( std::strstr( fragment, "#define GLX_POST_OUTPUT_TRANSFORM 1" ) != nullptr );
	CHECK( std::strstr( fragment, "clamp(glxNonNegativeVec3(nits), 0.0, glxMaxOutput())" ) != nullptr );
	CHECK( std::strstr( fragment, "glxPqEncode(color * glxPaperWhite())" ) != nullptr );
	CHECK( std::strstr( fragment, "uniform sampler2D u_ColorGradeLut" ) != nullptr );
	CHECK( std::strstr( fragment, "glxApplyLiftGammaGain" ) != nullptr );
	CHECK( std::strstr( fragment, "glxApplyWhitePoint" ) != nullptr );
	CHECK( std::strstr( fragment, "glxSampleLutAtlas" ) != nullptr );
	CHECK( std::strstr( fragment, "* lutScale" ) != nullptr );
	CHECK( std::strstr( fragment, "vec4(color, 1.0)" ) != nullptr );
	CHECK( std::strstr( fragment, "glxToneMapAcesFitted" ) != nullptr );
	CHECK( std::strstr( fragment, "glxLinearSrgbToBt2020" ) != nullptr );
	CHECK( std::strstr( fragment, "glxPqEncode" ) != nullptr );

	const glx::PostShaderSourceSummary hdrSummary =
		glx::GLX_PostShaderSource_BuildSummary( plan );
	CHECK( hdrSummary.valid == qtrue );
	CHECK( hdrSummary.truncated == qfalse );
	CHECK( hdrSummary.sourceHash != legacySummary.sourceHash );
	CHECK( hdrSummary.fragmentBytes == fragmentBytes );

	output.transfer = glx::OutputTransfer::MacEdr;
	output.outputPrimaries = glx::OutputPrimaries::DisplayP3;
	output.greyscale = 0.5f;
	plan = glx::GLX_PostShader_BuildPlanForOutput( output, qtrue );
	CHECK( glx::GLX_PostShaderSource_WriteFragment( plan, fragment, sizeof( fragment ), &fragmentBytes ) == qtrue );
	CHECK( std::strstr( fragment, "#define GLX_POST_BLOOM_COMBINE 1" ) != nullptr );
	CHECK( std::strstr( fragment, "#define GLX_POST_GREYSCALE 1" ) != nullptr );
	CHECK( std::strstr( fragment, "#define GLX_POST_DISPLAY_P3_OUTPUT 1" ) != nullptr );
	CHECK( std::strstr( fragment, "#define GLX_POST_HDR_HEADROOM_OUTPUT 1" ) != nullptr );
	CHECK( std::strstr( fragment, "GLX_POST_SAMPLE2D(u_Bloom" ) != nullptr );
	CHECK( std::strstr( fragment, "glxApplyGreyscale" ) != nullptr );
	CHECK( std::strstr( fragment, "glxLinearSrgbToDisplayP3" ) != nullptr );
	CHECK( std::strstr( fragment, "clamp(color, 0.0, glxHeadroom())" ) != nullptr );

	output.gamutMap = glx::GamutMapMode::Clip;
	plan = glx::GLX_PostShader_BuildPlanForOutput( output, qfalse );
	CHECK( glx::GLX_PostShaderSource_WriteFragment( plan, fragment, sizeof( fragment ), &fragmentBytes ) == qtrue );
	CHECK( std::strstr( fragment, "#define GLX_POST_GAMUT_CLIP 1" ) != nullptr );
	CHECK( std::strstr( fragment, "return glxSaturate(color)" ) != nullptr );

	plan = glx::GLX_PostShader_BuildPlanForPass( output, qtrue, qfalse );
	CHECK( glx::GLX_PostShaderSource_WriteFragment( plan, fragment, sizeof( fragment ), &fragmentBytes ) == qtrue );
	CHECK( std::strstr( fragment, "#define GLX_POST_OUTPUT_TRANSFORM 0" ) != nullptr );
	CHECK( std::strstr( fragment, "#define GLX_POST_BLOOM_COMBINE 1" ) != nullptr );

	char tiny[32];
	CHECK( glx::GLX_PostShaderSource_WriteFragment( plan, tiny, sizeof( tiny ), nullptr ) == qfalse );
	CHECK( tiny[sizeof( tiny ) - 1] == '\0' );

	output.precisionMode = 8;
	plan = glx::GLX_PostShader_BuildPlan( output );
	CHECK( plan.valid == qfalse );
	CHECK( glx::GLX_PostShaderSource_WriteFragment( plan, fragment, sizeof( fragment ), &fragmentBytes ) == qfalse );
	CHECK( fragmentBytes == 0 );

	return true;
}

bool GL41ExecutorPolicyIsMacModernAndAvoidsUnavailableAppleFeatures()
{
	const glx::TierExecutionPolicy policy =
		glx::GLX_RenderIR_TierExecutionPolicy( glx::RenderProductTier::GL41 );

	CHECK( std::strcmp( policy.executorName, "mac-modern" ) == 0 );
	CHECK( policy.fixedFunction == qfalse );
	CHECK( policy.streamUploads == qtrue );
	CHECK( policy.materialCompiler == qtrue );
	CHECK( policy.commonMaterials == qtrue );
	CHECK( policy.dynamicEntities == qtrue );
	CHECK( policy.modernPostChain == qtrue );
	CHECK( policy.sceneLinearOutput == qtrue );
	CHECK( policy.fboPostProcess == qtrue );
	CHECK( policy.uboFrameObjectConstants == qtrue );
	CHECK( policy.timerQueries == qtrue );
	CHECK( policy.syncAwareUploads == qtrue );
	CHECK( policy.staticBufferOwnership == qtrue );
	CHECK( policy.dynamicBufferOwnership == qtrue );
	CHECK( policy.macOS41Ceiling == qtrue );
	CHECK( policy.highQualitySdrOutput == qtrue );
	CHECK( policy.optionalHardwareHdrOutput == qtrue );
	CHECK( policy.persistentUploads == qfalse );
	CHECK( policy.indirectSubmission == qfalse );
	CHECK( policy.directStateAccess == qfalse );
	CHECK( policy.debugOutputRequired == qfalse );
	CHECK( policy.bufferStorageRequired == qfalse );
	CHECK( policy.directStateAccessRequired == qfalse );
	CHECK( policy.multiDrawIndirectRequired == qfalse );
	CHECK( policy.screenshots == qtrue );
	CHECK( policy.demos == qtrue );
	CHECK( std::strstr( policy.unavailable, "GL4.3" ) != nullptr );
	CHECK( std::strstr( policy.unavailable, "GL4.4" ) != nullptr );
	CHECK( std::strstr( policy.unavailable, "GL4.5" ) != nullptr );

	glx::UploadPlan staticUpload = glx::GLX_RenderIR_MakeUploadPlan(
		glx::UploadPlanKind::StaticWorld, static_cast<int>( glx::StreamStrategy::MapBufferRange ),
		8192, 6144, 2048 );
	staticUpload.sync = glx::UploadSyncPolicy::FrameFence;
	CHECK( glx::GLX_RenderIR_TierSupportsUploadPlan( glx::RenderProductTier::GL41, staticUpload ) == qtrue );

	glx::UploadPlan streamUpload = glx::GLX_RenderIR_MakeUploadPlan(
		glx::UploadPlanKind::TransientStream, static_cast<int>( glx::StreamStrategy::MapBufferRange ),
		1024, 512, 256 );
	streamUpload.texcoordBytes = 128;
	streamUpload.sync = glx::UploadSyncPolicy::FrameFence;
	CHECK( glx::GLX_RenderIR_TierSupportsUploadPlan( glx::RenderProductTier::GL41, streamUpload ) == qtrue );

	glx::UploadPlan persistentUpload = streamUpload;
	persistentUpload.strategy = static_cast<int>( glx::StreamStrategy::PersistentMapped );
	persistentUpload.sync = glx::UploadSyncPolicy::PersistentFence;
	CHECK( glx::GLX_RenderIR_TierSupportsUploadPlan( glx::RenderProductTier::GL41, persistentUpload ) == qfalse );
	CHECK( glx::GLX_RenderIR_TierSupportsUploadPlan( glx::RenderProductTier::GL46, persistentUpload ) == qtrue );

	glx::MaterialIR material = glx::GLX_RenderIR_MakeMaterial(
		0, GLX_STAGE_LIGHTMAP | GLX_STAGE_MULTITEXTURE | GLX_STAGE_TEXMOD |
			GLX_STAGE_DEPTH_FRAGMENT | GLX_STAGE_ENVIRONMENT,
		GLX_MATERIAL_STATE_DEPTHMASK_TRUE | GLX_MATERIAL_STATE_ATEST_GE_80, 3 );
	material.rgbGen = GLX_MATERIAL_RGBGEN_VERTEX;
	material.alphaGen = GLX_MATERIAL_ALPHAGEN_VERTEX;
	material.tcGen0 = GLX_MATERIAL_TCGEN_TEXTURE;
	material.tcGen1 = GLX_MATERIAL_TCGEN_LIGHTMAP;
	CHECK( glx::GLX_RenderIR_TierSupportsMaterial( glx::RenderProductTier::GL41, material ) == qtrue );

	glx::WorldPacket packet {};
	packet.pass = glx::FramePassKind::SkyAndOpaqueWorld;
	packet.surfaces = 8;
	packet.vertexes = 256;
	packet.indexes = 384;
	packet.itemCount = 8;
	packet.material = material;
	packet.upload = staticUpload;
	CHECK( glx::GLX_RenderIR_TierSupportsWorldPacket( glx::RenderProductTier::GL41, packet ) == qtrue );
	packet.upload = persistentUpload;
	CHECK( glx::GLX_RenderIR_TierSupportsWorldPacket( glx::RenderProductTier::GL41, packet ) == qfalse );

	glx::DynamicDraw draw {};
	draw.kind = glx::DynamicDrawKind::Indexed;
	draw.pass = glx::FramePassKind::DynamicScene;
	draw.primitive = 0x0004;
	draw.count = 192;
	draw.indexType = 0x1403;
	draw.indices = reinterpret_cast<const void *>( static_cast<uintptr_t>( 0x100 ) );
	draw.legacyReason = GLX_LEGACY_DELEGATION_NONE;
	draw.profilerPath = GLX_DRAW_STREAM_GENERIC;
	draw.material = material;
	draw.upload = streamUpload;
	CHECK( glx::GLX_RenderIR_TierSupportsDynamicDraw( glx::RenderProductTier::GL41, draw ) == qtrue );
	draw.upload = persistentUpload;
	CHECK( glx::GLX_RenderIR_TierSupportsDynamicDraw( glx::RenderProductTier::GL41, draw ) == qfalse );

	glx::OutputTransform output = glx::GLX_RenderIR_DefaultOutputTransform();
	output.transfer = glx::OutputTransfer::MacEdr;
	CHECK( glx::GLX_RenderIR_TierSupportsOutputTransform( glx::RenderProductTier::GL41, output ) == qtrue );
	CHECK( glx::GLX_RenderIR_TierSupportsOutputTransform( glx::RenderProductTier::GL2X, output ) == qfalse );

	glx::PostNode post {};
	post.kind = glx::PostNodeKind::ToneMap;
	post.pass = glx::FramePassKind::PostProcess;
	post.sequence = 3;
	post.output = output;
	CHECK( glx::GLX_RenderIR_TierSupportsPostNode( glx::RenderProductTier::GL41, post ) == qtrue );
	post.kind = glx::PostNodeKind::Grade;
	CHECK( glx::GLX_RenderIR_TierSupportsPostNode( glx::RenderProductTier::GL41, post ) == qtrue );

	return true;
}

bool GL46ExecutorPolicyIsHighEndAndRequiresModernDriverFeatures()
{
	const glx::TierExecutionPolicy policy =
		glx::GLX_RenderIR_TierExecutionPolicy( glx::RenderProductTier::GL46 );

	CHECK( std::strcmp( policy.executorName, "high-end" ) == 0 );
	CHECK( policy.fixedFunction == qfalse );
	CHECK( policy.streamUploads == qtrue );
	CHECK( policy.materialCompiler == qtrue );
	CHECK( policy.commonMaterials == qtrue );
	CHECK( policy.dynamicEntities == qtrue );
	CHECK( policy.modernPostChain == qtrue );
	CHECK( policy.sceneLinearOutput == qtrue );
	CHECK( policy.fboPostProcess == qtrue );
	CHECK( policy.uboFrameObjectConstants == qtrue );
	CHECK( policy.timerQueries == qtrue );
	CHECK( policy.syncAwareUploads == qtrue );
	CHECK( policy.staticBufferOwnership == qtrue );
	CHECK( policy.dynamicBufferOwnership == qtrue );
	CHECK( policy.persistentUploads == qtrue );
	CHECK( policy.indirectSubmission == qtrue );
	CHECK( policy.directStateAccess == qtrue );
	CHECK( policy.debugOutputRequired == qtrue );
	CHECK( policy.bufferStorageRequired == qtrue );
	CHECK( policy.directStateAccessRequired == qtrue );
	CHECK( policy.multiDrawIndirectRequired == qtrue );
	CHECK( policy.bufferStorageUploads == qtrue );
	CHECK( policy.syncHeavyStreaming == qtrue );
	CHECK( policy.multiDrawIndirectSubmission == qtrue );
	CHECK( policy.aggressiveStaticWorldSubmission == qtrue );
	CHECK( policy.detailedGpuCounters == qtrue );
	CHECK( policy.optionalHardwareHdrOutput == qtrue );
	CHECK( std::strcmp( policy.unavailable, "none" ) == 0 );

	glx::UploadPlan persistentUpload = glx::GLX_RenderIR_MakeUploadPlan(
		glx::UploadPlanKind::TransientStream, static_cast<int>( glx::StreamStrategy::PersistentMapped ),
		2048, 1024, 512 );
	persistentUpload.texcoordBytes = 256;
	persistentUpload.sync = glx::UploadSyncPolicy::PersistentFence;
	CHECK( glx::GLX_RenderIR_TierSupportsUploadPlan( glx::RenderProductTier::GL46, persistentUpload ) == qtrue );
	CHECK( glx::GLX_RenderIR_TierSupportsUploadPlan( glx::RenderProductTier::GL41, persistentUpload ) == qfalse );

	glx::UploadPlan staticPersistentUpload = persistentUpload;
	staticPersistentUpload.kind = glx::UploadPlanKind::StaticWorld;
	staticPersistentUpload.bytes = 16384;
	staticPersistentUpload.vertexBytes = 12288;
	staticPersistentUpload.indexBytes = 4096;
	staticPersistentUpload.texcoordBytes = 0;
	CHECK( glx::GLX_RenderIR_TierSupportsUploadPlan( glx::RenderProductTier::GL46, staticPersistentUpload ) == qtrue );

	glx::MaterialIR material = glx::GLX_RenderIR_MakeMaterial(
		0, GLX_STAGE_LIGHTMAP | GLX_STAGE_MULTITEXTURE | GLX_STAGE_TEXMOD |
			GLX_STAGE_DEPTH_FRAGMENT | GLX_STAGE_ENVIRONMENT | GLX_STAGE_SCREEN_MAP |
			GLX_STAGE_DLIGHT_MAP,
		GLX_MATERIAL_STATE_DEPTHMASK_TRUE | GLX_MATERIAL_STATE_ATEST_GE_80, 4 );
	material.rgbGen = GLX_MATERIAL_RGBGEN_VERTEX;
	material.alphaGen = GLX_MATERIAL_ALPHAGEN_VERTEX;
	material.tcGen0 = GLX_MATERIAL_TCGEN_TEXTURE;
	material.tcGen1 = GLX_MATERIAL_TCGEN_LIGHTMAP;
	material.texMods0 = 2;
	material.texMods1 = 1;
	CHECK( glx::GLX_RenderIR_TierSupportsMaterial( glx::RenderProductTier::GL46, material ) == qtrue );

	glx::WorldPacket packet {};
	packet.pass = glx::FramePassKind::SkyAndOpaqueWorld;
	packet.surfaces = 16;
	packet.vertexes = 512;
	packet.indexes = 768;
	packet.itemCount = 16;
	packet.material = material;
	packet.upload = staticPersistentUpload;
	CHECK( glx::GLX_RenderIR_TierSupportsWorldPacket( glx::RenderProductTier::GL46, packet ) == qtrue );

	glx::DynamicDraw draw {};
	draw.kind = glx::DynamicDrawKind::Indexed;
	draw.pass = glx::FramePassKind::DynamicScene;
	draw.primitive = 0x0004;
	draw.count = 384;
	draw.indexType = 0x1403;
	draw.indices = reinterpret_cast<const void *>( static_cast<uintptr_t>( 0x200 ) );
	draw.legacyReason = GLX_LEGACY_DELEGATION_NONE;
	draw.profilerPath = GLX_DRAW_STREAM_GENERIC;
	draw.material = material;
	draw.upload = persistentUpload;
	CHECK( glx::GLX_RenderIR_TierSupportsDynamicDraw( glx::RenderProductTier::GL46, draw ) == qtrue );

	glx::OutputTransform output = glx::GLX_RenderIR_DefaultOutputTransform();
	output.transfer = glx::OutputTransfer::Hdr10Pq;
	CHECK( glx::GLX_RenderIR_TierSupportsOutputTransform( glx::RenderProductTier::GL46, output ) == qtrue );
	CHECK( glx::GLX_RenderIR_TierSupportsOutputTransform( glx::RenderProductTier::GL2X, output ) == qfalse );

	glx::PostNode post {};
	post.kind = glx::PostNodeKind::Grade;
	post.pass = glx::FramePassKind::PostProcess;
	post.sequence = 4;
	post.output = output;
	CHECK( glx::GLX_RenderIR_TierSupportsPostNode( glx::RenderProductTier::GL46, post ) == qtrue );
	post.kind = glx::PostNodeKind::ToneMap;
	CHECK( glx::GLX_RenderIR_TierSupportsPostNode( glx::RenderProductTier::GL46, post ) == qtrue );

	return true;
}

bool MotionBlurUsesCameraMotionAndRejectsViewDiscontinuities()
{
	motionBlurViewState_t state {};
	const float origin[3] = { 0.0f, 0.0f, 0.0f };
	const float cutOrigin[3] = { MOTION_BLUR_CUT_DISTANCE + 1.0f, 0.0f, 0.0f };
	const float identity[3][3] = {
		{ 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }
	};
	float invalidOrigin[3] = { 0.0f, 0.0f, 0.0f };
	float invalidAxis[3][3];
	float yawed[3][3];
	float radius[2];
	float bounds[4];
	int viewRect[4];
	const float angle = 5.0f * 0.01745329251994329577f;
	const float nan = std::numeric_limits<float>::quiet_NaN();

	yawed[0][0] = std::cos( angle );
	yawed[0][1] = std::sin( angle );
	yawed[0][2] = 0.0f;
	yawed[1][0] = -std::sin( angle );
	yawed[1][1] = std::cos( angle );
	yawed[1][2] = 0.0f;
	yawed[2][0] = yawed[2][1] = 0.0f;
	yawed[2][2] = 1.0f;
	std::memcpy( invalidAxis, identity, sizeof( invalidAxis ) );
	invalidOrigin[1] = nan;
	invalidAxis[2][1] = nan;

	CHECK( R_MotionBlur_Calculate( &state, qtrue, nan, 992, origin,
		identity, 90.0f, 73.0f, 640, 480, radius ) == qfalse );
	CHECK( state.valid == qfalse );
	CHECK( R_MotionBlur_Calculate( &state, qtrue, 0.25f, 992, invalidOrigin,
		identity, 90.0f, 73.0f, 640, 480, radius ) == qfalse );
	CHECK( R_MotionBlur_Calculate( &state, qtrue, 0.25f, 992, origin,
		invalidAxis, 90.0f, 73.0f, 640, 480, radius ) == qfalse );
	CHECK( R_MotionBlur_Calculate( &state, qtrue, 0.25f, 992, origin,
		identity, nan, 73.0f, 640, 480, radius ) == qfalse );

	CHECK( R_MotionBlur_Calculate( &state, qtrue, 0.25f, 1000, origin,
		identity, 90.0f, 73.0f, 640, 480, radius ) == qfalse );
	CHECK( R_MotionBlur_Calculate( &state, qtrue, 0.25f, 1016, origin,
		yawed, 90.0f, 73.0f, 640, 480, radius ) == qtrue );
	CHECK( std::fabs( radius[0] * 640.0f ) > 1.0f );
	CHECK( std::fabs( radius[1] ) < 0.00001f );
	CHECK( R_MotionBlur_Calculate( &state, qtrue, 0.25f, 1032, origin,
		yawed, 90.0f, 73.0f, 640, 480, radius ) == qfalse );

	CHECK( R_MotionBlur_Calculate( &state, qtrue, 0.25f,
		1032 + MOTION_BLUR_MAX_DELTA_MSEC + 1, origin, identity,
		90.0f, 73.0f, 640, 480, radius ) == qfalse );
	CHECK( R_MotionBlur_Calculate( &state, qtrue, 0.25f, 1200, cutOrigin,
		identity, 90.0f, 73.0f, 640, 480, radius ) == qfalse );
	CHECK( R_MotionBlur_Calculate( &state, qfalse, 0.25f, 1216, origin,
		identity, 90.0f, 73.0f, 640, 480, radius ) == qfalse );
	CHECK( state.valid == qfalse );
	CHECK( R_MotionBlur_Calculate( &state, qtrue, 0.25f,
		std::numeric_limits<int>::min(), origin, identity,
		90.0f, 73.0f, 640, 480, radius ) == qfalse );
	CHECK( R_MotionBlur_Calculate( &state, qtrue, 0.25f,
		std::numeric_limits<int>::max(), origin, yawed,
		90.0f, 73.0f, 640, 480, radius ) == qfalse );

	CHECK( R_MotionBlur_CalculateViewRect( 640, 480, 32, 24, 576, 432,
		viewRect, bounds ) == qtrue );
	CHECK( viewRect[0] == 32 && viewRect[1] == 24 );
	CHECK( viewRect[2] == 576 && viewRect[3] == 432 );
	CHECK( std::fabs( bounds[0] - 32.5f / 640.0f ) < 0.00001f );
	CHECK( std::fabs( bounds[3] - 455.5f / 480.0f ) < 0.00001f );
	CHECK( R_MotionBlur_CalculateViewRect( 640, 480, -10, 20, 100, 100,
		viewRect, bounds ) == qtrue );
	CHECK( viewRect[0] == 0 && viewRect[2] == 90 );
	CHECK( R_MotionBlur_CalculateViewRect( 640, 480, 700, 20, 100, 100,
		viewRect, bounds ) == qfalse );

	return true;
}

bool LiquidClassificationAndImpulseLifetimeStayDeterministic()
{
	liquidInteraction_t interaction {};
	const vec3_t origin = { 10.0f, 20.0f, 30.0f };
	const vec3_t world = { 14.0f, 26.0f, 38.0f };
	const vec3_t axis[3] = {
		{ 1.0f, 0.0f, 0.0f },
		{ 0.0f, 1.0f, 0.0f },
		{ 0.0f, 0.0f, 1.0f }
	};
	vec3_t local;

	CHECK( !R_LiquidContentsEnabled( CONTENTS_WATER, 0 ) );
	CHECK( R_LiquidContentsEnabled( CONTENTS_WATER, 1 ) );
	CHECK( !R_LiquidContentsEnabled( CONTENTS_SLIME, 1 ) );
	CHECK( R_LiquidContentsEnabled( CONTENTS_SLIME | CONTENTS_LAVA, 2 ) );
	CHECK( R_LiquidContentsReflectionScale( CONTENTS_WATER ) == 1.0f );
	CHECK( R_LiquidContentsReflectionScale( CONTENTS_SLIME ) == 0.55f );
	CHECK( R_LiquidContentsReflectionScale( CONTENTS_LAVA ) == 0.25f );
	CHECK( R_LiquidViewportCoversTarget( 0, 0, 1920, 1080, 1920, 1080 ) );
	CHECK( !R_LiquidViewportCoversTarget( 96, 54, 1728, 972, 1920, 1080 ) );
	CHECK( !R_LiquidViewportCoversTarget( 0, 0, 1920, 1080, 0, 1080 ) );

	interaction.time = 1000;
	interaction.radius = 18.0f;
	interaction.strength = 1.0f;
	CHECK( !R_LiquidInteractionActive( &interaction, 999 ) );
	CHECK( R_LiquidInteractionActive( &interaction,
		1000 + LIQUID_IMPULSE_LIFETIME_MSEC - 1 ) );
	CHECK( !R_LiquidInteractionActive( &interaction,
		1000 + LIQUID_IMPULSE_LIFETIME_MSEC ) );
	interaction.time = std::numeric_limits<int>::min();
	CHECK( !R_LiquidInteractionActive( &interaction,
		std::numeric_limits<int>::max() ) );

	R_LiquidWorldToLocal( world, origin, axis, local );
	CHECK( local[0] == 4.0f );
	CHECK( local[1] == 6.0f );
	CHECK( local[2] == 8.0f );
	return true;
}

bool LensFlareLayoutIsVisibleAndDeterministic()
{
	float x;
	float y;
	const float nan = std::numeric_limits<float>::quiet_NaN();

	CHECK( R_LensFlareSpriteCount() == 6 );
	CHECK( r_lensFlareSprites[0].axisPosition == 0.0f );
	CHECK( r_lensFlareSprites[0].halfWidthScale == r_lensFlareSprites[0].halfHeightScale );
	CHECK( r_lensFlareSprites[1].halfWidthScale >
		r_lensFlareSprites[1].halfHeightScale * 10.0f );
	CHECK( r_lensFlareSprites[R_LensFlareSpriteCount() - 1].axisPosition > 1.0f );
	R_LensFlareSpritePosition( &r_lensFlareSprites[R_LensFlareSpriteCount() - 1],
		0.0f, 20.0f, 100.0f, 60.0f, &x, &y );
	CHECK( std::fabs( x - 148.0f ) < 0.0001f );
	CHECK( std::fabs( y - 79.2f ) < 0.0001f );
	CHECK( R_LensFlareEdgeAttenuation( 100.0f, 60.0f, 100.0f, 60.0f,
		200.0f, 120.0f ) == 1.0f );
	CHECK( R_LensFlareEdgeAttenuation( 0.0f, 60.0f, 100.0f, 60.0f,
		200.0f, 120.0f ) == 0.75f );
	CHECK( R_LensFlareEdgeAttenuation( 0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 120.0f ) == 0.0f );
	CHECK( R_LensFlareEdgeAttenuation( nan, 0.0f, 0.0f, 0.0f,
		200.0f, 120.0f ) == 0.0f );
	CHECK( R_LensFlareSpriteColor( 0.4f, 255,
		&r_lensFlareSprites[0], 0, 0.75f ) >= 20 );
	CHECK( R_LensFlareSpriteColor( 1.0f, 255,
		&r_lensFlareSprites[3], 0, 1.0f ) >= 89 );
	CHECK( R_LensFlareSpriteColor( nan, 255,
		&r_lensFlareSprites[0], 0, 1.0f ) == 0 );
	CHECK( R_LensFlareSpriteColor( 1.0f, 255,
		&r_lensFlareSprites[0], 3, 1.0f ) == 0 );

	return true;
}

} // namespace

int main()
{
	struct Test {
		const char *name;
		bool ( *fn )();
	};

	const Test tests[] = {
		{ "CubemapFaceAxesAreOrthonormalAndConsistent", CubemapFaceAxesAreOrthonormalAndConsistent },
		{ "MaterialKeysClassifyRcShapes", MaterialKeysClassifyRcShapes },
		{ "MaterialKeysRejectUnsupportedCombines", MaterialKeysRejectUnsupportedCombines },
		{ "MaterialKeysTreatSpecialSceneFlagsAsGates", MaterialKeysTreatSpecialSceneFlagsAsGates },
		{ "MaterialStageKeysCoverPreparedIdTech3StageLanguage", MaterialStageKeysCoverPreparedIdTech3StageLanguage },
		{ "MaterialIRCompilesToProgramStatePlans", MaterialIRCompilesToProgramStatePlans },
		{ "MaterialParameterBlocksMirrorNativeRenderInputs", MaterialParameterBlocksMirrorNativeRenderInputs },
		{ "StreamGatesMatchRcAllowlist", StreamGatesMatchRcAllowlist },
		{ "StreamBroadKeyModeRemainsDeveloperEscapeHatch", StreamBroadKeyModeRemainsDeveloperEscapeHatch },
		{ "StreamSpecialSceneGatesAreExplicit", StreamSpecialSceneGatesAreExplicit },
		{ "StreamDynamicLightAutoGateIsTierAndReadinessBound", StreamDynamicLightAutoGateIsTierAndReadinessBound },
		{ "StreamShadowGateIsExplicit", StreamShadowGateIsExplicit },
		{ "StreamBeamGateIsExplicit", StreamBeamGateIsExplicit },
		{ "StreamPostProcessGateIsExplicit", StreamPostProcessGateIsExplicit },
		{ "StreamDynamicCategoriesNormalizeToSceneProducts", StreamDynamicCategoriesNormalizeToSceneProducts },
		{ "CapabilityLogicClassifiesTiersAndExtensions", CapabilityLogicClassifiesTiersAndExtensions },
		{ "StreamStrategySelectionFollowsFallbackLadder", StreamStrategySelectionFollowsFallbackLadder },
		{ "StaticWorldPacketLogicClassifiesRunsAndPolicies", StaticWorldPacketLogicClassifiesRunsAndPolicies },
		{ "RenderIRDefaultPassScheduleIsDeterministic", RenderIRDefaultPassScheduleIsDeterministic },
		{ "RenderIRProductsValidate", RenderIRProductsValidate },
		{ "RenderIRProjectedDlightRecordsValidate", RenderIRProjectedDlightRecordsValidate },
		{ "RenderIRProjectedDlightMdiBatchesValidateOffsetsAndRejects", RenderIRProjectedDlightMdiBatchesValidateOffsetsAndRejects },
		{ "ExecutorConsumesProjectedDlightMdiPlansAndSubmittedDrawAccounting", ExecutorConsumesProjectedDlightMdiPlansAndSubmittedDrawAccounting },
		{ "RenderIRTierMappingKeepsSingleProductContract", RenderIRTierMappingKeepsSingleProductContract },
		{ "GL12ExecutorPolicyIsFixedFunctionAndSdrOnly", GL12ExecutorPolicyIsFixedFunctionAndSdrOnly },
		{ "GL2XExecutorPolicyIsProgrammableAndAvoidsLaterRequirements", GL2XExecutorPolicyIsProgrammableAndAvoidsLaterRequirements },
		{ "GL3XExecutorPolicyIsPerformanceAndAvoidsGL4OnlyRequirements", GL3XExecutorPolicyIsPerformanceAndAvoidsGL4OnlyRequirements },
		{ "PostOutputPlansSeparatePlannedAndExecutableOwnership", PostOutputPlansSeparatePlannedAndExecutableOwnership },
		{ "DisplayOutputStateHashTracksRuntimeHdrCapabilityChanges", DisplayOutputStateHashTracksRuntimeHdrCapabilityChanges },
		{ "CaptureExportPoliciesStayExplicitAndSdrDefault", CaptureExportPoliciesStayExplicitAndSdrDefault },
		{ "HdrColorMathReferencesCoverPipelineContracts", HdrColorMathReferencesCoverPipelineContracts },
		{ "PostShaderPlansClassifyOutputTransformProgramShape", PostShaderPlansClassifyOutputTransformProgramShape },
		{ "PostShaderFinalEligibilityCoversLegacyGamma", PostShaderFinalEligibilityCoversLegacyGamma },
		{ "PostShaderSourcesEmitDeterministicProgramShape", PostShaderSourcesEmitDeterministicProgramShape },
		{ "GL41ExecutorPolicyIsMacModernAndAvoidsUnavailableAppleFeatures", GL41ExecutorPolicyIsMacModernAndAvoidsUnavailableAppleFeatures },
		{ "GL46ExecutorPolicyIsHighEndAndRequiresModernDriverFeatures", GL46ExecutorPolicyIsHighEndAndRequiresModernDriverFeatures },
		{ "MotionBlurUsesCameraMotionAndRejectsViewDiscontinuities", MotionBlurUsesCameraMotionAndRejectsViewDiscontinuities },
		{ "LiquidClassificationAndImpulseLifetimeStayDeterministic", LiquidClassificationAndImpulseLifetimeStayDeterministic },
		{ "LensFlareLayoutIsVisibleAndDeterministic", LensFlareLayoutIsVisibleAndDeterministic },
	};

	for ( const Test &test : tests ) {
		if ( !test.fn() ) {
			std::fprintf( stderr, "FAILED: %s\n", test.name );
			return 1;
		}
		std::printf( "passed: %s\n", test.name );
	}

	return 0;
}
