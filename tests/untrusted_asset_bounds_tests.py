"""Regression gates for the bounds/lifetime fixes applied to untrusted input paths.

Every case here guards a defect that was reachable from data an attacker can
supply: a downloaded BSP, a pk3 asset, a network snapshot, or a client's own
reliable-command stream. The assertions are deliberately written against the
guard rather than the surrounding code so unrelated refactors do not trip them.
"""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

RENDERERS = ("renderer", "renderervk", "rendererrtx")


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def body(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    return source[begin : source.index(end, begin)]


class CollisionModelBoundsTests(unittest.TestCase):
    """code/qcommon/cm_load.c -- BSP lumps are attacker-supplied."""

    def setUp(self) -> None:
        self.source = read("code/qcommon/cm_load.c")

    def test_visibility_lump_rejects_a_partial_header(self) -> None:
        block = body(self.source, "static void CMod_LoadVisibility", "CMod_LoadPatches")
        # len in 1..7 made `len - VIS_HEADER` negative and wrap in Com_Memcpy
        self.assertIn("if ( len < VIS_HEADER ) {", block)
        self.assertIn("truncated visibility lump", block)
        self.assertNotIn("if ( !len ) {", block)

    def test_visibility_cluster_table_must_fit_the_payload(self) -> None:
        block = body(self.source, "static void CMod_LoadVisibility", "CMod_LoadPatches")
        # CM_ClusterPVS() indexes visibility + cluster * clusterBytes unchecked
        self.assertIn("numClusters < 0 || clusterBytes <= 0", block)
        # must compare by division: the product overflows on the 32-bit target
        self.assertIn("numClusters > ( len - VIS_HEADER ) / clusterBytes", block)
        self.assertNotIn("(size_t)numClusters * (size_t)clusterBytes", block)
        # a mismatch degrades to fullvis rather than dropping the map
        self.assertIn("CMod_SetFullVisibility();", block)

    def test_full_visibility_fallback_keeps_the_leaf_derived_cluster_count(self) -> None:
        block = body(self.source, "static void CMod_SetFullVisibility", "static void CMod_LoadVisibility")
        self.assertIn("cm.vised = qfalse;", block)
        self.assertIn("cm.clusterBytes = ( cm.numClusters + 31 ) & ~31;", block)
        self.assertIn("Com_Memset( cm.visibility, 255, cm.clusterBytes );", block)

    def test_patch_vertex_range_is_bounded_before_the_read_loop(self) -> None:
        block = body(self.source, "static void CMod_LoadPatches", "//====")
        self.assertIn("numDrawVerts = verts->filelen / sizeof(*dv);", block)
        self.assertIn(
            "if ( c < 0 || firstVert < 0 || firstVert > numDrawVerts || c > numDrawVerts - firstVert ) {",
            block,
        )
        # the guard must precede the copy that uses it
        self.assertLess(block.index("numDrawVerts - firstVert"), block.index("dv_p = dv + firstVert;"))
        # and the raw unbounded form must be gone
        self.assertNotIn("dv + LittleLong( in->firstVert )", block)

    def test_patch_shader_index_is_bounded_like_its_siblings(self) -> None:
        block = body(self.source, "static void CMod_LoadPatches", "//====")
        self.assertIn("if ( shaderNum < 0 || shaderNum >= cm.numShaders ) {", block)
        self.assertLess(block.index("shaderNum >= cm.numShaders"), block.index("cm.shaders[shaderNum].contentFlags"))

    def test_patch_loader_does_not_print_per_surface(self) -> None:
        block = body(self.source, "static void CMod_LoadPatches", "//====")
        self.assertNotIn("CM_LoadPatches: surface", block)

    def test_advertisement_lump_is_parsed_before_the_file_is_released(self) -> None:
        block = body(self.source, "CMod_CheckLeafBrushes();", "CM_InitBoxHull();")
        self.assertLess(
            block.index("QLBSP_ReadAdvertisementLump( buf"),
            block.index("FS_FreeFile( buf );"),
        )
        self.assertLess(
            block.index("QLBSP_ReadAdvertisementLump( buf"),
            block.index("Z_Free( translated.data );"),
        )


class ServerUntrustedInputTests(unittest.TestCase):
    def test_server_command_overflow_collapses_the_window_before_dropping(self) -> None:
        source = read("code/server/sv_main.cpp")
        block = body(source, "void SV_AddServerCommand", "void QDECL SV_SendServerCommand")
        # SV_DropClient() broadcasts a print that re-enters this function while
        # the client is still CS_ACTIVE; without this the recursion is unbounded
        self.assertIn("client->reliableAcknowledge = client->reliableSequence;", block)
        self.assertLess(
            block.index("client->reliableAcknowledge = client->reliableSequence;"),
            block.index('SV_DropClient( client, "Server command overflow" );'),
        )
        # the stale comment claiming an == test guards recursion must be gone
        self.assertNotIn("we check == instead of >=", block)

    def test_clients_are_freed_before_the_server_struct_is_cleared(self) -> None:
        source = read("code/server/sv_init.cpp")
        block = body(source, "SV_RemoveOperatorCommands();", 'Cvar_Set( "sv_running", "0" );')
        # SV_Clients() is bounded by sv.maxclients, which SV_ClearServer() zeroes
        self.assertLess(block.index("SV_FreeClient( &client );"), block.index("SV_ClearServer();"))
        self.assertLess(block.index("SV_ClearServer();"), block.index("SV_ZFree( svs.clients );"))

    def test_locate_game_data_rejects_a_zero_entity_count(self) -> None:
        source = read("code/server/sv_game.cpp")
        block = body(source, "static void SV_LocateGameData", "sv.gentities = gEnts;")
        # numGEntities is the divisor below, in an unsigned division that traps
        self.assertIn("if ( numGEntities <= 0 || numGEntities > MAX_GENTITIES ) {", block)
        self.assertLess(
            block.index("numGEntities <= 0"),
            block.index("gvm->exactDataLength / numGEntities"),
        )

    def test_trace_scratch_arrays_are_not_value_initialized(self) -> None:
        source = read("code/server/sv_world.cpp")
        # 4 KB of dead memset per trace in the hottest server path
        self.assertIn("std::array<int, MAX_GENTITIES> touchlist;", source)
        self.assertIn("std::array<int, MAX_GENTITIES> touch;", source)
        self.assertNotIn("std::array<int, MAX_GENTITIES> touchlist{};", source)
        self.assertNotIn("std::array<int, MAX_GENTITIES> touch{};", source)


class ClientUntrustedInputTests(unittest.TestCase):
    def test_demo_snapshot_entity_copy_is_clamped(self) -> None:
        source = read("code/client/cl_main.cpp")
        block = body(source, "static void CL_WriteSnapshot", "CL_Record_f")
        self.assertIn("if ( savedEntities > MAX_SNAPSHOT_ENTITIES )", block)
        self.assertIn("savedEntities = MAX_SNAPSHOT_ENTITIES;", block)
        self.assertIn("for ( i = 0; i < savedEntities; i++ )", block)
        # load-bearing: this count feeds oldSnap->numEntities on the next pass
        self.assertIn("saved_snap.numEntities = savedEntities;", block)
        self.assertLess(block.index("saved_snap = *snap;"), block.index("saved_snap.numEntities = savedEntities;"))

    def test_roq_header_rejection_stops_playback(self) -> None:
        source = read("code/client/cl_cin.cpp")
        self.assertIn("static qboolean RoQ_init( void );", source)
        init = body(source, "static qboolean RoQ_init( void )\n{", "\n}\n")
        self.assertIn("cinTable[currentHandle].RoQFrameSize = 0;", init)
        self.assertIn("return qfalse;", init)
        # both callers must honour it -- each overwrites status right after
        self.assertIn("if ( !RoQ_init() ) {", source)
        self.assertIn("if (RoQID == 0x1084 && RoQ_init())", source)

    def test_roq_audio_decode_is_clamped_to_the_stack_buffer(self) -> None:
        source = read("code/client/cl_cin.cpp")
        block = body(source, "case\tZA_SOUND_MONO:", "case\tROQ_QUAD_INFO:")
        # RllDecodeMonoToStereo writes two shorts per input byte
        self.assertIn("if ( decodeBytes > sbuf.size() / 2 )", block)
        # RllDecodeStereoToStereo writes one short per input byte
        self.assertIn("if ( decodeBytes > sbuf.size() )", block)
        self.assertNotIn("sbuf.data(), cinTable[currentHandle].RoQFrameSize", block)

    def test_roq_chunk_walk_stays_inside_the_file_buffer(self) -> None:
        source = read("code/client/cl_cin.cpp")
        # the buffer carries the trailing chunk header a 65536-byte chunk implies
        self.assertIn("std::array<byte, 65536 + 8> file;", source)
        block = body(source, "static void RoQInterrupt()", "static qboolean RoQ_init( void )")
        self.assertIn("if ( frameOffset + 8 > cin.file.size() ) {", block)
        self.assertNotIn("framedata\t\t += cinTable[currentHandle].RoQFrameSize;", block)
        # the legal chunk-size guard must NOT have been tightened
        self.assertIn("RoQFrameSize>65536", block)


class SharedImageLoaderTests(unittest.TestCase):
    """code/renderercommon -- one copy, compiled into all three backends."""

    def test_pcx_truncation_path_returns_instead_of_falling_through(self) -> None:
        source = read("code/renderercommon/tr_image_pcx.c")
        block = body(source, "if(pix < pic8+size)", "palette = end-768;")
        # without the return this fell into a use-after-free plus a double free
        truncation = block[: block.index("}")]
        self.assertIn("ri.FS_FreeFile (pcx);", truncation)
        self.assertIn("ri.Free (pic8);", truncation)
        self.assertIn("return;", truncation)

    def test_pcx_palette_bound_is_a_real_test(self) -> None:
        source = read("code/renderercommon/tr_image_pcx.c")
        # the old clause compared a file offset against the address of end-769
        self.assertNotIn("end - (byte*)769", source)
        self.assertIn("if (len < 769 || raw.b > end - 769 || end[-769] != 0x0c)", source)

    def test_pcx_unsupported_header_frees_the_file(self) -> None:
        source = read("code/renderercommon/tr_image_pcx.c")
        block = body(source, "Bad or unsupported pcx file", "pix = pic8 = ri.Malloc")
        self.assertIn("ri.FS_FreeFile (pcx);", block)

    def test_png_chunks_use_subtraction_checked_payload_and_crc_bounds(self) -> None:
        source = read("code/renderercommon/tr_image_png.c")
        helper = body(source, "static qboolean BufferedFileSkipChunkData", "PNG_GetPixelLayout")
        self.assertIn("BF->BytesLeft < 0", helper)
        self.assertIn("Length > (uint32_t)BF->BytesLeft", helper)
        self.assertIn("BF->BytesLeft - (int)Length", helper)
        self.assertNotIn("Length + PNG_ChunkCRC_Size", source)

    def test_png_inflate_is_bounded_by_the_validated_ihdr_layout(self) -> None:
        source = read("code/renderercommon/tr_image_png.c")
        decompress = body(source, "static uint32_t DecompressIDATs", "the Paeth predictor")
        self.assertIn("uint32_t ExpectedDataLength", decompress)
        self.assertIn("DecompressedData = ri.Malloc((int)ExpectedDataLength);", decompress)
        self.assertIn("puffDestLen != ExpectedDataLength", decompress)
        self.assertNotIn("puff(NULL", decompress)
        self.assertIn("ChunkHeaderLength > 256 * 3", source)


class QVMInputBoundaryTests(unittest.TestCase):
    def test_header_ranges_are_checked_without_signed_addition(self) -> None:
        bounds = read("code/qcommon/vm_header_bounds.h")
        self.assertIn("header->codeOffset < headerSize", bounds)
        self.assertIn("header->codeLength > remaining", bounds)
        self.assertIn("header->dataLength > remaining", bounds)
        self.assertIn("header->jtrgLength != remaining", bounds)
        self.assertIn("header->bssLength < 0", bounds)
        self.assertNotIn("codeOffset + header->codeLength", bounds)

    def test_instruction_count_is_bounded_before_decode_and_allocation(self) -> None:
        bounds = read("code/qcommon/vm_header_bounds.h")
        self.assertIn("header->instructionCount <= 0", bounds)
        self.assertIn("header->instructionCount > header->codeLength", bounds)
        self.assertIn("maxInstructionCount", bounds)

        source = read("code/qcommon/vm.c")
        loader = body(source, "const char *VM_LoadInstructions", "static qboolean safe_address")
        self.assertLess(loader.index("code_pos >= code_end"), loader.index("op0 = *code_pos"))
        self.assertIn("code_end - code_pos < 1 + n", loader)


class FilesystemWriteBoundaryTests(unittest.TestCase):
    def test_all_public_write_open_paths_validate_qpaths_before_opening(self) -> None:
        source = read("code/qcommon/files.c")
        for start, end in (
            ("fileHandle_t FS_SV_FOpenFileWrite", "FS_SV_FOpenFileRead"),
            ("fileHandle_t FS_FOpenProfileFileWrite", "FS_FOpenProfileFileRead"),
            ("fileHandle_t FS_FOpenFileWrite", "FS_FOpenFileAppend"),
            ("fileHandle_t FS_FOpenFileAppend", "FS_FOpenFileRead"),
        ):
            with self.subTest(function=start):
                block = body(source, start, end)
                self.assertIn("QpathIsValid", block)
                self.assertLess(block.index("QpathIsValid"), block.index("Sys_FOpen"))

        pipe = body(source, "fileHandle_t FS_PipeOpenWrite", "void FS_PipeClose")
        self.assertIn("FS_WriteQpathIsValid( filename )", pipe)
        self.assertLess(pipe.index("FS_WriteQpathIsValid"), pipe.index("_popen"))

    def test_write_lengths_and_handles_are_validated_at_both_boundaries(self) -> None:
        source = read("code/qcommon/files.c")
        core = body(source, "int FS_Write", "FS_Printf")
        vm = body(source, "void FS_VM_WriteFile", "FS_VM_Seek")
        self.assertIn("FS_WriteRequestIsValid( buffer, len, h )", core)
        self.assertIn("FS_WriteRequestIsValid( buffer, len, f )", vm)
        self.assertIn("if ( len == 0 )", core)

    def test_raw_remove_is_private_and_qpath_removes_are_rooted(self) -> None:
        source = read("code/qcommon/files.c")
        header = read("code/qcommon/qcommon.h")
        self.assertIn("static void FS_Remove( const char *osPath )", source)
        self.assertNotIn("void FS_Remove( const char *osPath );", header)
        self.assertIn("void FS_SV_HomeRemove( const char *osPath )", source)
        self.assertIn("FS_WriteQpathIsValid( osPath )", source)

    def test_rename_checks_paths_and_blocked_extensions_before_mutation(self) -> None:
        source = read("code/qcommon/files.c")
        for start, end in (
            ("void FS_SV_Rename", "void FS_Rename"),
            ("void FS_Rename", "#ifdef USE_HANDLE_CACHE"),
        ):
            with self.subTest(function=start):
                block = body(source, start, end)
                self.assertIn("FS_WriteQpathIsValid( from )", block)
                self.assertIn("FS_CheckFilenameIsNotAllowed( to", block)
                self.assertLess(
                    block.index("FS_CheckFilenameIsNotAllowed( to"),
                    block.index("Sys_ReplaceFile"),
                )

        extensions = body(source, "qboolean FS_AllowedExtension", "FS_CheckFilenameIsNotAllowed")
        self.assertLess(extensions.index("if ( !e )"), extensions.index("e - fileName >= 3"))


class HuffmanInputBoundaryTests(unittest.TestCase):
    def test_decoder_is_bounded_and_connectionless_packets_honor_failure(self) -> None:
        decoder = read("code/qcommon/huffman.c")
        server = read("code/server/sv_main.cpp")
        self.assertIn("(size_t)bloc >= bitCount", decoder)
        self.assertIn("if (!Huff_Receive", decoder)
        self.assertIn("qboolean Huff_Decompress", decoder)
        self.assertIn("if ( !Huff_Decompress( msg, 12 ) )", server)

class RendererModelLoaderTests(unittest.TestCase):
    """The three backends carry independent copies -- each must be gated."""

    def test_mdr_bone_count_is_bounded_by_the_render_time_array(self) -> None:
        for renderer in RENDERERS:
            with self.subTest(renderer=renderer):
                source = read(f"code/{renderer}/tr_model.c")
                block = body(
                    source,
                    "static qboolean R_LoadMDR( model_t *mod, void *buffer, int filesize, const char *mod_name )",
                    "mod->dataSize += size;",
                )
                # RB_MDRSurfaceAnim lerps into mdrBone_t[MDR_MAX_BONES]
                self.assertIn("pinmodel->numBones < 0 || pinmodel->numBones > MDR_MAX_BONES", block)
                # numFrames feeds the size arithmetic and must be checked first
                self.assertIn("if(pinmodel->numFrames < 1)", block)
                self.assertLess(
                    block.index("pinmodel->numFrames < 1"),
                    block.index("size += pinmodel->numFrames * sizeof(frame->name);"),
                )

    def test_mdr_bone_array_is_still_sized_by_the_same_constant(self) -> None:
        self.assertIn("#define\tMDR_MAX_BONES\t128", read("code/qcommon/qfiles.h"))
        for renderer in RENDERERS:
            with self.subTest(renderer=renderer):
                self.assertIn("bones[MDR_MAX_BONES]", read(f"code/{renderer}/tr_animation.c"))

    def test_md3_xyznormals_bound_includes_frames_and_uses_the_hunk_size(self) -> None:
        for renderer in RENDERERS:
            with self.subTest(renderer=renderer):
                source = read(f"code/{renderer}/tr_model.c")
                block = body(source, "// swap all the surfaces", "// find the next surface")
                self.assertIn(
                    "(int64_t)surf->numVerts * surf->numFrames * sizeof( md3XyzNormal_t ) > size",
                    block,
                )
                # every surface offset is now measured from hdr, against size
                self.assertIn("surfBase = (int64_t)( (byte *)surf - (byte *)hdr );", block)
                self.assertNotIn("fileSize", block)
                # a surface may not declare fewer frames than the model
                self.assertIn("surf->numFrames < hdr->numFrames", block)

    def test_md3_counts_over_batch_capacity_are_rejected_before_size_use(self) -> None:
        for renderer in RENDERERS:
            with self.subTest(renderer=renderer):
                source = read(f"code/{renderer}/tr_model.c")
                block = body(source, "// swap all the surfaces", "// find the next surface")
                # Retail-sized surfaces may exactly fill a batch; only larger
                # declarations are unsafe and must be rejected here.
                self.assertIn("surf->numVerts < 0 || surf->numVerts > SHADER_MAX_VERTEXES", block)
                self.assertIn("surf->numTriangles < 0 || surf->numTriangles*3 > SHADER_MAX_INDEXES", block)
                self.assertLess(
                    block.index("SHADER_MAX_VERTEXES"),
                    block.index("surfBase = (int64_t)"),
                )


class RendererLightmapTests(unittest.TestCase):
    def test_shader_lightmap_index_is_bounded_by_the_page_count(self) -> None:
        for renderer in RENDERERS:
            with self.subTest(renderer=renderer):
                source = read(f"code/{renderer}/tr_shader.c")
                block = body(source, '"*lightmap", 9 ) == 0', "imgFlags_t flags = IMGFLAG_NONE;")
                self.assertIn("page >= tr.numLightmaps", block)
                self.assertIn("tr.lightmaps[page]", block)
                # R_GetLightmapCoords()/FinishStage() select the page with '/'
                self.assertIn("tr.mergeLightmaps ? ( lightmapIndex / tr.lightmapMod ) : lightmapIndex", block)
                self.assertNotIn("tr.lightmaps[lightmapIndex % tr.lightmapMod]", block)
                # the LIGHTMAP_INDEX_OFFSET tag must survive so FinishStage still corrects it
                self.assertIn("LIGHTMAP_INDEX_OFFSET + lightmapIndex", block)

    def test_finish_stage_lightmap_lookup_is_bounded(self) -> None:
        for renderer in RENDERERS:
            with self.subTest(renderer=renderer):
                source = read(f"code/{renderer}/tr_shader.c")
                self.assertIn(
                    "if ( tr.lightmaps != NULL && lightmapIndex >= 0 && lightmapIndex < tr.numLightmaps ) {",
                    source,
                )

    def test_lightmap_pointer_is_cleared_on_every_map_load(self) -> None:
        for renderer in RENDERERS:
            with self.subTest(renderer=renderer):
                source = read(f"code/{renderer}/tr_bsp.c")
                block = body(source, "static void R_LoadLightmaps", "numLightmaps = l->filelen /")
                # the early returns below would otherwise leave a dangling
                # pointer into the previous map's hunk block
                self.assertIn("tr.numLightmaps = 0;", block)
                self.assertIn("tr.lightmaps = NULL;", block)


class VulkanRendererTests(unittest.TestCase):
    def test_rtx_compaction_preserves_the_acceleration_structure_valid_flag(self) -> None:
        source = read("code/rendererrtx/vk.c")
        # anchor on the definition; a forward declaration appears far earlier
        block = body(source, "uint64_t *savedBytes )\n{", "\n}\n")
        # vk_rt_create_as() zeroes the struct, so the copy cleared the flag
        self.assertIn("*as = compactedAs;", block)
        self.assertIn("as->valid = qtrue;", block)
        self.assertLess(block.index("*as = compactedAs;"), block.index("as->valid = qtrue;"))

    def test_renderervk_depth_barriers_cover_the_stencil_aspect(self) -> None:
        source = read("code/renderervk/vk.c")
        block = body(source, "static void record_image_layout_transition", "switch ( old_layout )")
        self.assertIn("glConfig.stencilBits > 0", block)
        self.assertIn("image_aspect_flags |= VK_IMAGE_ASPECT_STENCIL_BIT;", block)
        for image in (
            "vk.depth_image",
            "vk.depth_fade_image",
            "vk.dlight_shadow_image",
            "vk.spot_shadow_image",
            "vk.csm_shadow_image",
        ):
            self.assertIn(image, block)

    def test_sampler_descriptor_pools_budget_every_bloom_set(self) -> None:
        # bloom_image_descriptor is [1 + VK_NUM_BLOOM_PASSES*2]; the pools
        # reserved only VK_NUM_BLOOM_PASSES*2
        for renderer in ("renderervk", "rendererrtx"):
            with self.subTest(renderer=renderer):
                header = read(f"code/{renderer}/vk.h")
                self.assertIn("bloom_image_descriptor[1+VK_NUM_BLOOM_PASSES*2]", header)
                source = read(f"code/{renderer}/vk.c")
                pool = body(source, "pool_size[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;", "pool_size[1].type")
                self.assertIn("( 1 + VK_NUM_BLOOM_PASSES * 2 )", pool)
                # the bare, unparenthesised term must be gone
                self.assertNotIn("+ VK_NUM_BLOOM_PASSES * 2;", pool)
                self.assertNotIn("\t\t\tVK_NUM_BLOOM_PASSES * 2;", pool)


class PlatformRobustnessTests(unittest.TestCase):
    """Not built on Windows; gated by source inspection instead."""

    def test_win32_gl_hard_failure_releases_the_device_context(self) -> None:
        source = read("code/win32/win_glimp.cpp")
        block = body(source, "if ( tpfd == TRY_PFD_FAIL_HARD )", "punt if we've already tried")
        self.assertIn("ReleaseDC( g_wv.hWnd, glw_state.hDC );", block)
        self.assertIn("glw_state.hDC = NULL;", block)

    def test_win32_window_teardown_resets_the_pixel_format_latch(self) -> None:
        source = read("code/win32/win_glimp.cpp")
        block = body(source, "DestroyWindow( g_wv.hWnd );", "SetForegroundWindow")
        # otherwise the 16-bit retry skips both GLW_MakeContext attempts
        self.assertIn("glw_state.pixelFormatSet = qfalse;", block)

    def test_win32_console_append_cannot_underflow(self) -> None:
        source = read("code/win32/win_syscon.cpp")
        # sizeof - strlen - 2 wrapped to SIZE_MAX once the buffer saturated
        self.assertNotIn("strncat( s_wcd.consoleText", source)
        self.assertIn("Q_strcat( s_wcd.consoleText, sizeof( s_wcd.consoleText ), s );", source)
        # flush first, matching unix SysCon_AppendSubmittedCommand
        self.assertIn(
            "if ( strlen( s_wcd.consoleText ) + strlen( s ) + 2 >= sizeof( s_wcd.consoleText ) ) {",
            source,
        )

    def test_x11_nearest_monitor_rebases_and_clamps_the_index(self) -> None:
        source = read("code/unix/x11_randr.cpp")
        block = body(source, "static monitor_t *FindNearestMonitor", "search by nearest distance")
        self.assertIn("i = ( cx - minx ) / slen;", block)
        self.assertIn("if ( i >= cnt )", block)
        self.assertNotIn("return list[ cx / slen ];", block)

    def test_x11_window_minimized_validates_the_property_reply(self) -> None:
        source = read("code/unix/linux_glimp.cpp")
        block = body(source, "static qboolean WindowMinimized", "static int X11_CardinalCoordinate")
        for initialization in (
            "unsigned long numItems = 0;",
            "unsigned long bytesAfter = 0;",
            "Atom actualType = None;",
            "unsigned char *propertyData = NULL;",
            "int actualFormat = 0;",
        ):
            self.assertIn(initialization, block)
        self.assertIn(") != Success )", block)
        self.assertIn(
            "actualType == None && actualFormat == 0 && numItems == 0",
            block,
        )
        self.assertIn(
            "actualType != XA_ATOM || actualFormat != 32 ||",
            block,
        )
        self.assertIn("bytesAfter != 0", block)
        self.assertIn("kMaximumWindowStateAtoms", block)
        self.assertNotIn("0x7FFFFFFF", block)
        self.assertIn("( numItems > 0 && !propertyData )", block)
        self.assertIn("XFree( propertyData );", block)
        # validation must precede the loop that dereferences atoms
        self.assertLess(
            block.index("actualFormat != 32"),
            block.index("if ( atoms[i] == netWMStateHidden )"),
        )


if __name__ == "__main__":
    unittest.main()
