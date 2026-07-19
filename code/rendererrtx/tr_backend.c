/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

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
#include "tr_local.h"

backEndData_t	*backEndData;
backEndState_t	backEnd;

typedef enum {
	RTX_FRAME_STAGE_IDLE = 0,
	RTX_FRAME_STAGE_SETUP,
	RTX_FRAME_STAGE_SCENE,
	RTX_FRAME_STAGE_POST,
	RTX_FRAME_STAGE_PRESENT
} rtxFrameStage_t;

typedef enum {
	RTX_FRAME_PASS_DRAW_BUFFER = 0,
	RTX_FRAME_PASS_CLEAR_COLOR,
	RTX_FRAME_PASS_CLEAR_DEPTH,
	RTX_FRAME_PASS_DRAW_SURFS,
	RTX_FRAME_PASS_RT_TRACE,
	RTX_FRAME_PASS_UI_2D,
	RTX_FRAME_PASS_FINISH_BLOOM,
	RTX_FRAME_PASS_SWAP_BUFFERS,
	RTX_FRAME_PASS_COUNT
} rtxFramePass_t;

typedef struct {
	rtxFrameStage_t	stage;
	unsigned int	passMask;
	char		passSequence[192];
} rtxFrameGraphState_t;

static rtxFrameGraphState_t s_frameGraph;

static int RB_FrameGraph_DebugLevel( void )
{
	if ( !rtx_debug_framegraph ) {
		return 0;
	}
	return rtx_debug_framegraph->integer;
}

static const char *RB_FrameGraph_StageName( rtxFrameStage_t stage )
{
	switch ( stage ) {
	case RTX_FRAME_STAGE_IDLE: return "idle";
	case RTX_FRAME_STAGE_SETUP: return "setup";
	case RTX_FRAME_STAGE_SCENE: return "scene";
	case RTX_FRAME_STAGE_POST: return "post";
	case RTX_FRAME_STAGE_PRESENT: return "present";
	default: return "unknown";
	}
}

static const char *RB_FrameGraph_PassName( rtxFramePass_t pass )
{
	switch ( pass ) {
	case RTX_FRAME_PASS_DRAW_BUFFER: return "draw_buffer";
	case RTX_FRAME_PASS_CLEAR_COLOR: return "clear_color";
	case RTX_FRAME_PASS_CLEAR_DEPTH: return "clear_depth";
	case RTX_FRAME_PASS_DRAW_SURFS: return "draw_surfs";
	case RTX_FRAME_PASS_RT_TRACE: return "rt_trace";
	case RTX_FRAME_PASS_UI_2D: return "ui_2d";
	case RTX_FRAME_PASS_FINISH_BLOOM: return "finish_bloom";
	case RTX_FRAME_PASS_SWAP_BUFFERS: return "swap_buffers";
	default: return "unknown";
	}
}

static rtxFrameStage_t RB_FrameGraph_PassStage( rtxFramePass_t pass )
{
	switch ( pass ) {
	case RTX_FRAME_PASS_DRAW_BUFFER:
	case RTX_FRAME_PASS_CLEAR_COLOR:
	case RTX_FRAME_PASS_CLEAR_DEPTH:
		return RTX_FRAME_STAGE_SETUP;
	case RTX_FRAME_PASS_DRAW_SURFS:
	case RTX_FRAME_PASS_RT_TRACE:
	case RTX_FRAME_PASS_UI_2D:
		return RTX_FRAME_STAGE_SCENE;
	case RTX_FRAME_PASS_FINISH_BLOOM:
		return RTX_FRAME_STAGE_POST;
	case RTX_FRAME_PASS_SWAP_BUFFERS:
		return RTX_FRAME_STAGE_PRESENT;
	default:
		return RTX_FRAME_STAGE_IDLE;
	}
}

static void RB_FrameGraph_Reset( void )
{
	s_frameGraph.stage = RTX_FRAME_STAGE_IDLE;
	s_frameGraph.passMask = 0;
	s_frameGraph.passSequence[0] = '\0';
}

static void RB_FrameGraph_AppendPass( rtxFramePass_t pass )
{
	const unsigned int passBit = 1u << (unsigned int)pass;

	if ( s_frameGraph.passMask & passBit ) {
		return;
	}

	if ( s_frameGraph.passSequence[0] ) {
		Q_strcat( s_frameGraph.passSequence, sizeof( s_frameGraph.passSequence ), " -> " );
	}
	Q_strcat( s_frameGraph.passSequence, sizeof( s_frameGraph.passSequence ), RB_FrameGraph_PassName( pass ) );
	s_frameGraph.passMask |= passBit;
}

static void RB_FrameGraph_NotePass( rtxFramePass_t pass )
{
	const int debugLevel = RB_FrameGraph_DebugLevel();
	const rtxFrameStage_t nextStage = RB_FrameGraph_PassStage( pass );

	if ( pass == RTX_FRAME_PASS_DRAW_BUFFER ) {
		if ( s_frameGraph.passMask != 0 && debugLevel >= 1 ) {
			ri.Printf( PRINT_WARNING, "RTX frame graph: restarting lifecycle at draw_buffer (stage: %s)\n",
				RB_FrameGraph_StageName( s_frameGraph.stage ) );
		}
		RB_FrameGraph_Reset();
	}

	if ( s_frameGraph.stage == RTX_FRAME_STAGE_IDLE && pass != RTX_FRAME_PASS_DRAW_BUFFER && debugLevel >= 1 ) {
		ri.Printf( PRINT_WARNING, "RTX frame graph: pass '%s' occurred before draw_buffer\n",
			RB_FrameGraph_PassName( pass ) );
	}

	if ( nextStage < s_frameGraph.stage && debugLevel >= 1 ) {
		ri.Printf( PRINT_WARNING, "RTX frame graph: out-of-order pass transition %s -> %s (%s)\n",
			RB_FrameGraph_StageName( s_frameGraph.stage ),
			RB_FrameGraph_StageName( nextStage ),
			RB_FrameGraph_PassName( pass ) );
	}

	if ( nextStage > s_frameGraph.stage ) {
		s_frameGraph.stage = nextStage;
	}

	RB_FrameGraph_AppendPass( pass );
}

static void RB_FrameGraph_EndLifecycle( qboolean completed )
{
	const int debugLevel = RB_FrameGraph_DebugLevel();
	const unsigned int drawBufferBit = 1u << (unsigned int)RTX_FRAME_PASS_DRAW_BUFFER;
	const unsigned int swapBuffersBit = 1u << (unsigned int)RTX_FRAME_PASS_SWAP_BUFFERS;

	if ( s_frameGraph.passMask == 0 ) {
		return;
	}

	if ( debugLevel >= 1 ) {
		if ( !( s_frameGraph.passMask & drawBufferBit ) ) {
			ri.Printf( PRINT_WARNING, "RTX frame graph: lifecycle missing draw_buffer pass\n" );
		}

		if ( completed ) {
			if ( !( s_frameGraph.passMask & swapBuffersBit ) ) {
				ri.Printf( PRINT_WARNING, "RTX frame graph: lifecycle missing swap_buffers pass\n" );
			}
		} else {
			ri.Printf( PRINT_WARNING, "RTX frame graph: command list ended before swap_buffers\n" );
		}
	}

	if ( debugLevel >= 2 && s_frameGraph.passSequence[0] ) {
		ri.Printf( PRINT_DEVELOPER, "RTX frame graph: %s\n", s_frameGraph.passSequence );
	}

	RB_FrameGraph_Reset();
}

#ifndef USE_VULKAN
static const float s_flipMatrix[16] = {
	// convert from our coordinate system (looking down X)
	// to OpenGL's coordinate system (looking down -Z)
	0, 0, -1, 0,
	-1, 0, 0, 0,
	0, 1, 0, 0,
	0, 0, 0, 1
};


const float *GL_Ortho( const float left, const float right, const float bottom, const float top, const float znear, const float zfar )
{
	static float m[ 16 ] = { 0 };

	m[0] = 2.0f / (right - left);
	m[5] = 2.0f / (top - bottom);
	m[10] = - 2.0f / (zfar - znear);
	m[12] = - (right + left)/(right - left);
	m[13] = - (top + bottom) / (top - bottom);
	m[14] = - (zfar + znear) / (zfar - znear);
	m[15] = 1.0f;

	return m;
}
#endif


/*
** GL_Bind
*/
void GL_Bind( image_t *image ) {
#ifdef USE_VULKAN
	if ( !image ) {
		ri.Printf( PRINT_WARNING, "GL_Bind: NULL image\n" );
		image = tr.defaultImage;
	}

	if ( r_nobind->integer && tr.dlightImage ) {		// performance evaluation option
		image = tr.dlightImage;
	}

	//if ( glState.currenttextures[glState.currenttmu] != texnum ) {
		image->frameUsed = tr.frameCount;
		vk_update_descriptor( glState.currenttmu + VK_DESC_TEXTURE_BASE, image->descriptor );

	//}
#else
	GLuint texnum;

	if ( !image ) {
		ri.Printf( PRINT_WARNING, "GL_Bind: NULL image\n" );
		texnum = tr.defaultImage->texnum;
	} else {
		texnum = image->texnum;
	}

	if ( r_nobind->integer && tr.dlightImage ) {		// performance evaluation option
		texnum = tr.dlightImage->texnum;
	}

	if ( glState.currenttextures[glState.currenttmu] != texnum ) {
		if ( image ) {
			image->frameUsed = tr.frameCount;
		}
		glState.currenttextures[glState.currenttmu] = texnum;
		qglBindTexture (GL_TEXTURE_2D, texnum);
	}
#endif
}


/*
** GL_SelectTexture
*/
void GL_SelectTexture( int unit )
{
#ifndef USE_VULKAN
	if ( glState.currenttmu == unit )
	{
		return;
	}
#endif

	if ( unit >= glConfig.numTextureUnits )
	{
		ri.Error( ERR_DROP, "GL_SelectTexture: unit = %i", unit );
	}
#ifndef USE_VULKAN
	qglActiveTextureARB( GL_TEXTURE0_ARB + unit );
#endif
	glState.currenttmu = unit;
}


/*
** GL_SelectClientTexture
*/
#ifndef USE_VULKAN
static void GL_SelectClientTexture( int unit )
{
	if ( glState.currentArray == unit )
	{
		return;
	}

	if ( unit >= glConfig.numTextureUnits )
	{
		ri.Error( ERR_DROP, "GL_SelectClientTexture: unit = %i", unit );
	}

	qglClientActiveTextureARB( GL_TEXTURE0_ARB + unit );

	glState.currentArray = unit;
}
#endif


/*
** GL_Cull
*/
void GL_Cull( cullType_t cullType ) {
	if ( glState.faceCulling == cullType ) {
		return;
	}

	glState.faceCulling = cullType;
#ifndef USE_VULKAN
	if ( cullType == CT_TWO_SIDED )
	{
		qglDisable( GL_CULL_FACE );
	}
	else
	{
		qboolean cullFront;
		qglEnable( GL_CULL_FACE );

		cullFront = (cullType == CT_FRONT_SIDED);
		if ( backEnd.viewParms.portalView == PV_MIRROR )
		{
			cullFront = !cullFront;
		}

		qglCullFace( cullFront ? GL_FRONT : GL_BACK );
	}
#endif
}


/*
** GL_TexEnv
*/
void GL_TexEnv( GLint env )
{
#ifndef USE_VULKAN
	if ( env == glState.texEnv[ glState.currenttmu ] )
		return;

	glState.texEnv[ glState.currenttmu ] = env;

	switch ( env )
	{
	case GL_MODULATE:
	case GL_REPLACE:
	case GL_DECAL:
	case GL_ADD:
		qglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, env );
		break;
	default:
		ri.Error( ERR_DROP, "GL_TexEnv: invalid env '%d' passed", env );
		break;
	}
#endif
}


/*
** GL_State
**
** This routine is responsible for setting the most commonly changed state
** in Q3.
*/
void GL_State( unsigned stateBits )
{
#ifndef USE_VULKAN
	unsigned diff = stateBits ^ glState.glStateBits;

	if ( !diff )
	{
		return;
	}

	//
	// check depthFunc bits
	//
	if ( diff & GLS_DEPTHFUNC_EQUAL )
	{
		if ( stateBits & GLS_DEPTHFUNC_EQUAL )
		{
			qglDepthFunc( GL_EQUAL );
		}
		else
		{
			qglDepthFunc( GL_LEQUAL );
		}
	}

	//
	// check blend bits
	//
	if ( diff & GLS_BLEND_BITS )
	{
		GLenum srcFactor = GL_ONE, dstFactor = GL_ONE;

		if ( stateBits & GLS_BLEND_BITS )
		{
			switch ( stateBits & GLS_SRCBLEND_BITS )
			{
			case GLS_SRCBLEND_ZERO:
				srcFactor = GL_ZERO;
				break;
			case GLS_SRCBLEND_ONE:
				srcFactor = GL_ONE;
				break;
			case GLS_SRCBLEND_DST_COLOR:
				srcFactor = GL_DST_COLOR;
				break;
			case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:
				srcFactor = GL_ONE_MINUS_DST_COLOR;
				break;
			case GLS_SRCBLEND_SRC_ALPHA:
				srcFactor = GL_SRC_ALPHA;
				break;
			case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:
				srcFactor = GL_ONE_MINUS_SRC_ALPHA;
				break;
			case GLS_SRCBLEND_DST_ALPHA:
				srcFactor = GL_DST_ALPHA;
				break;
			case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:
				srcFactor = GL_ONE_MINUS_DST_ALPHA;
				break;
			case GLS_SRCBLEND_ALPHA_SATURATE:
				srcFactor = GL_SRC_ALPHA_SATURATE;
				break;
			default:
				ri.Error( ERR_DROP, "GL_State: invalid src blend state bits" );
				break;
			}

			switch ( stateBits & GLS_DSTBLEND_BITS )
			{
			case GLS_DSTBLEND_ZERO:
				dstFactor = GL_ZERO;
				break;
			case GLS_DSTBLEND_ONE:
				dstFactor = GL_ONE;
				break;
			case GLS_DSTBLEND_SRC_COLOR:
				dstFactor = GL_SRC_COLOR;
				break;
			case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:
				dstFactor = GL_ONE_MINUS_SRC_COLOR;
				break;
			case GLS_DSTBLEND_SRC_ALPHA:
				dstFactor = GL_SRC_ALPHA;
				break;
			case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:
				dstFactor = GL_ONE_MINUS_SRC_ALPHA;
				break;
			case GLS_DSTBLEND_DST_ALPHA:
				dstFactor = GL_DST_ALPHA;
				break;
			case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:
				dstFactor = GL_ONE_MINUS_DST_ALPHA;
				break;
			default:
				ri.Error( ERR_DROP, "GL_State: invalid dst blend state bits" );
				break;
			}

			qglEnable( GL_BLEND );
			qglBlendFunc( srcFactor, dstFactor );
		}
		else
		{
			qglDisable( GL_BLEND );
		}
	}

	//
	// check depthmask
	//
	if ( diff & GLS_DEPTHMASK_TRUE )
	{
		if ( stateBits & GLS_DEPTHMASK_TRUE )
		{
			qglDepthMask( GL_TRUE );
		}
		else
		{
			qglDepthMask( GL_FALSE );
		}
	}

	//
	// fill/line mode
	//
	if ( diff & GLS_POLYMODE_LINE )
	{
		if ( stateBits & GLS_POLYMODE_LINE )
		{
			qglPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
		}
		else
		{
			qglPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
		}
	}

	//
	// depthtest
	//
	if ( diff & GLS_DEPTHTEST_DISABLE )
	{
		if ( stateBits & GLS_DEPTHTEST_DISABLE )
		{
			qglDisable( GL_DEPTH_TEST );
		}
		else
		{
			qglEnable( GL_DEPTH_TEST );
		}
	}

	//
	// alpha test
	//
	if ( diff & GLS_ATEST_BITS )
	{
		switch ( stateBits & GLS_ATEST_BITS )
		{
		case 0:
			qglDisable( GL_ALPHA_TEST );
			break;
		case GLS_ATEST_GT_0:
			qglEnable( GL_ALPHA_TEST );
			qglAlphaFunc( GL_GREATER, 0.0f );
			break;
		case GLS_ATEST_LT_80:
			qglEnable( GL_ALPHA_TEST );
			qglAlphaFunc( GL_LESS, 0.5f );
			break;
		case GLS_ATEST_GE_80:
			qglEnable( GL_ALPHA_TEST );
			qglAlphaFunc( GL_GEQUAL, 0.5f );
			break;
		default:
			ri.Error( ERR_DROP, "GL_State: invalid alpha test bits" );
			break;
		}
	}

	glState.glStateBits = stateBits;
#endif // USE_VULKAN
}


#ifndef USE_VULKAN
void GL_ClientState( int unit, unsigned stateBits )
{
	unsigned diff = stateBits ^ glState.glClientStateBits[ unit ];

	if ( diff == 0 )
	{
		if ( stateBits )
		{
			GL_SelectClientTexture( unit );
		}
		return;
	}

	GL_SelectClientTexture( unit );

	if ( diff & CLS_COLOR_ARRAY )
	{
		if ( stateBits & CLS_COLOR_ARRAY )
			qglEnableClientState( GL_COLOR_ARRAY );
		else
			qglDisableClientState( GL_COLOR_ARRAY );
	}

	if ( diff & CLS_NORMAL_ARRAY )
	{
		if ( stateBits & CLS_NORMAL_ARRAY )
			qglEnableClientState( GL_NORMAL_ARRAY );
		else
			qglDisableClientState( GL_NORMAL_ARRAY );
	}

	if ( diff & CLS_TEXCOORD_ARRAY )
	{
		if ( stateBits & CLS_TEXCOORD_ARRAY )
			qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
		else
			qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
	}

	glState.glClientStateBits[ unit ] = stateBits;
}
#endif


static void RB_SetGL2D( void );

/*
================
RB_Hyperspace

A player has predicted a teleport, but hasn't arrived yet
================
*/
static void RB_Hyperspace( void ) {
	color4ub_t c;

	if ( !backEnd.isHyperspace ) {
		// do initialization shit
	}

	if ( tess.shader != tr.whiteShader ) {
		RB_EndSurface();
		RB_BeginSurface( tr.whiteShader, 0 );
	}

#ifdef USE_VBO
	VBO_UnBind();
#endif

	RB_SetGL2D();

	if ( r_teleporterFlash->integer == 0 ) {
		c.rgba[0] = c.rgba[1] = c.rgba[2] = 0; // fade to black
	} else {
		c.rgba[0] = c.rgba[1] = c.rgba[2] = (backEnd.refdef.time & 255); // fade to white
	}
	c.rgba[3] = 255;

	RB_AddQuadStamp2( backEnd.refdef.x, backEnd.refdef.y, backEnd.refdef.width, backEnd.refdef.height,
		0.0, 0.0, 0.0, 0.0, c );

	RB_EndSurface();

	tess.numIndexes = 0;
	tess.numVertexes = 0;

	backEnd.isHyperspace = qtrue;
}


static void SetViewportAndScissor( void ) {
#ifdef USE_VULKAN
	//Com_Memcpy( vk_world.modelview_transform, backEnd.or.modelMatrix, 64 );
	//vk_update_mvp();
	// force depth range and viewport/scissor updates
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;
#else
	qglMatrixMode(GL_PROJECTION);
	qglLoadMatrixf( backEnd.viewParms.projectionMatrix );
	qglMatrixMode(GL_MODELVIEW);

	// set the window clipping
	qglViewport( backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
		backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight );
	qglScissor( backEnd.viewParms.scissorX, backEnd.viewParms.scissorY,
		backEnd.viewParms.scissorWidth, backEnd.viewParms.scissorHeight );
#endif
}


/*
=================
RB_BeginDrawingView

Any mirrored or portaled views have already been drawn, so prepare
to actually render the visible surfaces for this view
=================
*/
static void RB_BeginDrawingView( void ) {
#ifndef USE_VULKAN
	int clearBits = 0;
#endif

	// sync with gl if needed
	if ( r_finish->integer == 1 && !glState.finishCalled ) {
#ifdef USE_VULKAN
		vk_queue_wait_idle();
#else
		qglFinish();
#endif
		glState.finishCalled = qtrue;
	} else if ( r_finish->integer == 0 ) {
		glState.finishCalled = qtrue;
	}

	// we will need to change the projection matrix before drawing
	// 2D images again
	backEnd.projection2D = qfalse;

	//
	// set the modelview matrix for the viewer
	//
	SetViewportAndScissor();

#ifdef USE_VULKAN
	vk_clear_depth( qtrue );
#else
	// ensures that depth writes are enabled for the depth clear
	GL_State( GLS_DEFAULT );

	// clear relevant buffers
	clearBits = GL_DEPTH_BUFFER_BIT;

	if ( r_shadows->integer == 2 )
	{
		clearBits |= GL_STENCIL_BUFFER_BIT;
	}
	if ( 0 && r_fastsky->integer && !( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) )
	{
		clearBits |= GL_COLOR_BUFFER_BIT;	// FIXME: only if sky shaders have been used
#ifdef _DEBUG
		qglClearColor( 0.8f, 0.7f, 0.4f, 1.0f );	// FIXME: get color of sky
#else
		qglClearColor( 0.0f, 0.0f, 0.0f, 1.0f );	// FIXME: get color of sky
#endif
	}
	qglClear( clearBits );
#endif

	if ( backEnd.refdef.rdflags & RDF_HYPERSPACE ) {
		RB_Hyperspace();
		backEnd.projection2D = qfalse;
		SetViewportAndScissor();
	} else {
		backEnd.isHyperspace = qfalse;
	}

	glState.faceCulling = -1;		// force face culling to set next time

	// we will only draw a sun if there was sky rendered in this view
	backEnd.skyRenderedThisView = qfalse;
}

/*
==================
RB_RenderDrawSurfList
==================
*/
typedef enum {
	RB_DRAWSURFS_ALL,
	RB_DRAWSURFS_RT_BASE,
	RB_DRAWSURFS_RT_OVERLAY
} rbDrawSurfsMode_t;

#ifdef USE_PMLIGHT
static void RB_LightingPass( rbDrawSurfsMode_t mode, qboolean finish );
static void RB_CSMShadowReceiverPass( drawSurf_t *drawSurfs, int numDrawSurfs );
static void RB_RenderCSMShadowAtlas( drawSurf_t *drawSurfs, int numDrawSurfs );
static void RB_RenderDlightShadowAtlas( void );
static void RB_RenderSpotShadowAtlas( drawSurf_t *drawSurfs, int numDrawSurfs );

static qboolean RB_ShadowManagerPreMainPass( shadowManagerPass_t pass )
{
	switch ( pass ) {
		case SHADOW_MANAGER_PASS_CSM_ATLAS:
		case SHADOW_MANAGER_PASS_POINT_ATLAS:
		case SHADOW_MANAGER_PASS_SPOT_ATLAS:
			return qtrue;
		default:
			return qfalse;
	}
}

static void RB_RunShadowManagerPass( shadowManagerPass_t pass,
	drawSurf_t *drawSurfs, int numDrawSurfs )
{
	switch ( pass ) {
		case SHADOW_MANAGER_PASS_CSM_ATLAS:
			RB_RenderCSMShadowAtlas( drawSurfs, numDrawSurfs );
			break;
		case SHADOW_MANAGER_PASS_POINT_ATLAS:
			RB_RenderDlightShadowAtlas();
			break;
		case SHADOW_MANAGER_PASS_SPOT_ATLAS:
			RB_RenderSpotShadowAtlas( drawSurfs, numDrawSurfs );
			break;
		case SHADOW_MANAGER_PASS_CSM_RECEIVER:
			RB_CSMShadowReceiverPass( drawSurfs, numDrawSurfs );
			break;
		default:
			break;
	}
}

static void RB_RunScheduledShadowManagerPass( shadowManagerPass_t pass,
	drawSurf_t *drawSurfs, int numDrawSurfs )
{
	if ( tr.shadowManager.planned &&
		!R_ShadowManagerPassScheduled( &tr.shadowManager, pass ) ) {
		return;
	}

	RB_RunShadowManagerPass( pass, drawSurfs, numDrawSurfs );
}

static void RB_RunScheduledShadowManagerPreMainPasses( drawSurf_t *drawSurfs, int numDrawSurfs )
{
	static const shadowManagerPass_t fallbackPassOrder[] = {
		SHADOW_MANAGER_PASS_CSM_ATLAS,
		SHADOW_MANAGER_PASS_POINT_ATLAS,
		SHADOW_MANAGER_PASS_SPOT_ATLAS
	};
	const shadowManagerPass_t *passOrder;
	int passCount;
	int i;

	if ( tr.shadowManager.planned ) {
		passOrder = tr.shadowManager.scheduledPassOrder;
		passCount = MIN( tr.shadowManager.scheduledPasses,
			SHADOW_MANAGER_MAX_SCHEDULED_PASSES );
	} else {
		passOrder = fallbackPassOrder;
		passCount = (int)ARRAY_LEN( fallbackPassOrder );
	}

	for ( i = 0; i < passCount; i++ ) {
		if ( !RB_ShadowManagerPreMainPass( passOrder[i] ) ) {
			continue;
		}
		RB_RunScheduledShadowManagerPass( passOrder[i], drawSurfs, numDrawSurfs );
	}
}
#endif

static qboolean RB_DrawSurfIncluded( int entityNum, const shader_t *shader, int fogNum, rbDrawSurfsMode_t mode )
{
	const qboolean rtBaseSurface =
		( entityNum == REFENTITYNUM_WORLD &&
			R_RtShaderNativeSupported( shader ) &&
			fogNum == 0 ) ? qtrue : qfalse;

	if ( mode == RB_DRAWSURFS_ALL ) {
		return qtrue;
	}
	if ( mode == RB_DRAWSURFS_RT_BASE ) {
		return rtBaseSurface;
	}

	/*
	 * The pre-trace raster pass contains only static world materials whose
	 * coverage and base texture semantics are faithfully represented by
	 * closest-hit shading. Draw every other surface exactly once after the
	 * trace: entities, remaps, animated/deformed/multi-stage materials,
	 * portals, sky, decals, alpha/translucency, fogged world, particles,
	 * effects and stencil shadow volumes.
	 */
	return rtBaseSurface ? qfalse : qtrue;
}

#ifdef USE_VULKAN
static qboolean RB_IsPrimaryFullView( void )
{
	return ( ( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) == 0 &&
		backEnd.viewParms.portalView == PV_NONE &&
		backEnd.viewParms.stereoFrame == STEREO_CENTER &&
		backEnd.viewParms.viewportX == 0 &&
		backEnd.viewParms.viewportY == 0 &&
		backEnd.viewParms.viewportWidth == glConfig.vidWidth &&
		backEnd.viewParms.viewportHeight == glConfig.vidHeight ) ?
			qtrue : qfalse;
}

static void RB_DrawWorldCelOutlineForScene( qboolean *drawn )
{
	if ( !drawn || *drawn || !R_CelShadingWorldActive() ) {
		return;
	}

	vk_draw_world_cel_outline();
	*drawn = qtrue;
}

static qboolean RB_ShaderNeedsLiquidSnapshot( const shader_t *shader )
{
	const shader_t *state;

	if ( !shader || shader->sort < SS_FOG ) {
		return qfalse;
	}
	state = shader->remappedShader ? shader->remappedShader : shader;
	if ( !R_LiquidShaderSupported( state ) ||
		!r_liquid || r_liquid->integer <= 0 ||
		( ( !r_liquidRefraction || r_liquidRefraction->value <= 0.0f ) &&
		  ( !r_liquidReflection || r_liquidReflection->value <= 0.0f ) ) ||
		!R_LiquidContentsEnabled( shader->contentFlags | state->contentFlags,
			r_liquid->integer ) ) {
		return qfalse;
	}
	if ( !vk.fboActive || backEnd.liquidScreenMapDone ||
		( vk.renderPassIndex != RENDER_PASS_MAIN &&
		  vk.renderPassIndex != RENDER_PASS_POST_BLOOM ) ||
		!RB_IsPrimaryFullView() ||
		glConfig.stereoEnabled ) {
		return qfalse;
	}
	return qtrue;
}

static qboolean RB_DrawSurfListNeedsLiquidSnapshot( drawSurf_t *drawSurfs,
	int numDrawSurfs, rbDrawSurfsMode_t mode )
{
	int i;

	if ( !drawSurfs || numDrawSurfs <= 0 || !r_liquid ||
		r_liquid->integer <= 0 ||
		( ( !r_liquidRefraction || r_liquidRefraction->value <= 0.0f ) &&
		  ( !r_liquidReflection || r_liquidReflection->value <= 0.0f ) ) ||
		!vk.fboActive ||
		vk.liquidSnapshot.color_descriptor == VK_NULL_HANDLE ) {
		return qfalse;
	}
	for ( i = 0; i < numDrawSurfs; i++ ) {
		shader_t *shader;
		int entityNum, fogNum, dlighted;

		R_DecomposeSort( drawSurfs[i].sort, &entityNum, &shader,
			&fogNum, &dlighted );
		if ( !RB_DrawSurfIncluded( entityNum, shader, fogNum, mode ) ) {
			continue;
		}
		if ( RB_ShaderNeedsLiquidSnapshot( shader ) ) {
			return qtrue;
		}
	}
	return qfalse;
}
#endif

static void RB_RenderDrawSurfList( drawSurf_t *drawSurfs, int numDrawSurfs, rbDrawSurfsMode_t mode ) {
	shader_t		*shader = NULL, *oldShader;
	int				fogNum = 0;
	int				entityNum = REFENTITYNUM_WORLD, oldEntityNum;
	int				dlighted = 0;
	qboolean		depthRange, isCrosshair;
#ifndef USE_VULKAN
	qboolean		oldDepthRange, wasCrosshair;
#endif
	int				i;
	drawSurf_t		*drawSurf;
	unsigned int	oldSort;
	qboolean		sortDecomposed;
#ifdef USE_PMLIGHT
	float			oldShaderSort;
	qboolean		csmReceiverDrawn;
#endif
#ifdef USE_VULKAN
	qboolean		depthFadeSnapshot;
	qboolean		liquidSnapshotPending;
	qboolean		worldCelOutlineDrawn;
#endif
	double			originalTime; // -EC-

	// save original time for entity shader offsets
	originalTime = backEnd.refdef.floatTime;

	// draw everything
	oldEntityNum = -1;
	backEnd.currentEntity = &tr.worldEntity;
	oldShader = NULL;
#ifndef USE_VULKAN
	oldDepthRange = qfalse;
	wasCrosshair = qfalse;
#endif
	oldSort = MAX_UINT;
#ifdef USE_PMLIGHT
	oldShaderSort = -1;
	csmReceiverDrawn = ( mode == RB_DRAWSURFS_RT_OVERLAY ) ? qtrue : qfalse;
#endif
#ifdef USE_VULKAN
	depthFadeSnapshot = vk_depth_fade_ready();
	worldCelOutlineDrawn = qfalse;
	liquidSnapshotPending = RB_DrawSurfListNeedsLiquidSnapshot(
		drawSurfs, numDrawSurfs, mode );
#endif
	depthRange = qfalse;

	if ( mode != RB_DRAWSURFS_RT_OVERLAY ) {
		backEnd.pc.c_surfaces += numDrawSurfs;
	}

	for (i = 0, drawSurf = drawSurfs ; i < numDrawSurfs ; i++, drawSurf++) {
		sortDecomposed = qfalse;
		if ( mode != RB_DRAWSURFS_ALL ) {
			R_DecomposeSort( drawSurf->sort, &entityNum, &shader, &fogNum, &dlighted );
			sortDecomposed = qtrue;
			if ( !RB_DrawSurfIncluded( entityNum, shader, fogNum, mode ) ) {
				continue;
			}
		}

		if ( drawSurf->sort == oldSort ) {
			// fast path, same as previous sort
			rb_surfaceTable[ *drawSurf->surface ]( drawSurf->surface );
			continue;
		}

		if ( !sortDecomposed ) {
			R_DecomposeSort( drawSurf->sort, &entityNum, &shader, &fogNum, &dlighted );
		}
#ifdef USE_VULKAN
		if ( vk.renderPassIndex == RENDER_PASS_SCREENMAP && entityNum != REFENTITYNUM_WORLD && backEnd.refdef.entities[ entityNum ].e.renderfx & RF_DEPTHHACK ) {
			continue;
		}
		if ( !depthFadeSnapshot && shader &&
			shader->sort > SS_OPAQUE &&
			RB_IsPrimaryFullView() &&
			( vk.renderPassIndex == RENDER_PASS_MAIN ||
			  vk.renderPassIndex == RENDER_PASS_POST_BLOOM ) &&
			vk_depth_fade_available() ) {
			RB_EndSurface();
			vk_copy_depth_fade();
			depthFadeSnapshot = vk_depth_fade_ready();
			if ( depthFadeSnapshot ) {
				oldShader = NULL;
				oldSort = MAX_UINT;
				oldEntityNum = -1;
			}
		}
#endif
		//
		// change the tess parameters if needed
		// a "entityMergable" shader is a shader that can have surfaces from separate
		// entities merged into a single batch, like smoke and blood puff sprites
		if ( ( (oldSort ^ drawSurf->sort ) & ~QSORT_REFENTITYNUM_MASK )
			|| ( !qlRendererCvars.forceMergeEntities->integer && !shader->entityMergable ) ) {
			//if ( oldShader != NULL ) {
				RB_EndSurface();
			//}
#ifdef USE_PMLIGHT
			#define INSERT_POINT SS_FOG
			if ( !csmReceiverDrawn && oldShaderSort < INSERT_POINT && shader->sort >= INSERT_POINT ) {
				RB_RunScheduledShadowManagerPass( SHADOW_MANAGER_PASS_CSM_RECEIVER,
					drawSurfs, numDrawSurfs );
				csmReceiverDrawn = qtrue;
				oldEntityNum = -1;
			}
			if ( mode != RB_DRAWSURFS_RT_BASE &&
				backEnd.refdef.numLitSurfs && oldShaderSort < INSERT_POINT && shader->sort >= INSERT_POINT ) {
				//RB_BeginDrawingLitSurfs(); // no need, already setup in RB_BeginDrawingView()
#ifdef USE_VULKAN
				RB_LightingPass( mode, qtrue );
#else
				if ( depthRange ) {
					qglDepthRange( 0, 1 );
					RB_LightingPass( mode, qtrue );
					qglDepthRange( 0, 0.3 );
				} else {
					RB_LightingPass( mode, qtrue );
				}
#endif
				oldEntityNum = -1; // force matrix setup
			}
			oldShaderSort = shader->sort;
#endif
#ifdef USE_VULKAN
			/* Keep screen-space world edges below fog, liquids and transparency. */
			if ( shader->sort > SS_OPAQUE ) {
				RB_DrawWorldCelOutlineForScene( &worldCelOutlineDrawn );
			}
			/*
			 * Snapshot only after opaque raster/PMLIGHT work, and before the
			 * first fog/liquid/translucent batch. In RT composition this runs
			 * in the overlay list, after the trace has produced the base.
			 */
			if ( liquidSnapshotPending && shader->sort >= SS_FOG ) {
				if ( !vk_depth_fade_ready() &&
					vk_depth_fade_available() ) {
					vk_copy_depth_fade();
				}
				if ( vk_capture_liquid_scene() ) {
					oldSort = MAX_UINT;
					oldEntityNum = -1;
					oldShader = NULL;
				}
				liquidSnapshotPending = qfalse;
			}
#endif
			RB_BeginSurface( shader, fogNum );
			oldShader = shader;
		}

		oldSort = drawSurf->sort;

		//
		// change the modelview matrix if needed
		//
		if ( entityNum != oldEntityNum ) {
			depthRange = isCrosshair = qfalse;

			if ( entityNum != REFENTITYNUM_WORLD ) {
				backEnd.currentEntity = &backEnd.refdef.entities[entityNum];
				if ( backEnd.currentEntity->intShaderTime )
					backEnd.refdef.floatTime = originalTime - (double)(backEnd.currentEntity->e.shaderTime.i) * 0.001;
				else
					backEnd.refdef.floatTime = originalTime - (double)backEnd.currentEntity->e.shaderTime.f;

				// set up the transformation matrix
				R_RotateForEntity( backEnd.currentEntity, &backEnd.viewParms, &backEnd.or );
				// set up the dynamic lighting if needed
#ifdef USE_LEGACY_DLIGHTS
#ifdef USE_PMLIGHT
				if ( !r_dlightMode->integer )
#endif
				if ( backEnd.currentEntity->needDlights ) {
					R_TransformDlights( backEnd.refdef.num_dlights, backEnd.refdef.dlights, &backEnd.or );
				}
#endif // USE_LEGACY_DLIGHTS
				if ( backEnd.currentEntity->e.renderfx & RF_DEPTHHACK ) {
					// hack the depth range to prevent view model from poking into walls
					depthRange = qtrue;

					if(backEnd.currentEntity->e.renderfx & RF_CROSSHAIR)
						isCrosshair = qtrue;
				}
			} else {
				backEnd.currentEntity = &tr.worldEntity;
				backEnd.refdef.floatTime = originalTime;
				backEnd.or = backEnd.viewParms.world;
#ifdef USE_LEGACY_DLIGHTS
#ifdef USE_PMLIGHT
				if ( !r_dlightMode->integer )
#endif
				R_TransformDlights( backEnd.refdef.num_dlights, backEnd.refdef.dlights, &backEnd.or );
#endif // USE_LEGACY_DLIGHTS
			}

			// we have to reset the shaderTime as well otherwise image animations on
			// the world (like water) continue with the wrong frame
			tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;

#ifdef USE_VULKAN
			Com_Memcpy( vk_world.modelview_transform, backEnd.or.modelMatrix, 64 );
			tess.depthRange = depthRange ? DEPTH_RANGE_WEAPON : DEPTH_RANGE_NORMAL;
			vk_update_mvp( NULL );
#else
			qglLoadMatrixf( backEnd.or.modelMatrix );
#endif

			//
			// change depthrange. Also change projection matrix so first person weapon does not look like coming
			// out of the screen.
			//
#ifndef USE_VULKAN
			if (oldDepthRange != depthRange || wasCrosshair != isCrosshair)
			{
				if (depthRange)
				{
					if(backEnd.viewParms.stereoFrame != STEREO_CENTER)
					{
						if(isCrosshair)
						{
							if(oldDepthRange)
							{
								// was not a crosshair but now is, change back proj matrix
								qglMatrixMode(GL_PROJECTION);
								qglLoadMatrixf(backEnd.viewParms.projectionMatrix);
								qglMatrixMode(GL_MODELVIEW);
							}
						}
						else
						{
							viewParms_t temp = backEnd.viewParms;

							R_SetupProjection(&temp, r_znear->value, qfalse);

							qglMatrixMode(GL_PROJECTION);
							qglLoadMatrixf(temp.projectionMatrix);
							qglMatrixMode(GL_MODELVIEW);
						}
					}

					if(!oldDepthRange)
						qglDepthRange (0, 0.3);
				}
				else
				{
					if(!wasCrosshair && backEnd.viewParms.stereoFrame != STEREO_CENTER)
					{
						qglMatrixMode(GL_PROJECTION);
						qglLoadMatrixf(backEnd.viewParms.projectionMatrix);
						qglMatrixMode(GL_MODELVIEW);
					}

					qglDepthRange (0, 1);
				}
				oldDepthRange = depthRange;
				wasCrosshair = isCrosshair;
			}
#endif

			oldEntityNum = entityNum;
		}

		// add the triangles for this surface
		rb_surfaceTable[ *drawSurf->surface ]( drawSurf->surface );
	}

	// draw the contents of the last shader batch
	if ( oldShader != NULL ) {
		RB_EndSurface();
	}

#ifdef USE_PMLIGHT
	if ( !csmReceiverDrawn ) {
		RB_RunScheduledShadowManagerPass( SHADOW_MANAGER_PASS_CSM_RECEIVER,
			drawSurfs, numDrawSurfs );
	}
#endif

#ifdef USE_VULKAN
	if ( mode != RB_DRAWSURFS_RT_BASE && !worldCelOutlineDrawn ) {
		RB_DrawWorldCelOutlineForScene( &worldCelOutlineDrawn );
	}
#endif

	backEnd.refdef.floatTime = originalTime;

	// go back to the world modelview matrix
#ifdef USE_VULKAN
	Com_Memcpy( vk_world.modelview_transform, backEnd.viewParms.world.modelMatrix, 64 );
	tess.depthRange = DEPTH_RANGE_NORMAL;
	//vk_update_mvp();
#else
	qglLoadMatrixf( backEnd.viewParms.world.modelMatrix );
	if ( depthRange ) {
		qglDepthRange(0, 1);
	}
#endif
}


#ifdef USE_PMLIGHT
/*
=================
RB_BeginDrawingLitView
=================
*/
static void RB_BeginDrawingLitSurfs( void )
{
	// we will need to change the projection matrix before drawing
	// 2D images again
	backEnd.projection2D = qfalse;

	// we will only draw a sun if there was sky rendered in this view
	backEnd.skyRenderedThisView = qfalse;

	//
	// set the modelview matrix for the viewer
	//
	SetViewportAndScissor();

	glState.faceCulling = -1;		// force face culling to set next time
}


/*
==================
RB_RenderLitSurfList
==================
*/
static void RB_RenderLitSurfList( dlight_t* dl, rbDrawSurfsMode_t mode ) {
	shader_t		*shader = NULL, *oldShader;
	int				fogNum = 0;
	int				entityNum = REFENTITYNUM_WORLD, oldEntityNum;
#ifndef USE_VULKAN
	qboolean		oldDepthRange, wasCrosshair;
#endif
	qboolean		depthRange, isCrosshair;
	const litSurf_t	*litSurf;
	unsigned int	oldSort;
	qboolean		sortDecomposed;
	double			originalTime; // -EC-

	// save original time for entity shader offsets
	originalTime = backEnd.refdef.floatTime;

	// draw everything
	oldEntityNum = -1;
	backEnd.currentEntity = &tr.worldEntity;
	oldShader = NULL;
#ifndef USE_VULKAN
	oldDepthRange = qfalse;
	wasCrosshair = qfalse;
#endif
	oldSort = MAX_UINT;
	depthRange = qfalse;

	tess.dlightUpdateParams = qtrue;

	for ( litSurf = dl->head; litSurf; litSurf = litSurf->next ) {
		if ( litSurf->flags & LSF_SHADOW_CASTER_ONLY ) {
			continue;
		}
		sortDecomposed = qfalse;
		if ( mode != RB_DRAWSURFS_ALL ) {
			R_DecomposeLitSort( litSurf->sort, &entityNum, &shader, &fogNum );
			sortDecomposed = qtrue;
			if ( !RB_DrawSurfIncluded( entityNum, shader, fogNum, mode ) ) {
				continue;
			}
		}

		//if ( litSurf->sort == sort ) {
		if ( litSurf->sort == oldSort ) {
			// fast path, same as previous sort
			rb_surfaceTable[ *litSurf->surface ]( litSurf->surface );
			continue;
		}

		if ( !sortDecomposed ) {
			R_DecomposeLitSort( litSurf->sort, &entityNum, &shader, &fogNum );
		}
#ifdef USE_VULKAN
		if ( vk.renderPassIndex == RENDER_PASS_SCREENMAP && entityNum != REFENTITYNUM_WORLD && backEnd.refdef.entities[ entityNum ].e.renderfx & RF_DEPTHHACK ) {
			continue;
		}
#endif
		// anything BEFORE opaque is sky/portal, anything AFTER it should never have been added
		//assert( shader->sort == SS_OPAQUE );
		// !!! but MIRRORS can trip that assert, so just do this for now
		//if ( shader->sort < SS_OPAQUE )
		//	continue;

		//
		// change the tess parameters if needed
		// a "entityMergable" shader is a shader that can have surfaces from separate
		// entities merged into a single batch, like smoke and blood puff sprites
		if ( ( (oldSort ^ litSurf->sort) & ~QSORT_REFENTITYNUM_MASK )
			|| ( !qlRendererCvars.forceMergeEntities->integer && !shader->entityMergable ) ) {
			if ( oldShader != NULL ) {
				RB_EndSurface();
			}
			RB_BeginSurface( shader, fogNum );
			oldShader = shader;
		}

		oldSort = litSurf->sort;

		//
		// change the modelview matrix if needed
		//
		if ( entityNum != oldEntityNum ) {
			depthRange = isCrosshair = qfalse;

			if ( entityNum != REFENTITYNUM_WORLD ) {
				backEnd.currentEntity = &backEnd.refdef.entities[entityNum];

				if ( backEnd.currentEntity->intShaderTime )
					backEnd.refdef.floatTime = originalTime - (double)(backEnd.currentEntity->e.shaderTime.i) * 0.001;
				else
					backEnd.refdef.floatTime = originalTime - (double)backEnd.currentEntity->e.shaderTime.f;

				// set up the transformation matrix
				R_RotateForEntity( backEnd.currentEntity, &backEnd.viewParms, &backEnd.or );

				if ( backEnd.currentEntity->e.renderfx & RF_DEPTHHACK ) {
					// hack the depth range to prevent view model from poking into walls
					depthRange = qtrue;

					if(backEnd.currentEntity->e.renderfx & RF_CROSSHAIR)
						isCrosshair = qtrue;
				}
			} else {
				backEnd.currentEntity = &tr.worldEntity;
				backEnd.refdef.floatTime = originalTime;
				backEnd.or = backEnd.viewParms.world;
			}

			// we have to reset the shaderTime as well otherwise image animations on
			// the world (like water) continue with the wrong frame
			tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;

			// set up the dynamic lighting
			R_TransformDlights( 1, dl, &backEnd.or );
			tess.dlightUpdateParams = qtrue;

#ifdef USE_VULKAN
			tess.depthRange = depthRange ? DEPTH_RANGE_WEAPON : DEPTH_RANGE_NORMAL;
			Com_Memcpy( vk_world.modelview_transform, backEnd.or.modelMatrix, 64 );
			vk_update_mvp( NULL );
#else
			qglLoadMatrixf( backEnd.or.modelMatrix );

			//
			// change depthrange. Also change projection matrix so first person weapon does not look like coming
			// out of the screen.
			//

			if (oldDepthRange != depthRange || wasCrosshair != isCrosshair)
			{
				if (depthRange)
				{
					if(backEnd.viewParms.stereoFrame != STEREO_CENTER)
					{
						if(isCrosshair)
						{
							if(oldDepthRange)
							{
								// was not a crosshair but now is, change back proj matrix
								qglMatrixMode(GL_PROJECTION);
								qglLoadMatrixf(backEnd.viewParms.projectionMatrix);
								qglMatrixMode(GL_MODELVIEW);
							}
						}
						else
						{
							viewParms_t temp = backEnd.viewParms;

							R_SetupProjection(&temp, r_znear->value, qfalse);

							qglMatrixMode(GL_PROJECTION);
							qglLoadMatrixf(temp.projectionMatrix);
							qglMatrixMode(GL_MODELVIEW);
						}
					}

					if(!oldDepthRange)
						qglDepthRange (0, 0.3);
				}
				else
				{
					if(!wasCrosshair && backEnd.viewParms.stereoFrame != STEREO_CENTER)
					{
						qglMatrixMode(GL_PROJECTION);
						qglLoadMatrixf(backEnd.viewParms.projectionMatrix);
						qglMatrixMode(GL_MODELVIEW);
					}

					qglDepthRange (0, 1);
				}
				oldDepthRange = depthRange;
				wasCrosshair = isCrosshair;
			}
#endif

			oldEntityNum = entityNum;
		}

		// add the triangles for this surface
		rb_surfaceTable[ *litSurf->surface ]( litSurf->surface );
	}

	// draw the contents of the last shader batch
	if ( oldShader != NULL ) {
		RB_EndSurface();
	}

	backEnd.refdef.floatTime = originalTime;

	// go back to the world modelview matrix
#ifdef USE_VULKAN
	Com_Memcpy( vk_world.modelview_transform, backEnd.viewParms.world.modelMatrix, 64 );
	tess.depthRange = DEPTH_RANGE_NORMAL;
	//vk_update_mvp();
#else
	qglLoadMatrixf( backEnd.viewParms.world.modelMatrix );
	if ( depthRange ) {
		qglDepthRange (0, 1);
	}
#endif // !USE_VULKAN
}
#endif // USE_PMLIGHT


/*
============================================================================

RENDER BACK END FUNCTIONS

============================================================================
*/

/*
================
RB_SetGL2D
================
*/
static void RB_SetGL2D( void ) {
	backEnd.projection2D = qtrue;

#ifdef USE_VULKAN
	vk_update_mvp( NULL );

	// force depth range and viewport/scissor updates
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;
#else
	// set 2D virtual screen size
	qglViewport( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
	qglScissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
	qglMatrixMode( GL_PROJECTION );
	qglLoadMatrixf( GL_Ortho( 0, glConfig.vidWidth, glConfig.vidHeight, 0, 0, 1 ) );
	qglMatrixMode( GL_MODELVIEW );
	qglLoadIdentity();

	GL_State( GLS_DEPTHTEST_DISABLE |
		GLS_SRCBLEND_SRC_ALPHA |
		GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );

	GL_Cull( CT_TWO_SIDED );
	qglDisable( GL_CLIP_PLANE0 );
#endif

	// set time for 2D shaders
	backEnd.refdef.time = ri.Milliseconds();
	backEnd.refdef.floatTime = (double)backEnd.refdef.time * 0.001; // -EC-: cast to double
}


/*
=============
RE_StretchRaw

FIXME: not exactly backend
Stretches a raw 32 bit power of 2 bitmap image over the given screen rectangle.
Used for cinematics.
=============
*/
void RE_StretchRaw( int x, int y, int w, int h, int cols, int rows, const byte *data, int client, qboolean dirty ) {
	int			i, j;
	int			start, end;

	if ( !tr.registered ) {
		return;
	}

	start = 0;
	if ( r_speeds->integer ) {
		start = ri.Milliseconds();
	}

	// make sure rows and cols are powers of 2
	for ( i = 0 ; ( 1 << i ) < cols ; i++ ) {
	}
	for ( j = 0 ; ( 1 << j ) < rows ; j++ ) {
	}

	if ( ( 1 << i ) != cols || ( 1 << j ) != rows ) {
		ri.Error( ERR_DROP, "%s(): size not a power of 2: %i by %i", __func__, cols, rows );
	}

	RE_UploadCinematic( w, h, cols, rows, data, client, dirty );

	if ( r_speeds->integer ) {
		end = ri.Milliseconds();
		ri.Printf( PRINT_ALL, "RE_UploadCinematic( %i, %i ): %i msec\n", cols, rows, end - start );
	}

	tr.cinematicShader->stages[0]->bundle[0].image[0] = tr.scratchImage[client];
	RE_StretchPic( x, y, w, h, 0.5f / cols, 0.5f / rows, 1.0f - 0.5f / cols, 1.0f - 0.5 / rows, tr.cinematicShader->index );
}


void RE_UploadCinematic( int w, int h, int cols, int rows, const byte *data, int client, qboolean dirty ) {

	image_t *image;

	if ( !tr.scratchImage[ client ] ) {
		tr.scratchImage[ client ] = R_CreateImage( va( "*scratch%i", client ), NULL, (byte *)data, cols, rows, IMGFLAG_CLAMPTOEDGE | IMGFLAG_RGB | IMGFLAG_NOSCALE );
		return;
	}

	image = tr.scratchImage[ client ];

#ifndef USE_VULKAN
	GL_Bind( image );
#endif

	// if the scratchImage isn't in the format we want, specify it as a new texture
	if ( cols != image->width || rows != image->height ) {
		image->width = image->uploadWidth = cols;
		image->height = image->uploadHeight = rows;
#ifdef USE_VULKAN
		vk_create_image( image, cols, rows, 1 );
		vk_upload_image_data( image, 0, 0, cols, rows, 1, (byte *)data, cols * rows * 4, qfalse );
#else
		qglTexImage2D( GL_TEXTURE_2D, 0, image->internalFormat, cols, rows, 0, GL_RGBA, GL_UNSIGNED_BYTE, data );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gl_clamp_mode );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gl_clamp_mode );
#endif
	} else if ( dirty ) {
		// otherwise, just subimage upload it so that drivers can tell we are going to be changing
		// it and don't try and do a texture compression
#ifdef USE_VULKAN
		vk_upload_image_data( image, 0, 0, cols, rows, 1, (byte *)data, cols * rows * 4, qtrue );
#else
		qglTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, cols, rows, GL_RGBA, GL_UNSIGNED_BYTE, data );
#endif
	}
}


/*
=============
RB_SetColor
=============
*/
static const void *RB_SetColor( const void *data ) {
	const setColorCommand_t	*cmd;

	cmd = (const setColorCommand_t *)data;
	RB_FrameGraph_NotePass( RTX_FRAME_PASS_UI_2D );

	backEnd.color2D.rgba[0] = cmd->color[0] * 255;
	backEnd.color2D.rgba[1] = cmd->color[1] * 255;
	backEnd.color2D.rgba[2] = cmd->color[2] * 255;
	backEnd.color2D.rgba[3] = cmd->color[3] * 255;

	return (const void *)(cmd + 1);
}


/*
=============
RB_StretchPic
=============
*/
static const void *RB_StretchPic( const void *data ) {
	const stretchPicCommand_t	*cmd;
	shader_t *shader;

	cmd = (const stretchPicCommand_t *)data;
	RB_FrameGraph_NotePass( RTX_FRAME_PASS_UI_2D );

	shader = cmd->shader;
	if ( shader != tess.shader ) {
		if ( tess.numIndexes ) {
			RB_EndSurface();
		}
		backEnd.currentEntity = &backEnd.entity2D;
		RB_BeginSurface( shader, 0 );
	}

#ifdef USE_VBO
	VBO_UnBind();
#endif

	if ( !backEnd.projection2D ) {
		RB_SetGL2D();
	}

#ifdef USE_VULKAN
	if ( r_bloom->integer ) {
		vk_bloom();
	}
#endif

	RB_AddQuadStamp2( cmd->x, cmd->y, cmd->w, cmd->h, cmd->s1, cmd->t1, cmd->s2, cmd->t2, backEnd.color2D );

	return (const void *)(cmd + 1);
}


#ifdef USE_PMLIGHT
static qboolean RB_DlightShadowsNeeded( void )
{
	int i;

	if ( ( !r_dlightShadows || !r_dlightShadows->integer ) &&
		( !r_shadowCorrectness || !r_shadowCorrectness->integer ) ) {
		return qfalse;
	}

	if ( tr.shadowManager.planned ) {
		return R_ShadowManagerPassScheduled( &tr.shadowManager,
			SHADOW_MANAGER_PASS_POINT_ATLAS );
	}

	for ( i = 0; i < backEnd.viewParms.num_dlights; i++ ) {
		if ( backEnd.viewParms.dlights[i].shadowPlanned ) {
			return qtrue;
		}
	}

	return qfalse;
}
static qboolean RB_ShadowCorrectnessRejectsAlphaTest( const shader_t *shader )
{
	int i;

	if ( !r_shadowCorrectness || !r_shadowCorrectness->integer || !shader ) {
		return qfalse;
	}

	for ( i = 0; i < shader->numUnfoggedPasses; i++ ) {
		const shaderStage_t *stage = shader->stages[i];

		if ( stage && ( stage->stateBits & GLS_ATEST_BITS ) ) {
			return qtrue;
		}
	}

	return qfalse;
}

static const float rb_dlightShadowFlipMatrix[16] = {
	0, 0, -1, 0,
	-1, 0, 0, 0,
	0, 1, 0, 0,
	0, 0, 0, 1
};

static void RB_DlightShadowMultMatrix( const float *a, const float *b, float *out )
{
	int i, j;

	for ( i = 0; i < 4; i++ ) {
		for ( j = 0; j < 4; j++ ) {
			out[ i * 4 + j ] =
				a[ i * 4 + 0 ] * b[ 0 * 4 + j ] +
				a[ i * 4 + 1 ] * b[ 1 * 4 + j ] +
				a[ i * 4 + 2 ] * b[ 2 * 4 + j ] +
				a[ i * 4 + 3 ] * b[ 3 * 4 + j ];
		}
	}
}

static void RB_SetDlightShadowFaceAxis( vec3_t baseAxis[3], int faceIndex, vec3_t outAxis[3] )
{
	switch ( faceIndex ) {
		case 0:
			AxisCopy( baseAxis, outAxis );
			break;
		case 1:
			VectorNegate( baseAxis[0], outAxis[0] );
			VectorNegate( baseAxis[1], outAxis[1] );
			VectorCopy( baseAxis[2], outAxis[2] );
			break;
		case 2:
			VectorCopy( baseAxis[1], outAxis[0] );
			VectorNegate( baseAxis[0], outAxis[1] );
			VectorCopy( baseAxis[2], outAxis[2] );
			break;
		case 3:
			VectorNegate( baseAxis[1], outAxis[0] );
			VectorCopy( baseAxis[0], outAxis[1] );
			VectorCopy( baseAxis[2], outAxis[2] );
			break;
		case 4:
			VectorCopy( baseAxis[2], outAxis[0] );
			VectorCopy( baseAxis[1], outAxis[1] );
			VectorNegate( baseAxis[0], outAxis[2] );
			break;
		case 5:
		default:
			VectorNegate( baseAxis[2], outAxis[0] );
			VectorCopy( baseAxis[1], outAxis[1] );
			VectorCopy( baseAxis[0], outAxis[2] );
			break;
	}
}

static void RB_SetDlightShadowProjectionZ( viewParms_t *viewParms, float zNear, float zFar )
{
	float depth = zFar - zNear;

	viewParms->projectionMatrix[2] = 0.0f;
	viewParms->projectionMatrix[6] = 0.0f;
#ifdef USE_REVERSED_DEPTH
	viewParms->projectionMatrix[10] = zNear / depth;
	viewParms->projectionMatrix[14] = zFar * zNear / depth;
#else
	viewParms->projectionMatrix[10] = -zFar / depth;
	viewParms->projectionMatrix[14] = -zFar * zNear / depth;
#endif
}

static void RB_BuildDlightShadowView( const dlight_t *dl, const shadowPointLightPlan_t *plan,
	int face, int atlasHeight, viewParms_t *shadowParms )
{
	float viewerMatrix[16];
	float zNear;
	float zFar;
	vec3_t baseAxis[3];
	int atlasX;
	int atlasY;
	int atlasFaceSize;

	Com_Memset( shadowParms, 0, sizeof( *shadowParms ) );
	atlasX = plan ? plan->atlasX[face] : dl->shadowAtlasX[face];
	atlasY = plan ? plan->atlasY[face] : dl->shadowAtlasY[face];
	atlasFaceSize = plan ? plan->atlasFaceSize : dl->shadowAtlasFaceSize;
	shadowParms->viewportX = atlasX;
	shadowParms->viewportY = atlasHeight - atlasY - atlasFaceSize;
	shadowParms->viewportWidth = atlasFaceSize;
	shadowParms->viewportHeight = atlasFaceSize;
	shadowParms->scissorX = shadowParms->viewportX;
	shadowParms->scissorY = shadowParms->viewportY;
	shadowParms->scissorWidth = shadowParms->viewportWidth;
	shadowParms->scissorHeight = shadowParms->viewportHeight;
	shadowParms->fovX = 90.0f;
	shadowParms->fovY = 90.0f;
	shadowParms->zFar = MAX( dl->radius, 64.0f );
	shadowParms->stereoFrame = STEREO_CENTER;
	shadowParms->portalView = PV_NONE;
	shadowParms->passFlags = VPF_DLIGHT_SHADOW;
	VectorCopy( dl->origin, shadowParms->or.origin );
	VectorCopy( dl->origin, shadowParms->pvsOrigin );

	VectorSet( baseAxis[0], 1.0f, 0.0f, 0.0f );
	VectorSet( baseAxis[1], 0.0f, 1.0f, 0.0f );
	VectorSet( baseAxis[2], 0.0f, 0.0f, 1.0f );
	RB_SetDlightShadowFaceAxis( baseAxis, face, shadowParms->or.axis );

	Com_Memset( &shadowParms->world, 0, sizeof( shadowParms->world ) );
	shadowParms->world.axis[0][0] = 1.0f;
	shadowParms->world.axis[1][1] = 1.0f;
	shadowParms->world.axis[2][2] = 1.0f;
	VectorCopy( shadowParms->or.origin, shadowParms->world.viewOrigin );

	viewerMatrix[0] = shadowParms->or.axis[0][0];
	viewerMatrix[4] = shadowParms->or.axis[0][1];
	viewerMatrix[8] = shadowParms->or.axis[0][2];
	viewerMatrix[12] = -dl->origin[0] * viewerMatrix[0] + -dl->origin[1] * viewerMatrix[4] + -dl->origin[2] * viewerMatrix[8];
	viewerMatrix[1] = shadowParms->or.axis[1][0];
	viewerMatrix[5] = shadowParms->or.axis[1][1];
	viewerMatrix[9] = shadowParms->or.axis[1][2];
	viewerMatrix[13] = -dl->origin[0] * viewerMatrix[1] + -dl->origin[1] * viewerMatrix[5] + -dl->origin[2] * viewerMatrix[9];
	viewerMatrix[2] = shadowParms->or.axis[2][0];
	viewerMatrix[6] = shadowParms->or.axis[2][1];
	viewerMatrix[10] = shadowParms->or.axis[2][2];
	viewerMatrix[14] = -dl->origin[0] * viewerMatrix[2] + -dl->origin[1] * viewerMatrix[6] + -dl->origin[2] * viewerMatrix[10];
	viewerMatrix[3] = 0.0f;
	viewerMatrix[7] = 0.0f;
	viewerMatrix[11] = 0.0f;
	viewerMatrix[15] = 1.0f;

	RB_DlightShadowMultMatrix( viewerMatrix, rb_dlightShadowFlipMatrix, shadowParms->world.modelMatrix );

	zNear = 1.0f;
	zFar = MAX( shadowParms->zFar, zNear + 1.0f );
	R_SetupProjection( shadowParms, zNear, qtrue );
	RB_SetDlightShadowProjectionZ( shadowParms, zNear, zFar );
}

static void RB_RecordShadowCorrectnessFace( const dlight_t *dl,
	const shadowPointLightPlan_t *plan, int face, int atlasWidth, int atlasHeight,
	unsigned int atlasGeneration, const viewParms_t *shadowParms,
	qboolean cached, qboolean rendered, int surfaces )
{
	shadowCorrectnessDebug_t *debug;
	shadowCorrectnessFaceDebug_t *record;
	int recordIndex;

	if ( !r_shadowCorrectness || !r_shadowCorrectness->integer || !dl || !shadowParms ) {
		return;
	}

	debug = &tr.shadowCorrectnessDebug;
	if ( !debug->active ) {
		Com_Memset( debug, 0, sizeof( *debug ) );
		debug->active = qtrue;
		debug->frameSceneNum = tr.shadowManager.frameSceneNum;
		debug->frameCount = tr.shadowManager.frameCount;
		debug->viewCount = tr.shadowManager.viewCount;
		debug->atlasWidth = atlasWidth;
		debug->atlasHeight = atlasHeight;
		debug->atlasGeneration = atlasGeneration;
		debug->atlasFaceSize = plan ? plan->atlasFaceSize : dl->shadowAtlasFaceSize;
#ifdef USE_REVERSED_DEPTH
		debug->reversedDepth = qtrue;
		debug->clearDepth = 0.0f;
#else
		debug->reversedDepth = qfalse;
		debug->clearDepth = 1.0f;
#endif
		debug->depthZeroToOne = qtrue;
		debug->apiViewportTopLeft = qtrue;
		debug->samplerTopLeft = qtrue;
		debug->clipYFlipped = qtrue;
		debug->receiverBias = R_ShadowClampReceiverBias(
			r_dlightShadowBias ? r_dlightShadowBias->value : 4.0f );
		debug->casterDepthBias = R_ShadowClampCasterDepthBias(
			r_dlightShadowCasterDepthBias ? r_dlightShadowCasterDepthBias->value : 1.0f );
		debug->casterSlopeBias = R_ShadowClampCasterSlopeBias(
			r_dlightShadowCasterSlopeBias ? r_dlightShadowCasterSlopeBias->value : 1.0f );
		debug->casterNormalBias = R_ShadowClampCasterNormalBias(
			r_dlightShadowCasterNormalBias ? r_dlightShadowCasterNormalBias->value : 0.25f );
		debug->requestedFilterMode =
			r_dlightShadowFilter ? r_dlightShadowFilter->integer : SHADOW_FILTER_POISSON_4;
		debug->effectiveFilterMode = SHADOW_FILTER_HARD;
		debug->filterSampleCount = R_ShadowFilterSampleCount( debug->effectiveFilterMode );
		R_ShadowFilterOffsets( debug->effectiveFilterMode,
			&debug->filterInnerOffset, &debug->filterOuterOffset );
	}

	if ( debug->faceCount >= SHADOW_CORRECTNESS_MAX_FACE_RECORDS ) {
		return;
	}

	recordIndex = debug->faceCount++;
	record = &debug->faces[recordIndex];
	Com_Memset( record, 0, sizeof( *record ) );
	record->valid = qtrue;
	record->dlightIndex = plan ? plan->dlightIndex : -1;
	record->shadowIndex = plan ? plan->shadowIndex : dl->shadowIndex;
	record->face = face;
	record->atlasBaseFace = plan ? plan->atlasBaseFace : dl->shadowAtlasBaseFace;
	record->atlasFaceSize = plan ? plan->atlasFaceSize : dl->shadowAtlasFaceSize;
	record->atlasX = plan ? plan->atlasX[face] : dl->shadowAtlasX[face];
	record->atlasY = plan ? plan->atlasY[face] : dl->shadowAtlasY[face];
	record->viewportX = shadowParms->viewportX;
	record->viewportY = shadowParms->viewportY;
	record->viewportWidth = shadowParms->viewportWidth;
	record->viewportHeight = shadowParms->viewportHeight;
	record->scissorX = shadowParms->scissorX;
	record->scissorY = shadowParms->scissorY;
	record->scissorWidth = shadowParms->scissorWidth;
	record->scissorHeight = shadowParms->scissorHeight;
	record->apiViewportX = shadowParms->viewportX;
	record->apiViewportY = atlasHeight -
		( shadowParms->viewportY + shadowParms->viewportHeight );
	record->apiViewportWidth = shadowParms->viewportWidth;
	record->apiViewportHeight = shadowParms->viewportHeight;
	record->apiScissorX = shadowParms->scissorX;
	record->apiScissorY = atlasHeight -
		( shadowParms->scissorY + shadowParms->scissorHeight );
	record->apiScissorWidth = shadowParms->scissorWidth;
	record->apiScissorHeight = shadowParms->scissorHeight;
	record->zNear = 1.0f;
	record->zFar = shadowParms->zFar;
	record->cached = cached ? qtrue : qfalse;
	record->rendered = rendered ? qtrue : qfalse;
	record->surfaces = surfaces;
	Com_Memcpy( record->projectionMatrix, shadowParms->projectionMatrix,
		sizeof( record->projectionMatrix ) );
	Com_Memcpy( record->modelMatrix, shadowParms->world.modelMatrix,
		sizeof( record->modelMatrix ) );
}

static void RB_SetDlightShadowView( const viewParms_t *shadowParms )
{
	backEnd.viewParms = *shadowParms;
	backEnd.or = shadowParms->world;
	backEnd.currentEntity = &tr.worldEntity;

	SetViewportAndScissor();
	Com_Memcpy( vk_world.modelview_transform, backEnd.or.modelMatrix, 64 );
	tess.depthRange = DEPTH_RANGE_NORMAL;
	vk_update_mvp( NULL );
}

static qboolean RB_DlightShadowCasterAllowed( const shader_t *shader, const surfaceType_t *surface )
{
	if ( !shader || !surface ) {
		return qfalse;
	}
	if ( shader->sort != SS_OPAQUE || shader->isSky || shader->polygonOffset ||
		(shader->surfaceFlags & ( SURF_SKY | SURF_NODLIGHT )) ) {
		return qfalse;
	}
	if ( shader->lightingStage < 0 ) {
		return qfalse;
	}
	if ( RB_ShadowCorrectnessRejectsAlphaTest( shader ) ) {
		return qfalse;
	}

	switch ( *surface ) {
		case SF_FACE:
		case SF_GRID:
		case SF_TRIANGLES:
		case SF_MD3:
		case SF_MDR:
		case SF_IQM:
			return qtrue;
		default:
			return qfalse;
	}
}

static qboolean RB_DlightShadowMD3Bounds( const md3Surface_t *surface, vec3_t mins, vec3_t maxs )
{
	const md3XyzNormal_t *oldVerts;
	const md3XyzNormal_t *newVerts;
	const trRefEntity_t *ent;
	vec3_t point;
	int oldFrame, newFrame;
	int i;

	if ( !surface || surface->numVerts <= 0 || surface->numFrames <= 0 ) {
		return qfalse;
	}

	ent = backEnd.currentEntity;
	if ( !ent ) {
		return qfalse;
	}

	oldFrame = ent->e.oldframe;
	newFrame = ent->e.frame;
	if ( oldFrame < 0 || oldFrame >= surface->numFrames ||
		newFrame < 0 || newFrame >= surface->numFrames ) {
		return qfalse;
	}

	oldVerts = (const md3XyzNormal_t *)((const byte *)surface + surface->ofsXyzNormals) + oldFrame * surface->numVerts;
	newVerts = (const md3XyzNormal_t *)((const byte *)surface + surface->ofsXyzNormals) + newFrame * surface->numVerts;

	ClearBounds( mins, maxs );
	for ( i = 0; i < surface->numVerts; i++ ) {
		point[0] = oldVerts[i].xyz[0] * MD3_XYZ_SCALE;
		point[1] = oldVerts[i].xyz[1] * MD3_XYZ_SCALE;
		point[2] = oldVerts[i].xyz[2] * MD3_XYZ_SCALE;
		AddPointToBounds( point, mins, maxs );

		point[0] = newVerts[i].xyz[0] * MD3_XYZ_SCALE;
		point[1] = newVerts[i].xyz[1] * MD3_XYZ_SCALE;
		point[2] = newVerts[i].xyz[2] * MD3_XYZ_SCALE;
		AddPointToBounds( point, mins, maxs );
	}

	return qtrue;
}

static qboolean RB_DlightShadowMDRBounds( const mdrSurface_t *surface, vec3_t mins, vec3_t maxs )
{
	const mdrHeader_t *header;
	const mdrFrame_t *oldFrameData;
	const mdrFrame_t *newFrameData;
	const trRefEntity_t *ent;
	int frameSize;
	int oldFrame, newFrame;
	int i;

	if ( !surface ) {
		return qfalse;
	}

	header = (const mdrHeader_t *)((const byte *)surface + surface->ofsHeader);
	if ( !header || header->numFrames <= 0 ) {
		return qfalse;
	}

	ent = backEnd.currentEntity;
	if ( !ent ) {
		return qfalse;
	}

	oldFrame = ent->e.oldframe;
	newFrame = ent->e.frame;
	if ( oldFrame < 0 || oldFrame >= header->numFrames ||
		newFrame < 0 || newFrame >= header->numFrames ) {
		return qfalse;
	}

	frameSize = (size_t)( &((mdrFrame_t *)0)->bones[ header->numBones ] );
	oldFrameData = (const mdrFrame_t *)((const byte *)header + header->ofsFrames + frameSize * oldFrame);
	newFrameData = (const mdrFrame_t *)((const byte *)header + header->ofsFrames + frameSize * newFrame);

	for ( i = 0; i < 3; i++ ) {
		mins[i] = MIN( oldFrameData->bounds[0][i], newFrameData->bounds[0][i] );
		maxs[i] = MAX( oldFrameData->bounds[1][i], newFrameData->bounds[1][i] );
	}

	return qtrue;
}

static qboolean RB_DlightShadowIQMBounds( const srfIQModel_t *surface, vec3_t mins, vec3_t maxs )
{
	const iqmData_t *data;
	const vec_t *oldBounds;
	const vec_t *newBounds;
	const trRefEntity_t *ent;
	int oldFrame, newFrame;
	int i;

	if ( !surface || !surface->data || !surface->data->bounds ) {
		return qfalse;
	}

	data = surface->data;
	ent = backEnd.currentEntity;
	if ( !ent ) {
		return qfalse;
	}

	oldFrame = ent->e.oldframe;
	newFrame = ent->e.frame;
	if ( oldFrame < 0 || newFrame < 0 ) {
		return qfalse;
	}
	if ( data->num_frames > 0 &&
		( oldFrame >= data->num_frames || newFrame >= data->num_frames ) ) {
		return qfalse;
	}

	oldBounds = data->bounds + 6 * oldFrame;
	newBounds = data->bounds + 6 * newFrame;
	for ( i = 0; i < 3; i++ ) {
		mins[i] = MIN( oldBounds[i], newBounds[i] );
		maxs[i] = MAX( oldBounds[i + 3], newBounds[i + 3] );
	}

	return qtrue;
}

static qboolean RB_DlightShadowSurfaceBounds( const surfaceType_t *surface, vec3_t mins, vec3_t maxs )
{
	const srfSurfaceFace_t *face;
	const srfGridMesh_t *grid;
	const srfTriangles_t *triangles;
	int i;

	if ( !surface ) {
		return qfalse;
	}

	switch ( *surface ) {
		case SF_FACE:
			face = (const srfSurfaceFace_t *)surface;
			if ( face->numPoints <= 0 ) {
				return qfalse;
			}
			ClearBounds( mins, maxs );
			for ( i = 0; i < face->numPoints; i++ ) {
				AddPointToBounds( face->points[i], mins, maxs );
			}
			return qtrue;
		case SF_GRID:
			grid = (const srfGridMesh_t *)surface;
			VectorCopy( grid->meshBounds[0], mins );
			VectorCopy( grid->meshBounds[1], maxs );
			return qtrue;
		case SF_TRIANGLES:
			triangles = (const srfTriangles_t *)surface;
			VectorCopy( triangles->bounds[0], mins );
			VectorCopy( triangles->bounds[1], maxs );
			return qtrue;
		case SF_MD3:
			return RB_DlightShadowMD3Bounds( (const md3Surface_t *)surface, mins, maxs );
		case SF_MDR:
			return RB_DlightShadowMDRBounds( (const mdrSurface_t *)surface, mins, maxs );
		case SF_IQM:
			return RB_DlightShadowIQMBounds( (const srfIQModel_t *)surface, mins, maxs );
		default:
			return qfalse;
	}
}

#define RB_DLIGHT_CASTER_BOUNDS_MEMO_SLOTS 1024

typedef struct {
	unsigned int serial;
	const surfaceType_t *surface;
	int entityNum;
	qboolean hasBounds;
	vec3_t mins;
	vec3_t maxs;
} dlightCasterBoundsMemoSlot_t;

static dlightCasterBoundsMemoSlot_t rb_dlightCasterBoundsMemo[RB_DLIGHT_CASTER_BOUNDS_MEMO_SLOTS];
static unsigned int rb_dlightCasterBoundsMemoSerial;

// caster bounds are cube-face-invariant for a point light; the lit-surface
// chain walks in the same order for every face, so slots are keyed by chain
// position and validated against the surface and entity they were derived from
static void RB_DlightShadowCasterBoundsMemoReset( void )
{
	rb_dlightCasterBoundsMemoSerial++;
	if ( rb_dlightCasterBoundsMemoSerial == 0 ) {
		Com_Memset( rb_dlightCasterBoundsMemo, 0, sizeof( rb_dlightCasterBoundsMemo ) );
		rb_dlightCasterBoundsMemoSerial = 1;
	}
}

static qboolean RB_DlightShadowSurfaceBoundsMemo( const surfaceType_t *surface, int entityNum,
	int chainIndex, vec3_t mins, vec3_t maxs )
{
	dlightCasterBoundsMemoSlot_t *slot;

	if ( chainIndex < 0 || chainIndex >= RB_DLIGHT_CASTER_BOUNDS_MEMO_SLOTS ) {
		return RB_DlightShadowSurfaceBounds( surface, mins, maxs );
	}

	slot = &rb_dlightCasterBoundsMemo[chainIndex];
	if ( slot->serial == rb_dlightCasterBoundsMemoSerial &&
		slot->surface == surface && slot->entityNum == entityNum ) {
		if ( !slot->hasBounds ) {
			return qfalse;
		}
		VectorCopy( slot->mins, mins );
		VectorCopy( slot->maxs, maxs );
		return qtrue;
	}

	slot->serial = rb_dlightCasterBoundsMemoSerial;
	slot->surface = surface;
	slot->entityNum = entityNum;
	slot->hasBounds = RB_DlightShadowSurfaceBounds( surface, mins, maxs );
	if ( slot->hasBounds ) {
		VectorCopy( mins, slot->mins );
		VectorCopy( maxs, slot->maxs );
	}

	return slot->hasBounds;
}

static qboolean RB_DlightShadowBoundsCulledForOrientation( const vec3_t mins, const vec3_t maxs,
	const orientationr_t *or )
{
	const cplane_t *plane;
	vec3_t local;
	vec3_t transformed[8];
	int i, j;
	qboolean front;

	for ( i = 0; i < 8; i++ ) {
		local[0] = (i & 1) ? maxs[0] : mins[0];
		local[1] = (i & 2) ? maxs[1] : mins[1];
		local[2] = (i & 4) ? maxs[2] : mins[2];

		VectorCopy( or->origin, transformed[i] );
		VectorMA( transformed[i], local[0], or->axis[0], transformed[i] );
		VectorMA( transformed[i], local[1], or->axis[1], transformed[i] );
		VectorMA( transformed[i], local[2], or->axis[2], transformed[i] );
	}

	for ( i = 0; i < 4; i++ ) {
		plane = &backEnd.viewParms.frustum[i];
		front = qfalse;
		for ( j = 0; j < 8; j++ ) {
			if ( DotProduct( transformed[j], plane->normal ) > plane->dist ) {
				front = qtrue;
				break;
			}
		}
		if ( !front ) {
			return qtrue;
		}
	}

	return qfalse;
}

static qboolean RB_DlightShadowSurfaceCulledForOrientation( const surfaceType_t *surface, const orientationr_t *or )
{
	vec3_t mins, maxs;

	if ( r_nocull->integer || !or || !RB_DlightShadowSurfaceBounds( surface, mins, maxs ) ) {
		return qfalse;
	}

	return RB_DlightShadowBoundsCulledForOrientation( mins, maxs, or );
}

static qboolean RB_DlightShadowSurfaceCulledForOrientationMemo( const surfaceType_t *surface,
	const orientationr_t *or, int entityNum, int chainIndex )
{
	vec3_t mins, maxs;

	if ( r_nocull->integer || !or ||
		!RB_DlightShadowSurfaceBoundsMemo( surface, entityNum, chainIndex, mins, maxs ) ) {
		return qfalse;
	}

	return RB_DlightShadowBoundsCulledForOrientation( mins, maxs, or );
}

static qboolean RB_DlightShadowSurfaceCulled( const surfaceType_t *surface )
{
	return RB_DlightShadowSurfaceCulledForOrientation( surface, &backEnd.or );
}

static qboolean RB_DlightShadowEntityCasterAllowed( int entityNum )
{
	const trRefEntity_t *ent;
	const model_t *model;

	if ( entityNum == REFENTITYNUM_WORLD ) {
		return qtrue;
	}
	if ( entityNum < 0 || entityNum >= backEnd.refdef.num_entities ) {
		return qfalse;
	}

	ent = &backEnd.refdef.entities[entityNum];
	// View weapons may receive shadowed dlights, but they must not cast into
	// the shared dlight shadow atlas used by the surrounding world.
	if ( ent->e.reType != RT_MODEL ||
		(ent->e.renderfx & ( RF_NOSHADOW | RF_FIRST_PERSON | RF_DEPTHHACK )) ) {
		return qfalse;
	}

	model = R_GetModelByHandle( ent->e.hModel );
	if ( !model ) {
		return qfalse;
	}

	switch ( model->type ) {
		case MOD_BRUSH:
		case MOD_MESH:
		case MOD_MDR:
		case MOD_IQM:
			return qtrue;
		default:
			return qfalse;
	}
}

static void RB_DlightShadowCasterEntityOrientation( int entityNum, orientationr_t *or )
{
	if ( entityNum != REFENTITYNUM_WORLD ) {
		backEnd.currentEntity = &backEnd.refdef.entities[entityNum];
		R_RotateForEntity( backEnd.currentEntity, &backEnd.viewParms, or );
	} else {
		backEnd.currentEntity = &tr.worldEntity;
		*or = backEnd.viewParms.world;
	}
}

static qboolean RB_DlightShadowFaceHasCasters( const dlight_t *dl )
{
	const litSurf_t *litSurf;
	shader_t *shader;
	int entityNum, oldEntityNum;
	int fogNum;
	int chainIndex;
	orientationr_t casterOr;

	oldEntityNum = -1;
	chainIndex = -1;
	for ( litSurf = dl->head; litSurf; litSurf = litSurf->next ) {
		chainIndex++;
		R_DecomposeLitSort( litSurf->sort, &entityNum, &shader, &fogNum );
		if ( !RB_DlightShadowEntityCasterAllowed( entityNum ) ) {
			continue;
		}
		if ( !RB_DlightShadowCasterAllowed( shader, litSurf->surface ) ) {
			continue;
		}
		if ( entityNum != oldEntityNum ) {
			RB_DlightShadowCasterEntityOrientation( entityNum, &casterOr );
			oldEntityNum = entityNum;
		}
		if ( !RB_DlightShadowSurfaceCulledForOrientationMemo( litSurf->surface, &casterOr,
			entityNum, chainIndex ) ) {
			return qtrue;
		}
	}

	return qfalse;
}

#define RB_DLIGHT_SHADOW_CACHE_SLOTS ( MAX_DLIGHTS * DLIGHT_SHADOW_FACES )

typedef struct {
	qboolean valid;
	unsigned int generation;
	unsigned int signature;
	qboolean hasCasters;
} dlightShadowCacheSlot_t;

static dlightShadowCacheSlot_t rb_dlightShadowCache[RB_DLIGHT_SHADOW_CACHE_SLOTS];

static unsigned int RB_DlightShadowCacheHashUInt( unsigned int hash, unsigned int value )
{
	hash ^= value;
	hash *= 16777619U;
	return hash;
}

static unsigned int RB_DlightShadowCacheHashFloat( unsigned int hash, float value )
{
	uint32_t bits;

	Com_Memcpy( &bits, &value, sizeof( bits ) );
	return RB_DlightShadowCacheHashUInt( hash, bits );
}

static unsigned int RB_DlightShadowCacheHashPtr( unsigned int hash, const void *ptr )
{
	uint64_t bits;

	bits = (uint64_t)(intptr_t)ptr;
	hash = RB_DlightShadowCacheHashUInt( hash, (unsigned int)bits );
	hash = RB_DlightShadowCacheHashUInt( hash, (unsigned int)( bits >> 32 ) );
	return hash;
}

static qboolean RB_DlightShadowCacheSignature( const dlight_t *dl,
	const shadowPointLightPlan_t *plan, unsigned int *signature )
{
	const litSurf_t *litSurf;
	shader_t *shader;
	int entityNum;
	int fogNum;
	int i;
	int atlasBaseFace;
	int atlasFaceSize;
	int receiverCount;
	unsigned int hash;

	atlasBaseFace = plan ? plan->atlasBaseFace : ( dl ? dl->shadowAtlasBaseFace : -1 );
	atlasFaceSize = plan ? plan->atlasFaceSize : ( dl ? dl->shadowAtlasFaceSize : 0 );
	receiverCount = plan ? plan->receiverCount : ( dl ? dl->shadowReceiverCount : 0 );
	if ( !dl || !signature || dl->linear || atlasFaceSize <= 0 || atlasBaseFace < 0 ) {
		return qfalse;
	}

	hash = 2166136261U;
	for ( i = 0; i < 3; i++ ) {
		hash = RB_DlightShadowCacheHashFloat( hash, dl->origin[i] );
		hash = RB_DlightShadowCacheHashFloat( hash, dl->color[i] );
	}
	hash = RB_DlightShadowCacheHashFloat( hash, dl->radius );
	hash = RB_DlightShadowCacheHashUInt( hash, (unsigned int)dl->additive );
	hash = RB_DlightShadowCacheHashUInt( hash, (unsigned int)receiverCount );
	hash = RB_DlightShadowCacheHashUInt( hash, (unsigned int)atlasFaceSize );

	for ( litSurf = dl->head; litSurf; litSurf = litSurf->next ) {
		R_DecomposeLitSort( litSurf->sort, &entityNum, &shader, &fogNum );
		if ( entityNum != REFENTITYNUM_WORLD ) {
			return qfalse;
		}

		hash = RB_DlightShadowCacheHashUInt( hash, litSurf->sort );
		hash = RB_DlightShadowCacheHashPtr( hash, litSurf->surface );

		if ( !RB_DlightShadowCasterAllowed( shader, litSurf->surface ) ) {
			continue;
		}
		if ( shader->numDeforms > 0 ) {
			return qfalse;
		}
	}

	*signature = hash;
	return qtrue;
}

static int RB_DlightShadowCacheSlot( const dlight_t *dl, const shadowPointLightPlan_t *plan,
	int face )
{
	int slot;
	int atlasBaseFace;

	if ( face < 0 || face >= DLIGHT_SHADOW_FACES ) {
		return -1;
	}

	atlasBaseFace = plan ? plan->atlasBaseFace : ( dl ? dl->shadowAtlasBaseFace : -1 );
	slot = atlasBaseFace + face;
	if ( slot < 0 || slot >= RB_DLIGHT_SHADOW_CACHE_SLOTS ) {
		return -1;
	}

	return slot;
}

static qboolean RB_DlightShadowCacheLookup( const dlight_t *dl,
	const shadowPointLightPlan_t *plan, int face, unsigned int signature,
	unsigned int generation, qboolean *hasCasters )
{
	int slot;
	const dlightShadowCacheSlot_t *entry;

	slot = RB_DlightShadowCacheSlot( dl, plan, face );
	if ( slot < 0 ) {
		return qfalse;
	}

	entry = &rb_dlightShadowCache[slot];
	if ( !entry->valid || entry->generation != generation || entry->signature != signature ) {
		return qfalse;
	}

	*hasCasters = entry->hasCasters;
	return qtrue;
}

static void RB_DlightShadowCacheStore( const dlight_t *dl, const shadowPointLightPlan_t *plan,
	int face, unsigned int signature, unsigned int generation, qboolean hasCasters )
{
	int slot;
	dlightShadowCacheSlot_t *entry;

	slot = RB_DlightShadowCacheSlot( dl, plan, face );
	if ( slot < 0 ) {
		return;
	}

	entry = &rb_dlightShadowCache[slot];
	entry->valid = qtrue;
	entry->generation = generation;
	entry->signature = signature;
	entry->hasCasters = hasCasters;
}

static void RB_DlightShadowCacheInvalidate( const dlight_t *dl,
	const shadowPointLightPlan_t *plan, int face )
{
	int slot;

	slot = RB_DlightShadowCacheSlot( dl, plan, face );
	if ( slot >= 0 ) {
		rb_dlightShadowCache[slot].valid = qfalse;
	}
}

typedef struct {
	qboolean valid;
	unsigned int generation;
	unsigned int signature;
} csmShadowCache_t;

static csmShadowCache_t rb_csmShadowCache;

static qboolean RB_CSMShadowEntityAllowed( int entityNum )
{
	const trRefEntity_t *ent;
	const model_t *model;

	if ( entityNum == REFENTITYNUM_WORLD ) {
		return qtrue;
	}
	if ( entityNum < 0 || entityNum >= backEnd.refdef.num_entities ) {
		return qfalse;
	}

	ent = &backEnd.refdef.entities[entityNum];
	if ( ent->e.reType != RT_MODEL ||
		(ent->e.renderfx & ( RF_NOSHADOW | RF_DEPTHHACK )) ) {
		return qfalse;
	}

	model = R_GetModelByHandle( ent->e.hModel );
	if ( !model ) {
		return qfalse;
	}

	switch ( model->type ) {
		case MOD_BRUSH:
		case MOD_MESH:
		case MOD_MDR:
		case MOD_IQM:
			return qtrue;
		default:
			return qfalse;
	}
}

static qboolean RB_CSMShadowSurfaceAllowed( const shader_t *shader, const surfaceType_t *surface )
{
	if ( !shader || !surface ) {
		return qfalse;
	}
	if ( shader->sort != SS_OPAQUE || shader->isSky || shader->polygonOffset ||
		(shader->surfaceFlags & ( SURF_SKY | SURF_NODLIGHT )) ) {
		return qfalse;
	}
	if ( RB_ShadowCorrectnessRejectsAlphaTest( shader ) ) {
		return qfalse;
	}

	switch ( *surface ) {
		case SF_FACE:
		case SF_GRID:
		case SF_TRIANGLES:
		case SF_MD3:
		case SF_MDR:
		case SF_IQM:
			return qtrue;
		default:
			return qfalse;
	}
}

static qboolean RB_CSMShadowSurfaceCulledForCascade( const surfaceType_t *surface,
	const orientationr_t *or, const csmCascadePlan_t *cascade )
{
	vec3_t mins, maxs;
	vec3_t local;
	vec3_t transformed[8];
	float cullMargin;
	int axis, side;
	int i;

	if ( r_nocull->integer || !or || !cascade ||
		!RB_DlightShadowSurfaceBounds( surface, mins, maxs ) ) {
		return qfalse;
	}

	for ( i = 0; i < 8; i++ ) {
		local[0] = ( i & 1 ) ? maxs[0] : mins[0];
		local[1] = ( i & 2 ) ? maxs[1] : mins[1];
		local[2] = ( i & 4 ) ? maxs[2] : mins[2];

		VectorCopy( or->origin, transformed[i] );
		VectorMA( transformed[i], local[0], or->axis[0], transformed[i] );
		VectorMA( transformed[i], local[1], or->axis[1], transformed[i] );
		VectorMA( transformed[i], local[2], or->axis[2], transformed[i] );
	}

	cullMargin = cascade->texelSize * 2.0f;
	for ( axis = 0; axis < 3; axis++ ) {
		for ( side = 0; side < 2; side++ ) {
			qboolean outside = qtrue;
			float bound = cascade->bounds[side][axis] + ( side ? cullMargin : -cullMargin );

			for ( i = 0; i < 8; i++ ) {
				float d = DotProduct( transformed[i], cascade->axis[axis] );
				if ( side == 0 ) {
					if ( d >= bound ) {
						outside = qfalse;
						break;
					}
				} else if ( d <= bound ) {
					outside = qfalse;
					break;
				}
			}
			if ( outside ) {
				return qtrue;
			}
		}
	}

	return qfalse;
}

static qboolean RB_CSMShadowDrawSurfAllowed( const drawSurf_t *drawSurf, int *entityNum )
{
	shader_t *shader;
	int fogNum;
	int dlighted;

	if ( !drawSurf || !drawSurf->surface ) {
		return qfalse;
	}

	R_DecomposeSort( drawSurf->sort, entityNum, &shader, &fogNum, &dlighted );
	return ( RB_CSMShadowEntityAllowed( *entityNum ) &&
		RB_CSMShadowSurfaceAllowed( shader, drawSurf->surface ) ) ? qtrue : qfalse;
}

static int RB_CSMShadowCountDrawSurfs( drawSurf_t *drawSurfs, int numDrawSurfs )
{
	int count = 0;
	int entityNum;
	int i;

	for ( i = 0; i < numDrawSurfs; i++ ) {
		if ( RB_CSMShadowDrawSurfAllowed( &drawSurfs[i], &entityNum ) ) {
			count++;
		}
	}
	return count;
}

static float RB_CSMShadowCacheQuantizeDepth( float value, float texelSize )
{
	if ( texelSize <= 0.0f ) {
		return value;
	}

	return floorf( value / texelSize );
}

static qboolean RB_CSMShadowCacheSignature( drawSurf_t *drawSurfs, int numDrawSurfs,
	unsigned int *signature )
{
	unsigned int hash;
	int i, j;

	if ( !signature || !tr.csm.enabled ) {
		return qfalse;
	}

	hash = 2166136261U;
	hash = RB_DlightShadowCacheHashUInt( hash, (unsigned int)tr.csm.cascadeCount );
	hash = RB_DlightShadowCacheHashUInt( hash, (unsigned int)tr.csm.resolution );
	for ( i = 0; i < tr.csm.cascadeCount; i++ ) {
		const csmCascadePlan_t *cascade = &tr.csm.cascades[i];

		/* R_CSMPlanCascade leaves the light-depth bounds unsnapped, so they
		   drift with every camera move; hash them at texel granularity so
		   sub-texel drift still reuses the cached atlas. The doubled depth
		   extent keeps that <1 texel of staleness harmless to receivers. */
		hash = RB_DlightShadowCacheHashFloat( hash,
			RB_CSMShadowCacheQuantizeDepth( cascade->bounds[0][0], cascade->texelSize ) );
		hash = RB_DlightShadowCacheHashFloat( hash,
			RB_CSMShadowCacheQuantizeDepth( cascade->bounds[1][0], cascade->texelSize ) );

		for ( j = 1; j < 3; j++ ) {
			hash = RB_DlightShadowCacheHashFloat( hash, cascade->bounds[0][j] );
			hash = RB_DlightShadowCacheHashFloat( hash, cascade->bounds[1][j] );
		}
		for ( j = 0; j < 3; j++ ) {
			hash = RB_DlightShadowCacheHashFloat( hash, cascade->axis[0][j] );
			hash = RB_DlightShadowCacheHashFloat( hash, cascade->axis[1][j] );
			hash = RB_DlightShadowCacheHashFloat( hash, cascade->axis[2][j] );
		}
	}

	for ( i = 0; i < numDrawSurfs; i++ ) {
		const drawSurf_t *drawSurf = &drawSurfs[i];
		const trRefEntity_t *ent;
		shader_t *shader;
		int entityNum;
		int fogNum;
		int dlighted;
		int axis;

		R_DecomposeSort( drawSurf->sort, &entityNum, &shader, &fogNum, &dlighted );
		if ( !RB_CSMShadowEntityAllowed( entityNum ) ||
			!RB_CSMShadowSurfaceAllowed( shader, drawSurf->surface ) ) {
			continue;
		}
		if ( shader->numDeforms > 0 ) {
			return qfalse;
		}

		hash = RB_DlightShadowCacheHashUInt( hash, drawSurf->sort );
		hash = RB_DlightShadowCacheHashPtr( hash, drawSurf->surface );
		hash = RB_DlightShadowCacheHashUInt( hash, (unsigned int)entityNum );
		if ( entityNum != REFENTITYNUM_WORLD ) {
			ent = &backEnd.refdef.entities[entityNum];
			hash = RB_DlightShadowCacheHashUInt( hash, (unsigned int)ent->e.hModel );
			hash = RB_DlightShadowCacheHashUInt( hash, (unsigned int)ent->e.frame );
			hash = RB_DlightShadowCacheHashUInt( hash, (unsigned int)ent->e.oldframe );
			hash = RB_DlightShadowCacheHashFloat( hash, ent->e.backlerp );
			for ( axis = 0; axis < 3; axis++ ) {
				hash = RB_DlightShadowCacheHashFloat( hash, ent->e.origin[axis] );
				hash = RB_DlightShadowCacheHashFloat( hash, ent->e.axis[0][axis] );
				hash = RB_DlightShadowCacheHashFloat( hash, ent->e.axis[1][axis] );
				hash = RB_DlightShadowCacheHashFloat( hash, ent->e.axis[2][axis] );
			}
		}
	}

	*signature = hash;
	return qtrue;
}

static void RB_SetCSMShadowProjection( const csmCascadePlan_t *cascade, viewParms_t *shadowParms )
{
	float extentX = MAX( cascade->bounds[1][0] - cascade->bounds[0][0], 1.0f );
	float extentY = MAX( cascade->bounds[1][1] - cascade->bounds[0][1], 1.0f );
	float extentZ = MAX( cascade->bounds[1][2] - cascade->bounds[0][2], 1.0f );

	Com_Memset( shadowParms->projectionMatrix, 0, sizeof( shadowParms->projectionMatrix ) );
	shadowParms->projectionMatrix[0] = 2.0f / extentY;
	shadowParms->projectionMatrix[5] = 2.0f / extentZ;
	shadowParms->projectionMatrix[10] = -1.0f / extentX;
	shadowParms->projectionMatrix[14] = -cascade->bounds[0][0] / extentX;
	shadowParms->projectionMatrix[12] = -( cascade->bounds[1][1] + cascade->bounds[0][1] ) / extentY;
	shadowParms->projectionMatrix[13] = -( cascade->bounds[1][2] + cascade->bounds[0][2] ) / extentZ;
	shadowParms->projectionMatrix[15] = 1.0f;
}

static void RB_BuildCSMShadowView( int cascadeIndex, viewParms_t *shadowParms )
{
	const csmCascadePlan_t *cascade = &tr.csm.cascades[cascadeIndex];
	int cascadeSize = vk_csm_shadow_cascade_size();

	Com_Memset( shadowParms, 0, sizeof( *shadowParms ) );
	shadowParms->viewportX = cascadeIndex * cascadeSize;
	shadowParms->viewportY = 0;
	shadowParms->viewportWidth = cascadeSize;
	shadowParms->viewportHeight = cascadeSize;
	shadowParms->scissorX = shadowParms->viewportX;
	shadowParms->scissorY = shadowParms->viewportY;
	shadowParms->scissorWidth = shadowParms->viewportWidth;
	shadowParms->scissorHeight = shadowParms->viewportHeight;
	shadowParms->zFar = cascade->bounds[1][0] - cascade->bounds[0][0];
	shadowParms->stereoFrame = STEREO_CENTER;
	shadowParms->portalView = PV_NONE;
	shadowParms->passFlags = VPF_DLIGHT_SHADOW;

	Com_Memset( &shadowParms->world, 0, sizeof( shadowParms->world ) );
	shadowParms->world.axis[0][0] = 1.0f;
	shadowParms->world.axis[1][1] = 1.0f;
	shadowParms->world.axis[2][2] = 1.0f;

	shadowParms->world.modelMatrix[0] = cascade->axis[1][0];
	shadowParms->world.modelMatrix[4] = cascade->axis[1][1];
	shadowParms->world.modelMatrix[8] = cascade->axis[1][2];
	shadowParms->world.modelMatrix[12] = 0.0f;
	shadowParms->world.modelMatrix[1] = cascade->axis[2][0];
	shadowParms->world.modelMatrix[5] = cascade->axis[2][1];
	shadowParms->world.modelMatrix[9] = cascade->axis[2][2];
	shadowParms->world.modelMatrix[13] = 0.0f;
	shadowParms->world.modelMatrix[2] = -cascade->axis[0][0];
	shadowParms->world.modelMatrix[6] = -cascade->axis[0][1];
	shadowParms->world.modelMatrix[10] = -cascade->axis[0][2];
	shadowParms->world.modelMatrix[14] = 0.0f;
	shadowParms->world.modelMatrix[3] = 0.0f;
	shadowParms->world.modelMatrix[7] = 0.0f;
	shadowParms->world.modelMatrix[11] = 0.0f;
	shadowParms->world.modelMatrix[15] = 1.0f;

	RB_SetCSMShadowProjection( cascade, shadowParms );
}

static void RB_SetCSMShadowView( const viewParms_t *shadowParms )
{
	backEnd.viewParms = *shadowParms;
	backEnd.or = shadowParms->world;
	backEnd.currentEntity = &tr.worldEntity;

	SetViewportAndScissor();
	Com_Memcpy( vk_world.modelview_transform, backEnd.or.modelMatrix, 64 );
	tess.depthRange = DEPTH_RANGE_NORMAL;
	vk_update_mvp( NULL );
}

static void RB_SetCSMShadowEntity( int entityNum, const viewParms_t *viewParms, double originalTime )
{
	if ( entityNum == REFENTITYNUM_WORLD ) {
		backEnd.currentEntity = &tr.worldEntity;
		backEnd.refdef.floatTime = originalTime;
		backEnd.or = viewParms->world;
		Com_Memcpy( vk_world.modelview_transform, backEnd.or.modelMatrix, 64 );
		tess.depthRange = DEPTH_RANGE_NORMAL;
		vk_update_mvp( NULL );
		return;
	}

	backEnd.currentEntity = &backEnd.refdef.entities[entityNum];

	if ( backEnd.currentEntity->intShaderTime ) {
		backEnd.refdef.floatTime = originalTime - (double)(backEnd.currentEntity->e.shaderTime.i) * 0.001;
	} else {
		backEnd.refdef.floatTime = originalTime - (double)backEnd.currentEntity->e.shaderTime.f;
	}

	R_RotateForEntity( backEnd.currentEntity, viewParms, &backEnd.or );
	Com_Memcpy( vk_world.modelview_transform, backEnd.or.modelMatrix, 64 );
	tess.depthRange = DEPTH_RANGE_NORMAL;
	vk_update_mvp( NULL );
}

static int RB_RenderCSMShadowCascade( drawSurf_t *drawSurfs, int numDrawSurfs,
	const csmCascadePlan_t *cascade, double originalTime )
{
	orientationr_t casterOr;
	int currentEntityNum;
	int surfaces = 0;
	int i;
	qboolean surfaceActive = qfalse;

	currentEntityNum = -1;
	for ( i = 0; i < numDrawSurfs; i++ ) {
		drawSurf_t *drawSurf = &drawSurfs[i];
		shader_t *shader;
		int entityNum;
		int fogNum;
		int dlighted;

		R_DecomposeSort( drawSurf->sort, &entityNum, &shader, &fogNum, &dlighted );
		if ( !RB_CSMShadowEntityAllowed( entityNum ) ||
			!RB_CSMShadowSurfaceAllowed( shader, drawSurf->surface ) ) {
			continue;
		}

		if ( entityNum != currentEntityNum ) {
			if ( surfaceActive ) {
				RB_EndSurface();
				tess.csmCasterPass = qfalse;
				surfaceActive = qfalse;
			}

			RB_SetCSMShadowEntity( entityNum, &backEnd.viewParms, originalTime );
			casterOr = backEnd.or;
			currentEntityNum = entityNum;
		}

		if ( RB_CSMShadowSurfaceCulledForCascade( drawSurf->surface, &casterOr, cascade ) ) {
			continue;
		}

		if ( !surfaceActive ) {
			RB_BeginSurface( tr.defaultShader, 0 );
			tess.allowVBO = qfalse;
			tess.csmCasterPass = qtrue;
			surfaceActive = qtrue;
		}

		rb_surfaceTable[ *drawSurf->surface ]( drawSurf->surface );
		surfaces++;
	}

	if ( surfaceActive ) {
		RB_EndSurface();
		tess.csmCasterPass = qfalse;
	}

	backEnd.pc.c_csmShadowAtlasSurfaces += surfaces;
	return surfaces;
}

static void RB_RenderCSMShadowAtlas( drawSurf_t *drawSurfs, int numDrawSurfs )
{
	trRefdef_t savedRefdef;
	viewParms_t savedViewParms;
	orientationr_t savedOr;
	const trRefEntity_t *savedEntity;
	qboolean savedProjection2D;
#ifdef USE_VBO
	qboolean savedAllowVBO;
#endif
	unsigned int signature = 0;
	unsigned int generation;
	qboolean cacheable;
	int startMsec;
	int cascadeIndex;
	int surfaces;
	double originalTime;

	if ( ( tr.shadowManager.planned &&
			!R_ShadowManagerPassScheduled( &tr.shadowManager,
				SHADOW_MANAGER_PASS_CSM_ATLAS ) ) ||
		!tr.csm.enabled || !vk_csm_shadow_atlas_available() ||
		RB_CSMShadowCountDrawSurfs( drawSurfs, numDrawSurfs ) <= 0 ) {
		return;
	}

	generation = vk_csm_shadow_atlas_generation();
	cacheable = RB_CSMShadowCacheSignature( drawSurfs, numDrawSurfs, &signature );
	if ( cacheable && rb_csmShadowCache.valid &&
		rb_csmShadowCache.generation == generation &&
		rb_csmShadowCache.signature == signature ) {
		backEnd.pc.c_csmShadowAtlasCacheHits++;
		vk_mark_csm_shadow_atlas_rendered();
		R_ShadowManagerPublishCSMAtlas( &tr.shadowManager,
			vk_csm_shadow_atlas_ready(), generation );
		return;
	}
	if ( cacheable ) {
		backEnd.pc.c_csmShadowAtlasCacheMisses++;
	} else {
		backEnd.pc.c_csmShadowAtlasCacheUncacheable++;
	}

	RB_EndSurface();
	if ( !vk_begin_csm_shadow_render_pass() ) {
		return;
	}

	startMsec = ri.Milliseconds();
	savedRefdef = backEnd.refdef;
	savedViewParms = backEnd.viewParms;
	savedOr = backEnd.or;
	savedEntity = backEnd.currentEntity;
	savedProjection2D = backEnd.projection2D;
#ifdef USE_VBO
	savedAllowVBO = tess.allowVBO;
	tess.allowVBO = qfalse;
#endif
	backEnd.projection2D = qtrue;
	vk_clear_depth_force( qfalse );
	backEnd.projection2D = qfalse;
	originalTime = backEnd.refdef.floatTime;
	surfaces = 0;

	for ( cascadeIndex = 0; cascadeIndex < tr.csm.cascadeCount; cascadeIndex++ ) {
		viewParms_t shadowParms;

		RB_BuildCSMShadowView( cascadeIndex, &shadowParms );
		RB_SetCSMShadowView( &shadowParms );
		surfaces += RB_RenderCSMShadowCascade( drawSurfs, numDrawSurfs,
			&tr.csm.cascades[cascadeIndex], originalTime );
	}

	backEnd.refdef = savedRefdef;
	backEnd.viewParms = savedViewParms;
	backEnd.or = savedOr;
	backEnd.currentEntity = savedEntity;
	backEnd.projection2D = savedProjection2D;
#ifdef USE_VBO
	tess.allowVBO = savedAllowVBO;
#endif
	Com_Memcpy( vk_world.modelview_transform, backEnd.or.modelMatrix, 64 );
	tess.depthRange = DEPTH_RANGE_NORMAL;
	vk_update_mvp( NULL );
	vk_end_csm_shadow_render_pass();
	backEnd.pc.c_csmShadowAtlasMsec += ri.Milliseconds() - startMsec;
	R_ShadowManagerPublishCSMAtlas( &tr.shadowManager,
		vk_csm_shadow_atlas_ready(), generation );

	if ( cacheable && surfaces > 0 ) {
		rb_csmShadowCache.valid = qtrue;
		rb_csmShadowCache.generation = generation;
		rb_csmShadowCache.signature = signature;
	} else {
		rb_csmShadowCache.valid = qfalse;
	}
}

static void RB_CSMShadowReceiverPass( drawSurf_t *drawSurfs, int numDrawSurfs )
{
	const csmCascadePlan_t *cascade;
	viewParms_t savedViewParms;
	orientationr_t savedOr;
	const trRefEntity_t *savedEntity;
	double originalTime;
	int cascadeIndex;

	if ( ( tr.shadowManager.planned &&
			( !R_ShadowManagerPassScheduled( &tr.shadowManager,
					SHADOW_MANAGER_PASS_CSM_RECEIVER ) ||
				!tr.shadowManager.csmAtlasPublication.published ) ) ||
		!tr.csm.enabled ||
		( !tr.shadowManager.planned && !vk_csm_shadow_atlas_ready() ) ||
		RB_CSMShadowCountDrawSurfs( drawSurfs, numDrawSurfs ) <= 0 ) {
		return;
	}

	savedViewParms = backEnd.viewParms;
	savedOr = backEnd.or;
	savedEntity = backEnd.currentEntity;
	originalTime = backEnd.refdef.floatTime;

	for ( cascadeIndex = 0; cascadeIndex < tr.csm.cascadeCount; cascadeIndex++ ) {
		int currentEntityNum;
		int entitySurfaces = 0;
		int worldSurfaces = 0;
		int i;
		qboolean surfaceActive = qfalse;

		cascade = &tr.csm.cascades[cascadeIndex];
		currentEntityNum = -1;
		for ( i = 0; i < numDrawSurfs; i++ ) {
			drawSurf_t *drawSurf = &drawSurfs[i];
			shader_t *shader;
			int entityNum;
			int fogNum;
			int dlighted;

			if ( drawSurf->flags & DSF_SHADOW_CASTER_ONLY ) {
				continue;
			}
			R_DecomposeSort( drawSurf->sort, &entityNum, &shader, &fogNum, &dlighted );
			if ( !RB_CSMShadowEntityAllowed( entityNum ) ||
				!RB_CSMShadowSurfaceAllowed( shader, drawSurf->surface ) ) {
				continue;
			}

			if ( entityNum != currentEntityNum ) {
				if ( surfaceActive ) {
					RB_EndSurface();
					tess.csmShadowPass = qfalse;
					surfaceActive = qfalse;
				}

				RB_SetCSMShadowEntity( entityNum, &savedViewParms, originalTime );
				currentEntityNum = entityNum;
			}

			if ( entityNum == REFENTITYNUM_WORLD &&
				RB_CSMShadowSurfaceCulledForCascade( drawSurf->surface, &backEnd.or, cascade ) ) {
				continue;
			}

			if ( !surfaceActive ) {
				RB_BeginSurface( tr.whiteShader, 0 );
				tess.allowVBO = qfalse;
				tess.csmShadowPass = qtrue;
				tess.csmCascade = cascadeIndex;
				surfaceActive = qtrue;
			}

			rb_surfaceTable[ *drawSurf->surface ]( drawSurf->surface );
			if ( entityNum == REFENTITYNUM_WORLD ) {
				worldSurfaces++;
			} else {
				entitySurfaces++;
			}
		}

		if ( surfaceActive ) {
			RB_EndSurface();
			tess.csmShadowPass = qfalse;
		}

		backEnd.pc.c_csmShadowReceiverWorldSurfaces += worldSurfaces;
		backEnd.pc.c_csmShadowReceiverEntitySurfaces += entitySurfaces;
	}

	backEnd.viewParms = savedViewParms;
	backEnd.or = savedOr;
	backEnd.currentEntity = savedEntity;
	backEnd.refdef.floatTime = originalTime;
	Com_Memcpy( vk_world.modelview_transform, backEnd.or.modelMatrix, 64 );
	tess.depthRange = DEPTH_RANGE_NORMAL;
	vk_update_mvp( NULL );
}

static void RB_SetDlightShadowCasterEntity( int entityNum, double originalTime )
{
	if ( entityNum != REFENTITYNUM_WORLD ) {
		backEnd.currentEntity = &backEnd.refdef.entities[entityNum];

		if ( backEnd.currentEntity->intShaderTime ) {
			backEnd.refdef.floatTime = originalTime - (double)(backEnd.currentEntity->e.shaderTime.i) * 0.001;
		} else {
			backEnd.refdef.floatTime = originalTime - (double)backEnd.currentEntity->e.shaderTime.f;
		}

		R_RotateForEntity( backEnd.currentEntity, &backEnd.viewParms, &backEnd.or );
	} else {
		backEnd.currentEntity = &tr.worldEntity;
		backEnd.refdef.floatTime = originalTime;
		backEnd.or = backEnd.viewParms.world;
	}

	Com_Memcpy( vk_world.modelview_transform, backEnd.or.modelMatrix, 64 );
	tess.depthRange = DEPTH_RANGE_NORMAL;
	vk_update_mvp( NULL );
}

static int RB_CollectDlightShadowCasterEntities( const dlight_t *dl, int *entityNums, int maxEntityNums )
{
	const litSurf_t *litSurf;
	shader_t *shader;
	byte entityQueued[MAX_REFENTITIES + 1];
	int entityNum;
	int fogNum;
	int entityCount;

	Com_Memset( entityQueued, 0, sizeof( entityQueued ) );
	entityCount = 0;

	for ( litSurf = dl->head; litSurf; litSurf = litSurf->next ) {
		R_DecomposeLitSort( litSurf->sort, &entityNum, &shader, &fogNum );
		if ( !RB_DlightShadowEntityCasterAllowed( entityNum ) ) {
			continue;
		}
		if ( !RB_DlightShadowCasterAllowed( shader, litSurf->surface ) ) {
			continue;
		}
		if ( entityQueued[entityNum] ) {
			continue;
		}
		if ( entityCount >= maxEntityNums ) {
			break;
		}

		entityQueued[entityNum] = qtrue;
		entityNums[entityCount++] = entityNum;
	}

	return entityCount;
}

static int RB_RenderDlightShadowEntityCasters( const dlight_t *dl, int targetEntityNum, double originalTime )
{
	const litSurf_t *litSurf;
	shader_t *shader;
	int entityNum;
	int fogNum;
	int chainIndex;
	int surfaces;
	qboolean surfaceActive;

	surfaces = 0;
	surfaceActive = qfalse;
	chainIndex = -1;
	RB_SetDlightShadowCasterEntity( targetEntityNum, originalTime );

	for ( litSurf = dl->head; litSurf; litSurf = litSurf->next ) {
		chainIndex++;
		R_DecomposeLitSort( litSurf->sort, &entityNum, &shader, &fogNum );
		if ( entityNum != targetEntityNum ) {
			continue;
		}
		if ( !RB_DlightShadowCasterAllowed( shader, litSurf->surface ) ) {
			continue;
		}
		if ( RB_DlightShadowSurfaceCulledForOrientationMemo( litSurf->surface, &backEnd.or,
			entityNum, chainIndex ) ) {
			continue;
		}

		if ( !surfaceActive ) {
			RB_BeginSurface( tr.defaultShader, 0 );
			tess.allowVBO = qfalse;
			surfaceActive = qtrue;
		}

		rb_surfaceTable[ *litSurf->surface ]( litSurf->surface );
		surfaces++;
	}

	if ( surfaceActive ) {
		RB_EndSurface();
		backEnd.pc.c_dlightShadowAtlasBatches++;
		backEnd.pc.c_dlightShadowAtlasDraws++;
	}

	return surfaces;
}

static int RB_RenderDlightShadowCasters( const dlight_t *dl )
{
	int entityNums[MAX_REFENTITIES + 1];
	int entityCount;
	int entityIndex;
	int surfaces;
	double originalTime;

	originalTime = backEnd.refdef.floatTime;
	surfaces = 0;
	entityCount = RB_CollectDlightShadowCasterEntities( dl, entityNums, ARRAY_LEN( entityNums ) );

	for ( entityIndex = 0; entityIndex < entityCount; entityIndex++ ) {
		surfaces += RB_RenderDlightShadowEntityCasters( dl, entityNums[entityIndex], originalTime );
	}

	RB_SetDlightShadowCasterEntity( REFENTITYNUM_WORLD, originalTime );
	return surfaces;
}

static float RB_SpotShadowFov( const shadowSpotLightPlan_t *plan )
{
	float outerAngle;

	outerAngle = plan ? plan->outerAngle : 0.0f;
	if ( outerAngle <= 0.0f ) {
		outerAngle = 75.0f;
	}
	return Com_Clamp( 5.0f, 170.0f, outerAngle * 2.0f );
}

static qboolean RB_BuildSpotShadowView( const shadowSpotLightPlan_t *plan,
	int atlasHeight, viewParms_t *shadowParms )
{
	float viewerMatrix[16];
	float zNear;
	float zFar;
	float fov;

	if ( !plan || !shadowParms || !plan->atlasAllocated ||
		plan->atlasTileSize <= 0 || atlasHeight <= 0 || plan->radius <= 0.0f ) {
		return qfalse;
	}

	Com_Memset( shadowParms, 0, sizeof( *shadowParms ) );
	shadowParms->viewportX = plan->atlasX;
	shadowParms->viewportY = atlasHeight - plan->atlasY - plan->atlasTileSize;
	shadowParms->viewportWidth = plan->atlasTileSize;
	shadowParms->viewportHeight = plan->atlasTileSize;
	shadowParms->scissorX = shadowParms->viewportX;
	shadowParms->scissorY = shadowParms->viewportY;
	shadowParms->scissorWidth = shadowParms->viewportWidth;
	shadowParms->scissorHeight = shadowParms->viewportHeight;
	fov = RB_SpotShadowFov( plan );
	shadowParms->fovX = fov;
	shadowParms->fovY = fov;
	shadowParms->zFar = MAX( plan->radius, 64.0f );
	shadowParms->stereoFrame = STEREO_CENTER;
	shadowParms->portalView = PV_NONE;
	shadowParms->passFlags = VPF_DLIGHT_SHADOW;
	VectorCopy( plan->origin, shadowParms->or.origin );
	VectorCopy( plan->origin, shadowParms->pvsOrigin );

	if ( VectorNormalize2( plan->direction, shadowParms->or.axis[0] ) <= 0.0f ) {
		return qfalse;
	}
	MakeNormalVectors( shadowParms->or.axis[0], shadowParms->or.axis[1], shadowParms->or.axis[2] );

	Com_Memset( &shadowParms->world, 0, sizeof( shadowParms->world ) );
	shadowParms->world.axis[0][0] = 1.0f;
	shadowParms->world.axis[1][1] = 1.0f;
	shadowParms->world.axis[2][2] = 1.0f;
	VectorCopy( shadowParms->or.origin, shadowParms->world.viewOrigin );

	viewerMatrix[0] = shadowParms->or.axis[0][0];
	viewerMatrix[4] = shadowParms->or.axis[0][1];
	viewerMatrix[8] = shadowParms->or.axis[0][2];
	viewerMatrix[12] = -plan->origin[0] * viewerMatrix[0] + -plan->origin[1] * viewerMatrix[4] + -plan->origin[2] * viewerMatrix[8];
	viewerMatrix[1] = shadowParms->or.axis[1][0];
	viewerMatrix[5] = shadowParms->or.axis[1][1];
	viewerMatrix[9] = shadowParms->or.axis[1][2];
	viewerMatrix[13] = -plan->origin[0] * viewerMatrix[1] + -plan->origin[1] * viewerMatrix[5] + -plan->origin[2] * viewerMatrix[9];
	viewerMatrix[2] = shadowParms->or.axis[2][0];
	viewerMatrix[6] = shadowParms->or.axis[2][1];
	viewerMatrix[10] = shadowParms->or.axis[2][2];
	viewerMatrix[14] = -plan->origin[0] * viewerMatrix[2] + -plan->origin[1] * viewerMatrix[6] + -plan->origin[2] * viewerMatrix[10];
	viewerMatrix[3] = 0.0f;
	viewerMatrix[7] = 0.0f;
	viewerMatrix[11] = 0.0f;
	viewerMatrix[15] = 1.0f;

	RB_DlightShadowMultMatrix( viewerMatrix, rb_dlightShadowFlipMatrix, shadowParms->world.modelMatrix );

	zNear = 1.0f;
	zFar = MAX( shadowParms->zFar, zNear + 1.0f );
	R_SetupProjection( shadowParms, zNear, qtrue );
	RB_SetDlightShadowProjectionZ( shadowParms, zNear, zFar );
	return qtrue;
}

static int RB_RenderSpotShadowCasters( drawSurf_t *drawSurfs, int numDrawSurfs,
	double originalTime )
{
	int currentEntityNum;
	int surfaces;
	qboolean surfaceActive;
	int i;

	currentEntityNum = -1;
	surfaces = 0;
	surfaceActive = qfalse;

	for ( i = 0; i < numDrawSurfs; i++ ) {
		drawSurf_t *drawSurf = &drawSurfs[i];
		shader_t *shader;
		int entityNum;
		int fogNum;
		int dlighted;

		R_DecomposeSort( drawSurf->sort, &entityNum, &shader, &fogNum, &dlighted );
		if ( !RB_DlightShadowEntityCasterAllowed( entityNum ) ||
			!RB_DlightShadowCasterAllowed( shader, drawSurf->surface ) ) {
			continue;
		}

		if ( entityNum != currentEntityNum ) {
			if ( surfaceActive ) {
				RB_EndSurface();
				surfaceActive = qfalse;
			}
			RB_SetDlightShadowCasterEntity( entityNum, originalTime );
			currentEntityNum = entityNum;
		}

		if ( RB_DlightShadowSurfaceCulled( drawSurf->surface ) ) {
			continue;
		}

		if ( !surfaceActive ) {
			RB_BeginSurface( tr.defaultShader, 0 );
			tess.allowVBO = qfalse;
			surfaceActive = qtrue;
		}

		rb_surfaceTable[ *drawSurf->surface ]( drawSurf->surface );
		surfaces++;
	}

	if ( surfaceActive ) {
		RB_EndSurface();
	}

	RB_SetDlightShadowCasterEntity( REFENTITYNUM_WORLD, originalTime );
	return surfaces;
}

typedef struct {
	qboolean valid;
	unsigned int generation;
	unsigned int signature;
} spotShadowCache_t;

static spotShadowCache_t rb_spotShadowCache;

static qboolean RB_SpotShadowCacheSignature( drawSurf_t *drawSurfs, int numDrawSurfs,
	unsigned int *signature )
{
	unsigned int hash;
	int i;

	if ( !signature || tr.shadowManager.spotPlanCount <= 0 ) {
		return qfalse;
	}

	hash = 2166136261U;
	hash = RB_DlightShadowCacheHashUInt( hash, (unsigned int)tr.shadowManager.spotPlanCount );
	hash = RB_DlightShadowCacheHashUInt( hash, (unsigned int)vk_spot_shadow_atlas_height() );
	for ( i = 0; i < tr.shadowManager.spotPlanCount; i++ ) {
		const shadowSpotLightPlan_t *plan = &tr.shadowManager.spotPlans[i];
		int axis;

		hash = RB_DlightShadowCacheHashUInt( hash, (unsigned int)plan->atlasAllocated );
		hash = RB_DlightShadowCacheHashUInt( hash, (unsigned int)plan->atlasX );
		hash = RB_DlightShadowCacheHashUInt( hash, (unsigned int)plan->atlasY );
		hash = RB_DlightShadowCacheHashUInt( hash, (unsigned int)plan->atlasTileSize );
		hash = RB_DlightShadowCacheHashFloat( hash, plan->radius );
		hash = RB_DlightShadowCacheHashFloat( hash, plan->outerAngle );
		for ( axis = 0; axis < 3; axis++ ) {
			hash = RB_DlightShadowCacheHashFloat( hash, plan->origin[axis] );
			hash = RB_DlightShadowCacheHashFloat( hash, plan->direction[axis] );
		}
	}

	for ( i = 0; i < numDrawSurfs; i++ ) {
		const drawSurf_t *drawSurf = &drawSurfs[i];
		shader_t *shader;
		int entityNum;
		int fogNum;
		int dlighted;

		R_DecomposeSort( drawSurf->sort, &entityNum, &shader, &fogNum, &dlighted );
		if ( !RB_DlightShadowEntityCasterAllowed( entityNum ) ||
			!RB_DlightShadowCasterAllowed( shader, drawSurf->surface ) ) {
			continue;
		}
		// entity and brush-model casters move independently of the spot
		// plans; mirror the world-only dlight tile cache policy and re-render
		if ( entityNum != REFENTITYNUM_WORLD ) {
			return qfalse;
		}
		if ( shader->numDeforms > 0 ) {
			return qfalse;
		}

		hash = RB_DlightShadowCacheHashUInt( hash, drawSurf->sort );
		hash = RB_DlightShadowCacheHashPtr( hash, drawSurf->surface );
	}

	*signature = hash;
	return qtrue;
}

static void RB_RenderSpotShadowAtlas( drawSurf_t *drawSurfs, int numDrawSurfs )
{
	trRefdef_t savedRefdef;
	viewParms_t savedViewParms;
	orientationr_t savedOr;
	const trRefEntity_t *savedEntity;
	qboolean savedProjection2D;
#ifdef USE_VBO
	qboolean savedAllowVBO;
#endif
	int atlasHeight;
	unsigned int atlasGeneration;
	unsigned int signature = 0;
	qboolean cacheable;
	int surfaces;
	int i;
	double originalTime;

	if ( !tr.shadowManager.planned ||
		!R_ShadowManagerPassScheduled( &tr.shadowManager,
			SHADOW_MANAGER_PASS_SPOT_ATLAS ) ||
		tr.shadowManager.spotPlanCount <= 0 || !vk_spot_shadow_atlas_available() ) {
		return;
	}

	atlasGeneration = vk_spot_shadow_atlas_generation();
	cacheable = RB_SpotShadowCacheSignature( drawSurfs, numDrawSurfs, &signature );
	if ( cacheable && rb_spotShadowCache.valid &&
		rb_spotShadowCache.generation == atlasGeneration &&
		rb_spotShadowCache.signature == signature ) {
		backEnd.pc.c_spotShadowAtlasCacheHits++;
		vk_mark_spot_shadow_atlas_rendered();
		R_ShadowManagerPublishSpotAtlas( &tr.shadowManager,
			vk_spot_shadow_atlas_ready(), atlasGeneration );
		return;
	}
	if ( cacheable ) {
		backEnd.pc.c_spotShadowAtlasCacheMisses++;
	} else {
		backEnd.pc.c_spotShadowAtlasCacheUncacheable++;
	}

	RB_EndSurface();
	if ( !vk_begin_spot_shadow_render_pass() ) {
		return;
	}

	savedRefdef = backEnd.refdef;
	savedViewParms = backEnd.viewParms;
	savedOr = backEnd.or;
	savedEntity = backEnd.currentEntity;
	savedProjection2D = backEnd.projection2D;
#ifdef USE_VBO
	savedAllowVBO = tess.allowVBO;
	tess.allowVBO = qfalse;
#endif
	backEnd.projection2D = qfalse;
	atlasHeight = vk_spot_shadow_atlas_height();
	originalTime = backEnd.refdef.floatTime;
	surfaces = 0;

	for ( i = 0; i < tr.shadowManager.spotPlanCount; i++ ) {
		const shadowSpotLightPlan_t *plan;
		viewParms_t shadowParms;

		plan = &tr.shadowManager.spotPlans[i];
		if ( !RB_BuildSpotShadowView( plan, atlasHeight, &shadowParms ) ) {
			continue;
		}

		RB_SetDlightShadowView( &shadowParms );
		vk_clear_depth_force( qfalse );
		surfaces += RB_RenderSpotShadowCasters( drawSurfs, numDrawSurfs, originalTime );
	}

	backEnd.refdef = savedRefdef;
	backEnd.viewParms = savedViewParms;
	backEnd.or = savedOr;
	backEnd.currentEntity = savedEntity;
	backEnd.projection2D = savedProjection2D;
#ifdef USE_VBO
	tess.allowVBO = savedAllowVBO;
#endif
	Com_Memcpy( vk_world.modelview_transform, backEnd.or.modelMatrix, 64 );
	tess.depthRange = DEPTH_RANGE_NORMAL;
	vk_update_mvp( NULL );
	vk_end_spot_shadow_render_pass();
	R_ShadowManagerPublishSpotAtlas( &tr.shadowManager,
		vk_spot_shadow_atlas_ready(), atlasGeneration );

	if ( cacheable && surfaces > 0 ) {
		rb_spotShadowCache.valid = qtrue;
		rb_spotShadowCache.generation = atlasGeneration;
		rb_spotShadowCache.signature = signature;
	} else {
		rb_spotShadowCache.valid = qfalse;
	}
}

static void RB_RenderDlightShadowAtlas( void )
{
	dlight_t *dl;
	trRefdef_t savedRefdef;
	viewParms_t savedViewParms;
	orientationr_t savedOr;
	const trRefEntity_t *savedEntity;
	qboolean savedProjection2D;
#ifdef USE_VBO
	qboolean savedAllowVBO;
#endif
	int atlasHeight;
	int startMsec;
	unsigned int atlasGeneration;
	qboolean correctnessMode;
	int i, face;

	if ( !RB_DlightShadowsNeeded() ) {
		return;
	}

	RB_EndSurface();
	if ( !vk_begin_dlight_shadow_render_pass() ) {
		return;
	}
	startMsec = ri.Milliseconds();
	savedRefdef = backEnd.refdef;
	savedViewParms = backEnd.viewParms;
	savedOr = backEnd.or;
	savedEntity = backEnd.currentEntity;
	savedProjection2D = backEnd.projection2D;
#ifdef USE_VBO
	savedAllowVBO = tess.allowVBO;
	tess.allowVBO = qfalse;
#endif
	backEnd.projection2D = qfalse;
	atlasHeight = vk_dlight_shadow_atlas_height();
	atlasGeneration = vk_dlight_shadow_atlas_generation();
	correctnessMode = ( r_shadowCorrectness && r_shadowCorrectness->integer ) ? qtrue : qfalse;

	for ( i = 0; i < ( tr.shadowManager.planned ?
		tr.shadowManager.pointPlanCount : savedViewParms.num_dlights ); i++ ) {
		const shadowPointLightPlan_t *plan;
		unsigned int cacheSignature;
		qboolean cacheable;
		qboolean lightRendered;

		if ( tr.shadowManager.planned ) {
			plan = &tr.shadowManager.pointPlans[i];
			if ( !plan->atlasAllocated || plan->atlasFaceSize <= 0 ||
				plan->dlightIndex < 0 || plan->dlightIndex >= savedViewParms.num_dlights ) {
				continue;
			}
			dl = &savedViewParms.dlights[plan->dlightIndex];
		} else {
			plan = NULL;
			dl = &savedViewParms.dlights[i];
			if ( !dl->shadowPlanned || dl->shadowAtlasFaceSize <= 0 ) {
				continue;
			}
		}

		RB_DlightShadowCasterBoundsMemoReset();
		cacheSignature = 0;
		cacheable = RB_DlightShadowCacheSignature( dl, plan, &cacheSignature );
		lightRendered = qfalse;
		for ( face = 0; face < DLIGHT_SHADOW_FACES; face++ ) {
			viewParms_t shadowParms;
			qboolean shadowParmsBuilt;
			qboolean cachedHasCasters;
			qboolean faceHasCasters;
			int surfaces;

			shadowParmsBuilt = qfalse;
			if ( correctnessMode ) {
				RB_BuildDlightShadowView( dl, plan, face, atlasHeight, &shadowParms );
				shadowParmsBuilt = qtrue;
			}

			if ( cacheable && RB_DlightShadowCacheLookup( dl, plan, face, cacheSignature, atlasGeneration, &cachedHasCasters ) ) {
				if ( correctnessMode ) {
					RB_RecordShadowCorrectnessFace( dl, plan, face,
						tr.shadowManager.dlightAtlasWidth, atlasHeight, atlasGeneration,
						&shadowParms, qtrue, qfalse, cachedHasCasters ? -1 : 0 );
				}
				continue;
			}

			if ( !shadowParmsBuilt ) {
				RB_BuildDlightShadowView( dl, plan, face, atlasHeight, &shadowParms );
			}
			RB_SetDlightShadowView( &shadowParms );
			vk_clear_depth_force( qfalse );

			faceHasCasters = RB_DlightShadowFaceHasCasters( dl );
			if ( !faceHasCasters ) {
				if ( cacheable ) {
					RB_DlightShadowCacheStore( dl, plan, face, cacheSignature, atlasGeneration, qfalse );
				} else {
					RB_DlightShadowCacheInvalidate( dl, plan, face );
				}
				RB_RecordShadowCorrectnessFace( dl, plan, face,
					tr.shadowManager.dlightAtlasWidth, atlasHeight, atlasGeneration,
					&shadowParms, qfalse, qtrue, 0 );
				continue;
			}

			surfaces = RB_RenderDlightShadowCasters( dl );
			if ( cacheable ) {
				RB_DlightShadowCacheStore( dl, plan, face, cacheSignature, atlasGeneration,
					surfaces > 0 ? qtrue : qfalse );
			} else {
				RB_DlightShadowCacheInvalidate( dl, plan, face );
			}
			RB_RecordShadowCorrectnessFace( dl, plan, face,
				tr.shadowManager.dlightAtlasWidth, atlasHeight, atlasGeneration,
				&shadowParms, qfalse, qtrue, surfaces );
			if ( surfaces <= 0 ) {
				continue;
			}
			if ( !lightRendered ) {
				backEnd.pc.c_dlightShadowAtlasLights++;
				lightRendered = qtrue;
			}
			backEnd.pc.c_dlightShadowAtlasFaces++;
			backEnd.pc.c_dlightShadowAtlasSurfaces += surfaces;
		}
	}

	backEnd.refdef = savedRefdef;
	backEnd.viewParms = savedViewParms;
	backEnd.or = savedOr;
	backEnd.currentEntity = savedEntity;
	backEnd.projection2D = savedProjection2D;
#ifdef USE_VBO
	tess.allowVBO = savedAllowVBO;
#endif
	Com_Memcpy( vk_world.modelview_transform, backEnd.or.modelMatrix, 64 );
	tess.depthRange = DEPTH_RANGE_NORMAL;
	vk_update_mvp( NULL );
	vk_end_dlight_shadow_render_pass();
	R_ShadowManagerPublishPointAtlas( &tr.shadowManager,
		vk_dlight_shadow_atlas_ready(), atlasGeneration );
	if ( correctnessMode && tr.shadowCorrectnessDebug.active ) {
		tr.shadowCorrectnessDebug.atlasPublished =
			tr.shadowManager.pointAtlasPublication.published;
	}
	backEnd.pc.c_dlightShadowAtlasMsec += ri.Milliseconds() - startMsec;
}
static void RB_LightingPass( rbDrawSurfsMode_t mode, qboolean finish )
{
	dlight_t	*dl;
	int	i;

#ifdef USE_VBO
	//VBO_Flush();
	//tess.allowVBO = qfalse; // for now
#endif

	tess.dlightPass = qtrue;

	for ( i = 0; i < backEnd.viewParms.num_dlights; i++ )
	{
		dl = &backEnd.viewParms.dlights[i];
		if ( dl->head )
		{
			tess.light = dl;
			RB_RenderLitSurfList( dl, mode );
		}
	}

	tess.dlightPass = qfalse;

	if ( finish ) {
		backEnd.viewParms.num_dlights = 0;
	}
}
#endif


static void transform_to_eye_space( const vec3_t v, vec3_t v_eye )
{
	const float *m = backEnd.viewParms.world.modelMatrix;
	v_eye[0] = m[0]*v[0] + m[4]*v[1] + m[8 ]*v[2] + m[12];
	v_eye[1] = m[1]*v[0] + m[5]*v[1] + m[9 ]*v[2] + m[13];
	v_eye[2] = m[2]*v[0] + m[6]*v[1] + m[10]*v[2] + m[14];
}


/*
================
RE_DrawWebUISurface

Uploads the retail Quake Live host WebUI surface synchronously and draws it
through the normal 2D command path.  The dimension limit also keeps the byte
count passed to the legacy Vulkan upload interface representable as an int.
================
*/
void RE_DrawWebUISurface( int x, int y, int w, int h, int cols, int rows,
	const byte *data, qboolean dirty ) {
	image_t *image;
	int uploadBytes;

	if ( !tr.registered || !data || cols <= 0 || rows <= 0
		|| cols > 4096 || rows > 4096 ) {
		return;
	}

	uploadBytes = cols * rows * 4;

	if ( !tr.webUIImage ) {
		tr.webUIImage = R_CreateImage(
			"*webui", NULL, (byte *)data, cols, rows,
			IMGFLAG_CLAMPTOEDGE | IMGFLAG_NO_COMPRESSION | IMGFLAG_NOSCALE );
		if ( !tr.webUIImage ) {
			return;
		}
		tr.webUIShader = RE_RegisterShaderFromImage(
			"*webui", LIGHTMAP_2D, tr.webUIImage, qfalse );
		dirty = qfalse;
	}

	image = tr.webUIImage;
	if ( cols != image->width || rows != image->height ) {
		image->width = image->uploadWidth = cols;
		image->height = image->uploadHeight = rows;
#ifdef USE_VULKAN
		vk_create_image( image, cols, rows, 1 );
		vk_upload_image_data( image, 0, 0, cols, rows, 1,
			(byte *)data, uploadBytes, qfalse );
#else
		GL_Bind( image );
		qglTexImage2D( GL_TEXTURE_2D, 0, image->internalFormat,
			cols, rows, 0, GL_RGBA, GL_UNSIGNED_BYTE, data );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gl_clamp_mode );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gl_clamp_mode );
#endif
	} else if ( dirty ) {
#ifdef USE_VULKAN
		vk_upload_image_data( image, 0, 0, cols, rows, 1,
			(byte *)data, uploadBytes, qtrue );
#else
		GL_Bind( image );
		qglTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0,
			cols, rows, GL_RGBA, GL_UNSIGNED_BYTE, data );
#endif
	}

	RE_StretchPic( x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, tr.webUIShader );
}


/*
================
RB_DebugPolygon
================
*/
static void RB_DebugPolygon( int color, int numPoints, float *points ) {
	vec3_t pa;
	vec3_t pb;
	vec3_t p;
	vec3_t q;
	vec3_t n;
	int i;

	if ( numPoints < 3 ) {
		return;
	}

	transform_to_eye_space( &points[0], pa );
	transform_to_eye_space( &points[3], pb );
	VectorSubtract( pb, pa, p );

	for ( i = 2; i < numPoints; i++ ) {
		transform_to_eye_space( &points[3*i], pb );
		VectorSubtract( pb, pa, q );
		CrossProduct( q, p, n );
		if ( VectorLength( n ) > 1e-5 ) {
			break;
		}
	}

	if ( DotProduct( n, pa ) >= 0 ) {
		return; // discard backfacing polygon
	}

#ifdef USE_VULKAN
	// Solid shade.
	for (i = 0; i < numPoints; i++) {
		VectorCopy(&points[3*i], tess.xyz[i]);

		tess.svars.colors[0][i].rgba[0] = (color&1) ? 255 : 0;
		tess.svars.colors[0][i].rgba[1] = (color&2) ? 255 : 0;
		tess.svars.colors[0][i].rgba[2] = (color&4) ? 255 : 0;
		tess.svars.colors[0][i].rgba[3] = 255;
	}
	tess.numVertexes = numPoints;

	tess.numIndexes = 0;
	for (i = 1; i < numPoints - 1; i++) {
		tess.indexes[tess.numIndexes + 0] = 0;
		tess.indexes[tess.numIndexes + 1] = i;
		tess.indexes[tess.numIndexes + 2] = i + 1;
		tess.numIndexes += 3;
	}

	vk_bind_index();
	vk_bind_pipeline( vk.surface_debug_pipeline_solid );
	vk_bind_geometry( TESS_XYZ | TESS_RGBA0 | TESS_ST0 );
	vk_draw_geometry( DEPTH_RANGE_NORMAL, qtrue );

	// Outline.
	Com_Memset( tess.svars.colors[0], tr.identityLightByte, numPoints * 2 * sizeof( color4ub_t ) );

	for ( i = 0; i < numPoints; i++ ) {
		VectorCopy( &points[3*i], tess.xyz[2*i] );
		VectorCopy( &points[3*((i + 1) % numPoints)], tess.xyz[2*i + 1] );
	}
	tess.numVertexes = numPoints * 2;
	tess.numIndexes = 0;

	vk_bind_pipeline( vk.surface_debug_pipeline_outline );
	vk_bind_geometry( TESS_XYZ | TESS_RGBA0 );
	vk_draw_geometry( DEPTH_RANGE_ZERO, qfalse );
	tess.numVertexes = 0;
#else
	GL_SelectTexture( 0 );
	qglDisable( GL_TEXTURE_2D );

	GL_ClientState( 0, CLS_NONE );
	qglVertexPointer( 3, GL_FLOAT, 0, points );

	// draw solid shade
	GL_State( GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE );
	qglColor4f( color&1, (color>>1)&1, (color>>2)&1, 1 );
	qglDrawArrays( GL_TRIANGLE_FAN, 0, numPoints );

	// draw wireframe outline
	qglDepthRange( 0, 0 );
	qglColor4f( 1, 1, 1, 1 );
	qglDrawArrays( GL_LINE_LOOP, 0, numPoints );
	qglDepthRange( 0, 1 );

	qglEnable( GL_TEXTURE_2D );
#endif
}


/*
====================
RB_DebugGraphics

Visualization aid for movement clipping debugging
====================
*/
static void RB_DebugGraphics( void ) {

	if ( !r_debugSurface->integer ) {
		return;
	}

	GL_Bind( tr.whiteImage );
#ifdef USE_VULKAN
	vk_update_mvp( NULL );
#else
	GL_Cull( CT_FRONT_SIDED );
#endif
	ri.CM_DrawDebugSurface( RB_DebugPolygon );
}


/*
=============
RB_DrawSurfs
=============
*/
#ifdef USE_VULKAN
static void RB_PreparePostProcessForHud3D( const trRefdef_t *refdef )
{
	if ( !backEnd.doneSurfaces ||
		!r_hudExcludePostProcess ||
		!r_hudExcludePostProcess->integer ||
		!( refdef->rdflags & RDF_NOWORLDMODEL ) ) {
		return;
	}

	if ( r_bloom && r_bloom->integer && !backEnd.doneBloom ) {
		vk_bloom();
	}
}
#endif

static const void *RB_DrawSurfs( const void *data ) {
	const drawSurfsCommand_t *cmd;
#ifdef USE_VULKAN
	qboolean rtComposition;
	qboolean rtTraceCompleted = qfalse;
	qboolean requiredPrimaryTrace = qfalse;
#endif
#ifdef USE_PMLIGHT
	const csmPlan_t *managerCSM;
#endif

	RB_FrameGraph_NotePass( RTX_FRAME_PASS_DRAW_SURFS );

	cmd = (const drawSurfsCommand_t *)data;

	// finish any 2D drawing if needed
	RB_EndSurface();

#ifdef USE_VULKAN
	RB_PreparePostProcessForHud3D( &cmd->refdef );
#endif

	backEnd.refdef = cmd->refdef;
	backEnd.viewParms = cmd->viewParms;
#ifdef USE_PMLIGHT
	tr.shadowManager = cmd->shadowManager;
	managerCSM = R_ShadowManagerCSMPlan( &tr.shadowManager );
	tr.csm = managerCSM ? *managerCSM : cmd->csm;
#endif

#ifdef USE_VBO
	VBO_UnBind();
#endif

#ifdef USE_PMLIGHT
	RB_RunScheduledShadowManagerPreMainPasses( cmd->drawSurfs, cmd->numDrawSurfs );
#endif

	// clear the z buffer, set the modelview, etc
	RB_BeginDrawingView();

#ifdef USE_VULKAN
	rtComposition = vk_rt_primary_view_eligible();
	if ( RB_IsPrimaryFullView() &&
		!glConfig.stereoEnabled ) {
		backEnd.liquidScreenMapDone = qfalse;
	}
	RB_RenderDrawSurfList(
		cmd->drawSurfs,
		cmd->numDrawSurfs,
		rtComposition ? RB_DRAWSURFS_RT_BASE : RB_DRAWSURFS_ALL );
#else
	RB_RenderDrawSurfList( cmd->drawSurfs, cmd->numDrawSurfs, RB_DRAWSURFS_ALL );
#endif

#ifdef USE_VBO
	VBO_UnBind();
#endif

#ifdef USE_VULKAN
	if ( rtComposition ) {
#ifdef USE_PMLIGHT
		const qboolean rasterOwnsRtBaseLights =
			( rtx_rt_raster_reference &&
				rtx_rt_raster_reference->integer ) ? qtrue : qfalse;

		/*
		 * Compatibility composition samples the raster base in raygen.
		 * Put base-surface PMLIGHT into that reference before tracing so
		 * visibility-only RT lights can occlude the contribution they own.
		 * Overlay surfaces remain lit later at their original composition
		 * point. Full-RT mode still owns base lighting itself.
		 */
		if ( rasterOwnsRtBaseLights &&
			backEnd.refdef.numLitSurfs && backEnd.viewParms.num_dlights ) {
			RB_BeginDrawingLitSurfs();
			RB_LightingPass( RB_DRAWSURFS_RT_BASE, qfalse );
		}
#endif

		requiredPrimaryTrace =
			( rtx_rt_require && rtx_rt_require->integer &&
				rtComposition ) ?
				qtrue : qfalse;
		RB_FrameGraph_NotePass( RTX_FRAME_PASS_RT_TRACE );
		rtTraceCompleted = vk_rt_trace_frame();
		if ( requiredPrimaryTrace && !rtTraceCompleted ) {
			ri.Error( ERR_FATAL,
				"RTX RT: rtx_rt_require 1 primary-view dispatch or scene-color copy failed; refusing silent raster fallback" );
		}

#ifdef USE_PMLIGHT
		/*
		 * Experimental full-RT shading owns base lights when tracing succeeds.
		 * A skipped/failed trace recovers that raster contribution here.
		 */
		if ( !rasterOwnsRtBaseLights && !rtTraceCompleted &&
			backEnd.refdef.numLitSurfs && backEnd.viewParms.num_dlights ) {
			RB_BeginDrawingLitSurfs();
			RB_LightingPass( RB_DRAWSURFS_RT_BASE, qfalse );
		}
#endif

		/* The trace validates its composition path before ending the raster
		 * pass.  Success continues in post-bloom; a permissive failure keeps the
		 * complete raster pass active.  The overlay is submitted exactly once in
		 * either state. */
		RB_RenderDrawSurfList( cmd->drawSurfs, cmd->numDrawSurfs, RB_DRAWSURFS_RT_OVERLAY );
	}
#endif

	if ( r_drawSun->integer ) {
		RB_DrawSun( 0.1f, tr.sunShader );
	}

	// darken down any stencil shadows
	RB_ShadowFinish();

	// add light flares on lights that aren't obscured
	RB_RenderFlares();

#ifdef USE_PMLIGHT
	if ( backEnd.refdef.numLitSurfs && backEnd.viewParms.num_dlights ) {
		RB_BeginDrawingLitSurfs();
#ifdef USE_VULKAN
		RB_LightingPass(
			rtComposition ? RB_DRAWSURFS_RT_OVERLAY : RB_DRAWSURFS_ALL,
			qtrue );
#else
		RB_LightingPass( RB_DRAWSURFS_ALL, qtrue );
#endif
	}
#endif

	// draw main system development information (surface outlines, etc)
	RB_DebugGraphics();

#ifdef USE_VULKAN
	vk_draw_global_fog();

	if ( cmd->refdef.switchRenderPass ) {
		vk_end_render_pass();
		vk_begin_main_render_pass();
		backEnd.screenMapDone = qtrue;
	}
#endif

	//TODO Maybe check for rdf_noworld stuff but q3mme has full 3d ui
	backEnd.doneSurfaces = qtrue; // for bloom
#ifdef USE_VULKAN
	vk_motion_blur();
#endif

	return (const void *)(cmd + 1);
}


/*
=============
RB_DrawBuffer
=============
*/
static const void *RB_DrawBuffer( const void *data ) {
	const drawBufferCommand_t	*cmd;

	cmd = (const drawBufferCommand_t *)data;
	RB_FrameGraph_NotePass( RTX_FRAME_PASS_DRAW_BUFFER );

#ifdef USE_VULKAN
	vk_begin_frame();

	tess.depthRange = DEPTH_RANGE_NORMAL;

	// force depth range and viewport/scissor updates
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;

	if ( r_clear->integer && vk.clearAttachment ) {
		const vec4_t color = {1, 0, 0.5, 1};
		backEnd.projection2D = qtrue; // to ensure we have viewport that occupies entire window
		vk_clear_color( color );
		backEnd.projection2D = qfalse;
	}
#else
	qglDrawBuffer( cmd->buffer );

	// clear screen for debugging
	if ( r_clear->integer ) {
		qglClearColor( 1, 0, 0.5, 1 );
		qglClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	}
#endif

	return (const void *)(cmd + 1);
}


/*
===============
RB_ShowImages

Draw all the images to the screen, on top of whatever
was there.  This is used to test for texture thrashing.

Also called by RE_EndRegistration
===============
*/
#ifdef USE_VULKAN
void RB_ShowImages( void )
{
	int i;

	if ( !backEnd.projection2D ) {
		RB_SetGL2D();
	}

	// draw full-screen quad
	tess.numVertexes = 4;

	tess.svars.colors[0][0].u32 = ~0U; // 255-255-255-255
	tess.svars.colors[0][1].u32 = ~0U;
	tess.svars.colors[0][2].u32 = ~0U;
	tess.svars.colors[0][3].u32 = ~0U;

	tess.svars.texcoords[0][0][0] = 0.0f;
	tess.svars.texcoords[0][0][1] = 0.0f;

	tess.svars.texcoords[0][1][0] = 1.0f;
	tess.svars.texcoords[0][1][1] = 0.0f;

	tess.svars.texcoords[0][2][0] = 0.0f;
	tess.svars.texcoords[0][2][1] = 1.0f;

	tess.svars.texcoords[0][3][0] = 1.0f;
	tess.svars.texcoords[0][3][1] = 1.0f;

	tess.svars.texcoordPtr[0] = tess.svars.texcoords[0];

	tess.xyz[0][0] = 0.0f;
	tess.xyz[0][1] = 0.0f;

	tess.xyz[1][0] = (float)glConfig.vidWidth;
	tess.xyz[1][1] = 0.0f;

	tess.xyz[2][0] = 0.0f;
	tess.xyz[2][1] = (float)glConfig.vidHeight;

	tess.xyz[3][0] = (float)glConfig.vidWidth;
	tess.xyz[3][1] = (float)glConfig.vidHeight;

	vk_bind_pipeline( vk.images_debug_pipeline2 );
	vk_bind_geometry( TESS_XYZ | TESS_RGBA0 | TESS_ST0 );
	vk_draw_geometry( DEPTH_RANGE_NORMAL, qfalse );

	for ( i = 0; i < tr.numImages; i++ ) {
		image_t* image = tr.images[i];

		float w = glConfig.vidWidth / 20;
		float h = glConfig.vidHeight / 15;
		float x = i % 20 * w;
		float y = i / 20 * h;

		// show in proportional size in mode 2
		if ( r_showImages->integer == 2 ) {
			w *= image->uploadWidth / 512.0f;
			h *= image->uploadHeight / 512.0f;
		}

		tess.xyz[0][0] = x;
		tess.xyz[0][1] = y;

		tess.xyz[1][0] = x + w;
		tess.xyz[1][1] = y;

		tess.xyz[2][0] = x;
		tess.xyz[2][1] = y + h;

		tess.xyz[3][0] = x + w;
		tess.xyz[3][1] = y + h;

		GL_Bind( image );
		vk_bind_pipeline( vk.images_debug_pipeline );
		vk_bind_geometry( TESS_XYZ );
		vk_draw_geometry( DEPTH_RANGE_NORMAL, qfalse );
	}

	tess.numIndexes = 0;
	tess.numVertexes = 0;
}
#else
void RB_ShowImages( void ) {
	int		i;
	image_t	*image;
	float	x, y, w, h;
	int		start, end;
	const vec2_t t[4] = { {0,0}, {1,0}, {0,1}, {1,1} };
	vec3_t v[4];

	if ( !backEnd.projection2D ) {
		RB_SetGL2D();
	}

	qglClear( GL_COLOR_BUFFER_BIT );

	qglFinish();

	GL_ClientState( 0, CLS_TEXCOORD_ARRAY );
	qglTexCoordPointer( 2, GL_FLOAT, 0, t );

	start = ri.Milliseconds();

	for ( i = 0; i < tr.numImages; i++ ) {
		image = tr.images[ i ];
		w = glConfig.vidWidth / 20;
		h = glConfig.vidHeight / 15;
		x = i % 20 * w;
		y = i / 20 * h;

		// show in proportional size in mode 2
		if ( r_showImages->integer == 2 ) {
			w *= image->uploadWidth / 512.0f;
			h *= image->uploadHeight / 512.0f;
		}

		GL_Bind( image );

		VectorSet(v[0],x,y,0);
		VectorSet(v[1],x+w,y,0);
		VectorSet(v[2],x,y+h,0);
		VectorSet(v[3],x+w,y+h,0);

		qglVertexPointer( 3, GL_FLOAT, 0, v );
		qglDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	}

	qglFinish();

	end = ri.Milliseconds();
	ri.Printf( PRINT_ALL, "%i msec to draw all images\n", end - start );
}
#endif


/*
=============
RB_ColorMask
=============
*/
static const void *RB_ColorMask( const void *data )
{
	const colorMaskCommand_t *cmd = data;
	RB_FrameGraph_NotePass( RTX_FRAME_PASS_UI_2D );
#ifdef USE_VULKAN
	// TODO: implement! ZZZZZZZZZZZ
#else
	qglColorMask( cmd->rgba[0], cmd->rgba[1], cmd->rgba[2], cmd->rgba[3] );
#endif

	return (const void *)(cmd + 1);
}


/*
=============
RB_ClearDepth
=============
*/
static const void *RB_ClearDepth( const void *data )
{
	const clearDepthCommand_t *cmd = data;
	RB_FrameGraph_NotePass( RTX_FRAME_PASS_CLEAR_DEPTH );

	RB_EndSurface();

#ifdef USE_VULKAN
	vk_clear_depth( r_shadows->integer == 2 ? qtrue : qfalse );
#else
	qglClear( GL_DEPTH_BUFFER_BIT );
#endif

	return (const void *)(cmd + 1);
}


/*
=============
RB_ClearColor
=============
*/
static const void *RB_ClearColor( const void *data )
{
	const clearColorCommand_t *cmd = data;
	vec4_t clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };

	if ( r_fastsky->integer ) {
		R_QLFastSkyColor( clearColor );
	}
	RB_FrameGraph_NotePass( RTX_FRAME_PASS_CLEAR_COLOR );

#ifdef USE_VULKAN
	backEnd.projection2D = qtrue;
	vk_clear_color( clearColor );
	backEnd.projection2D = qfalse;
#else
	qglViewport( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
	qglScissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
	qglClearColor( clearColor[0], clearColor[1], clearColor[2], clearColor[3] );
	qglClear( GL_COLOR_BUFFER_BIT );
#endif

	return (const void *)(cmd + 1);
}


/*
=============
RB_FinishBloom
=============
*/
static const void *RB_FinishBloom( const void *data )
{
	const finishBloomCommand_t *cmd = data;
	RB_FrameGraph_NotePass( RTX_FRAME_PASS_FINISH_BLOOM );

	RB_EndSurface();

#ifdef USE_VULKAN
	if ( r_bloom->integer ) {
		vk_bloom();
	}
#endif

	// texture swapping test
	if ( r_showImages->integer ) {
		RB_ShowImages();
	}

	backEnd.drawConsole = qtrue;

	return (const void *)(cmd + 1);
}


static const void *RB_SwapBuffers( const void *data ) {

	const swapBuffersCommand_t	*cmd = (const swapBuffersCommand_t *)data;
	RB_FrameGraph_NotePass( RTX_FRAME_PASS_SWAP_BUFFERS );

	// finish any 2D drawing if needed
	RB_EndSurface();

	// texture swapping test
	if ( r_showImages->integer && !backEnd.drawConsole ) {
		RB_ShowImages();
	}

	tr.needScreenMap = 0;

#ifdef USE_VULKAN
	vk_end_frame();

	if ( backEnd.doneSurfaces && !glState.finishCalled ) {
		vk_queue_wait_idle();
	}
#else
	if ( backEnd.doneSurfaces && !glState.finishCalled ) {
		qglFinish();
	}
#endif

#ifdef USE_VULKAN
	if ( ( backEnd.screenshotMask || backEnd.levelshotPending ) && vk.cmd->waitForFence ) {
#else
	if ( ( backEnd.screenshotMask || backEnd.levelshotPending ) && tr.frameCount > 1 ) {
#endif
		if ( backEnd.screenshotMask & SCREENSHOT_PNG && backEnd.screenshotPNG[0] ) {
			RB_TakeScreenshotPNG( 0, 0, gls.captureWidth, gls.captureHeight, backEnd.screenshotPNG );
			if ( !backEnd.screenShotPNGsilent ) {
				ri.Printf( PRINT_ALL, "Wrote %s\n", backEnd.screenshotPNG );
			}
		}
		if ( backEnd.screenshotMask & SCREENSHOT_TGA && backEnd.screenshotTGA[0] ) {
			RB_TakeScreenshot( 0, 0, gls.captureWidth, gls.captureHeight, backEnd.screenshotTGA );
			if ( !backEnd.screenShotTGAsilent ) {
				ri.Printf( PRINT_ALL, "Wrote %s\n", backEnd.screenshotTGA );
			}
		}
		if ( backEnd.screenshotMask & SCREENSHOT_JPG && backEnd.screenshotJPG[0] ) {
			RB_TakeScreenshotJPEG( 0, 0, gls.captureWidth, gls.captureHeight, backEnd.screenshotJPG );
			if ( !backEnd.screenShotJPGsilent ) {
				ri.Printf( PRINT_ALL, "Wrote %s\n", backEnd.screenshotJPG );
			}
		}
		if ( backEnd.screenshotMask & SCREENSHOT_BMP && ( backEnd.screenshotBMP[0] || ( backEnd.screenshotMask & SCREENSHOT_BMP_CLIPBOARD ) ) ) {
			RB_TakeScreenshotBMP( 0, 0, gls.captureWidth, gls.captureHeight, backEnd.screenshotBMP, backEnd.screenshotMask & SCREENSHOT_BMP_CLIPBOARD );
			if ( !backEnd.screenShotBMPsilent ) {
				ri.Printf( PRINT_ALL, "Wrote %s\n", backEnd.screenshotBMP );
			}
		}
		if ( backEnd.screenshotMask & SCREENSHOT_AVI ) {
			RB_TakeVideoFrameCmd( &backEnd.vcmd );
		}
		if ( backEnd.levelshotPending ) {
			RB_TakeLevelShot();
			backEnd.levelshotPending = qfalse;
		}

		backEnd.screenshotPNG[0] = '\0';
		backEnd.screenshotJPG[0] = '\0';
		backEnd.screenshotTGA[0] = '\0';
		backEnd.screenshotBMP[0] = '\0';
		backEnd.screenshotMask = 0;

		if ( !backEnd.levelshotPending ) {
			ri.Cvar_Set( "cl_captureActive", "0" );
		}
	}

#ifdef USE_VULKAN
	vk_present_frame();
#else
	ri.GLimp_EndFrame();
#endif

	backEnd.projection2D = qfalse;
	backEnd.doneSurfaces = qfalse;
	backEnd.bloomProtectHighlights = qfalse;
	backEnd.drawConsole = qfalse;
#ifdef USE_VULKAN
	backEnd.doneBloom = qfalse;
	backEnd.doneMotionBlur = qfalse;
#endif
	RB_FrameGraph_EndLifecycle( qtrue );

	return (const void *)(cmd + 1);
}


/*
====================
RB_ExecuteRenderCommands
====================
*/
void RB_ExecuteRenderCommands( const void *data ) {

	backEnd.pc.msec = ri.Milliseconds();

	while ( 1 ) {
		data = PADP(data, sizeof(void *));

		switch ( *(const int *)data ) {
		case RC_SET_COLOR:
			data = RB_SetColor( data );
			break;
		case RC_STRETCH_PIC:
			data = RB_StretchPic( data );
			break;
		case RC_DRAW_SURFS:
			data = RB_DrawSurfs( data );
			break;
		case RC_DRAW_BUFFER:
			data = RB_DrawBuffer( data );
			break;
		case RC_SWAP_BUFFERS:
			data = RB_SwapBuffers( data );
			break;
		case RC_FINISHBLOOM:
			data = RB_FinishBloom(data);
			break;
		case RC_COLORMASK:
			data = RB_ColorMask(data);
			break;
		case RC_CLEARDEPTH:
			data = RB_ClearDepth(data);
			break;
		case RC_CLEARCOLOR:
			data = RB_ClearColor(data);
			break;
		case RC_END_OF_LIST:
		default:
			// stop rendering
			RB_FrameGraph_EndLifecycle( qfalse );
#ifdef USE_VULKAN
			vk_end_frame();
//			if (com_errorEntered && (begin_frame_called && !end_frame_called)) {
//				vk_end_frame();
//			}
#else
			backEnd.pc.msec = ri.Milliseconds() - backEnd.pc.msec;
#endif
			return;
		}
	}
}
