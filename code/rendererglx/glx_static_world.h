#ifndef GLX_STATIC_WORLD_H
#define GLX_STATIC_WORLD_H

#include "glx_local.h"

namespace glx {

static constexpr int GLX_STATIC_WORLD_PACKET_LIMIT = 512;
static constexpr int GLX_STATIC_WORLD_ITEM_LOOKUP_LIMIT = 65536;
static constexpr int GLX_STATIC_WORLD_QUEUE_COMMAND_SPAN_LIMIT = 64;

struct StaticWorldPacketStats {
	char shaderName[MAX_QPATH];
	int sort;
	int surfaces;
	int vertexes;
	int indexes;
	int firstItem;
	int itemCount;
	int vertexOffset;
	int vertexBytes;
	int indexOffset;
	int indexBytes;
	int shaderStagePasses;
	int flags;
	unsigned int queueFullRuns;
	unsigned int queuePartialRuns;
	unsigned int drawFullRuns;
	unsigned int drawPartialRuns;
	unsigned int drawManifestRuns;
	unsigned int drawIndexes;
};

struct StaticWorldIndirectCommandStats {
	unsigned int count;
	unsigned int instanceCount;
	unsigned int firstIndex;
	int baseVertex;
	unsigned int baseInstance;
};

struct StaticWorldCommandSpanStats {
	int firstCommand;
	int commandCount;
	unsigned int indexes;
	int firstPacket;
	int lastPacket;
	int firstSourceRun;
	int lastSourceRun;
};

struct StaticWorldStats {
	cvar_t *r_glxWorldRenderer;
	cvar_t *r_glxStaticWorldArena;
	cvar_t *r_glxStaticWorldArenaDraw;
	cvar_t *r_glxStaticWorldDraw;
	cvar_t *r_glxStaticWorldSoftDraw;
	cvar_t *r_glxStaticWorldDrawPolicy;
	cvar_t *r_glxStaticWorldMultiDraw;
	cvar_t *r_glxStaticWorldPacketBatch;
	cvar_t *r_glxStaticWorldIndirectBuffer;
	cvar_t *r_glxStaticWorldIndirectDraw;
	cvar_t *r_glxStaticWorldMultiDrawIndirect;
	cvar_t *r_glxStaticWorldMultiDrawIndirectCompact;
	cvar_t *r_glxStaticWorldMultiDrawIndirectSpans;
	qboolean drawIndirectAvailable;
	qboolean multiDrawIndirectAvailable;
	int surfaces;
	int vertexes;
	int indexes;
	int vertexBytes;
	int indexBytes;
	int batches;
	int largestBatchSurfaces;
	int faceSurfaces;
	int gridSurfaces;
	int triangleSurfaces;
	int shaderStagePasses;
	int maxShaderStages;
	unsigned int generations;
	unsigned int queueBatches;
	unsigned int queueItems;
	unsigned int queueVertexes;
	unsigned int queueIndexes;
	unsigned int queueDeviceRuns;
	unsigned int queueDeviceIndexes;
	unsigned int queueSoftIndexes;
	unsigned int queueDeviceFullPacketRuns;
	unsigned int queueDevicePartialPacketRuns;
	unsigned int queueDevicePacketMisses;
	unsigned int queueDeviceItemMismatches;
	unsigned int queueDeviceIndirectCommandRuns;
	unsigned int queueDeviceIndirectCommands;
	unsigned int queueDeviceIndirectIndexes;
	unsigned int queueDeviceIndirectBreaks;
	unsigned int largestQueueIndexes;
	unsigned int largestDeviceRunIndexes;
	unsigned int largestIndirectCommandRun;
	unsigned int largestIndirectCommandSpanIndexes;
	unsigned int lastQueueItems;
	unsigned int lastQueueVertexes;
	unsigned int lastQueueIndexes;
	unsigned int lastQueueDeviceRuns;
	unsigned int lastQueueDeviceIndexes;
	unsigned int lastQueueSoftIndexes;
	unsigned int lastQueueDeviceFullPacketRuns;
	unsigned int lastQueueDevicePartialPacketRuns;
	unsigned int lastQueueDevicePacketMisses;
	unsigned int lastQueueDeviceItemMismatches;
	unsigned int lastQueueLargestDeviceRunIndexes;
	unsigned int lastQueueDeviceIndirectCommandRuns;
	unsigned int lastQueueDeviceIndirectCommands;
	unsigned int lastQueueDeviceIndirectIndexes;
	unsigned int lastQueueDeviceIndirectBreaks;
	unsigned int lastQueueLargestIndirectCommandRun;
	unsigned int lastQueueLargestIndirectCommandSpanIndexes;
	unsigned int lastQueueCommandSpanCount;
	unsigned int lastQueueCommandSpanOverflow;
	StaticWorldCommandSpanStats lastQueueCommandSpans[GLX_STATIC_WORLD_QUEUE_COMMAND_SPAN_LIMIT];
	int packetCount;
	int packetOverflow;
	int packetSurfaces;
	int packetVertexes;
	int packetIndexes;
	int packetShaderStagePasses;
	StaticWorldPacketStats packets[GLX_STATIC_WORLD_PACKET_LIMIT];
	StaticWorldPacketStats largestPacket;
	int itemToPacket[GLX_STATIC_WORLD_ITEM_LOOKUP_LIMIT + 1];
	int itemLookupMaxItem;
	unsigned int itemLookupMappedItems;
	unsigned int itemLookupOverflows;
	unsigned int itemLookupHits;
	unsigned int itemLookupMisses;
	unsigned int itemLookupFallbacks;
	unsigned int itemLookupMismatches;
	StaticWorldIndirectCommandStats indirectPackets[GLX_STATIC_WORLD_PACKET_LIMIT];
	unsigned int indirectPacketCount;
	unsigned int indirectPacketIndexes;
	unsigned int indirectPacketBytes;
	unsigned int indirectPacketInvalid;
	unsigned int indirectPacketMisaligned;
	int indirectPacketToCommand[GLX_STATIC_WORLD_PACKET_LIMIT];
	int indirectCommandToPacket[GLX_STATIC_WORLD_PACKET_LIMIT];
	GLuint indirectCommandBuffer;
	GLuint indirectCompactCommandBuffer;
	GLuint indirectBufferBinding;
	unsigned int indirectBufferBuilds;
	unsigned int indirectBufferSkips;
	unsigned int indirectBufferUnsupported;
	unsigned int indirectBufferFailures;
	unsigned int indirectBufferCommands;
	unsigned int indirectBufferBytes;
	unsigned int indirectCompactBufferBytes;
	unsigned int indirectCompactBufferCapacityBytes;
	qboolean indirectBufferReady;
	qboolean indirectBufferBindingKnown;
	unsigned int indirectBufferBindingQueries;
	unsigned int indirectBufferBindingCacheHits;
	unsigned int indirectBufferBindingRestores;
	unsigned int indirectDrawAttempts;
	unsigned int indirectDrawCalls;
	unsigned int indirectDrawIndexes;
	unsigned int indirectDrawFallbacks;
	unsigned int indirectDrawSkips;
	unsigned int indirectDrawNoCommand;
	unsigned int indirectDrawErrors;
	GLuint arenaVertexBuffer;
	GLuint arenaIndexBuffer;
	int arenaVertexBytes;
	int arenaIndexBytes;
	unsigned int arenaBuilds;
	unsigned int arenaSkips;
	unsigned int arenaFailures;
	unsigned int arenaVertexBinds;
	unsigned int arenaIndexBinds;
	unsigned int arenaDrawSkips;
	unsigned int drawAttempts;
	unsigned int drawCalls;
	unsigned int drawFallbacks;
	unsigned int drawPolicySkips;
	unsigned int drawIndexes;
	unsigned int drawPacketHits;
	unsigned int drawPacketFullHits;
	unsigned int drawPacketPartialHits;
	unsigned int drawPacketMisses;
	unsigned int drawPacketItemMismatches;
	unsigned int drawArenaCalls;
	unsigned int drawLegacyBufferCalls;
	unsigned int drawManifestPacketCalls;
	unsigned int drawManifestPacketIndexes;
	unsigned int softDrawAttempts;
	unsigned int softDrawCalls;
	unsigned int softDrawFallbacks;
	unsigned int softDrawIndexes;
	unsigned int largestSoftDrawIndexes;
	unsigned int largestDrawIndexes;
	unsigned int lastDrawOffset;
	unsigned int lastDrawIndexes;
	unsigned int multiDrawAttempts;
	unsigned int multiDrawCalls;
	unsigned int multiDrawFallbacks;
	unsigned int multiDrawRuns;
	unsigned int multiDrawIndexes;
	unsigned int multiDrawFilteredAttempts;
	unsigned int multiDrawFilteredBatches;
	unsigned int multiDrawFilteredRuns;
	unsigned int multiDrawFilteredCandidates;
	unsigned int multiDrawFilteredSkips;
	unsigned int multiDrawFilteredOrderBarriers;
	unsigned int multiDrawFilteredInvalidBarriers;
	unsigned int multiDrawFilteredPolicyBarriers;
	unsigned int multiDrawFilteredLastBarrierReason;
	int multiDrawFilteredLastBarrierRun;
	unsigned int packetBatchAttempts;
	unsigned int packetBatchBatches;
	unsigned int packetBatchRuns;
	unsigned int packetBatchIndexes;
	unsigned int packetBatchFallbackRuns;
	unsigned int packetBatchSingleRuns;
	unsigned int packetBatchLastSegments;
	unsigned int packetBatchLastPacketRuns;
	unsigned int packetBatchLastFallbackRuns;
	unsigned int packetBatchLastSingles;
	unsigned int packetBatchLargestSegments;
	unsigned int multiDrawIndirectAttempts;
	unsigned int multiDrawIndirectCalls;
	unsigned int multiDrawIndirectStaticCalls;
	unsigned int multiDrawIndirectCompactCalls;
	unsigned int multiDrawIndirectRuns;
	unsigned int multiDrawIndirectIndexes;
	unsigned int multiDrawIndirectFallbacks;
	unsigned int multiDrawIndirectSkips;
	unsigned int multiDrawIndirectErrors;
	unsigned int multiDrawIndirectLargestRun;
	unsigned int multiDrawIndirectLargestIndexes;
	unsigned int multiDrawIndirectLastRuns;
	unsigned int multiDrawIndirectLastIndexes;
	unsigned int multiDrawIndirectLastSource;
	int multiDrawIndirectLastFirstCommand;
	unsigned int multiDrawIndirectCompactUploads;
	unsigned int multiDrawIndirectCompactBytes;
	unsigned int multiDrawIndirectCompactOrphans;
	unsigned int multiDrawIndirectCompactSubDatas;
	unsigned int multiDrawIndirectCompactGrows;
	unsigned int multiDrawIndirectCompactFailures;
	unsigned int multiDrawIndirectLastRejectReason;
	unsigned int multiDrawIndirectLastRejectRuns;
	unsigned int multiDrawIndirectLastRejectIndexes;
	int multiDrawIndirectLastRejectCommand;
	unsigned int multiDrawIndirectSpanAttempts;
	unsigned int multiDrawIndirectSpanBatches;
	unsigned int multiDrawIndirectSpanMdiRuns;
	unsigned int multiDrawIndirectSpanFallbackRuns;
	unsigned int multiDrawIndirectSpanSingles;
	unsigned int multiDrawIndirectSpanSingleDraws;
	unsigned int multiDrawIndirectSpanSingleIndirectDraws;
	unsigned int multiDrawIndirectSpanLastSegments;
	unsigned int multiDrawIndirectSpanLastMdiRuns;
	unsigned int multiDrawIndirectSpanLastFallbackRuns;
	unsigned int multiDrawIndirectSpanLastSingles;
	unsigned int multiDrawIndirectSpanLargestSegments;
	unsigned int multiDrawIndirectCommandSpanAttempts;
	unsigned int multiDrawIndirectCommandSpanBatches;
	unsigned int multiDrawIndirectCommandSpanMdiRuns;
	unsigned int multiDrawIndirectCommandSpanFallbackRuns;
	unsigned int multiDrawIndirectCommandSpanSingletons;
	unsigned int multiDrawIndirectCommandSpanSingletonDraws;
	unsigned int multiDrawIndirectCommandSpanSingletonIndirectDraws;
	unsigned int multiDrawIndirectCommandSpanLastSegments;
	unsigned int multiDrawIndirectCommandSpanLastMdiRuns;
	unsigned int multiDrawIndirectCommandSpanLastFallbackRuns;
	unsigned int multiDrawIndirectCommandSpanLastSingletonDraws;
	unsigned int multiDrawIndirectCommandSpanLargestSegments;
	unsigned int multiDrawIndirectUnsupported;
	unsigned int multiDrawIndirectShortBatches;
	unsigned int multiDrawIndirectNonManifest;
	unsigned int multiDrawIndirectMissingCommand;
	unsigned int multiDrawIndirectNonContiguous;
	qboolean arenaReady;
};

void GLX_StaticWorld_RegisterCvars( StaticWorldStats *stats );
void GLX_StaticWorld_SetCapabilities( StaticWorldStats *stats,
	qboolean drawIndirectAvailable, qboolean multiDrawIndirectAvailable );
void GLX_StaticWorld_Clear( StaticWorldStats *stats );
void GLX_StaticWorld_Record( StaticWorldStats *stats, int surfaces, int vertexes, int indexes, int vertexBytes, int indexBytes );
void GLX_StaticWorld_RecordBatches( StaticWorldStats *stats, int batches, int largestBatchSurfaces,
	int faceSurfaces, int gridSurfaces, int triangleSurfaces, int shaderStagePasses, int maxShaderStages );
void GLX_StaticWorld_RecordPacket( StaticWorldStats *stats, const char *shaderName, int sort,
	int surfaces, int vertexes, int indexes, int firstItem, int itemCount, int vertexOffset, int vertexBytes,
	int indexOffset, int indexBytes, int shaderStagePasses, int flags );
void GLX_StaticWorld_UploadArena( StaticWorldStats *stats, const void *vertexData, int vertexBytes,
	const void *indexData, int indexBytes );
void GLX_StaticWorld_UploadIndirectCommands( StaticWorldStats *stats, qboolean drawIndirectAvailable );
GLuint GLX_StaticWorld_ArenaVertexBufferForDraw( StaticWorldStats *stats );
GLuint GLX_StaticWorld_ArenaIndexBufferForDraw( StaticWorldStats *stats );
qboolean GLX_StaticWorld_DrawDeviceRun( StaticWorldStats *stats, int indexes, int offsetBytes,
	int firstItem, int itemCount, GLenum indexType, int indexBytes,
	const char *shaderName, int sort, qboolean arenaBound );
qboolean GLX_StaticWorld_DrawDeviceRuns( StaticWorldStats *stats, int runCount,
	const int *counts, const void *const *offsets, const int *firstItems, const int *itemCounts,
	GLenum indexType, int indexBytes,
	const char *shaderName, int sort, qboolean arenaBound );
qboolean GLX_StaticWorld_DrawSoftIndexes( StaticWorldStats *stats, int indexes, const void *indexData,
	GLenum indexType, int indexBytes, const char *shaderName, int sort, qboolean arenaBound );
int GLX_StaticWorld_DrawDeviceRunsFiltered( StaticWorldStats *stats, int runCount,
	const int *counts, const void *const *offsets, const int *firstItems, const int *itemCounts,
	int *drawnRuns, GLenum indexType, int indexBytes,
	const char *shaderName, int sort, qboolean arenaBound );
void GLX_StaticWorld_RecordQueue( StaticWorldStats *stats, int queuedItems, int queuedVertexes, int queuedIndexes,
	int deviceRuns, int deviceIndexes, int softIndexes, int largestDeviceRunIndexes );
void GLX_StaticWorld_RecordDeviceRuns( StaticWorldStats *stats, int runCount,
	const int *counts, const void *const *offsets, const int *firstItems, const int *itemCounts,
	int indexBytes, const char *shaderName, int sort );
float GLX_StaticWorld_TotalMegabytes( const StaticWorldStats &stats );
void GLX_StaticWorld_PrintInfo( const StaticWorldStats &stats );
void GLX_StaticWorld_PrintPackets( const StaticWorldStats &stats, int limit );
void GLX_StaticWorld_PrintHotPackets( const StaticWorldStats &stats, int limit );
void GLX_StaticWorld_PrintIndirectCommands( const StaticWorldStats &stats, int limit );
void GLX_StaticWorld_PrintSpanDiagnostics( const StaticWorldStats &stats, int limit );
const char *GLX_StaticWorld_MdiRejectName( unsigned int reason );
const char *GLX_StaticWorld_FilteredBarrierName( unsigned int reason );

} // namespace glx

#endif // GLX_STATIC_WORLD_H
