from __future__ import annotations

import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8", errors="strict")


def section(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    return source[begin : source.index(end, begin + len(start))]


class RtxVulkanSafetySourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.vk = read("code/rendererrtx/vk.c")
        cls.header = read("code/rendererrtx/vk.h")
        cls.backend = read("code/rendererrtx/tr_backend.c")

    def test_vulkan_12_core_promotion_and_entrypoint_aliases_are_supported(self) -> None:
        instance = section(
            self.vk,
            "static void create_instance( void )\n{",
            "static void vk_destroy_instance",
        )
        device = section(
            self.vk,
            "static qboolean vk_create_device( VkPhysicalDevice physical_device",
            "static void vk_destroy_instance",
        )
        loader = section(
            self.vk,
            "static void init_vulkan_library( void )\n{",
            "static void deinit_instance_functions",
        )

        self.assertIn("PFN_vkEnumerateInstanceVersion", self.vk)
        self.assertIn("qvkEnumerateInstanceVersion", instance)
        self.assertIn("appInfo.apiVersion = s_vkInstanceApiVersion", instance)
        self.assertIn("VK_API_VERSION_1_2", instance)
        self.assertIn("INIT_INSTANCE_FUNCTION_EXT( vkEnumerateInstanceVersion )", loader)

        self.assertIn("core12 =", device)
        self.assertIn("s_vkInstanceApiVersion", device)
        self.assertIn("physicalDeviceProperties.apiVersion", device)
        self.assertIn("bufferDeviceAddressExtension || core12", device)
        self.assertIn("descriptorIndexingExtension || core12", device)
        self.assertGreaterEqual(device.count("if ( !core12 )"), 2)
        self.assertIn("VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME", device)
        self.assertIn("VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME", device)
        self.assertIn("VK_KHR_SPIRV_1_4_EXTENSION_NAME", device)
        self.assertIn("VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME", device)

        self.assertIn("INIT_DEVICE_FUNCTION_EXT(vkGetBufferDeviceAddress)", loader)
        self.assertIn("INIT_DEVICE_FUNCTION_EXT(vkGetBufferDeviceAddressKHR)", loader)
        address = section(
            self.vk,
            "static VkDeviceAddress vk_get_buffer_device_address",
            "static qboolean vk_rt_create_buffer",
        )
        self.assertIn("qvkGetBufferDeviceAddress( vk.device", address)
        self.assertIn("qvkGetBufferDeviceAddressKHR( vk.device", address)
        self.assertIn("return 0;", address)

    def test_shader_binding_table_layout_is_checked_before_allocation(self) -> None:
        sbt = section(
            self.vk,
            "static qboolean vk_rt_build_sbt( void )\n{",
            "static qboolean vk_rt_ensure_pipeline",
        )

        self.assertIn("vk_rt_is_power_of_two( handleAlignment )", sbt)
        self.assertIn("vk_rt_is_power_of_two( groupBaseAlignment )", sbt)
        self.assertIn("vk_rt_align_up_checked", sbt)
        self.assertIn("vk_rt_device_size_add_checked", sbt)
        self.assertIn("vk_rt_device_size_multiply_checked", sbt)
        self.assertIn("maxShaderGroupStride", sbt)
        self.assertIn("handlesBytes > INT_MAX", sbt)
        self.assertIn("sbtSize > SIZE_MAX", sbt)
        self.assertIn("vk.rt.sbt_buffer.address % groupBaseAlignment", sbt)
        self.assertIn("vk_rt_destroy_buffer( &vk.rt.sbt_buffer )", sbt)
        self.assertNotIn("vk_rt_align_up(", sbt)

        create_buffer = section(
            self.vk,
            "static qboolean vk_rt_create_buffer( VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryProps, qboolean needDeviceAddress, const char *debugName, rtxVkRtBuffer_t *outBuffer )\n{",
            "static void vk_rt_destroy_buffer",
        )
        self.assertIn("needDeviceAddress && outBuffer->address == 0", create_buffer)
        self.assertIn("Com_Memset( outBuffer, 0, sizeof( *outBuffer ) )", create_buffer)

    def test_descriptor_capacity_is_gated_and_large_update_scratch_is_not_on_stack(self) -> None:
        device = section(
            self.vk,
            "static qboolean vk_create_device( VkPhysicalDevice physical_device",
            "static void vk_destroy_instance",
        )
        update = section(
            self.vk,
            "static qboolean vk_rt_update_descriptor_set( void )\n{",
            "static qboolean vk_rt_ensure_dynamic_blas",
        )

        for limit in (
            "maxPerStageDescriptorSampledImages",
            "maxDescriptorSetSampledImages",
            "maxPerStageDescriptorSamplers",
            "maxDescriptorSetSamplers",
            "maxPerStageDescriptorStorageBuffers",
            "maxPerStageDescriptorStorageImages",
            "maxPerStageResources",
            "maxPerStageDescriptorAccelerationStructures",
        ):
            with self.subTest(limit=limit):
                self.assertIn(limit, device)

        self.assertIn(
            "static VkDescriptorImageInfo s_vkRtDescriptorImageInfos[4 + RTX_RT_MAX_SCENE_TEXTURES]",
            self.vk,
        )
        self.assertIn("VkDescriptorImageInfo *imageInfos = s_vkRtDescriptorImageInfos", update)
        self.assertNotIn("VkDescriptorImageInfo imageInfos[", update)

    def test_masked_hits_use_any_hit_with_bounded_materials_and_explicit_lod(self) -> None:
        any_hit = read("code/rendererrtx/shaders/rt_main.rahit")
        raygen = read("code/rendererrtx/shaders/rt_main.rgen")
        closest_hit = read("code/rendererrtx/shaders/rt_main.rchit")
        reflection = json.loads(
            read("code/rendererrtx/shaders/spirv/shader_reflection.json")
        )
        pipeline = section(
            self.vk,
            "static qboolean vk_rt_ensure_pipeline( void )\n{",
            "static qboolean vk_rt_update_descriptor_set",
        )

        self.assertIn("pc.worldMaterialCount", any_hit)
        self.assertIn("pc.dynamicMaterialCount", any_hit)
        self.assertIn("min(index, maxCount - 1u)", any_hit)
        self.assertIn("textureLod(", any_hit)
        self.assertIn("ignoreIntersectionEXT;", any_hit)
        self.assertNotIn("gl_RayFlagsOpaqueEXT", raygen)
        self.assertNotIn("gl_RayFlagsOpaqueEXT", closest_hit)
        self.assertIn("VkPipelineShaderStageCreateInfo stages[5]", pipeline)
        self.assertIn("stages[4].stage = VK_SHADER_STAGE_ANY_HIT_BIT_KHR", pipeline)
        self.assertIn("groups[3].anyHitShader = 4", pipeline)
        self.assertGreaterEqual(self.vk.count("VK_SHADER_STAGE_ANY_HIT_BIT_KHR"), 9)
        self.assertIn(
            "VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR",
            self.vk,
        )
        self.assertTrue(
            any(
                shader["source"] == "rt_main.rahit" and shader["stage"] == "rahit"
                for shader in reflection["shaders"]
            )
        )

    def test_primary_dispatch_and_output_composition_fail_before_raster_state_is_lost(self) -> None:
        primary = section(
            self.vk,
            "qboolean vk_rt_primary_view_eligible( void )\n{",
            "qboolean vk_rt_trace_frame( void )",
        )
        trace = section(
            self.vk,
            "qboolean vk_rt_trace_frame( void )\n{",
            "static const char *vk_capability_value",
        )

        for gate in (
            "vk.renderPassIndex == RENDER_PASS_MAIN",
            "portalView == PV_NONE",
            "stereoFrame == STEREO_CENTER",
            "viewportX == 0",
            "viewportY == 0",
            "viewportWidth == glConfig.vidWidth",
            "viewportHeight == glConfig.vidHeight",
            "vk.rt.lastFrameBuilt == (uint32_t)tr.frameCount",
            "RDF_HYPERSPACE",
        ):
            with self.subTest(gate=gate):
                self.assertIn(gate, primary)

        preflight = trace.index("vk_rt_supports_reconstruction_blit()")
        end_pass = trace.index("vk_end_render_pass()")
        self.assertLess(preflight, end_pass)
        self.assertIn("preserving the complete raster frame", trace)
        self.assertIn("VkImageMemoryBarrier sceneColorBarrier", trace)
        self.assertIn("VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT", trace)
        self.assertIn("VK_ACCESS_SHADER_READ_BIT", trace)
        self.assertIn("VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR", trace)
        self.assertIn("VK_ACCESS_TRANSFER_WRITE_BIT", trace)
        self.assertIn("VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT", trace)
        self.assertIn("refusing silent raster fallback", self.backend)

    def test_frame_mutable_resources_and_upload_synchronization_are_explicit(self) -> None:
        self.assertIn("light_buffer[ NUM_COMMAND_BUFFERS ]", self.header)
        self.assertIn("temporal_params_buffer[ NUM_COMMAND_BUFFERS ]", self.header)

        descriptors = section(
            self.vk,
            "static qboolean vk_rt_update_descriptor_set( void )\n{",
            "static qboolean vk_rt_ensure_dynamic_blas",
        )
        self.assertIn("vk.cmd_index % NUM_COMMAND_BUFFERS", descriptors)
        self.assertIn("light_buffer[descriptorIndex]", descriptors)
        self.assertIn("temporal_params_buffer[descriptorIndex]", descriptors)

        trace = section(
            self.vk,
            "qboolean vk_rt_trace_frame( void )\n{",
            "static const char *vk_capability_value",
        )
        self.assertIn("VkImageMemoryBarrier traceBarriers[3]", trace)
        self.assertIn("history_image[historyReadIndex]", trace)
        self.assertIn("history_image[historyWriteIndex]", trace)
        self.assertIn("VK_ACCESS_SHADER_READ_BIT", trace)
        self.assertIn("VK_ACCESS_SHADER_WRITE_BIT", trace)

        self.assertGreaterEqual(
            self.vk.count(
                "const VkPipelineStageFlags wait_dst_stage_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT"
            ),
            2,
        )
        release = section(
            self.vk,
            "void vk_release_resources( refShutdownCode_t code )",
            "#if 0\nstatic void record_buffer_memory_barrier",
        )
        wait = release.index("vk_wait_staging_buffer();")
        reset = release.index("qvkResetCommandBuffer( vk.staging_command_buffer, 0 )", wait)
        destroy = release.index("vk_destroy_sync_primitives();", reset)
        create = release.index("vk_create_sync_primitives();", destroy)
        self.assertLess(wait, reset)
        self.assertLess(reset, destroy)
        self.assertLess(destroy, create)

    def test_attachment_pool_covers_the_maximum_supported_feature_mix(self) -> None:
        bloom_passes = re.search(
            r"#define\s+VK_NUM_BLOOM_PASSES\s+(\d+)", self.header
        )
        pool_size = re.search(
            r"#define\s+MAX_ATTACHMENTS_IN_POOL\s+"
            r"\(\s*(\d+)\s*\+\s*VK_NUM_BLOOM_PASSES\s*\*\s*2\s*\)",
            self.header,
        )

        self.assertIsNotNone(bloom_passes)
        self.assertIsNotNone(pool_size)
        assert bloom_passes is not None and pool_size is not None

        # Nine bloom images plus twelve possible non-bloom attachments:
        # scene, motion, liquid, screen-map color/MSAA/depth, main MSAA or
        # depth snapshot, capture, main depth, and three shadow atlases.
        capacity = int(pool_size.group(1)) + int(bloom_passes.group(1)) * 2
        self.assertGreaterEqual(capacity, 21)


if __name__ == "__main__":
    unittest.main()
