#include "tr_local.h"
#include "vk.h"
#include "../renderercommon/tr_motion_blur.h"

#if defined (_DEBUG)
#define USE_VK_VALIDATION
#if defined (_WIN32)
#include <windows.h> // for win32 debug callback
#endif
#endif

static int vkSamples = VK_SAMPLE_COUNT_1_BIT;
static int vkMaxSamples = VK_SAMPLE_COUNT_1_BIT;

static int vk_select_sample_count( int requestedSamples, VkSampleCountFlags supportedSamples )
{
	static const int sampleCounts[] = {
		VK_SAMPLE_COUNT_64_BIT,
		VK_SAMPLE_COUNT_32_BIT,
		VK_SAMPLE_COUNT_16_BIT,
		VK_SAMPLE_COUNT_8_BIT,
		VK_SAMPLE_COUNT_4_BIT,
		VK_SAMPLE_COUNT_2_BIT,
		VK_SAMPLE_COUNT_1_BIT
	};
	int i;

	if ( requestedSamples <= 0 ) {
		return VK_SAMPLE_COUNT_1_BIT;
	}
	if ( requestedSamples < 2 ) {
		requestedSamples = 2;
	}

	for ( i = 0; i < ARRAY_LEN( sampleCounts ); i++ ) {
		if ( sampleCounts[ i ] <= requestedSamples &&
			( supportedSamples & sampleCounts[ i ] ) ) {
			return sampleCounts[ i ];
		}
	}

	return VK_SAMPLE_COUNT_1_BIT;
}

#define VK_POST_COLOR_SPACE_SDR 0
#define VK_POST_COLOR_SPACE_HDR10_ST2084 1

static VkInstance vk_instance = VK_NULL_HANDLE;
static VkSurfaceKHR vk_surface = VK_NULL_HANDLE;
static qboolean vk_instance_debug_utils = qfalse;
static qboolean vk_instance_swapchain_colorspace = qfalse;
static char vk_current_render_pass_label[64];
static VkRenderPass vk_current_render_pass = VK_NULL_HANDLE;
static motionBlurViewState_t vk_motion_blur_view;

#ifdef USE_VK_VALIDATION
static VkDebugReportCallbackEXT vk_debug_callback = VK_NULL_HANDLE;
static VkDebugUtilsMessengerEXT vk_debug_messenger = VK_NULL_HANDLE;
#endif

//
// Vulkan API functions used by the renderer.
//
static PFN_vkCreateInstance								qvkCreateInstance;
static PFN_vkEnumerateInstanceExtensionProperties		qvkEnumerateInstanceExtensionProperties;

static PFN_vkCreateDevice								qvkCreateDevice;
static PFN_vkDestroyInstance							qvkDestroyInstance;
static PFN_vkEnumerateDeviceExtensionProperties			qvkEnumerateDeviceExtensionProperties;
static PFN_vkEnumeratePhysicalDevices					qvkEnumeratePhysicalDevices;
static PFN_vkGetDeviceProcAddr							qvkGetDeviceProcAddr;
static PFN_vkGetPhysicalDeviceFeatures					qvkGetPhysicalDeviceFeatures;
static PFN_vkGetPhysicalDeviceFeatures2					qvkGetPhysicalDeviceFeatures2;
static PFN_vkGetPhysicalDeviceFeatures2KHR				qvkGetPhysicalDeviceFeatures2KHR;
static PFN_vkGetPhysicalDeviceFormatProperties			qvkGetPhysicalDeviceFormatProperties;
static PFN_vkGetPhysicalDeviceMemoryProperties			qvkGetPhysicalDeviceMemoryProperties;
static PFN_vkGetPhysicalDeviceProperties				qvkGetPhysicalDeviceProperties;
static PFN_vkGetPhysicalDeviceProperties2				qvkGetPhysicalDeviceProperties2;
static PFN_vkGetPhysicalDeviceProperties2KHR			qvkGetPhysicalDeviceProperties2KHR;
static PFN_vkGetPhysicalDeviceQueueFamilyProperties		qvkGetPhysicalDeviceQueueFamilyProperties;
static PFN_vkDestroySurfaceKHR							qvkDestroySurfaceKHR;
static PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR	qvkGetPhysicalDeviceSurfaceCapabilitiesKHR;
static PFN_vkGetPhysicalDeviceSurfaceFormatsKHR			qvkGetPhysicalDeviceSurfaceFormatsKHR;
static PFN_vkGetPhysicalDeviceSurfacePresentModesKHR	qvkGetPhysicalDeviceSurfacePresentModesKHR;
static PFN_vkGetPhysicalDeviceSurfaceSupportKHR			qvkGetPhysicalDeviceSurfaceSupportKHR;
#ifdef USE_VK_VALIDATION
static PFN_vkCreateDebugReportCallbackEXT				qvkCreateDebugReportCallbackEXT;
static PFN_vkDestroyDebugReportCallbackEXT				qvkDestroyDebugReportCallbackEXT;
static PFN_vkCreateDebugUtilsMessengerEXT				qvkCreateDebugUtilsMessengerEXT;
static PFN_vkDestroyDebugUtilsMessengerEXT				qvkDestroyDebugUtilsMessengerEXT;
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
static PFN_vkCmdCopyImageToBuffer						qvkCmdCopyImageToBuffer;
static PFN_vkCmdDraw									qvkCmdDraw;
static PFN_vkCmdDrawIndexed								qvkCmdDrawIndexed;
static PFN_vkCmdEndRenderPass							qvkCmdEndRenderPass;
static PFN_vkCmdBeginRenderingKHR						qvkCmdBeginRenderingKHR;
static PFN_vkCmdEndRenderingKHR							qvkCmdEndRenderingKHR;
static PFN_vkCmdNextSubpass								qvkCmdNextSubpass;
static PFN_vkCmdPipelineBarrier							qvkCmdPipelineBarrier;
static PFN_vkCmdPipelineBarrier2KHR						qvkCmdPipelineBarrier2KHR;
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
static PFN_vkCreateQueryPool							qvkCreateQueryPool;
static PFN_vkCreateRenderPass							qvkCreateRenderPass;
static PFN_vkCreateRenderPass2KHR						qvkCreateRenderPass2KHR;
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
static PFN_vkDestroyQueryPool							qvkDestroyQueryPool;
static PFN_vkDestroyRenderPass							qvkDestroyRenderPass;
static PFN_vkDestroySampler								qvkDestroySampler;
static PFN_vkDestroySemaphore							qvkDestroySemaphore;
static PFN_vkDestroyShaderModule						qvkDestroyShaderModule;
static PFN_vkDeviceWaitIdle								qvkDeviceWaitIdle;
static PFN_vkEndCommandBuffer							qvkEndCommandBuffer;
static PFN_vkFlushMappedMemoryRanges					qvkFlushMappedMemoryRanges;
static PFN_vkFreeDescriptorSets							qvkFreeDescriptorSets;
static PFN_vkFreeMemory									qvkFreeMemory;
static PFN_vkGetBufferMemoryRequirements				qvkGetBufferMemoryRequirements;
static PFN_vkGetDeviceQueue								qvkGetDeviceQueue;
static PFN_vkGetImageMemoryRequirements					qvkGetImageMemoryRequirements;
static PFN_vkGetImageSubresourceLayout					qvkGetImageSubresourceLayout;
static PFN_vkGetPipelineCacheData						qvkGetPipelineCacheData;
static PFN_vkGetQueryPoolResults						qvkGetQueryPoolResults;
static PFN_vkInvalidateMappedMemoryRanges				qvkInvalidateMappedMemoryRanges;
static PFN_vkMapMemory									qvkMapMemory;
static PFN_vkQueueSubmit								qvkQueueSubmit;
static PFN_vkQueueWaitIdle								qvkQueueWaitIdle;
static PFN_vkResetCommandPool							qvkResetCommandPool;
static PFN_vkResetDescriptorPool						qvkResetDescriptorPool;
static PFN_vkResetFences								qvkResetFences;
static PFN_vkUnmapMemory								qvkUnmapMemory;
static PFN_vkUpdateDescriptorSets						qvkUpdateDescriptorSets;
static PFN_vkWaitForFences								qvkWaitForFences;
static PFN_vkSetHdrMetadataEXT							qvkSetHdrMetadataEXT;
static PFN_vkAcquireNextImageKHR						qvkAcquireNextImageKHR;
static PFN_vkCreateSwapchainKHR							qvkCreateSwapchainKHR;
static PFN_vkDestroySwapchainKHR						qvkDestroySwapchainKHR;
static PFN_vkGetSwapchainImagesKHR						qvkGetSwapchainImagesKHR;
static PFN_vkQueuePresentKHR							qvkQueuePresentKHR;

static PFN_vkGetBufferMemoryRequirements2KHR			qvkGetBufferMemoryRequirements2KHR;
static PFN_vkGetImageMemoryRequirements2KHR				qvkGetImageMemoryRequirements2KHR;

static PFN_vkSetDebugUtilsObjectNameEXT					qvkSetDebugUtilsObjectNameEXT;
static PFN_vkCmdBeginDebugUtilsLabelEXT					qvkCmdBeginDebugUtilsLabelEXT;
static PFN_vkCmdEndDebugUtilsLabelEXT					qvkCmdEndDebugUtilsLabelEXT;
static PFN_vkCmdInsertDebugUtilsLabelEXT				qvkCmdInsertDebugUtilsLabelEXT;
static PFN_vkDebugMarkerSetObjectNameEXT				qvkDebugMarkerSetObjectNameEXT;

////////////////////////////////////////////////////////////////////////////

// forward declaration
VkPipeline create_pipeline( const Vk_Pipeline_Def *def, renderPass_t renderPassIndex, uint32_t def_index );
static void vk_insert_debug_label( VkCommandBuffer command_buffer, const char *name, float r, float g, float b, float a );

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

const char *vk_color_space_string( VkColorSpaceKHR colorSpace )
{
	static char buf[32];

	switch ( colorSpace ) {
		CASE_STR( VK_COLOR_SPACE_SRGB_NONLINEAR_KHR );
		CASE_STR( VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT );
		CASE_STR( VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT );
		CASE_STR( VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT );
		CASE_STR( VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT );
		CASE_STR( VK_COLOR_SPACE_BT709_LINEAR_EXT );
		CASE_STR( VK_COLOR_SPACE_BT709_NONLINEAR_EXT );
		CASE_STR( VK_COLOR_SPACE_BT2020_LINEAR_EXT );
		CASE_STR( VK_COLOR_SPACE_HDR10_ST2084_EXT );
		CASE_STR( VK_COLOR_SPACE_HDR10_HLG_EXT );
		CASE_STR( VK_COLOR_SPACE_PASS_THROUGH_EXT );
		CASE_STR( VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT );
		CASE_STR( VK_COLOR_SPACE_DISPLAY_NATIVE_AMD );
	default:
		Com_sprintf( buf, sizeof( buf ), "colorspace#%i", colorSpace );
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

static qboolean vk_depth_fade_resolve_supported( void );
static qboolean vk_depth_fade_uses_depth_resolve( void );
static qboolean vk_depth_fade_requested( void );
static renderPass_t vk_pipeline_render_pass_index( void );


static void vk_query_modern_device_features( VkPhysicalDevice physical_device,
	qboolean querySynchronization2, qboolean queryDynamicRendering,
	qboolean *synchronization2, qboolean *dynamicRendering )
{
	VkPhysicalDeviceFeatures2 features2;
	VkPhysicalDeviceSynchronization2Features synchronization2_features;
	VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering_features;
	void **pNextPtr;

	*synchronization2 = qfalse;
	*dynamicRendering = qfalse;

	if ( ( !querySynchronization2 && !queryDynamicRendering ) ||
		( qvkGetPhysicalDeviceFeatures2 == NULL && qvkGetPhysicalDeviceFeatures2KHR == NULL ) ) {
		return;
	}

	Com_Memset( &features2, 0, sizeof( features2 ) );
	Com_Memset( &synchronization2_features, 0, sizeof( synchronization2_features ) );
	Com_Memset( &dynamic_rendering_features, 0, sizeof( dynamic_rendering_features ) );

	features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	features2.pNext = NULL;
	pNextPtr = (void **)&features2.pNext;

	synchronization2_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
	synchronization2_features.pNext = NULL;

	dynamic_rendering_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
	dynamic_rendering_features.pNext = NULL;

	if ( querySynchronization2 ) {
		*pNextPtr = &synchronization2_features;
		pNextPtr = (void **)&synchronization2_features.pNext;
	}

	if ( queryDynamicRendering ) {
		*pNextPtr = &dynamic_rendering_features;
	}

	if ( qvkGetPhysicalDeviceFeatures2 != NULL ) {
		qvkGetPhysicalDeviceFeatures2( physical_device, &features2 );
	} else {
		qvkGetPhysicalDeviceFeatures2KHR( physical_device, &features2 );
	}

	*synchronization2 = synchronization2_features.synchronization2 ? qtrue : qfalse;
	*dynamicRendering = dynamic_rendering_features.dynamicRendering ? qtrue : qfalse;
}

static qboolean vk_query_depth_stencil_resolve( VkPhysicalDevice physical_device, VkResolveModeFlagBits *resolveMode )
{
	VkPhysicalDeviceProperties2 properties2;
	VkPhysicalDeviceDepthStencilResolveProperties resolveProperties;
	VkResolveModeFlagBits preferredMode;

	if ( resolveMode == NULL ||
		( qvkGetPhysicalDeviceProperties2 == NULL && qvkGetPhysicalDeviceProperties2KHR == NULL ) ) {
		return qfalse;
	}

	Com_Memset( &properties2, 0, sizeof( properties2 ) );
	Com_Memset( &resolveProperties, 0, sizeof( resolveProperties ) );
	properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	properties2.pNext = &resolveProperties;
	resolveProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES;

	if ( qvkGetPhysicalDeviceProperties2 != NULL ) {
		qvkGetPhysicalDeviceProperties2( physical_device, &properties2 );
	} else {
		qvkGetPhysicalDeviceProperties2KHR( physical_device, &properties2 );
	}

#ifdef USE_REVERSED_DEPTH
	preferredMode = VK_RESOLVE_MODE_MAX_BIT;
#else
	preferredMode = VK_RESOLVE_MODE_MIN_BIT;
#endif

	if ( resolveProperties.supportedDepthResolveModes & preferredMode ) {
		*resolveMode = preferredMode;
		return qtrue;
	}
	if ( resolveProperties.supportedDepthResolveModes & VK_RESOLVE_MODE_SAMPLE_ZERO_BIT ) {
		*resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
		return qtrue;
	}

	return qfalse;
}


static VkPipelineStageFlags2 vk_pipeline_stage_flags2( VkPipelineStageFlags flags )
{
	// Synchronization2 preserves the legacy stage bits in the low mask.
	return (VkPipelineStageFlags2)flags;
}


static VkAccessFlags2 vk_access_flags2( VkAccessFlags flags )
{
	// All current callers use Vulkan 1.x access bits, which alias into sync2.
	return (VkAccessFlags2)flags;
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


static void vk_wait_upload_context( vk_upload_context_t *context, const char *location )
{
	VkResult res;

	if ( context == NULL || !context->submitted ) {
		return;
	}

	res = qvkWaitForFences( vk.device, 1, &context->fence, VK_TRUE, 5 * 1000000000ULL );
	if ( res != VK_SUCCESS ) {
		ri.Error( ERR_FATAL, "Vulkan: upload command fence wait failed with %s at %s", vk_result_string( res ), location ? location : "unknown" );
	}

	VK_CHECK( qvkResetFences( vk.device, 1, &context->fence ) );
	context->submitted = qfalse;
}


static void vk_wait_upload_contexts( const char *location )
{
	uint32_t i;

	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		vk_wait_upload_context( &vk.upload_contexts[i], location );
	}
}


static void vk_reset_command_pool_for_reuse( VkCommandPool command_pool, qboolean upload_pool )
{
	if ( command_pool == VK_NULL_HANDLE ) {
		return;
	}

	VK_CHECK( qvkResetCommandPool( vk.device, command_pool, 0 ) );

	if ( upload_pool ) {
		vk.stats.upload_pool_resets++;
	} else {
		vk.stats.command_pool_resets++;
	}
}


static vk_upload_context_t *vk_find_upload_context( VkCommandBuffer command_buffer )
{
	uint32_t i;

	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		if ( vk.upload_contexts[i].command_buffer == command_buffer ) {
			return &vk.upload_contexts[i];
		}
	}

	return NULL;
}


static VkCommandBuffer begin_command_buffer( void )
{
	vk_upload_context_t *context;
	VkCommandBufferBeginInfo begin_info;

	context = &vk.upload_contexts[ vk.upload_context_index ];
	vk.upload_context_index = ( vk.upload_context_index + 1 ) % NUM_COMMAND_BUFFERS;

	if ( context->command_pool == VK_NULL_HANDLE || context->command_buffer == VK_NULL_HANDLE ) {
		ri.Error( ERR_FATAL, "Vulkan: upload command context is not initialized" );
	}

	vk_wait_upload_context( context, __func__ );
	vk_reset_command_pool_for_reuse( context->command_pool, qtrue );

	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.pNext = NULL;
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	begin_info.pInheritanceInfo = NULL;

	VK_CHECK( qvkBeginCommandBuffer( context->command_buffer, &begin_info ) );

	return context->command_buffer;
}


static void end_command_buffer( VkCommandBuffer command_buffer, const char *location )
{
	vk_upload_context_t *context;
#ifdef USE_UPLOAD_QUEUE
	const VkPipelineStageFlags wait_dst_stage_mask = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
	VkSemaphore waits;
#endif
	VkSubmitInfo submit_info;
	VkCommandBuffer cmdbuf[1];

	cmdbuf[0] = command_buffer;
	context = vk_find_upload_context( command_buffer );
	if ( context == NULL ) {
		ri.Error( ERR_FATAL, "Vulkan: upload command buffer was not allocated from the upload context pool" );
	}

	vk_insert_debug_label( command_buffer, location ? location : "upload helper", 0.5f, 0.7f, 1.0f, 1.0f );

	VK_CHECK( qvkEndCommandBuffer( command_buffer ) );

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

	VK_CHECK( qvkQueueSubmit( vk.queue, 1, &submit_info, context->fence ) );
	context->submitted = qtrue;
	vk_wait_upload_context( context, location );
}


static void record_image_layout_transition( VkCommandBuffer command_buffer, VkImage image, VkImageAspectFlags image_aspect_flags, 
	VkImageLayout old_layout, VkImageLayout new_layout, uint32_t src_stage_override, uint32_t dst_stage_override ) {
	VkImageMemoryBarrier barrier;
	uint32_t src_stage, dst_stage;

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
			src_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
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
			dst_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
			barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
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

	if ( vk.synchronization2 && qvkCmdPipelineBarrier2KHR ) {
		VkImageMemoryBarrier2 barrier2;
		VkDependencyInfo dependency;

		barrier2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		barrier2.pNext = NULL;
		barrier2.srcStageMask = vk_pipeline_stage_flags2( src_stage );
		barrier2.srcAccessMask = vk_access_flags2( barrier.srcAccessMask );
		barrier2.dstStageMask = vk_pipeline_stage_flags2( dst_stage );
		barrier2.dstAccessMask = vk_access_flags2( barrier.dstAccessMask );
		barrier2.oldLayout = barrier.oldLayout;
		barrier2.newLayout = barrier.newLayout;
		barrier2.srcQueueFamilyIndex = barrier.srcQueueFamilyIndex;
		barrier2.dstQueueFamilyIndex = barrier.dstQueueFamilyIndex;
		barrier2.image = barrier.image;
		barrier2.subresourceRange = barrier.subresourceRange;

		dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependency.pNext = NULL;
		dependency.dependencyFlags = 0;
		dependency.memoryBarrierCount = 0;
		dependency.pMemoryBarriers = NULL;
		dependency.bufferMemoryBarrierCount = 0;
		dependency.pBufferMemoryBarriers = NULL;
		dependency.imageMemoryBarrierCount = 1;
		dependency.pImageMemoryBarriers = &barrier2;

		qvkCmdPipelineBarrier2KHR( command_buffer, &dependency );
		vk.stats.sync2_barriers++;
		return;
	}

	qvkCmdPipelineBarrier( command_buffer, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &barrier );
	vk.stats.legacy_barriers++;
}


// debug markers
#define SET_OBJECT_NAME(obj,objName,objType) vk_set_object_name( (uint64_t)(uintptr_t)(obj), (objName), (objType) )

static VkObjectType vk_debug_report_object_type_to_object_type( VkDebugReportObjectTypeEXT objType )
{
	switch ( objType ) {
		case VK_DEBUG_REPORT_OBJECT_TYPE_INSTANCE_EXT: return VK_OBJECT_TYPE_INSTANCE;
		case VK_DEBUG_REPORT_OBJECT_TYPE_PHYSICAL_DEVICE_EXT: return VK_OBJECT_TYPE_PHYSICAL_DEVICE;
		case VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_EXT: return VK_OBJECT_TYPE_DEVICE;
		case VK_DEBUG_REPORT_OBJECT_TYPE_QUEUE_EXT: return VK_OBJECT_TYPE_QUEUE;
		case VK_DEBUG_REPORT_OBJECT_TYPE_SEMAPHORE_EXT: return VK_OBJECT_TYPE_SEMAPHORE;
		case VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_BUFFER_EXT: return VK_OBJECT_TYPE_COMMAND_BUFFER;
		case VK_DEBUG_REPORT_OBJECT_TYPE_FENCE_EXT: return VK_OBJECT_TYPE_FENCE;
		case VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT: return VK_OBJECT_TYPE_DEVICE_MEMORY;
		case VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT: return VK_OBJECT_TYPE_BUFFER;
		case VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT: return VK_OBJECT_TYPE_IMAGE;
		case VK_DEBUG_REPORT_OBJECT_TYPE_QUERY_POOL_EXT: return VK_OBJECT_TYPE_QUERY_POOL;
		case VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_VIEW_EXT: return VK_OBJECT_TYPE_BUFFER_VIEW;
		case VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT: return VK_OBJECT_TYPE_IMAGE_VIEW;
		case VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT: return VK_OBJECT_TYPE_SHADER_MODULE;
		case VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_CACHE_EXT: return VK_OBJECT_TYPE_PIPELINE_CACHE;
		case VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_LAYOUT_EXT: return VK_OBJECT_TYPE_PIPELINE_LAYOUT;
		case VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT: return VK_OBJECT_TYPE_RENDER_PASS;
		case VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT: return VK_OBJECT_TYPE_PIPELINE;
		case VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT_EXT: return VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT;
		case VK_DEBUG_REPORT_OBJECT_TYPE_SAMPLER_EXT: return VK_OBJECT_TYPE_SAMPLER;
		case VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_POOL_EXT: return VK_OBJECT_TYPE_DESCRIPTOR_POOL;
		case VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT: return VK_OBJECT_TYPE_DESCRIPTOR_SET;
		case VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT: return VK_OBJECT_TYPE_FRAMEBUFFER;
		case VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_POOL_EXT: return VK_OBJECT_TYPE_COMMAND_POOL;
		case VK_DEBUG_REPORT_OBJECT_TYPE_SURFACE_KHR_EXT: return VK_OBJECT_TYPE_SURFACE_KHR;
		case VK_DEBUG_REPORT_OBJECT_TYPE_SWAPCHAIN_KHR_EXT: return VK_OBJECT_TYPE_SWAPCHAIN_KHR;
		case VK_DEBUG_REPORT_OBJECT_TYPE_DEBUG_REPORT_CALLBACK_EXT_EXT: return VK_OBJECT_TYPE_DEBUG_REPORT_CALLBACK_EXT;
		case VK_DEBUG_REPORT_OBJECT_TYPE_DISPLAY_KHR_EXT: return VK_OBJECT_TYPE_DISPLAY_KHR;
		case VK_DEBUG_REPORT_OBJECT_TYPE_DISPLAY_MODE_KHR_EXT: return VK_OBJECT_TYPE_DISPLAY_MODE_KHR;
		default: return VK_OBJECT_TYPE_UNKNOWN;
	}
}


static void vk_set_object_name( uint64_t obj, const char *objName, VkDebugReportObjectTypeEXT objType )
{
	if ( qvkSetDebugUtilsObjectNameEXT && obj )
	{
		VkDebugUtilsObjectNameInfoEXT info;
		info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
		info.pNext = NULL;
		info.objectType = vk_debug_report_object_type_to_object_type( objType );
		info.objectHandle = obj;
		info.pObjectName = objName;
		qvkSetDebugUtilsObjectNameEXT( vk.device, &info );
		return;
	}

	if ( qvkDebugMarkerSetObjectNameEXT && obj )
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


static VkCommandPool vk_create_transient_command_pool( const char *name )
{
	VkCommandPoolCreateInfo desc;
	VkCommandPool command_pool;

	desc.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	desc.queueFamilyIndex = vk.queue_family_index;

	VK_CHECK( qvkCreateCommandPool( vk.device, &desc, NULL, &command_pool ) );

	if ( name != NULL ) {
		SET_OBJECT_NAME( command_pool, name, VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_POOL_EXT );
	}

	return command_pool;
}


static void vk_allocate_primary_command_buffer( VkCommandPool command_pool, VkCommandBuffer *command_buffer, const char *name )
{
	VkCommandBufferAllocateInfo alloc_info;

	alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	alloc_info.pNext = NULL;
	alloc_info.commandPool = command_pool;
	alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	alloc_info.commandBufferCount = 1;

	VK_CHECK( qvkAllocateCommandBuffers( vk.device, &alloc_info, command_buffer ) );

	if ( name != NULL ) {
		SET_OBJECT_NAME( *command_buffer, name, VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_BUFFER_EXT );
	}
}


static void vk_destroy_command_contexts( void )
{
	uint32_t i;

#ifdef USE_UPLOAD_QUEUE
	if ( vk.staging_command_pool != VK_NULL_HANDLE ) {
		qvkDestroyCommandPool( vk.device, vk.staging_command_pool, NULL );
		vk.staging_command_pool = VK_NULL_HANDLE;
		vk.staging_command_buffer = VK_NULL_HANDLE;
	}
#endif

	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		if ( vk.upload_contexts[i].command_pool != VK_NULL_HANDLE ) {
			qvkDestroyCommandPool( vk.device, vk.upload_contexts[i].command_pool, NULL );
			vk.upload_contexts[i].command_pool = VK_NULL_HANDLE;
			vk.upload_contexts[i].command_buffer = VK_NULL_HANDLE;
		}
	}

	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		if ( vk.tess[i].command_pool != VK_NULL_HANDLE ) {
			qvkDestroyCommandPool( vk.device, vk.tess[i].command_pool, NULL );
			vk.tess[i].command_pool = VK_NULL_HANDLE;
			vk.tess[i].command_buffer = VK_NULL_HANDLE;
		}
	}
}


static VkMemoryPropertyFlags vk_memory_type_properties( uint32_t memory_type_index )
{
	VkPhysicalDeviceMemoryProperties memory_properties;

	qvkGetPhysicalDeviceMemoryProperties( vk.physical_device, &memory_properties );

	if ( memory_type_index >= memory_properties.memoryTypeCount ) {
		return 0;
	}

	return memory_properties.memoryTypes[ memory_type_index ].propertyFlags;
}


static void vk_track_memory_allocate( const vk_memory_allocation_t *allocation )
{
	if ( allocation == NULL || allocation->memory == VK_NULL_HANDLE ) {
		return;
	}

	vk.stats.memory_allocated += allocation->size;
	vk.stats.memory_allocations++;

	if ( allocation->category < VK_MEMORY_CATEGORY_COUNT ) {
		vk.stats.memory_by_category[ allocation->category ] += allocation->size;
		vk.stats.memory_allocations_by_category[ allocation->category ]++;
	}

	if ( vk.stats.memory_allocated > vk.stats.memory_peak_allocated ) {
		vk.stats.memory_peak_allocated = vk.stats.memory_allocated;
	}
	if ( vk.stats.memory_allocations > vk.stats.memory_peak_allocations ) {
		vk.stats.memory_peak_allocations = vk.stats.memory_allocations;
	}
}


static void vk_track_memory_free( const vk_memory_allocation_t *allocation )
{
	if ( allocation == NULL || allocation->memory == VK_NULL_HANDLE ) {
		return;
	}

	if ( allocation->size <= vk.stats.memory_allocated ) {
		vk.stats.memory_allocated -= allocation->size;
	} else {
		vk.stats.memory_allocated = 0;
	}
	if ( vk.stats.memory_allocations > 0 ) {
		vk.stats.memory_allocations--;
	}

	if ( allocation->category < VK_MEMORY_CATEGORY_COUNT ) {
		if ( allocation->size <= vk.stats.memory_by_category[ allocation->category ] ) {
			vk.stats.memory_by_category[ allocation->category ] -= allocation->size;
		} else {
			vk.stats.memory_by_category[ allocation->category ] = 0;
		}
		if ( vk.stats.memory_allocations_by_category[ allocation->category ] > 0 ) {
			vk.stats.memory_allocations_by_category[ allocation->category ]--;
		}
	}
}


static void vk_allocate_memory( vk_memory_allocation_t *allocation, VkDeviceSize size, uint32_t memory_type_index,
	vk_memory_category_t category, const char *name, const void *pNext )
{
	VkMemoryAllocateInfo alloc_info;

	Com_Memset( allocation, 0, sizeof( *allocation ) );

	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.pNext = pNext;
	alloc_info.allocationSize = size;
	alloc_info.memoryTypeIndex = memory_type_index;

	VK_CHECK( qvkAllocateMemory( vk.device, &alloc_info, NULL, &allocation->memory ) );

	allocation->size = size;
	allocation->memory_type_index = memory_type_index;
	allocation->properties = vk_memory_type_properties( memory_type_index );
	allocation->category = category;

	vk_track_memory_allocate( allocation );

	if ( name != NULL ) {
		SET_OBJECT_NAME( allocation->memory, name, VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT );
	}
}


static void vk_free_memory_allocation( vk_memory_allocation_t *allocation )
{
	if ( allocation == NULL || allocation->memory == VK_NULL_HANDLE ) {
		return;
	}

	vk_track_memory_free( allocation );
	qvkFreeMemory( vk.device, allocation->memory, NULL );
	Com_Memset( allocation, 0, sizeof( *allocation ) );
}


static void vk_reset_transient_stats( void )
{
	vk.stats.vertex_buffer_max = 0;
	vk.stats.push_size = 0;
	vk.stats.push_size_max = 0;
	vk.stats.descriptor_writes = 0;
	vk.stats.descriptor_bind_calls = 0;
	vk.stats.descriptor_bind_sets = 0;
	vk.stats.material_descriptor_hits = 0;
	vk.stats.material_descriptor_misses = 0;
	vk.stats.command_pool_resets = 0;
	vk.stats.upload_pool_resets = 0;
	vk.stats.sync2_barriers = 0;
	vk.stats.legacy_barriers = 0;
}


static uint32_t vk_hash_bytes( const void *data, uint32_t size )
{
	const byte *bytes = (const byte *)data;
	uint32_t hash = 2166136261u;
	uint32_t i;

	for ( i = 0; i < size; i++ ) {
		hash ^= bytes[i];
		hash *= 16777619u;
	}

	return hash;
}


static qboolean vk_pipeline_cache_header_matches( const void *data, uint32_t data_size, const VkPhysicalDeviceProperties *props )
{
	const byte *bytes = (const byte *)data;
	uint32_t header_length;
	uint32_t header_version;
	uint32_t vendor_id;
	uint32_t device_id;

	if ( data == NULL || props == NULL || data_size < 32 ) {
		return qfalse;
	}

	Com_Memcpy( &header_length, bytes + 0, sizeof( header_length ) );
	Com_Memcpy( &header_version, bytes + 4, sizeof( header_version ) );
	Com_Memcpy( &vendor_id, bytes + 8, sizeof( vendor_id ) );
	Com_Memcpy( &device_id, bytes + 12, sizeof( device_id ) );

	if ( header_length < 32 || header_length > data_size ) {
		return qfalse;
	}
	if ( header_version != VK_PIPELINE_CACHE_HEADER_VERSION_ONE ) {
		return qfalse;
	}
	if ( vendor_id != props->vendorID || device_id != props->deviceID ) {
		return qfalse;
	}
	if ( memcmp( bytes + 16, props->pipelineCacheUUID, VK_UUID_SIZE ) != 0 ) {
		return qfalse;
	}

	return qtrue;
}


static void vk_set_pipeline_cache_path( const VkPhysicalDeviceProperties *props )
{
	uint32_t uuid_hash;

	if ( props == NULL ) {
		vk.pipelineCachePath[0] = '\0';
		return;
	}

	uuid_hash = vk_hash_bytes( props->pipelineCacheUUID, VK_UUID_SIZE );
	Com_sprintf( vk.pipelineCachePath, sizeof( vk.pipelineCachePath ),
		"cache/vkpc_%08x_%08x_%08x_%08x.bin",
		props->vendorID, props->deviceID, props->driverVersion, uuid_hash );
}


static void vk_create_pipeline_cache( const VkPhysicalDeviceProperties *props )
{
	VkPipelineCacheCreateInfo ci;
	VkResult res;
	void *cache_data;
	int cache_length;
	qboolean cache_valid;

	Com_Memset( &ci, 0, sizeof( ci ) );
	ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;

	cache_data = NULL;
	cache_length = 0;
	cache_valid = qfalse;

	vk.pipelineCache = VK_NULL_HANDLE;
	vk.pipelineCacheInitialSize = 0;
	vk.pipelineCacheSavedSize = 0;
	vk.pipelineCacheLoaded = qfalse;
	vk.pipelineCacheSaveFailed = qfalse;
	vk_set_pipeline_cache_path( props );

	if ( vk.pipelineCachePath[0] != '\0' ) {
		cache_length = ri.FS_ReadFile( vk.pipelineCachePath, &cache_data );
		if ( cache_length > 0 ) {
			if ( cache_length <= VK_PIPELINE_CACHE_MAX_BYTES &&
				vk_pipeline_cache_header_matches( cache_data, (uint32_t)cache_length, props ) ) {
				ci.initialDataSize = (size_t)cache_length;
				ci.pInitialData = cache_data;
				vk.pipelineCacheInitialSize = (uint32_t)cache_length;
				cache_valid = qtrue;
			} else {
				ri.Printf( PRINT_DEVELOPER, "Vulkan: ignoring stale pipeline cache %s\n", vk.pipelineCachePath );
			}
		}
	}

	res = qvkCreatePipelineCache( vk.device, &ci, NULL, &vk.pipelineCache );
	if ( res != VK_SUCCESS && cache_valid ) {
		ri.Printf( PRINT_WARNING, "Vulkan: failed to load pipeline cache %s (%s), retrying empty\n",
			vk.pipelineCachePath, vk_result_string( res ) );
		ci.initialDataSize = 0;
		ci.pInitialData = NULL;
		vk.pipelineCacheInitialSize = 0;
		cache_valid = qfalse;
		res = qvkCreatePipelineCache( vk.device, &ci, NULL, &vk.pipelineCache );
	}

	if ( cache_data != NULL ) {
		ri.FS_FreeFile( cache_data );
	}

	if ( res != VK_SUCCESS ) {
		ri.Error( ERR_FATAL, "Vulkan: vkCreatePipelineCache returned %s", vk_result_string( res ) );
	}

	vk.pipelineCacheLoaded = cache_valid;
	SET_OBJECT_NAME( vk.pipelineCache, "pipeline cache", VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_CACHE_EXT );

	if ( vk.pipelineCacheLoaded ) {
		ri.Printf( PRINT_DEVELOPER, "Vulkan: loaded pipeline cache %s (%u KB)\n",
			vk.pipelineCachePath, ( vk.pipelineCacheInitialSize + 1023 ) / 1024 );
	}
}


static void vk_save_pipeline_cache( void )
{
	void *cache_data;
	size_t cache_size;
	VkResult res;

	if ( vk.pipelineCache == VK_NULL_HANDLE || qvkGetPipelineCacheData == NULL ||
		vk.pipelineCachePath[0] == '\0' || vk.pipelineCacheSaveFailed ) {
		return;
	}

	cache_size = 0;
	res = qvkGetPipelineCacheData( vk.device, vk.pipelineCache, &cache_size, NULL );
	if ( res != VK_SUCCESS ) {
		ri.Printf( PRINT_WARNING, "Vulkan: vkGetPipelineCacheData size query returned %s\n", vk_result_string( res ) );
		vk.pipelineCacheSaveFailed = qtrue;
		return;
	}

	if ( cache_size == 0 ) {
		return;
	}
	if ( cache_size > VK_PIPELINE_CACHE_MAX_BYTES ) {
		ri.Printf( PRINT_WARNING, "Vulkan: pipeline cache is too large to save (%u KB)\n", (unsigned int)( ( cache_size + 1023 ) / 1024 ) );
		vk.pipelineCacheSaveFailed = qtrue;
		return;
	}

	cache_data = ri.Malloc( (int)cache_size );
	res = qvkGetPipelineCacheData( vk.device, vk.pipelineCache, &cache_size, cache_data );
	if ( res != VK_SUCCESS ) {
		ri.Printf( PRINT_WARNING, "Vulkan: vkGetPipelineCacheData returned %s while saving\n", vk_result_string( res ) );
		vk.pipelineCacheSaveFailed = qtrue;
	} else if ( cache_size > VK_PIPELINE_CACHE_MAX_BYTES ) {
		ri.Printf( PRINT_WARNING, "Vulkan: pipeline cache grew too large to save (%u KB)\n", (unsigned int)( ( cache_size + 1023 ) / 1024 ) );
		vk.pipelineCacheSaveFailed = qtrue;
	} else {
		ri.FS_WriteFile( vk.pipelineCachePath, cache_data, (int)cache_size );
		vk.pipelineCacheSavedSize = (uint32_t)cache_size;
		ri.Printf( PRINT_DEVELOPER, "Vulkan: saved pipeline cache %s (%u KB)\n",
			vk.pipelineCachePath, ( vk.pipelineCacheSavedSize + 1023 ) / 1024 );
	}
	ri.Free( cache_data );
}


static void vk_begin_debug_label( VkCommandBuffer command_buffer, const char *name, float r, float g, float b, float a )
{
	VkDebugUtilsLabelEXT label;

	if ( !qvkCmdBeginDebugUtilsLabelEXT || !command_buffer || !name ) {
		return;
	}

	label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
	label.pNext = NULL;
	label.pLabelName = name;
	label.color[0] = r;
	label.color[1] = g;
	label.color[2] = b;
	label.color[3] = a;
	qvkCmdBeginDebugUtilsLabelEXT( command_buffer, &label );
}


static void vk_end_debug_label( VkCommandBuffer command_buffer )
{
	if ( qvkCmdEndDebugUtilsLabelEXT && command_buffer ) {
		qvkCmdEndDebugUtilsLabelEXT( command_buffer );
	}
}


static void vk_insert_debug_label( VkCommandBuffer command_buffer, const char *name, float r, float g, float b, float a )
{
	VkDebugUtilsLabelEXT label;

	if ( !qvkCmdInsertDebugUtilsLabelEXT || !command_buffer || !name ) {
		return;
	}

	label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
	label.pNext = NULL;
	label.pLabelName = name;
	label.color[0] = r;
	label.color[1] = g;
	label.color[2] = b;
	label.color[3] = a;
	qvkCmdInsertDebugUtilsLabelEXT( command_buffer, &label );
}


static void vk_reset_frame_timestamps( vk_tess_t *cmd )
{
	if ( !vk.timestamps || cmd->timestamp_query_pool == VK_NULL_HANDLE ) {
		return;
	}

	cmd->timestamp_query_count = 0;
	cmd->timestamp_query_valid = qfalse;
	qvkCmdResetQueryPool( cmd->command_buffer, cmd->timestamp_query_pool, 0, VK_MAX_FRAME_TIMESTAMPS );
}


static void vk_write_timestamp( const char *name, VkPipelineStageFlagBits stage )
{
	uint32_t query_index;

	if ( !vk.timestamps || vk.cmd == NULL || vk.cmd->timestamp_query_pool == VK_NULL_HANDLE ) {
		return;
	}
	if ( vk.cmd->timestamp_query_count >= VK_MAX_FRAME_TIMESTAMPS ) {
		return;
	}

	query_index = vk.cmd->timestamp_query_count++;
	Q_strncpyz( vk.cmd->timestamp_query_names[ query_index ], name ? name : "timestamp", sizeof( vk.cmd->timestamp_query_names[ query_index ] ) );
	qvkCmdWriteTimestamp( vk.cmd->command_buffer, stage, vk.cmd->timestamp_query_pool, query_index );
	vk.cmd->timestamp_query_valid = qtrue;
}


static void vk_report_frame_timestamps( vk_tess_t *cmd )
{
	uint64_t timestamps[ VK_MAX_FRAME_TIMESTAMPS ];
	uint64_t mask;
	uint32_t i;
	VkResult res;

	if ( !vk.timestamps || cmd->timestamp_query_pool == VK_NULL_HANDLE || !cmd->timestamp_query_valid ) {
		return;
	}

	if ( r_speeds == NULL || r_speeds->integer != 7 || cmd->timestamp_query_count < 2 ) {
		cmd->timestamp_query_valid = qfalse;
		return;
	}

	res = qvkGetQueryPoolResults( vk.device, cmd->timestamp_query_pool, 0, cmd->timestamp_query_count,
		sizeof( timestamps[0] ) * cmd->timestamp_query_count, timestamps, sizeof( timestamps[0] ),
		VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT );
	if ( res != VK_SUCCESS ) {
		ri.Printf( PRINT_WARNING, "Vulkan: timestamp query read failed with %s\n", vk_result_string( res ) );
		cmd->timestamp_query_valid = qfalse;
		return;
	}

	if ( vk.timestampValidBits < 64 ) {
		mask = ( 1ULL << vk.timestampValidBits ) - 1ULL;
	} else {
		mask = ~0ULL;
	}

	ri.Printf( PRINT_ALL, "Vulkan GPU timings:\n" );
	for ( i = 1; i < cmd->timestamp_query_count; i++ ) {
		uint64_t delta = ( timestamps[i] - timestamps[i - 1] ) & mask;
		double msec = (double)delta * (double)vk.timestampPeriod / 1000000.0;
		ri.Printf( PRINT_ALL, "  %s -> %s: %.3f ms\n", cmd->timestamp_query_names[i - 1], cmd->timestamp_query_names[i], msec );
	}

	cmd->timestamp_query_valid = qfalse;
}


static void vk_set_hdr_metadata( void )
{
	VkHdrMetadataEXT metadata;
	float max_luminance;
	float max_cll;
	float max_fall;

	if ( !vk.hdrDisplayActive || !vk.hdrMetadata || !qvkSetHdrMetadataEXT || vk.swapchain == VK_NULL_HANDLE ) {
		return;
	}

	max_luminance = Com_Clamp( 200.0f, 10000.0f, r_hdrDisplayMaxLuminance->value );
	if ( vk.displayOutput.maxLuminanceNits >= 200.0f ) {
		max_luminance = Com_Clamp( 200.0f, max_luminance, vk.displayOutput.maxLuminanceNits );
	}
	max_cll = Com_Clamp( 200.0f, 10000.0f, r_hdrDisplayMaxCLL->value );
	if ( max_cll > max_luminance ) {
		max_cll = max_luminance;
	}
	max_fall = Com_Clamp( 80.0f, max_cll, r_hdrDisplayMaxFALL->value );

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
		ri.Printf( PRINT_ALL, "...selected presentation mode: %s, image count: %i, format: %s, %s%s\n",
			pmode_to_str( present_mode ), image_count, vk_format_string( surface_format.format ),
			vk_color_space_string( surface_format.colorSpace ), vk.hdrDisplayActive ? " (native HDR)" : "" );
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
		ri.Printf( PRINT_WARNING, "...%s is not available, presenting HDR10 without static HDR metadata\n", VK_EXT_HDR_METADATA_EXTENSION_NAME );
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
	deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	deps[1].srcSubpass = 0;
	deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	Com_Memset( &desc, 0, sizeof( desc ) );
	desc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	desc.pAttachments = &attachment;
	desc.attachmentCount = 1;
	desc.pSubpasses = &subpass;
	desc.subpassCount = 1;
	desc.pDependencies = deps;
	desc.dependencyCount = 2;

	VK_CHECK( qvkCreateRenderPass( vk.device, &desc, NULL, &vk.render_pass.dlight_shadow ) );
	SET_OBJECT_NAME( vk.render_pass.dlight_shadow, "render pass - sampled shadow atlas", VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT );
}

static void vk_attachment_reference2_from_legacy( VkAttachmentReference2 *dst, const VkAttachmentReference *src )
{
	Com_Memset( dst, 0, sizeof( *dst ) );
	dst->sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
	if ( src ) {
		dst->attachment = src->attachment;
		dst->layout = src->layout;
	} else {
		dst->attachment = VK_ATTACHMENT_UNUSED;
		dst->layout = VK_IMAGE_LAYOUT_UNDEFINED;
	}
}


static VkResult vk_create_render_pass2_with_depth_resolve( const VkRenderPassCreateInfo *srcDesc,
	const VkAttachmentReference *depthResolveRef, VkRenderPass *renderPass )
{
	VkAttachmentDescription2 attachments2[4];
	VkAttachmentReference2 colorRefs2[1];
	VkAttachmentReference2 colorResolveRefs2[1];
	VkAttachmentReference2 depthRef2;
	VkAttachmentReference2 depthResolveRef2;
	VkSubpassDescriptionDepthStencilResolve depthResolve;
	VkSubpassDescription2 subpass2;
	VkSubpassDependency2 deps2[3];
	VkRenderPassCreateInfo2 desc2;
	const VkSubpassDescription *srcSubpass;
	uint32_t i;

	if ( !qvkCreateRenderPass2KHR || !srcDesc || !srcDesc->pSubpasses ||
		srcDesc->subpassCount != 1 || srcDesc->attachmentCount > ARRAY_LEN( attachments2 ) ) {
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	}

	srcSubpass = &srcDesc->pSubpasses[0];
	if ( srcSubpass->colorAttachmentCount > ARRAY_LEN( colorRefs2 ) ||
		srcDesc->dependencyCount > ARRAY_LEN( deps2 ) ) {
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	Com_Memset( attachments2, 0, sizeof( attachments2 ) );
	for ( i = 0; i < srcDesc->attachmentCount; i++ ) {
		attachments2[i].sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
		attachments2[i].flags = srcDesc->pAttachments[i].flags;
		attachments2[i].format = srcDesc->pAttachments[i].format;
		attachments2[i].samples = srcDesc->pAttachments[i].samples;
		attachments2[i].loadOp = srcDesc->pAttachments[i].loadOp;
		attachments2[i].storeOp = srcDesc->pAttachments[i].storeOp;
		attachments2[i].stencilLoadOp = srcDesc->pAttachments[i].stencilLoadOp;
		attachments2[i].stencilStoreOp = srcDesc->pAttachments[i].stencilStoreOp;
		attachments2[i].initialLayout = srcDesc->pAttachments[i].initialLayout;
		attachments2[i].finalLayout = srcDesc->pAttachments[i].finalLayout;
	}

	for ( i = 0; i < srcSubpass->colorAttachmentCount; i++ ) {
		vk_attachment_reference2_from_legacy( &colorRefs2[i], &srcSubpass->pColorAttachments[i] );
		if ( srcSubpass->pResolveAttachments ) {
			vk_attachment_reference2_from_legacy( &colorResolveRefs2[i], &srcSubpass->pResolveAttachments[i] );
		}
	}
	vk_attachment_reference2_from_legacy( &depthRef2, srcSubpass->pDepthStencilAttachment );
	vk_attachment_reference2_from_legacy( &depthResolveRef2, depthResolveRef );

	Com_Memset( &depthResolve, 0, sizeof( depthResolve ) );
	depthResolve.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE;
	depthResolve.depthResolveMode = vk.depthResolveMode;
	depthResolve.stencilResolveMode = VK_RESOLVE_MODE_NONE;
	depthResolve.pDepthStencilResolveAttachment = &depthResolveRef2;

	Com_Memset( &subpass2, 0, sizeof( subpass2 ) );
	subpass2.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
	subpass2.pNext = &depthResolve;
	subpass2.flags = srcSubpass->flags;
	subpass2.pipelineBindPoint = srcSubpass->pipelineBindPoint;
	subpass2.inputAttachmentCount = srcSubpass->inputAttachmentCount;
	subpass2.pColorAttachments = colorRefs2;
	subpass2.colorAttachmentCount = srcSubpass->colorAttachmentCount;
	subpass2.pResolveAttachments = srcSubpass->pResolveAttachments ? colorResolveRefs2 : NULL;
	subpass2.pDepthStencilAttachment = srcSubpass->pDepthStencilAttachment ? &depthRef2 : NULL;
	subpass2.preserveAttachmentCount = srcSubpass->preserveAttachmentCount;
	subpass2.pPreserveAttachments = srcSubpass->pPreserveAttachments;

	Com_Memset( deps2, 0, sizeof( deps2 ) );
	for ( i = 0; i < srcDesc->dependencyCount; i++ ) {
		deps2[i].sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
		deps2[i].srcSubpass = srcDesc->pDependencies[i].srcSubpass;
		deps2[i].dstSubpass = srcDesc->pDependencies[i].dstSubpass;
		deps2[i].srcStageMask = srcDesc->pDependencies[i].srcStageMask;
		deps2[i].dstStageMask = srcDesc->pDependencies[i].dstStageMask;
		deps2[i].srcAccessMask = srcDesc->pDependencies[i].srcAccessMask;
		deps2[i].dstAccessMask = srcDesc->pDependencies[i].dstAccessMask;
		deps2[i].dependencyFlags = srcDesc->pDependencies[i].dependencyFlags;
	}

	Com_Memset( &desc2, 0, sizeof( desc2 ) );
	desc2.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2;
	desc2.flags = srcDesc->flags;
	desc2.attachmentCount = srcDesc->attachmentCount;
	desc2.pAttachments = attachments2;
	desc2.subpassCount = 1;
	desc2.pSubpasses = &subpass2;
	desc2.dependencyCount = srcDesc->dependencyCount;
	desc2.pDependencies = deps2;

	return qvkCreateRenderPass2KHR( vk.device, &desc2, NULL, renderPass );
}


static void vk_create_render_passes( void )
{
	VkAttachmentDescription attachments[4]; // color | depth | msaa color | depth resolve
	VkAttachmentReference colorResolveRef;
	VkAttachmentReference colorRef0;
	VkAttachmentReference depthRef0;
	VkAttachmentReference depthResolveRef;
	VkSubpassDescription subpass;
	VkSubpassDependency deps[3];
	VkSubpassDependency captureDeps[2];
	VkRenderPassCreateInfo desc;
	VkFormat depth_format;
	VkDevice device;
	qboolean depthResolveActive;
	qboolean liquidCaptureActive;
	uint32_t i;

	depth_format = vk.depth_format;
	device = vk.device;
	depthResolveActive = vk_depth_fade_uses_depth_resolve();
	liquidCaptureActive = ( r_fbo->integer && r_liquid &&
		r_liquid->integer ) ? qtrue : qfalse;

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
	if ( r_bloom->integer || ( r_motionBlur && r_motionBlur->integer ) ||
		vk_depth_fade_supported() || liquidCaptureActive ) {
		attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE; // keep it for post-bloom pass
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
		if ( r_bloom->integer || ( r_motionBlur && r_motionBlur->integer ) ||
			liquidCaptureActive ) {
			attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE; // keep it for post-bloom pass
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

	if ( depthResolveActive ) {
		attachments[3].flags = 0;
		attachments[3].format = depth_format;
		attachments[3].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[3].initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		attachments[3].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		desc.attachmentCount = 4;
		depthResolveRef.attachment = 3;
		depthResolveRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
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
		attachments[1].stencilLoadOp = glConfig.stencilBits ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		VK_CHECK( qvkCreateRenderPass( device, &desc, NULL, &vk.render_pass.main_load ) );
		SET_OBJECT_NAME( vk.render_pass.main_load, "render pass - main load", VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT );

		vk_create_dlight_shadow_render_pass( depth_format );

		return;
	}

	desc.dependencyCount = 2;
	desc.pDependencies = &deps[0];

	deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	deps[0].dstSubpass = 0;
	deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;			// What pipeline stage must have completed for the dependency
	deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;	// What pipeline stage is waiting on the dependency
	deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;						// What access scopes are influence the dependency
	deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; // What access scopes are waiting on the dependency
	deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;					// Only need the current fragment (or tile) synchronized, not the whole framebuffer

	deps[1].srcSubpass = 0;
	deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;	// Fragment data has been written
	deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;			// Don't start shading until data is available
	deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;			// Waiting for color data to be written
	deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;						// Don't read things from the shader before ready
	deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;					// Only need the current fragment (or tile) synchronized, not the whole framebuffer

	if ( depthResolveActive || liquidCaptureActive ) {
		deps[0].dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		deps[0].dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		deps[1].srcStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		deps[1].srcAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		deps[1].dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		deps[1].dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	}
	if ( liquidCaptureActive && vk.msaaActive ) {
		deps[0].srcStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		deps[0].srcAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		deps[1].dstStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		deps[1].dstAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	}

	if ( depthResolveActive ) {
		VK_CHECK( vk_create_render_pass2_with_depth_resolve( &desc, &depthResolveRef, &vk.render_pass.main ) );
	} else {
		VK_CHECK( qvkCreateRenderPass( device, &desc, NULL, &vk.render_pass.main ) );
	}
	SET_OBJECT_NAME( vk.render_pass.main, "render pass - main", VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT );

	if ( depthResolveActive ) {
		desc.attachmentCount = 3;
	}
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachments[1].stencilLoadOp = glConfig.stencilBits ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	if ( vk.msaaActive ) {
		attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	}
	VK_CHECK( qvkCreateRenderPass( device, &desc, NULL, &vk.render_pass.main_load ) );
	SET_OBJECT_NAME( vk.render_pass.main_load, "render pass - main load", VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT );

	vk_create_dlight_shadow_render_pass( depth_format );

	if ( r_bloom->integer ) {

		// post-bloom pass
		// color buffer
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // load from previous pass
		 // depth buffer
		attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		if ( vk.msaaActive ) {
			// msaa render target
			attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		}
		if ( depthResolveActive ) {
			desc.attachmentCount = 3;
		}
		VK_CHECK( qvkCreateRenderPass( device, &desc, NULL, &vk.render_pass.post_bloom ) );
		SET_OBJECT_NAME( vk.render_pass.post_bloom, "render pass - post_bloom", VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT );

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

	if ( r_motionBlur && r_motionBlur->integer ) {
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

		VK_CHECK( qvkCreateRenderPass( device, &desc, NULL, &vk.render_pass.motion_blur ) );
		SET_OBJECT_NAME( vk.render_pass.motion_blur, "render pass - motion blur", VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT );
	}

	// SDR capture render pass (normal screenshots and cubemap face extraction)
	{
		Com_Memset( &subpass, 0, sizeof( subpass ) );
		Com_Memset( captureDeps, 0, sizeof( captureDeps ) );

		captureDeps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		captureDeps[0].dstSubpass = 0;
		captureDeps[0].srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		captureDeps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		captureDeps[0].srcAccessMask = 0;
		captureDeps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		captureDeps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
		captureDeps[1].srcSubpass = 0;
		captureDeps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		captureDeps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		captureDeps[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		captureDeps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		captureDeps[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		captureDeps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

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
		desc.dependencyCount = ARRAY_LEN( captureDeps );
		desc.pDependencies = captureDeps;

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
		/* Scaling and later warped reads cross framebuffer regions, so these
		 * dependencies must make the whole image visible rather than only the
		 * same tile/pixel region. */
		liquidDeps[0].dependencyFlags = 0;
		liquidDeps[1].srcSubpass = 0;
		liquidDeps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		liquidDeps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		liquidDeps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		liquidDeps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		liquidDeps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		liquidDeps[1].dependencyFlags = 0;

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
		SET_OBJECT_NAME( vk.render_pass.liquid_snapshot, "render pass - liquid snapshot",
			VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT );
	}
}


static void allocate_and_bind_image_memory(VkImage image) {
	VkMemoryRequirements memory_requirements;
	VkDeviceSize chunk_size;
	VkDeviceSize alignment;
	ImageChunk *chunk;
	VkDeviceSize offset;
	uint32_t memory_type_index;
	int i;

	qvkGetImageMemoryRequirements(vk.device, image, &memory_requirements);
	memory_type_index = find_memory_type( memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );

	chunk = NULL;
	offset = 0;

	// Try to find an existing chunk of sufficient capacity.
	alignment = memory_requirements.alignment;
	for ( i = 0; i < vk_world.num_image_chunks; i++ ) {
		if ( vk_world.image_chunks[i].allocation.memory_type_index != memory_type_index ) {
			continue;
		}

		// ensure that memory region has proper alignment
		offset = PAD( vk_world.image_chunks[i].used, alignment );

		if ( offset + memory_requirements.size <= vk_world.image_chunks[i].allocation.size ) {
			chunk = &vk_world.image_chunks[i];
			chunk->used = offset + memory_requirements.size;
			break;
		}
	}

	// Allocate a new chunk in case we couldn't find suitable existing chunk.
	if (chunk == NULL) {
		if (vk_world.num_image_chunks >= MAX_IMAGE_CHUNKS) {
			ri.Error(ERR_FATAL, "Vulkan: image chunk limit has been reached" );
		}

		chunk = &vk_world.image_chunks[vk_world.num_image_chunks];
		chunk_size = MAX( vk.image_chunk_size, PAD( memory_requirements.size, alignment ) );
		chunk->used = memory_requirements.size;
		offset = 0;

		vk_allocate_memory( &chunk->allocation, chunk_size, memory_type_index, VK_MEMORY_CATEGORY_WORLD_IMAGE,
			va( "world image memory chunk %i", vk_world.num_image_chunks ), NULL );

		vk_world.num_image_chunks++;
	}

	VK_CHECK(qvkBindImageMemory(vk.device, image, chunk->allocation.memory, offset));
}


static void vk_clean_staging_buffer( void )
{
	if ( vk.staging_buffer.handle != VK_NULL_HANDLE ) {
		qvkDestroyBuffer( vk.device, vk.staging_buffer.handle, NULL );
		vk.staging_buffer.handle = VK_NULL_HANDLE;
	}

	//if ( vk.staging_buffer.ptr != NULL ) 
	//	qvkUnmapMemory( vk.device, vk.staging_buffer.allocation.memory ) {
	//	vk.staging_buffer.ptr = NULL;
	//}

	vk_free_memory_allocation( &vk.staging_buffer.allocation );

	vk.staging_buffer.ptr = NULL;
	vk.staging_buffer.size = 0;
#ifdef USE_UPLOAD_QUEUE
	vk.staging_buffer.offset = 0;
#endif
}


#ifdef USE_UPLOAD_QUEUE
static qboolean vk_wait_staging_buffer( void )
{
	if ( vk.aux_fence_wait ) {
		VkResult res = qvkWaitForFences( vk.device, 1, &vk.aux_fence, VK_TRUE, 5 * 1000000000ULL );
		if ( res != VK_SUCCESS ) {
			ri.Error( ERR_FATAL, "vkWaitForFences() failed with %s at %s", vk_result_string( res ), __func__ );
		}
		qvkResetFences( vk.device, 1, &vk.aux_fence );
		vk_reset_command_pool_for_reuse( vk.staging_command_pool, qtrue );
		vk.staging_buffer.offset = 0; // FIXME: is this correct?
		vk.aux_fence_wait = qfalse;
		return qtrue;
	} else {
		return qfalse;
	}
}


static void vk_flush_staging_buffer( qboolean final )
{
	const VkPipelineStageFlags wait_dst_stage_mask = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
	VkSemaphore waits;
	VkSubmitInfo submit_info;
	VkResult res;

	if ( vk.staging_buffer.offset == 0 ) {
		return;
	}

	//ri.Printf( PRINT_WARNING, S_COLOR_CYAN ">>> flush %i bytes (final=%i)<<<\n", (int)vk_world.staging_buffer_offset, final );

	vk.staging_buffer.offset = 0;

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
		VK_CHECK( qvkQueueSubmit( vk.queue, 1, &submit_info, vk.aux_fence ) );
		vk.aux_fence_wait = qtrue;
	} else {
		// if submission before another upload then do explicit wait
		submit_info.signalSemaphoreCount = 0;
		submit_info.pSignalSemaphores = NULL;
		VK_CHECK( qvkQueueSubmit( vk.queue, 1, &submit_info, vk.aux_fence ) );
		res = qvkWaitForFences( vk.device, 1, &vk.aux_fence, VK_TRUE, 5 * 1000000000ULL );
		if ( res != VK_SUCCESS ) {
			ri.Error( ERR_FATAL, "vkWaitForFences() failed with %s at %s", vk_result_string( res ), __func__ );
		}
		qvkResetFences( vk.device, 1, &vk.aux_fence );
		vk_reset_command_pool_for_reuse( vk.staging_command_pool, qtrue );
	}
}
#endif // USE_UPLOAD_QUEUE


static void vk_alloc_staging_buffer( VkDeviceSize size )
{
	VkBufferCreateInfo buffer_desc;
	VkMemoryRequirements memory_requirements;
	uint32_t memory_type;
	void *data;

	vk_clean_staging_buffer();

	vk.staging_buffer.size = MAX( size, STAGING_BUFFER_SIZE );
	vk.staging_buffer.size = PAD( vk.staging_buffer.size, 1024 * 1024 );

	buffer_desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_desc.pNext = NULL;
	buffer_desc.flags = 0;
	buffer_desc.size = vk.staging_buffer.size;
	buffer_desc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	buffer_desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	buffer_desc.queueFamilyIndexCount = 0;
	buffer_desc.pQueueFamilyIndices = NULL;
	VK_CHECK(qvkCreateBuffer(vk.device, &buffer_desc, NULL, &vk.staging_buffer.handle));

	qvkGetBufferMemoryRequirements( vk.device, vk.staging_buffer.handle, &memory_requirements );

	memory_type = find_memory_type( memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );

	vk_allocate_memory( &vk.staging_buffer.allocation, memory_requirements.size, memory_type,
		VK_MEMORY_CATEGORY_STAGING, "staging buffer memory", NULL );
	VK_CHECK(qvkBindBufferMemory(vk.device, vk.staging_buffer.handle, vk.staging_buffer.allocation.memory, 0));

	VK_CHECK(qvkMapMemory(vk.device, vk.staging_buffer.allocation.memory, 0, VK_WHOLE_SIZE, 0, &data));
	vk.staging_buffer.ptr = (byte*)data;
#ifdef USE_UPLOAD_QUEUE
	vk.staging_buffer.offset = 0;
#endif
	SET_OBJECT_NAME( vk.staging_buffer.handle, "staging buffer", VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT );
}


#ifdef USE_VK_VALIDATION
static const char *debug_utils_severity_string( VkDebugUtilsMessageSeverityFlagBitsEXT severity )
{
	if ( severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ) {
		return "error";
	}
	if ( severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT ) {
		return "warning";
	}
	if ( severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT ) {
		return "info";
	}
	return "verbose";
}


static void print_debug_message( const char *source, const char *severity, const char *message )
{
	ri.Printf( PRINT_WARNING, "Vulkan %s %s: %s\n", source, severity ? severity : "message", message ? message : "" );
#ifdef _WIN32
	OutputDebugStringA( "Vulkan validation: " );
	OutputDebugStringA( message ? message : "" );
	OutputDebugStringA( "\n" );
#endif
}


static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT object_type, uint64_t object, size_t location,
	int32_t message_code, const char* layer_prefix, const char* message, void* user_data) {
	const char *severity;

	if ( flags & VK_DEBUG_REPORT_ERROR_BIT_EXT ) {
		severity = "error";
	} else if ( flags & VK_DEBUG_REPORT_WARNING_BIT_EXT ) {
		severity = "warning";
	} else if ( flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT ) {
		severity = "performance";
	} else {
		severity = layer_prefix ? layer_prefix : "message";
	}

	print_debug_message( "debug-report", severity, message );
	return VK_FALSE;
}


static VKAPI_ATTR VkBool32 VKAPI_CALL debug_utils_callback( VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageTypes, const VkDebugUtilsMessengerCallbackDataEXT *callbackData, void *userData )
{
	print_debug_message( "debug-utils", debug_utils_severity_string( messageSeverity ), callbackData ? callbackData->pMessage : NULL );
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
	if ( Q_stricmp( ext, VK_EXT_DEBUG_REPORT_EXTENSION_NAME ) == 0 )
		return qtrue;

	if ( Q_stricmp( ext, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME ) == 0 )
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
	const char* validation_layer_name = "VK_LAYER_KHRONOS_validation";
	const char* validation_layer_name2 = "VK_LAYER_LUNARG_standard_validation";
	const VkValidationFeatureEnableEXT validation_feature_enables[] = {
		VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
		VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT
	};
	VkValidationFeaturesEXT validation_features;
	qboolean enable_validation_features;
#endif
	VkInstanceCreateInfo desc;
	VkInstanceCreateFlags flags;
	VkExtensionProperties *extension_properties;
	VkResult res;
	const char **extension_names;
	uint32_t i, n, count, extension_count;
	VkApplicationInfo appInfo;

	flags = 0;
	count = 0;
	extension_count = 0;
#ifdef USE_VK_VALIDATION
	enable_validation_features = qfalse;
#endif
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

		if ( Q_stricmp( ext, VK_EXT_DEBUG_UTILS_EXTENSION_NAME ) == 0 ) {
			vk.debugUtils = qtrue;
			vk_instance_debug_utils = qtrue;
		}

		if ( Q_stricmp( ext, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME ) == 0 ) {
			vk.swapchainColorspace = qtrue;
			vk_instance_swapchain_colorspace = qtrue;
		}

#ifdef USE_VK_VALIDATION
		if ( Q_stricmp( ext, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME ) == 0 ) {
			enable_validation_features = qtrue;
		}
#endif

		ri.Printf(PRINT_DEVELOPER, "instance extension: %s\n", ext);
	}

	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pNext = NULL;
	appInfo.pApplicationName = NULL; // Q3_VERSION;
	appInfo.applicationVersion = 0x0;
	appInfo.pEngineName = NULL;
	appInfo.engineVersion = 0x0;
#ifdef _DEBUG
	appInfo.apiVersion = VK_API_VERSION_1_1;
#else
	appInfo.apiVersion = VK_API_VERSION_1_0;
#endif

	// create instance
	desc.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = flags;
	desc.pApplicationInfo = &appInfo;
	desc.enabledExtensionCount = extension_count;
	desc.ppEnabledExtensionNames = extension_names;

#ifdef USE_VK_VALIDATION
	if ( enable_validation_features ) {
		validation_features.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
		validation_features.pNext = NULL;
		validation_features.enabledValidationFeatureCount = ARRAY_LEN( validation_feature_enables );
		validation_features.pEnabledValidationFeatures = validation_feature_enables;
		validation_features.disabledValidationFeatureCount = 0;
		validation_features.pDisabledValidationFeatures = NULL;
		desc.pNext = &validation_features;
	}

	desc.enabledLayerCount = 1;
	desc.ppEnabledLayerNames = &validation_layer_name;

	res = qvkCreateInstance( &desc, NULL, &vk_instance );

	if ( res == VK_ERROR_LAYER_NOT_PRESENT ) {

		desc.enabledLayerCount = 1;
		desc.ppEnabledLayerNames = &validation_layer_name2;

		res = qvkCreateInstance( &desc, NULL, &vk_instance );

		if ( res == VK_ERROR_LAYER_NOT_PRESENT ) {

			ri.Printf( PRINT_WARNING, "...validation layer is not available\n" );

			// try without validation layer
			desc.enabledLayerCount = 0;
			desc.ppEnabledLayerNames = NULL;

			res = qvkCreateInstance( &desc, NULL, &vk_instance );
		}
	}
#else
	desc.enabledLayerCount = 0;
	desc.ppEnabledLayerNames = NULL;

	res = qvkCreateInstance( &desc, NULL, &vk_instance );
#endif

	ri.Free( (void*)extension_names );
	ri.Free( extension_properties );

	if ( res != VK_SUCCESS ) {
		ri.Error( ERR_FATAL, "Vulkan: instance creation failed with %s", vk_result_string( res ) );
	}
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


static VkFormat get_hdr_format( VkFormat base_format )
{
	int precision;

	if ( r_fbo->integer == 0 ) {
		return base_format;
	}

	if ( vk.hdrDisplayActive ) {
		return VK_FORMAT_R16G16B16A16_UNORM;
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
	if ( r_hdr && r_hdr->integer > 0 ) {
		return VK_FORMAT_R16G16B16A16_UNORM;
	}
	return base_format;
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

static qboolean vk_find_surface_format( const VkSurfaceFormatKHR *candidates, uint32_t count, VkFormat bgr, VkFormat rgb,
	VkColorSpaceKHR colorSpace, VkSurfaceFormatKHR *out )
{
	uint32_t i;

	for ( i = 0; i < count; i++ ) {
		if ( ( candidates[i].format == bgr || candidates[i].format == rgb ) && candidates[i].colorSpace == colorSpace ) {
			*out = candidates[i];
			return qtrue;
		}
	}

	return qfalse;
}


static qboolean vk_find_hdr10_surface_format( const VkSurfaceFormatKHR *candidates, uint32_t count, VkSurfaceFormatKHR *out )
{
	uint32_t i;
	const VkFormat hdr_formats[] = {
		VK_FORMAT_A2B10G10R10_UNORM_PACK32,
		VK_FORMAT_A2R10G10B10_UNORM_PACK32
	};

	for ( i = 0; i < ARRAY_LEN( hdr_formats ); i++ ) {
		uint32_t j;
		for ( j = 0; j < count; j++ ) {
			if ( candidates[j].format == hdr_formats[i] && candidates[j].colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT ) {
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
		return ( r_outputAllowExperimentalLinuxHDR && r_outputAllowExperimentalLinuxHDR->integer &&
			vk.displayOutput.linuxHdrExperimental && vk.displayOutput.explicitLinuxHdrProtocol ) ?
			qtrue : qfalse;
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
		if ( !vk_find_surface_format( candidates, format_count, base_bgr, base_rgb, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, &vk.base_format ) ) {
			vk.base_format = candidates[0];
		}

		if ( !vk_find_surface_format( candidates, format_count, ext_bgr, ext_rgb, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, &vk.present_format ) ) {
			vk.present_format = vk.base_format;
		}
	}

	vk_query_display_output();
	vk.outputBackend = ROUTPUT_BACKEND_SDR_SRGB;
	vk.hdrDisplayActive = qfalse;
	if ( vk_output_request_wants_hdr10() ) {
		if ( !r_fbo->integer ) {
			ri.Printf( PRINT_WARNING, "...native HDR presentation requires \\r_fbo 1, using SDR presentation\n" );
		} else if ( !vk.swapchainColorspace ) {
			ri.Printf( PRINT_WARNING, "...%s is not available, using SDR presentation\n", VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME );
		} else if ( format_count == 1 && candidates[0].format == VK_FORMAT_UNDEFINED ) {
			ri.Printf( PRINT_WARNING, "...surface reports arbitrary SDR format selection only, using SDR presentation\n" );
		} else if ( vk_find_hdr10_surface_format( candidates, format_count, &hdr10_format ) ) {
			vk.present_format = hdr10_format;
			vk.hdrDisplayActive = qtrue;
			vk.outputBackend = ROUTPUT_BACKEND_HDR10_PQ;
		} else {
			ri.Printf( PRINT_WARNING, "...HDR10 ST2084 surface format is not available, using SDR presentation\n" );
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
	vk.depth_format = get_depth_format( physical_device );

	vk.color_format = get_hdr_format( vk.base_format.format );

	vk.capture_format = VK_FORMAT_R8G8B8A8_UNORM;

	vk.bloom_format = vk.base_format.format;

	vk.blitEnabled = vk_blit_enabled( physical_device, vk.color_format, vk.capture_format );

	if ( !vk.blitEnabled )
	{
		vk.capture_format = vk.color_format;
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


static qboolean vk_create_device( VkPhysicalDevice physical_device, int device_index ) {

	VkPhysicalDeviceSynchronization2Features synchronization2_features;
	VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering_features;
#ifdef _DEBUG
	VkPhysicalDeviceTimelineSemaphoreFeatures timeline_semaphore;
	VkPhysicalDeviceVulkanMemoryModelFeatures memory_model;
	VkPhysicalDeviceBufferDeviceAddressFeatures devaddr_features;
	VkPhysicalDevice8BitStorageFeatures storage_8bit_features;
#endif

	ri.Printf( PRINT_ALL, "...selected physical device: %i\n", device_index );

	vk.wideLines = qfalse;
	vk.samplerAnisotropy = qfalse;
	vk.fragmentStores = qfalse;
	vk.dedicatedAllocation = qfalse;
	vk.debugMarkers = qfalse;
	vk.hdrMetadata = qfalse;
	vk.synchronization2 = qfalse;
	vk.dynamicRendering = qfalse;
	vk.depthStencilResolve = qfalse;
	vk.depthResolveMode = VK_RESOLVE_MODE_NONE;

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
		for (i = 0; i < queue_family_count; i++) {
			VkBool32 presentation_supported;
			VK_CHECK( qvkGetPhysicalDeviceSurfaceSupportKHR( physical_device, i, vk_surface, &presentation_supported ) );

			if (presentation_supported && (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
				vk.queue_family_index = i;
				vk.timestampValidBits = queue_families[i].timestampValidBits;
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
		const char *device_extension_list[16];
		uint32_t device_extension_count;
		const char *ext, *end;
		char *str;
		const float priority = 1.0;
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
		qboolean synchronization2Extension = qfalse;
		qboolean dynamicRenderingExtension = qfalse;
		qboolean createRenderPass2Extension = qfalse;
		qboolean depthStencilResolveExtension = qfalse;
		qboolean synchronization2Feature = qfalse;
		qboolean dynamicRenderingFeature = qfalse;
		qboolean enableDepthStencilResolve = qfalse;
		qboolean enableSynchronization2 = qfalse;
		qboolean enableDynamicRendering = qfalse;
		const void** pNextPtr;
#ifdef _DEBUG
		qboolean timelineSemaphore = qfalse;
		qboolean memoryModel = qfalse;
		qboolean devAddrFeat = qfalse;
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
			} else if ( strcmp( ext, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME ) == 0 ) {
				synchronization2Extension = qtrue;
			} else if ( strcmp( ext, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME ) == 0 ) {
				dynamicRenderingExtension = qtrue;
			} else if ( strcmp( ext, VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME ) == 0 ) {
				createRenderPass2Extension = qtrue;
			} else if ( strcmp( ext, VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME ) == 0 ) {
				depthStencilResolveExtension = qtrue;
#ifdef _DEBUG
			} else if ( strcmp( ext, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME ) == 0 ) {
				timelineSemaphore = qtrue;
			} else if ( strcmp( ext, VK_KHR_VULKAN_MEMORY_MODEL_EXTENSION_NAME ) == 0 ) {
				memoryModel = qtrue;
			} else if ( strcmp( ext, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME ) == 0 ) {
				devAddrFeat = qtrue;
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

		vk_query_modern_device_features( physical_device,
			synchronization2Extension, dynamicRenderingExtension,
			&synchronization2Feature, &dynamicRenderingFeature );
		enableSynchronization2 = synchronization2Extension && synchronization2Feature;
		enableDynamicRendering = dynamicRenderingExtension && dynamicRenderingFeature;
		enableDepthStencilResolve = createRenderPass2Extension && depthStencilResolveExtension &&
			vk_query_depth_stencil_resolve( physical_device, &vk.depthResolveMode );

		if ( synchronization2Extension && !synchronization2Feature ) {
			ri.Printf( PRINT_DEVELOPER, "...%s advertised but feature bit is unavailable\n", VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME );
		}
		if ( dynamicRenderingExtension && !dynamicRenderingFeature ) {
			ri.Printf( PRINT_DEVELOPER, "...%s advertised but feature bit is unavailable\n", VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME );
		}

		device_extension_count = 0;

		if ( !swapchainSupported ) {
			ri.Printf( PRINT_ERROR, "...required device extension is not available: %s\n", VK_KHR_SWAPCHAIN_EXTENSION_NAME );
			return qfalse;
		}

		if ( !memoryRequirements2 )
			dedicatedAllocation = qfalse;
		else
			vk.dedicatedAllocation = dedicatedAllocation;

#ifndef USE_DEDICATED_ALLOCATION
		vk.dedicatedAllocation = qfalse;
#endif

		device_extension_list[ device_extension_count++ ] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
		if ( portabilitySubset ) {
			device_extension_list[ device_extension_count++ ] = VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME;
		}

		if ( vk.dedicatedAllocation ) {
			device_extension_list[ device_extension_count++ ] = VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME;
			device_extension_list[ device_extension_count++ ] = VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME;
		}

		if ( debugMarker ) {
			device_extension_list[ device_extension_count++ ] = VK_EXT_DEBUG_MARKER_EXTENSION_NAME;
		}

		if ( hdrMetadata ) {
			device_extension_list[ device_extension_count++ ] = VK_EXT_HDR_METADATA_EXTENSION_NAME;
		}

		if ( enableSynchronization2 ) {
			device_extension_list[ device_extension_count++ ] = VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME;
		}

		if ( enableDynamicRendering ) {
			device_extension_list[ device_extension_count++ ] = VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME;
		}

		if ( enableDepthStencilResolve ) {
			device_extension_list[ device_extension_count++ ] = VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME;
			device_extension_list[ device_extension_count++ ] = VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME;
		}
#ifdef _DEBUG
		if ( timelineSemaphore ) {
			device_extension_list[ device_extension_count++ ] = VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME;
		}

		if ( memoryModel ) {
			device_extension_list[ device_extension_count++ ] = VK_KHR_VULKAN_MEMORY_MODEL_EXTENSION_NAME;
		}

		if ( devAddrFeat ) {
			device_extension_list[ device_extension_count++ ] = VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME;
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
		queue_desc.queueCount = 1;
		queue_desc.pQueuePriorities = &priority;

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

		if ( enableSynchronization2 ) {
			*pNextPtr = &synchronization2_features;
			synchronization2_features.pNext = NULL;
			synchronization2_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
			synchronization2_features.synchronization2 = VK_TRUE;
			pNextPtr = (const void **)&synchronization2_features.pNext;
		}

		if ( enableDynamicRendering ) {
			*pNextPtr = &dynamic_rendering_features;
			dynamic_rendering_features.pNext = NULL;
			dynamic_rendering_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
			dynamic_rendering_features.dynamicRendering = VK_TRUE;
			pNextPtr = (const void **)&dynamic_rendering_features.pNext;
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

		if ( devAddrFeat ) {
			*pNextPtr = &devaddr_features;
			devaddr_features.pNext = NULL;
			devaddr_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
			devaddr_features.bufferDeviceAddress = VK_TRUE;
			devaddr_features.bufferDeviceAddressCaptureReplay = VK_FALSE;
			devaddr_features.bufferDeviceAddressMultiDevice = VK_FALSE;
			pNextPtr = (const void **)&devaddr_features.pNext;
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

		vk.debugMarkers = debugMarker;
		vk.hdrMetadata = hdrMetadata;
		vk.synchronization2 = enableSynchronization2;
		vk.dynamicRendering = enableDynamicRendering;
		vk.depthStencilResolve = enableDepthStencilResolve;
	}

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
	if ( vk_debug_messenger ) {
		if ( qvkDestroyDebugUtilsMessengerEXT != NULL ) {
			qvkDestroyDebugUtilsMessengerEXT( vk_instance, vk_debug_messenger, NULL );
		}
		vk_debug_messenger = VK_NULL_HANDLE;
	}

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
	vk_instance_debug_utils = qfalse;
}


static void init_vulkan_library( void )
{
	VkPhysicalDeviceProperties props;
	VkPhysicalDevice *physical_devices;
	uint32_t device_count;
	int device_index, i;
	VkResult res;

	Com_Memset( &vk, 0, sizeof( vk ) );
	vk_current_render_pass = VK_NULL_HANDLE;
	vk_current_render_pass_label[0] = '\0';

	if ( vk_instance == VK_NULL_HANDLE ) {

		// force cleanup
		vk_destroy_instance();

		// Get functions that do not depend on VkInstance (vk_instance == nullptr at this point).
		INIT_INSTANCE_FUNCTION( vkCreateInstance )
		INIT_INSTANCE_FUNCTION( vkEnumerateInstanceExtensionProperties )

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
		INIT_INSTANCE_FUNCTION_EXT( vkCreateDebugUtilsMessengerEXT )
		INIT_INSTANCE_FUNCTION_EXT( vkDestroyDebugUtilsMessengerEXT )
		INIT_INSTANCE_FUNCTION_EXT( vkCreateDebugReportCallbackEXT )
		INIT_INSTANCE_FUNCTION_EXT( vkDestroyDebugReportCallbackEXT )

		// Prefer debug-utils diagnostics; keep debug-report as a loader/runtime fallback.
		if ( qvkCreateDebugUtilsMessengerEXT && qvkDestroyDebugUtilsMessengerEXT ) {
			VkDebugUtilsMessengerCreateInfoEXT desc;
			desc.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			desc.pNext = NULL;
			desc.flags = 0;
			desc.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			desc.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			desc.pfnUserCallback = &debug_utils_callback;
			desc.pUserData = NULL;

			VK_CHECK( qvkCreateDebugUtilsMessengerEXT( vk_instance, &desc, NULL, &vk_debug_messenger ) );
		} else if ( qvkCreateDebugReportCallbackEXT && qvkDestroyDebugReportCallbackEXT ) {
			VkDebugReportCallbackCreateInfoEXT desc;
			desc.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
			desc.pNext = NULL;
			desc.flags = VK_DEBUG_REPORT_WARNING_BIT_EXT |
				VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT |
				VK_DEBUG_REPORT_ERROR_BIT_EXT;
			desc.pfnCallback = &debug_callback;
			desc.pUserData = NULL;

			VK_CHECK( qvkCreateDebugReportCallbackEXT( vk_instance, &desc, NULL, &vk_debug_callback ) );
		}
#endif

		// create surface
		if ( !ri.VK_CreateSurface( vk_instance, &vk_surface ) ) {
			ri.Error( ERR_FATAL, "Error creating Vulkan surface" );
			return;
		}
	} // vk_instance == VK_NULL_HANDLE

	vk.debugUtils = vk_instance_debug_utils;
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
	INIT_DEVICE_FUNCTION(vkCmdCopyImageToBuffer)
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
	INIT_DEVICE_FUNCTION(vkCreateQueryPool)
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
	INIT_DEVICE_FUNCTION(vkDestroyQueryPool)
	INIT_DEVICE_FUNCTION(vkDestroyRenderPass)
	INIT_DEVICE_FUNCTION(vkDestroySampler)
	INIT_DEVICE_FUNCTION(vkDestroySemaphore)
	INIT_DEVICE_FUNCTION(vkDestroyShaderModule)
	INIT_DEVICE_FUNCTION(vkDeviceWaitIdle)
	INIT_DEVICE_FUNCTION(vkEndCommandBuffer)
	INIT_DEVICE_FUNCTION(vkFlushMappedMemoryRanges)
	INIT_DEVICE_FUNCTION(vkFreeDescriptorSets)
	INIT_DEVICE_FUNCTION(vkFreeMemory)
	INIT_DEVICE_FUNCTION(vkGetBufferMemoryRequirements)
	INIT_DEVICE_FUNCTION(vkGetDeviceQueue)
	INIT_DEVICE_FUNCTION(vkGetImageMemoryRequirements)
	INIT_DEVICE_FUNCTION(vkGetImageSubresourceLayout)
	INIT_DEVICE_FUNCTION(vkGetPipelineCacheData)
	INIT_DEVICE_FUNCTION(vkGetQueryPoolResults)
	INIT_DEVICE_FUNCTION(vkInvalidateMappedMemoryRanges)
	INIT_DEVICE_FUNCTION(vkMapMemory)
	INIT_DEVICE_FUNCTION(vkQueueSubmit)
	INIT_DEVICE_FUNCTION(vkQueueWaitIdle)
	INIT_DEVICE_FUNCTION(vkResetCommandPool)
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

	if ( vk.dedicatedAllocation ) {
		INIT_DEVICE_FUNCTION_EXT(vkGetBufferMemoryRequirements2KHR);
		INIT_DEVICE_FUNCTION_EXT(vkGetImageMemoryRequirements2KHR);
		if ( !qvkGetBufferMemoryRequirements2KHR || !qvkGetImageMemoryRequirements2KHR ) {
			vk.dedicatedAllocation = qfalse;
		}
	}

	if ( vk.debugUtils ) {
		INIT_DEVICE_FUNCTION_EXT(vkSetDebugUtilsObjectNameEXT)
		INIT_DEVICE_FUNCTION_EXT(vkCmdBeginDebugUtilsLabelEXT)
		INIT_DEVICE_FUNCTION_EXT(vkCmdEndDebugUtilsLabelEXT)
		INIT_DEVICE_FUNCTION_EXT(vkCmdInsertDebugUtilsLabelEXT)
		if ( !qvkSetDebugUtilsObjectNameEXT ) {
			vk.debugUtils = qfalse;
		}
	}

	if ( vk.debugMarkers ) {
		INIT_DEVICE_FUNCTION_EXT(vkDebugMarkerSetObjectNameEXT)
	}

	if ( vk.hdrMetadata ) {
		INIT_DEVICE_FUNCTION_EXT(vkSetHdrMetadataEXT)
		if ( !qvkSetHdrMetadataEXT ) {
			vk.hdrMetadata = qfalse;
		}
	}

	if ( vk.synchronization2 ) {
		INIT_DEVICE_FUNCTION_EXT(vkCmdPipelineBarrier2KHR)
		if ( !qvkCmdPipelineBarrier2KHR ) {
			vk.synchronization2 = qfalse;
		}
	}

	if ( vk.dynamicRendering ) {
		INIT_DEVICE_FUNCTION_EXT(vkCmdBeginRenderingKHR)
		INIT_DEVICE_FUNCTION_EXT(vkCmdEndRenderingKHR)
		if ( !qvkCmdBeginRenderingKHR || !qvkCmdEndRenderingKHR ) {
			vk.dynamicRendering = qfalse;
		}
	}

	if ( vk.depthStencilResolve ) {
		INIT_DEVICE_FUNCTION_EXT(vkCreateRenderPass2KHR)
		if ( !qvkCreateRenderPass2KHR ) {
			vk.depthStencilResolve = qfalse;
		}
	}
}

#undef INIT_INSTANCE_FUNCTION
#undef INIT_INSTANCE_FUNCTION_EXT
#undef INIT_DEVICE_FUNCTION
#undef INIT_DEVICE_FUNCTION_EXT

static void deinit_instance_functions( void )
{
	qvkCreateInstance = NULL;
	qvkEnumerateInstanceExtensionProperties = NULL;

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
	qvkCreateDebugUtilsMessengerEXT = NULL;
	qvkDestroyDebugUtilsMessengerEXT = NULL;
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
	qvkCmdCopyImageToBuffer						= NULL;
	qvkCmdDraw									= NULL;
	qvkCmdDrawIndexed							= NULL;
	qvkCmdEndRenderPass							= NULL;
	qvkCmdBeginRenderingKHR						= NULL;
	qvkCmdEndRenderingKHR						= NULL;
	qvkCmdNextSubpass							= NULL;
	qvkCmdPipelineBarrier						= NULL;
	qvkCmdPipelineBarrier2KHR					= NULL;
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
	qvkCreateQueryPool							= NULL;
	qvkCreateRenderPass							= NULL;
	qvkCreateRenderPass2KHR						= NULL;
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
	qvkDestroyQueryPool							= NULL;
	qvkDestroyRenderPass						= NULL;
	qvkDestroySampler							= NULL;
	qvkDestroySemaphore							= NULL;
	qvkDestroyShaderModule						= NULL;
	qvkDeviceWaitIdle							= NULL;
	qvkEndCommandBuffer							= NULL;
	qvkFlushMappedMemoryRanges					= NULL;
	qvkFreeDescriptorSets						= NULL;
	qvkFreeMemory								= NULL;
	qvkGetBufferMemoryRequirements				= NULL;
	qvkGetDeviceQueue							= NULL;
	qvkGetImageMemoryRequirements				= NULL;
	qvkGetImageSubresourceLayout				= NULL;
	qvkGetPipelineCacheData						= NULL;
	qvkGetQueryPoolResults						= NULL;
	qvkInvalidateMappedMemoryRanges				= NULL;
	qvkMapMemory								= NULL;
	qvkQueueSubmit								= NULL;
	qvkQueueWaitIdle							= NULL;
	qvkResetCommandPool							= NULL;
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
	qvkSetHdrMetadataEXT						= NULL;

	qvkGetBufferMemoryRequirements2KHR			= NULL;
	qvkGetImageMemoryRequirements2KHR			= NULL;

	qvkSetDebugUtilsObjectNameEXT				= NULL;
	qvkCmdBeginDebugUtilsLabelEXT				= NULL;
	qvkCmdEndDebugUtilsLabelEXT					= NULL;
	qvkCmdInsertDebugUtilsLabelEXT				= NULL;
	qvkDebugMarkerSetObjectNameEXT				= NULL;
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


static VkShaderModule SHADER_MODULE_OPTIONAL( const uint8_t *bytes, const int count,
	const char *description )
{
	VkShaderModuleCreateInfo desc;
	VkShaderModule module = VK_NULL_HANDLE;
	VkResult result;

	if ( !bytes || count <= 0 || count % 4 != 0 ) {
		ri.Printf( PRINT_WARNING,
			"WARNING: Vulkan optional %s shader has an invalid SPIR-V buffer; feature disabled\n",
			description );
		return VK_NULL_HANDLE;
	}

	desc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.codeSize = count;
	desc.pCode = (const uint32_t *)bytes;
	result = qvkCreateShaderModule( vk.device, &desc, NULL, &module );
	if ( result != VK_SUCCESS ) {
		ri.Printf( PRINT_WARNING,
			"WARNING: Vulkan optional %s shader module failed (%s); feature disabled\n",
			description, vk_result_string( result ) );
		return VK_NULL_HANDLE;
	}

	return module;
}


static void vk_update_descriptor_sets( uint32_t descriptor_write_count, const VkWriteDescriptorSet *descriptor_writes )
{
	qvkUpdateDescriptorSets( vk.device, descriptor_write_count, descriptor_writes, 0, NULL );
	vk.stats.descriptor_writes += descriptor_write_count;
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

	vk_update_descriptor_sets( 1, &desc );
}


static VkSampler vk_find_sampler( const Vk_Sampler_Def *def ) {
	VkSamplerAddressMode address_mode;
	VkSamplerCreateInfo desc;
	VkSampler sampler;
	VkFilter mag_filter;
	VkFilter min_filter;
	VkSamplerMipmapMode mipmap_mode;
	float requestedAnisotropy;
	float maxLod;
	int i;

	// Look for sampler among existing samplers.
	for ( i = 0; i < vk.samplers.count; i++ ) {
		const Vk_Sampler_Def *cur_def = &vk.samplers.def[i];
		if ( memcmp( cur_def, def, sizeof( *def ) ) == 0 ) {
			return vk.samplers.handle[i];
		}
	}

	// Create new sampler.
	if ( vk.samplers.count >= MAX_VK_SAMPLERS ) {
		ri.Error( ERR_DROP, "vk_find_sampler: MAX_VK_SAMPLERS hit\n" );
		// return VK_NULL_HANDLE;
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

	desc.anisotropyEnable = VK_FALSE;
	desc.maxAnisotropy = 1.0f;
	if ( !def->noAnisotropy && mipmap_mode == VK_SAMPLER_MIPMAP_MODE_LINEAR &&
		mag_filter != VK_FILTER_NEAREST &&
		r_ext_texture_filter_anisotropic && r_ext_texture_filter_anisotropic->integer &&
		vk.samplerAnisotropy ) {
		requestedAnisotropy = Com_Clamp( 1.0f, vk.maxAnisotropy,
			r_ext_max_anisotropy ? (float)r_ext_max_anisotropy->integer : 1.0f );
		if ( requestedAnisotropy > 1.0f ) {
			desc.anisotropyEnable = VK_TRUE;
			desc.maxAnisotropy = requestedAnisotropy;
		}
	}

	desc.compareEnable = VK_FALSE;
	desc.compareOp = VK_COMPARE_OP_ALWAYS;
	desc.minLod = 0.0f;
	desc.maxLod = (maxLod == vk.maxLod) ? VK_LOD_CLAMP_NONE : maxLod;
	desc.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	desc.unnormalizedCoordinates = VK_FALSE;

	VK_CHECK( qvkCreateSampler( vk.device, &desc, NULL, &sampler ) );

	SET_OBJECT_NAME( sampler, va( "image sampler %i", vk.samplers.count ), VK_DEBUG_REPORT_OBJECT_TYPE_SAMPLER_EXT );

	vk.samplers.def[ vk.samplers.count ] = *def;
	vk.samplers.handle[ vk.samplers.count ] = sampler;
	vk.samplers.count++;

	return sampler;
}


void vk_destroy_samplers( void )
{
	int i;

	for ( i = 0; i < vk.samplers.count; i++ ) {
		qvkDestroySampler( vk.device, vk.samplers.handle[i], NULL );
		memset( &vk.samplers.def[i], 0x0, sizeof( vk.samplers.def[i] ) );
		vk.samplers.handle[i] = VK_NULL_HANDLE;
	}

	vk.samplers.count = 0;
}


void vk_update_attachment_descriptors( void ) {

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

		vk_update_descriptor_sets( 1, &desc );

		if ( vk.liquidSnapshot.source_descriptor ) {
			/* The main scene's normal output sampler may intentionally be nearest
			 * filtered. Downsampling the private liquid snapshot must be linear or
			 * a half-resolution target becomes a shimmering pixel grid. */
			sd.gl_mag_filter = sd.gl_min_filter = GL_LINEAR;
			sd.max_lod_1_0 = qfalse;
			sd.noAnisotropy = qtrue;
			info.sampler = vk_find_sampler( &sd );
			info.imageView = vk.color_image_view;
			desc.dstSet = vk.liquidSnapshot.source_descriptor;
			vk_update_descriptor_sets( 1, &desc );
		}

		if ( vk.motion_blur_image_view && vk.motion_blur_descriptor ) {
			sd.gl_mag_filter = sd.gl_min_filter = vk.blitFilter;
			sd.max_lod_1_0 = qtrue;
			info.sampler = vk_find_sampler( &sd );
			info.imageView = vk.motion_blur_image_view;
			desc.dstSet = vk.motion_blur_descriptor;
			vk_update_descriptor_sets( 1, &desc );
		}

		// screenmap
		sd.gl_mag_filter = sd.gl_min_filter = GL_LINEAR;
		sd.max_lod_1_0 = qfalse;
		sd.noAnisotropy = qtrue;

		info.sampler = vk_find_sampler( &sd );

		info.imageView = vk.screenMap.color_image_view;
		desc.dstSet = vk.screenMap.color_descriptor;

		vk_update_descriptor_sets( 1, &desc );

		if ( vk.liquidSnapshot.color_image_view &&
			vk.liquidSnapshot.color_descriptor ) {
			info.imageView = vk.liquidSnapshot.color_image_view;
			desc.dstSet = vk.liquidSnapshot.color_descriptor;
			vk_update_descriptor_sets( 1, &desc );
		}

		// bloom images
		if ( r_bloom->integer )
		{
			uint32_t i;
			for ( i = 0; i < ARRAY_LEN( vk.bloom_image_descriptor ); i++ )
			{
				info.imageView = vk.bloom_image_view[i];
				desc.dstSet = vk.bloom_image_descriptor[i];

				vk_update_descriptor_sets( 1, &desc );
			}
		}
	}

	if ( vk.depth_fade_image_view && vk.depth_fade_descriptor )
	{
		VkDescriptorImageInfo info;
		VkWriteDescriptorSet desc;
		Vk_Sampler_Def sd;

		Com_Memset( &sd, 0, sizeof( sd ) );
		sd.gl_mag_filter = sd.gl_min_filter = GL_NEAREST;
		sd.address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sd.max_lod_1_0 = qtrue;
		sd.noAnisotropy = qtrue;

		info.sampler = vk_find_sampler( &sd );
		info.imageView = vk.depth_fade_image_view;
		info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		desc.dstSet = vk.depth_fade_descriptor;
		desc.dstBinding = 0;
		desc.dstArrayElement = 0;
		desc.descriptorCount = 1;
		desc.pNext = NULL;
		desc.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		desc.pImageInfo = &info;
		desc.pBufferInfo = NULL;
		desc.pTexelBufferView = NULL;

		vk_update_descriptor_sets( 1, &desc );
	}

	if ( vk.dlight_shadow_image_view && vk.dlight_shadow_descriptor )
	{
		VkDescriptorImageInfo info;
		VkWriteDescriptorSet desc;
		Vk_Sampler_Def sd;

		Com_Memset( &sd, 0, sizeof( sd ) );
		sd.gl_mag_filter = sd.gl_min_filter = GL_NEAREST;
		sd.address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sd.max_lod_1_0 = qtrue;
		sd.noAnisotropy = qtrue;

		info.sampler = vk_find_sampler( &sd );
		info.imageView = vk.dlight_shadow_image_view;
		info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		desc.dstSet = vk.dlight_shadow_descriptor;
		desc.dstBinding = 0;
		desc.dstArrayElement = 0;
		desc.descriptorCount = 1;
		desc.pNext = NULL;
		desc.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		desc.pImageInfo = &info;
		desc.pBufferInfo = NULL;
		desc.pTexelBufferView = NULL;

		vk_update_descriptor_sets( 1, &desc );
	}

	if ( vk.spot_shadow_image_view && vk.spot_shadow_descriptor )
	{
		VkDescriptorImageInfo info;
		VkWriteDescriptorSet desc;
		Vk_Sampler_Def sd;

		Com_Memset( &sd, 0, sizeof( sd ) );
		sd.gl_mag_filter = sd.gl_min_filter = GL_NEAREST;
		sd.address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sd.max_lod_1_0 = qtrue;
		sd.noAnisotropy = qtrue;

		info.sampler = vk_find_sampler( &sd );
		info.imageView = vk.spot_shadow_image_view;
		info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		desc.dstSet = vk.spot_shadow_descriptor;
		desc.dstBinding = 0;
		desc.dstArrayElement = 0;
		desc.descriptorCount = 1;
		desc.pNext = NULL;
		desc.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		desc.pImageInfo = &info;
		desc.pBufferInfo = NULL;
		desc.pTexelBufferView = NULL;

		vk_update_descriptor_sets( 1, &desc );
	}

	if ( vk.csm_shadow_image_view && vk.csm_shadow_descriptor )
	{
		VkDescriptorImageInfo info;
		VkWriteDescriptorSet desc;
		Vk_Sampler_Def sd;

		Com_Memset( &sd, 0, sizeof( sd ) );
		sd.gl_mag_filter = sd.gl_min_filter = GL_NEAREST;
		sd.address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sd.max_lod_1_0 = qtrue;
		sd.noAnisotropy = qtrue;

		info.sampler = vk_find_sampler( &sd );
		info.imageView = vk.csm_shadow_image_view;
		info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		desc.dstSet = vk.csm_shadow_descriptor;
		desc.dstBinding = 0;
		desc.dstArrayElement = 0;
		desc.descriptorCount = 1;
		desc.pNext = NULL;
		desc.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		desc.pImageInfo = &info;
		desc.pBufferInfo = NULL;
		desc.pTexelBufferView = NULL;

		vk_update_descriptor_sets( 1, &desc );
	}
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
	alloc.pSetLayouts = &vk.set_layout_storage;

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

	vk_update_descriptor_sets( 1, &desc );

	// allocated and update descriptor set
	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ )
	{
		alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		alloc.pNext = NULL;
		alloc.descriptorPool = vk.descriptor_pool;
		alloc.descriptorSetCount = 1;
		alloc.pSetLayouts = &vk.set_layout_uniform;

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
		alloc.pSetLayouts = &vk.set_layout_sampler;

		VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc, &vk.color_descriptor ) );
		if ( vk.motion_blur_image_view ) {
			VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc, &vk.motion_blur_descriptor ) );
			SET_OBJECT_NAME( vk.motion_blur_descriptor, "motion blur scratch descriptor", VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT );
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
			SET_OBJECT_NAME( vk.liquidSnapshot.color_descriptor, "liquid snapshot descriptor",
				VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT );
		}

		vk_update_attachment_descriptors();
	}

	if ( vk.depth_fade_image_view )
	{
		alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		alloc.pNext = NULL;
		alloc.descriptorPool = vk.descriptor_pool;
		alloc.descriptorSetCount = 1;
		alloc.pSetLayouts = &vk.set_layout_sampler;

		VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc, &vk.depth_fade_descriptor ) );
		SET_OBJECT_NAME( vk.depth_fade_descriptor, "depth fade descriptor", VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT );

		vk_update_attachment_descriptors();
	}

	if ( vk.dlight_shadow_image_view )
	{
		alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		alloc.pNext = NULL;
		alloc.descriptorPool = vk.descriptor_pool;
		alloc.descriptorSetCount = 1;
		alloc.pSetLayouts = &vk.set_layout_sampler;

		VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc, &vk.dlight_shadow_descriptor ) );
		SET_OBJECT_NAME( vk.dlight_shadow_descriptor, "dlight shadow atlas descriptor", VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT );

		vk_update_attachment_descriptors();
	}

	if ( vk.spot_shadow_image_view )
	{
		alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		alloc.pNext = NULL;
		alloc.descriptorPool = vk.descriptor_pool;
		alloc.descriptorSetCount = 1;
		alloc.pSetLayouts = &vk.set_layout_sampler;

		VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc, &vk.spot_shadow_descriptor ) );
		SET_OBJECT_NAME( vk.spot_shadow_descriptor, "spot shadow atlas descriptor", VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT );

		vk_update_attachment_descriptors();
	}

	if ( vk.csm_shadow_image_view )
	{
		alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		alloc.pNext = NULL;
		alloc.descriptorPool = vk.descriptor_pool;
		alloc.descriptorSetCount = 1;
		alloc.pSetLayouts = &vk.set_layout_sampler;

		VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc, &vk.csm_shadow_descriptor ) );
		SET_OBJECT_NAME( vk.csm_shadow_descriptor, "csm shadow atlas descriptor", VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT );

		vk_update_attachment_descriptors();
	}
}


static void vk_release_geometry_buffers( void )
{
	int i;

	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		qvkDestroyBuffer( vk.device, vk.tess[i].vertex_buffer, NULL );
		vk.tess[i].vertex_buffer = VK_NULL_HANDLE;
	}

	vk_free_memory_allocation( &vk.geometry_buffer_allocation );
}


static void vk_create_geometry_buffers( VkDeviceSize size )
{
	VkMemoryRequirements vb_memory_requirements;
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

	vk_allocate_memory( &vk.geometry_buffer_allocation, vb_memory_requirements.size * NUM_COMMAND_BUFFERS, memory_type,
		VK_MEMORY_CATEGORY_GEOMETRY, "geometry buffer memory", NULL );
	VK_CHECK( qvkMapMemory( vk.device, vk.geometry_buffer_allocation.memory, 0, VK_WHOLE_SIZE, 0, &data ) );

	vertex_buffer_offset = 0;

	for ( i = 0 ; i < NUM_COMMAND_BUFFERS; i++ ) {
		qvkBindBufferMemory( vk.device, vk.tess[i].vertex_buffer, vk.geometry_buffer_allocation.memory, vertex_buffer_offset );
		vk.tess[i].vertex_buffer_ptr = (byte*)data + vertex_buffer_offset;
		vk.tess[i].vertex_buffer_offset = 0;
		vertex_buffer_offset += vb_memory_requirements.size;

		SET_OBJECT_NAME( vk.tess[i].vertex_buffer, va( "geometry buffer %i", i ), VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT );
	}

	vk.geometry_buffer_size = vb_memory_requirements.size;

	vk_reset_transient_stats();
}


static void vk_create_storage_buffer( uint32_t size )
{
	VkMemoryRequirements memory_requirements;
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

	vk_allocate_memory( &vk.storage.allocation, memory_requirements.size, memory_type,
		VK_MEMORY_CATEGORY_STORAGE, "storage buffer memory", NULL );
	VK_CHECK( qvkMapMemory( vk.device, vk.storage.allocation.memory, 0, VK_WHOLE_SIZE, 0, (void**)&vk.storage.buffer_ptr ) );

	Com_Memset( vk.storage.buffer_ptr, 0, memory_requirements.size );

	qvkBindBufferMemory( vk.device, vk.storage.buffer, vk.storage.allocation.memory, 0 );

	SET_OBJECT_NAME( vk.storage.buffer, "storage buffer", VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT );
	SET_OBJECT_NAME( vk.storage.descriptor, "storage buffer", VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT );
}


#ifdef USE_VBO
void vk_release_vbo( void )
{
	if ( vk.vbo.vertex_buffer )
		qvkDestroyBuffer( vk.device, vk.vbo.vertex_buffer, NULL );
	vk.vbo.vertex_buffer = VK_NULL_HANDLE;

	vk_free_memory_allocation( &vk.vbo.allocation );
}


qboolean vk_alloc_vbo( const byte *vbo_data, int vbo_size )
{
	VkMemoryRequirements vb_mem_reqs;
	VkBufferCreateInfo desc;
	VkDeviceSize vertex_buffer_offset;
	VkDeviceSize allocationSize;
	uint32_t memory_type_bits;
	VkCommandBuffer command_buffer;
	VkBufferCopy copyRegion[1];
	VkDeviceSize uploadDone;

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

	// memory requirements
	qvkGetBufferMemoryRequirements( vk.device, vk.vbo.vertex_buffer, &vb_mem_reqs );
	vertex_buffer_offset = 0;
	allocationSize = vertex_buffer_offset + vb_mem_reqs.size;
	memory_type_bits = vb_mem_reqs.memoryTypeBits;

	vk_allocate_memory( &vk.vbo.allocation, allocationSize,
		find_memory_type( memory_type_bits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ),
		VK_MEMORY_CATEGORY_STATIC_VBO, "static VBO memory", NULL );
	qvkBindBufferMemory( vk.device, vk.vbo.vertex_buffer, vk.vbo.allocation.memory, vertex_buffer_offset );

	// staging buffers

#ifdef USE_UPLOAD_QUEUE
	vk_flush_staging_buffer( qfalse );
#endif
	// utilize existing staging buffer
	uploadDone = 0;
	while ( uploadDone < vbo_size ) {
		VkDeviceSize uploadSize = vk.staging_buffer.size;
		if ( uploadDone + uploadSize > vbo_size ) {
			uploadSize = vbo_size - uploadDone;
		}
		memcpy(vk.staging_buffer.ptr + 0, vbo_data + uploadDone, uploadSize);
		command_buffer = begin_command_buffer();
		copyRegion[0].srcOffset = 0;
		copyRegion[0].dstOffset = uploadDone;
		copyRegion[0].size = uploadSize;
		qvkCmdCopyBuffer( command_buffer, vk.staging_buffer.handle, vk.vbo.vertex_buffer, 1, &copyRegion[0] );
		end_command_buffer( command_buffer, __func__ );
		uploadDone += uploadSize;
	}

	SET_OBJECT_NAME( vk.vbo.vertex_buffer, "static VBO", VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT );

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

	if ( r_liquid && r_liquid->integer ) {
		vk.modules.liquid_vs = SHADER_MODULE( liquid_vert_spv );
		vk.modules.liquid_fs = SHADER_MODULE( liquid_frag_spv );
		vk.modules.liquid_copy_fs = SHADER_MODULE( liquid_copy_frag_spv );

		SET_OBJECT_NAME( vk.modules.liquid_vs, "liquid effect vertex module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
		SET_OBJECT_NAME( vk.modules.liquid_fs, "liquid effect fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
		SET_OBJECT_NAME( vk.modules.liquid_copy_fs, "liquid scene-copy fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
	}

	vk.modules.bloom_fs = SHADER_MODULE( bloom_frag_spv );
	vk.modules.blur_fs = SHADER_MODULE( blur_frag_spv );
	vk.modules.blend_fs = SHADER_MODULE( blend_frag_spv );
	vk.modules.motion_blur_fs = SHADER_MODULE( motion_blur_frag_spv );
	vk.modules.world_outline_fs = SHADER_MODULE( world_outline_frag_spv );
	if ( r_globalFog && r_globalFog->integer ) {
		vk.modules.global_fog_fs = SHADER_MODULE_OPTIONAL( global_fog_frag_spv,
			sizeof( global_fog_frag_spv ), "global fog" );
	}

	SET_OBJECT_NAME( vk.modules.bloom_fs, "bloom extraction fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
	SET_OBJECT_NAME( vk.modules.blur_fs, "gaussian blur fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
	SET_OBJECT_NAME( vk.modules.blend_fs, "final bloom blend fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
	SET_OBJECT_NAME( vk.modules.motion_blur_fs, "motion blur fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
	SET_OBJECT_NAME( vk.modules.world_outline_fs, "world cel depth-outline fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
	if ( vk.modules.global_fog_fs != VK_NULL_HANDLE ) {
		SET_OBJECT_NAME( vk.modules.global_fog_fs, "global fog fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
	}

	vk.modules.gamma_fs = SHADER_MODULE( gamma_frag_spv );
	vk.modules.gamma_vs = SHADER_MODULE( gamma_vert_spv );

	SET_OBJECT_NAME( vk.modules.gamma_fs, "gamma post-processing fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
	SET_OBJECT_NAME( vk.modules.gamma_vs, "gamma post-processing vertex module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
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
		def.depth_fade = ( ( ( ( r_dlightShadows && r_dlightShadows->integer ) ||
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
		def.state_bits = GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO | GLS_DEPTHFUNC_EQUAL;
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

	// Enhanced liquids add a refraction underlay and bounded material sheen.
	if ( r_liquid && r_liquid->integer ) {
		int i, j, k;

		Com_Memset( &def, 0, sizeof( def ) );
		def.shader_type = TYPE_LIQUID;
		def.state_bits = GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
		for ( i = 0; i < 3; i++ ) {
			def.face_culling = i;
			for ( j = 0; j < 2; j++ ) {
				def.polygon_offset = j ? qtrue : qfalse;
				for ( k = 0; k < 2; k++ ) {
					def.mirror = k ? qtrue : qfalse;
					vk.liquid_pipelines[i][j][k] = vk_find_pipeline_ext( 0, &def, qfalse );
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

void vk_create_blur_pipeline( uint32_t index, uint32_t width, uint32_t height, qboolean horizontal_pass );

static int vk_color_grade_mode( void )
{
	int mode;

	if ( !r_hdr || r_hdr->integer <= 0 || !r_colorGrade ) {
		return 0;
	}
	mode = r_colorGrade->integer;
	if ( mode < 0 ) {
		return 0;
	}
	if ( mode > 3 ) {
		return 3;
	}
	return mode;
}

static qboolean vk_color_grade_uses_lgg( int mode )
{
	return ( mode == 1 || mode == 3 ) ? qtrue : qfalse;
}

static qboolean vk_color_grade_uses_lut( int mode )
{
	return ( mode == 2 || mode == 3 ) ? qtrue : qfalse;
}

static void vk_parse_vec3_cvar( const cvar_t *cvar, float fallback0, float fallback1,
	float fallback2, float minValue, float maxValue, float out[3] )
{
	float values[3];

	values[0] = fallback0;
	values[1] = fallback1;
	values[2] = fallback2;
	if ( cvar && cvar->string && cvar->string[0] ) {
		(void)sscanf( cvar->string, "%f %f %f", &values[0], &values[1], &values[2] );
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
		x = -0.2661239e9f / t3 - 0.2343580e6f / t2 + 0.8776956e3f / t + 0.179910f;
	} else {
		x = -3.0258469e9f / t3 + 2.1070379e6f / t2 + 0.2226347e3f / t + 0.240390f;
	}

	if ( t < 2222.0f ) {
		y = -1.1063814f * x * x * x - 1.34811020f * x * x + 2.18555832f * x - 0.20219683f;
	} else if ( t < 4000.0f ) {
		y = -0.9549476f * x * x * x - 1.37418593f * x * x + 2.09137015f * x - 0.16748867f;
	} else {
		y = 3.0817580f * x * x * x - 5.87338670f * x * x + 3.75112997f * x - 0.37001483f;
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

static void vk_build_bradford_adaptation( float sourceKelvin, float targetKelvin, float matrix[9] )
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
	float src[3], dst[3], srcCone[3], dstCone[3], scale[3], scaledBradford[9];
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
		scale[row] = fabs( srcCone[row] ) > 0.0001f ? dstCone[row] / srcCone[row] : 1.0f;
	}

	for ( row = 0; row < 3; row++ ) {
		for ( col = 0; col < 3; col++ ) {
			scaledBradford[row * 3 + col] = scale[row] * bradford[row * 3 + col];
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

static qboolean vk_validate_color_grade_lut_atlas( const image_t *image, int *size )
{
	int lutSize;

	if ( !image || image->width <= 0 || image->height <= 0 ) {
		return qfalse;
	}
	if ( image->width != image->height * image->height ) {
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
	byte data[ IDENTITY_LUT_SIZE * IDENTITY_LUT_SIZE * IDENTITY_LUT_SIZE * 4 ];
	int r, g, b;
	const int width = IDENTITY_LUT_SIZE * IDENTITY_LUT_SIZE;

	if ( identityLut ) {
		if ( size ) {
			*size = IDENTITY_LUT_SIZE;
		}
		return identityLut;
	}

	for ( b = 0; b < IDENTITY_LUT_SIZE; b++ ) {
		for ( g = 0; g < IDENTITY_LUT_SIZE; g++ ) {
			for ( r = 0; r < IDENTITY_LUT_SIZE; r++ ) {
				const int index = ( g * width + b * IDENTITY_LUT_SIZE + r ) * 4;
				data[index + 0] = (byte)( r * 255 / ( IDENTITY_LUT_SIZE - 1 ) );
				data[index + 1] = (byte)( g * 255 / ( IDENTITY_LUT_SIZE - 1 ) );
				data[index + 2] = (byte)( b * 255 / ( IDENTITY_LUT_SIZE - 1 ) );
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

	if ( !r_colorGradeLUT || r_colorGradeLUT->modificationCount == lutModificationCount ) {
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
				"WARNING: color-grade LUT '%s' must use width N*N and height N; using identity LUT\n",
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
		if ( tr.whiteImage && tr.whiteImage->descriptor != VK_NULL_HANDLE ) {
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
	qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		vk.pipeline_layout_post_process, 0, ARRAY_LEN( post_sets ), post_sets, 0, NULL );
}

static void vk_push_post_process_constants( void )
{
	float constants[4] = { 0.0f };
	float invWidth = glConfig.vidWidth > 0 ? 1.0f / (float)glConfig.vidWidth : 1.0f;
	float invHeight = glConfig.vidHeight > 0 ? 1.0f / (float)glConfig.vidHeight : 1.0f;

	constants[0] = tr.refdef.floatTime > 0.0 ? (float)tr.refdef.floatTime :
		(float)tr.frameCount * ( 1.0f / 60.0f );
	constants[1] = invWidth;
	constants[2] = invHeight;
	qvkCmdPushConstants( vk.cmd->command_buffer, vk.pipeline_layout_post_process,
		VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( constants ), constants );
}

void vk_update_post_process_pipelines( void )
{
	vk_set_hdr_metadata();

	if ( vk.fboActive ) {
		// update gamma shader
		vk_create_post_process_pipeline( 0, 0, 0 );
		vk_create_post_process_pipeline( 5, glConfig.vidWidth, glConfig.vidHeight );
		if ( r_globalFog && r_globalFog->integer &&
			vk.modules.global_fog_fs != VK_NULL_HANDLE ) {
			vk_create_post_process_pipeline( 10, glConfig.vidWidth, glConfig.vidHeight );
		}
		if ( r_motionBlur && r_motionBlur->integer ) {
			vk_create_post_process_pipeline( 6, glConfig.vidWidth, glConfig.vidHeight );
			vk_create_post_process_pipeline( 7, glConfig.vidWidth, glConfig.vidHeight );
		}
		if ( r_liquid && r_liquid->integer ) {
			vk_create_post_process_pipeline( 9,
				vk.liquidSnapshotWidth, vk.liquidSnapshotHeight );
		}
		// update capture pipeline
		vk_create_post_process_pipeline( 3, gls.captureWidth, gls.captureHeight );
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
			vk_create_post_process_pipeline( 4, glConfig.vidWidth, glConfig.vidHeight ); // bloom blending for cel outlines
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
	qboolean allocated;
	// for layout transition:
	VkImageAspectFlags aspect_flags;
	VkImageLayout image_layout;
	VkFormat image_format;
} vk_attach_desc_t;

static vk_attach_desc_t attachments[ MAX_ATTACHMENTS_IN_POOL ];
static uint32_t num_attachments = 0;


static void vk_clear_attachment_pool( void )
{
	Com_Memset( attachments, 0, sizeof( attachments ) );
	num_attachments = 0;
}


static qboolean vk_attachment_is_transient( const vk_attach_desc_t *attachment )
{
	return ( attachment->usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT ) ? qtrue : qfalse;
}


static qboolean vk_attachment_supports_memory( uint32_t memoryTypeBits, VkMemoryPropertyFlags properties )
{
	return find_memory_type2( memoryTypeBits, properties, NULL ) != ~0U;
}


static qboolean vk_attachment_matches_pass( const vk_attach_desc_t *attachment, int pass )
{
	qboolean transient;
	qboolean supports_lazy;

	if ( attachment->allocated ) {
		return qfalse;
	}

	transient = vk_attachment_is_transient( attachment );
	supports_lazy = transient && vk_attachment_supports_memory( attachment->reqs.memoryTypeBits,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT );

	switch ( pass ) {
		case 0:
			return supports_lazy;
		case 1:
			return transient && !supports_lazy;
		default:
			return !transient;
	}
}


static uint32_t vk_select_attachment_memory_type( uint32_t memoryTypeBits, qboolean preferLazy )
{
	uint32_t memoryTypeIndex;

	if ( preferLazy ) {
		memoryTypeIndex = find_memory_type2( memoryTypeBits,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT, NULL );
		if ( memoryTypeIndex != ~0U ) {
			return memoryTypeIndex;
		}
	}

	return find_memory_type( memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
}


static void vk_alloc_attachment_batch( int pass )
{
	VkImageViewCreateInfo view_desc;
	VkMemoryDedicatedAllocateInfoKHR alloc_info2;
	vk_memory_allocation_t allocation;
	VkDeviceSize offset;
	uint32_t memoryTypeBits;
	uint32_t memoryTypeIndex;
	uint32_t batch[ MAX_ATTACHMENTS_IN_POOL ];
	uint32_t batch_count;
	qboolean preferLazy;
	uint32_t i, j;

	if ( vk.image_memory_count >= ARRAY_LEN( vk.image_memory ) ) {
		ri.Error( ERR_DROP, "vk.image_memory_count == %i", (int)ARRAY_LEN( vk.image_memory ) );
	}

	memoryTypeBits = ~0U;
	preferLazy = ( pass == 0 ) ? qtrue : qfalse;
	offset = 0;
	batch_count = 0;

	for ( i = 0; i < num_attachments; i++ ) {
		uint32_t candidateMemoryTypeBits;

		if ( !vk_attachment_matches_pass( &attachments[ i ], pass ) ) {
			continue;
		}

		candidateMemoryTypeBits = memoryTypeBits & attachments[ i ].reqs.memoryTypeBits;
		if ( batch_count > 0 &&
			!vk_attachment_supports_memory( candidateMemoryTypeBits,
				preferLazy ? ( VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT ) : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ) ) {
			continue;
		}

		batch[ batch_count++ ] = i;
		memoryTypeBits = candidateMemoryTypeBits;
	}

	if ( batch_count == 0 ) {
		return;
	}

	for ( j = 0; j < batch_count; j++ ) {
		i = batch[ j ];
#ifdef MIN_IMAGE_ALIGN
		VkDeviceSize alignment = MAX( attachments[ i ].reqs.alignment, MIN_IMAGE_ALIGN );
#else
		VkDeviceSize alignment = attachments[ i ].reqs.alignment;
#endif
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

	memoryTypeIndex = vk_select_attachment_memory_type( memoryTypeBits, preferLazy );

#ifdef _DEBUG
	ri.Printf( PRINT_ALL, "memory type bits: %04x\n", memoryTypeBits );
	ri.Printf( PRINT_ALL, "memory type index: %04x\n", memoryTypeIndex );
	ri.Printf( PRINT_ALL, "total size: %i\n", (int)offset );
#endif

	Com_Memset( &alloc_info2, 0, sizeof( alloc_info2 ) );
	if ( batch_count == 1 ) {
		if ( vk.dedicatedAllocation ) {
			alloc_info2.sType =  VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR;
			alloc_info2.image = attachments[ batch[0] ].descriptor;
		}
	}

	// allocate and bind memory
	vk_allocate_memory( &allocation, offset, memoryTypeIndex, VK_MEMORY_CATEGORY_ATTACHMENTS,
		va( "framebuffer memory chunk %i", vk.image_memory_count ),
		alloc_info2.sType ? &alloc_info2 : NULL );
	allocation.transient = vk_attachment_is_transient( &attachments[ batch[0] ] );

	vk.image_memory[ vk.image_memory_count++ ] = allocation;

	for ( j = 0; j < batch_count; j++ ) {
		i = batch[ j ];

		VK_CHECK( qvkBindImageMemory( vk.device, attachments[i].descriptor, allocation.memory, attachments[i].memory_offset ) );
		attachments[ i ].allocated = qtrue;

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
}


static void vk_alloc_attachments( void )
{
	VkCommandBuffer command_buffer;
	uint32_t i;
	int pass;

	if ( num_attachments == 0 ) {
		return;
	}

	for ( pass = 0; pass < 3; pass++ ) {
		qboolean keepAllocating;

		do {
			uint32_t oldCount = vk.image_memory_count;
			vk_alloc_attachment_batch( pass );
			keepAllocating = ( oldCount != vk.image_memory_count ) ? qtrue : qfalse;
		} while ( keepAllocating );
	}

	for ( i = 0; i < num_attachments; i++ ) {
		if ( !attachments[i].allocated ) {
			ri.Error( ERR_FATAL, "Vulkan: attachment memory allocation failed" );
		}
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
		attachments[ num_attachments ].allocated = qfalse;
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
	VkImageUsageFlags usage, VkImage *image, VkImageView *image_view, VkImageLayout image_layout, qboolean allowTransient )
{
	VkImageCreateInfo create_desc;
	VkMemoryRequirements memory_requirements;

	if ( allowTransient && !( usage & ( VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT ) ) )
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


static void create_depth_attachment( uint32_t width, uint32_t height, VkSampleCountFlagBits samples, VkImage *image, VkImageView *image_view, qboolean allowTransient )
{
	VkImageCreateInfo create_desc;
	VkMemoryRequirements memory_requirements;
	VkImageAspectFlags image_aspect_flags;
	qboolean depthFadeSource;
	qboolean depthFadeResolveSource;

	// create depth image
	depthFadeSource = ( image == &vk.depth_image && vk_depth_fade_supported() );
	depthFadeResolveSource = ( depthFadeSource && vk_depth_fade_resolve_supported() );

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
	if ( depthFadeSource && !depthFadeResolveSource ) {
		create_desc.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}
	if ( allowTransient && !depthFadeSource ) {
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


static void create_depth_fade_attachment( uint32_t width, uint32_t height, VkImage *image, VkImageView *image_view )
{
	VkImageCreateInfo create_desc;
	VkMemoryRequirements memory_requirements;
	VkImageUsageFlags usage;

	usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	if ( vk_depth_fade_resolve_supported() ) {
		usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	}

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
	create_desc.samples = VK_SAMPLE_COUNT_1_BIT;
	create_desc.tiling = VK_IMAGE_TILING_OPTIMAL;
	create_desc.usage = usage;
	create_desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	create_desc.queueFamilyIndexCount = 0;
	create_desc.pQueueFamilyIndices = NULL;
	create_desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VK_CHECK( qvkCreateImage( vk.device, &create_desc, NULL, image ) );

	vk_get_image_memory_erquirements( *image, &memory_requirements );

	vk_add_attachment_desc( *image, image_view, create_desc.usage, &memory_requirements, vk.depth_format, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
}


static qboolean vk_depth_format_sampled_supported( void )
{
	VkFormatProperties props;

	if ( vk.depth_format == VK_FORMAT_UNDEFINED ) {
		return qfalse;
	}

	qvkGetPhysicalDeviceFormatProperties( vk.physical_device, vk.depth_format, &props );
	return ( props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT ) ? qtrue : qfalse;
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
	qboolean correctnessMode = ( r_shadowCorrectness && r_shadowCorrectness->integer ) ? qtrue : qfalse;

	if ( ( !correctnessMode && ( !r_dlightShadows || !r_dlightShadows->integer ||
		!r_dlightShadowMaxLights || r_dlightShadowMaxLights->integer <= 0 ) ) ||
		!r_dlightMode || !r_dlightMode->integer ||
		!vk_depth_format_sampled_supported() ) {
		return qfalse;
	}

	return R_DlightShadowAtlasLayout( correctnessMode ? 1 : r_dlightShadowMaxLights->integer,
		r_dlightShadowResolution ? r_dlightShadowResolution->integer : 256,
		glConfig.maxTextureSize, layout );
#else
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
	return qfalse;
#endif
}


static void vk_store_dlight_shadow_atlas_layout( const dlightShadowAtlasLayout_t *layout )
{
	vk.dlight_shadow_atlas_width = layout->width;
	vk.dlight_shadow_atlas_height = layout->height;
	vk.dlight_shadow_face_size = layout->faceSize;
	vk.dlight_shadow_atlas_columns = layout->columns;
	vk.dlight_shadow_atlas_rows = layout->rows;
	vk.dlight_shadow_max_lights = layout->maxLights;
	vk_invalidate_dlight_shadow_atlas_generation();
}


static void vk_store_spot_shadow_atlas_layout( const spotShadowAtlasLayout_t *layout )
{
	vk.spot_shadow_atlas_width = layout->width;
	vk.spot_shadow_atlas_height = layout->height;
	vk.spot_shadow_tile_size = layout->tileSize;
	vk.spot_shadow_atlas_columns = layout->columns;
	vk.spot_shadow_atlas_rows = layout->rows;
	vk.spot_shadow_max_lights = layout->maxLights;
	vk_invalidate_spot_shadow_atlas_generation();
}


static void vk_store_csm_shadow_atlas_layout( const csmShadowAtlasLayout_t *layout )
{
	vk.csm_shadow_atlas_width = layout->width;
	vk.csm_shadow_atlas_height = layout->height;
	vk.csm_shadow_cascade_size = layout->cascadeSize;
	vk.csm_shadow_cascade_count = layout->cascadeCount;
	vk_invalidate_csm_shadow_atlas_generation();
}


static void create_dlight_shadow_attachment( uint32_t width, uint32_t height, VkImage *image, VkImageView *image_view )
{
	VkImageCreateInfo create_desc;
	VkMemoryRequirements memory_requirements;

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
	create_desc.samples = VK_SAMPLE_COUNT_1_BIT;
	create_desc.tiling = VK_IMAGE_TILING_OPTIMAL;
	create_desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	create_desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	create_desc.queueFamilyIndexCount = 0;
	create_desc.pQueueFamilyIndices = NULL;
	create_desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VK_CHECK( qvkCreateImage( vk.device, &create_desc, NULL, image ) );

	vk_get_image_memory_erquirements( *image, &memory_requirements );

	vk_add_attachment_desc( *image, image_view, create_desc.usage, &memory_requirements, vk.depth_format,
		VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
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
			usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, &vk.color_image, &vk.color_image_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, qfalse );
		if ( r_motionBlur && r_motionBlur->integer ) {
			create_color_attachment( glConfig.vidWidth, glConfig.vidHeight, VK_SAMPLE_COUNT_1_BIT, vk.color_format,
				usage, &vk.motion_blur_image, &vk.motion_blur_image_view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, qfalse );
		}
		if ( r_liquid && r_liquid->integer &&
			vk.liquidSnapshotWidth > 0 && vk.liquidSnapshotHeight > 0 ) {
			create_color_attachment( vk.liquidSnapshotWidth, vk.liquidSnapshotHeight,
				VK_SAMPLE_COUNT_1_BIT, vk.color_format, usage,
				&vk.liquidSnapshot.color_image, &vk.liquidSnapshot.color_image_view,
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
		create_depth_attachment( vk.screenMapWidth, vk.screenMapHeight, vk.screenMapSamples, &vk.screenMap.depth_image, &vk.screenMap.depth_image_view, qtrue );

		if ( vk.msaaActive ) {
			create_color_attachment( glConfig.vidWidth, glConfig.vidHeight, vkSamples, vk.color_format,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &vk.msaa_image, &vk.msaa_image_view, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				!( r_bloom->integer || ( r_motionBlur && r_motionBlur->integer ) ||
					( r_liquid && r_liquid->integer ) ) );
		}

		// Dedicated post-output capture target keeps screenshots SDR and independent
		// of swapchain color space, window scaling, and hardware HDR output.
		usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		create_color_attachment( gls.captureWidth, gls.captureHeight, VK_SAMPLE_COUNT_1_BIT, vk.capture_format,
			usage, &vk.capture.image, &vk.capture.image_view, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, qfalse );
	} // if ( vk.fboActive )

	//vk_alloc_attachments();

	create_depth_attachment( glConfig.vidWidth, glConfig.vidHeight, vkSamples, &vk.depth_image, &vk.depth_image_view,
		( vk.fboActive && ( r_bloom->integer || ( r_motionBlur && r_motionBlur->integer ) ||
			( r_liquid && r_liquid->integer ) ) ) ? qfalse : qtrue );

	if ( vk_depth_fade_supported() ) {
		create_depth_fade_attachment( glConfig.vidWidth, glConfig.vidHeight, &vk.depth_fade_image, &vk.depth_fade_image_view );
	}

	if ( vk_dlight_shadow_atlas_layout( &shadowLayout ) ) {
		create_dlight_shadow_attachment( shadowLayout.width, shadowLayout.height,
			&vk.dlight_shadow_image, &vk.dlight_shadow_image_view );
		vk_store_dlight_shadow_atlas_layout( &shadowLayout );
		ri.Printf( PRINT_ALL, "...dynamic-light shadow atlas %ix%i (%i px faces, %i lights)\n",
			shadowLayout.width, shadowLayout.height, shadowLayout.faceSize, shadowLayout.maxLights );
	}

	if ( vk_spot_shadow_atlas_layout( &spotShadowLayout ) ) {
		create_dlight_shadow_attachment( spotShadowLayout.width, spotShadowLayout.height,
			&vk.spot_shadow_image, &vk.spot_shadow_image_view );
		vk_store_spot_shadow_atlas_layout( &spotShadowLayout );
		ri.Printf( PRINT_ALL, "...spotlight shadow atlas %ix%i (%i px tiles, %i lights)\n",
			spotShadowLayout.width, spotShadowLayout.height,
			spotShadowLayout.tileSize, spotShadowLayout.maxLights );
	}

	if ( vk_csm_shadow_atlas_layout( &csmShadowLayout ) ) {
		create_dlight_shadow_attachment( csmShadowLayout.width, csmShadowLayout.height,
			&vk.csm_shadow_image, &vk.csm_shadow_image_view );
		vk_store_csm_shadow_atlas_layout( &csmShadowLayout );
		ri.Printf( PRINT_ALL, "...sky-sun shadow atlas %ix%i (%i px cascades, %i cascades)\n",
			csmShadowLayout.width, csmShadowLayout.height,
			csmShadowLayout.cascadeSize, csmShadowLayout.cascadeCount );
	}

	vk_alloc_attachments();

	for ( i = 0; i < vk.image_memory_count; i++ )
	{
		SET_OBJECT_NAME( vk.image_memory[i].memory, va( "framebuffer memory chunk %i", i ), VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT );
	}

	SET_OBJECT_NAME( vk.depth_image, "depth attachment", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT );
	SET_OBJECT_NAME( vk.depth_image_view, "depth attachment", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT );
	SET_OBJECT_NAME( vk.depth_fade_image, "depth fade attachment", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT );
	SET_OBJECT_NAME( vk.depth_fade_image_view, "depth fade attachment", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT );
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

	SET_OBJECT_NAME( vk.capture.image, "capture image", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT );
	SET_OBJECT_NAME( vk.capture.image_view, "capture image view", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT );

	for ( i = 0; i < ARRAY_LEN( vk.bloom_image ); i++ )
	{
		SET_OBJECT_NAME( vk.bloom_image[i], va( "bloom attachment %i", i ), VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT );
		SET_OBJECT_NAME( vk.bloom_image_view[i], va( "bloom attachment %i", i ), VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT );
	}
}


static void vk_create_framebuffers( void )
{
	VkImageView framebufferAttachments[4];
	VkFramebufferCreateInfo desc;
	qboolean depthResolveActive;
	uint32_t n;

	depthResolveActive = vk_depth_fade_uses_depth_resolve();

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

			desc.renderPass = vk.render_pass.main_load;
			VK_CHECK( qvkCreateFramebuffer( vk.device, &desc, NULL, &vk.framebuffers.main_load[n] ) );
			SET_OBJECT_NAME( vk.framebuffers.main_load[n], va( "framebuffer - main load %i", n ), VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT );
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
				if ( depthResolveActive ) {
					desc.attachmentCount = 4;
					framebufferAttachments[3] = vk.depth_fade_image_view;
				}
				VK_CHECK( qvkCreateFramebuffer( vk.device, &desc, NULL, &vk.framebuffers.main[n] ) );
				SET_OBJECT_NAME( vk.framebuffers.main[n], "framebuffer - main", VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT );

				desc.renderPass = vk.render_pass.main_load;
				desc.attachmentCount = vk.msaaActive ? 3 : 2;
				VK_CHECK( qvkCreateFramebuffer( vk.device, &desc, NULL, &vk.framebuffers.main_load[n] ) );
				SET_OBJECT_NAME( vk.framebuffers.main_load[n], "framebuffer - main load", VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT );
			}
			else
			{
				vk.framebuffers.main[n] = vk.framebuffers.main[0];
				vk.framebuffers.main_load[n] = vk.framebuffers.main_load[0];
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
		VK_CHECK( qvkCreateFramebuffer( vk.device, &desc, NULL, &vk.framebuffers.motion_blur ) );
		SET_OBJECT_NAME( vk.framebuffers.motion_blur, "framebuffer - motion blur scratch", VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT );
	}

	if ( vk.render_pass.dlight_shadow != VK_NULL_HANDLE && vk.dlight_shadow_image_view != VK_NULL_HANDLE )
	{
		desc.renderPass = vk.render_pass.dlight_shadow;
		desc.attachmentCount = 1;
		desc.width = vk.dlight_shadow_atlas_width;
		desc.height = vk.dlight_shadow_atlas_height;
		framebufferAttachments[0] = vk.dlight_shadow_image_view;
		VK_CHECK( qvkCreateFramebuffer( vk.device, &desc, NULL, &vk.framebuffers.dlight_shadow ) );
		SET_OBJECT_NAME( vk.framebuffers.dlight_shadow, "framebuffer - dlight shadow atlas", VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT );
	}

	if ( vk.render_pass.dlight_shadow != VK_NULL_HANDLE && vk.spot_shadow_image_view != VK_NULL_HANDLE )
	{
		desc.renderPass = vk.render_pass.dlight_shadow;
		desc.attachmentCount = 1;
		desc.width = vk.spot_shadow_atlas_width;
		desc.height = vk.spot_shadow_atlas_height;
		framebufferAttachments[0] = vk.spot_shadow_image_view;
		VK_CHECK( qvkCreateFramebuffer( vk.device, &desc, NULL, &vk.framebuffers.spot_shadow ) );
		SET_OBJECT_NAME( vk.framebuffers.spot_shadow, "framebuffer - spot shadow atlas", VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT );
	}

	if ( vk.render_pass.dlight_shadow != VK_NULL_HANDLE && vk.csm_shadow_image_view != VK_NULL_HANDLE )
	{
		desc.renderPass = vk.render_pass.dlight_shadow;
		desc.attachmentCount = 1;
		desc.width = vk.csm_shadow_atlas_width;
		desc.height = vk.csm_shadow_atlas_height;
		framebufferAttachments[0] = vk.csm_shadow_image_view;
		VK_CHECK( qvkCreateFramebuffer( vk.device, &desc, NULL, &vk.framebuffers.csm_shadow ) );
		SET_OBJECT_NAME( vk.framebuffers.csm_shadow, "framebuffer - csm shadow atlas", VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT );
	}

	if ( vk.fboActive )
	{
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
				"framebuffer - liquid snapshot", VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT );
		}

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
}


static void vk_create_sync_primitives( void ) {
	VkSemaphoreCreateInfo desc;
	VkFenceCreateInfo fence_desc;
	VkQueryPoolCreateInfo query_desc;
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

		if ( vk.timestamps ) {
			query_desc.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
			query_desc.pNext = NULL;
			query_desc.flags = 0;
			query_desc.queryType = VK_QUERY_TYPE_TIMESTAMP;
			query_desc.queryCount = VK_MAX_FRAME_TIMESTAMPS;
			query_desc.pipelineStatistics = 0;

			VK_CHECK( qvkCreateQueryPool( vk.device, &query_desc, NULL, &vk.tess[i].timestamp_query_pool ) );
			vk.tess[i].timestamp_query_count = 0;
			vk.tess[i].timestamp_query_valid = qfalse;

			SET_OBJECT_NAME( vk.tess[i].timestamp_query_pool, va( "timestamp query pool %i", i ), VK_DEBUG_REPORT_OBJECT_TYPE_QUERY_POOL_EXT );
		}

		SET_OBJECT_NAME( vk.tess[i].image_acquired, va( "image_acquired semaphore %i", i ), VK_DEBUG_REPORT_OBJECT_TYPE_SEMAPHORE_EXT );
#ifdef USE_UPLOAD_QUEUE
		SET_OBJECT_NAME( vk.tess[i].rendering_finished2, va( "rendering_finished2 semaphore %i", i ), VK_DEBUG_REPORT_OBJECT_TYPE_SEMAPHORE_EXT );
#endif
		SET_OBJECT_NAME( vk.tess[i].rendering_finished_fence, va( "rendering_finished fence %i", i ), VK_DEBUG_REPORT_OBJECT_TYPE_FENCE_EXT );
	}

	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ )
	{
		fence_desc.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fence_desc.pNext = NULL;
		fence_desc.flags = 0;

		VK_CHECK( qvkCreateFence( vk.device, &fence_desc, NULL, &vk.upload_contexts[i].fence ) );
		vk.upload_contexts[i].submitted = qfalse;

		SET_OBJECT_NAME( vk.upload_contexts[i].fence, va( "upload command fence %i", i ), VK_DEBUG_REPORT_OBJECT_TYPE_FENCE_EXT );
	}
	vk.upload_context_index = 0;

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
}


static void vk_destroy_sync_primitives( void  ) {
	uint32_t i;

	vk_wait_upload_contexts( __func__ );

#ifdef USE_UPLOAD_QUEUE
	qvkDestroySemaphore( vk.device, vk.image_uploaded2, NULL );
#endif

	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		qvkDestroySemaphore( vk.device, vk.tess[i].image_acquired, NULL );
#ifdef USE_UPLOAD_QUEUE
		qvkDestroySemaphore( vk.device, vk.tess[i].rendering_finished2, NULL );
#endif
		qvkDestroyFence( vk.device, vk.tess[i].rendering_finished_fence, NULL );
		if ( vk.tess[i].timestamp_query_pool != VK_NULL_HANDLE ) {
			qvkDestroyQueryPool( vk.device, vk.tess[i].timestamp_query_pool, NULL );
			vk.tess[i].timestamp_query_pool = VK_NULL_HANDLE;
		}
		vk.tess[i].timestamp_query_count = 0;
		vk.tess[i].timestamp_query_valid = qfalse;
		vk.tess[i].waitForFence = qfalse;
		vk.tess[i].swapchain_image_acquired = qfalse;
	}

	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		if ( vk.upload_contexts[i].fence != VK_NULL_HANDLE ) {
			qvkDestroyFence( vk.device, vk.upload_contexts[i].fence, NULL );
			vk.upload_contexts[i].fence = VK_NULL_HANDLE;
		}
		vk.upload_contexts[i].submitted = qfalse;
	}
	vk.upload_context_index = 0;

#ifdef USE_UPLOAD_QUEUE
	qvkDestroyFence( vk.device, vk.aux_fence, NULL );

	vk.rendering_finished = VK_NULL_HANDLE;
	vk.image_uploaded = VK_NULL_HANDLE;
#endif
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
		if ( vk.framebuffers.main_load[n] != VK_NULL_HANDLE ) {
			if ( !vk.fboActive || n == 0 ) {
				qvkDestroyFramebuffer( vk.device, vk.framebuffers.main_load[n], NULL );
			}
			vk.framebuffers.main_load[n] = VK_NULL_HANDLE;
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
	if ( vk.framebuffers.motion_blur != VK_NULL_HANDLE ) {
		qvkDestroyFramebuffer( vk.device, vk.framebuffers.motion_blur, NULL );
		vk.framebuffers.motion_blur = VK_NULL_HANDLE;
	}

	if ( vk.framebuffers.screenmap != VK_NULL_HANDLE ) {
		qvkDestroyFramebuffer( vk.device, vk.framebuffers.screenmap, NULL );
		vk.framebuffers.screenmap = VK_NULL_HANDLE;
	}
	if ( vk.framebuffers.liquid_snapshot != VK_NULL_HANDLE ) {
		qvkDestroyFramebuffer( vk.device, vk.framebuffers.liquid_snapshot, NULL );
		vk.framebuffers.liquid_snapshot = VK_NULL_HANDLE;
	}

	if ( vk.framebuffers.capture != VK_NULL_HANDLE ) {
		qvkDestroyFramebuffer( vk.device, vk.framebuffers.capture, NULL );
		vk.framebuffers.capture = VK_NULL_HANDLE;
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
}


static void vk_destroy_swapchain( void ) {
	uint32_t i;

	for ( i = 0; i < vk.swapchain_image_count; i++ ) {
		if ( vk.swapchain_image_views[i] != VK_NULL_HANDLE ) {
			qvkDestroyImageView( vk.device, vk.swapchain_image_views[i], NULL );
			vk.swapchain_image_views[i] = VK_NULL_HANDLE;
		}
		if ( vk.swapchain_rendering_finished[i] != VK_NULL_HANDLE ) {
			qvkDestroySemaphore( vk.device, vk.swapchain_rendering_finished[i], NULL );
			vk.swapchain_rendering_finished[i] = VK_NULL_HANDLE;
		}
	}

	qvkDestroySwapchainKHR( vk.device, vk.swapchain, NULL );
}

static void vk_destroy_attachments( void );
static void vk_destroy_render_passes( void );
static void vk_destroy_pipelines( qboolean resetCount );

static void vk_restart_swapchain( const char *funcname, VkResult res )
{
	uint32_t i;

	if ( !ri.CL_IsMinimized() ) {
#ifdef _DEBUG
		ri.Printf( PRINT_WARNING, "%s(%s): restarting swapchain...\n", funcname, vk_result_string( res ) );
#else
		ri.Printf( PRINT_WARNING, "%s(): restarting swapchain...\n", funcname );
#endif
	}

	vk_wait_idle();
	vk_release_cubemap_capture();

	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		vk_reset_command_pool_for_reuse( vk.tess[i].command_pool, qfalse );
	}

#ifdef USE_UPLOAD_QUEUE
	vk_reset_command_pool_for_reuse( vk.staging_command_pool, qtrue );
#endif

	vk_destroy_pipelines( qfalse );
	vk_destroy_framebuffers();
	vk_destroy_render_passes();
	vk_destroy_attachments();
	vk_destroy_swapchain();
	vk_destroy_sync_primitives();

	vk_select_surface_format( vk.physical_device, vk_surface );
	setup_surface_formats( vk.physical_device );

	vk_create_sync_primitives();
	vk_create_swapchain( vk.physical_device, vk.device, vk_surface, vk.present_format, &vk.swapchain, qfalse );
	vk_create_attachments();
	vk_create_render_passes();
	vk_create_framebuffers();

	vk_update_attachment_descriptors();

	vk_update_post_process_pipelines();
	vk_warm_pipelines( qtrue );
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

	init_vulkan_library();

	qvkGetDeviceQueue( vk.device, vk.queue_family_index, 0, &vk.queue );

	qvkGetPhysicalDeviceProperties( vk.physical_device, &props );

	vk.cmd = vk.tess + 0;
	vk.uniform_alignment = props.limits.minUniformBufferOffsetAlignment;
	vk.uniform_item_size = PAD( (uint32_t)sizeof( vkUniform_t ), vk.uniform_alignment );
	vk.timestampPeriod = props.limits.timestampPeriod;
	vk.timestamps = ( vk.timestampValidBits > 0 && vk.timestampPeriod > 0.0f ) ? qtrue : qfalse;

	// for flare visibility tests
	vk.storage_alignment = MAX( props.limits.minStorageBufferOffsetAlignment, sizeof( uint32_t ) );

	vk.maxAnisotropy = props.limits.maxSamplerAnisotropy;

	vk.blitFilter = GL_NEAREST;
	vk.windowAdjusted = qfalse;
	vk.blitX0 = vk.blitY0 = 0;

	vk_set_render_scale();

	vk.fboActive = r_fbo->integer ? qtrue : qfalse;
	vk.msaaActive = qfalse;

	// multisampling

	vkMaxSamples = props.limits.framebufferColorSampleCounts &
		props.limits.framebufferDepthSampleCounts;

	if ( vk.fboActive && r_ext_multisample->integer ) {
		const int requestedSamples = MAX( r_ext_multisample->integer, 2 );
		vkSamples = vk_select_sample_count( requestedSamples, vkMaxSamples );
		vk.msaaActive = ( vkSamples > VK_SAMPLE_COUNT_1_BIT ) ? qtrue : qfalse;
		if ( vk.msaaActive ) {
			if ( vkSamples == requestedSamples ) {
				ri.Printf( PRINT_ALL, "...using %ix MSAA\n", vkSamples );
			} else {
				ri.Printf( PRINT_ALL, "...using %ix MSAA (requested %ix)\n",
					vkSamples, requestedSamples );
			}
		} else {
			ri.Printf( PRINT_ALL, "...MSAA is not available for the selected Vulkan color/depth framebuffer formats\n" );
		}
	} else {
		vkSamples = VK_SAMPLE_COUNT_1_BIT;
	}

	if ( vk.msaaActive && vk_depth_fade_requested() ) {
		// Soft particles depend on the established single-sample depth snapshot.
		ri.Printf( PRINT_WARNING,
			"...Vulkan depth fade requested; disabling MSAA so depth fade can use the single-sample depth copy path\n" );
		vkSamples = VK_SAMPLE_COUNT_1_BIT;
		vk.msaaActive = qfalse;
	}

	/* Keep the legacy $screenMap target independent of enhanced liquids. */
	vk.screenMapSamples = vk_select_sample_count( 4, vkMaxSamples );
	vk.screenMapWidth = MAX( 4, (uint32_t)( glConfig.vidWidth / 16.0f ) );
	vk.screenMapHeight = MAX( 4, (uint32_t)( glConfig.vidHeight / 16.0f ) );

	vk.liquidSnapshotWidth = vk.liquidSnapshotHeight = 0;
	if ( r_liquid && r_liquid->integer ) {
		const float scale = Com_Clamp( 0.25f, 1.0f,
			r_liquidResolution ? r_liquidResolution->value : 1.0f );

		vk.liquidSnapshotWidth = MAX( 64, (uint32_t)( glConfig.vidWidth * scale + 0.5f ) );
		vk.liquidSnapshotHeight = MAX( 64, (uint32_t)( glConfig.vidHeight * scale + 0.5f ) );
	}

	vk.defaults.geometry_size = VERTEX_BUFFER_SIZE;
	vk.defaults.staging_size = STAGING_BUFFER_SIZE;

	// get memory size & defaults
	{
		VkPhysicalDeviceMemoryProperties memoryProps;
		VkDeviceSize maxDedicatedSize = 0;
		VkDeviceSize maxBARSize = 0;
		qvkGetPhysicalDeviceMemoryProperties( vk.physical_device, &memoryProps );
		for ( i = 0; i < memoryProps.memoryTypeCount; i++ ) {
			if ( memoryProps.memoryTypes[i].propertyFlags == VK_MEMORY_HEAP_DEVICE_LOCAL_BIT ) {
				maxDedicatedSize = memoryProps.memoryHeaps[memoryProps.memoryTypes[i].heapIndex].size;
			}
			else if ( memoryProps.memoryTypes[i].propertyFlags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT ) {
				if ( maxDedicatedSize == 0 || memoryProps.memoryHeaps[memoryProps.memoryTypes[i].heapIndex].size > maxDedicatedSize ) {
					maxDedicatedSize = memoryProps.memoryHeaps[memoryProps.memoryTypes[i].heapIndex].size;
				}
			}
			if ( memoryProps.memoryTypes[i].propertyFlags == (VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ) {
				maxBARSize = memoryProps.memoryHeaps[memoryProps.memoryTypes[i].heapIndex].size;
			}
			else if ( (memoryProps.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) == (VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ) {
				if ( maxBARSize == 0 ) {
					maxBARSize = memoryProps.memoryHeaps[memoryProps.memoryTypes[i].heapIndex].size;
				}
			}
		}

		if ( maxDedicatedSize != 0 ) {
			ri.Printf( PRINT_ALL, "...device memory size: %iMB\n", (int)((maxDedicatedSize + (1024 * 1024) - 1) / (1024 * 1024)) );
		}
		if ( maxBARSize != 0 ) {
			if ( maxBARSize >= 128 * 1024 * 1024 ) {
				// user larger buffers to avoid potential reallocations
				vk.defaults.geometry_size = VERTEX_BUFFER_SIZE_HI;
				vk.defaults.staging_size = STAGING_BUFFER_SIZE_HI;
			}
#ifdef _DEBUG
			ri.Printf( PRINT_ALL, "...BAR memory size: %iMB\n", (int)((maxBARSize + (1024 * 1024) - 1) / (1024 * 1024)) );
#endif
		}
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

	if ( vk_depth_fade_requested() && vk.maxBoundDescriptorSets <= VK_DESC_DEPTH_FADE ) {
		ri.Printf( PRINT_WARNING,
			"...Vulkan depth fade disabled: device exposes %u bound descriptor sets, %u required\n",
			vk.maxBoundDescriptorSets, VK_DESC_DEPTH_FADE + 1 );
	}

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

#ifdef _WIN32
	// Intel iGPU drivers from 101.5333 to 101.6737 have a known bug that causes
	// VK_ERROR_DEVICE_LOST during vkQueueSubmit, see https://github.com/ec-/Quake3e/issues/312
	if ( props.vendorID == 0x8086 ) {
		uint32_t drvMajor = props.driverVersion >> 14;
		uint32_t drvMinor = props.driverVersion & 0x3FFF;
		if ( drvMajor == 101 && drvMinor >= 5333 && drvMinor <= 6737 ) {
			Com_sprintf( vk.driverNote, sizeof( vk.driverNote ), S_COLOR_WARNING
				"\nWARNING: Intel driver %i.%i is known to cause Vulkan crashes.\n"
				"Consider updating to driver >= 101.6790 or downgrading to <= 101.5186.\n",
				drvMajor, drvMinor );
		}
	}
#endif

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

	// do early texture mode setup to avoid redundant descriptor updates in GL_SetDefaultState()
	vk.samplers.filter_min = -1;
	vk.samplers.filter_max = -1;
	GL_TextureMode( r_textureMode->string );
	r_textureMode->modified = qfalse;

	//
	// Sync primitives.
	//
	vk_create_sync_primitives();

#ifdef USE_UPLOAD_QUEUE
	vk.staging_command_pool = vk_create_transient_command_pool( "staging upload command pool" );
	vk_allocate_primary_command_buffer( vk.staging_command_pool, &vk.staging_command_buffer, "staging upload command buffer" );
#endif

	//
	// Reusable helper command contexts for uploads, layout transitions and readback copies.
	//
	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ )
	{
		vk.upload_contexts[i].command_pool = vk_create_transient_command_pool( va( "upload command pool %i", i ) );
		vk_allocate_primary_command_buffer( vk.upload_contexts[i].command_pool, &vk.upload_contexts[i].command_buffer, va( "upload command buffer %i", i ) );
	}

	//
	// Per-frame command contexts and color attachments.
	//
	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ )
	{
		vk.tess[i].command_pool = vk_create_transient_command_pool( va( "frame command pool %i", i ) );
		vk_allocate_primary_command_buffer( vk.tess[i].command_pool, &vk.tess[i].command_buffer, va( "frame command buffer %i", i ) );
	}

	//
	// Descriptor pool.
	//
	{
		VkDescriptorPoolSize pool_size[3];
		VkDescriptorPoolCreateInfo desc;
		uint32_t poolIndex, maxSets;

		pool_size[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		pool_size[0].descriptorCount = MAX_DRAWIMAGES + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + VK_NUM_BLOOM_PASSES * 2; // color, motion, screenmap, liquid source/target, depth fade, shadow atlases, bloom descriptors

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
	vk_create_layout_binding( 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, &vk.set_layout_sampler );
	vk_create_layout_binding( 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT, &vk.set_layout_uniform );
	vk_create_layout_binding( 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT, &vk.set_layout_storage );
	//vk_create_layout_binding( 0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, VK_SHADER_STAGE_FRAGMENT_BIT, &vk.set_layout_input );

	//
	// Pipeline layouts.
	//
	{
		VkDescriptorSetLayout set_layouts[6];
		VkPipelineLayoutCreateInfo desc;
		VkPushConstantRange push_range;
		VkPushConstantRange post_push_range;

		push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		push_range.offset = 0;
		push_range.size = 64; // 16 floats
		post_push_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		post_push_range.offset = 0;
		post_push_range.size = 48; // global-fog color, mode, and depth reconstruction

		// standard pipelines
		set_layouts[0] = vk.set_layout_uniform; // fog/dlight parameters
		set_layouts[1] = vk.set_layout_sampler; // diffuse
		set_layouts[2] = vk.set_layout_sampler; // lightmap / fog-only
		set_layouts[3] = vk.set_layout_sampler; // blend
		set_layouts[4] = vk.set_layout_sampler; // collapsed fog texture
		desc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		desc.pNext = NULL;
		desc.flags = 0;
		desc.setLayoutCount = (vk.maxBoundDescriptorSets >= VK_DESC_COUNT) ? VK_DESC_COUNT : 4;
		desc.pSetLayouts = set_layouts;
		desc.pushConstantRangeCount = 1;
		desc.pPushConstantRanges = &push_range;

		VK_CHECK(qvkCreatePipelineLayout(vk.device, &desc, NULL, &vk.pipeline_layout));

		// flare test pipeline
		set_layouts[0] = vk.set_layout_storage; // dynamic storage buffer

		desc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		desc.pNext = NULL;
		desc.flags = 0;
		desc.setLayoutCount = 1;
		desc.pSetLayouts = set_layouts;
		desc.pushConstantRangeCount = 1;
		desc.pPushConstantRanges = &push_range;

		VK_CHECK( qvkCreatePipelineLayout( vk.device, &desc, NULL, &vk.pipeline_layout_storage ) );

		// post-processing pipeline
		set_layouts[0] = vk.set_layout_sampler; // sampler
		set_layouts[1] = vk.set_layout_sampler; // sampler
		set_layouts[2] = vk.set_layout_sampler; // sampler
		set_layouts[3] = vk.set_layout_sampler; // sampler

		desc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		desc.pNext = NULL;
		desc.flags = 0;
		desc.setLayoutCount = 2;
		desc.pSetLayouts = set_layouts;
		desc.pushConstantRangeCount = 1;
		desc.pPushConstantRanges = &post_push_range;

		VK_CHECK( qvkCreatePipelineLayout( vk.device, &desc, NULL, &vk.pipeline_layout_post_process ) );

		desc.setLayoutCount = VK_NUM_BLOOM_PASSES;
		desc.pushConstantRangeCount = 0;
		desc.pPushConstantRanges = NULL;

		VK_CHECK( qvkCreatePipelineLayout( vk.device, &desc, NULL, &vk.pipeline_layout_blend ) );

		SET_OBJECT_NAME( vk.pipeline_layout, "pipeline layout - main", VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_LAYOUT_EXT );
		SET_OBJECT_NAME( vk.pipeline_layout_post_process, "pipeline layout - post-processing", VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_LAYOUT_EXT );
		SET_OBJECT_NAME( vk.pipeline_layout_blend, "pipeline layout - blend", VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_LAYOUT_EXT );
	}

	vk.geometry_buffer_size_new = vk.defaults.geometry_size;
	vk_create_geometry_buffers( vk.geometry_buffer_size_new );
	vk.geometry_buffer_size_new = 0;

	vk_create_storage_buffer( MAX_FLARES * vk.storage_alignment );

	vk_create_shader_modules();

	vk_create_pipeline_cache( &props );

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

	// preallocate staging buffer
	if ( vk.defaults.staging_size == STAGING_BUFFER_SIZE_HI ) {
		vk_alloc_staging_buffer( vk.defaults.staging_size );
	}

	vk.active = qtrue;
}


void vk_create_pipelines( void )
{
	vk_alloc_persistent_pipelines();

	vk.pipelines_world_base = vk.pipelines_count;
}


void vk_warm_pipelines( qboolean include_screenmap )
{
	uint32_t i;
	uint32_t count;
	uint32_t warmed;
	int start_msec;

	if ( vk.device == VK_NULL_HANDLE || vk.render_pass.main == VK_NULL_HANDLE ) {
		return;
	}

	count = vk.pipelines_count;
	warmed = 0;
	start_msec = ri.Milliseconds();

	for ( i = 0; i < count; i++ ) {
		VK_Pipeline_t *pipeline = &vk.pipelines[i];

		if ( pipeline->handle[ RENDER_PASS_MAIN ] == VK_NULL_HANDLE ) {
			pipeline->handle[ RENDER_PASS_MAIN ] = create_pipeline( &pipeline->def, RENDER_PASS_MAIN, i );
			warmed++;
		}

		if ( vk_depth_fade_uses_depth_resolve() && vk.render_pass.main_load != VK_NULL_HANDLE &&
			pipeline->handle[ RENDER_PASS_MAIN_LOAD ] == VK_NULL_HANDLE ) {
			pipeline->handle[ RENDER_PASS_MAIN_LOAD ] = create_pipeline( &pipeline->def, RENDER_PASS_MAIN_LOAD, i );
			warmed++;
		}

		if ( include_screenmap && vk.render_pass.screenmap != VK_NULL_HANDLE &&
			pipeline->handle[ RENDER_PASS_SCREENMAP ] == VK_NULL_HANDLE ) {
			pipeline->handle[ RENDER_PASS_SCREENMAP ] = create_pipeline( &pipeline->def, RENDER_PASS_SCREENMAP, i );
			warmed++;
		}
	}

	if ( warmed > 0 ) {
		ri.Printf( PRINT_DEVELOPER, "Vulkan: warmed %u pipeline handles in %i msec\n",
			warmed, ri.Milliseconds() - start_msec );
	}
}


static void vk_destroy_image_and_view( VkImage *image, VkImageView *image_view )
{
	if ( image_view != NULL && *image_view != VK_NULL_HANDLE ) {
		qvkDestroyImageView( vk.device, *image_view, NULL );
		*image_view = VK_NULL_HANDLE;
	}

	if ( image != NULL && *image != VK_NULL_HANDLE ) {
		qvkDestroyImage( vk.device, *image, NULL );
		*image = VK_NULL_HANDLE;
	}
}


static void vk_destroy_attachments( void )
{
	uint32_t i;

	for ( i = 0; i < ARRAY_LEN( vk.bloom_image ); i++ ) {
		vk_destroy_image_and_view( &vk.bloom_image[i], &vk.bloom_image_view[i] );
	}

	vk_destroy_image_and_view( &vk.color_image, &vk.color_image_view );
	vk_destroy_image_and_view( &vk.motion_blur_image, &vk.motion_blur_image_view );
	vk_destroy_image_and_view( &vk.liquidSnapshot.color_image,
		&vk.liquidSnapshot.color_image_view );
	vk_destroy_image_and_view( &vk.msaa_image, &vk.msaa_image_view );
	vk_destroy_image_and_view( &vk.depth_image, &vk.depth_image_view );
	vk_destroy_image_and_view( &vk.depth_fade_image, &vk.depth_fade_image_view );
	vk_destroy_image_and_view( &vk.dlight_shadow_image, &vk.dlight_shadow_image_view );
	vk_destroy_image_and_view( &vk.spot_shadow_image, &vk.spot_shadow_image_view );
	vk_destroy_image_and_view( &vk.csm_shadow_image, &vk.csm_shadow_image_view );
	vk_destroy_image_and_view( &vk.screenMap.color_image, &vk.screenMap.color_image_view );
	vk_destroy_image_and_view( &vk.screenMap.color_image_msaa, &vk.screenMap.color_image_view_msaa );
	vk_destroy_image_and_view( &vk.screenMap.depth_image, &vk.screenMap.depth_image_view );
	vk_destroy_image_and_view( &vk.capture.image, &vk.capture.image_view );
	vk_clear_dlight_shadow_atlas_layout();
	vk_clear_spot_shadow_atlas_layout();
	vk_clear_csm_shadow_atlas_layout();

	for ( i = 0; i < vk.image_memory_count; i++ ) {
		vk_free_memory_allocation( &vk.image_memory[i] );
	}

	vk.image_memory_count = 0;
	R_MotionBlur_ResetView( &vk_motion_blur_view );
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

	if ( vk.render_pass.screenmap != VK_NULL_HANDLE ) {
		qvkDestroyRenderPass( vk.device, vk.render_pass.screenmap, NULL );
		vk.render_pass.screenmap = VK_NULL_HANDLE;
	}
	if ( vk.render_pass.liquid_snapshot != VK_NULL_HANDLE ) {
		qvkDestroyRenderPass( vk.device, vk.render_pass.liquid_snapshot, NULL );
		vk.render_pass.liquid_snapshot = VK_NULL_HANDLE;
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
	if ( vk.liquid_snapshot_pipeline != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.liquid_snapshot_pipeline, NULL );
		vk.liquid_snapshot_pipeline = VK_NULL_HANDLE;
	}

	if ( vk.world_outline_pipeline != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.world_outline_pipeline, NULL );
		vk.world_outline_pipeline = VK_NULL_HANDLE;
	}
	if ( vk.global_fog_pipeline != VK_NULL_HANDLE ) {
		qvkDestroyPipeline( vk.device, vk.global_fog_pipeline, NULL );
		vk.global_fog_pipeline = VK_NULL_HANDLE;
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

	if ( qvkQueuePresentKHR == NULL ) { // not fully initialized
		goto __cleanup;
	}

	vk_release_cubemap_capture();
	vk_destroy_framebuffers();

	vk_destroy_pipelines( qtrue ); // reset counter

	vk_destroy_render_passes();

	vk_destroy_attachments();

	vk_destroy_swapchain();

	if ( vk.pipelineCache != VK_NULL_HANDLE ) {
		vk_save_pipeline_cache();
		qvkDestroyPipelineCache( vk.device, vk.pipelineCache, NULL );
		vk.pipelineCache = VK_NULL_HANDLE;
	}

	vk_wait_upload_contexts( __func__ );

	vk_destroy_command_contexts();

	qvkDestroyDescriptorPool(vk.device, vk.descriptor_pool, NULL);

	qvkDestroyDescriptorSetLayout(vk.device, vk.set_layout_sampler, NULL);
	qvkDestroyDescriptorSetLayout(vk.device, vk.set_layout_uniform, NULL);
	qvkDestroyDescriptorSetLayout(vk.device, vk.set_layout_storage, NULL);

	qvkDestroyPipelineLayout(vk.device, vk.pipeline_layout, NULL);
	qvkDestroyPipelineLayout(vk.device, vk.pipeline_layout_storage, NULL);
	qvkDestroyPipelineLayout(vk.device, vk.pipeline_layout_post_process, NULL);
	qvkDestroyPipelineLayout(vk.device, vk.pipeline_layout_blend, NULL);

#ifdef USE_VBO
	vk_release_vbo();
#endif

	vk_clean_staging_buffer();

	vk_release_geometry_buffers();

	vk_destroy_samplers();

	vk_destroy_sync_primitives();

	qvkDestroyBuffer( vk.device, vk.storage.buffer, NULL );
	vk_free_memory_allocation( &vk.storage.allocation );

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

	qvkDestroyShaderModule(vk.device, vk.modules.bloom_fs, NULL);
	qvkDestroyShaderModule(vk.device, vk.modules.blur_fs, NULL);
	qvkDestroyShaderModule(vk.device, vk.modules.blend_fs, NULL);
	qvkDestroyShaderModule(vk.device, vk.modules.motion_blur_fs, NULL);
	qvkDestroyShaderModule(vk.device, vk.modules.world_outline_fs, NULL);
	if ( vk.modules.global_fog_fs != VK_NULL_HANDLE ) {
		qvkDestroyShaderModule( vk.device, vk.modules.global_fog_fs, NULL );
		vk.modules.global_fog_fs = VK_NULL_HANDLE;
	}

	qvkDestroyShaderModule(vk.device, vk.modules.gamma_vs, NULL);
	qvkDestroyShaderModule(vk.device, vk.modules.gamma_fs, NULL);

__cleanup:
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


void vk_release_resources( void ) {
	int i, j;

	vk_wait_idle();

	for (i = 0; i < vk_world.num_image_chunks; i++) {
		vk_free_memory_allocation( &vk_world.image_chunks[i].allocation );
	}

	vk_clean_staging_buffer();

	// vk_destroy_samplers();

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
	}
#if 0 // do not reduce chunk size
	else if ( vk_world.num_image_chunks == 1 ) {
		// otherwise set to default if used less than a half
		if ( vk_world.image_chunks[0].used < ( IMAGE_CHUNK_SIZE - (IMAGE_CHUNK_SIZE / 10) ) ) {
			vk.image_chunk_size = IMAGE_CHUNK_SIZE;
		}
	}
#endif

	Com_Memset( &vk_world, 0, sizeof( vk_world ) );

	// Reset geometry buffers offsets
	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		vk.tess[i].uniform_read_offset = 0;
		vk.tess[i].vertex_buffer_offset = 0;
	}

	Com_Memset( vk.cmd->buf_offset, 0, sizeof( vk.cmd->buf_offset ) );
	Com_Memset( vk.cmd->vbo_offset, 0, sizeof( vk.cmd->vbo_offset ) );

	vk_reset_transient_stats();
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
		desc.pSetLayouts = &vk.set_layout_sampler;

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

	VkCommandBuffer   command_buffer;
	VkBufferImageCopy regions[16];
	VkBufferImageCopy region;
	byte *buf;
	int n;

	int num_regions = 0;
	int buffer_size = 0;

	buf = resample_image_data( image->internalFormat, pixels, size, &n /*bpp*/ );

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

		buffer_size += width * height * n;

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
	if ( vk_wait_staging_buffer() ) {
		// wait for vkQueueSubmit() completion before new upload
	}

	if ( vk.staging_buffer.size - vk.staging_buffer.offset < buffer_size ) {
		// try to flush staging buffer and reset offset
		vk_flush_staging_buffer( qfalse );
	}

	if ( vk.staging_buffer.size /* - vk_world.staging_buffer_offset */ < buffer_size ) {
		// if still not enough - reallocate staging buffer
		vk_alloc_staging_buffer( buffer_size );
	}

	for ( n = 0; n < num_regions; n++ ) {
		regions[n].bufferOffset += vk.staging_buffer.offset;
	}

	Com_Memcpy( vk.staging_buffer.ptr + vk.staging_buffer.offset, buf, buffer_size );

	if ( vk.staging_buffer.offset == 0 ) {
		VkCommandBufferBeginInfo begin_info;
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.pNext = NULL;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		begin_info.pInheritanceInfo = NULL;
		VK_CHECK( qvkBeginCommandBuffer( vk.staging_command_buffer, &begin_info ) );
	}

	//ri.Printf( PRINT_WARNING, "batch @%6i + %i %s \n", (int)vk_world.staging_buffer_offset, (int)buffer_size, image->imgName );
	vk.staging_buffer.offset += buffer_size;

	command_buffer = vk.staging_command_buffer;

	if ( update ) {
		record_image_layout_transition( command_buffer, image->handle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, 0 );
	} else {
		record_image_layout_transition( command_buffer, image->handle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_HOST_BIT, 0 );
	}

	qvkCmdCopyBufferToImage( command_buffer, vk.staging_buffer.handle, image->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, num_regions, regions );

	// final transition after upload comleted
	record_image_layout_transition( command_buffer, image->handle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 0 );
#else
	if ( vk.staging_buffer.size < buffer_size ) {
		vk_alloc_staging_buffer( buffer_size );
	}

	Com_Memcpy( vk.staging_buffer.ptr, buf, buffer_size );

	command_buffer = begin_command_buffer();
	// record_buffer_memory_barrier( command_buffer, vk_world.staging_buffer, VK_WHOLE_SIZE, 0, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT );
	if ( update ) {
		record_image_layout_transition( command_buffer, image->handle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, 0 );
	} else {
		record_image_layout_transition( command_buffer, image->handle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_HOST_BIT, 0 );
	}
	qvkCmdCopyBufferToImage( command_buffer, vk.staging_buffer.handle, image->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, num_regions, regions );
	record_image_layout_transition( command_buffer, image->handle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 0 );
	end_command_buffer( command_buffer, __func__ );
#endif

	if ( buf != pixels ) {
		ri.Hunk_FreeTempMemory( buf );
	}
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

	vk_update_descriptor_sets( 1, &descriptor_write );
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
	VkSpecializationMapEntry spec_entries[46];
	VkSpecializationInfo frag_spec_info;
	VkPipeline *pipeline;
	VkShaderModule fsmodule;
	VkRenderPass renderpass;
	VkPipelineLayout layout;
	VkSampleCountFlagBits samples;
	const char *pipeline_name;
	qboolean blend;
	qboolean optional = qfalse;
	VkResult createResult;

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
		int cubemap_capture_mode;
	} frag_spec_data;

	switch ( program_index ) {
		case 1: // bloom extraction
			pipeline = &vk.bloom_extract_pipeline;
			fsmodule = vk.modules.bloom_fs;
			renderpass = vk.render_pass.bloom_extract;
			layout = vk.pipeline_layout_post_process;
			samples = VK_SAMPLE_COUNT_1_BIT;
			pipeline_name = "bloom extraction pipeline";
			blend = qfalse;
			break;
		case 2: // final bloom blend
			pipeline = &vk.bloom_blend_pipeline;
			fsmodule = vk.modules.blend_fs;
			renderpass = vk.render_pass.post_bloom;
			layout = vk.pipeline_layout_blend;
			samples = vkSamples;
			pipeline_name = "bloom blend pipeline";
			blend = qtrue;
			break;
		case 4: // final bloom blend, preserving dark cel-outline pixels
			pipeline = &vk.bloom_blend_cel_pipeline;
			fsmodule = vk.modules.blend_fs;
			renderpass = vk.render_pass.post_bloom;
			layout = vk.pipeline_layout_blend;
			samples = vkSamples;
			pipeline_name = "bloom blend cel-outline pipeline";
			blend = qtrue;
			break;
		case 6: // camera-motion blur into the scratch attachment
			pipeline = &vk.motion_blur_pipeline;
			fsmodule = vk.modules.motion_blur_fs;
			renderpass = vk.render_pass.motion_blur;
			layout = vk.pipeline_layout_post_process;
			samples = VK_SAMPLE_COUNT_1_BIT;
			pipeline_name = "motion blur scene pipeline";
			blend = qfalse;
			break;
		case 7: // copy the blurred scratch image back to the main scene target
			pipeline = &vk.motion_blur_copy_pipeline;
			fsmodule = vk.modules.motion_blur_fs;
			renderpass = vk.render_pass.main_load;
			layout = vk.pipeline_layout_post_process;
			samples = vkSamples;
			pipeline_name = "motion blur scene copy pipeline";
			blend = qfalse;
			break;
		case 8: // crop and transform a square cubemap face into the readback scratch target
			pipeline = &vk.cubemap_capture.pipeline;
			fsmodule = vk.modules.gamma_fs;
			renderpass = vk.render_pass.capture;
			layout = vk.pipeline_layout_post_process;
			samples = VK_SAMPLE_COUNT_1_BIT;
			pipeline_name = "cubemap capture pipeline";
			blend = qfalse;
			break;
		case 9: // copy the stable pre-fog scene into the scaled liquid snapshot
			pipeline = &vk.liquid_snapshot_pipeline;
			fsmodule = vk.modules.liquid_copy_fs;
			renderpass = vk.render_pass.liquid_snapshot;
			layout = vk.pipeline_layout_post_process;
			samples = VK_SAMPLE_COUNT_1_BIT;
			pipeline_name = "liquid scene snapshot pipeline";
			blend = qfalse;
			break;
		case 5: // world cel depth edge overlay
			pipeline = &vk.world_outline_pipeline;
			fsmodule = vk.modules.world_outline_fs;
			renderpass = vk.render_pass.main_load;
			layout = vk.pipeline_layout_post_process;
			samples = vkSamples;
			pipeline_name = "world cel depth-outline pipeline";
			blend = qtrue;
			break;
		case 10: // depth-aware global fog overlay
			pipeline = &vk.global_fog_pipeline;
			fsmodule = vk.modules.global_fog_fs;
			renderpass = vk.render_pass.main_load;
			layout = vk.pipeline_layout_post_process;
			samples = vkSamples;
			pipeline_name = "global fog overlay pipeline";
			blend = qtrue;
			optional = qtrue;
			break;
		case 3: // capture buffer extraction
			pipeline = &vk.capture_pipeline;
			fsmodule = vk.modules.gamma_fs;
			renderpass = vk.render_pass.capture;
			layout = vk.pipeline_layout_post_process;
			samples = VK_SAMPLE_COUNT_1_BIT;
			pipeline_name = "capture buffer pipeline";
			blend = qfalse;
			break;
		default: // gamma correction
			pipeline = &vk.gamma_pipeline;
			fsmodule = vk.modules.gamma_fs;
			renderpass = vk.render_pass.gamma;
			layout = vk.pipeline_layout_post_process;
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
	frag_spec_data.output_color_space = ( program_index == 0 && vk.hdrDisplayActive ) ? VK_POST_COLOR_SPACE_HDR10_ST2084 : VK_POST_COLOR_SPACE_SDR;
	frag_spec_data.hdr_paper_white = Com_Clamp( 80.0f, 500.0f, r_hdrDisplayPaperWhite->value );
	frag_spec_data.hdr_max_luminance = Com_Clamp( frag_spec_data.hdr_paper_white, 10000.0f, r_hdrDisplayMaxLuminance->value );
	if ( vk.hdrDisplayActive && vk.displayOutput.maxLuminanceNits >= frag_spec_data.hdr_paper_white ) {
		frag_spec_data.hdr_max_luminance = Com_Clamp( frag_spec_data.hdr_paper_white,
			frag_spec_data.hdr_max_luminance, vk.displayOutput.maxLuminanceNits );
	}
	frag_spec_data.scene_linear_mode = ( r_hdr && r_hdr->integer > 0 ) ? 1 : 0;
	frag_spec_data.tonemap_mode = ( frag_spec_data.scene_linear_mode &&
		( program_index == 0 || program_index == 1 || program_index == 3 || program_index == 8 ) ) ?
		r_tonemap->integer : 0;
	frag_spec_data.tonemap_exposure = Com_Clamp( 0.1f, 8.0f, r_tonemapExposure->value );
	frag_spec_data.bloom_soft_knee = Com_Clamp( 0.0f, 1.0f, r_bloom_soft_knee->value );
	{
		const int color_grade_mode = vk_color_grade_mode();
		const qboolean use_lgg = vk_color_grade_uses_lgg( color_grade_mode );
		const qboolean use_lut = vk_color_grade_uses_lut( color_grade_mode );
		const float sourceWhitePoint = ( r_colorGradeWhitePoint ) ?
			Com_Clamp( 1000.0f, 40000.0f, r_colorGradeWhitePoint->value ) : 6504.0f;
		const float targetWhitePoint = ( r_colorGradeAdaptWhitePoint ) ?
			Com_Clamp( 1000.0f, 40000.0f, r_colorGradeAdaptWhitePoint->value ) : 6504.0f;
		int lutSize = 0;

		frag_spec_data.color_grade_mode = color_grade_mode;
		frag_spec_data.color_grade_lut_scale = ( r_colorGradeLUTScale ) ?
			Com_Clamp( 1.0f, 32.0f, r_colorGradeLUTScale->value ) : 4.0f;

		if ( use_lgg ) {
			vk_parse_vec3_cvar( r_colorGradeLift, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, frag_spec_data.grade_lift );
			vk_parse_vec3_cvar( r_colorGradeGamma, 1.0f, 1.0f, 1.0f, 0.1f, 8.0f, frag_spec_data.grade_gamma );
			vk_parse_vec3_cvar( r_colorGradeGain, 1.0f, 1.0f, 1.0f, 0.0f, 8.0f, frag_spec_data.grade_gain );
			vk_build_bradford_adaptation( sourceWhitePoint, targetWhitePoint, frag_spec_data.white_point );
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
			vk_color_grade_lut_image( &lutSize );
		}
		frag_spec_data.color_grade_lut_size = use_lut ? lutSize : 0;
	}
	frag_spec_data.crt_amount = r_crtAmount ? Com_Clamp( 0.0f, 1.0f, r_crtAmount->value ) : 1.0f;
	frag_spec_data.crt_scanline_strength = r_crtScanlineStrength ?
		Com_Clamp( 0.0f, 1.0f, r_crtScanlineStrength->value ) : 0.55f;
	frag_spec_data.crt_mask_strength = r_crtMaskStrength ?
		Com_Clamp( 0.0f, 1.0f, r_crtMaskStrength->value ) : 0.35f;
	frag_spec_data.crt_curvature = r_crtCurvature ?
		Com_Clamp( 0.0f, 0.25f, r_crtCurvature->value ) : 0.01f;
	frag_spec_data.crt_chromatic = r_crtChromatic ?
		Com_Clamp( 0.0f, 8.0f, r_crtChromatic->value ) : 1.35f;
	frag_spec_data.crt_mode = ( r_crt && r_crt->integer && frag_spec_data.crt_amount > 0.001f &&
		( program_index == 0 || program_index == 3 ) ) ? 1 : 0;
	frag_spec_data.cubemap_capture_mode = ( program_index == 8 ) ? 1 : 0;

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

	spec_entries[11].constantID = 11;
	spec_entries[11].offset = offsetof( struct FragSpecData, output_color_space );
	spec_entries[11].size = sizeof( frag_spec_data.output_color_space );

	spec_entries[12].constantID = 12;
	spec_entries[12].offset = offsetof( struct FragSpecData, hdr_paper_white );
	spec_entries[12].size = sizeof( frag_spec_data.hdr_paper_white );

	spec_entries[13].constantID = 13;
	spec_entries[13].offset = offsetof( struct FragSpecData, hdr_max_luminance );
	spec_entries[13].size = sizeof( frag_spec_data.hdr_max_luminance );

	spec_entries[14].constantID = 14;
	spec_entries[14].offset = offsetof( struct FragSpecData, tonemap_mode );
	spec_entries[14].size = sizeof( frag_spec_data.tonemap_mode );

	spec_entries[15].constantID = 15;
	spec_entries[15].offset = offsetof( struct FragSpecData, tonemap_exposure );
	spec_entries[15].size = sizeof( frag_spec_data.tonemap_exposure );

	spec_entries[16].constantID = 16;
	spec_entries[16].offset = offsetof( struct FragSpecData, bloom_soft_knee );
	spec_entries[16].size = sizeof( frag_spec_data.bloom_soft_knee );

	spec_entries[17].constantID = 17;
	spec_entries[17].offset = offsetof( struct FragSpecData, scene_linear_mode );
	spec_entries[17].size = sizeof( frag_spec_data.scene_linear_mode );

	spec_entries[18].constantID = 18;
	spec_entries[18].offset = offsetof( struct FragSpecData, color_grade_mode );
	spec_entries[18].size = sizeof( frag_spec_data.color_grade_mode );

	spec_entries[19].constantID = 19;
	spec_entries[19].offset = offsetof( struct FragSpecData, grade_lift ) + sizeof( frag_spec_data.grade_lift[0] ) * 0;
	spec_entries[19].size = sizeof( frag_spec_data.grade_lift[0] );

	spec_entries[20].constantID = 20;
	spec_entries[20].offset = offsetof( struct FragSpecData, grade_lift ) + sizeof( frag_spec_data.grade_lift[0] ) * 1;
	spec_entries[20].size = sizeof( frag_spec_data.grade_lift[0] );

	spec_entries[21].constantID = 21;
	spec_entries[21].offset = offsetof( struct FragSpecData, grade_lift ) + sizeof( frag_spec_data.grade_lift[0] ) * 2;
	spec_entries[21].size = sizeof( frag_spec_data.grade_lift[0] );

	spec_entries[22].constantID = 22;
	spec_entries[22].offset = offsetof( struct FragSpecData, grade_gamma ) + sizeof( frag_spec_data.grade_gamma[0] ) * 0;
	spec_entries[22].size = sizeof( frag_spec_data.grade_gamma[0] );

	spec_entries[23].constantID = 23;
	spec_entries[23].offset = offsetof( struct FragSpecData, grade_gamma ) + sizeof( frag_spec_data.grade_gamma[0] ) * 1;
	spec_entries[23].size = sizeof( frag_spec_data.grade_gamma[0] );

	spec_entries[24].constantID = 24;
	spec_entries[24].offset = offsetof( struct FragSpecData, grade_gamma ) + sizeof( frag_spec_data.grade_gamma[0] ) * 2;
	spec_entries[24].size = sizeof( frag_spec_data.grade_gamma[0] );

	spec_entries[25].constantID = 25;
	spec_entries[25].offset = offsetof( struct FragSpecData, grade_gain ) + sizeof( frag_spec_data.grade_gain[0] ) * 0;
	spec_entries[25].size = sizeof( frag_spec_data.grade_gain[0] );

	spec_entries[26].constantID = 26;
	spec_entries[26].offset = offsetof( struct FragSpecData, grade_gain ) + sizeof( frag_spec_data.grade_gain[0] ) * 1;
	spec_entries[26].size = sizeof( frag_spec_data.grade_gain[0] );

	spec_entries[27].constantID = 27;
	spec_entries[27].offset = offsetof( struct FragSpecData, grade_gain ) + sizeof( frag_spec_data.grade_gain[0] ) * 2;
	spec_entries[27].size = sizeof( frag_spec_data.grade_gain[0] );

	spec_entries[28].constantID = 28;
	spec_entries[28].offset = offsetof( struct FragSpecData, white_point ) + sizeof( frag_spec_data.white_point[0] ) * 0;
	spec_entries[28].size = sizeof( frag_spec_data.white_point[0] );

	spec_entries[29].constantID = 29;
	spec_entries[29].offset = offsetof( struct FragSpecData, white_point ) + sizeof( frag_spec_data.white_point[0] ) * 1;
	spec_entries[29].size = sizeof( frag_spec_data.white_point[0] );

	spec_entries[30].constantID = 30;
	spec_entries[30].offset = offsetof( struct FragSpecData, white_point ) + sizeof( frag_spec_data.white_point[0] ) * 2;
	spec_entries[30].size = sizeof( frag_spec_data.white_point[0] );

	spec_entries[31].constantID = 31;
	spec_entries[31].offset = offsetof( struct FragSpecData, white_point ) + sizeof( frag_spec_data.white_point[0] ) * 3;
	spec_entries[31].size = sizeof( frag_spec_data.white_point[0] );

	spec_entries[32].constantID = 32;
	spec_entries[32].offset = offsetof( struct FragSpecData, white_point ) + sizeof( frag_spec_data.white_point[0] ) * 4;
	spec_entries[32].size = sizeof( frag_spec_data.white_point[0] );

	spec_entries[33].constantID = 33;
	spec_entries[33].offset = offsetof( struct FragSpecData, white_point ) + sizeof( frag_spec_data.white_point[0] ) * 5;
	spec_entries[33].size = sizeof( frag_spec_data.white_point[0] );

	spec_entries[34].constantID = 34;
	spec_entries[34].offset = offsetof( struct FragSpecData, white_point ) + sizeof( frag_spec_data.white_point[0] ) * 6;
	spec_entries[34].size = sizeof( frag_spec_data.white_point[0] );

	spec_entries[35].constantID = 35;
	spec_entries[35].offset = offsetof( struct FragSpecData, white_point ) + sizeof( frag_spec_data.white_point[0] ) * 7;
	spec_entries[35].size = sizeof( frag_spec_data.white_point[0] );

	spec_entries[36].constantID = 36;
	spec_entries[36].offset = offsetof( struct FragSpecData, white_point ) + sizeof( frag_spec_data.white_point[0] ) * 8;
	spec_entries[36].size = sizeof( frag_spec_data.white_point[0] );

	spec_entries[37].constantID = 37;
	spec_entries[37].offset = offsetof( struct FragSpecData, color_grade_lut_size );
	spec_entries[37].size = sizeof( frag_spec_data.color_grade_lut_size );

	spec_entries[38].constantID = 38;
	spec_entries[38].offset = offsetof( struct FragSpecData, color_grade_lut_scale );
	spec_entries[38].size = sizeof( frag_spec_data.color_grade_lut_scale );

	spec_entries[39].constantID = 39;
	spec_entries[39].offset = offsetof( struct FragSpecData, crt_mode );
	spec_entries[39].size = sizeof( frag_spec_data.crt_mode );

	spec_entries[40].constantID = 40;
	spec_entries[40].offset = offsetof( struct FragSpecData, crt_amount );
	spec_entries[40].size = sizeof( frag_spec_data.crt_amount );

	spec_entries[41].constantID = 41;
	spec_entries[41].offset = offsetof( struct FragSpecData, crt_scanline_strength );
	spec_entries[41].size = sizeof( frag_spec_data.crt_scanline_strength );

	spec_entries[42].constantID = 42;
	spec_entries[42].offset = offsetof( struct FragSpecData, crt_mask_strength );
	spec_entries[42].size = sizeof( frag_spec_data.crt_mask_strength );

	spec_entries[43].constantID = 43;
	spec_entries[43].offset = offsetof( struct FragSpecData, crt_curvature );
	spec_entries[43].size = sizeof( frag_spec_data.crt_curvature );

	spec_entries[44].constantID = 44;
	spec_entries[44].offset = offsetof( struct FragSpecData, crt_chromatic );
	spec_entries[44].size = sizeof( frag_spec_data.crt_chromatic );

	spec_entries[45].constantID = 45;
	spec_entries[45].offset = offsetof( struct FragSpecData, cubemap_capture_mode );
	spec_entries[45].size = sizeof( frag_spec_data.cubemap_capture_mode );

	frag_spec_info.mapEntryCount = ARRAY_LEN( spec_entries );
	frag_spec_info.pMapEntries = spec_entries;
	frag_spec_info.dataSize = sizeof( frag_spec_data );
	frag_spec_info.pData = &frag_spec_data;

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
		if ( program_index == 5 || program_index == 10 ) {
			attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			attachment_blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			attachment_blend_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			attachment_blend_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		} else {
			attachment_blend_state.srcColorBlendFactor = ( program_index == 4 ) ?
				VK_BLEND_FACTOR_DST_COLOR : VK_BLEND_FACTOR_ONE;
			attachment_blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
			attachment_blend_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			attachment_blend_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		}
		attachment_blend_state.colorBlendOp = VK_BLEND_OP_ADD;
		attachment_blend_state.alphaBlendOp = VK_BLEND_OP_ADD;
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

	if ( optional ) {
		createResult = qvkCreateGraphicsPipelines( vk.device, vk.pipelineCache, 1,
			&create_info, NULL, pipeline );
		if ( createResult != VK_SUCCESS || *pipeline == VK_NULL_HANDLE ) {
			if ( *pipeline != VK_NULL_HANDLE ) {
				qvkDestroyPipeline( vk.device, *pipeline, NULL );
				*pipeline = VK_NULL_HANDLE;
			}
			ri.Printf( PRINT_WARNING,
				"WARNING: Vulkan optional global fog pipeline failed (%s); feature disabled\n",
				vk_result_string( createResult ) );
			return;
		}
	} else {
		VK_CHECK( qvkCreateGraphicsPipelines( vk.device, vk.pipelineCache, 1,
			&create_info, NULL, pipeline ) );
	}

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
	create_info.layout = vk.pipeline_layout_post_process; // one input attachment
	create_info.renderPass = vk.render_pass.blur[ index ];
	create_info.subpass = 0;
	create_info.basePipelineHandle = VK_NULL_HANDLE;
	create_info.basePipelineIndex = -1;

	VK_CHECK( qvkCreateGraphicsPipelines( vk.device, vk.pipelineCache, 1, &create_info, NULL, pipeline ) );

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
	VkDynamicState dynamic_state_array[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS };
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

	if ( r_ext_alpha_to_coverage && r_ext_alpha_to_coverage->integer &&
		vkSamples != VK_SAMPLE_COUNT_1_BIT && frag_spec_data[0].i ) {
		frag_spec_data[3].i = 1;
		alphaToCoverage = VK_TRUE;
	}

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
	frag_spec_data[11].i = def->depth_fade ? 1 : 0;

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

		case TYPE_LIQUID:
			push_bind( 0, sizeof( vec4_t ) );
			push_bind( 5, sizeof( vec4_t ) );
			push_attr( 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT );
			push_attr( 5, 5, VK_FORMAT_R32G32B32A32_SFLOAT );
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
		multisample_state.rasterizationSamples = (renderPassIndex == RENDER_PASS_SCREENMAP) ? vk.screenMapSamples : vkSamples;
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
		depth_stencil_state.depthCompareOp = (state_bits & GLS_DEPTHFUNC_EQUAL) ? VK_COMPARE_OP_EQUAL : VK_COMPARE_OP_LESS_OR_EQUAL;
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
		/* Do not perturb destination alpha: later authored stages may use it as
		 * a blend factor even though the liquid enhancement only changes RGB. */
		attachment_blend_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
			VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
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
	dynamic_state.dynamicStateCount = ( renderPassIndex == RENDER_PASS_DLIGHT_SHADOW ||
		renderPassIndex == RENDER_PASS_SPOT_SHADOW ||
		renderPassIndex == RENDER_PASS_CSM_SHADOW ) ? ARRAY_LEN( dynamic_state_array ) : 2;
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
		create_info.layout = vk.pipeline_layout_storage;
	else
		create_info.layout = vk.pipeline_layout;

	if ( renderPassIndex == RENDER_PASS_DLIGHT_SHADOW ||
		renderPassIndex == RENDER_PASS_SPOT_SHADOW ||
		renderPassIndex == RENDER_PASS_CSM_SHADOW )
		create_info.renderPass = vk.render_pass.dlight_shadow;
	else if ( renderPassIndex == RENDER_PASS_SCREENMAP )
		create_info.renderPass = vk.render_pass.screenmap;
	else if ( renderPassIndex == RENDER_PASS_MAIN_LOAD )
		create_info.renderPass = vk.render_pass.main_load;
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
		const renderPass_t pass = vk_pipeline_render_pass_index();
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

	if ( !backEnd.projection2D && R_ViewPassUsesScissor( &backEnd.viewParms ) )
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

		if (r->offset.x + r->extent.width > vk.renderWidth)
			r->extent.width = vk.renderWidth - r->offset.x;
		if (r->offset.y + r->extent.height > vk.renderHeight)
			r->extent.height = vk.renderHeight - r->offset.y;
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


/* Model-space to clip-space transform for the active draw, in the same
 * Vulkan clip-Y convention the liquid vertex shader receives through push
 * constants. The liquid fragment shader re-projects reflected proxy points
 * with it, so both must stay in lockstep. */
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

	qvkCmdPushConstants( vk.cmd->command_buffer, vk.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof( push_constants ), push_constants );

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


static void vk_dirty_descriptor_range( int start, int end )
{
	if ( start < 0 || end < start || end >= VK_DESC_COUNT ) {
		ri.Error( ERR_FATAL, "Vulkan: invalid descriptor range %i..%i", start, end );
	}

	vk.cmd->descriptor_set.start = ( (uint32_t)start < vk.cmd->descriptor_set.start ) ? (uint32_t)start : vk.cmd->descriptor_set.start;
	vk.cmd->descriptor_set.end = ( (uint32_t)end > vk.cmd->descriptor_set.end ) ? (uint32_t)end : vk.cmd->descriptor_set.end;
}


void vk_update_descriptor( int index, VkDescriptorSet descriptor )
{
	if ( vk.cmd->descriptor_set.current[ index ] != descriptor ) {
		vk_dirty_descriptor_range( index, index );
	}
	vk.cmd->descriptor_set.current[ index ] = descriptor;
}


void vk_update_descriptor_offset( int index, uint32_t offset )
{
	if ( index != VK_DESC_UNIFORM ) {
		ri.Error( ERR_FATAL, "Vulkan: descriptor set %i does not have a dynamic offset", index );
	}
	if ( vk.cmd->descriptor_set.offset[ index ] != offset ) {
		vk_dirty_descriptor_range( index, index );
	}
	vk.cmd->descriptor_set.offset[ index ] = offset;
}


void vk_material_init( vk_material_t *material )
{
	Com_Memset( material, 0, sizeof( *material ) );
}


void vk_material_set_descriptor( vk_material_t *material, int index, VkDescriptorSet descriptor )
{
	if ( index < VK_DESC_TEXTURE_BASE || index >= VK_DESC_COUNT ) {
		ri.Error( ERR_FATAL, "Vulkan: invalid material descriptor index %i", index );
	}

	material->descriptor[ index ] = descriptor;
	material->descriptor_mask |= VK_DESCRIPTOR_MASK( index );
}


void vk_bind_material( const vk_material_t *material )
{
	int i;
	int start;
	int end;
	qboolean dirty;

	if ( material == NULL || material->descriptor_mask == 0 ) {
		return;
	}

	start = VK_DESC_COUNT;
	end = -1;
	dirty = qfalse;

	for ( i = VK_DESC_TEXTURE_BASE; i < VK_DESC_COUNT; i++ ) {
		VkDescriptorSet descriptor;

		if ( ( material->descriptor_mask & VK_DESCRIPTOR_MASK( i ) ) == 0 ) {
			continue;
		}

		descriptor = material->descriptor[ i ];
		if ( descriptor == VK_NULL_HANDLE ) {
			descriptor = tr.whiteImage->descriptor;
		}

		if ( vk.cmd->descriptor_set.current[ i ] != descriptor ) {
			dirty = qtrue;
		}

		vk.cmd->descriptor_set.current[ i ] = descriptor;
		start = MIN( start, i );
		end = MAX( end, i );
	}

	if ( dirty ) {
		vk_dirty_descriptor_range( start, end );
		vk.stats.material_descriptor_misses++;
	} else {
		vk.stats.material_descriptor_hits++;
	}
}


void vk_bind_descriptor_sets( void )
{
	uint32_t offsets[2], offset_count;
	uint32_t start, end, count, i;

	start = vk.cmd->descriptor_set.start;
	if ( start == ~0U )
		return;

	end = vk.cmd->descriptor_set.end;

	offset_count = 0;
	if ( /*start == VK_DESC_STORAGE || */ start == VK_DESC_UNIFORM ) { // uniform offset or storage offset
		offsets[ offset_count++ ] = vk.cmd->descriptor_set.offset[ start ];
	}

	count = end - start + 1;

	/* Fill NULL descriptor gaps, including the range end: full-range rebinds
	 * after mid-frame post-process passes can cover sets (texture2, fog
	 * collapse) that no material has used yet this frame, and one NULL handle
	 * would invalidate the entire bind. */
	for ( i = start + 1; i <= end; i++ ) {
		if ( vk.cmd->descriptor_set.current[i] == VK_NULL_HANDLE ) {
			vk.cmd->descriptor_set.current[i] = tr.whiteImage->descriptor;
		}
	}

	qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_layout, start, count, vk.cmd->descriptor_set.current + start, offset_count, offsets );
	vk.stats.descriptor_bind_calls++;
	vk.stats.descriptor_bind_sets += count;

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

	depthBias = r_dlightShadowCasterDepthBias ? r_dlightShadowCasterDepthBias->value : 1.0f;
	slopeBias = r_dlightShadowCasterSlopeBias ? r_dlightShadowCasterSlopeBias->value : 1.0f;
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

	qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_layout_storage, VK_DESC_STORAGE, 1, &vk.storage.descriptor, 1, &storage_offset );

	// configure pipeline's dynamic state
	vk_update_depth_range( DEPTH_RANGE_NORMAL );

	qvkCmdDraw( vk.cmd->command_buffer, tess.numVertexes, 1, 0, 0 );
}


static const char *vk_render_pass_label( VkRenderPass renderPass )
{
	uint32_t i;

	if ( renderPass == vk.render_pass.main ) {
		return "main render pass";
	}
	if ( renderPass == vk.render_pass.main_load ) {
		return "main load render pass";
	}
	if ( renderPass == vk.render_pass.screenmap ) {
		return "screenmap render pass";
	}
	if ( renderPass == vk.render_pass.liquid_snapshot ) {
		return "liquid snapshot render pass";
	}
	if ( renderPass == vk.render_pass.gamma ) {
		return "gamma render pass";
	}
	if ( renderPass == vk.render_pass.capture ) {
		return "capture render pass";
	}
	if ( renderPass == vk.render_pass.motion_blur ) {
		return "motion blur render pass";
	}
	if ( renderPass == vk.render_pass.bloom_extract ) {
		return "bloom extract render pass";
	}
	if ( renderPass == vk.render_pass.post_bloom ) {
		return "post-bloom render pass";
	}
	if ( renderPass == vk.render_pass.dlight_shadow ) {
		if ( vk.renderPassIndex == RENDER_PASS_CSM_SHADOW ) {
			return "csm shadow atlas render pass";
		}
		if ( vk.renderPassIndex == RENDER_PASS_SPOT_SHADOW ) {
			return "spot shadow atlas render pass";
		}
		return "dlight shadow atlas render pass";
	}

	for ( i = 0; i < ARRAY_LEN( vk.render_pass.blur ); i++ ) {
		if ( renderPass == vk.render_pass.blur[i] ) {
			return "bloom blur render pass";
		}
	}

	return "render pass";
}


static void vk_begin_render_pass( VkRenderPass renderPass, VkFramebuffer frameBuffer, qboolean clearValues, uint32_t width, uint32_t height )
{
	const char *label;
	VkRenderPassBeginInfo render_pass_begin_info;
	VkClearValue clear_values[3];

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
		if ( renderPass == vk.render_pass.dlight_shadow ) {
			if ( vk.renderPassIndex == RENDER_PASS_CSM_SHADOW ) {
				clear_values[0].depthStencil.depth = 1.0f;
			} else {
#ifndef USE_REVERSED_DEPTH
				clear_values[0].depthStencil.depth = 1.0f;
#endif
			}
			render_pass_begin_info.clearValueCount = 1;
			render_pass_begin_info.pClearValues = clear_values;
			vk_world.dirty_depth_attachment = 0;
		} else {
#ifndef USE_REVERSED_DEPTH
			clear_values[1].depthStencil.depth = 1.0;
#endif
			if ( renderPass == vk.render_pass.screenmap ) {
				render_pass_begin_info.clearValueCount =
					vk.screenMapSamples > VK_SAMPLE_COUNT_1_BIT ? 3 : 2;
			} else {
				render_pass_begin_info.clearValueCount = vk.msaaActive ? 3 : 2;
			}
			render_pass_begin_info.pClearValues = clear_values;

			vk_world.dirty_depth_attachment = 0;
		}
	} else {
		render_pass_begin_info.clearValueCount = 0;
		render_pass_begin_info.pClearValues = NULL;
	}

	label = vk_render_pass_label( renderPass );
	Q_strncpyz( vk_current_render_pass_label, label, sizeof( vk_current_render_pass_label ) );
	vk_current_render_pass = renderPass;
	vk_write_timestamp( va( "%s begin", label ), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT );
	vk_begin_debug_label( vk.cmd->command_buffer, label, 0.25f, 0.55f, 0.95f, 1.0f );
	qvkCmdBeginRenderPass( vk.cmd->command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE );

	vk.cmd->last_pipeline = VK_NULL_HANDLE;
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;
}


void vk_begin_main_render_pass( void )
{
	VkFramebuffer frameBuffer = vk.framebuffers.main[ vk.cmd->swapchain_image_index ];

	vk.renderPassIndex = RENDER_PASS_MAIN;
	vk.depth_fade_copied = qfalse;
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
	VkFramebuffer frameBuffer = vk.framebuffers.main_load[ vk.cmd->swapchain_image_index ];

	vk.renderPassIndex = RENDER_PASS_MAIN;

	vk.renderWidth = glConfig.vidWidth;
	vk.renderHeight = glConfig.vidHeight;

	vk.renderScaleX = vk.renderScaleY = 1.0f;

	vk_begin_render_pass( vk.render_pass.main_load, frameBuffer, qfalse, vk.renderWidth, vk.renderHeight );
}


void vk_begin_post_bloom_render_pass( void )
{
	VkFramebuffer frameBuffer = vk_depth_fade_uses_depth_resolve() ?
		vk.framebuffers.main_load[ vk.cmd->swapchain_image_index ] :
		vk.framebuffers.main[ vk.cmd->swapchain_image_index ];

	vk.renderPassIndex = RENDER_PASS_POST_BLOOM;

	vk.renderWidth = glConfig.vidWidth;
	vk.renderHeight = glConfig.vidHeight;

	//vk.renderScaleX = (float)vk.renderWidth / (float)glConfig.vidWidth;
	//vk.renderScaleY = (float)vk.renderHeight / (float)glConfig.vidHeight;
	vk.renderScaleX = vk.renderScaleY = 1.0f;

	vk_begin_render_pass( vk.render_pass.post_bloom, frameBuffer, qfalse, vk.renderWidth, vk.renderHeight );
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


static void vk_begin_liquid_snapshot_render_pass( void )
{
	vk.renderPassIndex = RENDER_PASS_LIQUID_SNAPSHOT;
	vk.renderWidth = vk.liquidSnapshotWidth;
	vk.renderHeight = vk.liquidSnapshotHeight;
	vk.renderScaleX = (float)vk.renderWidth / (float)glConfig.vidWidth;
	vk.renderScaleY = (float)vk.renderHeight / (float)glConfig.vidHeight;
	vk_begin_render_pass( vk.render_pass.liquid_snapshot,
		vk.framebuffers.liquid_snapshot, qfalse, vk.renderWidth, vk.renderHeight );
}


void vk_end_render_pass( void )
{
	qboolean depthFadeResolved;

	depthFadeResolved = ( vk_current_render_pass == vk.render_pass.main &&
		vk_depth_fade_uses_depth_resolve() ) ? qtrue : qfalse;

	qvkCmdEndRenderPass( vk.cmd->command_buffer );
	vk_write_timestamp( va( "%s end", vk_current_render_pass_label[0] ? vk_current_render_pass_label : "render pass" ), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT );
	vk_end_debug_label( vk.cmd->command_buffer );
	if ( depthFadeResolved ) {
		vk.depth_fade_copied = qtrue;
	}
	vk_current_render_pass = VK_NULL_HANDLE;
	vk_current_render_pass_label[0] = '\0';

//	vk.renderPassIndex = RENDER_PASS_MAIN;
}


qboolean vk_capture_liquid_scene( void )
{
	VkMemoryBarrier barrier;

	if ( backEnd.liquidScreenMapDone ) {
		return qtrue;
	}
	if ( !r_liquid || !r_liquid->integer ||
		!vk.fboActive || vk.liquid_snapshot_pipeline == VK_NULL_HANDLE ||
		vk.liquidSnapshot.source_descriptor == VK_NULL_HANDLE ||
		vk.liquidSnapshot.color_descriptor == VK_NULL_HANDLE ||
		vk.render_pass.liquid_snapshot == VK_NULL_HANDLE ||
		vk.framebuffers.liquid_snapshot == VK_NULL_HANDLE ||
		vk.renderPassIndex != RENDER_PASS_MAIN ||
		( vk_current_render_pass != vk.render_pass.main &&
		  vk_current_render_pass != vk.render_pass.main_load ) ) {
		return qfalse;
	}

	/* Vulkan cannot sample the active main color attachment. End it so its
	 * resolve image becomes readable, copy into the private scaled liquid
	 * target, then resume the scene with load semantics. */
	vk_end_render_pass();
	Com_Memset( &barrier, 0, sizeof( barrier ) );
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
		VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	qvkCmdPipelineBarrier( vk.cmd->command_buffer,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		0, 1, &barrier, 0, NULL, 0, NULL );
	vk_begin_liquid_snapshot_render_pass();
	qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		vk.liquid_snapshot_pipeline );
	qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		vk.pipeline_layout_post_process, 0, 1,
		&vk.liquidSnapshot.source_descriptor, 0, NULL );
	qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
	vk_end_render_pass();
	vk_begin_main_render_pass_load();

	/* The fullscreen copy bound the post-process pipeline layout and a
	 * static-viewport pipeline: cached descriptor-set state loses layout
	 * compatibility and the dynamic viewport/scissor become undefined. Keep
	 * the cached set handles, force a full rebind on the next draw, and
	 * poison the scissor cache so vk_update_depth_range re-emits it instead
	 * of trusting a stale match against undefined GPU state. */
	vk.cmd->last_pipeline = VK_NULL_HANDLE;
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;
	vk.cmd->descriptor_set.start = 0;
	vk.cmd->descriptor_set.end = VK_DESC_COUNT - 1;
	Com_Memset( &vk.cmd->scissor_rect, 0xff, sizeof( vk.cmd->scissor_rect ) );
	backEnd.liquidScreenMapDone = qtrue;
	return qtrue;
}


static qboolean vk_depth_fade_resolve_supported( void )
{
	return ( vk.msaaActive && vk.depthStencilResolve ) ? qtrue : qfalse;
}


static qboolean vk_depth_fade_uses_depth_resolve( void )
{
	return ( vk_depth_fade_resolve_supported() &&
		vk.depth_fade_image != VK_NULL_HANDLE &&
		vk.depth_fade_image_view != VK_NULL_HANDLE ) ? qtrue : qfalse;
}


static qboolean vk_depth_fade_requested( void )
{
	return ( ( r_depthFade && r_depthFade->integer ) || R_CelShadingWorldActive() ||
		( r_globalFog && r_globalFog->integer ) ) ? qtrue : qfalse;
}


static renderPass_t vk_pipeline_render_pass_index( void )
{
	if ( vk.renderPassIndex == RENDER_PASS_MAIN &&
		vk_current_render_pass == vk.render_pass.main_load ) {
		return RENDER_PASS_MAIN_LOAD;
	}
	return vk.renderPassIndex;
}


qboolean vk_depth_fade_supported( void )
{
	const qboolean sampleCompatible = ( !vk.msaaActive || vk.depthStencilResolve ) ? qtrue : qfalse;

	return ( vk_depth_fade_requested() &&
		vk.maxBoundDescriptorSets > VK_DESC_DEPTH_FADE && sampleCompatible ) ? qtrue : qfalse;
}


qboolean vk_depth_fade_available( void )
{
	return ( vk_depth_fade_supported() && vk.depth_fade_image != VK_NULL_HANDLE && vk.depth_fade_image_view != VK_NULL_HANDLE && vk.depth_fade_descriptor != VK_NULL_HANDLE ) ? qtrue : qfalse;
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
	return ( vk_dlight_shadow_atlas_available() && vk.dlight_shadow_rendered ) ? qtrue : qfalse;
}

qboolean vk_begin_dlight_shadow_render_pass( void )
{
	vk.dlight_shadow_rendered = qfalse;

	if ( !vk_dlight_shadow_atlas_available() ||
		vk.render_pass.dlight_shadow == VK_NULL_HANDLE ||
		vk.framebuffers.dlight_shadow == VK_NULL_HANDLE ||
		vk.renderPassIndex != RENDER_PASS_MAIN ) {
		return qfalse;
	}

	vk_end_render_pass();
	vk.renderPassIndex = RENDER_PASS_DLIGHT_SHADOW;
	vk.renderWidth = vk.dlight_shadow_atlas_width;
	vk.renderHeight = vk.dlight_shadow_atlas_height;
	vk.renderScaleX = 1.0f;
	vk.renderScaleY = 1.0f;
	vk_begin_render_pass( vk.render_pass.dlight_shadow, vk.framebuffers.dlight_shadow, qfalse,
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
	return ( vk_spot_shadow_atlas_available() && vk.spot_shadow_rendered ) ? qtrue : qfalse;
}

qboolean vk_begin_spot_shadow_render_pass( void )
{
	vk.spot_shadow_rendered = qfalse;

	if ( !vk_spot_shadow_atlas_available() ||
		vk.render_pass.dlight_shadow == VK_NULL_HANDLE ||
		vk.framebuffers.spot_shadow == VK_NULL_HANDLE ||
		vk.renderPassIndex != RENDER_PASS_MAIN ) {
		return qfalse;
	}

	vk_end_render_pass();
	vk.renderPassIndex = RENDER_PASS_SPOT_SHADOW;
	vk.renderWidth = vk.spot_shadow_atlas_width;
	vk.renderHeight = vk.spot_shadow_atlas_height;
	vk.renderScaleX = 1.0f;
	vk.renderScaleY = 1.0f;
	vk_begin_render_pass( vk.render_pass.dlight_shadow, vk.framebuffers.spot_shadow, qfalse,
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
	return ( vk_csm_shadow_atlas_available() && vk.csm_shadow_rendered ) ? qtrue : qfalse;
}

qboolean vk_begin_csm_shadow_render_pass( void )
{
	vk.csm_shadow_rendered = qfalse;

	if ( !vk_csm_shadow_atlas_available() ||
		vk.render_pass.dlight_shadow == VK_NULL_HANDLE ||
		vk.framebuffers.csm_shadow == VK_NULL_HANDLE ||
		vk.renderPassIndex != RENDER_PASS_MAIN ) {
		return qfalse;
	}

	vk_end_render_pass();
	vk.renderPassIndex = RENDER_PASS_CSM_SHADOW;
	vk.renderWidth = vk.csm_shadow_atlas_width;
	vk.renderHeight = vk.csm_shadow_atlas_height;
	vk.renderScaleX = 1.0f;
	vk.renderScaleY = 1.0f;
	vk_begin_render_pass( vk.render_pass.dlight_shadow, vk.framebuffers.csm_shadow, qfalse,
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
	return ( vk_depth_fade_available() && vk.depth_fade_copied ) ? qtrue : qfalse;
}


void vk_copy_depth_fade( void )
{
	VkImageCopy region;
	VkCommandBuffer command_buffer;

	if ( !vk_depth_fade_available() || vk.renderPassIndex != RENDER_PASS_MAIN ) {
		return;
	}

	if ( vk_depth_fade_uses_depth_resolve() ) {
		if ( vk_depth_fade_ready() ) {
			return;
		}
		if ( vk_current_render_pass != vk.render_pass.main ) {
			return;
		}
		vk_end_render_pass();
		vk_begin_main_render_pass_load();
		return;
	}

	if ( vk_current_render_pass != vk.render_pass.main &&
		vk_current_render_pass != vk.render_pass.main_load ) {
		return;
	}

	vk_end_render_pass();

	command_buffer = vk.cmd->command_buffer;

	record_image_layout_transition( command_buffer, vk.depth_image, VK_IMAGE_ASPECT_DEPTH_BIT,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0, 0 );
	record_image_layout_transition( command_buffer, vk.depth_fade_image, VK_IMAGE_ASPECT_DEPTH_BIT,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, 0 );

	Com_Memset( &region, 0, sizeof( region ) );
	region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	region.srcSubresource.baseArrayLayer = 0;
	region.srcSubresource.layerCount = 1;
	region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	region.dstSubresource.baseArrayLayer = 0;
	region.dstSubresource.layerCount = 1;
	region.extent.width = glConfig.vidWidth;
	region.extent.height = glConfig.vidHeight;
	region.extent.depth = 1;

	qvkCmdCopyImage( command_buffer,
		vk.depth_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		vk.depth_fade_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &region );

	record_image_layout_transition( command_buffer, vk.depth_fade_image, VK_IMAGE_ASPECT_DEPTH_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 0 );
	record_image_layout_transition( command_buffer, vk.depth_image, VK_IMAGE_ASPECT_DEPTH_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 0, 0 );

	vk.depth_fade_copied = qtrue;
	vk_begin_main_render_pass_load();
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
		!vk.fboActive || vk.world_outline_pipeline == VK_NULL_HANDLE ||
		!vk_depth_fade_available() || vk.renderPassIndex != RENDER_PASS_MAIN ) {
		return;
	}

	width = r_celShadingWorldWidth ? Com_Clamp( 1.0f, 8.0f, r_celShadingWorldWidth->value ) : 2.0f;
	alpha = r_celShadingWorldAlpha ? Com_Clamp( 0.0f, 1.0f, r_celShadingWorldAlpha->value ) : 1.0f;
	threshold = r_celShadingWorldDepthThreshold ? Com_Clamp( 0.0001f, 0.02f, r_celShadingWorldDepthThreshold->value ) : 0.0015f;
	if ( alpha <= 0.0f ) {
		return;
	}

	if ( !vk_depth_fade_ready() ) {
		vk_copy_depth_fade();
	}
	if ( !vk_depth_fade_ready() || vk.renderPassIndex != RENDER_PASS_MAIN ) {
		return;
	}

	constants[0] = glConfig.vidWidth > 0 ? width / (float)glConfig.vidWidth : 1.0f;
	constants[1] = glConfig.vidHeight > 0 ? width / (float)glConfig.vidHeight : 1.0f;
	constants[2] = threshold;
	constants[3] = alpha;

	descriptor = vk.depth_fade_descriptor;
	qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.world_outline_pipeline );
	qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		vk.pipeline_layout_post_process, 0, 1, &descriptor, 0, NULL );
	qvkCmdPushConstants( vk.cmd->command_buffer, vk.pipeline_layout_post_process,
		VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( constants ), constants );
	qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );

	vk.cmd->last_pipeline = VK_NULL_HANDLE;
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;
	vk.cmd->descriptor_set.start = 0;
	vk.cmd->descriptor_set.end = VK_DESC_COUNT - 1;
	/* The static-scissor overlay pipeline left the dynamic scissor undefined;
	 * poison the cache so the next 3D draw re-emits it. */
	Com_Memset( &vk.cmd->scissor_rect, 0xff, sizeof( vk.cmd->scissor_rect ) );
}


void vk_draw_global_fog( void )
{
	const globalFog_t *fog = tr.world ? &tr.world->globalFog : NULL;
	float constants[12];
	float opacity;
	float zNear;
	float zFar;
	VkDescriptorSet descriptor;

	if ( !r_globalFog || !r_globalFog->integer || !r_globalFogStrength || !fog ||
		!fog->loaded || !vk.fboActive || vk.global_fog_pipeline == VK_NULL_HANDLE ||
		( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) || !vk_depth_fade_ready() ||
		vk.renderPassIndex != RENDER_PASS_MAIN ||
		( vk_current_render_pass != vk.render_pass.main &&
			vk_current_render_pass != vk.render_pass.main_load ) ) {
		return;
	}

	opacity = Com_Clamp( 0.0f, 1.0f, fog->opacity * r_globalFogStrength->value );
	zNear = r_znear ? r_znear->value : 4.0f;
	zFar = backEnd.viewParms.zFar;
	if ( opacity <= 0.0f || zNear <= 0.0f || zFar <= zNear ) {
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
	constants[11] = 1.0f; // USE_REVERSED_DEPTH

	descriptor = vk.depth_fade_descriptor;
	qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		vk.global_fog_pipeline );
	qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		vk.pipeline_layout_post_process, 0, 1, &descriptor, 0, NULL );
	qvkCmdPushConstants( vk.cmd->command_buffer, vk.pipeline_layout_post_process,
		VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( constants ), constants );
	qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );

	/* The overlay binds the post-process layout.  Force the next material or
	 * HUD draw to restore its pipeline, descriptor, depth-range, and scissor. */
	vk.cmd->last_pipeline = VK_NULL_HANDLE;
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;
	vk.cmd->descriptor_set.start = 0;
	vk.cmd->descriptor_set.end = VK_DESC_COUNT - 1;
	Com_Memset( &vk.cmd->scissor_rect, 0xff, sizeof( vk.cmd->scissor_rect ) );
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

static void vk_reset_frame_resource_arena( vk_tess_t *frame )
{
	frame->uniform_read_offset = 0;
	frame->vertex_buffer_offset = 0;
	Com_Memset( frame->buf_offset, 0, sizeof( frame->buf_offset ) );
	Com_Memset( frame->vbo_offset, 0, sizeof( frame->vbo_offset ) );
	frame->curr_index_buffer = VK_NULL_HANDLE;
	frame->curr_index_offset = 0;
	frame->num_indexes = 0;

	Com_Memset( &frame->descriptor_set, 0, sizeof( frame->descriptor_set ) );
	frame->descriptor_set.start = ~0U;

	Com_Memset( &frame->scissor_rect, 0, sizeof( frame->scissor_rect ) );

	vk.stats.push_size = 0;
}


void vk_begin_frame( void )
{
	VkCommandBufferBeginInfo begin_info;
	VkResult res;

	if ( vk.frame_count++ ) // might happen during stereo rendering
		return;

#ifdef USE_UPLOAD_QUEUE
	vk_flush_staging_buffer( qtrue );
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
		vk_report_frame_timestamps( vk.cmd );
		vk_reset_command_pool_for_reuse( vk.cmd->command_pool, qfalse );
	}

	if ( !ri.CL_IsMinimized() && !vk.cmd->swapchain_image_acquired ) {
		qboolean retry = qfalse;
_retry:
		res = qvkAcquireNextImageKHR( vk.device, vk.swapchain, 1 * 1000000000ULL, vk.cmd->image_acquired, VK_NULL_HANDLE, &vk.cmd->swapchain_image_index );
		// when running via RDP: "Application has already acquired the maximum number of images (0x2)"
		// probably caused by "device lost" errors
		if ( res < 0 ) {
			if ( res == VK_ERROR_OUT_OF_DATE_KHR && retry == qfalse ) {
				// swapchain re-creation needed
				retry = qtrue;
				vk_restart_swapchain( __func__, res );
				goto _retry;
			} else {
				ri.Error( ERR_FATAL, "vkAcquireNextImageKHR returned %s", vk_result_string( res ) );
			}
		}
		vk.cmd->swapchain_image_acquired = qtrue;
	}

	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.pNext = NULL;
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	begin_info.pInheritanceInfo = NULL;

	VK_CHECK( qvkBeginCommandBuffer( vk.cmd->command_buffer, &begin_info ) );
	vk_begin_debug_label( vk.cmd->command_buffer, "frame", 0.1f, 0.8f, 0.35f, 1.0f );
	vk_reset_frame_timestamps( vk.cmd );
	vk_write_timestamp( "frame begin", VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT );
#ifdef USE_VBO
	VBO_ResetRecordStats();
#endif

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
	backEnd.liquidScreenMapDone = qfalse;

	if ( vk_find_screenmap_drawsurfs() ) {
		vk_begin_screenmap_render_pass();
	} else {
		vk_begin_main_render_pass();
	}

	// dynamic vertex buffer layout
	vk_reset_frame_resource_arena( vk.cmd );
}


static void vk_resize_geometry_buffer( void )
{
	int i;

	vk_end_render_pass();
	vk_end_debug_label( vk.cmd->command_buffer );
	vk.cmd->timestamp_query_valid = qfalse;

	VK_CHECK( qvkEndCommandBuffer( vk.cmd->command_buffer ) );

	vk_reset_command_pool_for_reuse( vk.cmd->command_pool, qfalse );

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
	const VkPipelineStageFlags wait_dst_stage_mask[2] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
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
		return;
	}

	if ( backEnd.screenshotCubeFrontPending && !backEnd.screenshotCubeFailed ) {
		if ( !vk_capture_cubemap_face( 0, backEnd.screenshotCubeFrontSize ) ) {
			backEnd.screenshotCubeFailed = qtrue;
			ri.Printf( PRINT_WARNING,
				"WARNING: screenshot cubemap cancelled: Vulkan front-face capture failed.\n" );
		}
	}

	if ( vk.fboActive )
	{
		vk.cmd->last_pipeline = VK_NULL_HANDLE; // do not restore clobbered descriptors in vk_bloom()

		if ( r_bloom->integer && !backEnd.screenshotCubeActive &&
			!backEnd.screenshotCubeFrontPending )
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

		if ( !ri.CL_IsMinimized() )
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
	vk_write_timestamp( "frame end", VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT );
	vk_end_debug_label( vk.cmd->command_buffer );

	VK_CHECK( qvkEndCommandBuffer( vk.cmd->command_buffer ) );

	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.pNext = NULL;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &vk.cmd->command_buffer;
	if ( !ri.CL_IsMinimized() ) {
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
	switch ( res ) {
		case VK_SUCCESS:
			break;
		case VK_SUBOPTIMAL_KHR:
		case VK_ERROR_OUT_OF_DATE_KHR:
			// swapchain re-creation needed
			vk_restart_swapchain( __func__, res );
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


static uint32_t vk_capture_pixel_width( VkFormat format )
{
	switch ( format ) {
		case VK_FORMAT_B4G4R4A4_UNORM_PACK16:
			return 2;
		case VK_FORMAT_R16G16B16A16_UNORM:
			return 8;
		default:
			return 4;
	}
}


void vk_release_cubemap_capture( void )
{
	if ( vk.cubemap_capture.readback_ptr && vk.cubemap_capture.readback_allocation.memory ) {
		qvkUnmapMemory( vk.device, vk.cubemap_capture.readback_allocation.memory );
		vk.cubemap_capture.readback_ptr = NULL;
	}
	if ( vk.cubemap_capture.framebuffer ) {
		qvkDestroyFramebuffer( vk.device, vk.cubemap_capture.framebuffer, NULL );
		vk.cubemap_capture.framebuffer = VK_NULL_HANDLE;
	}
	if ( vk.cubemap_capture.pipeline ) {
		qvkDestroyPipeline( vk.device, vk.cubemap_capture.pipeline, NULL );
		vk.cubemap_capture.pipeline = VK_NULL_HANDLE;
	}
	if ( vk.cubemap_capture.image_view ) {
		qvkDestroyImageView( vk.device, vk.cubemap_capture.image_view, NULL );
		vk.cubemap_capture.image_view = VK_NULL_HANDLE;
	}
	if ( vk.cubemap_capture.image ) {
		qvkDestroyImage( vk.device, vk.cubemap_capture.image, NULL );
		vk.cubemap_capture.image = VK_NULL_HANDLE;
	}
	if ( vk.cubemap_capture.readback_buffer ) {
		qvkDestroyBuffer( vk.device, vk.cubemap_capture.readback_buffer, NULL );
		vk.cubemap_capture.readback_buffer = VK_NULL_HANDLE;
	}
	vk_free_memory_allocation( &vk.cubemap_capture.image_allocation );
	vk_free_memory_allocation( &vk.cubemap_capture.readback_allocation );
	vk.cubemap_capture.face_stride = 0;
	vk.cubemap_capture.face_size = 0;
	vk.cubemap_capture.captured_mask = 0;
	vk.cubemap_capture.invalidate_readback = qfalse;
	vk.cubemap_capture.invalidate_pending = qfalse;
}


static qboolean vk_create_cubemap_capture( uint32_t faceSize )
{
	VkPhysicalDeviceProperties properties;
	VkMemoryRequirements memoryRequirements;
	VkMemoryPropertyFlags memoryFlags;
	VkImageCreateInfo imageDesc;
	VkImageViewCreateInfo viewDesc;
	VkFramebufferCreateInfo framebufferDesc;
	VkBufferCreateInfo bufferDesc;
	VkDeviceSize faceBytes;
	VkDeviceSize alignment;
	VkDeviceSize bufferBytes;
	uint32_t memoryType;

	if ( faceSize == 0 ) {
		return qfalse;
	}
	if ( vk.cubemap_capture.face_size == faceSize &&
		vk.cubemap_capture.readback_buffer != VK_NULL_HANDLE &&
		( !vk.fboActive || ( vk.cubemap_capture.image != VK_NULL_HANDLE &&
			vk.cubemap_capture.pipeline != VK_NULL_HANDLE ) ) ) {
		return qtrue;
	}

	vk_release_cubemap_capture();
	qvkGetPhysicalDeviceProperties( vk.physical_device, &properties );
	if ( faceSize > properties.limits.maxImageDimension2D ) {
		ri.Printf( PRINT_WARNING, "WARNING: cubemap face size %u exceeds the Vulkan device limit.\n", faceSize );
		return qfalse;
	}

	faceBytes = (VkDeviceSize)faceSize * faceSize * vk_capture_pixel_width( vk.capture_format );
	alignment = MAX( (VkDeviceSize)4, properties.limits.optimalBufferCopyOffsetAlignment );
	if ( faceBytes > ~(VkDeviceSize)0 - ( alignment - 1 ) ) {
		return qfalse;
	}
	vk.cubemap_capture.face_stride = ( ( faceBytes + alignment - 1 ) / alignment ) * alignment;
	if ( vk.cubemap_capture.face_stride > ~(VkDeviceSize)0 / 6 ) {
		return qfalse;
	}
	bufferBytes = vk.cubemap_capture.face_stride * 6;

	Com_Memset( &bufferDesc, 0, sizeof( bufferDesc ) );
	bufferDesc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferDesc.size = bufferBytes;
	bufferDesc.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	bufferDesc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VK_CHECK( qvkCreateBuffer( vk.device, &bufferDesc, NULL,
		&vk.cubemap_capture.readback_buffer ) );
	qvkGetBufferMemoryRequirements( vk.device, vk.cubemap_capture.readback_buffer,
		&memoryRequirements );
	memoryType = find_memory_type2( memoryRequirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
		VK_MEMORY_PROPERTY_HOST_CACHED_BIT, &memoryFlags );
	if ( memoryType == ~0U ) {
		memoryType = find_memory_type2( memoryRequirements.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
			&memoryFlags );
	}
	if ( memoryType == ~0U ) {
		memoryType = find_memory_type2( memoryRequirements.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&memoryFlags );
	}
	if ( memoryType == ~0U ) {
		ri.Printf( PRINT_WARNING, "WARNING: no host-visible Vulkan memory is available for cubemap capture.\n" );
		vk_release_cubemap_capture();
		return qfalse;
	}
	vk.cubemap_capture.invalidate_readback =
		( memoryFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ) ? qfalse : qtrue;
	vk_allocate_memory( &vk.cubemap_capture.readback_allocation, memoryRequirements.size,
		memoryType, VK_MEMORY_CATEGORY_READBACK, "cubemap readback buffer memory", NULL );
	VK_CHECK( qvkBindBufferMemory( vk.device, vk.cubemap_capture.readback_buffer,
		vk.cubemap_capture.readback_allocation.memory, 0 ) );
	VK_CHECK( qvkMapMemory( vk.device, vk.cubemap_capture.readback_allocation.memory,
		0, VK_WHOLE_SIZE, 0, (void **)&vk.cubemap_capture.readback_ptr ) );
	SET_OBJECT_NAME( vk.cubemap_capture.readback_buffer, "cubemap readback buffer",
		VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT );

	if ( vk.fboActive ) {
		Com_Memset( &imageDesc, 0, sizeof( imageDesc ) );
		imageDesc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageDesc.imageType = VK_IMAGE_TYPE_2D;
		imageDesc.format = vk.capture_format;
		imageDesc.extent.width = faceSize;
		imageDesc.extent.height = faceSize;
		imageDesc.extent.depth = 1;
		imageDesc.mipLevels = 1;
		imageDesc.arrayLayers = 1;
		imageDesc.samples = VK_SAMPLE_COUNT_1_BIT;
		imageDesc.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		imageDesc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VK_CHECK( qvkCreateImage( vk.device, &imageDesc, NULL,
			&vk.cubemap_capture.image ) );
		qvkGetImageMemoryRequirements( vk.device, vk.cubemap_capture.image, &memoryRequirements );
		memoryType = find_memory_type( memoryRequirements.memoryTypeBits,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
		vk_allocate_memory( &vk.cubemap_capture.image_allocation, memoryRequirements.size,
			memoryType, VK_MEMORY_CATEGORY_ATTACHMENTS, "cubemap capture image memory", NULL );
		VK_CHECK( qvkBindImageMemory( vk.device, vk.cubemap_capture.image,
			vk.cubemap_capture.image_allocation.memory, 0 ) );

		Com_Memset( &viewDesc, 0, sizeof( viewDesc ) );
		viewDesc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewDesc.image = vk.cubemap_capture.image;
		viewDesc.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewDesc.format = vk.capture_format;
		viewDesc.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewDesc.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewDesc.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewDesc.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewDesc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewDesc.subresourceRange.baseMipLevel = 0;
		viewDesc.subresourceRange.levelCount = 1;
		viewDesc.subresourceRange.baseArrayLayer = 0;
		viewDesc.subresourceRange.layerCount = 1;
		VK_CHECK( qvkCreateImageView( vk.device, &viewDesc, NULL,
			&vk.cubemap_capture.image_view ) );

		Com_Memset( &framebufferDesc, 0, sizeof( framebufferDesc ) );
		framebufferDesc.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferDesc.renderPass = vk.render_pass.capture;
		framebufferDesc.attachmentCount = 1;
		framebufferDesc.pAttachments = &vk.cubemap_capture.image_view;
		framebufferDesc.width = faceSize;
		framebufferDesc.height = faceSize;
		framebufferDesc.layers = 1;
		VK_CHECK( qvkCreateFramebuffer( vk.device, &framebufferDesc, NULL,
			&vk.cubemap_capture.framebuffer ) );

		vk_create_post_process_pipeline( 8, faceSize, faceSize );
		SET_OBJECT_NAME( vk.cubemap_capture.image, "cubemap capture image",
			VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT );
		SET_OBJECT_NAME( vk.cubemap_capture.image_view, "cubemap capture image view",
			VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT );
		SET_OBJECT_NAME( vk.cubemap_capture.framebuffer, "cubemap capture framebuffer",
			VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT );
	}

	vk.cubemap_capture.face_size = faceSize;
	vk.cubemap_capture.captured_mask = 0;
	return qtrue;
}


qboolean vk_capture_cubemap_face( uint32_t faceIndex, uint32_t faceSize )
{
	VkBufferImageCopy copy;
	VkImage sourceImage;
	VkImageLayout sourceLayout;
	uint32_t sourceFaceSize;
	uint32_t sourceX;
	uint32_t sourceY;

	if ( faceIndex > 5 || faceSize == 0 ||
		vk.renderPassIndex != RENDER_PASS_MAIN || !backEnd.doneSurfaces ) {
		return qfalse;
	}
	if ( !vk_create_cubemap_capture( faceSize ) ) {
		return qfalse;
	}
	if ( faceIndex == 1 ) {
		vk.cubemap_capture.captured_mask = 0;
	}

	vk_end_render_pass();
	if ( vk.fboActive ) {
		float constants[4];

		sourceFaceSize = MIN( glConfig.vidWidth, glConfig.vidHeight );
		constants[0] = 0.0f;
		constants[1] = ( glConfig.vidHeight - sourceFaceSize ) / (float)glConfig.vidHeight;
		constants[2] = sourceFaceSize / (float)glConfig.vidWidth;
		constants[3] = sourceFaceSize / (float)glConfig.vidHeight;

		vk_begin_render_pass( vk.render_pass.capture, vk.cubemap_capture.framebuffer,
			qfalse, faceSize, faceSize );
		qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vk.cubemap_capture.pipeline );
		qvkCmdPushConstants( vk.cmd->command_buffer, vk.pipeline_layout_post_process,
			VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( constants ), constants );
		vk_bind_gamma_descriptor_sets();
		qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
		vk_end_render_pass();
		sourceImage = vk.cubemap_capture.image;
		sourceLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		sourceX = sourceY = 0;
	} else {
		sourceImage = vk.swapchain_images[ vk.cmd->swapchain_image_index ];
		sourceLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		sourceX = 0;
		sourceY = gls.captureHeight - faceSize;
		record_image_layout_transition( vk.cmd->command_buffer, sourceImage,
			VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0, 0 );
	}

	Com_Memset( &copy, 0, sizeof( copy ) );
	copy.bufferOffset = (VkDeviceSize)faceIndex * vk.cubemap_capture.face_stride;
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.mipLevel = 0;
	copy.imageSubresource.baseArrayLayer = 0;
	copy.imageSubresource.layerCount = 1;
	copy.imageOffset.x = sourceX;
	copy.imageOffset.y = sourceY;
	copy.imageExtent.width = faceSize;
	copy.imageExtent.height = faceSize;
	copy.imageExtent.depth = 1;
	qvkCmdCopyImageToBuffer( vk.cmd->command_buffer, sourceImage, sourceLayout,
		vk.cubemap_capture.readback_buffer, 1, &copy );

	if ( !vk.fboActive ) {
		record_image_layout_transition( vk.cmd->command_buffer, sourceImage,
			VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 0, 0 );
	}
	if ( ( vk.cubemap_capture.captured_mask | ( 1u << faceIndex ) ) == 0x3fu ) {
		VkBufferMemoryBarrier barrier;
		Com_Memset( &barrier, 0, sizeof( barrier ) );
		barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.buffer = vk.cubemap_capture.readback_buffer;
		barrier.offset = 0;
		barrier.size = VK_WHOLE_SIZE;
		qvkCmdPipelineBarrier( vk.cmd->command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &barrier, 0, NULL );
	}

	vk.cubemap_capture.captured_mask |= 1u << faceIndex;
	if ( vk.cubemap_capture.invalidate_readback ) {
		vk.cubemap_capture.invalidate_pending = qtrue;
	}
	vk_begin_main_render_pass_load();
	vk.cmd->last_pipeline = VK_NULL_HANDLE;
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;
	vk.cmd->descriptor_set.start = 0;
	vk.cmd->descriptor_set.end = VK_DESC_COUNT - 1;
	backEnd.doneSurfaces = qfalse;
	backEnd.doneBloom = qfalse;
	backEnd.doneMotionBlur = qfalse;
	return qtrue;
}


qboolean vk_read_cubemap_face( byte *buffer, uint32_t faceIndex, uint32_t faceSize )
{
	const uint32_t pixelWidth = vk_capture_pixel_width( vk.capture_format );
	const byte *data;
	byte *dst;
	uint32_t x, y;

	if ( !buffer || faceIndex > 5 ||
		faceSize != vk.cubemap_capture.face_size ||
		!( vk.cubemap_capture.captured_mask & ( 1u << faceIndex ) ) ||
		!vk.cubemap_capture.readback_ptr ) {
		return qfalse;
	}
	if ( vk.cubemap_capture.invalidate_pending ) {
		VkMappedMemoryRange range;
		Com_Memset( &range, 0, sizeof( range ) );
		range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		range.memory = vk.cubemap_capture.readback_allocation.memory;
		range.offset = 0;
		range.size = VK_WHOLE_SIZE;
		VK_CHECK( qvkInvalidateMappedMemoryRanges( vk.device, 1, &range ) );
		vk.cubemap_capture.invalidate_pending = qfalse;
	}

	data = vk.cubemap_capture.readback_ptr +
		(VkDeviceSize)faceIndex * vk.cubemap_capture.face_stride;
	dst = buffer + (size_t)faceSize * ( faceSize - 1 ) * 3;
	for ( y = 0; y < faceSize; y++ ) {
		if ( pixelWidth == 2 ) {
			const uint16_t *src = (const uint16_t *)data;
			for ( x = 0; x < faceSize; x++ ) {
				dst[x * 3 + 0] = ( ( src[x] >> 12 ) & 0xF ) << 4;
				dst[x * 3 + 1] = ( ( src[x] >> 8 ) & 0xF ) << 4;
				dst[x * 3 + 2] = ( ( src[x] >> 4 ) & 0xF ) << 4;
			}
		} else if ( pixelWidth == 8 ) {
			const uint16_t *src = (const uint16_t *)data;
			for ( x = 0; x < faceSize; x++ ) {
				dst[x * 3 + 0] = src[x * 4 + 0] >> 8;
				dst[x * 3 + 1] = src[x * 4 + 1] >> 8;
				dst[x * 3 + 2] = src[x * 4 + 2] >> 8;
			}
		} else {
			for ( x = 0; x < faceSize; x++ ) {
				Com_Memcpy( dst + x * 3, data + x * 4, 3 );
			}
		}
		data += (size_t)faceSize * pixelWidth;
		dst -= (size_t)faceSize * 3;
	}

	if ( is_bgr( vk.capture_format ) ) {
		dst = buffer;
		for ( x = 0; x < faceSize * faceSize; x++, dst += 3 ) {
			const byte tmp = dst[0];
			dst[0] = dst[2];
			dst[2] = tmp;
		}
	}
	return qtrue;
}


void vk_read_pixels( byte *buffer, uint32_t x, uint32_t y, uint32_t width, uint32_t height )
{
	VkCommandBuffer command_buffer;
	vk_memory_allocation_t memory;
	VkMemoryRequirements memory_requirements;
	VkMemoryPropertyFlags memory_reqs;
	VkMemoryPropertyFlags memory_flags;
	uint32_t memory_type_index;
	VkImageSubresource subresource;
	VkSubresourceLayout layout;
	VkImageCreateInfo desc;
	VkImage srcImage;
	VkImageLayout srcImageLayout;
	uint32_t srcWidth;
	uint32_t srcHeight;
	uint32_t srcY;
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
			srcWidth = gls.captureWidth;
			srcHeight = gls.captureHeight;
		} else {
			srcImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			srcImage = vk.color_image;
			srcWidth = glConfig.vidWidth;
			srcHeight = glConfig.vidHeight;
		}
	} else {
		srcImageLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		srcImage = vk.swapchain_images[ vk.cmd->swapchain_image_index ];
		srcWidth = gls.windowWidth;
		srcHeight = gls.windowHeight;
	}
	if ( width == 0 || height == 0 || x > srcWidth || y > srcHeight ||
		width > srcWidth - x || height > srcHeight - y ) {
		ri.Error( ERR_DROP, "%s(): capture rectangle %u,%u %ux%u exceeds %ux%u source",
			__func__, x, y, width, height, srcWidth, srcHeight );
		return;
	}
	srcY = srcHeight - y - height;

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

	// host_cached bit is desirable for fast reads
	memory_reqs = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
	memory_type_index = find_memory_type2( memory_requirements.memoryTypeBits, memory_reqs, &memory_flags );
	if ( memory_type_index == ~0 ) {
		// try less explicit flags, without host_coherent
		memory_reqs = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
		memory_type_index = find_memory_type2( memory_requirements.memoryTypeBits, memory_reqs, &memory_flags );
		if ( memory_type_index == ~0U ) {
			// slowest case
			memory_reqs = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
			memory_type_index = find_memory_type2( memory_requirements.memoryTypeBits, memory_reqs, &memory_flags );
			if ( memory_type_index == ~0U ) {
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

	vk_allocate_memory( &memory, memory_requirements.size, memory_type_index,
		VK_MEMORY_CATEGORY_READBACK, "readback image memory", NULL );
	VK_CHECK(qvkBindImageMemory(vk.device, dstImage, memory.memory, 0));

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
		region.srcOffsets[0].x = x;
		region.srcOffsets[0].y = srcY;
		region.srcOffsets[0].z = 0;
		region.srcOffsets[1].x = x + width;
		region.srcOffsets[1].y = srcY + height;
		region.srcOffsets[1].z = 1;
		region.dstSubresource = region.srcSubresource;
		region.dstOffsets[0].x = 0;
		region.dstOffsets[0].y = 0;
		region.dstOffsets[0].z = 0;
		region.dstOffsets[1].x = width;
		region.dstOffsets[1].y = height;
		region.dstOffsets[1].z = 1;

		qvkCmdBlitImage( command_buffer, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_NEAREST );

	} else {
		VkImageCopy region;

		region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.srcSubresource.mipLevel = 0;
		region.srcSubresource.baseArrayLayer = 0;
		region.srcSubresource.layerCount = 1;
		region.srcOffset.x = x;
		region.srcOffset.y = srcY;
		region.srcOffset.z = 0;
		region.dstSubresource = region.srcSubresource;
		region.dstOffset.x = 0;
		region.dstOffset.y = 0;
		region.dstOffset.z = 0;
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

	VK_CHECK( qvkMapMemory( vk.device, memory.memory, 0, VK_WHOLE_SIZE, 0, (void**)&data ) );

	if ( invalidate_ptr )
	{
		VkMappedMemoryRange range;
		range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		range.pNext = NULL;
		range.memory = memory.memory;
		range.size = VK_WHOLE_SIZE;
		range.offset = 0;
		qvkInvalidateMappedMemoryRanges( vk.device, 1, &range );
	}

	data += layout.offset;

	pixel_width = vk_capture_pixel_width( vk.capture_format );

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
	vk_free_memory_allocation( &memory );

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
	const qboolean requested = ( r_motionBlur && r_motionBlur->integer &&
		r_motionBlurStrength && r_motionBlurStrength->value > 0.0f ) ? qtrue : qfalse;
	float constants[8] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f };
	int viewRect[4];

	if ( !requested ) {
		R_MotionBlur_ResetView( &vk_motion_blur_view );
		return qfalse;
	}
	if ( backEnd.screenshotCubeActive || backEnd.screenshotCubeFrontPending ) {
		R_MotionBlur_ResetView( &vk_motion_blur_view );
		return qfalse;
	}
	/* Recursive portal/mirror views are submitted before the primary view.
	 * Ignore them without disturbing the primary camera's temporal history. */
	if ( R_ViewPassIsPortal( &backEnd.viewParms ) ) {
		return qfalse;
	}
	if ( backEnd.doneMotionBlur ) {
		return qfalse;
	}
	if ( vk.renderPassIndex != RENDER_PASS_MAIN ) {
		return qfalse;
	}
	if ( !backEnd.doneSurfaces || !vk.fboActive || ri.CL_IsMinimized() ||
		glConfig.stereoEnabled || vk.motion_blur_image == VK_NULL_HANDLE ||
		vk.motion_blur_descriptor == VK_NULL_HANDLE ||
		vk.framebuffers.motion_blur == VK_NULL_HANDLE ||
		vk.motion_blur_pipeline == VK_NULL_HANDLE ||
		vk.motion_blur_copy_pipeline == VK_NULL_HANDLE ) {
		R_MotionBlur_ResetView( &vk_motion_blur_view );
		return qfalse;
	}

	backEnd.doneMotionBlur = qtrue;
	if ( !R_MotionBlur_Calculate( &vk_motion_blur_view, qtrue,
		r_motionBlurStrength->value, ri.Milliseconds(), backEnd.refdef.vieworg,
		backEnd.refdef.viewaxis, backEnd.refdef.fov_x, backEnd.refdef.fov_y,
		glConfig.vidWidth, glConfig.vidHeight, constants ) ) {
		return qfalse;
	}
	if ( !R_MotionBlur_CalculateViewRect( glConfig.vidWidth, glConfig.vidHeight,
		backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
		backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight,
		viewRect, constants + 4 ) ) {
		return qfalse;
	}

	vk_end_render_pass();
	vk_begin_render_pass( vk.render_pass.motion_blur, vk.framebuffers.motion_blur,
		qfalse, glConfig.vidWidth, glConfig.vidHeight );

	qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		vk.motion_blur_pipeline );
	qvkCmdPushConstants( vk.cmd->command_buffer, vk.pipeline_layout_post_process,
		VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( constants ), constants );
	qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		vk.pipeline_layout_post_process, 0, 1, &vk.color_descriptor, 0, NULL );
	qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
	vk_end_render_pass();

	/* Resume the main target so later HUD/console draws remain sharp. */
	vk_begin_main_render_pass_load();
	constants[0] = constants[1] = 0.0f;
	qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		vk.motion_blur_copy_pipeline );
	qvkCmdPushConstants( vk.cmd->command_buffer, vk.pipeline_layout_post_process,
		VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( constants ), constants );
	qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		vk.pipeline_layout_post_process, 0, 1, &vk.motion_blur_descriptor, 0, NULL );
	qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );

	/* Direct post-process binds bypass the normal descriptor/pipeline caches. */
	vk.cmd->last_pipeline = VK_NULL_HANDLE;
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;
	vk.cmd->descriptor_set.start = 0;
	vk.cmd->descriptor_set.end = VK_DESC_COUNT - 1;
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
	qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_layout_post_process, 0, 1, &vk.color_descriptor, 0, NULL );
	qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
	vk_end_render_pass();

	for ( i = 0; i < VK_NUM_BLOOM_PASSES*2; i+=2 ) {
		// horizontal blur
		vk_begin_blur_render_pass( i+0 );
		qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.blur_pipeline[i+0] );
		qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_layout_post_process, 0, 1, &vk.bloom_image_descriptor[i+0], 0, NULL );
		qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
		vk_end_render_pass();

		// vectical blur
		vk_begin_blur_render_pass( i+1 );
		qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.blur_pipeline[i+1] );
		qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_layout_post_process, 0, 1, &vk.bloom_image_descriptor[i+1], 0, NULL );
		qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
		vk_end_render_pass();
#if 0
		// horizontal blur
		vk_begin_blur_render_pass( i+0 );
		qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.blur_pipeline[i+0] );
		qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_layout_post_process, 0, 1, &vk.bloom_image_descriptor[i+2], 0, NULL );
		qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
		vk_end_render_pass();

		// vectical blur
		vk_begin_blur_render_pass( i+1 );
		qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.blur_pipeline[i+1] );
		qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_layout_post_process, 0, 1, &vk.bloom_image_descriptor[i+1], 0, NULL );
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
		qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			R_BloomProtectHighlightsActive() && vk.bloom_blend_cel_pipeline != VK_NULL_HANDLE ?
				vk.bloom_blend_cel_pipeline : vk.bloom_blend_pipeline );
		qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_layout_blend, 0, ARRAY_LEN(dset), dset, 0, NULL );
		qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
	}

	// invalidate pipeline state cache
	//vk.cmd->last_pipeline = VK_NULL_HANDLE;

	if ( vk.cmd->last_pipeline != VK_NULL_HANDLE )
	{
		// restore last pipeline
		qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.cmd->last_pipeline );

		vk_update_mvp( NULL );

		// force depth range and viewport/scissor updates
		vk.cmd->depth_range = DEPTH_RANGE_COUNT;

		// restore clobbered descriptor sets
		for ( i = 0; i < VK_NUM_BLOOM_PASSES; i++ ) {
			if ( vk.cmd->descriptor_set.current[i] != VK_NULL_HANDLE ) {
				if ( i == VK_DESC_UNIFORM /*|| i == VK_DESC_STORAGE*/ )
					qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_layout, i, 1, &vk.cmd->descriptor_set.current[i], 1, &vk.cmd->descriptor_set.offset[i] );
				else
					qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_layout, i, 1, &vk.cmd->descriptor_set.current[i], 0, NULL );
			}
		}
	}

	backEnd.doneBloom = qtrue;

	return qtrue;
}
