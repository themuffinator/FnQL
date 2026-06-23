#include "glx_executor.h"
#include "glx_draw.h"

namespace glx {

typedef qboolean ( *DynamicDrawExecutor )( const DynamicDraw &draw );
typedef qboolean ( *FramePassExecutor )( const FramePass &pass );
typedef qboolean ( *WorldPacketExecutor )( const WorldPacket &packet );
typedef qboolean ( *MaterialExecutor )( const MaterialIR &material );
typedef qboolean ( *UploadExecutor )( const UploadPlan &plan );
typedef qboolean ( *PostExecutor )( const PostNode &node );
typedef qboolean ( *OutputExecutor )( const OutputTransform &transform );

struct TierExecutor {
	RenderProductTier tier;
	FramePassExecutor framePass;
	DynamicDrawExecutor dynamicDraw;
	WorldPacketExecutor worldPacket;
	MaterialExecutor material;
	UploadExecutor upload;
	PostExecutor post;
	OutputExecutor output;
};

static qboolean GLX_Executor_ValidateFramePass( const FramePass &pass )
{
	return pass.sequence >= 0 ? qtrue : qfalse;
}

static qboolean GLX_Executor_GL12WorldPacket( const WorldPacket &packet )
{
	return GLX_RenderIR_TierSupportsWorldPacket( RenderProductTier::GL12, packet );
}

static qboolean GLX_Executor_GL2XWorldPacket( const WorldPacket &packet )
{
	return GLX_RenderIR_TierSupportsWorldPacket( RenderProductTier::GL2X, packet );
}

static qboolean GLX_Executor_GL3XWorldPacket( const WorldPacket &packet )
{
	return GLX_RenderIR_TierSupportsWorldPacket( RenderProductTier::GL3X, packet );
}

static qboolean GLX_Executor_GL41WorldPacket( const WorldPacket &packet )
{
	return GLX_RenderIR_TierSupportsWorldPacket( RenderProductTier::GL41, packet );
}

static qboolean GLX_Executor_GL46WorldPacket( const WorldPacket &packet )
{
	return GLX_RenderIR_TierSupportsWorldPacket( RenderProductTier::GL46, packet );
}

static qboolean GLX_Executor_GL12Material( const MaterialIR &material )
{
	return GLX_RenderIR_TierSupportsMaterial( RenderProductTier::GL12, material );
}

static qboolean GLX_Executor_GL2XMaterial( const MaterialIR &material )
{
	return GLX_RenderIR_TierSupportsMaterial( RenderProductTier::GL2X, material );
}

static qboolean GLX_Executor_GL3XMaterial( const MaterialIR &material )
{
	return GLX_RenderIR_TierSupportsMaterial( RenderProductTier::GL3X, material );
}

static qboolean GLX_Executor_GL41Material( const MaterialIR &material )
{
	return GLX_RenderIR_TierSupportsMaterial( RenderProductTier::GL41, material );
}

static qboolean GLX_Executor_GL46Material( const MaterialIR &material )
{
	return GLX_RenderIR_TierSupportsMaterial( RenderProductTier::GL46, material );
}

static qboolean GLX_Executor_GL12UploadPlan( const UploadPlan &plan )
{
	return GLX_RenderIR_TierSupportsUploadPlan( RenderProductTier::GL12, plan );
}

static qboolean GLX_Executor_GL2XUploadPlan( const UploadPlan &plan )
{
	return GLX_RenderIR_TierSupportsUploadPlan( RenderProductTier::GL2X, plan );
}

static qboolean GLX_Executor_GL3XUploadPlan( const UploadPlan &plan )
{
	return GLX_RenderIR_TierSupportsUploadPlan( RenderProductTier::GL3X, plan );
}

static qboolean GLX_Executor_GL41UploadPlan( const UploadPlan &plan )
{
	return GLX_RenderIR_TierSupportsUploadPlan( RenderProductTier::GL41, plan );
}

static qboolean GLX_Executor_GL46UploadPlan( const UploadPlan &plan )
{
	return GLX_RenderIR_TierSupportsUploadPlan( RenderProductTier::GL46, plan );
}

static qboolean GLX_Executor_GL12PostNode( const PostNode &node )
{
	return GLX_RenderIR_TierSupportsPostNode( RenderProductTier::GL12, node );
}

static qboolean GLX_Executor_GL2XPostNode( const PostNode &node )
{
	return GLX_RenderIR_TierSupportsPostNode( RenderProductTier::GL2X, node );
}

static qboolean GLX_Executor_GL3XPostNode( const PostNode &node )
{
	return GLX_RenderIR_TierSupportsPostNode( RenderProductTier::GL3X, node );
}

static qboolean GLX_Executor_GL41PostNode( const PostNode &node )
{
	return GLX_RenderIR_TierSupportsPostNode( RenderProductTier::GL41, node );
}

static qboolean GLX_Executor_GL46PostNode( const PostNode &node )
{
	return GLX_RenderIR_TierSupportsPostNode( RenderProductTier::GL46, node );
}

static qboolean GLX_Executor_GL12OutputTransform( const OutputTransform &transform )
{
	return GLX_RenderIR_TierSupportsOutputTransform( RenderProductTier::GL12, transform );
}

static qboolean GLX_Executor_GL2XOutputTransform( const OutputTransform &transform )
{
	return GLX_RenderIR_TierSupportsOutputTransform( RenderProductTier::GL2X, transform );
}

static qboolean GLX_Executor_GL3XOutputTransform( const OutputTransform &transform )
{
	return GLX_RenderIR_TierSupportsOutputTransform( RenderProductTier::GL3X, transform );
}

static qboolean GLX_Executor_GL41OutputTransform( const OutputTransform &transform )
{
	return GLX_RenderIR_TierSupportsOutputTransform( RenderProductTier::GL41, transform );
}

static qboolean GLX_Executor_GL46OutputTransform( const OutputTransform &transform )
{
	return GLX_RenderIR_TierSupportsOutputTransform( RenderProductTier::GL46, transform );
}

static qboolean GLX_Executor_SubmitDynamicDraw( const DynamicDraw &draw )
{
	// precondition: every caller has already validated the draw through
	// GLX_RenderIR_TierSupportsDynamicDraw(), which runs ValidateDynamicDraw
	if ( draw.kind == DynamicDrawKind::Indexed ) {
		return GLX_Draw_DrawElements( draw.primitive, draw.count, draw.indexType, draw.indices );
	}
	return GLX_Draw_DrawArrays( draw.primitive, draw.first, draw.count );
}

static qboolean GLX_Executor_GL12DynamicDraw( const DynamicDraw &draw )
{
	if ( !GLX_RenderIR_TierSupportsDynamicDraw( RenderProductTier::GL12, draw ) ) {
		return qfalse;
	}
	return GLX_Executor_SubmitDynamicDraw( draw );
}

static qboolean GLX_Executor_GL2XDynamicDraw( const DynamicDraw &draw )
{
	if ( !GLX_RenderIR_TierSupportsDynamicDraw( RenderProductTier::GL2X, draw ) ) {
		return qfalse;
	}
	return GLX_Executor_SubmitDynamicDraw( draw );
}

static qboolean GLX_Executor_GL3XDynamicDraw( const DynamicDraw &draw )
{
	if ( !GLX_RenderIR_TierSupportsDynamicDraw( RenderProductTier::GL3X, draw ) ) {
		return qfalse;
	}
	return GLX_Executor_SubmitDynamicDraw( draw );
}

static qboolean GLX_Executor_GL41DynamicDraw( const DynamicDraw &draw )
{
	if ( !GLX_RenderIR_TierSupportsDynamicDraw( RenderProductTier::GL41, draw ) ) {
		return qfalse;
	}
	return GLX_Executor_SubmitDynamicDraw( draw );
}

static qboolean GLX_Executor_GL46DynamicDraw( const DynamicDraw &draw )
{
	if ( !GLX_RenderIR_TierSupportsDynamicDraw( RenderProductTier::GL46, draw ) ) {
		return qfalse;
	}
	return GLX_Executor_SubmitDynamicDraw( draw );
}

static const TierExecutor GLX_EXECUTORS[] = {
	{ RenderProductTier::GL12, GLX_Executor_ValidateFramePass, GLX_Executor_GL12DynamicDraw,
		GLX_Executor_GL12WorldPacket, GLX_Executor_GL12Material,
		GLX_Executor_GL12UploadPlan, GLX_Executor_GL12PostNode,
		GLX_Executor_GL12OutputTransform },
	{ RenderProductTier::GL2X, GLX_Executor_ValidateFramePass, GLX_Executor_GL2XDynamicDraw,
		GLX_Executor_GL2XWorldPacket, GLX_Executor_GL2XMaterial,
		GLX_Executor_GL2XUploadPlan, GLX_Executor_GL2XPostNode,
		GLX_Executor_GL2XOutputTransform },
	{ RenderProductTier::GL3X, GLX_Executor_ValidateFramePass, GLX_Executor_GL3XDynamicDraw,
		GLX_Executor_GL3XWorldPacket, GLX_Executor_GL3XMaterial,
		GLX_Executor_GL3XUploadPlan, GLX_Executor_GL3XPostNode,
		GLX_Executor_GL3XOutputTransform },
	{ RenderProductTier::GL41, GLX_Executor_ValidateFramePass, GLX_Executor_GL41DynamicDraw,
		GLX_Executor_GL41WorldPacket, GLX_Executor_GL41Material,
		GLX_Executor_GL41UploadPlan, GLX_Executor_GL41PostNode,
		GLX_Executor_GL41OutputTransform },
	{ RenderProductTier::GL46, GLX_Executor_ValidateFramePass, GLX_Executor_GL46DynamicDraw,
		GLX_Executor_GL46WorldPacket, GLX_Executor_GL46Material,
		GLX_Executor_GL46UploadPlan, GLX_Executor_GL46PostNode,
		GLX_Executor_GL46OutputTransform },
};

static const TierExecutor &GLX_Executor_ForTier( RenderProductTier tier )
{
	for ( const TierExecutor &executor : GLX_EXECUTORS ) {
		if ( executor.tier == tier ) {
			return executor;
		}
	}
	return GLX_EXECUTORS[0];
}

static void GLX_Executor_RecordReject( ExecutorState *state )
{
	if ( state ) {
		state->rejectedProducts++;
	}
}

static void GLX_Executor_RecordDynamicDrawAccounting( ExecutorState *state,
	const DynamicDraw &draw )
{
	const unsigned int count = draw.count > 0 ? static_cast<unsigned int>( draw.count ) : 0u;
	const int role = static_cast<int>( draw.role );
	const int pass = static_cast<int>( draw.pass );

	if ( !state ) {
		return;
	}

	if ( role >= 0 && role < GLX_RENDER_IR_DYNAMIC_DRAW_ROLE_COUNT ) {
		state->dynamicDrawRoleDraws[role]++;
		if ( draw.kind == DynamicDrawKind::Indexed ) {
			state->dynamicDrawRoleIndexes[role] += count;
		} else {
			state->dynamicDrawRoleVertices[role] += count;
		}
	}

	if ( pass >= 0 && pass < GLX_RENDER_IR_PASS_COUNT ) {
		state->dynamicDrawPassDraws[pass]++;
		if ( draw.kind == DynamicDrawKind::Indexed ) {
			state->dynamicDrawPassIndexes[pass] += count;
		} else {
			state->dynamicDrawPassVertices[pass] += count;
		}
	}
}

void GLX_Executor_Reset( ExecutorState *state )
{
	if ( !state ) {
		return;
	}

	*state = {};
	state->tier = RenderProductTier::GL12;
}

void GLX_Executor_Init( ExecutorState *state, const Capabilities &caps )
{
	if ( !state ) {
		return;
	}

	GLX_Executor_Reset( state );
	state->tier = GLX_Executor_TierForCapabilities( caps );
}

void GLX_Executor_Shutdown( ExecutorState *state )
{
	GLX_Draw_Shutdown();
	GLX_Executor_Reset( state );
}

RenderProductTier GLX_Executor_TierForCapabilities( const Capabilities &caps )
{
	return caps.tier;
}

const char *GLX_Executor_TierName( const ExecutorState &state )
{
	return GLX_RenderIR_TierName( state.tier );
}

const char *GLX_Executor_ModeName( const ExecutorState &state )
{
	const TierExecutionPolicy policy = GLX_RenderIR_TierExecutionPolicy( state.tier );
	return policy.executorName;
}

static void GLX_Executor_RecordUnsupportedGL12Upload( ExecutorState *state,
	const UploadPlan &plan )
{
	if ( !state || state->tier != RenderProductTier::GL12 ) {
		return;
	}
	if ( plan.kind == UploadPlanKind::TransientStream || plan.kind == UploadPlanKind::PostProcess ) {
		state->unsupportedStreamUploads++;
	}
}

static void GLX_Executor_RecordUnsupportedGL2XUpload( ExecutorState *state,
	const UploadPlan &plan )
{
	if ( !state || state->tier != RenderProductTier::GL2X ) {
		return;
	}
	if ( plan.sync == UploadSyncPolicy::PersistentFence ||
		plan.strategy == static_cast<int>( StreamStrategy::PersistentMapped ) ) {
		state->unsupportedAdvancedUploads++;
	}
}

static qboolean GLX_Executor_IsPersistentUploadPlan( const UploadPlan &plan )
{
	return plan.sync == UploadSyncPolicy::PersistentFence ||
		plan.strategy == static_cast<int>( StreamStrategy::PersistentMapped ) ? qtrue : qfalse;
}

static qboolean GLX_Executor_IsSyncAwareUploadPlan( const UploadPlan &plan )
{
	return plan.sync == UploadSyncPolicy::FrameFence ||
		plan.sync == UploadSyncPolicy::PersistentFence ? qtrue : qfalse;
}

static void GLX_Executor_RecordUnsupportedGL3XUpload( ExecutorState *state,
	const UploadPlan &plan )
{
	if ( !state || state->tier != RenderProductTier::GL3X ) {
		return;
	}
	if ( GLX_Executor_IsPersistentUploadPlan( plan ) ) {
		state->unsupportedPersistentUploads++;
	}
}

static void GLX_Executor_RecordGL3XBufferOwnership( ExecutorState *state,
	const UploadPlan &plan )
{
	if ( !state || state->tier != RenderProductTier::GL3X ) {
		return;
	}
	if ( GLX_Executor_IsSyncAwareUploadPlan( plan ) ) {
		state->syncUploadPlans++;
	}
	if ( plan.kind == UploadPlanKind::StaticWorld ) {
		state->staticBufferProducts++;
	}
	if ( plan.kind == UploadPlanKind::TransientStream || plan.kind == UploadPlanKind::PostProcess ) {
		state->dynamicBufferProducts++;
	}
}

static void GLX_Executor_RecordUnsupportedGL41Upload( ExecutorState *state,
	const UploadPlan &plan )
{
	if ( !state || state->tier != RenderProductTier::GL41 ) {
		return;
	}
	if ( GLX_Executor_IsPersistentUploadPlan( plan ) ) {
		state->unsupportedPersistentUploads++;
	}
}

static void GLX_Executor_RecordGL41BufferOwnership( ExecutorState *state,
	const UploadPlan &plan )
{
	if ( !state || state->tier != RenderProductTier::GL41 ) {
		return;
	}
	if ( GLX_Executor_IsSyncAwareUploadPlan( plan ) ) {
		state->macModernSyncUploadPlans++;
	}
	if ( plan.kind == UploadPlanKind::StaticWorld ) {
		state->macModernStaticBufferProducts++;
	}
	if ( plan.kind == UploadPlanKind::TransientStream || plan.kind == UploadPlanKind::PostProcess ) {
		state->macModernDynamicBufferProducts++;
	}
}

static void GLX_Executor_RecordGL46HighEndProduct( ExecutorState *state,
	const UploadPlan &plan )
{
	if ( !state || state->tier != RenderProductTier::GL46 ) {
		return;
	}
	if ( GLX_Executor_IsPersistentUploadPlan( plan ) ) {
		state->highEndPersistentUploads++;
	}
	if ( GLX_Executor_IsSyncAwareUploadPlan( plan ) ) {
		state->highEndSyncUploads++;
	}
	if ( plan.kind == UploadPlanKind::StaticWorld ) {
		state->highEndStaticBufferProducts++;
		state->highEndMdiProducts++;
		state->highEndAggressiveStaticProducts++;
	}
	if ( plan.kind == UploadPlanKind::TransientStream || plan.kind == UploadPlanKind::PostProcess ) {
		state->highEndDynamicBufferProducts++;
	}
	if ( plan.kind != UploadPlanKind::NoUpload && plan.kind != UploadPlanKind::ClientMemory ) {
		state->highEndDsaProducts++;
	}
}

static void GLX_Executor_RecordUnsupportedGL12Post( ExecutorState *state,
	const PostNode &node )
{
	if ( !state || state->tier != RenderProductTier::GL12 ) {
		return;
	}
	if ( node.kind == PostNodeKind::BloomPrefinal || node.kind == PostNodeKind::BloomFinal ||
		node.kind == PostNodeKind::ToneMap || node.kind == PostNodeKind::Grade ) {
		state->unsupportedPostNodes++;
	}
}

static void GLX_Executor_RecordUnsupportedGL2XPost( ExecutorState *state,
	const PostNode &node )
{
	if ( !state || state->tier != RenderProductTier::GL2X ) {
		return;
	}
	if ( node.kind == PostNodeKind::ToneMap || node.kind == PostNodeKind::Grade ) {
		state->unsupportedPostNodes++;
	}
}

static void GLX_Executor_RecordUnsupportedGL12Output( ExecutorState *state,
	const OutputTransform &transform )
{
	if ( !state || state->tier != RenderProductTier::GL12 ) {
		return;
	}
	if ( transform.transfer != OutputTransfer::SdrSrgb &&
		transform.transfer != OutputTransfer::ScreenshotSrgb ) {
		state->unsupportedOutputTransforms++;
	}
}

static void GLX_Executor_RecordUnsupportedGL2XOutput( ExecutorState *state,
	const OutputTransform &transform )
{
	if ( !state || state->tier != RenderProductTier::GL2X ) {
		return;
	}
	if ( transform.transfer != OutputTransfer::SdrSrgb &&
		transform.transfer != OutputTransfer::ScreenshotSrgb ) {
		state->unsupportedOutputTransforms++;
	}
}

qboolean GLX_Executor_ConsumeFrameSchedule( ExecutorState *state, const FramePass *passes,
	int count )
{
	if ( !state || state->frameScheduleConsumed ) {
		GLX_Executor_RecordReject( state );
		return qfalse;
	}

	state->frameScheduleConsumed = qtrue;
	state->frameScheduleCount = count;
	GLX_RenderIR_FormatPassSchedule( passes, count, state->frameScheduleText,
		GLX_RENDER_IR_PASS_SCHEDULE_TEXT_BYTES );

	if ( !GLX_RenderIR_ValidatePassSchedule( passes, count ) ) {
		state->frameScheduleValid = qfalse;
		state->frameScheduleHash = 0;
		GLX_Executor_RecordReject( state );
		return qfalse;
	}

	state->frameScheduleValid = qtrue;
	state->frameScheduleHash = GLX_RenderIR_PassScheduleHash( passes, count );
	for ( int i = 0; i < count; i++ ) {
		state->frameSchedule[i] = passes[i];
		if ( !GLX_Executor_ConsumeFramePass( state, passes[i] ) ) {
			state->frameScheduleValid = qfalse;
			return qfalse;
		}
	}
	return qtrue;
}

qboolean GLX_Executor_ConsumeFramePass( ExecutorState *state, const FramePass &pass )
{
	const TierExecutor &executor = GLX_Executor_ForTier( state ? state->tier : RenderProductTier::GL12 );

	if ( !state || !GLX_RenderIR_TierConsumesProduct( state->tier, RenderProductKind::FramePass ) ||
		!executor.framePass( pass ) ) {
		GLX_Executor_RecordReject( state );
		return qfalse;
	}

	state->framePasses++;
	return qtrue;
}

qboolean GLX_Executor_ConsumeWorldPacket( ExecutorState *state, const WorldPacket &packet )
{
	const TierExecutor &executor = GLX_Executor_ForTier( state ? state->tier : RenderProductTier::GL12 );

	if ( !state || !GLX_RenderIR_TierConsumesProduct( state->tier, RenderProductKind::WorldPacket ) ||
		!executor.worldPacket( packet ) ) {
		GLX_Executor_RecordUnsupportedGL2XUpload( state, packet.upload );
		GLX_Executor_RecordUnsupportedGL3XUpload( state, packet.upload );
		GLX_Executor_RecordUnsupportedGL41Upload( state, packet.upload );
		GLX_Executor_RecordReject( state );
		return qfalse;
	}

	state->worldPackets++;
	if ( packet.projectedDlights.recordCount > 0 ) {
		state->worldPacketsWithProjectedDlights++;
		state->worldPacketProjectedDlightRecords +=
			static_cast<unsigned int>( packet.projectedDlights.recordCount );
	}
	state->materialPlans++;
	state->uploadPlans++;
	if ( state->tier == RenderProductTier::GL2X ) {
		state->programmableMaterialPlans++;
	}
	if ( state->tier == RenderProductTier::GL3X ) {
		state->performanceMaterialPlans++;
		GLX_Executor_RecordGL3XBufferOwnership( state, packet.upload );
	}
	if ( state->tier == RenderProductTier::GL41 ) {
		state->macModernMaterialPlans++;
		GLX_Executor_RecordGL41BufferOwnership( state, packet.upload );
	}
	if ( state->tier == RenderProductTier::GL46 ) {
		state->highEndMaterialPlans++;
		GLX_Executor_RecordGL46HighEndProduct( state, packet.upload );
	}
	return qtrue;
}

qboolean GLX_Executor_ConsumeMaterial( ExecutorState *state, const MaterialIR &material )
{
	const TierExecutor &executor = GLX_Executor_ForTier( state ? state->tier : RenderProductTier::GL12 );

	if ( !state || !GLX_RenderIR_TierConsumesProduct( state->tier, RenderProductKind::MaterialIR ) ||
		!executor.material( material ) ) {
		GLX_Executor_RecordReject( state );
		return qfalse;
	}

	state->materialPlans++;
	if ( state->tier == RenderProductTier::GL2X ) {
		state->programmableMaterialPlans++;
	}
	if ( state->tier == RenderProductTier::GL3X ) {
		state->performanceMaterialPlans++;
	}
	if ( state->tier == RenderProductTier::GL41 ) {
		state->macModernMaterialPlans++;
	}
	if ( state->tier == RenderProductTier::GL46 ) {
		state->highEndMaterialPlans++;
	}
	return qtrue;
}

qboolean GLX_Executor_ConsumeUploadPlan( ExecutorState *state, const UploadPlan &plan )
{
	const TierExecutor &executor = GLX_Executor_ForTier( state ? state->tier : RenderProductTier::GL12 );

	if ( !state || !GLX_RenderIR_TierConsumesProduct( state->tier, RenderProductKind::UploadPlan ) ||
		!executor.upload( plan ) ) {
		GLX_Executor_RecordUnsupportedGL12Upload( state, plan );
		GLX_Executor_RecordUnsupportedGL2XUpload( state, plan );
		GLX_Executor_RecordUnsupportedGL3XUpload( state, plan );
		GLX_Executor_RecordUnsupportedGL41Upload( state, plan );
		GLX_Executor_RecordReject( state );
		return qfalse;
	}

	state->uploadPlans++;
	GLX_Executor_RecordGL3XBufferOwnership( state, plan );
	GLX_Executor_RecordGL41BufferOwnership( state, plan );
	GLX_Executor_RecordGL46HighEndProduct( state, plan );
	return qtrue;
}

qboolean GLX_Executor_ConsumePostNode( ExecutorState *state, const PostNode &node )
{
	const TierExecutor &executor = GLX_Executor_ForTier( state ? state->tier : RenderProductTier::GL12 );

	if ( !state || !GLX_RenderIR_TierConsumesProduct( state->tier, RenderProductKind::PostNode ) ||
		!executor.post( node ) ) {
		GLX_Executor_RecordUnsupportedGL12Post( state, node );
		GLX_Executor_RecordUnsupportedGL2XPost( state, node );
		GLX_Executor_RecordUnsupportedGL2XOutput( state, node.output );
		GLX_Executor_RecordReject( state );
		return qfalse;
	}

	state->postNodes++;
	state->lastPostNodeHash = GLX_RenderIR_HashPostNode( node );
	if ( state->tier == RenderProductTier::GL2X ) {
		state->postprocessLiteNodes++;
	}
	if ( state->tier == RenderProductTier::GL3X ) {
		state->fboPostNodes++;
	}
	if ( state->tier == RenderProductTier::GL41 ) {
		state->macModernPostNodes++;
	}
	if ( state->tier == RenderProductTier::GL46 ) {
		state->highEndPostNodes++;
	}
	return qtrue;
}

qboolean GLX_Executor_ConsumeOutputTransform( ExecutorState *state, const OutputTransform &transform )
{
	const TierExecutor &executor = GLX_Executor_ForTier( state ? state->tier : RenderProductTier::GL12 );

	if ( !state || !GLX_RenderIR_TierConsumesProduct( state->tier, RenderProductKind::OutputTransform ) ||
		!executor.output( transform ) ) {
		GLX_Executor_RecordUnsupportedGL12Output( state, transform );
		GLX_Executor_RecordUnsupportedGL2XOutput( state, transform );
		GLX_Executor_RecordReject( state );
		return qfalse;
	}

	state->outputTransforms++;
	state->lastOutputTransformHash = GLX_RenderIR_HashOutputTransform( transform );
	return qtrue;
}

static void GLX_Executor_RecordAcceptedDynamicDraw( ExecutorState *state,
	const DynamicDraw &draw );

static qboolean GLX_Executor_AcceptDynamicDraw( ExecutorState *state,
	const DynamicDraw &draw )
{
	if ( !state || !GLX_RenderIR_TierConsumesProduct( state->tier, RenderProductKind::DynamicDraw ) ||
		!GLX_RenderIR_TierSupportsDynamicDraw( state->tier, draw ) ) {
		GLX_Executor_RecordUnsupportedGL12Upload( state, draw.upload );
		GLX_Executor_RecordUnsupportedGL2XUpload( state, draw.upload );
		GLX_Executor_RecordUnsupportedGL3XUpload( state, draw.upload );
		GLX_Executor_RecordUnsupportedGL41Upload( state, draw.upload );
		GLX_Executor_RecordReject( state );
		return qfalse;
	}

	GLX_Executor_RecordAcceptedDynamicDraw( state, draw );
	return qtrue;
}

static void GLX_Executor_RecordAcceptedDynamicDraw( ExecutorState *state,
	const DynamicDraw &draw )
{
	state->dynamicDraws++;
	if ( draw.projectedDlights.recordCount > 0 ) {
		state->dynamicDrawsWithProjectedDlights++;
		state->dynamicDrawProjectedDlightRecords +=
			static_cast<unsigned int>( draw.projectedDlights.recordCount );
	}
	GLX_Executor_RecordDynamicDrawAccounting( state, draw );
	if ( state->tier == RenderProductTier::GL12 ) {
		state->fixedFunctionDraws++;
		if ( draw.upload.kind == UploadPlanKind::NoUpload || draw.upload.kind == UploadPlanKind::ClientMemory ) {
			state->clientMemoryDraws++;
		}
	}
	if ( state->tier == RenderProductTier::GL2X ) {
		state->programmableDraws++;
		if ( draw.upload.kind == UploadPlanKind::TransientStream ) {
			state->streamUploadDraws++;
		}
	}
	if ( state->tier == RenderProductTier::GL3X ) {
		state->performanceDraws++;
		GLX_Executor_RecordGL3XBufferOwnership( state, draw.upload );
	}
	if ( state->tier == RenderProductTier::GL41 ) {
		state->macModernDraws++;
		GLX_Executor_RecordGL41BufferOwnership( state, draw.upload );
	}
	if ( state->tier == RenderProductTier::GL46 ) {
		state->highEndDraws++;
		GLX_Executor_RecordGL46HighEndProduct( state, draw.upload );
	}
	if ( draw.kind == DynamicDrawKind::Indexed ) {
		state->dynamicIndexes += static_cast<unsigned int>( draw.count );
	} else {
		state->dynamicVertices += static_cast<unsigned int>( draw.count );
	}
}

qboolean GLX_Executor_ConsumeDynamicDraw( ExecutorState *state, const DynamicDraw &draw )
{
	return GLX_Executor_AcceptDynamicDraw( state, draw );
}

qboolean GLX_Executor_ExecuteDynamicDraw( ExecutorState *state, const DynamicDraw &draw )
{
	if ( !state || !GLX_RenderIR_TierConsumesProduct( state->tier, RenderProductKind::DynamicDraw ) ||
		!GLX_RenderIR_TierSupportsDynamicDraw( state->tier, draw ) ) {
		GLX_Executor_RecordUnsupportedGL12Upload( state, draw.upload );
		GLX_Executor_RecordUnsupportedGL2XUpload( state, draw.upload );
		GLX_Executor_RecordUnsupportedGL3XUpload( state, draw.upload );
		GLX_Executor_RecordUnsupportedGL41Upload( state, draw.upload );
		GLX_Executor_RecordReject( state );
		return qfalse;
	}
	if ( !GLX_Executor_SubmitDynamicDraw( draw ) ) {
		GLX_Executor_RecordReject( state );
		return qfalse;
	}
	// the draw is already validated for this tier; record accounting without
	// re-running tier consume/support validation a third time
	GLX_Executor_RecordAcceptedDynamicDraw( state, draw );
	return qtrue;
}

qboolean GLX_Executor_ConsumeProjectedDlightDynamicMdiPlan( ExecutorState *state,
	const ProjectedDlightDynamicMdiPlan &plan )
{
	if ( !state || state->tier != RenderProductTier::GL46 ||
		!plan.valid || !plan.eligible ||
		!GLX_RenderIR_TierSupportsUploadPlan( state->tier,
			plan.commandUploadPlan ) ) {
		GLX_Executor_RecordReject( state );
		return qfalse;
	}

	state->highEndProjectedDlightMdiCandidates +=
		static_cast<unsigned int>( plan.drawCount > 0 ? plan.drawCount : 0 );
	state->highEndProjectedDlightMdiRecords += plan.projectedRecordCount;
	state->highEndProjectedDlightMdiIndexes += plan.indexCount;
	return qtrue;
}

void GLX_Executor_PrintInfo( const ExecutorState &state )
{
	const TierExecutionPolicy policy = GLX_RenderIR_TierExecutionPolicy( state.tier );
	const int genericRole = static_cast<int>( DynamicDrawRole::Generic );
	const int dlightRole = static_cast<int>( DynamicDrawRole::DynamicLight );
	const int shadowRole = static_cast<int>( DynamicDrawRole::Shadow );
	const int beamRole = static_cast<int>( DynamicDrawRole::Beam );
	const int postRole = static_cast<int>( DynamicDrawRole::PostProcess );
	const int dlightPass = static_cast<int>( FramePassKind::DynamicLights );
	const int scenePass = static_cast<int>( FramePassKind::DynamicScene );
	const int postPass = static_cast<int>( FramePassKind::PostProcess );
	const unsigned int focusedPassDraws = state.dynamicDrawPassDraws[dlightPass] +
		state.dynamicDrawPassDraws[scenePass] + state.dynamicDrawPassDraws[postPass];
	const unsigned int focusedPassIndexes = state.dynamicDrawPassIndexes[dlightPass] +
		state.dynamicDrawPassIndexes[scenePass] + state.dynamicDrawPassIndexes[postPass];
	const unsigned int focusedPassVertices = state.dynamicDrawPassVertices[dlightPass] +
		state.dynamicDrawPassVertices[scenePass] + state.dynamicDrawPassVertices[postPass];
	const unsigned int otherPassDraws = state.dynamicDraws > focusedPassDraws ?
		state.dynamicDraws - focusedPassDraws : 0u;
	const unsigned int otherPassIndexes = state.dynamicIndexes > focusedPassIndexes ?
		state.dynamicIndexes - focusedPassIndexes : 0u;
	const unsigned int otherPassVertices = state.dynamicVertices > focusedPassVertices ?
		state.dynamicVertices - focusedPassVertices : 0u;

	RI().Printf( PRINT_ALL, "  render IR executor: %s (%s)\n",
		GLX_Executor_TierName( state ), policy.executorName );
	if ( state.tier == RenderProductTier::GL12 ) {
		RI().Printf( PRINT_ALL, "  GL12 fixed-function executor: active yes, client-memory draws %s, stream uploads %s, material compiler %s, modern post chain %s\n",
			BoolName( policy.clientMemoryDraws ), BoolName( policy.streamUploads ),
			BoolName( policy.materialCompiler ), BoolName( policy.modernPostChain ) );
		RI().Printf( PRINT_ALL, "  GL12 fixed-function support: lightmaps %s, multitexture %s, fog %s, sprites %s, beams %s, dynamic lights %s, stencil shadows if available %s, screenshots %s, demos %s\n",
			BoolName( policy.lightmaps ), BoolName( policy.multitexture ),
			BoolName( policy.fog ), BoolName( policy.sprites ),
			BoolName( policy.beams ), BoolName( policy.dynamicLights ),
			BoolName( policy.stencilShadowsIfAvailable ), BoolName( policy.screenshots ),
			BoolName( policy.demos ) );
		RI().Printf( PRINT_ALL, "  GL12 fixed-function unavailable: %s\n", policy.unavailable );
		RI().Printf( PRINT_ALL, "  GL12 fixed-function counters: draws %u, client-memory %u, unsupported stream uploads %u, unsupported post nodes %u, unsupported outputs %u\n",
			state.fixedFunctionDraws, state.clientMemoryDraws,
			state.unsupportedStreamUploads, state.unsupportedPostNodes,
			state.unsupportedOutputTransforms );
	}
	if ( state.tier == RenderProductTier::GL2X ) {
		RI().Printf( PRINT_ALL, "  GL2X programmable executor: active yes, client-memory fallback %s, stream uploads %s, material compiler %s, postprocess-lite %s, modern post chain %s, scene-linear output %s\n",
			BoolName( policy.clientMemoryDraws ), BoolName( policy.streamUploads ),
			BoolName( policy.materialCompiler ), BoolName( policy.postProcessLite ),
			BoolName( policy.modernPostChain ), BoolName( policy.sceneLinearOutput ) );
		RI().Printf( PRINT_ALL, "  GL2X programmable support: common materials %s, dynamic entities %s, lightmaps %s, multitexture %s, fog %s, sprites %s, beams %s, screenshots %s, demos %s\n",
			BoolName( policy.commonMaterials ), BoolName( policy.dynamicEntities ),
			BoolName( policy.lightmaps ), BoolName( policy.multitexture ),
			BoolName( policy.fog ), BoolName( policy.sprites ),
			BoolName( policy.beams ), BoolName( policy.screenshots ),
			BoolName( policy.demos ) );
		RI().Printf( PRINT_ALL, "  GL2X programmable unavailable: %s\n", policy.unavailable );
		RI().Printf( PRINT_ALL, "  GL2X programmable counters: draws %u, stream-upload draws %u, material plans %u, post-lite nodes %u, unsupported advanced uploads %u, unsupported post nodes %u, unsupported outputs %u\n",
			state.programmableDraws, state.streamUploadDraws,
			state.programmableMaterialPlans, state.postprocessLiteNodes,
			state.unsupportedAdvancedUploads, state.unsupportedPostNodes,
			state.unsupportedOutputTransforms );
	}
	if ( state.tier == RenderProductTier::GL3X ) {
		RI().Printf( PRINT_ALL, "  GL3X performance executor: active yes, FBO postprocess %s, UBO frame/object constants %s, timer queries %s, sync-aware uploads %s, static buffer ownership %s, dynamic buffer ownership %s, persistent uploads %s, indirect submission %s, direct state access %s\n",
			BoolName( policy.fboPostProcess ),
			BoolName( policy.uboFrameObjectConstants ),
			BoolName( policy.timerQueries ),
			BoolName( policy.syncAwareUploads ),
			BoolName( policy.staticBufferOwnership ),
			BoolName( policy.dynamicBufferOwnership ),
			BoolName( policy.persistentUploads ),
			BoolName( policy.indirectSubmission ),
			BoolName( policy.directStateAccess ) );
		RI().Printf( PRINT_ALL, "  GL3X performance support: material compiler %s, common materials %s, dynamic entities %s, modern post chain %s, scene-linear output %s, screenshots %s, demos %s\n",
			BoolName( policy.materialCompiler ),
			BoolName( policy.commonMaterials ),
			BoolName( policy.dynamicEntities ),
			BoolName( policy.modernPostChain ),
			BoolName( policy.sceneLinearOutput ),
			BoolName( policy.screenshots ),
			BoolName( policy.demos ) );
		RI().Printf( PRINT_ALL, "  GL3X performance unavailable: %s\n", policy.unavailable );
		RI().Printf( PRINT_ALL, "  GL3X performance counters: draws %u, sync uploads %u, static buffers %u, dynamic buffers %u, post nodes %u, material plans %u, unsupported persistent uploads %u\n",
			state.performanceDraws,
			state.syncUploadPlans,
			state.staticBufferProducts,
			state.dynamicBufferProducts,
			state.fboPostNodes,
			state.performanceMaterialPlans,
			state.unsupportedPersistentUploads );
	}
	if ( state.tier == RenderProductTier::GL41 ) {
		RI().Printf( PRINT_ALL, "  GL41 mac-modern executor: active yes, FBO postprocess %s, UBO frame/object constants %s, timer queries %s, sync-aware uploads %s, static buffer ownership %s, dynamic buffer ownership %s, macOS 4.1 ceiling %s\n",
			BoolName( policy.fboPostProcess ),
			BoolName( policy.uboFrameObjectConstants ),
			BoolName( policy.timerQueries ),
			BoolName( policy.syncAwareUploads ),
			BoolName( policy.staticBufferOwnership ),
			BoolName( policy.dynamicBufferOwnership ),
			BoolName( policy.macOS41Ceiling ) );
		RI().Printf( PRINT_ALL, "  GL41 mac-modern support: material compiler %s, common materials %s, dynamic entities %s, modern post chain %s, scene-linear output %s, high-quality SDR %s, optional hardware HDR %s, screenshots %s, demos %s\n",
			BoolName( policy.materialCompiler ),
			BoolName( policy.commonMaterials ),
			BoolName( policy.dynamicEntities ),
			BoolName( policy.modernPostChain ),
			BoolName( policy.sceneLinearOutput ),
			BoolName( policy.highQualitySdrOutput ),
			BoolName( policy.optionalHardwareHdrOutput ),
			BoolName( policy.screenshots ),
			BoolName( policy.demos ) );
		RI().Printf( PRINT_ALL, "  GL41 mac-modern GL4+ requirements: debug output %s, buffer storage %s, direct state access %s, multi-draw indirect %s, persistent uploads %s\n",
			BoolName( policy.debugOutputRequired ),
			BoolName( policy.bufferStorageRequired ),
			BoolName( policy.directStateAccessRequired ),
			BoolName( policy.multiDrawIndirectRequired ),
			BoolName( policy.persistentUploads ) );
		RI().Printf( PRINT_ALL, "  GL41 mac-modern unavailable: %s\n", policy.unavailable );
		RI().Printf( PRINT_ALL, "  GL41 mac-modern counters: draws %u, sync uploads %u, static buffers %u, dynamic buffers %u, post nodes %u, material plans %u, unsupported persistent uploads %u\n",
			state.macModernDraws,
			state.macModernSyncUploadPlans,
			state.macModernStaticBufferProducts,
			state.macModernDynamicBufferProducts,
			state.macModernPostNodes,
			state.macModernMaterialPlans,
			state.unsupportedPersistentUploads );
	}
	if ( state.tier == RenderProductTier::GL46 ) {
		RI().Printf( PRINT_ALL, "  GL46 high-end executor: active yes, persistent uploads %s, buffer storage uploads %s, sync-heavy streaming %s, direct state access %s, multi-draw indirect %s, aggressive static-world submission %s, detailed GPU counters %s\n",
			BoolName( policy.persistentUploads ),
			BoolName( policy.bufferStorageUploads ),
			BoolName( policy.syncHeavyStreaming ),
			BoolName( policy.directStateAccess ),
			BoolName( policy.multiDrawIndirectSubmission ),
			BoolName( policy.aggressiveStaticWorldSubmission ),
			BoolName( policy.detailedGpuCounters ) );
		RI().Printf( PRINT_ALL, "  GL46 high-end support: material compiler %s, common materials %s, dynamic entities %s, modern post chain %s, scene-linear output %s, hardware HDR output %s, screenshots %s, demos %s\n",
			BoolName( policy.materialCompiler ),
			BoolName( policy.commonMaterials ),
			BoolName( policy.dynamicEntities ),
			BoolName( policy.modernPostChain ),
			BoolName( policy.sceneLinearOutput ),
			BoolName( policy.optionalHardwareHdrOutput ),
			BoolName( policy.screenshots ),
			BoolName( policy.demos ) );
		RI().Printf( PRINT_ALL, "  GL46 high-end requirements: debug output %s, buffer storage %s, direct state access %s, multi-draw indirect %s\n",
			BoolName( policy.debugOutputRequired ),
			BoolName( policy.bufferStorageRequired ),
			BoolName( policy.directStateAccessRequired ),
			BoolName( policy.multiDrawIndirectRequired ) );
		RI().Printf( PRINT_ALL, "  GL46 high-end counters: draws %u, persistent uploads %u, sync uploads %u, DSA products %u, MDI products %u, aggressive static products %u, post nodes %u, material plans %u\n",
			state.highEndDraws,
			state.highEndPersistentUploads,
			state.highEndSyncUploads,
			state.highEndDsaProducts,
			state.highEndMdiProducts,
			state.highEndAggressiveStaticProducts,
			state.highEndPostNodes,
			state.highEndMaterialPlans );
		RI().Printf( PRINT_ALL, "  GL46 projected dlight MDI candidates: draws %u, records %u, indexes %u\n",
			state.highEndProjectedDlightMdiCandidates,
			state.highEndProjectedDlightMdiRecords,
			state.highEndProjectedDlightMdiIndexes );
	}
	RI().Printf( PRINT_ALL, "  pass schedule: %s %i/%08x %s\n",
		state.frameScheduleValid ? "valid" : "invalid",
		state.frameScheduleCount,
		state.frameScheduleHash,
		state.frameScheduleText[0] ? state.frameScheduleText : "none" );
	RI().Printf( PRINT_ALL, "  render IR products: passes %u, world packets %u, projected world packets %u/%u lights, dynamic draws %u, projected dynamic draws %u/%u lights, materials %u, uploads %u, post nodes %u, outputs %u, rejects %u\n",
		state.framePasses,
		state.worldPackets,
		state.worldPacketsWithProjectedDlights,
		state.worldPacketProjectedDlightRecords,
		state.dynamicDraws,
		state.dynamicDrawsWithProjectedDlights,
		state.dynamicDrawProjectedDlightRecords,
		state.materialPlans,
		state.uploadPlans,
		state.postNodes,
		state.outputTransforms,
		state.rejectedProducts );
	RI().Printf( PRINT_ALL, "  render IR dynamic roles: generic %u/%u/%u, dlight %u/%u/%u, shadow %u/%u/%u, beam %u/%u/%u, post %u/%u/%u\n",
		state.dynamicDrawRoleDraws[genericRole],
		state.dynamicDrawRoleIndexes[genericRole],
		state.dynamicDrawRoleVertices[genericRole],
		state.dynamicDrawRoleDraws[dlightRole],
		state.dynamicDrawRoleIndexes[dlightRole],
		state.dynamicDrawRoleVertices[dlightRole],
		state.dynamicDrawRoleDraws[shadowRole],
		state.dynamicDrawRoleIndexes[shadowRole],
		state.dynamicDrawRoleVertices[shadowRole],
		state.dynamicDrawRoleDraws[beamRole],
		state.dynamicDrawRoleIndexes[beamRole],
		state.dynamicDrawRoleVertices[beamRole],
		state.dynamicDrawRoleDraws[postRole],
		state.dynamicDrawRoleIndexes[postRole],
		state.dynamicDrawRoleVertices[postRole] );
	RI().Printf( PRINT_ALL, "  render IR dynamic passes: dlight %u/%u/%u, scene %u/%u/%u, post %u/%u/%u, other %u/%u/%u\n",
		state.dynamicDrawPassDraws[dlightPass],
		state.dynamicDrawPassIndexes[dlightPass],
		state.dynamicDrawPassVertices[dlightPass],
		state.dynamicDrawPassDraws[scenePass],
		state.dynamicDrawPassIndexes[scenePass],
		state.dynamicDrawPassVertices[scenePass],
		state.dynamicDrawPassDraws[postPass],
		state.dynamicDrawPassIndexes[postPass],
		state.dynamicDrawPassVertices[postPass],
		otherPassDraws,
		otherPassIndexes,
		otherPassVertices );
}

} // namespace glx
