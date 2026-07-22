#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <stdlib.h>

#include "tr_local.h"
#include "vk.h"
#include "../renderercommon/tr_motion_blur.h"

#define VK_POST_COLOR_SPACE_SDR 0
#define VK_POST_COLOR_SPACE_HDR10_ST2084 1

#if defined( _DEBUG ) || !defined( NDEBUG )
#define USE_VK_VALIDATION
#endif

static int vkSamples = VK_SAMPLE_COUNT_1_BIT;
static int vkMaxSamples = VK_SAMPLE_COUNT_1_BIT;
static motionBlurViewState_t vk_motion_blur_view;

static VkInstance vk_instance = VK_NULL_HANDLE;
static VkSurfaceKHR vk_surface = VK_NULL_HANDLE;
static qboolean vk_instance_swapchain_colorspace = qfalse;

#ifdef USE_VK_VALIDATION
VkDebugReportCallbackEXT vk_debug_callback = VK_NULL_HANDLE;
#endif

//
// Vulkan API functions used by the renderer.
//
static PFN_vkCreateInstance								qvkCreateInstance;
static PFN_vkEnumerateInstanceExtensionProperties		qvkEnumerateInstanceExtensionProperties;
static PFN_vkEnumerateInstanceVersion					qvkEnumerateInstanceVersion;

static PFN_vkCreateDevice								qvkCreateDevice;
static PFN_vkDestroyInstance							qvkDestroyInstance;
static PFN_vkEnumerateDeviceExtensionProperties			qvkEnumerateDeviceExtensionProperties;
static PFN_vkEnumeratePhysicalDevices					qvkEnumeratePhysicalDevices;
static PFN_vkGetDeviceProcAddr							qvkGetDeviceProcAddr;
static PFN_vkGetPhysicalDeviceFeatures					qvkGetPhysicalDeviceFeatures;
static PFN_vkGetPhysicalDeviceFeatures2				qvkGetPhysicalDeviceFeatures2;
static PFN_vkGetPhysicalDeviceFeatures2KHR				qvkGetPhysicalDeviceFeatures2KHR;
static PFN_vkGetPhysicalDeviceFormatProperties			qvkGetPhysicalDeviceFormatProperties;
static PFN_vkGetPhysicalDeviceMemoryProperties			qvkGetPhysicalDeviceMemoryProperties;
static PFN_vkGetPhysicalDeviceProperties				qvkGetPhysicalDeviceProperties;
static PFN_vkGetPhysicalDeviceQueueFamilyProperties		qvkGetPhysicalDeviceQueueFamilyProperties;
static PFN_vkDestroySurfaceKHR							qvkDestroySurfaceKHR;
static PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR	qvkGetPhysicalDeviceSurfaceCapabilitiesKHR;
static PFN_vkGetPhysicalDeviceSurfaceFormatsKHR			qvkGetPhysicalDeviceSurfaceFormatsKHR;
static PFN_vkGetPhysicalDeviceSurfacePresentModesKHR	qvkGetPhysicalDeviceSurfacePresentModesKHR;
static PFN_vkGetPhysicalDeviceSurfaceSupportKHR			qvkGetPhysicalDeviceSurfaceSupportKHR;
#ifdef USE_VK_VALIDATION
static PFN_vkCreateDebugReportCallbackEXT				qvkCreateDebugReportCallbackEXT;
static PFN_vkDestroyDebugReportCallbackEXT				qvkDestroyDebugReportCallbackEXT;
#endif
static PFN_vkAllocateCommandBuffers						qvkAllocateCommandBuffers;
static PFN_vkAllocateDescriptorSets						qvkAllocateDescriptorSets;
static PFN_vkAllocateMemory								qvkAllocateMemory;
static PFN_vkBeginCommandBuffer							qvkBeginCommandBuffer;
static PFN_vkBindBufferMemory							qvkBindBufferMemory;
static PFN_vkBindImageMemory							qvkBindImageMemory;
static PFN_vkCmdBeginRenderPass							qvkCmdBeginRenderPass;
static PFN_vkCmdBindDescriptorSets						qvkCmdBindDescriptorSets;
static PFN_vkCmdBindIndexBuffer							qvkCmdBindIndexBuffer;
static PFN_vkCmdBindPipeline							qvkCmdBindPipeline;
static PFN_vkCmdBindVertexBuffers						qvkCmdBindVertexBuffers;
static PFN_vkCmdBlitImage								qvkCmdBlitImage;
static PFN_vkCmdClearAttachments						qvkCmdClearAttachments;
static PFN_vkCmdCopyBuffer								qvkCmdCopyBuffer;
static PFN_vkCmdCopyBufferToImage						qvkCmdCopyBufferToImage;
static PFN_vkCmdCopyImage								qvkCmdCopyImage;
static PFN_vkCmdDraw									qvkCmdDraw;
static PFN_vkCmdDrawIndexed								qvkCmdDrawIndexed;
static PFN_vkCmdEndRenderPass							qvkCmdEndRenderPass;
static PFN_vkCmdNextSubpass								qvkCmdNextSubpass;
static PFN_vkCmdPipelineBarrier							qvkCmdPipelineBarrier;
static PFN_vkCmdPushConstants							qvkCmdPushConstants;
static PFN_vkCmdResetQueryPool							qvkCmdResetQueryPool;
static PFN_vkCmdSetDepthBias							qvkCmdSetDepthBias;
static PFN_vkCmdSetScissor								qvkCmdSetScissor;
static PFN_vkCmdSetViewport								qvkCmdSetViewport;
static PFN_vkCmdWriteTimestamp							qvkCmdWriteTimestamp;
static PFN_vkCreateBuffer								qvkCreateBuffer;
static PFN_vkCreateCommandPool							qvkCreateCommandPool;
static PFN_vkCreateDescriptorPool						qvkCreateDescriptorPool;
static PFN_vkCreateDescriptorSetLayout					qvkCreateDescriptorSetLayout;
static PFN_vkCreateFence								qvkCreateFence;
static PFN_vkCreateFramebuffer							qvkCreateFramebuffer;
static PFN_vkCreateGraphicsPipelines					qvkCreateGraphicsPipelines;
static PFN_vkCreateImage								qvkCreateImage;
static PFN_vkCreateImageView							qvkCreateImageView;
static PFN_vkCreatePipelineLayout						qvkCreatePipelineLayout;
static PFN_vkCreatePipelineCache						qvkCreatePipelineCache;
static PFN_vkCreateRenderPass							qvkCreateRenderPass;
static PFN_vkCreateSampler								qvkCreateSampler;
static PFN_vkCreateSemaphore							qvkCreateSemaphore;
static PFN_vkCreateShaderModule							qvkCreateShaderModule;
static PFN_vkDestroyBuffer								qvkDestroyBuffer;
static PFN_vkDestroyCommandPool							qvkDestroyCommandPool;
static PFN_vkDestroyDescriptorPool						qvkDestroyDescriptorPool;
static PFN_vkDestroyDescriptorSetLayout					qvkDestroyDescriptorSetLayout;
static PFN_vkDestroyDevice								qvkDestroyDevice;
static PFN_vkDestroyFence								qvkDestroyFence;
static PFN_vkDestroyFramebuffer							qvkDestroyFramebuffer;
static PFN_vkDestroyImage								qvkDestroyImage;
static PFN_vkDestroyImageView							qvkDestroyImageView;
static PFN_vkDestroyPipeline							qvkDestroyPipeline;
static PFN_vkDestroyPipelineCache						qvkDestroyPipelineCache;
static PFN_vkDestroyPipelineLayout						qvkDestroyPipelineLayout;
static PFN_vkDestroyRenderPass							qvkDestroyRenderPass;
static PFN_vkDestroySampler								qvkDestroySampler;
static PFN_vkDestroySemaphore							qvkDestroySemaphore;
static PFN_vkDestroyShaderModule						qvkDestroyShaderModule;
static PFN_vkDeviceWaitIdle								qvkDeviceWaitIdle;
static PFN_vkEndCommandBuffer							qvkEndCommandBuffer;
static PFN_vkFlushMappedMemoryRanges					qvkFlushMappedMemoryRanges;
static PFN_vkFreeCommandBuffers							qvkFreeCommandBuffers;
static PFN_vkFreeDescriptorSets							qvkFreeDescriptorSets;
static PFN_vkFreeMemory									qvkFreeMemory;
static PFN_vkGetBufferMemoryRequirements				qvkGetBufferMemoryRequirements;
static PFN_vkGetDeviceQueue								qvkGetDeviceQueue;
static PFN_vkGetImageMemoryRequirements					qvkGetImageMemoryRequirements;
static PFN_vkGetImageSubresourceLayout					qvkGetImageSubresourceLayout;
static PFN_vkInvalidateMappedMemoryRanges				qvkInvalidateMappedMemoryRanges;
static PFN_vkMapMemory									qvkMapMemory;
static PFN_vkQueueSubmit								qvkQueueSubmit;
static PFN_vkQueueWaitIdle								qvkQueueWaitIdle;
static PFN_vkResetCommandBuffer							qvkResetCommandBuffer;
static PFN_vkResetDescriptorPool						qvkResetDescriptorPool;
static PFN_vkResetFences								qvkResetFences;
static PFN_vkUnmapMemory								qvkUnmapMemory;
static PFN_vkUpdateDescriptorSets						qvkUpdateDescriptorSets;
static PFN_vkWaitForFences								qvkWaitForFences;
static PFN_vkAcquireNextImageKHR						qvkAcquireNextImageKHR;
static PFN_vkCreateSwapchainKHR							qvkCreateSwapchainKHR;
static PFN_vkDestroySwapchainKHR						qvkDestroySwapchainKHR;
static PFN_vkGetSwapchainImagesKHR						qvkGetSwapchainImagesKHR;
static PFN_vkQueuePresentKHR							qvkQueuePresentKHR;
static PFN_vkGetPhysicalDeviceProperties2				qvkGetPhysicalDeviceProperties2;
static PFN_vkGetPhysicalDeviceProperties2KHR			qvkGetPhysicalDeviceProperties2KHR;
static PFN_vkCreateQueryPool							qvkCreateQueryPool;
static PFN_vkDestroyQueryPool							qvkDestroyQueryPool;
static PFN_vkGetQueryPoolResults						qvkGetQueryPoolResults;
static PFN_vkSetHdrMetadataEXT							qvkSetHdrMetadataEXT;

static PFN_vkGetBufferMemoryRequirements2KHR			qvkGetBufferMemoryRequirements2KHR;
static PFN_vkGetImageMemoryRequirements2KHR				qvkGetImageMemoryRequirements2KHR;
static PFN_vkGetBufferDeviceAddress						qvkGetBufferDeviceAddress;
static PFN_vkGetBufferDeviceAddressKHR					qvkGetBufferDeviceAddressKHR;
static PFN_vkCreateAccelerationStructureKHR				qvkCreateAccelerationStructureKHR;
static PFN_vkDestroyAccelerationStructureKHR			qvkDestroyAccelerationStructureKHR;
static PFN_vkGetAccelerationStructureBuildSizesKHR		qvkGetAccelerationStructureBuildSizesKHR;
static PFN_vkCmdBuildAccelerationStructuresKHR			qvkCmdBuildAccelerationStructuresKHR;
static PFN_vkGetAccelerationStructureDeviceAddressKHR	qvkGetAccelerationStructureDeviceAddressKHR;
static PFN_vkCreateRayTracingPipelinesKHR				qvkCreateRayTracingPipelinesKHR;
static PFN_vkGetRayTracingShaderGroupHandlesKHR			qvkGetRayTracingShaderGroupHandlesKHR;
static PFN_vkCmdTraceRaysKHR							qvkCmdTraceRaysKHR;
static PFN_vkCmdCopyAccelerationStructureKHR			qvkCmdCopyAccelerationStructureKHR;
static PFN_vkCmdWriteAccelerationStructuresPropertiesKHR qvkCmdWriteAccelerationStructuresPropertiesKHR;

static PFN_vkDebugMarkerSetObjectNameEXT				qvkDebugMarkerSetObjectNameEXT;
static PFN_vkCmdDebugMarkerBeginEXT					qvkCmdDebugMarkerBeginEXT;
static PFN_vkCmdDebugMarkerEndEXT						qvkCmdDebugMarkerEndEXT;
static PFN_vkCmdDebugMarkerInsertEXT					qvkCmdDebugMarkerInsertEXT;

////////////////////////////////////////////////////////////////////////////

// forward declaration
VkPipeline create_pipeline( const Vk_Pipeline_Def *def, renderPass_t renderPassIndex, uint32_t def_index );
static const char *vk_result_string( VkResult code );
static uint32_t find_memory_type( uint32_t memory_type_bits, VkMemoryPropertyFlags properties );
static VkCommandBuffer begin_command_buffer( void );
static void end_command_buffer( VkCommandBuffer command_buffer, const char *location );
static qboolean end_command_buffer_soft( VkCommandBuffer command_buffer, const char *location );
static void vk_begin_post_bloom_render_pass( void );
static void vk_begin_global_fog_render_pass( void );
static void vk_begin_liquid_snapshot_render_pass( void );

static qboolean vk_liquid_requested( void )
{
	return ( r_liquid && r_liquid->integer > 0 ) ? qtrue : qfalse;
}

static qboolean vk_depth_fade_requested( void )
{
	return ( ( r_depthFade && r_depthFade->integer ) ||
		R_CelShadingWorldActive() ) ? qtrue : qfalse;
}

static qboolean vk_scene_depth_snapshot_requested( void )
{
	return ( vk_liquid_requested() || vk_depth_fade_requested() ) ?
		qtrue : qfalse;
}

static qboolean vk_global_fog_requested( void )
{
	return ( r_globalFog && r_globalFog->integer &&
		vk.globalFogDepthSampleSupported ) ? qtrue : qfalse;
}

static qboolean vk_global_fog_enabled( void )
{
	return ( vk_global_fog_requested() && vk.fboActive &&
		vkSamples == VK_SAMPLE_COUNT_1_BIT ) ? qtrue : qfalse;
}

typedef enum {
	RTX_VK_LIFETIME_UNINITIALIZED = 0,
	RTX_VK_LIFETIME_INITIALIZING,
	RTX_VK_LIFETIME_READY,
	RTX_VK_LIFETIME_RESIZING,
	RTX_VK_LIFETIME_SHUTTING_DOWN
} rtxVkLifetimeState_t;

typedef enum {
	RTX_VK_RESOURCE_SYNC_PRIMITIVES = 0,
	RTX_VK_RESOURCE_SWAPCHAIN,
	RTX_VK_RESOURCE_ATTACHMENTS,
	RTX_VK_RESOURCE_RENDER_PASSES,
	RTX_VK_RESOURCE_FRAMEBUFFERS,
	RTX_VK_RESOURCE_GEOMETRY_BUFFERS,
	RTX_VK_RESOURCE_STORAGE_BUFFER,
	RTX_VK_RESOURCE_COUNT
} rtxVkResource_t;

typedef struct {
	qboolean alive;
	uint32_t createCount;
	uint32_t destroyCount;
	uint32_t useCount;
	uint32_t lastSerial;
	uint32_t lastUseFrame;
} rtxVkResourceEntry_t;

typedef struct {
	rtxVkLifetimeState_t state;
	rtxVkResourceEntry_t resources[ RTX_VK_RESOURCE_COUNT ];
	uint32_t serial;
	uint32_t generation;
	uint32_t frameSerial;
	qboolean frameActive;
} rtxVkLifetimeTracker_t;

static rtxVkLifetimeTracker_t s_vkLifetimeTracker;
static qboolean s_vkSwapchainResizePending = qfalse;
static qboolean s_vkSwapchainResizeRestarting = qfalse;
static VkResult s_vkSwapchainResizeResult = VK_SUCCESS;
static qboolean s_vkValidationLayerEnabled = qfalse;
static uint32_t s_vkInstanceApiVersion = VK_API_VERSION_1_0;
static qboolean s_vkRtDynResBlitWarned = qfalse;
static int s_vkRtTextureBudgetFrame = -1;
static VkDeviceSize s_vkRtTextureBudgetUsed = 0;

typedef enum {
	RTX_VK_DESC_LAYOUT_SAMPLER = 0,
	RTX_VK_DESC_LAYOUT_UNIFORM,
	RTX_VK_DESC_LAYOUT_STORAGE,
	RTX_VK_DESC_LAYOUT_COUNT
} rtxVkDescriptorLayoutId_t;

typedef enum {
	RTX_VK_PIPELINE_LAYOUT_MAIN = 0,
	RTX_VK_PIPELINE_LAYOUT_STORAGE,
	RTX_VK_PIPELINE_LAYOUT_POST_PROCESS,
	RTX_VK_PIPELINE_LAYOUT_BLEND,
	RTX_VK_PIPELINE_LAYOUT_COUNT
} rtxVkPipelineLayoutId_t;

typedef struct {
	float xyz[4];
	float normal[4];
	float tangent[4];
	float texCoord[2];
	uint32_t color;
	uint32_t materialIndex;
} rtxRtPackedVertex_t;

typedef enum {
	RTX_RT_COLORSPACE_LINEAR = 0,
	RTX_RT_COLORSPACE_SRGB,
	RTX_RT_COLORSPACE_HDR
} rtxRtColorSpace_t;

typedef enum {
	RTX_RT_MATFLAG_NONE = 0,
	RTX_RT_MATFLAG_MASKED = 1u << 0,
	RTX_RT_MATFLAG_EMISSIVE = 1u << 1,
	RTX_RT_MATFLAG_TRANSLUCENT = 1u << 2,
	RTX_RT_MATFLAG_TWO_SIDED = 1u << 3,
	RTX_RT_MATFLAG_SKY = 1u << 4,
	RTX_RT_MATFLAG_DECAL = 1u << 5,
	RTX_RT_MATFLAG_PARTICLE = 1u << 6,
	RTX_RT_MATFLAG_EFFECT = 1u << 7,
	RTX_RT_MATFLAG_ALPHA_LT = 1u << 8,
	RTX_RT_MATFLAG_CULL_FRONT = 1u << 9,
	RTX_RT_MATFLAG_CULL_BACK = 1u << 10,
	RTX_RT_MATFLAG_RASTER_OVERLAY = 1u << 11
} rtxRtMaterialFlags_t;

typedef struct {
	char name[MAX_QPATH];
	float baseColor[4];
	float emissiveColor[3];
	float emissiveScale;
	float roughness;
	float metallic;
	float alphaCutoff;
	uint32_t flags;
	uint32_t albedoColorSpace;
	uint32_t normalColorSpace;
	uint32_t emissiveColorSpace;
	int32_t albedoTextureIndex;
	uint32_t sourceStageCount;
} rtxRtMaterial_t;

typedef struct {
	float baseColor[4];
	float emissiveColorScale[4];
	float pbrParams[4];			// roughness, metallic, alphaCutoff, ior
	uint32_t metadata[4];		// flags, albedoCS, normalCS, emissiveCS
	uint32_t textureInfo[4];	// albedoTextureIndex, reserved[3]
} rtxRtGpuMaterial_t;

typedef struct {
	float positionRadius[4];	// xyz + radius
	float colorType[4];		// rgb + type (0 point, 1 directional)
	float directionSoftness[4];	// xyz + softness
	uint32_t metadata[4];		// light policy flags + reserved
} rtxRtGpuLight_t;

#define RTX_RT_LIGHT_FLAG_CASTS_SHADOWS ( 1u << 0 )
#define RTX_RT_LIGHT_FLAG_SHADOW_ONLY ( 1u << 1 )

#define RTX_RT_MAX_LIGHT_COORDINATE 16777216.0f
#define RTX_RT_MAX_LIGHT_RADIUS 1048576.0f
#define RTX_RT_MAX_LIGHT_RADIANCE 65536.0f

#define RTX_RT_MAX_WORLD_ENTITY_LIGHTS 512

typedef struct {
	const world_t *world;
	uint32_t count;
	qboolean hasSkySurface;
	qboolean warnedEmpty;
	vec3_t worldspawnColor;
	float worldspawnAmbient;
	rtxRtGpuLight_t lights[RTX_RT_MAX_WORLD_ENTITY_LIGHTS];
} rtxRtWorldLightCache_t;

static rtxRtWorldLightCache_t s_vkRtWorldLightCache;

static qboolean vk_rt_float_in_finite_range( float value, float minimum,
	float maximum )
{
	return ( value == value && value >= minimum && value <= maximum ) ?
		qtrue : qfalse;
}

static qboolean vk_rt_vec3_in_finite_range( const float *value, float limit )
{
	return ( value &&
		vk_rt_float_in_finite_range( value[0], -limit, limit ) &&
		vk_rt_float_in_finite_range( value[1], -limit, limit ) &&
		vk_rt_float_in_finite_range( value[2], -limit, limit ) ) ?
		qtrue : qfalse;
}

static qboolean vk_rt_gpu_light_is_valid( const rtxRtGpuLight_t *light )
{
	const float type = light ? light->colorType[3] : -1.0f;
	float directionLengthSq;

	if ( !light ||
		!vk_rt_vec3_in_finite_range( light->colorType,
			RTX_RT_MAX_LIGHT_RADIANCE ) ||
		light->colorType[0] < 0.0f || light->colorType[1] < 0.0f ||
		light->colorType[2] < 0.0f ||
		!vk_rt_float_in_finite_range( light->directionSoftness[3],
			0.0f, RTX_RT_MAX_LIGHT_RADIUS ) ) {
		return qfalse;
	}

	if ( type == 1.0f ) {
		if ( !vk_rt_vec3_in_finite_range( light->directionSoftness, 1.0f ) ) {
			return qfalse;
		}
		directionLengthSq =
			light->directionSoftness[0] * light->directionSoftness[0] +
			light->directionSoftness[1] * light->directionSoftness[1] +
			light->directionSoftness[2] * light->directionSoftness[2];
		if ( !vk_rt_float_in_finite_range( directionLengthSq, 1.0e-8f, 1.0001f ) ) {
			return qfalse;
		}
		/* An authored directional light has no finite origin/radius.  The
		 * synthesized sky fallback may carry a bounded proxy volume. */
		if ( light->positionRadius[3] != 0.0f &&
			( !vk_rt_vec3_in_finite_range( light->positionRadius,
				RTX_RT_MAX_LIGHT_COORDINATE ) ||
			  !vk_rt_float_in_finite_range( light->positionRadius[3], 0.0f,
				RTX_RT_MAX_LIGHT_RADIUS ) ) ) {
			return qfalse;
		}
		return qtrue;
	}

	if ( type != 0.0f && type != 2.0f ) {
		return qfalse;
	}
	if ( !vk_rt_vec3_in_finite_range( light->positionRadius,
			RTX_RT_MAX_LIGHT_COORDINATE ) ||
		!vk_rt_float_in_finite_range( light->positionRadius[3],
			FLT_MIN, RTX_RT_MAX_LIGHT_RADIUS ) ) {
		return qfalse;
	}
	if ( type == 2.0f &&
		!vk_rt_vec3_in_finite_range( light->directionSoftness,
			RTX_RT_MAX_LIGHT_COORDINATE ) ) {
		return qfalse;
	}

	return qtrue;
}

typedef struct {
	rtxRtPackedVertex_t *vertices;
	uint32_t *indices;
	rtxRtMaterial_t *materials;
	const shader_t **materialShaders;
	uint32_t numVertices;
	uint32_t numIndices;
	uint32_t numMaterials;
	uint32_t vertexCapacity;
	uint32_t indexCapacity;
	uint32_t materialCapacity;
	uint32_t sourceTriangleCount;
	uint32_t degenerateTriangleCount;
	uint32_t maskedTriangleCount;
	uint32_t anyHitTriangleCount;
	uint32_t sourceEntityCount;
	uint32_t sourceEffectCount;
} rtxRtCpuGeometry_t;

typedef struct {
	float cameraOriginTanHalfFovX[4];
	float cameraForwardTanHalfFovY[4];
	float cameraRightWidth[4];
	float cameraUpHeight[4];
	float sunDirection[4];
	float sunColorIntensity[4];
	uint32_t debugMode;
	uint32_t frameIndex;
	uint32_t activeInstances;
	uint32_t lightCount;
	uint32_t worldMaterialCount;
	uint32_t dynamicMaterialCount;
	uint32_t shadowMode;
	float shadowSoftness;
	float indirectStrength;
	float reflectionStrength;
	float skyIntensity;
	float refractionIor;
	float sunIntensity;
	uint32_t refractiveMode;
	uint32_t indirectBounce;
} rtxRtPushConstants_t;

typedef struct {
	float prevCameraOriginTanHalfFovX[4];
	float prevCameraForwardTanHalfFovY[4];
	float prevCameraRightWidth[4];
	float prevCameraUpHeight[4];
	float dynamicMotionAndExposure[4]; // xyz dynamic centroid delta, w exposure
	float temporalParams0[4];          // alpha, clamp, depthReject, motionReject
	float denoiseParams[4];            // depthSigma, normalSigma, fireflyThreshold, fireflyStrength
	float exposureParams[4];           // exposureMin, exposureMax, exposureAdapt, dynamicMotionScale
	float legacyColorParams[4];        // enabled, intensityScale, RT contribution strength, reserved
	float readabilityParams[4];        // lift, contrast, saturation, shadowFloor
	uint32_t modes[4];                 // temporalEnable, spatialEnable, tonemapMode, bitflags
} rtxRtTemporalParams_t;

#define RTX_RT_DESC_SET 0
#define RTX_RT_DESC_BINDING_TLAS 0
#define RTX_RT_DESC_BINDING_OUTPUT 1
#define RTX_RT_DESC_BINDING_WORLD_VERTICES 2
#define RTX_RT_DESC_BINDING_WORLD_INDICES 3
#define RTX_RT_DESC_BINDING_WORLD_MATERIALS 4
#define RTX_RT_DESC_BINDING_DYNAMIC_VERTICES 5
#define RTX_RT_DESC_BINDING_DYNAMIC_INDICES 6
#define RTX_RT_DESC_BINDING_DYNAMIC_MATERIALS 7
#define RTX_RT_DESC_BINDING_LIGHTS 8
#define RTX_RT_DESC_BINDING_HISTORY_IN 9
#define RTX_RT_DESC_BINDING_HISTORY_OUT 10
#define RTX_RT_DESC_BINDING_TEMPORAL_PARAMS 11
#define RTX_RT_DESC_BINDING_SCENE_COLOR 12
#define RTX_RT_DESC_BINDING_SCENE_TEXTURES 13
#define RTX_RT_MAX_DEBUG_VISUALIZER 10
#define RTX_RT_MAX_SCENE_TEXTURES MAX_DRAWIMAGES
#define RTX_RT_INVALID_TEXTURE_INDEX 0xFFFFFFFFu

#define RTX_RT_SCENE_COLOR_SAMPLER_COUNT 1u
#define RTX_RT_STORAGE_BUFFER_COUNT 8u
#define RTX_RT_STORAGE_IMAGE_COUNT 3u
#define RTX_RT_ACCELERATION_STRUCTURE_COUNT 1u
#define RTX_RT_SAMPLED_IMAGE_COUNT \
	( RTX_RT_MAX_SCENE_TEXTURES + RTX_RT_SCENE_COLOR_SAMPLER_COUNT )
#define RTX_RT_PER_STAGE_RESOURCE_COUNT \
	( RTX_RT_SAMPLED_IMAGE_COUNT + RTX_RT_STORAGE_BUFFER_COUNT + \
	  RTX_RT_STORAGE_IMAGE_COUNT + RTX_RT_ACCELERATION_STRUCTURE_COUNT )

/* Descriptor updates run on the renderer thread.  Keep the full fixed shader
 * table out of the call stack without paying a per-frame heap allocation. */
static VkDescriptorImageInfo s_vkRtDescriptorImageInfos[4 + RTX_RT_MAX_SCENE_TEXTURES];

#define RTX_RT_TEMPORAL_FLAG_HISTORY_VALID ( 1u << 0 )
#define RTX_RT_TEMPORAL_FLAG_RESET ( 1u << 1 )
#define RTX_RT_TEMPORAL_FLAG_AUTO_EXPOSURE ( 1u << 2 )
#define RTX_RT_MODE_FLAG_PARTICLE_VOLUME ( 1u << 3 )
#define RTX_RT_MODE_FLAG_RASTER_REFERENCE ( 1u << 4 )
#define RTX_RT_MODE_FLAG_SCENE_LINEAR_OUTPUT ( 1u << 5 )
#define RTX_RT_LEGACY_SUN_UNIT_SCALE ( 1.0f / 100.0f )

static void vk_rt_shutdown( void );
static qboolean vk_rt_trace_available( void );
static qboolean vk_rt_init( void );
static qboolean vk_rt_ensure_pipeline( void );
static qboolean vk_rt_ensure_output_image( void );
static qboolean vk_rt_ensure_temporal_params_buffer( void );
static qboolean vk_rt_ensure_timing_query_pool( void );
static void vk_rt_read_timing_query_results( void );
static void vk_rt_update_scalability_controller( void );
static void vk_rt_log_perf_summary( void );
static qboolean vk_rt_ensure_world_blas( VkDeviceSize *frameBudgetBytesUsed, VkDeviceSize frameBudgetBytes );
static qboolean vk_rt_ensure_dynamic_blas( VkDeviceSize *frameBudgetBytesUsed, VkDeviceSize frameBudgetBytes );
static qboolean vk_rt_build_tlas( VkDeviceSize *frameBudgetBytesUsed, VkDeviceSize frameBudgetBytes );
static void vk_rt_reset_state( void );
static qboolean vk_rt_align_up_checked( VkDeviceSize value, VkDeviceSize alignment, VkDeviceSize *outValue );
static qboolean vk_rt_budget_allows( VkDeviceSize bytesNeeded, VkDeviceSize *bytesUsed, VkDeviceSize budgetBytes );
static qboolean vk_rt_create_buffer( VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryProps, qboolean needDeviceAddress, const char *debugName, rtxVkRtBuffer_t *outBuffer );
static void vk_rt_destroy_buffer( rtxVkRtBuffer_t *buffer );
static qboolean vk_rt_upload_buffer_data( const rtxVkRtBuffer_t *buffer, const void *src, size_t bytes );
static qboolean vk_rt_wait_for_inflight_frames( const char *reason );
static qboolean vk_rt_ensure_buffer_capacity( rtxVkRtBuffer_t *buffer, VkDeviceSize minSize, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryProps, qboolean needDeviceAddress, const char *debugName );
static void vk_rt_destroy_as( rtxVkRtAccelerationStructure_t *as );
static qboolean vk_rt_create_as( VkAccelerationStructureTypeKHR type, VkDeviceSize size, const char *debugName, rtxVkRtAccelerationStructure_t *outAs );
static qboolean vk_rt_ensure_scratch_buffer( VkDeviceSize minSize );
static qboolean vk_rt_compact_as( rtxVkRtAccelerationStructure_t *as, VkQueryPool queryPool, uint32_t queryIndex, const char *debugName, uint64_t *savedBytes );
static qboolean vk_rt_build_triangles_blas( rtxVkRtAccelerationStructure_t *outAs, const rtxVkRtBuffer_t *vertexBuffer, uint32_t vertexCount, VkDeviceSize vertexStride, const rtxVkRtBuffer_t *indexBuffer, uint32_t indexCount, qboolean opaqueGeometry, qboolean allowCompaction, const char *debugName );
static qboolean vk_rt_extract_world_geometry( rtxRtCpuGeometry_t *geometry );
static qboolean vk_rt_extract_dynamic_geometry( rtxRtCpuGeometry_t *geometry );
static void vk_rt_cpu_geometry_init( rtxRtCpuGeometry_t *geometry );
static void vk_rt_cpu_geometry_free( rtxRtCpuGeometry_t *geometry );
static qboolean vk_rt_cpu_geometry_reserve( rtxRtCpuGeometry_t *geometry, uint32_t additionalVertices, uint32_t additionalIndices );
static qboolean vk_rt_cpu_geometry_reserve_materials( rtxRtCpuGeometry_t *geometry, uint32_t additionalMaterials );
static qboolean vk_rt_cpu_geometry_add_vertex( rtxRtCpuGeometry_t *geometry, const vec3_t xyz, const vec3_t normal, const vec4_t tangent, const vec2_t texCoord, uint32_t color, uint32_t materialIndex, uint32_t *outIndex );
static qboolean vk_rt_cpu_geometry_add_index( rtxRtCpuGeometry_t *geometry, uint32_t index );
static qboolean vk_rt_cpu_geometry_add_triangle( rtxRtCpuGeometry_t *geometry, uint32_t i0, uint32_t i1, uint32_t i2 );
static qboolean vk_rt_cpu_geometry_find_or_add_material( rtxRtCpuGeometry_t *geometry, const shader_t *shader, uint32_t *outMaterialIndex );
static qboolean vk_rt_cpu_geometry_find_or_add_material_ex( rtxRtCpuGeometry_t *geometry, const shader_t *shader, uint32_t extraFlags, uint32_t *outMaterialIndex );
static qboolean vk_rt_append_surface_face_geometry( rtxRtCpuGeometry_t *geometry, const srfSurfaceFace_t *surface, const shader_t *shader, uint32_t materialFlags );
static qboolean vk_rt_append_surface_grid_geometry( rtxRtCpuGeometry_t *geometry, const srfGridMesh_t *surface, const shader_t *shader, uint32_t materialFlags );
static qboolean vk_rt_append_surface_triangles_geometry( rtxRtCpuGeometry_t *geometry, const srfTriangles_t *surface, const shader_t *shader, uint32_t materialFlags );
static qboolean vk_rt_append_brush_entity_geometry( rtxRtCpuGeometry_t *geometry, const trRefEntity_t *ent, const model_t *model );
static qboolean vk_rt_append_md3_entity_geometry( rtxRtCpuGeometry_t *geometry, const trRefEntity_t *ent );
static qboolean vk_rt_append_iqm_entity_geometry( rtxRtCpuGeometry_t *geometry, const trRefEntity_t *ent );
static qboolean vk_rt_append_mdr_proxy_geometry( rtxRtCpuGeometry_t *geometry, const trRefEntity_t *ent, const shader_t *shader );
static qboolean vk_rt_append_sprite_entity_geometry( rtxRtCpuGeometry_t *geometry, const trRefEntity_t *ent );
static qboolean vk_rt_append_beam_entity_geometry( rtxRtCpuGeometry_t *geometry, const trRefEntity_t *ent );
static qboolean vk_rt_append_poly_geometry( rtxRtCpuGeometry_t *geometry, const srfPoly_t *poly );
static qboolean vk_rt_append_dynamic_model_entity_geometry( rtxRtCpuGeometry_t *geometry, const trRefEntity_t *ent, uint32_t *outModelCount );
static qboolean vk_rt_poly_is_mark_decal( const srfPoly_t *poly );
static uint32_t vk_rt_estimate_dynamic_scene_signature( vec3_t outCentroid, uint32_t *outModelCount, uint32_t *outEffectCount, float quantStep );
static void vk_rt_apply_effect_material_tuning( rtxRtCpuGeometry_t *geometry, uint32_t materialIndex, qboolean particleLike, qboolean decalLike );
static void vk_rt_transform_geometry_range( rtxRtCpuGeometry_t *geometry, uint32_t firstVertex, const trRefEntity_t *ent );
static qboolean vk_rt_generate_tangents( rtxRtCpuGeometry_t *geometry );
static qboolean vk_rt_build_dynamic_scene_blas( VkDeviceSize *frameBudgetBytesUsed, VkDeviceSize frameBudgetBytes, VkDeviceSize *outBuildBytes );
static qboolean vk_rt_ensure_descriptor_resources( void );
static qboolean vk_rt_update_descriptor_set( void );
static qboolean vk_rt_upload_material_buffer( const rtxRtMaterial_t *materials, uint32_t materialCount, rtxVkRtBuffer_t *targetBuffer, uint32_t *outMaterialCount, const char *debugName );
static qboolean vk_rt_update_light_buffer( void );
static void vk_rt_reset_world_light_cache( void );
static void vk_rt_rebuild_world_light_cache( void );
static qboolean vk_rt_consume_cvar_modified( cvar_t *cvar );
static void vk_rt_handle_cached_resource_cvar_changes( void );
static uint32_t vk_rt_append_world_entity_lights( rtxRtGpuLight_t *lights, uint32_t count, uint32_t maxLights );
static uint32_t vk_rt_surface_light_reservation( uint32_t maxLights, uint32_t usedLights );
static uint32_t vk_rt_append_surface_lights( rtxRtGpuLight_t *lights, uint32_t count, uint32_t maxLights );
static qboolean vk_rt_world_has_sky_surface( void );
static void vk_rt_estimate_world_bounds( vec3_t center, float *radius );
static void vk_rt_resolve_sun_params( vec3_t outDirection, vec3_t outColor, qboolean *outFallbackFromSky );
static qboolean vk_rt_build_sbt( void );
static VkDeviceSize vk_rt_query_triangles_build_bytes( uint32_t vertexCount, uint32_t indexCount, qboolean opaqueGeometry, VkBuildAccelerationStructureFlagsKHR buildFlags );
static VkDeviceSize vk_rt_query_tlas_build_bytes( uint32_t instanceCount, VkBuildAccelerationStructureFlagsKHR buildFlags );
static uint32_t vk_rt_clamped_visualizer_mode( void );
static uint32_t vk_rt_collect_instances( VkAccelerationStructureInstanceKHR *instances, uint32_t maxInstances, qboolean *throttledByInstanceBudget );
static void vk_rt_fill_push_constants( rtxRtPushConstants_t *push );
static qboolean vk_rt_update_temporal_state( void );
static void vk_rt_reset_temporal_history( const char *reason );
static void vk_rt_destroy_output_image( void );
static const shader_t *vk_rt_resolve_md3_shader( const trRefEntity_t *ent, const md3Surface_t *surface );
static const shader_t *vk_rt_resolve_iqm_shader( const trRefEntity_t *ent, const srfIQModel_t *surface );
static void vk_rt_decode_md3_normal( short packedNormal, vec3_t outNormal );
static void vk_rt_transform_point( const trRefEntity_t *ent, const vec3_t local, vec3_t world );
static void vk_rt_transform_direction( const trRefEntity_t *ent, const vec3_t local, vec3_t world );
static uint32_t vk_rt_color_u32_from_bytes( const byte *rgba );
static qboolean vk_rt_material_name_contains_token( const char *name, const char *token );
static float vk_rt_material_alpha_cutoff_from_state_bits( unsigned stateBits );
static rtxRtColorSpace_t vk_rt_material_albedo_color_space( const image_t *image );
static rtxRtColorSpace_t vk_rt_material_data_color_space( const image_t *image );
static int32_t vk_rt_image_to_scene_texture_index( const image_t *image );
static void vk_rt_translate_shader_to_material( const shader_t *shader, rtxRtMaterial_t *material );
static void vk_rt_apply_material_overrides( rtxRtMaterial_t *material );
static VkSampler vk_find_sampler( const Vk_Sampler_Def *def );

static int vk_lifetime_debug_level( void )
{
	if ( !rtx_debug_resource_lifetime ) {
		return 0;
	}
	return rtx_debug_resource_lifetime->integer;
}

static int vk_debug_markers_mode( void )
{
	if ( !rtx_debug_gpu_markers ) {
		return 0;
	}
	return rtx_debug_gpu_markers->integer;
}

static qboolean vk_validation_requested( void )
{
	if ( !rtx_debug_vk_validation ) {
		return qfalse;
	}
	return rtx_debug_vk_validation->integer ? qtrue : qfalse;
}

typedef enum {
	RTX_RT_MODE_DISABLED = 0,
	RTX_RT_MODE_RAY_QUERY = 1,
	RTX_RT_MODE_RAY_TRACING_PIPELINE = 2
} rtxRtMode_t;

static int vk_caps_report_level( void )
{
	if ( !rtx_caps_report ) {
		return 0;
	}
	return rtx_caps_report->integer;
}

static int vk_requested_rt_mode( void )
{
	if ( !rtx_rt_mode ) {
		return RTX_RT_MODE_DISABLED;
	}
	return rtx_rt_mode->integer;
}

static qboolean vk_rt_mode_required( void )
{
	if ( !rtx_rt_require ) {
		return qfalse;
	}
	return rtx_rt_require->integer ? qtrue : qfalse;
}

static int vk_rt_debug_visualizer_mode( void )
{
	if ( !rtx_rt_debug_visualizer ) {
		return 0;
	}
	return rtx_rt_debug_visualizer->integer;
}

static int vk_rt_quality_preset( void )
{
	if ( !rtx_rt_quality_preset ) {
		return 0;
	}
	return Com_Clamp( 0, 4, rtx_rt_quality_preset->integer );
}

static qboolean vk_rt_adaptive_budget_enabled( void )
{
	if ( !rtx_rt_adaptive_budget ) {
		return qfalse;
	}
	return rtx_rt_adaptive_budget->integer ? qtrue : qfalse;
}

static float vk_rt_target_frame_ms( void )
{
	static const float presetTarget[5] = { 12.0f, 16.6f, 13.8f, 11.1f, 8.3f };
	int preset = vk_rt_quality_preset();

	if ( preset > 0 ) {
		return presetTarget[preset];
	}
	if ( !rtx_rt_target_frame_ms ) {
		return 12.0f;
	}
	return Com_Clamp( 4.0f, 50.0f, rtx_rt_target_frame_ms->value );
}

static float vk_rt_budget_response( void )
{
	if ( !rtx_rt_budget_response ) {
		return 0.18f;
	}
	return Com_Clamp( 0.01f, 1.0f, rtx_rt_budget_response->value );
}

static float vk_rt_budget_min_scale( void )
{
	static const float presetMinScale[5] = { 0.45f, 0.40f, 0.45f, 0.50f, 0.60f };
	int preset = vk_rt_quality_preset();

	if ( preset > 0 ) {
		return presetMinScale[preset];
	}
	if ( !rtx_rt_budget_min_scale ) {
		return 0.45f;
	}
	return Com_Clamp( 0.1f, 1.0f, rtx_rt_budget_min_scale->value );
}

static float vk_rt_budget_max_scale( void )
{
	static const float presetMaxScale[5] = { 1.25f, 1.00f, 1.10f, 1.25f, 1.40f };
	int preset = vk_rt_quality_preset();

	if ( preset > 0 ) {
		return presetMaxScale[preset];
	}
	if ( !rtx_rt_budget_max_scale ) {
		return 1.25f;
	}
	return Com_Clamp( 1.0f, 2.0f, rtx_rt_budget_max_scale->value );
}

static int vk_rt_perf_timing_mode( void )
{
	if ( !rtx_rt_perf_timing ) {
		return 0;
	}
	return Com_Clamp( 0, 2, rtx_rt_perf_timing->integer );
}

static int vk_rt_perf_interval_frames( void )
{
	if ( !rtx_rt_perf_interval ) {
		return 120;
	}
	return Com_Clamp( 1, 4096, rtx_rt_perf_interval->integer );
}

static qboolean vk_rt_dynamic_resolution_enabled( void )
{
	if ( !rtx_rt_dynamic_resolution ) {
		return qfalse;
	}
	return rtx_rt_dynamic_resolution->integer ? qtrue : qfalse;
}

static float vk_rt_dynamic_resolution_min_scale( void )
{
	static const float presetMin[5] = { 0.60f, 0.55f, 0.65f, 0.75f, 0.85f };
	int preset = vk_rt_quality_preset();

	if ( preset > 0 ) {
		return presetMin[preset];
	}
	if ( !rtx_rt_dynamic_resolution_min ) {
		return 0.60f;
	}
	return Com_Clamp( 0.25f, 1.0f, rtx_rt_dynamic_resolution_min->value );
}

static float vk_rt_dynamic_resolution_max_scale( void )
{
	static const float presetMax[5] = { 1.00f, 0.85f, 0.95f, 1.00f, 1.00f };
	int preset = vk_rt_quality_preset();

	if ( preset > 0 ) {
		return presetMax[preset];
	}
	if ( !rtx_rt_dynamic_resolution_max ) {
		return 1.00f;
	}
	return Com_Clamp( 0.25f, 1.5f, rtx_rt_dynamic_resolution_max->value );
}

static float vk_rt_dynamic_resolution_rate( void )
{
	if ( !rtx_rt_dynamic_resolution_rate ) {
		return 0.08f;
	}
	return Com_Clamp( 0.01f, 1.0f, rtx_rt_dynamic_resolution_rate->value );
}

static qboolean vk_rt_async_overlap_enabled( void )
{
	if ( !rtx_rt_async_overlap ) {
		return qfalse;
	}
	return rtx_rt_async_overlap->integer ? qtrue : qfalse;
}

static int vk_rt_as_rebuild_interval( void )
{
	static const int presetInterval[5] = { 1, 3, 2, 1, 1 };
	int preset = vk_rt_quality_preset();

	if ( preset > 0 ) {
		return presetInterval[preset];
	}
	if ( !rtx_rt_as_rebuild_interval ) {
		return 1;
	}
	return Com_Clamp( 1, 16, rtx_rt_as_rebuild_interval->integer );
}

static float vk_rt_as_dirty_threshold( void )
{
	if ( !rtx_rt_as_dirty_threshold ) {
		return 6.0f;
	}
	return Com_Clamp( 0.0f, 4096.0f, rtx_rt_as_dirty_threshold->value );
}

static int vk_rt_texture_stream_budget_mb( void )
{
	if ( !rtx_rt_texture_stream_budget_mb ) {
		return 96;
	}
	return Com_Clamp( 0, 2048, rtx_rt_texture_stream_budget_mb->integer );
}

static qboolean vk_rt_texture_stream_stats_enabled( void )
{
	if ( !rtx_rt_texture_stream_stats ) {
		return qfalse;
	}
	return rtx_rt_texture_stream_stats->integer ? qtrue : qfalse;
}

static int vk_rt_prep_frame_stride( void )
{
	static const int presetStride[5] = { 1, 3, 2, 1, 1 };
	int preset = vk_rt_quality_preset();

	if ( preset > 0 ) {
		return presetStride[preset];
	}
	if ( !rtx_rt_prep_frame_stride ) {
		return 1;
	}
	return Com_Clamp( 1, 8, rtx_rt_prep_frame_stride->integer );
}

static int64_t vk_rt_now_microseconds( void )
{
	if ( ri.Microseconds ) {
		return ri.Microseconds();
	}
	return (int64_t)ri.Milliseconds() * 1000;
}

static float vk_rt_elapsed_ms( int64_t startUs, int64_t endUs )
{
	if ( endUs <= startUs ) {
		return 0.0f;
	}
	return (float)( (double)( endUs - startUs ) * ( 1.0 / 1000.0 ) );
}

static uint32_t vk_rt_hash_mix_u32( uint32_t hash, uint32_t value )
{
	return ( hash ^ value ) * 16777619u;
}

static uint32_t vk_rt_hash_quantized_float( uint32_t hash, float value, float quantStep )
{
	const float step = ( quantStep > 0.0f ) ? quantStep : 1.0f;
	const float scaled = value / step;
	const int32_t quantized = (int32_t)( scaled >= 0.0f ? floorf( scaled + 0.5f ) : ceilf( scaled - 0.5f ) );
	return vk_rt_hash_mix_u32( hash, (uint32_t)quantized );
}

static qboolean vk_rt_entity_visible_in_current_view( const trRefEntity_t *ent )
{
	if ( !ent ) {
		return qfalse;
	}

	// Match legacy visibility behavior for first/third-person entities.
	if ( ( ent->e.renderfx & RF_FIRST_PERSON ) &&
		( backEnd.viewParms.portalView != PV_NONE ||
			( backEnd.refdef.rdflags & RDF_NOFIRSTPERSON ) ) ) {
		return qfalse;
	}
	if ( ( ent->e.renderfx & RF_THIRD_PERSON ) && ( backEnd.viewParms.portalView == PV_NONE ) ) {
		return qfalse;
	}

	return qtrue;
}

static uint32_t vk_rt_estimate_dynamic_scene_signature( vec3_t outCentroid, uint32_t *outModelCount, uint32_t *outEffectCount, float quantStep )
{
	uint32_t hash = 2166136261u;
	vec3_t sumOrigin = { 0.0f, 0.0f, 0.0f };
	uint32_t sampleCount = 0;
	const float originQuant = MAX( 1.0f, quantStep );
	const float axisQuant = 0.02f;
	int i;

	if ( outModelCount ) {
		*outModelCount = 0;
	}
	if ( outEffectCount ) {
		*outEffectCount = 0;
	}

	for ( i = 0; i < backEnd.refdef.num_entities; i++ ) {
		const trRefEntity_t *ent = &backEnd.refdef.entities[i];
		const refEntity_t *ref = &ent->e;
		vec3_t sampledOrigin;
		qboolean include = qfalse;
		uint32_t modelHandle = 0u;

		if ( !vk_rt_entity_visible_in_current_view( ent ) ) {
			continue;
		}

		VectorCopy( ref->origin, sampledOrigin );

		switch ( ref->reType ) {
		case RT_MODEL:
			if ( ref->hModel <= 0 ) {
				continue;
			}
			include = qtrue;
			modelHandle = (uint32_t)ref->hModel;
			if ( outModelCount ) {
				*outModelCount += 1u;
			}
			hash = vk_rt_hash_mix_u32( hash, (uint32_t)ref->frame );
			hash = vk_rt_hash_mix_u32( hash, (uint32_t)ref->oldframe );
			hash = vk_rt_hash_quantized_float( hash, ref->backlerp, 0.01f );
			hash = vk_rt_hash_quantized_float( hash, ref->axis[0][0], axisQuant );
			hash = vk_rt_hash_quantized_float( hash, ref->axis[0][1], axisQuant );
			hash = vk_rt_hash_quantized_float( hash, ref->axis[0][2], axisQuant );
			hash = vk_rt_hash_quantized_float( hash, ref->axis[1][0], axisQuant );
			hash = vk_rt_hash_quantized_float( hash, ref->axis[1][1], axisQuant );
			hash = vk_rt_hash_quantized_float( hash, ref->axis[1][2], axisQuant );
			hash = vk_rt_hash_quantized_float( hash, ref->axis[2][0], axisQuant );
			hash = vk_rt_hash_quantized_float( hash, ref->axis[2][1], axisQuant );
			hash = vk_rt_hash_quantized_float( hash, ref->axis[2][2], axisQuant );
			break;
		case RT_SPRITE:
		case RT_BEAM:
		case RT_RAIL_CORE:
		case RT_RAIL_RINGS:
		case RT_LIGHTNING:
			include = qtrue;
			if ( outEffectCount ) {
				*outEffectCount += 1u;
			}
			if ( ref->reType == RT_BEAM || ref->reType == RT_RAIL_CORE ||
				ref->reType == RT_RAIL_RINGS || ref->reType == RT_LIGHTNING ) {
				VectorAdd( ref->origin, ref->oldorigin, sampledOrigin );
				VectorScale( sampledOrigin, 0.5f, sampledOrigin );
			}
			break;
		default:
			break;
		}

		if ( !include ) {
			continue;
		}

		hash = vk_rt_hash_mix_u32( hash, (uint32_t)ref->reType );
		hash = vk_rt_hash_mix_u32( hash, modelHandle );
		hash = vk_rt_hash_mix_u32( hash, (uint32_t)ref->renderfx );
		hash = vk_rt_hash_quantized_float( hash, sampledOrigin[0], originQuant );
		hash = vk_rt_hash_quantized_float( hash, sampledOrigin[1], originQuant );
		hash = vk_rt_hash_quantized_float( hash, sampledOrigin[2], originQuant );

		VectorAdd( sumOrigin, sampledOrigin, sumOrigin );
		sampleCount++;
	}

	hash = vk_rt_hash_mix_u32( hash, (uint32_t)backEnd.refdef.numPolys );

	if ( sampleCount > 0 ) {
		const float invCount = 1.0f / (float)sampleCount;
		VectorScale( sumOrigin, invCount, outCentroid );
	} else {
		VectorCopy( vk.rt.dynamic_centroid_prev, outCentroid );
	}

	return hash;
}

static qboolean vk_rt_supports_reconstruction_blit( void )
{
	VkFormatProperties srcProps;
	VkFormatProperties dstProps;

	qvkGetPhysicalDeviceFormatProperties( vk.physical_device, vk.rt.output_format, &srcProps );
	if ( ( srcProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT ) == 0 ) {
		return qfalse;
	}
	if ( ( srcProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT ) == 0 ) {
		return qfalse;
	}

	qvkGetPhysicalDeviceFormatProperties( vk.physical_device, vk.color_format, &dstProps );
	if ( ( dstProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT ) == 0 ) {
		return qfalse;
	}

	return qtrue;
}

static qboolean vk_rt_ensure_timing_query_pool( void )
{
	VkQueryPoolCreateInfo queryInfo;
	VkResult result;

	if ( vk_rt_perf_timing_mode() <= 0 ) {
		if ( vk.rt.timing_query_pool != VK_NULL_HANDLE ) {
			if ( !vk_rt_wait_for_inflight_frames( "RT timing query disable" ) ) {
				return qfalse;
			}
			qvkDestroyQueryPool( vk.device, vk.rt.timing_query_pool, NULL );
			vk.rt.timing_query_pool = VK_NULL_HANDLE;
			vk.rt.timing_query_count = 0;
		}
		return qtrue;
	}

	if ( !qvkCreateQueryPool || !qvkCmdResetQueryPool || !qvkCmdWriteTimestamp || !qvkGetQueryPoolResults ) {
		return qtrue;
	}

	if ( vk.rt.timing_query_pool != VK_NULL_HANDLE ) {
		return qtrue;
	}

	vk.rt.timing_query_stride = 4;
	vk.rt.timing_query_count = vk.rt.timing_query_stride * 2u;

	Com_Memset( &queryInfo, 0, sizeof( queryInfo ) );
	queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
	queryInfo.queryCount = vk.rt.timing_query_count;

	result = qvkCreateQueryPool( vk.device, &queryInfo, NULL, &vk.rt.timing_query_pool );
	if ( result != VK_SUCCESS ) {
		ri.Printf( PRINT_WARNING, "RTX RT: failed to create timing query pool: %s\n", vk_result_string( result ) );
		vk.rt.timing_query_pool = VK_NULL_HANDLE;
		vk.rt.timing_query_count = 0;
		return qtrue;
	}

	return qtrue;
}

static void vk_rt_read_timing_query_results( void )
{
	uint64_t queryData[8];
	const uint32_t currentSlot = (uint32_t)( tr.frameCount & 1 );
	const uint32_t previousSlot = currentSlot ^ 1u;
	const uint32_t baseQuery = previousSlot * vk.rt.timing_query_stride;
	VkResult result;
	double toMs;
	uint64_t traceStart;
	uint64_t traceEnd;
	uint64_t copyEnd;
	uint64_t frameEnd;
	qboolean complete;

	if ( vk_rt_perf_timing_mode() <= 0 ) {
		return;
	}

	if ( vk.rt.timing_query_pool == VK_NULL_HANDLE || vk.rt.timing_query_stride < 4 || vk.rt.timing_query_count < 8 ) {
		return;
	}

	Com_Memset( queryData, 0, sizeof( queryData ) );
	result = qvkGetQueryPoolResults(
		vk.device,
		vk.rt.timing_query_pool,
		baseQuery,
		vk.rt.timing_query_stride,
		sizeof( queryData ),
		queryData,
		sizeof( uint64_t ) * 2,
		VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT );
	if ( result != VK_SUCCESS && result != VK_NOT_READY ) {
		return;
	}

	complete = ( queryData[1] != 0u && queryData[3] != 0u && queryData[5] != 0u && queryData[7] != 0u ) ? qtrue : qfalse;
	if ( !complete ) {
		return;
	}

	traceStart = queryData[0];
	traceEnd = queryData[2];
	copyEnd = queryData[4];
	frameEnd = queryData[6];
	toMs = (double)MAX( 0.0f, vk.rt.gpu_timestamp_period_ns ) * ( 1.0 / 1000000.0 );

	if ( traceEnd >= traceStart ) {
		vk.rt.perf.gpu_trace_ms = (float)( (double)( traceEnd - traceStart ) * toMs );
	}
	if ( copyEnd >= traceEnd ) {
		vk.rt.perf.gpu_copy_ms = (float)( (double)( copyEnd - traceEnd ) * toMs );
	}
	if ( frameEnd >= traceStart ) {
		vk.rt.perf.gpu_frame_ms = (float)( (double)( frameEnd - traceStart ) * toMs );
	}
}

static void vk_rt_update_scalability_controller( void )
{
	float sourceFrameMs;
	float targetFrameMs;
	float response;
	float minScale;
	float maxScale;

	sourceFrameMs = ( vk_rt_perf_timing_mode() > 0 && vk.rt.perf.gpu_frame_ms > 0.0f ) ?
		vk.rt.perf.gpu_frame_ms : vk.rt.perf.cpu_frame_ms;
	targetFrameMs = vk_rt_target_frame_ms();
	response = vk_rt_budget_response();
	minScale = vk_rt_budget_min_scale();
	maxScale = vk_rt_budget_max_scale();

	if ( minScale > maxScale ) {
		float tmp = minScale;
		minScale = maxScale;
		maxScale = tmp;
	}

	if ( !vk_rt_adaptive_budget_enabled() ) {
		vk.rt.adaptive_budget_scale = 1.0f;
	} else if ( sourceFrameMs > 0.0f && targetFrameMs > 0.0f ) {
		const float delta = ( targetFrameMs - sourceFrameMs ) / targetFrameMs;
		vk.rt.adaptive_budget_scale += response * delta;
		vk.rt.adaptive_budget_scale = Com_Clamp( minScale, maxScale, vk.rt.adaptive_budget_scale );
	} else {
		vk.rt.adaptive_budget_scale = Com_Clamp( minScale, maxScale, vk.rt.adaptive_budget_scale );
	}

	if ( vk_rt_dynamic_resolution_enabled() ) {
		float dynMin = vk_rt_dynamic_resolution_min_scale();
		float dynMax = vk_rt_dynamic_resolution_max_scale();
		float dynRate = vk_rt_dynamic_resolution_rate();

		if ( dynMin > dynMax ) {
			float tmp = dynMin;
			dynMin = dynMax;
			dynMax = tmp;
		}

		if ( sourceFrameMs > 0.0f && targetFrameMs > 0.0f ) {
			const float delta = ( targetFrameMs - sourceFrameMs ) / targetFrameMs;
			vk.rt.dynamic_resolution_target_scale += response * delta;
		} else {
			vk.rt.dynamic_resolution_target_scale = dynMax;
		}

		vk.rt.dynamic_resolution_target_scale = Com_Clamp( dynMin, dynMax, vk.rt.dynamic_resolution_target_scale );
		vk.rt.dynamic_resolution_scale += ( vk.rt.dynamic_resolution_target_scale - vk.rt.dynamic_resolution_scale ) * dynRate;
		vk.rt.dynamic_resolution_scale = Com_Clamp( dynMin, dynMax, vk.rt.dynamic_resolution_scale );
	} else {
		vk.rt.dynamic_resolution_target_scale = 1.0f;
		vk.rt.dynamic_resolution_scale = 1.0f;
	}
}

static void vk_rt_log_perf_summary( void )
{
	static uint64_t lastTextureBytes = 0;
	static uint64_t lastTextureFlushes = 0;
	static uint64_t lastAsyncSubmits = 0;
	const int timingMode = vk_rt_perf_timing_mode();
	const int interval = vk_rt_perf_interval_frames();
	const qboolean wantTextureStats = vk_rt_texture_stream_stats_enabled();
	const qboolean printPerFrame = ( timingMode >= 2 ) ? qtrue : qfalse;
	const qboolean printPeriodic = ( ( tr.frameCount % interval ) == 0 ) ? qtrue : qfalse;

	if ( timingMode <= 0 && !wantTextureStats ) {
		return;
	}
	if ( !printPerFrame && !printPeriodic ) {
		return;
	}

	if ( timingMode > 0 ) {
		ri.Printf( PRINT_ALL,
			"RTX RT perf: cpu(ms) dyn=%.3f world=%.3f tlas=%.3f temporal=%.3f dispatch=%.3f copy=%.3f frame=%.3f | gpu(ms) trace=%.3f copy=%.3f frame=%.3f | scale budget=%.3f dynres=%.3f (%ux%u)\n",
			vk.rt.perf.cpu_dynamic_ms,
			vk.rt.perf.cpu_world_ms,
			vk.rt.perf.cpu_tlas_ms,
			vk.rt.perf.cpu_temporal_ms,
			vk.rt.perf.cpu_dispatch_ms,
			vk.rt.perf.cpu_copy_ms,
			vk.rt.perf.cpu_frame_ms,
			vk.rt.perf.gpu_trace_ms,
			vk.rt.perf.gpu_copy_ms,
			vk.rt.perf.gpu_frame_ms,
			vk.rt.adaptive_budget_scale,
			vk.rt.dynamic_resolution_scale,
			vk.rt.output_width,
			vk.rt.output_height );
	}

	if ( wantTextureStats ) {
		const uint64_t bytesDelta = vk.rt.stats.texture_stream_bytes - lastTextureBytes;
		const uint64_t flushDelta = vk.rt.stats.texture_stream_flushes - lastTextureFlushes;
		const uint64_t asyncDelta = vk.rt.stats.async_overlap_submits - lastAsyncSubmits;
		const float mbDelta = (float)( (double)bytesDelta / ( 1024.0 * 1024.0 ) );

		ri.Printf( PRINT_ALL,
			"RTX RT stream: uploads=%.2fMB flushes=%llu asyncSubmits=%llu budget=%dMB staging=%lluKB rebuildSkips=%llu prepStrideSkips=%llu\n",
			mbDelta,
			(unsigned long long)flushDelta,
			(unsigned long long)asyncDelta,
			vk_rt_texture_stream_budget_mb(),
			(unsigned long long)( vk_world.staging_buffer_size / 1024u ),
			(unsigned long long)vk.rt.stats.dynamic_rebuild_skips,
			(unsigned long long)vk.rt.stats.prep_stride_skips );

		lastTextureBytes = vk.rt.stats.texture_stream_bytes;
		lastTextureFlushes = vk.rt.stats.texture_stream_flushes;
		lastAsyncSubmits = vk.rt.stats.async_overlap_submits;
	}
}

static int vk_rt_as_build_budget_mb( void )
{
	static const int presetBudget[5] = { 96, 48, 96, 160, 256 };
	int preset = vk_rt_quality_preset();
	int base;
	float scale = 1.0f;

	if ( preset > 0 ) {
		base = presetBudget[preset];
	} else if ( rtx_rt_as_build_budget_mb ) {
		base = rtx_rt_as_build_budget_mb->integer;
	} else {
		base = 0;
	}

	if ( base <= 0 ) {
		return 0;
	}

	if ( vk_rt_adaptive_budget_enabled() ) {
		scale = Com_Clamp( vk_rt_budget_min_scale(), vk_rt_budget_max_scale(), vk.rt.adaptive_budget_scale );
	}

	return Com_Clamp( 1, 4096, (int)( (float)base * scale ) );
}

static int vk_rt_as_instance_budget( void )
{
	static const int presetInstances[5] = { 1024, 384, 768, 1536, 3072 };
	int preset = vk_rt_quality_preset();
	int base;
	float scale = 1.0f;

	if ( preset > 0 ) {
		base = presetInstances[preset];
	} else if ( rtx_rt_as_instance_budget ) {
		base = rtx_rt_as_instance_budget->integer;
	} else {
		base = 1024;
	}

	if ( vk_rt_adaptive_budget_enabled() ) {
		scale = Com_Clamp( vk_rt_budget_min_scale(), vk_rt_budget_max_scale(), vk.rt.adaptive_budget_scale );
	}

	return Com_Clamp( 64, 16384, (int)( (float)base * scale ) );
}

static qboolean vk_rt_as_compaction_enabled( void )
{
	if ( !rtx_rt_as_compaction ) {
		return qfalse;
	}
	return rtx_rt_as_compaction->integer ? qtrue : qfalse;
}

static int vk_rt_debug_as_stats_level( void )
{
	if ( !rtx_rt_debug_as_stats ) {
		return 0;
	}
	return rtx_rt_debug_as_stats->integer;
}

static qboolean vk_rt_debug_should_log_skip( int *lastLoggedFrame )
{
	const int logIntervalFrames = 128;
	const int frame = tr.frameCount;

	if ( vk_rt_debug_as_stats_level() < 2 ) {
		return qfalse;
	}
	if ( *lastLoggedFrame >= 0 &&
		frame >= *lastLoggedFrame &&
		frame - *lastLoggedFrame < logIntervalFrames ) {
		return qfalse;
	}

	*lastLoggedFrame = frame;
	return qtrue;
}

static qboolean vk_rt_dynamic_blas_enabled( void )
{
	if ( !rtx_rt_dynamic_blas ) {
		return qfalse;
	}
	return rtx_rt_dynamic_blas->integer ? qtrue : qfalse;
}

static qboolean vk_rt_temporal_enabled( void )
{
	if ( !rtx_rt_temporal_enable ) {
		return qtrue;
	}
	return rtx_rt_temporal_enable->integer ? qtrue : qfalse;
}

static float vk_rt_temporal_alpha( void )
{
	if ( !rtx_rt_temporal_alpha ) {
		return 0.88f;
	}
	return Com_Clamp( 0.0f, 0.98f, rtx_rt_temporal_alpha->value );
}

static float vk_rt_temporal_clamp_strength( void )
{
	if ( !rtx_rt_temporal_clamp ) {
		return 1.25f;
	}
	return MAX( 0.0f, rtx_rt_temporal_clamp->value );
}

static float vk_rt_temporal_depth_reject( void )
{
	if ( !rtx_rt_temporal_depth_reject ) {
		return 4.0f;
	}
	return MAX( 0.0f, rtx_rt_temporal_depth_reject->value );
}

static float vk_rt_temporal_motion_reject( void )
{
	if ( !rtx_rt_temporal_motion_reject ) {
		return 18.0f;
	}
	return MAX( 0.0f, rtx_rt_temporal_motion_reject->value );
}

static float vk_rt_temporal_camera_cut_distance( void )
{
	if ( !rtx_rt_temporal_camera_cut ) {
		return 128.0f;
	}
	return MAX( 0.0f, rtx_rt_temporal_camera_cut->value );
}

static qboolean vk_rt_spatial_denoise_enabled( void )
{
	if ( !rtx_rt_spatial_denoise ) {
		return qtrue;
	}
	return rtx_rt_spatial_denoise->integer ? qtrue : qfalse;
}

static float vk_rt_spatial_sigma_depth( void )
{
	if ( !rtx_rt_spatial_sigma_depth ) {
		return 2.5f;
	}
	return MAX( 0.0f, rtx_rt_spatial_sigma_depth->value );
}

static float vk_rt_spatial_sigma_normal( void )
{
	if ( !rtx_rt_spatial_sigma_normal ) {
		return 24.0f;
	}
	return MAX( 0.0f, rtx_rt_spatial_sigma_normal->value );
}

static float vk_rt_firefly_threshold( void )
{
	if ( !rtx_rt_firefly_threshold ) {
		return 8.0f;
	}
	return MAX( 0.0f, rtx_rt_firefly_threshold->value );
}

static float vk_rt_firefly_strength( void )
{
	if ( !rtx_rt_firefly_strength ) {
		return 0.75f;
	}
	return Com_Clamp( 0.0f, 1.0f, rtx_rt_firefly_strength->value );
}

static int vk_rt_tonemap_mode( void )
{
	if ( !rtx_rt_tonemap_mode ) {
		return 2;
	}
	return Com_Clamp( 0, 2, rtx_rt_tonemap_mode->integer );
}

static float vk_rt_exposure_manual( void )
{
	if ( !rtx_rt_exposure ) {
		return 1.0f;
	}
	return Com_Clamp( 0.01f, 32.0f, rtx_rt_exposure->value );
}

static qboolean vk_rt_auto_exposure_enabled( void )
{
	if ( !rtx_rt_auto_exposure ) {
		return qtrue;
	}
	return rtx_rt_auto_exposure->integer ? qtrue : qfalse;
}

static float vk_rt_exposure_speed( void )
{
	if ( !rtx_rt_exposure_speed ) {
		return 0.08f;
	}
	return Com_Clamp( 0.0f, 1.0f, rtx_rt_exposure_speed->value );
}

static float vk_rt_exposure_min( void )
{
	if ( !rtx_rt_exposure_min ) {
		return 0.2f;
	}
	return Com_Clamp( 0.01f, 32.0f, rtx_rt_exposure_min->value );
}

static float vk_rt_exposure_max( void )
{
	if ( !rtx_rt_exposure_max ) {
		return 4.0f;
	}
	return Com_Clamp( 0.01f, 64.0f, rtx_rt_exposure_max->value );
}

static float vk_rt_motion_dynamic_scale( void )
{
	if ( !rtx_rt_motion_dynamic_scale ) {
		return 1.0f;
	}
	return Com_Clamp( 0.0f, 4.0f, rtx_rt_motion_dynamic_scale->value );
}

static qboolean vk_rt_post_validate_enabled( void )
{
	if ( !rtx_rt_post_validate ) {
		return qfalse;
	}
	return rtx_rt_post_validate->integer ? qtrue : qfalse;
}

static qboolean vk_rt_first_person_priority_enabled( void )
{
	if ( !rtx_rt_first_person_priority ) {
		return qtrue;
	}
	return rtx_rt_first_person_priority->integer ? qtrue : qfalse;
}

static float vk_rt_effect_radius_scale( void )
{
	if ( !rtx_rt_effect_radius_scale ) {
		return 1.2f;
	}
	return Com_Clamp( 0.1f, 4.0f, rtx_rt_effect_radius_scale->value );
}

static float vk_rt_effect_emissive_boost( void )
{
	if ( !rtx_rt_effect_emissive_boost ) {
		return 2.0f;
	}
	return MAX( 0.0f, rtx_rt_effect_emissive_boost->value );
}

static qboolean vk_rt_marks_enabled( void )
{
	if ( !rtx_rt_marks_enable ) {
		return qtrue;
	}
	return rtx_rt_marks_enable->integer ? qtrue : qfalse;
}

static qboolean vk_rt_particle_volume_enabled( void )
{
	if ( !rtx_rt_particle_volume ) {
		return qtrue;
	}
	return rtx_rt_particle_volume->integer ? qtrue : qfalse;
}

static qboolean vk_rt_ui_passthrough_enabled( void )
{
	if ( !rtx_rt_ui_passthrough ) {
		return qtrue;
	}
	return rtx_rt_ui_passthrough->integer ? qtrue : qfalse;
}

static qboolean vk_rt_camera_mode_validate_enabled( void )
{
	if ( !rtx_rt_camera_mode_validate ) {
		return qtrue;
	}
	return rtx_rt_camera_mode_validate->integer ? qtrue : qfalse;
}

static qboolean vk_rt_raster_reference_enabled( void )
{
	if ( !rtx_rt_raster_reference ) {
		return qfalse;
	}
	return rtx_rt_raster_reference->integer ? qtrue : qfalse;
}

static float vk_rt_world_light_scale( void )
{
	if ( !rtx_rt_world_light_scale ) {
		return 0.0f;
	}
	return MAX( 0.0f, rtx_rt_world_light_scale->value );
}

static float vk_rt_legacy_intensity_scale( void )
{
	if ( !rtx_rt_legacy_color_compat ||
		!rtx_rt_legacy_color_compat->integer ||
		!r_intensity ) {
		return 1.0f;
	}
	return Com_Clamp( 0.5f, 4.0f, r_intensity->value );
}

static float vk_rt_readability_lift( void )
{
	if ( !rtx_rt_readability_lift ) {
		return 0.06f;
	}
	return Com_Clamp( -1.0f, 1.0f, rtx_rt_readability_lift->value );
}

static float vk_rt_readability_contrast( void )
{
	if ( !rtx_rt_readability_contrast ) {
		return 1.05f;
	}
	return Com_Clamp( 0.2f, 3.0f, rtx_rt_readability_contrast->value );
}

static float vk_rt_readability_saturation( void )
{
	if ( !rtx_rt_readability_saturation ) {
		return 1.08f;
	}
	return Com_Clamp( 0.0f, 3.0f, rtx_rt_readability_saturation->value );
}

static float vk_rt_readability_shadow_floor( void )
{
	if ( !rtx_rt_readability_shadow_floor ) {
		return 0.05f;
	}
	return Com_Clamp( 0.0f, 0.5f, rtx_rt_readability_shadow_floor->value );
}

static int vk_rt_shadow_mode( void )
{
	if ( !rtx_rt_shadow_mode ) {
		return 2;
	}
	return rtx_rt_shadow_mode->integer;
}

static float vk_rt_shadow_softness( void )
{
	if ( !rtx_rt_shadow_softness ) {
		return 0.25f;
	}
	return MAX( 0.0f, rtx_rt_shadow_softness->value );
}

static qboolean vk_rt_indirect_bounce_enabled( void )
{
	if ( !rtx_rt_indirect_bounce ) {
		return qtrue;
	}
	return rtx_rt_indirect_bounce->integer ? qtrue : qfalse;
}

static float vk_rt_indirect_strength( void )
{
	if ( !rtx_rt_indirect_strength ) {
		return 0.35f;
	}
	return MAX( 0.0f, rtx_rt_indirect_strength->value );
}

static float vk_rt_reflection_strength( void )
{
	if ( !rtx_rt_reflection_strength ) {
		return 1.0f;
	}
	return MAX( 0.0f, rtx_rt_reflection_strength->value );
}

static float vk_rt_sky_intensity( void )
{
	if ( !rtx_rt_sky_intensity ) {
		return 1.0f;
	}
	return MAX( 0.0f, rtx_rt_sky_intensity->value );
}

static float vk_rt_sun_intensity( void )
{
	if ( !rtx_rt_sun_intensity ) {
		return 1.0f;
	}
	return MAX( 0.0f, rtx_rt_sun_intensity->value );
}

static int vk_rt_refractive_mode( void )
{
	if ( !rtx_rt_refractive_mode ) {
		return 1;
	}
	return rtx_rt_refractive_mode->integer;
}

static float vk_rt_refractive_ior( void )
{
	if ( !rtx_rt_refractive_ior ) {
		return 1.33f;
	}
	return Com_Clamp( 1.0f, 2.5f, rtx_rt_refractive_ior->value );
}

static uint32_t vk_rt_max_lights( void )
{
	static const int presetLights[5] = { 64, 24, 48, 64, 96 };
	int preset = vk_rt_quality_preset();
	int base;
	float scale = 1.0f;

	if ( preset > 0 ) {
		base = presetLights[preset];
	} else if ( rtx_rt_max_lights ) {
		base = rtx_rt_max_lights->integer;
	} else {
		base = 64;
	}

	if ( vk_rt_adaptive_budget_enabled() ) {
		scale = Com_Clamp( vk_rt_budget_min_scale(), vk_rt_budget_max_scale(), vk.rt.adaptive_budget_scale );
	}

	return (uint32_t)Com_Clamp( 8, 256, (int)( (float)base * scale ) );
}

static const char *vk_rt_mode_name( int mode )
{
	switch ( mode ) {
	case RTX_RT_MODE_DISABLED: return "disabled";
	case RTX_RT_MODE_RAY_QUERY: return "ray_query";
	case RTX_RT_MODE_RAY_TRACING_PIPELINE: return "ray_tracing_pipeline";
	default: return "unknown";
	}
}

static qboolean vk_rt_pipeline_enabled( void )
{
	return ( vk.caps.activeRtMode == RTX_RT_MODE_RAY_TRACING_PIPELINE ) ? qtrue : qfalse;
}

static qboolean vk_rt_trace_available( void )
{
	return vk_rt_pipeline_enabled();
}

static void vk_rt_reset_state( void )
{
	Com_Memset( &vk.rt, 0, sizeof( vk.rt ) );
	vk.rt.output_format = VK_FORMAT_R16G16B16A16_SFLOAT;
	vk.rt.activeVisualizer = 0;
	vk.rt.history_read_index = 0;
	vk.rt.history_valid = qfalse;
	vk.rt.temporal_reset_pending = qtrue;
	vk.rt.temporal_has_prev_camera = qfalse;
	vk.rt.temporal_prev_refdef_time = 0;
	vk.rt.temporal_prev_rdflags = 0;
	vk.rt.temporal_prev_stereo_frame = 0;
	vk.rt.temporal_exposure = vk_rt_exposure_manual();
	vk.rt.adaptive_budget_scale = 1.0f;
	vk.rt.dynamic_resolution_scale = 1.0f;
	vk.rt.dynamic_resolution_target_scale = 1.0f;
	vk.rt.dynamic_scene_signature = 0u;
	vk.rt.dynamic_last_rebuild_frame = -1;
	vk.rt.gpu_timestamp_period_ns = 1.0f;
	vk.rt.timing_query_pool = VK_NULL_HANDLE;
	vk.rt.timing_query_count = 0;
	vk.rt.timing_query_stride = 4;
	VectorClear( vk.rt.dynamic_centroid_current );
	VectorClear( vk.rt.dynamic_centroid_prev );
	VectorClear( vk.rt.dynamic_last_rebuild_centroid );
	s_vkRtDynResBlitWarned = qfalse;
	s_vkRtTextureBudgetFrame = -1;
	s_vkRtTextureBudgetUsed = 0;
	vk_rt_reset_world_light_cache();
}

static qboolean vk_rt_align_up_checked( VkDeviceSize value, VkDeviceSize alignment, VkDeviceSize *outValue )
{
	VkDeviceSize remainder;
	VkDeviceSize padding;

	if ( !outValue ) {
		return qfalse;
	}
	if ( alignment == 0 ) {
		*outValue = value;
		return qtrue;
	}

	remainder = value % alignment;
	if ( remainder == 0 ) {
		*outValue = value;
		return qtrue;
	}

	padding = alignment - remainder;
	if ( value > ( (VkDeviceSize)-1 ) - padding ) {
		return qfalse;
	}

	*outValue = value + padding;
	return qtrue;
}

static qboolean vk_rt_device_size_add_checked( VkDeviceSize lhs, VkDeviceSize rhs, VkDeviceSize *outValue )
{
	if ( !outValue || lhs > ( (VkDeviceSize)-1 ) - rhs ) {
		return qfalse;
	}
	*outValue = lhs + rhs;
	return qtrue;
}

static qboolean vk_rt_device_size_multiply_checked( VkDeviceSize lhs, VkDeviceSize rhs, VkDeviceSize *outValue )
{
	if ( !outValue || ( lhs != 0 && rhs > ( (VkDeviceSize)-1 ) / lhs ) ) {
		return qfalse;
	}
	*outValue = lhs * rhs;
	return qtrue;
}

static qboolean vk_rt_is_power_of_two( VkDeviceSize value )
{
	return ( value != 0 && ( value & ( value - 1 ) ) == 0 ) ? qtrue : qfalse;
}

static qboolean vk_rt_wait_for_inflight_frames( const char *reason )
{
	VkFence fences[ NUM_COMMAND_BUFFERS ];
	uint32_t count = 0;
	uint32_t i;
	VkResult result;

	if ( !vk.active || vk.device == VK_NULL_HANDLE ) {
		return qtrue;
	}

	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		if ( vk.tess[i].waitForFence && vk.tess[i].rendering_finished_fence != VK_NULL_HANDLE ) {
			fences[count++] = vk.tess[i].rendering_finished_fence;
		}
	}

	if ( count == 0 ) {
		return qtrue;
	}

	result = qvkWaitForFences( vk.device, count, fences, VK_TRUE, 1000000000000ULL );
	if ( result != VK_SUCCESS ) {
		ri.Printf( PRINT_ERROR, "RTX RT: wait-for-fences failed before %s: %s\n",
			reason ? reason : "resource rebuild", vk_result_string( result ) );
		return qfalse;
	}

	return qtrue;
}

static uint32_t vk_rt_clamped_visualizer_mode( void )
{
	int mode = vk_rt_debug_visualizer_mode();
	if ( mode < 0 ) {
		mode = 0;
	}
	if ( mode > RTX_RT_MAX_DEBUG_VISUALIZER ) {
		mode = RTX_RT_MAX_DEBUG_VISUALIZER;
	}
	return (uint32_t)mode;
}

static qboolean vk_rt_budget_allows( VkDeviceSize bytesNeeded, VkDeviceSize *bytesUsed, VkDeviceSize budgetBytes )
{
	if ( budgetBytes == 0 ) {
		return qtrue;
	}

	if ( bytesNeeded > budgetBytes || *bytesUsed > budgetBytes || ( budgetBytes - *bytesUsed ) < bytesNeeded ) {
		vk.rt.stats.throttled_builds++;
		if ( vk_rt_debug_as_stats_level() >= 1 ) {
			ri.Printf( PRINT_DEVELOPER,
				"RTX RT: build throttled (needed=%llu, used=%llu, budget=%llu)\n",
				(unsigned long long)bytesNeeded,
				(unsigned long long)( *bytesUsed ),
				(unsigned long long)budgetBytes );
		}
		return qfalse;
	}

	*bytesUsed += bytesNeeded;
	return qtrue;
}

static VkDeviceAddress vk_get_buffer_device_address( const VkBufferDeviceAddressInfo *addressInfo )
{
	if ( qvkGetBufferDeviceAddress ) {
		return qvkGetBufferDeviceAddress( vk.device, addressInfo );
	}
	if ( qvkGetBufferDeviceAddressKHR ) {
		return qvkGetBufferDeviceAddressKHR( vk.device, addressInfo );
	}
	return 0;
}

static qboolean vk_rt_create_buffer( VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryProps, qboolean needDeviceAddress, const char *debugName, rtxVkRtBuffer_t *outBuffer )
{
	VkBufferCreateInfo bufferInfo;
	VkMemoryRequirements memoryRequirements;
	VkMemoryAllocateInfo allocInfo;
	VkMemoryAllocateFlagsInfo allocFlagsInfo;
	VkResult result;
	void *mapped = NULL;
	uint32_t memoryType;
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;

	Com_Memset( outBuffer, 0, sizeof( *outBuffer ) );

	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.pNext = NULL;
	bufferInfo.flags = 0;
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	bufferInfo.queueFamilyIndexCount = 0;
	bufferInfo.pQueueFamilyIndices = NULL;

	result = qvkCreateBuffer( vk.device, &bufferInfo, NULL, &buffer );
	if ( result != VK_SUCCESS ) {
		ri.Printf( PRINT_ERROR, "RTX RT: failed to create buffer '%s': %s\n",
			debugName ? debugName : "unnamed", vk_result_string( result ) );
		return qfalse;
	}

	qvkGetBufferMemoryRequirements( vk.device, buffer, &memoryRequirements );
	memoryType = find_memory_type( memoryRequirements.memoryTypeBits, memoryProps );

	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.pNext = NULL;
	allocInfo.allocationSize = memoryRequirements.size;
	allocInfo.memoryTypeIndex = memoryType;

	if ( needDeviceAddress ) {
		Com_Memset( &allocFlagsInfo, 0, sizeof( allocFlagsInfo ) );
		allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
		allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
		allocInfo.pNext = &allocFlagsInfo;
	}

	result = qvkAllocateMemory( vk.device, &allocInfo, NULL, &memory );
	if ( result != VK_SUCCESS ) {
		ri.Printf( PRINT_ERROR, "RTX RT: failed to allocate buffer memory '%s': %s\n",
			debugName ? debugName : "unnamed", vk_result_string( result ) );
		qvkDestroyBuffer( vk.device, buffer, NULL );
		return qfalse;
	}

	result = qvkBindBufferMemory( vk.device, buffer, memory, 0 );
	if ( result != VK_SUCCESS ) {
		ri.Printf( PRINT_ERROR, "RTX RT: failed to bind buffer memory '%s': %s\n",
			debugName ? debugName : "unnamed", vk_result_string( result ) );
		qvkFreeMemory( vk.device, memory, NULL );
		qvkDestroyBuffer( vk.device, buffer, NULL );
		return qfalse;
	}

	if ( memoryProps & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ) {
		result = qvkMapMemory( vk.device, memory, 0, VK_WHOLE_SIZE, 0, &mapped );
		if ( result != VK_SUCCESS ) {
			ri.Printf( PRINT_ERROR, "RTX RT: failed to map buffer '%s': %s\n",
				debugName ? debugName : "unnamed", vk_result_string( result ) );
			qvkFreeMemory( vk.device, memory, NULL );
			qvkDestroyBuffer( vk.device, buffer, NULL );
			return qfalse;
		}
	}

	outBuffer->buffer = buffer;
	outBuffer->memory = memory;
	outBuffer->capacity = size;
	outBuffer->allocationSize = memoryRequirements.size;
	outBuffer->address = 0;

	if ( needDeviceAddress ) {
		VkBufferDeviceAddressInfo addressInfo;
		addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
		addressInfo.pNext = NULL;
		addressInfo.buffer = buffer;
		outBuffer->address = vk_get_buffer_device_address( &addressInfo );
	}

	if ( mapped ) {
		qvkUnmapMemory( vk.device, memory );
	}

	if ( needDeviceAddress && outBuffer->address == 0 ) {
		ri.Printf( PRINT_ERROR, "RTX RT: buffer '%s' has no device address\n",
			debugName ? debugName : "unnamed" );
		qvkFreeMemory( vk.device, memory, NULL );
		qvkDestroyBuffer( vk.device, buffer, NULL );
		Com_Memset( outBuffer, 0, sizeof( *outBuffer ) );
		return qfalse;
	}

	return qtrue;
}

static void vk_rt_destroy_buffer( rtxVkRtBuffer_t *buffer )
{
	if ( !buffer ) {
		return;
	}

	if ( buffer->buffer != VK_NULL_HANDLE ) {
		qvkDestroyBuffer( vk.device, buffer->buffer, NULL );
	}
	if ( buffer->memory != VK_NULL_HANDLE ) {
		qvkFreeMemory( vk.device, buffer->memory, NULL );
	}

	Com_Memset( buffer, 0, sizeof( *buffer ) );
}

static qboolean vk_rt_ensure_buffer_capacity( rtxVkRtBuffer_t *buffer, VkDeviceSize minSize, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryProps, qboolean needDeviceAddress, const char *debugName )
{
	VkDeviceSize allocSize;

	if ( !buffer ) {
		return qfalse;
	}

	allocSize = ( minSize > 0 ) ? minSize : 1;

	if ( buffer->buffer != VK_NULL_HANDLE && buffer->memory != VK_NULL_HANDLE ) {
		if ( buffer->capacity >= allocSize && ( !needDeviceAddress || buffer->address != 0 ) ) {
			return qtrue;
		}

		if ( buffer->capacity > 0 && buffer->capacity <= ( (VkDeviceSize)-1 ) / 2 ) {
			VkDeviceSize grown = buffer->capacity * 2;
			if ( grown > allocSize ) {
				allocSize = grown;
			}
		}

		if ( !vk_rt_wait_for_inflight_frames( debugName ) ) {
			return qfalse;
		}
		vk_rt_destroy_buffer( buffer );
	}

	return vk_rt_create_buffer(
		allocSize,
		usage,
		memoryProps,
		needDeviceAddress,
		debugName,
		buffer );
}

static qboolean vk_rt_upload_buffer_data( const rtxVkRtBuffer_t *buffer, const void *src, size_t bytes )
{
	void *mapped = NULL;
	VkResult result;
	VkMappedMemoryRange range;
	VkDeviceSize uploadSize;
	VkDeviceSize atomSize = 1;
	VkDeviceSize flushSize;
	VkPhysicalDeviceProperties props;

	if ( !buffer || buffer->memory == VK_NULL_HANDLE || !src || bytes == 0 ) {
		return qfalse;
	}

	uploadSize = (VkDeviceSize)bytes;
	if ( (size_t)uploadSize != bytes ) {
		ri.Printf( PRINT_ERROR, "RTX RT: upload size is not representable by Vulkan\n" );
		return qfalse;
	}
	if ( uploadSize > buffer->capacity ) {
		ri.Printf( PRINT_ERROR, "RTX RT: upload size %llu exceeds buffer size %llu\n",
			(unsigned long long)uploadSize, (unsigned long long)buffer->capacity );
		return qfalse;
	}

	if ( qvkGetPhysicalDeviceProperties ) {
		qvkGetPhysicalDeviceProperties( vk.physical_device, &props );
		if ( props.limits.nonCoherentAtomSize > 0 ) {
			atomSize = props.limits.nonCoherentAtomSize;
		}
	}

	if ( !vk_rt_align_up_checked( uploadSize, atomSize, &flushSize ) ||
		flushSize > buffer->allocationSize ) {
		flushSize = buffer->allocationSize;
	}
	if ( flushSize < uploadSize ) {
		ri.Printf( PRINT_ERROR, "RTX RT: backing allocation is smaller than upload range\n" );
		return qfalse;
	}

	result = qvkMapMemory( vk.device, buffer->memory, 0, flushSize, 0, &mapped );
	if ( result != VK_SUCCESS ) {
		ri.Printf( PRINT_ERROR, "RTX RT: failed to map upload buffer: %s\n", vk_result_string( result ) );
		return qfalse;
	}

	Com_Memcpy( mapped, src, bytes );

	range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
	range.pNext = NULL;
	range.memory = buffer->memory;
	range.offset = 0;
	range.size = flushSize;

	result = qvkFlushMappedMemoryRanges( vk.device, 1, &range );
	if ( result != VK_SUCCESS ) {
		ri.Printf( PRINT_ERROR, "RTX RT: failed to flush upload buffer: %s\n", vk_result_string( result ) );
		qvkUnmapMemory( vk.device, buffer->memory );
		return qfalse;
	}

	qvkUnmapMemory( vk.device, buffer->memory );
	return qtrue;
}

static qboolean vk_rt_upload_material_buffer( const rtxRtMaterial_t *materials, uint32_t materialCount, rtxVkRtBuffer_t *targetBuffer, uint32_t *outMaterialCount, const char *debugName )
{
	rtxRtGpuMaterial_t *gpuMaterials = NULL;
	const uint32_t uploadCount = materialCount > 0 ? materialCount : 1u;
	size_t uploadBytes = (size_t)uploadCount * sizeof( rtxRtGpuMaterial_t );
	uint32_t i;

	gpuMaterials = (rtxRtGpuMaterial_t *)ri.Malloc( uploadBytes );
	Com_Memset( gpuMaterials, 0, uploadBytes );

	for ( i = 0; i < materialCount; i++ ) {
		const rtxRtMaterial_t *src = &materials[i];
		rtxRtGpuMaterial_t *dst = &gpuMaterials[i];

		dst->baseColor[0] = src->baseColor[0];
		dst->baseColor[1] = src->baseColor[1];
		dst->baseColor[2] = src->baseColor[2];
		dst->baseColor[3] = src->baseColor[3];
		dst->emissiveColorScale[0] = src->emissiveColor[0];
		dst->emissiveColorScale[1] = src->emissiveColor[1];
		dst->emissiveColorScale[2] = src->emissiveColor[2];
		dst->emissiveColorScale[3] = src->emissiveScale;
		dst->pbrParams[0] = src->roughness;
		dst->pbrParams[1] = src->metallic;
		dst->pbrParams[2] = src->alphaCutoff;
		dst->pbrParams[3] = vk_rt_refractive_ior();
			dst->metadata[0] = src->flags;
			dst->metadata[1] = src->albedoColorSpace;
			dst->metadata[2] = src->normalColorSpace;
			dst->metadata[3] = src->emissiveColorSpace;
			dst->textureInfo[0] = ( src->albedoTextureIndex >= 0 ) ? (uint32_t)src->albedoTextureIndex : RTX_RT_INVALID_TEXTURE_INDEX;
			dst->textureInfo[1] = 0u;
			dst->textureInfo[2] = 0u;
			dst->textureInfo[3] = 0u;
		}

	if ( materialCount == 0 ) {
		gpuMaterials[0].baseColor[0] = 1.0f;
		gpuMaterials[0].baseColor[1] = 1.0f;
		gpuMaterials[0].baseColor[2] = 1.0f;
		gpuMaterials[0].baseColor[3] = 1.0f;
		gpuMaterials[0].pbrParams[0] = 0.85f;
		gpuMaterials[0].pbrParams[3] = vk_rt_refractive_ior();
		gpuMaterials[0].textureInfo[0] = RTX_RT_INVALID_TEXTURE_INDEX;
	}

	if ( !vk_rt_ensure_buffer_capacity(
		targetBuffer,
		(VkDeviceSize)uploadBytes,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		qfalse,
		debugName ) ) {
		ri.Free( gpuMaterials );
		return qfalse;
	}

	if ( !vk_rt_upload_buffer_data( targetBuffer, gpuMaterials, uploadBytes ) ) {
		ri.Free( gpuMaterials );
		return qfalse;
	}

	ri.Free( gpuMaterials );
	if ( outMaterialCount ) {
		*outMaterialCount = uploadCount;
	}
	return qtrue;
}

static qboolean vk_rt_parse_entity_vec3( const char *text, vec3_t outVec )
{
	const char *cursor;
	int i;

	if ( !text || !outVec ) {
		return qfalse;
	}

	cursor = text;
	for ( i = 0; i < 3; i++ ) {
		char *end;
		double parsed;

		while ( *cursor && isspace( (unsigned char)*cursor ) ) {
			cursor++;
		}
		if ( !*cursor ) {
			return qfalse;
		}
		errno = 0;
		end = NULL;
		parsed = strtod( cursor, &end );
		if ( end == cursor || !end || errno == ERANGE ||
			!( parsed >= -(double)FLT_MAX && parsed <= (double)FLT_MAX ) ) {
			return qfalse;
		}
		outVec[i] = (float)parsed;
		cursor = end;
	}

	while ( *cursor && isspace( (unsigned char)*cursor ) ) {
		cursor++;
	}
	return *cursor ? qfalse : qtrue;
}

static qboolean vk_rt_parse_entity_float_value( const char *text, float *outValue )
{
	char *end;
	double parsed;
	const char *cursor;

	if ( !text || !outValue ) {
		return qfalse;
	}
	cursor = text;
	while ( *cursor && isspace( (unsigned char)*cursor ) ) {
		cursor++;
	}
	if ( !*cursor ) {
		return qfalse;
	}
	errno = 0;
	end = NULL;
	parsed = strtod( cursor, &end );
	if ( end == cursor || !end || errno == ERANGE ||
		!( parsed >= -(double)FLT_MAX && parsed <= (double)FLT_MAX ) ) {
		return qfalse;
	}
	while ( *end && isspace( (unsigned char)*end ) ) {
		end++;
	}
	if ( *end ) {
		return qfalse;
	}
	*outValue = (float)parsed;
	return qtrue;
}

static qboolean vk_rt_parse_entity_light_value( const char *text, float *outValue )
{
	float values[4];
	const char *cursor;
	int count = 0;

	if ( !text || !outValue ) {
		return qfalse;
	}

	cursor = text;
	while ( count < 4 ) {
		char *end;
		double parsed;

		while ( *cursor && isspace( (unsigned char)*cursor ) ) {
			cursor++;
		}
		if ( !*cursor ) {
			break;
		}
		errno = 0;
		end = NULL;
		parsed = strtod( cursor, &end );
		if ( end == cursor || !end || errno == ERANGE ||
			!( parsed >= -(double)FLT_MAX && parsed <= (double)FLT_MAX ) ) {
			return qfalse;
		}
		values[count++] = (float)parsed;
		cursor = end;
	}

	while ( *cursor && isspace( (unsigned char)*cursor ) ) {
		cursor++;
	}
	if ( *cursor || ( count != 1 && count != 4 ) ) {
		return qfalse;
	}

	*outValue = values[count == 4 ? 3 : 0];
	return qtrue;
}

static qboolean vk_rt_world_has_sky_surface( void )
{
	const bmodel_t *worldModel;
	int i;

	if ( !tr.world || !tr.world->bmodels ) {
		return qfalse;
	}

	worldModel = &tr.world->bmodels[0];
	if ( !worldModel->firstSurface || worldModel->numSurfaces <= 0 ) {
		return qfalse;
	}

	for ( i = 0; i < worldModel->numSurfaces; i++ ) {
		const msurface_t *surf = &worldModel->firstSurface[i];
		if ( surf->shader && surf->shader->isSky ) {
			return qtrue;
		}
	}

	return qfalse;
}

static void vk_rt_estimate_world_bounds( vec3_t center, float *radius )
{
	vec3_t mins = { -2048.0f, -2048.0f, -2048.0f };
	vec3_t maxs = { 2048.0f, 2048.0f, 2048.0f };
	vec3_t extents;

	if ( tr.world && tr.world->bmodels ) {
		const float *worldMins = tr.world->bmodels[0].bounds[0];
		const float *worldMaxs = tr.world->bmodels[0].bounds[1];

		if ( vk_rt_vec3_in_finite_range( worldMins,
				RTX_RT_MAX_LIGHT_COORDINATE ) &&
			vk_rt_vec3_in_finite_range( worldMaxs,
				RTX_RT_MAX_LIGHT_COORDINATE ) &&
			worldMins[0] <= worldMaxs[0] &&
			worldMins[1] <= worldMaxs[1] &&
			worldMins[2] <= worldMaxs[2] ) {
			VectorCopy( worldMins, mins );
			VectorCopy( worldMaxs, maxs );
		}
	}

	VectorAdd( mins, maxs, center );
	VectorScale( center, 0.5f, center );
	VectorSubtract( maxs, mins, extents );

	if ( radius ) {
		const float estimatedRadius = 0.5f * VectorLength( extents );
		*radius = vk_rt_float_in_finite_range( estimatedRadius, 0.0f,
			RTX_RT_MAX_LIGHT_RADIUS ) ?
				Com_Clamp( 1024.0f, RTX_RT_MAX_LIGHT_RADIUS,
					estimatedRadius ) : 4096.0f;
	}
}

static void vk_rt_resolve_sun_params( vec3_t outDirection, vec3_t outColor, qboolean *outFallbackFromSky )
{
	float sunLen;
	const float minElevation = 0.25f;
	const qboolean authoredSun =
		( vk_rt_vec3_in_finite_range( tr.sunLight,
			RTX_RT_MAX_LIGHT_RADIANCE ) &&
		  tr.sunLight[0] >= 0.0f && tr.sunLight[1] >= 0.0f &&
		  tr.sunLight[2] >= 0.0f &&
		  VectorLengthSquared( tr.sunLight ) > 1e-8f ) ? qtrue : qfalse;

	if ( outFallbackFromSky ) {
		*outFallbackFromSky = qfalse;
	}

	if ( vk_rt_vec3_in_finite_range( tr.sunDirection,
		RTX_RT_MAX_LIGHT_COORDINATE ) ) {
		VectorCopy( tr.sunDirection, outDirection );
		sunLen = VectorNormalize( outDirection );
	} else {
		sunLen = 0.0f;
	}
	if ( !vk_rt_float_in_finite_range( sunLen, FLT_MIN, FLT_MAX ) ||
		!vk_rt_vec3_in_finite_range( outDirection, 1.0f ) ) {
		// Match renderer default sky sun heading when map scripts do not provide q3map_sun.
		VectorSet( outDirection, 0.45f, 0.30f, 0.90f );
		VectorNormalize( outDirection );
	}
	// Keep only synthesized fallback light in a sensible upper-hemisphere band.
	// Authored low-elevation suns must retain their map-defined direction.
	if ( !authoredSun && outDirection[2] < minElevation ) {
		const float targetXYLen = sqrtf( MAX( 0.0f, 1.0f - minElevation * minElevation ) );
		const float currentXYLen = sqrtf( outDirection[0] * outDirection[0] + outDirection[1] * outDirection[1] );
		if ( currentXYLen > 1e-5f ) {
			const float scale = targetXYLen / currentXYLen;
			outDirection[0] *= scale;
			outDirection[1] *= scale;
		} else {
			outDirection[0] = 0.8321f;
			outDirection[1] = 0.4962f;
		}
		outDirection[2] = minElevation;
		VectorNormalize( outDirection );
	}

	if ( authoredSun ) {
		/*
		 * q3map_sun intensity is a compile-era light unit (retail values are
		 * commonly 25..250), not scene-linear radiance. Normalize it at the
		 * RT boundary while retaining the authored chroma and map variation.
		 */
		VectorScale( tr.sunLight, RTX_RT_LEGACY_SUN_UNIT_SCALE, outColor );
		return;
	}

	vk_rt_rebuild_world_light_cache();

	if ( s_vkRtWorldLightCache.hasSkySurface ) {
		float ambientScale = Com_Clamp( 0.75f, 3.0f, 1.20f + s_vkRtWorldLightCache.worldspawnAmbient * ( 1.0f / 56.0f ) );
		VectorScale( s_vkRtWorldLightCache.worldspawnColor, ambientScale, outColor );
		if ( outFallbackFromSky ) {
			*outFallbackFromSky = qtrue;
		}
		return;
	}

	VectorSet( outColor, 0.18f, 0.20f, 0.24f );
}

static void vk_rt_reset_world_light_cache( void )
{
	Com_Memset( &s_vkRtWorldLightCache, 0, sizeof( s_vkRtWorldLightCache ) );
	VectorSet( s_vkRtWorldLightCache.worldspawnColor, 1.0f, 1.0f, 1.0f );
	s_vkRtWorldLightCache.worldspawnAmbient = 5.0f;
}

static void vk_rt_rebuild_world_light_cache( void )
{
	const char *parse;
	const char *token;
	char keyName[MAX_TOKEN_CHARS];
	char value[MAX_TOKEN_CHARS];
	uint32_t truncated = 0;

	if ( s_vkRtWorldLightCache.world == tr.world ) {
		return;
	}

	vk_rt_reset_world_light_cache();
	s_vkRtWorldLightCache.world = tr.world;

	if ( !tr.world || !tr.world->entityString || !tr.world->entityString[0] ) {
		return;
	}

	parse = tr.world->entityString;

	while ( 1 ) {
		char className[MAX_QPATH] = { 0 };
		qboolean isLight = qfalse;
		qboolean isWorldspawn = qfalse;
		qboolean hasOrigin = qfalse;
		qboolean hasColor = qfalse;
		qboolean hasWorldColor = qfalse;
		qboolean hasWorldAmbient = qfalse;
		vec3_t origin = { 0.0f, 0.0f, 0.0f };
		vec3_t color = { 1.0f, 1.0f, 1.0f };
		vec3_t worldColor = { 1.0f, 1.0f, 1.0f };
		float worldAmbient = 5.0f;
		float lightValue = 300.0f;
		float radiusValue = 0.0f;
		float scaleValue = 1.0f;
		float maxColor;
		rtxRtGpuLight_t *dst;

		token = COM_ParseExt( &parse, qtrue );
		if ( !token[0] ) {
			break;
		}
		if ( token[0] != '{' ) {
			continue;
		}

		while ( 1 ) {
			token = COM_ParseExt( &parse, qtrue );
			if ( !token[0] ) {
				break;
			}
			if ( token[0] == '}' ) {
				break;
			}

			Q_strncpyz( keyName, token, sizeof( keyName ) );

			token = COM_ParseExt( &parse, qfalse );
			if ( !token[0] ) {
				break;
			}

			Q_strncpyz( value, token, sizeof( value ) );

			if ( !Q_stricmp( keyName, "classname" ) ) {
				Q_strncpyz( className, value, sizeof( className ) );
				isLight = ( !Q_stricmp( value, "light" ) ||
					!Q_stricmp( value, "lightJunior" ) ) ? qtrue : qfalse;
				continue;
			}

			if ( !Q_stricmp( keyName, "origin" ) ) {
				if ( vk_rt_parse_entity_vec3( value, origin ) ) {
					hasOrigin = qtrue;
				}
				continue;
			}

			if ( !Q_stricmp( keyName, "ambient" ) ) {
				if ( vk_rt_parse_entity_float_value( value, &worldAmbient ) ) {
					hasWorldAmbient = qtrue;
				}
				continue;
			}

			if ( !Q_stricmp( keyName, "_color" ) || !Q_stricmp( keyName, "color" ) ) {
				vec3_t parsedColor;
				if ( vk_rt_parse_entity_vec3( value, parsedColor ) ) {
					VectorCopy( parsedColor, color );
					hasColor = qtrue;
					if ( !Q_stricmp( keyName, "_color" ) ) {
						VectorCopy( parsedColor, worldColor );
						hasWorldColor = qtrue;
					}
				}
				continue;
			}

			if ( !Q_stricmp( keyName, "light" ) || !Q_stricmp( keyName, "_light" ) ) {
				float parsedLight;
				if ( vk_rt_parse_entity_light_value( value, &parsedLight ) ) {
					lightValue = parsedLight;
				}
				continue;
			}

			if ( !Q_stricmp( keyName, "radius" ) || !Q_stricmp( keyName, "_radius" ) ) {
				(void)vk_rt_parse_entity_float_value( value, &radiusValue );
				continue;
			}

			if ( !Q_stricmp( keyName, "scale" ) ) {
				(void)vk_rt_parse_entity_float_value( value, &scaleValue );
				continue;
			}
		}

		isWorldspawn = !Q_stricmp( className, "worldspawn" ) ? qtrue : qfalse;
		if ( isWorldspawn ) {
			float worldMaxColor = MAX( worldColor[0], MAX( worldColor[1], worldColor[2] ) );
			if ( hasWorldColor && worldMaxColor > 1.0f ) {
				VectorScale( worldColor, 1.0f / 255.0f, worldColor );
			}
			if ( hasWorldColor ) {
				s_vkRtWorldLightCache.worldspawnColor[0] = Com_Clamp( 0.0f, 4.0f, worldColor[0] );
				s_vkRtWorldLightCache.worldspawnColor[1] = Com_Clamp( 0.0f, 4.0f, worldColor[1] );
				s_vkRtWorldLightCache.worldspawnColor[2] = Com_Clamp( 0.0f, 4.0f, worldColor[2] );
			}
			if ( hasWorldAmbient ) {
				s_vkRtWorldLightCache.worldspawnAmbient = Com_Clamp( 0.0f, 1024.0f, worldAmbient );
			}
		}

		if ( !isLight || !hasOrigin ) {
			continue;
		}
		if ( !vk_rt_vec3_in_finite_range( origin,
				RTX_RT_MAX_LIGHT_COORDINATE ) ) {
			continue;
		}

		if ( s_vkRtWorldLightCache.count >= RTX_RT_MAX_WORLD_ENTITY_LIGHTS ) {
			truncated++;
			continue;
		}

	if ( !hasColor ) {
		VectorSet( color, 1.0f, 1.0f, 1.0f );
	}
		maxColor = MAX( color[0], MAX( color[1], color[2] ) );
		if ( maxColor > 1.0f ) {
			VectorScale( color, 1.0f / 255.0f, color );
		}
		color[0] = Com_Clamp( 0.0f, 4.0f, color[0] );
		color[1] = Com_Clamp( 0.0f, 4.0f, color[1] );
		color[2] = Com_Clamp( 0.0f, 4.0f, color[2] );

	lightValue = Com_Clamp( 1.0f, 8192.0f, lightValue );
	scaleValue = Com_Clamp( 0.01f, 16.0f, scaleValue );

		if ( radiusValue <= 0.0f ) {
			radiusValue = MAX( 96.0f, lightValue * 1.5f );
		}
		radiusValue = Com_Clamp( 32.0f, 4096.0f, radiusValue );

		dst = &s_vkRtWorldLightCache.lights[ s_vkRtWorldLightCache.count ];
		Com_Memset( dst, 0, sizeof( *dst ) );
		dst->positionRadius[0] = origin[0];
		dst->positionRadius[1] = origin[1];
		dst->positionRadius[2] = origin[2];
		dst->positionRadius[3] = radiusValue;
	dst->colorType[0] = color[0] * ( lightValue / 300.0f ) * scaleValue * vk_rt_world_light_scale();
	dst->colorType[1] = color[1] * ( lightValue / 300.0f ) * scaleValue * vk_rt_world_light_scale();
	dst->colorType[2] = color[2] * ( lightValue / 300.0f ) * scaleValue * vk_rt_world_light_scale();
	dst->colorType[3] = 0.0f;
		dst->directionSoftness[0] = 0.0f;
		dst->directionSoftness[1] = 0.0f;
		dst->directionSoftness[2] = 0.0f;
		dst->directionSoftness[3] = vk_rt_shadow_softness();
		dst->metadata[0] = RTX_RT_LIGHT_FLAG_CASTS_SHADOWS;
		if ( vk_rt_gpu_light_is_valid( dst ) ) {
			s_vkRtWorldLightCache.count++;
		} else {
			Com_Memset( dst, 0, sizeof( *dst ) );
		}
	}

	s_vkRtWorldLightCache.hasSkySurface = vk_rt_world_has_sky_surface();

	if ( vk_rt_world_light_scale() > 0.0f &&
		s_vkRtWorldLightCache.count == 0 && !s_vkRtWorldLightCache.warnedEmpty ) {
		ri.Printf( PRINT_WARNING,
			"RTX RT: no map light entities detected in '%s'; direct RT lighting relies on sun and dynamic lights only.\n",
			tr.world ? tr.world->baseName : "unknown" );
		s_vkRtWorldLightCache.warnedEmpty = qtrue;
	} else if ( vk_rt_world_light_scale() > 0.0f &&
		s_vkRtWorldLightCache.count > 0 ) {
		ri.Printf( PRINT_ALL,
			"RTX RT: world entity lights active: %u%s\n",
			s_vkRtWorldLightCache.count,
			truncated ? va( " (%u truncated)", truncated ) : "" );
	}
}

static uint32_t vk_rt_append_world_entity_lights( rtxRtGpuLight_t *lights, uint32_t count, uint32_t maxLights )
{
	const uint32_t available = ( maxLights > count ) ? ( maxLights - count ) : 0u;
	uint32_t remaining;
	uint32_t i;
	float shadowSoftness;

	if ( !lights || available == 0 ) {
		return count;
	}
	if ( vk_rt_world_light_scale() <= 0.0f ) {
		return count;
	}

	vk_rt_rebuild_world_light_cache();
	if ( s_vkRtWorldLightCache.count == 0 ) {
		return count;
	}

	shadowSoftness = vk_rt_shadow_softness();
	remaining = available;

	if ( s_vkRtWorldLightCache.count <= remaining ) {
		for ( i = 0; i < s_vkRtWorldLightCache.count && count < maxLights; i++ ) {
			rtxRtGpuLight_t candidate = s_vkRtWorldLightCache.lights[i];
			candidate.directionSoftness[3] = shadowSoftness;
			if ( vk_rt_gpu_light_is_valid( &candidate ) ) {
				lights[count++] = candidate;
			}
		}
		return count;
	}

	{
		qboolean selected[RTX_RT_MAX_WORLD_ENTITY_LIGHTS];
		uint32_t selectCount = 0;

		Com_Memset( selected, 0, sizeof( selected ) );

		while ( selectCount < remaining && count < maxLights ) {
			uint32_t bestIndex = ~0u;
			double bestDistSq = DBL_MAX;

			for ( i = 0; i < s_vkRtWorldLightCache.count; i++ ) {
				const rtxRtGpuLight_t *candidate = &s_vkRtWorldLightCache.lights[i];
				double dx;
				double dy;
				double dz;
				double distSq;

				if ( selected[i] ) {
					continue;
				}
				if ( !vk_rt_gpu_light_is_valid( candidate ) ) {
					selected[i] = qtrue;
					continue;
				}

				dx = candidate->positionRadius[0] - backEnd.refdef.vieworg[0];
				dy = candidate->positionRadius[1] - backEnd.refdef.vieworg[1];
				dz = candidate->positionRadius[2] - backEnd.refdef.vieworg[2];
				distSq = dx * dx + dy * dy + dz * dz;

				if ( distSq < bestDistSq ) {
					bestDistSq = distSq;
					bestIndex = i;
				}
			}

			if ( bestIndex == ~0u ) {
				break;
			}

			selected[bestIndex] = qtrue;
			lights[count] = s_vkRtWorldLightCache.lights[bestIndex];
			lights[count].directionSoftness[3] = shadowSoftness;
			if ( vk_rt_gpu_light_is_valid( &lights[count] ) ) {
				count++;
			}
			selectCount++;
		}
	}

	return count;
}

static uint32_t vk_rt_surface_light_reservation( uint32_t maxLights, uint32_t usedLights )
{
	uint32_t available;
	uint32_t reservationCap;
	uint32_t selectedCount;

	if ( vk_rt_raster_reference_enabled() ||
		backEnd.refdef.numRtSurfaceLights <= 0 ||
		usedLights >= maxLights ) {
		return 0u;
	}

	selectedCount = (uint32_t)backEnd.refdef.numRtSurfaceLights;
	available = maxLights - usedLights;
	reservationCap = maxLights / 4u;
	if ( reservationCap < 1u ) {
		reservationCap = 1u;
	} else if ( reservationCap > 2u ) {
		reservationCap = 2u;
	}

	return MIN( selectedCount, MIN( reservationCap, available ) );
}

static uint32_t vk_rt_append_surface_lights( rtxRtGpuLight_t *lights,
	uint32_t count, uint32_t maxLights )
{
	int i;

	if ( !lights || count >= maxLights || vk_rt_raster_reference_enabled() ) {
		return count;
	}

	for ( i = 0; i < backEnd.refdef.numRtSurfaceLights && count < maxLights; i++ ) {
		const surfaceLightProxy_t *proxy = &backEnd.refdef.rtSurfaceLights[i];
		rtxRtGpuLight_t candidate;
		rtxRtGpuLight_t *dst;
		vec3_t end;
		const qboolean linear =
			( proxy->projection == SURFACE_LIGHT_PROXY_LINEAR ) ?
				qtrue : qfalse;

		if ( !vk_rt_vec3_in_finite_range( proxy->origin,
				RTX_RT_MAX_LIGHT_COORDINATE ) ||
			!vk_rt_vec3_in_finite_range( proxy->color,
				RTX_RT_MAX_LIGHT_RADIANCE ) ||
			!vk_rt_float_in_finite_range( proxy->radius, FLT_MIN,
				RTX_RT_MAX_LIGHT_RADIUS ) ||
			( linear &&
			  !vk_rt_vec3_in_finite_range( proxy->normal, 1.0f ) ) ) {
			continue;
		}
		if ( linear ) {
			VectorMA( proxy->origin, proxy->radius, proxy->normal, end );
			if ( !vk_rt_vec3_in_finite_range( end,
					RTX_RT_MAX_LIGHT_COORDINATE ) ) {
				continue;
			}
		}

		Com_Memset( &candidate, 0, sizeof( candidate ) );
		candidate.positionRadius[0] = proxy->origin[0];
		candidate.positionRadius[1] = proxy->origin[1];
		candidate.positionRadius[2] = proxy->origin[2];
		candidate.positionRadius[3] = proxy->radius;
		candidate.colorType[0] = proxy->color[0];
		candidate.colorType[1] = proxy->color[1];
		candidate.colorType[2] = proxy->color[2];
		candidate.colorType[3] = linear ? 2.0f : 0.0f;
		if ( linear ) {
			candidate.directionSoftness[0] = end[0];
			candidate.directionSoftness[1] = end[1];
			candidate.directionSoftness[2] = end[2];
		}
		candidate.directionSoftness[3] = vk_rt_shadow_softness();
		candidate.metadata[0] = RTX_RT_LIGHT_FLAG_CASTS_SHADOWS;
		if ( !vk_rt_gpu_light_is_valid( &candidate ) ) {
			continue;
		}
		dst = &lights[count++];
		*dst = candidate;
	}

	return count;
}

static qboolean vk_rt_update_light_buffer( void )
{
	rtxRtGpuLight_t *lights = NULL;
	rtxVkRtBuffer_t *lightBuffer;
	uint32_t maxLights = vk_rt_max_lights();
	uint32_t count = 0;
	uint32_t reservedSurfaceLights;
	uint32_t gameplayLightLimit;
	uint32_t uploadCount;
	uint32_t bufferIndex = (uint32_t)( vk.cmd_index % NUM_COMMAND_BUFFERS );
	size_t uploadBytes;
	vec3_t sunDir;
	vec3_t sunColor;
	vec3_t sunOrigin;
	float sunRadius;
	qboolean fallbackSunFromSky = qfalse;
	qboolean rasterReference = vk_rt_raster_reference_enabled();
	qboolean traceLightShadows = vk_rt_shadow_mode() > 0 ? qtrue : qfalse;
	int i;

	lightBuffer = &vk.rt.light_buffer[bufferIndex];
	lights = (rtxRtGpuLight_t *)ri.Malloc( maxLights * sizeof( rtxRtGpuLight_t ) );
	Com_Memset( lights, 0, maxLights * sizeof( rtxRtGpuLight_t ) );

	vk_rt_resolve_sun_params( sunDir, sunColor, &fallbackSunFromSky );
	if ( VectorLengthSquared( sunColor ) > 1e-8f && count < maxLights ) {
		rtxRtGpuLight_t sun;

		Com_Memset( &sun, 0, sizeof( sun ) );
		sun.colorType[0] = sunColor[0] * vk_rt_sun_intensity();
		sun.colorType[1] = sunColor[1] * vk_rt_sun_intensity();
		sun.colorType[2] = sunColor[2] * vk_rt_sun_intensity();
		sun.colorType[3] = 1.0f;
		sun.directionSoftness[0] = sunDir[0];
		sun.directionSoftness[1] = sunDir[1];
		sun.directionSoftness[2] = sunDir[2];
		sun.directionSoftness[3] = vk_rt_shadow_softness();
		sun.metadata[0] = RTX_RT_LIGHT_FLAG_CASTS_SHADOWS;

		if ( fallbackSunFromSky ) {
			vk_rt_estimate_world_bounds( sunOrigin, &sunRadius );
			VectorMA( sunOrigin, sunRadius * 2.5f + 2048.0f, sunDir, sunOrigin );
			sun.positionRadius[0] = sunOrigin[0];
			sun.positionRadius[1] = sunOrigin[1];
			sun.positionRadius[2] = sunOrigin[2];
			sun.positionRadius[3] = MIN( sunRadius * 6.0f,
				RTX_RT_MAX_LIGHT_RADIUS );
		}
		if ( vk_rt_gpu_light_is_valid( &sun ) ) {
			lights[count++] = sun;
		}
	}

	reservedSurfaceLights = vk_rt_surface_light_reservation( maxLights, count );
	gameplayLightLimit = maxLights - reservedSurfaceLights;

	/*
	 * Compatibility composition keeps PMLIGHT authoritative for game and
	 * promoted sidecar light color. Upload its shadow-casting lights as
	 * visibility-only inputs so RT can occlude the raster-owned contribution
	 * without adding it a second time. Experimental full-RT shading uploads
	 * every light and uses the same castsRtShadows policy for direct lighting.
	 */
	for ( i = 0; i < backEnd.refdef.num_dlights && count < gameplayLightLimit; i++ ) {
		const dlight_t *dl = &backEnd.refdef.dlights[i];
		rtxRtGpuLight_t candidate;
		rtxRtGpuLight_t *dst;
		float radius = dl->radius;
		qboolean shadowOnly = rasterReference;

		if ( ( !rasterReference && backEnd.refdef.numRtSurfaceLights > 0 &&
			( dl->sourceFlags & DLIGHT_SOURCE_SURFACE ) ) ||
			!vk_rt_vec3_in_finite_range( dl->origin,
				RTX_RT_MAX_LIGHT_COORDINATE ) ||
			!vk_rt_vec3_in_finite_range( dl->color,
				RTX_RT_MAX_LIGHT_RADIANCE ) ||
			!vk_rt_float_in_finite_range( radius, FLT_MIN,
				RTX_RT_MAX_LIGHT_RADIUS ) ||
			( dl->linear && !vk_rt_vec3_in_finite_range( dl->origin2,
				RTX_RT_MAX_LIGHT_COORDINATE ) ) ||
			( shadowOnly && ( !traceLightShadows || !dl->castsRtShadows ) ) ) {
			continue;
		}

		Com_Memset( &candidate, 0, sizeof( candidate ) );
		candidate.positionRadius[0] = dl->origin[0];
		candidate.positionRadius[1] = dl->origin[1];
		candidate.positionRadius[2] = dl->origin[2];
		candidate.positionRadius[3] = radius;
		candidate.colorType[0] = dl->color[0];
		candidate.colorType[1] = dl->color[1];
		candidate.colorType[2] = dl->color[2];
		candidate.colorType[3] = dl->linear ? 2.0f : 0.0f;
		candidate.directionSoftness[0] = dl->linear ? dl->origin2[0] : 0.0f;
		candidate.directionSoftness[1] = dl->linear ? dl->origin2[1] : 0.0f;
		candidate.directionSoftness[2] = dl->linear ? dl->origin2[2] : 0.0f;
		candidate.directionSoftness[3] = vk_rt_shadow_softness();
		candidate.metadata[0] =
			( dl->castsRtShadows ? RTX_RT_LIGHT_FLAG_CASTS_SHADOWS : 0u ) |
			( shadowOnly ? RTX_RT_LIGHT_FLAG_SHADOW_ONLY : 0u );
		if ( !vk_rt_gpu_light_is_valid( &candidate ) ) {
			continue;
		}
		dst = &lights[count++];
		*dst = candidate;
	}

	count = vk_rt_append_surface_lights( lights, count, maxLights );
	count = vk_rt_append_world_entity_lights( lights, count, maxLights );

	uploadCount = MAX( count, 1u );
	uploadBytes = (size_t)uploadCount * sizeof( rtxRtGpuLight_t );

	if ( !vk_rt_ensure_buffer_capacity(
		lightBuffer,
		(VkDeviceSize)uploadBytes,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		qfalse,
		va( "rt lights frame %u", bufferIndex ) ) ) {
		ri.Free( lights );
		return qfalse;
	}

	if ( count == 0 ) {
		Com_Memset( &lights[0], 0, sizeof( lights[0] ) );
	}

	if ( !vk_rt_upload_buffer_data( lightBuffer, lights, uploadBytes ) ) {
		ri.Free( lights );
		return qfalse;
	}

	ri.Free( lights );
	vk.rt.light_count = count;
	vk.rt.stats.active_lights = count;
	return qtrue;
}

static void vk_rt_destroy_as( rtxVkRtAccelerationStructure_t *as )
{
	if ( !as ) {
		return;
	}

	if ( as->handle != VK_NULL_HANDLE ) {
		qvkDestroyAccelerationStructureKHR( vk.device, as->handle, NULL );
	}
	vk_rt_destroy_buffer( &as->storage );
	Com_Memset( as, 0, sizeof( *as ) );
}

static qboolean vk_rt_create_as( VkAccelerationStructureTypeKHR type, VkDeviceSize size, const char *debugName, rtxVkRtAccelerationStructure_t *outAs )
{
	VkAccelerationStructureCreateInfoKHR createInfo;
	VkResult result;

	Com_Memset( outAs, 0, sizeof( *outAs ) );

	if ( !vk_rt_create_buffer(
		size,
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		qtrue,
		debugName,
		&outAs->storage ) ) {
		return qfalse;
	}

	createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
	createInfo.pNext = NULL;
	createInfo.createFlags = 0;
	createInfo.buffer = outAs->storage.buffer;
	createInfo.offset = 0;
	createInfo.size = size;
	createInfo.type = type;
	createInfo.deviceAddress = 0;

	result = qvkCreateAccelerationStructureKHR( vk.device, &createInfo, NULL, &outAs->handle );
	if ( result != VK_SUCCESS ) {
		ri.Printf( PRINT_ERROR, "RTX RT: failed to create AS '%s': %s\n",
			debugName ? debugName : "unnamed", vk_result_string( result ) );
		vk_rt_destroy_buffer( &outAs->storage );
		return qfalse;
	}

	outAs->size = size;
	return qtrue;
}

static qboolean vk_rt_ensure_scratch_buffer( VkDeviceSize minSize )
{
	VkDeviceSize alignment = 256;
	VkDeviceSize alignedSize;
	VkDeviceSize requiredSize;

	if ( vk.rt.as_props.minAccelerationStructureScratchOffsetAlignment > alignment ) {
		alignment = vk.rt.as_props.minAccelerationStructureScratchOffsetAlignment;
	}

	if ( !vk_rt_align_up_checked( minSize, alignment, &alignedSize ) ||
		alignedSize > ( (VkDeviceSize)-1 ) - alignment ) {
		return qfalse;
	}
	requiredSize = alignedSize + alignment;

	if ( vk.rt.scratch_buffer.buffer != VK_NULL_HANDLE ) {
		VkDeviceSize alignedAddressValue;
		if ( vk_rt_align_up_checked( (VkDeviceSize)vk.rt.scratch_buffer.address,
			alignment, &alignedAddressValue ) ) {
			const VkDeviceSize alignedOffset = alignedAddressValue -
				(VkDeviceSize)vk.rt.scratch_buffer.address;
			if ( alignedOffset <= vk.rt.scratch_buffer.capacity &&
				alignedSize <= vk.rt.scratch_buffer.capacity - alignedOffset ) {
				return qtrue;
			}
		}
	}

	if ( vk.rt.scratch_buffer.buffer != VK_NULL_HANDLE ) {
		if ( !vk_rt_wait_for_inflight_frames( "scratch buffer resize" ) ) {
			return qfalse;
		}
	}
	vk_rt_destroy_buffer( &vk.rt.scratch_buffer );

	if ( !vk_rt_create_buffer(
		requiredSize,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		qtrue,
		"rt scratch",
		&vk.rt.scratch_buffer ) ) {
		return qfalse;
	}

	return qtrue;
}

static VkDeviceAddress vk_rt_scratch_device_address( void )
{
	VkDeviceSize alignment = 256;
	VkDeviceSize alignedAddress;

	if ( vk.rt.as_props.minAccelerationStructureScratchOffsetAlignment > alignment ) {
		alignment = vk.rt.as_props.minAccelerationStructureScratchOffsetAlignment;
	}

	if ( !vk_rt_align_up_checked( (VkDeviceSize)vk.rt.scratch_buffer.address,
		alignment, &alignedAddress ) ) {
		return 0;
	}
	return (VkDeviceAddress)alignedAddress;
}

static qboolean vk_rt_require_scratch_device_address( VkDeviceAddress *outAddress )
{
	VkDeviceAddress alignedAddress;
	VkDeviceSize alignedOffset;

	if ( !outAddress ) {
		return qfalse;
	}

	alignedAddress = vk_rt_scratch_device_address();
	if ( alignedAddress == 0 || alignedAddress < vk.rt.scratch_buffer.address ) {
		return qfalse;
	}
	alignedOffset = (VkDeviceSize)( alignedAddress - vk.rt.scratch_buffer.address );
	if ( alignedOffset >= vk.rt.scratch_buffer.capacity ) {
		return qfalse;
	}

	*outAddress = alignedAddress;
	return qtrue;
}

static qboolean vk_rt_compact_as( rtxVkRtAccelerationStructure_t *as, VkQueryPool queryPool, uint32_t queryIndex, const char *debugName, uint64_t *savedBytes )
{
	VkCopyAccelerationStructureInfoKHR copyInfo;
	rtxVkRtAccelerationStructure_t compactedAs;
	VkCommandBuffer cmd;
	VkDeviceSize compactedSize = 0;
	VkDeviceSize oldSize = 0;
	VkResult result;

	if ( !as || !as->valid || as->handle == VK_NULL_HANDLE ) {
		return qfalse;
	}

	result = qvkGetQueryPoolResults(
		vk.device,
		queryPool,
		queryIndex,
		1,
		sizeof( compactedSize ),
		&compactedSize,
		sizeof( compactedSize ),
		VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT );
	if ( result != VK_SUCCESS || compactedSize == 0 || compactedSize >= as->size ) {
		return qfalse;
	}

	if ( !vk_rt_create_as( VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, compactedSize, debugName, &compactedAs ) ) {
		return qfalse;
	}

	cmd = begin_command_buffer();

	copyInfo.sType = VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR;
	copyInfo.pNext = NULL;
	copyInfo.src = as->handle;
	copyInfo.dst = compactedAs.handle;
	copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;

	qvkCmdCopyAccelerationStructureKHR( cmd, &copyInfo );
	if ( !end_command_buffer_soft( cmd, __func__ ) ) {
		vk_rt_destroy_as( &compactedAs );
		return qfalse;
	}

	oldSize = as->size;
	vk_rt_destroy_as( as );
	*as = compactedAs;
	as->compacted = qtrue;

	{
		VkAccelerationStructureDeviceAddressInfoKHR addressInfo;
		addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
		addressInfo.pNext = NULL;
		addressInfo.accelerationStructure = as->handle;
		as->deviceAddress = qvkGetAccelerationStructureDeviceAddressKHR( vk.device, &addressInfo );
	}

	if ( savedBytes ) {
		*savedBytes += (uint64_t)( oldSize > compactedSize ? ( oldSize - compactedSize ) : 0 );
	}

	return qtrue;
}

static VkDeviceSize vk_rt_query_triangles_build_bytes( uint32_t vertexCount, uint32_t indexCount, qboolean opaqueGeometry, VkBuildAccelerationStructureFlagsKHR buildFlags )
{
	VkAccelerationStructureGeometryTrianglesDataKHR triangles;
	VkAccelerationStructureGeometryKHR geometry;
	VkAccelerationStructureBuildGeometryInfoKHR buildInfo;
	VkAccelerationStructureBuildSizesInfoKHR buildSizes;
	const uint32_t primitiveCount = indexCount / 3;

	Com_Memset( &triangles, 0, sizeof( triangles ) );
	triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	triangles.vertexData.deviceAddress = 0;
	triangles.vertexStride = sizeof( rtxRtPackedVertex_t );
	triangles.maxVertex = vertexCount ? ( vertexCount - 1 ) : 0;
	triangles.indexType = VK_INDEX_TYPE_UINT32;
	triangles.indexData.deviceAddress = 0;

	Com_Memset( &geometry, 0, sizeof( geometry ) );
	geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	geometry.flags = opaqueGeometry ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0;
	geometry.geometry.triangles = triangles;

	Com_Memset( &buildInfo, 0, sizeof( buildInfo ) );
	buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	buildInfo.flags = buildFlags;
	buildInfo.geometryCount = 1;
	buildInfo.pGeometries = &geometry;

	Com_Memset( &buildSizes, 0, sizeof( buildSizes ) );
	buildSizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

	qvkGetAccelerationStructureBuildSizesKHR(
		vk.device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&buildInfo,
		&primitiveCount,
		&buildSizes );

	return buildSizes.accelerationStructureSize + buildSizes.buildScratchSize;
}

static VkDeviceSize vk_rt_query_tlas_build_bytes( uint32_t instanceCount, VkBuildAccelerationStructureFlagsKHR buildFlags )
{
	VkAccelerationStructureGeometryInstancesDataKHR instancesData;
	VkAccelerationStructureGeometryKHR geometry;
	VkAccelerationStructureBuildGeometryInfoKHR buildInfo;
	VkAccelerationStructureBuildSizesInfoKHR buildSizes;

	Com_Memset( &instancesData, 0, sizeof( instancesData ) );
	instancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	instancesData.arrayOfPointers = VK_FALSE;
	instancesData.data.deviceAddress = 0;

	Com_Memset( &geometry, 0, sizeof( geometry ) );
	geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	geometry.geometry.instances = instancesData;

	Com_Memset( &buildInfo, 0, sizeof( buildInfo ) );
	buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	buildInfo.flags = buildFlags;
	buildInfo.geometryCount = 1;
	buildInfo.pGeometries = &geometry;

	Com_Memset( &buildSizes, 0, sizeof( buildSizes ) );
	buildSizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

	qvkGetAccelerationStructureBuildSizesKHR(
		vk.device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&buildInfo,
		&instanceCount,
		&buildSizes );

	return buildSizes.accelerationStructureSize + buildSizes.buildScratchSize;
}

static void vk_rt_cpu_geometry_init( rtxRtCpuGeometry_t *geometry )
{
	Com_Memset( geometry, 0, sizeof( *geometry ) );
}

static void vk_rt_cpu_geometry_free( rtxRtCpuGeometry_t *geometry )
{
	if ( geometry->vertices ) {
		ri.Free( geometry->vertices );
	}
	if ( geometry->indices ) {
		ri.Free( geometry->indices );
	}
	if ( geometry->materials ) {
		ri.Free( geometry->materials );
	}
	if ( geometry->materialShaders ) {
		ri.Free( geometry->materialShaders );
	}
	Com_Memset( geometry, 0, sizeof( *geometry ) );
}

static qboolean vk_rt_cpu_array_growth( uint32_t currentCapacity, uint32_t requiredCount,
	uint32_t initialCapacity, size_t elementSize, const char *label,
	uint32_t *outCapacity, size_t *outBytes )
{
	const size_t allocatorMaxCount = elementSize ? (size_t)INT_MAX / elementSize : 0;
	const uint32_t maxCount = allocatorMaxCount > (size_t)UINT32_MAX ?
		UINT32_MAX : (uint32_t)allocatorMaxCount;
	uint32_t newCapacity;

	if ( !outCapacity || !outBytes || elementSize == 0 || requiredCount > maxCount ) {
		ri.Printf( PRINT_WARNING,
			"RTX RT: refusing oversized %s geometry allocation (%u elements)\n",
			label ? label : "CPU", requiredCount );
		return qfalse;
	}

	newCapacity = currentCapacity ? currentCapacity : initialCapacity;
	if ( newCapacity > maxCount ) {
		return qfalse;
	}
	while ( newCapacity < requiredCount ) {
		if ( newCapacity > maxCount / 2u ) {
			newCapacity = maxCount;
			break;
		}
		newCapacity *= 2u;
	}
	if ( newCapacity < requiredCount ) {
		return qfalse;
	}

	*outCapacity = newCapacity;
	*outBytes = (size_t)newCapacity * elementSize;
	return qtrue;
}

static qboolean vk_rt_checked_u32_multiply( uint32_t lhs, uint32_t rhs, uint32_t *outValue )
{
	if ( !outValue || ( lhs != 0 && rhs > UINT32_MAX / lhs ) ) {
		return qfalse;
	}
	*outValue = lhs * rhs;
	return qtrue;
}

static qboolean vk_rt_cpu_geometry_reserve( rtxRtCpuGeometry_t *geometry, uint32_t additionalVertices, uint32_t additionalIndices )
{
	rtxRtPackedVertex_t *newVertices = NULL;
	uint32_t *newIndices = NULL;
	uint32_t requiredVertices;
	uint32_t requiredIndices;
	uint32_t newVertexCapacity = 0;
	uint32_t newIndexCapacity = 0;
	size_t newVertexBytes = 0;
	size_t newIndexBytes = 0;

	if ( !geometry || additionalVertices > UINT32_MAX - geometry->numVertices ||
		additionalIndices > UINT32_MAX - geometry->numIndices ) {
		ri.Printf( PRINT_WARNING, "RTX RT: refusing overflowing CPU geometry counts\n" );
		return qfalse;
	}

	requiredVertices = geometry->numVertices + additionalVertices;
	requiredIndices = geometry->numIndices + additionalIndices;

	if ( requiredVertices > geometry->vertexCapacity &&
		!vk_rt_cpu_array_growth( geometry->vertexCapacity, requiredVertices, 1024u,
			sizeof( rtxRtPackedVertex_t ), "vertex", &newVertexCapacity, &newVertexBytes ) ) {
		return qfalse;
	}
	if ( requiredIndices > geometry->indexCapacity &&
		!vk_rt_cpu_array_growth( geometry->indexCapacity, requiredIndices, 2048u,
			sizeof( uint32_t ), "index", &newIndexCapacity, &newIndexBytes ) ) {
		return qfalse;
	}

	if ( newVertexBytes > 0 ) {
		newVertices = (rtxRtPackedVertex_t *)ri.Malloc( (int)newVertexBytes );
		if ( !newVertices ) {
			return qfalse;
		}
	}
	if ( newIndexBytes > 0 ) {
		newIndices = (uint32_t *)ri.Malloc( (int)newIndexBytes );
		if ( !newIndices ) {
			if ( newVertices ) {
				ri.Free( newVertices );
			}
			return qfalse;
		}
	}

	if ( newVertices ) {
		if ( geometry->vertices && geometry->numVertices > 0 ) {
			Com_Memcpy( newVertices, geometry->vertices,
				(size_t)geometry->numVertices * sizeof( rtxRtPackedVertex_t ) );
		}
		if ( geometry->vertices ) {
			ri.Free( geometry->vertices );
		}
		geometry->vertices = newVertices;
		geometry->vertexCapacity = newVertexCapacity;
	}
	if ( newIndices ) {
		if ( geometry->indices && geometry->numIndices > 0 ) {
			Com_Memcpy( newIndices, geometry->indices,
				(size_t)geometry->numIndices * sizeof( uint32_t ) );
		}
		if ( geometry->indices ) {
			ri.Free( geometry->indices );
		}
		geometry->indices = newIndices;
		geometry->indexCapacity = newIndexCapacity;
	}

	return qtrue;
}

static qboolean vk_rt_cpu_geometry_reserve_materials( rtxRtCpuGeometry_t *geometry, uint32_t additionalMaterials )
{
	rtxRtMaterial_t *newMaterials;
	const shader_t **newMaterialShaders;
	uint32_t requiredMaterials;
	uint32_t newCapacity;
	size_t allocationBytes;
	size_t materialBytes;
	size_t shaderBytes;
	const size_t maxElementSize = sizeof( rtxRtMaterial_t ) > sizeof( shader_t * ) ?
		sizeof( rtxRtMaterial_t ) : sizeof( shader_t * );

	if ( !geometry || additionalMaterials > UINT32_MAX - geometry->numMaterials ) {
		ri.Printf( PRINT_WARNING, "RTX RT: refusing overflowing material count\n" );
		return qfalse;
	}

	requiredMaterials = geometry->numMaterials + additionalMaterials;
	if ( requiredMaterials <= geometry->materialCapacity ) {
		return qtrue;
	}
	if ( !vk_rt_cpu_array_growth( geometry->materialCapacity, requiredMaterials, 128u,
		maxElementSize, "material", &newCapacity, &allocationBytes ) ) {
		return qfalse;
	}

	materialBytes = (size_t)newCapacity * sizeof( rtxRtMaterial_t );
	shaderBytes = (size_t)newCapacity * sizeof( shader_t * );
	newMaterials = (rtxRtMaterial_t *)ri.Malloc( (int)materialBytes );
	newMaterialShaders = (const shader_t **)ri.Malloc( (int)shaderBytes );
	if ( !newMaterials || !newMaterialShaders ) {
		if ( newMaterials ) {
			ri.Free( newMaterials );
		}
		if ( newMaterialShaders ) {
			ri.Free( newMaterialShaders );
		}
		return qfalse;
	}

	if ( geometry->materials && geometry->numMaterials > 0 ) {
		Com_Memcpy( newMaterials, geometry->materials,
			(size_t)geometry->numMaterials * sizeof( rtxRtMaterial_t ) );
		Com_Memcpy( newMaterialShaders, geometry->materialShaders,
			(size_t)geometry->numMaterials * sizeof( shader_t * ) );
	}
	if ( geometry->materials ) {
		ri.Free( geometry->materials );
	}
	if ( geometry->materialShaders ) {
		ri.Free( geometry->materialShaders );
	}

	geometry->materials = newMaterials;
	geometry->materialShaders = newMaterialShaders;
	geometry->materialCapacity = newCapacity;
	return qtrue;
}

static uint32_t vk_rt_color_u32_from_bytes( const byte *rgba )
{
	color4ub_t packed;
	packed.rgba[0] = rgba ? rgba[0] : 255;
	packed.rgba[1] = rgba ? rgba[1] : 255;
	packed.rgba[2] = rgba ? rgba[2] : 255;
	packed.rgba[3] = rgba ? rgba[3] : 255;
	return packed.u32;
}

static qboolean vk_rt_material_name_contains_token( const char *name, const char *token )
{
	char loweredName[MAX_QPATH];
	char loweredToken[64];

	if ( !name || !token || !token[0] ) {
		return qfalse;
	}

	Q_strncpyz( loweredName, name, sizeof( loweredName ) );
	Q_strncpyz( loweredToken, token, sizeof( loweredToken ) );
	Q_strlwr( loweredName );
	Q_strlwr( loweredToken );

	return strstr( loweredName, loweredToken ) ? qtrue : qfalse;
}

static float vk_rt_material_alpha_cutoff_from_state_bits( unsigned stateBits )
{
	switch ( stateBits & GLS_ATEST_BITS ) {
	case GLS_ATEST_GT_0:
		return 1.0f / 255.0f;
	case GLS_ATEST_GE_80:
		return 0.5f;
	case GLS_ATEST_LT_80:
		return 0.5f;
	default:
		return 0.0f;
	}
}

static rtxRtColorSpace_t vk_rt_material_albedo_color_space( const image_t *image )
{
	const char *name;

	if ( !image || !image->imgName ) {
		return RTX_RT_COLORSPACE_SRGB;
	}

	/*
	 * An sRGB image view performs the authored-color decode before the ray
	 * shader samples it. Material metadata describes the value returned by
	 * the descriptor, not the source asset, so mark that sampled value linear
	 * and keep closest-hit from applying the software decode a second time.
	 */
	if ( image->srgbDecode ) {
		return RTX_RT_COLORSPACE_LINEAR;
	}

	if ( image->colorSpace == IMAGE_COLORSPACE_LINEAR ||
		image->colorSpace == IMAGE_COLORSPACE_DATA ) {
		return RTX_RT_COLORSPACE_LINEAR;
	}

	name = image->imgName;
	if ( vk_rt_material_name_contains_token( name, ".hdr" ) || vk_rt_material_name_contains_token( name, ".exr" ) ) {
		return RTX_RT_COLORSPACE_HDR;
	}

	return RTX_RT_COLORSPACE_SRGB;
}

static rtxRtColorSpace_t vk_rt_material_data_color_space( const image_t *image )
{
	if ( !image || !image->imgName ) {
		return RTX_RT_COLORSPACE_LINEAR;
	}

	if ( vk_rt_material_name_contains_token( image->imgName, ".hdr" ) || vk_rt_material_name_contains_token( image->imgName, ".exr" ) ) {
		return RTX_RT_COLORSPACE_HDR;
	}

	return RTX_RT_COLORSPACE_LINEAR;
}

static int32_t vk_rt_image_to_scene_texture_index( const image_t *image )
{
	int i;

	if ( !image ) {
		return -1;
	}

	for ( i = 0; i < tr.numImages; i++ ) {
		if ( tr.images[i] == image ) {
			return i;
		}
	}

	return -1;
}

static void vk_rt_apply_material_overrides( rtxRtMaterial_t *material )
{
	if ( !material ) {
		return;
	}

	if ( rtx_rt_material_override ) {
		switch ( rtx_rt_material_override->integer ) {
		case 1: // clay
			material->metallic = 0.0f;
			material->roughness = 1.0f;
			material->emissiveScale = 0.0f;
			VectorClear( material->emissiveColor );
			break;
		case 2: // chrome
			material->metallic = 1.0f;
			material->roughness = 0.05f;
			break;
		case 3: // emissive debug
			material->flags |= RTX_RT_MATFLAG_EMISSIVE;
			material->emissiveScale = 10.0f;
			material->emissiveColor[0] = 1.0f;
			material->emissiveColor[1] = 1.0f;
			material->emissiveColor[2] = 1.0f;
			break;
		default:
			break;
		}
	}

	if ( rtx_rt_material_roughness_override && rtx_rt_material_roughness_override->value >= 0.0f ) {
		material->roughness = Com_Clamp( 0.0f, 1.0f, rtx_rt_material_roughness_override->value );
	}
	if ( rtx_rt_material_metallic_override && rtx_rt_material_metallic_override->value >= 0.0f ) {
		material->metallic = Com_Clamp( 0.0f, 1.0f, rtx_rt_material_metallic_override->value );
	}
	if ( rtx_rt_material_emissive_override && rtx_rt_material_emissive_override->value >= 0.0f ) {
		material->flags |= RTX_RT_MATFLAG_EMISSIVE;
		material->emissiveScale = MAX( 0.0f, rtx_rt_material_emissive_override->value );
	}
	if ( rtx_rt_masked_mode && rtx_rt_masked_mode->integer == 0 ) {
		material->flags &= ~( RTX_RT_MATFLAG_MASKED | RTX_RT_MATFLAG_ALPHA_LT );
		material->alphaCutoff = 0.0f;
	}
	if ( rtx_rt_masked_cutoff && rtx_rt_masked_cutoff->value >= 0.0f ) {
		material->alphaCutoff = Com_Clamp( 0.0f, 1.0f, rtx_rt_masked_cutoff->value );
		if ( material->alphaCutoff > 0.0f ) {
			material->flags |= RTX_RT_MATFLAG_MASKED;
		}
	}
}

static void vk_rt_translate_shader_to_material( const shader_t *shader, rtxRtMaterial_t *material )
{
	int i;
	image_t *albedoImage = NULL;
	image_t *fallbackImage = NULL;
	float emissiveKeywordBoost = 1.0f;
	const qboolean useHeuristics =
		( rtx_rt_material_heuristics && rtx_rt_material_heuristics->integer ) ? qtrue : qfalse;

	if ( shader && shader->remappedShader ) {
		shader = shader->remappedShader;
	}

	Com_Memset( material, 0, sizeof( *material ) );
	Q_strncpyz( material->name, shader ? shader->name : "<null>", sizeof( material->name ) );
	material->baseColor[0] = 1.0f;
	material->baseColor[1] = 1.0f;
	material->baseColor[2] = 1.0f;
	material->baseColor[3] = 1.0f;
	material->roughness = 0.85f;
	material->metallic = 0.0f;
	material->alphaCutoff = 0.0f;
	material->emissiveColor[0] = 0.0f;
	material->emissiveColor[1] = 0.0f;
	material->emissiveColor[2] = 0.0f;
	material->emissiveScale = ( rtx_rt_emissive_scale ? MAX( 0.0f, rtx_rt_emissive_scale->value ) : 1.0f );
	material->albedoColorSpace = RTX_RT_COLORSPACE_SRGB;
	material->normalColorSpace = RTX_RT_COLORSPACE_LINEAR;
	material->emissiveColorSpace = RTX_RT_COLORSPACE_SRGB;
	material->albedoTextureIndex = -1;

	if ( useHeuristics && rtx_rt_emissive_keyword_boost ) {
		emissiveKeywordBoost = MAX( 0.0f, rtx_rt_emissive_keyword_boost->value );
	}

	if ( !shader ) {
		vk_rt_apply_material_overrides( material );
		return;
	}

	if ( shader->cullType == CT_TWO_SIDED ) {
		material->flags |= RTX_RT_MATFLAG_TWO_SIDED;
	} else if ( shader->cullType == CT_BACK_SIDED ) {
		material->flags |= RTX_RT_MATFLAG_CULL_FRONT;
	} else {
		material->flags |= RTX_RT_MATFLAG_CULL_BACK;
	}
	if ( shader->sort > SS_OPAQUE ) {
		material->flags |= RTX_RT_MATFLAG_TRANSLUCENT;
	}
	if ( shader->sort == SS_DECAL ) {
		material->flags |= RTX_RT_MATFLAG_DECAL;
	}
	if ( shader->isSky || ( shader->surfaceFlags & SURF_SKY ) ) {
		material->flags |= RTX_RT_MATFLAG_SKY | RTX_RT_MATFLAG_EMISSIVE;
		material->emissiveColor[0] = 1.0f;
		material->emissiveColor[1] = 1.0f;
		material->emissiveColor[2] = 1.0f;
		material->emissiveScale = MAX( material->emissiveScale, 2.0f );
	}
	if ( useHeuristics ) {
		if ( shader->surfaceFlags & SURF_METALSTEPS ) {
			material->metallic = MAX( material->metallic, 0.85f );
			material->roughness = MIN( material->roughness, 0.35f );
		}
		if ( shader->surfaceFlags & SURF_SLICK ) {
			material->roughness = MIN( material->roughness, 0.2f );
		}

		if ( vk_rt_material_name_contains_token( shader->name, "metal" ) ) {
			material->metallic = MAX( material->metallic, 0.75f );
			material->roughness = MIN( material->roughness, 0.4f );
		}
		if ( vk_rt_material_name_contains_token( shader->name, "glass" ) ) {
			material->flags |= RTX_RT_MATFLAG_TRANSLUCENT;
			material->roughness = MIN( material->roughness, 0.08f );
		}
		if ( vk_rt_material_name_contains_token( shader->name, "decal" ) ||
			vk_rt_material_name_contains_token( shader->name, "mark" ) ||
			vk_rt_material_name_contains_token( shader->name, "bullet" ) ||
			vk_rt_material_name_contains_token( shader->name, "scorch" ) ) {
			material->flags |= RTX_RT_MATFLAG_DECAL;
		}
		if ( vk_rt_material_name_contains_token( shader->name, "particle" ) ||
			vk_rt_material_name_contains_token( shader->name, "smoke" ) ||
			vk_rt_material_name_contains_token( shader->name, "spark" ) ||
			vk_rt_material_name_contains_token( shader->name, "muzzle" ) ||
			vk_rt_material_name_contains_token( shader->name, "trail" ) ||
			vk_rt_material_name_contains_token( shader->name, "flare" ) ||
			vk_rt_material_name_contains_token( shader->name, "rail" ) ||
			vk_rt_material_name_contains_token( shader->name, "plasma" ) ) {
			material->flags |= RTX_RT_MATFLAG_PARTICLE | RTX_RT_MATFLAG_EFFECT | RTX_RT_MATFLAG_TRANSLUCENT;
		}
		if ( vk_rt_material_name_contains_token( shader->name, "grate" ) ||
			vk_rt_material_name_contains_token( shader->name, "fence" ) ||
			vk_rt_material_name_contains_token( shader->name, "foliage" ) ) {
			material->flags |= RTX_RT_MATFLAG_MASKED;
			material->alphaCutoff = MAX( material->alphaCutoff, 0.5f );
		}
	}

	for ( i = 0; i < shader->numUnfoggedPasses; i++ ) {
		const shaderStage_t *stage = shader->stages[i];
		uint32_t bundleCount;
		uint32_t bundleIndex;
		unsigned blendBits;
		float stageCutoff;

		if ( !stage || !stage->active ) {
			continue;
		}

		material->sourceStageCount++;

		stageCutoff = vk_rt_material_alpha_cutoff_from_state_bits( stage->stateBits );
		if ( stageCutoff > 0.0f ) {
			material->flags |= RTX_RT_MATFLAG_MASKED;
			material->alphaCutoff = MAX( material->alphaCutoff, stageCutoff );
			if ( ( stage->stateBits & GLS_ATEST_BITS ) == GLS_ATEST_LT_80 ) {
				material->flags |= RTX_RT_MATFLAG_ALPHA_LT;
			}
		}

		blendBits = stage->stateBits & GLS_BLEND_BITS;
		if ( blendBits == ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE ) ) {
			material->flags |= RTX_RT_MATFLAG_EMISSIVE;
			material->emissiveScale = MAX( material->emissiveScale, 1.5f );
		}

		bundleCount = stage->numTexBundles;
		if ( bundleCount == 0 ) {
			bundleCount = 1;
		}
		bundleCount = MIN( bundleCount, (uint32_t)NUM_TEXTURE_BUNDLES );

		for ( bundleIndex = 0; bundleIndex < bundleCount; bundleIndex++ ) {
			const textureBundle_t *bundle = &stage->bundle[bundleIndex];
			const image_t *image = bundle->image[0];
			const qboolean lightmap =
				( bundle->tcGen == TCGEN_LIGHTMAP ||
					bundle->lightmap != LIGHTMAP_INDEX_NONE ||
					( image && ( image->flags & IMGFLAG_LIGHTMAP ) ) ) ?
						qtrue : qfalse;

			if ( !fallbackImage && image ) {
				fallbackImage = (image_t *)image;
			}
			if ( !albedoImage && image && !lightmap ) {
				albedoImage = (image_t *)image;
			}

			if ( !lightmap && bundle->rgbGen == CGEN_CONST ) {
				material->baseColor[0] = bundle->constantColor.rgba[0] / 255.0f;
				material->baseColor[1] = bundle->constantColor.rgba[1] / 255.0f;
				material->baseColor[2] = bundle->constantColor.rgba[2] / 255.0f;
				material->baseColor[3] = bundle->constantColor.rgba[3] / 255.0f;
			}

			if ( useHeuristics && !lightmap && image && image->imgName ) {
				if ( vk_rt_material_name_contains_token( image->imgName, "_n" ) ||
					vk_rt_material_name_contains_token( image->imgName, "normal" ) ) {
					material->normalColorSpace = vk_rt_material_data_color_space( image );
					material->roughness = MIN( material->roughness, 0.75f );
				}

				if ( vk_rt_material_name_contains_token( image->imgName, "metal" ) ||
					vk_rt_material_name_contains_token( image->imgName, "_m" ) ) {
					material->metallic = MAX( material->metallic, 0.9f );
				}

				if ( vk_rt_material_name_contains_token( image->imgName, "rough" ) ||
					vk_rt_material_name_contains_token( image->imgName, "gloss" ) ) {
					material->roughness = MIN( material->roughness, 0.6f );
				}

				if ( vk_rt_material_name_contains_token( image->imgName, "glow" ) ||
					vk_rt_material_name_contains_token( image->imgName, "emissive" ) ||
					vk_rt_material_name_contains_token( image->imgName, "light" ) ) {
					material->flags |= RTX_RT_MATFLAG_EMISSIVE;
					material->emissiveColor[0] = MAX( material->emissiveColor[0], 1.0f );
					material->emissiveColor[1] = MAX( material->emissiveColor[1], 1.0f );
					material->emissiveColor[2] = MAX( material->emissiveColor[2], 1.0f );
					material->emissiveColorSpace = vk_rt_material_albedo_color_space( image );
					material->emissiveScale = MAX( material->emissiveScale, emissiveKeywordBoost );
				}

				if ( vk_rt_material_name_contains_token( image->imgName, "decal" ) ||
					vk_rt_material_name_contains_token( image->imgName, "mark" ) ||
					vk_rt_material_name_contains_token( image->imgName, "scorch" ) ) {
					material->flags |= RTX_RT_MATFLAG_DECAL;
				}

				if ( vk_rt_material_name_contains_token( image->imgName, "particle" ) ||
					vk_rt_material_name_contains_token( image->imgName, "smoke" ) ||
					vk_rt_material_name_contains_token( image->imgName, "spark" ) ||
					vk_rt_material_name_contains_token( image->imgName, "muzzle" ) ||
					vk_rt_material_name_contains_token( image->imgName, "trail" ) ||
					vk_rt_material_name_contains_token( image->imgName, "flare" ) ||
					vk_rt_material_name_contains_token( image->imgName, "rail" ) ) {
					material->flags |= RTX_RT_MATFLAG_PARTICLE | RTX_RT_MATFLAG_EFFECT | RTX_RT_MATFLAG_TRANSLUCENT;
				}
			}
		}
	}

	if ( material->flags & RTX_RT_MATFLAG_DECAL ) {
		material->roughness = MIN( material->roughness, 0.55f );
	}
	if ( material->flags & RTX_RT_MATFLAG_PARTICLE ) {
		material->roughness = MAX( material->roughness, 0.7f );
	}

	if ( !albedoImage ) {
		albedoImage = fallbackImage;
	}

	if ( albedoImage ) {
		material->albedoColorSpace = vk_rt_material_albedo_color_space( albedoImage );
		material->emissiveColorSpace = vk_rt_material_albedo_color_space( albedoImage );
		material->albedoTextureIndex = vk_rt_image_to_scene_texture_index( albedoImage );
	} else {
		material->albedoColorSpace = RTX_RT_COLORSPACE_SRGB;
		material->emissiveColorSpace = RTX_RT_COLORSPACE_SRGB;
		material->albedoTextureIndex = -1;
	}

	if ( material->flags & RTX_RT_MATFLAG_MASKED ) {
		material->alphaCutoff = Com_Clamp( 0.0f, 1.0f, material->alphaCutoff );
	}

	vk_rt_apply_material_overrides( material );
}

static qboolean vk_rt_cpu_geometry_find_or_add_material_ex( rtxRtCpuGeometry_t *geometry, const shader_t *shader, uint32_t extraFlags, uint32_t *outMaterialIndex )
{
	uint32_t i;
	rtxRtMaterial_t material;
	const uint32_t ownershipFlags = RTX_RT_MATFLAG_RASTER_OVERLAY;

	for ( i = 0; i < geometry->numMaterials; i++ ) {
		if ( geometry->materialShaders[i] == shader &&
			( geometry->materials[i].flags & ownershipFlags ) ==
				( extraFlags & ownershipFlags ) ) {
			if ( outMaterialIndex ) {
				*outMaterialIndex = i;
			}
			return qtrue;
		}
	}

	if ( !vk_rt_cpu_geometry_reserve_materials( geometry, 1 ) ) {
		return qfalse;
	}

	vk_rt_translate_shader_to_material( shader, &material );
	material.flags |= extraFlags;
	geometry->materials[ geometry->numMaterials ] = material;
	geometry->materialShaders[ geometry->numMaterials ] = shader;

	if ( outMaterialIndex ) {
		*outMaterialIndex = geometry->numMaterials;
	}

	geometry->numMaterials++;
	return qtrue;
}

static qboolean vk_rt_consume_cvar_modified( cvar_t *cvar )
{
	if ( !cvar || !cvar->modified ) {
		return qfalse;
	}

	cvar->modified = qfalse;
	return qtrue;
}

static void vk_rt_handle_cached_resource_cvar_changes( void )
{
	qboolean materialChanged = qfalse;
	qboolean lightChanged;

	lightChanged = vk_rt_consume_cvar_modified( rtx_rt_world_light_scale );
	materialChanged |= vk_rt_consume_cvar_modified( rtx_rt_masked_mode );
	materialChanged |= vk_rt_consume_cvar_modified( rtx_rt_masked_cutoff );
	materialChanged |= vk_rt_consume_cvar_modified( rtx_rt_material_heuristics );
	materialChanged |= vk_rt_consume_cvar_modified( rtx_rt_material_override );
	materialChanged |= vk_rt_consume_cvar_modified( rtx_rt_material_roughness_override );
	materialChanged |= vk_rt_consume_cvar_modified( rtx_rt_material_metallic_override );
	materialChanged |= vk_rt_consume_cvar_modified( rtx_rt_material_emissive_override );
	materialChanged |= vk_rt_consume_cvar_modified( rtx_rt_emissive_scale );
	materialChanged |= vk_rt_consume_cvar_modified( rtx_rt_emissive_keyword_boost );
	materialChanged |= vk_rt_consume_cvar_modified( rtx_rt_effect_radius_scale );
	materialChanged |= vk_rt_consume_cvar_modified( rtx_rt_effect_emissive_boost );
	materialChanged |= vk_rt_consume_cvar_modified( rtx_rt_marks_enable );
	materialChanged |= vk_rt_consume_cvar_modified( rtx_rt_dynamic_effects );

	if ( lightChanged ) {
		vk_rt_reset_world_light_cache();
		vk_rt_reset_temporal_history( "world light scale changed" );
	}

	if ( !materialChanged ) {
		return;
	}

	if ( vk.rt.worldGeometryReady ||
		vk.rt.world_blas.valid ||
		vk.rt.dynamic_blas.valid ||
		vk.rt.world_material_buffer.buffer != VK_NULL_HANDLE ||
		vk.rt.dynamic_material_buffer.buffer != VK_NULL_HANDLE ) {
		vk_rt_invalidate( "RT material/geometry configuration changed" );
	} else {
		vk_rt_reset_temporal_history( "RT material/geometry configuration changed" );
	}
}

static qboolean vk_rt_cpu_geometry_find_or_add_material( rtxRtCpuGeometry_t *geometry, const shader_t *shader, uint32_t *outMaterialIndex )
{
	return vk_rt_cpu_geometry_find_or_add_material_ex(
		geometry,
		shader,
		RTX_RT_MATFLAG_NONE,
		outMaterialIndex );
}

static qboolean vk_rt_cpu_geometry_add_vertex( rtxRtCpuGeometry_t *geometry, const vec3_t xyz, const vec3_t normal, const vec4_t tangent, const vec2_t texCoord, uint32_t color, uint32_t materialIndex, uint32_t *outIndex )
{
	rtxRtPackedVertex_t *vertex;

	if ( !vk_rt_cpu_geometry_reserve( geometry, 1, 0 ) ) {
		return qfalse;
	}

	vertex = &geometry->vertices[ geometry->numVertices ];
	vertex->xyz[0] = xyz[0];
	vertex->xyz[1] = xyz[1];
	vertex->xyz[2] = xyz[2];
	vertex->xyz[3] = 1.0f;
	vertex->normal[0] = normal[0];
	vertex->normal[1] = normal[1];
	vertex->normal[2] = normal[2];
	vertex->normal[3] = 0.0f;
	vertex->tangent[0] = tangent[0];
	vertex->tangent[1] = tangent[1];
	vertex->tangent[2] = tangent[2];
	vertex->tangent[3] = tangent[3];
	vertex->texCoord[0] = texCoord[0];
	vertex->texCoord[1] = texCoord[1];
	vertex->color = color;
	vertex->materialIndex = materialIndex;

	if ( outIndex ) {
		*outIndex = geometry->numVertices;
	}

	geometry->numVertices++;
	return qtrue;
}

static qboolean vk_rt_cpu_geometry_add_index( rtxRtCpuGeometry_t *geometry, uint32_t index )
{
	if ( !vk_rt_cpu_geometry_reserve( geometry, 0, 1 ) ) {
		return qfalse;
	}

	geometry->indices[ geometry->numIndices ] = index;
	geometry->numIndices++;
	return qtrue;
}

static qboolean vk_rt_cpu_geometry_add_triangle( rtxRtCpuGeometry_t *geometry,
	uint32_t i0, uint32_t i1, uint32_t i2 )
{
	uint32_t firstIndex;

	if ( !vk_rt_cpu_geometry_reserve( geometry, 0, 3 ) ) {
		return qfalse;
	}

	firstIndex = geometry->numIndices;
	geometry->indices[firstIndex + 0] = i0;
	geometry->indices[firstIndex + 1] = i1;
	geometry->indices[firstIndex + 2] = i2;
	geometry->numIndices += 3;
	return qtrue;
}

static qboolean vk_rt_cpu_geometry_sanitize_indices( rtxRtCpuGeometry_t *geometry, uint32_t *outDroppedTriangles )
{
	uint32_t write = 0;
	uint32_t droppedTriangles = 0;
	uint32_t i;

	if ( outDroppedTriangles ) {
		*outDroppedTriangles = 0;
	}

	if ( !geometry || !geometry->indices || geometry->numVertices == 0 || geometry->numIndices == 0 ) {
		return qtrue;
	}

	for ( i = 0; i + 2 < geometry->numIndices; i += 3 ) {
		const uint32_t i0 = geometry->indices[i + 0];
		const uint32_t i1 = geometry->indices[i + 1];
		const uint32_t i2 = geometry->indices[i + 2];

		if ( i0 >= geometry->numVertices || i1 >= geometry->numVertices || i2 >= geometry->numVertices ||
			i0 == i1 || i1 == i2 || i0 == i2 ) {
			droppedTriangles++;
			continue;
		}

		geometry->indices[write + 0] = i0;
		geometry->indices[write + 1] = i1;
		geometry->indices[write + 2] = i2;
		write += 3;
	}

	if ( i < geometry->numIndices ) {
		droppedTriangles++;
	}

	geometry->numIndices = write;
	if ( outDroppedTriangles ) {
		*outDroppedTriangles = droppedTriangles;
	}

	return qtrue;
}

static qboolean vk_rt_append_surface_face_geometry( rtxRtCpuGeometry_t *geometry, const srfSurfaceFace_t *surface, const shader_t *shader, uint32_t materialFlags )
{
	const int *indices;
	vec4_t tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
	uint32_t baseVertex;
	uint32_t materialIndex = 0;
	uint32_t validTriangleCount = 0;
	int completeIndexCount;
	int i;

	if ( !surface || surface->numPoints < 3 || surface->numIndices < 3 ) {
		return qtrue;
	}

	indices = (const int *)( (const byte *)surface + surface->ofsIndices );
	completeIndexCount = surface->numIndices - surface->numIndices % 3;
	for ( i = 0; i < completeIndexCount; i += 3 ) {
		if ( indices[i + 0] < 0 || indices[i + 0] >= surface->numPoints ||
			indices[i + 1] < 0 || indices[i + 1] >= surface->numPoints ||
			indices[i + 2] < 0 || indices[i + 2] >= surface->numPoints ) {
			continue;
		}
		validTriangleCount++;
	}

	if ( !vk_rt_cpu_geometry_find_or_add_material_ex( geometry, shader, materialFlags, &materialIndex ) ) {
		return qfalse;
	}

	if ( !vk_rt_cpu_geometry_reserve( geometry, (uint32_t)surface->numPoints, validTriangleCount * 3u ) ) {
		return qfalse;
	}

	baseVertex = geometry->numVertices;
	for ( i = 0; i < surface->numPoints; i++ ) {
		const float *p = surface->points[i];
		const vec3_t xyz = { p[0], p[1], p[2] };
		vec3_t normal;
		vec2_t texCoord;
		floatint_t packedColor;

		if ( surface->normals ) {
			VectorCopy( &surface->normals[i * 3], normal );
		} else {
			VectorCopy( surface->plane.normal, normal );
		}

		texCoord[0] = p[3];
		texCoord[1] = p[4];
		packedColor.f = p[7];

		if ( !vk_rt_cpu_geometry_add_vertex( geometry, xyz, normal, tangent, texCoord, packedColor.u, materialIndex, NULL ) ) {
			return qfalse;
		}
	}

	for ( i = 0; i < completeIndexCount; i += 3 ) {
		const int i0 = indices[i + 0];
		const int i1 = indices[i + 1];
		const int i2 = indices[i + 2];

		if ( i0 < 0 || i0 >= surface->numPoints ||
			i1 < 0 || i1 >= surface->numPoints ||
			i2 < 0 || i2 >= surface->numPoints ) {
			continue;
		}
		if ( !vk_rt_cpu_geometry_add_triangle( geometry,
			baseVertex + (uint32_t)i0,
			baseVertex + (uint32_t)i1,
			baseVertex + (uint32_t)i2 ) ) {
			return qfalse;
		}
	}

	geometry->sourceTriangleCount += validTriangleCount;
	if ( geometry->materials[ materialIndex ].flags & RTX_RT_MATFLAG_MASKED ) {
		geometry->maskedTriangleCount += validTriangleCount;
	}
	if ( geometry->materials[ materialIndex ].flags &
		( RTX_RT_MATFLAG_MASKED |
			RTX_RT_MATFLAG_CULL_FRONT |
			RTX_RT_MATFLAG_CULL_BACK |
			RTX_RT_MATFLAG_RASTER_OVERLAY ) ) {
		geometry->anyHitTriangleCount += validTriangleCount;
	}

	return qtrue;
}

static qboolean vk_rt_append_surface_grid_geometry( rtxRtCpuGeometry_t *geometry, const srfGridMesh_t *surface, const shader_t *shader, uint32_t materialFlags )
{
	int row;
	int col;
	vec4_t tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
	uint32_t baseVertex;
	uint32_t materialIndex = 0;
	uint32_t vertexCount;
	uint32_t cellCount;
	uint32_t indexCount;
	uint32_t triCount;

	if ( !surface || surface->width < 2 || surface->height < 2 ) {
		return qtrue;
	}
	if ( !vk_rt_checked_u32_multiply( (uint32_t)surface->width,
		(uint32_t)surface->height, &vertexCount ) ||
		!vk_rt_checked_u32_multiply( (uint32_t)( surface->width - 1 ),
			(uint32_t)( surface->height - 1 ), &cellCount ) ||
		!vk_rt_checked_u32_multiply( cellCount, 6u, &indexCount ) ||
		!vk_rt_checked_u32_multiply( cellCount, 2u, &triCount ) ) {
		ri.Printf( PRINT_WARNING, "RTX RT: refusing overflowing grid geometry\n" );
		return qfalse;
	}

	if ( !vk_rt_cpu_geometry_find_or_add_material_ex( geometry, shader, materialFlags, &materialIndex ) ) {
		return qfalse;
	}

	if ( !vk_rt_cpu_geometry_reserve(
		geometry,
		vertexCount,
		indexCount ) ) {
		return qfalse;
	}

	baseVertex = geometry->numVertices;
	for ( row = 0; row < surface->height; row++ ) {
		for ( col = 0; col < surface->width; col++ ) {
			const drawVert_t *vertex = &surface->verts[ row * surface->width + col ];
			if ( !vk_rt_cpu_geometry_add_vertex( geometry, vertex->xyz, vertex->normal, tangent, vertex->st, vertex->color.u32, materialIndex, NULL ) ) {
				return qfalse;
			}
		}
	}

	for ( row = 0; row < surface->height - 1; row++ ) {
		for ( col = 0; col < surface->width - 1; col++ ) {
			uint32_t i0 = baseVertex + (uint32_t)( row * surface->width + col );
			uint32_t i1 = baseVertex + (uint32_t)( row * surface->width + col + 1 );
			uint32_t i2 = baseVertex + (uint32_t)( ( row + 1 ) * surface->width + col );
			uint32_t i3 = baseVertex + (uint32_t)( ( row + 1 ) * surface->width + col + 1 );

			if ( !vk_rt_cpu_geometry_add_index( geometry, i0 ) ||
				!vk_rt_cpu_geometry_add_index( geometry, i2 ) ||
				!vk_rt_cpu_geometry_add_index( geometry, i1 ) ||
				!vk_rt_cpu_geometry_add_index( geometry, i1 ) ||
				!vk_rt_cpu_geometry_add_index( geometry, i2 ) ||
				!vk_rt_cpu_geometry_add_index( geometry, i3 ) ) {
				return qfalse;
			}
		}
	}

	geometry->sourceTriangleCount += triCount;
	if ( geometry->materials[ materialIndex ].flags & RTX_RT_MATFLAG_MASKED ) {
		geometry->maskedTriangleCount += triCount;
	}
	if ( geometry->materials[ materialIndex ].flags &
		( RTX_RT_MATFLAG_MASKED |
			RTX_RT_MATFLAG_CULL_FRONT |
			RTX_RT_MATFLAG_CULL_BACK |
			RTX_RT_MATFLAG_RASTER_OVERLAY ) ) {
		geometry->anyHitTriangleCount += triCount;
	}

	return qtrue;
}

static qboolean vk_rt_append_surface_triangles_geometry( rtxRtCpuGeometry_t *geometry, const srfTriangles_t *surface, const shader_t *shader, uint32_t materialFlags )
{
	uint32_t baseVertex;
	uint32_t materialIndex = 0;
	uint32_t validTriangleCount = 0;
	vec4_t tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
	int completeIndexCount;
	int i;

	if ( !surface || surface->numVerts < 3 || surface->numIndexes < 3 ) {
		return qtrue;
	}

	completeIndexCount = surface->numIndexes - surface->numIndexes % 3;
	for ( i = 0; i < completeIndexCount; i += 3 ) {
		if ( surface->indexes[i + 0] < 0 || surface->indexes[i + 0] >= surface->numVerts ||
			surface->indexes[i + 1] < 0 || surface->indexes[i + 1] >= surface->numVerts ||
			surface->indexes[i + 2] < 0 || surface->indexes[i + 2] >= surface->numVerts ) {
			continue;
		}
		validTriangleCount++;
	}

	if ( !vk_rt_cpu_geometry_find_or_add_material_ex( geometry, shader, materialFlags, &materialIndex ) ) {
		return qfalse;
	}

	if ( !vk_rt_cpu_geometry_reserve( geometry, (uint32_t)surface->numVerts, validTriangleCount * 3u ) ) {
		return qfalse;
	}

	baseVertex = geometry->numVertices;
	for ( i = 0; i < surface->numVerts; i++ ) {
		if ( !vk_rt_cpu_geometry_add_vertex( geometry, surface->verts[i].xyz, surface->verts[i].normal, tangent, surface->verts[i].st, surface->verts[i].color.u32, materialIndex, NULL ) ) {
			return qfalse;
		}
	}

	for ( i = 0; i < completeIndexCount; i += 3 ) {
		const int i0 = surface->indexes[i + 0];
		const int i1 = surface->indexes[i + 1];
		const int i2 = surface->indexes[i + 2];

		if ( i0 < 0 || i0 >= surface->numVerts ||
			i1 < 0 || i1 >= surface->numVerts ||
			i2 < 0 || i2 >= surface->numVerts ) {
			continue;
		}
		if ( !vk_rt_cpu_geometry_add_triangle( geometry,
			baseVertex + (uint32_t)i0,
			baseVertex + (uint32_t)i1,
			baseVertex + (uint32_t)i2 ) ) {
			return qfalse;
		}
	}

	geometry->sourceTriangleCount += validTriangleCount;
	if ( geometry->materials[ materialIndex ].flags & RTX_RT_MATFLAG_MASKED ) {
		geometry->maskedTriangleCount += validTriangleCount;
	}
	if ( geometry->materials[ materialIndex ].flags &
		( RTX_RT_MATFLAG_MASKED |
			RTX_RT_MATFLAG_CULL_FRONT |
			RTX_RT_MATFLAG_CULL_BACK |
			RTX_RT_MATFLAG_RASTER_OVERLAY ) ) {
		geometry->anyHitTriangleCount += validTriangleCount;
	}

	return qtrue;
}

static qboolean vk_rt_generate_tangents( rtxRtCpuGeometry_t *geometry )
{
	float *tan1;
	float *tan2;
	float *normalAccum;
	uint32_t tri;
	uint32_t i;

	if ( !geometry || geometry->numVertices == 0 || geometry->numIndices < 3 ) {
		return qtrue;
	}

	tan1 = (float *)ri.Malloc( geometry->numVertices * 3 * sizeof( float ) );
	tan2 = (float *)ri.Malloc( geometry->numVertices * 3 * sizeof( float ) );
	normalAccum = (float *)ri.Malloc( geometry->numVertices * 3 * sizeof( float ) );

	Com_Memset( tan1, 0, geometry->numVertices * 3 * sizeof( float ) );
	Com_Memset( tan2, 0, geometry->numVertices * 3 * sizeof( float ) );
	Com_Memset( normalAccum, 0, geometry->numVertices * 3 * sizeof( float ) );

	for ( tri = 0; tri + 2 < geometry->numIndices; tri += 3 ) {
		uint32_t i0 = geometry->indices[ tri + 0 ];
		uint32_t i1 = geometry->indices[ tri + 1 ];
		uint32_t i2 = geometry->indices[ tri + 2 ];
		rtxRtPackedVertex_t *v0;
		rtxRtPackedVertex_t *v1;
		rtxRtPackedVertex_t *v2;
		vec3_t edge1;
		vec3_t edge2;
		vec3_t faceNormal;
		float du1;
		float dv1;
		float du2;
		float dv2;
		float det;
		float invDet;
		vec3_t sdir;
		vec3_t tdir;
		int axis;

		if ( i0 >= geometry->numVertices || i1 >= geometry->numVertices || i2 >= geometry->numVertices ) {
			geometry->degenerateTriangleCount++;
			continue;
		}

		v0 = &geometry->vertices[ i0 ];
		v1 = &geometry->vertices[ i1 ];
		v2 = &geometry->vertices[ i2 ];

		VectorSubtract( v1->xyz, v0->xyz, edge1 );
		VectorSubtract( v2->xyz, v0->xyz, edge2 );
		CrossProduct( edge1, edge2, faceNormal );
		if ( VectorLengthSquared( faceNormal ) > 1e-10f ) {
			for ( axis = 0; axis < 3; axis++ ) {
				normalAccum[i0 * 3 + axis] += faceNormal[axis];
				normalAccum[i1 * 3 + axis] += faceNormal[axis];
				normalAccum[i2 * 3 + axis] += faceNormal[axis];
			}
		}

		du1 = v1->texCoord[0] - v0->texCoord[0];
		dv1 = v1->texCoord[1] - v0->texCoord[1];
		du2 = v2->texCoord[0] - v0->texCoord[0];
		dv2 = v2->texCoord[1] - v0->texCoord[1];
		det = du1 * dv2 - du2 * dv1;
		if ( fabsf( det ) < 1e-12f ) {
			geometry->degenerateTriangleCount++;
			continue;
		}

		invDet = 1.0f / det;
		sdir[0] = ( dv2 * edge1[0] - dv1 * edge2[0] ) * invDet;
		sdir[1] = ( dv2 * edge1[1] - dv1 * edge2[1] ) * invDet;
		sdir[2] = ( dv2 * edge1[2] - dv1 * edge2[2] ) * invDet;

		tdir[0] = ( du1 * edge2[0] - du2 * edge1[0] ) * invDet;
		tdir[1] = ( du1 * edge2[1] - du2 * edge1[1] ) * invDet;
		tdir[2] = ( du1 * edge2[2] - du2 * edge1[2] ) * invDet;

		for ( axis = 0; axis < 3; axis++ ) {
			tan1[i0 * 3 + axis] += sdir[axis];
			tan1[i1 * 3 + axis] += sdir[axis];
			tan1[i2 * 3 + axis] += sdir[axis];

			tan2[i0 * 3 + axis] += tdir[axis];
			tan2[i1 * 3 + axis] += tdir[axis];
			tan2[i2 * 3 + axis] += tdir[axis];
		}
	}

	for ( i = 0; i < geometry->numVertices; i++ ) {
		rtxRtPackedVertex_t *v = &geometry->vertices[i];
		vec3_t n = { v->normal[0], v->normal[1], v->normal[2] };
		vec3_t t = { tan1[i * 3 + 0], tan1[i * 3 + 1], tan1[i * 3 + 2] };
		vec3_t b = { tan2[i * 3 + 0], tan2[i * 3 + 1], tan2[i * 3 + 2] };
		float nLenSq = VectorLengthSquared( n );
		float tLenSq = VectorLengthSquared( t );
		float ndott;
		vec3_t c;
		float handedness;

		if ( nLenSq < 1e-10f ) {
			n[0] = normalAccum[i * 3 + 0];
			n[1] = normalAccum[i * 3 + 1];
			n[2] = normalAccum[i * 3 + 2];
			if ( VectorLengthSquared( n ) < 1e-10f ) {
				n[0] = 0.0f;
				n[1] = 0.0f;
				n[2] = 1.0f;
			}
		}
		VectorNormalize( n );

		if ( tLenSq < 1e-10f ) {
			if ( fabsf( n[2] ) < 0.999f ) {
				vec3_t up = { 0.0f, 0.0f, 1.0f };
				CrossProduct( up, n, t );
			} else {
				vec3_t up = { 0.0f, 1.0f, 0.0f };
				CrossProduct( up, n, t );
			}
		}

		ndott = DotProduct( n, t );
		VectorMA( t, -ndott, n, t );
		if ( VectorNormalize( t ) == 0.0f ) {
			t[0] = 1.0f;
			t[1] = 0.0f;
			t[2] = 0.0f;
		}

		CrossProduct( n, t, c );
		handedness = ( DotProduct( c, b ) < 0.0f ) ? -1.0f : 1.0f;

		v->normal[0] = n[0];
		v->normal[1] = n[1];
		v->normal[2] = n[2];
		v->normal[3] = 0.0f;
		v->tangent[0] = t[0];
		v->tangent[1] = t[1];
		v->tangent[2] = t[2];
		v->tangent[3] = handedness;
	}

	ri.Free( tan1 );
	ri.Free( tan2 );
	ri.Free( normalAccum );
	return qtrue;
}

static qboolean vk_rt_extract_world_geometry( rtxRtCpuGeometry_t *geometry )
{
	const bmodel_t *worldModel;
	int i;
	uint32_t faceCount = 0;
	uint32_t gridCount = 0;
	uint32_t triCount = 0;
	uint32_t skySkipCount = 0;
	uint32_t rasterOverlayOccluderCount = 0;
	uint32_t fogOverlayOccluderCount = 0;
	uint32_t nonOccludingOverlaySkipCount = 0;
	uint32_t referenceTriangleCount = 0;
	uint32_t shaderFallbacks = 0;

	vk_rt_cpu_geometry_init( geometry );

	if ( !tr.world || !tr.worldMapLoaded || !tr.world->bmodels ) {
		return qfalse;
	}

	worldModel = &tr.world->bmodels[0];
	if ( !worldModel->firstSurface || worldModel->numSurfaces <= 0 ) {
		return qfalse;
	}

	for ( i = 0; i < worldModel->numSurfaces; i++ ) {
		const msurface_t *surface = &worldModel->firstSurface[i];
		const shader_t *shader = surface->shader ? surface->shader : tr.defaultShader;
		uint32_t materialFlags = RTX_RT_MATFLAG_NONE;

		if ( !surface->shader ) {
			shaderFallbacks++;
		}
		if ( shader && ( shader->isSky || ( shader->surfaceFlags & SURF_SKY ) ) ) {
			skySkipCount++;
			continue;
		}
		if ( shader && shader->sort != SS_OPAQUE && shader->sort != SS_SEE_THROUGH ) {
			/*
			 * Portals, decals, banners and translucent effects remain entirely
			 * authored.  Opaque and alpha-tested structural surfaces below
			 * stay in the BLAS even when raster owns their final color.
			 */
			nonOccludingOverlaySkipCount++;
			continue;
		}

		if ( surface->fogIndex != 0 || !R_RtShaderNativeSupported( shader ) ) {
			materialFlags |= RTX_RT_MATFLAG_RASTER_OVERLAY;
			if ( surface->fogIndex != 0 ) {
				fogOverlayOccluderCount++;
			} else {
				rasterOverlayOccluderCount++;
			}
		}

		switch ( *surface->data ) {
		case SF_FACE:
		{
			const srfSurfaceFace_t *face = (const srfSurfaceFace_t *)surface->data;
			if ( !vk_rt_append_surface_face_geometry( geometry, (const srfSurfaceFace_t *)surface->data, shader, materialFlags ) ) {
				vk_rt_cpu_geometry_free( geometry );
				return qfalse;
			}
			referenceTriangleCount += (uint32_t)( face->numIndices / 3 );
			faceCount++;
			break;
		}
		case SF_GRID:
		{
			const srfGridMesh_t *grid = (const srfGridMesh_t *)surface->data;
			if ( !vk_rt_append_surface_grid_geometry( geometry, (const srfGridMesh_t *)surface->data, shader, materialFlags ) ) {
				vk_rt_cpu_geometry_free( geometry );
				return qfalse;
			}
			if ( grid->width >= 2 && grid->height >= 2 ) {
				referenceTriangleCount += (uint32_t)( ( grid->width - 1 ) * ( grid->height - 1 ) * 2 );
			}
			gridCount++;
			break;
		}
		case SF_TRIANGLES:
		{
			const srfTriangles_t *tris = (const srfTriangles_t *)surface->data;
			if ( !vk_rt_append_surface_triangles_geometry( geometry, (const srfTriangles_t *)surface->data, shader, materialFlags ) ) {
				vk_rt_cpu_geometry_free( geometry );
				return qfalse;
			}
			referenceTriangleCount += (uint32_t)( tris->numIndexes / 3 );
			triCount++;
			break;
		}
		default:
			break;
		}
	}

	if ( !vk_rt_generate_tangents( geometry ) ) {
		vk_rt_cpu_geometry_free( geometry );
		return qfalse;
	}

	vk.rt.stats.world_triangles = geometry->sourceTriangleCount;
	vk.rt.world_masked_triangle_count = geometry->maskedTriangleCount;
	vk.rt.stats.masked_triangles =
		vk.rt.world_masked_triangle_count +
		vk.rt.dynamic_masked_triangle_count;

	if ( geometry->sourceTriangleCount != referenceTriangleCount ) {
		ri.Printf( PRINT_WARNING,
			"RTX RT: world triangle count mismatch (rt=%u raster_ref=%u)\n",
			geometry->sourceTriangleCount,
			referenceTriangleCount );
	}

	if ( vk_rt_debug_as_stats_level() >= 1 ) {
		ri.Printf( PRINT_DEVELOPER,
			"RTX RT: world extraction complete (faces=%u, grids=%u, triangles=%u, trianglesRef=%u, vertices=%u, indices=%u, materials=%u, maskedTriangles=%u, anyHitTriangles=%u, degenerate=%u, shaderFallbacks=%u)\n",
			faceCount,
			gridCount,
			triCount,
			referenceTriangleCount,
			geometry->numVertices,
			geometry->numIndices,
			geometry->numMaterials,
			geometry->maskedTriangleCount,
			geometry->anyHitTriangleCount,
			geometry->degenerateTriangleCount,
			shaderFallbacks );
		if ( skySkipCount > 0 ) {
			ri.Printf( PRINT_DEVELOPER, "RTX RT: skipped %u sky surfaces from RT BLAS (sky handled via miss/environment)\n", skySkipCount );
		}
		if ( rasterOverlayOccluderCount > 0 ) {
			ri.Printf( PRINT_DEVELOPER,
				"RTX RT: retained %u authored-overlay structural surfaces as shadow/visibility occluders\n",
				rasterOverlayOccluderCount );
		}
		if ( fogOverlayOccluderCount > 0 ) {
			ri.Printf( PRINT_DEVELOPER,
				"RTX RT: retained %u fogged structural surfaces as shadow/visibility occluders while raster owns fog color\n",
				fogOverlayOccluderCount );
		}
		if ( nonOccludingOverlaySkipCount > 0 ) {
			ri.Printf( PRINT_DEVELOPER,
				"RTX RT: skipped %u non-structural authored overlay surfaces from RT BLAS\n",
				nonOccludingOverlaySkipCount );
		}
	}

	if ( rtx_rt_material_debug && rtx_rt_material_debug->integer >= 2 ) {
		uint32_t maxPrint = MIN( geometry->numMaterials, 24u );
		uint32_t texturedCount = 0u;
		uint32_t m;
		for ( m = 0; m < geometry->numMaterials; m++ ) {
			if ( geometry->materials[m].albedoTextureIndex >= 0 ) {
				texturedCount++;
			}
		}
		ri.Printf( PRINT_DEVELOPER,
			"RTX RT: world materials with albedo textures: %u / %u\n",
			texturedCount,
			geometry->numMaterials );
		for ( m = 0; m < maxPrint; m++ ) {
			const rtxRtMaterial_t *mat = &geometry->materials[m];
			ri.Printf( PRINT_DEVELOPER,
				"RTX RT material[%u] '%s': flags=0x%x base=(%.2f %.2f %.2f) rm=(%.2f %.2f) emissiveScale=%.2f alphaCutoff=%.2f cs=(%u %u %u) tex=%d\n",
				m,
				mat->name,
				mat->flags,
				mat->baseColor[0], mat->baseColor[1], mat->baseColor[2],
				mat->roughness, mat->metallic,
				mat->emissiveScale,
				mat->alphaCutoff,
				mat->albedoColorSpace,
				mat->normalColorSpace,
				mat->emissiveColorSpace,
				mat->albedoTextureIndex );
		}
	}

	return ( geometry->numVertices >= 3 && geometry->numIndices >= 3 ) ? qtrue : qfalse;
}

static qboolean vk_rt_build_triangles_blas( rtxVkRtAccelerationStructure_t *outAs, const rtxVkRtBuffer_t *vertexBuffer, uint32_t vertexCount, VkDeviceSize vertexStride, const rtxVkRtBuffer_t *indexBuffer, uint32_t indexCount, qboolean opaqueGeometry, qboolean allowCompaction, const char *debugName )
{
	VkAccelerationStructureGeometryTrianglesDataKHR triangles;
	VkAccelerationStructureGeometryKHR geometry;
	VkAccelerationStructureBuildGeometryInfoKHR buildInfo;
	VkAccelerationStructureBuildSizesInfoKHR buildSizes;
	VkAccelerationStructureBuildRangeInfoKHR rangeInfo;
	VkAccelerationStructureBuildRangeInfoKHR *rangeInfos[1];
	VkDeviceAddress scratchAddress = 0;
	VkCommandBuffer cmd;
	VkQueryPool queryPool = VK_NULL_HANDLE;
	VkQueryPoolCreateInfo queryInfo;
	VkResult result;
	VkBuildAccelerationStructureFlagsKHR buildFlags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	uint64_t bytesSaved = 0;

	if ( !vertexBuffer || !indexBuffer || vertexBuffer->address == 0 || indexBuffer->address == 0 ) {
		ri.Printf( PRINT_WARNING,
			"RTX RT: skipping BLAS build '%s' due to missing buffer device address (vtx=0x%llx idx=0x%llx)\n",
			debugName ? debugName : "unnamed",
			(unsigned long long)( vertexBuffer ? vertexBuffer->address : 0 ),
			(unsigned long long)( indexBuffer ? indexBuffer->address : 0 ) );
		return qfalse;
	}

	if ( allowCompaction ) {
		buildFlags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
	}

	Com_Memset( &triangles, 0, sizeof( triangles ) );
	triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	triangles.vertexData.deviceAddress = vertexBuffer->address;
	triangles.vertexStride = vertexStride;
	triangles.maxVertex = vertexCount ? ( vertexCount - 1 ) : 0;
	triangles.indexType = VK_INDEX_TYPE_UINT32;
	triangles.indexData.deviceAddress = indexBuffer->address;

	Com_Memset( &geometry, 0, sizeof( geometry ) );
	geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	geometry.flags = opaqueGeometry ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0;
	geometry.geometry.triangles = triangles;

	Com_Memset( &buildInfo, 0, sizeof( buildInfo ) );
	buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	buildInfo.flags = buildFlags;
	buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	buildInfo.geometryCount = 1;
	buildInfo.pGeometries = &geometry;

	Com_Memset( &buildSizes, 0, sizeof( buildSizes ) );
	buildSizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	{
		const uint32_t primitiveCount = indexCount / 3;
		qvkGetAccelerationStructureBuildSizesKHR(
			vk.device,
			VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
			&buildInfo,
			&primitiveCount,
			&buildSizes );
	}

	if ( outAs->valid ) {
		if ( !vk_rt_wait_for_inflight_frames( debugName ? debugName : "BLAS rebuild" ) ) {
			return qfalse;
		}
	}
	vk_rt_destroy_as( outAs );
	if ( !vk_rt_create_as( VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, buildSizes.accelerationStructureSize, debugName, outAs ) ) {
		return qfalse;
	}

	if ( !vk_rt_ensure_scratch_buffer( buildSizes.buildScratchSize ) ) {
		vk_rt_destroy_as( outAs );
		return qfalse;
	}
	if ( !vk_rt_require_scratch_device_address( &scratchAddress ) ) {
		vk_rt_destroy_as( outAs );
		return qfalse;
	}

	buildInfo.dstAccelerationStructure = outAs->handle;
	buildInfo.scratchData.deviceAddress = scratchAddress;

	Com_Memset( &rangeInfo, 0, sizeof( rangeInfo ) );
	rangeInfo.primitiveCount = indexCount / 3;
	rangeInfo.primitiveOffset = 0;
	rangeInfo.firstVertex = 0;
	rangeInfo.transformOffset = 0;
	rangeInfos[0] = &rangeInfo;

	if ( allowCompaction ) {
		Com_Memset( &queryInfo, 0, sizeof( queryInfo ) );
		queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		queryInfo.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
		queryInfo.queryCount = 1;

		result = qvkCreateQueryPool( vk.device, &queryInfo, NULL, &queryPool );
		if ( result != VK_SUCCESS ) {
			ri.Printf( PRINT_WARNING, "RTX RT: failed to create compaction query pool: %s\n", vk_result_string( result ) );
			queryPool = VK_NULL_HANDLE;
		}
	}

	cmd = begin_command_buffer();
	if ( queryPool != VK_NULL_HANDLE ) {
		qvkCmdResetQueryPool( cmd, queryPool, 0, 1 );
	}
	qvkCmdBuildAccelerationStructuresKHR( cmd, 1, &buildInfo, (const VkAccelerationStructureBuildRangeInfoKHR *const *)rangeInfos );

	if ( queryPool != VK_NULL_HANDLE ) {
		qvkCmdWriteAccelerationStructuresPropertiesKHR(
			cmd,
			1,
			&outAs->handle,
			VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
			queryPool,
			0 );
	}

	if ( !end_command_buffer_soft( cmd, __func__ ) ) {
		ri.Printf( PRINT_WARNING,
			"RTX RT: BLAS build submission failed for '%s' (verts=%u indices=%u)\n",
			debugName ? debugName : "unnamed",
			vertexCount,
			indexCount );
		vk_rt_destroy_as( outAs );
		if ( queryPool != VK_NULL_HANDLE ) {
			qvkDestroyQueryPool( vk.device, queryPool, NULL );
		}
		return qfalse;
	}

	outAs->valid = qtrue;
	outAs->compacted = qfalse;

	if ( queryPool != VK_NULL_HANDLE ) {
		if ( vk_rt_compact_as( outAs, queryPool, 0, debugName, &bytesSaved ) ) {
			outAs->compacted = qtrue;
			vk.rt.stats.compacted_bytes_saved += bytesSaved;
		}
		qvkDestroyQueryPool( vk.device, queryPool, NULL );
		queryPool = VK_NULL_HANDLE;
	}

	{
		VkAccelerationStructureDeviceAddressInfoKHR addressInfo;
		addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
		addressInfo.pNext = NULL;
		addressInfo.accelerationStructure = outAs->handle;
		outAs->deviceAddress = qvkGetAccelerationStructureDeviceAddressKHR( vk.device, &addressInfo );
	}

	return qtrue;
}

static void vk_rt_transform_point( const trRefEntity_t *ent, const vec3_t local, vec3_t world )
{
	VectorCopy( ent->e.origin, world );
	VectorMA( world, local[0], ent->e.axis[0], world );
	VectorMA( world, local[1], ent->e.axis[1], world );
	VectorMA( world, local[2], ent->e.axis[2], world );
}

static void vk_rt_transform_direction( const trRefEntity_t *ent, const vec3_t local, vec3_t world )
{
	world[0] = local[0] * ent->e.axis[0][0] + local[1] * ent->e.axis[1][0] + local[2] * ent->e.axis[2][0];
	world[1] = local[0] * ent->e.axis[0][1] + local[1] * ent->e.axis[1][1] + local[2] * ent->e.axis[2][1];
	world[2] = local[0] * ent->e.axis[0][2] + local[1] * ent->e.axis[1][2] + local[2] * ent->e.axis[2][2];
	if ( VectorNormalize( world ) == 0.0f ) {
		world[0] = 0.0f;
		world[1] = 0.0f;
		world[2] = 1.0f;
	}
}

static void vk_rt_transform_geometry_range( rtxRtCpuGeometry_t *geometry, uint32_t firstVertex, const trRefEntity_t *ent )
{
	uint32_t v;

	if ( !geometry || !ent || firstVertex >= geometry->numVertices ) {
		return;
	}

	for ( v = firstVertex; v < geometry->numVertices; v++ ) {
		rtxRtPackedVertex_t *vertex = &geometry->vertices[v];
		vec3_t localPos = { vertex->xyz[0], vertex->xyz[1], vertex->xyz[2] };
		vec3_t localNormal = { vertex->normal[0], vertex->normal[1], vertex->normal[2] };
		vec3_t localTangent = { vertex->tangent[0], vertex->tangent[1], vertex->tangent[2] };
		vec3_t worldPos;
		vec3_t worldNormal;
		vec3_t worldTangent;

		vk_rt_transform_point( ent, localPos, worldPos );
		vk_rt_transform_direction( ent, localNormal, worldNormal );
		vk_rt_transform_direction( ent, localTangent, worldTangent );

		vertex->xyz[0] = worldPos[0];
		vertex->xyz[1] = worldPos[1];
		vertex->xyz[2] = worldPos[2];
		vertex->normal[0] = worldNormal[0];
		vertex->normal[1] = worldNormal[1];
		vertex->normal[2] = worldNormal[2];
		vertex->tangent[0] = worldTangent[0];
		vertex->tangent[1] = worldTangent[1];
		vertex->tangent[2] = worldTangent[2];
	}
}

static void vk_rt_apply_effect_material_tuning( rtxRtCpuGeometry_t *geometry, uint32_t materialIndex, qboolean particleLike, qboolean decalLike )
{
	rtxRtMaterial_t *material;
	float emissiveBoost;

	if ( !geometry || materialIndex >= geometry->numMaterials ) {
		return;
	}

	material = &geometry->materials[materialIndex];
	material->flags |= RTX_RT_MATFLAG_EFFECT;

	if ( particleLike ) {
		material->flags |= RTX_RT_MATFLAG_PARTICLE | RTX_RT_MATFLAG_TRANSLUCENT;
		material->roughness = MAX( material->roughness, 0.7f );
	}

	if ( decalLike ) {
		material->flags |= RTX_RT_MATFLAG_DECAL;
		material->roughness = MIN( material->roughness, 0.55f );
	}

	emissiveBoost = vk_rt_effect_emissive_boost();
	if ( emissiveBoost > 0.0f && ( particleLike || ( material->flags & RTX_RT_MATFLAG_EMISSIVE ) ) ) {
		material->flags |= RTX_RT_MATFLAG_EMISSIVE;
		material->emissiveScale = MAX( material->emissiveScale, emissiveBoost );
		if ( material->emissiveColor[0] <= 0.0f && material->emissiveColor[1] <= 0.0f && material->emissiveColor[2] <= 0.0f ) {
			material->emissiveColor[0] = 1.0f;
			material->emissiveColor[1] = 1.0f;
			material->emissiveColor[2] = 1.0f;
		}
	}
}

static qboolean vk_rt_append_brush_entity_geometry( rtxRtCpuGeometry_t *geometry, const trRefEntity_t *ent, const model_t *model )
{
	const bmodel_t *bmodel;
	int s;

	if ( !geometry || !ent || !model || model->type != MOD_BRUSH || !model->bmodel ) {
		return qtrue;
	}

	bmodel = model->bmodel;
	for ( s = 0; s < bmodel->numSurfaces; s++ ) {
		const msurface_t *surface = &bmodel->firstSurface[s];
		const shader_t *shader = ( surface && surface->shader ) ? surface->shader : tr.defaultShader;
		uint32_t firstVertex = geometry->numVertices;

		if ( !surface || !surface->data ) {
			continue;
		}

		switch ( *surface->data ) {
		case SF_FACE:
			if ( !vk_rt_append_surface_face_geometry(
				geometry,
				(const srfSurfaceFace_t *)surface->data,
				shader,
				RTX_RT_MATFLAG_RASTER_OVERLAY ) ) {
				return qfalse;
			}
			break;
		case SF_GRID:
			if ( !vk_rt_append_surface_grid_geometry(
				geometry,
				(const srfGridMesh_t *)surface->data,
				shader,
				RTX_RT_MATFLAG_RASTER_OVERLAY ) ) {
				return qfalse;
			}
			break;
		case SF_TRIANGLES:
			if ( !vk_rt_append_surface_triangles_geometry(
				geometry,
				(const srfTriangles_t *)surface->data,
				shader,
				RTX_RT_MATFLAG_RASTER_OVERLAY ) ) {
				return qfalse;
			}
			break;
		default:
			break;
		}

		vk_rt_transform_geometry_range( geometry, firstVertex, ent );
	}

	return qtrue;
}

static void vk_rt_decode_md3_normal( short packedNormal, vec3_t outNormal )
{
	const uint8_t lat = (uint8_t)( ( packedNormal >> 8 ) & 0xFF );
	const uint8_t lng = (uint8_t)( packedNormal & 0xFF );
	const float latRad = (float)lat * ( 2.0f * (float)M_PI / 255.0f );
	const float lngRad = (float)lng * ( 2.0f * (float)M_PI / 255.0f );
	outNormal[0] = cosf( latRad ) * sinf( lngRad );
	outNormal[1] = sinf( latRad ) * sinf( lngRad );
	outNormal[2] = cosf( lngRad );
}

static const shader_t *vk_rt_resolve_md3_shader( const trRefEntity_t *ent, const md3Surface_t *surface )
{
	const shader_t *shader = tr.defaultShader;

	if ( ent->e.customShader ) {
		shader = R_GetShaderByHandle( ent->e.customShader );
	} else if ( ent->e.customSkin > 0 && ent->e.customSkin < tr.numSkins ) {
		const skin_t *skin = R_GetSkinByHandle( ent->e.customSkin );
		int i;

		for ( i = 0; i < skin->numSurfaces; i++ ) {
			if ( !strcmp( skin->surfaces[i].name, surface->name ) ) {
				shader = skin->surfaces[i].shader;
				break;
			}
		}
	} else if ( surface->numShaders > 0 ) {
		const md3Shader_t *md3Shader = (const md3Shader_t *)( (const byte *)surface + surface->ofsShaders );
		const int shaderIndex = ent->e.skinNum % surface->numShaders;
		md3Shader += shaderIndex;
		if ( md3Shader->shaderIndex >= 0 && md3Shader->shaderIndex < tr.numShaders ) {
			shader = tr.shaders[ md3Shader->shaderIndex ];
		}
	}

	return shader ? shader : tr.defaultShader;
}

static const shader_t *vk_rt_resolve_iqm_shader( const trRefEntity_t *ent, const srfIQModel_t *surface )
{
	const shader_t *shader = tr.defaultShader;

	if ( ent->e.customShader ) {
		shader = R_GetShaderByHandle( ent->e.customShader );
	} else if ( ent->e.customSkin > 0 && ent->e.customSkin < tr.numSkins ) {
		const skin_t *skin = R_GetSkinByHandle( ent->e.customSkin );
		int i;
		for ( i = 0; i < skin->numSurfaces; i++ ) {
			if ( !strcmp( skin->surfaces[i].name, surface->name ) ) {
				shader = skin->surfaces[i].shader;
				break;
			}
		}
	} else if ( surface->shader ) {
		shader = surface->shader;
	}

	return shader ? shader : tr.defaultShader;
}

static qboolean vk_rt_append_sprite_entity_geometry( rtxRtCpuGeometry_t *geometry, const trRefEntity_t *ent )
{
	const shader_t *shader;
	uint32_t materialIndex = 0;
	vec3_t left;
	vec3_t up;
	vec3_t normal;
	vec3_t corners[4];
	vec2_t texCoords[4] = {
		{ 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f }
	};
	vec4_t tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
	uint32_t baseVertex;
	uint32_t color;
	float radius;
	float ang;
	float s;
	float c;
	int i;

	radius = ent->e.radius;
	if ( radius <= 0.0f ) {
		radius = 8.0f;
	}
	radius *= vk_rt_effect_radius_scale();

	shader = ent->e.customShader ? R_GetShaderByHandle( ent->e.customShader ) : tr.defaultShader;
	if ( !vk_rt_cpu_geometry_find_or_add_material( geometry, shader, &materialIndex ) ) {
		return qfalse;
	}
	vk_rt_apply_effect_material_tuning( geometry, materialIndex, qtrue, qfalse );

	if ( ent->e.rotation == 0.0f ) {
		VectorScale( backEnd.viewParms.or.axis[1], radius, left );
		VectorScale( backEnd.viewParms.or.axis[2], radius, up );
	} else {
		ang = M_PI * ent->e.rotation / 180.0f;
		s = sinf( ang );
		c = cosf( ang );
		VectorScale( backEnd.viewParms.or.axis[1], c * radius, left );
		VectorMA( left, -s * radius, backEnd.viewParms.or.axis[2], left );
		VectorScale( backEnd.viewParms.or.axis[2], c * radius, up );
		VectorMA( up, s * radius, backEnd.viewParms.or.axis[1], up );
	}

	VectorSubtract( vec3_origin, backEnd.viewParms.or.axis[0], normal );
	VectorNormalize( normal );

	VectorAdd( ent->e.origin, left, corners[0] );
	VectorAdd( corners[0], up, corners[0] );
	VectorSubtract( ent->e.origin, left, corners[1] );
	VectorAdd( corners[1], up, corners[1] );
	VectorSubtract( ent->e.origin, left, corners[2] );
	VectorSubtract( corners[2], up, corners[2] );
	VectorAdd( ent->e.origin, left, corners[3] );
	VectorSubtract( corners[3], up, corners[3] );

	color = ent->e.shader.u32 ? ent->e.shader.u32 : 0xFFFFFFFFu;

	if ( !vk_rt_cpu_geometry_reserve( geometry, 4, 6 ) ) {
		return qfalse;
	}

	baseVertex = geometry->numVertices;
	for ( i = 0; i < 4; i++ ) {
		if ( !vk_rt_cpu_geometry_add_vertex( geometry, corners[i], normal, tangent, texCoords[i], color, materialIndex, NULL ) ) {
			return qfalse;
		}
	}

	if ( !vk_rt_cpu_geometry_add_index( geometry, baseVertex + 0 ) ||
		!vk_rt_cpu_geometry_add_index( geometry, baseVertex + 1 ) ||
		!vk_rt_cpu_geometry_add_index( geometry, baseVertex + 3 ) ||
		!vk_rt_cpu_geometry_add_index( geometry, baseVertex + 3 ) ||
		!vk_rt_cpu_geometry_add_index( geometry, baseVertex + 1 ) ||
		!vk_rt_cpu_geometry_add_index( geometry, baseVertex + 2 ) ) {
		return qfalse;
	}

	geometry->sourceTriangleCount += 2;
	if ( geometry->materials[ materialIndex ].flags & RTX_RT_MATFLAG_MASKED ) {
		geometry->maskedTriangleCount += 2;
	}
	geometry->sourceEffectCount++;
	return qtrue;
}

static qboolean vk_rt_append_beam_entity_geometry( rtxRtCpuGeometry_t *geometry, const trRefEntity_t *ent )
{
	const shader_t *shader;
	uint32_t materialIndex = 0;
	vec3_t start;
	vec3_t end;
	vec3_t direction;
	vec3_t view;
	vec3_t side;
	vec3_t normal;
	vec3_t corners[4];
	vec2_t texCoords[4] = {
		{ 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f }
	};
	vec4_t tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
	uint32_t baseVertex;
	float width;
	uint32_t color;
	int i;

	VectorCopy( ent->e.origin, start );
	VectorCopy( ent->e.oldorigin, end );
	VectorSubtract( end, start, direction );
	if ( VectorNormalize( direction ) == 0.0f ) {
		return qtrue;
	}

	VectorSubtract( start, backEnd.viewParms.or.origin, view );
	if ( VectorNormalize( view ) == 0.0f ) {
		VectorSet( view, 0.0f, 0.0f, 1.0f );
	}

	CrossProduct( direction, view, side );
	if ( VectorNormalize( side ) == 0.0f ) {
		CrossProduct( direction, backEnd.viewParms.or.axis[2], side );
		if ( VectorNormalize( side ) == 0.0f ) {
			return qtrue;
		}
	}

	width = ent->e.radius;
	if ( width <= 0.0f ) {
		width = ent->e.frame > 0 ? (float)ent->e.frame : 2.0f;
	}
	width *= vk_rt_effect_radius_scale();
	VectorScale( side, width * 0.5f, side );

	VectorAdd( start, side, corners[0] );
	VectorSubtract( start, side, corners[1] );
	VectorSubtract( end, side, corners[2] );
	VectorAdd( end, side, corners[3] );

	CrossProduct( side, direction, normal );
	if ( VectorNormalize( normal ) == 0.0f ) {
		VectorSet( normal, 0.0f, 0.0f, 1.0f );
	}

	shader = ent->e.customShader ? R_GetShaderByHandle( ent->e.customShader ) : tr.defaultShader;
	if ( !vk_rt_cpu_geometry_find_or_add_material( geometry, shader, &materialIndex ) ) {
		return qfalse;
	}
	vk_rt_apply_effect_material_tuning( geometry, materialIndex, qtrue, qfalse );

	color = ent->e.shader.u32 ? ent->e.shader.u32 : 0xFFFFFFFFu;

	if ( !vk_rt_cpu_geometry_reserve( geometry, 4, 6 ) ) {
		return qfalse;
	}

	baseVertex = geometry->numVertices;
	for ( i = 0; i < 4; i++ ) {
		if ( !vk_rt_cpu_geometry_add_vertex( geometry, corners[i], normal, tangent, texCoords[i], color, materialIndex, NULL ) ) {
			return qfalse;
		}
	}

	if ( !vk_rt_cpu_geometry_add_index( geometry, baseVertex + 0 ) ||
		!vk_rt_cpu_geometry_add_index( geometry, baseVertex + 1 ) ||
		!vk_rt_cpu_geometry_add_index( geometry, baseVertex + 3 ) ||
		!vk_rt_cpu_geometry_add_index( geometry, baseVertex + 3 ) ||
		!vk_rt_cpu_geometry_add_index( geometry, baseVertex + 1 ) ||
		!vk_rt_cpu_geometry_add_index( geometry, baseVertex + 2 ) ) {
		return qfalse;
	}

	geometry->sourceTriangleCount += 2;
	if ( geometry->materials[ materialIndex ].flags & RTX_RT_MATFLAG_MASKED ) {
		geometry->maskedTriangleCount += 2;
	}
	geometry->sourceEffectCount++;
	return qtrue;
}

static qboolean vk_rt_append_poly_geometry( rtxRtCpuGeometry_t *geometry, const srfPoly_t *poly )
{
	const shader_t *shader;
	uint32_t materialIndex = 0;
	uint32_t baseVertex;
	vec3_t normal = { 0.0f, 0.0f, 1.0f };
	vec4_t tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
	vec3_t edgeA;
	vec3_t edgeB;
	uint32_t triangleCount;
	uint32_t indexCount;
	int i;

	if ( !poly || poly->numVerts < 3 ) {
		return qtrue;
	}
	triangleCount = (uint32_t)poly->numVerts - 2u;
	if ( !vk_rt_checked_u32_multiply( triangleCount, 3u, &indexCount ) ) {
		ri.Printf( PRINT_WARNING, "RTX RT: refusing overflowing polygon geometry\n" );
		return qfalse;
	}

	shader = poly->hShader ? R_GetShaderByHandle( poly->hShader ) : tr.defaultShader;
	if ( !vk_rt_cpu_geometry_find_or_add_material( geometry, shader, &materialIndex ) ) {
		return qfalse;
	}
	vk_rt_apply_effect_material_tuning(
		geometry,
		materialIndex,
		( shader && shader->sort >= SS_BLEND0 ) ? qtrue : qfalse,
		( shader && shader->sort == SS_DECAL ) ? qtrue : qfalse );

	VectorSubtract( poly->verts[1].xyz, poly->verts[0].xyz, edgeA );
	VectorSubtract( poly->verts[2].xyz, poly->verts[0].xyz, edgeB );
	CrossProduct( edgeA, edgeB, normal );
	if ( VectorNormalize( normal ) == 0.0f ) {
		VectorSet( normal, 0.0f, 0.0f, 1.0f );
	}

	if ( !vk_rt_cpu_geometry_reserve( geometry, (uint32_t)poly->numVerts, indexCount ) ) {
		return qfalse;
	}

	baseVertex = geometry->numVertices;
	for ( i = 0; i < poly->numVerts; i++ ) {
		const vec2_t st = { poly->verts[i].st[0], poly->verts[i].st[1] };
		if ( !vk_rt_cpu_geometry_add_vertex( geometry, poly->verts[i].xyz, normal, tangent, st, poly->verts[i].modulate.u32, materialIndex, NULL ) ) {
			return qfalse;
		}
	}

	for ( i = 1; i < poly->numVerts - 1; i++ ) {
		if ( !vk_rt_cpu_geometry_add_index( geometry, baseVertex + 0 ) ||
			!vk_rt_cpu_geometry_add_index( geometry, baseVertex + (uint32_t)i ) ||
			!vk_rt_cpu_geometry_add_index( geometry, baseVertex + (uint32_t)( i + 1 ) ) ) {
			return qfalse;
		}
	}

	geometry->sourceTriangleCount += triangleCount;
	if ( geometry->materials[ materialIndex ].flags & RTX_RT_MATFLAG_MASKED ) {
		geometry->maskedTriangleCount += triangleCount;
	}
	geometry->sourceEffectCount++;
	return qtrue;
}

static qboolean vk_rt_append_mdr_proxy_geometry( rtxRtCpuGeometry_t *geometry, const trRefEntity_t *ent, const shader_t *shader )
{
	static const uint32_t kCubeIndices[36] = {
		0, 2, 1, 0, 3, 2,
		4, 5, 6, 4, 6, 7,
		0, 1, 5, 0, 5, 4,
		2, 3, 7, 2, 7, 6,
		1, 2, 6, 1, 6, 5,
		3, 0, 4, 3, 4, 7
	};
	vec3_t mins;
	vec3_t maxs;
	vec3_t cornersLocal[8];
	vec3_t cornerWorld;
	vec3_t normal = { 0.0f, 0.0f, 1.0f };
	vec2_t uv = { 0.0f, 0.0f };
	vec4_t tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
	uint32_t materialIndex = 0;
	uint32_t baseVertex;
	int i;

	R_ModelBounds( ent->e.hModel, mins, maxs );
	if ( mins[0] >= maxs[0] || mins[1] >= maxs[1] || mins[2] >= maxs[2] ) {
		return qtrue;
	}

	if ( !vk_rt_cpu_geometry_find_or_add_material( geometry, shader ? shader : tr.defaultShader, &materialIndex ) ) {
		return qfalse;
	}

	VectorSet( cornersLocal[0], mins[0], mins[1], mins[2] );
	VectorSet( cornersLocal[1], maxs[0], mins[1], mins[2] );
	VectorSet( cornersLocal[2], maxs[0], maxs[1], mins[2] );
	VectorSet( cornersLocal[3], mins[0], maxs[1], mins[2] );
	VectorSet( cornersLocal[4], mins[0], mins[1], maxs[2] );
	VectorSet( cornersLocal[5], maxs[0], mins[1], maxs[2] );
	VectorSet( cornersLocal[6], maxs[0], maxs[1], maxs[2] );
	VectorSet( cornersLocal[7], mins[0], maxs[1], maxs[2] );

	if ( !vk_rt_cpu_geometry_reserve( geometry, 8, ARRAY_LEN( kCubeIndices ) ) ) {
		return qfalse;
	}

	baseVertex = geometry->numVertices;
	for ( i = 0; i < 8; i++ ) {
		vk_rt_transform_point( ent, cornersLocal[i], cornerWorld );
		if ( !vk_rt_cpu_geometry_add_vertex( geometry, cornerWorld, normal, tangent, uv, 0xFFFFFFFFu, materialIndex, NULL ) ) {
			return qfalse;
		}
	}

	for ( i = 0; i < ARRAY_LEN( kCubeIndices ); i++ ) {
		if ( !vk_rt_cpu_geometry_add_index( geometry, baseVertex + kCubeIndices[i] ) ) {
			return qfalse;
		}
	}

	geometry->sourceTriangleCount += ARRAY_LEN( kCubeIndices ) / 3;
	if ( geometry->materials[ materialIndex ].flags & RTX_RT_MATFLAG_MASKED ) {
		geometry->maskedTriangleCount += ARRAY_LEN( kCubeIndices ) / 3;
	}
	geometry->sourceEntityCount++;
	return qtrue;
}

static qboolean vk_rt_append_md3_entity_geometry( rtxRtCpuGeometry_t *geometry, const trRefEntity_t *ent )
{
	const model_t *model = R_GetModelByHandle( ent->e.hModel );
	const md3Header_t *header;
	int lod;
	int frame;
	int oldFrame;
	float backlerp;
	const md3Surface_t *surface;
	int i;

	if ( !model || model->type != MOD_MESH ) {
		return qtrue;
	}

	lod = R_ComputeLOD( (trRefEntity_t *)ent );
	if ( lod < 0 ) {
		lod = 0;
	}
	if ( lod >= MD3_MAX_LODS ) {
		lod = MD3_MAX_LODS - 1;
	}

	header = model->md3[lod];
	if ( !header || header->numSurfaces <= 0 ) {
		return qtrue;
	}

	frame = ent->e.frame;
	oldFrame = ent->e.oldframe;
	if ( header->numFrames > 0 ) {
		if ( frame < 0 ) {
			frame = 0;
		}
		if ( oldFrame < 0 ) {
			oldFrame = 0;
		}
		frame %= header->numFrames;
		oldFrame %= header->numFrames;
	}

	backlerp = ( oldFrame == frame ) ? 0.0f : ent->e.backlerp;
	backlerp = Com_Clamp( 0.0f, 1.0f, backlerp );

	surface = (const md3Surface_t *)( (const byte *)header + header->ofsSurfaces );
	for ( i = 0; i < header->numSurfaces; i++ ) {
		const shader_t *shader = vk_rt_resolve_md3_shader( ent, surface );
		uint32_t materialIndex = 0;
		const md3XyzNormal_t *newVerts;
		const md3XyzNormal_t *oldVerts;
		const float *texCoords;
		const int *triangles;
		uint32_t baseVertex;
		uint32_t surfaceIndexCount;
		uint32_t validTriangleCount = 0;
		int v;
		int t;
		vec4_t tangent = { 1.0f, 0.0f, 0.0f, 1.0f };

		if ( !vk_rt_cpu_geometry_find_or_add_material( geometry, shader, &materialIndex ) ) {
			return qfalse;
		}
		if ( ent->e.renderfx & RF_FIRST_PERSON ) {
			vk_rt_apply_effect_material_tuning( geometry, materialIndex, qfalse, qfalse );
		}

		if ( surface->numVerts < 0 || surface->numTriangles < 0 ||
			!vk_rt_checked_u32_multiply( (uint32_t)surface->numTriangles, 3u,
				&surfaceIndexCount ) ) {
			ri.Printf( PRINT_WARNING, "RTX RT: refusing malformed MD3 geometry\n" );
			return qfalse;
		}
		if ( !vk_rt_cpu_geometry_reserve( geometry, (uint32_t)surface->numVerts, surfaceIndexCount ) ) {
			return qfalse;
		}

		newVerts = (const md3XyzNormal_t *)( (const byte *)surface + surface->ofsXyzNormals ) + frame * surface->numVerts;
		oldVerts = (const md3XyzNormal_t *)( (const byte *)surface + surface->ofsXyzNormals ) + oldFrame * surface->numVerts;
		texCoords = (const float *)( (const byte *)surface + surface->ofsSt );
		baseVertex = geometry->numVertices;

		for ( v = 0; v < surface->numVerts; v++ ) {
			vec3_t localPos;
			vec3_t worldPos;
			vec3_t normalNew;
			vec3_t normalOld;
			vec3_t normalLocal;
			vec3_t normalWorld;
			vec2_t uv;
			const float invScale = (float)MD3_XYZ_SCALE;

			localPos[0] = newVerts[v].xyz[0] * invScale;
			localPos[1] = newVerts[v].xyz[1] * invScale;
			localPos[2] = newVerts[v].xyz[2] * invScale;

			vk_rt_decode_md3_normal( newVerts[v].normal, normalNew );

			if ( backlerp > 0.0f ) {
				vec3_t oldPos;
				const float frontlerp = 1.0f - backlerp;
				oldPos[0] = oldVerts[v].xyz[0] * invScale;
				oldPos[1] = oldVerts[v].xyz[1] * invScale;
				oldPos[2] = oldVerts[v].xyz[2] * invScale;
				vk_rt_decode_md3_normal( oldVerts[v].normal, normalOld );

				localPos[0] = localPos[0] * frontlerp + oldPos[0] * backlerp;
				localPos[1] = localPos[1] * frontlerp + oldPos[1] * backlerp;
				localPos[2] = localPos[2] * frontlerp + oldPos[2] * backlerp;
				normalLocal[0] = normalNew[0] * frontlerp + normalOld[0] * backlerp;
				normalLocal[1] = normalNew[1] * frontlerp + normalOld[1] * backlerp;
				normalLocal[2] = normalNew[2] * frontlerp + normalOld[2] * backlerp;
				VectorNormalize( normalLocal );
			} else {
				VectorCopy( normalNew, normalLocal );
			}

			vk_rt_transform_point( ent, localPos, worldPos );
			vk_rt_transform_direction( ent, normalLocal, normalWorld );

			uv[0] = texCoords[v * 2 + 0];
			uv[1] = texCoords[v * 2 + 1];

			if ( !vk_rt_cpu_geometry_add_vertex( geometry, worldPos, normalWorld, tangent, uv, 0xFFFFFFFFu, materialIndex, NULL ) ) {
				return qfalse;
			}
		}

		triangles = (const int *)( (const byte *)surface + surface->ofsTriangles );
		for ( t = 0; t < surface->numTriangles; t++ ) {
			const int i0 = triangles[t * 3 + 0];
			const int i1 = triangles[t * 3 + 1];
			const int i2 = triangles[t * 3 + 2];
			if ( i0 < 0 || i0 >= surface->numVerts ||
				i1 < 0 || i1 >= surface->numVerts ||
				i2 < 0 || i2 >= surface->numVerts ) {
				continue;
			}
			if ( !vk_rt_cpu_geometry_add_triangle( geometry,
				baseVertex + (uint32_t)i0,
				baseVertex + (uint32_t)i1,
				baseVertex + (uint32_t)i2 ) ) {
				return qfalse;
			}
			validTriangleCount++;
		}

		geometry->sourceTriangleCount += validTriangleCount;
		if ( geometry->materials[ materialIndex ].flags & RTX_RT_MATFLAG_MASKED ) {
			geometry->maskedTriangleCount += validTriangleCount;
		}
		geometry->sourceEntityCount++;

		surface = (const md3Surface_t *)( (const byte *)surface + surface->ofsEnd );
	}

	return qtrue;
}

static void vk_rt_iqm_matrix34_multiply( const float *a, const float *b, float *out )
{
	out[ 0] = a[0] * b[0] + a[1] * b[4] + a[ 2] * b[ 8];
	out[ 1] = a[0] * b[1] + a[1] * b[5] + a[ 2] * b[ 9];
	out[ 2] = a[0] * b[2] + a[1] * b[6] + a[ 2] * b[10];
	out[ 3] = a[0] * b[3] + a[1] * b[7] + a[ 2] * b[11] + a[ 3];
	out[ 4] = a[4] * b[0] + a[5] * b[4] + a[ 6] * b[ 8];
	out[ 5] = a[4] * b[1] + a[5] * b[5] + a[ 6] * b[ 9];
	out[ 6] = a[4] * b[2] + a[5] * b[6] + a[ 6] * b[10];
	out[ 7] = a[4] * b[3] + a[5] * b[7] + a[ 6] * b[11] + a[ 7];
	out[ 8] = a[8] * b[0] + a[9] * b[4] + a[10] * b[ 8];
	out[ 9] = a[8] * b[1] + a[9] * b[5] + a[10] * b[ 9];
	out[10] = a[8] * b[2] + a[9] * b[6] + a[10] * b[10];
	out[11] = a[8] * b[3] + a[9] * b[7] + a[10] * b[11] + a[11];
}

static void vk_rt_iqm_joint_to_matrix( const quat_t rot, const vec3_t scale, const vec3_t trans, float *mat )
{
	float xx = 2.0f * rot[0] * rot[0];
	float yy = 2.0f * rot[1] * rot[1];
	float zz = 2.0f * rot[2] * rot[2];
	float xy = 2.0f * rot[0] * rot[1];
	float xz = 2.0f * rot[0] * rot[2];
	float yz = 2.0f * rot[1] * rot[2];
	float wx = 2.0f * rot[3] * rot[0];
	float wy = 2.0f * rot[3] * rot[1];
	float wz = 2.0f * rot[3] * rot[2];

	mat[ 0] = scale[0] * (1.0f - (yy + zz));
	mat[ 1] = scale[0] * (xy - wz);
	mat[ 2] = scale[0] * (xz + wy);
	mat[ 3] = trans[0];
	mat[ 4] = scale[1] * (xy + wz);
	mat[ 5] = scale[1] * (1.0f - (xx + zz));
	mat[ 6] = scale[1] * (yz - wx);
	mat[ 7] = trans[1];
	mat[ 8] = scale[2] * (xz - wy);
	mat[ 9] = scale[2] * (yz + wx);
	mat[10] = scale[2] * (1.0f - (xx + yy));
	mat[11] = trans[2];
}

static void vk_rt_iqm_quat_slerp( const quat_t from, const quat_t toIn, float fraction, quat_t out )
{
	float angle;
	float cosAngle;
	float sinAngle;
	float backlerp;
	float lerp;
	quat_t to;

	cosAngle = from[0] * toIn[0] + from[1] * toIn[1] + from[2] * toIn[2] + from[3] * toIn[3];
	if ( cosAngle < 0.0f ) {
		cosAngle = -cosAngle;
		to[0] = -toIn[0];
		to[1] = -toIn[1];
		to[2] = -toIn[2];
		to[3] = -toIn[3];
	} else {
		QuatCopy( toIn, to );
	}

	if ( cosAngle < 0.999999f ) {
		angle = acosf( cosAngle );
		sinAngle = sinf( angle );
		backlerp = sinf( ( 1.0f - fraction ) * angle ) / sinAngle;
		lerp = sinf( fraction * angle ) / sinAngle;
	} else {
		backlerp = 1.0f - fraction;
		lerp = fraction;
	}

	out[0] = from[0] * backlerp + to[0] * lerp;
	out[1] = from[1] * backlerp + to[1] * lerp;
	out[2] = from[2] * backlerp + to[2] * lerp;
	out[3] = from[3] * backlerp + to[3] * lerp;
}

static void vk_rt_iqm_compute_pose_mats( const iqmData_t *data, int frame, int oldframe, float backlerp, float *poseMats )
{
	iqmTransform_t relativeJoints[IQM_MAX_JOINTS];
	iqmTransform_t *relativeJoint;
	const iqmTransform_t *pose;
	const iqmTransform_t *oldPose;
	const int *jointParent;
	const float *invBindMat;
	float *poseMat;
	float lerp;
	int i;

	relativeJoint = relativeJoints;

	if ( oldframe == frame ) {
		pose = &data->poses[frame * data->num_poses];
		for ( i = 0; i < data->num_poses; i++, pose++, relativeJoint++ ) {
			VectorCopy( pose->translate, relativeJoint->translate );
			QuatCopy( pose->rotate, relativeJoint->rotate );
			VectorCopy( pose->scale, relativeJoint->scale );
		}
	} else {
		lerp = 1.0f - backlerp;
		pose = &data->poses[frame * data->num_poses];
		oldPose = &data->poses[oldframe * data->num_poses];
		for ( i = 0; i < data->num_poses; i++, oldPose++, pose++, relativeJoint++ ) {
			relativeJoint->translate[0] = oldPose->translate[0] * backlerp + pose->translate[0] * lerp;
			relativeJoint->translate[1] = oldPose->translate[1] * backlerp + pose->translate[1] * lerp;
			relativeJoint->translate[2] = oldPose->translate[2] * backlerp + pose->translate[2] * lerp;

			relativeJoint->scale[0] = oldPose->scale[0] * backlerp + pose->scale[0] * lerp;
			relativeJoint->scale[1] = oldPose->scale[1] * backlerp + pose->scale[1] * lerp;
			relativeJoint->scale[2] = oldPose->scale[2] * backlerp + pose->scale[2] * lerp;

			vk_rt_iqm_quat_slerp( oldPose->rotate, pose->rotate, lerp, relativeJoint->rotate );
		}
	}

	relativeJoint = relativeJoints;
	jointParent = data->jointParents;
	invBindMat = data->invBindJoints;
	poseMat = poseMats;
	for ( i = 0; i < data->num_poses; i++, relativeJoint++, jointParent++, invBindMat += 12, poseMat += 12 ) {
		float mat1[12];
		float mat2[12];

		vk_rt_iqm_joint_to_matrix( relativeJoint->rotate, relativeJoint->scale, relativeJoint->translate, mat1 );

		if ( *jointParent >= 0 ) {
			vk_rt_iqm_matrix34_multiply( &data->bindJoints[(*jointParent)*12], mat1, mat2 );
			vk_rt_iqm_matrix34_multiply( mat2, invBindMat, mat1 );
			vk_rt_iqm_matrix34_multiply( &poseMats[(*jointParent)*12], mat1, poseMat );
		} else {
			vk_rt_iqm_matrix34_multiply( mat1, invBindMat, poseMat );
		}
	}
}

static qboolean vk_rt_append_iqm_entity_geometry( rtxRtCpuGeometry_t *geometry, const trRefEntity_t *ent )
{
	static const float kIdentity34[12] = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f
	};
	const model_t *model = R_GetModelByHandle( ent->e.hModel );
	const iqmData_t *data;
	float poseMats[IQM_MAX_JOINTS * 12];
	int frame;
	int oldframe;
	float backlerp;
	int skinningMode;
	int s;

	if ( !model || model->type != MOD_IQM ) {
		return qtrue;
	}

	data = (const iqmData_t *)model->modelData;
	if ( !data || data->num_surfaces <= 0 ) {
		return qtrue;
	}

	frame = data->num_frames ? ent->e.frame % data->num_frames : 0;
	oldframe = data->num_frames ? ent->e.oldframe % data->num_frames : 0;
	if ( frame < 0 ) {
		frame += data->num_frames;
	}
	if ( oldframe < 0 ) {
		oldframe += data->num_frames;
	}

	backlerp = Com_Clamp( 0.0f, 1.0f, ent->e.backlerp );
	skinningMode = rtx_rt_iqm_skinning ? rtx_rt_iqm_skinning->integer : 1;
	if ( skinningMode <= 0 || data->num_poses <= 0 ) {
		Com_Memset( poseMats, 0, sizeof( poseMats ) );
	} else {
		if ( skinningMode >= 2 ) {
			oldframe = frame;
			backlerp = 0.0f;
		}
		vk_rt_iqm_compute_pose_mats( data, frame, oldframe, backlerp, poseMats );
	}

	for ( s = 0; s < data->num_surfaces; s++ ) {
		const srfIQModel_t *surface = &data->surfaces[s];
		const shader_t *shader = vk_rt_resolve_iqm_shader( ent, surface );
		uint32_t materialIndex = 0;
		uint32_t baseVertex;
		uint32_t surfaceIndexCount;
		uint32_t validTriangleCount = 0;
		vec4_t tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
		float *influenceVtxMat = NULL;
		float *influenceNrmMat = NULL;
		qboolean useSkinning = qfalse;
		int i;

		if ( !vk_rt_cpu_geometry_find_or_add_material( geometry, shader, &materialIndex ) ) {
			return qfalse;
		}
		if ( ent->e.renderfx & RF_FIRST_PERSON ) {
			vk_rt_apply_effect_material_tuning( geometry, materialIndex, qfalse, qfalse );
		}

		if ( surface->num_vertexes < 0 || surface->num_triangles < 0 ||
			!vk_rt_checked_u32_multiply( (uint32_t)surface->num_triangles, 3u,
				&surfaceIndexCount ) ) {
			ri.Printf( PRINT_WARNING, "RTX RT: refusing malformed IQM geometry\n" );
			return qfalse;
		}
		if ( !vk_rt_cpu_geometry_reserve( geometry, (uint32_t)surface->num_vertexes, surfaceIndexCount ) ) {
			return qfalse;
		}

		if ( skinningMode > 0 && data->num_poses > 0 && data->influences && surface->num_influences > 0 ) {
			useSkinning = qtrue;
			influenceVtxMat = (float *)ri.Malloc( surface->num_influences * 12 * sizeof( float ) );
			influenceNrmMat = (float *)ri.Malloc( surface->num_influences * 9 * sizeof( float ) );
			if ( !influenceVtxMat || !influenceNrmMat ) {
				if ( influenceVtxMat ) {
					ri.Free( influenceVtxMat );
					influenceVtxMat = NULL;
				}
				if ( influenceNrmMat ) {
					ri.Free( influenceNrmMat );
					influenceNrmMat = NULL;
				}
				useSkinning = qfalse;
			}

			for ( i = 0; useSkinning && i < surface->num_influences; i++ ) {
				int influence = surface->first_influence + i;
				float *vtxMat = &influenceVtxMat[12*i];
				float *nrmMat = &influenceNrmMat[9*i];
				float blendWeights[4];
				int j;

				if ( data->blendWeightsType == IQM_FLOAT ) {
					blendWeights[0] = data->influenceBlendWeights.f[4*influence + 0];
					blendWeights[1] = data->influenceBlendWeights.f[4*influence + 1];
					blendWeights[2] = data->influenceBlendWeights.f[4*influence + 2];
					blendWeights[3] = data->influenceBlendWeights.f[4*influence + 3];
				} else {
					blendWeights[0] = (float)data->influenceBlendWeights.b[4*influence + 0] / 255.0f;
					blendWeights[1] = (float)data->influenceBlendWeights.b[4*influence + 1] / 255.0f;
					blendWeights[2] = (float)data->influenceBlendWeights.b[4*influence + 2] / 255.0f;
					blendWeights[3] = (float)data->influenceBlendWeights.b[4*influence + 3] / 255.0f;
				}

				if ( blendWeights[0] <= 0.0f ) {
					Com_Memcpy( vtxMat, kIdentity34, sizeof( kIdentity34 ) );
				} else {
					vtxMat[0] = blendWeights[0] * poseMats[12 * data->influenceBlendIndexes[4*influence + 0] + 0];
					vtxMat[1] = blendWeights[0] * poseMats[12 * data->influenceBlendIndexes[4*influence + 0] + 1];
					vtxMat[2] = blendWeights[0] * poseMats[12 * data->influenceBlendIndexes[4*influence + 0] + 2];
					vtxMat[3] = blendWeights[0] * poseMats[12 * data->influenceBlendIndexes[4*influence + 0] + 3];
					vtxMat[4] = blendWeights[0] * poseMats[12 * data->influenceBlendIndexes[4*influence + 0] + 4];
					vtxMat[5] = blendWeights[0] * poseMats[12 * data->influenceBlendIndexes[4*influence + 0] + 5];
					vtxMat[6] = blendWeights[0] * poseMats[12 * data->influenceBlendIndexes[4*influence + 0] + 6];
					vtxMat[7] = blendWeights[0] * poseMats[12 * data->influenceBlendIndexes[4*influence + 0] + 7];
					vtxMat[8] = blendWeights[0] * poseMats[12 * data->influenceBlendIndexes[4*influence + 0] + 8];
					vtxMat[9] = blendWeights[0] * poseMats[12 * data->influenceBlendIndexes[4*influence + 0] + 9];
					vtxMat[10] = blendWeights[0] * poseMats[12 * data->influenceBlendIndexes[4*influence + 0] + 10];
					vtxMat[11] = blendWeights[0] * poseMats[12 * data->influenceBlendIndexes[4*influence + 0] + 11];

					for ( j = 1; j < 4; j++ ) {
						if ( blendWeights[j] <= 0.0f ) {
							break;
						}
						vtxMat[0] += blendWeights[j] * poseMats[12 * data->influenceBlendIndexes[4*influence + j] + 0];
						vtxMat[1] += blendWeights[j] * poseMats[12 * data->influenceBlendIndexes[4*influence + j] + 1];
						vtxMat[2] += blendWeights[j] * poseMats[12 * data->influenceBlendIndexes[4*influence + j] + 2];
						vtxMat[3] += blendWeights[j] * poseMats[12 * data->influenceBlendIndexes[4*influence + j] + 3];
						vtxMat[4] += blendWeights[j] * poseMats[12 * data->influenceBlendIndexes[4*influence + j] + 4];
						vtxMat[5] += blendWeights[j] * poseMats[12 * data->influenceBlendIndexes[4*influence + j] + 5];
						vtxMat[6] += blendWeights[j] * poseMats[12 * data->influenceBlendIndexes[4*influence + j] + 6];
						vtxMat[7] += blendWeights[j] * poseMats[12 * data->influenceBlendIndexes[4*influence + j] + 7];
						vtxMat[8] += blendWeights[j] * poseMats[12 * data->influenceBlendIndexes[4*influence + j] + 8];
						vtxMat[9] += blendWeights[j] * poseMats[12 * data->influenceBlendIndexes[4*influence + j] + 9];
						vtxMat[10] += blendWeights[j] * poseMats[12 * data->influenceBlendIndexes[4*influence + j] + 10];
						vtxMat[11] += blendWeights[j] * poseMats[12 * data->influenceBlendIndexes[4*influence + j] + 11];
					}
				}

				nrmMat[ 0] = vtxMat[ 5]*vtxMat[10] - vtxMat[ 6]*vtxMat[ 9];
				nrmMat[ 1] = vtxMat[ 6]*vtxMat[ 8] - vtxMat[ 4]*vtxMat[10];
				nrmMat[ 2] = vtxMat[ 4]*vtxMat[ 9] - vtxMat[ 5]*vtxMat[ 8];
				nrmMat[ 3] = vtxMat[ 2]*vtxMat[ 9] - vtxMat[ 1]*vtxMat[10];
				nrmMat[ 4] = vtxMat[ 0]*vtxMat[10] - vtxMat[ 2]*vtxMat[ 8];
				nrmMat[ 5] = vtxMat[ 1]*vtxMat[ 8] - vtxMat[ 0]*vtxMat[ 9];
				nrmMat[ 6] = vtxMat[ 1]*vtxMat[ 6] - vtxMat[ 2]*vtxMat[ 5];
				nrmMat[ 7] = vtxMat[ 2]*vtxMat[ 4] - vtxMat[ 0]*vtxMat[ 6];
				nrmMat[ 8] = vtxMat[ 0]*vtxMat[ 5] - vtxMat[ 1]*vtxMat[ 4];
			}
		}

		baseVertex = geometry->numVertices;
		for ( i = 0; i < surface->num_vertexes; i++ ) {
			const int vtx = surface->first_vertex + i;
			const float *xyz = &data->positions[vtx * 3];
			const float *normal = &data->normals[vtx * 3];
			const float *texCoord = &data->texcoords[vtx * 2];
			vec3_t localPos;
			vec3_t localNormal;
			vec3_t worldPos;
			vec3_t worldNormal;
			vec2_t uv;
			uint32_t color = 0xFFFFFFFFu;

			if ( useSkinning ) {
				int influence = data->influences[vtx] - surface->first_influence;
				float *vtxMat;
				float *nrmMat;

				if ( influence < 0 || influence >= surface->num_influences ) {
					influence = 0;
				}

				vtxMat = &influenceVtxMat[12*influence];
				nrmMat = &influenceNrmMat[9*influence];

				localPos[0] = vtxMat[0] * xyz[0] + vtxMat[1] * xyz[1] + vtxMat[2] * xyz[2] + vtxMat[3];
				localPos[1] = vtxMat[4] * xyz[0] + vtxMat[5] * xyz[1] + vtxMat[6] * xyz[2] + vtxMat[7];
				localPos[2] = vtxMat[8] * xyz[0] + vtxMat[9] * xyz[1] + vtxMat[10] * xyz[2] + vtxMat[11];

				localNormal[0] = nrmMat[0] * normal[0] + nrmMat[1] * normal[1] + nrmMat[2] * normal[2];
				localNormal[1] = nrmMat[3] * normal[0] + nrmMat[4] * normal[1] + nrmMat[5] * normal[2];
				localNormal[2] = nrmMat[6] * normal[0] + nrmMat[7] * normal[1] + nrmMat[8] * normal[2];
				VectorNormalize( localNormal );
			} else {
				localPos[0] = xyz[0];
				localPos[1] = xyz[1];
				localPos[2] = xyz[2];
				localNormal[0] = normal[0];
				localNormal[1] = normal[1];
				localNormal[2] = normal[2];
			}

			vk_rt_transform_point( ent, localPos, worldPos );
			vk_rt_transform_direction( ent, localNormal, worldNormal );
			uv[0] = texCoord[0];
			uv[1] = texCoord[1];

			if ( data->colors ) {
				color = vk_rt_color_u32_from_bytes( &data->colors[vtx * 4] );
			}

			if ( !vk_rt_cpu_geometry_add_vertex( geometry, worldPos, worldNormal, tangent, uv, color, materialIndex, NULL ) ) {
				if ( influenceVtxMat ) {
					ri.Free( influenceVtxMat );
				}
				if ( influenceNrmMat ) {
					ri.Free( influenceNrmMat );
				}
				return qfalse;
			}
		}

		for ( i = 0; i < surface->num_triangles; i++ ) {
			const int *tri = &data->triangles[3 * ( surface->first_triangle + i )];
			int i0 = tri[0] - surface->first_vertex;
			int i1 = tri[1] - surface->first_vertex;
			int i2 = tri[2] - surface->first_vertex;

			if ( i0 < 0 || i0 >= surface->num_vertexes ||
				i1 < 0 || i1 >= surface->num_vertexes ||
				i2 < 0 || i2 >= surface->num_vertexes ) {
				continue;
			}

			if ( !vk_rt_cpu_geometry_add_triangle( geometry,
				baseVertex + (uint32_t)i0,
				baseVertex + (uint32_t)i1,
				baseVertex + (uint32_t)i2 ) ) {
				if ( influenceVtxMat ) {
					ri.Free( influenceVtxMat );
				}
				if ( influenceNrmMat ) {
					ri.Free( influenceNrmMat );
				}
				return qfalse;
			}
			validTriangleCount++;
		}

		if ( influenceVtxMat ) {
			ri.Free( influenceVtxMat );
		}
		if ( influenceNrmMat ) {
			ri.Free( influenceNrmMat );
		}

		geometry->sourceTriangleCount += validTriangleCount;
		if ( geometry->materials[ materialIndex ].flags & RTX_RT_MATFLAG_MASKED ) {
			geometry->maskedTriangleCount += validTriangleCount;
		}
		geometry->sourceEntityCount++;
	}

	return qtrue;
}

static qboolean vk_rt_append_dynamic_model_entity_geometry( rtxRtCpuGeometry_t *geometry, const trRefEntity_t *ent, uint32_t *outModelCount )
{
	const model_t *model;
	qboolean counted = qfalse;

	if ( !geometry || !ent || ent->e.reType != RT_MODEL || ent->e.hModel <= 0 ) {
		return qtrue;
	}

	model = R_GetModelByHandle( ent->e.hModel );
	if ( !model ) {
		return qtrue;
	}

	switch ( model->type ) {
	case MOD_MESH:
		if ( !vk_rt_append_md3_entity_geometry( geometry, ent ) ) {
			return qfalse;
		}
		counted = qtrue;
		break;
	case MOD_IQM:
		if ( !vk_rt_append_iqm_entity_geometry( geometry, ent ) ) {
			return qfalse;
		}
		counted = qtrue;
		break;
	case MOD_BRUSH:
		if ( !vk_rt_append_brush_entity_geometry( geometry, ent, model ) ) {
			return qfalse;
		}
		counted = qtrue;
		break;
	case MOD_MDR:
		if ( !vk_rt_append_mdr_proxy_geometry( geometry, ent, tr.defaultShader ) ) {
			return qfalse;
		}
		counted = qtrue;
		break;
	default:
		break;
	}

	if ( counted && outModelCount ) {
		*outModelCount += 1u;
	}

	return qtrue;
}

static qboolean vk_rt_poly_is_mark_decal( const srfPoly_t *poly )
{
	const shader_t *shader;

	if ( !poly ) {
		return qfalse;
	}

	shader = poly->hShader ? R_GetShaderByHandle( poly->hShader ) : NULL;
	if ( !shader ) {
		return qfalse;
	}

	if ( shader->sort == SS_DECAL ) {
		return qtrue;
	}
	if ( !rtx_rt_material_heuristics || !rtx_rt_material_heuristics->integer ) {
		return qfalse;
	}

	if ( vk_rt_material_name_contains_token( shader->name, "decal" ) ||
		vk_rt_material_name_contains_token( shader->name, "mark" ) ||
		vk_rt_material_name_contains_token( shader->name, "bullet" ) ||
		vk_rt_material_name_contains_token( shader->name, "scorch" ) ||
		vk_rt_material_name_contains_token( shader->name, "impact" ) ) {
		return qtrue;
	}

	return qfalse;
}

static qboolean vk_rt_extract_dynamic_geometry( rtxRtCpuGeometry_t *geometry )
{
	uint32_t effectMode = ( rtx_rt_dynamic_effects ? (uint32_t)MAX( 0, rtx_rt_dynamic_effects->integer ) : 1u );
	uint32_t entityBudget = (uint32_t)MAX( 1, vk_rt_as_instance_budget() );
	qboolean prioritizeFirstPerson = vk_rt_first_person_priority_enabled();
	qboolean allowMarks = vk_rt_marks_enabled();
	uint32_t modelCount = 0;
	uint32_t effectCount = 0;
	int modelPasses = prioritizeFirstPerson ? 2 : 1;
	int pass;
	int i;

	vk_rt_cpu_geometry_init( geometry );

	if ( !backEnd.refdef.entities && !( backEnd.refdef.polys && ( effectMode >= 2 || allowMarks ) ) ) {
		return qtrue;
	}

	for ( pass = 0; pass < modelPasses; pass++ ) {
		for ( i = 0; i < backEnd.refdef.num_entities; i++ ) {
			const trRefEntity_t *ent = &backEnd.refdef.entities[i];
			qboolean isFirstPerson;

			if ( ent->e.reType != RT_MODEL ) {
				continue;
			}
			if ( !vk_rt_entity_visible_in_current_view( ent ) ) {
				continue;
			}

			isFirstPerson = ( ent->e.renderfx & RF_FIRST_PERSON ) ? qtrue : qfalse;
			if ( prioritizeFirstPerson ) {
				if ( pass == 0 && !isFirstPerson ) {
					continue;
				}
				if ( pass == 1 && isFirstPerson ) {
					continue;
				}
			}

			if ( modelCount >= entityBudget ) {
				continue;
			}

			if ( !vk_rt_append_dynamic_model_entity_geometry( geometry, ent, &modelCount ) ) {
				vk_rt_cpu_geometry_free( geometry );
				return qfalse;
			}
		}
	}

	if ( effectMode > 0 ) {
		for ( i = 0; i < backEnd.refdef.num_entities; i++ ) {
			const trRefEntity_t *ent = &backEnd.refdef.entities[i];

			if ( !vk_rt_entity_visible_in_current_view( ent ) ) {
				continue;
			}

			switch ( ent->e.reType ) {
			case RT_SPRITE:
				if ( !vk_rt_append_sprite_entity_geometry( geometry, ent ) ) {
					vk_rt_cpu_geometry_free( geometry );
					return qfalse;
				}
				effectCount++;
				break;
			case RT_BEAM:
			case RT_RAIL_CORE:
			case RT_RAIL_RINGS:
			case RT_LIGHTNING:
				if ( !vk_rt_append_beam_entity_geometry( geometry, ent ) ) {
					vk_rt_cpu_geometry_free( geometry );
					return qfalse;
				}
				effectCount++;
				break;
			default:
				break;
			}
		}
	}

	if ( backEnd.refdef.polys && ( effectMode >= 2 || allowMarks ) ) {
		for ( i = 0; i < backEnd.refdef.numPolys; i++ ) {
			const srfPoly_t *poly = &backEnd.refdef.polys[i];
			if ( effectMode < 2 && !vk_rt_poly_is_mark_decal( poly ) ) {
				continue;
			}
			if ( !vk_rt_append_poly_geometry( geometry, poly ) ) {
				vk_rt_cpu_geometry_free( geometry );
				return qfalse;
			}
			effectCount++;
		}
	}

	geometry->sourceEntityCount = modelCount;
	geometry->sourceEffectCount = effectCount;

	/*
	 * Dynamic entities remain authored raster overlays.  Their RT geometry is
	 * retained for shadow/visibility rays, while the any-hit shader rejects it
	 * from primary camera rays so it cannot replace animated raster shading.
	 */
	for ( i = 0; i < (int)geometry->numMaterials; i++ ) {
		geometry->materials[i].flags |= RTX_RT_MATFLAG_RASTER_OVERLAY;
	}
	geometry->anyHitTriangleCount = geometry->sourceTriangleCount;

	if ( geometry->numVertices >= 3 && geometry->numIndices >= 3 ) {
		if ( !vk_rt_generate_tangents( geometry ) ) {
			vk_rt_cpu_geometry_free( geometry );
			return qfalse;
		}
	}

	return qtrue;
}

static qboolean vk_rt_build_dynamic_scene_blas( VkDeviceSize *frameBudgetBytesUsed, VkDeviceSize frameBudgetBytes, VkDeviceSize *outBuildBytes )
{
	rtxRtCpuGeometry_t geometry;
	VkBuildAccelerationStructureFlagsKHR flags;
	VkDeviceSize buildBytes;
	uint32_t droppedTriangles = 0;
	vec3_t mins;
	vec3_t maxs;
	uint32_t v;

	if ( outBuildBytes ) {
		*outBuildBytes = 0;
	}

	if ( !vk_rt_extract_dynamic_geometry( &geometry ) ) {
		return qfalse;
	}

	if ( !vk_rt_cpu_geometry_sanitize_indices( &geometry, &droppedTriangles ) ) {
		vk_rt_cpu_geometry_free( &geometry );
		return qfalse;
	}
	if ( droppedTriangles > 0 && vk_rt_debug_as_stats_level() >= 1 ) {
		ri.Printf( PRINT_WARNING,
			"RTX RT: dropped %u invalid/degenerate dynamic triangles before BLAS build\n",
			droppedTriangles );
	}

	if ( geometry.numVertices > 0 ) {
		VectorSet( mins, geometry.vertices[0].xyz[0], geometry.vertices[0].xyz[1], geometry.vertices[0].xyz[2] );
		VectorCopy( mins, maxs );
		for ( v = 1; v < geometry.numVertices; v++ ) {
			const float *p = geometry.vertices[v].xyz;
			mins[0] = MIN( mins[0], p[0] );
			mins[1] = MIN( mins[1], p[1] );
			mins[2] = MIN( mins[2], p[2] );
			maxs[0] = MAX( maxs[0], p[0] );
			maxs[1] = MAX( maxs[1], p[1] );
			maxs[2] = MAX( maxs[2], p[2] );
		}
		VectorAdd( mins, maxs, vk.rt.dynamic_centroid_current );
		VectorScale( vk.rt.dynamic_centroid_current, 0.5f, vk.rt.dynamic_centroid_current );
	} else {
		VectorCopy( vk.rt.dynamic_centroid_prev, vk.rt.dynamic_centroid_current );
	}

	if ( geometry.numVertices < 3 || geometry.numIndices < 3 ) {
		if ( vk.rt.dynamic_blas.valid ||
			vk.rt.dynamic_vertex_buffer.buffer != VK_NULL_HANDLE ||
			vk.rt.dynamic_index_buffer.buffer != VK_NULL_HANDLE ||
			vk.rt.dynamic_material_buffer.buffer != VK_NULL_HANDLE ) {
			if ( !vk_rt_wait_for_inflight_frames( "dynamic geometry cleanup" ) ) {
				vk_rt_cpu_geometry_free( &geometry );
				return qfalse;
			}
		}
		vk_rt_destroy_as( &vk.rt.dynamic_blas );
		vk_rt_destroy_buffer( &vk.rt.dynamic_vertex_buffer );
		vk_rt_destroy_buffer( &vk.rt.dynamic_index_buffer );
		vk_rt_destroy_buffer( &vk.rt.dynamic_material_buffer );
		vk.rt.dynamic_vertex_count = 0;
		vk.rt.dynamic_index_count = 0;
		vk.rt.dynamic_material_count = 0;
		vk.rt.dynamic_entity_count = geometry.sourceEntityCount;
		vk.rt.dynamic_effect_count = geometry.sourceEffectCount;
		vk.rt.dynamic_masked_triangle_count = 0;
		vk.rt.stats.dynamic_triangles = 0;
		vk.rt.stats.masked_triangles = vk.rt.world_masked_triangle_count;
		vk.rt.needsRebuild = qtrue;
		vk_rt_cpu_geometry_free( &geometry );
		return qtrue;
	}

	// Dynamic BLAS is rebuilt frequently; skip compaction to avoid query/copy overhead and instability.
	flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	buildBytes = vk_rt_query_triangles_build_bytes(
		geometry.numVertices,
		geometry.numIndices,
		geometry.anyHitTriangleCount == 0u ? qtrue : qfalse,
		flags );
	if ( outBuildBytes ) {
		*outBuildBytes = buildBytes;
	}
	if ( !vk_rt_budget_allows( buildBytes, frameBudgetBytesUsed, frameBudgetBytes ) ) {
		vk_rt_cpu_geometry_free( &geometry );
		return qfalse;
	}

	/*
	 * The dynamic buffers are host-visible and may still be consumed by an
	 * in-flight frame.  Wait before writing them, not merely before replacing
	 * the old BLAS after the upload has already happened.
	 */
	if ( vk.rt.dynamic_blas.valid &&
		!vk_rt_wait_for_inflight_frames( "dynamic geometry upload" ) ) {
		vk_rt_cpu_geometry_free( &geometry );
		return qfalse;
	}

	if ( !vk_rt_ensure_buffer_capacity(
		&vk.rt.dynamic_vertex_buffer,
		geometry.numVertices * sizeof( rtxRtPackedVertex_t ),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		qtrue,
		"rt dynamic vertices" ) ||
		!vk_rt_ensure_buffer_capacity(
			&vk.rt.dynamic_index_buffer,
			geometry.numIndices * sizeof( uint32_t ),
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
				VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
				VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			qtrue,
			"rt dynamic indices" ) ) {
		vk_rt_cpu_geometry_free( &geometry );
		return qfalse;
	}

	if ( !vk_rt_upload_buffer_data(
		&vk.rt.dynamic_vertex_buffer,
		geometry.vertices,
		(size_t)geometry.numVertices * sizeof( rtxRtPackedVertex_t ) ) ||
		!vk_rt_upload_buffer_data(
			&vk.rt.dynamic_index_buffer,
			geometry.indices,
			(size_t)geometry.numIndices * sizeof( uint32_t ) ) ) {
		vk_rt_cpu_geometry_free( &geometry );
		return qfalse;
	}

	if ( !vk_rt_upload_material_buffer(
		geometry.materials,
		geometry.numMaterials,
		&vk.rt.dynamic_material_buffer,
		&vk.rt.dynamic_material_count,
		"rt dynamic materials" ) ) {
		vk_rt_cpu_geometry_free( &geometry );
		return qfalse;
	}

	vk.rt.dynamic_vertex_count = geometry.numVertices;
	vk.rt.dynamic_index_count = geometry.numIndices;
	vk.rt.dynamic_entity_count = geometry.sourceEntityCount;
	vk.rt.dynamic_effect_count = geometry.sourceEffectCount;
	vk.rt.dynamic_masked_triangle_count = geometry.maskedTriangleCount;
	vk.rt.stats.dynamic_triangles = geometry.sourceTriangleCount;
	vk.rt.stats.masked_triangles =
		vk.rt.world_masked_triangle_count +
		vk.rt.dynamic_masked_triangle_count;

	if ( !vk_rt_build_triangles_blas(
		&vk.rt.dynamic_blas,
		&vk.rt.dynamic_vertex_buffer,
		vk.rt.dynamic_vertex_count,
		sizeof( rtxRtPackedVertex_t ),
		&vk.rt.dynamic_index_buffer,
		vk.rt.dynamic_index_count,
		geometry.anyHitTriangleCount == 0u ? qtrue : qfalse,
		qfalse,
		"rt dynamic scene blas" ) ) {
		vk_rt_cpu_geometry_free( &geometry );
		return qfalse;
	}

	vk.rt.stats.dynamic_build_bytes += (uint64_t)( vk.rt.dynamic_blas.size + vk.rt.scratch_buffer.allocationSize );
	vk.rt.needsRebuild = qtrue;

	if ( vk_rt_debug_as_stats_level() >= 1 ) {
		ri.Printf( PRINT_DEVELOPER,
			"RTX RT: dynamic extraction complete (entities=%u, effects=%u, vertices=%u, indices=%u, triangles=%u, materials=%u, maskedTriangles=%u, anyHitTriangles=%u, degenerate=%u)\n",
			geometry.sourceEntityCount,
			geometry.sourceEffectCount,
			geometry.numVertices,
			geometry.numIndices,
			geometry.sourceTriangleCount,
			geometry.numMaterials,
			geometry.maskedTriangleCount,
			geometry.anyHitTriangleCount,
			geometry.degenerateTriangleCount );
	}

	if ( rtx_rt_material_debug && rtx_rt_material_debug->integer >= 3 ) {
		uint32_t maxPrint = MIN( geometry.numMaterials, 24u );
		uint32_t m;
		for ( m = 0; m < maxPrint; m++ ) {
			const rtxRtMaterial_t *mat = &geometry.materials[m];
			ri.Printf( PRINT_DEVELOPER,
				"RTX RT dynamic material[%u] '%s': flags=0x%x rm=(%.2f %.2f) emissiveScale=%.2f alphaCutoff=%.2f\n",
				m,
				mat->name,
				mat->flags,
				mat->roughness,
				mat->metallic,
				mat->emissiveScale,
				mat->alphaCutoff );
		}
	}

	vk_rt_cpu_geometry_free( &geometry );
	return qtrue;
}

static qboolean vk_rt_ensure_descriptor_resources( void )
{
	VkDescriptorPoolSize poolSizes[4];
	VkDescriptorPoolCreateInfo poolInfo;
	VkDescriptorSetLayoutBinding bindings[14];
	VkDescriptorSetLayoutCreateInfo layoutInfo;
	VkPipelineLayoutCreateInfo pipelineLayoutInfo;
	VkDescriptorSetAllocateInfo allocInfo;
	VkDescriptorSetLayout descriptorLayouts[ NUM_COMMAND_BUFFERS ];
	uint32_t i;
	VkResult result;

	if ( vk.rt.descriptor_set_layout != VK_NULL_HANDLE &&
		vk.rt.descriptor_pool != VK_NULL_HANDLE &&
		vk.rt.pipeline_layout != VK_NULL_HANDLE ) {
		for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
			if ( vk.rt.descriptor_set[i] == VK_NULL_HANDLE ) {
				break;
			}
		}
		if ( i == NUM_COMMAND_BUFFERS ) {
			return qtrue;
		}
	}

	if ( vk.rt.descriptor_set_layout != VK_NULL_HANDLE ) {
		qvkDestroyDescriptorSetLayout( vk.device, vk.rt.descriptor_set_layout, NULL );
		vk.rt.descriptor_set_layout = VK_NULL_HANDLE;
	}
	if ( vk.rt.descriptor_pool != VK_NULL_HANDLE ) {
		qvkDestroyDescriptorPool( vk.device, vk.rt.descriptor_pool, NULL );
		vk.rt.descriptor_pool = VK_NULL_HANDLE;
	}
	if ( vk.rt.pipeline_layout != VK_NULL_HANDLE ) {
		qvkDestroyPipelineLayout( vk.device, vk.rt.pipeline_layout, NULL );
		vk.rt.pipeline_layout = VK_NULL_HANDLE;
	}
	Com_Memset( vk.rt.descriptor_set, 0, sizeof( vk.rt.descriptor_set ) );

	poolSizes[0].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	poolSizes[0].descriptorCount = NUM_COMMAND_BUFFERS;
	poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	poolSizes[1].descriptorCount = NUM_COMMAND_BUFFERS * 3;
	poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	poolSizes[2].descriptorCount = NUM_COMMAND_BUFFERS * 8;
	poolSizes[3].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[3].descriptorCount = NUM_COMMAND_BUFFERS * ( 1 + RTX_RT_MAX_SCENE_TEXTURES );

	Com_Memset( &poolInfo, 0, sizeof( poolInfo ) );
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.maxSets = NUM_COMMAND_BUFFERS;
	poolInfo.poolSizeCount = ARRAY_LEN( poolSizes );
	poolInfo.pPoolSizes = poolSizes;
	result = qvkCreateDescriptorPool( vk.device, &poolInfo, NULL, &vk.rt.descriptor_pool );
	if ( result != VK_SUCCESS ) {
		ri.Printf( PRINT_ERROR, "RTX RT: failed to create descriptor pool: %s\n", vk_result_string( result ) );
		return qfalse;
	}

	Com_Memset( bindings, 0, sizeof( bindings ) );
	bindings[0].binding = RTX_RT_DESC_BINDING_TLAS;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
	bindings[1].binding = RTX_RT_DESC_BINDING_OUTPUT;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	bindings[1].descriptorCount = 1;
	bindings[1].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
	bindings[2].binding = RTX_RT_DESC_BINDING_WORLD_VERTICES;
	bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	bindings[2].descriptorCount = 1;
	bindings[2].stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
	bindings[3].binding = RTX_RT_DESC_BINDING_WORLD_INDICES;
	bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	bindings[3].descriptorCount = 1;
	bindings[3].stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
	bindings[4].binding = RTX_RT_DESC_BINDING_WORLD_MATERIALS;
	bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	bindings[4].descriptorCount = 1;
	bindings[4].stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
	bindings[5].binding = RTX_RT_DESC_BINDING_DYNAMIC_VERTICES;
	bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	bindings[5].descriptorCount = 1;
	bindings[5].stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
	bindings[6].binding = RTX_RT_DESC_BINDING_DYNAMIC_INDICES;
	bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	bindings[6].descriptorCount = 1;
	bindings[6].stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
	bindings[7].binding = RTX_RT_DESC_BINDING_DYNAMIC_MATERIALS;
	bindings[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	bindings[7].descriptorCount = 1;
	bindings[7].stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
	bindings[8].binding = RTX_RT_DESC_BINDING_LIGHTS;
	bindings[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	bindings[8].descriptorCount = 1;
	bindings[8].stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_RAYGEN_BIT_KHR;
	bindings[9].binding = RTX_RT_DESC_BINDING_HISTORY_IN;
	bindings[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	bindings[9].descriptorCount = 1;
	bindings[9].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
	bindings[10].binding = RTX_RT_DESC_BINDING_HISTORY_OUT;
	bindings[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	bindings[10].descriptorCount = 1;
	bindings[10].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
	bindings[11].binding = RTX_RT_DESC_BINDING_TEMPORAL_PARAMS;
	bindings[11].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	bindings[11].descriptorCount = 1;
	bindings[11].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
	bindings[12].binding = RTX_RT_DESC_BINDING_SCENE_COLOR;
	bindings[12].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[12].descriptorCount = 1;
	bindings[12].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
	bindings[13].binding = RTX_RT_DESC_BINDING_SCENE_TEXTURES;
	bindings[13].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[13].descriptorCount = RTX_RT_MAX_SCENE_TEXTURES;
	bindings[13].stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR;

	Com_Memset( &layoutInfo, 0, sizeof( layoutInfo ) );
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = ARRAY_LEN( bindings );
	layoutInfo.pBindings = bindings;
	result = qvkCreateDescriptorSetLayout( vk.device, &layoutInfo, NULL, &vk.rt.descriptor_set_layout );
	if ( result != VK_SUCCESS ) {
		ri.Printf( PRINT_ERROR, "RTX RT: failed to create descriptor set layout: %s\n", vk_result_string( result ) );
		return qfalse;
	}

	{
		VkPushConstantRange pushRange;
		pushRange.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR |
			VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
		pushRange.offset = 0;
		pushRange.size = sizeof( rtxRtPushConstants_t );

		Com_Memset( &pipelineLayoutInfo, 0, sizeof( pipelineLayoutInfo ) );
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = &vk.rt.descriptor_set_layout;
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = &pushRange;
		result = qvkCreatePipelineLayout( vk.device, &pipelineLayoutInfo, NULL, &vk.rt.pipeline_layout );
		if ( result != VK_SUCCESS ) {
			ri.Printf( PRINT_ERROR, "RTX RT: failed to create pipeline layout: %s\n", vk_result_string( result ) );
			return qfalse;
		}
	}

	Com_Memset( &allocInfo, 0, sizeof( allocInfo ) );
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = vk.rt.descriptor_pool;
	allocInfo.descriptorSetCount = NUM_COMMAND_BUFFERS;
	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		descriptorLayouts[i] = vk.rt.descriptor_set_layout;
	}
	allocInfo.pSetLayouts = descriptorLayouts;
	result = qvkAllocateDescriptorSets( vk.device, &allocInfo, vk.rt.descriptor_set );
	if ( result != VK_SUCCESS ) {
		ri.Printf( PRINT_ERROR, "RTX RT: failed to allocate RT descriptor sets: %s\n", vk_result_string( result ) );
		return qfalse;
	}

	return qtrue;
}

static void vk_rt_destroy_output_image( void )
{
	int i;
	for ( i = 0; i < 2; i++ ) {
		if ( vk.rt.history_view[i] != VK_NULL_HANDLE ) {
			qvkDestroyImageView( vk.device, vk.rt.history_view[i], NULL );
			vk.rt.history_view[i] = VK_NULL_HANDLE;
		}
		if ( vk.rt.history_image[i] != VK_NULL_HANDLE ) {
			qvkDestroyImage( vk.device, vk.rt.history_image[i], NULL );
			vk.rt.history_image[i] = VK_NULL_HANDLE;
		}
		if ( vk.rt.history_memory[i] != VK_NULL_HANDLE ) {
			qvkFreeMemory( vk.device, vk.rt.history_memory[i], NULL );
			vk.rt.history_memory[i] = VK_NULL_HANDLE;
		}
	}
	if ( vk.rt.output_view != VK_NULL_HANDLE ) {
		qvkDestroyImageView( vk.device, vk.rt.output_view, NULL );
		vk.rt.output_view = VK_NULL_HANDLE;
	}
	if ( vk.rt.output_image != VK_NULL_HANDLE ) {
		qvkDestroyImage( vk.device, vk.rt.output_image, NULL );
		vk.rt.output_image = VK_NULL_HANDLE;
	}
	if ( vk.rt.output_memory != VK_NULL_HANDLE ) {
		qvkFreeMemory( vk.device, vk.rt.output_memory, NULL );
		vk.rt.output_memory = VK_NULL_HANDLE;
	}
	vk.rt.output_width = 0;
	vk.rt.output_height = 0;
	vk.rt.history_read_index = 0;
	vk.rt.history_valid = qfalse;
	vk.rt.temporal_reset_pending = qtrue;
}

static qboolean vk_rt_ensure_output_image( void )
{
	VkImageCreateInfo imageInfo;
	VkMemoryRequirements memoryRequirements;
	VkMemoryAllocateInfo allocInfo;
	VkImageViewCreateInfo viewInfo;
	VkCommandBuffer cmd;
	VkImageMemoryBarrier barriers[3];
	VkImage *images[3] = {
		&vk.rt.output_image,
		&vk.rt.history_image[0],
		&vk.rt.history_image[1]
	};
	VkImageView *views[3] = {
		&vk.rt.output_view,
		&vk.rt.history_view[0],
		&vk.rt.history_view[1]
	};
	VkDeviceMemory *memories[3] = {
		&vk.rt.output_memory,
		&vk.rt.history_memory[0],
		&vk.rt.history_memory[1]
	};
	const VkImageUsageFlags usages[3] = {
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		VK_IMAGE_USAGE_STORAGE_BIT,
		VK_IMAGE_USAGE_STORAGE_BIT
	};
	VkResult result;
	float resolutionScale = vk.rt.dynamic_resolution_scale;
	uint32_t baseWidth = (uint32_t)MAX( 1, glConfig.vidWidth );
	uint32_t baseHeight = (uint32_t)MAX( 1, glConfig.vidHeight );
	uint32_t width;
	uint32_t height;
	int i;

	if ( vk_rt_dynamic_resolution_enabled() ) {
		float minScale = vk_rt_dynamic_resolution_min_scale();
		float maxScale = vk_rt_dynamic_resolution_max_scale();
		if ( minScale > maxScale ) {
			float tmp = minScale;
			minScale = maxScale;
			maxScale = tmp;
		}
		resolutionScale = Com_Clamp( minScale, maxScale, resolutionScale );
	} else {
		resolutionScale = 1.0f;
	}

	width = (uint32_t)MAX( 1, (int)floorf( (float)baseWidth * resolutionScale + 0.5f ) );
	height = (uint32_t)MAX( 1, (int)floorf( (float)baseHeight * resolutionScale + 0.5f ) );
	vk.rt.dynamic_resolution_scale = MIN( (float)width / (float)baseWidth, (float)height / (float)baseHeight );

	if ( vk.rt.output_image != VK_NULL_HANDLE &&
		vk.rt.history_image[0] != VK_NULL_HANDLE &&
		vk.rt.history_image[1] != VK_NULL_HANDLE &&
		vk.rt.output_width == width &&
		vk.rt.output_height == height ) {
		return qtrue;
	}

	if ( vk.rt.output_image != VK_NULL_HANDLE ||
		vk.rt.history_image[0] != VK_NULL_HANDLE ||
		vk.rt.history_image[1] != VK_NULL_HANDLE ) {
		if ( !vk_rt_wait_for_inflight_frames( "RT output resize" ) ) {
			return qfalse;
		}
	}

	vk_rt_destroy_output_image();
	vk.rt.descriptorsValid = qfalse;
	vk.rt.dispatchValid = qfalse;

	for ( i = 0; i < 3; i++ ) {
		Com_Memset( &imageInfo, 0, sizeof( imageInfo ) );
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent.width = width;
		imageInfo.extent.height = height;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = vk.rt.output_format;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = usages[i];
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		result = qvkCreateImage( vk.device, &imageInfo, NULL, images[i] );
		if ( result != VK_SUCCESS ) {
			ri.Printf( PRINT_ERROR, "RTX RT: failed to create RT storage image %d: %s\n", i, vk_result_string( result ) );
			vk_rt_destroy_output_image();
			return qfalse;
		}

		qvkGetImageMemoryRequirements( vk.device, *images[i], &memoryRequirements );

		Com_Memset( &allocInfo, 0, sizeof( allocInfo ) );
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memoryRequirements.size;
		allocInfo.memoryTypeIndex = find_memory_type( memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
		result = qvkAllocateMemory( vk.device, &allocInfo, NULL, memories[i] );
		if ( result != VK_SUCCESS ) {
			ri.Printf( PRINT_ERROR, "RTX RT: failed to allocate RT storage image memory %d: %s\n", i, vk_result_string( result ) );
			vk_rt_destroy_output_image();
			return qfalse;
		}

		result = qvkBindImageMemory( vk.device, *images[i], *memories[i], 0 );
		if ( result != VK_SUCCESS ) {
			ri.Printf( PRINT_ERROR, "RTX RT: failed to bind RT storage image memory %d: %s\n", i, vk_result_string( result ) );
			vk_rt_destroy_output_image();
			return qfalse;
		}

		Com_Memset( &viewInfo, 0, sizeof( viewInfo ) );
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = *images[i];
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = vk.rt.output_format;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;
		result = qvkCreateImageView( vk.device, &viewInfo, NULL, views[i] );
		if ( result != VK_SUCCESS ) {
			ri.Printf( PRINT_ERROR, "RTX RT: failed to create RT storage image view %d: %s\n", i, vk_result_string( result ) );
			vk_rt_destroy_output_image();
			return qfalse;
		}
	}

	cmd = begin_command_buffer();
	for ( i = 0; i < 3; i++ ) {
		Com_Memset( &barriers[i], 0, sizeof( barriers[i] ) );
		barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barriers[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barriers[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
		barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[i].image = *images[i];
		barriers[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barriers[i].subresourceRange.baseMipLevel = 0;
		barriers[i].subresourceRange.levelCount = 1;
		barriers[i].subresourceRange.baseArrayLayer = 0;
		barriers[i].subresourceRange.layerCount = 1;
		barriers[i].srcAccessMask = 0;
		barriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	}

	qvkCmdPipelineBarrier(
		cmd,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
		0,
		0, NULL,
		0, NULL,
		ARRAY_LEN( barriers ), barriers );
	if ( !end_command_buffer_soft( cmd, __func__ ) ) {
		vk_rt_destroy_output_image();
		return qfalse;
	}

	vk.rt.output_width = width;
	vk.rt.output_height = height;
	vk.rt.history_read_index = 0;
	vk.rt.history_valid = qfalse;
	vk.rt.temporal_reset_pending = qtrue;
	vk.rt.post_validation_logged = qfalse;

	return qtrue;
}

static qboolean vk_rt_build_sbt( void )
{
	const uint32_t groupCount = 4;
	const uint32_t handleSize = vk.rt.rt_pipeline_props.shaderGroupHandleSize;
	const uint32_t handleAlignment = vk.rt.rt_pipeline_props.shaderGroupHandleAlignment;
	const uint32_t groupBaseAlignment = vk.rt.rt_pipeline_props.shaderGroupBaseAlignment;
	const VkDeviceSize raygenOffset = 0;
	VkDeviceSize alignedHandleSize;
	VkDeviceSize missOffset;
	VkDeviceSize hitOffset;
	VkDeviceSize missSize;
	VkDeviceSize sbtSize;
	VkDeviceSize handlesBytes;
	VkDeviceSize nextOffset;
	byte *handles = NULL;
	size_t handlesSize;
	void *mapped = NULL;
	VkResult result;
	byte *dst;

	vk.rt.sbtValid = qfalse;
	if ( handleSize == 0 || !vk_rt_is_power_of_two( handleAlignment ) ||
		!vk_rt_is_power_of_two( groupBaseAlignment ) ) {
		ri.Printf( PRINT_ERROR,
			"RTX RT: invalid SBT handle layout (size=%u, alignment=%u, base=%u)\n",
			handleSize, handleAlignment, groupBaseAlignment );
		return qfalse;
	}

	if ( !vk_rt_align_up_checked( handleSize, handleAlignment, &alignedHandleSize ) ||
		alignedHandleSize == 0 ||
		alignedHandleSize > vk.rt.rt_pipeline_props.maxShaderGroupStride ||
		!vk_rt_device_size_add_checked( raygenOffset, alignedHandleSize, &nextOffset ) ||
		!vk_rt_align_up_checked( nextOffset, groupBaseAlignment, &missOffset ) ||
		!vk_rt_device_size_multiply_checked( alignedHandleSize, 2, &missSize ) ||
		!vk_rt_device_size_add_checked( missOffset, missSize, &nextOffset ) ||
		!vk_rt_align_up_checked( nextOffset, groupBaseAlignment, &hitOffset ) ||
		!vk_rt_device_size_add_checked( hitOffset, alignedHandleSize, &sbtSize ) ||
		!vk_rt_device_size_multiply_checked( handleSize, groupCount, &handlesBytes ) ||
		handlesBytes == 0 || handlesBytes > INT_MAX || sbtSize > SIZE_MAX ) {
		ri.Printf( PRINT_ERROR, "RTX RT: unsafe or unsupported SBT layout reported by the device\n" );
		return qfalse;
	}
	handlesSize = (size_t)handlesBytes;

	handles = (byte *)ri.Malloc( handlesSize );
	if ( !handles ) {
		ri.Printf( PRINT_ERROR, "RTX RT: failed to allocate temporary shader group handle buffer\n" );
		return qfalse;
	}
	Com_Memset( handles, 0, handlesSize );
	result = qvkGetRayTracingShaderGroupHandlesKHR(
		vk.device,
		vk.rt.pipeline,
		0,
		groupCount,
		handlesSize,
		handles );
	if ( result != VK_SUCCESS ) {
		ri.Printf( PRINT_ERROR, "RTX RT: failed to get RT shader group handles: %s\n", vk_result_string( result ) );
		ri.Free( handles );
		return qfalse;
	}

	vk_rt_destroy_buffer( &vk.rt.sbt_buffer );
	if ( !vk_rt_create_buffer(
		sbtSize,
		VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		qtrue,
		"rt sbt",
		&vk.rt.sbt_buffer ) ) {
		ri.Free( handles );
		return qfalse;
	}

	result = qvkMapMemory( vk.device, vk.rt.sbt_buffer.memory, 0, sbtSize, 0, &mapped );
	if ( result != VK_SUCCESS ) {
		ri.Printf( PRINT_ERROR, "RTX RT: failed to map SBT buffer: %s\n", vk_result_string( result ) );
		ri.Free( handles );
		vk_rt_destroy_buffer( &vk.rt.sbt_buffer );
		return qfalse;
	}

	Com_Memset( mapped, 0, (size_t)sbtSize );
	dst = (byte *)mapped;
	Com_Memcpy( dst + (size_t)raygenOffset, handles, handleSize );
	Com_Memcpy( dst + (size_t)missOffset, handles + handleSize, handleSize );
	Com_Memcpy( dst + (size_t)( missOffset + alignedHandleSize ), handles + ( handleSize * 2u ), handleSize );
	Com_Memcpy( dst + (size_t)hitOffset, handles + ( handleSize * 3u ), handleSize );
	qvkUnmapMemory( vk.device, vk.rt.sbt_buffer.memory );
	ri.Free( handles );

	if ( vk.rt.sbt_buffer.address == 0 ||
		( vk.rt.sbt_buffer.address % groupBaseAlignment ) != 0 ) {
		ri.Printf( PRINT_ERROR,
			"RTX RT: SBT buffer address does not satisfy base alignment %u\n",
			groupBaseAlignment );
		vk_rt_destroy_buffer( &vk.rt.sbt_buffer );
		return qfalse;
	}

	vk.rt.sbt_raygen.deviceAddress = vk.rt.sbt_buffer.address + raygenOffset;
	vk.rt.sbt_raygen.stride = alignedHandleSize;
	vk.rt.sbt_raygen.size = alignedHandleSize;

	vk.rt.sbt_miss.deviceAddress = vk.rt.sbt_buffer.address + missOffset;
	vk.rt.sbt_miss.stride = alignedHandleSize;
	vk.rt.sbt_miss.size = missSize;

	vk.rt.sbt_hit.deviceAddress = vk.rt.sbt_buffer.address + hitOffset;
	vk.rt.sbt_hit.stride = alignedHandleSize;
	vk.rt.sbt_hit.size = alignedHandleSize;

	Com_Memset( &vk.rt.sbt_callable, 0, sizeof( vk.rt.sbt_callable ) );
	vk.rt.sbtValid = qtrue;
	return qtrue;
}

static qboolean vk_rt_ensure_pipeline( void )
{
	VkPipelineShaderStageCreateInfo stages[5];
	VkRayTracingShaderGroupCreateInfoKHR groups[4];
	VkRayTracingPipelineCreateInfoKHR pipelineInfo;
	VkResult result;

	if ( vk.rt.pipeline != VK_NULL_HANDLE && vk.rt.sbtValid ) {
		return qtrue;
	}

	if ( !vk_rt_ensure_descriptor_resources() ) {
		return qfalse;
	}

	if ( vk.modules.rt_rgen == VK_NULL_HANDLE || vk.modules.rt_rmiss == VK_NULL_HANDLE ||
		vk.modules.rt_rmiss_shadow == VK_NULL_HANDLE || vk.modules.rt_rchit == VK_NULL_HANDLE ||
		vk.modules.rt_rahit == VK_NULL_HANDLE ) {
		ri.Printf( PRINT_ERROR, "RTX RT: ray tracing shader modules are missing\n" );
		return qfalse;
	}

	if ( vk.rt.pipeline != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.rt.pipeline, NULL );
		vk.rt.pipeline = VK_NULL_HANDLE;
	}
	vk.rt.sbtValid = qfalse;

	Com_Memset( stages, 0, sizeof( stages ) );
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
	stages[0].module = vk.modules.rt_rgen;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_MISS_BIT_KHR;
	stages[1].module = vk.modules.rt_rmiss;
	stages[1].pName = "main";
	stages[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[2].stage = VK_SHADER_STAGE_MISS_BIT_KHR;
	stages[2].module = vk.modules.rt_rmiss_shadow;
	stages[2].pName = "main";
	stages[3].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[3].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
	stages[3].module = vk.modules.rt_rchit;
	stages[3].pName = "main";
	stages[4].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[4].stage = VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
	stages[4].module = vk.modules.rt_rahit;
	stages[4].pName = "main";

	Com_Memset( groups, 0, sizeof( groups ) );
	groups[0].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
	groups[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
	groups[0].generalShader = 0;
	groups[0].closestHitShader = VK_SHADER_UNUSED_KHR;
	groups[0].anyHitShader = VK_SHADER_UNUSED_KHR;
	groups[0].intersectionShader = VK_SHADER_UNUSED_KHR;

	groups[1].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
	groups[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
	groups[1].generalShader = 1;
	groups[1].closestHitShader = VK_SHADER_UNUSED_KHR;
	groups[1].anyHitShader = VK_SHADER_UNUSED_KHR;
	groups[1].intersectionShader = VK_SHADER_UNUSED_KHR;

	groups[2].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
	groups[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
	groups[2].generalShader = 2;
	groups[2].closestHitShader = VK_SHADER_UNUSED_KHR;
	groups[2].anyHitShader = VK_SHADER_UNUSED_KHR;
	groups[2].intersectionShader = VK_SHADER_UNUSED_KHR;

	groups[3].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
	groups[3].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
	groups[3].generalShader = VK_SHADER_UNUSED_KHR;
	groups[3].closestHitShader = 3;
	groups[3].anyHitShader = 4;
	groups[3].intersectionShader = VK_SHADER_UNUSED_KHR;

	Com_Memset( &pipelineInfo, 0, sizeof( pipelineInfo ) );
	pipelineInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
	pipelineInfo.stageCount = ARRAY_LEN( stages );
	pipelineInfo.pStages = stages;
	pipelineInfo.groupCount = ARRAY_LEN( groups );
	pipelineInfo.pGroups = groups;
	pipelineInfo.maxPipelineRayRecursionDepth = 2;
	pipelineInfo.layout = vk.rt.pipeline_layout;

	result = qvkCreateRayTracingPipelinesKHR(
		vk.device,
		VK_NULL_HANDLE,
		vk.pipelineCache,
		1,
		&pipelineInfo,
		NULL,
		&vk.rt.pipeline );
	if ( result != VK_SUCCESS ) {
		ri.Printf( PRINT_ERROR, "RTX RT: failed to create ray tracing pipeline: %s\n", vk_result_string( result ) );
		return qfalse;
	}

	return vk_rt_build_sbt();
}

static qboolean vk_rt_update_descriptor_set( void )
{
	VkWriteDescriptorSetAccelerationStructureKHR asInfo;
	VkDescriptorImageInfo *imageInfos = s_vkRtDescriptorImageInfos;
	VkDescriptorBufferInfo bufferInfos[8];
	VkWriteDescriptorSet writes[14];
	VkDescriptorSet descriptorSet;
	Vk_Sampler_Def sceneSamplerDef;
	const image_t *sceneImage;
	const rtxVkRtBuffer_t *dynamicVertices;
	const rtxVkRtBuffer_t *dynamicIndices;
	const rtxVkRtBuffer_t *dynamicMaterials;
	const rtxVkRtBuffer_t *lightBuffer;
	const rtxVkRtBuffer_t *temporalBuffer;
	uint32_t i;
	uint32_t historyReadIndex = vk.rt.history_read_index & 1u;
	uint32_t historyWriteIndex = historyReadIndex ^ 1u;
	uint32_t descriptorIndex = (uint32_t)( vk.cmd_index % NUM_COMMAND_BUFFERS );

	descriptorSet = vk.rt.descriptor_set[descriptorIndex];
	if ( descriptorSet == VK_NULL_HANDLE || !vk.rt.tlas.valid || vk.rt.output_view == VK_NULL_HANDLE ) {
		return qfalse;
	}
	if ( vk.rt.history_view[historyReadIndex] == VK_NULL_HANDLE || vk.rt.history_view[historyWriteIndex] == VK_NULL_HANDLE ) {
		return qfalse;
	}
	lightBuffer = &vk.rt.light_buffer[descriptorIndex];
	temporalBuffer = &vk.rt.temporal_params_buffer[descriptorIndex];
	if ( temporalBuffer->buffer == VK_NULL_HANDLE ) {
		return qfalse;
	}
	if ( vk.color_image_view == VK_NULL_HANDLE ) {
		return qfalse;
	}

	if ( vk.rt.world_vertex_buffer.buffer == VK_NULL_HANDLE ||
		vk.rt.world_index_buffer.buffer == VK_NULL_HANDLE ||
		vk.rt.world_material_buffer.buffer == VK_NULL_HANDLE ) {
		return qfalse;
	}

	if ( !vk_rt_update_light_buffer() || lightBuffer->buffer == VK_NULL_HANDLE ) {
		return qfalse;
	}

	dynamicVertices = ( vk.rt.dynamic_vertex_buffer.buffer != VK_NULL_HANDLE ) ? &vk.rt.dynamic_vertex_buffer : &vk.rt.world_vertex_buffer;
	dynamicIndices = ( vk.rt.dynamic_index_buffer.buffer != VK_NULL_HANDLE ) ? &vk.rt.dynamic_index_buffer : &vk.rt.world_index_buffer;
	dynamicMaterials = ( vk.rt.dynamic_material_buffer.buffer != VK_NULL_HANDLE ) ? &vk.rt.dynamic_material_buffer : &vk.rt.world_material_buffer;

	Com_Memset( &asInfo, 0, sizeof( asInfo ) );
	asInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
	asInfo.accelerationStructureCount = 1;
	asInfo.pAccelerationStructures = &vk.rt.tlas.handle;

	Com_Memset( imageInfos, 0, sizeof( s_vkRtDescriptorImageInfos ) );
	imageInfos[0].sampler = VK_NULL_HANDLE;
	imageInfos[0].imageView = vk.rt.output_view;
	imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	imageInfos[1].sampler = VK_NULL_HANDLE;
	imageInfos[1].imageView = vk.rt.history_view[historyReadIndex];
	imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	imageInfos[2].sampler = VK_NULL_HANDLE;
	imageInfos[2].imageView = vk.rt.history_view[historyWriteIndex];
	imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	Com_Memset( &sceneSamplerDef, 0, sizeof( sceneSamplerDef ) );
	sceneSamplerDef.gl_mag_filter = GL_LINEAR;
	sceneSamplerDef.gl_min_filter = GL_LINEAR;
	sceneSamplerDef.address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sceneSamplerDef.max_lod_1_0 = qtrue;
	sceneSamplerDef.noAnisotropy = qtrue;
	imageInfos[3].sampler = vk_find_sampler( &sceneSamplerDef );
	imageInfos[3].imageView = vk.color_image_view;
	imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	for ( i = 0; i < RTX_RT_MAX_SCENE_TEXTURES; i++ ) {
		sceneImage = ( i < (uint32_t)tr.numImages ) ? tr.images[i] : NULL;
		if ( !sceneImage || sceneImage->view == VK_NULL_HANDLE ) {
			sceneImage = tr.whiteImage;
		}
		if ( !sceneImage || sceneImage->view == VK_NULL_HANDLE ) {
			imageInfos[4 + i].sampler = imageInfos[3].sampler;
			imageInfos[4 + i].imageView = imageInfos[3].imageView;
			imageInfos[4 + i].imageLayout = imageInfos[3].imageLayout;
			continue;
		}

		Com_Memset( &sceneSamplerDef, 0, sizeof( sceneSamplerDef ) );
		sceneSamplerDef.address_mode = sceneImage->wrapClampMode;
		if ( sceneImage->flags & IMGFLAG_MIPMAP ) {
			sceneSamplerDef.gl_mag_filter = gl_filter_max;
			sceneSamplerDef.gl_min_filter = gl_filter_min;
		} else {
			sceneSamplerDef.gl_mag_filter = GL_LINEAR;
			sceneSamplerDef.gl_min_filter = GL_LINEAR;
			sceneSamplerDef.noAnisotropy = qtrue;
		}
		imageInfos[4 + i].sampler = vk_find_sampler( &sceneSamplerDef );
		imageInfos[4 + i].imageView = sceneImage->view;
		imageInfos[4 + i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	Com_Memset( bufferInfos, 0, sizeof( bufferInfos ) );
	bufferInfos[0].buffer = vk.rt.world_vertex_buffer.buffer;
	bufferInfos[0].offset = 0;
	bufferInfos[0].range = VK_WHOLE_SIZE;
	bufferInfos[1].buffer = vk.rt.world_index_buffer.buffer;
	bufferInfos[1].offset = 0;
	bufferInfos[1].range = VK_WHOLE_SIZE;
	bufferInfos[2].buffer = vk.rt.world_material_buffer.buffer;
	bufferInfos[2].offset = 0;
	bufferInfos[2].range = VK_WHOLE_SIZE;
	bufferInfos[3].buffer = dynamicVertices->buffer;
	bufferInfos[3].offset = 0;
	bufferInfos[3].range = VK_WHOLE_SIZE;
	bufferInfos[4].buffer = dynamicIndices->buffer;
	bufferInfos[4].offset = 0;
	bufferInfos[4].range = VK_WHOLE_SIZE;
	bufferInfos[5].buffer = dynamicMaterials->buffer;
	bufferInfos[5].offset = 0;
	bufferInfos[5].range = VK_WHOLE_SIZE;
	bufferInfos[6].buffer = lightBuffer->buffer;
	bufferInfos[6].offset = 0;
	bufferInfos[6].range = VK_WHOLE_SIZE;
	bufferInfos[7].buffer = temporalBuffer->buffer;
	bufferInfos[7].offset = 0;
	bufferInfos[7].range = VK_WHOLE_SIZE;

	Com_Memset( writes, 0, sizeof( writes ) );
	writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[0].pNext = &asInfo;
	writes[0].dstSet = descriptorSet;
	writes[0].dstBinding = RTX_RT_DESC_BINDING_TLAS;
	writes[0].descriptorCount = 1;
	writes[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

	writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[1].dstSet = descriptorSet;
	writes[1].dstBinding = RTX_RT_DESC_BINDING_OUTPUT;
	writes[1].descriptorCount = 1;
	writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	writes[1].pImageInfo = &imageInfos[0];

	writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[2].dstSet = descriptorSet;
	writes[2].dstBinding = RTX_RT_DESC_BINDING_WORLD_VERTICES;
	writes[2].descriptorCount = 1;
	writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	writes[2].pBufferInfo = &bufferInfos[0];

	writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[3].dstSet = descriptorSet;
	writes[3].dstBinding = RTX_RT_DESC_BINDING_WORLD_INDICES;
	writes[3].descriptorCount = 1;
	writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	writes[3].pBufferInfo = &bufferInfos[1];

	writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[4].dstSet = descriptorSet;
	writes[4].dstBinding = RTX_RT_DESC_BINDING_WORLD_MATERIALS;
	writes[4].descriptorCount = 1;
	writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	writes[4].pBufferInfo = &bufferInfos[2];

	writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[5].dstSet = descriptorSet;
	writes[5].dstBinding = RTX_RT_DESC_BINDING_DYNAMIC_VERTICES;
	writes[5].descriptorCount = 1;
	writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	writes[5].pBufferInfo = &bufferInfos[3];

	writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[6].dstSet = descriptorSet;
	writes[6].dstBinding = RTX_RT_DESC_BINDING_DYNAMIC_INDICES;
	writes[6].descriptorCount = 1;
	writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	writes[6].pBufferInfo = &bufferInfos[4];

	writes[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[7].dstSet = descriptorSet;
	writes[7].dstBinding = RTX_RT_DESC_BINDING_DYNAMIC_MATERIALS;
	writes[7].descriptorCount = 1;
	writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	writes[7].pBufferInfo = &bufferInfos[5];

	writes[8].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[8].dstSet = descriptorSet;
	writes[8].dstBinding = RTX_RT_DESC_BINDING_LIGHTS;
	writes[8].descriptorCount = 1;
	writes[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	writes[8].pBufferInfo = &bufferInfos[6];

	writes[9].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[9].dstSet = descriptorSet;
	writes[9].dstBinding = RTX_RT_DESC_BINDING_HISTORY_IN;
	writes[9].descriptorCount = 1;
	writes[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	writes[9].pImageInfo = &imageInfos[1];

	writes[10].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[10].dstSet = descriptorSet;
	writes[10].dstBinding = RTX_RT_DESC_BINDING_HISTORY_OUT;
	writes[10].descriptorCount = 1;
	writes[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	writes[10].pImageInfo = &imageInfos[2];

	writes[11].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[11].dstSet = descriptorSet;
	writes[11].dstBinding = RTX_RT_DESC_BINDING_TEMPORAL_PARAMS;
	writes[11].descriptorCount = 1;
	writes[11].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	writes[11].pBufferInfo = &bufferInfos[7];

	writes[12].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[12].dstSet = descriptorSet;
	writes[12].dstBinding = RTX_RT_DESC_BINDING_SCENE_COLOR;
	writes[12].descriptorCount = 1;
	writes[12].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writes[12].pImageInfo = &imageInfos[3];

	writes[13].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[13].dstSet = descriptorSet;
	writes[13].dstBinding = RTX_RT_DESC_BINDING_SCENE_TEXTURES;
	writes[13].descriptorCount = RTX_RT_MAX_SCENE_TEXTURES;
	writes[13].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writes[13].pImageInfo = &imageInfos[4];

	qvkUpdateDescriptorSets( vk.device, ARRAY_LEN( writes ), writes, 0, NULL );
	vk.rt.descriptorsValid = qtrue;
	return qtrue;
}

static qboolean vk_rt_ensure_dynamic_blas( VkDeviceSize *frameBudgetBytesUsed, VkDeviceSize frameBudgetBytes )
{
	VkDeviceSize ignoredBuildBytes = 0;
	vec3_t estimatedCentroid;
	uint32_t estimatedModelCount = 0;
	uint32_t estimatedEffectCount = 0;
	uint32_t estimatedSignature = 0u;
	const int rebuildInterval = vk_rt_as_rebuild_interval();
	const int prepStride = vk_rt_prep_frame_stride();
	const float dirtyThreshold = MAX( 0.0f, vk_rt_as_dirty_threshold() );
	const qboolean hadDynamicResources =
		( vk.rt.dynamic_blas.valid ||
			vk.rt.dynamic_vertex_buffer.buffer != VK_NULL_HANDLE ||
			vk.rt.dynamic_index_buffer.buffer != VK_NULL_HANDLE ||
			vk.rt.dynamic_material_buffer.buffer != VK_NULL_HANDLE ) ? qtrue : qfalse;
	qboolean forceRebuild = qfalse;
	qboolean shouldRebuild = qfalse;

	if ( !vk_rt_dynamic_blas_enabled() ) {
		const qboolean dynamicStateChanged =
			hadDynamicResources ||
			vk.rt.dynamic_entity_count != 0u ||
			vk.rt.dynamic_effect_count != 0u ||
			vk.rt.dynamic_scene_signature != 0u;

		if ( hadDynamicResources ) {
			if ( vk_rt_debug_as_stats_level() >= 1 ) {
				ri.Printf( PRINT_DEVELOPER, "RTX RT: dynamic BLAS disabled via rtx_rt_dynamic_blas 0, using world-only TLAS instances\n" );
			}
			if ( !vk_rt_wait_for_inflight_frames( "dynamic BLAS disable" ) ) {
				return qfalse;
			}
		}

		vk_rt_destroy_as( &vk.rt.dynamic_blas );
		vk_rt_destroy_buffer( &vk.rt.dynamic_vertex_buffer );
		vk_rt_destroy_buffer( &vk.rt.dynamic_index_buffer );
		vk_rt_destroy_buffer( &vk.rt.dynamic_material_buffer );
		vk.rt.dynamic_vertex_count = 0;
		vk.rt.dynamic_index_count = 0;
		vk.rt.dynamic_material_count = 0;
		vk.rt.dynamic_entity_count = 0;
		vk.rt.dynamic_effect_count = 0;
		vk.rt.dynamic_masked_triangle_count = 0;
		vk.rt.stats.dynamic_triangles = 0;
		vk.rt.stats.masked_triangles = vk.rt.world_masked_triangle_count;
		vk.rt.dynamic_scene_signature = 0u;
		vk.rt.dynamic_last_rebuild_frame = tr.frameCount;
		VectorCopy( vk.rt.dynamic_centroid_prev, vk.rt.dynamic_centroid_current );
		VectorCopy( vk.rt.dynamic_centroid_prev, vk.rt.dynamic_last_rebuild_centroid );
		if ( dynamicStateChanged ) {
			vk.rt.needsRebuild = qtrue;
		}
		return qtrue;
	}

	estimatedSignature = vk_rt_estimate_dynamic_scene_signature(
		estimatedCentroid,
		&estimatedModelCount,
		&estimatedEffectCount,
		MAX( 1.0f, dirtyThreshold * 0.25f ) );
	vk.rt.dynamic_entity_count = estimatedModelCount;
	vk.rt.dynamic_effect_count = estimatedEffectCount;
	VectorCopy( estimatedCentroid, vk.rt.dynamic_centroid_current );

	if ( !hadDynamicResources && estimatedModelCount == 0u && estimatedEffectCount == 0u && backEnd.refdef.numPolys == 0 ) {
		vk.rt.dynamic_scene_signature = estimatedSignature;
		vk.rt.dynamic_last_rebuild_frame = tr.frameCount;
		vk.rt.dynamic_masked_triangle_count = 0;
		vk.rt.stats.dynamic_triangles = 0;
		vk.rt.stats.masked_triangles = vk.rt.world_masked_triangle_count;
		return qtrue;
	}

	if ( !hadDynamicResources ) {
		shouldRebuild = qtrue;
	} else {
		vec3_t delta;
		float centroidDelta;
		const int framesSinceRebuild = ( vk.rt.dynamic_last_rebuild_frame >= 0 ) ?
			( tr.frameCount - vk.rt.dynamic_last_rebuild_frame ) : rebuildInterval;

		VectorSubtract( estimatedCentroid, vk.rt.dynamic_last_rebuild_centroid, delta );
		centroidDelta = VectorLength( delta );

		forceRebuild = ( estimatedModelCount == 0u && estimatedEffectCount == 0u && backEnd.refdef.numPolys == 0 ) ? qtrue : qfalse;
		if ( !forceRebuild && prepStride > 1 && ( tr.frameCount % prepStride ) != 0 ) {
			vk.rt.stats.prep_stride_skips++;
			return qtrue;
		}
		if ( !forceRebuild && framesSinceRebuild < rebuildInterval ) {
			vk.rt.stats.dynamic_rebuild_skips++;
			return qtrue;
		}
		if ( !forceRebuild &&
			estimatedSignature == vk.rt.dynamic_scene_signature &&
			centroidDelta < dirtyThreshold ) {
			vk.rt.stats.dynamic_rebuild_skips++;
			return qtrue;
		}

		shouldRebuild = qtrue;
	}

	if ( !shouldRebuild ) {
		return qtrue;
	}

	if ( !vk_rt_build_dynamic_scene_blas( frameBudgetBytesUsed, frameBudgetBytes, &ignoredBuildBytes ) ) {
		return qfalse;
	}

	vk.rt.dynamic_scene_signature = estimatedSignature;
	vk.rt.dynamic_last_rebuild_frame = tr.frameCount;
	VectorCopy( vk.rt.dynamic_centroid_current, vk.rt.dynamic_last_rebuild_centroid );

	return qtrue;
}

static qboolean vk_rt_ensure_world_blas( VkDeviceSize *frameBudgetBytesUsed, VkDeviceSize frameBudgetBytes )
{
	rtxRtCpuGeometry_t geometry;
	VkBuildAccelerationStructureFlagsKHR flags;
	VkDeviceSize buildBytes;
	uint32_t droppedTriangles = 0;

	if ( vk.rt.world_blas.valid && vk.rt.worldGeometryReady ) {
		return qtrue;
	}

	if ( !vk_rt_extract_world_geometry( &geometry ) ) {
		vk_rt_cpu_geometry_free( &geometry );
		return qfalse;
	}

	if ( !vk_rt_cpu_geometry_sanitize_indices( &geometry, &droppedTriangles ) ) {
		vk_rt_cpu_geometry_free( &geometry );
		return qfalse;
	}
	if ( droppedTriangles > 0 && vk_rt_debug_as_stats_level() >= 1 ) {
		ri.Printf( PRINT_WARNING,
			"RTX RT: dropped %u invalid/degenerate world triangles before BLAS build\n",
			droppedTriangles );
	}

	flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
		( vk_rt_as_compaction_enabled() ? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR : 0 );
	buildBytes = vk_rt_query_triangles_build_bytes(
		geometry.numVertices,
		geometry.numIndices,
		geometry.anyHitTriangleCount == 0u ? qtrue : qfalse,
		flags );
	if ( !vk_rt_budget_allows( buildBytes, frameBudgetBytesUsed, frameBudgetBytes ) ) {
		vk_rt_cpu_geometry_free( &geometry );
		return qfalse;
	}

	if ( !vk_rt_ensure_buffer_capacity(
		&vk.rt.world_vertex_buffer,
		geometry.numVertices * sizeof( rtxRtPackedVertex_t ),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		qtrue,
		"rt world vertices" ) ||
		!vk_rt_ensure_buffer_capacity(
			&vk.rt.world_index_buffer,
			geometry.numIndices * sizeof( uint32_t ),
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
				VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
				VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			qtrue,
			"rt world indices" ) ) {
		vk_rt_cpu_geometry_free( &geometry );
		return qfalse;
	}

	if ( !vk_rt_upload_buffer_data(
		&vk.rt.world_vertex_buffer,
		geometry.vertices,
		(size_t)geometry.numVertices * sizeof( rtxRtPackedVertex_t ) ) ||
		!vk_rt_upload_buffer_data(
			&vk.rt.world_index_buffer,
			geometry.indices,
			(size_t)geometry.numIndices * sizeof( uint32_t ) ) ) {
		vk_rt_cpu_geometry_free( &geometry );
		return qfalse;
	}

	if ( !vk_rt_upload_material_buffer(
		geometry.materials,
		geometry.numMaterials,
		&vk.rt.world_material_buffer,
		&vk.rt.world_material_count,
		"rt world materials" ) ) {
		vk_rt_cpu_geometry_free( &geometry );
		return qfalse;
	}

	vk.rt.world_vertex_count = geometry.numVertices;
	vk.rt.world_index_count = geometry.numIndices;

	if ( !vk_rt_build_triangles_blas(
		&vk.rt.world_blas,
		&vk.rt.world_vertex_buffer,
		vk.rt.world_vertex_count,
		sizeof( rtxRtPackedVertex_t ),
		&vk.rt.world_index_buffer,
		vk.rt.world_index_count,
		geometry.anyHitTriangleCount == 0u ? qtrue : qfalse,
		vk_rt_as_compaction_enabled(),
		"rt world blas" ) ) {
		vk_rt_cpu_geometry_free( &geometry );
		return qfalse;
	}

	vk.rt.worldGeometryReady = qtrue;
	vk.rt.stats.world_build_bytes += (uint64_t)( vk.rt.world_blas.size + vk.rt.scratch_buffer.allocationSize );
	vk.rt.needsRebuild = qtrue;
	vk_rt_cpu_geometry_free( &geometry );
	return qtrue;
}

static uint32_t vk_rt_collect_instances( VkAccelerationStructureInstanceKHR *instances, uint32_t maxInstances, qboolean *throttledByInstanceBudget )
{
	uint32_t count = 0;

	if ( throttledByInstanceBudget ) {
		*throttledByInstanceBudget = qfalse;
	}

	if ( maxInstances == 0 ) {
		return 0;
	}

	if ( vk.rt.world_blas.valid ) {
		VkAccelerationStructureInstanceKHR *worldInstance = &instances[count++];
		Com_Memset( worldInstance, 0, sizeof( *worldInstance ) );
		worldInstance->transform.matrix[0][0] = 1.0f;
		worldInstance->transform.matrix[1][1] = 1.0f;
		worldInstance->transform.matrix[2][2] = 1.0f;
		worldInstance->instanceCustomIndex = 0;
		worldInstance->mask = 0xFF;
		worldInstance->instanceShaderBindingTableRecordOffset = 0;
		worldInstance->flags =
			VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR |
			VK_GEOMETRY_INSTANCE_TRIANGLE_FLIP_FACING_BIT_KHR;
		worldInstance->accelerationStructureReference = vk.rt.world_blas.deviceAddress;
	}

	if ( !vk.rt.dynamic_blas.valid ) {
		return count;
	}

	if ( count >= maxInstances ) {
		if ( throttledByInstanceBudget ) {
			*throttledByInstanceBudget = qtrue;
		}
		return count;
	}

	{
		VkAccelerationStructureInstanceKHR *dynamicInstance = &instances[count++];
		Com_Memset( dynamicInstance, 0, sizeof( *dynamicInstance ) );
		dynamicInstance->transform.matrix[0][0] = 1.0f;
		dynamicInstance->transform.matrix[1][1] = 1.0f;
		dynamicInstance->transform.matrix[2][2] = 1.0f;
		dynamicInstance->instanceCustomIndex = 1;
		dynamicInstance->mask = 0xFF;
		dynamicInstance->instanceShaderBindingTableRecordOffset = 0;
		dynamicInstance->flags =
			VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR |
			VK_GEOMETRY_INSTANCE_TRIANGLE_FLIP_FACING_BIT_KHR;
		dynamicInstance->accelerationStructureReference = vk.rt.dynamic_blas.deviceAddress;
	}

	return count;
}

static qboolean vk_rt_build_tlas( VkDeviceSize *frameBudgetBytesUsed, VkDeviceSize frameBudgetBytes )
{
	VkAccelerationStructureInstanceKHR *instances = NULL;
	uint32_t requestedCapacity;
	uint32_t instanceCount;
	VkDeviceAddress instanceDataAddress;
	VkDeviceAddress scratchAddress;
	qboolean throttled = qfalse;
	VkBuildAccelerationStructureFlagsKHR buildFlags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	VkDeviceSize buildBytes;
	VkAccelerationStructureGeometryInstancesDataKHR instancesData;
	VkAccelerationStructureGeometryKHR geometry;
	VkAccelerationStructureBuildGeometryInfoKHR buildInfo;
	VkAccelerationStructureBuildSizesInfoKHR buildSizes;
	VkAccelerationStructureBuildRangeInfoKHR rangeInfo;
	VkAccelerationStructureBuildRangeInfoKHR *rangeInfos[1];
	VkAccelerationStructureDeviceAddressInfoKHR addressInfo;
	VkCommandBuffer cmd;

	requestedCapacity = 4;

	instances = (VkAccelerationStructureInstanceKHR *)ri.Malloc(
		requestedCapacity * sizeof( VkAccelerationStructureInstanceKHR ) );
	if ( !instances ) {
		return qfalse;
	}

	instanceCount = vk_rt_collect_instances( instances, requestedCapacity, &throttled );
	if ( throttled ) {
		vk.rt.stats.throttled_builds++;
	}
	if ( instanceCount == 0 ) {
		ri.Free( instances );
		return qfalse;
	}
	if ( vk.rt.tlas.valid && vk.rt.tlasValid && !vk.rt.needsRebuild && vk.rt.tlas_instance_count == instanceCount ) {
		ri.Free( instances );
		return qtrue;
	}

	buildBytes = vk_rt_query_tlas_build_bytes( instanceCount, buildFlags );
	if ( !vk_rt_budget_allows( buildBytes, frameBudgetBytesUsed, frameBudgetBytes ) ) {
		ri.Free( instances );
		return qfalse;
	}

	if ( vk.rt.tlas_instance_capacity < instanceCount || vk.rt.tlas_instance_buffer.buffer == VK_NULL_HANDLE ) {
		if ( !vk_rt_ensure_buffer_capacity(
			&vk.rt.tlas_instance_buffer,
			requestedCapacity * sizeof( VkAccelerationStructureInstanceKHR ),
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
				VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			qtrue,
			"rt tlas instances" ) ) {
			ri.Free( instances );
			return qfalse;
		}
		vk.rt.tlas_instance_capacity = requestedCapacity;
	}

	if ( !vk_rt_upload_buffer_data(
		&vk.rt.tlas_instance_buffer,
		instances,
		(size_t)instanceCount * sizeof( VkAccelerationStructureInstanceKHR ) ) ) {
		ri.Free( instances );
		return qfalse;
	}
	vk.rt.tlas_instance_count = instanceCount;
	ri.Free( instances );

	instanceDataAddress = vk.rt.tlas_instance_buffer.address;
	Com_Memset( &instancesData, 0, sizeof( instancesData ) );
	instancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	instancesData.arrayOfPointers = VK_FALSE;
	instancesData.data.deviceAddress = instanceDataAddress;

	Com_Memset( &geometry, 0, sizeof( geometry ) );
	geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	geometry.geometry.instances = instancesData;

	Com_Memset( &buildInfo, 0, sizeof( buildInfo ) );
	buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	buildInfo.flags = buildFlags;
	buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	buildInfo.geometryCount = 1;
	buildInfo.pGeometries = &geometry;

	Com_Memset( &buildSizes, 0, sizeof( buildSizes ) );
	buildSizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	qvkGetAccelerationStructureBuildSizesKHR(
		vk.device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&buildInfo,
		&instanceCount,
		&buildSizes );

	if ( vk.rt.tlas.valid ) {
		if ( !vk_rt_wait_for_inflight_frames( "TLAS rebuild" ) ) {
			return qfalse;
		}
	}
	vk_rt_destroy_as( &vk.rt.tlas );
	if ( !vk_rt_create_as( VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, buildSizes.accelerationStructureSize, "rt tlas", &vk.rt.tlas ) ) {
		return qfalse;
	}

	if ( !vk_rt_ensure_scratch_buffer( buildSizes.buildScratchSize ) ) {
		return qfalse;
	}
	if ( !vk_rt_require_scratch_device_address( &scratchAddress ) ) {
		return qfalse;
	}

	buildInfo.dstAccelerationStructure = vk.rt.tlas.handle;
	buildInfo.scratchData.deviceAddress = scratchAddress;

	Com_Memset( &rangeInfo, 0, sizeof( rangeInfo ) );
	rangeInfo.primitiveCount = instanceCount;
	rangeInfo.primitiveOffset = 0;
	rangeInfo.firstVertex = 0;
	rangeInfo.transformOffset = 0;
	rangeInfos[0] = &rangeInfo;

	cmd = begin_command_buffer();
	qvkCmdBuildAccelerationStructuresKHR( cmd, 1, &buildInfo, (const VkAccelerationStructureBuildRangeInfoKHR *const *)rangeInfos );
	if ( !end_command_buffer_soft( cmd, __func__ ) ) {
		vk_rt_destroy_as( &vk.rt.tlas );
		vk.rt.tlasValid = qfalse;
		return qfalse;
	}

	addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
	addressInfo.pNext = NULL;
	addressInfo.accelerationStructure = vk.rt.tlas.handle;
	vk.rt.tlas.deviceAddress = qvkGetAccelerationStructureDeviceAddressKHR( vk.device, &addressInfo );
	vk.rt.tlas.valid = qtrue;
	vk.rt.tlasValid = qtrue;
	vk.rt.descriptorsValid = qfalse;
	vk.rt.needsRebuild = qfalse;
	vk.rt.stats.tlas_build_bytes += (uint64_t)( vk.rt.tlas.size + vk.rt.scratch_buffer.allocationSize );

	return qtrue;
}

static qboolean vk_rt_ensure_temporal_params_buffer( void )
{
	const uint32_t bufferIndex = (uint32_t)( vk.cmd_index % NUM_COMMAND_BUFFERS );
	rtxVkRtBuffer_t *temporalBuffer = &vk.rt.temporal_params_buffer[bufferIndex];

	if ( temporalBuffer->buffer != VK_NULL_HANDLE ) {
		return qtrue;
	}
	return vk_rt_create_buffer(
		sizeof( rtxRtTemporalParams_t ),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		qfalse,
		va( "rt temporal params frame %u", bufferIndex ),
		temporalBuffer );
}

static void vk_rt_reset_temporal_history( const char *reason )
{
	if ( vk_rt_debug_as_stats_level() >= 1 ) {
		ri.Printf( PRINT_DEVELOPER, "RTX RT: temporal history reset (%s)\n", reason ? reason : "unspecified" );
	}
	vk.rt.history_valid = qfalse;
	vk.rt.temporal_reset_pending = qtrue;
	vk.rt.stats.history_resets++;
}

static qboolean vk_rt_update_temporal_state( void )
{
	rtxRtTemporalParams_t params;
	const float halfFovX = tanf( DEG2RAD( backEnd.viewParms.fovX * 0.5f ) );
	const float halfFovY = tanf( DEG2RAD( backEnd.viewParms.fovY * 0.5f ) );
	vec3_t currentOrigin;
	vec3_t currentForward;
	vec3_t currentRight;
	vec3_t currentUp;
	vec3_t originDelta;
	vec3_t dynamicDelta;
	float cutDistance = vk_rt_temporal_camera_cut_distance();
	float forwardDot = 1.0f;
	float moveDistance = 0.0f;
	float exposureMin = vk_rt_exposure_min();
	float exposureMax = vk_rt_exposure_max();
	float exposureAdapt = vk_rt_exposure_speed();
	float exposureTarget;
	float sceneLumaEstimate;
	int refdefTime = backEnd.refdef.time;
	int timeDelta = 0;
	qboolean modeValidation = vk_rt_camera_mode_validate_enabled();
	qboolean temporalReset = vk.rt.temporal_reset_pending;
	uint32_t flags = 0;
	qboolean logReset = temporalReset;

	VectorCopy( backEnd.viewParms.or.origin, currentOrigin );
	VectorCopy( backEnd.viewParms.or.axis[0], currentForward );
	VectorScale( backEnd.viewParms.or.axis[1], -1.0f, currentRight );
	VectorCopy( backEnd.viewParms.or.axis[2], currentUp );

	if ( !vk_rt_ensure_temporal_params_buffer() ) {
		return qfalse;
	}

	if ( vk.rt.temporal_has_prev_camera ) {
		VectorSubtract( currentOrigin, vk.rt.temporal_prev_origin, originDelta );
		moveDistance = VectorLength( originDelta );
		forwardDot = DotProduct( currentForward, vk.rt.temporal_prev_forward );
		if ( ( cutDistance > 0.0f && moveDistance > cutDistance ) || forwardDot < 0.5f ) {
			temporalReset = qtrue;
			logReset = qtrue;
		}
		if ( fabsf( halfFovX - vk.rt.temporal_prev_tan_half_fov_x ) > 0.01f ||
			fabsf( halfFovY - vk.rt.temporal_prev_tan_half_fov_y ) > 0.01f ) {
			temporalReset = qtrue;
			logReset = qtrue;
		}
		if ( modeValidation ) {
			timeDelta = refdefTime - vk.rt.temporal_prev_refdef_time;
			if ( backEnd.refdef.rdflags != vk.rt.temporal_prev_rdflags ||
				backEnd.refdef.stereoFrame != vk.rt.temporal_prev_stereo_frame ||
				timeDelta < -1 || timeDelta > 250 ) {
				temporalReset = qtrue;
				logReset = qtrue;
			}
		}
	} else {
		temporalReset = qtrue;
		logReset = qtrue;
	}

	if ( rtx_rt_temporal_enable && rtx_rt_temporal_enable->modified ) {
		rtx_rt_temporal_enable->modified = qfalse;
		temporalReset = qtrue;
		logReset = qtrue;
	}
	if ( rtx_rt_temporal_alpha && rtx_rt_temporal_alpha->modified ) {
		rtx_rt_temporal_alpha->modified = qfalse;
		temporalReset = qtrue;
		logReset = qtrue;
	}
	if ( rtx_rt_temporal_clamp && rtx_rt_temporal_clamp->modified ) {
		rtx_rt_temporal_clamp->modified = qfalse;
		temporalReset = qtrue;
		logReset = qtrue;
	}
	if ( rtx_rt_temporal_depth_reject && rtx_rt_temporal_depth_reject->modified ) {
		rtx_rt_temporal_depth_reject->modified = qfalse;
		temporalReset = qtrue;
		logReset = qtrue;
	}
	if ( rtx_rt_temporal_motion_reject && rtx_rt_temporal_motion_reject->modified ) {
		rtx_rt_temporal_motion_reject->modified = qfalse;
		temporalReset = qtrue;
		logReset = qtrue;
	}
	if ( rtx_rt_temporal_camera_cut && rtx_rt_temporal_camera_cut->modified ) {
		rtx_rt_temporal_camera_cut->modified = qfalse;
		temporalReset = qtrue;
		logReset = qtrue;
	}
	if ( rtx_rt_spatial_denoise && rtx_rt_spatial_denoise->modified ) {
		rtx_rt_spatial_denoise->modified = qfalse;
		temporalReset = qtrue;
		logReset = qtrue;
	}
	if ( rtx_rt_spatial_sigma_depth && rtx_rt_spatial_sigma_depth->modified ) {
		rtx_rt_spatial_sigma_depth->modified = qfalse;
		temporalReset = qtrue;
		logReset = qtrue;
	}
	if ( rtx_rt_spatial_sigma_normal && rtx_rt_spatial_sigma_normal->modified ) {
		rtx_rt_spatial_sigma_normal->modified = qfalse;
		temporalReset = qtrue;
		logReset = qtrue;
	}
	if ( rtx_rt_firefly_threshold && rtx_rt_firefly_threshold->modified ) {
		rtx_rt_firefly_threshold->modified = qfalse;
	}
	if ( rtx_rt_firefly_strength && rtx_rt_firefly_strength->modified ) {
		rtx_rt_firefly_strength->modified = qfalse;
	}
	if ( rtx_rt_tonemap_mode && rtx_rt_tonemap_mode->modified ) {
		rtx_rt_tonemap_mode->modified = qfalse;
	}
	if ( rtx_rt_exposure && rtx_rt_exposure->modified ) {
		rtx_rt_exposure->modified = qfalse;
	}
	if ( rtx_rt_auto_exposure && rtx_rt_auto_exposure->modified ) {
		rtx_rt_auto_exposure->modified = qfalse;
	}
	if ( rtx_rt_exposure_speed && rtx_rt_exposure_speed->modified ) {
		rtx_rt_exposure_speed->modified = qfalse;
	}
	if ( rtx_rt_exposure_min && rtx_rt_exposure_min->modified ) {
		rtx_rt_exposure_min->modified = qfalse;
	}
	if ( rtx_rt_exposure_max && rtx_rt_exposure_max->modified ) {
		rtx_rt_exposure_max->modified = qfalse;
	}
	if ( rtx_rt_motion_dynamic_scale && rtx_rt_motion_dynamic_scale->modified ) {
		rtx_rt_motion_dynamic_scale->modified = qfalse;
	}
	if ( rtx_rt_particle_volume && rtx_rt_particle_volume->modified ) {
		rtx_rt_particle_volume->modified = qfalse;
		temporalReset = qtrue;
		logReset = qtrue;
	}
	if ( rtx_rt_legacy_color_compat && rtx_rt_legacy_color_compat->modified ) {
		rtx_rt_legacy_color_compat->modified = qfalse;
		temporalReset = qtrue;
		logReset = qtrue;
	}
	if ( rtx_rt_raster_reference && rtx_rt_raster_reference->modified ) {
		rtx_rt_raster_reference->modified = qfalse;
		temporalReset = qtrue;
		logReset = qtrue;
	}
	if ( rtx_rt_readability_lift && rtx_rt_readability_lift->modified ) {
		rtx_rt_readability_lift->modified = qfalse;
		temporalReset = qtrue;
		logReset = qtrue;
	}
	if ( rtx_rt_readability_contrast && rtx_rt_readability_contrast->modified ) {
		rtx_rt_readability_contrast->modified = qfalse;
		temporalReset = qtrue;
		logReset = qtrue;
	}
	if ( rtx_rt_readability_saturation && rtx_rt_readability_saturation->modified ) {
		rtx_rt_readability_saturation->modified = qfalse;
		temporalReset = qtrue;
		logReset = qtrue;
	}
	if ( rtx_rt_readability_shadow_floor && rtx_rt_readability_shadow_floor->modified ) {
		rtx_rt_readability_shadow_floor->modified = qfalse;
		temporalReset = qtrue;
		logReset = qtrue;
	}
	if ( r_intensity && r_intensity->modified ) {
		r_intensity->modified = qfalse;
		temporalReset = qtrue;
		logReset = qtrue;
	}

	if ( exposureMin > exposureMax ) {
		float tmp = exposureMin;
		exposureMin = exposureMax;
		exposureMax = tmp;
	}

	if ( !vk.rt.temporal_has_prev_camera ) {
		vk.rt.temporal_exposure = vk_rt_exposure_manual();
	}

	/*
	 * This SDR-only compatibility exposure is deliberately stable. Raw light
	 * count is not an energy estimate: maps with many tiny compile lights
	 * otherwise collapse to the minimum exposure even when their total
	 * contribution is modest. Keep the empirically calibrated sun control
	 * until a measured GPU luminance reduction replaces this heuristic.
	 */
	sceneLumaEstimate = 0.20f +
		( 0.35f * vk_rt_sun_intensity() * RTX_RT_LEGACY_SUN_UNIT_SCALE );
	sceneLumaEstimate = MAX( 0.03f, sceneLumaEstimate );

	if ( vk_rt_auto_exposure_enabled() ) {
		exposureTarget = 0.18f / sceneLumaEstimate;
		vk.rt.temporal_exposure = vk.rt.temporal_exposure + ( exposureTarget - vk.rt.temporal_exposure ) * exposureAdapt;
	} else {
		vk.rt.temporal_exposure = vk_rt_exposure_manual();
	}
	vk.rt.temporal_exposure = Com_Clamp( exposureMin, exposureMax, vk.rt.temporal_exposure );

	VectorSubtract( vk.rt.dynamic_centroid_current, vk.rt.dynamic_centroid_prev, dynamicDelta );

	Com_Memset( &params, 0, sizeof( params ) );
	if ( vk.rt.temporal_has_prev_camera ) {
		params.prevCameraOriginTanHalfFovX[0] = vk.rt.temporal_prev_origin[0];
		params.prevCameraOriginTanHalfFovX[1] = vk.rt.temporal_prev_origin[1];
		params.prevCameraOriginTanHalfFovX[2] = vk.rt.temporal_prev_origin[2];
		params.prevCameraOriginTanHalfFovX[3] = vk.rt.temporal_prev_tan_half_fov_x;
		params.prevCameraForwardTanHalfFovY[0] = vk.rt.temporal_prev_forward[0];
		params.prevCameraForwardTanHalfFovY[1] = vk.rt.temporal_prev_forward[1];
		params.prevCameraForwardTanHalfFovY[2] = vk.rt.temporal_prev_forward[2];
		params.prevCameraForwardTanHalfFovY[3] = vk.rt.temporal_prev_tan_half_fov_y;
		params.prevCameraRightWidth[0] = vk.rt.temporal_prev_right[0];
		params.prevCameraRightWidth[1] = vk.rt.temporal_prev_right[1];
		params.prevCameraRightWidth[2] = vk.rt.temporal_prev_right[2];
		params.prevCameraRightWidth[3] = (float)vk.rt.output_width;
		params.prevCameraUpHeight[0] = vk.rt.temporal_prev_up[0];
		params.prevCameraUpHeight[1] = vk.rt.temporal_prev_up[1];
		params.prevCameraUpHeight[2] = vk.rt.temporal_prev_up[2];
		params.prevCameraUpHeight[3] = (float)vk.rt.output_height;
	} else {
		params.prevCameraOriginTanHalfFovX[0] = currentOrigin[0];
		params.prevCameraOriginTanHalfFovX[1] = currentOrigin[1];
		params.prevCameraOriginTanHalfFovX[2] = currentOrigin[2];
		params.prevCameraOriginTanHalfFovX[3] = halfFovX;
		params.prevCameraForwardTanHalfFovY[0] = currentForward[0];
		params.prevCameraForwardTanHalfFovY[1] = currentForward[1];
		params.prevCameraForwardTanHalfFovY[2] = currentForward[2];
		params.prevCameraForwardTanHalfFovY[3] = halfFovY;
		params.prevCameraRightWidth[0] = currentRight[0];
		params.prevCameraRightWidth[1] = currentRight[1];
		params.prevCameraRightWidth[2] = currentRight[2];
		params.prevCameraRightWidth[3] = (float)vk.rt.output_width;
		params.prevCameraUpHeight[0] = currentUp[0];
		params.prevCameraUpHeight[1] = currentUp[1];
		params.prevCameraUpHeight[2] = currentUp[2];
		params.prevCameraUpHeight[3] = (float)vk.rt.output_height;
	}

	params.dynamicMotionAndExposure[0] = dynamicDelta[0];
	params.dynamicMotionAndExposure[1] = dynamicDelta[1];
	params.dynamicMotionAndExposure[2] = dynamicDelta[2];
	params.dynamicMotionAndExposure[3] = vk.rt.temporal_exposure;

	params.temporalParams0[0] = vk_rt_temporal_alpha();
	params.temporalParams0[1] = vk_rt_temporal_clamp_strength();
	params.temporalParams0[2] = vk_rt_temporal_depth_reject();
	params.temporalParams0[3] = vk_rt_temporal_motion_reject();

	params.denoiseParams[0] = vk_rt_spatial_sigma_depth();
	params.denoiseParams[1] = vk_rt_spatial_sigma_normal();
	params.denoiseParams[2] = vk_rt_firefly_threshold();
	params.denoiseParams[3] = vk_rt_firefly_strength();

	params.exposureParams[0] = exposureMin;
	params.exposureParams[1] = exposureMax;
	params.exposureParams[2] = exposureAdapt;
	params.exposureParams[3] = vk_rt_motion_dynamic_scale();

	params.legacyColorParams[0] = vk_rt_raster_reference_enabled() ? 1.0f : 0.0f;
	params.legacyColorParams[1] = vk_rt_legacy_intensity_scale();
	/*
	 * Preserve the authored raster base and bound RT shadow/specular cues.
	 * The compact RT material representation does not yet express the full
	 * lightmap and multi-stage shader language.
	 */
	params.legacyColorParams[2] = 0.35f;
	params.legacyColorParams[3] = 0.0f;

	params.readabilityParams[0] = vk_rt_readability_lift();
	params.readabilityParams[1] = vk_rt_readability_contrast();
	params.readabilityParams[2] = vk_rt_readability_saturation();
	params.readabilityParams[3] = vk_rt_readability_shadow_floor();

	params.modes[0] = vk_rt_temporal_enabled() ? 1u : 0u;
	params.modes[1] = vk_rt_spatial_denoise_enabled() ? 1u : 0u;
	params.modes[2] = (uint32_t)vk_rt_tonemap_mode();
	flags = 0u;
	if ( vk.rt.history_valid && !temporalReset ) {
		flags |= RTX_RT_TEMPORAL_FLAG_HISTORY_VALID;
	}
	if ( temporalReset ) {
		flags |= RTX_RT_TEMPORAL_FLAG_RESET;
	}
	if ( vk_rt_auto_exposure_enabled() ) {
		flags |= RTX_RT_TEMPORAL_FLAG_AUTO_EXPOSURE;
	}
	if ( vk_rt_particle_volume_enabled() ) {
		flags |= RTX_RT_MODE_FLAG_PARTICLE_VOLUME;
	}
	if ( vk_rt_raster_reference_enabled() ) {
		flags |= RTX_RT_MODE_FLAG_RASTER_REFERENCE;
	}
	if ( vk_scene_linear_enabled() ) {
		flags |= RTX_RT_MODE_FLAG_SCENE_LINEAR_OUTPUT;
	}
	params.modes[3] = flags;

	if ( !vk_rt_upload_buffer_data(
		&vk.rt.temporal_params_buffer[ vk.cmd_index % NUM_COMMAND_BUFFERS ],
		&params,
		sizeof( params ) ) ) {
		return qfalse;
	}

	if ( temporalReset ) {
		vk.rt.history_valid = qfalse;
		vk.rt.temporal_reset_pending = qfalse;
		if ( logReset ) {
			if ( vk_rt_debug_as_stats_level() >= 1 ) {
				ri.Printf( PRINT_DEVELOPER,
					"RTX RT: temporal reset applied (cameraMove=%.2f cameraDot=%.3f cutoff=%.2f rdflags=0x%x->0x%x timeDelta=%d)\n",
					moveDistance,
					forwardDot,
					cutDistance,
					vk.rt.temporal_prev_rdflags,
					backEnd.refdef.rdflags,
					timeDelta );
			}
			vk.rt.stats.history_resets++;
		}
	}

	VectorCopy( currentOrigin, vk.rt.temporal_prev_origin );
	VectorCopy( currentForward, vk.rt.temporal_prev_forward );
	VectorCopy( currentRight, vk.rt.temporal_prev_right );
	VectorCopy( currentUp, vk.rt.temporal_prev_up );
	vk.rt.temporal_prev_tan_half_fov_x = halfFovX;
	vk.rt.temporal_prev_tan_half_fov_y = halfFovY;
	vk.rt.temporal_prev_refdef_time = refdefTime;
	vk.rt.temporal_prev_rdflags = backEnd.refdef.rdflags;
	vk.rt.temporal_prev_stereo_frame = (int)backEnd.refdef.stereoFrame;
	vk.rt.temporal_has_prev_camera = qtrue;

	return qtrue;
}

static void vk_rt_fill_push_constants( rtxRtPushConstants_t *push )
{
	const float halfFovX = tanf( DEG2RAD( backEnd.viewParms.fovX * 0.5f ) );
	const float halfFovY = tanf( DEG2RAD( backEnd.viewParms.fovY * 0.5f ) );
	vec3_t sunDir;
	vec3_t sunColor;

	Com_Memset( push, 0, sizeof( *push ) );
	push->cameraOriginTanHalfFovX[0] = backEnd.viewParms.or.origin[0];
	push->cameraOriginTanHalfFovX[1] = backEnd.viewParms.or.origin[1];
	push->cameraOriginTanHalfFovX[2] = backEnd.viewParms.or.origin[2];
	push->cameraOriginTanHalfFovX[3] = halfFovX;

	push->cameraForwardTanHalfFovY[0] = backEnd.viewParms.or.axis[0][0];
	push->cameraForwardTanHalfFovY[1] = backEnd.viewParms.or.axis[0][1];
	push->cameraForwardTanHalfFovY[2] = backEnd.viewParms.or.axis[0][2];
	push->cameraForwardTanHalfFovY[3] = halfFovY;

	push->cameraRightWidth[0] = -backEnd.viewParms.or.axis[1][0];
	push->cameraRightWidth[1] = -backEnd.viewParms.or.axis[1][1];
	push->cameraRightWidth[2] = -backEnd.viewParms.or.axis[1][2];
	push->cameraRightWidth[3] = (float)vk.rt.output_width;

	push->cameraUpHeight[0] = backEnd.viewParms.or.axis[2][0];
	push->cameraUpHeight[1] = backEnd.viewParms.or.axis[2][1];
	push->cameraUpHeight[2] = backEnd.viewParms.or.axis[2][2];
	push->cameraUpHeight[3] = (float)vk.rt.output_height;

	vk_rt_resolve_sun_params( sunDir, sunColor, NULL );

	push->sunDirection[0] = sunDir[0];
	push->sunDirection[1] = sunDir[1];
	push->sunDirection[2] = sunDir[2];
	push->sunDirection[3] = 0.0f;

	push->sunColorIntensity[0] = sunColor[0];
	push->sunColorIntensity[1] = sunColor[1];
	push->sunColorIntensity[2] = sunColor[2];
	push->sunColorIntensity[3] = vk_rt_sun_intensity();

	push->debugMode = vk.rt.activeVisualizer;
	push->frameIndex = tr.frameCount;
	push->activeInstances = vk.rt.tlas_instance_count;
	push->lightCount = vk.rt.light_count;
	push->worldMaterialCount = vk.rt.world_material_count;
	push->dynamicMaterialCount = vk.rt.dynamic_material_count;
	push->shadowMode = (uint32_t)Com_Clamp( 0, 2, vk_rt_shadow_mode() );
	push->shadowSoftness = vk_rt_shadow_softness();
	push->indirectStrength = vk_rt_indirect_strength();
	push->reflectionStrength = vk_rt_reflection_strength();
	push->skyIntensity = vk_rt_sky_intensity();
	push->refractionIor = vk_rt_refractive_ior();
	push->sunIntensity = vk_rt_sun_intensity();
	push->refractiveMode = (uint32_t)Com_Clamp( 0, 2, vk_rt_refractive_mode() );
	push->indirectBounce = vk_rt_indirect_bounce_enabled() ? 1u : 0u;
}

static qboolean vk_rt_init( void )
{
	if ( vk.rt.initialized ) {
		return qtrue;
	}

	vk_rt_reset_state();

	vk.rt.rt_pipeline_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
	vk.rt.rt_pipeline_props.pNext = &vk.rt.as_props;
	vk.rt.as_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
	vk.rt.as_props.pNext = NULL;

	if ( !qvkGetPhysicalDeviceProperties2 && !qvkGetPhysicalDeviceProperties2KHR ) {
		ri.Printf( PRINT_ERROR,
			"RTX RT: vkGetPhysicalDeviceProperties2/core-or-KHR is unavailable\n" );
		return qfalse;
	}

	{
		VkPhysicalDeviceProperties2 props2;
		Com_Memset( &props2, 0, sizeof( props2 ) );
		props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		props2.pNext = &vk.rt.rt_pipeline_props;
		if ( qvkGetPhysicalDeviceProperties2 ) {
			qvkGetPhysicalDeviceProperties2( vk.physical_device, &props2 );
		} else {
			qvkGetPhysicalDeviceProperties2KHR( vk.physical_device, &props2 );
		}
		vk.rt.gpu_timestamp_period_ns = ( props2.properties.limits.timestampPeriod > 0.0f ) ?
			props2.properties.limits.timestampPeriod : 1.0f;
	}

	vk.rt.needsRebuild = qtrue;
	vk.rt.enabled = qtrue;
	vk.rt.initialized = qtrue;
	return qtrue;
}

void vk_rt_invalidate( const char *reason )
{
	int i;

	if ( !vk.rt.initialized ) {
		return;
	}

	if ( vk_rt_debug_as_stats_level() >= 1 ) {
		ri.Printf( PRINT_DEVELOPER, "RTX RT: invalidating RT state (%s)\n", reason ? reason : "unspecified" );
	}

	if ( !vk_rt_wait_for_inflight_frames( reason ? reason : "RT state invalidation" ) ) {
		return;
	}

	vk_rt_destroy_as( &vk.rt.world_blas );
	vk_rt_destroy_buffer( &vk.rt.world_vertex_buffer );
	vk_rt_destroy_buffer( &vk.rt.world_index_buffer );
	vk_rt_destroy_buffer( &vk.rt.world_material_buffer );
	vk_rt_destroy_as( &vk.rt.dynamic_blas );
	vk_rt_destroy_buffer( &vk.rt.dynamic_vertex_buffer );
	vk_rt_destroy_buffer( &vk.rt.dynamic_index_buffer );
	vk_rt_destroy_buffer( &vk.rt.dynamic_material_buffer );
	vk_rt_destroy_as( &vk.rt.tlas );
	vk_rt_destroy_buffer( &vk.rt.tlas_instance_buffer );
	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		vk_rt_destroy_buffer( &vk.rt.light_buffer[i] );
	}

	vk.rt.world_vertex_count = 0;
	vk.rt.world_index_count = 0;
	vk.rt.world_material_count = 0;
	vk.rt.world_masked_triangle_count = 0;
	vk.rt.dynamic_vertex_count = 0;
	vk.rt.dynamic_index_count = 0;
	vk.rt.dynamic_material_count = 0;
	vk.rt.dynamic_entity_count = 0;
	vk.rt.dynamic_effect_count = 0;
	vk.rt.dynamic_masked_triangle_count = 0;
	vk.rt.light_count = 0;
	vk.rt.tlas_instance_count = 0;
	vk.rt.tlas_instance_capacity = 0;
	vk.rt.stats.world_triangles = 0;
	vk.rt.stats.dynamic_triangles = 0;
	vk.rt.stats.masked_triangles = 0;
	vk.rt.stats.active_lights = 0;
	vk.rt.worldGeometryReady = qfalse;
	vk.rt.tlasValid = qfalse;
	vk.rt.dispatchValid = qfalse;
	vk.rt.descriptorsValid = qfalse;
	vk.rt.needsRebuild = qtrue;
	vk.rt.generation++;
	vk_rt_reset_temporal_history( reason );
	vk.rt.history_read_index = 0;
	vk.rt.temporal_has_prev_camera = qfalse;
	vk.rt.post_validation_logged = qfalse;
	vk.rt.dynamic_scene_signature = 0u;
	vk.rt.dynamic_last_rebuild_frame = -1;
	VectorClear( vk.rt.dynamic_centroid_current );
	VectorClear( vk.rt.dynamic_centroid_prev );
	VectorClear( vk.rt.dynamic_last_rebuild_centroid );
	vk_rt_reset_world_light_cache();
}

static void vk_rt_shutdown( void )
{
	int i;

	if ( !vk.rt.initialized ) {
		return;
	}

	vk_rt_destroy_output_image();
	vk_rt_destroy_buffer( &vk.rt.sbt_buffer );
	vk_rt_destroy_as( &vk.rt.tlas );
	vk_rt_destroy_as( &vk.rt.dynamic_blas );
	vk_rt_destroy_as( &vk.rt.world_blas );
	vk_rt_destroy_buffer( &vk.rt.tlas_instance_buffer );
	vk_rt_destroy_buffer( &vk.rt.dynamic_vertex_buffer );
	vk_rt_destroy_buffer( &vk.rt.dynamic_index_buffer );
	vk_rt_destroy_buffer( &vk.rt.dynamic_material_buffer );
	vk_rt_destroy_buffer( &vk.rt.world_vertex_buffer );
	vk_rt_destroy_buffer( &vk.rt.world_index_buffer );
	vk_rt_destroy_buffer( &vk.rt.world_material_buffer );
	vk_rt_destroy_buffer( &vk.rt.scratch_buffer );
	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		vk_rt_destroy_buffer( &vk.rt.light_buffer[i] );
		vk_rt_destroy_buffer( &vk.rt.temporal_params_buffer[i] );
	}

	if ( vk.rt.pipeline != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.rt.pipeline, NULL );
		vk.rt.pipeline = VK_NULL_HANDLE;
	}
	if ( vk.rt.pipeline_layout != VK_NULL_HANDLE ) {
		qvkDestroyPipelineLayout( vk.device, vk.rt.pipeline_layout, NULL );
		vk.rt.pipeline_layout = VK_NULL_HANDLE;
	}
	if ( vk.rt.descriptor_set_layout != VK_NULL_HANDLE ) {
		qvkDestroyDescriptorSetLayout( vk.device, vk.rt.descriptor_set_layout, NULL );
		vk.rt.descriptor_set_layout = VK_NULL_HANDLE;
	}
	if ( vk.rt.descriptor_pool != VK_NULL_HANDLE ) {
		qvkDestroyDescriptorPool( vk.device, vk.rt.descriptor_pool, NULL );
		vk.rt.descriptor_pool = VK_NULL_HANDLE;
	}
	if ( vk.rt.compaction_query_pool != VK_NULL_HANDLE ) {
		qvkDestroyQueryPool( vk.device, vk.rt.compaction_query_pool, NULL );
		vk.rt.compaction_query_pool = VK_NULL_HANDLE;
	}
	if ( vk.rt.timing_query_pool != VK_NULL_HANDLE ) {
		qvkDestroyQueryPool( vk.device, vk.rt.timing_query_pool, NULL );
		vk.rt.timing_query_pool = VK_NULL_HANDLE;
	}

	vk_rt_reset_state();
}

qboolean vk_rt_primary_view_eligible( void )
{
	return ( vk.caps.activeRtMode == 2 &&
		vk.fboActive &&
		!( backEnd.refdef.rdflags & ( RDF_NOWORLDMODEL | RDF_HYPERSPACE ) ) &&
		vk.renderPassIndex == RENDER_PASS_MAIN &&
		backEnd.viewParms.portalView == PV_NONE &&
		backEnd.viewParms.stereoFrame == STEREO_CENTER &&
		!glConfig.stereoEnabled &&
		backEnd.viewParms.viewportX == 0 &&
		backEnd.viewParms.viewportY == 0 &&
		backEnd.viewParms.viewportWidth == glConfig.vidWidth &&
		backEnd.viewParms.viewportHeight == glConfig.vidHeight &&
		!( vk.rt.dispatchValid &&
			vk.rt.lastFrameBuilt == (uint32_t)tr.frameCount ) ) ?
			qtrue : qfalse;
}

qboolean vk_rt_trace_frame( void )
{
	static int lastFboSkipFrame = -1;
	static int lastUiSkipFrame = -1;
	static int lastHyperspaceSkipFrame = -1;
	static int lastNonPrimarySkipFrame = -1;
	VkDeviceSize budgetBytes = 0;
	VkDeviceSize frameBuildBytesUsed = 0;
	VkImageMemoryBarrier sceneColorBarrier;
	VkImageMemoryBarrier traceBarriers[3];
	rtxRtPushConstants_t push;
	int budgetMb;
	int64_t cpuFrameStartUs = 0;
	int64_t cpuStageStartUs = 0;
	int64_t cpuNowUs = 0;
	qboolean needCpuTiming = qfalse;
	qboolean timingEnabled = qfalse;
	uint32_t timingBaseQuery = 0u;
	qboolean copiedToColor = qfalse;

	if ( !vk_rt_trace_available() ) {
		return qfalse;
	}

	if ( !vk.active || !vk.cmd || vk.cmd->command_buffer == VK_NULL_HANDLE ) {
		return qfalse;
	}

	if ( !vk.rt.initialized && !vk_rt_init() ) {
		return qfalse;
	}

	if ( !tr.worldMapLoaded || !tr.world ) {
		vk.rt.stats.skipped_dispatches++;
		return qfalse;
	}

	if ( !vk.fboActive ) {
		vk.rt.stats.skipped_dispatches++;
		if ( vk_rt_debug_should_log_skip( &lastFboSkipFrame ) ) {
			ri.Printf( PRINT_DEVELOPER, "RTX RT: skipping dispatch because r_fbo is disabled\n" );
		}
		return qfalse;
	}

	if ( vk_rt_ui_passthrough_enabled() && ( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) ) {
		vk.rt.stats.skipped_dispatches++;
		if ( vk_rt_debug_should_log_skip( &lastUiSkipFrame ) ) {
			ri.Printf( PRINT_DEVELOPER, "RTX RT: skipping dispatch for RDF_NOWORLDMODEL UI/console composition view\n" );
		}
		return qfalse;
	}
	if ( backEnd.refdef.rdflags & RDF_HYPERSPACE ) {
		vk.rt.stats.skipped_dispatches++;
		if ( vk_rt_debug_should_log_skip( &lastHyperspaceSkipFrame ) ) {
			ri.Printf( PRINT_DEVELOPER, "RTX RT: skipping dispatch for RDF_HYPERSPACE view\n" );
		}
		return qfalse;
	}

	/*
	 * Portal/mirror views and screen-map capture commands render into
	 * sub-views of the frame.  The RT output is a full-size image, so tracing
	 * them here would overwrite unrelated pixels and invalidate resources
	 * referenced by earlier commands.  Their raster result is composited by
	 * the primary view's portal shader instead.
	 */
	if ( !vk_rt_primary_view_eligible() ) {
		vk.rt.stats.skipped_dispatches++;
		if ( vk_rt_debug_should_log_skip( &lastNonPrimarySkipFrame ) ) {
			ri.Printf( PRINT_DEVELOPER,
				"RTX RT: skipping non-primary/duplicate view (renderPass=%d portalView=%d stereo=%d viewport=%d,%d %dx%d)\n",
				(int)vk.renderPassIndex,
				(int)backEnd.viewParms.portalView,
				(int)backEnd.viewParms.stereoFrame,
				backEnd.viewParms.viewportX,
				backEnd.viewParms.viewportY,
				backEnd.viewParms.viewportWidth,
				backEnd.viewParms.viewportHeight );
		}
		return qfalse;
	}

	vk_rt_read_timing_query_results();
	vk_rt_update_scalability_controller();
	vk_rt_handle_cached_resource_cvar_changes();

	needCpuTiming = ( vk_rt_perf_timing_mode() > 0 || vk_rt_adaptive_budget_enabled() || vk_rt_dynamic_resolution_enabled() ) ? qtrue : qfalse;
	if ( needCpuTiming ) {
		cpuFrameStartUs = vk_rt_now_microseconds();
		vk.rt.perf.cpu_dynamic_ms = 0.0f;
		vk.rt.perf.cpu_world_ms = 0.0f;
		vk.rt.perf.cpu_tlas_ms = 0.0f;
		vk.rt.perf.cpu_temporal_ms = 0.0f;
		vk.rt.perf.cpu_dispatch_ms = 0.0f;
		vk.rt.perf.cpu_copy_ms = 0.0f;
	}

	budgetMb = vk_rt_as_build_budget_mb();
	if ( budgetMb > 0 ) {
		budgetBytes = (VkDeviceSize)budgetMb * 1024u * 1024u;
	}

	vk.rt.activeVisualizer = vk_rt_clamped_visualizer_mode();

	if ( !vk_rt_ensure_pipeline() || !vk_rt_ensure_output_image() ) {
		vk.rt.stats.skipped_dispatches++;
		return qfalse;
	}

	/* Resolve the complete output-to-scene composition plan before ending the
	 * raster pass.  A permissive failure therefore leaves the complete raster
	 * frame and its render pass intact; strict mode can fail closed upstream. */
	if ( vk.rt.output_width != (uint32_t)glConfig.vidWidth ||
		vk.rt.output_height != (uint32_t)glConfig.vidHeight ||
		vk.rt.output_format != vk.color_format ) {
		if ( !vk_rt_supports_reconstruction_blit() ) {
			if ( !s_vkRtDynResBlitWarned ) {
				ri.Printf( PRINT_WARNING,
					"RTX RT: no valid output composition path for %s -> %s; preserving the complete raster frame\n",
					vk_format_string( vk.rt.output_format ),
					vk_format_string( vk.color_format ) );
				s_vkRtDynResBlitWarned = qtrue;
			}
			vk.rt.stats.skipped_dispatches++;
			return qfalse;
		}
	}

	if ( needCpuTiming ) {
		cpuStageStartUs = vk_rt_now_microseconds();
	}
	if ( !vk_rt_ensure_dynamic_blas( &frameBuildBytesUsed, budgetBytes ) ) {
		vk.rt.stats.skipped_dispatches++;
		return qfalse;
	}
	if ( needCpuTiming ) {
		cpuNowUs = vk_rt_now_microseconds();
		vk.rt.perf.cpu_dynamic_ms = vk_rt_elapsed_ms( cpuStageStartUs, cpuNowUs );
	}

	if ( needCpuTiming ) {
		cpuStageStartUs = vk_rt_now_microseconds();
	}
	if ( !vk_rt_ensure_world_blas( &frameBuildBytesUsed, budgetBytes ) ) {
		vk.rt.stats.skipped_dispatches++;
		return qfalse;
	}
	if ( needCpuTiming ) {
		cpuNowUs = vk_rt_now_microseconds();
		vk.rt.perf.cpu_world_ms = vk_rt_elapsed_ms( cpuStageStartUs, cpuNowUs );
	}

	if ( needCpuTiming ) {
		cpuStageStartUs = vk_rt_now_microseconds();
	}
	if ( !vk_rt_build_tlas( &frameBuildBytesUsed, budgetBytes ) ) {
		vk.rt.stats.skipped_dispatches++;
		return qfalse;
	}
	if ( needCpuTiming ) {
		cpuNowUs = vk_rt_now_microseconds();
		vk.rt.perf.cpu_tlas_ms = vk_rt_elapsed_ms( cpuStageStartUs, cpuNowUs );
	}

	if ( needCpuTiming ) {
		cpuStageStartUs = vk_rt_now_microseconds();
	}
	if ( !vk_rt_update_temporal_state() ) {
		vk.rt.stats.skipped_dispatches++;
		return qfalse;
	}
	if ( needCpuTiming ) {
		cpuNowUs = vk_rt_now_microseconds();
		vk.rt.perf.cpu_temporal_ms = vk_rt_elapsed_ms( cpuStageStartUs, cpuNowUs );
	}

	if ( !vk_rt_update_descriptor_set() || !vk.rt.sbtValid ) {
		vk.rt.stats.skipped_dispatches++;
		return qfalse;
	}

	vk_end_render_pass();

	/*
	 * The raster reference is sampled directly by raygen.  Keep this barrier
	 * local to the active RT path so raster-only devices never see an RT stage
	 * in their render-pass dependencies.
	 */
	Com_Memset( &sceneColorBarrier, 0, sizeof( sceneColorBarrier ) );
	sceneColorBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	sceneColorBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	sceneColorBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	sceneColorBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	sceneColorBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	sceneColorBarrier.image = vk.color_image;
	sceneColorBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	sceneColorBarrier.subresourceRange.levelCount = 1;
	sceneColorBarrier.subresourceRange.layerCount = 1;
	sceneColorBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	sceneColorBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	qvkCmdPipelineBarrier(
		vk.cmd->command_buffer,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
		0,
		0, NULL,
		0, NULL,
		1, &sceneColorBarrier );

	if ( vk_rt_ensure_timing_query_pool() &&
		vk.rt.timing_query_pool != VK_NULL_HANDLE &&
		qvkCmdResetQueryPool &&
		qvkCmdWriteTimestamp ) {
		const uint32_t querySlot = (uint32_t)( tr.frameCount & 1u );
		timingBaseQuery = querySlot * vk.rt.timing_query_stride;
		qvkCmdResetQueryPool( vk.cmd->command_buffer, vk.rt.timing_query_pool, timingBaseQuery, vk.rt.timing_query_stride );
		qvkCmdWriteTimestamp(
			vk.cmd->command_buffer,
			VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
			vk.rt.timing_query_pool,
			timingBaseQuery + 0u );
		timingEnabled = qtrue;
	}

	if ( needCpuTiming ) {
		cpuStageStartUs = vk_rt_now_microseconds();
	}

	Com_Memset( traceBarriers, 0, sizeof( traceBarriers ) );
	{
		const uint32_t historyReadIndex = vk.rt.history_read_index & 1u;
		const uint32_t historyWriteIndex = historyReadIndex ^ 1u;
		int barrierIndex;

		for ( barrierIndex = 0; barrierIndex < ARRAY_LEN( traceBarriers ); barrierIndex++ ) {
			traceBarriers[barrierIndex].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			traceBarriers[barrierIndex].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
			traceBarriers[barrierIndex].newLayout = VK_IMAGE_LAYOUT_GENERAL;
			traceBarriers[barrierIndex].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			traceBarriers[barrierIndex].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			traceBarriers[barrierIndex].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			traceBarriers[barrierIndex].subresourceRange.baseMipLevel = 0;
			traceBarriers[barrierIndex].subresourceRange.levelCount = 1;
			traceBarriers[barrierIndex].subresourceRange.baseArrayLayer = 0;
			traceBarriers[barrierIndex].subresourceRange.layerCount = 1;
			traceBarriers[barrierIndex].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		}

		traceBarriers[0].image = vk.rt.output_image;
		traceBarriers[0].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		traceBarriers[1].image = vk.rt.history_image[historyReadIndex];
		traceBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		traceBarriers[2].image = vk.rt.history_image[historyWriteIndex];
		traceBarriers[2].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	}
	qvkCmdPipelineBarrier(
		vk.cmd->command_buffer,
		VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
		VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
		0,
		0, NULL,
		0, NULL,
		ARRAY_LEN( traceBarriers ), traceBarriers );

	qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, vk.rt.pipeline );
	qvkCmdBindDescriptorSets(
		vk.cmd->command_buffer,
		VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
		vk.rt.pipeline_layout,
		0,
		1,
		&vk.rt.descriptor_set[ vk.cmd_index % NUM_COMMAND_BUFFERS ],
		0,
		NULL );

	vk_rt_fill_push_constants( &push );
	qvkCmdPushConstants(
		vk.cmd->command_buffer,
		vk.rt.pipeline_layout,
		VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR |
			VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
		0,
		sizeof( push ),
		&push );

	qvkCmdTraceRaysKHR(
		vk.cmd->command_buffer,
		&vk.rt.sbt_raygen,
		&vk.rt.sbt_miss,
		&vk.rt.sbt_hit,
		&vk.rt.sbt_callable,
		vk.rt.output_width,
		vk.rt.output_height,
		1 );

	if ( timingEnabled ) {
		qvkCmdWriteTimestamp(
			vk.cmd->command_buffer,
			VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
			vk.rt.timing_query_pool,
			timingBaseQuery + 1u );
	}
	if ( needCpuTiming ) {
		cpuNowUs = vk_rt_now_microseconds();
		vk.rt.perf.cpu_dispatch_ms = vk_rt_elapsed_ms( cpuStageStartUs, cpuNowUs );
		cpuStageStartUs = vk_rt_now_microseconds();
	}

	if ( vk.fboActive ) {
		VkImageMemoryBarrier copyBarriers[2];
		VkImageCopy copyRegion;
		const qboolean needsFormatConversion =
			( vk.rt.output_format != vk.color_format ) ? qtrue : qfalse;
		const qboolean needsReconstruction =
			( vk.rt.output_width != (uint32_t)glConfig.vidWidth ||
				vk.rt.output_height != (uint32_t)glConfig.vidHeight ) ? qtrue : qfalse;
		const qboolean needsBlit = ( needsReconstruction || needsFormatConversion ) ? qtrue : qfalse;
		const qboolean canReconstructionBlit = needsBlit ? vk_rt_supports_reconstruction_blit() : qfalse;

		Com_Memset( copyBarriers, 0, sizeof( copyBarriers ) );
		copyBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		copyBarriers[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		copyBarriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		copyBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		copyBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		copyBarriers[0].image = vk.rt.output_image;
		copyBarriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyBarriers[0].subresourceRange.levelCount = 1;
		copyBarriers[0].subresourceRange.layerCount = 1;
		copyBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		copyBarriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

		copyBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		copyBarriers[1].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		copyBarriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		copyBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		copyBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		copyBarriers[1].image = vk.color_image;
		copyBarriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyBarriers[1].subresourceRange.levelCount = 1;
		copyBarriers[1].subresourceRange.layerCount = 1;
		copyBarriers[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		copyBarriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		qvkCmdPipelineBarrier(
			vk.cmd->command_buffer,
			VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0,
			0, NULL,
			0, NULL,
			2, copyBarriers );

		Com_Memset( &copyRegion, 0, sizeof( copyRegion ) );
		copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.srcSubresource.layerCount = 1;
		copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.dstSubresource.layerCount = 1;
		copyRegion.extent.depth = 1;

		if ( needsBlit && canReconstructionBlit ) {
			VkImageBlit blitRegion;
			Com_Memset( &blitRegion, 0, sizeof( blitRegion ) );
			blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blitRegion.srcSubresource.layerCount = 1;
			blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blitRegion.dstSubresource.layerCount = 1;
			blitRegion.srcOffsets[1].x = (int32_t)vk.rt.output_width;
			blitRegion.srcOffsets[1].y = (int32_t)vk.rt.output_height;
			blitRegion.srcOffsets[1].z = 1;
			blitRegion.dstOffsets[1].x = glConfig.vidWidth;
			blitRegion.dstOffsets[1].y = glConfig.vidHeight;
			blitRegion.dstOffsets[1].z = 1;

			qvkCmdBlitImage(
				vk.cmd->command_buffer,
				vk.rt.output_image,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				vk.color_image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1,
				&blitRegion,
				VK_FILTER_LINEAR );
			copiedToColor = qtrue;
		} else if ( !needsBlit ) {
			copyRegion.extent.width = vk.rt.output_width;
			copyRegion.extent.height = vk.rt.output_height;

			qvkCmdCopyImage(
				vk.cmd->command_buffer,
				vk.rt.output_image,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				vk.color_image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1,
				&copyRegion );
			copiedToColor = qtrue;
		} else {
			if ( needsBlit && !canReconstructionBlit && !s_vkRtDynResBlitWarned ) {
				ri.Printf( PRINT_WARNING,
					"RTX RT: output blit path unavailable for output/color formats (%s -> %s)%s%s; preserving the complete raster frame\n",
					vk_format_string( vk.rt.output_format ),
					vk_format_string( vk.color_format ),
					needsReconstruction ? ", reconstruction requested" : "",
					needsFormatConversion ? ", format conversion required" : "" );
				s_vkRtDynResBlitWarned = qtrue;
			}
		}

		copyBarriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		copyBarriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
		copyBarriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		copyBarriers[0].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

		copyBarriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		copyBarriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		copyBarriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		copyBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
			VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		qvkCmdPipelineBarrier(
			vk.cmd->command_buffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR |
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0,
			0, NULL,
			0, NULL,
			2, copyBarriers );
	}
	if ( timingEnabled ) {
		qvkCmdWriteTimestamp(
			vk.cmd->command_buffer,
			copiedToColor ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
			vk.rt.timing_query_pool,
			timingBaseQuery + 2u );
		qvkCmdWriteTimestamp(
			vk.cmd->command_buffer,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			vk.rt.timing_query_pool,
			timingBaseQuery + 3u );
	}
	if ( needCpuTiming ) {
		cpuNowUs = vk_rt_now_microseconds();
		vk.rt.perf.cpu_copy_ms = vk_rt_elapsed_ms( cpuStageStartUs, cpuNowUs );
	}

	vk_begin_post_bloom_render_pass();

	vk.rt.dispatchValid = qtrue;
	vk.rt.lastFrameBuilt = tr.frameCount;
	vk.rt.stats.dispatch_count++;
	vk.rt.history_read_index ^= 1u;
	vk.rt.history_valid = qtrue;
	VectorCopy( vk.rt.dynamic_centroid_current, vk.rt.dynamic_centroid_prev );
	if ( needCpuTiming ) {
		cpuNowUs = vk_rt_now_microseconds();
		vk.rt.perf.cpu_frame_ms = vk_rt_elapsed_ms( cpuFrameStartUs, cpuNowUs );
	}
	vk_rt_log_perf_summary();

	if ( copiedToColor && vk_rt_post_validate_enabled() && !vk.rt.post_validation_logged ) {
		ri.Printf( PRINT_ALL,
			"RTX RT post stack validation: trace->copy->post_bloom->gamma ordering active (HUD/UI/cinematics remain in post-bloom/main composition path)\n" );
		ri.Printf( PRINT_ALL,
			"RTX RT primary dispatch validation: vkCmdTraceRaysKHR output copied to scene color; shading=%s; raster_role=depth_fallback_overlay\n",
			vk_rt_raster_reference_enabled() ? "raster_reference_compat" : "native_rt" );
		vk.rt.post_validation_logged = qtrue;
	}

	if ( vk_rt_debug_as_stats_level() >= 2 && ( ( tr.frameCount & 127 ) == 0 ) ) {
		ri.Printf( PRINT_DEVELOPER,
			"RTX RT stats: world=%lluB dynamic=%lluB tlas=%lluB compact_saved=%lluB dispatch=%llu skipped=%llu throttled=%llu historyResets=%llu tris(world=%llu dynamic=%llu masked=%llu) mats(world=%u dynamic=%u) dyn(entities=%u effects=%u) lights=%llu\n",
			(unsigned long long)vk.rt.stats.world_build_bytes,
			(unsigned long long)vk.rt.stats.dynamic_build_bytes,
			(unsigned long long)vk.rt.stats.tlas_build_bytes,
			(unsigned long long)vk.rt.stats.compacted_bytes_saved,
			(unsigned long long)vk.rt.stats.dispatch_count,
			(unsigned long long)vk.rt.stats.skipped_dispatches,
			(unsigned long long)vk.rt.stats.throttled_builds,
			(unsigned long long)vk.rt.stats.history_resets,
			(unsigned long long)vk.rt.stats.world_triangles,
			(unsigned long long)vk.rt.stats.dynamic_triangles,
			(unsigned long long)vk.rt.stats.masked_triangles,
			vk.rt.world_material_count,
			vk.rt.dynamic_material_count,
			vk.rt.dynamic_entity_count,
			vk.rt.dynamic_effect_count,
			(unsigned long long)vk.rt.stats.active_lights );
	}

	return copiedToColor;
}

static const char *vk_capability_value( qboolean supported )
{
	return supported ? "yes" : "no";
}

static void vk_report_capabilities( const char *context )
{
	const int reportLevel = vk_caps_report_level();

	if ( reportLevel <= 0 ) {
		return;
	}

	ri.Printf( PRINT_ALL,
		"RTX capabilities (%s): swapchain=%s debug_markers=%s descriptor_indexing=%s buffer_device_address=%s\n",
		context ? context : "runtime",
		vk_capability_value( vk.caps.swapchain ),
		vk_capability_value( vk.caps.debugMarkers ),
		vk_capability_value( vk.caps.descriptorIndexing ),
		vk_capability_value( vk.caps.bufferDeviceAddress ) );

	if ( reportLevel >= 2 ) {
		ri.Printf( PRINT_ALL,
			"RTX capabilities (%s): timeline_semaphore=%s memory_model=%s deferred_host_ops=%s dedicated_allocation=%s\n",
			context ? context : "runtime",
			vk_capability_value( vk.caps.timelineSemaphore ),
			vk_capability_value( vk.caps.vulkanMemoryModel ),
			vk_capability_value( vk.caps.deferredHostOperations ),
			vk_capability_value( vk.caps.dedicatedAllocation ) );
		ri.Printf( PRINT_ALL,
			"RTX capabilities (%s): acceleration_structure=%s ray_query=%s ray_tracing_pipeline=%s\n",
			context ? context : "runtime",
			vk_capability_value( vk.caps.accelerationStructure ),
			vk_capability_value( vk.caps.rayQuery ),
			vk_capability_value( vk.caps.rayTracingPipeline ) );
	}

	ri.Printf( PRINT_ALL, "RTX capability gate: requested=%s (%d), active=%s (%d), require=%d\n",
		vk_rt_mode_name( vk.caps.requestedRtMode ), vk.caps.requestedRtMode,
		vk_rt_mode_name( vk.caps.activeRtMode ), vk.caps.activeRtMode,
		vk_rt_mode_required() ? 1 : 0 );
}

static const char *vk_lifetime_state_name( rtxVkLifetimeState_t state )
{
	switch ( state ) {
	case RTX_VK_LIFETIME_UNINITIALIZED: return "uninitialized";
	case RTX_VK_LIFETIME_INITIALIZING: return "initializing";
	case RTX_VK_LIFETIME_READY: return "ready";
	case RTX_VK_LIFETIME_RESIZING: return "resizing";
	case RTX_VK_LIFETIME_SHUTTING_DOWN: return "shutting_down";
	default: return "unknown";
	}
}

static const char *vk_lifetime_resource_name( rtxVkResource_t resource )
{
	switch ( resource ) {
	case RTX_VK_RESOURCE_SYNC_PRIMITIVES: return "sync_primitives";
	case RTX_VK_RESOURCE_SWAPCHAIN: return "swapchain";
	case RTX_VK_RESOURCE_ATTACHMENTS: return "attachments";
	case RTX_VK_RESOURCE_RENDER_PASSES: return "render_passes";
	case RTX_VK_RESOURCE_FRAMEBUFFERS: return "framebuffers";
	case RTX_VK_RESOURCE_GEOMETRY_BUFFERS: return "geometry_buffers";
	case RTX_VK_RESOURCE_STORAGE_BUFFER: return "storage_buffer";
	default: return "unknown";
	}
}

static void vk_lifetime_reset( void )
{
	Com_Memset( &s_vkLifetimeTracker, 0, sizeof( s_vkLifetimeTracker ) );
	s_vkLifetimeTracker.state = RTX_VK_LIFETIME_UNINITIALIZED;
}

static void vk_lifetime_set_state( rtxVkLifetimeState_t state, const char *reason )
{
	const int debugLevel = vk_lifetime_debug_level();

	s_vkLifetimeTracker.state = state;

	if ( debugLevel >= 2 ) {
		ri.Printf( PRINT_DEVELOPER, "RTX resource lifetime: state=%s (%s), generation=%u\n",
			vk_lifetime_state_name( state ), reason ? reason : "no-reason", s_vkLifetimeTracker.generation );
	}
}

static void vk_lifetime_mark_create( rtxVkResource_t resource, const char *context )
{
	rtxVkResourceEntry_t *entry = &s_vkLifetimeTracker.resources[ resource ];
	const int debugLevel = vk_lifetime_debug_level();

	s_vkLifetimeTracker.serial++;

	if ( entry->alive && debugLevel >= 1 ) {
		ri.Printf( PRINT_WARNING, "RTX resource lifetime: duplicate create of %s in %s (state=%s)\n",
			vk_lifetime_resource_name( resource ), context, vk_lifetime_state_name( s_vkLifetimeTracker.state ) );
	}

	entry->alive = qtrue;
	entry->createCount++;
	entry->lastSerial = s_vkLifetimeTracker.serial;
}

static void vk_lifetime_mark_destroy( rtxVkResource_t resource, const char *context )
{
	rtxVkResourceEntry_t *entry = &s_vkLifetimeTracker.resources[ resource ];
	const int debugLevel = vk_lifetime_debug_level();

	s_vkLifetimeTracker.serial++;

	if ( !entry->alive && debugLevel >= 1 ) {
		ri.Printf( PRINT_WARNING, "RTX resource lifetime: destroy of non-live %s in %s (state=%s)\n",
			vk_lifetime_resource_name( resource ), context, vk_lifetime_state_name( s_vkLifetimeTracker.state ) );
	}

	entry->alive = qfalse;
	entry->destroyCount++;
	entry->lastSerial = s_vkLifetimeTracker.serial;
}

static void vk_lifetime_mark_use( rtxVkResource_t resource, const char *context )
{
	rtxVkResourceEntry_t *entry = &s_vkLifetimeTracker.resources[ resource ];
	const int debugLevel = vk_lifetime_debug_level();

	if ( !entry->alive ) {
		if ( debugLevel >= 1 ) {
			ri.Printf( PRINT_WARNING, "RTX resource lifetime: use of non-live %s in %s (state=%s)\n",
				vk_lifetime_resource_name( resource ), context, vk_lifetime_state_name( s_vkLifetimeTracker.state ) );
		}
		return;
	}

	entry->useCount++;
	entry->lastUseFrame = tr.frameCount;
}

static uint32_t vk_lifetime_ready_mask( void )
{
	return
		( 1u << RTX_VK_RESOURCE_SYNC_PRIMITIVES ) |
		( 1u << RTX_VK_RESOURCE_SWAPCHAIN ) |
		( 1u << RTX_VK_RESOURCE_ATTACHMENTS ) |
		( 1u << RTX_VK_RESOURCE_RENDER_PASSES ) |
		( 1u << RTX_VK_RESOURCE_FRAMEBUFFERS ) |
		( 1u << RTX_VK_RESOURCE_GEOMETRY_BUFFERS ) |
		( 1u << RTX_VK_RESOURCE_STORAGE_BUFFER );
}

static void vk_lifetime_check_ready( const char *context )
{
	const int debugLevel = vk_lifetime_debug_level();
	uint32_t mask = vk_lifetime_ready_mask();
	uint32_t i;

	if ( debugLevel < 1 ) {
		return;
	}

	for ( i = 0; i < RTX_VK_RESOURCE_COUNT; i++ ) {
		if ( !( mask & ( 1u << i ) ) ) {
			continue;
		}

		if ( !s_vkLifetimeTracker.resources[i].alive ) {
			ri.Printf( PRINT_WARNING, "RTX resource lifetime: %s missing at %s\n",
				vk_lifetime_resource_name( (rtxVkResource_t)i ), context );
		}
	}
}

static void vk_lifetime_check_shutdown( const char *context )
{
	const int debugLevel = vk_lifetime_debug_level();
	uint32_t i;

	if ( debugLevel < 1 ) {
		return;
	}

	for ( i = 0; i < RTX_VK_RESOURCE_COUNT; i++ ) {
		if ( s_vkLifetimeTracker.resources[i].alive ) {
			ri.Printf( PRINT_WARNING, "RTX resource lifetime: %s still live at %s\n",
				vk_lifetime_resource_name( (rtxVkResource_t)i ), context );
		}
	}
}

static void vk_lifetime_begin_init( void )
{
	vk_lifetime_reset();
	s_vkSwapchainResizePending = qfalse;
	s_vkSwapchainResizeRestarting = qfalse;
	s_vkSwapchainResizeResult = VK_SUCCESS;
	vk_lifetime_set_state( RTX_VK_LIFETIME_INITIALIZING, "vk_initialize begin" );
}

static void vk_lifetime_end_init( void )
{
	vk_lifetime_check_ready( "vk_initialize end" );
	vk_lifetime_set_state( RTX_VK_LIFETIME_READY, "vk_initialize end" );
}

static void vk_lifetime_begin_resize( const char *context )
{
	const int debugLevel = vk_lifetime_debug_level();

	if ( s_vkLifetimeTracker.state != RTX_VK_LIFETIME_READY && debugLevel >= 1 ) {
		ri.Printf( PRINT_WARNING, "RTX resource lifetime: resize entered from %s state (%s)\n",
			vk_lifetime_state_name( s_vkLifetimeTracker.state ), context );
	}

	s_vkLifetimeTracker.generation++;
	vk_lifetime_set_state( RTX_VK_LIFETIME_RESIZING, context );
}

static void vk_lifetime_end_resize( const char *context )
{
	vk_lifetime_check_ready( context );
	vk_lifetime_set_state( RTX_VK_LIFETIME_READY, context );
}

static void vk_lifetime_begin_shutdown( void )
{
	vk_lifetime_set_state( RTX_VK_LIFETIME_SHUTTING_DOWN, "vk_shutdown begin" );
}

static void vk_lifetime_end_shutdown( void )
{
	vk_lifetime_check_shutdown( "vk_shutdown end" );
	vk_lifetime_set_state( RTX_VK_LIFETIME_UNINITIALIZED, "vk_shutdown end" );
	s_vkLifetimeTracker.frameActive = qfalse;
	s_vkSwapchainResizePending = qfalse;
	s_vkSwapchainResizeRestarting = qfalse;
	s_vkSwapchainResizeResult = VK_SUCCESS;
}

static void vk_lifetime_begin_frame( void )
{
	const int debugLevel = vk_lifetime_debug_level();

	if ( s_vkLifetimeTracker.state != RTX_VK_LIFETIME_READY && debugLevel >= 1 ) {
		ri.Printf( PRINT_WARNING, "RTX resource lifetime: frame begin while state=%s\n",
			vk_lifetime_state_name( s_vkLifetimeTracker.state ) );
	}

	if ( s_vkLifetimeTracker.frameActive && debugLevel >= 1 ) {
		ri.Printf( PRINT_WARNING, "RTX resource lifetime: nested frame begin detected\n" );
	}

	s_vkLifetimeTracker.frameActive = qtrue;
	s_vkLifetimeTracker.frameSerial++;

	vk_lifetime_mark_use( RTX_VK_RESOURCE_SYNC_PRIMITIVES, "vk_begin_frame" );
	vk_lifetime_mark_use( RTX_VK_RESOURCE_ATTACHMENTS, "vk_begin_frame" );
	vk_lifetime_mark_use( RTX_VK_RESOURCE_RENDER_PASSES, "vk_begin_frame" );
	vk_lifetime_mark_use( RTX_VK_RESOURCE_FRAMEBUFFERS, "vk_begin_frame" );
	vk_lifetime_mark_use( RTX_VK_RESOURCE_GEOMETRY_BUFFERS, "vk_begin_frame" );
	vk_lifetime_mark_use( RTX_VK_RESOURCE_STORAGE_BUFFER, "vk_begin_frame" );
}

static void vk_lifetime_end_frame( qboolean submitted )
{
	const int debugLevel = vk_lifetime_debug_level();

	if ( !s_vkLifetimeTracker.frameActive ) {
		if ( debugLevel >= 1 ) {
			ri.Printf( PRINT_WARNING, "RTX resource lifetime: frame end without active frame\n" );
		}
		return;
	}

	if ( submitted ) {
		vk_lifetime_mark_use( RTX_VK_RESOURCE_SYNC_PRIMITIVES, "vk_end_frame submit" );
	}

	s_vkLifetimeTracker.frameActive = qfalse;
}

static void vk_schedule_swapchain_resize( const char *context, VkResult res, const char *reason )
{
	const int debugLevel = vk_lifetime_debug_level();
	const qboolean shouldLog =
		( !s_vkSwapchainResizePending || s_vkSwapchainResizeResult != res || debugLevel >= 2 );

	s_vkSwapchainResizePending = qtrue;
	s_vkSwapchainResizeResult = res;

	if ( shouldLog ) {
		ri.Printf( PRINT_WARNING, "Vulkan: swapchain resize deferred in %s (%s)%s%s\n",
			context ? context : "unknown",
			vk_result_string( res ),
			reason ? ": " : "",
			reason ? reason : "" );
	}
}

static void vk_clear_scheduled_swapchain_resize( void )
{
	s_vkSwapchainResizePending = qfalse;
	s_vkSwapchainResizeResult = VK_SUCCESS;
}

static qboolean vk_swapchain_can_restart( void )
{
	if ( ri.CL_IsMinimized() ) {
		return qfalse;
	}

	if ( gls.windowWidth <= 0 || gls.windowHeight <= 0 ) {
		return qfalse;
	}

	return qtrue;
}

static uint32_t find_memory_type( uint32_t memory_type_bits, VkMemoryPropertyFlags properties ) {
	VkPhysicalDeviceMemoryProperties memory_properties;
	uint32_t i;

	qvkGetPhysicalDeviceMemoryProperties( vk.physical_device, &memory_properties );

	for ( i = 0; i < memory_properties.memoryTypeCount; i++ ) {
		if ((memory_type_bits & (1 << i)) != 0 &&
			(memory_properties.memoryTypes[i].propertyFlags & properties) == properties) {
			return i;
		}
	}
	ri.Error( ERR_FATAL, "Vulkan: failed to find matching memory type with requested properties" );
	return ~0U;
}


static uint32_t find_memory_type2( uint32_t memory_type_bits, VkMemoryPropertyFlags properties, VkMemoryPropertyFlags *outprops ) {
	VkPhysicalDeviceMemoryProperties memory_properties;
	uint32_t i;

	qvkGetPhysicalDeviceMemoryProperties( vk.physical_device, &memory_properties );

	for ( i = 0; i < memory_properties.memoryTypeCount; i++ ) {
		if ( (memory_type_bits & (1 << i)) != 0 && (memory_properties.memoryTypes[i].propertyFlags & properties) == properties ) {
			if ( outprops ) {
				*outprops = memory_properties.memoryTypes[i].propertyFlags;
			}
			return i;
		}
	}

	return ~0U;
}


static const char *pmode_to_str( VkPresentModeKHR mode )
{
	static char buf[32];

	switch ( mode ) {
		case VK_PRESENT_MODE_IMMEDIATE_KHR: return "IMMEDIATE";
		case VK_PRESENT_MODE_MAILBOX_KHR: return "MAILBOX";
		case VK_PRESENT_MODE_FIFO_KHR: return "FIFO";
		case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO_RELAXED";
		case VK_PRESENT_MODE_FIFO_LATEST_READY_EXT: return "FIFO_LATEST_READY";
		default: sprintf( buf, "mode#%x", mode ); return buf;
	};
}


#define CASE_STR(x) case (x): return #x

const char *vk_format_string( VkFormat format )
{
	static char buf[16];

	switch ( format ) {
		// color formats
		CASE_STR( VK_FORMAT_R5G5B5A1_UNORM_PACK16 );
		CASE_STR( VK_FORMAT_B5G5R5A1_UNORM_PACK16 );
		CASE_STR( VK_FORMAT_R5G6B5_UNORM_PACK16 );
		CASE_STR( VK_FORMAT_B5G6R5_UNORM_PACK16 );
		CASE_STR( VK_FORMAT_B8G8R8A8_SRGB );
		CASE_STR( VK_FORMAT_R8G8B8A8_SRGB );
		CASE_STR( VK_FORMAT_B8G8R8A8_SNORM );
		CASE_STR( VK_FORMAT_R8G8B8A8_SNORM );
		CASE_STR( VK_FORMAT_B8G8R8A8_UNORM );
		CASE_STR( VK_FORMAT_R8G8B8A8_UNORM );
		CASE_STR( VK_FORMAT_B4G4R4A4_UNORM_PACK16 );
		CASE_STR( VK_FORMAT_R4G4B4A4_UNORM_PACK16 );
		CASE_STR( VK_FORMAT_R16G16B16A16_UNORM );
		CASE_STR( VK_FORMAT_R16G16B16A16_SFLOAT );
		CASE_STR( VK_FORMAT_A2B10G10R10_UNORM_PACK32 );
		CASE_STR( VK_FORMAT_A2R10G10B10_UNORM_PACK32 );
		CASE_STR( VK_FORMAT_B10G11R11_UFLOAT_PACK32 );
		// depth formats
		CASE_STR( VK_FORMAT_D16_UNORM );
		CASE_STR( VK_FORMAT_D16_UNORM_S8_UINT );
		CASE_STR( VK_FORMAT_X8_D24_UNORM_PACK32 );
		CASE_STR( VK_FORMAT_D24_UNORM_S8_UINT );
		CASE_STR( VK_FORMAT_D32_SFLOAT );
		CASE_STR( VK_FORMAT_D32_SFLOAT_S8_UINT );
	default:
		Com_sprintf( buf, sizeof( buf ), "#%i", format );
		return buf;
	}
}


static const char *vk_result_string( VkResult code ) {
	static char buffer[32];

	switch ( code ) {
		CASE_STR( VK_SUCCESS );
		CASE_STR( VK_NOT_READY );
		CASE_STR( VK_TIMEOUT );
		CASE_STR( VK_EVENT_SET );
		CASE_STR( VK_EVENT_RESET );
		CASE_STR( VK_INCOMPLETE );
		CASE_STR( VK_ERROR_OUT_OF_HOST_MEMORY );
		CASE_STR( VK_ERROR_OUT_OF_DEVICE_MEMORY );
		CASE_STR( VK_ERROR_INITIALIZATION_FAILED );
		CASE_STR( VK_ERROR_DEVICE_LOST );
		CASE_STR( VK_ERROR_MEMORY_MAP_FAILED );
		CASE_STR( VK_ERROR_LAYER_NOT_PRESENT );
		CASE_STR( VK_ERROR_EXTENSION_NOT_PRESENT );
		CASE_STR( VK_ERROR_FEATURE_NOT_PRESENT );
		CASE_STR( VK_ERROR_INCOMPATIBLE_DRIVER );
		CASE_STR( VK_ERROR_TOO_MANY_OBJECTS );
		CASE_STR( VK_ERROR_FORMAT_NOT_SUPPORTED );
		CASE_STR( VK_ERROR_FRAGMENTED_POOL );
		CASE_STR( VK_ERROR_UNKNOWN );
		CASE_STR( VK_ERROR_OUT_OF_POOL_MEMORY );
		CASE_STR( VK_ERROR_INVALID_EXTERNAL_HANDLE );
		CASE_STR( VK_ERROR_FRAGMENTATION );
		CASE_STR( VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS );
		CASE_STR( VK_ERROR_SURFACE_LOST_KHR );
		CASE_STR( VK_ERROR_NATIVE_WINDOW_IN_USE_KHR );
		CASE_STR( VK_SUBOPTIMAL_KHR );
		CASE_STR( VK_ERROR_OUT_OF_DATE_KHR );
		CASE_STR( VK_ERROR_INCOMPATIBLE_DISPLAY_KHR );
		CASE_STR( VK_ERROR_VALIDATION_FAILED_EXT );
		CASE_STR( VK_ERROR_INVALID_SHADER_NV );
		CASE_STR( VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT );
		CASE_STR( VK_ERROR_NOT_PERMITTED_EXT );
		CASE_STR( VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT );
		CASE_STR( VK_THREAD_IDLE_KHR );
		CASE_STR( VK_THREAD_DONE_KHR );
		CASE_STR( VK_OPERATION_DEFERRED_KHR );
		CASE_STR( VK_OPERATION_NOT_DEFERRED_KHR );
		CASE_STR( VK_PIPELINE_COMPILE_REQUIRED_EXT );
	default:
		sprintf( buffer, "code %i", code );
		return buffer;
	}
}
#undef CASE_STR

#define VK_CHECK( function_call ) { \
	VkResult vkCheckResult = function_call; \
	if ( vkCheckResult < 0 ) { \
		ri.Error( ERR_FATAL, "Vulkan: %s returned %s", #function_call, vk_result_string( vkCheckResult ) ); \
	} \
}


/*
static VkFlags get_composite_alpha( VkCompositeAlphaFlagsKHR flags )
{
	const VkCompositeAlphaFlagBitsKHR compositeFlags[] = {
		VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
	};
	int i;

	for ( i = 1; i < ARRAY_LEN( compositeFlags ); i++ ) {
		if ( flags & compositeFlags[i] ) {
			return compositeFlags[i];
		}
	}

	return compositeFlags[0];
}
*/


static VkCommandBuffer begin_command_buffer( void )
{
	VkCommandBufferBeginInfo begin_info;
	VkCommandBufferAllocateInfo alloc_info;
	VkCommandBuffer command_buffer;

	alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	alloc_info.pNext = NULL;
	alloc_info.commandPool = vk.command_pool;
	alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	alloc_info.commandBufferCount = 1;
	VK_CHECK( qvkAllocateCommandBuffers( vk.device, &alloc_info, &command_buffer ) );

	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.pNext = NULL;
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	begin_info.pInheritanceInfo = NULL;

	VK_CHECK( qvkBeginCommandBuffer( command_buffer, &begin_info ) );

	return command_buffer;
}


static qboolean end_command_buffer_internal( VkCommandBuffer command_buffer, const char *location, qboolean fatalOnError )
{
#ifdef USE_UPLOAD_QUEUE
	const VkPipelineStageFlags wait_dst_stage_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	VkSemaphore waits;
#endif
	VkSubmitInfo submit_info;
	VkCommandBuffer cmdbuf[1];
	VkResult res;
	const char *ctx = location ? location : "unknown";

	cmdbuf[0] = command_buffer;

	res = qvkEndCommandBuffer( command_buffer );
	if ( res != VK_SUCCESS ) {
		qvkFreeCommandBuffers( vk.device, vk.command_pool, 1, cmdbuf );
		if ( fatalOnError ) {
			ri.Error( ERR_FATAL, "Vulkan: %s returned %s", "vkEndCommandBuffer", vk_result_string( res ) );
		}
		ri.Printf( PRINT_ERROR, "RTX RT: vkEndCommandBuffer failed at %s: %s\n", ctx, vk_result_string( res ) );
		return qfalse;
	}

	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.pNext = NULL;
#ifdef USE_UPLOAD_QUEUE
	if ( vk.rendering_finished != VK_NULL_HANDLE ) {
		waits = vk.rendering_finished;
		vk.rendering_finished = VK_NULL_HANDLE;
		submit_info.waitSemaphoreCount = 1;
		submit_info.pWaitSemaphores = &waits;
		submit_info.pWaitDstStageMask = &wait_dst_stage_mask;
	} else 
#endif
	{
		submit_info.waitSemaphoreCount = 0;
		submit_info.pWaitSemaphores = NULL;
		submit_info.pWaitDstStageMask = NULL;
	}

	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = cmdbuf;
	submit_info.signalSemaphoreCount = 0;
	submit_info.pSignalSemaphores = NULL;

	res = qvkQueueSubmit( vk.queue, 1, &submit_info, VK_NULL_HANDLE );
	if ( res != VK_SUCCESS ) {
		qvkFreeCommandBuffers( vk.device, vk.command_pool, 1, cmdbuf );
		if ( fatalOnError ) {
			ri.Error( ERR_FATAL, "Vulkan: %s returned %s", "vkQueueSubmit", vk_result_string( res ) );
		}
		ri.Printf( PRINT_ERROR, "RTX RT: vkQueueSubmit failed at %s: %s\n", ctx, vk_result_string( res ) );
		return qfalse;
	}

	res = qvkQueueWaitIdle( vk.queue );
	if ( res != VK_SUCCESS ) {
		qvkFreeCommandBuffers( vk.device, vk.command_pool, 1, cmdbuf );
		if ( fatalOnError ) {
			ri.Error( ERR_FATAL, "Vulkan: %s returned %s", "vkQueueWaitIdle", vk_result_string( res ) );
		}
		ri.Printf( PRINT_ERROR, "RTX RT: vkQueueWaitIdle failed at %s: %s\n", ctx, vk_result_string( res ) );
		return qfalse;
	}

	qvkFreeCommandBuffers( vk.device, vk.command_pool, 1, cmdbuf );
	return qtrue;
}

static void end_command_buffer( VkCommandBuffer command_buffer, const char *location )
{
	(void)end_command_buffer_internal( command_buffer, location, qtrue );
}

static qboolean end_command_buffer_soft( VkCommandBuffer command_buffer, const char *location )
{
	return end_command_buffer_internal( command_buffer, location, qfalse );
}


static void record_image_layout_transition( VkCommandBuffer command_buffer, VkImage image, VkImageAspectFlags image_aspect_flags, 
	VkImageLayout old_layout, VkImageLayout new_layout, uint32_t src_stage_override, uint32_t dst_stage_override ) {
	VkImageMemoryBarrier barrier;
	uint32_t src_stage, dst_stage;

	/*
	 * The renderer does not enable separateDepthStencilLayouts.  A barrier
	 * covering either of our depth/stencil images must therefore transition
	 * both aspects, even when the image view and copy region intentionally
	 * address only the depth aspect.
	 */
	if ( glConfig.stencilBits > 0 &&
		( image == vk.depth_image || image == vk.liquidDepth.image ) ) {
		image_aspect_flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
	}

	switch ( old_layout ) {
		case VK_IMAGE_LAYOUT_UNDEFINED:
			if ( src_stage_override != 0 )
				src_stage = src_stage_override;
			else
				src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			barrier.srcAccessMask = VK_ACCESS_NONE;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			src_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
				VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
			src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
			src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			barrier.srcAccessMask = VK_ACCESS_NONE;
			break;
		default:
			ri.Error( ERR_DROP, "unsupported old layout %i", old_layout );
			src_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			barrier.srcAccessMask = VK_ACCESS_NONE;
			break;
	}

	switch ( new_layout ) {
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			dst_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
				VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			barrier.dstAccessMask =
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
			dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			barrier.dstAccessMask = VK_ACCESS_NONE;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
			dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			break;
		default:
			ri.Error( ERR_DROP, "unsupported new layout %i", new_layout);
			dst_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			barrier.dstAccessMask = VK_ACCESS_NONE;
			break;
	}


	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.pNext = NULL;
	//barrier.srcAccessMask = src_access_flags;
	//barrier.dstAccessMask = dst_access_flags;
	barrier.oldLayout = old_layout;
	barrier.newLayout = new_layout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = image_aspect_flags;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

	qvkCmdPipelineBarrier( command_buffer, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &barrier );
}


// debug markers
#define SET_OBJECT_NAME(obj,objName,objType) vk_set_object_name( (uint64_t)(obj), (objName), (objType) )

static qboolean vk_debug_object_names_enabled( void )
{
	return ( vk_debug_markers_mode() >= 1 && vk.debugMarkers && qvkDebugMarkerSetObjectNameEXT ) ? qtrue : qfalse;
}

static qboolean vk_debug_pass_markers_enabled( void )
{
	return ( vk_debug_markers_mode() >= 2 && vk.debugMarkers && qvkCmdDebugMarkerBeginEXT && qvkCmdDebugMarkerEndEXT ) ? qtrue : qfalse;
}

static void vk_set_object_name( uint64_t obj, const char *objName, VkDebugReportObjectTypeEXT objType )
{
	if ( vk_debug_object_names_enabled() && obj )
	{
		VkDebugMarkerObjectNameInfoEXT info;
		info.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT;
		info.pNext = NULL;
		info.objectType = objType;
		info.object = obj;
		info.pObjectName = objName;
		qvkDebugMarkerSetObjectNameEXT( vk.device, &info );
	}
}

static void vk_cmd_debug_marker_begin( const char *markerName, const float color[4] )
{
	VkDebugMarkerMarkerInfoEXT info;

	if ( !vk_debug_pass_markers_enabled() || !vk.cmd ) {
		return;
	}

	info.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;
	info.pNext = NULL;
	info.pMarkerName = markerName;
	info.color[0] = color[0];
	info.color[1] = color[1];
	info.color[2] = color[2];
	info.color[3] = color[3];

	qvkCmdDebugMarkerBeginEXT( vk.cmd->command_buffer, &info );
}

static void vk_cmd_debug_marker_end( void )
{
	if ( !vk_debug_pass_markers_enabled() || !vk.cmd ) {
		return;
	}

	qvkCmdDebugMarkerEndEXT( vk.cmd->command_buffer );
}

static void vk_debug_render_pass_label( VkRenderPass renderPass, char *markerName, size_t markerNameSize, float color[4] )
{
	uint32_t i;

	Q_strncpyz( markerName, "RTX Pass: unknown", markerNameSize );
	color[0] = 0.5f; color[1] = 0.5f; color[2] = 0.5f; color[3] = 1.0f;

	if ( renderPass == vk.render_pass.main ) {
		Q_strncpyz( markerName, "RTX Pass: main", markerNameSize );
		color[0] = 0.10f; color[1] = 0.55f; color[2] = 0.95f; color[3] = 1.0f;
		return;
	}
	if ( renderPass == vk.render_pass.main_load ) {
		Q_strncpyz( markerName, "RTX Pass: main load", markerNameSize );
		color[0] = 0.10f; color[1] = 0.55f; color[2] = 0.95f; color[3] = 1.0f;
		return;
	}
	if ( renderPass == vk.render_pass.dlight_shadow ) {
		if ( vk.renderPassIndex == RENDER_PASS_CSM_SHADOW ) {
			Q_strncpyz( markerName, "RTX Pass: csm shadow atlas", markerNameSize );
		} else if ( vk.renderPassIndex == RENDER_PASS_SPOT_SHADOW ) {
			Q_strncpyz( markerName, "RTX Pass: spot shadow atlas", markerNameSize );
		} else {
			Q_strncpyz( markerName, "RTX Pass: dlight shadow atlas", markerNameSize );
		}
		color[0] = 0.72f; color[1] = 0.30f; color[2] = 0.16f; color[3] = 1.0f;
		return;
	}

	if ( renderPass == vk.render_pass.post_bloom ) {
		Q_strncpyz( markerName, "RTX Pass: post_bloom", markerNameSize );
		color[0] = 0.18f; color[1] = 0.62f; color[2] = 0.92f; color[3] = 1.0f;
		return;
	}

	if ( renderPass == vk.render_pass.global_fog ) {
		Q_strncpyz( markerName, "RTX Pass: global_fog", markerNameSize );
		color[0] = 0.55f; color[1] = 0.59f; color[2] = 0.61f; color[3] = 1.0f;
		return;
	}

	if ( renderPass == vk.render_pass.bloom_extract ) {
		Q_strncpyz( markerName, "RTX Pass: bloom_extract", markerNameSize );
		color[0] = 0.93f; color[1] = 0.68f; color[2] = 0.20f; color[3] = 1.0f;
		return;
	}

	for ( i = 0; i < VK_NUM_BLOOM_PASSES * 2; i++ ) {
		if ( renderPass == vk.render_pass.blur[ i ] ) {
			Com_sprintf( markerName, (int)markerNameSize, "RTX Pass: blur %u", i );
			color[0] = 0.96f; color[1] = 0.80f; color[2] = 0.25f; color[3] = 1.0f;
			return;
		}
	}

	if ( renderPass == vk.render_pass.capture ) {
		Q_strncpyz( markerName, "RTX Pass: capture", markerNameSize );
		color[0] = 0.95f; color[1] = 0.35f; color[2] = 0.30f; color[3] = 1.0f;
		return;
	}

	if ( renderPass == vk.render_pass.gamma ) {
		Q_strncpyz( markerName, "RTX Pass: gamma", markerNameSize );
		color[0] = 0.38f; color[1] = 0.82f; color[2] = 0.45f; color[3] = 1.0f;
		return;
	}

	if ( renderPass == vk.render_pass.screenmap ) {
		Q_strncpyz( markerName, "RTX Pass: screenmap", markerNameSize );
		color[0] = 0.62f; color[1] = 0.52f; color[2] = 0.88f; color[3] = 1.0f;
		return;
	}

	if ( renderPass == vk.render_pass.motion_blur ) {
		Q_strncpyz( markerName, "RTX Pass: motion blur", markerNameSize );
		color[0] = 0.78f; color[1] = 0.36f; color[2] = 0.84f; color[3] = 1.0f;
		return;
	}
}

static void vk_debug_render_pass_begin( VkRenderPass renderPass )
{
	char markerName[64];
	float color[4];

	if ( !vk_debug_pass_markers_enabled() ) {
		return;
	}

	vk_debug_render_pass_label( renderPass, markerName, sizeof( markerName ), color );
	vk_cmd_debug_marker_begin( markerName, color );
}

static void vk_debug_render_pass_end( void )
{
	vk_cmd_debug_marker_end();
}


static void vk_set_hdr_metadata( void )
{
	VkHdrMetadataEXT metadata;
	float max_luminance;
	float max_cll;
	float max_fall;

	if ( !vk.hdrDisplayActive || !vk.hdrMetadata || !qvkSetHdrMetadataEXT ||
		vk.swapchain == VK_NULL_HANDLE ) {
		return;
	}
	max_luminance = Com_Clamp( 200.0f, 10000.0f,
		r_hdrDisplayMaxLuminance ? r_hdrDisplayMaxLuminance->value : 1000.0f );
	if ( vk.displayOutput.maxLuminanceNits >= 200.0f ) {
		max_luminance = Com_Clamp( 200.0f, max_luminance,
			vk.displayOutput.maxLuminanceNits );
	}
	max_cll = Com_Clamp( 200.0f, 10000.0f,
		r_hdrDisplayMaxCLL ? r_hdrDisplayMaxCLL->value : 1000.0f );
	if ( max_cll > max_luminance ) {
		max_cll = max_luminance;
	}
	max_fall = Com_Clamp( 80.0f, max_cll,
		r_hdrDisplayMaxFALL ? r_hdrDisplayMaxFALL->value : 400.0f );

	Com_Memset( &metadata, 0, sizeof( metadata ) );
	metadata.sType = VK_STRUCTURE_TYPE_HDR_METADATA_EXT;
	metadata.pNext = NULL;
	metadata.displayPrimaryRed.x = 0.708f;
	metadata.displayPrimaryRed.y = 0.292f;
	metadata.displayPrimaryGreen.x = 0.170f;
	metadata.displayPrimaryGreen.y = 0.797f;
	metadata.displayPrimaryBlue.x = 0.131f;
	metadata.displayPrimaryBlue.y = 0.046f;
	metadata.whitePoint.x = 0.3127f;
	metadata.whitePoint.y = 0.3290f;
	metadata.maxLuminance = max_luminance;
	metadata.minLuminance = 0.005f;
	metadata.maxContentLightLevel = max_cll;
	metadata.maxFrameAverageLightLevel = max_fall;

	qvkSetHdrMetadataEXT( vk.device, 1, &vk.swapchain, &metadata );
}


static void vk_create_swapchain( VkPhysicalDevice physical_device, VkDevice device, VkSurfaceKHR surface, VkSurfaceFormatKHR surface_format, VkSwapchainKHR *swapchain, qboolean verbose ) {
	VkImageViewCreateInfo view;
	VkSurfaceCapabilitiesKHR surface_caps;
	VkExtent2D image_extent;
	uint32_t present_mode_count, i;
	VkPresentModeKHR present_mode;
	VkPresentModeKHR *present_modes;
	uint32_t image_count;
	VkSwapchainCreateInfoKHR desc;
	qboolean mailbox_supported = qfalse;
	qboolean immediate_supported = qfalse;
	qboolean fifo_relaxed_supported = qfalse;
	int v;

	VK_CHECK( qvkGetPhysicalDeviceSurfaceCapabilitiesKHR( physical_device, surface, &surface_caps ) );

	image_extent = surface_caps.currentExtent;
	if ( image_extent.width == 0xffffffff && image_extent.height == 0xffffffff ) {
		image_extent.width = MIN( surface_caps.maxImageExtent.width, MAX( surface_caps.minImageExtent.width, (uint32_t) glConfig.vidWidth ) );
		image_extent.height = MIN( surface_caps.maxImageExtent.height, MAX( surface_caps.minImageExtent.height, (uint32_t) glConfig.vidHeight ) );
	}

	vk.clearAttachment = qtrue;

	if ( !vk.fboActive ) {
		// VK_IMAGE_USAGE_TRANSFER_DST_BIT is required by image clear operations.
		if ( ( surface_caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT ) == 0 ) {
			vk.clearAttachment = qfalse;
			ri.Printf( PRINT_WARNING, "VK_IMAGE_USAGE_TRANSFER_DST_BIT is not supported by the swapchain, \\r_clear might not work\n" );
		}
		// VK_IMAGE_USAGE_TRANSFER_SRC_BIT is required in order to take screenshots.
		if ((surface_caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0) {
			ri.Error(ERR_FATAL, "create_swapchain: VK_IMAGE_USAGE_TRANSFER_SRC_BIT is not supported by the swapchain");
		}
	}

	// determine present mode and swapchain image count
	VK_CHECK(qvkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_mode_count, NULL));

	present_modes = (VkPresentModeKHR *) ri.Malloc( present_mode_count * sizeof( VkPresentModeKHR ) );
	VK_CHECK(qvkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_mode_count, present_modes));

	if ( verbose ) {
		ri.Printf( PRINT_ALL, "...presentation modes:" );
	}
	for ( i = 0; i < present_mode_count; i++ ) {
		if ( verbose ) {
			ri.Printf( PRINT_ALL, " %s", pmode_to_str( present_modes[i] ) );
		}
		if ( present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR )
			mailbox_supported = qtrue;
		else if ( present_modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR )
			immediate_supported = qtrue;
		else if ( present_modes[i] == VK_PRESENT_MODE_FIFO_RELAXED_KHR )
			fifo_relaxed_supported = qtrue;
	}
	if ( verbose ) {
		ri.Printf( PRINT_ALL, "\n" );
	}

	ri.Free( present_modes );

	if ( ( v = ri.Cvar_VariableIntegerValue( "r_swapInterval" ) ) != 0 ) {
		if ( v == 2 && mailbox_supported )
			present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
		else if ( fifo_relaxed_supported )
			present_mode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
		else
			present_mode = VK_PRESENT_MODE_FIFO_KHR;
		image_count = MAX( MIN_SWAPCHAIN_IMAGES_FIFO, surface_caps.minImageCount );
	} else {
		if ( immediate_supported ) {
			present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
			image_count = MAX( MIN_SWAPCHAIN_IMAGES_IMM, surface_caps.minImageCount );
		} else if ( mailbox_supported ) {
			present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
			image_count = MAX( MIN_SWAPCHAIN_IMAGES_MAILBOX, surface_caps.minImageCount );
		} else if ( fifo_relaxed_supported ) {
			present_mode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
			image_count = MAX( MIN_SWAPCHAIN_IMAGES_FIFO, surface_caps.minImageCount );
		} else {
			present_mode = VK_PRESENT_MODE_FIFO_KHR;
			image_count = MAX( MIN_SWAPCHAIN_IMAGES_FIFO, surface_caps.minImageCount );
		}
	}

	if ( image_count < 2 ) {
		image_count = 2;
	}

	if ( surface_caps.maxImageCount == 0 && present_mode == VK_PRESENT_MODE_FIFO_KHR ) {
		image_count = MAX( MIN_SWAPCHAIN_IMAGES_FIFO_0, surface_caps.minImageCount );
	} else if ( surface_caps.maxImageCount > 0 ) {
		image_count = MIN( MIN( image_count, surface_caps.maxImageCount ), MAX_SWAPCHAIN_IMAGES );
	}

	if ( verbose ) {
		ri.Printf( PRINT_ALL, "...selected presentation mode: %s, image count: %i\n", pmode_to_str( present_mode ), image_count );
	}

	// create swap chain
	desc.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.surface = surface;
	desc.minImageCount = image_count;
	desc.imageFormat = surface_format.format;
	desc.imageColorSpace = surface_format.colorSpace;
	desc.imageExtent = image_extent;
	desc.imageArrayLayers = 1;
	desc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	if ( !vk.fboActive ) {
		desc.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}
	desc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	desc.queueFamilyIndexCount = 0;
	desc.pQueueFamilyIndices = NULL;
	desc.preTransform = surface_caps.currentTransform;
	//desc.compositeAlpha = get_composite_alpha( surface_caps.supportedCompositeAlpha );
	desc.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	desc.presentMode = present_mode;
	desc.clipped = VK_TRUE;
	desc.oldSwapchain = VK_NULL_HANDLE;

	VK_CHECK( qvkCreateSwapchainKHR( device, &desc, NULL, swapchain ) );
	vk_set_hdr_metadata();
	if ( verbose && vk.hdrDisplayActive && !vk.hdrMetadata ) {
		ri.Printf( PRINT_WARNING,
			"...%s is not available, presenting HDR10 without static HDR metadata\n",
			VK_EXT_HDR_METADATA_EXTENSION_NAME );
	}

	VK_CHECK( qvkGetSwapchainImagesKHR( vk.device, vk.swapchain, &vk.swapchain_image_count, NULL ) );
	vk.swapchain_image_count = MIN( vk.swapchain_image_count, MAX_SWAPCHAIN_IMAGES );
	VK_CHECK( qvkGetSwapchainImagesKHR( vk.device, vk.swapchain, &vk.swapchain_image_count, vk.swapchain_images ) );

	for ( i = 0; i < vk.swapchain_image_count; i++ ) {
		view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view.pNext = NULL;
		view.flags = 0;
		view.image = vk.swapchain_images[i];
		view.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view.format = vk.present_format.format;
		view.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		view.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		view.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		view.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		view.subresourceRange.baseMipLevel = 0;
		view.subresourceRange.levelCount = 1;
		view.subresourceRange.baseArrayLayer = 0;
		view.subresourceRange.layerCount = 1;

		VK_CHECK( qvkCreateImageView( vk.device, &view, NULL, &vk.swapchain_image_views[i] ) );

		SET_OBJECT_NAME( vk.swapchain_images[i], va( "swapchain image %i", i ), VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT );
		SET_OBJECT_NAME( vk.swapchain_image_views[i], va( "swapchain image %i", i ), VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT );
	}

	for ( i = 0; i < vk.swapchain_image_count; i++ ) {
		VkSemaphoreCreateInfo s;
		s.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		s.pNext = NULL;
		s.flags = 0;
		VK_CHECK( qvkCreateSemaphore( vk.device, &s, NULL, &vk.swapchain_rendering_finished[i] ) );
		SET_OBJECT_NAME( vk.swapchain_rendering_finished[i], va( "swapchain_rendering_finished semaphore %i", i ), VK_DEBUG_REPORT_OBJECT_TYPE_SEMAPHORE_EXT );
	}

	if ( vk.initSwapchainLayout != VK_IMAGE_LAYOUT_UNDEFINED ) {
		VkCommandBuffer command_buffer = begin_command_buffer();

		for ( i = 0; i < vk.swapchain_image_count; i++ ) {
			record_image_layout_transition( command_buffer, vk.swapchain_images[i],
				VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED, vk.initSwapchainLayout, 0, 0 );
		}

		end_command_buffer( command_buffer, __func__ );
	}

	vk_lifetime_mark_create( RTX_VK_RESOURCE_SWAPCHAIN, __func__ );
}


static void vk_create_dlight_shadow_render_pass( VkFormat depth_format )
{
	VkAttachmentDescription attachment;
	VkAttachmentReference depthRef;
	VkSubpassDescription subpass;
	VkSubpassDependency deps[2];
	VkRenderPassCreateInfo desc;

	if ( vk.dlight_shadow_image_view == VK_NULL_HANDLE &&
		vk.spot_shadow_image_view == VK_NULL_HANDLE &&
		vk.csm_shadow_image_view == VK_NULL_HANDLE ) {
		return;
	}

	Com_Memset( &attachment, 0, sizeof( attachment ) );
	attachment.format = depth_format;
	attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	depthRef.attachment = 0;
	depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	Com_Memset( &subpass, 0, sizeof( subpass ) );
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 0;
	subpass.pDepthStencilAttachment = &depthRef;

	Com_Memset( deps, 0, sizeof( deps ) );
	deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	deps[0].dstSubpass = 0;
	deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
		VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	deps[1].srcSubpass = 0;
	deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	Com_Memset( &desc, 0, sizeof( desc ) );
	desc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	desc.pAttachments = &attachment;
	desc.attachmentCount = 1;
	desc.pSubpasses = &subpass;
	desc.subpassCount = 1;
	desc.pDependencies = deps;
	desc.dependencyCount = ARRAY_LEN( deps );

	VK_CHECK( qvkCreateRenderPass( vk.device, &desc, NULL,
		&vk.render_pass.dlight_shadow ) );
	SET_OBJECT_NAME( vk.render_pass.dlight_shadow,
		"render pass - sampled shadow atlas",
		VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT );
}


static void vk_create_render_passes( void )
{
	VkAttachmentDescription attachments[3]; // color | depth | msaa color
	VkAttachmentReference colorResolveRef;
	VkAttachmentReference colorRef0;
	VkAttachmentReference depthRef0;
	VkSubpassDescription subpass;
	VkSubpassDependency deps[3];
	VkSubpassDependency fogDeps[2];
	VkRenderPassCreateInfo desc;
	VkFormat depth_format;
	VkDevice device;
	const qboolean bloomEnabled = ( r_bloom && r_bloom->integer ) ? qtrue : qfalse;
	const qboolean motionBlurEnabled =
		( r_motionBlur && r_motionBlur->integer ) ? qtrue : qfalse;
	const qboolean globalFogEnabled = vk_global_fog_enabled();
	const qboolean depthFadeActive = vk_depth_fade_supported();
	const qboolean liquidCaptureActive =
		( vk.fboActive && vk_liquid_requested() ) ? qtrue : qfalse;
	const qboolean shadowAtlasActive =
		( vk.dlight_shadow_image_view != VK_NULL_HANDLE ||
		  vk.spot_shadow_image_view != VK_NULL_HANDLE ||
		  vk.csm_shadow_image_view != VK_NULL_HANDLE ) ? qtrue : qfalse;
	const qboolean needsPostMainPass =
		( bloomEnabled || motionBlurEnabled || globalFogEnabled || depthFadeActive ||
			liquidCaptureActive || shadowAtlasActive ||
			vk_rt_trace_available() ) ? qtrue : qfalse;
	uint32_t i;

	depth_format = vk.depth_format;
	device = vk.device;

	if ( r_fbo->integer == 0 )
	{
		// presentation
		attachments[0].flags = 0;
		attachments[0].format = vk.present_format.format;
		attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
#ifdef USE_BUFFER_CLEAR
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
#else
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;	// Assuming this will be completely overwritten
#endif
		attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;		// needed for presentation
		attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[0].initialLayout = vk.initSwapchainLayout;
		attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	}
	else
	{
		// resolve/color buffer
		attachments[0].flags = 0;
		attachments[0].format = vk.color_format;
		attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;

#ifdef USE_BUFFER_CLEAR
		if ( vk.msaaActive )
			attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;	// Assuming this will be completely overwritten
		else
			attachments[ 0 ].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
#else
		attachments[ 0 ].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;	// Assuming this will be completely overwritten
#endif

		attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;   // needed for next render pass
		attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	// depth buffer
	attachments[1].flags = 0;
	attachments[1].format = depth_format;
	attachments[1].samples = vkSamples;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // Need empty depth buffer before use
	attachments[1].stencilLoadOp = glConfig.stencilBits ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	if ( needsPostMainPass ) {
		attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE; // keep it for post-main pass (bloom/RT handoff)
		attachments[1].stencilStoreOp = glConfig.stencilBits ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
	} else {
		attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	}
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	colorRef0.attachment = 0;
	colorRef0.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	depthRef0.attachment = 1;
	depthRef0.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	Com_Memset( &subpass, 0, sizeof( subpass ) );
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef0;
	subpass.pDepthStencilAttachment = &depthRef0;

	Com_Memset( &desc, 0, sizeof( desc ) );
	desc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.pAttachments = attachments;
	desc.pSubpasses = &subpass;

	desc.subpassCount = 1;
	desc.attachmentCount = 2;

	if ( vk.msaaActive )
	{
		attachments[2].flags = 0;
		attachments[2].format = vk.color_format;
		attachments[2].samples = vkSamples;
#ifdef USE_BUFFER_CLEAR
		attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
#else
		attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
#endif
		if ( needsPostMainPass ) {
			attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE; // keep it for post-main pass (bloom/RT handoff)
		} else {
			attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // Intermediate storage (not written)
		}
		attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[2].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachments[2].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		desc.attachmentCount = 3;

		colorRef0.attachment = 2; // msaa image attachment
		colorRef0.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		colorResolveRef.attachment = 0; // resolve image attachment
		colorResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		subpass.pResolveAttachments = &colorResolveRef;
	}

	// subpass dependencies

	Com_Memset( &deps, 0, sizeof( deps ) );

	deps[2].srcSubpass = VK_SUBPASS_EXTERNAL;
	deps[2].dstSubpass = 0;
	deps[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;	// What pipeline stage is waiting on the dependency
	deps[2].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;	// What pipeline stage is waiting on the dependency
	deps[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;			// What access scopes are influence the dependency
	deps[2].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;			// What access scopes are waiting on the dependency
	deps[2].dependencyFlags = 0;

	if ( r_fbo->integer == 0 )
	{
		desc.dependencyCount = 1;
		desc.pDependencies = &deps[2];

		VK_CHECK( qvkCreateRenderPass( device, &desc, NULL, &vk.render_pass.main ) );
		SET_OBJECT_NAME( vk.render_pass.main, "render pass - main", VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT );

		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		attachments[1].stencilLoadOp = glConfig.stencilBits ?
			VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		VK_CHECK( qvkCreateRenderPass( device, &desc, NULL,
			&vk.render_pass.main_load ) );
		SET_OBJECT_NAME( vk.render_pass.main_load,
			"render pass - main load",
			VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT );
		vk_create_dlight_shadow_render_pass( depth_format );

		return;
	}

	desc.dependencyCount = 2;
	desc.pDependencies = &deps[0];

	deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	deps[0].dstSubpass = 0;
	deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
		VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
		VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT |
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;					// Only need the current fragment (or tile) synchronized, not the whole framebuffer

	deps[1].srcSubpass = 0;
	deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
		VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
		VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
		VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;					// Only need the current fragment (or tile) synchronized, not the whole framebuffer

	VK_CHECK( qvkCreateRenderPass( device, &desc, NULL, &vk.render_pass.main ) );
	SET_OBJECT_NAME( vk.render_pass.main, "render pass - main", VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT );

	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachments[1].stencilLoadOp = glConfig.stencilBits ?
		VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	if ( vk.msaaActive ) {
		attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	}
	VK_CHECK( qvkCreateRenderPass( device, &desc, NULL,
		&vk.render_pass.main_load ) );
	SET_OBJECT_NAME( vk.render_pass.main_load, "render pass - main load",
		VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT );
	vk_create_dlight_shadow_render_pass( depth_format );

	if ( needsPostMainPass ) {

		// post-bloom pass
		// color buffer
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // load from previous pass
		 // depth buffer
		attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		attachments[1].storeOp =
			( motionBlurEnabled || globalFogEnabled || depthFadeActive ||
			  liquidCaptureActive ) ?
			VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		attachments[1].stencilStoreOp =
			( ( motionBlurEnabled || globalFogEnabled || depthFadeActive ||
				liquidCaptureActive ) &&
			  glConfig.stencilBits ) ?
				VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
		if ( vk.msaaActive ) {
			// msaa render target
			attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		}
		VK_CHECK( qvkCreateRenderPass( device, &desc, NULL, &vk.render_pass.post_bloom ) );
		SET_OBJECT_NAME( vk.render_pass.post_bloom, "render pass - post_bloom", VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT );
	}

	if ( globalFogEnabled ) {
		/*
		 * Global fog samples the completed scene depth, so it uses a
		 * color-only load pass between two depth-owning main/post passes.
		 */
		Com_Memset( &attachments[0], 0, sizeof( attachments[0] ) );
		attachments[0].format = vk.color_format;
		attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		colorRef0.attachment = 0;
		colorRef0.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		Com_Memset( &subpass, 0, sizeof( subpass ) );
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorRef0;

		Com_Memset( fogDeps, 0, sizeof( fogDeps ) );
		fogDeps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		fogDeps[0].dstSubpass = 0;
		fogDeps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		fogDeps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		fogDeps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT |
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		fogDeps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		fogDeps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		fogDeps[1].srcSubpass = 0;
		fogDeps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		fogDeps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		fogDeps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		fogDeps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		fogDeps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
			VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		fogDeps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		Com_Memset( &desc, 0, sizeof( desc ) );
		desc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		desc.attachmentCount = 1;
		desc.pAttachments = attachments;
		desc.subpassCount = 1;
		desc.pSubpasses = &subpass;
		desc.dependencyCount = 2;
		desc.pDependencies = fogDeps;

		VK_CHECK( qvkCreateRenderPass( device, &desc, NULL, &vk.render_pass.global_fog ) );
		SET_OBJECT_NAME( vk.render_pass.global_fog, "render pass - global fog", VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT );
	}

	if ( bloomEnabled ) {
		// bloom extraction, using resolved/main fbo as a source
		desc.attachmentCount = 1;

		colorRef0.attachment = 0;
		colorRef0.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		Com_Memset( &subpass, 0, sizeof( subpass ) );
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorRef0;

		attachments[0].flags = 0;
		attachments[0].format = vk.bloom_format;
		attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;	// Assuming this will be completely overwritten
		attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;		// needed for next render pass
		attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VK_CHECK( qvkCreateRenderPass( device, &desc, NULL, &vk.render_pass.bloom_extract ) );
		SET_OBJECT_NAME( vk.render_pass.bloom_extract, "render pass - bloom_extract", VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT );

		for ( i = 0; i < ARRAY_LEN( vk.render_pass.blur ); i++ )
		{
			VK_CHECK( qvkCreateRenderPass( device, &desc, NULL, &vk.render_pass.blur[i] ) );
			SET_OBJECT_NAME( vk.render_pass.blur[i], va( "render pass - blur %i", i ), VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT );
		}
	}

	if ( motionBlurEnabled ) {
		/* Single-sample scratch target for the camera-motion blur kernel. */
		desc.attachmentCount = 1;
		desc.dependencyCount = 2;
		desc.pDependencies = &deps[0];

		colorRef0.attachment = 0;
		colorRef0.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		Com_Memset( &subpass, 0, sizeof( subpass ) );
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorRef0;
		desc.pSubpasses = &subpass;

		attachments[0].flags = 0;
		attachments[0].format = vk.color_format;
		attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VK_CHECK( qvkCreateRenderPass( device, &desc, NULL,
			&vk.render_pass.motion_blur ) );
		SET_OBJECT_NAME( vk.render_pass.motion_blur,
			"render pass - motion blur",
			VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT );
	}

	// capture render pass
	if ( vk.capture.image )
	{
		Com_Memset( &subpass, 0, sizeof( subpass ) );

		attachments[0].flags = 0;
		attachments[0].format = vk.capture_format;
		attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // this will be completely overwritten
		attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;   // needed for next render pass
		attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

		colorRef0.attachment = 0;
		colorRef0.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorRef0;

		desc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		desc.pNext = NULL;
		desc.flags = 0;
		desc.pAttachments = attachments;
		desc.attachmentCount = 1;
		desc.pSubpasses = &subpass;
		desc.subpassCount = 1;

		VK_CHECK( qvkCreateRenderPass( device, &desc, NULL, &vk.render_pass.capture ) );
		SET_OBJECT_NAME( vk.render_pass.capture, "render pass - capture", VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT );
	}

	colorRef0.attachment = 0;
	colorRef0.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	desc.attachmentCount = 1;

	Com_Memset( &subpass, 0, sizeof( subpass ) );
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef0;

	// gamma post-processing
	attachments[0].flags = 0;
	attachments[0].format = vk.present_format.format;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE; // needed for presentation
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = vk.initSwapchainLayout;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	desc.dependencyCount = 1;
	desc.pDependencies = &deps[2];

	VK_CHECK( qvkCreateRenderPass( device, &desc, NULL, &vk.render_pass.gamma ) );
	SET_OBJECT_NAME( vk.render_pass.gamma, "render pass - gamma", VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT );

	// screenmap
	desc.dependencyCount = 2;
	desc.pDependencies = &deps[0];

	// screenmap resolve/color buffer
	attachments[0].flags = 0;
	attachments[0].format = vk.color_format;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
#ifdef USE_BUFFER_CLEAR
	if ( vk.screenMapSamples > VK_SAMPLE_COUNT_1_BIT )
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	else
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
#else
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // Assuming this will be completely overwritten
#endif
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;   // needed for next render pass
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	// screenmap depth buffer
	attachments[1].flags = 0;
	attachments[1].format = depth_format;
	attachments[1].samples = vk.screenMapSamples;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // Need empty depth buffer before use
	attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	colorRef0.attachment = 0;
	colorRef0.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	depthRef0.attachment = 1;
	depthRef0.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	Com_Memset( &subpass, 0, sizeof( subpass ) );
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef0;
	subpass.pDepthStencilAttachment = &depthRef0;

	Com_Memset( &desc, 0, sizeof( desc ) );
	desc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.pAttachments = attachments;
	desc.pSubpasses = &subpass;
	desc.subpassCount = 1;
	desc.attachmentCount = 2;
	desc.dependencyCount = 2;
	desc.pDependencies = deps;

	if ( vk.screenMapSamples > VK_SAMPLE_COUNT_1_BIT ) {

		attachments[2].flags = 0;
		attachments[2].format = vk.color_format;
		attachments[2].samples = vk.screenMapSamples;
#ifdef USE_BUFFER_CLEAR
		attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
#else
		attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
#endif
		attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[2].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachments[2].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		desc.attachmentCount = 3;

		colorRef0.attachment = 2; // screenmap msaa image attachment
		colorRef0.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		colorResolveRef.attachment = 0; // screenmap resolve image attachment
		colorResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		subpass.pResolveAttachments = &colorResolveRef;
	}

	VK_CHECK( qvkCreateRenderPass( device, &desc, NULL, &vk.render_pass.screenmap ) );

	SET_OBJECT_NAME( vk.render_pass.screenmap, "render pass - screenmap", VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT );

	if ( liquidCaptureActive ) {
		VkAttachmentDescription liquidAttachment;
		VkAttachmentReference liquidColorRef;
		VkSubpassDescription liquidSubpass;
		VkSubpassDependency liquidDeps[2];
		VkRenderPassCreateInfo liquidDesc;

		Com_Memset( &liquidAttachment, 0, sizeof( liquidAttachment ) );
		liquidAttachment.format = vk.color_format;
		liquidAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		liquidAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		liquidAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		liquidAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		liquidAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		liquidAttachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		liquidAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		liquidColorRef.attachment = 0;
		liquidColorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		Com_Memset( &liquidSubpass, 0, sizeof( liquidSubpass ) );
		liquidSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		liquidSubpass.colorAttachmentCount = 1;
		liquidSubpass.pColorAttachments = &liquidColorRef;

		Com_Memset( liquidDeps, 0, sizeof( liquidDeps ) );
		liquidDeps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		liquidDeps[0].dstSubpass = 0;
		liquidDeps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		liquidDeps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		liquidDeps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		liquidDeps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		liquidDeps[1].srcSubpass = 0;
		liquidDeps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		liquidDeps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		liquidDeps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		liquidDeps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		liquidDeps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		Com_Memset( &liquidDesc, 0, sizeof( liquidDesc ) );
		liquidDesc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		liquidDesc.attachmentCount = 1;
		liquidDesc.pAttachments = &liquidAttachment;
		liquidDesc.subpassCount = 1;
		liquidDesc.pSubpasses = &liquidSubpass;
		liquidDesc.dependencyCount = ARRAY_LEN( liquidDeps );
		liquidDesc.pDependencies = liquidDeps;

		VK_CHECK( qvkCreateRenderPass( device, &liquidDesc, NULL,
			&vk.render_pass.liquid_snapshot ) );
		SET_OBJECT_NAME( vk.render_pass.liquid_snapshot,
			"render pass - liquid snapshot",
			VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT );
	}
	vk_lifetime_mark_create( RTX_VK_RESOURCE_RENDER_PASSES, __func__ );
}


static void allocate_and_bind_image_memory(VkImage image) {
	VkMemoryRequirements memory_requirements;
	VkDeviceSize alignment;
	ImageChunk *chunk;
	int i;

	qvkGetImageMemoryRequirements(vk.device, image, &memory_requirements);

	if ( memory_requirements.size > vk.image_chunk_size ) {
		ri.Error( ERR_FATAL, "Vulkan: could not allocate memory, image is too large (%ikbytes).",
			(int)(memory_requirements.size/1024) );
	}

	chunk = NULL;

	// Try to find an existing chunk of sufficient capacity.
	alignment = memory_requirements.alignment;
	for ( i = 0; i < vk_world.num_image_chunks; i++ ) {
		// ensure that memory region has proper alignment
		VkDeviceSize offset = PAD( vk_world.image_chunks[i].used, alignment );

		if ( offset + memory_requirements.size <= vk.image_chunk_size ) {
			chunk = &vk_world.image_chunks[i];
			chunk->used = offset + memory_requirements.size;
			break;
		}
	}

	// Allocate a new chunk in case we couldn't find suitable existing chunk.
	if (chunk == NULL) {
		VkMemoryAllocateInfo alloc_info;
		VkDeviceMemory memory;

		if (vk_world.num_image_chunks >= MAX_IMAGE_CHUNKS) {
			ri.Error(ERR_FATAL, "Vulkan: image chunk limit has been reached" );
		}

		alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.pNext = NULL;
		alloc_info.allocationSize = vk.image_chunk_size;
		alloc_info.memoryTypeIndex = find_memory_type( memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );

		VK_CHECK( qvkAllocateMemory( vk.device, &alloc_info, NULL, &memory ) );

		chunk = &vk_world.image_chunks[vk_world.num_image_chunks];
		chunk->memory = memory;
		chunk->used = memory_requirements.size;

		SET_OBJECT_NAME( memory, va( "image memory chunk %i", vk_world.num_image_chunks ), VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT );

		vk_world.num_image_chunks++;
	}

	VK_CHECK(qvkBindImageMemory(vk.device, image, chunk->memory, chunk->used - memory_requirements.size));
}


static void vk_clean_staging_buffer( void )
{
	if ( vk_world.staging_buffer != VK_NULL_HANDLE ) {
		qvkDestroyBuffer( vk.device, vk_world.staging_buffer, NULL );
		vk_world.staging_buffer = VK_NULL_HANDLE;
	}

	if ( vk_world.staging_buffer_memory != VK_NULL_HANDLE ) {
		qvkFreeMemory( vk.device, vk_world.staging_buffer_memory, NULL );
		vk_world.staging_buffer_memory = VK_NULL_HANDLE;
	}

	vk_world.staging_buffer_ptr = NULL;
	vk_world.staging_buffer_size = 0;
#ifdef USE_UPLOAD_QUEUE
	vk_world.staging_buffer_offset = 0;
#endif
}


#ifdef USE_UPLOAD_QUEUE
static void vk_wait_staging_buffer( void )
{
	if ( vk.aux_fence_wait )
	{
		VkResult res;
		res = qvkWaitForFences( vk.device, 1, &vk.aux_fence, VK_TRUE, 5 * 1000000000ULL );
		if ( res != VK_SUCCESS ) {
			ri.Error( ERR_FATAL, "vkWaitForFences() failed with %s at %s", vk_result_string( res ), __func__ );
		}
		qvkResetFences( vk.device, 1, &vk.aux_fence );
		vk.aux_fence_wait = qfalse;
		VK_CHECK( qvkResetCommandBuffer( vk.staging_command_buffer, 0 ) );
	}
}


static void vk_submit_staging_buffer( qboolean final )
{
	const VkPipelineStageFlags wait_dst_stage_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	VkQueue submitQueue = ( vk.async_queue_available && vk_rt_async_overlap_enabled() ) ? vk.async_queue : vk.queue;
	VkSemaphore waits;
	VkSubmitInfo submit_info;
	VkResult res;

	if ( vk_world.staging_buffer_offset == 0 ) {
		return;
	}

	//ri.Printf( PRINT_WARNING, S_COLOR_CYAN ">>> flush %i bytes (final=%i)<<<\n", (int)vk_world.staging_buffer_offset, final );

	vk_world.staging_buffer_offset = 0;
	vk.rt.stats.texture_stream_flushes++;

	VK_CHECK( qvkEndCommandBuffer( vk.staging_command_buffer ) );

	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.pNext = NULL;

	if ( vk.rendering_finished != VK_NULL_HANDLE ) {
		// first call after previous queue submission?
		waits = vk.rendering_finished;
		vk.rendering_finished = VK_NULL_HANDLE;
		submit_info.waitSemaphoreCount = 1;
		submit_info.pWaitSemaphores = &waits;
		submit_info.pWaitDstStageMask = &wait_dst_stage_mask;
	} else {
		submit_info.waitSemaphoreCount = 0;
		submit_info.pWaitSemaphores = NULL;
		submit_info.pWaitDstStageMask = NULL;
	}

	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &vk.staging_command_buffer;

	if ( vk.image_uploaded != VK_NULL_HANDLE ) {
		ri.Error( ERR_FATAL, "Vulkan: incorrect state during image upload" );
	}
	if ( final ) {
		// final submission before recording
		submit_info.signalSemaphoreCount = 1;
		submit_info.pSignalSemaphores = &vk.image_uploaded2;
		vk.image_uploaded = vk.image_uploaded2;
		VK_CHECK( qvkQueueSubmit( submitQueue, 1, &submit_info, vk.aux_fence ) );
		vk.aux_fence_wait = qtrue;
		if ( submitQueue != vk.queue ) {
			vk.rt.stats.async_overlap_submits++;
		}
	} else {
		// if submission before another upload then do explicit wait
		submit_info.signalSemaphoreCount = 0;
		submit_info.pSignalSemaphores = NULL;
		VK_CHECK( qvkQueueSubmit( submitQueue, 1, &submit_info, vk.aux_fence ) );
		res = qvkWaitForFences( vk.device, 1, &vk.aux_fence, VK_TRUE, 5 * 1000000000ULL );
		if ( res != VK_SUCCESS ) {
			ri.Error( ERR_FATAL, "vkWaitForFences() failed with %s at %s", vk_result_string( res ), __func__ );
		}
		qvkResetFences( vk.device, 1, &vk.aux_fence );
		VK_CHECK( qvkResetCommandBuffer( vk.staging_command_buffer, 0 ) );
		if ( submitQueue != vk.queue ) {
			vk.rt.stats.async_overlap_submits++;
		}
	}
}
#endif // USE_UPLOAD_QUEUE


static void ensure_staging_buffer_allocation( VkDeviceSize size )
{
	VkBufferCreateInfo buffer_desc;
	VkMemoryRequirements memory_requirements;
	VkMemoryAllocateInfo alloc_info;
	VkDeviceSize budgetCap = 0;
	uint32_t memory_type;
	void *data;

#ifdef USE_UPLOAD_QUEUE
	if ( vk_world.staging_buffer_size - vk_world.staging_buffer_offset >= size ) {
		return;
	}

	vk_submit_staging_buffer( qfalse );

	if ( vk_world.staging_buffer_size /* - vk_world.staging_buffer_offset */ >= size ) {
		return;
	}
#else
	if ( vk_world.staging_buffer_size >= size ) {
		return;
	}
#endif
	vk_clean_staging_buffer();

	vk_world.staging_buffer_size = MAX( size, STAGING_BUFFER_SIZE );
	budgetCap = (VkDeviceSize)vk_rt_texture_stream_budget_mb() * 1024u * 1024u;
	if ( budgetCap > 0 ) {
		// Keep staging growth bounded by configured streaming budget while still satisfying this allocation.
		vk_world.staging_buffer_size = MIN( vk_world.staging_buffer_size, MAX( size, budgetCap ) );
	}
	vk_world.staging_buffer_size = PAD( vk_world.staging_buffer_size, 1024 * 1024 );

	buffer_desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_desc.pNext = NULL;
	buffer_desc.flags = 0;
	buffer_desc.size = vk_world.staging_buffer_size;
	buffer_desc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	buffer_desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	buffer_desc.queueFamilyIndexCount = 0;
	buffer_desc.pQueueFamilyIndices = NULL;
	VK_CHECK(qvkCreateBuffer(vk.device, &buffer_desc, NULL, &vk_world.staging_buffer));

	qvkGetBufferMemoryRequirements( vk.device, vk_world.staging_buffer, &memory_requirements );

	memory_type = find_memory_type( memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );

	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.pNext = NULL;
	alloc_info.allocationSize = memory_requirements.size;
	alloc_info.memoryTypeIndex = memory_type;

	VK_CHECK(qvkAllocateMemory(vk.device, &alloc_info, NULL, &vk_world.staging_buffer_memory));
	VK_CHECK(qvkBindBufferMemory(vk.device, vk_world.staging_buffer, vk_world.staging_buffer_memory, 0));

	VK_CHECK(qvkMapMemory(vk.device, vk_world.staging_buffer_memory, 0, VK_WHOLE_SIZE, 0, &data));
	vk_world.staging_buffer_ptr = (byte*)data;
#ifdef USE_UPLOAD_QUEUE
	vk_world.staging_buffer_offset = 0;
#endif
	SET_OBJECT_NAME( vk_world.staging_buffer, "staging buffer", VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT );
	SET_OBJECT_NAME( vk_world.staging_buffer_memory, "staging buffer memory", VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT );
}


#ifdef USE_VK_VALIDATION
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT object_type, uint64_t object, size_t location,
	int32_t message_code, const char* layer_prefix, const char* message, void* user_data) {
	if ( message && message[0] ) {
		ri.Printf( PRINT_WARNING, "Vulkan validation (%s): %s\n",
			( layer_prefix && layer_prefix[0] ) ? layer_prefix : "unknown",
			message );
	}
	return VK_FALSE;
}
#endif


static qboolean used_instance_extension( const char *ext )
{
	const char *u;

	// allow all VK_*_surface extensions
	u = strrchr( ext, '_' );
	if ( u && Q_stricmp( u + 1, "surface" ) == 0 )
		return qtrue;

	if ( Q_stricmp( ext, VK_KHR_DISPLAY_EXTENSION_NAME ) == 0 )
		return qtrue; // needed for KMSDRM instances/devices?

	if ( Q_stricmp( ext, VK_KHR_SWAPCHAIN_EXTENSION_NAME ) == 0 )
		return qtrue;

#ifdef USE_VK_VALIDATION
	if ( vk_validation_requested() && Q_stricmp( ext, VK_EXT_DEBUG_REPORT_EXTENSION_NAME ) == 0 )
		return qtrue;
#endif

	if ( Q_stricmp( ext, VK_EXT_DEBUG_UTILS_EXTENSION_NAME ) == 0 )
		return qtrue;

	if ( Q_stricmp( ext, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME ) == 0 )
		return qtrue;

	if ( Q_stricmp( ext, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME ) == 0 )
		return qtrue;

	if ( Q_stricmp( ext, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME ) == 0 )
		return qtrue;

	return qfalse;
}


static void create_instance( void )
{
#ifdef USE_VK_VALIDATION
	const char* validation_layer_name = "VK_LAYER_LUNARG_standard_validation";
	const char* validation_layer_name2 = "VK_LAYER_KHRONOS_validation";
	const char* active_validation_layer = NULL;
#endif
	const qboolean wantValidation = vk_validation_requested();
	VkInstanceCreateInfo desc;
	VkInstanceCreateFlags flags;
	VkExtensionProperties *extension_properties;
	VkResult res;
	VkResult versionResult;
	const char **extension_names;
	uint32_t i, n, count, extension_count;
	uint32_t loaderApiVersion;
	VkApplicationInfo appInfo;

	flags = 0;
	count = 0;
	extension_count = 0;
	vk_instance_swapchain_colorspace = qfalse;
	s_vkValidationLayerEnabled = qfalse;
	VK_CHECK(qvkEnumerateInstanceExtensionProperties(NULL, &count, NULL));

	extension_properties = (VkExtensionProperties *)ri.Malloc(sizeof(VkExtensionProperties) * count);
	extension_names = (const char**)ri.Malloc(sizeof(char *) * count);

	VK_CHECK( qvkEnumerateInstanceExtensionProperties( NULL, &count, extension_properties ) );
	for ( i = 0; i < count; i++ ) {
		const char *ext = extension_properties[i].extensionName;

		if ( !used_instance_extension( ext ) ) {
			continue;
		}

		// search for duplicates
		for ( n = 0; n < extension_count; n++ ) {
			if ( Q_stricmp( ext, extension_names[ n ] ) == 0 ) {
				break;
			}
		}
		if ( n != extension_count ) {
			continue;
		}

		extension_names[ extension_count++ ] = ext;

		if ( Q_stricmp( ext, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME ) == 0 ) {
			flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
		}

		if ( Q_stricmp( ext, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME ) == 0 ) {
			vk_instance_swapchain_colorspace = qtrue;
		}

		ri.Printf(PRINT_DEVELOPER, "instance extension: %s\n", ext);
	}

	loaderApiVersion = VK_API_VERSION_1_0;
	if ( qvkEnumerateInstanceVersion ) {
		versionResult = qvkEnumerateInstanceVersion( &loaderApiVersion );
		if ( versionResult != VK_SUCCESS ) {
			ri.Printf( PRINT_WARNING,
				"Vulkan: could not query loader API version (%s); using Vulkan 1.0\n",
				vk_result_string( versionResult ) );
			loaderApiVersion = VK_API_VERSION_1_0;
		}
	}

	if ( VK_VERSION_MAJOR( loaderApiVersion ) > 1 ||
		( VK_VERSION_MAJOR( loaderApiVersion ) == 1 && VK_VERSION_MINOR( loaderApiVersion ) >= 2 ) ) {
		s_vkInstanceApiVersion = VK_API_VERSION_1_2;
	} else if ( VK_VERSION_MAJOR( loaderApiVersion ) == 1 &&
		VK_VERSION_MINOR( loaderApiVersion ) >= 1 ) {
		s_vkInstanceApiVersion = VK_API_VERSION_1_1;
	} else {
		s_vkInstanceApiVersion = VK_API_VERSION_1_0;
	}

	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pNext = NULL;
	appInfo.pApplicationName = NULL; // Q3_VERSION;
	appInfo.applicationVersion = 0x0;
	appInfo.pEngineName = NULL;
	appInfo.engineVersion = 0x0;
	appInfo.apiVersion = s_vkInstanceApiVersion;

	ri.Printf( PRINT_DEVELOPER, "Vulkan: loader API %u.%u, requesting %u.%u\n",
		VK_VERSION_MAJOR( loaderApiVersion ), VK_VERSION_MINOR( loaderApiVersion ),
		VK_VERSION_MAJOR( s_vkInstanceApiVersion ), VK_VERSION_MINOR( s_vkInstanceApiVersion ) );

	// create instance
	desc.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = flags;
	desc.pApplicationInfo = &appInfo;
	desc.enabledExtensionCount = extension_count;
	desc.ppEnabledExtensionNames = extension_names;

#ifdef USE_VK_VALIDATION
	if ( wantValidation ) {
		desc.enabledLayerCount = 1;
		desc.ppEnabledLayerNames = &validation_layer_name;
		res = qvkCreateInstance( &desc, NULL, &vk_instance );

		if ( res == VK_SUCCESS ) {
			active_validation_layer = validation_layer_name;
		}
		else if ( res == VK_ERROR_LAYER_NOT_PRESENT ) {
			desc.enabledLayerCount = 1;
			desc.ppEnabledLayerNames = &validation_layer_name2;
			res = qvkCreateInstance( &desc, NULL, &vk_instance );

			if ( res == VK_SUCCESS ) {
				active_validation_layer = validation_layer_name2;
			}
			else if ( res == VK_ERROR_LAYER_NOT_PRESENT ) {
				ri.Printf( PRINT_WARNING, "Vulkan: requested validation layers are not available\n" );

				// try without validation layer
				desc.enabledLayerCount = 0;
				desc.ppEnabledLayerNames = NULL;
				res = qvkCreateInstance( &desc, NULL, &vk_instance );
			}
		}
	} else {
		desc.enabledLayerCount = 0;
		desc.ppEnabledLayerNames = NULL;
		res = qvkCreateInstance( &desc, NULL, &vk_instance );
	}

	if ( active_validation_layer ) {
		s_vkValidationLayerEnabled = qtrue;
	}
#else
	if ( wantValidation ) {
		ri.Printf( PRINT_WARNING, "Vulkan: rtx_debug_vk_validation is enabled, but this build does not include validation-layer support\n" );
	}
	desc.enabledLayerCount = 0;
	desc.ppEnabledLayerNames = NULL;

	res = qvkCreateInstance( &desc, NULL, &vk_instance );
#endif

	ri.Free( (void*)extension_names );
	ri.Free( extension_properties );

	if ( res != VK_SUCCESS ) {
		ri.Error( ERR_FATAL, "Vulkan: instance creation failed with %s", vk_result_string( res ) );
	}

#ifdef USE_VK_VALIDATION
	if ( s_vkValidationLayerEnabled ) {
		ri.Printf( PRINT_ALL, "Vulkan: validation layer enabled (%s)\n", active_validation_layer );
	} else if ( wantValidation ) {
		ri.Printf( PRINT_WARNING, "Vulkan: running without validation layers for this instance\n" );
	}
#endif
}


static VkFormat get_depth_format( VkPhysicalDevice physical_device ) {
	VkFormatProperties props;
	VkFormat formats[2];
	int i;

	if ( glConfig.stencilBits > 0 ) {
		formats[0] = glConfig.depthBits == 16 ? VK_FORMAT_D16_UNORM_S8_UINT : VK_FORMAT_D24_UNORM_S8_UINT;
		formats[1] = VK_FORMAT_D32_SFLOAT_S8_UINT;
	} else {
		formats[0] = glConfig.depthBits == 16 ? VK_FORMAT_D16_UNORM : VK_FORMAT_X8_D24_UNORM_PACK32;
		formats[1] = VK_FORMAT_D32_SFLOAT;
	}

	for ( i = 0; i < ARRAY_LEN( formats ); i++ ) {
		qvkGetPhysicalDeviceFormatProperties( physical_device, formats[i], &props );
		if ( ( props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT ) != 0 ) {
			return formats[i];
		}
	}

	ri.Error( ERR_FATAL, "get_depth_format: failed to find depth attachment format" );
	return VK_FORMAT_UNDEFINED; // never get here
}


// Check if we can use vkCmdBlitImage for the given source and destination image formats.
static qboolean vk_blit_enabled( VkPhysicalDevice physical_device, const VkFormat srcFormat, const VkFormat dstFormat )
{
	VkFormatProperties formatProps;

	qvkGetPhysicalDeviceFormatProperties( physical_device, srcFormat, &formatProps );
	if ( ( formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT ) == 0 ) {
		return qfalse;
	}

	qvkGetPhysicalDeviceFormatProperties( physical_device, dstFormat, &formatProps );
	if ( ( formatProps.linearTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT ) == 0 ) {
		return qfalse;
	}

	return qtrue;
}


static qboolean vk_hdr_scene_format_supported( VkPhysicalDevice physical_device,
	VkFormat format )
{
	VkFormatProperties properties;
	/*
	 * The scene target is sampled by post/capture and may receive the RT
	 * reconstruction blit. Final SDR capture no longer blits from it, so
	 * requiring BLIT_SRC would reject otherwise valid RGBA16F devices.
	 */
	const VkFormatFeatureFlags requiredFeatures =
		VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
		VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
		VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
		VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
		VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
		VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
		VK_FORMAT_FEATURE_BLIT_DST_BIT;

	qvkGetPhysicalDeviceFormatProperties( physical_device, format, &properties );
	return ( properties.optimalTilingFeatures & requiredFeatures ) ==
		requiredFeatures ? qtrue : qfalse;
}


static VkFormat get_hdr_format( VkPhysicalDevice physical_device,
	VkFormat base_format )
{
	static qboolean warnedUnsupportedHdrFormat = qfalse;
	int precision;

	if ( r_fbo->integer == 0 ) {
		return base_format;
	}

	if ( r_hdr && r_hdr->integer > 0 ) {
		const VkFormat hdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

		if ( vk_hdr_scene_format_supported( physical_device, hdrFormat ) ) {
			return hdrFormat;
		}
		if ( !warnedUnsupportedHdrFormat ) {
			ri.Printf( PRINT_WARNING,
				"RTX: r_hdr requested floating-point scene storage but %s lacks the required color, sampling, transfer, and blit features; using %s SDR storage\n",
				vk_format_string( hdrFormat ), vk_format_string( base_format ) );
			warnedUnsupportedHdrFormat = qtrue;
		}
		return base_format;
	}

	precision = r_hdrPrecision ? r_hdrPrecision->integer : 0;
	switch ( precision ) {
	case -1:
		return VK_FORMAT_B4G4R4A4_UNORM_PACK16;
	case 8:
		return base_format;
	case 16:
		return VK_FORMAT_R16G16B16A16_UNORM;
	default:
		break;
	}

	if ( r_hdr && r_hdr->integer < 0 ) {
		return VK_FORMAT_B4G4R4A4_UNORM_PACK16;
	}
	return base_format;
}


qboolean vk_scene_linear_enabled( void )
{
	return ( r_fbo && r_fbo->integer &&
		r_hdr && r_hdr->integer > 0 &&
		vk.color_format == VK_FORMAT_R16G16B16A16_SFLOAT ) ? qtrue : qfalse;
}


typedef struct {
	int bits;
	VkFormat rgb;
	VkFormat bgr;
} present_format_t;

static const present_format_t present_formats[] = {
	//{12, VK_FORMAT_B4G4R4A4_UNORM_PACK16, VK_FORMAT_R4G4B4A4_UNORM_PACK16},
	//{15, VK_FORMAT_B5G5R5A1_UNORM_PACK16, VK_FORMAT_R5G5B5A1_UNORM_PACK16},
	{16, VK_FORMAT_B5G6R5_UNORM_PACK16, VK_FORMAT_R5G6B5_UNORM_PACK16},
	{24, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM},
	{30, VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_FORMAT_A2R10G10B10_UNORM_PACK32},
	//{32, VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_FORMAT_B10G11R11_UFLOAT_PACK32}
};

static void get_present_format( int present_bits, VkFormat *bgr, VkFormat *rgb ) {
	const present_format_t *pf, *sel;
	int i;

	sel = NULL;
	pf = present_formats;
	for ( i = 0; i < ARRAY_LEN( present_formats ); i++, pf++ ) {
		if ( pf->bits <= present_bits  ) {
			sel = pf;
		}
	}
	if ( !sel ) {
		*bgr = VK_FORMAT_B8G8R8A8_UNORM;
		*rgb = VK_FORMAT_R8G8B8A8_UNORM;
	} else {
		*bgr = sel->bgr;
		*rgb = sel->rgb;
	}
}


static qboolean vk_find_surface_format( const VkSurfaceFormatKHR *candidates, uint32_t count,
	VkFormat bgr, VkFormat rgb, VkColorSpaceKHR colorSpace, VkSurfaceFormatKHR *out )
{
	uint32_t i;

	for ( i = 0; i < count; i++ ) {
		if ( ( candidates[i].format == bgr || candidates[i].format == rgb ) &&
			candidates[i].colorSpace == colorSpace ) {
			*out = candidates[i];
			return qtrue;
		}
	}

	return qfalse;
}


static qboolean vk_find_hdr10_surface_format( const VkSurfaceFormatKHR *candidates,
	uint32_t count, VkSurfaceFormatKHR *out )
{
	static const VkFormat hdr_formats[] = {
		VK_FORMAT_A2B10G10R10_UNORM_PACK32,
		VK_FORMAT_A2R10G10B10_UNORM_PACK32
	};
	uint32_t i;

	for ( i = 0; i < ARRAY_LEN( hdr_formats ); i++ ) {
		uint32_t j;

		for ( j = 0; j < count; j++ ) {
			if ( candidates[j].format == hdr_formats[i] &&
				candidates[j].colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT ) {
				*out = candidates[j];
				return qtrue;
			}
		}
	}

	return qfalse;
}


static void vk_init_display_output_defaults( rendererDisplayOutput_t *output )
{
	if ( !output ) {
		return;
	}

	Com_Memset( output, 0, sizeof( *output ) );
	output->displayIndex = -1;
	output->nativeBackend = ROUTPUT_BACKEND_SDR_SRGB;
	output->sdrWhiteNits = 203.0f;
	output->hdrHeadroom = 1.0f;
	output->maxLuminanceNits = 203.0f;
	output->maxFullFrameLuminanceNits = 203.0f;
	Q_strncpyz( output->reason, "display output query unavailable", sizeof( output->reason ) );
}


static rendererOutputRequest_t vk_output_request( void )
{
	const int request = r_outputBackend ? r_outputBackend->integer : ROUTPUT_REQUEST_AUTO;

	if ( request < ROUTPUT_REQUEST_AUTO || request >= ROUTPUT_REQUEST_COUNT ) {
		return ROUTPUT_REQUEST_AUTO;
	}

	return (rendererOutputRequest_t)request;
}


static void vk_query_display_output( void )
{
	vk_init_display_output_defaults( &vk.displayOutput );
	if ( ri.GLimp_QueryDisplayOutput ) {
		ri.GLimp_QueryDisplayOutput( &vk.displayOutput );
	}
}


static qboolean vk_output_request_wants_hdr10( void )
{
	vk.outputRequest = vk_output_request();

	switch ( vk.outputRequest ) {
	case ROUTPUT_REQUEST_SDR_SRGB:
		return qfalse;
	case ROUTPUT_REQUEST_HDR10_PQ:
		return qtrue;
	case ROUTPUT_REQUEST_LINUX_EXPERIMENTAL_HDR:
		return ( r_outputAllowExperimentalLinuxHDR &&
			r_outputAllowExperimentalLinuxHDR->integer &&
			vk.displayOutput.linuxHdrExperimental &&
			vk.displayOutput.explicitLinuxHdrProtocol ) ? qtrue : qfalse;
	case ROUTPUT_REQUEST_AUTO:
		return ( r_hdrDisplay && r_hdrDisplay->integer ) ? qtrue : qfalse;
	case ROUTPUT_REQUEST_WINDOWS_SCRGB:
	case ROUTPUT_REQUEST_MACOS_EDR:
	default:
		return qfalse;
	}
}


static qboolean vk_select_surface_format( VkPhysicalDevice physical_device, VkSurfaceKHR surface )
{
	VkFormat base_bgr, base_rgb;
	VkFormat ext_bgr, ext_rgb;
	VkSurfaceFormatKHR hdr10_format;
	VkSurfaceFormatKHR *candidates;
	uint32_t format_count;
	VkResult res;

	res = qvkGetPhysicalDeviceSurfaceFormatsKHR( physical_device, surface, &format_count, NULL );
	if ( res < 0 ) {
		ri.Printf( PRINT_ERROR, "vkGetPhysicalDeviceSurfaceFormatsKHR returned %s\n", vk_result_string( res ) );
		return qfalse;
	}

	if ( format_count == 0 ) {
		ri.Printf( PRINT_ERROR, "...no surface formats found\n" );
		return qfalse;
	}

	candidates = (VkSurfaceFormatKHR*)ri.Malloc( format_count * sizeof(VkSurfaceFormatKHR) );

	VK_CHECK( qvkGetPhysicalDeviceSurfaceFormatsKHR( physical_device, surface, &format_count, candidates ) );

	get_present_format( 24, &base_bgr, &base_rgb );

	if ( r_fbo->integer ) {
		get_present_format( r_presentBits->integer, &ext_bgr, &ext_rgb );
	} else {
		ext_bgr = base_bgr;
		ext_rgb = base_rgb;
	}

	if ( format_count == 1 && candidates[0].format == VK_FORMAT_UNDEFINED ) {
		// special case that means we can choose any format
		vk.base_format.format = base_bgr;
		vk.base_format.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		vk.present_format.format = ext_bgr;
		vk.present_format.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	}
	else {
		if ( !vk_find_surface_format( candidates, format_count, base_bgr, base_rgb,
			VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, &vk.base_format ) ) {
			vk.base_format = candidates[0];
		}

		if ( !vk_find_surface_format( candidates, format_count, ext_bgr, ext_rgb,
			VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, &vk.present_format ) ) {
			vk.present_format = vk.base_format;
		}
	}

	vk_query_display_output();
	vk.outputBackend = ROUTPUT_BACKEND_SDR_SRGB;
	vk.hdrDisplayActive = qfalse;
	if ( vk_output_request_wants_hdr10() ) {
		if ( !r_fbo->integer ) {
			ri.Printf( PRINT_WARNING,
				"...native HDR presentation requires \\r_fbo 1, using SDR presentation\n" );
		} else if ( !vk.swapchainColorspace ) {
			ri.Printf( PRINT_WARNING, "...%s is not available, using SDR presentation\n",
				VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME );
		} else if ( format_count == 1 && candidates[0].format == VK_FORMAT_UNDEFINED ) {
			ri.Printf( PRINT_WARNING,
				"...surface reports arbitrary SDR format selection only, using SDR presentation\n" );
		} else if ( vk_find_hdr10_surface_format( candidates, format_count, &hdr10_format ) ) {
			vk.present_format = hdr10_format;
			vk.hdrDisplayActive = qtrue;
			vk.outputBackend = ROUTPUT_BACKEND_HDR10_PQ;
		} else {
			ri.Printf( PRINT_WARNING,
				"...HDR10 ST2084 surface format is not available, using SDR presentation\n" );
		}
	}

	if ( !r_fbo->integer ) {
		vk.present_format = vk.base_format;
	}

	ri.Free( candidates );

	return qtrue;
}


static void setup_surface_formats( VkPhysicalDevice physical_device )
{
	VkFormatProperties depthFormatProperties;

	vk.depth_format = get_depth_format( physical_device );
	qvkGetPhysicalDeviceFormatProperties( physical_device, vk.depth_format, &depthFormatProperties );
	vk.globalFogDepthSampleSupported =
		( depthFormatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT ) != 0
			? qtrue : qfalse;
	vk.liquidDepthSampleSupported = vk.globalFogDepthSampleSupported;

	vk.color_format = get_hdr_format( physical_device, vk.base_format.format );

	vk.capture_format = VK_FORMAT_R8G8B8A8_UNORM;

	vk.bloom_format = vk_scene_linear_enabled() ?
		VK_FORMAT_R16G16B16A16_SFLOAT : vk.base_format.format;

	if ( r_fbo->integer ) {
		vk.blitEnabled = vk_blit_enabled( physical_device,
			vk.capture_format, vk.capture_format );
	} else {
		vk.blitEnabled = vk_blit_enabled( physical_device,
			vk.present_format.format, vk.capture_format );
		if ( !vk.blitEnabled ) {
			/*
			 * vkCmdCopyImage requires matching formats. Preserve the
			 * swapchain channel order for direct non-FBO readback when a
			 * present-to-RGBA blit is unavailable.
			 */
			vk.capture_format = vk.present_format.format;
		}
	}

	if ( r_globalFog && r_globalFog->integer && !vk.globalFogDepthSampleSupported ) {
		ri.Printf( PRINT_WARNING,
			"RTX: depth format %s cannot be sampled; per-map global fog will be disabled on this device\n",
			vk_format_string( vk.depth_format ) );
	}
	if ( vk_depth_fade_requested() && !vk.liquidDepthSampleSupported ) {
		ri.Printf( PRINT_WARNING,
			"RTX: depth format %s cannot be sampled; soft-particle depth fade will use authored blending only\n",
			vk_format_string( vk.depth_format ) );
	}
	if ( vk_liquid_requested() && !vk.liquidDepthSampleSupported ) {
		ri.Printf( PRINT_WARNING,
			"RTX: depth format %s cannot be sampled; enhanced liquids will keep refraction/reflection but disable foreground depth rejection\n",
			vk_format_string( vk.depth_format ) );
	}
}


static const char *renderer_name( const VkPhysicalDeviceProperties *props ) {
	static char buf[sizeof( props->deviceName ) + 64];
	const char *device_type;

	switch ( props->deviceType ) {
		case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: device_type = "Integrated"; break;
		case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: device_type = "Discrete"; break;
		case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: device_type = "Virtual"; break;
		case VK_PHYSICAL_DEVICE_TYPE_CPU: device_type = "CPU"; break;
		default: device_type = "OTHER"; break;
	}

	Com_sprintf( buf, sizeof( buf ), "%s %s, 0x%04x",
		device_type, props->deviceName, props->deviceID );

	return buf;
}


static qboolean vk_query_rt_device_features(
	VkPhysicalDevice physical_device,
	qboolean queryBufferDeviceAddress,
	qboolean queryDescriptorIndexing,
	qboolean queryAccelerationStructure,
	qboolean queryRayQuery,
	qboolean queryRayTracingPipeline,
	qboolean *bufferDeviceAddress,
	qboolean *descriptorIndexing,
	qboolean *accelerationStructure,
	qboolean *rayQuery,
	qboolean *rayTracingPipeline )
{
	VkPhysicalDeviceFeatures2 features2;
	VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures;
	VkPhysicalDeviceDescriptorIndexingFeatures descriptorIndexingFeatures;
	VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures;
	VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures;
	VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures;
	void **pNextPtr;

	*bufferDeviceAddress = qfalse;
	*descriptorIndexing = qfalse;
	*accelerationStructure = qfalse;
	*rayQuery = qfalse;
	*rayTracingPipeline = qfalse;

	if ( qvkGetPhysicalDeviceFeatures2 == NULL &&
		qvkGetPhysicalDeviceFeatures2KHR == NULL ) {
		return qfalse;
	}

	Com_Memset( &features2, 0, sizeof( features2 ) );
	Com_Memset( &bufferDeviceAddressFeatures, 0, sizeof( bufferDeviceAddressFeatures ) );
	Com_Memset( &descriptorIndexingFeatures, 0, sizeof( descriptorIndexingFeatures ) );
	Com_Memset( &accelerationStructureFeatures, 0, sizeof( accelerationStructureFeatures ) );
	Com_Memset( &rayQueryFeatures, 0, sizeof( rayQueryFeatures ) );
	Com_Memset( &rayTracingPipelineFeatures, 0, sizeof( rayTracingPipelineFeatures ) );

	features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	pNextPtr = (void **)&features2.pNext;

	if ( queryBufferDeviceAddress ) {
		bufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
		*pNextPtr = &bufferDeviceAddressFeatures;
		pNextPtr = (void **)&bufferDeviceAddressFeatures.pNext;
	}
	if ( queryDescriptorIndexing ) {
		descriptorIndexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
		*pNextPtr = &descriptorIndexingFeatures;
		pNextPtr = (void **)&descriptorIndexingFeatures.pNext;
	}
	if ( queryAccelerationStructure ) {
		accelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
		*pNextPtr = &accelerationStructureFeatures;
		pNextPtr = (void **)&accelerationStructureFeatures.pNext;
	}
	if ( queryRayQuery ) {
		rayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
		*pNextPtr = &rayQueryFeatures;
		pNextPtr = (void **)&rayQueryFeatures.pNext;
	}
	if ( queryRayTracingPipeline ) {
		rayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
		*pNextPtr = &rayTracingPipelineFeatures;
	}

	if ( qvkGetPhysicalDeviceFeatures2 != NULL ) {
		qvkGetPhysicalDeviceFeatures2( physical_device, &features2 );
	} else {
		qvkGetPhysicalDeviceFeatures2KHR( physical_device, &features2 );
	}

	*bufferDeviceAddress = bufferDeviceAddressFeatures.bufferDeviceAddress ? qtrue : qfalse;
	*descriptorIndexing = descriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing ? qtrue : qfalse;
	*accelerationStructure = accelerationStructureFeatures.accelerationStructure ? qtrue : qfalse;
	*rayQuery = rayQueryFeatures.rayQuery ? qtrue : qfalse;
	*rayTracingPipeline = rayTracingPipelineFeatures.rayTracingPipeline ? qtrue : qfalse;
	return qtrue;
}


static qboolean vk_create_device( VkPhysicalDevice physical_device, int device_index ) {

	VkPhysicalDeviceBufferDeviceAddressFeatures devaddr_features;
	VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing_features;
	VkPhysicalDeviceAccelerationStructureFeaturesKHR accel_struct_features;
	VkPhysicalDeviceRayTracingPipelineFeaturesKHR rt_pipeline_features;
	VkPhysicalDeviceRayQueryFeaturesKHR ray_query_features;

#ifdef _DEBUG
	VkPhysicalDeviceTimelineSemaphoreFeatures timeline_semaphore;
	VkPhysicalDeviceVulkanMemoryModelFeatures memory_model;
	VkPhysicalDevice8BitStorageFeatures storage_8bit_features;
#endif

	ri.Printf( PRINT_ALL, "...selected physical device: %i\n", device_index );
	vk.hdrMetadata = qfalse;

	// select surface format
	if ( !vk_select_surface_format( physical_device, vk_surface ) ) {
		return qfalse;
	}

	setup_surface_formats( physical_device );

	// select queue family
	{
		VkQueueFamilyProperties *queue_families;
		uint32_t queue_family_count;
		uint32_t i;

		qvkGetPhysicalDeviceQueueFamilyProperties( physical_device, &queue_family_count, NULL );
		queue_families = (VkQueueFamilyProperties*)ri.Malloc( queue_family_count * sizeof( VkQueueFamilyProperties ) );
		qvkGetPhysicalDeviceQueueFamilyProperties( physical_device, &queue_family_count, queue_families );

		// select queue family with presentation and graphics support
		vk.queue_family_index = ~0U;
		vk.queue_family_queue_count = 0;
		for (i = 0; i < queue_family_count; i++) {
			VkBool32 presentation_supported;
			VK_CHECK( qvkGetPhysicalDeviceSurfaceSupportKHR( physical_device, i, vk_surface, &presentation_supported ) );

			if (presentation_supported && (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
				vk.queue_family_index = i;
				vk.queue_family_queue_count = queue_families[i].queueCount;
				break;
			}
		}

		ri.Free( queue_families );

		if ( vk.queue_family_index == ~0U ) {
			ri.Printf( PRINT_ERROR, "...failed to find graphics queue family\n" );

			return qfalse;
		}
	}

	// create VkDevice
	{
		const char *device_extension_list[24];
		uint32_t device_extension_count;
		const char *ext, *end;
		char *str;
		const float priorities[2] = { 1.0f, 1.0f };
		VkExtensionProperties *extension_properties;
		VkDeviceQueueCreateInfo queue_desc;
		VkPhysicalDeviceFeatures device_features;
		VkPhysicalDeviceFeatures features;
		VkDeviceCreateInfo device_desc;
		VkResult res;
		qboolean swapchainSupported = qfalse;
		qboolean portabilitySubset = qfalse;
		qboolean dedicatedAllocation = qfalse;
		qboolean memoryRequirements2 = qfalse;
		qboolean debugMarker = qfalse;
		qboolean hdrMetadata = qfalse;
		qboolean descriptorIndexingExtension = qfalse;
		qboolean descriptorIndexing = qfalse;
		qboolean timelineSemaphore = qfalse;
		qboolean memoryModel = qfalse;
		qboolean bufferDeviceAddressExtension = qfalse;
		qboolean devAddrFeat = qfalse;
		qboolean deferredHostOperations = qfalse;
		qboolean accelerationStructure = qfalse;
		qboolean rayQuery = qfalse;
		qboolean rayTracingPipeline = qfalse;
		qboolean spirv14Extension = qfalse;
		qboolean shaderFloatControlsExtension = qfalse;
		qboolean spirv14 = qfalse;
		qboolean shaderFloatControls = qfalse;
		qboolean core12 = qfalse;
		qboolean enableBufferDeviceAddress = qfalse;
		qboolean featureQueryAvailable = qfalse;
		qboolean bufferDeviceAddressFeature = qfalse;
		qboolean descriptorIndexingFeature = qfalse;
		qboolean accelerationStructureFeature = qfalse;
		qboolean rayQueryFeature = qfalse;
		qboolean rayTracingPipelineFeature = qfalse;
		VkPhysicalDeviceProperties physicalDeviceProperties;
		VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtPipelineProperties;
		VkPhysicalDeviceAccelerationStructurePropertiesKHR accelerationStructureProperties;
		VkFormatProperties rtOutputFormatProperties;
		VkFormatProperties rtColorFormatProperties;
		qboolean rtOutputFormatSupported = qfalse;
		qboolean rtPropertiesAvailable = qfalse;
		qboolean rtShaderGroupLayoutValid = qfalse;
		VkDeviceSize rtShaderGroupStride = 0;
		const int markerMode = vk_debug_markers_mode();
		const void **pNextPtr;
#ifdef _DEBUG
		qboolean storage8bit = qfalse;
#endif
		uint32_t i, len, count = 0;

		VK_CHECK( qvkEnumerateDeviceExtensionProperties( physical_device, NULL, &count, NULL ) );
		extension_properties = (VkExtensionProperties*)ri.Malloc( count * sizeof( VkExtensionProperties ) );
		VK_CHECK( qvkEnumerateDeviceExtensionProperties( physical_device, NULL, &count, extension_properties ) );

		// fill glConfig.extensions_string
		str = glConfig.extensions_string; *str = '\0';
		end = &glConfig.extensions_string[ sizeof( glConfig.extensions_string ) - 1];

		for ( i = 0; i < count; i++ ) {
			ext = extension_properties[i].extensionName;
			if ( strcmp( ext, VK_KHR_SWAPCHAIN_EXTENSION_NAME ) == 0 ) {
				swapchainSupported = qtrue;
			} else if ( strcmp( ext, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME ) == 0 ) {
				portabilitySubset = qtrue;
			} else if ( strcmp( ext, VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME ) == 0 ) {
				dedicatedAllocation = qtrue;
			} else if ( strcmp( ext, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME ) == 0 ) {
				memoryRequirements2 = qtrue;
			} else if ( strcmp( ext, VK_EXT_DEBUG_MARKER_EXTENSION_NAME ) == 0 ) {
				debugMarker = qtrue;
			} else if ( strcmp( ext, VK_EXT_HDR_METADATA_EXTENSION_NAME ) == 0 ) {
				hdrMetadata = qtrue;
			} else if ( strcmp( ext, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME ) == 0 ) {
				timelineSemaphore = qtrue;
			} else if ( strcmp( ext, VK_KHR_VULKAN_MEMORY_MODEL_EXTENSION_NAME ) == 0 ) {
				memoryModel = qtrue;
			} else if ( strcmp( ext, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME ) == 0 ) {
				bufferDeviceAddressExtension = qtrue;
			} else if ( strcmp( ext, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME ) == 0 ) {
				descriptorIndexingExtension = qtrue;
			} else if ( strcmp( ext, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME ) == 0 ) {
				deferredHostOperations = qtrue;
			} else if ( strcmp( ext, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME ) == 0 ) {
				accelerationStructure = qtrue;
			} else if ( strcmp( ext, VK_KHR_RAY_QUERY_EXTENSION_NAME ) == 0 ) {
				rayQuery = qtrue;
			} else if ( strcmp( ext, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME ) == 0 ) {
				rayTracingPipeline = qtrue;
			} else if ( strcmp( ext, VK_KHR_SPIRV_1_4_EXTENSION_NAME ) == 0 ) {
				spirv14Extension = qtrue;
			} else if ( strcmp( ext, VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME ) == 0 ) {
				shaderFloatControlsExtension = qtrue;
#ifdef _DEBUG
			} else if ( strcmp( ext, VK_KHR_8BIT_STORAGE_EXTENSION_NAME ) == 0 ) {
				storage8bit = qtrue;
#endif
			}
			// add this device extension to glConfig
			if ( i != 0 ) {
				if ( str + 1 >= end )
					continue;
				str = Q_stradd( str, " " );
			}
			len = (uint32_t)strlen( ext );
			if ( str + len >= end )
				continue;
			str = Q_stradd( str, ext );
		}

		ri.Free( extension_properties );

		qvkGetPhysicalDeviceProperties( physical_device, &physicalDeviceProperties );
		core12 = ( VK_VERSION_MAJOR( s_vkInstanceApiVersion ) > 1 ||
			( VK_VERSION_MAJOR( s_vkInstanceApiVersion ) == 1 &&
				VK_VERSION_MINOR( s_vkInstanceApiVersion ) >= 2 ) ) &&
			( VK_VERSION_MAJOR( physicalDeviceProperties.apiVersion ) > 1 ||
				( VK_VERSION_MAJOR( physicalDeviceProperties.apiVersion ) == 1 &&
					VK_VERSION_MINOR( physicalDeviceProperties.apiVersion ) >= 2 ) )
				? qtrue : qfalse;

		featureQueryAvailable = vk_query_rt_device_features(
			physical_device,
			bufferDeviceAddressExtension || core12,
			descriptorIndexingExtension || core12,
			accelerationStructure,
			rayQuery,
			rayTracingPipeline,
			&bufferDeviceAddressFeature,
			&descriptorIndexingFeature,
			&accelerationStructureFeature,
			&rayQueryFeature,
			&rayTracingPipelineFeature );

		devAddrFeat = ( ( bufferDeviceAddressExtension || core12 ) && bufferDeviceAddressFeature ) ? qtrue : qfalse;
		descriptorIndexing = ( ( descriptorIndexingExtension || core12 ) && descriptorIndexingFeature ) ? qtrue : qfalse;
		accelerationStructure = ( accelerationStructure && accelerationStructureFeature ) ? qtrue : qfalse;
		rayQuery = ( rayQuery && rayQueryFeature ) ? qtrue : qfalse;
		rayTracingPipeline = ( rayTracingPipeline && rayTracingPipelineFeature ) ? qtrue : qfalse;
		spirv14 = ( spirv14Extension || core12 ) ? qtrue : qfalse;
		shaderFloatControls = ( shaderFloatControlsExtension || core12 ) ? qtrue : qfalse;
		Com_Memset( &rtPipelineProperties, 0, sizeof( rtPipelineProperties ) );
		Com_Memset( &accelerationStructureProperties, 0, sizeof( accelerationStructureProperties ) );
		if ( ( qvkGetPhysicalDeviceProperties2 || qvkGetPhysicalDeviceProperties2KHR ) &&
			rayTracingPipeline ) {
			VkPhysicalDeviceProperties2 properties2;

			Com_Memset( &properties2, 0, sizeof( properties2 ) );
			properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
			rtPipelineProperties.sType =
				VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
			rtPipelineProperties.pNext = &accelerationStructureProperties;
			accelerationStructureProperties.sType =
				VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
			properties2.pNext = &rtPipelineProperties;
			if ( qvkGetPhysicalDeviceProperties2 ) {
				qvkGetPhysicalDeviceProperties2( physical_device, &properties2 );
			} else {
				qvkGetPhysicalDeviceProperties2KHR( physical_device, &properties2 );
			}
			rtPropertiesAvailable = qtrue;
			rtShaderGroupLayoutValid =
				rtPipelineProperties.shaderGroupHandleSize > 0 &&
				vk_rt_is_power_of_two( rtPipelineProperties.shaderGroupHandleAlignment ) &&
				vk_rt_is_power_of_two( rtPipelineProperties.shaderGroupBaseAlignment ) &&
				vk_rt_align_up_checked( rtPipelineProperties.shaderGroupHandleSize,
					rtPipelineProperties.shaderGroupHandleAlignment, &rtShaderGroupStride ) &&
				rtShaderGroupStride <= rtPipelineProperties.maxShaderGroupStride
					? qtrue : qfalse;
		}
		qvkGetPhysicalDeviceFormatProperties(
			physical_device,
			VK_FORMAT_R16G16B16A16_SFLOAT,
			&rtOutputFormatProperties );
		qvkGetPhysicalDeviceFormatProperties(
			physical_device,
			vk.color_format,
			&rtColorFormatProperties );
		if ( ( rtOutputFormatProperties.optimalTilingFeatures &
				( VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT ) ) ==
			( VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT ) ) {
			if ( vk.color_format == VK_FORMAT_R16G16B16A16_SFLOAT &&
				!vk_rt_dynamic_resolution_enabled() ) {
				rtOutputFormatSupported =
					( rtColorFormatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_DST_BIT ) != 0
						? qtrue : qfalse;
			} else {
				rtOutputFormatSupported =
					( rtOutputFormatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT ) != 0 &&
					( rtOutputFormatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT ) != 0 &&
					( rtColorFormatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT ) != 0
						? qtrue : qfalse;
			}
		}

		device_extension_count = 0;

		if ( !swapchainSupported ) {
			ri.Printf( PRINT_ERROR, "...required device extension is not available: %s\n", VK_KHR_SWAPCHAIN_EXTENSION_NAME );
			return qfalse;
		}

		vk.caps.swapchain = swapchainSupported;
		vk.caps.memoryRequirements2 = memoryRequirements2;
		vk.caps.descriptorIndexing = descriptorIndexing;
		vk.caps.bufferDeviceAddress = devAddrFeat;
		vk.caps.timelineSemaphore = timelineSemaphore;
		vk.caps.vulkanMemoryModel = memoryModel;
		vk.caps.deferredHostOperations = deferredHostOperations;
		vk.caps.accelerationStructure = accelerationStructure;
		vk.caps.rayQuery = rayQuery;
		vk.caps.rayTracingPipeline = rayTracingPipeline;

		if ( !memoryRequirements2 )
			dedicatedAllocation = qfalse;
		else
			vk.dedicatedAllocation = dedicatedAllocation;

#ifndef USE_DEDICATED_ALLOCATION
		vk.dedicatedAllocation = qfalse;
#endif
		vk.caps.dedicatedAllocation = vk.dedicatedAllocation;

		{
			const int requestedRtModeRaw = vk_requested_rt_mode();
			const int requestedRtMode = ( requestedRtModeRaw < RTX_RT_MODE_DISABLED || requestedRtModeRaw > RTX_RT_MODE_RAY_TRACING_PIPELINE )
				? RTX_RT_MODE_DISABLED : requestedRtModeRaw;
			int activeRtMode = RTX_RT_MODE_DISABLED;
			char missingCaps[256];

			missingCaps[0] = '\0';

			if ( requestedRtMode == RTX_RT_MODE_RAY_QUERY || requestedRtMode == RTX_RT_MODE_RAY_TRACING_PIPELINE ) {
				if ( !featureQueryAvailable ) {
					Q_strcat( missingCaps, sizeof( missingCaps ), "vkGetPhysicalDeviceFeatures2" );
				}
				if ( !accelerationStructure ) {
					if ( missingCaps[0] ) Q_strcat( missingCaps, sizeof( missingCaps ), ", " );
					Q_strcat( missingCaps, sizeof( missingCaps ), VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME );
				}
				if ( !descriptorIndexing ) {
					if ( missingCaps[0] ) Q_strcat( missingCaps, sizeof( missingCaps ), ", " );
					Q_strcat( missingCaps, sizeof( missingCaps ), VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME );
				}
				if ( !deferredHostOperations ) {
					if ( missingCaps[0] ) Q_strcat( missingCaps, sizeof( missingCaps ), ", " );
					Q_strcat( missingCaps, sizeof( missingCaps ), VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME );
				}
				if ( !devAddrFeat ) {
					if ( missingCaps[0] ) Q_strcat( missingCaps, sizeof( missingCaps ), ", " );
					Q_strcat( missingCaps, sizeof( missingCaps ), VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME );
				}
				if ( requestedRtMode == RTX_RT_MODE_RAY_QUERY && !rayQuery ) {
					if ( missingCaps[0] ) Q_strcat( missingCaps, sizeof( missingCaps ), ", " );
					Q_strcat( missingCaps, sizeof( missingCaps ), VK_KHR_RAY_QUERY_EXTENSION_NAME );
				}
				if ( requestedRtMode == RTX_RT_MODE_RAY_TRACING_PIPELINE && !rayTracingPipeline ) {
					if ( missingCaps[0] ) Q_strcat( missingCaps, sizeof( missingCaps ), ", " );
					Q_strcat( missingCaps, sizeof( missingCaps ), VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME );
				}
				if ( requestedRtMode == RTX_RT_MODE_RAY_TRACING_PIPELINE && !spirv14 ) {
					if ( missingCaps[0] ) Q_strcat( missingCaps, sizeof( missingCaps ), ", " );
					Q_strcat( missingCaps, sizeof( missingCaps ), VK_KHR_SPIRV_1_4_EXTENSION_NAME );
				}
				if ( requestedRtMode == RTX_RT_MODE_RAY_TRACING_PIPELINE && !shaderFloatControls ) {
					if ( missingCaps[0] ) Q_strcat( missingCaps, sizeof( missingCaps ), ", " );
					Q_strcat( missingCaps, sizeof( missingCaps ), VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME );
				}
				if ( requestedRtMode == RTX_RT_MODE_RAY_TRACING_PIPELINE &&
					physicalDeviceProperties.limits.maxPushConstantsSize < sizeof( rtxRtPushConstants_t ) ) {
					if ( missingCaps[0] ) Q_strcat( missingCaps, sizeof( missingCaps ), ", " );
					Q_strcat( missingCaps, sizeof( missingCaps ), "maxPushConstantsSize" );
				}
				if ( requestedRtMode == RTX_RT_MODE_RAY_TRACING_PIPELINE &&
					( physicalDeviceProperties.limits.maxPerStageDescriptorStorageBuffers < RTX_RT_STORAGE_BUFFER_COUNT ||
						physicalDeviceProperties.limits.maxDescriptorSetStorageBuffers < RTX_RT_STORAGE_BUFFER_COUNT ) ) {
					if ( missingCaps[0] ) Q_strcat( missingCaps, sizeof( missingCaps ), ", " );
					Q_strcat( missingCaps, sizeof( missingCaps ), "storage-buffer descriptor limits" );
				}
				if ( requestedRtMode == RTX_RT_MODE_RAY_TRACING_PIPELINE &&
					( physicalDeviceProperties.limits.maxPerStageDescriptorStorageImages < RTX_RT_STORAGE_IMAGE_COUNT ||
						physicalDeviceProperties.limits.maxDescriptorSetStorageImages < RTX_RT_STORAGE_IMAGE_COUNT ) ) {
					if ( missingCaps[0] ) Q_strcat( missingCaps, sizeof( missingCaps ), ", " );
					Q_strcat( missingCaps, sizeof( missingCaps ), "storage-image descriptor limits" );
				}
				if ( requestedRtMode == RTX_RT_MODE_RAY_TRACING_PIPELINE &&
					( physicalDeviceProperties.limits.maxPerStageDescriptorSampledImages < RTX_RT_SAMPLED_IMAGE_COUNT ||
						physicalDeviceProperties.limits.maxDescriptorSetSampledImages < RTX_RT_SAMPLED_IMAGE_COUNT ||
						physicalDeviceProperties.limits.maxPerStageDescriptorSamplers < RTX_RT_SAMPLED_IMAGE_COUNT ||
						physicalDeviceProperties.limits.maxDescriptorSetSamplers < RTX_RT_SAMPLED_IMAGE_COUNT ) ) {
					if ( missingCaps[0] ) Q_strcat( missingCaps, sizeof( missingCaps ), ", " );
					Q_strcat( missingCaps, sizeof( missingCaps ), "sampled-image descriptor limits" );
				}
				if ( requestedRtMode == RTX_RT_MODE_RAY_TRACING_PIPELINE &&
					physicalDeviceProperties.limits.maxPerStageResources <
						RTX_RT_PER_STAGE_RESOURCE_COUNT ) {
					if ( missingCaps[0] ) Q_strcat( missingCaps, sizeof( missingCaps ), ", " );
					Q_strcat( missingCaps, sizeof( missingCaps ), "maxPerStageResources" );
				}
				if ( requestedRtMode == RTX_RT_MODE_RAY_TRACING_PIPELINE &&
					( !rtPropertiesAvailable ||
						rtPipelineProperties.maxRayRecursionDepth < 2 ||
						!rtShaderGroupLayoutValid ) ) {
					if ( missingCaps[0] ) Q_strcat( missingCaps, sizeof( missingCaps ), ", " );
					Q_strcat( missingCaps, sizeof( missingCaps ), "ray-tracing pipeline limits" );
				}
				if ( requestedRtMode == RTX_RT_MODE_RAY_TRACING_PIPELINE &&
					( !rtPropertiesAvailable ||
						accelerationStructureProperties.maxPerStageDescriptorAccelerationStructures < RTX_RT_ACCELERATION_STRUCTURE_COUNT ||
						accelerationStructureProperties.maxDescriptorSetAccelerationStructures < RTX_RT_ACCELERATION_STRUCTURE_COUNT ) ) {
					if ( missingCaps[0] ) Q_strcat( missingCaps, sizeof( missingCaps ), ", " );
					Q_strcat( missingCaps, sizeof( missingCaps ), "acceleration-structure descriptor limits" );
				}
				if ( requestedRtMode == RTX_RT_MODE_RAY_TRACING_PIPELINE && !rtOutputFormatSupported ) {
					if ( missingCaps[0] ) Q_strcat( missingCaps, sizeof( missingCaps ), ", " );
					Q_strcat( missingCaps, sizeof( missingCaps ), "RT output composition format support" );
				}
			}

			if ( requestedRtMode == RTX_RT_MODE_DISABLED ) {
				activeRtMode = RTX_RT_MODE_DISABLED;
			} else if ( missingCaps[0] == '\0' ) {
				activeRtMode = requestedRtMode;
			} else if ( vk_rt_mode_required() ) {
				ri.Error( ERR_FATAL,
					"Vulkan: requested rtx_rt_mode %s requires unsupported device capabilities: %s\n"
					"Set rtx_rt_mode 0 (fallback mode), or set rtx_rt_require 0 to allow automatic fallback.",
					vk_rt_mode_name( requestedRtMode ), missingCaps );
			} else {
				ri.Printf( PRINT_WARNING,
					"Vulkan: requested rtx_rt_mode %s is unavailable, missing: %s; falling back to disabled mode.\n"
					"Set rtx_rt_require 1 to enforce strict startup failure instead of fallback.\n",
					vk_rt_mode_name( requestedRtMode ), missingCaps );
				activeRtMode = RTX_RT_MODE_DISABLED;
			}

			vk.caps.requestedRtMode = requestedRtMode;
			vk.caps.activeRtMode = activeRtMode;
		}

		device_extension_list[ device_extension_count++ ] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
		if ( portabilitySubset ) {
			device_extension_list[ device_extension_count++ ] = VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME;
		}

		if ( vk.caps.activeRtMode >= RTX_RT_MODE_RAY_QUERY ) {
			device_extension_list[ device_extension_count++ ] = VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME;
			if ( !core12 ) {
				device_extension_list[ device_extension_count++ ] = VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME;
				device_extension_list[ device_extension_count++ ] = VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME;
			}
			enableBufferDeviceAddress = qtrue;
			device_extension_list[ device_extension_count++ ] = VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME;

			if ( vk.caps.activeRtMode == RTX_RT_MODE_RAY_QUERY ) {
				device_extension_list[ device_extension_count++ ] = VK_KHR_RAY_QUERY_EXTENSION_NAME;
			}

			if ( vk.caps.activeRtMode == RTX_RT_MODE_RAY_TRACING_PIPELINE ) {
				device_extension_list[ device_extension_count++ ] = VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME;
				if ( !core12 ) {
					device_extension_list[ device_extension_count++ ] = VK_KHR_SPIRV_1_4_EXTENSION_NAME;
					device_extension_list[ device_extension_count++ ] = VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME;
				}
			}
		}

		if ( vk.dedicatedAllocation ) {
			device_extension_list[ device_extension_count++ ] = VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME;
			device_extension_list[ device_extension_count++ ] = VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME;
		}

		vk.debugMarkers = qfalse;
		if ( debugMarker && markerMode > 0 ) {
			device_extension_list[ device_extension_count++ ] = VK_EXT_DEBUG_MARKER_EXTENSION_NAME;
			vk.debugMarkers = qtrue;
		} else if ( markerMode > 0 && !debugMarker ) {
			ri.Printf( PRINT_WARNING, "Vulkan: GPU debug markers requested, but %s is not available on this device\n", VK_EXT_DEBUG_MARKER_EXTENSION_NAME );
		}
		vk.caps.debugMarkers = vk.debugMarkers;
		if ( hdrMetadata ) {
			device_extension_list[ device_extension_count++ ] = VK_EXT_HDR_METADATA_EXTENSION_NAME;
		}
#ifdef _DEBUG
		if ( timelineSemaphore ) {
			device_extension_list[ device_extension_count++ ] = VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME;
		}

		if ( memoryModel ) {
			device_extension_list[ device_extension_count++ ] = VK_KHR_VULKAN_MEMORY_MODEL_EXTENSION_NAME;
		}

		if ( devAddrFeat && vk.caps.activeRtMode == RTX_RT_MODE_DISABLED ) {
			if ( !core12 ) {
				device_extension_list[ device_extension_count++ ] = VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME;
			}
			enableBufferDeviceAddress = qtrue;
		}

		if ( storage8bit ) {
			device_extension_list[ device_extension_count++ ] = VK_KHR_8BIT_STORAGE_EXTENSION_NAME;
		}
#endif // _DEBUG
		qvkGetPhysicalDeviceFeatures( physical_device, &device_features );

		if ( device_features.fillModeNonSolid == VK_FALSE ) {
			ri.Printf( PRINT_ERROR, "...fillModeNonSolid feature is not supported\n" );
			return qfalse;
		}

		queue_desc.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queue_desc.pNext = NULL;
		queue_desc.flags = 0;
		queue_desc.queueFamilyIndex = vk.queue_family_index;
		queue_desc.queueCount = ( vk_rt_async_overlap_enabled() && vk.queue_family_queue_count > 1 ) ? 2 : 1;
		queue_desc.pQueuePriorities = priorities;

		Com_Memset( &features, 0, sizeof( features ) );
		features.fillModeNonSolid = VK_TRUE;

#ifdef _DEBUG
		if ( device_features.shaderInt64 ) {
			features.shaderInt64 = VK_TRUE;
		}
#endif
		if ( device_features.wideLines ) { // needed for RB_SurfaceAxis
			features.wideLines = VK_TRUE;
			vk.wideLines = qtrue;
		}

		if ( device_features.fragmentStoresAndAtomics && device_features.vertexPipelineStoresAndAtomics ) {
			features.vertexPipelineStoresAndAtomics = VK_TRUE;
			features.fragmentStoresAndAtomics = VK_TRUE;
			vk.fragmentStores = qtrue;
		}

		if ( r_ext_texture_filter_anisotropic->integer && device_features.samplerAnisotropy ) {
			features.samplerAnisotropy = VK_TRUE;
			vk.samplerAnisotropy = qtrue;
		}

		device_desc.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		device_desc.pNext = NULL;
		device_desc.flags = 0;
		device_desc.queueCreateInfoCount = 1;
		device_desc.pQueueCreateInfos = &queue_desc;
		device_desc.enabledLayerCount = 0;
		device_desc.ppEnabledLayerNames = NULL;
		device_desc.enabledExtensionCount = device_extension_count;
		device_desc.ppEnabledExtensionNames = device_extension_list;
		device_desc.pEnabledFeatures = &features;

		pNextPtr = (const void **)&device_desc.pNext;

		if ( enableBufferDeviceAddress ) {
			Com_Memset( &devaddr_features, 0, sizeof( devaddr_features ) );
			*pNextPtr = &devaddr_features;
			devaddr_features.pNext = NULL;
			devaddr_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
			devaddr_features.bufferDeviceAddress = VK_TRUE;
			devaddr_features.bufferDeviceAddressCaptureReplay = VK_FALSE;
			devaddr_features.bufferDeviceAddressMultiDevice = VK_FALSE;
			pNextPtr = (const void **)&devaddr_features.pNext;
		}

		if ( vk.caps.activeRtMode >= RTX_RT_MODE_RAY_QUERY ) {
			Com_Memset( &descriptor_indexing_features, 0, sizeof( descriptor_indexing_features ) );
			*pNextPtr = &descriptor_indexing_features;
			descriptor_indexing_features.pNext = NULL;
			descriptor_indexing_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
			descriptor_indexing_features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
			pNextPtr = (const void **)&descriptor_indexing_features.pNext;

			Com_Memset( &accel_struct_features, 0, sizeof( accel_struct_features ) );
			*pNextPtr = &accel_struct_features;
			accel_struct_features.pNext = NULL;
			accel_struct_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
			accel_struct_features.accelerationStructure = VK_TRUE;
			accel_struct_features.accelerationStructureCaptureReplay = VK_FALSE;
			accel_struct_features.accelerationStructureIndirectBuild = VK_FALSE;
			accel_struct_features.accelerationStructureHostCommands = VK_FALSE;
			accel_struct_features.descriptorBindingAccelerationStructureUpdateAfterBind = VK_FALSE;
			pNextPtr = (const void **)&accel_struct_features.pNext;
		}

		if ( vk.caps.activeRtMode == RTX_RT_MODE_RAY_QUERY ) {
			Com_Memset( &ray_query_features, 0, sizeof( ray_query_features ) );
			*pNextPtr = &ray_query_features;
			ray_query_features.pNext = NULL;
			ray_query_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
			ray_query_features.rayQuery = VK_TRUE;
			pNextPtr = (const void **)&ray_query_features.pNext;
		}

		if ( vk.caps.activeRtMode == RTX_RT_MODE_RAY_TRACING_PIPELINE ) {
			Com_Memset( &rt_pipeline_features, 0, sizeof( rt_pipeline_features ) );
			*pNextPtr = &rt_pipeline_features;
			rt_pipeline_features.pNext = NULL;
			rt_pipeline_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
			rt_pipeline_features.rayTracingPipeline = VK_TRUE;
			rt_pipeline_features.rayTracingPipelineShaderGroupHandleCaptureReplay = VK_FALSE;
			rt_pipeline_features.rayTracingPipelineShaderGroupHandleCaptureReplayMixed = VK_FALSE;
			rt_pipeline_features.rayTracingPipelineTraceRaysIndirect = VK_FALSE;
			rt_pipeline_features.rayTraversalPrimitiveCulling = VK_FALSE;
			pNextPtr = (const void **)&rt_pipeline_features.pNext;
		}

#ifdef _DEBUG
		if ( timelineSemaphore ) {
			*pNextPtr = &timeline_semaphore;
			timeline_semaphore.pNext = NULL;
			timeline_semaphore.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
			timeline_semaphore.timelineSemaphore = VK_TRUE;
			pNextPtr = (const void **)&timeline_semaphore.pNext;
		}

		if ( memoryModel ) {
			*pNextPtr = &memory_model;
			memory_model.pNext = NULL;
			memory_model.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES;
			memory_model.vulkanMemoryModel = VK_TRUE;
			memory_model.vulkanMemoryModelAvailabilityVisibilityChains = VK_FALSE;
			memory_model.vulkanMemoryModelDeviceScope = VK_TRUE;
			pNextPtr = (const void **)&memory_model.pNext;
		}

		if ( storage8bit ) {
			*pNextPtr = &storage_8bit_features;
			storage_8bit_features.pNext = NULL;
			storage_8bit_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES;
			storage_8bit_features.storageBuffer8BitAccess = VK_TRUE;
			storage_8bit_features.storagePushConstant8 = VK_FALSE;
			storage_8bit_features.uniformAndStorageBuffer8BitAccess = VK_TRUE;
			pNextPtr = (const void **)&storage_8bit_features.pNext;
		}
#endif
		res = qvkCreateDevice( physical_device, &device_desc, NULL, &vk.device );
		if ( res < 0 ) {
			ri.Printf( PRINT_ERROR, "vkCreateDevice returned %s\n", vk_result_string( res ) );
			return qfalse;
		}

		vk.hdrMetadata = hdrMetadata;
	}

	if ( vk.debugMarkers ) {
		ri.Printf( PRINT_ALL, "Vulkan: GPU debug markers enabled (rtx_debug_gpu_markers=%d)\n", vk_debug_markers_mode() );
	} else if ( vk_debug_markers_mode() > 0 ) {
		ri.Printf( PRINT_WARNING, "Vulkan: GPU debug markers requested but unavailable on this device\n" );
	}

	vk_report_capabilities( "startup" );

	return qtrue;
}


static void vk_copy_function_pointer( void *destination, size_t destination_size,
	const void *source, size_t source_size, const char *name )
{
	/* ISO C does not define conversions between data and function pointers, or
	 * between differently typed function pointers. Vulkan guarantees that its
	 * loader entry points are callable through the matching PFN type, so copy
	 * the representation after checking the platform uses equal sizes. */
	if ( destination_size != source_size ) {
		ri.Error( ERR_FATAL, "Vulkan entrypoint %s has an incompatible pointer size", name );
		return;
	}

	Com_Memcpy( destination, source, destination_size );
}

static void vk_load_instance_function( void *destination, size_t destination_size, const char *name )
{
	void *address = ri.VK_GetInstanceProcAddr( vk_instance, name );
	vk_copy_function_pointer( destination, destination_size, &address, sizeof( address ), name );
}

static void vk_load_device_function( void *destination, size_t destination_size, const char *name )
{
	PFN_vkVoidFunction address = qvkGetDeviceProcAddr( vk.device, name );
	vk_copy_function_pointer( destination, destination_size, &address, sizeof( address ), name );
}

#define INIT_INSTANCE_FUNCTION(func) \
	vk_load_instance_function( &q##func, sizeof( q##func ), #func ); \
	if (q##func == NULL) {											\
		ri.Error(ERR_FATAL, "Failed to find entrypoint %s", #func);	\
	}

#define INIT_INSTANCE_FUNCTION_EXT(func) \
	vk_load_instance_function( &q##func, sizeof( q##func ), #func );


#define INIT_DEVICE_FUNCTION(func) \
	vk_load_device_function( &q##func, sizeof( q##func ), #func );\
	if (q##func == NULL) {											\
		ri.Error(ERR_FATAL, "Failed to find entrypoint %s", #func);	\
	}

#define INIT_DEVICE_FUNCTION_EXT(func) \
	vk_load_device_function( &q##func, sizeof( q##func ), #func );


static void vk_destroy_instance( void ) {
	if ( vk_surface != VK_NULL_HANDLE ) {
		if ( qvkDestroySurfaceKHR != NULL ) {
			qvkDestroySurfaceKHR( vk_instance, vk_surface, NULL );
		}
		vk_surface = VK_NULL_HANDLE;
	}

#ifdef USE_VK_VALIDATION
	if ( vk_debug_callback ) {
		if ( qvkDestroyDebugReportCallbackEXT != NULL ) {
			qvkDestroyDebugReportCallbackEXT( vk_instance, vk_debug_callback, NULL );
		}
		vk_debug_callback = VK_NULL_HANDLE;
	}
#endif

	if ( vk_instance != VK_NULL_HANDLE ) {
		if ( qvkDestroyInstance ) {
			qvkDestroyInstance( vk_instance, NULL );
		}
		vk_instance = VK_NULL_HANDLE;
	}

	s_vkValidationLayerEnabled = qfalse;
	s_vkInstanceApiVersion = VK_API_VERSION_1_0;
	vk_instance_swapchain_colorspace = qfalse;
}


static void init_vulkan_library( void )
{
	VkPhysicalDeviceProperties props;
	VkPhysicalDevice *physical_devices;
	uint32_t device_count;
	int device_index, i;
	VkResult res;

	Com_Memset( &vk, 0, sizeof( vk ) );

	if ( vk_instance == VK_NULL_HANDLE ) {

		// force cleanup
		vk_destroy_instance();

		// Get functions that do not depend on VkInstance (vk_instance == nullptr at this point).
		INIT_INSTANCE_FUNCTION( vkCreateInstance )
		INIT_INSTANCE_FUNCTION( vkEnumerateInstanceExtensionProperties )
		INIT_INSTANCE_FUNCTION_EXT( vkEnumerateInstanceVersion )

		// Get instance level functions.
		create_instance();

		INIT_INSTANCE_FUNCTION( vkCreateDevice )
		INIT_INSTANCE_FUNCTION( vkDestroyInstance )
		INIT_INSTANCE_FUNCTION( vkEnumerateDeviceExtensionProperties )
		INIT_INSTANCE_FUNCTION( vkEnumeratePhysicalDevices )
		INIT_INSTANCE_FUNCTION( vkGetDeviceProcAddr )
		INIT_INSTANCE_FUNCTION( vkGetPhysicalDeviceFeatures )
		INIT_INSTANCE_FUNCTION_EXT( vkGetPhysicalDeviceFeatures2 )
		INIT_INSTANCE_FUNCTION_EXT( vkGetPhysicalDeviceFeatures2KHR )
		INIT_INSTANCE_FUNCTION( vkGetPhysicalDeviceFormatProperties )
		INIT_INSTANCE_FUNCTION( vkGetPhysicalDeviceMemoryProperties )
		INIT_INSTANCE_FUNCTION( vkGetPhysicalDeviceProperties )
		INIT_INSTANCE_FUNCTION_EXT( vkGetPhysicalDeviceProperties2 )
		INIT_INSTANCE_FUNCTION_EXT( vkGetPhysicalDeviceProperties2KHR )
		INIT_INSTANCE_FUNCTION( vkGetPhysicalDeviceQueueFamilyProperties )
		INIT_INSTANCE_FUNCTION( vkDestroySurfaceKHR )
		INIT_INSTANCE_FUNCTION( vkGetPhysicalDeviceSurfaceCapabilitiesKHR )
		INIT_INSTANCE_FUNCTION( vkGetPhysicalDeviceSurfaceFormatsKHR )
		INIT_INSTANCE_FUNCTION( vkGetPhysicalDeviceSurfacePresentModesKHR )
		INIT_INSTANCE_FUNCTION( vkGetPhysicalDeviceSurfaceSupportKHR )

#ifdef USE_VK_VALIDATION
		INIT_INSTANCE_FUNCTION_EXT( vkCreateDebugReportCallbackEXT )
		INIT_INSTANCE_FUNCTION_EXT( vkDestroyDebugReportCallbackEXT )

		// Create debug callback when validation layers are active.
		if ( s_vkValidationLayerEnabled && qvkCreateDebugReportCallbackEXT && qvkDestroyDebugReportCallbackEXT ) {
			VkDebugReportCallbackCreateInfoEXT desc;
			desc.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
			desc.pNext = NULL;
			desc.flags = VK_DEBUG_REPORT_WARNING_BIT_EXT |
				VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT |
				VK_DEBUG_REPORT_ERROR_BIT_EXT;
			desc.pfnCallback = &debug_callback;
			desc.pUserData = NULL;

			VK_CHECK( qvkCreateDebugReportCallbackEXT( vk_instance, &desc, NULL, &vk_debug_callback ) );
		} else if ( s_vkValidationLayerEnabled ) {
			ri.Printf( PRINT_WARNING, "Vulkan: validation layer is enabled, but debug report callback extension is unavailable\n" );
		}
#endif

		// create surface
		if ( !ri.VK_CreateSurface( vk_instance, &vk_surface ) ) {
			ri.Error( ERR_FATAL, "Error creating Vulkan surface" );
			return;
		}
	} // vk_instance == VK_NULL_HANDLE

	vk.swapchainColorspace = vk_instance_swapchain_colorspace;

	res = qvkEnumeratePhysicalDevices( vk_instance, &device_count, NULL );
	if ( device_count == 0 ) {
		ri.Error( ERR_FATAL, "Vulkan: no physical devices found" );
		return;
	}
	else if ( res < 0 ) {
		ri.Error( ERR_FATAL, "vkEnumeratePhysicalDevices returned %s", vk_result_string( res ) );
		return;
	}

	physical_devices = (VkPhysicalDevice*)ri.Malloc( device_count * sizeof( VkPhysicalDevice ) );
	VK_CHECK( qvkEnumeratePhysicalDevices( vk_instance, &device_count, physical_devices ) );

	// initial physical device index
	device_index = r_device->integer;

	ri.Printf( PRINT_ALL, ".......................\nAvailable physical devices:\n" );
	for ( i = 0; i < device_count; i++ ) {
		qvkGetPhysicalDeviceProperties( physical_devices[ i ], &props );
		ri.Printf( PRINT_ALL, " %i: %s\n", i, renderer_name( &props ) );
		if ( device_index == -1 && props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ) {
			device_index = i;
		} else if ( device_index == -2 && props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ) {
			device_index = i;
		}
	}
	ri.Printf( PRINT_ALL, ".......................\n" );

	vk.physical_device = VK_NULL_HANDLE;
	for ( i = 0; i < device_count; i++, device_index++ ) {
		if ( device_index >= device_count || device_index < 0 ) {
			device_index = 0;
		}
		if ( vk_create_device( physical_devices[ device_index ], device_index ) ) {
			vk.physical_device = physical_devices[ device_index ];
			break;
		}
	}

	ri.Free( physical_devices );

	if ( vk.physical_device == VK_NULL_HANDLE ) {
		ri.Error( ERR_FATAL, "Vulkan: unable to find any suitable physical device" );
		return;
	}

	//
	// Get device level functions.
	//
	INIT_DEVICE_FUNCTION(vkAllocateCommandBuffers)
	INIT_DEVICE_FUNCTION(vkAllocateDescriptorSets)
	INIT_DEVICE_FUNCTION(vkAllocateMemory)
	INIT_DEVICE_FUNCTION(vkBeginCommandBuffer)
	INIT_DEVICE_FUNCTION(vkBindBufferMemory)
	INIT_DEVICE_FUNCTION(vkBindImageMemory)
	INIT_DEVICE_FUNCTION(vkCmdBeginRenderPass)
	INIT_DEVICE_FUNCTION(vkCmdBindDescriptorSets)
	INIT_DEVICE_FUNCTION(vkCmdBindIndexBuffer)
	INIT_DEVICE_FUNCTION(vkCmdBindPipeline)
	INIT_DEVICE_FUNCTION(vkCmdBindVertexBuffers)
	INIT_DEVICE_FUNCTION(vkCmdBlitImage)
	INIT_DEVICE_FUNCTION(vkCmdClearAttachments)
	INIT_DEVICE_FUNCTION(vkCmdCopyBuffer)
	INIT_DEVICE_FUNCTION(vkCmdCopyBufferToImage)
	INIT_DEVICE_FUNCTION(vkCmdCopyImage)
	INIT_DEVICE_FUNCTION(vkCmdDraw)
	INIT_DEVICE_FUNCTION(vkCmdDrawIndexed)
	INIT_DEVICE_FUNCTION(vkCmdEndRenderPass)
	INIT_DEVICE_FUNCTION(vkCmdNextSubpass)
	INIT_DEVICE_FUNCTION(vkCmdPipelineBarrier)
	INIT_DEVICE_FUNCTION(vkCmdPushConstants)
	INIT_DEVICE_FUNCTION(vkCmdResetQueryPool)
	INIT_DEVICE_FUNCTION(vkCmdSetDepthBias)
	INIT_DEVICE_FUNCTION(vkCmdSetScissor)
	INIT_DEVICE_FUNCTION(vkCmdSetViewport)
	INIT_DEVICE_FUNCTION(vkCmdWriteTimestamp)
	INIT_DEVICE_FUNCTION(vkCreateBuffer)
	INIT_DEVICE_FUNCTION(vkCreateCommandPool)
	INIT_DEVICE_FUNCTION(vkCreateDescriptorPool)
	INIT_DEVICE_FUNCTION(vkCreateDescriptorSetLayout)
	INIT_DEVICE_FUNCTION(vkCreateFence)
	INIT_DEVICE_FUNCTION(vkCreateFramebuffer)
	INIT_DEVICE_FUNCTION(vkCreateGraphicsPipelines)
	INIT_DEVICE_FUNCTION(vkCreateImage)
	INIT_DEVICE_FUNCTION(vkCreateImageView)
	INIT_DEVICE_FUNCTION(vkCreatePipelineCache)
	INIT_DEVICE_FUNCTION(vkCreatePipelineLayout)
	INIT_DEVICE_FUNCTION(vkCreateRenderPass)
	INIT_DEVICE_FUNCTION(vkCreateSampler)
	INIT_DEVICE_FUNCTION(vkCreateSemaphore)
	INIT_DEVICE_FUNCTION(vkCreateShaderModule)
	INIT_DEVICE_FUNCTION(vkDestroyBuffer)
	INIT_DEVICE_FUNCTION(vkDestroyCommandPool)
	INIT_DEVICE_FUNCTION(vkDestroyDescriptorPool)
	INIT_DEVICE_FUNCTION(vkDestroyDescriptorSetLayout)
	INIT_DEVICE_FUNCTION(vkDestroyDevice)
	INIT_DEVICE_FUNCTION(vkDestroyFence)
	INIT_DEVICE_FUNCTION(vkDestroyFramebuffer)
	INIT_DEVICE_FUNCTION(vkDestroyImage)
	INIT_DEVICE_FUNCTION(vkDestroyImageView)
	INIT_DEVICE_FUNCTION(vkDestroyPipeline)
	INIT_DEVICE_FUNCTION(vkDestroyPipelineCache)
	INIT_DEVICE_FUNCTION(vkDestroyPipelineLayout)
	INIT_DEVICE_FUNCTION(vkDestroyRenderPass)
	INIT_DEVICE_FUNCTION(vkDestroySampler)
	INIT_DEVICE_FUNCTION(vkDestroySemaphore)
	INIT_DEVICE_FUNCTION(vkDestroyShaderModule)
	INIT_DEVICE_FUNCTION(vkDeviceWaitIdle)
	INIT_DEVICE_FUNCTION(vkEndCommandBuffer)
	INIT_DEVICE_FUNCTION(vkFlushMappedMemoryRanges)
	INIT_DEVICE_FUNCTION(vkFreeCommandBuffers)
	INIT_DEVICE_FUNCTION(vkFreeDescriptorSets)
	INIT_DEVICE_FUNCTION(vkFreeMemory)
	INIT_DEVICE_FUNCTION(vkGetBufferMemoryRequirements)
	INIT_DEVICE_FUNCTION(vkGetDeviceQueue)
	INIT_DEVICE_FUNCTION(vkGetImageMemoryRequirements)
	INIT_DEVICE_FUNCTION(vkGetImageSubresourceLayout)
	INIT_DEVICE_FUNCTION(vkInvalidateMappedMemoryRanges)
	INIT_DEVICE_FUNCTION(vkMapMemory)
	INIT_DEVICE_FUNCTION(vkQueueSubmit)
	INIT_DEVICE_FUNCTION(vkQueueWaitIdle)
	INIT_DEVICE_FUNCTION(vkResetCommandBuffer)
	INIT_DEVICE_FUNCTION(vkResetDescriptorPool)
	INIT_DEVICE_FUNCTION(vkResetFences)
	INIT_DEVICE_FUNCTION(vkUnmapMemory)
	INIT_DEVICE_FUNCTION(vkUpdateDescriptorSets)
	INIT_DEVICE_FUNCTION(vkWaitForFences)
	INIT_DEVICE_FUNCTION(vkAcquireNextImageKHR)
	INIT_DEVICE_FUNCTION(vkCreateSwapchainKHR)
	INIT_DEVICE_FUNCTION(vkDestroySwapchainKHR)
	INIT_DEVICE_FUNCTION(vkGetSwapchainImagesKHR)
	INIT_DEVICE_FUNCTION(vkQueuePresentKHR)
	INIT_DEVICE_FUNCTION(vkCreateQueryPool)
	INIT_DEVICE_FUNCTION(vkDestroyQueryPool)
	INIT_DEVICE_FUNCTION(vkGetQueryPoolResults)

	if ( vk.dedicatedAllocation ) {
		INIT_DEVICE_FUNCTION_EXT(vkGetBufferMemoryRequirements2KHR);
		INIT_DEVICE_FUNCTION_EXT(vkGetImageMemoryRequirements2KHR);
		if ( !qvkGetBufferMemoryRequirements2KHR || !qvkGetImageMemoryRequirements2KHR ) {
			vk.dedicatedAllocation = qfalse;
		}
	}

	if ( vk.hdrMetadata ) {
		INIT_DEVICE_FUNCTION_EXT(vkSetHdrMetadataEXT)
		if ( !qvkSetHdrMetadataEXT ) {
			vk.hdrMetadata = qfalse;
		}
	}

	if ( vk_rt_pipeline_enabled() ) {
		INIT_DEVICE_FUNCTION_EXT(vkGetBufferDeviceAddress)
		INIT_DEVICE_FUNCTION_EXT(vkGetBufferDeviceAddressKHR)
		INIT_DEVICE_FUNCTION_EXT(vkCreateAccelerationStructureKHR)
		INIT_DEVICE_FUNCTION_EXT(vkDestroyAccelerationStructureKHR)
		INIT_DEVICE_FUNCTION_EXT(vkGetAccelerationStructureBuildSizesKHR)
		INIT_DEVICE_FUNCTION_EXT(vkCmdBuildAccelerationStructuresKHR)
		INIT_DEVICE_FUNCTION_EXT(vkGetAccelerationStructureDeviceAddressKHR)
		INIT_DEVICE_FUNCTION_EXT(vkCreateRayTracingPipelinesKHR)
		INIT_DEVICE_FUNCTION_EXT(vkGetRayTracingShaderGroupHandlesKHR)
		INIT_DEVICE_FUNCTION_EXT(vkCmdTraceRaysKHR)
		INIT_DEVICE_FUNCTION_EXT(vkCmdCopyAccelerationStructureKHR)
		INIT_DEVICE_FUNCTION_EXT(vkCmdWriteAccelerationStructuresPropertiesKHR)

		if ( ( !qvkGetBufferDeviceAddress && !qvkGetBufferDeviceAddressKHR ) ||
			!qvkCreateAccelerationStructureKHR ||
			!qvkDestroyAccelerationStructureKHR ||
			!qvkGetAccelerationStructureBuildSizesKHR ||
			!qvkCmdBuildAccelerationStructuresKHR ||
			!qvkGetAccelerationStructureDeviceAddressKHR ||
			!qvkCreateRayTracingPipelinesKHR ||
			!qvkGetRayTracingShaderGroupHandlesKHR ||
			!qvkCmdTraceRaysKHR ||
			!qvkCmdCopyAccelerationStructureKHR ||
			!qvkCmdWriteAccelerationStructuresPropertiesKHR ) {
			ri.Error( ERR_FATAL, "Vulkan: ray tracing mode requested, but one or more required RT device entry points are unavailable" );
		}
	}

	if ( vk.debugMarkers ) {
		INIT_DEVICE_FUNCTION_EXT(vkDebugMarkerSetObjectNameEXT)
		INIT_DEVICE_FUNCTION_EXT(vkCmdDebugMarkerBeginEXT)
		INIT_DEVICE_FUNCTION_EXT(vkCmdDebugMarkerEndEXT)
		INIT_DEVICE_FUNCTION_EXT(vkCmdDebugMarkerInsertEXT)
		if ( vk_debug_markers_mode() >= 2 && ( !qvkCmdDebugMarkerBeginEXT || !qvkCmdDebugMarkerEndEXT ) ) {
			ri.Printf( PRINT_WARNING, "Vulkan: pass markers requested, but debug marker command entry points are unavailable\n" );
		}
	}
}

#undef INIT_INSTANCE_FUNCTION
#undef INIT_DEVICE_FUNCTION
#undef INIT_DEVICE_FUNCTION_EXT

static void deinit_instance_functions( void )
{
	qvkCreateInstance = NULL;
	qvkEnumerateInstanceExtensionProperties = NULL;
	qvkEnumerateInstanceVersion = NULL;

	// instance functions:
	qvkCreateDevice = NULL;
	qvkDestroyInstance = NULL;
	qvkEnumerateDeviceExtensionProperties = NULL;
	qvkEnumeratePhysicalDevices = NULL;
	qvkGetDeviceProcAddr = NULL;
	qvkGetPhysicalDeviceFeatures = NULL;
	qvkGetPhysicalDeviceFeatures2 = NULL;
	qvkGetPhysicalDeviceFeatures2KHR = NULL;
	qvkGetPhysicalDeviceFormatProperties = NULL;
	qvkGetPhysicalDeviceMemoryProperties = NULL;
	qvkGetPhysicalDeviceProperties = NULL;
	qvkGetPhysicalDeviceProperties2 = NULL;
	qvkGetPhysicalDeviceProperties2KHR = NULL;
	qvkGetPhysicalDeviceQueueFamilyProperties = NULL;
	qvkDestroySurfaceKHR = NULL;
	qvkGetPhysicalDeviceSurfaceCapabilitiesKHR = NULL;
	qvkGetPhysicalDeviceSurfaceFormatsKHR = NULL;
	qvkGetPhysicalDeviceSurfacePresentModesKHR = NULL;
	qvkGetPhysicalDeviceSurfaceSupportKHR = NULL;
#ifdef USE_VK_VALIDATION
	qvkCreateDebugReportCallbackEXT = NULL;
	qvkDestroyDebugReportCallbackEXT = NULL;
#endif
}


static void deinit_device_functions( void )
{
	// device functions:
	qvkAllocateCommandBuffers					= NULL;
	qvkAllocateDescriptorSets					= NULL;
	qvkAllocateMemory							= NULL;
	qvkBeginCommandBuffer						= NULL;
	qvkBindBufferMemory							= NULL;
	qvkBindImageMemory							= NULL;
	qvkCmdBeginRenderPass						= NULL;
	qvkCmdBindDescriptorSets					= NULL;
	qvkCmdBindIndexBuffer						= NULL;
	qvkCmdBindPipeline							= NULL;
	qvkCmdBindVertexBuffers						= NULL;
	qvkCmdBlitImage								= NULL;
	qvkCmdClearAttachments						= NULL;
	qvkCmdCopyBuffer							= NULL;
	qvkCmdCopyBufferToImage						= NULL;
	qvkCmdCopyImage								= NULL;
	qvkCmdDraw									= NULL;
	qvkCmdDrawIndexed							= NULL;
	qvkCmdEndRenderPass							= NULL;
	qvkCmdNextSubpass							= NULL;
	qvkCmdPipelineBarrier						= NULL;
	qvkCmdPushConstants							= NULL;
	qvkCmdResetQueryPool						= NULL;
	qvkCmdSetDepthBias							= NULL;
	qvkCmdSetScissor							= NULL;
	qvkCmdSetViewport							= NULL;
	qvkCmdWriteTimestamp						= NULL;
	qvkCreateBuffer								= NULL;
	qvkCreateCommandPool						= NULL;
	qvkCreateDescriptorPool						= NULL;
	qvkCreateDescriptorSetLayout				= NULL;
	qvkCreateFence								= NULL;
	qvkCreateFramebuffer						= NULL;
	qvkCreateGraphicsPipelines					= NULL;
	qvkCreateImage								= NULL;
	qvkCreateImageView							= NULL;
	qvkCreatePipelineCache						= NULL;
	qvkCreatePipelineLayout						= NULL;
	qvkCreateRenderPass							= NULL;
	qvkCreateSampler							= NULL;
	qvkCreateSemaphore							= NULL;
	qvkCreateShaderModule						= NULL;
	qvkDestroyBuffer							= NULL;
	qvkDestroyCommandPool						= NULL;
	qvkDestroyDescriptorPool					= NULL;
	qvkDestroyDescriptorSetLayout				= NULL;
	qvkDestroyDevice							= NULL;
	qvkDestroyFence								= NULL;
	qvkDestroyFramebuffer						= NULL;
	qvkDestroyImage								= NULL;
	qvkDestroyImageView							= NULL;
	qvkDestroyPipeline							= NULL;
	qvkDestroyPipelineCache						= NULL;
	qvkDestroyPipelineLayout					= NULL;
	qvkDestroyRenderPass						= NULL;
	qvkDestroySampler							= NULL;
	qvkDestroySemaphore							= NULL;
	qvkDestroyShaderModule						= NULL;
	qvkDeviceWaitIdle							= NULL;
	qvkEndCommandBuffer							= NULL;
	qvkFlushMappedMemoryRanges					= NULL;
	qvkFreeCommandBuffers						= NULL;
	qvkFreeDescriptorSets						= NULL;
	qvkFreeMemory								= NULL;
	qvkGetBufferMemoryRequirements				= NULL;
	qvkGetDeviceQueue							= NULL;
	qvkGetImageMemoryRequirements				= NULL;
	qvkGetImageSubresourceLayout				= NULL;
	qvkInvalidateMappedMemoryRanges				= NULL;
	qvkMapMemory								= NULL;
	qvkQueueSubmit								= NULL;
	qvkQueueWaitIdle							= NULL;
	qvkResetCommandBuffer						= NULL;
	qvkResetDescriptorPool						= NULL;
	qvkResetFences								= NULL;
	qvkUnmapMemory								= NULL;
	qvkUpdateDescriptorSets						= NULL;
	qvkWaitForFences							= NULL;
	qvkAcquireNextImageKHR						= NULL;
	qvkCreateSwapchainKHR						= NULL;
	qvkDestroySwapchainKHR						= NULL;
	qvkGetSwapchainImagesKHR					= NULL;
	qvkQueuePresentKHR							= NULL;
	qvkCreateQueryPool							= NULL;
	qvkDestroyQueryPool							= NULL;
	qvkGetQueryPoolResults						= NULL;
	qvkSetHdrMetadataEXT							= NULL;

	qvkGetBufferMemoryRequirements2KHR			= NULL;
	qvkGetImageMemoryRequirements2KHR			= NULL;
	qvkGetBufferDeviceAddress						= NULL;
	qvkGetBufferDeviceAddressKHR				= NULL;
	qvkCreateAccelerationStructureKHR			= NULL;
	qvkDestroyAccelerationStructureKHR			= NULL;
	qvkGetAccelerationStructureBuildSizesKHR	= NULL;
	qvkCmdBuildAccelerationStructuresKHR		= NULL;
	qvkGetAccelerationStructureDeviceAddressKHR	= NULL;
	qvkCreateRayTracingPipelinesKHR				= NULL;
	qvkGetRayTracingShaderGroupHandlesKHR		= NULL;
	qvkCmdTraceRaysKHR							= NULL;
	qvkCmdCopyAccelerationStructureKHR			= NULL;
	qvkCmdWriteAccelerationStructuresPropertiesKHR = NULL;

	qvkDebugMarkerSetObjectNameEXT				= NULL;
	qvkCmdDebugMarkerBeginEXT					= NULL;
	qvkCmdDebugMarkerEndEXT						= NULL;
	qvkCmdDebugMarkerInsertEXT					= NULL;
}


static VkShaderModule SHADER_MODULE(const uint8_t *bytes, const int count) {
	VkShaderModuleCreateInfo desc;
	VkShaderModule module;

	if ( count % 4 != 0 ) {
		ri.Error( ERR_FATAL, "Vulkan: SPIR-V binary buffer size is not a multiple of 4" );
	}

	desc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.codeSize = count;
	desc.pCode = (const uint32_t*)bytes;

	VK_CHECK(qvkCreateShaderModule(vk.device, &desc, NULL, &module));

	return module;
}


static void vk_create_layout_binding( int binding, VkDescriptorType type, VkShaderStageFlags flags, VkDescriptorSetLayout *layout )
{
	VkDescriptorSetLayoutBinding bind;
	VkDescriptorSetLayoutCreateInfo desc;

	bind.binding = binding;
	bind.descriptorType = type;
	bind.descriptorCount = 1;
	bind.stageFlags = flags;
	bind.pImmutableSamplers = NULL;

	desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.bindingCount = 1;
	desc.pBindings = &bind;

	VK_CHECK( qvkCreateDescriptorSetLayout(vk.device, &desc, NULL, layout ) );
}

typedef struct {
	rtxVkDescriptorLayoutId_t id;
	int binding;
	VkDescriptorType type;
	VkShaderStageFlags stageFlags;
	const char *debugName;
} rtxVkDescriptorLayoutSpec_t;

static const rtxVkDescriptorLayoutSpec_t s_vkDescriptorLayoutSpecs[] = {
	{ RTX_VK_DESC_LAYOUT_SAMPLER, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, "descriptor layout - sampler" },
	{ RTX_VK_DESC_LAYOUT_UNIFORM, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT, "descriptor layout - uniform" },
	{ RTX_VK_DESC_LAYOUT_STORAGE, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT, "descriptor layout - storage" }
};

static VkDescriptorSetLayout *vk_descriptor_layout_ref( rtxVkDescriptorLayoutId_t id )
{
	switch ( id ) {
	case RTX_VK_DESC_LAYOUT_SAMPLER:
		return &vk.set_layout_sampler;
	case RTX_VK_DESC_LAYOUT_UNIFORM:
		return &vk.set_layout_uniform;
	case RTX_VK_DESC_LAYOUT_STORAGE:
		return &vk.set_layout_storage;
	default:
		ri.Error( ERR_FATAL, "Vulkan: invalid descriptor layout id %i", (int)id );
		return &vk.set_layout_sampler;
	}
}

static VkPipelineLayout *vk_pipeline_layout_ref( rtxVkPipelineLayoutId_t id )
{
	switch ( id ) {
	case RTX_VK_PIPELINE_LAYOUT_MAIN:
		return &vk.pipeline_layout;
	case RTX_VK_PIPELINE_LAYOUT_STORAGE:
		return &vk.pipeline_layout_storage;
	case RTX_VK_PIPELINE_LAYOUT_POST_PROCESS:
		return &vk.pipeline_layout_post_process;
	case RTX_VK_PIPELINE_LAYOUT_BLEND:
		return &vk.pipeline_layout_blend;
	default:
		ri.Error( ERR_FATAL, "Vulkan: invalid pipeline layout id %i", (int)id );
		return &vk.pipeline_layout;
	}
}

static VkPipelineLayout vk_pipeline_layout_handle( rtxVkPipelineLayoutId_t id )
{
	return *vk_pipeline_layout_ref( id );
}

static void vk_create_descriptor_layout_registry( void )
{
	uint32_t i;

	for ( i = 0; i < ARRAY_LEN( s_vkDescriptorLayoutSpecs ); i++ ) {
		const rtxVkDescriptorLayoutSpec_t *spec = &s_vkDescriptorLayoutSpecs[i];
		VkDescriptorSetLayout *layout = vk_descriptor_layout_ref( spec->id );

		vk_create_layout_binding( spec->binding, spec->type, spec->stageFlags, layout );
		SET_OBJECT_NAME( *layout, spec->debugName, VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT_EXT );
	}
}

static void vk_destroy_descriptor_layout_registry( void )
{
	uint32_t i;

	for ( i = 0; i < ARRAY_LEN( s_vkDescriptorLayoutSpecs ); i++ ) {
		const rtxVkDescriptorLayoutSpec_t *spec = &s_vkDescriptorLayoutSpecs[i];
		VkDescriptorSetLayout *layout = vk_descriptor_layout_ref( spec->id );

		if ( *layout != VK_NULL_HANDLE ) {
			qvkDestroyDescriptorSetLayout( vk.device, *layout, NULL );
			*layout = VK_NULL_HANDLE;
		}
	}
}

static void vk_create_pipeline_layout_from_registry(
	rtxVkPipelineLayoutId_t pipelineLayoutId,
	const rtxVkDescriptorLayoutId_t *descriptorLayouts,
	uint32_t descriptorLayoutCount,
	VkShaderStageFlags pushConstantStageFlags,
	uint32_t pushConstantSize,
	const char *debugName )
{
	VkDescriptorSetLayout setLayouts[8];
	VkPipelineLayoutCreateInfo desc;
	VkPushConstantRange pushRange;
	VkPipelineLayout *layout;
	uint32_t i;

	if ( descriptorLayoutCount > ARRAY_LEN( setLayouts ) ) {
		ri.Error( ERR_FATAL, "Vulkan: descriptor layout count %u exceeds registry limit", descriptorLayoutCount );
	}

	layout = vk_pipeline_layout_ref( pipelineLayoutId );

	for ( i = 0; i < descriptorLayoutCount; i++ ) {
		setLayouts[i] = *vk_descriptor_layout_ref( descriptorLayouts[i] );
	}

	desc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.setLayoutCount = descriptorLayoutCount;
	desc.pSetLayouts = setLayouts;

	if ( pushConstantSize > 0 ) {
		pushRange.stageFlags = pushConstantStageFlags;
		pushRange.offset = 0;
		pushRange.size = pushConstantSize;
		desc.pushConstantRangeCount = 1;
		desc.pPushConstantRanges = &pushRange;
	} else {
		desc.pushConstantRangeCount = 0;
		desc.pPushConstantRanges = NULL;
	}

	VK_CHECK( qvkCreatePipelineLayout( vk.device, &desc, NULL, layout ) );
	SET_OBJECT_NAME( *layout, debugName, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_LAYOUT_EXT );
}

static void vk_create_pipeline_layout_registry( void )
{
	const rtxVkDescriptorLayoutId_t mainDescriptorLayouts[] = {
		RTX_VK_DESC_LAYOUT_UNIFORM,
		RTX_VK_DESC_LAYOUT_SAMPLER,
		RTX_VK_DESC_LAYOUT_SAMPLER,
		RTX_VK_DESC_LAYOUT_SAMPLER,
		RTX_VK_DESC_LAYOUT_SAMPLER
	};
	const rtxVkDescriptorLayoutId_t storageDescriptorLayouts[] = {
		RTX_VK_DESC_LAYOUT_STORAGE
	};
	const rtxVkDescriptorLayoutId_t postProcessDescriptorLayouts[] = {
		RTX_VK_DESC_LAYOUT_SAMPLER,
		RTX_VK_DESC_LAYOUT_SAMPLER
	};
	rtxVkDescriptorLayoutId_t blendDescriptorLayouts[ VK_NUM_BLOOM_PASSES ];
	uint32_t mainDescriptorLayoutCount;
	uint32_t i;

	mainDescriptorLayoutCount = ( vk.maxBoundDescriptorSets >= VK_DESC_COUNT ) ? VK_DESC_COUNT : 4;

	for ( i = 0; i < ARRAY_LEN( blendDescriptorLayouts ); i++ ) {
		blendDescriptorLayouts[i] = RTX_VK_DESC_LAYOUT_SAMPLER;
	}

	vk_create_pipeline_layout_from_registry(
		RTX_VK_PIPELINE_LAYOUT_MAIN,
		mainDescriptorLayouts,
		mainDescriptorLayoutCount,
		VK_SHADER_STAGE_VERTEX_BIT,
		64,
		"pipeline layout - main" );

	vk_create_pipeline_layout_from_registry(
		RTX_VK_PIPELINE_LAYOUT_STORAGE,
		storageDescriptorLayouts,
		ARRAY_LEN( storageDescriptorLayouts ),
		VK_SHADER_STAGE_VERTEX_BIT,
		64,
		"pipeline layout - storage" );

	vk_create_pipeline_layout_from_registry(
		RTX_VK_PIPELINE_LAYOUT_POST_PROCESS,
		postProcessDescriptorLayouts,
		ARRAY_LEN( postProcessDescriptorLayouts ),
		VK_SHADER_STAGE_FRAGMENT_BIT,
		12 * sizeof( float ),
		"pipeline layout - post-processing" );

	vk_create_pipeline_layout_from_registry(
		RTX_VK_PIPELINE_LAYOUT_BLEND,
		blendDescriptorLayouts,
		ARRAY_LEN( blendDescriptorLayouts ),
		0,
		0,
		"pipeline layout - blend" );
}

static void vk_destroy_pipeline_layout_registry( void )
{
	uint32_t i;

	for ( i = 0; i < RTX_VK_PIPELINE_LAYOUT_COUNT; i++ ) {
		VkPipelineLayout *layout = vk_pipeline_layout_ref( (rtxVkPipelineLayoutId_t)i );

		if ( *layout != VK_NULL_HANDLE ) {
			qvkDestroyPipelineLayout( vk.device, *layout, NULL );
			*layout = VK_NULL_HANDLE;
		}
	}
}


void vk_update_uniform_descriptor( VkDescriptorSet descriptor, VkBuffer buffer )
{
	VkDescriptorBufferInfo info;
	VkWriteDescriptorSet desc;

	info.buffer = buffer;
	info.offset = 0;
	info.range = sizeof( vkUniform_t );

	desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	desc.dstSet = descriptor;
	desc.dstBinding = 0;
	desc.dstArrayElement = 0;
	desc.descriptorCount = 1;
	desc.pNext = NULL;
	desc.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	desc.pImageInfo = NULL;
	desc.pBufferInfo = &info;
	desc.pTexelBufferView = NULL;

	qvkUpdateDescriptorSets( vk.device, 1, &desc, 0, NULL );
}


static VkSampler vk_find_sampler( const Vk_Sampler_Def *def ) {
	VkSamplerAddressMode address_mode;
	VkSamplerCreateInfo desc;
	VkSampler sampler;
	VkFilter mag_filter;
	VkFilter min_filter;
	VkSamplerMipmapMode mipmap_mode;
	float maxLod;
	int i;

	// Look for sampler among existing samplers.
	for ( i = 0; i < vk_world.num_samplers; i++ ) {
		const Vk_Sampler_Def *cur_def = &vk_world.sampler_defs[i];
		if ( memcmp( cur_def, def, sizeof( *def ) ) == 0 ) {
			return vk_world.samplers[i];
		}
	}

	// Create new sampler.
	if ( vk_world.num_samplers >= MAX_VK_SAMPLERS ) {
		ri.Error( ERR_DROP, "vk_find_sampler: MAX_VK_SAMPLERS hit\n" );
	}

	address_mode = def->address_mode;

	if (def->gl_mag_filter == GL_NEAREST) {
		mag_filter = VK_FILTER_NEAREST;
	} else if (def->gl_mag_filter == GL_LINEAR) {
		mag_filter = VK_FILTER_LINEAR;
	} else {
		ri.Error(ERR_FATAL, "vk_find_sampler: invalid gl_mag_filter");
		return VK_NULL_HANDLE;
	}

	maxLod = vk.maxLod;

	if (def->gl_min_filter == GL_NEAREST) {
		min_filter = VK_FILTER_NEAREST;
		mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		maxLod = 0.25f; // used to emulate OpenGL's GL_LINEAR/GL_NEAREST minification filter
	} else if (def->gl_min_filter == GL_LINEAR) {
		min_filter = VK_FILTER_LINEAR;
		mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		maxLod = 0.25f; // used to emulate OpenGL's GL_LINEAR/GL_NEAREST minification filter
	} else if (def->gl_min_filter == GL_NEAREST_MIPMAP_NEAREST) {
		min_filter = VK_FILTER_NEAREST;
		mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	} else if (def->gl_min_filter == GL_LINEAR_MIPMAP_NEAREST) {
		min_filter = VK_FILTER_LINEAR;
		mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	} else if (def->gl_min_filter == GL_NEAREST_MIPMAP_LINEAR) {
		min_filter = VK_FILTER_NEAREST;
		mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	} else if (def->gl_min_filter == GL_LINEAR_MIPMAP_LINEAR) {
		min_filter = VK_FILTER_LINEAR;
		mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	} else {
		ri.Error(ERR_FATAL, "vk_find_sampler: invalid gl_min_filter");
		return VK_NULL_HANDLE;
	}

	if ( def->max_lod_1_0 ) {
		maxLod = 1.0f;
	}

	desc.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.magFilter = mag_filter;
	desc.minFilter = min_filter;
	desc.mipmapMode = mipmap_mode;
	desc.addressModeU = address_mode;
	desc.addressModeV = address_mode;
	desc.addressModeW = address_mode;
	desc.mipLodBias = 0.0f;

	if ( def->noAnisotropy || mipmap_mode == VK_SAMPLER_MIPMAP_MODE_NEAREST || mag_filter == VK_FILTER_NEAREST ) {
		desc.anisotropyEnable = VK_FALSE;
		desc.maxAnisotropy = 1.0f;
	} else {
		desc.anisotropyEnable = (r_ext_texture_filter_anisotropic->integer && vk.samplerAnisotropy) ? VK_TRUE : VK_FALSE;
		if ( desc.anisotropyEnable ) {
			desc.maxAnisotropy = MIN( r_ext_max_anisotropy->integer, vk.maxAnisotropy );
		}
	}

	desc.compareEnable = VK_FALSE;
	desc.compareOp = VK_COMPARE_OP_ALWAYS;
	desc.minLod = 0.0f;
	desc.maxLod = (maxLod == vk.maxLod) ? VK_LOD_CLAMP_NONE : maxLod;
	desc.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	desc.unnormalizedCoordinates = VK_FALSE;

	VK_CHECK( qvkCreateSampler( vk.device, &desc, NULL, &sampler ) );

	SET_OBJECT_NAME( sampler, va( "image sampler %i", vk_world.num_samplers ), VK_DEBUG_REPORT_OBJECT_TYPE_SAMPLER_EXT );

	vk_world.sampler_defs[ vk_world.num_samplers ] = *def;
	vk_world.samplers[ vk_world.num_samplers ] = sampler;
	vk_world.num_samplers++;

	return sampler;
}


static void vk_update_shadow_attachment_descriptor( VkImageView imageView,
	VkDescriptorSet descriptor )
{
	VkDescriptorImageInfo info;
	VkWriteDescriptorSet write;
	Vk_Sampler_Def samplerDef;

	if ( imageView == VK_NULL_HANDLE || descriptor == VK_NULL_HANDLE ) {
		return;
	}

	Com_Memset( &samplerDef, 0, sizeof( samplerDef ) );
	samplerDef.gl_mag_filter = GL_NEAREST;
	samplerDef.gl_min_filter = GL_NEAREST;
	samplerDef.address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerDef.max_lod_1_0 = qtrue;
	samplerDef.noAnisotropy = qtrue;

	Com_Memset( &info, 0, sizeof( info ) );
	info.sampler = vk_find_sampler( &samplerDef );
	info.imageView = imageView;
	info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	Com_Memset( &write, 0, sizeof( write ) );
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = descriptor;
	write.dstBinding = 0;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.pImageInfo = &info;

	qvkUpdateDescriptorSets( vk.device, 1, &write, 0, NULL );
}


static void vk_update_attachment_descriptors( void ) {

	if ( vk.color_image_view )
	{
		VkDescriptorImageInfo info;
		VkWriteDescriptorSet desc;
		Vk_Sampler_Def sd;

		Com_Memset( &sd, 0, sizeof( sd ) );
		sd.gl_mag_filter = sd.gl_min_filter = vk.blitFilter;
		sd.address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sd.max_lod_1_0 = qtrue;
		sd.noAnisotropy = qtrue;

		info.sampler = vk_find_sampler( &sd );
		info.imageView = vk.color_image_view;
		info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		desc.dstSet = vk.color_descriptor;
		desc.dstBinding = 0;
		desc.dstArrayElement = 0;
		desc.descriptorCount = 1;
		desc.pNext = NULL;
		desc.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		desc.pImageInfo = &info;
		desc.pBufferInfo = NULL;
		desc.pTexelBufferView = NULL;

		qvkUpdateDescriptorSets( vk.device, 1, &desc, 0, NULL );

		if ( vk.motion_blur_image_view != VK_NULL_HANDLE &&
			vk.motion_blur_descriptor != VK_NULL_HANDLE ) {
			info.imageView = vk.motion_blur_image_view;
			desc.dstSet = vk.motion_blur_descriptor;
			qvkUpdateDescriptorSets( vk.device, 1, &desc, 0, NULL );
		}

		// screenmap
		sd.gl_mag_filter = sd.gl_min_filter = GL_LINEAR;
		sd.max_lod_1_0 = qfalse;
		sd.noAnisotropy = qtrue;

		info.sampler = vk_find_sampler( &sd );

		info.imageView = vk.screenMap.color_image_view;
		desc.dstSet = vk.screenMap.color_descriptor;

		qvkUpdateDescriptorSets( vk.device, 1, &desc, 0, NULL );

		if ( vk.liquidSnapshot.source_descriptor &&
			vk.liquidSnapshot.color_descriptor ) {
			info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			info.imageView = vk.color_image_view;
			desc.dstSet = vk.liquidSnapshot.source_descriptor;
			qvkUpdateDescriptorSets( vk.device, 1, &desc, 0, NULL );

			info.imageView = vk.liquidSnapshot.color_image_view;
			desc.dstSet = vk.liquidSnapshot.color_descriptor;
			qvkUpdateDescriptorSets( vk.device, 1, &desc, 0, NULL );
		}

		if ( vk.depth_sample_image_view && vk.depth_sample_descriptor ) {
			sd.gl_mag_filter = sd.gl_min_filter = GL_NEAREST;
			sd.max_lod_1_0 = qtrue;
			sd.noAnisotropy = qtrue;

			info.sampler = vk_find_sampler( &sd );
			info.imageView = vk.depth_sample_image_view;
			info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
			desc.dstSet = vk.depth_sample_descriptor;

			qvkUpdateDescriptorSets( vk.device, 1, &desc, 0, NULL );
		}

		// bloom images
		if ( r_bloom->integer )
		{
			uint32_t i;
			info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			for ( i = 0; i < ARRAY_LEN( vk.bloom_image_descriptor ); i++ )
			{
				info.imageView = vk.bloom_image_view[i];
				desc.dstSet = vk.bloom_image_descriptor[i];

				qvkUpdateDescriptorSets( vk.device, 1, &desc, 0, NULL );
			}
		}
	}

	if ( vk.liquidDepth.image_view && vk.liquidDepth.descriptor ) {
		VkDescriptorImageInfo info;
		VkWriteDescriptorSet desc;
		Vk_Sampler_Def sd;

		Com_Memset( &sd, 0, sizeof( sd ) );
		sd.gl_mag_filter = sd.gl_min_filter = GL_NEAREST;
		sd.address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sd.max_lod_1_0 = qtrue;
		sd.noAnisotropy = qtrue;

		info.sampler = vk_find_sampler( &sd );
		info.imageView = vk.liquidDepth.image_view;
		info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		Com_Memset( &desc, 0, sizeof( desc ) );
		desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		desc.dstSet = vk.liquidDepth.descriptor;
		desc.dstBinding = 0;
		desc.descriptorCount = 1;
		desc.descriptorType =
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		desc.pImageInfo = &info;

		qvkUpdateDescriptorSets( vk.device, 1, &desc, 0, NULL );
	}

	vk_update_shadow_attachment_descriptor( vk.dlight_shadow_image_view,
		vk.dlight_shadow_descriptor );
	vk_update_shadow_attachment_descriptor( vk.spot_shadow_image_view,
		vk.spot_shadow_descriptor );
	vk_update_shadow_attachment_descriptor( vk.csm_shadow_image_view,
		vk.csm_shadow_descriptor );
}


void vk_init_descriptors( void )
{
	VkDescriptorSetAllocateInfo alloc;
	VkDescriptorBufferInfo info;
	VkWriteDescriptorSet desc;
	uint32_t i;

	alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	alloc.pNext = NULL;
	alloc.descriptorPool = vk.descriptor_pool;
	alloc.descriptorSetCount = 1;
	alloc.pSetLayouts = vk_descriptor_layout_ref( RTX_VK_DESC_LAYOUT_STORAGE );

	VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc, &vk.storage.descriptor ) );

	info.buffer = vk.storage.buffer;
	info.offset = 0;
	info.range = sizeof( uint32_t );

	desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	desc.dstSet = vk.storage.descriptor;
	desc.dstBinding = 0;
	desc.dstArrayElement = 0;
	desc.descriptorCount = 1;
	desc.pNext = NULL;
	desc.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
	desc.pImageInfo = NULL;
	desc.pBufferInfo = &info;
	desc.pTexelBufferView = NULL;

	qvkUpdateDescriptorSets( vk.device, 1, &desc, 0, NULL );

	// allocated and update descriptor set
	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ )
	{
		alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		alloc.pNext = NULL;
		alloc.descriptorPool = vk.descriptor_pool;
		alloc.descriptorSetCount = 1;
		alloc.pSetLayouts = vk_descriptor_layout_ref( RTX_VK_DESC_LAYOUT_UNIFORM );

		VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc, &vk.tess[i].uniform_descriptor ) );

		vk_update_uniform_descriptor( vk.tess[ i ].uniform_descriptor, vk.tess[ i ].vertex_buffer );

		SET_OBJECT_NAME( vk.tess[ i ].uniform_descriptor, va( "uniform descriptor %i", i ), VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT );
	}

	if ( vk.color_image_view )
	{
		alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		alloc.pNext = NULL;
		alloc.descriptorPool = vk.descriptor_pool;
		alloc.descriptorSetCount = 1;
		alloc.pSetLayouts = vk_descriptor_layout_ref( RTX_VK_DESC_LAYOUT_SAMPLER );

		VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc, &vk.color_descriptor ) );
		if ( vk.motion_blur_image_view != VK_NULL_HANDLE ) {
			VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc,
				&vk.motion_blur_descriptor ) );
			SET_OBJECT_NAME( vk.motion_blur_descriptor,
				"motion blur scratch descriptor",
				VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT );
		}

		if ( vk.depth_sample_image_view ) {
			VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc, &vk.depth_sample_descriptor ) );
			SET_OBJECT_NAME( vk.depth_sample_descriptor, "global fog depth descriptor", VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT );
		}

		if ( r_bloom->integer )
		{
			for ( i = 0; i < ARRAY_LEN( vk.bloom_image_descriptor ); i++ )
			{
				VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc, &vk.bloom_image_descriptor[i] ) );
			}
		}

		alloc.descriptorSetCount = 1;
		VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc, &vk.screenMap.color_descriptor ) ); // screenmap

		if ( vk.liquidSnapshot.color_image_view ) {
			VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc,
				&vk.liquidSnapshot.source_descriptor ) );
			VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc,
				&vk.liquidSnapshot.color_descriptor ) );
			SET_OBJECT_NAME( vk.liquidSnapshot.source_descriptor,
				"liquid snapshot source descriptor",
				VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT );
			SET_OBJECT_NAME( vk.liquidSnapshot.color_descriptor,
				"liquid snapshot descriptor",
				VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT );
		}
		if ( vk.liquidDepth.image_view ) {
			VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc,
				&vk.liquidDepth.descriptor ) );
			SET_OBJECT_NAME( vk.liquidDepth.descriptor,
				"liquid depth descriptor",
				VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT );
		}

		vk_update_attachment_descriptors();
	}

	if ( vk.dlight_shadow_image_view ) {
		alloc.pSetLayouts =
			vk_descriptor_layout_ref( RTX_VK_DESC_LAYOUT_SAMPLER );
		VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc,
			&vk.dlight_shadow_descriptor ) );
		SET_OBJECT_NAME( vk.dlight_shadow_descriptor,
			"dlight shadow atlas descriptor",
			VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT );
	}

	if ( vk.spot_shadow_image_view ) {
		alloc.pSetLayouts =
			vk_descriptor_layout_ref( RTX_VK_DESC_LAYOUT_SAMPLER );
		VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc,
			&vk.spot_shadow_descriptor ) );
		SET_OBJECT_NAME( vk.spot_shadow_descriptor,
			"spot shadow atlas descriptor",
			VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT );
	}

	if ( vk.csm_shadow_image_view ) {
		alloc.pSetLayouts =
			vk_descriptor_layout_ref( RTX_VK_DESC_LAYOUT_SAMPLER );
		VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc,
			&vk.csm_shadow_descriptor ) );
		SET_OBJECT_NAME( vk.csm_shadow_descriptor,
			"csm shadow atlas descriptor",
			VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT );
	}

	vk_update_attachment_descriptors();
}


static void vk_release_geometry_buffers( void )
{
	int i;

	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		qvkDestroyBuffer( vk.device, vk.tess[i].vertex_buffer, NULL );
		vk.tess[i].vertex_buffer = VK_NULL_HANDLE;
	}

	qvkFreeMemory( vk.device, vk.geometry_buffer_memory, NULL );
	vk.geometry_buffer_memory = VK_NULL_HANDLE;
	vk_lifetime_mark_destroy( RTX_VK_RESOURCE_GEOMETRY_BUFFERS, __func__ );
}


static void vk_create_geometry_buffers( VkDeviceSize size )
{
	VkMemoryRequirements vb_memory_requirements;
	VkMemoryAllocateInfo alloc_info;
	VkBufferCreateInfo desc;
	VkDeviceSize vertex_buffer_offset;
	uint32_t memory_type_bits;
	uint32_t memory_type;
	void *data;
	int i;

	desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	desc.queueFamilyIndexCount = 0;
	desc.pQueueFamilyIndices = NULL;

	Com_Memset( &vb_memory_requirements, 0, sizeof( vb_memory_requirements ) );

	for ( i = 0 ; i < NUM_COMMAND_BUFFERS; i++ ) {
		desc.size = size;
		desc.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		VK_CHECK( qvkCreateBuffer( vk.device, &desc, NULL, &vk.tess[i].vertex_buffer ) );

		qvkGetBufferMemoryRequirements( vk.device, vk.tess[i].vertex_buffer, &vb_memory_requirements );
	}

	memory_type_bits = vb_memory_requirements.memoryTypeBits;
	memory_type = find_memory_type( memory_type_bits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );

	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.pNext = NULL;
	alloc_info.allocationSize = vb_memory_requirements.size * NUM_COMMAND_BUFFERS;
	alloc_info.memoryTypeIndex = memory_type;

	VK_CHECK( qvkAllocateMemory( vk.device, &alloc_info, NULL, &vk.geometry_buffer_memory ) );
	VK_CHECK( qvkMapMemory( vk.device, vk.geometry_buffer_memory, 0, VK_WHOLE_SIZE, 0, &data ) );

	vertex_buffer_offset = 0;

	for ( i = 0 ; i < NUM_COMMAND_BUFFERS; i++ ) {
		qvkBindBufferMemory( vk.device, vk.tess[i].vertex_buffer, vk.geometry_buffer_memory, vertex_buffer_offset );
		vk.tess[i].vertex_buffer_ptr = (byte*)data + vertex_buffer_offset;
		vk.tess[i].vertex_buffer_offset = 0;
		vertex_buffer_offset += vb_memory_requirements.size;

		SET_OBJECT_NAME( vk.tess[i].vertex_buffer, va( "geometry buffer %i", i ), VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT );
	}

	SET_OBJECT_NAME( vk.geometry_buffer_memory, "geometry buffer memory", VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT );

	vk.geometry_buffer_size = vb_memory_requirements.size;

	Com_Memset( &vk.stats, 0, sizeof( vk.stats ) );
	vk_lifetime_mark_create( RTX_VK_RESOURCE_GEOMETRY_BUFFERS, __func__ );
}


static void vk_create_storage_buffer( uint32_t size )
{
	VkMemoryRequirements memory_requirements;
	VkMemoryAllocateInfo alloc_info;
	VkBufferCreateInfo desc;
	uint32_t memory_type_bits;
	uint32_t memory_type;

	desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	desc.queueFamilyIndexCount = 0;
	desc.pQueueFamilyIndices = NULL;

	Com_Memset( &memory_requirements, 0, sizeof( memory_requirements ) );

	desc.size = size;
	desc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	VK_CHECK( qvkCreateBuffer( vk.device, &desc, NULL, &vk.storage.buffer ) );

	qvkGetBufferMemoryRequirements( vk.device, vk.storage.buffer, &memory_requirements );

	memory_type_bits = memory_requirements.memoryTypeBits;
	memory_type = find_memory_type( memory_type_bits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );

	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.pNext = NULL;
	alloc_info.allocationSize = memory_requirements.size;
	alloc_info.memoryTypeIndex = memory_type;

	VK_CHECK( qvkAllocateMemory( vk.device, &alloc_info, NULL, &vk.storage.memory ) );
	VK_CHECK( qvkMapMemory( vk.device, vk.storage.memory, 0, VK_WHOLE_SIZE, 0, (void**)&vk.storage.buffer_ptr ) );

	Com_Memset( vk.storage.buffer_ptr, 0, memory_requirements.size );

	qvkBindBufferMemory( vk.device, vk.storage.buffer, vk.storage.memory, 0 );

	SET_OBJECT_NAME( vk.storage.buffer, "storage buffer", VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT );
	SET_OBJECT_NAME( vk.storage.descriptor, "storage buffer", VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT );
	SET_OBJECT_NAME( vk.storage.memory, "storage buffer memory", VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT );
	vk_lifetime_mark_create( RTX_VK_RESOURCE_STORAGE_BUFFER, __func__ );
}

static void vk_destroy_storage_buffer( void )
{
	if ( vk.storage.buffer != VK_NULL_HANDLE ) {
		qvkDestroyBuffer( vk.device, vk.storage.buffer, NULL );
		vk.storage.buffer = VK_NULL_HANDLE;
	}

	if ( vk.storage.memory != VK_NULL_HANDLE ) {
		qvkFreeMemory( vk.device, vk.storage.memory, NULL );
		vk.storage.memory = VK_NULL_HANDLE;
	}

	vk.storage.buffer_ptr = NULL;
	vk_lifetime_mark_destroy( RTX_VK_RESOURCE_STORAGE_BUFFER, __func__ );
}


#ifdef USE_VBO
void vk_release_vbo( void )
{
	if ( vk.vbo.vertex_buffer )
		qvkDestroyBuffer( vk.device, vk.vbo.vertex_buffer, NULL );
	vk.vbo.vertex_buffer = VK_NULL_HANDLE;

	if ( vk.vbo.buffer_memory )
		qvkFreeMemory( vk.device, vk.vbo.buffer_memory, NULL );
	vk.vbo.buffer_memory = VK_NULL_HANDLE;
}


qboolean vk_alloc_vbo( const byte *vbo_data, int vbo_size )
{
	VkMemoryRequirements vb_mem_reqs;
	VkMemoryAllocateInfo alloc_info;
	VkBufferCreateInfo desc;
	VkDeviceSize vertex_buffer_offset;
	VkDeviceSize allocationSize;
	uint32_t memory_type_bits;
	VkBuffer staging_vertex_buffer;
	VkDeviceMemory staging_buffer_memory;
	VkCommandBuffer command_buffer;
	VkBufferCopy copyRegion[1];
	void *data;

	vk_release_vbo();

	desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	desc.queueFamilyIndexCount = 0;
	desc.pQueueFamilyIndices = NULL;

	// device-local buffer
	desc.size = vbo_size;
	desc.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	VK_CHECK( qvkCreateBuffer( vk.device, &desc, NULL, &vk.vbo.vertex_buffer ) );

	// staging buffer
	desc.size = vbo_size;
	desc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	VK_CHECK( qvkCreateBuffer( vk.device, &desc, NULL, &staging_vertex_buffer ) );

	// memory requirements
	qvkGetBufferMemoryRequirements( vk.device, vk.vbo.vertex_buffer, &vb_mem_reqs );
	vertex_buffer_offset = 0;
	allocationSize = vertex_buffer_offset + vb_mem_reqs.size;
	memory_type_bits = vb_mem_reqs.memoryTypeBits;

	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.pNext = NULL;
	alloc_info.allocationSize = allocationSize;
	alloc_info.memoryTypeIndex = find_memory_type( memory_type_bits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
	VK_CHECK( qvkAllocateMemory( vk.device, &alloc_info, NULL, &vk.vbo.buffer_memory ) );
	qvkBindBufferMemory( vk.device, vk.vbo.vertex_buffer, vk.vbo.buffer_memory, vertex_buffer_offset );

	// staging buffers

	// memory requirements
	qvkGetBufferMemoryRequirements( vk.device, staging_vertex_buffer, &vb_mem_reqs );
	vertex_buffer_offset = 0;
	allocationSize = vertex_buffer_offset + vb_mem_reqs.size;
	memory_type_bits = vb_mem_reqs.memoryTypeBits;

	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.pNext = NULL;
	alloc_info.allocationSize = allocationSize;
	alloc_info.memoryTypeIndex = find_memory_type( memory_type_bits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );
	VK_CHECK( qvkAllocateMemory( vk.device, &alloc_info, NULL, &staging_buffer_memory ) );
	qvkBindBufferMemory( vk.device, staging_vertex_buffer, staging_buffer_memory, vertex_buffer_offset );

	VK_CHECK( qvkMapMemory( vk.device, staging_buffer_memory, 0, VK_WHOLE_SIZE, 0, &data ) );
	memcpy( (byte*)data + vertex_buffer_offset, vbo_data, vbo_size );
	qvkUnmapMemory( vk.device, staging_buffer_memory );

	command_buffer = begin_command_buffer();
	copyRegion[0].srcOffset = 0;
	copyRegion[0].dstOffset = 0;
	copyRegion[0].size = vbo_size;
	qvkCmdCopyBuffer( command_buffer, staging_vertex_buffer, vk.vbo.vertex_buffer, 1, &copyRegion[0] );

	end_command_buffer( command_buffer, __func__ );

	qvkDestroyBuffer( vk.device, staging_vertex_buffer, NULL );
	qvkFreeMemory( vk.device, staging_buffer_memory, NULL );

	SET_OBJECT_NAME( vk.vbo.vertex_buffer, "static VBO", VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT );
	SET_OBJECT_NAME( vk.vbo.buffer_memory, "static VBO memory", VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT );

	return qtrue;
}
#endif

#include "shaders/spirv/shader_data.c"
#define SHADER_MODULE(name) SHADER_MODULE(name,sizeof(name))

static void vk_create_shader_modules( void )
{
	int i, j, k, l;

	vk.modules.vert.gen[0][0][0][0] = SHADER_MODULE( vert_tx0 );
	vk.modules.vert.gen[0][0][0][1] = SHADER_MODULE( vert_tx0_fog );
	vk.modules.vert.gen[0][0][1][0] = SHADER_MODULE( vert_tx0_env );
	vk.modules.vert.gen[0][0][1][1] = SHADER_MODULE( vert_tx0_env_fog );

	vk.modules.vert.gen[1][0][0][0] = SHADER_MODULE( vert_tx1 );
	vk.modules.vert.gen[1][0][0][1] = SHADER_MODULE( vert_tx1_fog );
	vk.modules.vert.gen[1][0][1][0] = SHADER_MODULE( vert_tx1_env );
	vk.modules.vert.gen[1][0][1][1] = SHADER_MODULE( vert_tx1_env_fog );

	vk.modules.vert.gen[1][1][0][0] = SHADER_MODULE( vert_tx1_cl );
	vk.modules.vert.gen[1][1][0][1] = SHADER_MODULE( vert_tx1_cl_fog );
	vk.modules.vert.gen[1][1][1][0] = SHADER_MODULE( vert_tx1_cl_env );
	vk.modules.vert.gen[1][1][1][1] = SHADER_MODULE( vert_tx1_cl_env_fog );

	vk.modules.vert.gen[2][0][0][0] = SHADER_MODULE( vert_tx2 );
	vk.modules.vert.gen[2][0][0][1] = SHADER_MODULE( vert_tx2_fog );
	vk.modules.vert.gen[2][0][1][0] = SHADER_MODULE( vert_tx2_env );
	vk.modules.vert.gen[2][0][1][1] = SHADER_MODULE( vert_tx2_env_fog );

	vk.modules.vert.gen[2][1][0][0] = SHADER_MODULE( vert_tx2_cl );
	vk.modules.vert.gen[2][1][0][1] = SHADER_MODULE( vert_tx2_cl_fog );
	vk.modules.vert.gen[2][1][1][0] = SHADER_MODULE( vert_tx2_cl_env );
	vk.modules.vert.gen[2][1][1][1] = SHADER_MODULE( vert_tx2_cl_env_fog );

	for ( i = 0; i < 3; i++ ) {
		const char *tx[] = { "single", "double", "triple" };
		const char *cl[] = { "", "+cl" };
		const char *env[] = { "", "+env" };
		const char *fog[] = { "", "+fog" };
		for ( j = 0; j < 2; j++ ) {
			for ( k = 0; k < 2; k++ ) {
				for ( l = 0; l < 2; l++ ) {
					const char *s = va( "%s-texture%s%s%s vertex module", tx[i], cl[j], env[k], fog[l] );
					SET_OBJECT_NAME( vk.modules.vert.gen[i][j][k][l], s, VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
				}
			}
		}
	}

	// specialized depth-fragment shader
	vk.modules.frag.gen0_df = SHADER_MODULE( frag_tx0_df );
	SET_OBJECT_NAME( vk.modules.frag.gen0_df, "single-texture df fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );

	// fixed-color (1.0) shader modules
	vk.modules.vert.ident1[0][0][0] = SHADER_MODULE( vert_tx0_ident1 );
	vk.modules.vert.ident1[0][0][1] = SHADER_MODULE( vert_tx0_ident1_fog );
	vk.modules.vert.ident1[0][1][0] = SHADER_MODULE( vert_tx0_ident1_env );
	vk.modules.vert.ident1[0][1][1] = SHADER_MODULE( vert_tx0_ident1_env_fog );
	vk.modules.vert.ident1[1][0][0] = SHADER_MODULE( vert_tx1_ident1 );
	vk.modules.vert.ident1[1][0][1] = SHADER_MODULE( vert_tx1_ident1_fog );
	vk.modules.vert.ident1[1][1][0] = SHADER_MODULE( vert_tx1_ident1_env );
	vk.modules.vert.ident1[1][1][1] = SHADER_MODULE( vert_tx1_ident1_env_fog );
	for ( i = 0; i < 2; i++ ) {
		const char *tx[] = { "single", "double" };
		const char *env[] = { "", "+env" };
		const char *fog[] = { "", "+fog" };
		for ( j = 0; j < 2; j++ ) {
			for ( k = 0; k < 2; k++ ) {
				const char *s = va( "%s-texture identity%s%s vertex module", tx[i], env[j], fog[k] );
				SET_OBJECT_NAME( vk.modules.vert.ident1[i][j][k], s, VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
			}
		}
	}

	vk.modules.frag.ident1[0][0] = SHADER_MODULE( frag_tx0_ident1 );
	vk.modules.frag.ident1[0][1] = SHADER_MODULE( frag_tx0_ident1_fog );
	vk.modules.frag.ident1[1][0] = SHADER_MODULE( frag_tx1_ident1 );
	vk.modules.frag.ident1[1][1] = SHADER_MODULE( frag_tx1_ident1_fog );
	for ( i = 0; i < 2; i++ ) {
		const char *tx[] = { "single", "double" };
		const char *fog[] = { "", "+fog" };
		for ( j = 0; j < 2; j++ ) {
			const char *s = va( "%s-texture identity%s fragment module", tx[i], fog[j] );
			SET_OBJECT_NAME( vk.modules.frag.ident1[i][j], s, VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
		}
	}

	vk.modules.vert.fixed[0][0][0] = SHADER_MODULE( vert_tx0_fixed );
	vk.modules.vert.fixed[0][0][1] = SHADER_MODULE( vert_tx0_fixed_fog );
	vk.modules.vert.fixed[0][1][0] = SHADER_MODULE( vert_tx0_fixed_env );
	vk.modules.vert.fixed[0][1][1] = SHADER_MODULE( vert_tx0_fixed_env_fog );
	vk.modules.vert.fixed[1][0][0] = SHADER_MODULE( vert_tx1_fixed );
	vk.modules.vert.fixed[1][0][1] = SHADER_MODULE( vert_tx1_fixed_fog );
	vk.modules.vert.fixed[1][1][0] = SHADER_MODULE( vert_tx1_fixed_env );
	vk.modules.vert.fixed[1][1][1] = SHADER_MODULE( vert_tx1_fixed_env_fog );
	for ( i = 0; i < 2; i++ ) {
		const char *tx[] = { "single", "double" };
		const char *env[] = { "", "+env" };
		const char *fog[] = { "", "+fog" };
		for ( j = 0; j < 2; j++ ) {
			for ( k = 0; k < 2; k++ ) {
				const char *s = va( "%s-texture fixed-color%s%s vertex module", tx[i], env[j], fog[k] );
				SET_OBJECT_NAME( vk.modules.vert.fixed[i][j][k], s, VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
			}
		}
	}

	vk.modules.frag.fixed[0][0] = SHADER_MODULE( frag_tx0_fixed );
	vk.modules.frag.fixed[0][1] = SHADER_MODULE( frag_tx0_fixed_fog );
	vk.modules.frag.fixed[1][0] = SHADER_MODULE( frag_tx1_fixed );
	vk.modules.frag.fixed[1][1] = SHADER_MODULE( frag_tx1_fixed_fog );
	for ( i = 0; i < 2; i++ ) {
		const char *tx[] = { "single", "double" };
		const char *fog[] = { "", "+fog" };
		for ( j = 0; j < 2; j++ ) {
			const char *s = va( "%s-texture fixed-color%s fragment module", tx[i], fog[j] );
			SET_OBJECT_NAME( vk.modules.frag.fixed[i][j], s, VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
		}
	}

	vk.modules.frag.ent[0][0] = SHADER_MODULE( frag_tx0_ent );
	vk.modules.frag.ent[0][1] = SHADER_MODULE( frag_tx0_ent_fog );
	//vk.modules.frag.ent[1][0] = SHADER_MODULE( frag_tx1_ent );
	//vk.modules.frag.ent[1][1] = SHADER_MODULE( frag_tx1_ent_fog );
	for ( i = 0; i < 1; i++ ) {
		const char *tx[] = { "single" /*, "double" */};
		const char *fog[] = { "", "+fog" };
		for ( j = 0; j < 2; j++ ) {
			const char *s = va( "%s-texture entity-color%s fragment module", tx[i], fog[j] );
			SET_OBJECT_NAME( vk.modules.frag.ent[i][j], s, VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
		}
	}

	vk.modules.frag.gen[0][0][0] = SHADER_MODULE( frag_tx0 );
	vk.modules.frag.gen[0][0][1] = SHADER_MODULE( frag_tx0_fog );

	vk.modules.frag.gen[1][0][0] = SHADER_MODULE( frag_tx1 );
	vk.modules.frag.gen[1][0][1] = SHADER_MODULE( frag_tx1_fog );

	vk.modules.frag.gen[1][1][0] = SHADER_MODULE( frag_tx1_cl );
	vk.modules.frag.gen[1][1][1] = SHADER_MODULE( frag_tx1_cl_fog );

	vk.modules.frag.gen[2][0][0] = SHADER_MODULE( frag_tx2 );
	vk.modules.frag.gen[2][0][1] = SHADER_MODULE( frag_tx2_fog );

	vk.modules.frag.gen[2][1][0] = SHADER_MODULE( frag_tx2_cl );
	vk.modules.frag.gen[2][1][1] = SHADER_MODULE( frag_tx2_cl_fog );

	for ( i = 0; i < 3; i++ ) {
		const char *tx[] = { "single", "double", "triple" };
		const char *cl[] = { "", "+cl" };
		const char *fog[] = { "", "+fog" };
		for ( j = 0; j < 2; j++ ) {
			for ( k = 0; k < 2; k++ ) {
				const char *s = va( "%s-texture%s%s fragment module", tx[i], cl[j], fog[k] );
				SET_OBJECT_NAME( vk.modules.frag.gen[i][j][k], s, VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
			}
		}
	}


	vk.modules.vert.light[0] = SHADER_MODULE( vert_light );
	vk.modules.vert.light[1] = SHADER_MODULE( vert_light_fog );
	SET_OBJECT_NAME( vk.modules.vert.light[0], "light vertex module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
	SET_OBJECT_NAME( vk.modules.vert.light[1], "light fog vertex module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );

	vk.modules.frag.light[0][0] = SHADER_MODULE( frag_light );
	vk.modules.frag.light[0][1] = SHADER_MODULE( frag_light_fog );
	vk.modules.frag.light[1][0] = SHADER_MODULE( frag_light_line );
	vk.modules.frag.light[1][1] = SHADER_MODULE( frag_light_line_fog );
	SET_OBJECT_NAME( vk.modules.frag.light[0][0], "light fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
	SET_OBJECT_NAME( vk.modules.frag.light[0][1], "light fog fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
	SET_OBJECT_NAME( vk.modules.frag.light[1][0], "linear light fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
	SET_OBJECT_NAME( vk.modules.frag.light[1][1], "linear light fog fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );

	vk.modules.vert.csm_shadow = SHADER_MODULE( csm_shadow_vert_spv );
	vk.modules.frag.csm_shadow = SHADER_MODULE( csm_shadow_frag_spv );
	SET_OBJECT_NAME( vk.modules.vert.csm_shadow, "csm shadow vertex module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
	SET_OBJECT_NAME( vk.modules.frag.csm_shadow, "csm shadow fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );

	vk.modules.color_fs = SHADER_MODULE( color_frag_spv );
	vk.modules.color_vs = SHADER_MODULE( color_vert_spv );

	SET_OBJECT_NAME( vk.modules.color_vs, "single-color vertex module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
	SET_OBJECT_NAME( vk.modules.color_fs, "single-color fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );

	vk.modules.fog_vs = SHADER_MODULE( fog_vert_spv );
	vk.modules.fog_fs = SHADER_MODULE( fog_frag_spv );

	SET_OBJECT_NAME( vk.modules.fog_vs, "fog-only vertex module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
	SET_OBJECT_NAME( vk.modules.fog_fs, "fog-only fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );

	vk.modules.dot_vs = SHADER_MODULE( dot_vert_spv );
	vk.modules.dot_fs = SHADER_MODULE( dot_frag_spv );

	SET_OBJECT_NAME( vk.modules.dot_vs, "dot vertex module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
	SET_OBJECT_NAME( vk.modules.dot_fs, "dot fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );

	if ( vk_liquid_requested() ) {
		vk.modules.liquid_vs = SHADER_MODULE( liquid_vert_spv );
		vk.modules.liquid_fs = SHADER_MODULE( liquid_frag_spv );
		vk.modules.liquid_copy_fs = SHADER_MODULE( liquid_copy_frag_spv );

		SET_OBJECT_NAME( vk.modules.liquid_vs,
			"liquid effect vertex module",
			VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
		SET_OBJECT_NAME( vk.modules.liquid_fs,
			"liquid effect fragment module",
			VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
		SET_OBJECT_NAME( vk.modules.liquid_copy_fs,
			"liquid scene-copy fragment module",
			VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
	}

	vk.modules.bloom_fs = SHADER_MODULE( bloom_frag_spv );
	vk.modules.blur_fs = SHADER_MODULE( blur_frag_spv );
	vk.modules.blend_fs = SHADER_MODULE( blend_frag_spv );
	vk.modules.motion_blur_fs = SHADER_MODULE( motion_blur_frag_spv );
	vk.modules.world_outline_fs = SHADER_MODULE( world_outline_frag_spv );
	vk.modules.global_fog_fs = SHADER_MODULE( global_fog_frag_spv );

	SET_OBJECT_NAME( vk.modules.bloom_fs, "bloom extraction fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
	SET_OBJECT_NAME( vk.modules.blur_fs, "gaussian blur fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
	SET_OBJECT_NAME( vk.modules.blend_fs, "final bloom blend fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
	SET_OBJECT_NAME( vk.modules.motion_blur_fs, "motion blur fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
	SET_OBJECT_NAME( vk.modules.world_outline_fs, "world cel depth-outline fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
	SET_OBJECT_NAME( vk.modules.global_fog_fs, "global fog fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );

	vk.modules.gamma_fs = SHADER_MODULE( gamma_frag_spv );
	vk.modules.gamma_vs = SHADER_MODULE( gamma_vert_spv );

	SET_OBJECT_NAME( vk.modules.gamma_fs, "gamma post-processing fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
	SET_OBJECT_NAME( vk.modules.gamma_vs, "gamma post-processing vertex module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );

	if ( vk_rt_pipeline_enabled() ) {
		vk.modules.rt_rgen = SHADER_MODULE( rt_main_rgen_spv );
		vk.modules.rt_rmiss = SHADER_MODULE( rt_main_rmiss_spv );
		vk.modules.rt_rmiss_shadow = SHADER_MODULE( rt_shadow_rmiss_spv );
		vk.modules.rt_rchit = SHADER_MODULE( rt_main_rchit_spv );
		vk.modules.rt_rahit = SHADER_MODULE( rt_main_rahit_spv );

		SET_OBJECT_NAME( vk.modules.rt_rgen, "rt raygen module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
		SET_OBJECT_NAME( vk.modules.rt_rmiss, "rt miss module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
		SET_OBJECT_NAME( vk.modules.rt_rmiss_shadow, "rt shadow miss module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
		SET_OBJECT_NAME( vk.modules.rt_rchit, "rt closest-hit module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
		SET_OBJECT_NAME( vk.modules.rt_rahit, "rt any-hit module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
	}
}


static void vk_alloc_persistent_pipelines( void )
{
	unsigned int state_bits;
	Vk_Pipeline_Def def;

	// skybox
	{
		Com_Memset(&def, 0, sizeof(def));
		def.shader_type = TYPE_SIGNLE_TEXTURE_FIXED_COLOR;
		def.color.rgb = tr.identityLightByte;
		def.color.alpha = tr.identityLightByte;
		def.face_culling = CT_FRONT_SIDED;
		def.polygon_offset = qfalse;
		def.mirror = qfalse;
		vk.skybox_pipeline = vk_find_pipeline_ext( 0, &def, qtrue );
	}

	// stencil shadows
	{
		cullType_t cull_types[2] = { CT_FRONT_SIDED, CT_BACK_SIDED };
		qboolean mirror_flags[2] = { qfalse, qtrue };
		int i, j;

		Com_Memset(&def, 0, sizeof(def));
		def.polygon_offset = qfalse;
		def.state_bits = 0;
		def.shader_type = TYPE_SIGNLE_TEXTURE;
		def.shadow_phase = SHADOW_EDGES;

		for (i = 0; i < 2; i++) {
			def.face_culling = cull_types[i];
			for (j = 0; j < 2; j++) {
				def.mirror = mirror_flags[j];
				vk.shadow_volume_pipelines[i][j] = vk_find_pipeline_ext( 0, &def, r_shadows->integer ? qtrue: qfalse );
			}
		}
	}
	{
		Com_Memset( &def, 0, sizeof( def ) );
		def.face_culling = CT_FRONT_SIDED;
		def.polygon_offset = qfalse;
		def.state_bits = GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO;
		def.shader_type = TYPE_SIGNLE_TEXTURE;
		def.mirror = qfalse;
		def.shadow_phase = SHADOW_FS_QUAD;
		def.primitives = TRIANGLE_STRIP;
		vk.shadow_finish_pipeline = vk_find_pipeline_ext( 0, &def, r_shadows->integer ? qtrue: qfalse );
	}

	// fog and dlights
	{
		unsigned int fog_state_bits[2] = {
			GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL, // fogPass == FP_EQUAL
			GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA // fogPass == FP_LE
		};
		unsigned int dlight_state_bits[2] = {
			GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL,	// modulated
			GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL			// additive
		};
		qboolean polygon_offset[2] = { qfalse, qtrue };
		int i, j, k;
#ifdef USE_PMLIGHT
		int l;
#endif

		Com_Memset(&def, 0, sizeof(def));
		def.shader_type = TYPE_SIGNLE_TEXTURE;
		def.mirror = qfalse;

		for ( i = 0; i < 2; i++ ) {
			unsigned fog_state = fog_state_bits[ i ];
			unsigned dlight_state = dlight_state_bits[ i ];

			for ( j = 0; j < 3; j++ ) {
				def.face_culling = j; // cullType_t value

				for ( k = 0; k < 2; k++ ) {
					def.polygon_offset = polygon_offset[ k ];
#ifdef USE_FOG_ONLY
					def.shader_type = TYPE_FOG_ONLY;
#else
					def.shader_type = TYPE_SIGNLE_TEXTURE;
#endif
					def.state_bits = fog_state;
					vk.fog_pipelines[ i ][ j ][ k ] = vk_find_pipeline_ext( 0, &def, qtrue );

					def.shader_type = TYPE_SIGNLE_TEXTURE;
					def.state_bits = dlight_state;
#ifdef USE_LEGACY_DLIGHTS
#ifdef USE_PMLIGHT
					vk.dlight_pipelines[ i ][ j ][ k ] = vk_find_pipeline_ext( 0, &def, r_dlightMode->integer == 0 ? qtrue : qfalse );
#else
					vk.dlight_pipelines[ i ][ j ][ k ] = vk_find_pipeline_ext( 0, &def, qtrue );
#endif
#endif
				}
			}
		}

#ifdef USE_PMLIGHT
		def.state_bits = GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL;
		def.depth_fade =
			( ( ( ( r_dlightShadows && r_dlightShadows->integer ) ||
					( r_shadowCorrectness && r_shadowCorrectness->integer ) ) &&
				r_dlightMode && r_dlightMode->integer &&
				vk_dlight_shadow_atlas_available() ) ||
			  ( !( r_shadowCorrectness && r_shadowCorrectness->integer ) &&
				r_spotShadows && r_spotShadows->integer &&
				vk_spot_shadow_atlas_available() ) ) ? 1 : 0;
		//def.shader_type = TYPE_SIGNLE_TEXTURE_LIGHTING;
		for (i = 0; i < 3; i++) { // cullType
			def.face_culling = i;
			for ( j = 0; j < 2; j++ ) { // polygonOffset
				def.polygon_offset = polygon_offset[j];
				for ( k = 0; k < 2; k++ ) {
					def.fog_stage = k; // fogStage
					for ( l = 0; l < 2; l++ ) {
						def.abs_light = l;
						def.shader_type = TYPE_SIGNLE_TEXTURE_LIGHTING;
						vk.dlight_pipelines_x[i][j][k][l] = vk_find_pipeline_ext( 0, &def, qfalse );
						def.shader_type = TYPE_SIGNLE_TEXTURE_LIGHTING_LINEAR;
						vk.dlight1_pipelines_x[i][j][k][l] = vk_find_pipeline_ext( 0, &def, qfalse );
					}
				}
			}
		}

		Com_Memset( &def, 0, sizeof( def ) );
		def.state_bits = GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO |
			GLS_DEPTHFUNC_EQUAL;
		def.shader_type = TYPE_CSM_SHADOW;
		def.face_culling = CT_TWO_SIDED;
		def.polygon_offset = qfalse;
		def.mirror = qfalse;
		vk.csm_shadow_pipeline = vk_find_pipeline_ext( 0, &def,
			( !( r_shadowCorrectness && r_shadowCorrectness->integer ) &&
			r_csmShadows && r_csmShadows->integer &&
			vk_csm_shadow_atlas_available() ) ? qtrue : qfalse );
#endif // USE_PMLIGHT
	}

	if ( vk_liquid_requested() ) {
		int i, j, k;

		Com_Memset( &def, 0, sizeof( def ) );
		def.shader_type = TYPE_LIQUID;
		def.state_bits = GLS_SRCBLEND_SRC_ALPHA |
			GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
		for ( i = 0; i < 3; i++ ) {
			def.face_culling = i;
			for ( j = 0; j < 2; j++ ) {
				def.polygon_offset = j ? qtrue : qfalse;
				for ( k = 0; k < 2; k++ ) {
					def.mirror = k ? qtrue : qfalse;
					vk.liquid_pipelines[i][j][k] =
						vk_find_pipeline_ext( 0, &def, qfalse );
				}
			}
		}
	}

	// RT_BEAM surface
	{
		Com_Memset(&def, 0, sizeof(def));
		def.state_bits = GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE;
		def.face_culling = CT_FRONT_SIDED;
		def.primitives = TRIANGLE_STRIP;
		vk.surface_beam_pipeline = vk_find_pipeline_ext( 0, &def, qfalse );
	}

	// axis for missing models
	{
		Com_Memset( &def, 0, sizeof( def ) );
		def.state_bits = GLS_DEFAULT;
		def.shader_type = TYPE_SIGNLE_TEXTURE;
		def.face_culling = CT_TWO_SIDED;
		def.primitives = LINE_LIST;
		if ( vk.wideLines )
			def.line_width = 3;
		vk.surface_axis_pipeline = vk_find_pipeline_ext( 0, &def, qfalse );
	}

	// flare visibility test dot
	if ( vk.fragmentStores )
	{
		Com_Memset( &def, 0, sizeof( def ) );
		//def.state_bits = GLS_DEFAULT;
		def.face_culling = CT_TWO_SIDED;
		def.shader_type = TYPE_DOT;
		def.primitives = POINT_LIST;
		vk.dot_pipeline = vk_find_pipeline_ext( 0, &def, qtrue );
	}

	// DrawTris()
	state_bits = GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE;
	{
		Com_Memset(&def, 0, sizeof(def));
		def.state_bits = state_bits;
		def.shader_type = TYPE_COLOR_WHITE;
		def.face_culling = CT_FRONT_SIDED;
		vk.tris_debug_pipeline = vk_find_pipeline_ext( 0, &def, qfalse );
	}
	{
		Com_Memset(&def, 0, sizeof(def));
		def.state_bits = state_bits;
		def.shader_type = TYPE_COLOR_WHITE;
		def.face_culling = CT_BACK_SIDED;
		vk.tris_mirror_debug_pipeline = vk_find_pipeline_ext( 0, &def, qfalse );
	}
	{
		Com_Memset(&def, 0, sizeof(def));
		def.state_bits = state_bits;
		def.shader_type = TYPE_COLOR_GREEN;
		def.face_culling = CT_FRONT_SIDED;
		vk.tris_debug_green_pipeline = vk_find_pipeline_ext( 0, &def, qfalse );
	}
	{
		Com_Memset(&def, 0, sizeof(def));
		def.state_bits = state_bits;
		def.shader_type = TYPE_COLOR_GREEN;
		def.face_culling = CT_BACK_SIDED;
		vk.tris_mirror_debug_green_pipeline = vk_find_pipeline_ext( 0, &def, qfalse );
	}
	{
		Com_Memset(&def, 0, sizeof(def));
		def.state_bits = state_bits;
		def.shader_type = TYPE_COLOR_RED;
		def.face_culling = CT_FRONT_SIDED;
		vk.tris_debug_red_pipeline = vk_find_pipeline_ext( 0, &def, qfalse );
	}
	{
		Com_Memset(&def, 0, sizeof(def));
		def.state_bits = state_bits;
		def.shader_type = TYPE_COLOR_RED;
		def.face_culling = CT_BACK_SIDED;
		vk.tris_mirror_debug_red_pipeline = vk_find_pipeline_ext( 0, &def, qfalse );
	}

	// DrawNormals()
	{
		Com_Memset(&def, 0, sizeof(def));
		def.state_bits = GLS_DEPTHMASK_TRUE;
		def.shader_type = TYPE_SIGNLE_TEXTURE;
		def.primitives = LINE_LIST;
		vk.normals_debug_pipeline = vk_find_pipeline_ext( 0, &def, qfalse );
	}

	// RB_DebugPolygon()
	{
		Com_Memset(&def, 0, sizeof(def));
		def.state_bits = GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE;
		def.shader_type = TYPE_SIGNLE_TEXTURE;
		vk.surface_debug_pipeline_solid = vk_find_pipeline_ext( 0, &def, qfalse );
	}
	{
		Com_Memset(&def, 0, sizeof(def));
		def.state_bits = GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE;
		def.shader_type = TYPE_SIGNLE_TEXTURE;
		def.primitives = LINE_LIST;
		vk.surface_debug_pipeline_outline = vk_find_pipeline_ext( 0, &def, qfalse );
	}

	// RB_ShowImages
	{
		Com_Memset(&def, 0, sizeof(def));
		def.state_bits = GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
		def.shader_type = TYPE_SIGNLE_TEXTURE;
		def.primitives = TRIANGLE_STRIP;
		vk.images_debug_pipeline = vk_find_pipeline_ext( 0, &def, qfalse );

		def.state_bits = GLS_DEPTHTEST_DISABLE;
		def.shader_type = TYPE_COLOR_BLACK;
		def.primitives = TRIANGLE_STRIP;
		vk.images_debug_pipeline2 = vk_find_pipeline_ext( 0, &def, qfalse );
	}
}

static int vk_color_grade_mode( void )
{
	int mode;

	/* The grade is defined in scene-linear space, so legacy LDR rendering
	 * deliberately retains its established final-pass behavior. */
	if ( !vk_scene_linear_enabled() || !r_colorGrade ) {
		return 0;
	}

	mode = r_colorGrade->integer;
	return Com_Clamp( 0, 3, mode );
}

static qboolean vk_color_grade_uses_lgg( int mode )
{
	return ( mode == 1 || mode == 3 ) ? qtrue : qfalse;
}

static qboolean vk_color_grade_uses_lut( int mode )
{
	return ( mode == 2 || mode == 3 ) ? qtrue : qfalse;
}

static void vk_parse_vec3_cvar( const cvar_t *cvar, float fallback0,
	float fallback1, float fallback2, float minValue, float maxValue,
	float out[3] )
{
	float values[3] = { fallback0, fallback1, fallback2 };

	if ( cvar && cvar->string && cvar->string[0] ) {
		(void)sscanf( cvar->string, "%f %f %f",
			&values[0], &values[1], &values[2] );
	}

	out[0] = Com_Clamp( minValue, maxValue, values[0] );
	out[1] = Com_Clamp( minValue, maxValue, values[1] );
	out[2] = Com_Clamp( minValue, maxValue, values[2] );
}

static void vk_set_identity_3x3( float matrix[9] )
{
	matrix[0] = 1.0f; matrix[1] = 0.0f; matrix[2] = 0.0f;
	matrix[3] = 0.0f; matrix[4] = 1.0f; matrix[5] = 0.0f;
	matrix[6] = 0.0f; matrix[7] = 0.0f; matrix[8] = 1.0f;
}

static void vk_cct_to_xyz( float kelvin, float xyz[3] )
{
	float x, y;
	const float t = Com_Clamp( 1667.0f, 25000.0f, kelvin );
	const float t2 = t * t;
	const float t3 = t2 * t;

	if ( t <= 4000.0f ) {
		x = -0.2661239e9f / t3 - 0.2343580e6f / t2 +
			0.8776956e3f / t + 0.179910f;
	} else {
		x = -3.0258469e9f / t3 + 2.1070379e6f / t2 +
			0.2226347e3f / t + 0.240390f;
	}

	if ( t < 2222.0f ) {
		y = -1.1063814f * x * x * x - 1.34811020f * x * x +
			2.18555832f * x - 0.20219683f;
	} else if ( t < 4000.0f ) {
		y = -0.9549476f * x * x * x - 1.37418593f * x * x +
			2.09137015f * x - 0.16748867f;
	} else {
		y = 3.0817580f * x * x * x - 5.87338670f * x * x +
			3.75112997f * x - 0.37001483f;
	}

	if ( y <= 0.0001f ) {
		xyz[0] = 0.95047f;
		xyz[1] = 1.0f;
		xyz[2] = 1.08883f;
		return;
	}

	xyz[0] = x / y;
	xyz[1] = 1.0f;
	xyz[2] = ( 1.0f - x - y ) / y;
}

static void vk_build_bradford_adaptation( float sourceKelvin,
	float targetKelvin, float matrix[9] )
{
	static const float bradford[9] = {
		 0.8951f,  0.2664f, -0.1614f,
		-0.7502f,  1.7135f,  0.0367f,
		 0.0389f, -0.0685f,  1.0296f
	};
	static const float bradfordInv[9] = {
		 0.9869929f, -0.1470543f,  0.1599627f,
		 0.4323053f,  0.5183603f,  0.0492912f,
		-0.0085287f,  0.0400428f,  0.9684867f
	};
	float src[3], dst[3], srcCone[3], dstCone[3];
	float scale[3], scaledBradford[9];
	int row, col;

	vk_cct_to_xyz( sourceKelvin, src );
	vk_cct_to_xyz( targetKelvin, dst );

	for ( row = 0; row < 3; row++ ) {
		srcCone[row] = bradford[row * 3 + 0] * src[0] +
			bradford[row * 3 + 1] * src[1] +
			bradford[row * 3 + 2] * src[2];
		dstCone[row] = bradford[row * 3 + 0] * dst[0] +
			bradford[row * 3 + 1] * dst[1] +
			bradford[row * 3 + 2] * dst[2];
		scale[row] = fabs( srcCone[row] ) > 0.0001f ?
			dstCone[row] / srcCone[row] : 1.0f;
	}

	for ( row = 0; row < 3; row++ ) {
		for ( col = 0; col < 3; col++ ) {
			scaledBradford[row * 3 + col] =
				scale[row] * bradford[row * 3 + col];
		}
	}

	for ( row = 0; row < 3; row++ ) {
		for ( col = 0; col < 3; col++ ) {
			matrix[row * 3 + col] =
				bradfordInv[row * 3 + 0] * scaledBradford[0 * 3 + col] +
				bradfordInv[row * 3 + 1] * scaledBradford[1 * 3 + col] +
				bradfordInv[row * 3 + 2] * scaledBradford[2 * 3 + col];
		}
	}
}

static qboolean vk_validate_color_grade_lut_atlas( const image_t *image,
	int *size )
{
	int lutSize;

	if ( !image || image->width <= 0 || image->height <= 0 ||
		image->width != image->height * image->height ) {
		return qfalse;
	}

	lutSize = image->height;
	if ( lutSize < 2 || lutSize > 64 ) {
		return qfalse;
	}

	if ( size ) {
		*size = lutSize;
	}
	return qtrue;
}

static image_t *vk_create_identity_color_grade_lut( int *size )
{
	static image_t *identityLut;
	enum { IDENTITY_LUT_SIZE = 16 };
	byte data[ IDENTITY_LUT_SIZE * IDENTITY_LUT_SIZE *
		IDENTITY_LUT_SIZE * 4 ];
	const int width = IDENTITY_LUT_SIZE * IDENTITY_LUT_SIZE;
	int r, g, b;

	if ( identityLut ) {
		if ( size ) {
			*size = IDENTITY_LUT_SIZE;
		}
		return identityLut;
	}

	for ( b = 0; b < IDENTITY_LUT_SIZE; b++ ) {
		for ( g = 0; g < IDENTITY_LUT_SIZE; g++ ) {
			for ( r = 0; r < IDENTITY_LUT_SIZE; r++ ) {
				const int index =
					( g * width + b * IDENTITY_LUT_SIZE + r ) * 4;
				data[index + 0] =
					(byte)( r * 255 / ( IDENTITY_LUT_SIZE - 1 ) );
				data[index + 1] =
					(byte)( g * 255 / ( IDENTITY_LUT_SIZE - 1 ) );
				data[index + 2] =
					(byte)( b * 255 / ( IDENTITY_LUT_SIZE - 1 ) );
				data[index + 3] = 255;
			}
		}
	}

	identityLut = R_CreateImage( "*colorGradeIdentityLUT", NULL, data,
		width, IDENTITY_LUT_SIZE,
		IMGFLAG_CLAMPTOEDGE | IMGFLAG_NO_COMPRESSION |
		IMGFLAG_NOSCALE | IMGFLAG_COLORSPACE_LINEAR );
	if ( size ) {
		*size = IDENTITY_LUT_SIZE;
	}
	return identityLut;
}

static image_t *vk_color_grade_lut_image( int *size )
{
	static image_t *lutImage;
	static int lutSize;
	static int lutModificationCount = -1;
	const int flags = IMGFLAG_CLAMPTOEDGE | IMGFLAG_NO_COMPRESSION |
		IMGFLAG_NOSCALE | IMGFLAG_COLORSPACE_LINEAR;

	if ( !r_colorGradeLUT ||
		r_colorGradeLUT->modificationCount == lutModificationCount ) {
		if ( size ) {
			*size = lutSize;
		}
		return lutImage;
	}

	lutModificationCount = r_colorGradeLUT->modificationCount;
	lutImage = NULL;
	lutSize = 0;

	if ( r_colorGradeLUT->string && r_colorGradeLUT->string[0] ) {
		image_t *loaded = R_FindImageFile( r_colorGradeLUT->string, flags );

		if ( vk_validate_color_grade_lut_atlas( loaded, &lutSize ) ) {
			lutImage = loaded;
		} else {
			ri.Printf( PRINT_WARNING,
				"WARNING: color-grade LUT '%s' must use width N*N and "
				"height N; using identity LUT\n",
				r_colorGradeLUT->string );
		}
	}

	if ( !lutImage ) {
		lutImage = vk_create_identity_color_grade_lut( &lutSize );
	}

	if ( size ) {
		*size = lutSize;
	}
	return lutImage;
}

static VkDescriptorSet vk_color_grade_lut_descriptor( void )
{
	image_t *image;
	int size;

	if ( !vk_color_grade_uses_lut( vk_color_grade_mode() ) ) {
		if ( tr.whiteImage &&
			tr.whiteImage->descriptor != VK_NULL_HANDLE ) {
			return tr.whiteImage->descriptor;
		}
		return vk.color_descriptor;
	}

	image = vk_color_grade_lut_image( &size );
	if ( image && image->descriptor != VK_NULL_HANDLE ) {
		return image->descriptor;
	}
	if ( tr.whiteImage && tr.whiteImage->descriptor != VK_NULL_HANDLE ) {
		return tr.whiteImage->descriptor;
	}
	return vk.color_descriptor;
}

static void vk_bind_gamma_descriptor_sets( void )
{
	VkDescriptorSet post_sets[2];

	post_sets[0] = vk.color_descriptor;
	post_sets[1] = vk_color_grade_lut_descriptor();
	qvkCmdBindDescriptorSets( vk.cmd->command_buffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		vk_pipeline_layout_handle( RTX_VK_PIPELINE_LAYOUT_POST_PROCESS ),
		0, ARRAY_LEN( post_sets ), post_sets, 0, NULL );
}

static void vk_push_post_process_constants( void )
{
	float constants[4] = { 0.0f };
	const float invWidth = glConfig.vidWidth > 0 ?
		1.0f / (float)glConfig.vidWidth : 1.0f;
	const float invHeight = glConfig.vidHeight > 0 ?
		1.0f / (float)glConfig.vidHeight : 1.0f;

	constants[0] = tr.refdef.floatTime > 0.0 ?
		(float)tr.refdef.floatTime : (float)tr.frameCount * ( 1.0f / 60.0f );
	constants[1] = invWidth;
	constants[2] = invHeight;
	qvkCmdPushConstants( vk.cmd->command_buffer,
		vk_pipeline_layout_handle( RTX_VK_PIPELINE_LAYOUT_POST_PROCESS ),
		VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( constants ), constants );
}

void vk_create_blur_pipeline( uint32_t index, uint32_t width, uint32_t height, qboolean horizontal_pass );

void vk_update_post_process_pipelines( void )
{
	vk_set_hdr_metadata();

	if ( vk.fboActive ) {
		// update gamma shader
		vk_create_post_process_pipeline( 0, 0, 0 );
		if ( vk.render_pass.post_bloom != VK_NULL_HANDLE ) {
			vk_create_post_process_pipeline( 6,
				glConfig.vidWidth, glConfig.vidHeight );
		}
		if ( vk.capture.image ) {
			// update capture pipeline
			vk_create_post_process_pipeline( 3, gls.captureWidth, gls.captureHeight );
		}
		if ( vk.render_pass.global_fog != VK_NULL_HANDLE ) {
			vk_create_post_process_pipeline( 4, glConfig.vidWidth, glConfig.vidHeight );
		}
		if ( vk.render_pass.liquid_snapshot != VK_NULL_HANDLE ) {
			vk_create_post_process_pipeline( 5,
				vk.liquidSnapshotWidth, vk.liquidSnapshotHeight );
		}
		if ( vk.render_pass.motion_blur != VK_NULL_HANDLE ) {
			vk_create_post_process_pipeline( 8,
				glConfig.vidWidth, glConfig.vidHeight );
			vk_create_post_process_pipeline( 9,
				glConfig.vidWidth, glConfig.vidHeight );
		}
		if ( r_bloom->integer ) {
			// update bloom shaders
			uint32_t width = gls.captureWidth;
			uint32_t height = gls.captureHeight;
			uint32_t i;

			vk_create_post_process_pipeline( 1, width, height ); // bloom extraction

			for ( i = 0; i < ARRAY_LEN( vk.blur_pipeline ); i += 2 ) {
				width /= 2;
				height /= 2;
				vk_create_blur_pipeline( i + 0, width, height, qtrue ); // horizontal
				vk_create_blur_pipeline( i + 1, width, height, qfalse ); // vertical
			}

			vk_create_post_process_pipeline( 2, glConfig.vidWidth, glConfig.vidHeight ); // bloom blending
			vk_create_post_process_pipeline( 7, glConfig.vidWidth, glConfig.vidHeight ); // bloom blend preserving cel outlines
		}
	}
}


typedef struct vk_attach_desc_s  {
	VkImage descriptor;
	VkImageView *image_view;
	VkImageUsageFlags usage;
	VkMemoryRequirements reqs;
	uint32_t memoryTypeIndex;
	VkDeviceSize  memory_offset;
	// for layout transition:
	VkImageAspectFlags aspect_flags;
	VkImageLayout image_layout;
	VkFormat image_format;
} vk_attach_desc_t;

static vk_attach_desc_t attachments[ MAX_ATTACHMENTS_IN_POOL ];
static uint32_t num_attachments = 0;


static void vk_clear_attachment_pool( void )
{
	num_attachments = 0;
}


static void vk_alloc_attachments( void )
{
	VkImageViewCreateInfo view_desc;
	VkMemoryDedicatedAllocateInfoKHR alloc_info2;
	VkMemoryAllocateInfo alloc_info;
	VkCommandBuffer command_buffer;
	VkDeviceMemory memory;
	VkDeviceSize offset;
	uint32_t memoryTypeBits;
	uint32_t memoryTypeIndex;
	uint32_t i;

	if ( num_attachments == 0 ) {
		return;
	}

	if ( vk.image_memory_count >= ARRAY_LEN( vk.image_memory ) ) {
		ri.Error( ERR_DROP, "vk.image_memory_count == %i", (int)ARRAY_LEN( vk.image_memory ) );
	}

	memoryTypeBits = ~0U;
	offset = 0;

	for ( i = 0; i < num_attachments; i++ ) {
#ifdef MIN_IMAGE_ALIGN
		VkDeviceSize alignment = MAX( attachments[ i ].reqs.alignment, MIN_IMAGE_ALIGN );
#else
		VkDeviceSize alignment = attachments[ i ].reqs.alignment;
#endif
		memoryTypeBits &= attachments[ i ].reqs.memoryTypeBits;
		offset = PAD( offset, alignment );
		attachments[ i ].memory_offset = offset;
		offset += attachments[ i ].reqs.size;
#ifdef _DEBUG
		ri.Printf( PRINT_ALL, S_COLOR_CYAN "[%i] type %i, size %i, align %i\n", i,
			attachments[ i ].reqs.memoryTypeBits,
			(int)attachments[ i ].reqs.size,
			(int)attachments[ i ].reqs.alignment );
#endif
	}

	if ( num_attachments == 1 && attachments[ 0 ].usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT ) {
		// try lazy memory
		memoryTypeIndex = find_memory_type2( memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT, NULL );
		if ( memoryTypeIndex == ~0U ) {
			memoryTypeIndex = find_memory_type( memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
		}
	} else {
		memoryTypeIndex = find_memory_type( memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
	}

#ifdef _DEBUG
	ri.Printf( PRINT_ALL, "memory type bits: %04x\n", memoryTypeBits );
	ri.Printf( PRINT_ALL, "memory type index: %04x\n", memoryTypeIndex );
	ri.Printf( PRINT_ALL, "total size: %i\n", (int)offset );
#endif

	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.pNext = NULL;
	alloc_info.allocationSize = offset;
	alloc_info.memoryTypeIndex = memoryTypeIndex;

	if ( num_attachments == 1 ) {
		if ( vk.dedicatedAllocation ) {
			Com_Memset( &alloc_info2, 0, sizeof( alloc_info2 ) );
			alloc_info2.sType =  VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR;
			alloc_info2.image = attachments[ 0 ].descriptor;
			alloc_info.pNext = &alloc_info2;
		}
	}

	// allocate and bind memory
	VK_CHECK( qvkAllocateMemory( vk.device, &alloc_info, NULL, &memory ) );

	vk.image_memory[ vk.image_memory_count++ ] = memory;

	for ( i = 0; i < num_attachments; i++ ) {

		VK_CHECK( qvkBindImageMemory( vk.device, attachments[i].descriptor, memory, attachments[i].memory_offset ) );

		// create color image view
		view_desc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view_desc.pNext = NULL;
		view_desc.flags = 0;
		view_desc.image = attachments[ i ].descriptor;
		view_desc.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view_desc.format = attachments[ i ].image_format;
		view_desc.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		view_desc.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		view_desc.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		view_desc.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		view_desc.subresourceRange.aspectMask = attachments[ i ].aspect_flags;
		view_desc.subresourceRange.baseMipLevel = 0;
		view_desc.subresourceRange.levelCount = 1;
		view_desc.subresourceRange.baseArrayLayer = 0;
		view_desc.subresourceRange.layerCount = 1;

		VK_CHECK( qvkCreateImageView( vk.device, &view_desc, NULL, attachments[ i ].image_view ) );
	}

	// perform layout transition
	command_buffer = begin_command_buffer();
	for ( i = 0; i < num_attachments; i++ ) {
		record_image_layout_transition( command_buffer,
			attachments[i].descriptor,
			attachments[i].aspect_flags,
			VK_IMAGE_LAYOUT_UNDEFINED, // old_layout
			attachments[i].image_layout,
			0, 0 );
	}
	end_command_buffer( command_buffer, __func__ );

	num_attachments = 0;
}


static void vk_add_attachment_desc( VkImage desc, VkImageView *image_view, VkImageUsageFlags usage, VkMemoryRequirements *reqs, VkFormat image_format, VkImageAspectFlags aspect_flags, VkImageLayout image_layout )
{
	if ( num_attachments >= ARRAY_LEN( attachments ) ) {
		ri.Error( ERR_FATAL, "Attachments array overflow" );
	} else {
		attachments[ num_attachments ].descriptor = desc;
		attachments[ num_attachments ].image_view = image_view;
		attachments[ num_attachments ].usage = usage;
		attachments[ num_attachments ].reqs = *reqs;
		attachments[ num_attachments ].aspect_flags = aspect_flags;
		attachments[ num_attachments ].image_layout = image_layout;
		attachments[ num_attachments ].image_format = image_format;
		attachments[ num_attachments ].memory_offset = 0;
		num_attachments++;
	}
}


static void vk_get_image_memory_erquirements( VkImage image, VkMemoryRequirements *memory_requirements )
{
	if ( vk.dedicatedAllocation ) {
		VkMemoryRequirements2KHR memory_requirements2;
		VkImageMemoryRequirementsInfo2KHR image_requirements2;
		VkMemoryDedicatedRequirementsKHR mem_req2;

		Com_Memset( &mem_req2, 0, sizeof( mem_req2 ) );
		mem_req2.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR;

		image_requirements2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2_KHR;
		image_requirements2.image = image;
		image_requirements2.pNext = NULL;

		Com_Memset( &memory_requirements2, 0, sizeof( memory_requirements2 ) );
		memory_requirements2.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2_KHR;
		memory_requirements2.pNext = &mem_req2;

		qvkGetImageMemoryRequirements2KHR( vk.device, &image_requirements2, &memory_requirements2 );

		*memory_requirements = memory_requirements2.memoryRequirements;
	} else {
		qvkGetImageMemoryRequirements( vk.device, image, memory_requirements );
	}
}


static void create_color_attachment( uint32_t width, uint32_t height, VkSampleCountFlagBits samples, VkFormat format,
	VkImageUsageFlags usage, VkImage *image, VkImageView *image_view, VkImageLayout image_layout, qboolean multisample )
{
	VkImageCreateInfo create_desc;
	VkMemoryRequirements memory_requirements;

	if ( multisample && !( usage & VK_IMAGE_USAGE_SAMPLED_BIT ) )
		usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;

	// create color image
	create_desc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	create_desc.pNext = NULL;
	create_desc.flags = 0;
	create_desc.imageType = VK_IMAGE_TYPE_2D;
	create_desc.format = format;
	create_desc.extent.width = width;
	create_desc.extent.height = height;
	create_desc.extent.depth = 1;
	create_desc.mipLevels = 1;
	create_desc.arrayLayers = 1;
	create_desc.samples = samples;
	create_desc.tiling = VK_IMAGE_TILING_OPTIMAL;
	create_desc.usage = usage;
	create_desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	create_desc.queueFamilyIndexCount = 0;
	create_desc.pQueueFamilyIndices = NULL;
	create_desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VK_CHECK( qvkCreateImage( vk.device, &create_desc, NULL, image ) );

	vk_get_image_memory_erquirements( *image, &memory_requirements );

	vk_add_attachment_desc( *image, image_view, usage, &memory_requirements, format, VK_IMAGE_ASPECT_COLOR_BIT, image_layout );
}


static void create_depth_attachment( uint32_t width, uint32_t height, VkSampleCountFlagBits samples,
	VkImage *image, VkImageView *image_view, qboolean allowTransient, qboolean sampled )
{
	VkImageCreateInfo create_desc;
	VkMemoryRequirements memory_requirements;
	VkImageAspectFlags image_aspect_flags;

	// create depth image
	create_desc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	create_desc.pNext = NULL;
	create_desc.flags = 0;
	create_desc.imageType = VK_IMAGE_TYPE_2D;
	create_desc.format = vk.depth_format;
	create_desc.extent.width = width;
	create_desc.extent.height = height;
	create_desc.extent.depth = 1;
	create_desc.mipLevels = 1;
	create_desc.arrayLayers = 1;
	create_desc.samples = samples;
	create_desc.tiling = VK_IMAGE_TILING_OPTIMAL;
	create_desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	if ( image == &vk.depth_image && vk.fboActive &&
		vk_scene_depth_snapshot_requested() &&
		samples == VK_SAMPLE_COUNT_1_BIT &&
		vk.liquidDepthSampleSupported ) {
		create_desc.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}
	if ( sampled ) {
		create_desc.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
	} else if ( allowTransient ) {
		create_desc.usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
	}
	create_desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	create_desc.queueFamilyIndexCount = 0;
	create_desc.pQueueFamilyIndices = NULL;
	create_desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	image_aspect_flags = VK_IMAGE_ASPECT_DEPTH_BIT;
	if ( glConfig.stencilBits > 0 )
		image_aspect_flags |= VK_IMAGE_ASPECT_STENCIL_BIT;

	VK_CHECK( qvkCreateImage( vk.device, &create_desc, NULL, image ) );

	vk_get_image_memory_erquirements( *image, &memory_requirements );

	vk_add_attachment_desc( *image, image_view, create_desc.usage, &memory_requirements, vk.depth_format, image_aspect_flags, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL );
}


static qboolean vk_depth_format_sampled_supported( void )
{
	VkFormatProperties props;

	if ( vk.depth_format == VK_FORMAT_UNDEFINED ) {
		return qfalse;
	}

	qvkGetPhysicalDeviceFormatProperties( vk.physical_device,
		vk.depth_format, &props );
	return ( props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT ) ?
		qtrue : qfalse;
}


static void vk_invalidate_dlight_shadow_atlas_generation( void )
{
	vk.dlight_shadow_generation++;
	if ( !vk.dlight_shadow_generation ) {
		vk.dlight_shadow_generation++;
	}
}


static void vk_invalidate_spot_shadow_atlas_generation( void )
{
	vk.spot_shadow_generation++;
	if ( !vk.spot_shadow_generation ) {
		vk.spot_shadow_generation++;
	}
}


static void vk_invalidate_csm_shadow_atlas_generation( void )
{
	vk.csm_shadow_generation++;
	if ( !vk.csm_shadow_generation ) {
		vk.csm_shadow_generation++;
	}
}


static void vk_clear_dlight_shadow_atlas_layout( void )
{
	vk.dlight_shadow_atlas_width = 0;
	vk.dlight_shadow_atlas_height = 0;
	vk.dlight_shadow_face_size = 0;
	vk.dlight_shadow_atlas_columns = 0;
	vk.dlight_shadow_atlas_rows = 0;
	vk.dlight_shadow_max_lights = 0;
	vk.dlight_shadow_rendered = qfalse;
	vk_invalidate_dlight_shadow_atlas_generation();
}


static void vk_clear_spot_shadow_atlas_layout( void )
{
	vk.spot_shadow_atlas_width = 0;
	vk.spot_shadow_atlas_height = 0;
	vk.spot_shadow_tile_size = 0;
	vk.spot_shadow_atlas_columns = 0;
	vk.spot_shadow_atlas_rows = 0;
	vk.spot_shadow_max_lights = 0;
	vk.spot_shadow_rendered = qfalse;
	vk_invalidate_spot_shadow_atlas_generation();
}


static void vk_clear_csm_shadow_atlas_layout( void )
{
	vk.csm_shadow_atlas_width = 0;
	vk.csm_shadow_atlas_height = 0;
	vk.csm_shadow_cascade_size = 0;
	vk.csm_shadow_cascade_count = 0;
	vk.csm_shadow_rendered = qfalse;
	vk_invalidate_csm_shadow_atlas_generation();
}


static qboolean vk_dlight_shadow_atlas_layout( dlightShadowAtlasLayout_t *layout )
{
#ifdef USE_PMLIGHT
	qboolean correctnessMode =
		( r_shadowCorrectness && r_shadowCorrectness->integer ) ?
			qtrue : qfalse;

	if ( ( !correctnessMode &&
		( !r_dlightShadows || !r_dlightShadows->integer ||
		  !r_dlightShadowMaxLights || r_dlightShadowMaxLights->integer <= 0 ) ) ||
		!r_dlightMode || !r_dlightMode->integer ||
		!vk_depth_format_sampled_supported() ) {
		return qfalse;
	}

	return R_DlightShadowAtlasLayout( correctnessMode ? 1 : r_dlightShadowMaxLights->integer,
		r_dlightShadowResolution ? r_dlightShadowResolution->integer : 256,
		glConfig.maxTextureSize, layout );
#else
	(void)layout;
	return qfalse;
#endif
}


static qboolean vk_spot_shadow_atlas_layout( spotShadowAtlasLayout_t *layout )
{
#ifdef USE_PMLIGHT
	if ( ( r_shadowCorrectness && r_shadowCorrectness->integer ) ||
		!r_spotShadows || !r_spotShadows->integer ||
		!r_spotShadowMaxLights || r_spotShadowMaxLights->integer <= 0 ||
		!vk_depth_format_sampled_supported() ) {
		return qfalse;
	}

	return R_SpotShadowAtlasLayout( r_spotShadowMaxLights->integer,
		r_spotShadowResolution ? r_spotShadowResolution->integer : 256,
		glConfig.maxTextureSize, layout );
#else
	(void)layout;
	return qfalse;
#endif
}


static qboolean vk_csm_shadow_atlas_layout( csmShadowAtlasLayout_t *layout )
{
#ifdef USE_PMLIGHT
	if ( ( r_shadowCorrectness && r_shadowCorrectness->integer ) ||
		!r_csmShadows || !r_csmShadows->integer ||
		!vk_depth_format_sampled_supported() ) {
		return qfalse;
	}

	return R_CSMShadowAtlasLayout(
		r_csmCascadeCount ? r_csmCascadeCount->integer : CSM_MAX_CASCADES,
		r_csmResolution ? r_csmResolution->integer : 1024,
		glConfig.maxTextureSize, layout );
#else
	(void)layout;
	return qfalse;
#endif
}


static void vk_store_dlight_shadow_atlas_layout(
	const dlightShadowAtlasLayout_t *layout )
{
	vk.dlight_shadow_atlas_width = layout->width;
	vk.dlight_shadow_atlas_height = layout->height;
	vk.dlight_shadow_face_size = layout->faceSize;
	vk.dlight_shadow_atlas_columns = layout->columns;
	vk.dlight_shadow_atlas_rows = layout->rows;
	vk.dlight_shadow_max_lights = layout->maxLights;
	vk_invalidate_dlight_shadow_atlas_generation();
}


static void vk_store_spot_shadow_atlas_layout(
	const spotShadowAtlasLayout_t *layout )
{
	vk.spot_shadow_atlas_width = layout->width;
	vk.spot_shadow_atlas_height = layout->height;
	vk.spot_shadow_tile_size = layout->tileSize;
	vk.spot_shadow_atlas_columns = layout->columns;
	vk.spot_shadow_atlas_rows = layout->rows;
	vk.spot_shadow_max_lights = layout->maxLights;
	vk_invalidate_spot_shadow_atlas_generation();
}


static void vk_store_csm_shadow_atlas_layout(
	const csmShadowAtlasLayout_t *layout )
{
	vk.csm_shadow_atlas_width = layout->width;
	vk.csm_shadow_atlas_height = layout->height;
	vk.csm_shadow_cascade_size = layout->cascadeSize;
	vk.csm_shadow_cascade_count = layout->cascadeCount;
	vk_invalidate_csm_shadow_atlas_generation();
}


static void create_dlight_shadow_attachment( uint32_t width, uint32_t height,
	VkImage *image, VkImageView *image_view )
{
	VkImageCreateInfo create_desc;
	VkMemoryRequirements memory_requirements;

	Com_Memset( &create_desc, 0, sizeof( create_desc ) );
	create_desc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	create_desc.imageType = VK_IMAGE_TYPE_2D;
	create_desc.format = vk.depth_format;
	create_desc.extent.width = width;
	create_desc.extent.height = height;
	create_desc.extent.depth = 1;
	create_desc.mipLevels = 1;
	create_desc.arrayLayers = 1;
	create_desc.samples = VK_SAMPLE_COUNT_1_BIT;
	create_desc.tiling = VK_IMAGE_TILING_OPTIMAL;
	create_desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
		VK_IMAGE_USAGE_SAMPLED_BIT;
	create_desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	create_desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VK_CHECK( qvkCreateImage( vk.device, &create_desc, NULL, image ) );
	vk_get_image_memory_erquirements( *image, &memory_requirements );
	vk_add_attachment_desc( *image, image_view, create_desc.usage,
		&memory_requirements, vk.depth_format, VK_IMAGE_ASPECT_DEPTH_BIT,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
}

static void create_liquid_depth_attachment( uint32_t width, uint32_t height )
{
	VkImageCreateInfo create_desc;
	VkMemoryRequirements memory_requirements;

	Com_Memset( &create_desc, 0, sizeof( create_desc ) );
	create_desc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	create_desc.imageType = VK_IMAGE_TYPE_2D;
	create_desc.format = vk.depth_format;
	create_desc.extent.width = width;
	create_desc.extent.height = height;
	create_desc.extent.depth = 1;
	create_desc.mipLevels = 1;
	create_desc.arrayLayers = 1;
	create_desc.samples = VK_SAMPLE_COUNT_1_BIT;
	create_desc.tiling = VK_IMAGE_TILING_OPTIMAL;
	create_desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
		VK_IMAGE_USAGE_SAMPLED_BIT;
	create_desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	create_desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VK_CHECK( qvkCreateImage( vk.device, &create_desc, NULL,
		&vk.liquidDepth.image ) );
	vk_get_image_memory_erquirements( vk.liquidDepth.image,
		&memory_requirements );
	vk_add_attachment_desc( vk.liquidDepth.image,
		&vk.liquidDepth.image_view, create_desc.usage,
		&memory_requirements, vk.depth_format,
		VK_IMAGE_ASPECT_DEPTH_BIT,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
}

static void vk_create_depth_sample_view( void )
{
	VkImageViewCreateInfo view;

	if ( !vk_global_fog_enabled() || vk.depth_image == VK_NULL_HANDLE ) {
		return;
	}

	Com_Memset( &view, 0, sizeof( view ) );
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.image = vk.depth_image;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = vk.depth_format;
	view.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	view.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	view.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	view.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	view.subresourceRange.baseMipLevel = 0;
	view.subresourceRange.levelCount = 1;
	view.subresourceRange.baseArrayLayer = 0;
	view.subresourceRange.layerCount = 1;

	VK_CHECK( qvkCreateImageView( vk.device, &view, NULL, &vk.depth_sample_image_view ) );
	SET_OBJECT_NAME( vk.depth_sample_image_view, "global fog depth sample view", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT );
}


static void vk_create_attachments( void )
{
	dlightShadowAtlasLayout_t shadowLayout;
	spotShadowAtlasLayout_t spotShadowLayout;
	csmShadowAtlasLayout_t csmShadowLayout;
	uint32_t i;

	vk_clear_attachment_pool();
	vk_clear_dlight_shadow_atlas_layout();
	vk_clear_spot_shadow_atlas_layout();
	vk_clear_csm_shadow_atlas_layout();

	// It looks like resulting performance depends from order you're creating/allocating
	// memory for attachments in vulkan i.e. similar images grouped together will provide best results
	// so [resolve0][resolve1][msaa0][msaa1][depth0][depth1] is most optimal
	// while cases like [resolve0][depth0][color0][...] is the worst

	// TODO: preallocate first image chunk in attachment' memory pool?
	if ( vk.fboActive ) {

		VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

		// bloom
		if ( r_bloom->integer ) {
			uint32_t width = gls.captureWidth;
			uint32_t height = gls.captureHeight;

			create_color_attachment( width, height, VK_SAMPLE_COUNT_1_BIT, vk.bloom_format,
				usage, &vk.bloom_image[0], &vk.bloom_image_view[0], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, qfalse );

			for ( i = 1; i < ARRAY_LEN( vk.bloom_image ); i += 2 ) {
				width /= 2;
				height /= 2;
				create_color_attachment( width, height, VK_SAMPLE_COUNT_1_BIT, vk.bloom_format,
					usage, &vk.bloom_image[i+0], &vk.bloom_image_view[i+0], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, qfalse );

				create_color_attachment( width, height, VK_SAMPLE_COUNT_1_BIT, vk.bloom_format,
					usage, &vk.bloom_image[i+1], &vk.bloom_image_view[i+1], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, qfalse );
			}
		}

		// post-processing/msaa-resolve
		create_color_attachment( glConfig.vidWidth, glConfig.vidHeight, VK_SAMPLE_COUNT_1_BIT, vk.color_format,
			usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			&vk.color_image, &vk.color_image_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, qfalse );
		if ( r_motionBlur && r_motionBlur->integer ) {
			create_color_attachment( glConfig.vidWidth, glConfig.vidHeight,
				VK_SAMPLE_COUNT_1_BIT, vk.color_format, usage,
				&vk.motion_blur_image, &vk.motion_blur_image_view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, qfalse );
		}

		if ( vk_liquid_requested() &&
			vk.liquidSnapshotWidth > 0 && vk.liquidSnapshotHeight > 0 ) {
			create_color_attachment( vk.liquidSnapshotWidth,
				vk.liquidSnapshotHeight, VK_SAMPLE_COUNT_1_BIT,
				vk.color_format, usage,
				&vk.liquidSnapshot.color_image,
				&vk.liquidSnapshot.color_image_view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, qfalse );
		}

		// screenmap-msaa
		if ( vk.screenMapSamples > VK_SAMPLE_COUNT_1_BIT ) {
			create_color_attachment( vk.screenMapWidth, vk.screenMapHeight, vk.screenMapSamples, vk.color_format,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &vk.screenMap.color_image_msaa, &vk.screenMap.color_image_view_msaa, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, qtrue );
		}

		// screenmap/msaa-resolve
		create_color_attachment( vk.screenMapWidth, vk.screenMapHeight, VK_SAMPLE_COUNT_1_BIT, vk.color_format,
			usage, &vk.screenMap.color_image, &vk.screenMap.color_image_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, qfalse );

		// screenmap depth
		create_depth_attachment( vk.screenMapWidth, vk.screenMapHeight, vk.screenMapSamples,
			&vk.screenMap.depth_image, &vk.screenMap.depth_image_view, qtrue, qfalse );

		if ( vk.msaaActive ) {
			create_color_attachment( glConfig.vidWidth, glConfig.vidHeight, vkSamples, vk.color_format,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &vk.msaa_image, &vk.msaa_image_view, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, qtrue );
		}

		/*
		 * Always retain an SDR capture target so screenshots, levelshots, and
		 * video frames observe the same final gamma/exposure/tone transform as
		 * presentation, including when supersampling is disabled.
		 */
		usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		create_color_attachment( gls.captureWidth, gls.captureHeight, VK_SAMPLE_COUNT_1_BIT, vk.capture_format,
			usage, &vk.capture.image, &vk.capture.image_view, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, qfalse );
	} // if ( vk.fboActive )

	//vk_alloc_attachments();

	create_depth_attachment( glConfig.vidWidth, glConfig.vidHeight, vkSamples,
		&vk.depth_image, &vk.depth_image_view,
		( vk.fboActive && ( r_bloom->integer || vk_global_fog_enabled() ||
			vk_scene_depth_snapshot_requested() ) ) ? qfalse : qtrue,
		vk_global_fog_enabled() );
	if ( vk.fboActive && vk_scene_depth_snapshot_requested() &&
		vkSamples == VK_SAMPLE_COUNT_1_BIT &&
		vk.liquidDepthSampleSupported ) {
		create_liquid_depth_attachment( glConfig.vidWidth,
			glConfig.vidHeight );
	}

	if ( vk_dlight_shadow_atlas_layout( &shadowLayout ) ) {
		create_dlight_shadow_attachment( shadowLayout.width,
			shadowLayout.height, &vk.dlight_shadow_image,
			&vk.dlight_shadow_image_view );
		vk_store_dlight_shadow_atlas_layout( &shadowLayout );
		ri.Printf( PRINT_ALL,
			"...dynamic-light shadow atlas %ix%i (%i px faces, %i lights)\n",
			shadowLayout.width, shadowLayout.height, shadowLayout.faceSize,
			shadowLayout.maxLights );
	}

	if ( vk_spot_shadow_atlas_layout( &spotShadowLayout ) ) {
		create_dlight_shadow_attachment( spotShadowLayout.width,
			spotShadowLayout.height, &vk.spot_shadow_image,
			&vk.spot_shadow_image_view );
		vk_store_spot_shadow_atlas_layout( &spotShadowLayout );
		ri.Printf( PRINT_ALL,
			"...spotlight shadow atlas %ix%i (%i px tiles, %i lights)\n",
			spotShadowLayout.width, spotShadowLayout.height,
			spotShadowLayout.tileSize, spotShadowLayout.maxLights );
	}

	if ( vk_csm_shadow_atlas_layout( &csmShadowLayout ) ) {
		create_dlight_shadow_attachment( csmShadowLayout.width,
			csmShadowLayout.height, &vk.csm_shadow_image,
			&vk.csm_shadow_image_view );
		vk_store_csm_shadow_atlas_layout( &csmShadowLayout );
		ri.Printf( PRINT_ALL,
			"...sky-sun shadow atlas %ix%i (%i px cascades, %i cascades)\n",
			csmShadowLayout.width, csmShadowLayout.height,
			csmShadowLayout.cascadeSize, csmShadowLayout.cascadeCount );
	}

	vk_alloc_attachments();
	vk_create_depth_sample_view();

	for ( i = 0; i < vk.image_memory_count; i++ )
	{
		SET_OBJECT_NAME( vk.image_memory[i], va( "framebuffer memory chunk %i", i ), VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT );
	}

	SET_OBJECT_NAME( vk.depth_image, "depth attachment", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT );
	SET_OBJECT_NAME( vk.depth_image_view, "depth attachment", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT );
	SET_OBJECT_NAME( vk.dlight_shadow_image, "dlight shadow atlas", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT );
	SET_OBJECT_NAME( vk.dlight_shadow_image_view, "dlight shadow atlas", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT );
	SET_OBJECT_NAME( vk.spot_shadow_image, "spot shadow atlas", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT );
	SET_OBJECT_NAME( vk.spot_shadow_image_view, "spot shadow atlas", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT );
	SET_OBJECT_NAME( vk.csm_shadow_image, "csm shadow atlas", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT );
	SET_OBJECT_NAME( vk.csm_shadow_image_view, "csm shadow atlas", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT );

	SET_OBJECT_NAME( vk.color_image, "color attachment", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT );
	SET_OBJECT_NAME( vk.color_image_view, "color attachment", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT );
	SET_OBJECT_NAME( vk.motion_blur_image, "motion blur scratch attachment", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT );
	SET_OBJECT_NAME( vk.motion_blur_image_view, "motion blur scratch attachment", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT );
	SET_OBJECT_NAME( vk.liquidSnapshot.color_image, "liquid snapshot attachment", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT );
	SET_OBJECT_NAME( vk.liquidSnapshot.color_image_view, "liquid snapshot attachment", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT );
	SET_OBJECT_NAME( vk.liquidDepth.image, "liquid depth snapshot", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT );
	SET_OBJECT_NAME( vk.liquidDepth.image_view, "liquid depth snapshot", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT );

	SET_OBJECT_NAME( vk.capture.image, "capture image", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT );
	SET_OBJECT_NAME( vk.capture.image_view, "capture image view", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT );

	for ( i = 0; i < ARRAY_LEN( vk.bloom_image ); i++ )
	{
		SET_OBJECT_NAME( vk.bloom_image[i], va( "bloom attachment %i", i ), VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT );
		SET_OBJECT_NAME( vk.bloom_image_view[i], va( "bloom attachment %i", i ), VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT );
	}

	vk_lifetime_mark_create( RTX_VK_RESOURCE_ATTACHMENTS, __func__ );
}


static void vk_create_framebuffers( void )
{
	VkImageView framebufferAttachments[3];
	VkFramebufferCreateInfo desc;
	uint32_t n;

	desc.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.pAttachments = framebufferAttachments;
	desc.layers = 1;

	for ( n = 0; n < vk.swapchain_image_count; n++ )
	{
		desc.renderPass = vk.render_pass.main;
		desc.attachmentCount = 2;
		if ( r_fbo->integer == 0 )
		{
			desc.width = gls.windowWidth;
			desc.height = gls.windowHeight;
			framebufferAttachments[0] = vk.swapchain_image_views[n];
			framebufferAttachments[1] = vk.depth_image_view;
			VK_CHECK( qvkCreateFramebuffer( vk.device, &desc, NULL, &vk.framebuffers.main[n] ) );

			SET_OBJECT_NAME( vk.framebuffers.main[n], va( "framebuffer - main %i", n ), VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT );
		}
		else
		{
			// same framebuffer configuration for main and post-bloom render passes
			if ( n == 0 )
			{
				desc.width = glConfig.vidWidth;
				desc.height = glConfig.vidHeight;
				framebufferAttachments[0] = vk.color_image_view;
				framebufferAttachments[1] = vk.depth_image_view;
				if ( vk.msaaActive )
				{
					desc.attachmentCount = 3;
					framebufferAttachments[2] = vk.msaa_image_view;
				}
				VK_CHECK( qvkCreateFramebuffer( vk.device, &desc, NULL, &vk.framebuffers.main[n] ) );
				SET_OBJECT_NAME( vk.framebuffers.main[n], "framebuffer - main", VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT );
			}
			else
			{
				vk.framebuffers.main[n] = vk.framebuffers.main[0];
			}

			// gamma correction
			desc.renderPass = vk.render_pass.gamma;
			desc.attachmentCount = 1;
			desc.width = gls.windowWidth;
			desc.height = gls.windowHeight;
			framebufferAttachments[0] = vk.swapchain_image_views[n];
			VK_CHECK( qvkCreateFramebuffer( vk.device, &desc, NULL, &vk.framebuffers.gamma[n] ) );

			SET_OBJECT_NAME( vk.framebuffers.gamma[n], "framebuffer - gamma-correction", VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT );
		}
	}

	if ( vk.render_pass.motion_blur != VK_NULL_HANDLE &&
		vk.motion_blur_image_view != VK_NULL_HANDLE ) {
		desc.renderPass = vk.render_pass.motion_blur;
		desc.attachmentCount = 1;
		desc.width = glConfig.vidWidth;
		desc.height = glConfig.vidHeight;
		framebufferAttachments[0] = vk.motion_blur_image_view;
		VK_CHECK( qvkCreateFramebuffer( vk.device, &desc, NULL,
			&vk.framebuffers.motion_blur ) );
		SET_OBJECT_NAME( vk.framebuffers.motion_blur,
			"framebuffer - motion blur scratch",
			VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT );
	}

	if ( vk.render_pass.dlight_shadow != VK_NULL_HANDLE &&
		vk.dlight_shadow_image_view != VK_NULL_HANDLE ) {
		desc.renderPass = vk.render_pass.dlight_shadow;
		desc.attachmentCount = 1;
		desc.width = vk.dlight_shadow_atlas_width;
		desc.height = vk.dlight_shadow_atlas_height;
		framebufferAttachments[0] = vk.dlight_shadow_image_view;
		VK_CHECK( qvkCreateFramebuffer( vk.device, &desc, NULL,
			&vk.framebuffers.dlight_shadow ) );
		SET_OBJECT_NAME( vk.framebuffers.dlight_shadow,
			"framebuffer - dlight shadow atlas",
			VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT );
	}

	if ( vk.render_pass.dlight_shadow != VK_NULL_HANDLE &&
		vk.spot_shadow_image_view != VK_NULL_HANDLE ) {
		desc.renderPass = vk.render_pass.dlight_shadow;
		desc.attachmentCount = 1;
		desc.width = vk.spot_shadow_atlas_width;
		desc.height = vk.spot_shadow_atlas_height;
		framebufferAttachments[0] = vk.spot_shadow_image_view;
		VK_CHECK( qvkCreateFramebuffer( vk.device, &desc, NULL,
			&vk.framebuffers.spot_shadow ) );
		SET_OBJECT_NAME( vk.framebuffers.spot_shadow,
			"framebuffer - spot shadow atlas",
			VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT );
	}

	if ( vk.render_pass.dlight_shadow != VK_NULL_HANDLE &&
		vk.csm_shadow_image_view != VK_NULL_HANDLE ) {
		desc.renderPass = vk.render_pass.dlight_shadow;
		desc.attachmentCount = 1;
		desc.width = vk.csm_shadow_atlas_width;
		desc.height = vk.csm_shadow_atlas_height;
		framebufferAttachments[0] = vk.csm_shadow_image_view;
		VK_CHECK( qvkCreateFramebuffer( vk.device, &desc, NULL,
			&vk.framebuffers.csm_shadow ) );
		SET_OBJECT_NAME( vk.framebuffers.csm_shadow,
			"framebuffer - csm shadow atlas",
			VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT );
	}

	if ( vk.fboActive )
	{
		if ( vk.render_pass.global_fog != VK_NULL_HANDLE ) {
			desc.renderPass = vk.render_pass.global_fog;
			desc.attachmentCount = 1;
			desc.width = glConfig.vidWidth;
			desc.height = glConfig.vidHeight;
			framebufferAttachments[0] = vk.color_image_view;
			VK_CHECK( qvkCreateFramebuffer( vk.device, &desc, NULL, &vk.framebuffers.global_fog ) );
			SET_OBJECT_NAME( vk.framebuffers.global_fog, "framebuffer - global fog", VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT );
		}

		if ( vk.render_pass.liquid_snapshot != VK_NULL_HANDLE &&
			vk.liquidSnapshot.color_image_view != VK_NULL_HANDLE ) {
			desc.renderPass = vk.render_pass.liquid_snapshot;
			desc.attachmentCount = 1;
			desc.width = vk.liquidSnapshotWidth;
			desc.height = vk.liquidSnapshotHeight;
			framebufferAttachments[0] = vk.liquidSnapshot.color_image_view;
			VK_CHECK( qvkCreateFramebuffer( vk.device, &desc, NULL,
				&vk.framebuffers.liquid_snapshot ) );
			SET_OBJECT_NAME( vk.framebuffers.liquid_snapshot,
				"framebuffer - liquid snapshot",
				VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT );
		}

		// screenmap
		desc.renderPass = vk.render_pass.screenmap;
		desc.attachmentCount = 2;
		desc.width = vk.screenMapWidth;
		desc.height = vk.screenMapHeight;
		framebufferAttachments[0] = vk.screenMap.color_image_view;
		framebufferAttachments[1] = vk.screenMap.depth_image_view;
		if ( vk.screenMapSamples > VK_SAMPLE_COUNT_1_BIT )
		{
			desc.attachmentCount = 3;
			framebufferAttachments[2] = vk.screenMap.color_image_view_msaa;
		}
		VK_CHECK( qvkCreateFramebuffer( vk.device, &desc, NULL, &vk.framebuffers.screenmap ) );
		SET_OBJECT_NAME( vk.framebuffers.screenmap, "framebuffer - screenmap", VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT );

		if ( vk.capture.image != VK_NULL_HANDLE )
		{
			framebufferAttachments[0] = vk.capture.image_view;

			desc.renderPass = vk.render_pass.capture;
			desc.pAttachments = framebufferAttachments;
			desc.attachmentCount = 1;
			desc.width = gls.captureWidth;
			desc.height = gls.captureHeight;

			VK_CHECK( qvkCreateFramebuffer( vk.device, &desc, NULL, &vk.framebuffers.capture ) );
			SET_OBJECT_NAME( vk.framebuffers.capture, "framebuffer - capture", VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT );
		}

		if ( r_bloom->integer )
		{
			uint32_t width = gls.captureWidth;
			uint32_t height = gls.captureHeight;

			// bloom color extraction
			desc.renderPass = vk.render_pass.bloom_extract;
			desc.width = width;
			desc.height = height;

			desc.attachmentCount = 1;
			framebufferAttachments[0] = vk.bloom_image_view[0];

			VK_CHECK( qvkCreateFramebuffer( vk.device, &desc, NULL, &vk.framebuffers.bloom_extract ) );

			SET_OBJECT_NAME( vk.framebuffers.bloom_extract, "framebuffer - bloom extraction", VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT );

			for ( n = 0; n < ARRAY_LEN( vk.framebuffers.blur ); n += 2 )
			{
				width /= 2;
				height /= 2;

				desc.renderPass = vk.render_pass.blur[n];
				desc.width = width;
				desc.height = height;

				desc.attachmentCount = 1;

				framebufferAttachments[0] = vk.bloom_image_view[n+0+1];
				VK_CHECK( qvkCreateFramebuffer( vk.device, &desc, NULL, &vk.framebuffers.blur[n+0] ) );

				framebufferAttachments[0] = vk.bloom_image_view[n+1+1];
				VK_CHECK( qvkCreateFramebuffer( vk.device, &desc, NULL, &vk.framebuffers.blur[n+1] ) );

				SET_OBJECT_NAME( vk.framebuffers.blur[n+0], va( "framebuffer - blur %i", n+0 ), VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT );
				SET_OBJECT_NAME( vk.framebuffers.blur[n+1], va( "framebuffer - blur %i", n+1 ), VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT );
			}
		}
	}

	vk_lifetime_mark_create( RTX_VK_RESOURCE_FRAMEBUFFERS, __func__ );
}


static void vk_create_sync_primitives( void ) {
	VkSemaphoreCreateInfo desc;
	VkFenceCreateInfo fence_desc;
	uint32_t i;

	desc.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = 0;

#ifdef USE_UPLOAD_QUEUE
	VK_CHECK( qvkCreateSemaphore( vk.device, &desc, NULL, &vk.image_uploaded2 ) );
#endif

	// all commands submitted
	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ )
	{
		desc.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		desc.pNext = NULL;
		desc.flags = 0;

		// swapchain image acquired
		VK_CHECK( qvkCreateSemaphore( vk.device, &desc, NULL, &vk.tess[i].image_acquired ) );

#ifdef USE_UPLOAD_QUEUE
		// second semaphore to synchronize additional tasks (e.g. image upload)
		VK_CHECK( qvkCreateSemaphore( vk.device, &desc, NULL, &vk.tess[i].rendering_finished2 ) );
#endif
		fence_desc.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fence_desc.pNext = NULL;
		//fence_desc.flags = VK_FENCE_CREATE_SIGNALED_BIT; // so it can be used to start rendering
		fence_desc.flags = 0; // non-signalled state

		VK_CHECK( qvkCreateFence( vk.device, &fence_desc, NULL, &vk.tess[i].rendering_finished_fence ) );
		vk.tess[i].waitForFence = qfalse;

		SET_OBJECT_NAME( vk.tess[i].image_acquired, va( "image_acquired semaphore %i", i ), VK_DEBUG_REPORT_OBJECT_TYPE_SEMAPHORE_EXT );
#ifdef USE_UPLOAD_QUEUE
		SET_OBJECT_NAME( vk.tess[i].rendering_finished2, va( "rendering_finished2 semaphore %i", i ), VK_DEBUG_REPORT_OBJECT_TYPE_SEMAPHORE_EXT );
#endif
		SET_OBJECT_NAME( vk.tess[i].rendering_finished_fence, va( "rendering_finished fence %i", i ), VK_DEBUG_REPORT_OBJECT_TYPE_FENCE_EXT );
	}

	fence_desc.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fence_desc.pNext = NULL;
	fence_desc.flags = 0;

#ifdef USE_UPLOAD_QUEUE
	VK_CHECK( qvkCreateFence( vk.device, &fence_desc, NULL, &vk.aux_fence ) );
	SET_OBJECT_NAME( vk.aux_fence, "aux fence", VK_DEBUG_REPORT_OBJECT_TYPE_FENCE_EXT );

	vk.rendering_finished = VK_NULL_HANDLE;
	vk.image_uploaded = VK_NULL_HANDLE;
	vk.aux_fence_wait = qfalse;
#endif

	vk_lifetime_mark_create( RTX_VK_RESOURCE_SYNC_PRIMITIVES, __func__ );
}


static void vk_destroy_sync_primitives( void  ) {
	uint32_t i;

#ifdef USE_UPLOAD_QUEUE
	qvkDestroySemaphore( vk.device, vk.image_uploaded2, NULL );
#endif

	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		qvkDestroySemaphore( vk.device, vk.tess[i].image_acquired, NULL );
#ifdef USE_UPLOAD_QUEUE
		qvkDestroySemaphore( vk.device, vk.tess[i].rendering_finished2, NULL );
#endif
		qvkDestroyFence( vk.device, vk.tess[i].rendering_finished_fence, NULL );
		vk.tess[i].waitForFence = qfalse;
		vk.tess[i].swapchain_image_acquired = qfalse;
	}

#ifdef USE_UPLOAD_QUEUE
	qvkDestroyFence( vk.device, vk.aux_fence, NULL );

	vk.rendering_finished = VK_NULL_HANDLE;
	vk.image_uploaded = VK_NULL_HANDLE;
#endif

	vk_lifetime_mark_destroy( RTX_VK_RESOURCE_SYNC_PRIMITIVES, __func__ );
}


static void vk_destroy_framebuffers( void ) {
	uint32_t n;

	for ( n = 0; n < vk.swapchain_image_count; n++ ) {
		if ( vk.framebuffers.main[n] != VK_NULL_HANDLE ) {
			if ( !vk.fboActive || n == 0 ) {
				qvkDestroyFramebuffer( vk.device, vk.framebuffers.main[n], NULL );
			}
			vk.framebuffers.main[n] = VK_NULL_HANDLE;
		}
		if ( vk.framebuffers.gamma[n] != VK_NULL_HANDLE ) {
			qvkDestroyFramebuffer( vk.device, vk.framebuffers.gamma[n], NULL );
			vk.framebuffers.gamma[n] = VK_NULL_HANDLE;
		}
	}

	if ( vk.framebuffers.bloom_extract != VK_NULL_HANDLE ) {
		qvkDestroyFramebuffer( vk.device, vk.framebuffers.bloom_extract, NULL );
		vk.framebuffers.bloom_extract = VK_NULL_HANDLE;
	}

	if ( vk.framebuffers.screenmap != VK_NULL_HANDLE ) {
		qvkDestroyFramebuffer( vk.device, vk.framebuffers.screenmap, NULL );
		vk.framebuffers.screenmap = VK_NULL_HANDLE;
	}

	if ( vk.framebuffers.capture != VK_NULL_HANDLE ) {
		qvkDestroyFramebuffer( vk.device, vk.framebuffers.capture, NULL );
		vk.framebuffers.capture = VK_NULL_HANDLE;
	}

	if ( vk.framebuffers.motion_blur != VK_NULL_HANDLE ) {
		qvkDestroyFramebuffer( vk.device, vk.framebuffers.motion_blur, NULL );
		vk.framebuffers.motion_blur = VK_NULL_HANDLE;
	}

	if ( vk.framebuffers.global_fog != VK_NULL_HANDLE ) {
		qvkDestroyFramebuffer( vk.device, vk.framebuffers.global_fog, NULL );
		vk.framebuffers.global_fog = VK_NULL_HANDLE;
	}
	if ( vk.framebuffers.liquid_snapshot != VK_NULL_HANDLE ) {
		qvkDestroyFramebuffer( vk.device,
			vk.framebuffers.liquid_snapshot, NULL );
		vk.framebuffers.liquid_snapshot = VK_NULL_HANDLE;
	}

	if ( vk.framebuffers.dlight_shadow != VK_NULL_HANDLE ) {
		qvkDestroyFramebuffer( vk.device, vk.framebuffers.dlight_shadow, NULL );
		vk.framebuffers.dlight_shadow = VK_NULL_HANDLE;
	}
	if ( vk.framebuffers.spot_shadow != VK_NULL_HANDLE ) {
		qvkDestroyFramebuffer( vk.device, vk.framebuffers.spot_shadow, NULL );
		vk.framebuffers.spot_shadow = VK_NULL_HANDLE;
	}
	if ( vk.framebuffers.csm_shadow != VK_NULL_HANDLE ) {
		qvkDestroyFramebuffer( vk.device, vk.framebuffers.csm_shadow, NULL );
		vk.framebuffers.csm_shadow = VK_NULL_HANDLE;
	}

	for ( n = 0; n < ARRAY_LEN( vk.framebuffers.blur ); n++ ) {
		if ( vk.framebuffers.blur[n] != VK_NULL_HANDLE ) {
			qvkDestroyFramebuffer( vk.device, vk.framebuffers.blur[n], NULL );
			vk.framebuffers.blur[n] = VK_NULL_HANDLE;
		}
	}

	vk_lifetime_mark_destroy( RTX_VK_RESOURCE_FRAMEBUFFERS, __func__ );
}


static void vk_destroy_swapchain( void ) {
	uint32_t i;

	for ( i = 0; i < ARRAY_LEN( vk.swapchain_image_views ); i++ ) {
		if ( vk.swapchain_image_views[i] != VK_NULL_HANDLE ) {
			qvkDestroyImageView( vk.device, vk.swapchain_image_views[i], NULL );
			vk.swapchain_image_views[i] = VK_NULL_HANDLE;
		}
		if ( vk.swapchain_rendering_finished[i] != VK_NULL_HANDLE ) {
			qvkDestroySemaphore( vk.device, vk.swapchain_rendering_finished[i], NULL );
			vk.swapchain_rendering_finished[i] = VK_NULL_HANDLE;
		}
		vk.swapchain_images[i] = VK_NULL_HANDLE;
	}

	if ( vk.swapchain != VK_NULL_HANDLE ) {
		qvkDestroySwapchainKHR( vk.device, vk.swapchain, NULL );
		vk.swapchain = VK_NULL_HANDLE;
	}
	vk.swapchain_image_count = 0;
	vk_lifetime_mark_destroy( RTX_VK_RESOURCE_SWAPCHAIN, __func__ );
}

static void vk_destroy_attachments( void );
static void vk_destroy_render_passes( void );
static void vk_destroy_pipelines( qboolean resetCount );
static void vk_set_render_scale( void );

static qboolean vk_restart_swapchain( const char *funcname, VkResult res )
{
	uint32_t i;

	if ( s_vkSwapchainResizeRestarting ) {
		vk_schedule_swapchain_resize( funcname, res, "restart already in progress" );
		return qfalse;
	}

	if ( !vk_swapchain_can_restart() ) {
		vk_schedule_swapchain_resize( funcname, res, "window is minimized or has zero extent" );
		return qfalse;
	}

#ifdef _DEBUG
	ri.Printf( PRINT_WARNING, "%s(%s): restarting swapchain...\n", funcname, vk_result_string( res ) );
#else
	ri.Printf(PRINT_WARNING, "%s(): restarting swapchain...\n", funcname );
#endif

	if ( !vk_select_surface_format( vk.physical_device, vk_surface ) ) {
		vk_schedule_swapchain_resize( funcname, res, "surface format query failed" );
		return qfalse;
	}
	setup_surface_formats( vk.physical_device );

	s_vkSwapchainResizeRestarting = qtrue;
	vk_clear_scheduled_swapchain_resize();

	vk_lifetime_begin_resize( funcname );
	vk_wait_idle();

	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		qvkResetCommandBuffer( vk.tess[i].command_buffer, 0 );
	}

#ifdef USE_UPLOAD_QUEUE
	qvkResetCommandBuffer( vk.staging_command_buffer, 0 );
#endif

	vk_destroy_pipelines( qfalse );
	vk_destroy_framebuffers();
	vk_destroy_render_passes();
	vk_destroy_attachments();
	vk_destroy_swapchain();
	vk_destroy_sync_primitives();

	vk.blitFilter = GL_NEAREST;
	vk.windowAdjusted = qfalse;
	vk.blitX0 = vk.blitY0 = 0;
	vk_set_render_scale();

	vk_create_sync_primitives();
	vk_create_swapchain( vk.physical_device, vk.device, vk_surface, vk.present_format, &vk.swapchain, qfalse );
	vk_create_attachments();
	vk_create_render_passes();
	vk_create_framebuffers();

	vk_update_attachment_descriptors();

	vk_update_post_process_pipelines();
	vk_lifetime_end_resize( funcname );
	s_vkSwapchainResizeRestarting = qfalse;
	return qtrue;
}


static void vk_set_render_scale( void )
{
	if ( gls.windowWidth != glConfig.vidWidth || gls.windowHeight != glConfig.vidHeight )
	{
		if ( r_renderScale->integer > 0 )
		{
			int scaleMode = r_renderScale->integer - 1;
			if ( scaleMode & 1 )
			{
				// preserve aspect ratio (black bars on sides)
				float windowAspect = (float) gls.windowWidth / (float) gls.windowHeight;
				float renderAspect = (float) glConfig.vidWidth / (float) glConfig.vidHeight;
				if ( windowAspect >= renderAspect )
				{
					float scale = (float)gls.windowHeight / ( float ) glConfig.vidHeight;
					int bias = ( gls.windowWidth - scale * (float) glConfig.vidWidth ) / 2;
					vk.blitX0 += bias;
				}
				else
				{
					float scale = (float)gls.windowWidth / ( float ) glConfig.vidWidth;
					int bias = ( gls.windowHeight - scale * (float) glConfig.vidHeight ) / 2;
					vk.blitY0 += bias;
				}
			}
			// linear filtering
			if ( scaleMode & 2 )
				vk.blitFilter = GL_LINEAR;
			else
				vk.blitFilter = GL_NEAREST;
		}

		vk.windowAdjusted = qtrue;
	}

	if ( r_fbo->integer && r_ext_supersample->integer && !r_renderScale->integer )
	{
		vk.blitFilter = GL_LINEAR;
	}
}


void vk_initialize( void )
{
	char buf[64], driver_version[64];
	const char *vendor_name;
	VkPhysicalDeviceProperties props;
	uint32_t major;
	uint32_t minor;
	uint32_t patch;
	uint32_t maxSize;
	uint32_t i;

	vk_lifetime_begin_init();
	init_vulkan_library();

	qvkGetDeviceQueue( vk.device, vk.queue_family_index, 0, &vk.queue );
	vk.async_queue = VK_NULL_HANDLE;
	vk.async_queue_available = qfalse;
	if ( vk_rt_async_overlap_enabled() && vk.queue_family_queue_count > 1 ) {
		qvkGetDeviceQueue( vk.device, vk.queue_family_index, 1, &vk.async_queue );
		if ( vk.async_queue != VK_NULL_HANDLE ) {
			vk.async_queue_available = qtrue;
			ri.Printf( PRINT_ALL, "RTX RT: async queue overlap enabled (family=%u queues=%u)\n",
				vk.queue_family_index, vk.queue_family_queue_count );
		}
	}

	qvkGetPhysicalDeviceProperties( vk.physical_device, &props );

	vk.cmd = vk.tess + 0;
	vk.uniform_alignment = props.limits.minUniformBufferOffsetAlignment;
	{
		const size_t alignment = (size_t)vk.uniform_alignment;
		vk.uniform_item_size = (uint32_t)( ( sizeof( vkUniform_t ) + alignment - 1 ) & ~( alignment - 1 ) );
	}

	// for flare visibility tests
	vk.storage_alignment = MAX( props.limits.minStorageBufferOffsetAlignment, sizeof( uint32_t ) );

	vk.maxAnisotropy = props.limits.maxSamplerAnisotropy;

	vk.blitFilter = GL_NEAREST;
	vk.windowAdjusted = qfalse;
	vk.blitX0 = vk.blitY0 = 0;

	vk_set_render_scale();

	if ( r_fbo->integer ) {
		const qboolean rtSingleSampleComposition =
			( vk.caps.activeRtMode == RTX_RT_MODE_RAY_TRACING_PIPELINE ) ?
				qtrue : qfalse;
		const qboolean depthFadeSingleSampleComposition =
			( vk_depth_fade_requested() &&
			  vk.liquidDepthSampleSupported &&
			  props.limits.maxBoundDescriptorSets > VK_DESC_DEPTH_FADE ) ?
				qtrue : qfalse;
		const qboolean singleSampleComposition =
			( rtSingleSampleComposition ||
			  depthFadeSingleSampleComposition ) ? qtrue : qfalse;

		vk.fboActive = qtrue;
		if ( r_ext_multisample->integer && !singleSampleComposition ) {
			vk.msaaActive = qtrue;
		} else if ( r_ext_multisample->integer && singleSampleComposition ) {
			if ( rtSingleSampleComposition ) {
				ri.Printf( PRINT_WARNING,
					"RTX RT: disabling raster MSAA; temporal/spatial RT reconstruction supplies antialiasing and the post-RT overlay is single-sample\n" );
			} else {
				ri.Printf( PRINT_WARNING,
					"RTX: disabling raster MSAA so soft-particle depth fade can sample the active scene depth; set r_depthFade 0 and vid_restart to retain MSAA\n" );
			}
		}
	} else {
		vk.fboActive = qfalse;
	}

	// multisampling

	vkMaxSamples = MIN( props.limits.sampledImageColorSampleCounts, props.limits.sampledImageDepthSampleCounts );

	if ( /*vk.fboActive &&*/ vk.msaaActive ) {
		VkSampleCountFlags mask = vkMaxSamples;
		vkSamples = MAX( log2pad( r_ext_multisample->integer, 1 ), VK_SAMPLE_COUNT_2_BIT );
		while ( vkSamples > mask )
				vkSamples >>= 1;
		ri.Printf( PRINT_ALL, "...using %ix MSAA\n", vkSamples );
	} else {
		vkSamples = VK_SAMPLE_COUNT_1_BIT;
	}

	if ( vk_global_fog_requested() && vkSamples != VK_SAMPLE_COUNT_1_BIT ) {
		ri.Printf( PRINT_WARNING,
			"RTX: per-map global fog is unavailable with raster MSAA enabled; disable r_ext_multisample or use RT pipeline mode 2\n" );
	}
	if ( vk_depth_fade_requested() &&
		vkSamples != VK_SAMPLE_COUNT_1_BIT ) {
		ri.Printf( PRINT_WARNING,
			"RTX: soft-particle depth fade is unavailable with raster MSAA enabled; authored particle blending remains active\n" );
	}
	if ( vk_liquid_requested() && vkSamples != VK_SAMPLE_COUNT_1_BIT ) {
		ri.Printf( PRINT_WARNING,
			"RTX: enhanced liquids keep color refraction/reflection with raster MSAA, but foreground depth rejection requires single-sample composition (or RT pipeline mode 2)\n" );
	}

	vk.screenMapSamples = MIN( vkMaxSamples, VK_SAMPLE_COUNT_4_BIT );

	vk.screenMapWidth = (float) glConfig.vidWidth / 16.0;
	if ( vk.screenMapWidth < 4 )
		vk.screenMapWidth = 4;

	vk.screenMapHeight = (float) glConfig.vidHeight / 16.0;
	if ( vk.screenMapHeight < 4 )
		vk.screenMapHeight = 4;

	vk.liquidSnapshotWidth = vk.liquidSnapshotHeight = 0;
	if ( vk_liquid_requested() ) {
		const float scale = Com_Clamp( 0.25f, 1.0f,
			r_liquidResolution ? r_liquidResolution->value : 1.0f );

		vk.liquidSnapshotWidth =
			MAX( 64, (uint32_t)( glConfig.vidWidth * scale + 0.5f ) );
		vk.liquidSnapshotHeight =
			MAX( 64, (uint32_t)( glConfig.vidHeight * scale + 0.5f ) );
	}

	// fill glConfig information

	// maxTextureSize must not exceed IMAGE_CHUNK_SIZE
	maxSize = sqrtf( IMAGE_CHUNK_SIZE / 4 );
	// round down to next power of 2
	glConfig.maxTextureSize = MIN( props.limits.maxImageDimension2D, log2pad( maxSize, 0 ) );

	if ( glConfig.maxTextureSize > MAX_TEXTURE_SIZE )
		glConfig.maxTextureSize = MAX_TEXTURE_SIZE; // ResampleTexture() relies on that maximum

	// default chunk size, may be doubled on demand
	vk.image_chunk_size = IMAGE_CHUNK_SIZE;

	vk.maxLod = 1 + Q_log2( glConfig.maxTextureSize );

	if ( props.limits.maxPerStageDescriptorSamplers != 0xFFFFFFFF )
		glConfig.numTextureUnits = props.limits.maxPerStageDescriptorSamplers;
	else
		glConfig.numTextureUnits = props.limits.maxBoundDescriptorSets;
	if ( glConfig.numTextureUnits > MAX_TEXTURE_UNITS )
		glConfig.numTextureUnits = MAX_TEXTURE_UNITS;

	vk.maxBoundDescriptorSets = props.limits.maxBoundDescriptorSets;

	if ( r_ext_texture_env_add->integer != 0 )
		glConfig.textureEnvAddAvailable = qtrue;
	else
		glConfig.textureEnvAddAvailable = qfalse;

	glConfig.textureCompression = TC_NONE;

	major = VK_VERSION_MAJOR(props.apiVersion);
	minor = VK_VERSION_MINOR(props.apiVersion);
	patch = VK_VERSION_PATCH(props.apiVersion);

	// decode driver version
	switch ( props.vendorID ) {
		case 0x10DE: // NVidia
			Com_sprintf( driver_version, sizeof( driver_version ), "%i.%i.%i.%i",
				(props.driverVersion >> 22) & 0x3FF,
				(props.driverVersion >> 14) & 0x0FF,
				(props.driverVersion >> 6) & 0x0FF,
				(props.driverVersion >> 0) & 0x03F );
			break;
#ifdef _WIN32
		case 0x8086: // Intel
			Com_sprintf( driver_version, sizeof( driver_version ), "%i.%i",
				(props.driverVersion >> 14),
				(props.driverVersion >> 0) & 0x3FFF );
			break;
#endif
		default:
			Com_sprintf( driver_version, sizeof( driver_version ), "%i.%i.%i",
				(props.driverVersion >> 22),
				(props.driverVersion >> 12) & 0x3FF,
				(props.driverVersion >> 0) & 0xFFF );
	}

	Com_sprintf( glConfig.version_string, sizeof( glConfig.version_string ), "API: %i.%i.%i, Driver: %s",
		major, minor, patch, driver_version );

	vk.offscreenRender = qtrue;

	if ( props.vendorID == 0x1002 ) {
		vendor_name = "Advanced Micro Devices, Inc.";
	} else if ( props.vendorID == 0x106B ) {
		vendor_name = "Apple Inc.";
	} else if ( props.vendorID == 0x10DE ) {
		// https://github.com/SaschaWillems/Vulkan/issues/493
		// we can't render to offscreen presentation surfaces on nvidia
		vk.offscreenRender = qfalse;
		vendor_name = "NVIDIA";
	} else if ( props.vendorID == 0x14E4 ) {
		vendor_name = "Broadcom Inc.";
	} else if ( props.vendorID == 0x1AE0 ) {
		vendor_name = "Google Inc.";
	} else if ( props.vendorID == 0x8086 ) {
		vendor_name = "Intel Corporation";
	} else if ( props.vendorID == VK_VENDOR_ID_MESA ) {
		vendor_name = "MESA";
	} else {
		Com_sprintf( buf, sizeof( buf ), "VendorID: %04x", props.vendorID );
		vendor_name = buf;
	}

	Q_strncpyz( glConfig.vendor_string, vendor_name, sizeof( glConfig.vendor_string ) );
	Q_strncpyz( glConfig.renderer_string, renderer_name( &props ), sizeof( glConfig.renderer_string ) );

	SET_OBJECT_NAME( (intptr_t)vk.device, glConfig.renderer_string, VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_EXT );

	//
	// Sync primitives.
	//
	vk_create_sync_primitives();

	//
	// Command pool.
	//
	{
		VkCommandPoolCreateInfo desc;

		desc.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		desc.pNext = NULL;
		desc.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		desc.queueFamilyIndex = vk.queue_family_index;

		VK_CHECK( qvkCreateCommandPool( vk.device, &desc, NULL, &vk.command_pool ) );

		SET_OBJECT_NAME( vk.command_pool, "command pool", VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_POOL_EXT );
	}

#ifdef USE_UPLOAD_QUEUE
	{
		VkCommandBufferAllocateInfo alloc_info;

		alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		alloc_info.pNext = NULL;
		alloc_info.commandPool = vk.command_pool;
		alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		alloc_info.commandBufferCount = 1;

		VK_CHECK( qvkAllocateCommandBuffers( vk.device, &alloc_info, &vk.staging_command_buffer ) );
	}
#endif

	//
	// Command buffers and color attachments.
	//
	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ )
	{
		VkCommandBufferAllocateInfo alloc_info;

		alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		alloc_info.pNext = NULL;
		alloc_info.commandPool = vk.command_pool;
		alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		alloc_info.commandBufferCount = 1;

		VK_CHECK( qvkAllocateCommandBuffers( vk.device, &alloc_info, &vk.tess[i].command_buffer ) );

		//SET_OBJECT_NAME( vk.tess[i].command_buffer, va( "command buffer %i", i ), VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_BUFFER_EXT );
	}

	//
	// Descriptor pool.
	//
	{
		VkDescriptorPoolSize pool_size[3];
		VkDescriptorPoolCreateInfo desc;
		uint32_t poolIndex, maxSets;

		pool_size[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		pool_size[0].descriptorCount =
			MAX_DRAWIMAGES + 1 + 1 + 1 + 1 + 1 + 3 + 3 +
			VK_NUM_BLOOM_PASSES * 2; // color, motion, screenmap, fog depth, liquid source/color/depth, shadow atlases, bloom

		pool_size[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		pool_size[1].descriptorCount = NUM_COMMAND_BUFFERS;

		//pool_size[2].type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
		//pool_size[2].descriptorCount = NUM_COMMAND_BUFFERS;

		pool_size[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
		pool_size[2].descriptorCount = 1;

		for ( poolIndex = 0, maxSets = 0; poolIndex < ARRAY_LEN( pool_size ); poolIndex++ ) {
			maxSets += pool_size[poolIndex].descriptorCount;
		}

		desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		desc.pNext = NULL;
		desc.flags = 0;
		desc.maxSets = maxSets;
		desc.poolSizeCount = ARRAY_LEN( pool_size );
		desc.pPoolSizes = pool_size;

		VK_CHECK( qvkCreateDescriptorPool( vk.device, &desc, NULL, &vk.descriptor_pool ) );
	}

	//
	// Descriptor set layout.
	//
	vk_create_descriptor_layout_registry();

	//
	// Pipeline layouts.
	//
	vk_create_pipeline_layout_registry();

	vk.geometry_buffer_size_new = VERTEX_BUFFER_SIZE;
	vk_create_geometry_buffers( vk.geometry_buffer_size_new );
	vk.geometry_buffer_size_new = 0;

	vk_create_storage_buffer( MAX_FLARES * vk.storage_alignment );

	vk_create_shader_modules();

	{
		VkPipelineCacheCreateInfo ci;
		Com_Memset( &ci, 0, sizeof( ci ) );
		ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
		VK_CHECK( qvkCreatePipelineCache( vk.device, &ci, NULL, &vk.pipelineCache ) );
	}

	vk.renderPassIndex = RENDER_PASS_MAIN; // default render pass

	// swapchain
	vk.initSwapchainLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	//vk.initSwapchainLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	vk_create_swapchain( vk.physical_device, vk.device, vk_surface, vk.present_format, &vk.swapchain, qtrue );

	// color/depth attachments
	vk_create_attachments();

	// renderpasses
	vk_create_render_passes();

	// framebuffers for each swapchain image
	vk_create_framebuffers();

	vk.active = qtrue;
	vk_lifetime_end_init();
}


void vk_create_pipelines( void )
{
	vk_alloc_persistent_pipelines();

	vk.pipelines_world_base = vk.pipelines_count;
}


static void vk_destroy_attachments( void )
{
	uint32_t i;

	if ( vk.bloom_image[0] ) {
		for ( i = 0; i < ARRAY_LEN( vk.bloom_image ); i++ ) {
			qvkDestroyImage( vk.device, vk.bloom_image[i], NULL );
			qvkDestroyImageView( vk.device, vk.bloom_image_view[i], NULL );
			vk.bloom_image[i] = VK_NULL_HANDLE;
			vk.bloom_image_view[i] = VK_NULL_HANDLE;
		}
	}

	if ( vk.color_image ) {
		qvkDestroyImage( vk.device, vk.color_image, NULL );
		qvkDestroyImageView( vk.device, vk.color_image_view, NULL );
		vk.color_image = VK_NULL_HANDLE;
		vk.color_image_view = VK_NULL_HANDLE;
	}
	if ( vk.motion_blur_image != VK_NULL_HANDLE ) {
		qvkDestroyImageView( vk.device, vk.motion_blur_image_view, NULL );
		qvkDestroyImage( vk.device, vk.motion_blur_image, NULL );
		vk.motion_blur_image = VK_NULL_HANDLE;
		vk.motion_blur_image_view = VK_NULL_HANDLE;
		vk.motion_blur_descriptor = VK_NULL_HANDLE;
	}
	R_MotionBlur_ResetView( &vk_motion_blur_view );

	if ( vk.msaa_image ) {
		qvkDestroyImage( vk.device, vk.msaa_image, NULL );
		qvkDestroyImageView( vk.device, vk.msaa_image_view, NULL );
		vk.msaa_image = VK_NULL_HANDLE;
		vk.msaa_image_view = VK_NULL_HANDLE;
	}

	if ( vk.depth_sample_image_view != VK_NULL_HANDLE ) {
		qvkDestroyImageView( vk.device, vk.depth_sample_image_view, NULL );
		vk.depth_sample_image_view = VK_NULL_HANDLE;
	}
	if ( vk.liquidSnapshot.color_image ) {
		qvkDestroyImageView( vk.device,
			vk.liquidSnapshot.color_image_view, NULL );
		qvkDestroyImage( vk.device,
			vk.liquidSnapshot.color_image, NULL );
		vk.liquidSnapshot.color_image = VK_NULL_HANDLE;
		vk.liquidSnapshot.color_image_view = VK_NULL_HANDLE;
	}
	if ( vk.liquidDepth.image ) {
		qvkDestroyImageView( vk.device,
			vk.liquidDepth.image_view, NULL );
		qvkDestroyImage( vk.device, vk.liquidDepth.image, NULL );
		vk.liquidDepth.image = VK_NULL_HANDLE;
		vk.liquidDepth.image_view = VK_NULL_HANDLE;
		vk.liquidDepth.copied = qfalse;
	}
	qvkDestroyImageView( vk.device, vk.depth_image_view, NULL );
	qvkDestroyImage( vk.device, vk.depth_image, NULL );
	vk.depth_image = VK_NULL_HANDLE;
	vk.depth_image_view = VK_NULL_HANDLE;

	if ( vk.dlight_shadow_image_view != VK_NULL_HANDLE ) {
		qvkDestroyImageView( vk.device, vk.dlight_shadow_image_view, NULL );
		vk.dlight_shadow_image_view = VK_NULL_HANDLE;
	}
	if ( vk.dlight_shadow_image != VK_NULL_HANDLE ) {
		qvkDestroyImage( vk.device, vk.dlight_shadow_image, NULL );
		vk.dlight_shadow_image = VK_NULL_HANDLE;
	}
	if ( vk.spot_shadow_image_view != VK_NULL_HANDLE ) {
		qvkDestroyImageView( vk.device, vk.spot_shadow_image_view, NULL );
		vk.spot_shadow_image_view = VK_NULL_HANDLE;
	}
	if ( vk.spot_shadow_image != VK_NULL_HANDLE ) {
		qvkDestroyImage( vk.device, vk.spot_shadow_image, NULL );
		vk.spot_shadow_image = VK_NULL_HANDLE;
	}
	if ( vk.csm_shadow_image_view != VK_NULL_HANDLE ) {
		qvkDestroyImageView( vk.device, vk.csm_shadow_image_view, NULL );
		vk.csm_shadow_image_view = VK_NULL_HANDLE;
	}
	if ( vk.csm_shadow_image != VK_NULL_HANDLE ) {
		qvkDestroyImage( vk.device, vk.csm_shadow_image, NULL );
		vk.csm_shadow_image = VK_NULL_HANDLE;
	}
	if ( vk.screenMap.color_image ) {
		qvkDestroyImage( vk.device, vk.screenMap.color_image, NULL );
		qvkDestroyImageView( vk.device, vk.screenMap.color_image_view, NULL );
		vk.screenMap.color_image = VK_NULL_HANDLE;
		vk.screenMap.color_image_view = VK_NULL_HANDLE;
	}

	if ( vk.screenMap.color_image_msaa ) {
		qvkDestroyImage( vk.device, vk.screenMap.color_image_msaa, NULL );
		qvkDestroyImageView( vk.device, vk.screenMap.color_image_view_msaa, NULL );
		vk.screenMap.color_image_msaa = VK_NULL_HANDLE;
		vk.screenMap.color_image_view_msaa = VK_NULL_HANDLE;
	}

	if ( vk.screenMap.depth_image ) {
		qvkDestroyImage( vk.device, vk.screenMap.depth_image, NULL );
		qvkDestroyImageView( vk.device, vk.screenMap.depth_image_view, NULL );
		vk.screenMap.depth_image = VK_NULL_HANDLE;
		vk.screenMap.depth_image_view = VK_NULL_HANDLE;
	}

	if ( vk.capture.image ) {
		qvkDestroyImage( vk.device, vk.capture.image, NULL );
		qvkDestroyImageView( vk.device, vk.capture.image_view, NULL );
		vk.capture.image = VK_NULL_HANDLE;
		vk.capture.image_view = VK_NULL_HANDLE;
	}

	for ( i = 0; i < vk.image_memory_count; i++ ) {
		qvkFreeMemory( vk.device, vk.image_memory[i], NULL );
	}

	vk.image_memory_count = 0;
	vk_clear_dlight_shadow_atlas_layout();
	vk_clear_spot_shadow_atlas_layout();
	vk_clear_csm_shadow_atlas_layout();
	vk_lifetime_mark_destroy( RTX_VK_RESOURCE_ATTACHMENTS, __func__ );
}


static void vk_destroy_render_passes( void )
{
	uint32_t i;

	if ( vk.render_pass.main != VK_NULL_HANDLE ) {
		qvkDestroyRenderPass( vk.device, vk.render_pass.main, NULL );
		vk.render_pass.main = VK_NULL_HANDLE;
	}
	if ( vk.render_pass.main_load != VK_NULL_HANDLE ) {
		qvkDestroyRenderPass( vk.device, vk.render_pass.main_load, NULL );
		vk.render_pass.main_load = VK_NULL_HANDLE;
	}

	if ( vk.render_pass.bloom_extract != VK_NULL_HANDLE ) {
		qvkDestroyRenderPass( vk.device, vk.render_pass.bloom_extract, NULL );
		vk.render_pass.bloom_extract = VK_NULL_HANDLE;
	}

	for ( i = 0; i < ARRAY_LEN( vk.render_pass.blur ); i++ ) {
		if ( vk.render_pass.blur[i] != VK_NULL_HANDLE ) {
			qvkDestroyRenderPass( vk.device, vk.render_pass.blur[i], NULL );
			vk.render_pass.blur[i] = VK_NULL_HANDLE;
		}
	}

	if ( vk.render_pass.post_bloom != VK_NULL_HANDLE ) {
		qvkDestroyRenderPass( vk.device, vk.render_pass.post_bloom, NULL );
		vk.render_pass.post_bloom = VK_NULL_HANDLE;
	}

	if ( vk.render_pass.global_fog != VK_NULL_HANDLE ) {
		qvkDestroyRenderPass( vk.device, vk.render_pass.global_fog, NULL );
		vk.render_pass.global_fog = VK_NULL_HANDLE;
	}
	if ( vk.render_pass.liquid_snapshot != VK_NULL_HANDLE ) {
		qvkDestroyRenderPass( vk.device,
			vk.render_pass.liquid_snapshot, NULL );
		vk.render_pass.liquid_snapshot = VK_NULL_HANDLE;
	}

	if ( vk.render_pass.screenmap != VK_NULL_HANDLE ) {
		qvkDestroyRenderPass( vk.device, vk.render_pass.screenmap, NULL );
		vk.render_pass.screenmap = VK_NULL_HANDLE;
	}

	if ( vk.render_pass.gamma != VK_NULL_HANDLE ) {
		qvkDestroyRenderPass( vk.device, vk.render_pass.gamma, NULL );
		vk.render_pass.gamma = VK_NULL_HANDLE;
	}

	if ( vk.render_pass.capture != VK_NULL_HANDLE ) {
		qvkDestroyRenderPass( vk.device, vk.render_pass.capture, NULL );
		vk.render_pass.capture = VK_NULL_HANDLE;
	}
	if ( vk.render_pass.motion_blur != VK_NULL_HANDLE ) {
		qvkDestroyRenderPass( vk.device, vk.render_pass.motion_blur, NULL );
		vk.render_pass.motion_blur = VK_NULL_HANDLE;
	}

	if ( vk.render_pass.dlight_shadow != VK_NULL_HANDLE ) {
		qvkDestroyRenderPass( vk.device, vk.render_pass.dlight_shadow, NULL );
		vk.render_pass.dlight_shadow = VK_NULL_HANDLE;
	}

	vk_lifetime_mark_destroy( RTX_VK_RESOURCE_RENDER_PASSES, __func__ );
}


static void vk_destroy_pipelines( qboolean resetCounter )
{
	uint32_t i, j;

	for ( i = 0; i < vk.pipelines_count; i++ ) {
		for ( j = 0; j < RENDER_PASS_COUNT; j++ ) {
			if ( vk.pipelines[i].handle[j] != VK_NULL_HANDLE ) {
				qvkDestroyPipeline( vk.device, vk.pipelines[i].handle[j], NULL );
				vk.pipelines[i].handle[j] = VK_NULL_HANDLE;
				vk.pipeline_create_count--;
			}
		}
	}

	if ( resetCounter ) {
		Com_Memset( &vk.pipelines, 0, sizeof( vk.pipelines ) );
		vk.pipelines_count = 0;
	}

	if ( vk.gamma_pipeline ) {
		qvkDestroyPipeline( vk.device, vk.gamma_pipeline, NULL );
		vk.gamma_pipeline = VK_NULL_HANDLE;
	}

	if ( vk.capture_pipeline ) {
		qvkDestroyPipeline( vk.device, vk.capture_pipeline, NULL );
		vk.capture_pipeline = VK_NULL_HANDLE;
	}

	if ( vk.bloom_extract_pipeline != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.bloom_extract_pipeline, NULL );
		vk.bloom_extract_pipeline = VK_NULL_HANDLE;
	}

	if ( vk.bloom_blend_pipeline != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.bloom_blend_pipeline, NULL );
		vk.bloom_blend_pipeline = VK_NULL_HANDLE;
	}
	if ( vk.bloom_blend_cel_pipeline != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.bloom_blend_cel_pipeline, NULL );
		vk.bloom_blend_cel_pipeline = VK_NULL_HANDLE;
	}
	if ( vk.motion_blur_pipeline != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.motion_blur_pipeline, NULL );
		vk.motion_blur_pipeline = VK_NULL_HANDLE;
	}
	if ( vk.motion_blur_copy_pipeline != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.motion_blur_copy_pipeline, NULL );
		vk.motion_blur_copy_pipeline = VK_NULL_HANDLE;
	}
	if ( vk.world_outline_pipeline != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.world_outline_pipeline, NULL );
		vk.world_outline_pipeline = VK_NULL_HANDLE;
	}

	if ( vk.global_fog_pipeline != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.global_fog_pipeline, NULL );
		vk.global_fog_pipeline = VK_NULL_HANDLE;
	}
	if ( vk.liquid_snapshot_pipeline != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device,
			vk.liquid_snapshot_pipeline, NULL );
		vk.liquid_snapshot_pipeline = VK_NULL_HANDLE;
	}

	for ( i = 0; i < ARRAY_LEN( vk.blur_pipeline ); i++ ) {
		if ( vk.blur_pipeline[i] != VK_NULL_HANDLE ) {
			qvkDestroyPipeline( vk.device, vk.blur_pipeline[i], NULL );
			vk.blur_pipeline[i] = VK_NULL_HANDLE;
		}
	}
}


void vk_shutdown( refShutdownCode_t code )
{
	int i, j, k, l;

	vk_lifetime_begin_shutdown();
	if ( qvkQueuePresentKHR == NULL ) { // not fully initialized
		goto __cleanup;
	}

	vk_rt_shutdown();

	vk_destroy_framebuffers();

	vk_destroy_pipelines( qtrue ); // reset counter

	vk_destroy_render_passes();

	vk_destroy_attachments();

	vk_destroy_swapchain();

	if ( vk.pipelineCache != VK_NULL_HANDLE ) {
		qvkDestroyPipelineCache( vk.device, vk.pipelineCache, NULL );
		vk.pipelineCache = VK_NULL_HANDLE;
	}

	qvkDestroyCommandPool( vk.device, vk.command_pool, NULL );

	qvkDestroyDescriptorPool(vk.device, vk.descriptor_pool, NULL);

	vk_destroy_pipeline_layout_registry();
	vk_destroy_descriptor_layout_registry();

#ifdef USE_VBO
	vk_release_vbo();
#endif

	vk_release_geometry_buffers();

	vk_destroy_sync_primitives();
	vk_destroy_storage_buffer();

	for ( i = 0; i < 3; i++ ) {
		for ( j = 0; j < 2; j++ ) {
			for ( k = 0; k < 2; k++ ) {
				for ( l = 0; l < 2; l++ ) {
					if ( vk.modules.vert.gen[i][j][k][l] != VK_NULL_HANDLE ) {
						qvkDestroyShaderModule( vk.device, vk.modules.vert.gen[i][j][k][l], NULL );
						vk.modules.vert.gen[i][j][k][l] = VK_NULL_HANDLE;
					}
				}
			}
		}
	}
	for ( i = 0; i < 3; i++ ) {
		for ( j = 0; j < 2; j++ ) {
			for ( k = 0; k < 2; k++ ) {
				if ( vk.modules.frag.gen[i][j][k] != VK_NULL_HANDLE ) {
					qvkDestroyShaderModule( vk.device, vk.modules.frag.gen[i][j][k], NULL );
					vk.modules.frag.gen[i][j][k] = VK_NULL_HANDLE;
				}
			}
		}
	}
	for ( i = 0; i < 2; i++ ) {
		if ( vk.modules.vert.light[i] != VK_NULL_HANDLE ) {
			qvkDestroyShaderModule( vk.device, vk.modules.vert.light[i], NULL );
			vk.modules.vert.light[i] = VK_NULL_HANDLE;
		}
		for ( j = 0; j < 2; j++ ) {
			if ( vk.modules.frag.light[i][j] != VK_NULL_HANDLE ) {
				qvkDestroyShaderModule( vk.device, vk.modules.frag.light[i][j], NULL );
				vk.modules.frag.light[i][j] = VK_NULL_HANDLE;
			}
		}
	}

	if ( vk.modules.vert.csm_shadow != VK_NULL_HANDLE ) {
		qvkDestroyShaderModule( vk.device, vk.modules.vert.csm_shadow, NULL );
		vk.modules.vert.csm_shadow = VK_NULL_HANDLE;
	}
	if ( vk.modules.frag.csm_shadow != VK_NULL_HANDLE ) {
		qvkDestroyShaderModule( vk.device, vk.modules.frag.csm_shadow, NULL );
		vk.modules.frag.csm_shadow = VK_NULL_HANDLE;
	}

	for ( i = 0; i < 2; i++ ) {
		for ( j = 0; j < 2; j++ ) {
			for ( k = 0; k < 2; k++ ) {
				qvkDestroyShaderModule( vk.device, vk.modules.vert.ident1[i][j][k], NULL );
				vk.modules.vert.ident1[i][j][k] = VK_NULL_HANDLE;
			}
			qvkDestroyShaderModule( vk.device, vk.modules.frag.ident1[i][j], NULL );
			vk.modules.frag.ident1[i][j] = VK_NULL_HANDLE;
		}
	}

	for ( i = 0; i < 2; i++ ) {
		for ( j = 0; j < 2; j++ ) {
			for ( k = 0; k < 2; k++ ) {
				qvkDestroyShaderModule( vk.device, vk.modules.vert.fixed[i][j][k], NULL );
				vk.modules.vert.fixed[i][j][k] = VK_NULL_HANDLE;
			}
			qvkDestroyShaderModule( vk.device, vk.modules.frag.fixed[i][j], NULL );
			vk.modules.frag.fixed[i][j] = VK_NULL_HANDLE;
		}
	}

	for ( i = 0; i < 1; i++ ) {
		for ( j = 0; j < 2; j++ ) {
			qvkDestroyShaderModule( vk.device, vk.modules.frag.ent[i][j], NULL );
			vk.modules.frag.ent[i][j] = VK_NULL_HANDLE;
		}
	}

	qvkDestroyShaderModule( vk.device, vk.modules.frag.gen0_df, NULL );

	qvkDestroyShaderModule( vk.device, vk.modules.color_fs, NULL );
	qvkDestroyShaderModule( vk.device, vk.modules.color_vs, NULL );

	qvkDestroyShaderModule(vk.device, vk.modules.fog_vs, NULL);
	qvkDestroyShaderModule(vk.device, vk.modules.fog_fs, NULL);

	qvkDestroyShaderModule(vk.device, vk.modules.dot_vs, NULL);
	qvkDestroyShaderModule(vk.device, vk.modules.dot_fs, NULL);
	qvkDestroyShaderModule(vk.device, vk.modules.liquid_vs, NULL);
	qvkDestroyShaderModule(vk.device, vk.modules.liquid_fs, NULL);
	qvkDestroyShaderModule(vk.device, vk.modules.liquid_copy_fs, NULL);
	vk.modules.liquid_vs = VK_NULL_HANDLE;
	vk.modules.liquid_fs = VK_NULL_HANDLE;
	vk.modules.liquid_copy_fs = VK_NULL_HANDLE;

	qvkDestroyShaderModule(vk.device, vk.modules.bloom_fs, NULL);
	qvkDestroyShaderModule(vk.device, vk.modules.blur_fs, NULL);
	qvkDestroyShaderModule(vk.device, vk.modules.blend_fs, NULL);
	qvkDestroyShaderModule(vk.device, vk.modules.motion_blur_fs, NULL);
	qvkDestroyShaderModule(vk.device, vk.modules.world_outline_fs, NULL);
	qvkDestroyShaderModule(vk.device, vk.modules.global_fog_fs, NULL);
	vk.modules.global_fog_fs = VK_NULL_HANDLE;

	qvkDestroyShaderModule(vk.device, vk.modules.gamma_vs, NULL);
	qvkDestroyShaderModule(vk.device, vk.modules.gamma_fs, NULL);

	if ( vk.modules.rt_rgen != VK_NULL_HANDLE ) {
		qvkDestroyShaderModule( vk.device, vk.modules.rt_rgen, NULL );
		vk.modules.rt_rgen = VK_NULL_HANDLE;
	}
	if ( vk.modules.rt_rmiss != VK_NULL_HANDLE ) {
		qvkDestroyShaderModule( vk.device, vk.modules.rt_rmiss, NULL );
		vk.modules.rt_rmiss = VK_NULL_HANDLE;
	}
	if ( vk.modules.rt_rmiss_shadow != VK_NULL_HANDLE ) {
		qvkDestroyShaderModule( vk.device, vk.modules.rt_rmiss_shadow, NULL );
		vk.modules.rt_rmiss_shadow = VK_NULL_HANDLE;
	}
	if ( vk.modules.rt_rchit != VK_NULL_HANDLE ) {
		qvkDestroyShaderModule( vk.device, vk.modules.rt_rchit, NULL );
		vk.modules.rt_rchit = VK_NULL_HANDLE;
	}
	if ( vk.modules.rt_rahit != VK_NULL_HANDLE ) {
		qvkDestroyShaderModule( vk.device, vk.modules.rt_rahit, NULL );
		vk.modules.rt_rahit = VK_NULL_HANDLE;
	}

__cleanup:
	vk_lifetime_end_shutdown();
	if ( vk.device != VK_NULL_HANDLE ) {
		qvkDestroyDevice( vk.device, NULL );
	}

	deinit_device_functions();

	Com_Memset( &vk, 0, sizeof( vk ) );
	Com_Memset( &vk_world, 0, sizeof( vk_world ) );
	
	if ( code != REF_KEEP_CONTEXT ) {
		vk_destroy_instance();
		deinit_instance_functions();
	}
}


void vk_wait_idle( void )
{
	VK_CHECK( qvkDeviceWaitIdle( vk.device ) );
}


void vk_queue_wait_idle( void )
{
	VK_CHECK( qvkQueueWaitIdle( vk.queue ) );
}


void vk_release_resources( refShutdownCode_t code ) {
	int i, j;

	vk_wait_idle();
#ifdef USE_UPLOAD_QUEUE
	if ( code == REF_KEEP_CONTEXT ) {
		/*
		 * A registration restart can happen after a final staging submit but
		 * before a frame consumes its binary upload semaphore. Device idle
		 * completes that submit but does not return binary semaphores to the
		 * unsignaled state. Reset the reusable staging command buffer and
		 * rebuild the upload/frame sync chain before the retained device is
		 * used for the next registration.
		 */
		vk_wait_staging_buffer();
		VK_CHECK( qvkResetCommandBuffer( vk.staging_command_buffer, 0 ) );
		vk_destroy_sync_primitives();
	}
#endif
	vk_rt_shutdown();

	for (i = 0; i < vk_world.num_image_chunks; i++)
		qvkFreeMemory(vk.device, vk_world.image_chunks[i].memory, NULL);

	vk_clean_staging_buffer();

	for (i = 0; i < vk_world.num_samplers; i++)
		qvkDestroySampler(vk.device, vk_world.samplers[i], NULL);

	for ( i = vk.pipelines_world_base; i < vk.pipelines_count; i++ ) {
		for ( j = 0; j < RENDER_PASS_COUNT; j++ ) {
			if ( vk.pipelines[i].handle[j] != VK_NULL_HANDLE ) {
				qvkDestroyPipeline( vk.device, vk.pipelines[i].handle[j], NULL );
				vk.pipelines[i].handle[j] = VK_NULL_HANDLE;
				vk.pipeline_create_count--;
			}
		}
		Com_Memset( &vk.pipelines[i], 0, sizeof( vk.pipelines[0] ) );
	}
	vk.pipelines_count = vk.pipelines_world_base;

	VK_CHECK( qvkResetDescriptorPool( vk.device, vk.descriptor_pool, 0 ) );

	if ( vk_world.num_image_chunks > 1 ) {
		// if we allocated more than 2 image chunks - use doubled default size
		vk.image_chunk_size = (IMAGE_CHUNK_SIZE * 2);
	} else if ( vk_world.num_image_chunks == 1 ) {
		// otherwise set to default if used less than a half
		if ( vk_world.image_chunks[0].used < ( IMAGE_CHUNK_SIZE - (IMAGE_CHUNK_SIZE / 10) ) ) {
			vk.image_chunk_size = IMAGE_CHUNK_SIZE;
		}
	}

	Com_Memset( &vk_world, 0, sizeof( vk_world ) );

	// Reset geometry buffers offsets
	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		vk.tess[i].uniform_read_offset = 0;
		vk.tess[i].vertex_buffer_offset = 0;
	}

	Com_Memset( vk.cmd->buf_offset, 0, sizeof( vk.cmd->buf_offset ) );
	Com_Memset( vk.cmd->vbo_offset, 0, sizeof( vk.cmd->vbo_offset ) );

	Com_Memset( &vk.stats, 0, sizeof( vk.stats ) );

#ifdef USE_UPLOAD_QUEUE
	if ( code == REF_KEEP_CONTEXT ) {
		vk_create_sync_primitives();
	}
#endif
}

#if 0
static void record_buffer_memory_barrier(VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize size, VkDeviceSize offset,
		VkPipelineStageFlags src_stages, VkPipelineStageFlags dst_stages,
		VkAccessFlags src_access, VkAccessFlags dst_access) {

	VkBufferMemoryBarrier barrier;
	barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	barrier.pNext = NULL;
	barrier.srcAccessMask = src_access;
	barrier.dstAccessMask = dst_access;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.buffer = buffer;
	barrier.offset = offset;
	barrier.size = size;

	qvkCmdPipelineBarrier( cb, src_stages, dst_stages, 0, 0, NULL, 1, &barrier, 0, NULL );
}
#endif

void vk_create_image( image_t *image, int width, int height, int mip_levels ) {

	VkFormat format = image->internalFormat;

	if ( image->handle ) {
		qvkDestroyImage( vk.device, image->handle, NULL );
		image->handle = VK_NULL_HANDLE;
	}

	if ( image->view ) {
		qvkDestroyImageView( vk.device, image->view, NULL );
		image->view = VK_NULL_HANDLE;
	}

	// create image
	{
		VkImageCreateInfo desc;

		desc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		desc.pNext = NULL;
		desc.flags = 0;
		desc.imageType = VK_IMAGE_TYPE_2D;
		desc.format = format;
		desc.extent.width = width;
		desc.extent.height = height;
		desc.extent.depth = 1;
		desc.mipLevels = mip_levels;
		desc.arrayLayers = 1;
		desc.samples = VK_SAMPLE_COUNT_1_BIT;
		desc.tiling = VK_IMAGE_TILING_OPTIMAL;
		desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		desc.queueFamilyIndexCount = 0;
		desc.pQueueFamilyIndices = NULL;
		desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VK_CHECK( qvkCreateImage( vk.device, &desc, NULL, &image->handle ) );

		allocate_and_bind_image_memory( image->handle );
	}

	// create image view
	{
		VkImageViewCreateInfo desc;

		desc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		desc.pNext = NULL;
		desc.flags = 0;
		desc.image = image->handle;
		desc.viewType = VK_IMAGE_VIEW_TYPE_2D;
		desc.format = format;
		desc.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		desc.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		desc.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		desc.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		desc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		desc.subresourceRange.baseMipLevel = 0;
		desc.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
		desc.subresourceRange.baseArrayLayer = 0;
		desc.subresourceRange.layerCount = 1;

		VK_CHECK( qvkCreateImageView( vk.device, &desc, NULL, &image->view ) );
	}

	// create associated descriptor set
	if ( image->descriptor == VK_NULL_HANDLE ) {
		VkDescriptorSetAllocateInfo desc;

		desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		desc.pNext = NULL;
		desc.descriptorPool = vk.descriptor_pool;
		desc.descriptorSetCount = 1;
		desc.pSetLayouts = vk_descriptor_layout_ref( RTX_VK_DESC_LAYOUT_SAMPLER );

		VK_CHECK( qvkAllocateDescriptorSets( vk.device, &desc, &image->descriptor ) );
	}

	vk_update_descriptor_set( image, mip_levels > 1 ? qtrue : qfalse );

	SET_OBJECT_NAME( image->handle, image->imgName, VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT );
	SET_OBJECT_NAME( image->view, image->imgName, VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT );
	SET_OBJECT_NAME( image->descriptor, image->imgName, VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT );
}


static byte *resample_image_data( const int target_format, byte *data, const int data_size, int *bytes_per_pixel )
{
	byte* buffer;
	uint16_t* p;
	int i, n;

	switch ( target_format ) {
	case VK_FORMAT_B4G4R4A4_UNORM_PACK16:
		buffer = (byte*)ri.Hunk_AllocateTempMemory( data_size / 2 );
		p = (uint16_t*)buffer;
		for ( i = 0; i < data_size; i += 4, p++ ) {
			byte r = data[i + 0];
			byte g = data[i + 1];
			byte b = data[i + 2];
			byte a = data[i + 3];
			*p = (uint32_t)((a / 255.0) * 15.0 + 0.5) |
				((uint32_t)((r / 255.0) * 15.0 + 0.5) << 4) |
				((uint32_t)((g / 255.0) * 15.0 + 0.5) << 8) |
				((uint32_t)((b / 255.0) * 15.0 + 0.5) << 12);
		}
		*bytes_per_pixel = 2;
		return buffer; // must be freed after upload!

	case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
		buffer = (byte*)ri.Hunk_AllocateTempMemory( data_size / 2 );
		p = (uint16_t*)buffer;
		for ( i = 0; i < data_size; i += 4, p++ ) {
			byte r = data[i + 0];
			byte g = data[i + 1];
			byte b = data[i + 2];
			*p = (uint32_t)((b / 255.0) * 31.0 + 0.5) |
				((uint32_t)((g / 255.0) * 31.0 + 0.5) << 5) |
				((uint32_t)((r / 255.0) * 31.0 + 0.5) << 10) |
				(1 << 15);
		}
		*bytes_per_pixel = 2;
		return buffer; // must be freed after upload!

	case VK_FORMAT_B8G8R8A8_UNORM:
		buffer = (byte*)ri.Hunk_AllocateTempMemory( data_size );
		for ( i = 0; i < data_size; i += 4 ) {
			buffer[i + 0] = data[i + 2];
			buffer[i + 1] = data[i + 1];
			buffer[i + 2] = data[i + 0];
			buffer[i + 3] = data[i + 3];
		}
		*bytes_per_pixel = 4;
		return buffer;

	case VK_FORMAT_R8G8B8_UNORM: {
		buffer = (byte*)ri.Hunk_AllocateTempMemory( (data_size * 3) / 4 );
		for ( i = 0, n = 0; i < data_size; i += 4, n += 3 ) {
			buffer[n + 0] = data[i + 0];
			buffer[n + 1] = data[i + 1];
			buffer[n + 2] = data[i + 2];
		}
		*bytes_per_pixel = 3;
		return buffer;
	}

	default:
		*bytes_per_pixel = 4;
		return data;
	}
}


void vk_upload_image_data( image_t *image, int x, int y, int width, int height, int mipmaps, byte *pixels, int size, qboolean update ) {

	VkCommandBuffer command_buffer;
	VkBufferImageCopy regions[16];
	VkBufferImageCopy region;
	const int streamBudgetMb = vk_rt_texture_stream_budget_mb();
	const VkDeviceSize streamBudgetBytes = ( streamBudgetMb > 0 ) ? (VkDeviceSize)streamBudgetMb * 1024u * 1024u : 0u;
	const int frameMarker = tr.frameCount;

	byte *buf;
	int bpp;
#ifdef USE_UPLOAD_QUEUE
	int i;
#endif

	int num_regions = 0;
	int buffer_size = 0;

	buf = resample_image_data( image->internalFormat, pixels, size, &bpp );

	while (qtrue) {
		Com_Memset(&region, 0, sizeof(region));
		region.bufferOffset = buffer_size;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = num_regions;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageOffset.x = x;
		region.imageOffset.y = y;
		region.imageOffset.z = 0;
		region.imageExtent.width = width;
		region.imageExtent.height = height;
		region.imageExtent.depth = 1;

		regions[num_regions] = region;
		num_regions++;

		buffer_size += width * height * bpp;

		if ( num_regions >= mipmaps || (width == 1 && height == 1) || num_regions >= ARRAY_LEN( regions ) )
			break;

		x >>= 1;
		y >>= 1;

		width >>= 1;
		if (width < 1) width = 1;

		height >>= 1;
		if (height < 1) height = 1;
	}

#ifdef USE_UPLOAD_QUEUE
	vk_wait_staging_buffer();
	if ( s_vkRtTextureBudgetFrame != frameMarker ) {
		s_vkRtTextureBudgetFrame = frameMarker;
		s_vkRtTextureBudgetUsed = 0;
	}
	if ( streamBudgetBytes > 0 &&
		( s_vkRtTextureBudgetUsed + (VkDeviceSize)buffer_size ) > streamBudgetBytes &&
		vk_world.staging_buffer_offset > 0 ) {
		vk_submit_staging_buffer( qfalse );
	}

	ensure_staging_buffer_allocation( buffer_size );

	for ( i = 0; i < num_regions; i++ ) {
		regions[i].bufferOffset += vk_world.staging_buffer_offset;
	}

	Com_Memcpy( vk_world.staging_buffer_ptr + vk_world.staging_buffer_offset, buf, buffer_size );

	if ( vk_world.staging_buffer_offset == 0 ) {
		VkCommandBufferBeginInfo begin_info;
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.pNext = NULL;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		begin_info.pInheritanceInfo = NULL;
		VK_CHECK( qvkBeginCommandBuffer( vk.staging_command_buffer, &begin_info ) );
	}
	//ri.Printf( PRINT_WARNING, "batch @%6i + %i %s \n", (int)vk_world.staging_buffer_offset, (int)buffer_size, image->imgName );
	vk_world.staging_buffer_offset += buffer_size;
	s_vkRtTextureBudgetUsed += (VkDeviceSize)buffer_size;

	command_buffer = vk.staging_command_buffer;
#else
	ensure_staging_buffer_allocation( buffer_size );

	Com_Memcpy( vk_world.staging_buffer_ptr, buf, buffer_size );

	command_buffer = begin_command_buffer();
#endif

	// record_buffer_memory_barrier( command_buffer, vk_world.staging_buffer, VK_WHOLE_SIZE, 0, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT );

	if ( update ) {
		record_image_layout_transition( command_buffer, image->handle, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, 0 );
	} else {
		record_image_layout_transition( command_buffer, image->handle, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_HOST_BIT, 0 );
	}

	qvkCmdCopyBufferToImage( command_buffer, vk_world.staging_buffer, image->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, num_regions, regions );

	record_image_layout_transition( command_buffer, image->handle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 0 );

#ifndef USE_UPLOAD_QUEUE
	end_command_buffer( command_buffer, __func__ );
#endif

	if ( buf != pixels ) {
		ri.Hunk_FreeTempMemory( buf );
	}

	vk.rt.stats.texture_stream_bytes += (uint64_t)buffer_size;
}


void vk_update_descriptor_set( image_t *image, qboolean mipmap ) {
	Vk_Sampler_Def sampler_def;
	VkDescriptorImageInfo image_info;
	VkWriteDescriptorSet descriptor_write;

	Com_Memset( &sampler_def, 0, sizeof( sampler_def ) );

	sampler_def.address_mode = image->wrapClampMode;

	if ( mipmap ) {
		sampler_def.gl_mag_filter = gl_filter_max;
		sampler_def.gl_min_filter = gl_filter_min;
	} else {
		sampler_def.gl_mag_filter = GL_LINEAR;
		sampler_def.gl_min_filter = GL_LINEAR;
		// no anisotropy without mipmaps
		sampler_def.noAnisotropy = qtrue;
	}

	image_info.sampler = vk_find_sampler( &sampler_def );
	image_info.imageView = image->view;
	image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptor_write.dstSet = image->descriptor;
	descriptor_write.dstBinding = 0;
	descriptor_write.dstArrayElement = 0;
	descriptor_write.descriptorCount = 1;
	descriptor_write.pNext = NULL;
	descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptor_write.pImageInfo = &image_info;
	descriptor_write.pBufferInfo = NULL;
	descriptor_write.pTexelBufferView = NULL;

	qvkUpdateDescriptorSets( vk.device, 1, &descriptor_write, 0, NULL );
}


void vk_destroy_image_resources( VkImage *image, VkImageView *imageView )
{
	if ( image != NULL ) {
		if ( *image != VK_NULL_HANDLE ) {
			qvkDestroyImage( vk.device, *image, NULL );
			*image = VK_NULL_HANDLE;
		}
	}
	if ( imageView != NULL ) {
		if ( *imageView != VK_NULL_HANDLE ) {
			qvkDestroyImageView( vk.device, *imageView, NULL );
			*imageView = VK_NULL_HANDLE;
		}
	}
}


static void set_shader_stage_desc(VkPipelineShaderStageCreateInfo *desc, VkShaderStageFlagBits stage, VkShaderModule shader_module, const char *entry) {
	desc->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	desc->pNext = NULL;
	desc->flags = 0;
	desc->stage = stage;
	desc->module = shader_module;
	desc->pName = entry;
	desc->pSpecializationInfo = NULL;
}


#define FORMAT_DEPTH(format, r_bits, g_bits, b_bits) case(VK_FORMAT_##format): *r = r_bits; *b = b_bits; *g = g_bits; return qtrue;
static qboolean vk_surface_format_color_depth( VkFormat format, int *r, int *g, int *b ) {
	switch (format) {
		// Common formats from https://vulkan.gpuinfo.org/listsurfaceformats.php
		FORMAT_DEPTH(B8G8R8A8_UNORM, 255, 255, 255)
			FORMAT_DEPTH(B8G8R8A8_SRGB, 255, 255, 255)
			FORMAT_DEPTH(A2B10G10R10_UNORM_PACK32, 1023, 1023, 1023)
			FORMAT_DEPTH(R8G8B8A8_UNORM, 255, 255, 255)
			FORMAT_DEPTH(R8G8B8A8_SRGB, 255, 255, 255)
			FORMAT_DEPTH(A2R10G10B10_UNORM_PACK32, 1023, 1023, 1023)
			FORMAT_DEPTH(R5G6B5_UNORM_PACK16, 31, 63, 31)
			FORMAT_DEPTH(R8G8B8A8_SNORM, 255, 255, 255)
			FORMAT_DEPTH(A8B8G8R8_UNORM_PACK32, 255, 255, 255)
			FORMAT_DEPTH(A8B8G8R8_SNORM_PACK32, 255, 255, 255)
			FORMAT_DEPTH(A8B8G8R8_SRGB_PACK32, 255, 255, 255)
			FORMAT_DEPTH(R16G16B16A16_UNORM, 65535, 65535, 65535)
			FORMAT_DEPTH(R16G16B16A16_SNORM, 65535, 65535, 65535)
			FORMAT_DEPTH(B5G6R5_UNORM_PACK16, 31, 63, 31)
			FORMAT_DEPTH(B8G8R8A8_SNORM, 255, 255, 255)
			FORMAT_DEPTH(R4G4B4A4_UNORM_PACK16, 15, 15, 15)
			FORMAT_DEPTH(B4G4R4A4_UNORM_PACK16, 15, 15, 15)
			FORMAT_DEPTH(A1R5G5B5_UNORM_PACK16, 31, 31, 31)
			FORMAT_DEPTH(R5G5B5A1_UNORM_PACK16, 31, 31, 31)
			FORMAT_DEPTH(B5G5R5A1_UNORM_PACK16, 31, 31, 31)
	default:
		*r = 255; *g = 255; *b = 255; return qfalse;
	}
}


void vk_create_post_process_pipeline( int program_index, uint32_t width, uint32_t height )
{
	VkPipelineShaderStageCreateInfo shader_stages[2];
	VkPipelineVertexInputStateCreateInfo vertex_input_state;
	VkPipelineInputAssemblyStateCreateInfo input_assembly_state;
	VkPipelineRasterizationStateCreateInfo rasterization_state;
	VkPipelineDepthStencilStateCreateInfo depth_stencil_state;
	VkPipelineViewportStateCreateInfo viewport_state;
	VkPipelineMultisampleStateCreateInfo multisample_state;
	VkPipelineColorBlendStateCreateInfo blend_state;
	VkPipelineColorBlendAttachmentState attachment_blend_state;
	VkGraphicsPipelineCreateInfo create_info;
	VkViewport viewport;
	VkRect2D scissor;
	VkSpecializationMapEntry spec_entries[45];
	VkSpecializationInfo frag_spec_info;
	VkPipeline *pipeline;
	VkShaderModule fsmodule;
	VkRenderPass renderpass;
	VkPipelineLayout layout;
	VkSampleCountFlagBits samples;
	const char *pipeline_name;
	qboolean blend;

	struct FragSpecData {
		float gamma;
		float overbright;
		float greyscale;
		float bloom_threshold;
		float bloom_intensity;
		int bloom_threshold_mode;
		int bloom_modulate;
		int dither;
		int depth_r;
		int depth_g;
		int depth_b;
		int output_color_space;
		float hdr_paper_white;
		float hdr_max_luminance;
		int tonemap_mode;
		float tonemap_exposure;
		float bloom_soft_knee;
		int scene_linear_mode;
		int color_grade_mode;
		float grade_lift[3];
		float grade_gamma[3];
		float grade_gain[3];
		float white_point[9];
		int color_grade_lut_size;
		float color_grade_lut_scale;
		int crt_mode;
		float crt_amount;
		float crt_scanline_strength;
		float crt_mask_strength;
		float crt_curvature;
		float crt_chromatic;
	} frag_spec_data;

	switch ( program_index ) {
		case 1: // bloom extraction
			pipeline = &vk.bloom_extract_pipeline;
			fsmodule = vk.modules.bloom_fs;
			renderpass = vk.render_pass.bloom_extract;
			layout = vk_pipeline_layout_handle( RTX_VK_PIPELINE_LAYOUT_POST_PROCESS );
			samples = VK_SAMPLE_COUNT_1_BIT;
			pipeline_name = "bloom extraction pipeline";
			blend = qfalse;
			break;
		case 2: // final bloom blend
			pipeline = &vk.bloom_blend_pipeline;
			fsmodule = vk.modules.blend_fs;
			renderpass = vk.render_pass.post_bloom;
			layout = vk_pipeline_layout_handle( RTX_VK_PIPELINE_LAYOUT_BLEND );
			samples = vkSamples;
			pipeline_name = "bloom blend pipeline";
			blend = qtrue;
			break;
		case 6: // world cel depth edge overlay in the resumed RTX composition pass
			pipeline = &vk.world_outline_pipeline;
			fsmodule = vk.modules.world_outline_fs;
			renderpass = vk.render_pass.post_bloom;
			layout = vk_pipeline_layout_handle( RTX_VK_PIPELINE_LAYOUT_POST_PROCESS );
			samples = vkSamples;
			pipeline_name = "world cel depth-outline pipeline";
			blend = qtrue;
			break;
		case 7: // final bloom blend that does not brighten dark cel outlines
			pipeline = &vk.bloom_blend_cel_pipeline;
			fsmodule = vk.modules.blend_fs;
			renderpass = vk.render_pass.post_bloom;
			layout = vk_pipeline_layout_handle( RTX_VK_PIPELINE_LAYOUT_BLEND );
			samples = vkSamples;
			pipeline_name = "bloom blend cel-outline pipeline";
			blend = qtrue;
			break;
		case 8: // camera-motion blur into the scratch attachment
			pipeline = &vk.motion_blur_pipeline;
			fsmodule = vk.modules.motion_blur_fs;
			renderpass = vk.render_pass.motion_blur;
			layout = vk_pipeline_layout_handle( RTX_VK_PIPELINE_LAYOUT_POST_PROCESS );
			samples = VK_SAMPLE_COUNT_1_BIT;
			pipeline_name = "motion blur scene pipeline";
			blend = qfalse;
			break;
		case 9: // copy blurred scene back to the current composition target
			pipeline = &vk.motion_blur_copy_pipeline;
			fsmodule = vk.modules.motion_blur_fs;
			renderpass = vk.render_pass.main_load;
			layout = vk_pipeline_layout_handle( RTX_VK_PIPELINE_LAYOUT_POST_PROCESS );
			samples = vkSamples;
			pipeline_name = "motion blur scene copy pipeline";
			blend = qfalse;
			break;
		case 3: // capture buffer extraction
			pipeline = &vk.capture_pipeline;
			fsmodule = vk.modules.gamma_fs;
			renderpass = vk.render_pass.capture;
			layout = vk_pipeline_layout_handle( RTX_VK_PIPELINE_LAYOUT_POST_PROCESS );
			samples = VK_SAMPLE_COUNT_1_BIT;
			pipeline_name = "capture buffer pipeline";
			blend = qfalse;
			break;
		case 4: // depth-aware global fog overlay
			pipeline = &vk.global_fog_pipeline;
			fsmodule = vk.modules.global_fog_fs;
			renderpass = vk.render_pass.global_fog;
			layout = vk_pipeline_layout_handle( RTX_VK_PIPELINE_LAYOUT_POST_PROCESS );
			samples = VK_SAMPLE_COUNT_1_BIT;
			pipeline_name = "global fog overlay pipeline";
			blend = qtrue;
			break;
		case 5: // stable pre-fog liquid scene snapshot
			pipeline = &vk.liquid_snapshot_pipeline;
			fsmodule = vk.modules.liquid_copy_fs;
			renderpass = vk.render_pass.liquid_snapshot;
			layout = vk_pipeline_layout_handle(
				RTX_VK_PIPELINE_LAYOUT_POST_PROCESS );
			samples = VK_SAMPLE_COUNT_1_BIT;
			pipeline_name = "liquid scene snapshot pipeline";
			blend = qfalse;
			break;
		default: // gamma correction
			pipeline = &vk.gamma_pipeline;
			fsmodule = vk.modules.gamma_fs;
			renderpass = vk.render_pass.gamma;
			layout = vk_pipeline_layout_handle( RTX_VK_PIPELINE_LAYOUT_POST_PROCESS );
			samples = VK_SAMPLE_COUNT_1_BIT;
			pipeline_name = "gamma-correction pipeline";
			blend = qfalse;
			break;
	}

	if ( *pipeline != VK_NULL_HANDLE ) {
		vk_wait_idle();
		qvkDestroyPipeline( vk.device, *pipeline, NULL );
		*pipeline = VK_NULL_HANDLE;
	}

	vertex_input_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertex_input_state.pNext = NULL;
	vertex_input_state.flags = 0;
	vertex_input_state.vertexBindingDescriptionCount = 0;
	vertex_input_state.pVertexBindingDescriptions = NULL;
	vertex_input_state.vertexAttributeDescriptionCount = 0;
	vertex_input_state.pVertexBindingDescriptions = NULL;

	// shaders
	set_shader_stage_desc( shader_stages+0, VK_SHADER_STAGE_VERTEX_BIT, vk.modules.gamma_vs, "main" );
	set_shader_stage_desc( shader_stages+1, VK_SHADER_STAGE_FRAGMENT_BIT, fsmodule, "main" );

	frag_spec_data.gamma = 1.0 / (r_gamma->value);
	frag_spec_data.overbright = (float)(1 << tr.overbrightBits);
	frag_spec_data.greyscale = r_greyscale->value;
	frag_spec_data.bloom_threshold = r_bloom_threshold->value;
	frag_spec_data.bloom_intensity = r_bloom_intensity->value;
	frag_spec_data.bloom_threshold_mode = r_bloom_threshold_mode->integer;
	frag_spec_data.bloom_modulate = r_bloom_modulate->integer;
	frag_spec_data.dither = r_dither->integer;
	frag_spec_data.output_color_space =
		( program_index == 0 && vk.hdrDisplayActive ) ?
			VK_POST_COLOR_SPACE_HDR10_ST2084 : VK_POST_COLOR_SPACE_SDR;
	frag_spec_data.hdr_paper_white = Com_Clamp( 80.0f, 500.0f,
		r_hdrDisplayPaperWhite ? r_hdrDisplayPaperWhite->value : 203.0f );
	frag_spec_data.hdr_max_luminance = Com_Clamp(
		frag_spec_data.hdr_paper_white, 10000.0f,
		r_hdrDisplayMaxLuminance ? r_hdrDisplayMaxLuminance->value : 1000.0f );
	if ( vk.hdrDisplayActive &&
		vk.displayOutput.maxLuminanceNits >= frag_spec_data.hdr_paper_white ) {
		frag_spec_data.hdr_max_luminance = Com_Clamp(
			frag_spec_data.hdr_paper_white,
			frag_spec_data.hdr_max_luminance,
			vk.displayOutput.maxLuminanceNits );
	}
	frag_spec_data.scene_linear_mode = vk_scene_linear_enabled() ? 1 : 0;
	frag_spec_data.tonemap_mode = ( frag_spec_data.scene_linear_mode &&
		( program_index == 0 || program_index == 1 || program_index == 3 ) ) ?
		r_tonemap->integer : 0;
	frag_spec_data.tonemap_exposure =
		Com_Clamp( 0.1f, 8.0f, r_tonemapExposure->value );
	frag_spec_data.bloom_soft_knee =
		Com_Clamp( 0.0f, 1.0f, r_bloom_soft_knee->value );
	{
		const int color_grade_mode = vk_color_grade_mode();
		const qboolean use_lgg =
			vk_color_grade_uses_lgg( color_grade_mode );
		const qboolean use_lut =
			vk_color_grade_uses_lut( color_grade_mode );
		const float sourceWhitePoint = r_colorGradeWhitePoint ?
			Com_Clamp( 1000.0f, 40000.0f,
				r_colorGradeWhitePoint->value ) : 6504.0f;
		const float targetWhitePoint = r_colorGradeAdaptWhitePoint ?
			Com_Clamp( 1000.0f, 40000.0f,
				r_colorGradeAdaptWhitePoint->value ) : 6504.0f;
		int lutSize = 0;

		frag_spec_data.color_grade_mode = color_grade_mode;
		frag_spec_data.color_grade_lut_scale = r_colorGradeLUTScale ?
			Com_Clamp( 1.0f, 32.0f, r_colorGradeLUTScale->value ) : 4.0f;

		if ( use_lgg ) {
			vk_parse_vec3_cvar( r_colorGradeLift,
				0.0f, 0.0f, 0.0f, -1.0f, 1.0f,
				frag_spec_data.grade_lift );
			vk_parse_vec3_cvar( r_colorGradeGamma,
				1.0f, 1.0f, 1.0f, 0.1f, 8.0f,
				frag_spec_data.grade_gamma );
			vk_parse_vec3_cvar( r_colorGradeGain,
				1.0f, 1.0f, 1.0f, 0.0f, 8.0f,
				frag_spec_data.grade_gain );
			vk_build_bradford_adaptation( sourceWhitePoint,
				targetWhitePoint, frag_spec_data.white_point );
		} else {
			frag_spec_data.grade_lift[0] = 0.0f;
			frag_spec_data.grade_lift[1] = 0.0f;
			frag_spec_data.grade_lift[2] = 0.0f;
			frag_spec_data.grade_gamma[0] = 1.0f;
			frag_spec_data.grade_gamma[1] = 1.0f;
			frag_spec_data.grade_gamma[2] = 1.0f;
			frag_spec_data.grade_gain[0] = 1.0f;
			frag_spec_data.grade_gain[1] = 1.0f;
			frag_spec_data.grade_gain[2] = 1.0f;
			vk_set_identity_3x3( frag_spec_data.white_point );
		}

		if ( use_lut ) {
			(void)vk_color_grade_lut_image( &lutSize );
		}
		frag_spec_data.color_grade_lut_size = use_lut ? lutSize : 0;
	}
	frag_spec_data.crt_amount = r_crtAmount ?
		Com_Clamp( 0.0f, 1.0f, r_crtAmount->value ) : 1.0f;
	frag_spec_data.crt_scanline_strength = r_crtScanlineStrength ?
		Com_Clamp( 0.0f, 1.0f, r_crtScanlineStrength->value ) : 0.55f;
	frag_spec_data.crt_mask_strength = r_crtMaskStrength ?
		Com_Clamp( 0.0f, 1.0f, r_crtMaskStrength->value ) : 0.35f;
	frag_spec_data.crt_curvature = r_crtCurvature ?
		Com_Clamp( 0.0f, 0.25f, r_crtCurvature->value ) : 0.01f;
	frag_spec_data.crt_chromatic = r_crtChromatic ?
		Com_Clamp( 0.0f, 8.0f, r_crtChromatic->value ) : 1.35f;
	frag_spec_data.crt_mode =
		( r_crt && r_crt->integer && frag_spec_data.crt_amount > 0.001f &&
		  ( program_index == 0 || program_index == 3 ) ) ? 1 : 0;

	if ( !vk_surface_format_color_depth( vk.present_format.format, &frag_spec_data.depth_r, &frag_spec_data.depth_g, &frag_spec_data.depth_b ) )
		ri.Printf( PRINT_ALL, "Format %s not recognized, dither to assume 8bpc\n", vk_format_string( vk.base_format.format ) );

	spec_entries[0].constantID = 0;
	spec_entries[0].offset = offsetof( struct FragSpecData, gamma );
	spec_entries[0].size = sizeof( frag_spec_data.gamma );

	spec_entries[1].constantID = 1;
	spec_entries[1].offset = offsetof( struct FragSpecData, overbright );
	spec_entries[1].size = sizeof( frag_spec_data.overbright );

	spec_entries[2].constantID = 2;
	spec_entries[2].offset = offsetof( struct FragSpecData, greyscale );
	spec_entries[2].size = sizeof( frag_spec_data.greyscale );

	spec_entries[3].constantID = 3;
	spec_entries[3].offset = offsetof( struct FragSpecData, bloom_threshold );
	spec_entries[3].size = sizeof( frag_spec_data.bloom_threshold );

	spec_entries[4].constantID = 4;
	spec_entries[4].offset = offsetof( struct FragSpecData, bloom_intensity );
	spec_entries[4].size = sizeof( frag_spec_data.bloom_intensity );

	spec_entries[5].constantID = 5;
	spec_entries[5].offset = offsetof( struct FragSpecData, bloom_threshold_mode );
	spec_entries[5].size = sizeof( frag_spec_data.bloom_threshold_mode );

	spec_entries[6].constantID = 6;
	spec_entries[6].offset = offsetof( struct FragSpecData, bloom_modulate );
	spec_entries[6].size = sizeof( frag_spec_data.bloom_modulate );

	spec_entries[7].constantID = 7;
	spec_entries[7].offset = offsetof( struct FragSpecData, dither );
	spec_entries[7].size = sizeof( frag_spec_data.dither );

	spec_entries[8].constantID = 8;
	spec_entries[8].offset = offsetof( struct FragSpecData, depth_r );
	spec_entries[8].size = sizeof( frag_spec_data.depth_r );

	spec_entries[9].constantID = 9;
	spec_entries[9].offset = offsetof(struct FragSpecData, depth_g);
	spec_entries[9].size = sizeof(frag_spec_data.depth_g);

	spec_entries[10].constantID = 10;
	spec_entries[10].offset = offsetof(struct FragSpecData, depth_b);
	spec_entries[10].size = sizeof(frag_spec_data.depth_b);

	#define VK_FRAG_SPEC_FIELD( index_, field_ ) do { \
		spec_entries[index_].constantID = (index_); \
		spec_entries[index_].offset = offsetof( struct FragSpecData, field_ ); \
		spec_entries[index_].size = sizeof( frag_spec_data.field_ ); \
	} while ( 0 )
	#define VK_FRAG_SPEC_ARRAY( index_, field_, element_ ) do { \
		spec_entries[index_].constantID = (index_); \
		spec_entries[index_].offset = offsetof( struct FragSpecData, field_ ) + \
			sizeof( frag_spec_data.field_[0] ) * (element_); \
		spec_entries[index_].size = sizeof( frag_spec_data.field_[0] ); \
	} while ( 0 )

	VK_FRAG_SPEC_FIELD( 11, output_color_space );
	VK_FRAG_SPEC_FIELD( 12, hdr_paper_white );
	VK_FRAG_SPEC_FIELD( 13, hdr_max_luminance );
	VK_FRAG_SPEC_FIELD( 14, tonemap_mode );
	VK_FRAG_SPEC_FIELD( 15, tonemap_exposure );
	VK_FRAG_SPEC_FIELD( 16, bloom_soft_knee );
	VK_FRAG_SPEC_FIELD( 17, scene_linear_mode );
	VK_FRAG_SPEC_FIELD( 18, color_grade_mode );
	VK_FRAG_SPEC_ARRAY( 19, grade_lift, 0 );
	VK_FRAG_SPEC_ARRAY( 20, grade_lift, 1 );
	VK_FRAG_SPEC_ARRAY( 21, grade_lift, 2 );
	VK_FRAG_SPEC_ARRAY( 22, grade_gamma, 0 );
	VK_FRAG_SPEC_ARRAY( 23, grade_gamma, 1 );
	VK_FRAG_SPEC_ARRAY( 24, grade_gamma, 2 );
	VK_FRAG_SPEC_ARRAY( 25, grade_gain, 0 );
	VK_FRAG_SPEC_ARRAY( 26, grade_gain, 1 );
	VK_FRAG_SPEC_ARRAY( 27, grade_gain, 2 );
	VK_FRAG_SPEC_ARRAY( 28, white_point, 0 );
	VK_FRAG_SPEC_ARRAY( 29, white_point, 1 );
	VK_FRAG_SPEC_ARRAY( 30, white_point, 2 );
	VK_FRAG_SPEC_ARRAY( 31, white_point, 3 );
	VK_FRAG_SPEC_ARRAY( 32, white_point, 4 );
	VK_FRAG_SPEC_ARRAY( 33, white_point, 5 );
	VK_FRAG_SPEC_ARRAY( 34, white_point, 6 );
	VK_FRAG_SPEC_ARRAY( 35, white_point, 7 );
	VK_FRAG_SPEC_ARRAY( 36, white_point, 8 );
	VK_FRAG_SPEC_FIELD( 37, color_grade_lut_size );
	VK_FRAG_SPEC_FIELD( 38, color_grade_lut_scale );
	VK_FRAG_SPEC_FIELD( 39, crt_mode );
	VK_FRAG_SPEC_FIELD( 40, crt_amount );
	VK_FRAG_SPEC_FIELD( 41, crt_scanline_strength );
	VK_FRAG_SPEC_FIELD( 42, crt_mask_strength );
	VK_FRAG_SPEC_FIELD( 43, crt_curvature );
	VK_FRAG_SPEC_FIELD( 44, crt_chromatic );

	#undef VK_FRAG_SPEC_ARRAY
	#undef VK_FRAG_SPEC_FIELD

	frag_spec_info.mapEntryCount = ARRAY_LEN( spec_entries );
	frag_spec_info.pMapEntries = spec_entries;
	frag_spec_info.dataSize = sizeof( frag_spec_data );
	frag_spec_info.pData = &frag_spec_data;

	shader_stages[1].pSpecializationInfo =
		( program_index == 4 || program_index == 5 || program_index == 6 ||
		  program_index == 8 || program_index == 9 ) ?
			NULL : &frag_spec_info;

	//
	// Primitive assembly.
	//
	input_assembly_state.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	input_assembly_state.pNext = NULL;
	input_assembly_state.flags = 0;
	input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	input_assembly_state.primitiveRestartEnable = VK_FALSE;

	//
	// Viewport.
	//
	if ( program_index == 0 ) {
		// gamma correction
		viewport.x = 0.0 + vk.blitX0;
		viewport.y = 0.0 + vk.blitY0;
		viewport.width = gls.windowWidth - vk.blitX0 * 2;
		viewport.height = gls.windowHeight - vk.blitY0 * 2;
	} else {
		// other post-processing
		viewport.x = 0.0;
		viewport.y = 0.0;
		viewport.width = width;
		viewport.height = height;
	}

	viewport.minDepth = 0.0;
	viewport.maxDepth = 1.0;

	scissor.offset.x = viewport.x;
	scissor.offset.y = viewport.y;
	scissor.extent.width = viewport.width;
	scissor.extent.height = viewport.height;

	viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport_state.pNext = NULL;
	viewport_state.flags = 0;
	viewport_state.viewportCount = 1;
	viewport_state.pViewports = &viewport;
	viewport_state.scissorCount = 1;
	viewport_state.pScissors = &scissor;

	//
	// Rasterization.
	//
	rasterization_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterization_state.pNext = NULL;
	rasterization_state.flags = 0;
	rasterization_state.depthClampEnable = VK_FALSE;
	rasterization_state.rasterizerDiscardEnable = VK_FALSE;
	rasterization_state.polygonMode = VK_POLYGON_MODE_FILL;
	//rasterization_state.cullMode = VK_CULL_MODE_BACK_BIT; // VK_CULL_MODE_NONE;
	rasterization_state.cullMode = VK_CULL_MODE_NONE;
	rasterization_state.frontFace = VK_FRONT_FACE_CLOCKWISE; // Q3 defaults to clockwise vertex order
	rasterization_state.depthBiasEnable = VK_FALSE;
	rasterization_state.depthBiasConstantFactor = 0.0f;
	rasterization_state.depthBiasClamp = 0.0f;
	rasterization_state.depthBiasSlopeFactor = 0.0f;
	rasterization_state.lineWidth = 1.0f;

	multisample_state.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample_state.pNext = NULL;
	multisample_state.flags = 0;
	multisample_state.rasterizationSamples = samples;
	multisample_state.sampleShadingEnable = VK_FALSE;
	multisample_state.minSampleShading = 1.0f;
	multisample_state.pSampleMask = NULL;
	multisample_state.alphaToCoverageEnable = VK_FALSE;
	multisample_state.alphaToOneEnable = VK_FALSE;

	Com_Memset(&attachment_blend_state, 0, sizeof(attachment_blend_state));
	attachment_blend_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	if ( blend ) {
		attachment_blend_state.blendEnable = VK_TRUE;
		if ( program_index == 4 || program_index == 6 ) {
			attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			attachment_blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			attachment_blend_state.colorBlendOp = VK_BLEND_OP_ADD;
			attachment_blend_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			attachment_blend_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			attachment_blend_state.alphaBlendOp = VK_BLEND_OP_ADD;
		} else if ( program_index == 7 ) {
			attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
			attachment_blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
			attachment_blend_state.colorBlendOp = VK_BLEND_OP_ADD;
			attachment_blend_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			attachment_blend_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			attachment_blend_state.alphaBlendOp = VK_BLEND_OP_ADD;
		} else {
			attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
			attachment_blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		}
	} else {
		attachment_blend_state.blendEnable = VK_FALSE;
	}

	blend_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend_state.pNext = NULL;
	blend_state.flags = 0;
	blend_state.logicOpEnable = VK_FALSE;
	blend_state.logicOp = VK_LOGIC_OP_COPY;
	blend_state.attachmentCount = 1;
	blend_state.pAttachments = &attachment_blend_state;
	blend_state.blendConstants[0] = 0.0f;
	blend_state.blendConstants[1] = 0.0f;
	blend_state.blendConstants[2] = 0.0f;
	blend_state.blendConstants[3] = 0.0f;

	Com_Memset( &depth_stencil_state, 0, sizeof( depth_stencil_state ) );

	depth_stencil_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depth_stencil_state.pNext = NULL;
	depth_stencil_state.flags = 0;
	depth_stencil_state.depthTestEnable = VK_FALSE;
	depth_stencil_state.depthWriteEnable = VK_FALSE;
	depth_stencil_state.depthCompareOp = VK_COMPARE_OP_NEVER;
	depth_stencil_state.depthBoundsTestEnable = VK_FALSE;
	depth_stencil_state.stencilTestEnable = VK_FALSE;
	depth_stencil_state.minDepthBounds = 0.0f;
	depth_stencil_state.maxDepthBounds = 1.0f;

	create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	create_info.pNext = NULL;
	create_info.flags = 0;
	create_info.stageCount = 2;
	create_info.pStages = shader_stages;
	create_info.pVertexInputState = &vertex_input_state;
	create_info.pInputAssemblyState = &input_assembly_state;
	create_info.pTessellationState = NULL;
	create_info.pViewportState = &viewport_state;
	create_info.pRasterizationState = &rasterization_state;
	create_info.pMultisampleState = &multisample_state;
	create_info.pDepthStencilState = (program_index == 2) ? &depth_stencil_state : NULL;
	create_info.pDepthStencilState = &depth_stencil_state;
	create_info.pColorBlendState = &blend_state;
	create_info.pDynamicState = NULL;
	create_info.layout = layout;
	create_info.renderPass = renderpass;
	create_info.subpass = 0;
	create_info.basePipelineHandle = VK_NULL_HANDLE;
	create_info.basePipelineIndex = -1;

	VK_CHECK( qvkCreateGraphicsPipelines( vk.device, VK_NULL_HANDLE, 1, &create_info, NULL, pipeline ) );

	SET_OBJECT_NAME( *pipeline, pipeline_name, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT );
}


void vk_create_blur_pipeline( uint32_t index, uint32_t width, uint32_t height, qboolean horizontal_pass )
{
	VkPipelineShaderStageCreateInfo shader_stages[2];
	VkPipelineVertexInputStateCreateInfo vertex_input_state;
	VkPipelineInputAssemblyStateCreateInfo input_assembly_state;
	VkPipelineRasterizationStateCreateInfo rasterization_state;
	VkPipelineViewportStateCreateInfo viewport_state;
	VkPipelineMultisampleStateCreateInfo multisample_state;
	VkPipelineColorBlendStateCreateInfo blend_state;
	VkPipelineColorBlendAttachmentState attachment_blend_state;
	VkGraphicsPipelineCreateInfo create_info;
	VkViewport viewport;
	VkRect2D scissor;
	float frag_spec_data[3]; // x-offset, y-offset, correction
	VkSpecializationMapEntry spec_entries[3];
	VkSpecializationInfo frag_spec_info;
	VkPipeline *pipeline;

	pipeline = &vk.blur_pipeline[ index ];

	if ( *pipeline != VK_NULL_HANDLE ) {
		vk_wait_idle();
		qvkDestroyPipeline( vk.device, *pipeline, NULL );
		*pipeline = VK_NULL_HANDLE;
	}

	vertex_input_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertex_input_state.pNext = NULL;
	vertex_input_state.flags = 0;
	vertex_input_state.vertexBindingDescriptionCount = 0;
	vertex_input_state.pVertexBindingDescriptions = NULL;
	vertex_input_state.vertexAttributeDescriptionCount = 0;
	vertex_input_state.pVertexBindingDescriptions = NULL;

	// shaders
	set_shader_stage_desc( shader_stages+0, VK_SHADER_STAGE_VERTEX_BIT, vk.modules.gamma_vs, "main" );
	set_shader_stage_desc( shader_stages+1, VK_SHADER_STAGE_FRAGMENT_BIT, vk.modules.blur_fs, "main" );

	frag_spec_data[0] = 1.2 / (float) width; // x offset
	frag_spec_data[1] = 1.2 / (float) height; // y offset
	frag_spec_data[2] = 1.0; // intensity?

	if ( horizontal_pass ) {
		frag_spec_data[1] = 0.0;
	} else {
		frag_spec_data[0] = 0.0;
	}

	spec_entries[0].constantID = 0;
	spec_entries[0].offset = 0 * sizeof( float );
	spec_entries[0].size = sizeof( float );

	spec_entries[1].constantID = 1;
	spec_entries[1].offset = 1 * sizeof( float );
	spec_entries[1].size = sizeof( float );

	spec_entries[2].constantID = 2;
	spec_entries[2].offset = 2 * sizeof( float );
	spec_entries[2].size = sizeof( float );

	frag_spec_info.mapEntryCount = 3;
	frag_spec_info.pMapEntries = spec_entries;
	frag_spec_info.dataSize = 3 * sizeof( float );
	frag_spec_info.pData = &frag_spec_data[0];

	shader_stages[1].pSpecializationInfo = &frag_spec_info;

	//
	// Primitive assembly.
	//
	input_assembly_state.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	input_assembly_state.pNext = NULL;
	input_assembly_state.flags = 0;
	input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	input_assembly_state.primitiveRestartEnable = VK_FALSE;

	//
	// Viewport.
	//
	viewport.x = 0.0;
	viewport.y = 0.0;
	viewport.width = width;
	viewport.height = height;
	viewport.minDepth = 0.0;
	viewport.maxDepth = 1.0;

	scissor.offset.x = viewport.x;
	scissor.offset.y = viewport.y;
	scissor.extent.width = viewport.width;
	scissor.extent.height = viewport.height;

	viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport_state.pNext = NULL;
	viewport_state.flags = 0;
	viewport_state.viewportCount = 1;
	viewport_state.pViewports = &viewport;
	viewport_state.scissorCount = 1;
	viewport_state.pScissors = &scissor;

	//
	// Rasterization.
	//
	rasterization_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterization_state.pNext = NULL;
	rasterization_state.flags = 0;
	rasterization_state.depthClampEnable = VK_FALSE;
	rasterization_state.rasterizerDiscardEnable = VK_FALSE;
	rasterization_state.polygonMode = VK_POLYGON_MODE_FILL;
	//rasterization_state.cullMode = VK_CULL_MODE_BACK_BIT; // VK_CULL_MODE_NONE;
	rasterization_state.cullMode = VK_CULL_MODE_NONE;
	rasterization_state.frontFace = VK_FRONT_FACE_CLOCKWISE; // Q3 defaults to clockwise vertex order
	rasterization_state.depthBiasEnable = VK_FALSE;
	rasterization_state.depthBiasConstantFactor = 0.0f;
	rasterization_state.depthBiasClamp = 0.0f;
	rasterization_state.depthBiasSlopeFactor = 0.0f;
	rasterization_state.lineWidth = 1.0f;

	multisample_state.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample_state.pNext = NULL;
	multisample_state.flags = 0;
	multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	multisample_state.sampleShadingEnable = VK_FALSE;
	multisample_state.minSampleShading = 1.0f;
	multisample_state.pSampleMask = NULL;
	multisample_state.alphaToCoverageEnable = VK_FALSE;
	multisample_state.alphaToOneEnable = VK_FALSE;

	Com_Memset(&attachment_blend_state, 0, sizeof(attachment_blend_state));
	attachment_blend_state.blendEnable = VK_FALSE;
	attachment_blend_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	blend_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend_state.pNext = NULL;
	blend_state.flags = 0;
	blend_state.logicOpEnable = VK_FALSE;
	blend_state.logicOp = VK_LOGIC_OP_COPY;
	blend_state.attachmentCount = 1;
	blend_state.pAttachments = &attachment_blend_state;
	blend_state.blendConstants[0] = 0.0f;
	blend_state.blendConstants[1] = 0.0f;
	blend_state.blendConstants[2] = 0.0f;
	blend_state.blendConstants[3] = 0.0f;

	create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	create_info.pNext = NULL;
	create_info.flags = 0;
	create_info.stageCount = 2;
	create_info.pStages = shader_stages;
	create_info.pVertexInputState = &vertex_input_state;
	create_info.pInputAssemblyState = &input_assembly_state;
	create_info.pTessellationState = NULL;
	create_info.pViewportState = &viewport_state;
	create_info.pRasterizationState = &rasterization_state;
	create_info.pMultisampleState = &multisample_state;
	create_info.pDepthStencilState = NULL;
	create_info.pColorBlendState = &blend_state;
	create_info.pDynamicState = NULL;
	create_info.layout = vk_pipeline_layout_handle( RTX_VK_PIPELINE_LAYOUT_POST_PROCESS ); // one input attachment
	create_info.renderPass = vk.render_pass.blur[ index ];
	create_info.subpass = 0;
	create_info.basePipelineHandle = VK_NULL_HANDLE;
	create_info.basePipelineIndex = -1;

	VK_CHECK( qvkCreateGraphicsPipelines( vk.device, VK_NULL_HANDLE, 1, &create_info, NULL, pipeline ) );

	SET_OBJECT_NAME( *pipeline, va( "%s blur pipeline %i", horizontal_pass ? "horizontal" : "vertical", index/2 + 1 ), VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT );
}


static VkVertexInputBindingDescription bindings[8];
static VkVertexInputAttributeDescription attribs[8];
static uint32_t num_binds;
static uint32_t num_attrs;

static void push_bind( uint32_t binding, uint32_t stride )
{
	bindings[ num_binds ].binding = binding;
	bindings[ num_binds ].stride = stride;
	bindings[ num_binds ].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	num_binds++;
}

static void push_attr( uint32_t location, uint32_t binding, VkFormat format )
{
	attribs[ num_attrs ].location = location;
	attribs[ num_attrs ].binding = binding;
	attribs[ num_attrs ].format = format;
	attribs[ num_attrs ].offset = 0;
	num_attrs++;
}


VkPipeline create_pipeline( const Vk_Pipeline_Def *def, renderPass_t renderPassIndex, uint32_t def_index ) {
	VkShaderModule *vs_module = NULL;
	VkShaderModule *fs_module = NULL;
	//int32_t vert_spec_data[1]; // clippping
	floatint_t frag_spec_data[12]; // 0:alpha-test-func, 1:alpha-test-value, 2:depth-fragment, 3:alpha-to-coverage, 4:color_mode, 5:abs_light, 6:multitexture mode, 7:discard mode, 8: ident.color, 9 - ident.alpha, 10 - acff, 11 - depth fade
	VkSpecializationMapEntry spec_entries[13];
	//VkSpecializationInfo vert_spec_info;
	VkSpecializationInfo frag_spec_info;
	VkPipelineVertexInputStateCreateInfo vertex_input_state;
	VkPipelineInputAssemblyStateCreateInfo input_assembly_state;
	VkPipelineRasterizationStateCreateInfo rasterization_state;
	VkPipelineViewportStateCreateInfo viewport_state;
	VkPipelineMultisampleStateCreateInfo multisample_state;
	VkPipelineDepthStencilStateCreateInfo depth_stencil_state;
	VkPipelineColorBlendStateCreateInfo blend_state;
	VkPipelineColorBlendAttachmentState attachment_blend_state;
	VkPipelineDynamicStateCreateInfo dynamic_state;
	VkDynamicState dynamic_state_array[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
		VK_DYNAMIC_STATE_DEPTH_BIAS
	};
	VkGraphicsPipelineCreateInfo create_info;
	VkPipeline pipeline;
	VkPipelineShaderStageCreateInfo shader_stages[2];
	VkBool32 alphaToCoverage = VK_FALSE;
	unsigned int atest_bits;
	unsigned int state_bits = def->state_bits;

	switch ( def->shader_type ) {

		case TYPE_SIGNLE_TEXTURE_LIGHTING:
			vs_module = &vk.modules.vert.light[0];
			fs_module = &vk.modules.frag.light[0][0];
			break;

		case TYPE_SIGNLE_TEXTURE_LIGHTING_LINEAR:
			vs_module = &vk.modules.vert.light[0];
			fs_module = &vk.modules.frag.light[1][0];
			break;

		case TYPE_SIGNLE_TEXTURE_DF:
			state_bits |= GLS_DEPTHMASK_TRUE;
			vs_module = &vk.modules.vert.ident1[0][0][0];
			fs_module = &vk.modules.frag.gen0_df;
			break;

		case TYPE_SIGNLE_TEXTURE_FIXED_COLOR:
			vs_module = &vk.modules.vert.fixed[0][0][0];
			fs_module = &vk.modules.frag.fixed[0][0];
			break;

		case TYPE_SIGNLE_TEXTURE_FIXED_COLOR_ENV:
			vs_module = &vk.modules.vert.fixed[0][1][0];
			fs_module = &vk.modules.frag.fixed[0][0];
			break;

		case TYPE_SIGNLE_TEXTURE_ENT_COLOR:
			vs_module = &vk.modules.vert.fixed[0][0][0];
			fs_module = &vk.modules.frag.ent[0][0];
			break;

		case TYPE_SIGNLE_TEXTURE_ENT_COLOR_ENV:
			vs_module = &vk.modules.vert.fixed[0][1][0];
			fs_module = &vk.modules.frag.ent[0][0];
			break;

		case TYPE_SIGNLE_TEXTURE:
			vs_module = &vk.modules.vert.gen[0][0][0][0];
			fs_module = &vk.modules.frag.gen[0][0][0];
			break;

		case TYPE_SIGNLE_TEXTURE_ENV:
			vs_module = &vk.modules.vert.gen[0][0][1][0];
			fs_module = &vk.modules.frag.gen[0][0][0];
			break;

		case TYPE_SIGNLE_TEXTURE_IDENTITY:
			vs_module = &vk.modules.vert.ident1[0][0][0];
			fs_module = &vk.modules.frag.ident1[0][0];
			break;

		case TYPE_SIGNLE_TEXTURE_IDENTITY_ENV:
			vs_module = &vk.modules.vert.ident1[0][1][0];
			fs_module = &vk.modules.frag.ident1[0][0];
			break;

		case TYPE_MULTI_TEXTURE_ADD2_IDENTITY:
		case TYPE_MULTI_TEXTURE_MUL2_IDENTITY:
			vs_module = &vk.modules.vert.ident1[1][0][0];
			fs_module = &vk.modules.frag.ident1[1][0];
			break;

		case TYPE_MULTI_TEXTURE_ADD2_IDENTITY_ENV:
		case TYPE_MULTI_TEXTURE_MUL2_IDENTITY_ENV:
			vs_module = &vk.modules.vert.ident1[1][1][0];
			fs_module = &vk.modules.frag.ident1[1][0];
			break;

		case TYPE_MULTI_TEXTURE_ADD2_FIXED_COLOR:
		case TYPE_MULTI_TEXTURE_MUL2_FIXED_COLOR:
			vs_module = &vk.modules.vert.fixed[1][0][0];
			fs_module = &vk.modules.frag.fixed[1][0];
			break;

		case TYPE_MULTI_TEXTURE_ADD2_FIXED_COLOR_ENV:
		case TYPE_MULTI_TEXTURE_MUL2_FIXED_COLOR_ENV:
			vs_module = &vk.modules.vert.fixed[1][1][0];
			fs_module = &vk.modules.frag.fixed[1][0];
			break;

		case TYPE_MULTI_TEXTURE_MUL2:
		case TYPE_MULTI_TEXTURE_ADD2_1_1:
		case TYPE_MULTI_TEXTURE_ADD2:
			vs_module = &vk.modules.vert.gen[1][0][0][0];
			fs_module = &vk.modules.frag.gen[1][0][0];
			break;

		case TYPE_MULTI_TEXTURE_MUL2_ENV:
		case TYPE_MULTI_TEXTURE_ADD2_1_1_ENV:
		case TYPE_MULTI_TEXTURE_ADD2_ENV:
			vs_module = &vk.modules.vert.gen[1][0][1][0];
			fs_module = &vk.modules.frag.gen[1][0][0];
			break;

		case TYPE_MULTI_TEXTURE_MUL3:
		case TYPE_MULTI_TEXTURE_ADD3_1_1:
		case TYPE_MULTI_TEXTURE_ADD3:
			vs_module = &vk.modules.vert.gen[2][0][0][0];
			fs_module = &vk.modules.frag.gen[2][0][0];
			break;

		case TYPE_MULTI_TEXTURE_MUL3_ENV:
		case TYPE_MULTI_TEXTURE_ADD3_1_1_ENV:
		case TYPE_MULTI_TEXTURE_ADD3_ENV:
			vs_module = &vk.modules.vert.gen[2][0][1][0];
			fs_module = &vk.modules.frag.gen[2][0][0];
			break;

		case TYPE_BLEND2_ADD:
		case TYPE_BLEND2_MUL:
		case TYPE_BLEND2_ALPHA:
		case TYPE_BLEND2_ONE_MINUS_ALPHA:
		case TYPE_BLEND2_MIX_ALPHA:
		case TYPE_BLEND2_MIX_ONE_MINUS_ALPHA:
		case TYPE_BLEND2_DST_COLOR_SRC_ALPHA:
			vs_module = &vk.modules.vert.gen[1][1][0][0];
			fs_module = &vk.modules.frag.gen[1][1][0];
			break;

		case TYPE_BLEND2_ADD_ENV:
		case TYPE_BLEND2_MUL_ENV:
		case TYPE_BLEND2_ALPHA_ENV:
		case TYPE_BLEND2_ONE_MINUS_ALPHA_ENV:
		case TYPE_BLEND2_MIX_ALPHA_ENV:
		case TYPE_BLEND2_MIX_ONE_MINUS_ALPHA_ENV:
		case TYPE_BLEND2_DST_COLOR_SRC_ALPHA_ENV:
			vs_module = &vk.modules.vert.gen[1][1][1][0];
			fs_module = &vk.modules.frag.gen[1][1][0];
			break;

		case TYPE_BLEND3_ADD:
		case TYPE_BLEND3_MUL:
		case TYPE_BLEND3_ALPHA:
		case TYPE_BLEND3_ONE_MINUS_ALPHA:
		case TYPE_BLEND3_MIX_ALPHA:
		case TYPE_BLEND3_MIX_ONE_MINUS_ALPHA:
		case TYPE_BLEND3_DST_COLOR_SRC_ALPHA:
			vs_module = &vk.modules.vert.gen[2][1][0][0];
			fs_module = &vk.modules.frag.gen[2][1][0];
			break;

		case TYPE_BLEND3_ADD_ENV:
		case TYPE_BLEND3_MUL_ENV:
		case TYPE_BLEND3_ALPHA_ENV:
		case TYPE_BLEND3_ONE_MINUS_ALPHA_ENV:
		case TYPE_BLEND3_MIX_ALPHA_ENV:
		case TYPE_BLEND3_MIX_ONE_MINUS_ALPHA_ENV:
		case TYPE_BLEND3_DST_COLOR_SRC_ALPHA_ENV:
			vs_module = &vk.modules.vert.gen[2][1][1][0];
			fs_module = &vk.modules.frag.gen[2][1][0];
			break;

		case TYPE_COLOR_BLACK:
		case TYPE_COLOR_WHITE:
		case TYPE_COLOR_GREEN:
		case TYPE_COLOR_RED:
			vs_module = &vk.modules.color_vs;
			fs_module = &vk.modules.color_fs;
			break;

		case TYPE_FOG_ONLY:
			vs_module = &vk.modules.fog_vs;
			fs_module = &vk.modules.fog_fs;
			break;

		case TYPE_DOT:
			vs_module = &vk.modules.dot_vs;
			fs_module = &vk.modules.dot_fs;
			break;

		case TYPE_LIQUID:
			vs_module = &vk.modules.liquid_vs;
			fs_module = &vk.modules.liquid_fs;
			break;

		case TYPE_CSM_SHADOW:
			vs_module = &vk.modules.vert.csm_shadow;
			fs_module = &vk.modules.frag.csm_shadow;
			break;

		default:
			ri.Error(ERR_DROP, "create_pipeline: unknown shader type %i\n", def->shader_type);
			return 0;
	}

	if ( def->fog_stage ) {
		switch ( def->shader_type ) {
			case TYPE_FOG_ONLY:
			case TYPE_DOT:
			case TYPE_LIQUID:
			case TYPE_SIGNLE_TEXTURE_DF:
			case TYPE_COLOR_BLACK:
			case TYPE_COLOR_WHITE:
			case TYPE_COLOR_GREEN:
			case TYPE_COLOR_RED:
				break;
			default:
				// switch to fogged modules
				vs_module++;
				fs_module++;
				break;
		}
	}

	set_shader_stage_desc(shader_stages+0, VK_SHADER_STAGE_VERTEX_BIT, *vs_module, "main");
	set_shader_stage_desc(shader_stages+1, VK_SHADER_STAGE_FRAGMENT_BIT, *fs_module, "main");

	//Com_Memset( vert_spec_data, 0, sizeof( vert_spec_data ) );
	Com_Memset( frag_spec_data, 0, sizeof( frag_spec_data ) );

	//vert_spec_data[0] = def->clipping_plane ? 1 : 0;

	// fragment shader specialization data
	atest_bits = state_bits & GLS_ATEST_BITS;
	switch ( atest_bits ) {
		case GLS_ATEST_GT_0:
			frag_spec_data[0].i = 1; // not equal
			frag_spec_data[1].f = 0.0f;
			break;
		case GLS_ATEST_LT_80:
			frag_spec_data[0].i = 2; // less than
			frag_spec_data[1].f = 0.5f;
			break;
		case GLS_ATEST_GE_80:
			frag_spec_data[0].i = 3; // greater or equal
			frag_spec_data[1].f = 0.5f;
			break;
		default:
			frag_spec_data[0].i = 0;
			frag_spec_data[1].f = 0.0f;
			break;
	};

	// depth fragment threshold
	frag_spec_data[2].f = 0.85f;

#if 0
	if ( r_ext_alpha_to_coverage && r_ext_alpha_to_coverage->integer &&
		vkSamples != VK_SAMPLE_COUNT_1_BIT && frag_spec_data[0].i ) {
		frag_spec_data[3].i = 1;
		alphaToCoverage = VK_TRUE;
	}
#endif

	// constant color
	switch ( def->shader_type ) {
		default: frag_spec_data[4].i = 0; break;
		case TYPE_COLOR_WHITE: frag_spec_data[4].i = 1; break;
		case TYPE_COLOR_GREEN: frag_spec_data[4].i = 2; break;
		case TYPE_COLOR_RED:   frag_spec_data[4].i = 3; break;
	}

	// abs lighting
	switch ( def->shader_type ) {
		case TYPE_SIGNLE_TEXTURE_LIGHTING:
		case TYPE_SIGNLE_TEXTURE_LIGHTING_LINEAR:
			frag_spec_data[5].i = def->abs_light ? 1 : 0;
		default:
			break;
	}

	// multutexture mode
	switch ( def->shader_type ) {
		case TYPE_MULTI_TEXTURE_MUL2_IDENTITY:
		case TYPE_MULTI_TEXTURE_MUL2_IDENTITY_ENV:
		case TYPE_MULTI_TEXTURE_MUL2_FIXED_COLOR:
		case TYPE_MULTI_TEXTURE_MUL2_FIXED_COLOR_ENV:
		case TYPE_MULTI_TEXTURE_MUL2:
		case TYPE_MULTI_TEXTURE_MUL2_ENV:
		case TYPE_MULTI_TEXTURE_MUL3:
		case TYPE_MULTI_TEXTURE_MUL3_ENV:
		case TYPE_BLEND2_MUL:
		case TYPE_BLEND2_MUL_ENV:
		case TYPE_BLEND3_MUL:
		case TYPE_BLEND3_MUL_ENV:
			frag_spec_data[6].i = 0;
			break;

		case TYPE_MULTI_TEXTURE_ADD2_IDENTITY:
		case TYPE_MULTI_TEXTURE_ADD2_IDENTITY_ENV:
		case TYPE_MULTI_TEXTURE_ADD2_FIXED_COLOR:
		case TYPE_MULTI_TEXTURE_ADD2_FIXED_COLOR_ENV:
		case TYPE_MULTI_TEXTURE_ADD2_1_1:
		case TYPE_MULTI_TEXTURE_ADD2_1_1_ENV:
		case TYPE_MULTI_TEXTURE_ADD3_1_1:
		case TYPE_MULTI_TEXTURE_ADD3_1_1_ENV:
			frag_spec_data[6].i = 1;
			break;

		case TYPE_MULTI_TEXTURE_ADD2:
		case TYPE_MULTI_TEXTURE_ADD2_ENV:
		case TYPE_MULTI_TEXTURE_ADD3:
		case TYPE_MULTI_TEXTURE_ADD3_ENV:
		case TYPE_BLEND2_ADD:
		case TYPE_BLEND2_ADD_ENV:
		case TYPE_BLEND3_ADD:
		case TYPE_BLEND3_ADD_ENV:
			frag_spec_data[6].i = 2;
			break;

		case TYPE_BLEND2_ALPHA:
		case TYPE_BLEND2_ALPHA_ENV:
		case TYPE_BLEND3_ALPHA:
		case TYPE_BLEND3_ALPHA_ENV:
			frag_spec_data[6].i = 3;
			break;

		case TYPE_BLEND2_ONE_MINUS_ALPHA:
		case TYPE_BLEND2_ONE_MINUS_ALPHA_ENV:
		case TYPE_BLEND3_ONE_MINUS_ALPHA:
		case TYPE_BLEND3_ONE_MINUS_ALPHA_ENV:
			frag_spec_data[6].i = 4;
			break;

		case TYPE_BLEND2_MIX_ALPHA:
		case TYPE_BLEND2_MIX_ALPHA_ENV:
		case TYPE_BLEND3_MIX_ALPHA:
		case TYPE_BLEND3_MIX_ALPHA_ENV:
			frag_spec_data[6].i = 5;
			break;

		case TYPE_BLEND2_MIX_ONE_MINUS_ALPHA:
		case TYPE_BLEND2_MIX_ONE_MINUS_ALPHA_ENV:
		case TYPE_BLEND3_MIX_ONE_MINUS_ALPHA:
		case TYPE_BLEND3_MIX_ONE_MINUS_ALPHA_ENV:
			frag_spec_data[6].i = 6;
			break;

		case TYPE_BLEND2_DST_COLOR_SRC_ALPHA:
		case TYPE_BLEND2_DST_COLOR_SRC_ALPHA_ENV:
		case TYPE_BLEND3_DST_COLOR_SRC_ALPHA:
		case TYPE_BLEND3_DST_COLOR_SRC_ALPHA_ENV:
			frag_spec_data[6].i = 7;
			break;

		default:
			break;
	}

	frag_spec_data[8].f = ((float)def->color.rgb) / 255.0;
	frag_spec_data[9].f = ((float)def->color.alpha) / 255.0;

	if ( def->fog_stage ) {
		frag_spec_data[10].i = def->acff;
	} else {
		frag_spec_data[10].i = 0;
	}
	frag_spec_data[11].i = def->depth_fade;

	//
	// vertex module specialization data
	//
#if 0
	spec_entries[0].constantID = 0; // clip_plane
	spec_entries[0].offset = 0 * sizeof( int32_t );
	spec_entries[0].size = sizeof( int32_t );

	vert_spec_info.mapEntryCount = 1;
	vert_spec_info.pMapEntries = spec_entries + 0;
	vert_spec_info.dataSize = 1 * sizeof( int32_t );
	vert_spec_info.pData = &vert_spec_data[0];
	shader_stages[0].pSpecializationInfo = &vert_spec_info;
#endif
	shader_stages[0].pSpecializationInfo = NULL;

	//
	// fragment module specialization data
	//

	spec_entries[1].constantID = 0;  // alpha-test-function
	spec_entries[1].offset = 0 * sizeof( int32_t );
	spec_entries[1].size = sizeof( int32_t );

	spec_entries[2].constantID = 1; // alpha-test-value
	spec_entries[2].offset = 1 * sizeof( int32_t );
	spec_entries[2].size = sizeof( float );

	spec_entries[3].constantID = 2; // depth-fragment
	spec_entries[3].offset = 2 * sizeof( int32_t );
	spec_entries[3].size = sizeof( float );

	spec_entries[4].constantID = 3; // alpha-to-coverage
	spec_entries[4].offset = 3 * sizeof( int32_t );
	spec_entries[4].size = sizeof( int32_t );

	spec_entries[5].constantID = 4; // color_mode
	spec_entries[5].offset = 4 * sizeof( int32_t );
	spec_entries[5].size = sizeof( int32_t );

	spec_entries[6].constantID = 5; // abs_light
	spec_entries[6].offset = 5 * sizeof( int32_t );
	spec_entries[6].size = sizeof( int32_t );

	spec_entries[7].constantID = 6; // multitexture mode
	spec_entries[7].offset = 6 * sizeof( int32_t );
	spec_entries[7].size = sizeof( int32_t );

	spec_entries[8].constantID = 7; // discard mode
	spec_entries[8].offset = 7 * sizeof( int32_t );
	spec_entries[8].size = sizeof( int32_t );

	spec_entries[9].constantID = 8; // fixed color
	spec_entries[9].offset = 8 * sizeof( int32_t );
	spec_entries[9].size = sizeof( float );

	spec_entries[10].constantID = 9; // fixed alpha
	spec_entries[10].offset = 9 * sizeof( int32_t );
	spec_entries[10].size = sizeof( float );

	spec_entries[11].constantID = 10; // acff
	spec_entries[11].offset = 10 * sizeof( int32_t );
	spec_entries[11].size = sizeof( int32_t );

	spec_entries[12].constantID = 11; // depth fade
	spec_entries[12].offset = 11 * sizeof( int32_t );
	spec_entries[12].size = sizeof( int32_t );

	frag_spec_info.mapEntryCount = 12;
	frag_spec_info.pMapEntries = spec_entries + 1;
	frag_spec_info.dataSize = sizeof( int32_t ) * 12;
	frag_spec_info.pData = &frag_spec_data[0];
	shader_stages[1].pSpecializationInfo =
		def->shader_type == TYPE_LIQUID ? NULL : &frag_spec_info;

	//
	// Vertex input
	//
	num_binds = num_attrs = 0;
	switch ( def->shader_type ) {

		case TYPE_LIQUID:
			push_bind( 0, sizeof( vec4_t ) );
			push_bind( 5, sizeof( vec4_t ) );
			push_attr( 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT );
			push_attr( 5, 5, VK_FORMAT_R32G32B32A32_SFLOAT );
			break;

		case TYPE_FOG_ONLY:
		case TYPE_DOT:
			push_bind( 0, sizeof( vec4_t ) );					// xyz array
			push_attr( 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT );
			break;

		case TYPE_COLOR_BLACK:
		case TYPE_COLOR_WHITE:
		case TYPE_COLOR_GREEN:
		case TYPE_COLOR_RED:
			push_bind( 0, sizeof( vec4_t ) );					// xyz array
			push_attr( 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT );
			break;

		case TYPE_CSM_SHADOW:
			push_bind( 0, sizeof( vec4_t ) );					// xyz array
			push_attr( 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT );
			break;

		case TYPE_SIGNLE_TEXTURE_DF:
		case TYPE_SIGNLE_TEXTURE_IDENTITY:
		case TYPE_SIGNLE_TEXTURE_FIXED_COLOR:
		case TYPE_SIGNLE_TEXTURE_ENT_COLOR:
			push_bind( 0, sizeof( vec4_t ) );					// xyz array
			push_bind( 2, sizeof( vec2_t ) );					// st0 array
			push_attr( 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT );
			push_attr( 2, 2, VK_FORMAT_R32G32_SFLOAT );
			break;

		case TYPE_SIGNLE_TEXTURE:
			push_bind( 0, sizeof( vec4_t ) );					// xyz array
			push_bind( 1, sizeof( color4ub_t ) );				// color array
			push_bind( 2, sizeof( vec2_t ) );					// st0 array
			push_attr( 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT );
			push_attr( 1, 1, VK_FORMAT_R8G8B8A8_UNORM );
			push_attr( 2, 2, VK_FORMAT_R32G32_SFLOAT );
			break;

		case TYPE_SIGNLE_TEXTURE_ENV:
			push_bind( 0, sizeof( vec4_t ) );					// xyz array
			push_bind( 1, sizeof( color4ub_t ) );				// color array
			//push_bind( 2, sizeof( vec2_t ) );					// st0 array
			push_bind( 5, sizeof( vec4_t ) );					// normals
			push_attr( 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT );
			push_attr( 1, 1, VK_FORMAT_R8G8B8A8_UNORM );
			//push_attr( 2, 2, VK_FORMAT_R8G8B8A8_UNORM );
			push_attr( 5, 5, VK_FORMAT_R32G32B32A32_SFLOAT );
			break;

		case TYPE_SIGNLE_TEXTURE_IDENTITY_ENV:
		case TYPE_SIGNLE_TEXTURE_FIXED_COLOR_ENV:
		case TYPE_SIGNLE_TEXTURE_ENT_COLOR_ENV:
			push_bind( 0, sizeof( vec4_t ) );					// xyz array
			push_bind( 5, sizeof( vec4_t ) );					// normals
			push_attr( 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT );
			push_attr( 5, 5, VK_FORMAT_R32G32B32A32_SFLOAT );
			break;

		case TYPE_SIGNLE_TEXTURE_LIGHTING:
		case TYPE_SIGNLE_TEXTURE_LIGHTING_LINEAR:
			push_bind( 0, sizeof( vec4_t ) );					// xyz array
			push_bind( 1, sizeof( vec2_t ) );					// st0 array
			push_bind( 2, sizeof( vec4_t ) );					// normals array
			push_attr( 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT );
			push_attr( 1, 1, VK_FORMAT_R32G32_SFLOAT );
			push_attr( 2, 2, VK_FORMAT_R32G32B32A32_SFLOAT );
			break;

		case TYPE_MULTI_TEXTURE_MUL2_IDENTITY:
		case TYPE_MULTI_TEXTURE_ADD2_IDENTITY:
		case TYPE_MULTI_TEXTURE_MUL2_FIXED_COLOR:
		case TYPE_MULTI_TEXTURE_ADD2_FIXED_COLOR:
			push_bind( 0, sizeof( vec4_t ) );					// xyz array
			push_bind( 2, sizeof( vec2_t ) );					// st0 array
			push_bind( 3, sizeof( vec2_t ) );					// st1 array
			push_attr( 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT );
			push_attr( 2, 2, VK_FORMAT_R32G32_SFLOAT );
			push_attr( 3, 3, VK_FORMAT_R32G32_SFLOAT );
			break;

		case TYPE_MULTI_TEXTURE_MUL2_IDENTITY_ENV:
		case TYPE_MULTI_TEXTURE_ADD2_IDENTITY_ENV:
		case TYPE_MULTI_TEXTURE_MUL2_FIXED_COLOR_ENV:
		case TYPE_MULTI_TEXTURE_ADD2_FIXED_COLOR_ENV:
			push_bind( 0, sizeof( vec4_t ) );					// xyz array
			push_bind( 3, sizeof( vec2_t ) );					// st1 array
			push_bind( 5, sizeof( vec4_t ) );					// normals
			push_attr( 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT );
			push_attr( 3, 3, VK_FORMAT_R32G32_SFLOAT );
			push_attr( 5, 5, VK_FORMAT_R32G32B32A32_SFLOAT );
			break;

		case TYPE_MULTI_TEXTURE_MUL2:
		case TYPE_MULTI_TEXTURE_ADD2_1_1:
		case TYPE_MULTI_TEXTURE_ADD2:
			push_bind( 0, sizeof( vec4_t ) );					// xyz array
			push_bind( 1, sizeof( color4ub_t ) );				// color array
			push_bind( 2, sizeof( vec2_t ) );					// st0 array
			push_bind( 3, sizeof( vec2_t ) );					// st1 array
			push_attr( 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT );
			push_attr( 1, 1, VK_FORMAT_R8G8B8A8_UNORM );
			push_attr( 2, 2, VK_FORMAT_R32G32_SFLOAT );
			push_attr( 3, 3, VK_FORMAT_R32G32_SFLOAT );
			break;

		case TYPE_MULTI_TEXTURE_MUL2_ENV:
		case TYPE_MULTI_TEXTURE_ADD2_1_1_ENV:
		case TYPE_MULTI_TEXTURE_ADD2_ENV:
			push_bind( 0, sizeof( vec4_t ) );					// xyz array
			push_bind( 1, sizeof( color4ub_t ) );				// color array
			//push_bind( 2, sizeof( vec2_t ) );					// st0 array
			push_bind( 3, sizeof( vec2_t ) );					// st1 array
			push_bind( 5, sizeof( vec4_t ) );					// normals
			push_attr( 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT );
			push_attr( 1, 1, VK_FORMAT_R8G8B8A8_UNORM );
			//push_attr( 2, 2, VK_FORMAT_R32G32_SFLOAT );
			push_attr( 3, 3, VK_FORMAT_R32G32_SFLOAT );
			push_attr( 5, 5, VK_FORMAT_R32G32B32A32_SFLOAT );
			break;

		case TYPE_MULTI_TEXTURE_MUL3:
		case TYPE_MULTI_TEXTURE_ADD3_1_1:
		case TYPE_MULTI_TEXTURE_ADD3:
			push_bind( 0, sizeof( vec4_t ) );					// xyz array
			push_bind( 1, sizeof( color4ub_t ) );				// color array
			push_bind( 2, sizeof( vec2_t ) );					// st0 array
			push_bind( 3, sizeof( vec2_t ) );					// st1 array
			push_bind( 4, sizeof( vec2_t ) );					// st2 array
			push_attr( 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT );
			push_attr( 1, 1, VK_FORMAT_R8G8B8A8_UNORM );
			push_attr( 2, 2, VK_FORMAT_R32G32_SFLOAT );
			push_attr( 3, 3, VK_FORMAT_R32G32_SFLOAT );
			push_attr( 4, 4, VK_FORMAT_R32G32_SFLOAT );
			break;

		case TYPE_MULTI_TEXTURE_MUL3_ENV:
		case TYPE_MULTI_TEXTURE_ADD3_1_1_ENV:
		case TYPE_MULTI_TEXTURE_ADD3_ENV:
			push_bind( 0, sizeof( vec4_t ) );					// xyz array
			push_bind( 1, sizeof( color4ub_t ) );				// color array
			//push_bind( 2, sizeof( vec2_t ) );					// st0 array
			push_bind( 3, sizeof( vec2_t ) );					// st1 array
			push_bind( 4, sizeof( vec2_t ) );					// st2 array
			push_bind( 5, sizeof( vec4_t ) );					// normals
			push_attr( 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT );
			push_attr( 1, 1, VK_FORMAT_R8G8B8A8_UNORM );
			//push_attr( 2, 2, VK_FORMAT_R32G32_SFLOAT );
			push_attr( 3, 3, VK_FORMAT_R32G32_SFLOAT );
			push_attr( 4, 4, VK_FORMAT_R32G32_SFLOAT );
			push_attr( 5, 5, VK_FORMAT_R32G32B32A32_SFLOAT );
			break;

		case TYPE_BLEND2_ADD:
		case TYPE_BLEND2_MUL:
		case TYPE_BLEND2_ALPHA:
		case TYPE_BLEND2_ONE_MINUS_ALPHA:
		case TYPE_BLEND2_MIX_ALPHA:
		case TYPE_BLEND2_MIX_ONE_MINUS_ALPHA:
		case TYPE_BLEND2_DST_COLOR_SRC_ALPHA:
			push_bind( 0, sizeof( vec4_t ) );					// xyz array
			push_bind( 1, sizeof( color4ub_t ) );				// color0 array
			push_bind( 2, sizeof( vec2_t ) );					// st0 array
			push_bind( 3, sizeof( vec2_t ) );					// st1 array
			push_bind( 6, sizeof( color4ub_t ) );				// color1 array
			push_attr( 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT );
			push_attr( 1, 1, VK_FORMAT_R8G8B8A8_UNORM );
			push_attr( 2, 2, VK_FORMAT_R32G32_SFLOAT );
			push_attr( 3, 3, VK_FORMAT_R32G32_SFLOAT );
			push_attr( 6, 6, VK_FORMAT_R8G8B8A8_UNORM );
			break;

		case TYPE_BLEND2_ADD_ENV:
		case TYPE_BLEND2_MUL_ENV:
		case TYPE_BLEND2_ALPHA_ENV:
		case TYPE_BLEND2_ONE_MINUS_ALPHA_ENV:
		case TYPE_BLEND2_MIX_ALPHA_ENV:
		case TYPE_BLEND2_MIX_ONE_MINUS_ALPHA_ENV:
		case TYPE_BLEND2_DST_COLOR_SRC_ALPHA_ENV:
			push_bind( 0, sizeof( vec4_t ) );					// xyz array
			push_bind( 1, sizeof( color4ub_t ) );				// color0 array
			//push_bind( 2, sizeof( vec2_t ) );					// st0 array
			push_bind( 3, sizeof( vec2_t ) );					// st1 array
			push_bind( 5, sizeof( vec4_t ) );					// normals
			push_bind( 6, sizeof( color4ub_t ) );				// color1 array
			push_attr( 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT );
			push_attr( 1, 1, VK_FORMAT_R8G8B8A8_UNORM );
			//push_attr( 2, 2, VK_FORMAT_R32G32_SFLOAT );
			push_attr( 3, 3, VK_FORMAT_R32G32_SFLOAT );
			push_attr( 5, 5, VK_FORMAT_R32G32B32A32_SFLOAT );
			push_attr( 6, 6, VK_FORMAT_R8G8B8A8_UNORM );
			break;

		case TYPE_BLEND3_ADD:
		case TYPE_BLEND3_MUL:
		case TYPE_BLEND3_ALPHA:
		case TYPE_BLEND3_ONE_MINUS_ALPHA:
		case TYPE_BLEND3_MIX_ALPHA:
		case TYPE_BLEND3_MIX_ONE_MINUS_ALPHA:
		case TYPE_BLEND3_DST_COLOR_SRC_ALPHA:
			push_bind( 0, sizeof( vec4_t ) );					// xyz array
			push_bind( 1, sizeof( color4ub_t ) );				// color0 array
			push_bind( 2, sizeof( vec2_t ) );					// st0 array
			push_bind( 3, sizeof( vec2_t ) );					// st1 array
			push_bind( 4, sizeof( vec2_t ) );					// st2 array
			push_bind( 6, sizeof( color4ub_t ) );				// color1 array
			push_bind( 7, sizeof( color4ub_t ) );				// color2 array
			push_attr( 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT );
			push_attr( 1, 1, VK_FORMAT_R8G8B8A8_UNORM );
			push_attr( 2, 2, VK_FORMAT_R32G32_SFLOAT );
			push_attr( 3, 3, VK_FORMAT_R32G32_SFLOAT );
			push_attr( 4, 4, VK_FORMAT_R32G32_SFLOAT );
			push_attr( 6, 6, VK_FORMAT_R8G8B8A8_UNORM );
			push_attr( 7, 7, VK_FORMAT_R8G8B8A8_UNORM );
			break;

		case TYPE_BLEND3_ADD_ENV:
		case TYPE_BLEND3_MUL_ENV:
		case TYPE_BLEND3_ALPHA_ENV:
		case TYPE_BLEND3_ONE_MINUS_ALPHA_ENV:
		case TYPE_BLEND3_MIX_ALPHA_ENV:
		case TYPE_BLEND3_MIX_ONE_MINUS_ALPHA_ENV:
		case TYPE_BLEND3_DST_COLOR_SRC_ALPHA_ENV:
			push_bind( 0, sizeof( vec4_t ) );					// xyz array
			push_bind( 1, sizeof( color4ub_t ) );				// color0 array
			//push_bind( 2, sizeof( vec2_t ) );					// st0 array
			push_bind( 3, sizeof( vec2_t ) );					// st1 array
			push_bind( 4, sizeof( vec2_t ) );					// st2 array
			push_bind( 5, sizeof( vec4_t ) );					// normals
			push_bind( 6, sizeof( color4ub_t ) );				// color1 array
			push_bind( 7, sizeof( color4ub_t ) );				// color2 array
			push_attr( 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT );
			push_attr( 1, 1, VK_FORMAT_R8G8B8A8_UNORM );
			//push_attr( 2, 2, VK_FORMAT_R32G32_SFLOAT );
			push_attr( 3, 3, VK_FORMAT_R32G32_SFLOAT );
			push_attr( 4, 4, VK_FORMAT_R32G32_SFLOAT );
			push_attr( 5, 5, VK_FORMAT_R32G32B32A32_SFLOAT );
			push_attr( 6, 6, VK_FORMAT_R8G8B8A8_UNORM );
			push_attr( 7, 7, VK_FORMAT_R8G8B8A8_UNORM );
			break;

		default:
			ri.Error( ERR_DROP, "%s: invalid shader type - %i", __func__, def->shader_type );
			break;
	}

	vertex_input_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertex_input_state.pNext = NULL;
	vertex_input_state.flags = 0;
	vertex_input_state.pVertexBindingDescriptions = bindings;
	vertex_input_state.pVertexAttributeDescriptions = attribs;
	vertex_input_state.vertexBindingDescriptionCount = num_binds;
	vertex_input_state.vertexAttributeDescriptionCount = num_attrs;

	//
	// Primitive assembly.
	//
	input_assembly_state.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	input_assembly_state.pNext = NULL;
	input_assembly_state.flags = 0;
	input_assembly_state.primitiveRestartEnable = VK_FALSE;

	switch ( def->primitives ) {
		case LINE_LIST: input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; break;
		case POINT_LIST: input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST; break;
		case TRIANGLE_STRIP: input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; break;
		default: input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; break;
	}

	//
	// Viewport.
	//
	viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport_state.pNext = NULL;
	viewport_state.flags = 0;
	viewport_state.viewportCount = 1;
	viewport_state.pViewports = NULL; // dynamic viewport state
	viewport_state.scissorCount = 1;
	viewport_state.pScissors = NULL; // dynamic scissor state

	//
	// Rasterization.
	//
	rasterization_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterization_state.pNext = NULL;
	rasterization_state.flags = 0;
	rasterization_state.depthClampEnable = VK_FALSE;
	rasterization_state.rasterizerDiscardEnable = VK_FALSE;
	if ( def->shader_type == TYPE_DOT ) {
		rasterization_state.polygonMode = VK_POLYGON_MODE_POINT;
	} else {
		rasterization_state.polygonMode = (state_bits & GLS_POLYMODE_LINE) ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
	}

	switch ( def->face_culling ) {
		case CT_TWO_SIDED:
			rasterization_state.cullMode = VK_CULL_MODE_NONE;
			break;
		case CT_FRONT_SIDED:
			rasterization_state.cullMode = (def->mirror ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT);
			break;
		case CT_BACK_SIDED:
			rasterization_state.cullMode = (def->mirror ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT);
			break;
		default:
			ri.Error( ERR_DROP, "create_pipeline: invalid face culling mode %i\n", def->face_culling );
			break;
	}

	rasterization_state.frontFace = VK_FRONT_FACE_CLOCKWISE; // Q3 defaults to clockwise vertex order

	 // depth bias state
	if ( renderPassIndex == RENDER_PASS_DLIGHT_SHADOW ||
		renderPassIndex == RENDER_PASS_SPOT_SHADOW ||
		renderPassIndex == RENDER_PASS_CSM_SHADOW ) {
		rasterization_state.depthBiasEnable = VK_TRUE;
		rasterization_state.depthBiasClamp = 0.0f;
		rasterization_state.depthBiasConstantFactor = 0.0f;
		rasterization_state.depthBiasSlopeFactor = 0.0f;
	} else if ( def->polygon_offset ) {
		rasterization_state.depthBiasEnable = VK_TRUE;
		rasterization_state.depthBiasClamp = 0.0f;
#ifdef USE_REVERSED_DEPTH
		rasterization_state.depthBiasConstantFactor = -r_offsetUnits->value;
		rasterization_state.depthBiasSlopeFactor = -r_offsetFactor->value;
#else
		rasterization_state.depthBiasConstantFactor = r_offsetUnits->value;
		rasterization_state.depthBiasSlopeFactor = r_offsetFactor->value;
#endif
	} else {
		rasterization_state.depthBiasEnable = VK_FALSE;
		rasterization_state.depthBiasClamp = 0.0f;
		rasterization_state.depthBiasConstantFactor = 0.0f;
		rasterization_state.depthBiasSlopeFactor = 0.0f;
	}

	if ( def->line_width )
		rasterization_state.lineWidth = (float)def->line_width;
	else
		rasterization_state.lineWidth = 1.0f;

	multisample_state.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample_state.pNext = NULL;
	multisample_state.flags = 0;

	if ( renderPassIndex == RENDER_PASS_DLIGHT_SHADOW ||
		renderPassIndex == RENDER_PASS_SPOT_SHADOW ||
		renderPassIndex == RENDER_PASS_CSM_SHADOW ) {
		multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	} else {
		multisample_state.rasterizationSamples =
			( renderPassIndex == RENDER_PASS_SCREENMAP ) ?
				vk.screenMapSamples : vkSamples;
	}

	multisample_state.sampleShadingEnable = VK_FALSE;
	multisample_state.minSampleShading = 1.0f;
	multisample_state.pSampleMask = NULL;
	multisample_state.alphaToCoverageEnable = alphaToCoverage;
	multisample_state.alphaToOneEnable = VK_FALSE;

	Com_Memset( &depth_stencil_state, 0, sizeof( depth_stencil_state ) );

	depth_stencil_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depth_stencil_state.pNext = NULL;
	depth_stencil_state.flags = 0;
	depth_stencil_state.depthTestEnable = (state_bits & GLS_DEPTHTEST_DISABLE) ? VK_FALSE : VK_TRUE;
	depth_stencil_state.depthWriteEnable = (state_bits & GLS_DEPTHMASK_TRUE) ? VK_TRUE : VK_FALSE;
	if ( renderPassIndex == RENDER_PASS_CSM_SHADOW ) {
		depth_stencil_state.depthCompareOp =
			( state_bits & GLS_DEPTHFUNC_EQUAL ) ?
				VK_COMPARE_OP_EQUAL : VK_COMPARE_OP_LESS_OR_EQUAL;
	} else {
#ifdef USE_REVERSED_DEPTH
		depth_stencil_state.depthCompareOp = (state_bits & GLS_DEPTHFUNC_EQUAL) ? VK_COMPARE_OP_EQUAL : VK_COMPARE_OP_GREATER_OR_EQUAL;
#else
		depth_stencil_state.depthCompareOp = (state_bits & GLS_DEPTHFUNC_EQUAL) ? VK_COMPARE_OP_EQUAL : VK_COMPARE_OP_LESS_OR_EQUAL;
#endif
	}
	depth_stencil_state.depthBoundsTestEnable = VK_FALSE;
	depth_stencil_state.stencilTestEnable = (def->shadow_phase != SHADOW_DISABLED) ? VK_TRUE : VK_FALSE;

	if (def->shadow_phase == SHADOW_EDGES) {
		depth_stencil_state.front.failOp = VK_STENCIL_OP_KEEP;
		depth_stencil_state.front.passOp = (def->face_culling == CT_FRONT_SIDED) ? VK_STENCIL_OP_INCREMENT_AND_CLAMP : VK_STENCIL_OP_DECREMENT_AND_CLAMP;
		depth_stencil_state.front.depthFailOp = VK_STENCIL_OP_KEEP;
		depth_stencil_state.front.compareOp = VK_COMPARE_OP_ALWAYS;
		depth_stencil_state.front.compareMask = 255;
		depth_stencil_state.front.writeMask = 255;
		depth_stencil_state.front.reference = 0;

		depth_stencil_state.back = depth_stencil_state.front;

	} else if (def->shadow_phase == SHADOW_FS_QUAD) {
		depth_stencil_state.front.failOp = VK_STENCIL_OP_KEEP;
		depth_stencil_state.front.passOp = VK_STENCIL_OP_KEEP;
		depth_stencil_state.front.depthFailOp = VK_STENCIL_OP_KEEP;
		depth_stencil_state.front.compareOp = VK_COMPARE_OP_NOT_EQUAL;
		depth_stencil_state.front.compareMask = 255;
		depth_stencil_state.front.writeMask = 255;
		depth_stencil_state.front.reference = 0;

		depth_stencil_state.back = depth_stencil_state.front;
	} else if (def->shadow_phase == SHADOW_OUTLINE_MASK) {
		depth_stencil_state.front.failOp = VK_STENCIL_OP_KEEP;
		depth_stencil_state.front.passOp = VK_STENCIL_OP_REPLACE;
		depth_stencil_state.front.depthFailOp = VK_STENCIL_OP_KEEP;
		depth_stencil_state.front.compareOp = VK_COMPARE_OP_ALWAYS;
		depth_stencil_state.front.compareMask = 255;
		depth_stencil_state.front.writeMask = 255;
		depth_stencil_state.front.reference = 1;

		depth_stencil_state.back = depth_stencil_state.front;
	} else if (def->shadow_phase == SHADOW_OUTLINE_SHELL) {
		depth_stencil_state.front.failOp = VK_STENCIL_OP_KEEP;
		depth_stencil_state.front.passOp = VK_STENCIL_OP_KEEP;
		depth_stencil_state.front.depthFailOp = VK_STENCIL_OP_KEEP;
		depth_stencil_state.front.compareOp = VK_COMPARE_OP_NOT_EQUAL;
		depth_stencil_state.front.compareMask = 255;
		depth_stencil_state.front.writeMask = 255;
		depth_stencil_state.front.reference = 1;

		depth_stencil_state.back = depth_stencil_state.front;
	} else if (def->shadow_phase == SHADOW_OUTLINE_CLEAR) {
		depth_stencil_state.front.failOp = VK_STENCIL_OP_KEEP;
		depth_stencil_state.front.passOp = VK_STENCIL_OP_ZERO;
		depth_stencil_state.front.depthFailOp = VK_STENCIL_OP_KEEP;
		depth_stencil_state.front.compareOp = VK_COMPARE_OP_ALWAYS;
		depth_stencil_state.front.compareMask = 255;
		depth_stencil_state.front.writeMask = 255;
		depth_stencil_state.front.reference = 0;

		depth_stencil_state.back = depth_stencil_state.front;
	}

	depth_stencil_state.minDepthBounds = 0.0f;
	depth_stencil_state.maxDepthBounds = 1.0f;

	Com_Memset(&attachment_blend_state, 0, sizeof(attachment_blend_state));
	attachment_blend_state.blendEnable = (state_bits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) ? VK_TRUE : VK_FALSE;

	if ( def->shadow_phase == SHADOW_EDGES || def->shadow_phase == SHADOW_OUTLINE_MASK ||
		def->shadow_phase == SHADOW_OUTLINE_CLEAR || def->shader_type == TYPE_SIGNLE_TEXTURE_DF )
		attachment_blend_state.colorWriteMask = 0;
	else if ( def->shader_type == TYPE_LIQUID )
		attachment_blend_state.colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT |
			VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT;
	else
		attachment_blend_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	if (attachment_blend_state.blendEnable) {
		switch (state_bits & GLS_SRCBLEND_BITS) {
			case GLS_SRCBLEND_ZERO:
				attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
				break;
			case GLS_SRCBLEND_ONE:
				attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
				break;
			case GLS_SRCBLEND_DST_COLOR:
				attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
				break;
			case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:
				attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
				break;
			case GLS_SRCBLEND_SRC_ALPHA:
				attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
				break;
			case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:
				attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				break;
			case GLS_SRCBLEND_DST_ALPHA:
				attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
				break;
			case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:
				attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
				break;
			case GLS_SRCBLEND_ALPHA_SATURATE:
				attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
				break;
			default:
				ri.Error( ERR_DROP, "create_pipeline: invalid src blend state bits\n" );
				break;
		}
		switch (state_bits & GLS_DSTBLEND_BITS) {
			case GLS_DSTBLEND_ZERO:
				attachment_blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
				break;
			case GLS_DSTBLEND_ONE:
				attachment_blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
				break;
			case GLS_DSTBLEND_SRC_COLOR:
				attachment_blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
				break;
			case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:
				attachment_blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
				break;
			case GLS_DSTBLEND_SRC_ALPHA:
				attachment_blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
				break;
			case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:
				attachment_blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				break;
			case GLS_DSTBLEND_DST_ALPHA:
				attachment_blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
				break;
			case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:
				attachment_blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
				break;
			default:
				ri.Error( ERR_DROP, "create_pipeline: invalid dst blend state bits\n" );
				break;
		}

		attachment_blend_state.srcAlphaBlendFactor = attachment_blend_state.srcColorBlendFactor;
		attachment_blend_state.dstAlphaBlendFactor = attachment_blend_state.dstColorBlendFactor;
		attachment_blend_state.colorBlendOp = VK_BLEND_OP_ADD;
		attachment_blend_state.alphaBlendOp = VK_BLEND_OP_ADD;

		if ( def->allow_discard && vkSamples != VK_SAMPLE_COUNT_1_BIT ) {
			// try to reduce pixel fillrate for transparent surfaces, this yields 1..10% fps increase when multisampling in enabled
			if ( attachment_blend_state.srcColorBlendFactor == VK_BLEND_FACTOR_SRC_ALPHA && attachment_blend_state.dstColorBlendFactor == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA ) {
				frag_spec_data[7].i = 1;
			} else if ( attachment_blend_state.srcColorBlendFactor == VK_BLEND_FACTOR_ONE && attachment_blend_state.dstColorBlendFactor == VK_BLEND_FACTOR_ONE ) {
				frag_spec_data[7].i = 2;
			}
		}
	}

	blend_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend_state.pNext = NULL;
	blend_state.flags = 0;
	blend_state.logicOpEnable = VK_FALSE;
	blend_state.logicOp = VK_LOGIC_OP_COPY;
	if ( renderPassIndex == RENDER_PASS_DLIGHT_SHADOW ||
		renderPassIndex == RENDER_PASS_SPOT_SHADOW ||
		renderPassIndex == RENDER_PASS_CSM_SHADOW ) {
		blend_state.attachmentCount = 0;
		blend_state.pAttachments = NULL;
	} else {
		blend_state.attachmentCount = 1;
		blend_state.pAttachments = &attachment_blend_state;
	}
	blend_state.blendConstants[0] = 0.0f;
	blend_state.blendConstants[1] = 0.0f;
	blend_state.blendConstants[2] = 0.0f;
	blend_state.blendConstants[3] = 0.0f;

	dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic_state.pNext = NULL;
	dynamic_state.flags = 0;
	dynamic_state.dynamicStateCount =
		( renderPassIndex == RENDER_PASS_DLIGHT_SHADOW ||
		  renderPassIndex == RENDER_PASS_SPOT_SHADOW ||
		  renderPassIndex == RENDER_PASS_CSM_SHADOW ) ?
			ARRAY_LEN( dynamic_state_array ) : 2;
	dynamic_state.pDynamicStates = dynamic_state_array;

	create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	create_info.pNext = NULL;
	create_info.flags = 0;
	create_info.stageCount = ARRAY_LEN(shader_stages);
	create_info.pStages = shader_stages;
	create_info.pVertexInputState = &vertex_input_state;
	create_info.pInputAssemblyState = &input_assembly_state;
	create_info.pTessellationState = NULL;
	create_info.pViewportState = &viewport_state;
	create_info.pRasterizationState = &rasterization_state;
	create_info.pMultisampleState = &multisample_state;
	create_info.pDepthStencilState = &depth_stencil_state;
	create_info.pColorBlendState = &blend_state;
	create_info.pDynamicState = &dynamic_state;

	if ( def->shader_type == TYPE_DOT )
		create_info.layout = vk_pipeline_layout_handle( RTX_VK_PIPELINE_LAYOUT_STORAGE );
	else
		create_info.layout = vk_pipeline_layout_handle( RTX_VK_PIPELINE_LAYOUT_MAIN );

	if ( renderPassIndex == RENDER_PASS_DLIGHT_SHADOW ||
		renderPassIndex == RENDER_PASS_SPOT_SHADOW ||
		renderPassIndex == RENDER_PASS_CSM_SHADOW )
		create_info.renderPass = vk.render_pass.dlight_shadow;
	else if ( renderPassIndex == RENDER_PASS_SCREENMAP )
		create_info.renderPass = vk.render_pass.screenmap;
	else
		create_info.renderPass = vk.render_pass.main;

	create_info.subpass = 0;
	create_info.basePipelineHandle = VK_NULL_HANDLE;
	create_info.basePipelineIndex = -1;

	VK_CHECK( qvkCreateGraphicsPipelines( vk.device, vk.pipelineCache, 1, &create_info, NULL, &pipeline ) );

	SET_OBJECT_NAME( pipeline, va( "pipeline def#%i, pass#%i", def_index, renderPassIndex ), VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT );

	vk.pipeline_create_count++;

	return pipeline;
}


static uint32_t vk_alloc_pipeline( const Vk_Pipeline_Def *def ) {
	VK_Pipeline_t *pipeline;
	if ( vk.pipelines_count >= MAX_VK_PIPELINES ) {
		ri.Error( ERR_DROP, "alloc_pipeline: MAX_VK_PIPELINES reached" );
		return 0;
	} else {
		int j;
		pipeline = &vk.pipelines[ vk.pipelines_count ];
		pipeline->def = *def;
		for ( j = 0; j < RENDER_PASS_COUNT; j++ ) {
			pipeline->handle[j] = VK_NULL_HANDLE;
		}
		return vk.pipelines_count++;
	}
}


VkPipeline vk_gen_pipeline( uint32_t index ) {
	if ( index < vk.pipelines_count ) {
		VK_Pipeline_t *pipeline = vk.pipelines + index;
		const renderPass_t pass = vk.renderPassIndex;
		if ( pipeline->handle[ pass ] == VK_NULL_HANDLE ) {
			pipeline->handle[ pass ] = create_pipeline( &pipeline->def, pass, index );
		}
		return pipeline->handle[ pass ];
	} else {
		ri.Error( ERR_FATAL, "%s(%i): NULL pipeline", __func__, index );
		return VK_NULL_HANDLE;
	}
}


uint32_t vk_find_pipeline_ext( uint32_t base, const Vk_Pipeline_Def *def, qboolean use ) {
	const Vk_Pipeline_Def *cur_def;
	uint32_t index;

	for ( index = base; index < vk.pipelines_count; index++ ) {
		cur_def = &vk.pipelines[ index ].def;
		if ( memcmp( cur_def, def, sizeof( *def ) ) == 0 ) {
			goto found;
		}
	}

	index = vk_alloc_pipeline( def );
found:

	if ( use )
		vk_gen_pipeline( index );

	return index;
}


void vk_get_pipeline_def( uint32_t pipeline, Vk_Pipeline_Def *def ) {
	if ( pipeline >= vk.pipelines_count ) {
		Com_Memset( def, 0, sizeof( *def ) );
	} else {
		Com_Memcpy( def, &vk.pipelines[ pipeline ].def, sizeof( *def ) );
	}
}


static void get_viewport_rect(VkRect2D *r)
{
	if ( backEnd.projection2D )
	{
		r->offset.x = 0;
		r->offset.y = 0;
		r->extent.width = vk.renderWidth;
		r->extent.height = vk.renderHeight;
	}
	else
	{
		r->offset.x = backEnd.viewParms.viewportX * vk.renderScaleX;
		r->offset.y = vk.renderHeight - (backEnd.viewParms.viewportY + backEnd.viewParms.viewportHeight) * vk.renderScaleY;
		r->extent.width = (float)backEnd.viewParms.viewportWidth * vk.renderScaleX;
		r->extent.height = (float)backEnd.viewParms.viewportHeight * vk.renderScaleY;
	}
}

static void get_viewport(VkViewport *viewport, Vk_Depth_Range depth_range) {
	VkRect2D r;

	get_viewport_rect( &r );

	viewport->x = (float)r.offset.x;
	viewport->y = (float)r.offset.y;
	viewport->width = (float)r.extent.width;
	viewport->height = (float)r.extent.height;

	switch ( depth_range ) {
		default:
#ifdef USE_REVERSED_DEPTH
		//case DEPTH_RANGE_NORMAL:
			viewport->minDepth = 0.0f;
			viewport->maxDepth = 1.0f;
			break;
		case DEPTH_RANGE_ZERO:
			viewport->minDepth = 1.0f;
			viewport->maxDepth = 1.0f;
			break;
		case DEPTH_RANGE_ONE:
			viewport->minDepth = 0.0f;
			viewport->maxDepth = 0.0f;
			break;
		case DEPTH_RANGE_WEAPON:
			viewport->minDepth = 0.6f;
			viewport->maxDepth = 1.0f;
			break;
#else
		//case DEPTH_RANGE_NORMAL:
			viewport->minDepth = 0.0f;
			viewport->maxDepth = 1.0f;
			break;
		case DEPTH_RANGE_ZERO:
			viewport->minDepth = 0.0f;
			viewport->maxDepth = 0.0f;
			break;
		case DEPTH_RANGE_ONE:
			viewport->minDepth = 1.0f;
			viewport->maxDepth = 1.0f;
			break;
		case DEPTH_RANGE_WEAPON:
			viewport->minDepth = 0.0f;
			viewport->maxDepth = 0.3f;
			break;
#endif
	}
}

static void get_scissor_rect(VkRect2D *r) {

	if ( backEnd.viewParms.portalView != PV_NONE )
	{
		r->offset.x = backEnd.viewParms.scissorX;
		r->offset.y = glConfig.vidHeight - backEnd.viewParms.scissorY - backEnd.viewParms.scissorHeight;
		r->extent.width = backEnd.viewParms.scissorWidth;
		r->extent.height = backEnd.viewParms.scissorHeight;
	}
	else
	{
		get_viewport_rect(r);

		if (r->offset.x < 0)
			r->offset.x = 0;
		if (r->offset.y < 0)
			r->offset.y = 0;

		if (r->offset.x + r->extent.width > glConfig.vidWidth)
			r->extent.width = glConfig.vidWidth - r->offset.x;
		if (r->offset.y + r->extent.height > glConfig.vidHeight)
			r->extent.height = glConfig.vidHeight - r->offset.y;
	}
}


static void get_mvp_transform( float *mvp )
{
	if ( backEnd.projection2D )
	{
		float mvp0 = 2.0f / glConfig.vidWidth;
		float mvp5 = 2.0f / glConfig.vidHeight;

		mvp[0]  =  mvp0; mvp[1]  =  0.0f; mvp[2]  = 0.0f; mvp[3]  = 0.0f;
		mvp[4]  =  0.0f; mvp[5]  =  mvp5; mvp[6]  = 0.0f; mvp[7]  = 0.0f;
#ifdef USE_REVERSED_DEPTH
		mvp[8]  =  0.0f; mvp[9]  =  0.0f; mvp[10] = 0.0f; mvp[11] = 0.0f;
		mvp[12] = -1.0f; mvp[13] = -1.0f; mvp[14] = 1.0f; mvp[15] = 1.0f;
#else
		mvp[8]  =  0.0f; mvp[9]  =  0.0f; mvp[10] = 1.0f; mvp[11] = 0.0f;
		mvp[12] = -1.0f; mvp[13] = -1.0f; mvp[14] = 0.0f; mvp[15] = 1.0f;
#endif
	}
	else
	{
		const float *p = backEnd.viewParms.projectionMatrix;
		float proj[16];
		Com_Memcpy( proj, p, 64 );

		// Update q3's OpenGL-style projection to Vulkan's clip-space Y convention.
		// Negate the whole clip-Y row: orthographic shadow projections carry
		// their light-space vertical offset in projectionMatrix[13].
		proj[1] = -p[1];
		proj[5] = -p[5];
		proj[9] = -p[9];
		proj[13] = -p[13];
		//proj[10] = ( p[10] - 1.0f ) / 2.0f;
		//proj[14] = p[14] / 2.0f;
		myGlMultMatrix( vk_world.modelview_transform, proj, mvp );
	}
}

void vk_get_liquid_mvp( float *mvp )
{
	get_mvp_transform( mvp );
}


void vk_clear_color( const vec4_t color ) {

	VkClearAttachment attachment;
	VkClearRect clear_rect;

	if ( !vk.active )
		return;

	attachment.colorAttachment = 0;
	attachment.clearValue.color.float32[0] = color[0];
	attachment.clearValue.color.float32[1] = color[1];
	attachment.clearValue.color.float32[2] = color[2];
	attachment.clearValue.color.float32[3] = color[3];
	attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	get_scissor_rect( &clear_rect.rect );
	clear_rect.baseArrayLayer = 0;
	clear_rect.layerCount = 1;

	qvkCmdClearAttachments( vk.cmd->command_buffer, 1, &attachment, 1, &clear_rect );
}


static void vk_clear_depth_internal( qboolean clear_stencil, qboolean force )
{
	VkClearAttachment attachment;
	VkClearRect clear_rect[1];

	if ( !vk.active )
		return;

	/*
	 * Depth can be cleared more than once in one command buffer (portals,
	 * stereo eyes, and later primary views).  A prior scene-depth snapshot is
	 * no longer valid even when this clear is elided for an already-clean
	 * attachment.
	 */
	if ( vk.renderPassIndex == RENDER_PASS_MAIN ) {
		vk.liquidDepth.copied = qfalse;
	}

	if ( !force && vk_world.dirty_depth_attachment == 0 )
		return;

	attachment.colorAttachment = 0;
	if ( vk.renderPassIndex == RENDER_PASS_CSM_SHADOW ) {
		attachment.clearValue.depthStencil.depth = 1.0f;
	} else {
#ifdef USE_REVERSED_DEPTH
		attachment.clearValue.depthStencil.depth = 0.0f;
#else
		attachment.clearValue.depthStencil.depth = 1.0f;
#endif
	}
	attachment.clearValue.depthStencil.stencil = 0;
	if ( clear_stencil && glConfig.stencilBits > 0 ) {
		attachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	} else {
		attachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	}

	get_scissor_rect( &clear_rect[0].rect );
	clear_rect[0].baseArrayLayer = 0;
	clear_rect[0].layerCount = 1;

	qvkCmdClearAttachments( vk.cmd->command_buffer, 1, &attachment, 1, clear_rect );
}


void vk_clear_depth( qboolean clear_stencil )
{
	vk_clear_depth_internal( clear_stencil, qfalse );
}


void vk_clear_depth_force( qboolean clear_stencil )
{
	vk_clear_depth_internal( clear_stencil, qtrue );
}


void vk_update_mvp( const float *m ) {
	float push_constants[16]; // mvp transform

	//
	// Specify push constants.
	//
	if ( m )
		Com_Memcpy( push_constants, m, sizeof( push_constants ) );
	else
		get_mvp_transform( push_constants );

	qvkCmdPushConstants( vk.cmd->command_buffer, vk_pipeline_layout_handle( RTX_VK_PIPELINE_LAYOUT_MAIN ), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof( push_constants ), push_constants );

	vk.stats.push_size += sizeof( push_constants );
}


static VkBuffer shade_bufs[8];
static int bind_base;
static int bind_count;

static void vk_bind_index_attr( int index )
{
	if ( bind_base == -1 ) {
		bind_base = index;
		bind_count = 1;
	} else {
		bind_count = index - bind_base + 1;
	}
}


static void vk_bind_attr( int index, unsigned int item_size, const void *src ) {
	const uint32_t offset = PAD( vk.cmd->vertex_buffer_offset, 32 );
	const uint32_t size = tess.numVertexes * item_size;

	if ( offset + size > vk.geometry_buffer_size ) {
		// schedule geometry buffer resize
		vk.geometry_buffer_size_new = log2pad( offset + size, 1 );
	} else {
		vk.cmd->buf_offset[ index ] = offset;
		Com_Memcpy( vk.cmd->vertex_buffer_ptr + offset, src, size );
		vk.cmd->vertex_buffer_offset = (VkDeviceSize)offset + size;
	}

	vk_bind_index_attr( index );
}


uint32_t vk_tess_index( uint32_t numIndexes, const void *src ) {
	const uint32_t offset = vk.cmd->vertex_buffer_offset;
	const uint32_t size = numIndexes * sizeof( tess.indexes[0] );

	if ( offset + size > vk.geometry_buffer_size ) {
		// schedule geometry buffer resize
		vk.geometry_buffer_size_new = log2pad( offset + size, 1 );
		return ~0U;
	} else {
		Com_Memcpy( vk.cmd->vertex_buffer_ptr + offset, src, size );
		vk.cmd->vertex_buffer_offset = (VkDeviceSize)offset + size;
		return offset;
	}
}


void vk_bind_index_buffer( VkBuffer buffer, uint32_t offset )
{
	if ( vk.cmd->curr_index_buffer != buffer || vk.cmd->curr_index_offset != offset )
		qvkCmdBindIndexBuffer( vk.cmd->command_buffer, buffer, offset, VK_INDEX_TYPE_UINT32 );

	vk.cmd->curr_index_buffer = buffer;
	vk.cmd->curr_index_offset = offset;
}


#ifdef USE_VBO
void vk_draw_indexed( uint32_t indexCount, uint32_t firstIndex )
{
	qvkCmdDrawIndexed( vk.cmd->command_buffer, indexCount, 1, firstIndex, 0, 0 );
}
#endif


void vk_bind_index( void )
{
#ifdef USE_VBO
	if ( tess.vboIndex ) {
		vk.cmd->num_indexes = 0;
		//qvkCmdBindIndexBuffer( vk.cmd->command_buffer, vk.vbo.index_buffer, tess.shader->iboOffset, VK_INDEX_TYPE_UINT32 );
		return;
	}
#endif

	vk_bind_index_ext( tess.numIndexes, tess.indexes );
}


void vk_bind_index_ext( const int numIndexes, const uint32_t *indexes )
{
	uint32_t offset	= vk_tess_index( numIndexes, indexes );
	if ( offset != ~0U ) {
		vk_bind_index_buffer( vk.cmd->vertex_buffer, offset );
		vk.cmd->num_indexes = numIndexes;
	} else {
		// overflowed
		vk.cmd->num_indexes = 0;
	}
}


void vk_bind_geometry( uint32_t flags )
{
	//unsigned int size;
	bind_base = -1;
	bind_count = 0;

	if ( ( flags & ( TESS_XYZ | TESS_RGBA0 | TESS_ST0 | TESS_ST1 | TESS_ST2 | TESS_NNN | TESS_RGBA1 | TESS_RGBA2 ) ) == 0 )
		return;

#ifdef USE_VBO
	if ( tess.vboIndex ) {

		shade_bufs[0] = shade_bufs[1] = shade_bufs[2] = shade_bufs[3] = shade_bufs[4] = shade_bufs[5] = shade_bufs[6] = shade_bufs[7] = vk.vbo.vertex_buffer;

		if ( flags & TESS_XYZ ) {  // 0
			vk.cmd->vbo_offset[0] = tess.shader->vboOffset + 0;
			vk_bind_index_attr( 0 );
		}

		if ( flags & TESS_RGBA0 ) { // 1
			vk.cmd->vbo_offset[1] = tess.shader->stages[ tess.vboStage ]->rgb_offset[0];
			vk_bind_index_attr( 1 );
		}

		if ( flags & TESS_ST0 ) {  // 2
			vk.cmd->vbo_offset[2] = tess.shader->stages[ tess.vboStage ]->tex_offset[0];
			vk_bind_index_attr( 2 );
		}

		if ( flags & TESS_ST1 ) {  // 3
			vk.cmd->vbo_offset[3] = tess.shader->stages[ tess.vboStage ]->tex_offset[1];
			vk_bind_index_attr( 3 );
		}

		if ( flags & TESS_ST2 ) {  // 4
			vk.cmd->vbo_offset[4] = tess.shader->stages[ tess.vboStage ]->tex_offset[2];
			vk_bind_index_attr( 4 );
		}

		if ( flags & TESS_NNN ) { // 5
			vk.cmd->vbo_offset[5] = tess.shader->normalOffset;
			vk_bind_index_attr( 5 );
		}

		if ( flags & TESS_RGBA1 ) { // 6
			vk.cmd->vbo_offset[6] = tess.shader->stages[ tess.vboStage ]->rgb_offset[1];
			vk_bind_index_attr( 6 );
		}

		if ( flags & TESS_RGBA2 ) { // 7
			vk.cmd->vbo_offset[7] = tess.shader->stages[ tess.vboStage ]->rgb_offset[2];
			vk_bind_index_attr( 7 );
		}

		qvkCmdBindVertexBuffers( vk.cmd->command_buffer, bind_base, bind_count, shade_bufs, vk.cmd->vbo_offset + bind_base );

	} else
#endif // USE_VBO
	{
		shade_bufs[0] = shade_bufs[1] = shade_bufs[2] = shade_bufs[3] = shade_bufs[4] = shade_bufs[5] = shade_bufs[6] = shade_bufs[7] = vk.cmd->vertex_buffer;

		if ( flags & TESS_XYZ ) {
			vk_bind_attr(0, sizeof(tess.xyz[0]), &tess.xyz[0]);
		}

		if ( flags & TESS_RGBA0 ) {
			vk_bind_attr(1, sizeof( color4ub_t ), tess.svars.colors[0][0].rgba);
		}

		if ( flags & TESS_ST0 ) {
			vk_bind_attr(2, sizeof( vec2_t ), tess.svars.texcoordPtr[0]);
		}

		if ( flags & TESS_ST1 ) {
			vk_bind_attr(3, sizeof( vec2_t ), tess.svars.texcoordPtr[1]);
		}

		if ( flags & TESS_ST2 ) {
			vk_bind_attr(4, sizeof( vec2_t ), tess.svars.texcoordPtr[2]);
		}

		if ( flags & TESS_NNN ) {
			vk_bind_attr(5, sizeof(tess.normal[0]), tess.normal);
		}

		if ( flags & TESS_RGBA1 ) {
			vk_bind_attr(6, sizeof( color4ub_t ), tess.svars.colors[1][0].rgba);
		}

		if ( flags & TESS_RGBA2 ) {
			vk_bind_attr(7, sizeof( color4ub_t ), tess.svars.colors[2][0].rgba);
		}

		qvkCmdBindVertexBuffers( vk.cmd->command_buffer, bind_base, bind_count, shade_bufs, vk.cmd->buf_offset + bind_base );
	}
}


void vk_bind_lighting( int stage, int bundle )
{
	bind_base = -1;
	bind_count = 0;

#ifdef USE_VBO
	if ( tess.vboIndex ) {

		shade_bufs[0] = shade_bufs[1] = shade_bufs[2] = vk.vbo.vertex_buffer;

		vk.cmd->vbo_offset[0] = tess.shader->vboOffset + 0;
		vk.cmd->vbo_offset[1] = tess.shader->stages[ stage ]->tex_offset[ bundle ];
		vk.cmd->vbo_offset[2] = tess.shader->normalOffset;

		qvkCmdBindVertexBuffers( vk.cmd->command_buffer, 0, 3, shade_bufs, vk.cmd->vbo_offset + 0 );

	}
	else
#endif // USE_VBO
	{
		shade_bufs[0] = shade_bufs[1] = shade_bufs[2] = vk.cmd->vertex_buffer;

		vk_bind_attr( 0, sizeof( tess.xyz[0] ), &tess.xyz[0] );
		vk_bind_attr( 1, sizeof( vec2_t ), tess.svars.texcoordPtr[ bundle ] );
		vk_bind_attr( 2, sizeof( tess.normal[0] ), tess.normal );

		qvkCmdBindVertexBuffers( vk.cmd->command_buffer, bind_base, bind_count, shade_bufs, vk.cmd->buf_offset + bind_base );
	}
}


void vk_reset_descriptor( int index )
{
	vk.cmd->descriptor_set.current[ index ] = VK_NULL_HANDLE;
}


void vk_update_descriptor( int index, VkDescriptorSet descriptor )
{
	if ( vk.cmd->descriptor_set.current[ index ] != descriptor ) {
		vk.cmd->descriptor_set.start = ( index < vk.cmd->descriptor_set.start ) ? index : vk.cmd->descriptor_set.start;
		vk.cmd->descriptor_set.end = ( index > vk.cmd->descriptor_set.end ) ? index : vk.cmd->descriptor_set.end;
	}
	vk.cmd->descriptor_set.current[ index ] = descriptor;
}


void vk_update_descriptor_offset( int index, uint32_t offset )
{
	vk.cmd->descriptor_set.offset[ index ] = offset;
}


void vk_bind_descriptor_sets( void )
{
	uint32_t offsets[2], offset_count;
	uint32_t start, end, count, i;

	start = vk.cmd->descriptor_set.start;
	if ( start == ~0U )
		return;

	end = vk.cmd->descriptor_set.end;

	/*
	 * Single-texture generic fragment variants declare the depth-fade sampler
	 * at set 2.  Vulkan treats that descriptor as statically used even when
	 * the specialization constant disables sampling, so the ordinary
	 * set-0/set-1 draw path must still bind a valid fallback set 2.
	 */
	if ( start <= VK_DESC_DEPTH_FADE &&
		end < VK_DESC_DEPTH_FADE &&
		vk.maxBoundDescriptorSets > VK_DESC_DEPTH_FADE ) {
		end = VK_DESC_DEPTH_FADE;
	}

	offset_count = 0;
	if ( /*start == VK_DESC_STORAGE || */ start == VK_DESC_UNIFORM ) { // uniform offset or storage offset
		offsets[ offset_count++ ] = vk.cmd->descriptor_set.offset[ start ];
	}

	count = end - start + 1;

	/*
	 * Post-process passes invalidate main-layout bindings and can request a
	 * full-range restore before any raster overlay has pushed a uniform.
	 * Set zero is dynamic-uniform, so it needs its own descriptor and offset;
	 * sampler gaps use the ordinary white-image fallback.
	 */
	for ( i = start; i <= end; i++ ) {
		if ( vk.cmd->descriptor_set.current[i] == VK_NULL_HANDLE ) {
			if ( i == VK_DESC_UNIFORM ) {
				vk.cmd->descriptor_set.current[i] =
					vk.cmd->uniform_descriptor;
				vk.cmd->descriptor_set.offset[i] =
					vk.cmd->uniform_read_offset;
			} else {
				vk.cmd->descriptor_set.current[i] =
					tr.whiteImage->descriptor;
			}
		}
	}

	qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline_layout_handle( RTX_VK_PIPELINE_LAYOUT_MAIN ), start, count, vk.cmd->descriptor_set.current + start, offset_count, offsets );

	vk.cmd->descriptor_set.end = 0;
	vk.cmd->descriptor_set.start = ~0U;
}


static void vk_set_dlight_shadow_depth_bias( void )
{
	float depthBias;
	float slopeBias;

	if ( !qvkCmdSetDepthBias ) {
		return;
	}

	depthBias = r_dlightShadowCasterDepthBias ?
		r_dlightShadowCasterDepthBias->value : 1.0f;
	slopeBias = r_dlightShadowCasterSlopeBias ?
		r_dlightShadowCasterSlopeBias->value : 1.0f;
	depthBias = R_ShadowClampCasterDepthBias( depthBias );
	slopeBias = R_ShadowClampCasterSlopeBias( slopeBias );

#ifdef USE_REVERSED_DEPTH
	depthBias = -depthBias;
	slopeBias = -slopeBias;
#endif

	qvkCmdSetDepthBias( vk.cmd->command_buffer, depthBias, 0.0f, slopeBias );
}


static void vk_set_csm_shadow_depth_bias( void )
{
	float depthBias;
	float slopeBias;

	if ( !qvkCmdSetDepthBias ) {
		return;
	}

	depthBias = r_csmCasterDepthBias ? r_csmCasterDepthBias->value : 1.5f;
	slopeBias = r_csmCasterSlopeBias ? r_csmCasterSlopeBias->value : 1.5f;
	depthBias = R_ShadowClampCasterDepthBias( depthBias );
	slopeBias = R_ShadowClampCasterSlopeBias( slopeBias );

	qvkCmdSetDepthBias( vk.cmd->command_buffer, depthBias, 0.0f, slopeBias );
}


void vk_bind_pipeline( uint32_t pipeline ) {
	VkPipeline vkpipe;

	vkpipe = vk_gen_pipeline( pipeline );

	if ( vkpipe != vk.cmd->last_pipeline ) {
		qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkpipe );
		vk.cmd->last_pipeline = vkpipe;
	}

	if ( vk.renderPassIndex == RENDER_PASS_DLIGHT_SHADOW ||
		vk.renderPassIndex == RENDER_PASS_SPOT_SHADOW ) {
		vk_set_dlight_shadow_depth_bias();
	} else if ( vk.renderPassIndex == RENDER_PASS_CSM_SHADOW ) {
		vk_set_csm_shadow_depth_bias();
	}

	vk_world.dirty_depth_attachment |= ( vk.pipelines[ pipeline ].def.state_bits & GLS_DEPTHMASK_TRUE );
}

static void vk_update_depth_range( Vk_Depth_Range depth_range )
{
	if ( vk.cmd->depth_range != depth_range ) {
		VkRect2D scissor_rect;
		VkViewport viewport;

		vk.cmd->depth_range = depth_range;

		get_scissor_rect( &scissor_rect );

		if ( memcmp( &vk.cmd->scissor_rect, &scissor_rect, sizeof( scissor_rect ) ) != 0 ) {
			qvkCmdSetScissor( vk.cmd->command_buffer, 0, 1, &scissor_rect );
			vk.cmd->scissor_rect = scissor_rect;
		}

		get_viewport( &viewport, depth_range );
		qvkCmdSetViewport( vk.cmd->command_buffer, 0, 1, &viewport );
	}
}


void vk_draw_geometry( Vk_Depth_Range depth_range, qboolean indexed ) {

	if ( vk.geometry_buffer_size_new ) {
		// geometry buffer overflow happened this frame
		return;
	}

	vk_bind_descriptor_sets();

	// configure pipeline's dynamic state
	vk_update_depth_range( depth_range );

	// issue draw call(s)
#ifdef USE_VBO
	if ( tess.vboIndex )
		VBO_RenderIBOItems();
	else
#endif
	if ( indexed ) {
		qvkCmdDrawIndexed( vk.cmd->command_buffer, vk.cmd->num_indexes, 1, 0, 0, 0 );
	} else {
		qvkCmdDraw( vk.cmd->command_buffer, tess.numVertexes, 1, 0, 0 );
	}
}


void vk_draw_dot( uint32_t storage_offset )
{
	if ( vk.geometry_buffer_size_new ) {
		// geometry buffer overflow happened this frame
		return;
	}

	qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline_layout_handle( RTX_VK_PIPELINE_LAYOUT_STORAGE ), VK_DESC_STORAGE, 1, &vk.storage.descriptor, 1, &storage_offset );

	// configure pipeline's dynamic state
	vk_update_depth_range( DEPTH_RANGE_NORMAL );

	qvkCmdDraw( vk.cmd->command_buffer, tess.numVertexes, 1, 0, 0 );
}


static void vk_begin_render_pass( VkRenderPass renderPass, VkFramebuffer frameBuffer, qboolean clearValues, uint32_t width, uint32_t height )
{
	VkRenderPassBeginInfo render_pass_begin_info;
	VkClearValue clear_values[3];

	if ( renderPass == VK_NULL_HANDLE || frameBuffer == VK_NULL_HANDLE ) {
		ri.Error( ERR_DROP, "vk_begin_render_pass: null render pass or framebuffer handle" );
		return;
	}

	// Begin render pass.

	render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	render_pass_begin_info.pNext = NULL;
	render_pass_begin_info.renderPass = renderPass;
	render_pass_begin_info.framebuffer = frameBuffer;
	render_pass_begin_info.renderArea.offset.x = 0;
	render_pass_begin_info.renderArea.offset.y = 0;
	render_pass_begin_info.renderArea.extent.width = width;
	render_pass_begin_info.renderArea.extent.height = height;

	if ( clearValues ) {
		// attachments layout:
		// [0] - resolve/color/presentation
		// [1] - depth/stencil
		// [2] - multisampled color, optional
		Com_Memset( clear_values, 0, sizeof( clear_values ) );
#ifndef USE_REVERSED_DEPTH
		clear_values[1].depthStencil.depth = 1.0;
#endif
		render_pass_begin_info.clearValueCount = vk.msaaActive ? 3 : 2;
		render_pass_begin_info.pClearValues = clear_values;

		vk_world.dirty_depth_attachment = 0;
	} else {
		render_pass_begin_info.clearValueCount = 0;
		render_pass_begin_info.pClearValues = NULL;
	}

	qvkCmdBeginRenderPass( vk.cmd->command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE );
	vk_debug_render_pass_begin( renderPass );

	vk.cmd->last_pipeline = VK_NULL_HANDLE;
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;
}


void vk_begin_main_render_pass( void )
{
	VkFramebuffer frameBuffer = vk.framebuffers.main[ vk.cmd->swapchain_image_index ];

	vk.renderPassIndex = RENDER_PASS_MAIN;
	vk.liquidDepth.copied = qfalse;
	vk.dlight_shadow_rendered = qfalse;
	vk.spot_shadow_rendered = qfalse;
	vk.csm_shadow_rendered = qfalse;

	vk.renderWidth = glConfig.vidWidth;
	vk.renderHeight = glConfig.vidHeight;

	//vk.renderScaleX = (float)vk.renderWidth / (float)glConfig.vidWidth;
	//vk.renderScaleY = (float)vk.renderHeight / (float)glConfig.vidHeight;
	vk.renderScaleX = vk.renderScaleY = 1.0f;

	vk_begin_render_pass( vk.render_pass.main, frameBuffer, qtrue, vk.renderWidth, vk.renderHeight );
}


void vk_begin_main_render_pass_load( void )
{
	VkFramebuffer frameBuffer =
		vk.framebuffers.main[ vk.cmd->swapchain_image_index ];

	vk.renderPassIndex = RENDER_PASS_MAIN;
	vk.renderWidth = glConfig.vidWidth;
	vk.renderHeight = glConfig.vidHeight;
	vk.renderScaleX = 1.0f;
	vk.renderScaleY = 1.0f;

	vk_begin_render_pass( vk.render_pass.main_load, frameBuffer, qfalse,
		vk.renderWidth, vk.renderHeight );
}


void vk_begin_post_bloom_render_pass( void )
{
	VkFramebuffer frameBuffer = vk.framebuffers.main[ vk.cmd->swapchain_image_index ];

	vk.renderPassIndex = RENDER_PASS_POST_BLOOM;

	vk.renderWidth = glConfig.vidWidth;
	vk.renderHeight = glConfig.vidHeight;

	//vk.renderScaleX = (float)vk.renderWidth / (float)glConfig.vidWidth;
	//vk.renderScaleY = (float)vk.renderHeight / (float)glConfig.vidHeight;
	vk.renderScaleX = vk.renderScaleY = 1.0f;

	vk_begin_render_pass( vk.render_pass.post_bloom, frameBuffer, qfalse, vk.renderWidth, vk.renderHeight );
}

static void vk_begin_global_fog_render_pass( void )
{
	vk.renderWidth = glConfig.vidWidth;
	vk.renderHeight = glConfig.vidHeight;
	vk.renderScaleX = vk.renderScaleY = 1.0f;

	vk_begin_render_pass( vk.render_pass.global_fog, vk.framebuffers.global_fog,
		qfalse, vk.renderWidth, vk.renderHeight );
}

static void vk_begin_liquid_snapshot_render_pass( void )
{
	vk.renderPassIndex = RENDER_PASS_LIQUID_SNAPSHOT;
	vk.renderWidth = vk.liquidSnapshotWidth;
	vk.renderHeight = vk.liquidSnapshotHeight;
	vk.renderScaleX =
		(float)vk.renderWidth / (float)glConfig.vidWidth;
	vk.renderScaleY =
		(float)vk.renderHeight / (float)glConfig.vidHeight;

	vk_begin_render_pass( vk.render_pass.liquid_snapshot,
		vk.framebuffers.liquid_snapshot, qfalse,
		vk.renderWidth, vk.renderHeight );
}


void vk_begin_bloom_extract_render_pass( void )
{
	VkFramebuffer frameBuffer = vk.framebuffers.bloom_extract;

	//vk.renderPassIndex = RENDER_PASS_BLOOM_EXTRACT; // doesn't matter, we will use dedicated pipelines

	vk.renderWidth = gls.captureWidth;
	vk.renderHeight = gls.captureHeight;

	//vk.renderScaleX = (float)vk.renderWidth / (float)glConfig.vidWidth;
	//vk.renderScaleY = (float)vk.renderHeight / (float)glConfig.vidHeight;
	vk.renderScaleX = vk.renderScaleY = 1.0f;

	vk_begin_render_pass( vk.render_pass.bloom_extract, frameBuffer, qfalse, vk.renderWidth, vk.renderHeight );
}


void vk_begin_blur_render_pass( uint32_t index )
{
	VkFramebuffer frameBuffer = vk.framebuffers.blur[ index ];

	//vk.renderPassIndex = RENDER_PASS_BLOOM_EXTRACT; // doesn't matter, we will use dedicated pipelines

	vk.renderWidth = gls.captureWidth / ( 2 << ( index / 2 ) );
	vk.renderHeight = gls.captureHeight / ( 2 << ( index / 2 ) );

	//vk.renderScaleX = (float)vk.renderWidth / (float)glConfig.vidWidth;
	//vk.renderScaleY = (float)vk.renderHeight / (float)glConfig.vidHeight;
	vk.renderScaleX = vk.renderScaleY = 1.0f;

	vk_begin_render_pass( vk.render_pass.blur[ index ], frameBuffer, qfalse, vk.renderWidth, vk.renderHeight );
}


static void vk_begin_screenmap_render_pass( void )
{
	VkFramebuffer frameBuffer = vk.framebuffers.screenmap;

	vk.renderPassIndex = RENDER_PASS_SCREENMAP;

	vk.renderWidth = vk.screenMapWidth;
	vk.renderHeight = vk.screenMapHeight;

	vk.renderScaleX = (float)vk.renderWidth / (float)glConfig.vidWidth;
	vk.renderScaleY = (float)vk.renderHeight / (float)glConfig.vidHeight;

	vk_begin_render_pass( vk.render_pass.screenmap, frameBuffer, qtrue, vk.renderWidth, vk.renderHeight );
}


void vk_end_render_pass( void )
{
	vk_debug_render_pass_end();
	qvkCmdEndRenderPass( vk.cmd->command_buffer );

//	vk.renderPassIndex = RENDER_PASS_MAIN;
}

static qboolean vk_scene_depth_snapshot_available( void )
{
	return ( vk.liquidDepth.image != VK_NULL_HANDLE &&
		vk.liquidDepth.image_view != VK_NULL_HANDLE &&
		vk.liquidDepth.descriptor != VK_NULL_HANDLE ) ? qtrue : qfalse;
}

qboolean vk_depth_fade_supported( void )
{
	return ( vk_depth_fade_requested() &&
		vk.fboActive &&
		vkSamples == VK_SAMPLE_COUNT_1_BIT &&
		vk.liquidDepthSampleSupported &&
		vk.maxBoundDescriptorSets > VK_DESC_DEPTH_FADE ) ?
			qtrue : qfalse;
}

qboolean vk_depth_fade_available( void )
{
	return ( vk_depth_fade_supported() &&
		vk_scene_depth_snapshot_available() ) ? qtrue : qfalse;
}

qboolean vk_dlight_shadow_atlas_available( void )
{
	return ( vk.dlight_shadow_image != VK_NULL_HANDLE &&
		vk.dlight_shadow_image_view != VK_NULL_HANDLE &&
		vk.dlight_shadow_descriptor != VK_NULL_HANDLE &&
		vk.dlight_shadow_face_size > 0 ) ? qtrue : qfalse;
}

qboolean vk_dlight_shadow_atlas_ready( void )
{
	return ( vk_dlight_shadow_atlas_available() &&
		vk.dlight_shadow_rendered ) ? qtrue : qfalse;
}

qboolean vk_begin_dlight_shadow_render_pass( void )
{
	vk.dlight_shadow_rendered = qfalse;

	if ( !vk_dlight_shadow_atlas_available() ||
		vk.render_pass.dlight_shadow == VK_NULL_HANDLE ||
		vk.framebuffers.dlight_shadow == VK_NULL_HANDLE ||
		vk.render_pass.main_load == VK_NULL_HANDLE ||
		vk.renderPassIndex != RENDER_PASS_MAIN ) {
		return qfalse;
	}

	vk_end_render_pass();
	vk.renderPassIndex = RENDER_PASS_DLIGHT_SHADOW;
	vk.renderWidth = vk.dlight_shadow_atlas_width;
	vk.renderHeight = vk.dlight_shadow_atlas_height;
	vk.renderScaleX = 1.0f;
	vk.renderScaleY = 1.0f;
	vk_begin_render_pass( vk.render_pass.dlight_shadow,
		vk.framebuffers.dlight_shadow, qfalse,
		vk.renderWidth, vk.renderHeight );
	return qtrue;
}

void vk_end_dlight_shadow_render_pass( void )
{
	if ( vk.renderPassIndex != RENDER_PASS_DLIGHT_SHADOW ) {
		return;
	}

	vk_end_render_pass();
	vk_begin_main_render_pass_load();
	vk.dlight_shadow_rendered = qtrue;
}

int vk_dlight_shadow_atlas_height( void )
{
	return (int)vk.dlight_shadow_atlas_height;
}

int vk_dlight_shadow_atlas_columns( void )
{
	return (int)vk.dlight_shadow_atlas_columns;
}

uint32_t vk_dlight_shadow_atlas_generation( void )
{
	return vk.dlight_shadow_generation;
}

qboolean vk_spot_shadow_atlas_available( void )
{
	return ( vk.spot_shadow_image != VK_NULL_HANDLE &&
		vk.spot_shadow_image_view != VK_NULL_HANDLE &&
		vk.spot_shadow_descriptor != VK_NULL_HANDLE &&
		vk.spot_shadow_tile_size > 0 ) ? qtrue : qfalse;
}

qboolean vk_spot_shadow_atlas_ready( void )
{
	return ( vk_spot_shadow_atlas_available() &&
		vk.spot_shadow_rendered ) ? qtrue : qfalse;
}

qboolean vk_begin_spot_shadow_render_pass( void )
{
	vk.spot_shadow_rendered = qfalse;

	if ( !vk_spot_shadow_atlas_available() ||
		vk.render_pass.dlight_shadow == VK_NULL_HANDLE ||
		vk.framebuffers.spot_shadow == VK_NULL_HANDLE ||
		vk.render_pass.main_load == VK_NULL_HANDLE ||
		vk.renderPassIndex != RENDER_PASS_MAIN ) {
		return qfalse;
	}

	vk_end_render_pass();
	vk.renderPassIndex = RENDER_PASS_SPOT_SHADOW;
	vk.renderWidth = vk.spot_shadow_atlas_width;
	vk.renderHeight = vk.spot_shadow_atlas_height;
	vk.renderScaleX = 1.0f;
	vk.renderScaleY = 1.0f;
	vk_begin_render_pass( vk.render_pass.dlight_shadow,
		vk.framebuffers.spot_shadow, qfalse,
		vk.renderWidth, vk.renderHeight );
	return qtrue;
}

void vk_end_spot_shadow_render_pass( void )
{
	if ( vk.renderPassIndex != RENDER_PASS_SPOT_SHADOW ) {
		return;
	}

	vk_end_render_pass();
	vk_begin_main_render_pass_load();
	vk.spot_shadow_rendered = qtrue;
}

void vk_mark_spot_shadow_atlas_rendered( void )
{
	if ( vk_spot_shadow_atlas_available() ) {
		vk.spot_shadow_rendered = qtrue;
	}
}

int vk_spot_shadow_atlas_width( void )
{
	return (int)vk.spot_shadow_atlas_width;
}

int vk_spot_shadow_atlas_height( void )
{
	return (int)vk.spot_shadow_atlas_height;
}

int vk_spot_shadow_tile_size( void )
{
	return (int)vk.spot_shadow_tile_size;
}

uint32_t vk_spot_shadow_atlas_generation( void )
{
	return vk.spot_shadow_generation;
}

VkDescriptorSet vk_spot_shadow_descriptor( void )
{
	return vk.spot_shadow_descriptor;
}

qboolean vk_csm_shadow_atlas_available( void )
{
	return ( vk.csm_shadow_image != VK_NULL_HANDLE &&
		vk.csm_shadow_image_view != VK_NULL_HANDLE &&
		vk.csm_shadow_descriptor != VK_NULL_HANDLE &&
		vk.csm_shadow_cascade_size > 0 &&
		vk.csm_shadow_cascade_count > 0 ) ? qtrue : qfalse;
}

qboolean vk_csm_shadow_atlas_ready( void )
{
	return ( vk_csm_shadow_atlas_available() &&
		vk.csm_shadow_rendered ) ? qtrue : qfalse;
}

qboolean vk_begin_csm_shadow_render_pass( void )
{
	vk.csm_shadow_rendered = qfalse;

	if ( !vk_csm_shadow_atlas_available() ||
		vk.render_pass.dlight_shadow == VK_NULL_HANDLE ||
		vk.framebuffers.csm_shadow == VK_NULL_HANDLE ||
		vk.render_pass.main_load == VK_NULL_HANDLE ||
		vk.renderPassIndex != RENDER_PASS_MAIN ) {
		return qfalse;
	}

	vk_end_render_pass();
	vk.renderPassIndex = RENDER_PASS_CSM_SHADOW;
	vk.renderWidth = vk.csm_shadow_atlas_width;
	vk.renderHeight = vk.csm_shadow_atlas_height;
	vk.renderScaleX = 1.0f;
	vk.renderScaleY = 1.0f;
	vk_begin_render_pass( vk.render_pass.dlight_shadow,
		vk.framebuffers.csm_shadow, qfalse,
		vk.renderWidth, vk.renderHeight );
	return qtrue;
}

void vk_end_csm_shadow_render_pass( void )
{
	if ( vk.renderPassIndex != RENDER_PASS_CSM_SHADOW ) {
		return;
	}

	vk_end_render_pass();
	vk_begin_main_render_pass_load();
	vk.csm_shadow_rendered = qtrue;
}

void vk_mark_csm_shadow_atlas_rendered( void )
{
	if ( vk_csm_shadow_atlas_available() ) {
		vk.csm_shadow_rendered = qtrue;
	}
}

int vk_csm_shadow_atlas_width( void )
{
	return (int)vk.csm_shadow_atlas_width;
}

int vk_csm_shadow_atlas_height( void )
{
	return (int)vk.csm_shadow_atlas_height;
}

int vk_csm_shadow_cascade_size( void )
{
	return (int)vk.csm_shadow_cascade_size;
}

uint32_t vk_csm_shadow_atlas_generation( void )
{
	return vk.csm_shadow_generation;
}

VkDescriptorSet vk_csm_shadow_descriptor( void )
{
	return vk.csm_shadow_descriptor;
}

qboolean vk_depth_fade_ready( void )
{
	return ( vk_depth_fade_available() && vk.liquidDepth.copied ) ?
		qtrue : qfalse;
}

qboolean vk_liquid_depth_ready( void )
{
	return ( vk_scene_depth_snapshot_available() &&
		vk.liquidDepth.copied ) ? qtrue : qfalse;
}

void vk_copy_depth_fade( void )
{
	VkImageCopy region;

	if ( !vk_depth_fade_available() || vk_depth_fade_ready() ||
		vk.render_pass.post_bloom == VK_NULL_HANDLE ||
		( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) ||
		backEnd.viewParms.portalView != PV_NONE ||
		backEnd.viewParms.stereoFrame != STEREO_CENTER ||
		backEnd.viewParms.viewportX != 0 ||
		backEnd.viewParms.viewportY != 0 ||
		backEnd.viewParms.viewportWidth != glConfig.vidWidth ||
		backEnd.viewParms.viewportHeight != glConfig.vidHeight ||
		( vk.renderPassIndex != RENDER_PASS_MAIN &&
		  vk.renderPassIndex != RENDER_PASS_POST_BLOOM ) ) {
		return;
	}

	vk_end_render_pass();

	record_image_layout_transition( vk.cmd->command_buffer,
		vk.depth_image, VK_IMAGE_ASPECT_DEPTH_BIT,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0, 0 );
	record_image_layout_transition( vk.cmd->command_buffer,
		vk.liquidDepth.image, VK_IMAGE_ASPECT_DEPTH_BIT,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, 0 );

	Com_Memset( &region, 0, sizeof( region ) );
	region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	region.srcSubresource.layerCount = 1;
	region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	region.dstSubresource.layerCount = 1;
	region.extent.width = glConfig.vidWidth;
	region.extent.height = glConfig.vidHeight;
	region.extent.depth = 1;

	qvkCmdCopyImage( vk.cmd->command_buffer,
		vk.depth_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		vk.liquidDepth.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &region );

	record_image_layout_transition( vk.cmd->command_buffer,
		vk.liquidDepth.image, VK_IMAGE_ASPECT_DEPTH_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 0 );
	record_image_layout_transition( vk.cmd->command_buffer,
		vk.depth_image, VK_IMAGE_ASPECT_DEPTH_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 0, 0 );

	vk.liquidDepth.copied = qtrue;
	vk_begin_post_bloom_render_pass();

	vk.cmd->last_pipeline = VK_NULL_HANDLE;
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;
	vk.cmd->descriptor_set.start = 0;
	vk.cmd->descriptor_set.end =
		MIN( VK_DESC_COUNT, vk.maxBoundDescriptorSets ) - 1;
	Com_Memset( &vk.cmd->scissor_rect, 0xff,
		sizeof( vk.cmd->scissor_rect ) );
}

void vk_draw_world_cel_outline( void )
{
	float constants[4];
	float width;
	float alpha;
	float threshold;
	VkDescriptorSet descriptor;

	if ( !R_CelShadingWorldActive() ||
		( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) ||
		backEnd.viewParms.portalView != PV_NONE ||
		backEnd.viewParms.stereoFrame != STEREO_CENTER ||
		backEnd.viewParms.viewportX != 0 ||
		backEnd.viewParms.viewportY != 0 ||
		backEnd.viewParms.viewportWidth != glConfig.vidWidth ||
		backEnd.viewParms.viewportHeight != glConfig.vidHeight ||
		!vk.fboActive || vk.world_outline_pipeline == VK_NULL_HANDLE ||
		!vk_depth_fade_available() ||
		( vk.renderPassIndex != RENDER_PASS_MAIN &&
		  vk.renderPassIndex != RENDER_PASS_POST_BLOOM ) ) {
		return;
	}

	width = r_celShadingWorldWidth ?
		Com_Clamp( 1.0f, 8.0f, r_celShadingWorldWidth->value ) : 2.0f;
	alpha = r_celShadingWorldAlpha ?
		Com_Clamp( 0.0f, 1.0f, r_celShadingWorldAlpha->value ) : 1.0f;
	threshold = r_celShadingWorldDepthThreshold ?
		Com_Clamp( 0.0001f, 0.02f,
			r_celShadingWorldDepthThreshold->value ) : 0.0015f;
	if ( alpha <= 0.0f ) {
		return;
	}

	if ( !vk_depth_fade_ready() ) {
		vk_copy_depth_fade();
	}
	/* The RTX depth snapshot helper resumes composition in post-bloom so the
	 * overlay is compatible with both raster-only and successful RT frames. */
	if ( !vk_depth_fade_ready() ||
		vk.renderPassIndex != RENDER_PASS_POST_BLOOM ) {
		return;
	}

	constants[0] = glConfig.vidWidth > 0 ?
		width / (float)glConfig.vidWidth : 1.0f;
	constants[1] = glConfig.vidHeight > 0 ?
		width / (float)glConfig.vidHeight : 1.0f;
	constants[2] = threshold;
	constants[3] = alpha;

	descriptor = vk.liquidDepth.descriptor;
	qvkCmdBindPipeline( vk.cmd->command_buffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS, vk.world_outline_pipeline );
	qvkCmdBindDescriptorSets( vk.cmd->command_buffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		vk_pipeline_layout_handle( RTX_VK_PIPELINE_LAYOUT_POST_PROCESS ),
		0, 1, &descriptor, 0, NULL );
	qvkCmdPushConstants( vk.cmd->command_buffer,
		vk_pipeline_layout_handle( RTX_VK_PIPELINE_LAYOUT_POST_PROCESS ),
		VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( constants ), constants );
	qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );

	vk.cmd->last_pipeline = VK_NULL_HANDLE;
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;
	vk.cmd->descriptor_set.start = 0;
	vk.cmd->descriptor_set.end =
		MIN( VK_DESC_COUNT, vk.maxBoundDescriptorSets ) - 1;
	Com_Memset( &vk.cmd->scissor_rect, 0xff,
		sizeof( vk.cmd->scissor_rect ) );
}

qboolean vk_capture_liquid_scene( void )
{
	VkMemoryBarrier barrier;

	if ( backEnd.liquidScreenMapDone ) {
		return qtrue;
	}
	if ( !vk_liquid_requested() || !vk.fboActive ||
		vk.liquid_snapshot_pipeline == VK_NULL_HANDLE ||
		vk.liquidSnapshot.source_descriptor == VK_NULL_HANDLE ||
		vk.liquidSnapshot.color_descriptor == VK_NULL_HANDLE ||
		vk.render_pass.liquid_snapshot == VK_NULL_HANDLE ||
		vk.render_pass.post_bloom == VK_NULL_HANDLE ||
		vk.framebuffers.liquid_snapshot == VK_NULL_HANDLE ||
		( vk.renderPassIndex != RENDER_PASS_MAIN &&
		  vk.renderPassIndex != RENDER_PASS_POST_BLOOM ) ) {
		return qfalse;
	}

	/*
	 * The active color target cannot be sampled in-place. End composition,
	 * copy optional single-sample depth, render the scaled immutable color
	 * snapshot, then resume the scene through the existing load pass.
	 */
	vk_end_render_pass();

	if ( !vk.liquidDepth.copied &&
		vk.liquidDepth.image != VK_NULL_HANDLE &&
		vkSamples == VK_SAMPLE_COUNT_1_BIT ) {
		VkImageCopy region;

		record_image_layout_transition( vk.cmd->command_buffer,
			vk.depth_image, VK_IMAGE_ASPECT_DEPTH_BIT,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0, 0 );
		record_image_layout_transition( vk.cmd->command_buffer,
			vk.liquidDepth.image, VK_IMAGE_ASPECT_DEPTH_BIT,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, 0 );

		Com_Memset( &region, 0, sizeof( region ) );
		region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		region.srcSubresource.layerCount = 1;
		region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		region.dstSubresource.layerCount = 1;
		region.extent.width = glConfig.vidWidth;
		region.extent.height = glConfig.vidHeight;
		region.extent.depth = 1;

		qvkCmdCopyImage( vk.cmd->command_buffer,
			vk.depth_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			vk.liquidDepth.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &region );

		record_image_layout_transition( vk.cmd->command_buffer,
			vk.liquidDepth.image, VK_IMAGE_ASPECT_DEPTH_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 0 );
		record_image_layout_transition( vk.cmd->command_buffer,
			vk.depth_image, VK_IMAGE_ASPECT_DEPTH_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 0, 0 );
		vk.liquidDepth.copied = qtrue;
	}

	Com_Memset( &barrier, 0, sizeof( barrier ) );
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
		VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	qvkCmdPipelineBarrier( vk.cmd->command_buffer,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
			VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		0, 1, &barrier, 0, NULL, 0, NULL );

	vk_begin_liquid_snapshot_render_pass();
	qvkCmdBindPipeline( vk.cmd->command_buffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		vk.liquid_snapshot_pipeline );
	qvkCmdBindDescriptorSets( vk.cmd->command_buffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		vk_pipeline_layout_handle( RTX_VK_PIPELINE_LAYOUT_POST_PROCESS ),
		0, 1, &vk.liquidSnapshot.source_descriptor, 0, NULL );
	qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
	vk_end_render_pass();
	vk_begin_post_bloom_render_pass();

	vk.cmd->last_pipeline = VK_NULL_HANDLE;
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;
	vk.cmd->descriptor_set.start = 0;
	vk.cmd->descriptor_set.end =
		MIN( VK_DESC_COUNT, vk.maxBoundDescriptorSets ) - 1;
	Com_Memset( &vk.cmd->scissor_rect, 0xff,
		sizeof( vk.cmd->scissor_rect ) );
	backEnd.liquidScreenMapDone = qtrue;
	return qtrue;
}

void vk_draw_global_fog( void )
{
	static int lastFogFrame = -1;
	const globalFog_t *fog = tr.world ? &tr.world->globalFog : NULL;
	VkImageAspectFlags depthAspects = VK_IMAGE_ASPECT_DEPTH_BIT;
	float constants[12];
	float opacity;
	float zNear;
	float zFar;

	if ( !vk_global_fog_enabled() || !fog || !fog->loaded ||
		vk.global_fog_pipeline == VK_NULL_HANDLE ||
		vk.render_pass.global_fog == VK_NULL_HANDLE ||
		vk.framebuffers.global_fog == VK_NULL_HANDLE ||
		vk.depth_sample_descriptor == VK_NULL_HANDLE ||
		( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) ||
		backEnd.viewParms.portalView != PV_NONE ||
		backEnd.viewParms.stereoFrame != STEREO_CENTER ||
		backEnd.viewParms.viewportX != 0 ||
		backEnd.viewParms.viewportY != 0 ||
		backEnd.viewParms.viewportWidth != glConfig.vidWidth ||
		backEnd.viewParms.viewportHeight != glConfig.vidHeight ||
		( vk.renderPassIndex != RENDER_PASS_MAIN &&
			vk.renderPassIndex != RENDER_PASS_POST_BLOOM ) ||
		lastFogFrame == tr.frameCount ) {
		return;
	}

	opacity = Com_Clamp( 0.0f, 1.0f,
		fog->opacity * ( r_globalFogStrength ? r_globalFogStrength->value : 1.0f ) );
	zNear = r_znear ? r_znear->value : 4.0f;
	zFar = backEnd.viewParms.zFar;
	if ( opacity <= 0.0f || fog->density <= 0.0f ||
		zNear <= 0.0f || zFar <= zNear ) {
		return;
	}

	constants[0] = fog->color[0];
	constants[1] = fog->color[1];
	constants[2] = fog->color[2];
	constants[3] = opacity;
	constants[4] = fog->start;
	constants[5] = fog->end;
	constants[6] = fog->density;
	constants[7] = (float)fog->mode;
	constants[8] = zNear;
	constants[9] = zFar;
	constants[10] = fog->sky ? 1.0f : 0.0f;
#ifdef USE_REVERSED_DEPTH
	constants[11] = 1.0f;
#else
	constants[11] = 0.0f;
#endif

	if ( glConfig.stencilBits > 0 ) {
		depthAspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
	}

	vk_end_render_pass();
	record_image_layout_transition( vk.cmd->command_buffer, vk.depth_image,
		depthAspects,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 0, 0 );

	vk_begin_global_fog_render_pass();
	qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		vk.global_fog_pipeline );
	qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		vk_pipeline_layout_handle( RTX_VK_PIPELINE_LAYOUT_POST_PROCESS ),
		0, 1, &vk.depth_sample_descriptor, 0, NULL );
	qvkCmdPushConstants( vk.cmd->command_buffer,
		vk_pipeline_layout_handle( RTX_VK_PIPELINE_LAYOUT_POST_PROCESS ),
		VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( constants ), constants );
	qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
	vk_end_render_pass();

	record_image_layout_transition( vk.cmd->command_buffer, vk.depth_image,
		depthAspects,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 0, 0 );
	vk_begin_post_bloom_render_pass();

	/*
	 * The fog pass binds the private post-process pipeline layout. Force the
	 * resumed material path to restore every main-layout descriptor and all
	 * dynamic viewport/scissor state before HUD or console draws.
	 */
	vk.cmd->last_pipeline = VK_NULL_HANDLE;
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;
	vk.cmd->descriptor_set.start = 0;
	vk.cmd->descriptor_set.end =
		MIN( VK_DESC_COUNT, vk.maxBoundDescriptorSets ) - 1;
	Com_Memset( &vk.cmd->scissor_rect, 0xff, sizeof( vk.cmd->scissor_rect ) );

	lastFogFrame = tr.frameCount;
}


static qboolean vk_find_screenmap_drawsurfs( void )
{
	const void *curCmd = &backEndData->commands.cmds;
	const drawBufferCommand_t *db_cmd;
	const drawSurfsCommand_t *ds_cmd;

	for ( ;; ) {
		curCmd = PADP( curCmd, sizeof(void *) );
		switch ( *(const int *)curCmd ) {
			case RC_DRAW_BUFFER:
				db_cmd = (const drawBufferCommand_t *)curCmd;
				curCmd = (const void *)(db_cmd + 1);
				break;
			case RC_DRAW_SURFS:
				ds_cmd = (const drawSurfsCommand_t *)curCmd;
				return ds_cmd->refdef.needScreenMap;
			default:
				return qfalse;
		}
	}
}


#ifndef UINT64_MAX
#define UINT64_MAX 0xFFFFFFFFFFFFFFFFULL
#endif

void vk_begin_frame( void )
{
	VkCommandBufferBeginInfo begin_info;
	VkResult res;

	if ( vk.frame_count++ ) // might happen during stereo rendering
		return;

	vk_lifetime_begin_frame();

#ifdef USE_UPLOAD_QUEUE
	vk_submit_staging_buffer( qtrue );
#endif

	vk.cmd = &vk.tess[ vk.cmd_index ];

	if ( vk.cmd->waitForFence ) {
		vk.cmd->waitForFence = qfalse;
		res = qvkWaitForFences( vk.device, 1, &vk.cmd->rendering_finished_fence, VK_FALSE, 1e10 );
		if ( res != VK_SUCCESS ) {
			if ( res == VK_ERROR_DEVICE_LOST ) {
				// silently discard previous command buffer
				ri.Printf( PRINT_WARNING, "Vulkan: %s returned %s", "vkWaitForFences", vk_result_string( res ) );
			}
			else {
				ri.Error( ERR_FATAL, "Vulkan: %s returned %s", "vkWaitForFences", vk_result_string( res ) );
			}
		}
		VK_CHECK( qvkResetFences( vk.device, 1, &vk.cmd->rendering_finished_fence ) );
	}

	if ( s_vkSwapchainResizePending ) {
		( void )vk_restart_swapchain( __func__, s_vkSwapchainResizeResult );
	}

	if ( !ri.CL_IsMinimized() && !vk.cmd->swapchain_image_acquired ) {
		qboolean retry = qfalse;
_retry:
		res = qvkAcquireNextImageKHR( vk.device, vk.swapchain, 1 * 1000000000ULL, vk.cmd->image_acquired, VK_NULL_HANDLE, &vk.cmd->swapchain_image_index );
		// when running via RDP: "Application has already acquired the maximum number of images (0x2)"
		// probably caused by "device lost" errors
		if ( res == VK_SUCCESS || res == VK_SUBOPTIMAL_KHR ) {
			vk.cmd->swapchain_image_acquired = qtrue;
			vk_lifetime_mark_use( RTX_VK_RESOURCE_SWAPCHAIN, "vkAcquireNextImageKHR" );

			if ( res == VK_SUBOPTIMAL_KHR ) {
				vk_schedule_swapchain_resize( __func__, res, "vkAcquireNextImageKHR returned VK_SUBOPTIMAL_KHR" );
			}
		} else if ( res == VK_ERROR_OUT_OF_DATE_KHR && retry == qfalse ) {
			// swapchain re-creation needed
			retry = qtrue;
			if ( vk_restart_swapchain( __func__, res ) ) {
				goto _retry;
			}
		} else if ( res == VK_ERROR_SURFACE_LOST_KHR || res == VK_TIMEOUT || res == VK_NOT_READY ) {
			vk_schedule_swapchain_resize( __func__, res, "vkAcquireNextImageKHR returned transient status" );
		} else if ( res < 0 ) {
			ri.Error( ERR_FATAL, "vkAcquireNextImageKHR returned %s", vk_result_string( res ) );
		} else {
			vk_schedule_swapchain_resize( __func__, res, "vkAcquireNextImageKHR returned non-success status" );
		}
		if ( !vk.cmd->swapchain_image_acquired && !vk.fboActive ) {
			ri.Error( ERR_FATAL, "vkAcquireNextImageKHR returned %s without an offscreen fallback path", vk_result_string( res ) );
		}
	}

	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.pNext = NULL;
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	begin_info.pInheritanceInfo = NULL;

	VK_CHECK( qvkBeginCommandBuffer( vk.cmd->command_buffer, &begin_info ) );

	// Ensure visibility of geometry buffers writes.
	//record_buffer_memory_barrier( vk.cmd->command_buffer, vk.cmd->vertex_buffer, vk.geometry_buffer_size, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT );

#if 0
	// add explicit layout transition dependency
	if ( vk.fboActive ) {
		record_image_layout_transition( vk.cmd->command_buffer, vk.color_image, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 0 );
	} else {
		record_image_layout_transition( vk.cmd->command_buffer, vk.swapchain_images[ vk.swapchain_image_index ], VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 0, 0 );
	}
#endif

	if ( vk.cmd->vertex_buffer_offset > vk.stats.vertex_buffer_max ) {
		vk.stats.vertex_buffer_max = vk.cmd->vertex_buffer_offset;
	}

	if ( vk.stats.push_size > vk.stats.push_size_max ) {
		vk.stats.push_size_max = vk.stats.push_size;
	}

	vk.cmd->last_pipeline = VK_NULL_HANDLE;

	backEnd.screenMapDone = qfalse;

	if ( vk_find_screenmap_drawsurfs() ) {
		vk_begin_screenmap_render_pass();
	} else {
		vk_begin_main_render_pass();
	}

	// dynamic vertex buffer layout
	vk.cmd->uniform_read_offset = 0;
	vk.cmd->vertex_buffer_offset = 0;
	Com_Memset( vk.cmd->buf_offset, 0, sizeof( vk.cmd->buf_offset ) );
	Com_Memset( vk.cmd->vbo_offset, 0, sizeof( vk.cmd->vbo_offset ) );
	vk.cmd->curr_index_buffer = VK_NULL_HANDLE;
	vk.cmd->curr_index_offset = 0;

	Com_Memset( &vk.cmd->descriptor_set, 0, sizeof( vk.cmd->descriptor_set ) );
	vk.cmd->descriptor_set.start = ~0U;
	//vk.cmd->descriptor_set.end = 0;

	Com_Memset( &vk.cmd->scissor_rect, 0, sizeof( vk.cmd->scissor_rect ) );

	// other stats
	vk.stats.push_size = 0;
}


static void vk_resize_geometry_buffer( void )
{
	int i;

	vk_end_render_pass();

	VK_CHECK( qvkEndCommandBuffer( vk.cmd->command_buffer ) );

	qvkResetCommandBuffer( vk.cmd->command_buffer, 0 );

	vk_wait_idle();

	vk_release_geometry_buffers();

	vk_create_geometry_buffers( vk.geometry_buffer_size_new );
	vk.geometry_buffer_size_new = 0;

	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ )
		vk_update_uniform_descriptor( vk.tess[ i ].uniform_descriptor, vk.tess[ i ].vertex_buffer );

	ri.Printf( PRINT_DEVELOPER, "...geometry buffer resized to %iK\n", (int)( vk.geometry_buffer_size / 1024 ) );
}


void vk_end_frame( void )
{
#ifdef USE_UPLOAD_QUEUE
	VkSemaphore waits[2], signals[2];
	const VkPipelineStageFlags wait_dst_stage_mask[2] = {
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
	};
#else
	const VkPipelineStageFlags wait_dst_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
#endif
	VkSubmitInfo submit_info;

	if ( vk.frame_count == 0 )
		return;

	vk.frame_count = 0;

	if ( vk.geometry_buffer_size_new )
	{
		vk_resize_geometry_buffer();
		// issue: one frame may be lost during video recording
		// solution: re-record all commands again? (might be complicated though)
		vk_lifetime_end_frame( qfalse );
		return;
	}

	if ( vk.fboActive )
	{
		vk.cmd->last_pipeline = VK_NULL_HANDLE; // do not restore clobbered descriptors in vk_bloom()

		if ( r_bloom->integer )
		{
			vk_bloom();
		}

		if ( ( backEnd.screenshotMask || backEnd.levelshotPending ) && vk.capture.image )
		{
			vk_end_render_pass();

			// render to capture FBO
			vk_begin_render_pass( vk.render_pass.capture, vk.framebuffers.capture, qfalse, gls.captureWidth, gls.captureHeight );
			qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.capture_pipeline );
			vk_push_post_process_constants();
			vk_bind_gamma_descriptor_sets();

			qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
		}

		if ( !ri.CL_IsMinimized() && vk.cmd->swapchain_image_acquired )
		{
			vk_end_render_pass();

			vk.renderWidth = gls.windowWidth;
			vk.renderHeight = gls.windowHeight;

			vk.renderScaleX = 1.0;
			vk.renderScaleY = 1.0;

			vk_begin_render_pass( vk.render_pass.gamma, vk.framebuffers.gamma[ vk.cmd->swapchain_image_index ], qfalse, vk.renderWidth, vk.renderHeight );
			qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.gamma_pipeline );
			vk_push_post_process_constants();
			vk_bind_gamma_descriptor_sets();

			qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
		}
	}

	vk_end_render_pass();

	VK_CHECK( qvkEndCommandBuffer( vk.cmd->command_buffer ) );

	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.pNext = NULL;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &vk.cmd->command_buffer;
	if ( !ri.CL_IsMinimized() && vk.cmd->swapchain_image_acquired ) {
#ifdef USE_UPLOAD_QUEUE
		if ( vk.image_uploaded != VK_NULL_HANDLE ) {
			waits[0] = vk.cmd->image_acquired;
			waits[1] = vk.image_uploaded;
			submit_info.waitSemaphoreCount = 2;
			submit_info.pWaitSemaphores = &waits[0];
			submit_info.pWaitDstStageMask = &wait_dst_stage_mask[0];
			signals[0] = vk.swapchain_rendering_finished[ vk.cmd->swapchain_image_index ];
			signals[1] = vk.cmd->rendering_finished2;
			submit_info.signalSemaphoreCount = 2;
			submit_info.pSignalSemaphores = &signals[0];

			vk.rendering_finished = vk.cmd->rendering_finished2;
			vk.image_uploaded = VK_NULL_HANDLE;
		} else if ( vk.rendering_finished != VK_NULL_HANDLE ) {
			waits[0] = vk.cmd->image_acquired;
			waits[1] = vk.rendering_finished;
			submit_info.waitSemaphoreCount = 2;
			submit_info.pWaitSemaphores = &waits[0];
			submit_info.pWaitDstStageMask = &wait_dst_stage_mask[0];
			signals[0] = vk.swapchain_rendering_finished[ vk.cmd->swapchain_image_index ];
			signals[1] = vk.cmd->rendering_finished2;
			submit_info.signalSemaphoreCount = 2;
			submit_info.pSignalSemaphores = &signals[0];

			vk.rendering_finished = vk.cmd->rendering_finished2;
		} else {
			submit_info.waitSemaphoreCount = 1;
			submit_info.pWaitSemaphores = &vk.cmd->image_acquired;
			submit_info.pWaitDstStageMask = &wait_dst_stage_mask[0];
			submit_info.signalSemaphoreCount = 1;
			submit_info.pSignalSemaphores = &vk.swapchain_rendering_finished[ vk.cmd->swapchain_image_index ];
		}
#else
		submit_info.waitSemaphoreCount = 1;
		submit_info.pWaitSemaphores = &vk.cmd->image_acquired;
		submit_info.pWaitDstStageMask = &wait_dst_stage_mask;
		submit_info.signalSemaphoreCount = 1;
		submit_info.pSignalSemaphores = &vk.swapchain_rendering_finished[ vk.cmd->swapchain_image_index ];
#endif
	} else {
		submit_info.waitSemaphoreCount = 0;
		submit_info.pWaitSemaphores = NULL;
		submit_info.pWaitDstStageMask = NULL;
		submit_info.signalSemaphoreCount = 0;
		submit_info.pSignalSemaphores = NULL;
	}

	VK_CHECK( qvkQueueSubmit( vk.queue, 1, &submit_info, vk.cmd->rendering_finished_fence ) );
	vk.cmd->waitForFence = qtrue;

	// presentation may take undefined time to complete, we can't measure it in a reliable way
	backEnd.pc.msec = ri.Milliseconds() - backEnd.pc.msec;
	vk_lifetime_end_frame( qtrue );

	vk.renderPassIndex = RENDER_PASS_MAIN;
}


void vk_present_frame( void )
{
	VkPresentInfoKHR present_info;
	VkResult res;

	if ( ri.CL_IsMinimized() || !vk.cmd->swapchain_image_acquired ) {
		return;
	}

	if ( !vk.cmd->waitForFence ) {
		// nothing has been submitted this frame due to geometry buffer overflow?
		return;
	}

	present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present_info.pNext = NULL;
	present_info.waitSemaphoreCount = 1;
	present_info.pWaitSemaphores = &vk.swapchain_rendering_finished[ vk.cmd->swapchain_image_index ];
	present_info.swapchainCount = 1;
	present_info.pSwapchains = &vk.swapchain;
	present_info.pImageIndices = &vk.cmd->swapchain_image_index;
	present_info.pResults = NULL;

	vk.cmd->swapchain_image_acquired = qfalse;

	res = qvkQueuePresentKHR( vk.queue, &present_info );
	vk_lifetime_mark_use( RTX_VK_RESOURCE_SWAPCHAIN, "vkQueuePresentKHR" );
	switch ( res ) {
		case VK_SUCCESS:
			break;
		case VK_SUBOPTIMAL_KHR:
		case VK_ERROR_OUT_OF_DATE_KHR:
		case VK_ERROR_SURFACE_LOST_KHR:
			// swapchain re-creation needed
			( void )vk_restart_swapchain( __func__, res );
			return;
		case VK_ERROR_DEVICE_LOST:
			// we can ignore that
			ri.Printf( PRINT_DEVELOPER, "vkQueuePresentKHR: device lost\n" );
			break;
		default:
			// or we don't
			ri.Error( ERR_FATAL, "vkQueuePresentKHR returned %s", vk_result_string( res ) );
	}

	// pickup next command buffer for rendering
	vk.cmd_index++;
	vk.cmd_index %= NUM_COMMAND_BUFFERS;
	vk.cmd = &vk.tess[ vk.cmd_index ];
}


static qboolean is_bgr( VkFormat format ) {
	switch ( format ) {
		case VK_FORMAT_B8G8R8A8_UNORM:
		case VK_FORMAT_B8G8R8A8_SNORM:
		case VK_FORMAT_B8G8R8A8_UINT:
		case VK_FORMAT_B8G8R8A8_SINT:
		case VK_FORMAT_B8G8R8A8_SRGB:
		case VK_FORMAT_B4G4R4A4_UNORM_PACK16:
			return qtrue;
		default:
			return qfalse;
	}
}


void vk_read_pixels( byte *buffer, uint32_t width, uint32_t height )
{
	VkCommandBuffer command_buffer;
	VkDeviceMemory memory;
	VkMemoryRequirements memory_requirements;
	VkMemoryPropertyFlags memory_reqs;
	VkMemoryPropertyFlags memory_flags;
	VkMemoryAllocateInfo alloc_info;
	VkImageSubresource subresource;
	VkSubresourceLayout layout;
	VkImageCreateInfo desc;
	VkImage srcImage;
	VkImageLayout srcImageLayout;
	VkImage dstImage;
	byte *buffer_ptr;
	byte *data;
	uint32_t pixel_width;
	uint32_t i, n;
	qboolean invalidate_ptr;

	VK_CHECK( qvkWaitForFences( vk.device, 1, &vk.cmd->rendering_finished_fence, VK_FALSE, 1e12 ) );

	if ( vk.fboActive ) {
		if ( vk.capture.image ) {
			// dedicated capture buffer
			srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			srcImage = vk.capture.image;
		} else {
			srcImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			srcImage = vk.color_image;
		}
	} else {
		srcImageLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		srcImage = vk.swapchain_images[ vk.cmd->swapchain_image_index ];
	}

	Com_Memset( &desc, 0, sizeof( desc ) );

	// Create image in host visible memory to serve as a destination for framebuffer pixels.
	desc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.imageType = VK_IMAGE_TYPE_2D;
	desc.format = vk.capture_format;
	desc.extent.width = width;
	desc.extent.height = height;
	desc.extent.depth = 1;
	desc.mipLevels = 1;
	desc.arrayLayers = 1;
	desc.samples = VK_SAMPLE_COUNT_1_BIT;
	desc.tiling = VK_IMAGE_TILING_LINEAR;
	desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	desc.queueFamilyIndexCount = 0;
	desc.pQueueFamilyIndices = NULL;
	desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VK_CHECK( qvkCreateImage( vk.device, &desc, NULL, &dstImage ) );

	qvkGetImageMemoryRequirements( vk.device, dstImage, &memory_requirements );

	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.pNext = NULL;
	alloc_info.allocationSize = memory_requirements.size;

	// host_cached bit is desirable for fast reads
	memory_reqs = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
	alloc_info.memoryTypeIndex = find_memory_type2( memory_requirements.memoryTypeBits, memory_reqs, &memory_flags );
	if ( alloc_info.memoryTypeIndex == ~0 ) {
		// try less explicit flags, without host_coherent
		memory_reqs = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
		alloc_info.memoryTypeIndex = find_memory_type2( memory_requirements.memoryTypeBits, memory_reqs, &memory_flags );
		if ( alloc_info.memoryTypeIndex == ~0U ) {
			// slowest case
			memory_reqs = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
			alloc_info.memoryTypeIndex = find_memory_type2( memory_requirements.memoryTypeBits, memory_reqs, &memory_flags );
			if ( alloc_info.memoryTypeIndex == ~0U ) {
				ri.Error( ERR_FATAL, "%s(): failed to find matching memory type for image capture", __func__ );
			}
		}
	}

	if ( memory_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ) {
		invalidate_ptr = qfalse;
	} else {
		 // according to specification - must be performed if host_coherent is not set
		invalidate_ptr = qtrue;
	}

	VK_CHECK(qvkAllocateMemory(vk.device, &alloc_info, NULL, &memory));
	VK_CHECK(qvkBindImageMemory(vk.device, dstImage, memory, 0));

	command_buffer = begin_command_buffer();

	if ( srcImageLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ) {
		record_image_layout_transition( command_buffer, srcImage,
			VK_IMAGE_ASPECT_COLOR_BIT,
			srcImageLayout,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			0, 0);
	}

	record_image_layout_transition( command_buffer, dstImage,
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, 0 );

	// end_command_buffer( command_buffer );

	// command_buffer = begin_command_buffer();

	if ( vk.blitEnabled ) {
		VkImageBlit region;

		region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.srcSubresource.mipLevel = 0;
		region.srcSubresource.baseArrayLayer = 0;
		region.srcSubresource.layerCount = 1;
		region.srcOffsets[0].x = 0;
		region.srcOffsets[0].y = 0;
		region.srcOffsets[0].z = 0;
		region.srcOffsets[1].x = width;
		region.srcOffsets[1].y = height;
		region.srcOffsets[1].z = 1;
		region.dstSubresource = region.srcSubresource;
		region.dstOffsets[0] = region.srcOffsets[0];
		region.dstOffsets[1] = region.srcOffsets[1];

		qvkCmdBlitImage( command_buffer, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_NEAREST );

	} else {
		VkImageCopy region;

		region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.srcSubresource.mipLevel = 0;
		region.srcSubresource.baseArrayLayer = 0;
		region.srcSubresource.layerCount = 1;
		region.srcOffset.x = 0;
		region.srcOffset.y = 0;
		region.srcOffset.z = 0;
		region.dstSubresource = region.srcSubresource;
		region.dstOffset = region.srcOffset;
		region.extent.width = width;
		region.extent.height = height;
		region.extent.depth = 1;

		qvkCmdCopyImage( command_buffer, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );
	}

	end_command_buffer( command_buffer, __func__ );

	// Copy data from destination image to memory buffer.
	subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subresource.mipLevel = 0;
	subresource.arrayLayer = 0;

	qvkGetImageSubresourceLayout( vk.device, dstImage, &subresource, &layout );

	VK_CHECK( qvkMapMemory( vk.device, memory, 0, VK_WHOLE_SIZE, 0, (void**)&data ) );

	if ( invalidate_ptr )
	{
		VkMappedMemoryRange range;
		range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		range.pNext = NULL;
		range.memory = memory;
		range.size = VK_WHOLE_SIZE;
		range.offset = 0;
		qvkInvalidateMappedMemoryRanges( vk.device, 1, &range );
	}

	data += layout.offset;

	switch ( vk.capture_format ) {
		case VK_FORMAT_B4G4R4A4_UNORM_PACK16: pixel_width = 2; break;
		case VK_FORMAT_R16G16B16A16_UNORM: pixel_width = 8; break;
		default: pixel_width = 4; break;
	}

	buffer_ptr = buffer + width * (height - 1) * 3;
	for ( i = 0; i < height; i++ ) {
		switch ( pixel_width ) {
			case 2: {
				uint16_t *src = (uint16_t*)data;
				for ( n = 0; n < width; n++ ) {
					buffer_ptr[n*3+0] = ((src[n]>>12)&0xF)<<4;
					buffer_ptr[n*3+1] = ((src[n]>>8)&0xF)<<4;
					buffer_ptr[n*3+2] = ((src[n]>>4)&0xF)<<4;
				}
			} break;

			case 4: {
				for ( n = 0; n < width; n++ ) {
					Com_Memcpy( &buffer_ptr[n*3], &data[n*4], 3 );
					//buffer_ptr[n*3+0] = data[n*4+0];
					//buffer_ptr[n*3+1] = data[n*4+1];
					//buffer_ptr[n*3+2] = data[n*4+2];
				}
			} break;

			case 8: {
				const uint16_t *src = (uint16_t*)data;
				for ( n = 0; n < width; n++ ) {
					buffer_ptr[n*3+0] = src[n*4+0]>>8;
					buffer_ptr[n*3+1] = src[n*4+1]>>8;
					buffer_ptr[n*3+2] = src[n*4+2]>>8;
				}
			} break;
		}
		buffer_ptr -= width * 3;
		data += layout.rowPitch;
	}

	if ( is_bgr( vk.capture_format ) ) {
		buffer_ptr = buffer;
		for ( i = 0; i < width * height; i++ ) {
			byte tmp = buffer_ptr[0];
			buffer_ptr[0] = buffer_ptr[2];
			buffer_ptr[2] = tmp;
			buffer_ptr += 3;
		}
	}

	qvkDestroyImage( vk.device, dstImage, NULL );
	qvkFreeMemory( vk.device, memory, NULL );

	// restore previous layout
	if ( srcImageLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ) {
		command_buffer = begin_command_buffer();

		record_image_layout_transition( command_buffer, srcImage,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			srcImageLayout, 0, 0 );

		end_command_buffer( command_buffer, "restore layout" );
	}
}


qboolean vk_motion_blur( void )
{
	const qboolean requested =
		( r_motionBlur && r_motionBlur->integer &&
		  r_motionBlurStrength && r_motionBlurStrength->value > 0.0f ) ?
			qtrue : qfalse;
	const qboolean resumePostBloom =
		( vk.renderPassIndex == RENDER_PASS_POST_BLOOM ) ? qtrue : qfalse;
	float constants[8] = {
		0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 1.0f
	};
	int viewRect[4];
	VkPipelineLayout layout;

	if ( !requested ) {
		R_MotionBlur_ResetView( &vk_motion_blur_view );
		return qfalse;
	}
	if ( backEnd.doneMotionBlur ) {
		return qfalse;
	}
	if ( ( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) ||
		backEnd.viewParms.portalView != PV_NONE ||
		backEnd.viewParms.stereoFrame != STEREO_CENTER ||
		backEnd.viewParms.viewportX != 0 ||
		backEnd.viewParms.viewportY != 0 ||
		backEnd.viewParms.viewportWidth != glConfig.vidWidth ||
		backEnd.viewParms.viewportHeight != glConfig.vidHeight ) {
		return qfalse;
	}
	if ( vk.renderPassIndex != RENDER_PASS_MAIN &&
		vk.renderPassIndex != RENDER_PASS_POST_BLOOM ) {
		return qfalse;
	}
	if ( !backEnd.doneSurfaces || !vk.fboActive || ri.CL_IsMinimized() ||
		glConfig.stereoEnabled ||
		vk.motion_blur_image == VK_NULL_HANDLE ||
		vk.motion_blur_descriptor == VK_NULL_HANDLE ||
		vk.render_pass.motion_blur == VK_NULL_HANDLE ||
		vk.framebuffers.motion_blur == VK_NULL_HANDLE ||
		vk.motion_blur_pipeline == VK_NULL_HANDLE ||
		vk.motion_blur_copy_pipeline == VK_NULL_HANDLE ) {
		R_MotionBlur_ResetView( &vk_motion_blur_view );
		return qfalse;
	}

	backEnd.doneMotionBlur = qtrue;
	if ( !R_MotionBlur_Calculate( &vk_motion_blur_view, qtrue,
		r_motionBlurStrength->value, ri.Milliseconds(),
		backEnd.refdef.vieworg, backEnd.refdef.viewaxis,
		backEnd.refdef.fov_x, backEnd.refdef.fov_y,
		glConfig.vidWidth, glConfig.vidHeight, constants ) ) {
		return qfalse;
	}
	if ( !R_MotionBlur_CalculateViewRect(
		glConfig.vidWidth, glConfig.vidHeight,
		backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
		backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight,
		viewRect, constants + 4 ) ) {
		return qfalse;
	}

	layout = vk_pipeline_layout_handle( RTX_VK_PIPELINE_LAYOUT_POST_PROCESS );
	vk_end_render_pass();
	vk_begin_render_pass( vk.render_pass.motion_blur,
		vk.framebuffers.motion_blur, qfalse,
		glConfig.vidWidth, glConfig.vidHeight );

	qvkCmdBindPipeline( vk.cmd->command_buffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS, vk.motion_blur_pipeline );
	qvkCmdPushConstants( vk.cmd->command_buffer, layout,
		VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( constants ), constants );
	qvkCmdBindDescriptorSets( vk.cmd->command_buffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
		0, 1, &vk.color_descriptor, 0, NULL );
	qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
	vk_end_render_pass();

	/* Resume the same raster/RT composition pass so later HUD and console
	 * batches stay sharp and a successful RT frame is never suppressed. */
	if ( resumePostBloom ) {
		vk_begin_post_bloom_render_pass();
	} else {
		vk_begin_main_render_pass_load();
	}
	constants[0] = constants[1] = 0.0f;
	qvkCmdBindPipeline( vk.cmd->command_buffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS, vk.motion_blur_copy_pipeline );
	qvkCmdPushConstants( vk.cmd->command_buffer, layout,
		VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( constants ), constants );
	qvkCmdBindDescriptorSets( vk.cmd->command_buffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
		0, 1, &vk.motion_blur_descriptor, 0, NULL );
	qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );

	vk.cmd->last_pipeline = VK_NULL_HANDLE;
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;
	vk.cmd->descriptor_set.start = 0;
	vk.cmd->descriptor_set.end =
		MIN( VK_DESC_COUNT, vk.maxBoundDescriptorSets ) - 1;
	Com_Memset( &vk.cmd->scissor_rect, 0xff,
		sizeof( vk.cmd->scissor_rect ) );
	return qtrue;
}


qboolean vk_bloom( void )
{
	uint32_t i;

	if ( vk.renderPassIndex == RENDER_PASS_SCREENMAP )
	{
		return qfalse;
	}

	if ( backEnd.doneBloom || !backEnd.doneSurfaces || !vk.fboActive )
	{
		return qfalse;
	}

	vk_end_render_pass(); // end main

	// bloom extraction
	vk_begin_bloom_extract_render_pass();
	qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.bloom_extract_pipeline );
	qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline_layout_handle( RTX_VK_PIPELINE_LAYOUT_POST_PROCESS ), 0, 1, &vk.color_descriptor, 0, NULL );
	qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
	vk_end_render_pass();

	for ( i = 0; i < VK_NUM_BLOOM_PASSES*2; i+=2 ) {
		// horizontal blur
		vk_begin_blur_render_pass( i+0 );
		qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.blur_pipeline[i+0] );
		qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline_layout_handle( RTX_VK_PIPELINE_LAYOUT_POST_PROCESS ), 0, 1, &vk.bloom_image_descriptor[i+0], 0, NULL );
		qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
		vk_end_render_pass();

		// vectical blur
		vk_begin_blur_render_pass( i+1 );
		qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.blur_pipeline[i+1] );
		qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline_layout_handle( RTX_VK_PIPELINE_LAYOUT_POST_PROCESS ), 0, 1, &vk.bloom_image_descriptor[i+1], 0, NULL );
		qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
		vk_end_render_pass();
#if 0
		// horizontal blur
		vk_begin_blur_render_pass( i+0 );
		qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.blur_pipeline[i+0] );
		qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline_layout_handle( RTX_VK_PIPELINE_LAYOUT_POST_PROCESS ), 0, 1, &vk.bloom_image_descriptor[i+2], 0, NULL );
		qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
		vk_end_render_pass();

		// vectical blur
		vk_begin_blur_render_pass( i+1 );
		qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.blur_pipeline[i+1] );
		qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline_layout_handle( RTX_VK_PIPELINE_LAYOUT_POST_PROCESS ), 0, 1, &vk.bloom_image_descriptor[i+1], 0, NULL );
		qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
		vk_end_render_pass();
#endif
	}

	vk_begin_post_bloom_render_pass(); // begin post-bloom
	{
		VkDescriptorSet dset[VK_NUM_BLOOM_PASSES];

		for ( i = 0; i < VK_NUM_BLOOM_PASSES; i++ )
		{
			dset[i] = vk.bloom_image_descriptor[(i+1)*2];
		}

		// blend downscaled buffers to main fbo
		qvkCmdBindPipeline( vk.cmd->command_buffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			R_BloomProtectHighlightsActive() &&
				vk.bloom_blend_cel_pipeline != VK_NULL_HANDLE ?
					vk.bloom_blend_cel_pipeline : vk.bloom_blend_pipeline );
		qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline_layout_handle( RTX_VK_PIPELINE_LAYOUT_BLEND ), 0, ARRAY_LEN(dset), dset, 0, NULL );
		qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
	}

	/*
	 * Bloom binds private pipeline layouts after the render-pass helper has
	 * already invalidated last_pipeline.  Force the next material/HUD draw to
	 * rebind every main-layout descriptor instead of attempting a partial,
	 * stale restoration here.
	 */
	vk.cmd->last_pipeline = VK_NULL_HANDLE;
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;
	vk.cmd->descriptor_set.start = 0;
	vk.cmd->descriptor_set.end =
		MIN( VK_DESC_COUNT, vk.maxBoundDescriptorSets ) - 1;
	Com_Memset( &vk.cmd->scissor_rect, 0xff, sizeof( vk.cmd->scissor_rect ) );

	backEnd.doneBloom = qtrue;

	return qtrue;
}
