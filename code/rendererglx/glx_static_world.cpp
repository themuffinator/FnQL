#include "glx_static_world.h"
#include "glx_static_world_logic.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#ifndef GL_ARRAY_BUFFER_BINDING_ARB
#define GL_ARRAY_BUFFER_BINDING_ARB 0x8894
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER_BINDING_ARB
#define GL_ELEMENT_ARRAY_BUFFER_BINDING_ARB 0x8895
#endif
#ifndef GL_DRAW_INDIRECT_BUFFER
#define GL_DRAW_INDIRECT_BUFFER 0x8F3F
#endif
#ifndef GL_DRAW_INDIRECT_BUFFER_BINDING
#define GL_DRAW_INDIRECT_BUFFER_BINDING 0x8F43
#endif
#ifndef GL_STREAM_DRAW
#define GL_STREAM_DRAW 0x88E0
#endif

namespace glx {

static_assert( sizeof( StaticWorldIndirectCommandStats ) == 20,
	"StaticWorldIndirectCommandStats must match DrawElementsIndirectCommand layout" );

typedef void ( APIENTRY *PFNGLXSTATICGENBUFFERSPROC )( GLsizei n, GLuint *buffers );
typedef void ( APIENTRY *PFNGLXSTATICDELETEBUFFERSPROC )( GLsizei n, const GLuint *buffers );
typedef void ( APIENTRY *PFNGLXSTATICBINDBUFFERPROC )( GLenum target, GLuint buffer );
typedef void ( APIENTRY *PFNGLXSTATICBUFFERDATAPROC )( GLenum target, GLsizeiptrARB size, const GLvoid *data, GLenum usage );
typedef void ( APIENTRY *PFNGLXSTATICBUFFERSUBDATAPROC )( GLenum target, GLintptrARB offset, GLsizeiptrARB size, const GLvoid *data );
typedef void ( APIENTRY *PFNGLXSTATICGETINTEGERVPROC )( GLenum pname, GLint *params );
typedef GLenum ( APIENTRY *PFNGLXSTATICGETERRORPROC )( void );
typedef void ( APIENTRY *PFNGLXSTATICDRAWELEMENTSPROC )( GLenum mode, GLsizei count, GLenum type, const GLvoid *indices );
typedef void ( APIENTRY *PFNGLXSTATICDRAWELEMENTSINDIRECTPROC )( GLenum mode, GLenum type, const GLvoid *indirect );
typedef void ( APIENTRY *PFNGLXSTATICMULTIDRAWELEMENTSPROC )( GLenum mode, const GLsizei *count,
	GLenum type, const GLvoid *const *indices, GLsizei drawcount );
typedef void ( APIENTRY *PFNGLXSTATICMULTIDRAWELEMENTSINDIRECTPROC )( GLenum mode, GLenum type,
	const GLvoid *indirect, GLsizei drawcount, GLsizei stride );

struct StaticWorldFns {
	PFNGLXSTATICGENBUFFERSPROC GenBuffers;
	PFNGLXSTATICDELETEBUFFERSPROC DeleteBuffers;
	PFNGLXSTATICBINDBUFFERPROC BindBuffer;
	PFNGLXSTATICBUFFERDATAPROC BufferData;
	PFNGLXSTATICBUFFERSUBDATAPROC BufferSubData;
	PFNGLXSTATICGETINTEGERVPROC GetIntegerv;
	PFNGLXSTATICGETERRORPROC GetError;
	PFNGLXSTATICDRAWELEMENTSPROC DrawElements;
	PFNGLXSTATICDRAWELEMENTSINDIRECTPROC DrawElementsIndirect;
	PFNGLXSTATICMULTIDRAWELEMENTSPROC MultiDrawElements;
	PFNGLXSTATICMULTIDRAWELEMENTSINDIRECTPROC MultiDrawElementsIndirect;
};

static StaticWorldFns s_fns {};
static constexpr int GLX_STATIC_WORLD_MULTIDRAW_LIMIT = 256;
static constexpr unsigned int GLX_STATIC_WORLD_MDI_REJECT_NONE = 0;
static constexpr unsigned int GLX_STATIC_WORLD_MDI_REJECT_SHORT_BATCH = 1;
static constexpr unsigned int GLX_STATIC_WORLD_MDI_REJECT_UNSUPPORTED = 2;
static constexpr unsigned int GLX_STATIC_WORLD_MDI_REJECT_NON_MANIFEST = 3;
static constexpr unsigned int GLX_STATIC_WORLD_MDI_REJECT_MISSING_COMMAND = 4;
static constexpr unsigned int GLX_STATIC_WORLD_MDI_REJECT_NON_CONTIGUOUS = 5;
static constexpr unsigned int GLX_STATIC_WORLD_MDI_REJECT_COMPACT_BUFFER = 6;
static constexpr unsigned int GLX_STATIC_WORLD_MDI_REJECT_COMPACT_UPLOAD = 7;
static constexpr unsigned int GLX_STATIC_WORLD_MDI_REJECT_DRAW_ERROR = 8;
static constexpr unsigned int GLX_STATIC_WORLD_FILTERED_BARRIER_NONE = 0;
static constexpr unsigned int GLX_STATIC_WORLD_FILTERED_BARRIER_INVALID = 1;
static constexpr unsigned int GLX_STATIC_WORLD_FILTERED_BARRIER_POLICY = 2;

const char *GLX_StaticWorld_MdiRejectName( unsigned int reason )
{
	switch ( reason ) {
	case GLX_STATIC_WORLD_MDI_REJECT_SHORT_BATCH:
		return "short-batch";
	case GLX_STATIC_WORLD_MDI_REJECT_UNSUPPORTED:
		return "unsupported";
	case GLX_STATIC_WORLD_MDI_REJECT_NON_MANIFEST:
		return "nonmanifest";
	case GLX_STATIC_WORLD_MDI_REJECT_MISSING_COMMAND:
		return "missing-command";
	case GLX_STATIC_WORLD_MDI_REJECT_NON_CONTIGUOUS:
		return "noncontiguous";
	case GLX_STATIC_WORLD_MDI_REJECT_COMPACT_BUFFER:
		return "compact-buffer";
	case GLX_STATIC_WORLD_MDI_REJECT_COMPACT_UPLOAD:
		return "compact-upload";
	case GLX_STATIC_WORLD_MDI_REJECT_DRAW_ERROR:
		return "draw-error";
	case GLX_STATIC_WORLD_MDI_REJECT_NONE:
	default:
		return "none";
	}
}

const char *GLX_StaticWorld_FilteredBarrierName( unsigned int reason )
{
	switch ( reason ) {
	case GLX_STATIC_WORLD_FILTERED_BARRIER_INVALID:
		return "invalid";
	case GLX_STATIC_WORLD_FILTERED_BARRIER_POLICY:
		return "policy";
	case GLX_STATIC_WORLD_FILTERED_BARRIER_NONE:
	default:
		return "none";
	}
}

static void *GLX_StaticWorld_ProcAddress( const char *name, const char *fallbackName = nullptr )
{
	void *proc = RI().GL_GetProcAddress ? RI().GL_GetProcAddress( name ) : nullptr;

	if ( !proc && fallbackName ) {
		proc = RI().GL_GetProcAddress( fallbackName );
	}

	return proc;
}

static qboolean GLX_StaticWorld_ResolveFns()
{
	if ( s_fns.GenBuffers && s_fns.DeleteBuffers && s_fns.BindBuffer &&
		s_fns.BufferData && s_fns.GetIntegerv && s_fns.GetError ) {
		return qtrue;
	}

	s_fns.GenBuffers = reinterpret_cast<PFNGLXSTATICGENBUFFERSPROC>(
		GLX_StaticWorld_ProcAddress( "glGenBuffersARB", "glGenBuffers" ) );
	s_fns.DeleteBuffers = reinterpret_cast<PFNGLXSTATICDELETEBUFFERSPROC>(
		GLX_StaticWorld_ProcAddress( "glDeleteBuffersARB", "glDeleteBuffers" ) );
	s_fns.BindBuffer = reinterpret_cast<PFNGLXSTATICBINDBUFFERPROC>(
		GLX_StaticWorld_ProcAddress( "glBindBufferARB", "glBindBuffer" ) );
	s_fns.BufferData = reinterpret_cast<PFNGLXSTATICBUFFERDATAPROC>(
		GLX_StaticWorld_ProcAddress( "glBufferDataARB", "glBufferData" ) );
	s_fns.BufferSubData = reinterpret_cast<PFNGLXSTATICBUFFERSUBDATAPROC>(
		GLX_StaticWorld_ProcAddress( "glBufferSubDataARB", "glBufferSubData" ) );
	s_fns.GetIntegerv = reinterpret_cast<PFNGLXSTATICGETINTEGERVPROC>(
		GLX_StaticWorld_ProcAddress( "glGetIntegerv" ) );
	s_fns.GetError = reinterpret_cast<PFNGLXSTATICGETERRORPROC>(
		GLX_StaticWorld_ProcAddress( "glGetError" ) );

	return s_fns.GenBuffers && s_fns.DeleteBuffers && s_fns.BindBuffer &&
		s_fns.BufferData && s_fns.GetIntegerv && s_fns.GetError ? qtrue : qfalse;
}

static void GLX_StaticWorld_ClearGLErrors()
{
	if ( !s_fns.GetError ) {
		return;
	}

	for ( int i = 0; i < 8 && s_fns.GetError() != GL_NO_ERROR; i++ ) {
	}
}

static qboolean GLX_StaticWorld_ResolveDrawFn()
{
	if ( s_fns.DrawElements ) {
		return qtrue;
	}

	s_fns.DrawElements = reinterpret_cast<PFNGLXSTATICDRAWELEMENTSPROC>(
		GLX_StaticWorld_ProcAddress( "glDrawElements" ) );

	return s_fns.DrawElements ? qtrue : qfalse;
}

static qboolean GLX_StaticWorld_ResolveDrawIndirectFn()
{
	if ( s_fns.DrawElementsIndirect ) {
		return qtrue;
	}

	s_fns.DrawElementsIndirect = reinterpret_cast<PFNGLXSTATICDRAWELEMENTSINDIRECTPROC>(
		GLX_StaticWorld_ProcAddress( "glDrawElementsIndirect" ) );

	return s_fns.DrawElementsIndirect ? qtrue : qfalse;
}

static qboolean GLX_StaticWorld_ResolveMultiDrawFn()
{
	if ( s_fns.MultiDrawElements ) {
		return qtrue;
	}

	s_fns.MultiDrawElements = reinterpret_cast<PFNGLXSTATICMULTIDRAWELEMENTSPROC>(
		GLX_StaticWorld_ProcAddress( "glMultiDrawElements" ) );

	return s_fns.MultiDrawElements ? qtrue : qfalse;
}

static qboolean GLX_StaticWorld_ResolveMultiDrawIndirectFn()
{
	if ( s_fns.MultiDrawElementsIndirect ) {
		return qtrue;
	}

	s_fns.MultiDrawElementsIndirect = reinterpret_cast<PFNGLXSTATICMULTIDRAWELEMENTSINDIRECTPROC>(
		GLX_StaticWorld_ProcAddress( "glMultiDrawElementsIndirect" ) );

	return s_fns.MultiDrawElementsIndirect ? qtrue : qfalse;
}

// GLx static-world code is the only in-repo owner of GL_DRAW_INDIRECT_BUFFER,
// so shadow the binding locally instead of querying it around every indirect draw.
static GLuint GLX_StaticWorld_CurrentIndirectBufferBinding( StaticWorldStats *stats )
{
	GLint current = 0;

	if ( !stats ) {
		return 0;
	}

	if ( stats->indirectBufferBindingKnown ) {
		stats->indirectBufferBindingCacheHits++;
		return stats->indirectBufferBinding;
	}

	if ( s_fns.GetIntegerv ) {
		s_fns.GetIntegerv( GL_DRAW_INDIRECT_BUFFER_BINDING, &current );
		stats->indirectBufferBindingQueries++;
	}
	stats->indirectBufferBinding = static_cast<GLuint>( current );
	stats->indirectBufferBindingKnown = qtrue;
	return stats->indirectBufferBinding;
}

static void GLX_StaticWorld_BindIndirectBufferTracked( StaticWorldStats *stats, GLuint buffer )
{
	if ( s_fns.BindBuffer ) {
		s_fns.BindBuffer( GL_DRAW_INDIRECT_BUFFER, buffer );
	}
	if ( stats ) {
		stats->indirectBufferBinding = buffer;
		stats->indirectBufferBindingKnown = qtrue;
	}
}

static void GLX_StaticWorld_RestoreIndirectBufferBinding( StaticWorldStats *stats, GLuint buffer )
{
	if ( !s_fns.BindBuffer ) {
		return;
	}

	if ( !stats ) {
		s_fns.BindBuffer( GL_DRAW_INDIRECT_BUFFER, buffer );
		return;
	}

	if ( !stats->indirectBufferBindingKnown || stats->indirectBufferBinding != buffer ) {
		GLX_StaticWorld_BindIndirectBufferTracked( stats, buffer );
	}
	stats->indirectBufferBindingRestores++;
}

static void GLX_StaticWorld_InvalidateIndirectBufferBinding( StaticWorldStats *stats )
{
	if ( !stats ) {
		return;
	}

	stats->indirectBufferBinding = 0;
	stats->indirectBufferBindingKnown = qfalse;
}

static void GLX_StaticWorld_ClearPacket( StaticWorldPacketStats *packet )
{
	if ( !packet ) {
		return;
	}

	packet->shaderName[0] = '\0';
	packet->sort = 0;
	packet->surfaces = 0;
	packet->vertexes = 0;
	packet->indexes = 0;
	packet->firstItem = 0;
	packet->itemCount = 0;
	packet->vertexOffset = 0;
	packet->vertexBytes = 0;
	packet->indexOffset = 0;
	packet->indexBytes = 0;
	packet->shaderStagePasses = 0;
	packet->flags = 0;
	packet->queueFullRuns = 0;
	packet->queuePartialRuns = 0;
	packet->drawFullRuns = 0;
	packet->drawPartialRuns = 0;
	packet->drawManifestRuns = 0;
	packet->drawIndexes = 0;
}

static void GLX_StaticWorld_ClearIndirectCommand( StaticWorldIndirectCommandStats *command )
{
	if ( !command ) {
		return;
	}

	command->count = 0;
	command->instanceCount = 0;
	command->firstIndex = 0;
	command->baseVertex = 0;
	command->baseInstance = 0;
}

static void GLX_StaticWorld_ClearCommandSpan( StaticWorldCommandSpanStats *span )
{
	if ( !span ) {
		return;
	}

	span->firstCommand = -1;
	span->commandCount = 0;
	span->indexes = 0;
	span->firstPacket = -1;
	span->lastPacket = -1;
	span->firstSourceRun = -1;
	span->lastSourceRun = -1;
}

static void GLX_StaticWorld_AddCounter( unsigned int *counter, unsigned long long value )
{
	if ( !counter || value == 0 ) {
		return;
	}

	const unsigned int room = ~0u - *counter;
	*counter += static_cast<unsigned int>( value > room ? room : value );
}

static void GLX_StaticWorld_ClearItemLookup( StaticWorldStats *stats )
{
	if ( !stats ) {
		return;
	}

	for ( int i = 0; i <= GLX_STATIC_WORLD_ITEM_LOOKUP_LIMIT; i++ ) {
		stats->itemToPacket[i] = -1;
	}

	stats->itemLookupMaxItem = 0;
	stats->itemLookupMappedItems = 0;
	stats->itemLookupOverflows = 0;
	stats->itemLookupHits = 0;
	stats->itemLookupMisses = 0;
	stats->itemLookupFallbacks = 0;
	stats->itemLookupMismatches = 0;
}

static void GLX_StaticWorld_ClearIndirectCommands( StaticWorldStats *stats )
{
	if ( !stats ) {
		return;
	}

	for ( int i = 0; i < GLX_STATIC_WORLD_PACKET_LIMIT; i++ ) {
		GLX_StaticWorld_ClearIndirectCommand( &stats->indirectPackets[i] );
		stats->indirectPacketToCommand[i] = -1;
		stats->indirectCommandToPacket[i] = -1;
	}

	stats->indirectPacketCount = 0;
	stats->indirectPacketIndexes = 0;
	stats->indirectPacketBytes = 0;
	stats->indirectPacketInvalid = 0;
	stats->indirectPacketMisaligned = 0;
}

static void GLX_StaticWorld_ClearLastQueueCommandSpans( StaticWorldStats *stats )
{
	if ( !stats ) {
		return;
	}

	stats->lastQueueCommandSpanCount = 0;
	stats->lastQueueCommandSpanOverflow = 0;
	stats->lastQueueLargestIndirectCommandSpanIndexes = 0;

	for ( int i = 0; i < GLX_STATIC_WORLD_QUEUE_COMMAND_SPAN_LIMIT; i++ ) {
		GLX_StaticWorld_ClearCommandSpan( &stats->lastQueueCommandSpans[i] );
	}
}

static void GLX_StaticWorld_ClearIndirectBufferStats( StaticWorldStats *stats )
{
	if ( !stats ) {
		return;
	}

	stats->indirectBufferBinding = 0;
	stats->indirectBufferBuilds = 0;
	stats->indirectBufferSkips = 0;
	stats->indirectBufferUnsupported = 0;
	stats->indirectBufferFailures = 0;
	stats->indirectBufferCommands = 0;
	stats->indirectBufferBytes = 0;
	stats->indirectCompactBufferBytes = 0;
	stats->indirectCompactBufferCapacityBytes = 0;
	stats->indirectBufferReady = qfalse;
	stats->indirectBufferBindingKnown = qfalse;
	stats->indirectBufferBindingQueries = 0;
	stats->indirectBufferBindingCacheHits = 0;
	stats->indirectBufferBindingRestores = 0;
}

static void GLX_StaticWorld_ClearIndirectDrawStats( StaticWorldStats *stats )
{
	if ( !stats ) {
		return;
	}

	stats->indirectDrawAttempts = 0;
	stats->indirectDrawCalls = 0;
	stats->indirectDrawIndexes = 0;
	stats->indirectDrawFallbacks = 0;
	stats->indirectDrawSkips = 0;
	stats->indirectDrawNoCommand = 0;
	stats->indirectDrawErrors = 0;
}

static void GLX_StaticWorld_RecordPacketItemLookup( StaticWorldStats *stats, int packetIndex,
	int firstItem, int itemCount )
{
	long long first;
	long long last;
	long long mapLast;

	if ( !stats || packetIndex < 0 || firstItem <= 0 || itemCount <= 0 ) {
		return;
	}

	first = firstItem;
	last = first + itemCount - 1;
	if ( last < first ) {
		return;
	}

	if ( first > GLX_STATIC_WORLD_ITEM_LOOKUP_LIMIT ) {
		GLX_StaticWorld_AddCounter( &stats->itemLookupOverflows,
			static_cast<unsigned long long>( last - first + 1 ) );
		return;
	}

	mapLast = last;
	if ( mapLast > GLX_STATIC_WORLD_ITEM_LOOKUP_LIMIT ) {
		GLX_StaticWorld_AddCounter( &stats->itemLookupOverflows,
			static_cast<unsigned long long>( mapLast - GLX_STATIC_WORLD_ITEM_LOOKUP_LIMIT ) );
		mapLast = GLX_STATIC_WORLD_ITEM_LOOKUP_LIMIT;
	}

	if ( static_cast<int>( mapLast ) > stats->itemLookupMaxItem ) {
		stats->itemLookupMaxItem = static_cast<int>( mapLast );
	}

	for ( int item = firstItem; item <= static_cast<int>( mapLast ); item++ ) {
		const int existingPacket = stats->itemToPacket[item];

		if ( existingPacket == -1 ) {
			stats->itemToPacket[item] = packetIndex;
			GLX_StaticWorld_AddCounter( &stats->itemLookupMappedItems, 1 );
		} else if ( existingPacket != packetIndex ) {
			GLX_StaticWorld_AddCounter( &stats->itemLookupMismatches, 1 );
		}
	}
}

static void GLX_StaticWorld_RecordPacketIndirectCommand( StaticWorldStats *stats, int packetIndex,
	int indexes, int indexOffset, int indexBytes )
{
	StaticWorldIndirectCommandStats *command;
	int commandIndex;

	if ( !stats || packetIndex < 0 || packetIndex >= GLX_STATIC_WORLD_PACKET_LIMIT ) {
		return;
	}

	command = &stats->indirectPackets[packetIndex];
	GLX_StaticWorld_ClearIndirectCommand( command );

	if ( indexes <= 0 || indexOffset < 0 || indexBytes <= 0 ) {
		stats->indirectPacketInvalid++;
		return;
	}
	if ( indexOffset % indexBytes ) {
		stats->indirectPacketMisaligned++;
		return;
	}

	if ( stats->indirectPacketCount >= GLX_STATIC_WORLD_PACKET_LIMIT ) {
		stats->indirectPacketInvalid++;
		return;
	}

	commandIndex = static_cast<int>( stats->indirectPacketCount );
	command->count = static_cast<unsigned int>( indexes );
	command->instanceCount = 1;
	command->firstIndex = static_cast<unsigned int>( indexOffset / indexBytes );
	command->baseVertex = 0;
	command->baseInstance = 0;

	stats->indirectPacketToCommand[packetIndex] = commandIndex;
	stats->indirectCommandToPacket[commandIndex] = packetIndex;
	stats->indirectPacketCount++;
	GLX_StaticWorld_AddCounter( &stats->indirectPacketIndexes, static_cast<unsigned int>( indexes ) );
	GLX_StaticWorld_AddCounter( &stats->indirectPacketBytes, sizeof( *command ) );
}

static void GLX_StaticWorld_ClearDrawStats( StaticWorldStats *stats )
{
	if ( !stats ) {
		return;
	}

	stats->drawAttempts = 0;
	stats->drawCalls = 0;
	stats->drawFallbacks = 0;
	stats->drawPolicySkips = 0;
	stats->drawIndexes = 0;
	stats->drawPacketHits = 0;
	stats->drawPacketFullHits = 0;
	stats->drawPacketPartialHits = 0;
	stats->drawPacketMisses = 0;
	stats->drawPacketItemMismatches = 0;
	stats->drawArenaCalls = 0;
	stats->drawLegacyBufferCalls = 0;
	stats->drawManifestPacketCalls = 0;
	stats->drawManifestPacketIndexes = 0;
	stats->softDrawAttempts = 0;
	stats->softDrawCalls = 0;
	stats->softDrawFallbacks = 0;
	stats->softDrawIndexes = 0;
	stats->largestSoftDrawIndexes = 0;
	stats->largestDrawIndexes = 0;
	stats->lastDrawOffset = 0;
	stats->lastDrawIndexes = 0;
	stats->multiDrawAttempts = 0;
	stats->multiDrawCalls = 0;
	stats->multiDrawFallbacks = 0;
	stats->multiDrawRuns = 0;
	stats->multiDrawIndexes = 0;
	stats->multiDrawFilteredAttempts = 0;
	stats->multiDrawFilteredBatches = 0;
	stats->multiDrawFilteredRuns = 0;
	stats->multiDrawFilteredCandidates = 0;
	stats->multiDrawFilteredSkips = 0;
	stats->multiDrawFilteredOrderBarriers = 0;
	stats->multiDrawFilteredInvalidBarriers = 0;
	stats->multiDrawFilteredPolicyBarriers = 0;
	stats->multiDrawFilteredLastBarrierReason = GLX_STATIC_WORLD_FILTERED_BARRIER_NONE;
	stats->multiDrawFilteredLastBarrierRun = -1;
	stats->packetBatchAttempts = 0;
	stats->packetBatchBatches = 0;
	stats->packetBatchRuns = 0;
	stats->packetBatchIndexes = 0;
	stats->packetBatchFallbackRuns = 0;
	stats->packetBatchSingleRuns = 0;
	stats->packetBatchLastSegments = 0;
	stats->packetBatchLastPacketRuns = 0;
	stats->packetBatchLastFallbackRuns = 0;
	stats->packetBatchLastSingles = 0;
	stats->packetBatchLargestSegments = 0;
	stats->multiDrawIndirectAttempts = 0;
	stats->multiDrawIndirectCalls = 0;
	stats->multiDrawIndirectStaticCalls = 0;
	stats->multiDrawIndirectCompactCalls = 0;
	stats->multiDrawIndirectRuns = 0;
	stats->multiDrawIndirectIndexes = 0;
	stats->multiDrawIndirectFallbacks = 0;
	stats->multiDrawIndirectSkips = 0;
	stats->multiDrawIndirectErrors = 0;
	stats->multiDrawIndirectLargestRun = 0;
	stats->multiDrawIndirectLargestIndexes = 0;
	stats->multiDrawIndirectLastRuns = 0;
	stats->multiDrawIndirectLastIndexes = 0;
	stats->multiDrawIndirectLastSource = 0;
	stats->multiDrawIndirectLastFirstCommand = -1;
	stats->multiDrawIndirectCompactUploads = 0;
	stats->multiDrawIndirectCompactBytes = 0;
	stats->multiDrawIndirectCompactOrphans = 0;
	stats->multiDrawIndirectCompactSubDatas = 0;
	stats->multiDrawIndirectCompactGrows = 0;
	stats->multiDrawIndirectCompactFailures = 0;
	stats->multiDrawIndirectLastRejectReason = GLX_STATIC_WORLD_MDI_REJECT_NONE;
	stats->multiDrawIndirectLastRejectRuns = 0;
	stats->multiDrawIndirectLastRejectIndexes = 0;
	stats->multiDrawIndirectLastRejectCommand = -1;
	stats->multiDrawIndirectSpanAttempts = 0;
	stats->multiDrawIndirectSpanBatches = 0;
	stats->multiDrawIndirectSpanMdiRuns = 0;
	stats->multiDrawIndirectSpanFallbackRuns = 0;
	stats->multiDrawIndirectSpanSingles = 0;
	stats->multiDrawIndirectSpanSingleDraws = 0;
	stats->multiDrawIndirectSpanSingleIndirectDraws = 0;
	stats->multiDrawIndirectSpanLastSegments = 0;
	stats->multiDrawIndirectSpanLastMdiRuns = 0;
	stats->multiDrawIndirectSpanLastFallbackRuns = 0;
	stats->multiDrawIndirectSpanLastSingles = 0;
	stats->multiDrawIndirectSpanLargestSegments = 0;
	stats->multiDrawIndirectCommandSpanAttempts = 0;
	stats->multiDrawIndirectCommandSpanBatches = 0;
	stats->multiDrawIndirectCommandSpanMdiRuns = 0;
	stats->multiDrawIndirectCommandSpanFallbackRuns = 0;
	stats->multiDrawIndirectCommandSpanSingletons = 0;
	stats->multiDrawIndirectCommandSpanSingletonDraws = 0;
	stats->multiDrawIndirectCommandSpanSingletonIndirectDraws = 0;
	stats->multiDrawIndirectCommandSpanLastSegments = 0;
	stats->multiDrawIndirectCommandSpanLastMdiRuns = 0;
	stats->multiDrawIndirectCommandSpanLastFallbackRuns = 0;
	stats->multiDrawIndirectCommandSpanLastSingletonDraws = 0;
	stats->multiDrawIndirectCommandSpanLargestSegments = 0;
	stats->multiDrawIndirectUnsupported = 0;
	stats->multiDrawIndirectShortBatches = 0;
	stats->multiDrawIndirectNonManifest = 0;
	stats->multiDrawIndirectMissingCommand = 0;
	stats->multiDrawIndirectNonContiguous = 0;
	GLX_StaticWorld_ClearIndirectDrawStats( stats );
}

static void GLX_StaticWorld_CopyPacket( StaticWorldPacketStats *packet, const char *shaderName, int sort,
	int surfaces, int vertexes, int indexes, int firstItem, int itemCount, int vertexOffset, int vertexBytes,
	int indexOffset, int indexBytes, int shaderStagePasses, int flags )
{
	if ( !packet ) {
		return;
	}

	std::snprintf( packet->shaderName, sizeof( packet->shaderName ), "%s",
		shaderName && *shaderName ? shaderName : "<unnamed>" );
	packet->sort = sort;
	packet->surfaces = surfaces;
	packet->vertexes = vertexes;
	packet->indexes = indexes;
	packet->firstItem = firstItem;
	packet->itemCount = itemCount;
	packet->vertexOffset = vertexOffset;
	packet->vertexBytes = vertexBytes;
	packet->indexOffset = indexOffset;
	packet->indexBytes = indexBytes;
	packet->shaderStagePasses = shaderStagePasses;
	packet->flags = flags;
	packet->queueFullRuns = 0;
	packet->queuePartialRuns = 0;
	packet->drawFullRuns = 0;
	packet->drawPartialRuns = 0;
	packet->drawManifestRuns = 0;
	packet->drawIndexes = 0;
}

struct StaticWorldPreparedRun {
	int sourceIndex;
	GLsizei count;
	const GLvoid *offset;
	int offsetBytes;
	StaticWorldRunPacket packet;
	qboolean manifestDraw;
};

static StaticWorldDrawPolicy GLX_StaticWorld_DrawPolicy( const StaticWorldStats &stats )
{
	const char *value = stats.r_glxStaticWorldDrawPolicy ? stats.r_glxStaticWorldDrawPolicy->string : nullptr;

	return GLX_StaticWorld_DrawPolicyFromString( value );
}

static qboolean GLX_StaticWorld_WorldRendererEnabled( const StaticWorldStats &stats )
{
	return stats.r_glxWorldRenderer && stats.r_glxWorldRenderer->integer ? qtrue : qfalse;
}

static qboolean GLX_StaticWorld_ArenaUploadEnabled( const StaticWorldStats &stats )
{
	return GLX_StaticWorld_WorldRendererEnabled( stats ) ||
		( stats.r_glxStaticWorldArena && stats.r_glxStaticWorldArena->integer ) ? qtrue : qfalse;
}

static qboolean GLX_StaticWorld_ArenaDrawEnabled( const StaticWorldStats &stats )
{
	return GLX_StaticWorld_WorldRendererEnabled( stats ) ||
		( stats.r_glxStaticWorldArenaDraw && stats.r_glxStaticWorldArenaDraw->integer ) ? qtrue : qfalse;
}

static qboolean GLX_StaticWorld_DeviceDrawEnabled( const StaticWorldStats &stats )
{
	return GLX_StaticWorld_WorldRendererEnabled( stats ) ||
		( stats.r_glxStaticWorldDraw && stats.r_glxStaticWorldDraw->integer ) ? qtrue : qfalse;
}

static qboolean GLX_StaticWorld_SoftDrawEnabled( const StaticWorldStats &stats )
{
	return GLX_StaticWorld_WorldRendererEnabled( stats ) ||
		( stats.r_glxStaticWorldSoftDraw && stats.r_glxStaticWorldSoftDraw->integer ) ? qtrue : qfalse;
}

static qboolean GLX_StaticWorld_MultiDrawEnabled( const StaticWorldStats &stats )
{
	return GLX_StaticWorld_WorldRendererEnabled( stats ) ||
		( stats.r_glxStaticWorldMultiDraw && stats.r_glxStaticWorldMultiDraw->integer ) ? qtrue : qfalse;
}

static qboolean GLX_StaticWorld_PacketBatchEnabled( const StaticWorldStats &stats )
{
	return stats.r_glxStaticWorldPacketBatch && stats.r_glxStaticWorldPacketBatch->integer ? qtrue : qfalse;
}

static qboolean GLX_StaticWorld_IndirectDrawRequested( const StaticWorldStats &stats )
{
	return stats.r_glxStaticWorldIndirectDraw && stats.r_glxStaticWorldIndirectDraw->integer ? qtrue : qfalse;
}

static qboolean GLX_StaticWorld_IndirectDrawEnabled( const StaticWorldStats &stats )
{
	return GLX_StaticWorld_IndirectDrawRequested( stats ) && stats.drawIndirectAvailable ? qtrue : qfalse;
}

static qboolean GLX_StaticWorld_MultiDrawIndirectRequested( const StaticWorldStats &stats )
{
	return stats.r_glxStaticWorldMultiDrawIndirect &&
		stats.r_glxStaticWorldMultiDrawIndirect->integer ? qtrue : qfalse;
}

static qboolean GLX_StaticWorld_MultiDrawIndirectEnabled( const StaticWorldStats &stats )
{
	return GLX_StaticWorld_MultiDrawIndirectRequested( stats ) &&
		stats.multiDrawIndirectAvailable ? qtrue : qfalse;
}

static qboolean GLX_StaticWorld_CompactMultiDrawIndirectEnabled( const StaticWorldStats &stats )
{
	return GLX_StaticWorld_MultiDrawIndirectEnabled( stats ) &&
		stats.r_glxStaticWorldMultiDrawIndirectCompact &&
		stats.r_glxStaticWorldMultiDrawIndirectCompact->integer ? qtrue : qfalse;
}

static qboolean GLX_StaticWorld_MultiDrawIndirectSpansEnabled( const StaticWorldStats &stats )
{
	return GLX_StaticWorld_MultiDrawIndirectEnabled( stats ) &&
		stats.r_glxStaticWorldMultiDrawIndirectSpans &&
		stats.r_glxStaticWorldMultiDrawIndirectSpans->integer ? qtrue : qfalse;
}

static StaticWorldDrawPolicy GLX_StaticWorld_EffectiveDrawPolicy( const StaticWorldStats &stats )
{
	if ( GLX_StaticWorld_WorldRendererEnabled( stats ) ) {
		return StaticWorldDrawPolicy::AllRuns;
	}

	return GLX_StaticWorld_DrawPolicy( stats );
}

static StaticWorldRunPacket GLX_StaticWorld_ClassifyRunAgainstPacket( const StaticWorldPacketStats &packet, int packetIndex,
	int offsetBytes, int runBytes, int firstItem, int itemCount, const char *shaderName, int sort )
{
	const StaticWorldPacketView view {
		packet.shaderName,
		packet.sort,
		packet.firstItem,
		packet.itemCount,
		packet.indexOffset,
		packet.indexBytes
	};

	return GLX_StaticWorld_ClassifyRunAgainstPacketView( view, packetIndex,
		offsetBytes, runBytes, firstItem, itemCount, shaderName, sort );
}

static StaticWorldRunPacket GLX_StaticWorld_ClassifyRunPacketByByteScan( const StaticWorldStats &stats,
	int offsetBytes, int runBytes, int firstItem, int itemCount, const char *shaderName, int sort )
{
	StaticWorldRunPacket result { StaticWorldPacketMatch::NoMatch, -1 };

	if ( offsetBytes < 0 || runBytes <= 0 ) {
		return result;
	}

	for ( int i = 0; i < stats.packetCount; i++ ) {
		result = GLX_StaticWorld_ClassifyRunAgainstPacket( stats.packets[i], i, offsetBytes, runBytes,
			firstItem, itemCount, shaderName, sort );
		if ( result.match != StaticWorldPacketMatch::NoMatch ) {
			return result;
		}
	}

	return result;
}

static qboolean GLX_StaticWorld_ClassifyRunPacketByItemLookup( StaticWorldStats *stats,
	int offsetBytes, int runBytes, int firstItem, int itemCount, const char *shaderName, int sort,
	StaticWorldRunPacket *result )
{
	long long runLastItem;
	int firstPacket;
	int lastPacket;
	StaticWorldRunPacket lookupResult { StaticWorldPacketMatch::NoMatch, -1 };

	if ( !stats || !result ) {
		return qfalse;
	}

	if ( firstItem <= 0 || itemCount <= 0 ) {
		stats->itemLookupMisses++;
		return qfalse;
	}

	runLastItem = static_cast<long long>( firstItem ) + itemCount - 1;
	if ( runLastItem < firstItem ) {
		stats->itemLookupMisses++;
		return qfalse;
	}

	if ( firstItem > GLX_STATIC_WORLD_ITEM_LOOKUP_LIMIT ||
		runLastItem > GLX_STATIC_WORLD_ITEM_LOOKUP_LIMIT ) {
		stats->itemLookupOverflows++;
		stats->itemLookupMisses++;
		return qfalse;
	}

	firstPacket = stats->itemToPacket[firstItem];
	lastPacket = stats->itemToPacket[static_cast<int>( runLastItem )];
	if ( firstPacket < 0 || lastPacket < 0 ) {
		stats->itemLookupMisses++;
		return qfalse;
	}
	if ( firstPacket != lastPacket ) {
		stats->itemLookupMismatches++;
		return qfalse;
	}
	if ( firstPacket >= stats->packetCount ) {
		stats->itemLookupMismatches++;
		return qfalse;
	}

	lookupResult = GLX_StaticWorld_ClassifyRunAgainstPacket( stats->packets[firstPacket], firstPacket,
		offsetBytes, runBytes, firstItem, itemCount, shaderName, sort );
	if ( lookupResult.match == StaticWorldPacketMatch::NoMatch ) {
		stats->itemLookupMismatches++;
		return qfalse;
	}

	stats->itemLookupHits++;
	*result = lookupResult;
	return qtrue;
}

static StaticWorldRunPacket GLX_StaticWorld_ClassifyRunPacketResult( StaticWorldStats *stats, int offsetBytes, int runBytes,
	int firstItem, int itemCount, const char *shaderName, int sort )
{
	StaticWorldRunPacket result { StaticWorldPacketMatch::NoMatch, -1 };

	if ( !stats || offsetBytes < 0 || runBytes <= 0 ) {
		return result;
	}

	if ( GLX_StaticWorld_ClassifyRunPacketByItemLookup( stats, offsetBytes, runBytes,
		firstItem, itemCount, shaderName, sort, &result ) ) {
		return result;
	}

	stats->itemLookupFallbacks++;
	return GLX_StaticWorld_ClassifyRunPacketByByteScan( *stats, offsetBytes, runBytes,
		firstItem, itemCount, shaderName, sort );
}

static qboolean GLX_StaticWorld_ValidateRun( int indexes, int offsetBytes, int indexBytes, int *runBytes )
{
	if ( runBytes ) {
		*runBytes = 0;
	}
	if ( indexes <= 0 || offsetBytes < 0 || indexBytes <= 0 ) {
		return qfalse;
	}
	if ( indexes > 0x7fffffff / indexBytes ) {
		return qfalse;
	}
	if ( runBytes ) {
		*runBytes = indexes * indexBytes;
	}
	return qtrue;
}

static StaticWorldPacketStats *GLX_StaticWorld_PacketForResult( StaticWorldStats *stats,
	const StaticWorldRunPacket &packet )
{
	if ( !stats || packet.packetIndex < 0 || packet.packetIndex >= stats->packetCount ) {
		return nullptr;
	}

	return &stats->packets[packet.packetIndex];
}

static int GLX_StaticWorld_CommandIndexForPacket( const StaticWorldStats *stats,
	const StaticWorldRunPacket &packet )
{
	int commandIndex;

	if ( !stats || packet.match != StaticWorldPacketMatch::Full ||
		packet.packetIndex < 0 || packet.packetIndex >= stats->packetCount ) {
		return -1;
	}

	commandIndex = stats->indirectPacketToCommand[packet.packetIndex];
	if ( commandIndex < 0 ||
		static_cast<unsigned int>( commandIndex ) >= stats->indirectPacketCount ) {
		return -1;
	}

	return commandIndex;
}

static int GLX_StaticWorld_CommandIndexForPreparedRun( const StaticWorldStats *stats,
	const StaticWorldPreparedRun &run )
{
	if ( !run.manifestDraw ) {
		return -1;
	}

	return GLX_StaticWorld_CommandIndexForPacket( stats, run.packet );
}

static const char *GLX_StaticWorld_MdiSourceName( unsigned int source )
{
	switch ( source ) {
	case 1:
		return "static";
	case 2:
		return "compact";
	default:
		return "none";
	}
}

static unsigned int GLX_StaticWorld_PreparedRunIndexes( const StaticWorldPreparedRun *runs, int runCount )
{
	unsigned int indexes = 0;

	if ( !runs || runCount <= 0 ) {
		return 0;
	}

	for ( int i = 0; i < runCount; i++ ) {
		if ( runs[i].count > 0 ) {
			GLX_StaticWorld_AddCounter( &indexes, static_cast<unsigned int>( runs[i].count ) );
		}
	}

	return indexes;
}

static void GLX_StaticWorld_RecordMdiReject( StaticWorldStats *stats, unsigned int reason,
	const StaticWorldPreparedRun *runs, int runCount, int commandIndex )
{
	if ( !stats ) {
		return;
	}

	stats->multiDrawIndirectLastRejectReason = reason;
	stats->multiDrawIndirectLastRejectRuns = static_cast<unsigned int>( runCount > 0 ? runCount : 0 );
	stats->multiDrawIndirectLastRejectIndexes = GLX_StaticWorld_PreparedRunIndexes( runs, runCount );
	stats->multiDrawIndirectLastRejectCommand = commandIndex;
}

static void GLX_StaticWorld_ClearMdiReject( StaticWorldStats *stats )
{
	if ( !stats ) {
		return;
	}

	stats->multiDrawIndirectLastRejectReason = GLX_STATIC_WORLD_MDI_REJECT_NONE;
	stats->multiDrawIndirectLastRejectRuns = 0;
	stats->multiDrawIndirectLastRejectIndexes = 0;
	stats->multiDrawIndirectLastRejectCommand = -1;
}

static void GLX_StaticWorld_RecordFilteredBarrier( StaticWorldStats *stats, unsigned int reason, int sourceRun )
{
	if ( !stats ) {
		return;
	}

	stats->multiDrawFilteredOrderBarriers++;
	stats->multiDrawFilteredLastBarrierReason = reason;
	stats->multiDrawFilteredLastBarrierRun = sourceRun;

	if ( reason == GLX_STATIC_WORLD_FILTERED_BARRIER_INVALID ) {
		stats->multiDrawFilteredInvalidBarriers++;
	} else if ( reason == GLX_STATIC_WORLD_FILTERED_BARRIER_POLICY ) {
		stats->multiDrawFilteredPolicyBarriers++;
	}
}

static void GLX_StaticWorld_RecordSubmittedRun( StaticWorldStats *stats, int indexes, int offsetBytes,
	const StaticWorldRunPacket &packet, qboolean manifestDraw )
{
	StaticWorldPacketStats *packetStats;

	if ( !stats || indexes <= 0 ) {
		return;
	}

	packetStats = GLX_StaticWorld_PacketForResult( stats, packet );

	switch ( packet.match ) {
	case StaticWorldPacketMatch::Full:
		stats->drawPacketHits++;
		stats->drawPacketFullHits++;
		if ( packetStats ) {
			packetStats->drawFullRuns++;
		}
		break;
	case StaticWorldPacketMatch::Partial:
		stats->drawPacketHits++;
		stats->drawPacketPartialHits++;
		if ( packetStats ) {
			packetStats->drawPartialRuns++;
		}
		break;
	case StaticWorldPacketMatch::ItemMismatch:
		stats->drawPacketItemMismatches++;
		break;
	case StaticWorldPacketMatch::NoMatch:
	default:
		stats->drawPacketMisses++;
		break;
	}

	if ( packetStats ) {
		packetStats->drawIndexes += static_cast<unsigned int>( indexes );
		if ( manifestDraw ) {
			packetStats->drawManifestRuns++;
		}
	}

	stats->drawIndexes += static_cast<unsigned int>( indexes );
	if ( static_cast<unsigned int>( indexes ) > stats->largestDrawIndexes ) {
		stats->largestDrawIndexes = static_cast<unsigned int>( indexes );
	}
	stats->lastDrawOffset = static_cast<unsigned int>( offsetBytes );
	stats->lastDrawIndexes = static_cast<unsigned int>( indexes );
}

static void GLX_StaticWorld_RecordMultiDrawSubmission( StaticWorldStats *stats,
	const StaticWorldPreparedRun *runs, int runCount, qboolean arenaBound )
{
	unsigned int totalIndexes = 0;
	unsigned int manifestRuns = 0;
	unsigned int manifestIndexes = 0;

	if ( !stats || !runs || runCount <= 0 ) {
		return;
	}

	for ( int i = 0; i < runCount; i++ ) {
		totalIndexes += static_cast<unsigned int>( runs[i].count );
		if ( runs[i].manifestDraw ) {
			manifestRuns++;
			manifestIndexes += static_cast<unsigned int>( runs[i].count );
		}
	}

	stats->drawAttempts += static_cast<unsigned int>( runCount );
	stats->drawCalls++;
	if ( arenaBound ) {
		stats->drawArenaCalls++;
	} else {
		stats->drawLegacyBufferCalls++;
	}
	stats->multiDrawCalls++;
	stats->multiDrawRuns += static_cast<unsigned int>( runCount );
	stats->multiDrawIndexes += totalIndexes;
	stats->drawManifestPacketCalls += manifestRuns;
	stats->drawManifestPacketIndexes += manifestIndexes;

	for ( int i = 0; i < runCount; i++ ) {
		GLX_StaticWorld_RecordSubmittedRun( stats, runs[i].count, runs[i].offsetBytes,
			runs[i].packet, runs[i].manifestDraw );
	}
}

static void GLX_StaticWorld_DeleteArena( StaticWorldStats *stats )
{
	GLint oldArrayBuffer = 0;
	GLint oldElementArrayBuffer = 0;

	if ( !stats ) {
		return;
	}

	if ( !GLX_StaticWorld_ResolveFns() ) {
		stats->arenaVertexBuffer = 0;
		stats->arenaIndexBuffer = 0;
		stats->arenaVertexBytes = 0;
		stats->arenaIndexBytes = 0;
		stats->arenaReady = qfalse;
		return;
	}

	s_fns.GetIntegerv( GL_ARRAY_BUFFER_BINDING_ARB, &oldArrayBuffer );
	s_fns.GetIntegerv( GL_ELEMENT_ARRAY_BUFFER_BINDING_ARB, &oldElementArrayBuffer );

	/* Never restore a name this delete is about to free. */
	if ( (GLuint)oldArrayBuffer == stats->arenaVertexBuffer ||
		(GLuint)oldArrayBuffer == stats->arenaIndexBuffer ) {
		oldArrayBuffer = 0;
	}
	if ( (GLuint)oldElementArrayBuffer == stats->arenaVertexBuffer ||
		(GLuint)oldElementArrayBuffer == stats->arenaIndexBuffer ) {
		oldElementArrayBuffer = 0;
	}

	if ( stats->arenaVertexBuffer ) {
		s_fns.DeleteBuffers( 1, &stats->arenaVertexBuffer );
		stats->arenaVertexBuffer = 0;
	}
	if ( stats->arenaIndexBuffer ) {
		s_fns.DeleteBuffers( 1, &stats->arenaIndexBuffer );
		stats->arenaIndexBuffer = 0;
	}

	s_fns.BindBuffer( GL_ARRAY_BUFFER_ARB, (GLuint)oldArrayBuffer );
	s_fns.BindBuffer( GL_ELEMENT_ARRAY_BUFFER_ARB, (GLuint)oldElementArrayBuffer );

	stats->arenaVertexBytes = 0;
	stats->arenaIndexBytes = 0;
	stats->arenaReady = qfalse;
}

static void GLX_StaticWorld_DeleteIndirectCommandBuffer( StaticWorldStats *stats )
{
	GLuint oldDrawIndirectBuffer = 0;
	GLuint restoreDrawIndirectBuffer = 0;
	qboolean restoreBinding = qfalse;

	if ( !stats ) {
		return;
	}

	if ( !GLX_StaticWorld_ResolveFns() ) {
		GLX_StaticWorld_InvalidateIndirectBufferBinding( stats );
		stats->indirectCommandBuffer = 0;
		stats->indirectCompactCommandBuffer = 0;
		stats->indirectBufferCommands = 0;
		stats->indirectBufferBytes = 0;
		stats->indirectCompactBufferBytes = 0;
		stats->indirectCompactBufferCapacityBytes = 0;
		stats->indirectBufferReady = qfalse;
		return;
	}

	if ( stats->indirectCommandBuffer || stats->indirectCompactCommandBuffer ) {
		oldDrawIndirectBuffer = GLX_StaticWorld_CurrentIndirectBufferBinding( stats );
		restoreDrawIndirectBuffer = oldDrawIndirectBuffer;
		if ( restoreDrawIndirectBuffer == stats->indirectCommandBuffer ||
			restoreDrawIndirectBuffer == stats->indirectCompactCommandBuffer ) {
			restoreDrawIndirectBuffer = 0;
		}
		restoreBinding = qtrue;
	}
	if ( stats->indirectCommandBuffer ) {
		s_fns.DeleteBuffers( 1, &stats->indirectCommandBuffer );
		stats->indirectCommandBuffer = 0;
	}
	if ( stats->indirectCompactCommandBuffer ) {
		s_fns.DeleteBuffers( 1, &stats->indirectCompactCommandBuffer );
		stats->indirectCompactCommandBuffer = 0;
	}

	if ( restoreBinding ) {
		GLX_StaticWorld_RestoreIndirectBufferBinding( stats, restoreDrawIndirectBuffer );
	}

	stats->indirectBufferCommands = 0;
	stats->indirectBufferBytes = 0;
	stats->indirectCompactBufferBytes = 0;
	stats->indirectCompactBufferCapacityBytes = 0;
	stats->indirectBufferReady = qfalse;
}

void GLX_StaticWorld_RegisterCvars( StaticWorldStats *stats )
{
	if ( !stats ) {
		return;
	}

	stats->r_glxWorldRenderer = RI().Cvar_Get( "r_glxWorldRenderer", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( stats->r_glxWorldRenderer,
		"Enable the compatibility-first GLx static world renderer bundle. Preserves legacy stage setup and falls back per draw." );

	stats->r_glxStaticWorldArena = RI().Cvar_Get( "r_glxStaticWorldArena", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( stats->r_glxStaticWorldArena,
		"Upload the packed legacy static world VBO/IBO payload into a GLx-owned arena for the shipped GLx static-world path." );

	stats->r_glxStaticWorldArenaDraw = RI().Cvar_Get( "r_glxStaticWorldArenaDraw", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( stats->r_glxStaticWorldArenaDraw,
		"Bind the GLx-owned static world arena for static-world VBO draws when available, falling back per draw." );

	stats->r_glxStaticWorldDraw = RI().Cvar_Get( "r_glxStaticWorldDraw", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( stats->r_glxStaticWorldDraw,
		"Submit static-world device-index VBO runs through the GLx draw dispatcher with packet validation." );

	stats->r_glxStaticWorldSoftDraw = RI().Cvar_Get( "r_glxStaticWorldSoftDraw", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( stats->r_glxStaticWorldSoftDraw,
		"Submit static-world CPU-index fallback runs through the GLx draw dispatcher. Implied by r_glxWorldRenderer." );

	stats->r_glxStaticWorldDrawPolicy = RI().Cvar_Get( "r_glxStaticWorldDrawPolicy", "full", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( stats->r_glxStaticWorldDrawPolicy,
		"Controls GLx static-world draw submission: full, contained, or all. r_glxWorldRenderer uses all." );

	stats->r_glxStaticWorldMultiDraw = RI().Cvar_Get( "r_glxStaticWorldMultiDraw", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( stats->r_glxStaticWorldMultiDraw,
		"Submit same-state static-world device-index VBO runs with glMultiDrawElements when available." );

	stats->r_glxStaticWorldPacketBatch = RI().Cvar_Get( "r_glxStaticWorldPacketBatch", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( stats->r_glxStaticWorldPacketBatch,
		"Split static-world GLx batches into ordered manifest-packet spans so full packets submit from packet-owned offsets before falling back to legacy run spans." );

	stats->r_glxStaticWorldIndirectBuffer = RI().Cvar_Get( "r_glxStaticWorldIndirectBuffer", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( stats->r_glxStaticWorldIndirectBuffer,
		"Upload the GLx static-world packet command manifest into a GL_DRAW_INDIRECT_BUFFER at map load when draw-indirect is available." );

	stats->r_glxStaticWorldIndirectDraw = RI().Cvar_Get( "r_glxStaticWorldIndirectDraw", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( stats->r_glxStaticWorldIndirectDraw,
		"Submit full static-world manifest packets with glDrawElementsIndirect when the GLx indirect command buffer is ready." );

	stats->r_glxStaticWorldMultiDrawIndirect = RI().Cvar_Get( "r_glxStaticWorldMultiDrawIndirect", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( stats->r_glxStaticWorldMultiDrawIndirect,
		"Submit contiguous full-packet static-world command spans with glMultiDrawElementsIndirect when the GLx indirect command buffer is ready." );

	stats->r_glxStaticWorldMultiDrawIndirectCompact = RI().Cvar_Get( "r_glxStaticWorldMultiDrawIndirectCompact", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( stats->r_glxStaticWorldMultiDrawIndirectCompact,
		"Upload compact visible full-packet static-world command batches for glMultiDrawElementsIndirect when static command slots are not adjacent or not uploaded. Experimental and off by default." );

	stats->r_glxStaticWorldMultiDrawIndirectSpans = RI().Cvar_Get( "r_glxStaticWorldMultiDrawIndirectSpans", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( stats->r_glxStaticWorldMultiDrawIndirectSpans,
		"Split filtered static-world multidraw batches into full-manifest MDI spans and fallback multidraw spans." );
}

void GLX_StaticWorld_SetCapabilities( StaticWorldStats *stats,
	qboolean drawIndirectAvailable, qboolean multiDrawIndirectAvailable )
{
	if ( !stats ) {
		return;
	}

	stats->drawIndirectAvailable = drawIndirectAvailable;
	stats->multiDrawIndirectAvailable = multiDrawIndirectAvailable;
}

void GLX_StaticWorld_Clear( StaticWorldStats *stats )
{
	if ( !stats ) {
		return;
	}

	GLX_StaticWorld_DeleteArena( stats );
	GLX_StaticWorld_DeleteIndirectCommandBuffer( stats );

	stats->drawIndirectAvailable = qfalse;
	stats->multiDrawIndirectAvailable = qfalse;
	stats->surfaces = 0;
	stats->vertexes = 0;
	stats->indexes = 0;
	stats->vertexBytes = 0;
	stats->indexBytes = 0;
	stats->batches = 0;
	stats->largestBatchSurfaces = 0;
	stats->faceSurfaces = 0;
	stats->gridSurfaces = 0;
	stats->triangleSurfaces = 0;
	stats->shaderStagePasses = 0;
	stats->maxShaderStages = 0;
	stats->generations = 0;
	stats->queueBatches = 0;
	stats->queueItems = 0;
	stats->queueVertexes = 0;
	stats->queueIndexes = 0;
	stats->queueDeviceRuns = 0;
	stats->queueDeviceIndexes = 0;
	stats->queueSoftIndexes = 0;
	stats->queueDeviceFullPacketRuns = 0;
	stats->queueDevicePartialPacketRuns = 0;
	stats->queueDevicePacketMisses = 0;
	stats->queueDeviceItemMismatches = 0;
	stats->queueDeviceIndirectCommandRuns = 0;
	stats->queueDeviceIndirectCommands = 0;
	stats->queueDeviceIndirectIndexes = 0;
	stats->queueDeviceIndirectBreaks = 0;
	stats->largestQueueIndexes = 0;
	stats->largestDeviceRunIndexes = 0;
	stats->largestIndirectCommandRun = 0;
	stats->largestIndirectCommandSpanIndexes = 0;
	stats->lastQueueItems = 0;
	stats->lastQueueVertexes = 0;
	stats->lastQueueIndexes = 0;
	stats->lastQueueDeviceRuns = 0;
	stats->lastQueueDeviceIndexes = 0;
	stats->lastQueueSoftIndexes = 0;
	stats->lastQueueDeviceFullPacketRuns = 0;
	stats->lastQueueDevicePartialPacketRuns = 0;
	stats->lastQueueDevicePacketMisses = 0;
	stats->lastQueueDeviceItemMismatches = 0;
	stats->lastQueueLargestDeviceRunIndexes = 0;
	stats->lastQueueDeviceIndirectCommandRuns = 0;
	stats->lastQueueDeviceIndirectCommands = 0;
	stats->lastQueueDeviceIndirectIndexes = 0;
	stats->lastQueueDeviceIndirectBreaks = 0;
	stats->lastQueueLargestIndirectCommandRun = 0;
	GLX_StaticWorld_ClearLastQueueCommandSpans( stats );
	stats->packetCount = 0;
	stats->packetOverflow = 0;
	stats->packetSurfaces = 0;
	stats->packetVertexes = 0;
	stats->packetIndexes = 0;
	stats->packetShaderStagePasses = 0;
	for ( int i = 0; i < GLX_STATIC_WORLD_PACKET_LIMIT; i++ ) {
		GLX_StaticWorld_ClearPacket( &stats->packets[i] );
	}
	GLX_StaticWorld_ClearPacket( &stats->largestPacket );
	GLX_StaticWorld_ClearItemLookup( stats );
	GLX_StaticWorld_ClearIndirectCommands( stats );
	GLX_StaticWorld_ClearIndirectBufferStats( stats );
	stats->arenaBuilds = 0;
	stats->arenaSkips = 0;
	stats->arenaFailures = 0;
	stats->arenaVertexBinds = 0;
	stats->arenaIndexBinds = 0;
	stats->arenaDrawSkips = 0;
	GLX_StaticWorld_ClearDrawStats( stats );
}

void GLX_StaticWorld_Record( StaticWorldStats *stats, int surfaces, int vertexes, int indexes, int vertexBytes, int indexBytes )
{
	if ( !stats ) {
		return;
	}

	if ( surfaces > 0 && stats->surfaces == 0 ) {
		stats->generations++;
	}

	stats->surfaces = surfaces;
	stats->vertexes = vertexes;
	stats->indexes = indexes;
	stats->vertexBytes = vertexBytes;
	stats->indexBytes = indexBytes;

	if ( surfaces == 0 ) {
		GLX_StaticWorld_DeleteArena( stats );
		GLX_StaticWorld_DeleteIndirectCommandBuffer( stats );
		stats->batches = 0;
		stats->largestBatchSurfaces = 0;
		stats->faceSurfaces = 0;
		stats->gridSurfaces = 0;
		stats->triangleSurfaces = 0;
		stats->shaderStagePasses = 0;
		stats->maxShaderStages = 0;
		stats->queueBatches = 0;
		stats->queueItems = 0;
		stats->queueVertexes = 0;
		stats->queueIndexes = 0;
		stats->queueDeviceRuns = 0;
		stats->queueDeviceIndexes = 0;
		stats->queueSoftIndexes = 0;
		stats->queueDeviceFullPacketRuns = 0;
		stats->queueDevicePartialPacketRuns = 0;
		stats->queueDevicePacketMisses = 0;
		stats->queueDeviceItemMismatches = 0;
		stats->queueDeviceIndirectCommandRuns = 0;
		stats->queueDeviceIndirectCommands = 0;
		stats->queueDeviceIndirectIndexes = 0;
		stats->queueDeviceIndirectBreaks = 0;
		stats->largestQueueIndexes = 0;
		stats->largestDeviceRunIndexes = 0;
		stats->largestIndirectCommandRun = 0;
		stats->largestIndirectCommandSpanIndexes = 0;
		stats->lastQueueItems = 0;
		stats->lastQueueVertexes = 0;
		stats->lastQueueIndexes = 0;
		stats->lastQueueDeviceRuns = 0;
		stats->lastQueueDeviceIndexes = 0;
		stats->lastQueueSoftIndexes = 0;
		stats->lastQueueDeviceFullPacketRuns = 0;
		stats->lastQueueDevicePartialPacketRuns = 0;
		stats->lastQueueDevicePacketMisses = 0;
		stats->lastQueueDeviceItemMismatches = 0;
		stats->lastQueueLargestDeviceRunIndexes = 0;
		stats->lastQueueDeviceIndirectCommandRuns = 0;
		stats->lastQueueDeviceIndirectCommands = 0;
		stats->lastQueueDeviceIndirectIndexes = 0;
		stats->lastQueueDeviceIndirectBreaks = 0;
		stats->lastQueueLargestIndirectCommandRun = 0;
		GLX_StaticWorld_ClearLastQueueCommandSpans( stats );
		stats->packetCount = 0;
		stats->packetOverflow = 0;
		stats->packetSurfaces = 0;
		stats->packetVertexes = 0;
		stats->packetIndexes = 0;
		stats->packetShaderStagePasses = 0;
		for ( int i = 0; i < GLX_STATIC_WORLD_PACKET_LIMIT; i++ ) {
			GLX_StaticWorld_ClearPacket( &stats->packets[i] );
		}
		GLX_StaticWorld_ClearPacket( &stats->largestPacket );
		GLX_StaticWorld_ClearItemLookup( stats );
		GLX_StaticWorld_ClearIndirectCommands( stats );
		GLX_StaticWorld_ClearIndirectBufferStats( stats );
		stats->arenaBuilds = 0;
		stats->arenaSkips = 0;
		stats->arenaFailures = 0;
		stats->arenaVertexBinds = 0;
		stats->arenaIndexBinds = 0;
		stats->arenaDrawSkips = 0;
		GLX_StaticWorld_ClearDrawStats( stats );
	}
}

void GLX_StaticWorld_RecordBatches( StaticWorldStats *stats, int batches, int largestBatchSurfaces,
	int faceSurfaces, int gridSurfaces, int triangleSurfaces, int shaderStagePasses, int maxShaderStages )
{
	if ( !stats ) {
		return;
	}

	stats->batches = batches;
	stats->largestBatchSurfaces = largestBatchSurfaces;
	stats->faceSurfaces = faceSurfaces;
	stats->gridSurfaces = gridSurfaces;
	stats->triangleSurfaces = triangleSurfaces;
	stats->shaderStagePasses = shaderStagePasses;
	stats->maxShaderStages = maxShaderStages;
}

void GLX_StaticWorld_RecordPacket( StaticWorldStats *stats, const char *shaderName, int sort,
	int surfaces, int vertexes, int indexes, int firstItem, int itemCount, int vertexOffset, int vertexBytes,
	int indexOffset, int indexBytes, int shaderStagePasses, int flags )
{
	if ( !stats || surfaces <= 0 ) {
		return;
	}

	stats->packetSurfaces += surfaces > 0 ? surfaces : 0;
	stats->packetVertexes += vertexes > 0 ? vertexes : 0;
	stats->packetIndexes += indexes > 0 ? indexes : 0;
	stats->packetShaderStagePasses += shaderStagePasses > 0 ? shaderStagePasses : 0;

	if ( indexes > stats->largestPacket.indexes ) {
		GLX_StaticWorld_CopyPacket( &stats->largestPacket, shaderName, sort,
			surfaces, vertexes, indexes, firstItem, itemCount, vertexOffset, vertexBytes,
			indexOffset, indexBytes, shaderStagePasses, flags );
	}

	if ( stats->packetCount >= GLX_STATIC_WORLD_PACKET_LIMIT ) {
		stats->packetOverflow++;
		return;
	}

	const int packetIndex = stats->packetCount;
	GLX_StaticWorld_CopyPacket( &stats->packets[packetIndex], shaderName, sort,
		surfaces, vertexes, indexes, firstItem, itemCount, vertexOffset, vertexBytes,
		indexOffset, indexBytes, shaderStagePasses, flags );
	GLX_StaticWorld_RecordPacketItemLookup( stats, packetIndex, firstItem, itemCount );
	GLX_StaticWorld_RecordPacketIndirectCommand( stats, packetIndex, indexes, indexOffset,
		indexes > 0 && indexBytes > 0 && indexBytes % indexes == 0 ? indexBytes / indexes : 0 );
	stats->packetCount++;
}

void GLX_StaticWorld_UploadArena( StaticWorldStats *stats, const void *vertexData, int vertexBytes,
	const void *indexData, int indexBytes )
{
	GLint oldArrayBuffer = 0;
	GLint oldElementArrayBuffer = 0;
	GLenum err;

	if ( !stats ) {
		return;
	}

	if ( !GLX_StaticWorld_ArenaUploadEnabled( *stats ) ) {
		stats->arenaSkips++;
		return;
	}

	GLX_StaticWorld_DeleteArena( stats );

	if ( !GLX_StaticWorld_ResolveFns() || !vertexData || !indexData ||
		vertexBytes <= 0 || indexBytes <= 0 ) {
		stats->arenaFailures++;
		return;
	}

	s_fns.GetIntegerv( GL_ARRAY_BUFFER_BINDING_ARB, &oldArrayBuffer );
	s_fns.GetIntegerv( GL_ELEMENT_ARRAY_BUFFER_BINDING_ARB, &oldElementArrayBuffer );
	GLX_StaticWorld_ClearGLErrors();

	s_fns.GenBuffers( 1, &stats->arenaVertexBuffer );
	err = s_fns.GetError();
	if ( err != GL_NO_ERROR || !stats->arenaVertexBuffer ) {
		stats->arenaFailures++;
		goto restore_bindings;
	}

	s_fns.BindBuffer( GL_ARRAY_BUFFER_ARB, stats->arenaVertexBuffer );
	s_fns.BufferData( GL_ARRAY_BUFFER_ARB, vertexBytes, vertexData, GL_STATIC_DRAW_ARB );
	err = s_fns.GetError();
	if ( err != GL_NO_ERROR ) {
		stats->arenaFailures++;
		goto fail_delete;
	}

	s_fns.GenBuffers( 1, &stats->arenaIndexBuffer );
	err = s_fns.GetError();
	if ( err != GL_NO_ERROR || !stats->arenaIndexBuffer ) {
		stats->arenaFailures++;
		goto fail_delete;
	}

	s_fns.BindBuffer( GL_ELEMENT_ARRAY_BUFFER_ARB, stats->arenaIndexBuffer );
	s_fns.BufferData( GL_ELEMENT_ARRAY_BUFFER_ARB, indexBytes, indexData, GL_STATIC_DRAW_ARB );
	err = s_fns.GetError();
	if ( err != GL_NO_ERROR ) {
		stats->arenaFailures++;
		goto fail_delete;
	}

	stats->arenaVertexBytes = vertexBytes;
	stats->arenaIndexBytes = indexBytes;
	stats->arenaBuilds++;
	stats->arenaReady = qtrue;
	goto restore_bindings;

fail_delete:
	if ( stats->arenaVertexBuffer ) {
		s_fns.DeleteBuffers( 1, &stats->arenaVertexBuffer );
		stats->arenaVertexBuffer = 0;
	}
	if ( stats->arenaIndexBuffer ) {
		s_fns.DeleteBuffers( 1, &stats->arenaIndexBuffer );
		stats->arenaIndexBuffer = 0;
	}
	stats->arenaVertexBytes = 0;
	stats->arenaIndexBytes = 0;
	stats->arenaReady = qfalse;

restore_bindings:
	s_fns.BindBuffer( GL_ARRAY_BUFFER_ARB, (GLuint)oldArrayBuffer );
	s_fns.BindBuffer( GL_ELEMENT_ARRAY_BUFFER_ARB, (GLuint)oldElementArrayBuffer );
}

void GLX_StaticWorld_UploadIndirectCommands( StaticWorldStats *stats, qboolean drawIndirectAvailable )
{
	StaticWorldIndirectCommandStats uploadCommands[GLX_STATIC_WORLD_PACKET_LIMIT];
	GLuint oldDrawIndirectBuffer = 0;
	GLenum err;
	int uploadCount = 0;
	int uploadBytes = 0;

	if ( !stats ) {
		return;
	}

	stats->drawIndirectAvailable = drawIndirectAvailable;
	GLX_StaticWorld_DeleteIndirectCommandBuffer( stats );

	if ( !stats->r_glxStaticWorldIndirectBuffer || !stats->r_glxStaticWorldIndirectBuffer->integer ) {
		stats->indirectBufferSkips++;
		return;
	}
	if ( !drawIndirectAvailable ) {
		stats->indirectBufferUnsupported++;
		return;
	}
	if ( stats->indirectPacketCount == 0 || stats->packetCount <= 0 ) {
		stats->indirectBufferSkips++;
		return;
	}
	if ( !GLX_StaticWorld_ResolveFns() ) {
		stats->indirectBufferFailures++;
		return;
	}

	for ( unsigned int commandIndex = 0;
		commandIndex < stats->indirectPacketCount && uploadCount < GLX_STATIC_WORLD_PACKET_LIMIT;
		commandIndex++ ) {
		const int packetIndex = stats->indirectCommandToPacket[commandIndex];
		const StaticWorldIndirectCommandStats *command = nullptr;

		if ( packetIndex >= 0 && packetIndex < stats->packetCount ) {
			command = &stats->indirectPackets[packetIndex];
		}
		if ( !command || command->count == 0 || command->instanceCount == 0 ) {
			/*
			Compacting over a skipped slot would desync every later
			indirectPacketToCommand byte offset used at draw time, so refuse
			the whole upload and let draws use the non-indirect path.
			*/
			stats->indirectBufferFailures++;
			return;
		}

		uploadCommands[uploadCount++] = *command;
	}

	if ( uploadCount <= 0 ) {
		stats->indirectBufferSkips++;
		return;
	}

	uploadBytes = uploadCount * static_cast<int>( sizeof( uploadCommands[0] ) );

	oldDrawIndirectBuffer = GLX_StaticWorld_CurrentIndirectBufferBinding( stats );
	GLX_StaticWorld_ClearGLErrors();

	s_fns.GenBuffers( 1, &stats->indirectCommandBuffer );
	err = s_fns.GetError();
	if ( err != GL_NO_ERROR || !stats->indirectCommandBuffer ) {
		stats->indirectBufferFailures++;
		goto fail_delete;
	}

	s_fns.BindBuffer( GL_DRAW_INDIRECT_BUFFER, stats->indirectCommandBuffer );
	s_fns.BufferData( GL_DRAW_INDIRECT_BUFFER, uploadBytes, uploadCommands, GL_STATIC_DRAW_ARB );
	err = s_fns.GetError();
	if ( err != GL_NO_ERROR ) {
		stats->indirectBufferFailures++;
		goto fail_delete;
	}

	stats->indirectBufferBuilds++;
	stats->indirectBufferCommands = static_cast<unsigned int>( uploadCount );
	stats->indirectBufferBytes = static_cast<unsigned int>( uploadBytes );
	stats->indirectBufferReady = qtrue;
	goto restore_binding;

fail_delete:
	if ( stats->indirectCommandBuffer ) {
		s_fns.DeleteBuffers( 1, &stats->indirectCommandBuffer );
		stats->indirectCommandBuffer = 0;
	}
	stats->indirectBufferCommands = 0;
	stats->indirectBufferBytes = 0;
	stats->indirectBufferReady = qfalse;

restore_binding:
	GLX_StaticWorld_RestoreIndirectBufferBinding( stats, oldDrawIndirectBuffer );
}

static qboolean GLX_StaticWorld_DrawPacketIndirect( StaticWorldStats *stats,
	const StaticWorldRunPacket &packet, GLenum indexType )
{
	const StaticWorldIndirectCommandStats *command;
	const GLvoid *commandOffset;
	GLuint oldDrawIndirectBuffer = 0;
	GLenum err;
	int commandIndex;

	if ( !stats || !GLX_StaticWorld_IndirectDrawEnabled( *stats ) ) {
		return qfalse;
	}

	stats->indirectDrawAttempts++;

	if ( packet.match != StaticWorldPacketMatch::Full ||
		packet.packetIndex < 0 || packet.packetIndex >= stats->packetCount ) {
		stats->indirectDrawSkips++;
		return qfalse;
	}
	if ( !stats->indirectBufferReady || !stats->indirectCommandBuffer ||
		!GLX_StaticWorld_ResolveFns() || !GLX_StaticWorld_ResolveDrawIndirectFn() ) {
		stats->indirectDrawFallbacks++;
		return qfalse;
	}

	commandIndex = GLX_StaticWorld_CommandIndexForPacket( stats, packet );
	if ( commandIndex < 0 ||
		static_cast<unsigned int>( commandIndex ) >= stats->indirectBufferCommands ) {
		stats->indirectDrawNoCommand++;
		stats->indirectDrawFallbacks++;
		return qfalse;
	}

	command = &stats->indirectPackets[packet.packetIndex];
	if ( command->count == 0 || command->instanceCount == 0 ) {
		stats->indirectDrawNoCommand++;
		stats->indirectDrawFallbacks++;
		return qfalse;
	}

	commandOffset = reinterpret_cast<const GLvoid *>(
		static_cast<std::intptr_t>( commandIndex * static_cast<int>( sizeof( *command ) ) ) );

	oldDrawIndirectBuffer = GLX_StaticWorld_CurrentIndirectBufferBinding( stats );
	GLX_StaticWorld_ClearGLErrors();
	GLX_StaticWorld_BindIndirectBufferTracked( stats, stats->indirectCommandBuffer );
	s_fns.DrawElementsIndirect( GL_TRIANGLES, indexType, commandOffset );
	err = s_fns.GetError();
	GLX_StaticWorld_RestoreIndirectBufferBinding( stats, oldDrawIndirectBuffer );

	if ( err != GL_NO_ERROR ) {
		stats->indirectDrawErrors++;
		stats->indirectDrawFallbacks++;
		return qfalse;
	}

	stats->indirectDrawCalls++;
	GLX_StaticWorld_AddCounter( &stats->indirectDrawIndexes, command->count );
	return qtrue;
}

GLuint GLX_StaticWorld_ArenaVertexBufferForDraw( StaticWorldStats *stats )
{
	if ( !stats || !GLX_StaticWorld_ArenaDrawEnabled( *stats ) ) {
		return 0;
	}
	if ( !stats->arenaReady || !stats->arenaVertexBuffer ) {
		stats->arenaDrawSkips++;
		return 0;
	}

	stats->arenaVertexBinds++;
	return stats->arenaVertexBuffer;
}

GLuint GLX_StaticWorld_ArenaIndexBufferForDraw( StaticWorldStats *stats )
{
	if ( !stats || !GLX_StaticWorld_ArenaDrawEnabled( *stats ) ) {
		return 0;
	}
	if ( !stats->arenaReady || !stats->arenaIndexBuffer ) {
		stats->arenaDrawSkips++;
		return 0;
	}

	stats->arenaIndexBinds++;
	return stats->arenaIndexBuffer;
}

qboolean GLX_StaticWorld_DrawDeviceRun( StaticWorldStats *stats, int indexes, int offsetBytes,
	int firstItem, int itemCount, GLenum indexType, int indexBytes,
	const char *shaderName, int sort, qboolean arenaBound )
{
	int runBytes;
	int drawIndexes = indexes;
	int drawOffsetBytes = offsetBytes;
	StaticWorldRunPacket packet;
	StaticWorldDrawPolicy policy;
	qboolean manifestDraw = qfalse;

	if ( !stats || !GLX_StaticWorld_DeviceDrawEnabled( *stats ) ) {
		return qfalse;
	}

	stats->drawAttempts++;

	if ( !GLX_StaticWorld_ValidateRun( indexes, offsetBytes, indexBytes, &runBytes ) ||
		!GLX_StaticWorld_ResolveDrawFn() ) {
		stats->drawFallbacks++;
		return qfalse;
	}

	packet = GLX_StaticWorld_ClassifyRunPacketResult( stats, offsetBytes, runBytes,
		firstItem, itemCount, shaderName, sort );
	policy = GLX_StaticWorld_EffectiveDrawPolicy( *stats );
	if ( !GLX_StaticWorld_DrawPolicyAllows( policy, packet.match ) ) {
		stats->drawFallbacks++;
		stats->drawPolicySkips++;
		return qfalse;
	}
	if ( packet.match == StaticWorldPacketMatch::Full &&
		packet.packetIndex >= 0 && packet.packetIndex < stats->packetCount ) {
		const StaticWorldPacketStats &manifestPacket = stats->packets[packet.packetIndex];
		if ( manifestPacket.indexes > 0 && manifestPacket.indexOffset >= 0 ) {
			drawIndexes = manifestPacket.indexes;
			drawOffsetBytes = manifestPacket.indexOffset;
			manifestDraw = qtrue;
			stats->drawManifestPacketCalls++;
			stats->drawManifestPacketIndexes += static_cast<unsigned int>( drawIndexes );
		}
	}

	if ( manifestDraw && GLX_StaticWorld_DrawPacketIndirect( stats, packet, indexType ) ) {
		stats->drawCalls++;
		if ( arenaBound ) {
			stats->drawArenaCalls++;
		} else {
			stats->drawLegacyBufferCalls++;
		}
		GLX_StaticWorld_RecordSubmittedRun( stats, drawIndexes, drawOffsetBytes, packet, manifestDraw );
		return qtrue;
	}

	s_fns.DrawElements( GL_TRIANGLES, drawIndexes, indexType,
		reinterpret_cast<const GLvoid *>( static_cast<std::intptr_t>( drawOffsetBytes ) ) );

	stats->drawCalls++;
	if ( arenaBound ) {
		stats->drawArenaCalls++;
	} else {
		stats->drawLegacyBufferCalls++;
	}
	GLX_StaticWorld_RecordSubmittedRun( stats, drawIndexes, drawOffsetBytes, packet, manifestDraw );

	return qtrue;
}

qboolean GLX_StaticWorld_DrawDeviceRuns( StaticWorldStats *stats, int runCount,
	const int *counts, const void *const *offsets, const int *firstItems, const int *itemCounts,
	GLenum indexType, int indexBytes,
	const char *shaderName, int sort, qboolean arenaBound )
{
	const GLvoid *multiOffsets[GLX_STATIC_WORLD_MULTIDRAW_LIMIT];
	GLsizei multiCounts[GLX_STATIC_WORLD_MULTIDRAW_LIMIT];
	int runBytes[GLX_STATIC_WORLD_MULTIDRAW_LIMIT];
	int offsetBytes[GLX_STATIC_WORLD_MULTIDRAW_LIMIT];
	StaticWorldRunPacket packets[GLX_STATIC_WORLD_MULTIDRAW_LIMIT];
	qboolean manifestDraws[GLX_STATIC_WORLD_MULTIDRAW_LIMIT];
	StaticWorldDrawPolicy policy;
	unsigned int totalIndexes = 0;
	unsigned int manifestPacketRuns = 0;
	unsigned int manifestPacketIndexes = 0;

	if ( !stats || !GLX_StaticWorld_MultiDrawEnabled( *stats ) ) {
		return qfalse;
	}
	if ( runCount <= 1 ) {
		return qfalse;
	}

	stats->multiDrawAttempts++;

	if ( runCount > GLX_STATIC_WORLD_MULTIDRAW_LIMIT ||
		!counts || !offsets || !firstItems || !itemCounts || !GLX_StaticWorld_ResolveMultiDrawFn() ) {
		stats->multiDrawFallbacks++;
		return qfalse;
	}

	policy = GLX_StaticWorld_EffectiveDrawPolicy( *stats );
	for ( int i = 0; i < runCount; i++ ) {
		manifestDraws[i] = qfalse;
		offsetBytes[i] = static_cast<int>( reinterpret_cast<std::intptr_t>( offsets[i] ) );
		if ( !GLX_StaticWorld_ValidateRun( counts[i], offsetBytes[i], indexBytes, &runBytes[i] ) ||
			firstItems[i] <= 0 || itemCounts[i] <= 0 ) {
			stats->multiDrawFallbacks++;
			return qfalse;
		}
		packets[i] = GLX_StaticWorld_ClassifyRunPacketResult( stats, offsetBytes[i], runBytes[i],
			firstItems[i], itemCounts[i], shaderName, sort );
		if ( !GLX_StaticWorld_DrawPolicyAllows( policy, packets[i].match ) ) {
			stats->multiDrawFallbacks++;
			stats->drawPolicySkips++;
			return qfalse;
		}
		if ( packets[i].match == StaticWorldPacketMatch::Full &&
			packets[i].packetIndex >= 0 && packets[i].packetIndex < stats->packetCount ) {
			const StaticWorldPacketStats &manifestPacket = stats->packets[packets[i].packetIndex];
			if ( manifestPacket.indexes > 0 && manifestPacket.indexOffset >= 0 ) {
				multiCounts[i] = static_cast<GLsizei>( manifestPacket.indexes );
				multiOffsets[i] = reinterpret_cast<const GLvoid *>(
					static_cast<std::intptr_t>( manifestPacket.indexOffset ) );
				offsetBytes[i] = manifestPacket.indexOffset;
				manifestDraws[i] = qtrue;
				manifestPacketRuns++;
				manifestPacketIndexes += static_cast<unsigned int>( manifestPacket.indexes );
			} else {
				multiCounts[i] = static_cast<GLsizei>( counts[i] );
				multiOffsets[i] = offsets[i];
			}
		} else {
			multiCounts[i] = static_cast<GLsizei>( counts[i] );
			multiOffsets[i] = offsets[i];
		}
		totalIndexes += static_cast<unsigned int>( multiCounts[i] );
	}

	stats->drawAttempts += static_cast<unsigned int>( runCount );
	s_fns.MultiDrawElements( GL_TRIANGLES, multiCounts, indexType, multiOffsets,
		static_cast<GLsizei>( runCount ) );

	stats->drawCalls++;
	if ( arenaBound ) {
		stats->drawArenaCalls++;
	} else {
		stats->drawLegacyBufferCalls++;
	}
	stats->multiDrawCalls++;
	stats->multiDrawRuns += static_cast<unsigned int>( runCount );
	stats->multiDrawIndexes += totalIndexes;
	stats->drawManifestPacketCalls += manifestPacketRuns;
	stats->drawManifestPacketIndexes += manifestPacketIndexes;

	for ( int i = 0; i < runCount; i++ ) {
		GLX_StaticWorld_RecordSubmittedRun( stats, multiCounts[i], offsetBytes[i],
			packets[i], manifestDraws[i] );
	}

	return qtrue;
}

qboolean GLX_StaticWorld_DrawSoftIndexes( StaticWorldStats *stats, int indexes, const void *indexData,
	GLenum indexType, int indexBytes, const char *shaderName, int sort, qboolean arenaBound )
{
	(void)shaderName;
	(void)sort;

	if ( !stats || !GLX_StaticWorld_SoftDrawEnabled( *stats ) ) {
		return qfalse;
	}

	stats->softDrawAttempts++;
	stats->drawAttempts++;

	if ( indexes <= 0 || !indexData || indexBytes <= 0 || !GLX_StaticWorld_ResolveDrawFn() ) {
		stats->softDrawFallbacks++;
		stats->drawFallbacks++;
		return qfalse;
	}

	s_fns.DrawElements( GL_TRIANGLES, indexes, indexType, indexData );

	stats->drawCalls++;
	if ( arenaBound ) {
		stats->drawArenaCalls++;
	} else {
		stats->drawLegacyBufferCalls++;
	}
	stats->softDrawCalls++;
	GLX_StaticWorld_AddCounter( &stats->softDrawIndexes, static_cast<unsigned int>( indexes ) );
	GLX_StaticWorld_AddCounter( &stats->drawIndexes, static_cast<unsigned int>( indexes ) );
	if ( static_cast<unsigned int>( indexes ) > stats->largestSoftDrawIndexes ) {
		stats->largestSoftDrawIndexes = static_cast<unsigned int>( indexes );
	}
	if ( static_cast<unsigned int>( indexes ) > stats->largestDrawIndexes ) {
		stats->largestDrawIndexes = static_cast<unsigned int>( indexes );
	}
	stats->lastDrawOffset = 0;
	stats->lastDrawIndexes = static_cast<unsigned int>( indexes );

	return qtrue;
}

static int GLX_StaticWorld_FlushFilteredMultiDraw( StaticWorldStats *stats,
	StaticWorldPreparedRun *runs, int runCount, int *drawnRuns,
	GLenum indexType, qboolean arenaBound )
{
	GLsizei multiCounts[GLX_STATIC_WORLD_MULTIDRAW_LIMIT];
	const GLvoid *multiOffsets[GLX_STATIC_WORLD_MULTIDRAW_LIMIT];

	if ( !stats || !runs || runCount <= 1 ) {
		return 0;
	}

	for ( int i = 0; i < runCount; i++ ) {
		multiCounts[i] = runs[i].count;
		multiOffsets[i] = runs[i].offset;
	}

	s_fns.MultiDrawElements( GL_TRIANGLES, multiCounts, indexType, multiOffsets,
		static_cast<GLsizei>( runCount ) );

	GLX_StaticWorld_RecordMultiDrawSubmission( stats, runs, runCount, arenaBound );
	stats->multiDrawFilteredBatches++;
	stats->multiDrawFilteredRuns += static_cast<unsigned int>( runCount );

	if ( drawnRuns ) {
		for ( int i = 0; i < runCount; i++ ) {
			if ( runs[i].sourceIndex >= 0 ) {
				drawnRuns[runs[i].sourceIndex] = 1;
			}
		}
	}

	return runCount;
}

static int GLX_StaticWorld_FlushPacketMultiDraw( StaticWorldStats *stats,
	StaticWorldPreparedRun *runs, int runCount, int *drawnRuns,
	GLenum indexType, qboolean arenaBound )
{
	GLsizei multiCounts[GLX_STATIC_WORLD_MULTIDRAW_LIMIT];
	const GLvoid *multiOffsets[GLX_STATIC_WORLD_MULTIDRAW_LIMIT];
	unsigned int totalIndexes = 0;

	if ( !stats || !runs || runCount <= 1 ) {
		return 0;
	}

	for ( int i = 0; i < runCount; i++ ) {
		if ( !runs[i].manifestDraw || runs[i].count <= 0 ) {
			return 0;
		}
		multiCounts[i] = runs[i].count;
		multiOffsets[i] = runs[i].offset;
		GLX_StaticWorld_AddCounter( &totalIndexes, static_cast<unsigned int>( runs[i].count ) );
	}

	s_fns.MultiDrawElements( GL_TRIANGLES, multiCounts, indexType, multiOffsets,
		static_cast<GLsizei>( runCount ) );

	GLX_StaticWorld_RecordMultiDrawSubmission( stats, runs, runCount, arenaBound );
	stats->packetBatchBatches++;
	GLX_StaticWorld_AddCounter( &stats->packetBatchRuns, static_cast<unsigned int>( runCount ) );
	GLX_StaticWorld_AddCounter( &stats->packetBatchIndexes, totalIndexes );

	if ( drawnRuns ) {
		for ( int i = 0; i < runCount; i++ ) {
			if ( runs[i].sourceIndex >= 0 ) {
				drawnRuns[runs[i].sourceIndex] = 1;
			}
		}
	}

	return runCount;
}

static qboolean GLX_StaticWorld_FlushFilteredMultiDrawIndirect( StaticWorldStats *stats,
	StaticWorldPreparedRun *runs, int runCount, int *drawnRuns,
	GLenum indexType, qboolean arenaBound )
{
	StaticWorldIndirectCommandStats compactCommands[GLX_STATIC_WORLD_MULTIDRAW_LIMIT];
	GLuint oldDrawIndirectBuffer = 0;
	GLenum err;
	const GLvoid *commandOffset = nullptr;
	unsigned int totalIndexes = 0;
	qboolean useCompactBuffer = qfalse;
	qboolean contiguousCommands = qtrue;
	qboolean compactEnabled = qfalse;
	int firstCommand = -1;
	int uploadBytes = 0;

	if ( !stats || !GLX_StaticWorld_MultiDrawIndirectEnabled( *stats ) ) {
		return qfalse;
	}

	stats->multiDrawIndirectAttempts++;

	if ( !runs || runCount <= 1 ) {
		stats->multiDrawIndirectSkips++;
		stats->multiDrawIndirectShortBatches++;
		GLX_StaticWorld_RecordMdiReject( stats, GLX_STATIC_WORLD_MDI_REJECT_SHORT_BATCH,
			runs, runCount, -1 );
		return qfalse;
	}
	if ( !GLX_StaticWorld_ResolveFns() || !GLX_StaticWorld_ResolveMultiDrawIndirectFn() ) {
		stats->multiDrawIndirectFallbacks++;
		stats->multiDrawIndirectUnsupported++;
		GLX_StaticWorld_RecordMdiReject( stats, GLX_STATIC_WORLD_MDI_REJECT_UNSUPPORTED,
			runs, runCount, -1 );
		return qfalse;
	}

	compactEnabled = GLX_StaticWorld_CompactMultiDrawIndirectEnabled( *stats );

	for ( int i = 0; i < runCount; i++ ) {
		int commandIndex;
		const StaticWorldIndirectCommandStats *command;

		if ( !runs[i].manifestDraw ) {
			stats->multiDrawIndirectSkips++;
			stats->multiDrawIndirectNonManifest++;
			GLX_StaticWorld_RecordMdiReject( stats, GLX_STATIC_WORLD_MDI_REJECT_NON_MANIFEST,
				runs, runCount, -1 );
			return qfalse;
		}

		commandIndex = GLX_StaticWorld_CommandIndexForPreparedRun( stats, runs[i] );
		if ( commandIndex < 0 ) {
			stats->multiDrawIndirectSkips++;
			stats->multiDrawIndirectMissingCommand++;
			GLX_StaticWorld_RecordMdiReject( stats, GLX_STATIC_WORLD_MDI_REJECT_MISSING_COMMAND,
				runs, runCount, commandIndex );
			return qfalse;
		}
		if ( runs[i].packet.packetIndex < 0 || runs[i].packet.packetIndex >= stats->packetCount ) {
			stats->multiDrawIndirectSkips++;
			stats->multiDrawIndirectMissingCommand++;
			GLX_StaticWorld_RecordMdiReject( stats, GLX_STATIC_WORLD_MDI_REJECT_MISSING_COMMAND,
				runs, runCount, commandIndex );
			return qfalse;
		}

		command = &stats->indirectPackets[runs[i].packet.packetIndex];
		if ( command->count == 0 || command->instanceCount == 0 ) {
			stats->multiDrawIndirectSkips++;
			stats->multiDrawIndirectMissingCommand++;
			GLX_StaticWorld_RecordMdiReject( stats, GLX_STATIC_WORLD_MDI_REJECT_MISSING_COMMAND,
				runs, runCount, commandIndex );
			return qfalse;
		}

		compactCommands[i] = *command;
		if ( i == 0 ) {
			firstCommand = commandIndex;
		} else if ( commandIndex != firstCommand + i ) {
			contiguousCommands = qfalse;
		}

		totalIndexes += command->count;
	}

	if ( contiguousCommands && stats->indirectBufferReady && stats->indirectCommandBuffer &&
		static_cast<unsigned int>( firstCommand + runCount ) <= stats->indirectBufferCommands ) {
		commandOffset = reinterpret_cast<const GLvoid *>(
			static_cast<std::intptr_t>( firstCommand * static_cast<int>( sizeof( StaticWorldIndirectCommandStats ) ) ) );
	} else {
		if ( !compactEnabled ) {
			stats->multiDrawIndirectSkips++;
			if ( contiguousCommands ) {
				stats->multiDrawIndirectUnsupported++;
				GLX_StaticWorld_RecordMdiReject( stats, GLX_STATIC_WORLD_MDI_REJECT_UNSUPPORTED,
					runs, runCount, firstCommand );
			} else {
				stats->multiDrawIndirectNonContiguous++;
				GLX_StaticWorld_RecordMdiReject( stats, GLX_STATIC_WORLD_MDI_REJECT_NON_CONTIGUOUS,
					runs, runCount, firstCommand );
			}
			return qfalse;
		}

		useCompactBuffer = qtrue;
		commandOffset = nullptr;
		uploadBytes = runCount * static_cast<int>( sizeof( compactCommands[0] ) );
	}

	oldDrawIndirectBuffer = GLX_StaticWorld_CurrentIndirectBufferBinding( stats );
	GLX_StaticWorld_ClearGLErrors();

	if ( useCompactBuffer ) {
		if ( !stats->indirectCompactCommandBuffer ) {
			s_fns.GenBuffers( 1, &stats->indirectCompactCommandBuffer );
			err = s_fns.GetError();
			if ( err != GL_NO_ERROR || !stats->indirectCompactCommandBuffer ) {
				stats->multiDrawIndirectErrors++;
				stats->multiDrawIndirectFallbacks++;
				stats->multiDrawIndirectCompactFailures++;
				GLX_StaticWorld_RecordMdiReject( stats, GLX_STATIC_WORLD_MDI_REJECT_COMPACT_BUFFER,
					runs, runCount, -1 );
				GLX_StaticWorld_RestoreIndirectBufferBinding( stats, oldDrawIndirectBuffer );
				return qfalse;
			}
		}

		GLX_StaticWorld_BindIndirectBufferTracked( stats, stats->indirectCompactCommandBuffer );
		/*
		Always orphan: BufferSubData at offset 0 would add an implicit sync
		against earlier compact batches of this frame the GPU may still be
		consuming, while a fresh GL_STREAM_DRAW store lets the driver ghost
		the tiny command block without stalling.
		*/
		if ( stats->indirectCompactBufferCapacityBytes > 0 &&
			stats->indirectCompactBufferCapacityBytes < static_cast<unsigned int>( uploadBytes ) ) {
			stats->multiDrawIndirectCompactGrows++;
		}
		s_fns.BufferData( GL_DRAW_INDIRECT_BUFFER, uploadBytes, compactCommands, GL_STREAM_DRAW );
		stats->indirectCompactBufferCapacityBytes = static_cast<unsigned int>( uploadBytes );
		stats->multiDrawIndirectCompactOrphans++;
		err = s_fns.GetError();
		if ( err != GL_NO_ERROR ) {
			stats->multiDrawIndirectErrors++;
			stats->multiDrawIndirectFallbacks++;
			stats->multiDrawIndirectCompactFailures++;
			stats->indirectCompactBufferCapacityBytes = 0;
			stats->indirectCompactBufferBytes = 0;
			GLX_StaticWorld_RecordMdiReject( stats, GLX_STATIC_WORLD_MDI_REJECT_COMPACT_UPLOAD,
				runs, runCount, -1 );
			GLX_StaticWorld_RestoreIndirectBufferBinding( stats, oldDrawIndirectBuffer );
			return qfalse;
		}

		stats->indirectCompactBufferBytes = static_cast<unsigned int>( uploadBytes );
		stats->multiDrawIndirectCompactUploads++;
		GLX_StaticWorld_AddCounter( &stats->multiDrawIndirectCompactBytes,
			static_cast<unsigned int>( uploadBytes ) );
	} else {
		GLX_StaticWorld_BindIndirectBufferTracked( stats, stats->indirectCommandBuffer );
	}

	s_fns.MultiDrawElementsIndirect( GL_TRIANGLES, indexType, commandOffset,
		static_cast<GLsizei>( runCount ), 0 );
	err = s_fns.GetError();
	GLX_StaticWorld_RestoreIndirectBufferBinding( stats, oldDrawIndirectBuffer );

	if ( err != GL_NO_ERROR ) {
		stats->multiDrawIndirectErrors++;
		stats->multiDrawIndirectFallbacks++;
		if ( useCompactBuffer ) {
			stats->multiDrawIndirectCompactFailures++;
		}
		GLX_StaticWorld_RecordMdiReject( stats, GLX_STATIC_WORLD_MDI_REJECT_DRAW_ERROR,
			runs, runCount, useCompactBuffer ? -1 : firstCommand );
		return qfalse;
	}

	GLX_StaticWorld_ClearMdiReject( stats );
	GLX_StaticWorld_RecordMultiDrawSubmission( stats, runs, runCount, arenaBound );
	stats->multiDrawFilteredBatches++;
	stats->multiDrawFilteredRuns += static_cast<unsigned int>( runCount );
	stats->multiDrawIndirectCalls++;
	if ( useCompactBuffer ) {
		stats->multiDrawIndirectCompactCalls++;
	} else {
		stats->multiDrawIndirectStaticCalls++;
	}
	stats->multiDrawIndirectRuns += static_cast<unsigned int>( runCount );
	stats->multiDrawIndirectIndexes += totalIndexes;
	if ( static_cast<unsigned int>( runCount ) > stats->multiDrawIndirectLargestRun ) {
		stats->multiDrawIndirectLargestRun = static_cast<unsigned int>( runCount );
	}
	if ( totalIndexes > stats->multiDrawIndirectLargestIndexes ) {
		stats->multiDrawIndirectLargestIndexes = totalIndexes;
	}
	stats->multiDrawIndirectLastRuns = static_cast<unsigned int>( runCount );
	stats->multiDrawIndirectLastIndexes = totalIndexes;
	stats->multiDrawIndirectLastSource = useCompactBuffer ? 2u : 1u;
	stats->multiDrawIndirectLastFirstCommand = useCompactBuffer ? -1 : firstCommand;

	if ( drawnRuns ) {
		for ( int i = 0; i < runCount; i++ ) {
			if ( runs[i].sourceIndex >= 0 ) {
				drawnRuns[runs[i].sourceIndex] = 1;
			}
		}
	}

	return qtrue;
}

static int GLX_StaticWorld_FlushPreparedSingleDraw( StaticWorldStats *stats,
	const StaticWorldPreparedRun &run, int *drawnRuns, GLenum indexType, qboolean arenaBound,
	qboolean *usedIndirect )
{
	qboolean indirectDraw = qfalse;

	if ( usedIndirect ) {
		*usedIndirect = qfalse;
	}

	if ( !stats || run.count <= 0 ) {
		return 0;
	}

	if ( run.manifestDraw && GLX_StaticWorld_DrawPacketIndirect( stats, run.packet, indexType ) ) {
		indirectDraw = qtrue;
	} else {
		if ( !GLX_StaticWorld_ResolveDrawFn() ) {
			return 0;
		}
		s_fns.DrawElements( GL_TRIANGLES, run.count, indexType, run.offset );
	}

	stats->drawAttempts++;
	stats->drawCalls++;
	if ( arenaBound ) {
		stats->drawArenaCalls++;
	} else {
		stats->drawLegacyBufferCalls++;
	}
	if ( run.manifestDraw ) {
		stats->drawManifestPacketCalls++;
		GLX_StaticWorld_AddCounter( &stats->drawManifestPacketIndexes,
			static_cast<unsigned int>( run.count ) );
	}
	GLX_StaticWorld_RecordSubmittedRun( stats, run.count, run.offsetBytes,
		run.packet, run.manifestDraw );

	if ( drawnRuns && run.sourceIndex >= 0 ) {
		drawnRuns[run.sourceIndex] = 1;
	}
	if ( usedIndirect ) {
		*usedIndirect = indirectDraw;
	}

	return 1;
}

static int GLX_StaticWorld_FlushManifestCommandSpans( StaticWorldStats *stats,
	StaticWorldPreparedRun *runs, int runCount, int *drawnRuns,
	GLenum indexType, qboolean arenaBound, unsigned int *mdiRuns, unsigned int *fallbackRuns )
{
	int spanStarts[GLX_STATIC_WORLD_MULTIDRAW_LIMIT];
	int spanCounts[GLX_STATIC_WORLD_MULTIDRAW_LIMIT];
	int spanCount = 0;
	int singletonSpans = 0;
	int spanStart = 0;
	int lastCommand = -2;
	int drawnCount = 0;
	unsigned int localMdiRuns = 0;
	unsigned int localFallbackRuns = 0;
	unsigned int localSingletonDraws = 0;

	if ( mdiRuns ) {
		*mdiRuns = 0;
	}
	if ( fallbackRuns ) {
		*fallbackRuns = 0;
	}

	if ( !stats || !runs || runCount <= 1 ) {
		return 0;
	}
	if ( !stats->indirectBufferReady || !stats->indirectCommandBuffer ||
		!GLX_StaticWorld_ResolveFns() || !GLX_StaticWorld_ResolveMultiDrawIndirectFn() ) {
		return 0;
	}

	stats->multiDrawIndirectCommandSpanAttempts++;

	for ( int i = 0; i < runCount; i++ ) {
		const int commandIndex = GLX_StaticWorld_CommandIndexForPreparedRun( stats, runs[i] );

		if ( commandIndex < 0 ||
			static_cast<unsigned int>( commandIndex ) >= stats->indirectBufferCommands ) {
			return 0;
		}

		if ( i > spanStart && commandIndex != lastCommand + 1 ) {
			const int count = i - spanStart;
			spanStarts[spanCount] = spanStart;
			spanCounts[spanCount] = count;
			if ( count <= 1 ) {
				singletonSpans++;
			}
			spanCount++;
			spanStart = i;
		}

		lastCommand = commandIndex;
	}

	if ( spanStart < runCount ) {
		const int count = runCount - spanStart;
		spanStarts[spanCount] = spanStart;
		spanCounts[spanCount] = count;
		if ( count <= 1 ) {
			singletonSpans++;
		}
		spanCount++;
	}

	if ( spanCount <= 1 ) {
		return 0;
	}
	if ( singletonSpans > 0 && !GLX_StaticWorld_ResolveDrawFn() ) {
		stats->multiDrawIndirectCommandSpanSingletons += static_cast<unsigned int>( singletonSpans );
		return 0;
	}

	for ( int i = 0; i < spanCount; i++ ) {
		const int start = spanStarts[i];
		const int count = spanCounts[i];

		if ( count == 1 ) {
			qboolean singleIndirect = qfalse;
			const int singleDrawn = GLX_StaticWorld_FlushPreparedSingleDraw( stats,
				runs[start], drawnRuns, indexType, arenaBound, &singleIndirect );
			drawnCount += singleDrawn;
			if ( singleDrawn > 0 ) {
				localFallbackRuns += static_cast<unsigned int>( singleDrawn );
				localSingletonDraws += static_cast<unsigned int>( singleDrawn );
				stats->multiDrawIndirectCommandSpanSingletonDraws += static_cast<unsigned int>( singleDrawn );
				if ( singleIndirect ) {
					stats->multiDrawIndirectCommandSpanSingletonIndirectDraws += static_cast<unsigned int>( singleDrawn );
				}
			}
			continue;
		}

		if ( GLX_StaticWorld_FlushFilteredMultiDrawIndirect( stats, &runs[start],
			count, drawnRuns, indexType, arenaBound ) ) {
			drawnCount += count;
			localMdiRuns += static_cast<unsigned int>( count );
			continue;
		}

		const int fallbackDrawn = GLX_StaticWorld_FlushFilteredMultiDraw( stats,
			&runs[start], count, drawnRuns, indexType, arenaBound );
		drawnCount += fallbackDrawn;
		if ( fallbackDrawn > 0 ) {
			localFallbackRuns += static_cast<unsigned int>( fallbackDrawn );
		}
	}

	if ( drawnCount > 0 ) {
		stats->multiDrawIndirectCommandSpanBatches++;
		stats->multiDrawIndirectCommandSpanMdiRuns += localMdiRuns;
		stats->multiDrawIndirectCommandSpanFallbackRuns += localFallbackRuns;
		stats->multiDrawIndirectCommandSpanLastSegments = static_cast<unsigned int>( spanCount );
		stats->multiDrawIndirectCommandSpanLastMdiRuns = localMdiRuns;
		stats->multiDrawIndirectCommandSpanLastFallbackRuns = localFallbackRuns;
		stats->multiDrawIndirectCommandSpanLastSingletonDraws = localSingletonDraws;
		if ( static_cast<unsigned int>( spanCount ) > stats->multiDrawIndirectCommandSpanLargestSegments ) {
			stats->multiDrawIndirectCommandSpanLargestSegments = static_cast<unsigned int>( spanCount );
		}
		if ( mdiRuns ) {
			*mdiRuns = localMdiRuns;
		}
		if ( fallbackRuns ) {
			*fallbackRuns = localFallbackRuns;
		}
	}

	return drawnCount;
}

static int GLX_StaticWorld_FlushPacketDrawSpans( StaticWorldStats *stats,
	StaticWorldPreparedRun *runs, int runCount, int *drawnRuns,
	GLenum indexType, qboolean arenaBound )
{
	int manifestRuns = 0;
	int spanStart = 0;
	int drawnCount = 0;
	unsigned int localSegments = 0;
	unsigned int localPacketRuns = 0;
	unsigned int localFallbackRuns = 0;
	unsigned int localSingles = 0;

	if ( !stats || !runs || runCount <= 1 || !GLX_StaticWorld_PacketBatchEnabled( *stats ) ) {
		return 0;
	}

	stats->packetBatchAttempts++;

	for ( int i = 0; i < runCount; i++ ) {
		if ( runs[i].manifestDraw ) {
			manifestRuns++;
		}
	}
	if ( manifestRuns <= 0 ) {
		return 0;
	}
	if ( !GLX_StaticWorld_ResolveDrawFn() || !GLX_StaticWorld_ResolveMultiDrawFn() ) {
		return 0;
	}

	while ( spanStart < runCount ) {
		const qboolean packetSpan = runs[spanStart].manifestDraw ? qtrue : qfalse;
		int spanEnd = spanStart + 1;
		int spanDrawn = 0;

		while ( spanEnd < runCount &&
			( runs[spanEnd].manifestDraw ? qtrue : qfalse ) == packetSpan ) {
			spanEnd++;
		}

		const int spanCount = spanEnd - spanStart;
		localSegments++;

		if ( spanCount == 1 ) {
			spanDrawn = GLX_StaticWorld_FlushPreparedSingleDraw( stats,
				runs[spanStart], drawnRuns, indexType, arenaBound, nullptr );
			if ( spanDrawn > 0 ) {
				localSingles += static_cast<unsigned int>( spanDrawn );
				if ( packetSpan ) {
					localPacketRuns += static_cast<unsigned int>( spanDrawn );
				} else {
					localFallbackRuns += static_cast<unsigned int>( spanDrawn );
				}
			}
		} else if ( packetSpan ) {
			spanDrawn = GLX_StaticWorld_FlushPacketMultiDraw( stats,
				&runs[spanStart], spanCount, drawnRuns, indexType, arenaBound );
			if ( spanDrawn > 0 ) {
				localPacketRuns += static_cast<unsigned int>( spanDrawn );
			}
		} else {
			spanDrawn = GLX_StaticWorld_FlushFilteredMultiDraw( stats,
				&runs[spanStart], spanCount, drawnRuns, indexType, arenaBound );
			if ( spanDrawn > 0 ) {
				localFallbackRuns += static_cast<unsigned int>( spanDrawn );
			}
		}

		drawnCount += spanDrawn;
		spanStart = spanEnd;
	}

	if ( drawnCount > 0 ) {
		GLX_StaticWorld_AddCounter( &stats->packetBatchFallbackRuns, localFallbackRuns );
		GLX_StaticWorld_AddCounter( &stats->packetBatchSingleRuns, localSingles );
		stats->packetBatchLastSegments = localSegments;
		stats->packetBatchLastPacketRuns = localPacketRuns;
		stats->packetBatchLastFallbackRuns = localFallbackRuns;
		stats->packetBatchLastSingles = localSingles;
		if ( localSegments > stats->packetBatchLargestSegments ) {
			stats->packetBatchLargestSegments = localSegments;
		}
	}

	return drawnCount;
}

static int GLX_StaticWorld_FlushFilteredDrawBatch( StaticWorldStats *stats,
	StaticWorldPreparedRun *runs, int runCount, int *drawnRuns,
	GLenum indexType, qboolean arenaBound )
{
	if ( stats && GLX_StaticWorld_MultiDrawIndirectSpansEnabled( *stats ) &&
		runs && runCount > 1 ) {
		int drawnCount = 0;
		int spanStart = 0;
		unsigned int localSegments = 0;
		unsigned int localMdiRuns = 0;
		unsigned int localFallbackRuns = 0;
		unsigned int localSingles = 0;
		qboolean drewSpan = qfalse;

		stats->multiDrawIndirectSpanAttempts++;

		while ( spanStart < runCount ) {
			const qboolean mdiSpan = runs[spanStart].manifestDraw ? qtrue : qfalse;
			int spanEnd = spanStart + 1;

			while ( spanEnd < runCount &&
				( runs[spanEnd].manifestDraw ? qtrue : qfalse ) == mdiSpan ) {
				spanEnd++;
			}

			const int spanCount = spanEnd - spanStart;
			localSegments++;
			if ( spanCount <= 1 ) {
				qboolean singleIndirect = qfalse;
				const int singleDrawn = GLX_StaticWorld_FlushPreparedSingleDraw( stats,
					runs[spanStart], drawnRuns, indexType, arenaBound, &singleIndirect );

				drawnCount += singleDrawn;
				stats->multiDrawIndirectSpanSingles++;
				localSingles++;
				if ( singleDrawn > 0 ) {
					stats->multiDrawIndirectSpanSingleDraws += static_cast<unsigned int>( singleDrawn );
					if ( singleIndirect ) {
						stats->multiDrawIndirectSpanSingleIndirectDraws += static_cast<unsigned int>( singleDrawn );
					}
					localFallbackRuns += static_cast<unsigned int>( singleDrawn );
					drewSpan = qtrue;
				}
				spanStart = spanEnd;
				continue;
			}

			if ( mdiSpan ) {
				unsigned int commandMdiRuns = 0;
				unsigned int commandFallbackRuns = 0;
				const qboolean compactEnabled = GLX_StaticWorld_CompactMultiDrawIndirectEnabled( *stats );
				int commandSpanDrawn = 0;

				if ( !compactEnabled ) {
					commandSpanDrawn = GLX_StaticWorld_FlushManifestCommandSpans( stats,
						&runs[spanStart], spanCount, drawnRuns, indexType, arenaBound,
						&commandMdiRuns, &commandFallbackRuns );
				}
				if ( commandSpanDrawn > 0 ) {
					drawnCount += commandSpanDrawn;
					stats->multiDrawIndirectSpanMdiRuns += commandMdiRuns;
					stats->multiDrawIndirectSpanFallbackRuns += commandFallbackRuns;
					localMdiRuns += commandMdiRuns;
					localFallbackRuns += commandFallbackRuns;
					drewSpan = qtrue;
				} else if ( GLX_StaticWorld_FlushFilteredMultiDrawIndirect( stats, &runs[spanStart],
					spanCount, drawnRuns, indexType, arenaBound ) ) {
					drawnCount += spanCount;
					stats->multiDrawIndirectSpanMdiRuns += static_cast<unsigned int>( spanCount );
					localMdiRuns += static_cast<unsigned int>( spanCount );
					drewSpan = qtrue;
				} else {
					const int fallbackDrawn = GLX_StaticWorld_FlushFilteredMultiDraw( stats,
						&runs[spanStart], spanCount, drawnRuns, indexType, arenaBound );
					drawnCount += fallbackDrawn;
					if ( fallbackDrawn > 0 ) {
						stats->multiDrawIndirectSpanFallbackRuns += static_cast<unsigned int>( fallbackDrawn );
						localFallbackRuns += static_cast<unsigned int>( fallbackDrawn );
						drewSpan = qtrue;
					}
				}
			} else {
				const int fallbackDrawn = GLX_StaticWorld_FlushFilteredMultiDraw( stats,
					&runs[spanStart], spanCount, drawnRuns, indexType, arenaBound );
				drawnCount += fallbackDrawn;
				if ( fallbackDrawn > 0 ) {
					stats->multiDrawIndirectSpanFallbackRuns += static_cast<unsigned int>( fallbackDrawn );
					localFallbackRuns += static_cast<unsigned int>( fallbackDrawn );
					drewSpan = qtrue;
				}
			}

			spanStart = spanEnd;
		}

		if ( drewSpan ) {
			stats->multiDrawIndirectSpanBatches++;
			stats->multiDrawIndirectSpanLastSegments = localSegments;
			stats->multiDrawIndirectSpanLastMdiRuns = localMdiRuns;
			stats->multiDrawIndirectSpanLastFallbackRuns = localFallbackRuns;
			stats->multiDrawIndirectSpanLastSingles = localSingles;
			if ( localSegments > stats->multiDrawIndirectSpanLargestSegments ) {
				stats->multiDrawIndirectSpanLargestSegments = localSegments;
			}
			return drawnCount;
		}
	}

	{
		const int packetDrawn = GLX_StaticWorld_FlushPacketDrawSpans( stats, runs, runCount,
			drawnRuns, indexType, arenaBound );
		if ( packetDrawn > 0 ) {
			return packetDrawn;
		}
	}

	if ( GLX_StaticWorld_FlushFilteredMultiDrawIndirect( stats, runs, runCount,
		drawnRuns, indexType, arenaBound ) ) {
		return runCount;
	}

	return GLX_StaticWorld_FlushFilteredMultiDraw( stats, runs, runCount,
		drawnRuns, indexType, arenaBound );
}

int GLX_StaticWorld_DrawDeviceRunsFiltered( StaticWorldStats *stats, int runCount,
	const int *counts, const void *const *offsets, const int *firstItems, const int *itemCounts,
	int *drawnRuns, GLenum indexType, int indexBytes,
	const char *shaderName, int sort, qboolean arenaBound )
{
	StaticWorldPreparedRun batch[GLX_STATIC_WORLD_MULTIDRAW_LIMIT];
	StaticWorldDrawPolicy policy;
	int batchCount = 0;
	int drawnCount = 0;

	const auto flushPendingBatch = [&]() -> int {
		int flushed = 0;

		if ( batchCount > 1 ) {
			flushed = GLX_StaticWorld_FlushFilteredDrawBatch( stats, batch,
				batchCount, drawnRuns, indexType, arenaBound );
		} else if ( batchCount == 1 ) {
			stats->multiDrawFilteredSkips++;
		}

		batchCount = 0;
		return flushed;
	};

	if ( !stats || !GLX_StaticWorld_MultiDrawEnabled( *stats ) ) {
		return 0;
	}
	if ( runCount <= 1 ) {
		return 0;
	}

	stats->multiDrawFilteredAttempts++;
	stats->multiDrawFilteredLastBarrierReason = GLX_STATIC_WORLD_FILTERED_BARRIER_NONE;
	stats->multiDrawFilteredLastBarrierRun = -1;

	if ( !counts || !offsets || !firstItems || !itemCounts || !drawnRuns ||
		!GLX_StaticWorld_ResolveMultiDrawFn() ) {
		stats->multiDrawFallbacks++;
		return 0;
	}

	policy = GLX_StaticWorld_EffectiveDrawPolicy( *stats );

	for ( int i = 0; i < runCount; i++ ) {
		int runBytes;
		int offsetBytes;
		StaticWorldRunPacket packet;
		StaticWorldPreparedRun prepared {};

		offsetBytes = static_cast<int>( reinterpret_cast<std::intptr_t>( offsets[i] ) );
		if ( !GLX_StaticWorld_ValidateRun( counts[i], offsetBytes, indexBytes, &runBytes ) ||
			firstItems[i] <= 0 || itemCounts[i] <= 0 ) {
			drawnCount += flushPendingBatch();
			stats->multiDrawFilteredSkips++;
			GLX_StaticWorld_RecordFilteredBarrier( stats,
				GLX_STATIC_WORLD_FILTERED_BARRIER_INVALID, i );
			return drawnCount;
		}

		packet = GLX_StaticWorld_ClassifyRunPacketResult( stats, offsetBytes, runBytes,
			firstItems[i], itemCounts[i], shaderName, sort );
		if ( !GLX_StaticWorld_DrawPolicyAllows( policy, packet.match ) ) {
			drawnCount += flushPendingBatch();
			stats->multiDrawFilteredSkips++;
			stats->drawPolicySkips++;
			GLX_StaticWorld_RecordFilteredBarrier( stats,
				GLX_STATIC_WORLD_FILTERED_BARRIER_POLICY, i );
			return drawnCount;
		}

		prepared.sourceIndex = i;
		prepared.count = static_cast<GLsizei>( counts[i] );
		prepared.offset = offsets[i];
		prepared.offsetBytes = offsetBytes;
		prepared.packet = packet;
		prepared.manifestDraw = qfalse;

		if ( packet.match == StaticWorldPacketMatch::Full &&
			packet.packetIndex >= 0 && packet.packetIndex < stats->packetCount ) {
			const StaticWorldPacketStats &manifestPacket = stats->packets[packet.packetIndex];
			if ( manifestPacket.indexes > 0 && manifestPacket.indexOffset >= 0 ) {
				prepared.count = static_cast<GLsizei>( manifestPacket.indexes );
				prepared.offset = reinterpret_cast<const GLvoid *>(
					static_cast<std::intptr_t>( manifestPacket.indexOffset ) );
				prepared.offsetBytes = manifestPacket.indexOffset;
				prepared.manifestDraw = qtrue;
			}
		}

		stats->multiDrawFilteredCandidates++;
		batch[batchCount++] = prepared;
		if ( batchCount == GLX_STATIC_WORLD_MULTIDRAW_LIMIT ) {
			drawnCount += flushPendingBatch();
		}
	}

	drawnCount += flushPendingBatch();

	return drawnCount;
}

void GLX_StaticWorld_RecordQueue( StaticWorldStats *stats, int queuedItems, int queuedVertexes, int queuedIndexes,
	int deviceRuns, int deviceIndexes, int softIndexes, int largestDeviceRunIndexes )
{
	if ( !stats ) {
		return;
	}

	stats->lastQueueDeviceFullPacketRuns = 0;
	stats->lastQueueDevicePartialPacketRuns = 0;
	stats->lastQueueDevicePacketMisses = 0;
	stats->lastQueueDeviceItemMismatches = 0;
	stats->lastQueueDeviceIndirectCommandRuns = 0;
	stats->lastQueueDeviceIndirectCommands = 0;
	stats->lastQueueDeviceIndirectIndexes = 0;
	stats->lastQueueDeviceIndirectBreaks = 0;
	stats->lastQueueLargestIndirectCommandRun = 0;
	GLX_StaticWorld_ClearLastQueueCommandSpans( stats );

	if ( queuedItems <= 0 ) {
		return;
	}

	stats->queueBatches++;
	stats->queueItems += static_cast<unsigned int>( queuedItems > 0 ? queuedItems : 0 );
	stats->queueVertexes += static_cast<unsigned int>( queuedVertexes > 0 ? queuedVertexes : 0 );
	stats->queueIndexes += static_cast<unsigned int>( queuedIndexes > 0 ? queuedIndexes : 0 );
	stats->queueDeviceRuns += static_cast<unsigned int>( deviceRuns > 0 ? deviceRuns : 0 );
	stats->queueDeviceIndexes += static_cast<unsigned int>( deviceIndexes > 0 ? deviceIndexes : 0 );
	stats->queueSoftIndexes += static_cast<unsigned int>( softIndexes > 0 ? softIndexes : 0 );

	if ( queuedIndexes > 0 && static_cast<unsigned int>( queuedIndexes ) > stats->largestQueueIndexes ) {
		stats->largestQueueIndexes = static_cast<unsigned int>( queuedIndexes );
	}
	if ( largestDeviceRunIndexes > 0 &&
		static_cast<unsigned int>( largestDeviceRunIndexes ) > stats->largestDeviceRunIndexes ) {
		stats->largestDeviceRunIndexes = static_cast<unsigned int>( largestDeviceRunIndexes );
	}

	stats->lastQueueItems = static_cast<unsigned int>( queuedItems > 0 ? queuedItems : 0 );
	stats->lastQueueVertexes = static_cast<unsigned int>( queuedVertexes > 0 ? queuedVertexes : 0 );
	stats->lastQueueIndexes = static_cast<unsigned int>( queuedIndexes > 0 ? queuedIndexes : 0 );
	stats->lastQueueDeviceRuns = static_cast<unsigned int>( deviceRuns > 0 ? deviceRuns : 0 );
	stats->lastQueueDeviceIndexes = static_cast<unsigned int>( deviceIndexes > 0 ? deviceIndexes : 0 );
	stats->lastQueueSoftIndexes = static_cast<unsigned int>( softIndexes > 0 ? softIndexes : 0 );
	stats->lastQueueLargestDeviceRunIndexes = static_cast<unsigned int>( largestDeviceRunIndexes > 0 ? largestDeviceRunIndexes : 0 );
}

void GLX_StaticWorld_RecordDeviceRuns( StaticWorldStats *stats, int runCount,
	const int *counts, const void *const *offsets, const int *firstItems, const int *itemCounts,
	int indexBytes, const char *shaderName, int sort )
{
	unsigned int full = 0;
	unsigned int partial = 0;
	unsigned int misses = 0;
	unsigned int itemMismatches = 0;
	unsigned int commandRuns = 0;
	unsigned int commandCount = 0;
	unsigned int commandIndexes = 0;
	unsigned int commandBreaks = 0;
	unsigned int currentCommandRun = 0;
	unsigned int currentCommandIndexes = 0;
	unsigned int largestCommandRun = 0;
	unsigned int largestCommandSpanIndexes = 0;
	int currentFirstCommand = -1;
	int currentFirstPacket = -1;
	int currentLastPacket = -1;
	int currentFirstSourceRun = -1;
	int currentLastSourceRun = -1;
	int lastCommandIndex = -2;

	const auto finishCommandRun = [&]() {
		if ( currentCommandRun == 0 ) {
			return;
		}
		commandRuns++;
		if ( currentCommandRun > largestCommandRun ) {
			largestCommandRun = currentCommandRun;
		}
		if ( currentCommandIndexes > largestCommandSpanIndexes ) {
			largestCommandSpanIndexes = currentCommandIndexes;
		}
		if ( stats->lastQueueCommandSpanCount < GLX_STATIC_WORLD_QUEUE_COMMAND_SPAN_LIMIT ) {
			StaticWorldCommandSpanStats &span =
				stats->lastQueueCommandSpans[stats->lastQueueCommandSpanCount++];
			span.firstCommand = currentFirstCommand;
			span.commandCount = static_cast<int>( currentCommandRun );
			span.indexes = currentCommandIndexes;
			span.firstPacket = currentFirstPacket;
			span.lastPacket = currentLastPacket;
			span.firstSourceRun = currentFirstSourceRun;
			span.lastSourceRun = currentLastSourceRun;
		} else {
			stats->lastQueueCommandSpanOverflow++;
		}
		currentCommandRun = 0;
		currentCommandIndexes = 0;
		currentFirstCommand = -1;
		currentFirstPacket = -1;
		currentLastPacket = -1;
		currentFirstSourceRun = -1;
		currentLastSourceRun = -1;
		lastCommandIndex = -2;
	};

	if ( !stats ) {
		return;
	}

	GLX_StaticWorld_ClearLastQueueCommandSpans( stats );

	if ( runCount <= 0 ) {
		return;
	}

	for ( int i = 0; i < runCount; i++ ) {
		int offsetBytes;
		int runBytes;
		StaticWorldRunPacket packet;
		StaticWorldPacketStats *packetStats;
		int commandIndex;

		if ( !counts || !offsets || !firstItems || !itemCounts ) {
			finishCommandRun();
			misses++;
			continue;
		}

		offsetBytes = static_cast<int>( reinterpret_cast<std::intptr_t>( offsets[i] ) );
		if ( !GLX_StaticWorld_ValidateRun( counts[i], offsetBytes, indexBytes, &runBytes ) ) {
			finishCommandRun();
			misses++;
			continue;
		}

		packet = GLX_StaticWorld_ClassifyRunPacketResult( stats, offsetBytes, runBytes,
			firstItems[i], itemCounts[i], shaderName, sort );
		packetStats = GLX_StaticWorld_PacketForResult( stats, packet );

		switch ( packet.match ) {
		case StaticWorldPacketMatch::Full:
			full++;
			if ( packetStats ) {
				packetStats->queueFullRuns++;
			}
			commandIndex = GLX_StaticWorld_CommandIndexForPacket( stats, packet );
			if ( commandIndex < 0 ) {
				finishCommandRun();
				commandBreaks++;
				break;
			}
			if ( currentCommandRun > 0 && commandIndex != lastCommandIndex + 1 ) {
				finishCommandRun();
				commandBreaks++;
			}
			if ( currentCommandRun == 0 ) {
				currentFirstCommand = commandIndex;
				currentFirstPacket = packet.packetIndex;
				currentFirstSourceRun = i;
			}
			currentCommandRun++;
			commandCount++;
			if ( packetStats && packetStats->indexes > 0 ) {
				GLX_StaticWorld_AddCounter( &commandIndexes,
					static_cast<unsigned int>( packetStats->indexes ) );
				GLX_StaticWorld_AddCounter( &currentCommandIndexes,
					static_cast<unsigned int>( packetStats->indexes ) );
			}
			currentLastPacket = packet.packetIndex;
			currentLastSourceRun = i;
			lastCommandIndex = commandIndex;
			break;
		case StaticWorldPacketMatch::Partial:
			finishCommandRun();
			partial++;
			if ( packetStats ) {
				packetStats->queuePartialRuns++;
			}
			break;
		case StaticWorldPacketMatch::ItemMismatch:
			finishCommandRun();
			itemMismatches++;
			break;
		case StaticWorldPacketMatch::NoMatch:
		default:
			finishCommandRun();
			misses++;
			break;
		}
	}

	finishCommandRun();

	stats->queueDeviceFullPacketRuns += full;
	stats->queueDevicePartialPacketRuns += partial;
	stats->queueDevicePacketMisses += misses;
	stats->queueDeviceItemMismatches += itemMismatches;
	GLX_StaticWorld_AddCounter( &stats->queueDeviceIndirectCommandRuns, commandRuns );
	GLX_StaticWorld_AddCounter( &stats->queueDeviceIndirectCommands, commandCount );
	GLX_StaticWorld_AddCounter( &stats->queueDeviceIndirectIndexes, commandIndexes );
	GLX_StaticWorld_AddCounter( &stats->queueDeviceIndirectBreaks, commandBreaks );
	if ( largestCommandRun > stats->largestIndirectCommandRun ) {
		stats->largestIndirectCommandRun = largestCommandRun;
	}
	if ( largestCommandSpanIndexes > stats->largestIndirectCommandSpanIndexes ) {
		stats->largestIndirectCommandSpanIndexes = largestCommandSpanIndexes;
	}
	stats->lastQueueDeviceFullPacketRuns = full;
	stats->lastQueueDevicePartialPacketRuns = partial;
	stats->lastQueueDevicePacketMisses = misses;
	stats->lastQueueDeviceItemMismatches = itemMismatches;
	stats->lastQueueDeviceIndirectCommandRuns = commandRuns;
	stats->lastQueueDeviceIndirectCommands = commandCount;
	stats->lastQueueDeviceIndirectIndexes = commandIndexes;
	stats->lastQueueDeviceIndirectBreaks = commandBreaks;
	stats->lastQueueLargestIndirectCommandRun = largestCommandRun;
	stats->lastQueueLargestIndirectCommandSpanIndexes = largestCommandSpanIndexes;
}

float GLX_StaticWorld_TotalMegabytes( const StaticWorldStats &stats )
{
	return ( stats.vertexBytes + stats.indexBytes ) / ( 1024.0f * 1024.0f );
}

static unsigned long long GLX_StaticWorld_PacketHeat( const StaticWorldPacketStats &packet )
{
	const unsigned long long indexes = static_cast<unsigned long long>( packet.indexes > 0 ? packet.indexes : 1 );
	const unsigned long long queueFull = static_cast<unsigned long long>( packet.queueFullRuns ) * indexes;
	const unsigned long long queuePartial = static_cast<unsigned long long>( packet.queuePartialRuns ) * ( indexes / 2 + 1 );
	const unsigned long long drawFull = static_cast<unsigned long long>( packet.drawFullRuns ) * indexes * 2;
	const unsigned long long drawPartial = static_cast<unsigned long long>( packet.drawPartialRuns ) * ( indexes + 1 );
	const unsigned long long manifest = static_cast<unsigned long long>( packet.drawManifestRuns ) * indexes * 4;
	const unsigned long long drawnIndexes = static_cast<unsigned long long>( packet.drawIndexes ) * 2;

	return queueFull + queuePartial + drawFull + drawPartial + manifest + drawnIndexes;
}

void GLX_StaticWorld_PrintInfo( const StaticWorldStats &stats )
{
	RI().Printf( PRINT_ALL, "  static world cache: %i surfaces, %i verts, %i indexes, %.2f MB\n",
		stats.surfaces, stats.vertexes, stats.indexes, GLX_StaticWorld_TotalMegabytes( stats ) );
	RI().Printf( PRINT_ALL, "  static world batches: %i, largest %i surfaces\n",
		stats.batches, stats.largestBatchSurfaces );
	RI().Printf( PRINT_ALL, "  static world surface mix: %i faces, %i grids, %i triangle meshes\n",
		stats.faceSurfaces, stats.gridSurfaces, stats.triangleSurfaces );
	RI().Printf( PRINT_ALL, "  static world shader stages: %i total passes, max %i per shader\n",
		stats.shaderStagePasses, stats.maxShaderStages );
	RI().Printf( PRINT_ALL, "  static world cache generations: %u\n", stats.generations );
	RI().Printf( PRINT_ALL, "  static world packets: %i stored, %i overflow, %i surfaces, %i verts, %i indexes\n",
		stats.packetCount, stats.packetOverflow, stats.packetSurfaces, stats.packetVertexes, stats.packetIndexes );
	RI().Printf( PRINT_ALL, "  static world packet lookup: %u mapped, max item %i/%i, hits %u, misses %u, fallbacks %u, mismatches %u, overflows %u\n",
		stats.itemLookupMappedItems, stats.itemLookupMaxItem, GLX_STATIC_WORLD_ITEM_LOOKUP_LIMIT,
		stats.itemLookupHits, stats.itemLookupMisses, stats.itemLookupFallbacks,
		stats.itemLookupMismatches, stats.itemLookupOverflows );
	RI().Printf( PRINT_ALL, "  static world indirect packets: %u commands, %u indexes, %u bytes, invalid %u, misaligned %u\n",
		stats.indirectPacketCount, stats.indirectPacketIndexes, stats.indirectPacketBytes,
		stats.indirectPacketInvalid, stats.indirectPacketMisaligned );
	RI().Printf( PRINT_ALL, "  static world indirect caps: draw %s, multidraw %s\n",
		BoolName( stats.drawIndirectAvailable ), BoolName( stats.multiDrawIndirectAvailable ) );
	RI().Printf( PRINT_ALL, "  static world indirect buffer: %s, builds %u, skips %u, unsupported %u, failures %u, %u commands, %u bytes\n",
		BoolName( stats.indirectBufferReady ), stats.indirectBufferBuilds,
		stats.indirectBufferSkips, stats.indirectBufferUnsupported,
		stats.indirectBufferFailures, stats.indirectBufferCommands,
		stats.indirectBufferBytes );
	RI().Printf( PRINT_ALL, "  static world indirect draw: %s, %u/%u calls, %u indexes, fallbacks %u, skips %u, no-command %u, errors %u\n",
		BoolName( stats.r_glxStaticWorldIndirectDraw && stats.r_glxStaticWorldIndirectDraw->integer ? qtrue : qfalse ),
		stats.indirectDrawCalls, stats.indirectDrawAttempts,
		stats.indirectDrawIndexes, stats.indirectDrawFallbacks,
		stats.indirectDrawSkips, stats.indirectDrawNoCommand,
		stats.indirectDrawErrors );
	RI().Printf( PRINT_ALL, "  static world largest packet: %s, items %i+%i, %i surfaces, %i verts, %i indexes, vbo %i+%i, ibo %i+%i, %i passes\n",
		stats.largestPacket.shaderName[0] ? stats.largestPacket.shaderName : "n/a",
		stats.largestPacket.firstItem, stats.largestPacket.itemCount,
		stats.largestPacket.surfaces, stats.largestPacket.vertexes,
		stats.largestPacket.indexes, stats.largestPacket.vertexOffset,
		stats.largestPacket.vertexBytes, stats.largestPacket.indexOffset,
		stats.largestPacket.indexBytes, stats.largestPacket.shaderStagePasses );
	RI().Printf( PRINT_ALL, "  static world GLx arena: %s, builds %u, skips %u, failures %u, binds v%u/i%u, draw skips %u, %.2f MB\n",
		BoolName( stats.arenaReady ), stats.arenaBuilds, stats.arenaSkips, stats.arenaFailures,
		stats.arenaVertexBinds, stats.arenaIndexBinds, stats.arenaDrawSkips,
		( stats.arenaVertexBytes + stats.arenaIndexBytes ) / ( 1024.0f * 1024.0f ) );
	RI().Printf( PRINT_ALL, "  static world GLx renderer: %s, arena upload %s, arena draw %s\n",
		BoolName( GLX_StaticWorld_WorldRendererEnabled( stats ) ),
		BoolName( GLX_StaticWorld_ArenaUploadEnabled( stats ) ),
		BoolName( GLX_StaticWorld_ArenaDrawEnabled( stats ) ) );
	RI().Printf( PRINT_ALL, "  static world GLx draw: %s, soft %s, policy %s, %u/%u calls, %u indexes, fallbacks %u, policy skips %u\n",
		BoolName( GLX_StaticWorld_DeviceDrawEnabled( stats ) ),
		BoolName( GLX_StaticWorld_SoftDrawEnabled( stats ) ),
		GLX_StaticWorld_DrawPolicyName( GLX_StaticWorld_EffectiveDrawPolicy( stats ) ),
		stats.drawCalls, stats.drawAttempts, stats.drawIndexes, stats.drawFallbacks,
		stats.drawPolicySkips );
	RI().Printf( PRINT_ALL, "  static world GLx draw packets: hits %u full/%u partial, misses %u, item mismatches %u\n",
		stats.drawPacketFullHits, stats.drawPacketPartialHits,
		stats.drawPacketMisses, stats.drawPacketItemMismatches );
	RI().Printf( PRINT_ALL, "  static world GLx draw source: arena %u, legacy-buffer %u, largest %u indexes, last %u+%u\n",
		stats.drawArenaCalls, stats.drawLegacyBufferCalls, stats.largestDrawIndexes,
		stats.lastDrawOffset, stats.lastDrawIndexes );
	RI().Printf( PRINT_ALL, "  static world GLx manifest draws: %u packet runs, %u indexes\n",
		stats.drawManifestPacketCalls, stats.drawManifestPacketIndexes );
	RI().Printf( PRINT_ALL, "  static world GLx soft draws: %u/%u calls, %u indexes, fallbacks %u, largest %u indexes\n",
		stats.softDrawCalls, stats.softDrawAttempts, stats.softDrawIndexes,
		stats.softDrawFallbacks, stats.largestSoftDrawIndexes );
	RI().Printf( PRINT_ALL, "  static world GLx multidraw: %s, %u calls, %u runs, %u indexes, attempts %u, fallbacks %u\n",
		BoolName( GLX_StaticWorld_MultiDrawEnabled( stats ) ),
		stats.multiDrawCalls, stats.multiDrawRuns, stats.multiDrawIndexes,
		stats.multiDrawAttempts, stats.multiDrawFallbacks );
	RI().Printf( PRINT_ALL, "  static world GLx filtered multidraw: attempts %u, batches %u, runs %u, candidates %u, skips %u\n",
		stats.multiDrawFilteredAttempts, stats.multiDrawFilteredBatches,
		stats.multiDrawFilteredRuns, stats.multiDrawFilteredCandidates,
		stats.multiDrawFilteredSkips );
	RI().Printf( PRINT_ALL, "  static world GLx filtered multidraw barriers: %u total, invalid %u, policy %u, last %s at run %i\n",
		stats.multiDrawFilteredOrderBarriers,
		stats.multiDrawFilteredInvalidBarriers,
		stats.multiDrawFilteredPolicyBarriers,
		GLX_StaticWorld_FilteredBarrierName( stats.multiDrawFilteredLastBarrierReason ),
		stats.multiDrawFilteredLastBarrierRun );
	RI().Printf( PRINT_ALL, "  static world GLx packet batches: %s, attempts %u, batches %u, packet runs %u/%u indexes, fallback runs %u, singles %u\n",
		BoolName( GLX_StaticWorld_PacketBatchEnabled( stats ) ),
		stats.packetBatchAttempts,
		stats.packetBatchBatches,
		stats.packetBatchRuns,
		stats.packetBatchIndexes,
		stats.packetBatchFallbackRuns,
		stats.packetBatchSingleRuns );
	RI().Printf( PRINT_ALL, "  static world GLx packet batch shape: last %u segments, packet runs %u, fallback runs %u, singles %u, largest %u segments\n",
		stats.packetBatchLastSegments,
		stats.packetBatchLastPacketRuns,
		stats.packetBatchLastFallbackRuns,
		stats.packetBatchLastSingles,
		stats.packetBatchLargestSegments );
	RI().Printf( PRINT_ALL, "  static world GLx multidraw indirect: %s, %u/%u calls, %u runs, %u indexes, fallbacks %u, skips %u, errors %u, largest %u\n",
		BoolName( stats.r_glxStaticWorldMultiDrawIndirect &&
			stats.r_glxStaticWorldMultiDrawIndirect->integer ? qtrue : qfalse ),
		stats.multiDrawIndirectCalls, stats.multiDrawIndirectAttempts,
		stats.multiDrawIndirectRuns, stats.multiDrawIndirectIndexes,
		stats.multiDrawIndirectFallbacks, stats.multiDrawIndirectSkips,
		stats.multiDrawIndirectErrors, stats.multiDrawIndirectLargestRun );
	RI().Printf( PRINT_ALL, "  static world GLx multidraw indirect shape: last %s %u runs/%u indexes/first command %i, largest %u runs/%u indexes\n",
		GLX_StaticWorld_MdiSourceName( stats.multiDrawIndirectLastSource ),
		stats.multiDrawIndirectLastRuns, stats.multiDrawIndirectLastIndexes,
		stats.multiDrawIndirectLastFirstCommand,
		stats.multiDrawIndirectLargestRun, stats.multiDrawIndirectLargestIndexes );
	RI().Printf( PRINT_ALL, "  static world GLx multidraw indirect sources: static %u, compact %s/%u calls, uploads %u, subdata %u, orphans %u, grows %u, upload bytes %u, failures %u, buffer %u/%u bytes\n",
		stats.multiDrawIndirectStaticCalls,
		BoolName( stats.r_glxStaticWorldMultiDrawIndirectCompact &&
			stats.r_glxStaticWorldMultiDrawIndirectCompact->integer ? qtrue : qfalse ),
		stats.multiDrawIndirectCompactCalls,
		stats.multiDrawIndirectCompactUploads,
		stats.multiDrawIndirectCompactSubDatas,
		stats.multiDrawIndirectCompactOrphans,
		stats.multiDrawIndirectCompactGrows,
		stats.multiDrawIndirectCompactBytes,
		stats.multiDrawIndirectCompactFailures,
		stats.indirectCompactBufferBytes,
		stats.indirectCompactBufferCapacityBytes );
	RI().Printf( PRINT_ALL, "  static world GLx multidraw indirect last reject: %s, %u runs, %u indexes, command %i\n",
		GLX_StaticWorld_MdiRejectName( stats.multiDrawIndirectLastRejectReason ),
		stats.multiDrawIndirectLastRejectRuns,
		stats.multiDrawIndirectLastRejectIndexes,
		stats.multiDrawIndirectLastRejectCommand );
	RI().Printf( PRINT_ALL, "  static world GLx multidraw indirect spans: %s, attempts %u, batches %u, mdi runs %u, fallback runs %u, singles %u, single draws %u/%u indirect\n",
		BoolName( stats.r_glxStaticWorldMultiDrawIndirectSpans &&
			stats.r_glxStaticWorldMultiDrawIndirectSpans->integer ? qtrue : qfalse ),
		stats.multiDrawIndirectSpanAttempts, stats.multiDrawIndirectSpanBatches,
		stats.multiDrawIndirectSpanMdiRuns, stats.multiDrawIndirectSpanFallbackRuns,
		stats.multiDrawIndirectSpanSingles,
		stats.multiDrawIndirectSpanSingleDraws,
		stats.multiDrawIndirectSpanSingleIndirectDraws );
	RI().Printf( PRINT_ALL, "  static world GLx multidraw indirect span shape: last %u segments, %u mdi runs, %u fallback runs, %u singles; largest %u segments\n",
		stats.multiDrawIndirectSpanLastSegments,
		stats.multiDrawIndirectSpanLastMdiRuns,
		stats.multiDrawIndirectSpanLastFallbackRuns,
		stats.multiDrawIndirectSpanLastSingles,
		stats.multiDrawIndirectSpanLargestSegments );
	RI().Printf( PRINT_ALL, "  static world GLx multidraw indirect command spans: attempts %u, batches %u, mdi runs %u, fallback runs %u, singleton draws %u/%u indirect, singleton blocks %u\n",
		stats.multiDrawIndirectCommandSpanAttempts,
		stats.multiDrawIndirectCommandSpanBatches,
		stats.multiDrawIndirectCommandSpanMdiRuns,
		stats.multiDrawIndirectCommandSpanFallbackRuns,
		stats.multiDrawIndirectCommandSpanSingletonDraws,
		stats.multiDrawIndirectCommandSpanSingletonIndirectDraws,
		stats.multiDrawIndirectCommandSpanSingletons );
	RI().Printf( PRINT_ALL, "  static world GLx multidraw indirect command span shape: last %u segments, %u mdi runs, %u fallback runs, %u singleton draws; largest %u segments\n",
		stats.multiDrawIndirectCommandSpanLastSegments,
		stats.multiDrawIndirectCommandSpanLastMdiRuns,
		stats.multiDrawIndirectCommandSpanLastFallbackRuns,
		stats.multiDrawIndirectCommandSpanLastSingletonDraws,
		stats.multiDrawIndirectCommandSpanLargestSegments );
	RI().Printf( PRINT_ALL, "  static world GLx multidraw indirect reasons: unsupported %u, short %u, nonmanifest %u, missing-command %u, noncontiguous %u\n",
		stats.multiDrawIndirectUnsupported, stats.multiDrawIndirectShortBatches,
		stats.multiDrawIndirectNonManifest, stats.multiDrawIndirectMissingCommand,
		stats.multiDrawIndirectNonContiguous );
	RI().Printf( PRINT_ALL, "  static world queue batches: %u, last %u items, %u verts, %u indexes\n",
		stats.queueBatches, stats.lastQueueItems, stats.lastQueueVertexes, stats.lastQueueIndexes );
	RI().Printf( PRINT_ALL, "  static world queue split: device %u indexes in %u runs, soft %u indexes, largest run %u indexes\n",
		stats.queueDeviceIndexes, stats.queueDeviceRuns, stats.queueSoftIndexes, stats.largestDeviceRunIndexes );
	RI().Printf( PRINT_ALL, "  static world queue packets: full %u, partial %u, misses %u, item mismatches %u; last full %u, partial %u, misses %u\n",
		stats.queueDeviceFullPacketRuns, stats.queueDevicePartialPacketRuns,
		stats.queueDevicePacketMisses, stats.queueDeviceItemMismatches,
		stats.lastQueueDeviceFullPacketRuns, stats.lastQueueDevicePartialPacketRuns,
		stats.lastQueueDevicePacketMisses );
	RI().Printf( PRINT_ALL, "  static world queue indirect commands: %u commands in %u runs, %u indexes, breaks %u, largest %u cmds/%u idx; last %u commands/%u runs/%u indexes, breaks %u, largest %u cmds/%u idx, stored spans %u+%u overflow\n",
		stats.queueDeviceIndirectCommands, stats.queueDeviceIndirectCommandRuns,
		stats.queueDeviceIndirectIndexes, stats.queueDeviceIndirectBreaks,
		stats.largestIndirectCommandRun, stats.largestIndirectCommandSpanIndexes,
		stats.lastQueueDeviceIndirectCommands,
		stats.lastQueueDeviceIndirectCommandRuns, stats.lastQueueDeviceIndirectIndexes,
		stats.lastQueueDeviceIndirectBreaks, stats.lastQueueLargestIndirectCommandRun,
		stats.lastQueueLargestIndirectCommandSpanIndexes,
		stats.lastQueueCommandSpanCount, stats.lastQueueCommandSpanOverflow );
}

void GLX_StaticWorld_PrintPackets( const StaticWorldStats &stats, int limit )
{
	if ( limit < 0 ) {
		limit = 0;
	}
	if ( limit > stats.packetCount ) {
		limit = stats.packetCount;
	}

	GLX_StaticWorld_PrintInfo( stats );

	RI().Printf( PRINT_ALL, "  static world packet list: showing %i of %i%s\n",
		limit, stats.packetCount, stats.packetOverflow ? " (overflowed)" : "" );
	for ( int i = 0; i < limit; i++ ) {
		const StaticWorldPacketStats &packet = stats.packets[i];

		RI().Printf( PRINT_ALL,
			"    #%i: %s sort %i, items %i+%i, %i surfaces, %i verts, %i indexes, vbo %i+%i, ibo %i+%i, passes %i, flags 0x%x, cmd %i, q %u/%u, d %u/%u, manifest %u/%u\n",
			i + 1, packet.shaderName[0] ? packet.shaderName : "<unnamed>",
			packet.sort, packet.firstItem, packet.itemCount,
			packet.surfaces, packet.vertexes, packet.indexes,
			packet.vertexOffset, packet.vertexBytes, packet.indexOffset, packet.indexBytes,
			packet.shaderStagePasses, packet.flags, stats.indirectPacketToCommand[i],
			packet.queueFullRuns, packet.queuePartialRuns,
			packet.drawFullRuns, packet.drawPartialRuns,
			packet.drawManifestRuns, packet.drawIndexes );
	}
}

void GLX_StaticWorld_PrintHotPackets( const StaticWorldStats &stats, int limit )
{
	int order[GLX_STATIC_WORLD_PACKET_LIMIT];
	unsigned long long heat[GLX_STATIC_WORLD_PACKET_LIMIT];
	int hotCount = 0;

	if ( limit < 0 ) {
		limit = 0;
	}
	if ( limit > 128 ) {
		limit = 128;
	}

	GLX_StaticWorld_PrintInfo( stats );

	for ( int i = 0; i < stats.packetCount; i++ ) {
		const unsigned long long packetHeat = GLX_StaticWorld_PacketHeat( stats.packets[i] );
		int insertAt;

		if ( packetHeat == 0 ) {
			continue;
		}

		insertAt = hotCount;
		while ( insertAt > 0 && packetHeat > heat[insertAt - 1] ) {
			if ( insertAt < GLX_STATIC_WORLD_PACKET_LIMIT ) {
				heat[insertAt] = heat[insertAt - 1];
				order[insertAt] = order[insertAt - 1];
			}
			insertAt--;
		}

		if ( insertAt < GLX_STATIC_WORLD_PACKET_LIMIT ) {
			heat[insertAt] = packetHeat;
			order[insertAt] = i;
			if ( hotCount < GLX_STATIC_WORLD_PACKET_LIMIT ) {
				hotCount++;
			}
		}
	}

	if ( limit > hotCount ) {
		limit = hotCount;
	}

	RI().Printf( PRINT_ALL, "  static world hot packets: showing %i of %i hot packets from %i stored\n",
		limit, hotCount, stats.packetCount );
	for ( int i = 0; i < limit; i++ ) {
		const int packetIndex = order[i];
		const StaticWorldPacketStats &packet = stats.packets[packetIndex];

		RI().Printf( PRINT_ALL,
			"    #%i heat %llu: packet %i %s sort %i, cmd %i, items %i+%i, %i surfaces, %i indexes, q %u/%u, d %u/%u, manifest %u/%u\n",
			i + 1, heat[i], packetIndex + 1,
			packet.shaderName[0] ? packet.shaderName : "<unnamed>",
			packet.sort, stats.indirectPacketToCommand[packetIndex],
			packet.firstItem, packet.itemCount,
			packet.surfaces, packet.indexes,
			packet.queueFullRuns, packet.queuePartialRuns,
			packet.drawFullRuns, packet.drawPartialRuns,
			packet.drawManifestRuns, packet.drawIndexes );
	}
}

void GLX_StaticWorld_PrintIndirectCommands( const StaticWorldStats &stats, int limit )
{
	if ( limit < 0 ) {
		limit = 0;
	}
	if ( limit > static_cast<int>( stats.indirectPacketCount ) ) {
		limit = static_cast<int>( stats.indirectPacketCount );
	}
	if ( limit > GLX_STATIC_WORLD_PACKET_LIMIT ) {
		limit = GLX_STATIC_WORLD_PACKET_LIMIT;
	}

	GLX_StaticWorld_PrintInfo( stats );

	RI().Printf( PRINT_ALL, "  static world indirect command list: showing %i of %u commands%s\n",
		limit, stats.indirectPacketCount, stats.indirectBufferReady ? " (buffer ready)" : "" );

	for ( int i = 0; i < limit; i++ ) {
		const int packetIndex = stats.indirectCommandToPacket[i];
		const StaticWorldPacketStats *packet = nullptr;
		const StaticWorldIndirectCommandStats *command = nullptr;

		if ( packetIndex >= 0 && packetIndex < stats.packetCount ) {
			packet = &stats.packets[packetIndex];
			command = &stats.indirectPackets[packetIndex];
		}

		if ( !packet || !command || command->count == 0 ) {
			RI().Printf( PRINT_ALL, "    cmd %i: invalid packet %i\n", i, packetIndex + 1 );
			continue;
		}

		RI().Printf( PRINT_ALL,
			"    cmd %i: packet %i %s sort %i, count %u, firstIndex %u, baseVertex %i, baseInstance %u, items %i+%i, ibo %i+%i\n",
			i, packetIndex + 1,
			packet->shaderName[0] ? packet->shaderName : "<unnamed>",
			packet->sort, command->count, command->firstIndex,
			command->baseVertex, command->baseInstance,
			packet->firstItem, packet->itemCount,
			packet->indexOffset, packet->indexBytes );
	}
}

void GLX_StaticWorld_PrintSpanDiagnostics( const StaticWorldStats &stats, int limit )
{
	if ( limit < 0 ) {
		limit = 0;
	}
	if ( limit > static_cast<int>( stats.lastQueueCommandSpanCount ) ) {
		limit = static_cast<int>( stats.lastQueueCommandSpanCount );
	}
	if ( limit > GLX_STATIC_WORLD_QUEUE_COMMAND_SPAN_LIMIT ) {
		limit = GLX_STATIC_WORLD_QUEUE_COMMAND_SPAN_LIMIT;
	}

	RI().Printf( PRINT_ALL, "  static world MDI diagnostics\n" );
	RI().Printf( PRINT_ALL, "    caps: draw-indirect %s, multi-draw-indirect %s, static buffer %s (%u commands/%u bytes)\n",
		BoolName( stats.drawIndirectAvailable ),
		BoolName( stats.multiDrawIndirectAvailable ),
		BoolName( stats.indirectBufferReady ),
		stats.indirectBufferCommands,
		stats.indirectBufferBytes );
	RI().Printf( PRINT_ALL, "    gates: multidraw %s, packet-batch %s, indirect %s, compact %s, spans %s, single indirect %s\n",
		BoolName( GLX_StaticWorld_MultiDrawEnabled( stats ) ),
		BoolName( GLX_StaticWorld_PacketBatchEnabled( stats ) ),
		BoolName( stats.r_glxStaticWorldMultiDrawIndirect &&
			stats.r_glxStaticWorldMultiDrawIndirect->integer ? qtrue : qfalse ),
		BoolName( stats.r_glxStaticWorldMultiDrawIndirectCompact &&
			stats.r_glxStaticWorldMultiDrawIndirectCompact->integer ? qtrue : qfalse ),
		BoolName( stats.r_glxStaticWorldMultiDrawIndirectSpans &&
			stats.r_glxStaticWorldMultiDrawIndirectSpans->integer ? qtrue : qfalse ),
		BoolName( stats.r_glxStaticWorldIndirectDraw &&
			stats.r_glxStaticWorldIndirectDraw->integer ? qtrue : qfalse ) );
	RI().Printf( PRINT_ALL, "    last success: %s %u runs/%u indexes, first static command %i; largest %u runs/%u indexes\n",
		GLX_StaticWorld_MdiSourceName( stats.multiDrawIndirectLastSource ),
		stats.multiDrawIndirectLastRuns,
		stats.multiDrawIndirectLastIndexes,
		stats.multiDrawIndirectLastFirstCommand,
		stats.multiDrawIndirectLargestRun,
		stats.multiDrawIndirectLargestIndexes );
	RI().Printf( PRINT_ALL, "    last reject: %s, %u runs/%u indexes, command %i\n",
		GLX_StaticWorld_MdiRejectName( stats.multiDrawIndirectLastRejectReason ),
		stats.multiDrawIndirectLastRejectRuns,
		stats.multiDrawIndirectLastRejectIndexes,
		stats.multiDrawIndirectLastRejectCommand );
	RI().Printf( PRINT_ALL, "    sources: static %u calls, compact %u calls/%u uploads/%u subdata/%u orphan/%u grow/%u bytes, failures %u, scratch %u/%u bytes\n",
		stats.multiDrawIndirectStaticCalls,
		stats.multiDrawIndirectCompactCalls,
		stats.multiDrawIndirectCompactUploads,
		stats.multiDrawIndirectCompactSubDatas,
		stats.multiDrawIndirectCompactOrphans,
		stats.multiDrawIndirectCompactGrows,
		stats.multiDrawIndirectCompactBytes,
		stats.multiDrawIndirectCompactFailures,
		stats.indirectCompactBufferBytes,
		stats.indirectCompactBufferCapacityBytes );
	RI().Printf( PRINT_ALL, "    filtered barriers: %u total, invalid %u, policy %u, last %s at run %i\n",
		stats.multiDrawFilteredOrderBarriers,
		stats.multiDrawFilteredInvalidBarriers,
		stats.multiDrawFilteredPolicyBarriers,
		GLX_StaticWorld_FilteredBarrierName( stats.multiDrawFilteredLastBarrierReason ),
		stats.multiDrawFilteredLastBarrierRun );
	RI().Printf( PRINT_ALL, "    packet batches: attempts %u, batches %u, packet runs %u/%u indexes, fallback runs %u, singles %u; last %u segments/%u packet/%u fallback/%u singles, largest %u segments\n",
		stats.packetBatchAttempts,
		stats.packetBatchBatches,
		stats.packetBatchRuns,
		stats.packetBatchIndexes,
		stats.packetBatchFallbackRuns,
		stats.packetBatchSingleRuns,
		stats.packetBatchLastSegments,
		stats.packetBatchLastPacketRuns,
		stats.packetBatchLastFallbackRuns,
		stats.packetBatchLastSingles,
		stats.packetBatchLargestSegments );
	RI().Printf( PRINT_ALL, "    outer spans: attempts %u, batches %u, mdi runs %u, fallback runs %u, singles %u, single draws %u/%u indirect; last %u segments/%u mdi/%u fallback/%u singles, largest %u segments\n",
		stats.multiDrawIndirectSpanAttempts,
		stats.multiDrawIndirectSpanBatches,
		stats.multiDrawIndirectSpanMdiRuns,
		stats.multiDrawIndirectSpanFallbackRuns,
		stats.multiDrawIndirectSpanSingles,
		stats.multiDrawIndirectSpanSingleDraws,
		stats.multiDrawIndirectSpanSingleIndirectDraws,
		stats.multiDrawIndirectSpanLastSegments,
		stats.multiDrawIndirectSpanLastMdiRuns,
		stats.multiDrawIndirectSpanLastFallbackRuns,
		stats.multiDrawIndirectSpanLastSingles,
		stats.multiDrawIndirectSpanLargestSegments );
	RI().Printf( PRINT_ALL, "    command spans: attempts %u, batches %u, mdi runs %u, fallback runs %u, singleton draws %u/%u indirect, singleton blocks %u; last %u segments/%u mdi/%u fallback/%u singleton, largest %u segments\n",
		stats.multiDrawIndirectCommandSpanAttempts,
		stats.multiDrawIndirectCommandSpanBatches,
		stats.multiDrawIndirectCommandSpanMdiRuns,
		stats.multiDrawIndirectCommandSpanFallbackRuns,
		stats.multiDrawIndirectCommandSpanSingletonDraws,
		stats.multiDrawIndirectCommandSpanSingletonIndirectDraws,
		stats.multiDrawIndirectCommandSpanSingletons,
		stats.multiDrawIndirectCommandSpanLastSegments,
		stats.multiDrawIndirectCommandSpanLastMdiRuns,
		stats.multiDrawIndirectCommandSpanLastFallbackRuns,
		stats.multiDrawIndirectCommandSpanLastSingletonDraws,
		stats.multiDrawIndirectCommandSpanLargestSegments );
	RI().Printf( PRINT_ALL, "    rejections: unsupported %u, short %u, nonmanifest %u, missing-command %u, noncontiguous %u, GL errors %u\n",
		stats.multiDrawIndirectUnsupported,
		stats.multiDrawIndirectShortBatches,
		stats.multiDrawIndirectNonManifest,
		stats.multiDrawIndirectMissingCommand,
		stats.multiDrawIndirectNonContiguous,
		stats.multiDrawIndirectErrors );
	RI().Printf( PRINT_ALL, "    last queue command spans: showing %i of %u, overflow %u, largest %u commands/%u indexes\n",
		limit,
		stats.lastQueueCommandSpanCount,
		stats.lastQueueCommandSpanOverflow,
		stats.lastQueueLargestIndirectCommandRun,
		stats.lastQueueLargestIndirectCommandSpanIndexes );

	for ( int i = 0; i < limit; i++ ) {
		const StaticWorldCommandSpanStats &span = stats.lastQueueCommandSpans[i];
		const int firstPacket = span.firstPacket >= 0 ? span.firstPacket + 1 : 0;
		const int lastPacket = span.lastPacket >= 0 ? span.lastPacket + 1 : 0;

		RI().Printf( PRINT_ALL,
			"      span %i: cmd %i+%i, packets %i..%i, queue runs %i..%i, %u indexes\n",
			i + 1,
			span.firstCommand,
			span.commandCount,
			firstPacket,
			lastPacket,
			span.firstSourceRun,
			span.lastSourceRun,
			span.indexes );
	}
}

} // namespace glx
