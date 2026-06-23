#ifndef GLX_EXECUTOR_H
#define GLX_EXECUTOR_H

#include "glx_local.h"
#include "glx_render_ir.h"

namespace glx {

struct ExecutorState {
	RenderProductTier tier;
	FramePass frameSchedule[GLX_RENDER_IR_PASS_COUNT];
	int frameScheduleCount;
	qboolean frameScheduleValid;
	qboolean frameScheduleConsumed;
	unsigned int frameScheduleHash;
	char frameScheduleText[GLX_RENDER_IR_PASS_SCHEDULE_TEXT_BYTES];
	unsigned int framePasses;
	unsigned int worldPackets;
	unsigned int worldPacketsWithProjectedDlights;
	unsigned int worldPacketProjectedDlightRecords;
	unsigned int dynamicDraws;
	unsigned int dynamicDrawsWithProjectedDlights;
	unsigned int dynamicDrawProjectedDlightRecords;
	unsigned int dynamicIndexes;
	unsigned int dynamicVertices;
	unsigned int dynamicDrawRoleDraws[GLX_RENDER_IR_DYNAMIC_DRAW_ROLE_COUNT];
	unsigned int dynamicDrawRoleIndexes[GLX_RENDER_IR_DYNAMIC_DRAW_ROLE_COUNT];
	unsigned int dynamicDrawRoleVertices[GLX_RENDER_IR_DYNAMIC_DRAW_ROLE_COUNT];
	unsigned int dynamicDrawPassDraws[GLX_RENDER_IR_PASS_COUNT];
	unsigned int dynamicDrawPassIndexes[GLX_RENDER_IR_PASS_COUNT];
	unsigned int dynamicDrawPassVertices[GLX_RENDER_IR_PASS_COUNT];
	unsigned int materialPlans;
	unsigned int uploadPlans;
	unsigned int postNodes;
	unsigned int outputTransforms;
	unsigned int lastPostNodeHash;
	unsigned int lastOutputTransformHash;
	unsigned int rejectedProducts;
	unsigned int fixedFunctionDraws;
	unsigned int clientMemoryDraws;
	unsigned int programmableDraws;
	unsigned int streamUploadDraws;
	unsigned int programmableMaterialPlans;
	unsigned int postprocessLiteNodes;
	unsigned int performanceDraws;
	unsigned int syncUploadPlans;
	unsigned int staticBufferProducts;
	unsigned int dynamicBufferProducts;
	unsigned int fboPostNodes;
	unsigned int performanceMaterialPlans;
	unsigned int macModernDraws;
	unsigned int macModernSyncUploadPlans;
	unsigned int macModernStaticBufferProducts;
	unsigned int macModernDynamicBufferProducts;
	unsigned int macModernPostNodes;
	unsigned int macModernMaterialPlans;
	unsigned int highEndDraws;
	unsigned int highEndPersistentUploads;
	unsigned int highEndSyncUploads;
	unsigned int highEndStaticBufferProducts;
	unsigned int highEndDynamicBufferProducts;
	unsigned int highEndDsaProducts;
	unsigned int highEndMdiProducts;
	unsigned int highEndAggressiveStaticProducts;
	unsigned int highEndPostNodes;
	unsigned int highEndMaterialPlans;
	unsigned int highEndProjectedDlightMdiCandidates;
	unsigned int highEndProjectedDlightMdiRecords;
	unsigned int highEndProjectedDlightMdiIndexes;
	unsigned int unsupportedStreamUploads;
	unsigned int unsupportedAdvancedUploads;
	unsigned int unsupportedPersistentUploads;
	unsigned int unsupportedPostNodes;
	unsigned int unsupportedOutputTransforms;
};

void GLX_Executor_Reset( ExecutorState *state );
void GLX_Executor_Init( ExecutorState *state, const Capabilities &caps );
void GLX_Executor_Shutdown( ExecutorState *state );
RenderProductTier GLX_Executor_TierForCapabilities( const Capabilities &caps );
const char *GLX_Executor_TierName( const ExecutorState &state );
const char *GLX_Executor_ModeName( const ExecutorState &state );
qboolean GLX_Executor_ConsumeFrameSchedule( ExecutorState *state, const FramePass *passes, int count );
qboolean GLX_Executor_ConsumeFramePass( ExecutorState *state, const FramePass &pass );
qboolean GLX_Executor_ConsumeWorldPacket( ExecutorState *state, const WorldPacket &packet );
qboolean GLX_Executor_ConsumeMaterial( ExecutorState *state, const MaterialIR &material );
qboolean GLX_Executor_ConsumeUploadPlan( ExecutorState *state, const UploadPlan &plan );
qboolean GLX_Executor_ConsumePostNode( ExecutorState *state, const PostNode &node );
qboolean GLX_Executor_ConsumeOutputTransform( ExecutorState *state, const OutputTransform &transform );
qboolean GLX_Executor_ConsumeDynamicDraw( ExecutorState *state, const DynamicDraw &draw );
qboolean GLX_Executor_ExecuteDynamicDraw( ExecutorState *state, const DynamicDraw &draw );
qboolean GLX_Executor_ConsumeProjectedDlightDynamicMdiPlan( ExecutorState *state,
	const ProjectedDlightDynamicMdiPlan &plan );
void GLX_Executor_PrintInfo( const ExecutorState &state );

} // namespace glx

#endif // GLX_EXECUTOR_H
