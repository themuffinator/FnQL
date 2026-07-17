CMAKE_MINIMUM_REQUIRED(VERSION 3.12)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version/fnql_version.h" FNQL_VERSION_STRING_LINE REGEX "^#define FNQL_VERSION_STRING \"[^\"]+\"$")
string(REGEX REPLACE "^#define FNQL_VERSION_STRING \"([^\"]+)\"$" "\\1" FNQL_VERSION_STRING "${FNQL_VERSION_STRING_LINE}")

PROJECT(FnQL VERSION ${FNQL_VERSION_STRING} LANGUAGES C CXX)

find_package(Threads REQUIRED)

SET(CMAKE_CXX_STANDARD 17)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)
SET(CMAKE_CXX_EXTENSIONS OFF)

IF(MSVC)
	# Standard-library templates use exception-aware cleanup even though the
	# engine does not throw across its C ABI boundaries.
	ADD_COMPILE_OPTIONS(
		$<$<COMPILE_LANGUAGE:CXX>:/EHsc>
		$<$<COMPILE_LANGUAGE:C>:/wd4201>
		$<$<COMPILE_LANGUAGE:CXX>:/wd4201>)
ENDIF()

SET(CNAME "fnql")
SET(DNAME "fnql.ded")

SET(RENDERER_PREFIX ${CNAME})
SET(RENDERER_DEFAULT opengl CACHE STRING "Default renderer: opengl, glx, vulkan, or opengl2")
set_property(CACHE RENDERER_DEFAULT PROPERTY STRINGS opengl glx vulkan opengl2)

OPTION(USE_SDL "" ON)
OPTION(USE_CURL "" ON)
OPTION(USE_VULKAN "" ON)
OPTION(USE_GLX "Build the GLx OpenGL-lineage renderer module" ON)
OPTION(USE_SYSTEM_JPEG "Use a system libjpeg instead of the Meson-fetched libjpeg-turbo source tree" OFF)
OPTION(USE_RENDERER_DLOPEN "" ON)
OPTION(FNQL_AUDIO_LOOPBACK_TESTS "Build deterministic audio runtime and OpenAL loopback tests" ON)
OPTION(FNQL_GLX_LOGIC_TESTS "Build deterministic GLx renderer logic tests" ON)

IF(RENDERER_DEFAULT STREQUAL "glx")
	SET(USE_GLX ON CACHE BOOL "Build the GLx OpenGL-lineage renderer module" FORCE)
ENDIF()

IF(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	# win_resource.rc already embeds q3.manifest; CMake's Windows-Clang rule
	# can add a generated manifest that lld-link rejects as a duplicate resource.
	FOREACH(lang C CXX)
		STRING(REPLACE " -Xlinker /MANIFEST:EMBED" "" CMAKE_${lang}_LINK_EXECUTABLE "${CMAKE_${lang}_LINK_EXECUTABLE}")
		STRING(REPLACE " /MANIFEST:NO" "" CMAKE_${lang}_LINK_EXECUTABLE "${CMAKE_${lang}_LINK_EXECUTABLE}")
	ENDFOREACH()
	IF(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
		SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /MANIFEST:NO")
	ENDIF()
ENDIF()

IF(NOT USE_RENDERER_DLOPEN AND RENDERER_DEFAULT STREQUAL "glx")
	SET(USE_VULKAN OFF)
ENDIF()

AUX_SOURCE_DIRECTORY(code/qcommon QCOMMON_SRCS)
list(APPEND QCOMMON_SRCS
	code/platform/fnql_steam.cpp
	code/platform/fnql_workshop.cpp)
# exclude platform-dependent QVM bytecode compilers
list(FILTER QCOMMON_SRCS EXCLUDE REGEX ".*vm_(aarch64|armv7l|powerpc|x86)\\.c$")
# add platform-dependent QVM bytecode compilers
IF(CMAKE_SYSTEM_PROCESSOR MATCHES AMD64|x86|i*86)
	IF(CMAKE_SYSTEM_PROCESSOR MATCHES AMD64)
		IF(WIN32)
			SET(BINEXT .x64)
		ELSE()
			SET(BINEXT .x86_64)
		ENDIF()
		SET(RENDEXT _x86_64)
	ELSE()
		#SET(BINEXT .x86)
		SET(RENDEXT _x86)
	ENDIF()
	list(APPEND QCOMMON_SRCS code/qcommon/vm_x86.c)
ELSEIF(CMAKE_SYSTEM_PROCESSOR MATCHES aarch64|arm64)
	SET(BINEXT .aarch64)
	SET(RENDEXT _aarch64)
	list(APPEND QCOMMON_SRCS code/qcommon/vm_aarch64.c)
ELSEIF(CMAKE_SYSTEM_PROCESSOR MATCHES arm*)
	SET(BINEXT .arm)
	SET(RENDEXT _arm)
	list(APPEND QCOMMON_SRCS code/qcommon/vm_armv7l.c)
ELSEIF(CMAKE_SYSTEM_PROCESSOR MATCHES "ppc64le|ppc64")
	IF(CMAKE_SYSTEM_PROCESSOR MATCHES "ppc64le")
		SET(BINEXT .ppc64le)
		SET(RENDEXT _ppc64le)
		list(APPEND QCOMMON_SRCS code/qcommon/vm_powerpc.c)
	ELSE()
		SET(BINEXT .ppc64)
		SET(RENDEXT _ppc64)
		list(APPEND QCOMMON_SRCS code/qcommon/vm_powerpc.c)
	ENDIF()
ELSE()
	SET(NO_VM_COMPILED ON)
ENDIF()

SET(PLATFORM_ASM_SRCS)
IF(MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
	enable_language(ASM_MASM)
	list(APPEND PLATFORM_ASM_SRCS code/asm/common_x64.asm)
ENDIF()

# botlib
AUX_SOURCE_DIRECTORY(code/botlib BOTLIB_SRCS)
ADD_LIBRARY(botlib OBJECT ${BOTLIB_SRCS})
TARGET_COMPILE_DEFINITIONS(botlib PRIVATE BOTLIB)

SET(SERVER_SRCS
	code/server/factory_catalog.cpp
	code/server/factory_rotation.cpp
	code/server/sv_bot.cpp
	code/server/sv_ccmds.cpp
	code/server/sv_client.cpp
	code/server/sv_filter.cpp
	code/server/sv_game.cpp
	code/server/sv_init.cpp
	code/server/sv_main.cpp
	code/server/sv_net_chan.cpp
	code/server/sv_platform.cpp
	code/server/sv_factory.cpp
	code/server/sv_snapshot.cpp
	code/server/sv_world.cpp
	code/server/sv_zmq.cpp
)

# universal binary
ADD_LIBRARY(qcommon OBJECT ${QCOMMON_SRCS} ${SERVER_SRCS} ${PLATFORM_ASM_SRCS})
TARGET_LINK_LIBRARIES(qcommon PRIVATE Threads::Threads)

# dedicated server
ADD_LIBRARY(qcommon_ded OBJECT ${QCOMMON_SRCS} ${SERVER_SRCS} ${PLATFORM_ASM_SRCS})
TARGET_COMPILE_DEFINITIONS(qcommon_ded PUBLIC DEDICATED)
TARGET_LINK_LIBRARIES(qcommon_ded PRIVATE Threads::Threads)

IF(NO_VM_COMPILED)
	TARGET_COMPILE_DEFINITIONS(qcommon PRIVATE NO_VM_COMPILED)
	TARGET_COMPILE_DEFINITIONS(qcommon_ded PRIVATE NO_VM_COMPILED)
ENDIF()

# client + cURL
AUX_SOURCE_DIRECTORY(code/client CLIENT_SRCS)
list(APPEND CLIENT_SRCS
	code/client/audio/snd_main.cpp
	code/client/audio/legacy/snd_adpcm.cpp
	code/client/audio/legacy/snd_dma.cpp
	code/client/audio/legacy/snd_mem.cpp
	code/client/audio/legacy/snd_mix.cpp
	code/client/audio/legacy/snd_wavelet.cpp
	code/client/audio/codecs/snd_codec.cpp
	code/client/audio/codecs/snd_codec_ogg.cpp
	code/client/audio/codecs/snd_codec_wav.cpp
	code/client/audio/openal/AudioSystem.cpp)
IF(NOT USE_CURL)
	list(REMOVE_ITEM CLIENT_SRCS code/client/cl_curl.cpp)
ENDIF()

#ADD_LIBRARY(client OBJECT ${CLIENT_SRCS})

# jpeg
set(FNQL_CLIENT_LINK_LIBS)
IF(USE_SYSTEM_JPEG)
	ADD_LIBRARY(client OBJECT ${CLIENT_SRCS})
	find_package(JPEG REQUIRED)
	TARGET_COMPILE_DEFINITIONS(client PRIVATE USE_SYSTEM_JPEG)
	TARGET_INCLUDE_DIRECTORIES(client PRIVATE ${JPEG_INCLUDE_DIRS})
	list(APPEND FNQL_CLIENT_LINK_LIBS ${JPEG_LIBRARIES})
ELSE()
	file(GLOB FNQL_LIBJPEG_TURBO_CANDIDATES CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/subprojects/libjpeg-turbo-*")
	foreach(FNQL_LIBJPEG_TURBO_CANDIDATE IN LISTS FNQL_LIBJPEG_TURBO_CANDIDATES)
		if(EXISTS "${FNQL_LIBJPEG_TURBO_CANDIDATE}/CMakeLists.txt"
				AND EXISTS "${FNQL_LIBJPEG_TURBO_CANDIDATE}/src/jpeglib.h")
			set(FNQL_LIBJPEG_TURBO_SOURCE_DIR "${FNQL_LIBJPEG_TURBO_CANDIDATE}")
		endif()
	endforeach()
	if(NOT FNQL_LIBJPEG_TURBO_SOURCE_DIR)
		message(FATAL_ERROR "USE_SYSTEM_JPEG=OFF requires subprojects/libjpeg-turbo-* from the Meson wrap, or configure with -DUSE_SYSTEM_JPEG=ON and a system libjpeg")
	endif()

	include(ExternalProject)
	set(FNQL_LIBJPEG_TURBO_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/libjpeg-turbo")
	if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
		set(FNQL_LIBJPEG_TURBO_LIBRARY "${FNQL_LIBJPEG_TURBO_BINARY_DIR}/jpeg-static.lib")
	else()
		set(FNQL_LIBJPEG_TURBO_LIBRARY "${FNQL_LIBJPEG_TURBO_BINARY_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}jpeg${CMAKE_STATIC_LIBRARY_SUFFIX}")
	endif()
	ExternalProject_Add(fnql_libjpeg_turbo
		SOURCE_DIR "${FNQL_LIBJPEG_TURBO_SOURCE_DIR}"
		BINARY_DIR "${FNQL_LIBJPEG_TURBO_BINARY_DIR}"
		CMAKE_ARGS
			"-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
			"-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
			"-DENABLE_SHARED=OFF"
			"-DENABLE_STATIC=ON"
			"-DWITH_CRT_DLL=ON"
			"-DWITH_JPEG8=ON"
			"-DWITH_SIMD=OFF"
			"-DWITH_TESTS=OFF"
			"-DWITH_TOOLS=OFF"
			"-DWITH_TURBOJPEG=OFF"
		BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target jpeg-static
		INSTALL_COMMAND ""
		BUILD_BYPRODUCTS "${FNQL_LIBJPEG_TURBO_LIBRARY}")
	add_library(fnql_jpeg STATIC IMPORTED GLOBAL)
	set_target_properties(fnql_jpeg PROPERTIES
		IMPORTED_LOCATION "${FNQL_LIBJPEG_TURBO_LIBRARY}"
		INTERFACE_INCLUDE_DIRECTORIES "${FNQL_LIBJPEG_TURBO_BINARY_DIR};${FNQL_LIBJPEG_TURBO_SOURCE_DIR}/src")
	add_dependencies(fnql_jpeg fnql_libjpeg_turbo)
	ADD_LIBRARY(client OBJECT ${CLIENT_SRCS})
	add_dependencies(client fnql_libjpeg_turbo)
	TARGET_COMPILE_DEFINITIONS(client PRIVATE USE_SYSTEM_JPEG)
	TARGET_INCLUDE_DIRECTORIES(client PRIVATE "${FNQL_LIBJPEG_TURBO_BINARY_DIR}" "${FNQL_LIBJPEG_TURBO_SOURCE_DIR}/src")
	list(APPEND FNQL_CLIENT_LINK_LIBS fnql_jpeg)
ENDIF()

target_compile_features(client PRIVATE cxx_std_17)

set(FNQL_OPENAL_INCLUDE_DIRS)
set(FNQL_OPENAL_INCLUDE_CANDIDATES
	"${CMAKE_CURRENT_SOURCE_DIR}/code/openal/include")
file(GLOB FNQL_OPENAL_SOFT_INCLUDE_CANDIDATES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/subprojects/openal-soft-*/include")
list(APPEND FNQL_OPENAL_INCLUDE_CANDIDATES ${FNQL_OPENAL_SOFT_INCLUDE_CANDIDATES})
foreach(FNQL_OPENAL_INCLUDE_CANDIDATE IN LISTS FNQL_OPENAL_INCLUDE_CANDIDATES)
	if(EXISTS "${FNQL_OPENAL_INCLUDE_CANDIDATE}/AL/al.h")
		list(APPEND FNQL_OPENAL_INCLUDE_DIRS "${FNQL_OPENAL_INCLUDE_CANDIDATE}")
	endif()
endforeach()
if(FNQL_OPENAL_INCLUDE_DIRS)
	target_include_directories(client PRIVATE ${FNQL_OPENAL_INCLUDE_DIRS})
endif()

ADD_EXECUTABLE(fnql-audiozonesc code/tools/audiozones/audiozonesc.cpp)
target_compile_features(fnql-audiozonesc PRIVATE cxx_std_17)
target_include_directories(fnql-audiozonesc PRIVATE code/client/audio/shared)
IF(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
	TARGET_LINK_LIBRARIES(fnql-audiozonesc PRIVATE stdc++fs)
ENDIF()

IF(FNQL_AUDIO_LOOPBACK_TESTS)
	enable_testing()
	ADD_EXECUTABLE(fnql_audio_zone_tests tests/audio/audiozone_runtime_tests.cpp)
	target_compile_features(fnql_audio_zone_tests PRIVATE cxx_std_17)
	target_include_directories(fnql_audio_zone_tests PRIVATE code/client/audio/shared)
	add_test(NAME fnql_audio_zones COMMAND fnql_audio_zone_tests)
	add_test(NAME fnql_audio_zone_authoring
		COMMAND $<TARGET_FILE:fnql-audiozonesc> -o "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/audiozone_authoring_test.azb" "${CMAKE_CURRENT_SOURCE_DIR}/tests/audio/authoring_metadata.audiozones")
	add_test(NAME fnql_audio_zone_authoring_audit
		COMMAND $<TARGET_FILE:fnql-audiozonesc> --audit --strict --samples 512 "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/audiozone_authoring_test.azb")
	set_tests_properties(fnql_audio_zone_authoring PROPERTIES FIXTURES_SETUP fnql_audio_zone_authoring_fixture)
	set_tests_properties(fnql_audio_zone_authoring_audit PROPERTIES
		DEPENDS fnql_audio_zone_authoring
		FIXTURES_REQUIRED fnql_audio_zone_authoring_fixture)

	ADD_EXECUTABLE(fnql_audio_recovery_tests tests/audio/recovery_policy_tests.cpp)
	target_compile_features(fnql_audio_recovery_tests PRIVATE cxx_std_17)
	target_include_directories(fnql_audio_recovery_tests PRIVATE code/client/audio/shared)
	add_test(NAME fnql_audio_recovery COMMAND fnql_audio_recovery_tests)

	ADD_EXECUTABLE(fnql_audio_occlusion_tests tests/audio/occlusion_policy_tests.cpp)
	target_compile_features(fnql_audio_occlusion_tests PRIVATE cxx_std_17)
	target_include_directories(fnql_audio_occlusion_tests PRIVATE code/client/audio/shared)
	add_test(NAME fnql_audio_occlusion COMMAND fnql_audio_occlusion_tests)

	ADD_EXECUTABLE(fnql_audio_loopback_tests tests/audio/openal_loopback_tests.cpp)
	target_compile_features(fnql_audio_loopback_tests PRIVATE cxx_std_17)
	if(FNQL_OPENAL_INCLUDE_DIRS)
		target_include_directories(fnql_audio_loopback_tests PRIVATE ${FNQL_OPENAL_INCLUDE_DIRS})
	endif()
	IF(UNIX)
		TARGET_LINK_LIBRARIES(fnql_audio_loopback_tests PRIVATE m ${CMAKE_DL_LIBS})
	ENDIF()
	add_test(NAME fnql_audio_loopback COMMAND fnql_audio_loopback_tests)
	set_tests_properties(fnql_audio_loopback PROPERTIES SKIP_RETURN_CODE 77)

	find_package(Python3 COMPONENTS Interpreter)
	IF(Python3_Interpreter_FOUND)
		add_test(NAME fnql_audio_zone_sweep_script
			COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/audio/audio_zone_sweep_tests.py")
		add_test(NAME fnql_audio_zone_material_map
			COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/audio/audio_zone_material_map_tests.py" $<TARGET_FILE:fnql-audiozonesc>)
	ENDIF()
ENDIF()

IF(FNQL_GLX_LOGIC_TESTS)
	enable_testing()
	ADD_EXECUTABLE(fnql_glx_logic_tests
		tests/glx/glx_logic_tests.cpp
		code/rendererglx/glx_executor.cpp)
	target_compile_features(fnql_glx_logic_tests PRIVATE cxx_std_17)
	target_include_directories(fnql_glx_logic_tests PRIVATE
		code/rendererglx
		code/qcommon
		code/renderercommon)
	add_test(NAME fnql_glx_logic COMMAND fnql_glx_logic_tests)
	find_package(Python3 COMPONENTS Interpreter)
	IF(Python3_Interpreter_FOUND)
		add_test(NAME fnql_glx_header_boundary
			COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/glx/glx_header_boundary_tests.py")
	ENDIF()
ENDIF()

enable_testing()
ADD_EXECUTABLE(fnql_window_placement_tests tests/window_placement_tests.cpp)
target_compile_features(fnql_window_placement_tests PRIVATE cxx_std_17)
add_test(NAME fnql_window_placement COMMAND fnql_window_placement_tests)

ADD_EXECUTABLE(fnql_ql_font_text_tests tests/ql_font_text_tests.cpp)
target_compile_features(fnql_ql_font_text_tests PRIVATE cxx_std_17)
add_test(NAME fnql_ql_font_text COMMAND fnql_ql_font_text_tests)

ADD_EXECUTABLE(fnql_ql_bsp_contract_tests tests/ql_bsp_contract_tests.cpp)
target_compile_features(fnql_ql_bsp_contract_tests PRIVATE cxx_std_17)
target_include_directories(fnql_ql_bsp_contract_tests PRIVATE code/qcommon)
add_test(NAME fnql_ql_bsp_contract COMMAND fnql_ql_bsp_contract_tests)

ADD_EXECUTABLE(fnql_netchan_profile_tests tests/netchan_profile_tests.cpp)
target_compile_features(fnql_netchan_profile_tests PRIVATE cxx_std_17)
target_include_directories(fnql_netchan_profile_tests PRIVATE code/qcommon)
add_test(NAME fnql_netchan_profiles COMMAND fnql_netchan_profile_tests)

ADD_EXECUTABLE(fnql_netchan_codec_tests tests/netchan_codec_tests.cpp)
target_compile_features(fnql_netchan_codec_tests PRIVATE cxx_std_17)
target_include_directories(fnql_netchan_codec_tests PRIVATE code/qcommon)
add_test(NAME fnql_netchan_codec COMMAND fnql_netchan_codec_tests)

ADD_EXECUTABLE(fnql_demo_stream_tests tests/demo_stream_tests.cpp)
target_compile_features(fnql_demo_stream_tests PRIVATE cxx_std_17)
target_include_directories(fnql_demo_stream_tests PRIVATE code/client)
add_test(NAME fnql_demo_stream COMMAND fnql_demo_stream_tests)

ADD_EXECUTABLE(fnql_webpak_format_tests tests/webpak_format_tests.cpp)
target_compile_features(fnql_webpak_format_tests PRIVATE cxx_std_17)
target_include_directories(fnql_webpak_format_tests PRIVATE code/client)
add_test(NAME fnql_webpak_format COMMAND fnql_webpak_format_tests)

ADD_EXECUTABLE(fnql_webui_backend_tests tests/webui_backend_tests.cpp)
target_compile_features(fnql_webui_backend_tests PRIVATE cxx_std_17)
target_include_directories(fnql_webui_backend_tests PRIVATE code/client)
add_test(NAME fnql_webui_backend COMMAND fnql_webui_backend_tests)

ADD_EXECUTABLE(fnql_zmq_endpoint_tests tests/zmq_endpoint_tests.cpp)
target_compile_features(fnql_zmq_endpoint_tests PRIVATE cxx_std_17)
target_include_directories(fnql_zmq_endpoint_tests PRIVATE code/server)
add_test(NAME fnql_zmq_endpoint COMMAND fnql_zmq_endpoint_tests)

ADD_EXECUTABLE(fnql_factory_catalog_tests
	tests/factory_catalog_tests.cpp
	code/server/factory_catalog.cpp)
target_compile_features(fnql_factory_catalog_tests PRIVATE cxx_std_17)
target_include_directories(fnql_factory_catalog_tests PRIVATE code/server)
add_test(NAME fnql_factory_catalog COMMAND fnql_factory_catalog_tests)

ADD_EXECUTABLE(fnql_factory_rotation_tests
	tests/factory_rotation_tests.cpp
	code/server/factory_rotation.cpp)
target_compile_features(fnql_factory_rotation_tests PRIVATE cxx_std_17)
target_include_directories(fnql_factory_rotation_tests PRIVATE code/server)
add_test(NAME fnql_factory_rotation COMMAND fnql_factory_rotation_tests)

find_package(Python3 COMPONENTS Interpreter)
IF(Python3_Interpreter_FOUND)
	add_test(NAME fnql_ql_font_source
		COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/ql_font_source_tests.py")
	add_test(NAME fnql_window_management_source
		COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/window_management_source_tests.py")
	add_test(NAME fnql_ql_services_source
		COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/ql_services_source_tests.py")
	add_test(NAME fnql_ql_cvar_configstring_source
		COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/ql_cvar_configstring_source_tests.py")
	add_test(NAME fnql_join_menu_source
		COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/join_menu_source_tests.py")
	add_test(NAME fnql_ql_demo_source
		COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/ql_demo_source_tests.py")
	add_test(NAME fnql_workshop_source
		COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/workshop_source_tests.py")
	add_test(NAME fnql_cvar_factory_snapshot_source
		COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/cvar_factory_snapshot_tests.py")
	add_test(NAME fnql_factory_runtime_source
		COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/factory_runtime_source_tests.py")
	add_test(NAME fnql_map_overbright_cap_source
		COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/map_overbright_cap_source_tests.py")
	add_test(NAME fnql_retail_factory_probe
		COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/retail_factory_probe_tests.py" --meson)
	set_tests_properties(fnql_retail_factory_probe PROPERTIES SKIP_RETURN_CODE 77)
ENDIF()

# renderers
set(FNQL_FONTSTASH_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/subprojects/fontstash/src")
if(NOT EXISTS "${FNQL_FONTSTASH_INCLUDE_DIR}/fontstash.h")
	message(FATAL_ERROR
		"Retail host fonts require the pinned FontStash wrap. Run 'meson subprojects download fontstash' before configuring CMake.")
endif()
ADD_COMPILE_DEFINITIONS(BUILD_FONTSTASH)
INCLUDE_DIRECTORIES(SYSTEM "${FNQL_FONTSTASH_INCLUDE_DIR}")

AUX_SOURCE_DIRECTORY(code/renderercommon RENDERER_COMMON_SRCS)
AUX_SOURCE_DIRECTORY(code/renderer RENDERER_GL_SRCS)
AUX_SOURCE_DIRECTORY(code/rendererglx RENDERER_GLX_SRCS)
AUX_SOURCE_DIRECTORY(code/renderervk RENDERER_VK_SRCS)

IF(USE_RENDERER_DLOPEN)
	SET(AUX_SRCS
		code/qcommon/q_shared.c
		code/qcommon/puff.c
		code/qcommon/q_math.c
		${PLATFORM_ASM_SRCS}
	)
	TARGET_COMPILE_DEFINITIONS(client PRIVATE USE_RENDERER_DLOPEN RENDERER_PREFIX="${RENDERER_PREFIX}" RENDERER_DEFAULT=${RENDERER_DEFAULT})

	ADD_LIBRARY(${RENDERER_PREFIX}_opengl${RENDEXT} SHARED ${RENDERER_GL_SRCS} ${RENDERER_COMMON_SRCS} ${AUX_SRCS})
	TARGET_COMPILE_DEFINITIONS(${RENDERER_PREFIX}_opengl${RENDEXT} PRIVATE USE_RENDERER_DLOPEN)
	SET_TARGET_PROPERTIES(${RENDERER_PREFIX}_opengl${RENDEXT} PROPERTIES PREFIX "")

	IF(USE_GLX)
		ADD_LIBRARY(${RENDERER_PREFIX}_glx${RENDEXT} SHARED ${RENDERER_GL_SRCS} ${RENDERER_GLX_SRCS} ${RENDERER_COMMON_SRCS} ${AUX_SRCS})
		TARGET_COMPILE_DEFINITIONS(${RENDERER_PREFIX}_glx${RENDEXT} PRIVATE USE_RENDERER_DLOPEN RENDERER_GLX)
		SET_TARGET_PROPERTIES(${RENDERER_PREFIX}_glx${RENDEXT} PROPERTIES PREFIX "")
	ENDIF()

	ADD_LIBRARY(${RENDERER_PREFIX}_vulkan${RENDEXT} SHARED ${RENDERER_VK_SRCS} ${RENDERER_COMMON_SRCS} ${AUX_SRCS})
	TARGET_COMPILE_DEFINITIONS(${RENDERER_PREFIX}_vulkan${RENDEXT} PRIVATE USE_RENDERER_DLOPEN RENDERER_VULKAN)
	SET_TARGET_PROPERTIES(${RENDERER_PREFIX}_vulkan${RENDEXT} PROPERTIES PREFIX "")
ELSE()
	IF(RENDERER_DEFAULT STREQUAL "glx")
		ADD_LIBRARY(${RENDERER_PREFIX}_glx OBJECT ${RENDERER_GL_SRCS} ${RENDERER_GLX_SRCS} ${RENDERER_COMMON_SRCS})
		TARGET_COMPILE_DEFINITIONS(${RENDERER_PREFIX}_glx PRIVATE RENDERER_GLX)
	ELSEIF(USE_VULKAN)
		ADD_LIBRARY(${RENDERER_PREFIX}_vulkan OBJECT ${RENDERER_VK_SRCS} ${RENDERER_COMMON_SRCS})
		TARGET_COMPILE_DEFINITIONS(${RENDERER_PREFIX}_vulkan PRIVATE RENDERER_VULKAN)
	ELSE()
		ADD_LIBRARY(${RENDERER_PREFIX}_opengl OBJECT ${RENDERER_GL_SRCS} ${RENDERER_COMMON_SRCS})
	ENDIF()
ENDIF()

# common function, window system
IF(UNIX)
	SET (Q3_SRCS
		code/unix/unix_main.cpp
		code/unix/unix_shared.cpp
		code/unix/unix_syscon.cpp
		code/unix/linux_signals.cpp)

	IF(USE_SDL)
		SET(Q3_UI_SRCS
			code/sdl/sdl_gamma.cpp
			code/sdl/sdl_glimp.cpp
			code/sdl/sdl_input.cpp
			code/sdl/sdl_snd.cpp)
	ELSE()
		SET(Q3_UI_SRCS
			code/unix/linux_glimp.cpp
			code/unix/linux_qgl.cpp
			code/unix/linux_snd.cpp
			code/unix/x11_dga.cpp
			code/unix/x11_randr.cpp
			code/unix/x11_vidmode.cpp)
		IF(USE_VULKAN)
			list(APPEND Q3_UI_SRCS code/unix/linux_qvk.cpp)
		ENDIF()
	ENDIF()

	IF(APPLE)
		SET(EXE_TYPE MACOSX_BUNDLE)
		list(APPEND Q3_SRCS code/unix/fnql.icns)
		set_source_files_properties(code/unix/fnql.icns PROPERTIES
			MACOSX_PACKAGE_LOCATION Resources)
		SET(MACOSX_BUNDLE_ICON_FILE fnql.icns)
	ENDIF()
ELSEIF(WIN32)
	SET(EXE_TYPE WIN32)
	# Prevent Windows headers from exporting legacy min/max macros into shared
	# client headers that use the C++ standard-library overloads.
	ADD_COMPILE_DEFINITIONS(NOMINMAX)
	IF(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		ADD_COMPILE_DEFINITIONS(_CRT_SECURE_NO_WARNINGS) # v3.12+
	ELSEIF(MSVC)
		SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /MANIFEST:NO")
		ADD_COMPILE_DEFINITIONS(_CRT_SECURE_NO_WARNINGS) # v3.12+
	ENDIF()

	SET(Q3_SRCS
		code/win32/win_main.cpp
		code/win32/win_shared.cpp
		code/win32/win_syscon.cpp
		code/win32/win_resource.rc)

	SET(Q3_UI_SRCS
		code/win32/win_gamma.cpp
		code/win32/win_glimp.cpp
		code/win32/win_input.cpp
		code/win32/win_minimize.cpp
		code/win32/win_qgl.cpp
		code/win32/win_snd.cpp
		code/win32/win_wndproc.cpp)
	IF(USE_VULKAN)
		list(APPEND Q3_UI_SRCS code/win32/win_qvk.cpp)
	ENDIF()
ENDIF(UNIX)

ADD_LIBRARY(q3ui OBJECT ${Q3_UI_SRCS})

IF(UNIX AND USE_SDL)
	find_package(SDL3 3.2.0 CONFIG QUIET COMPONENTS SDL3)
	IF(TARGET SDL3::SDL3)
		SET(Q3_SDL_TARGET SDL3::SDL3)
	ELSE()
		find_package(PkgConfig REQUIRED)
		pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3>=3.2.0)
		SET(Q3_SDL_TARGET PkgConfig::SDL3)
	ENDIF()

	TARGET_COMPILE_DEFINITIONS(q3ui PRIVATE SDL_FUNCTION_POINTER_IS_VOID_POINTER=1)
	TARGET_LINK_LIBRARIES(q3ui PRIVATE ${Q3_SDL_TARGET})
ELSEIF(UNIX AND NOT MSVC)
	IF(NOT USE_SDL)
		find_package (X11 REQUIRED)
		TARGET_LINK_LIBRARIES(q3ui ${X11_LIBRARIES})
	ENDIF()
ENDIF()

IF(USE_VULKAN)
	TARGET_COMPILE_DEFINITIONS(q3ui PRIVATE USE_VULKAN_API)
	TARGET_COMPILE_DEFINITIONS(client PRIVATE USE_VULKAN_API)
ENDIF()

IF(USE_RENDERER_DLOPEN OR RENDERER_DEFAULT STREQUAL "opengl" OR RENDERER_DEFAULT STREQUAL "glx")
	TARGET_COMPILE_DEFINITIONS(q3ui PRIVATE USE_OPENGL_API)
	TARGET_COMPILE_DEFINITIONS(client PRIVATE USE_OPENGL_API)
ENDIF()

# client executable

ADD_EXECUTABLE(${CNAME}${BINEXT} ${EXE_TYPE} ${Q3_SRCS})
TARGET_LINK_LIBRARIES(${CNAME}${BINEXT} qcommon botlib client q3ui Threads::Threads ${FNQL_CLIENT_LINK_LIBS})
set_property(TARGET ${CNAME}${BINEXT} PROPERTY LINKER_LANGUAGE CXX)
#TARGET_LINK_OPTIONS(${CNAME} PRIVATE -fno-pie) # v3.13+

IF(NOT USE_RENDERER_DLOPEN)
	IF(RENDERER_DEFAULT STREQUAL "glx")
		TARGET_LINK_LIBRARIES(${CNAME}${BINEXT} ${RENDERER_PREFIX}_glx)
	ELSEIF(USE_VULKAN)
		TARGET_LINK_LIBRARIES(${CNAME}${BINEXT} ${RENDERER_PREFIX}_vulkan)
	ELSE()
		TARGET_LINK_LIBRARIES(${CNAME}${BINEXT} ${RENDERER_PREFIX}_opengl)
	ENDIF()
ENDIF()

# dedicated server executable

ADD_EXECUTABLE(${DNAME}${BINEXT} ${EXE_TYPE} ${Q3_SRCS})
TARGET_LINK_LIBRARIES(${DNAME}${BINEXT} qcommon_ded botlib Threads::Threads)
set_property(TARGET ${DNAME}${BINEXT} PROPERTY LINKER_LANGUAGE CXX)
#TARGET_LINK_OPTIONS(${DNAME} PRIVATE -fno-pie) # v3.13+

IF(WIN32)
	TARGET_LINK_LIBRARIES(${CNAME}${BINEXT} winmm comctl32 ws2_32)
	TARGET_LINK_LIBRARIES(${DNAME}${BINEXT} winmm comctl32 ws2_32)

	if(CMAKE_GENERATOR_PLATFORM STREQUAL "Win32")
		SET(FNQL_OPENAL_DLL "${CMAKE_CURRENT_SOURCE_DIR}/code/openal/windows/x86/OpenAL32.dll")
	elseif(CMAKE_GENERATOR_PLATFORM STREQUAL "x64")
		SET(FNQL_OPENAL_DLL "${CMAKE_CURRENT_SOURCE_DIR}/code/openal/windows/x64/OpenAL32.dll")
	elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64|AARCH64|aarch64")
		unset(FNQL_OPENAL_DLL)
	elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
		SET(FNQL_OPENAL_DLL "${CMAKE_CURRENT_SOURCE_DIR}/code/openal/windows/x86/OpenAL32.dll")
	else()
		SET(FNQL_OPENAL_DLL "${CMAKE_CURRENT_SOURCE_DIR}/code/openal/windows/x64/OpenAL32.dll")
	endif()

	if(DEFINED FNQL_OPENAL_DLL AND EXISTS "${FNQL_OPENAL_DLL}")
		add_custom_command(
			TARGET ${CNAME}${BINEXT} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${FNQL_OPENAL_DLL}"
				"$<TARGET_FILE_DIR:${CNAME}${BINEXT}>"
		)
		if(FNQL_AUDIO_LOOPBACK_TESTS AND TARGET fnql_audio_loopback_tests)
			add_custom_command(
				TARGET fnql_audio_loopback_tests POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different
					"${FNQL_OPENAL_DLL}"
					"$<TARGET_FILE_DIR:fnql_audio_loopback_tests>"
			)
		endif()
	endif()
ELSE()
	TARGET_LINK_LIBRARIES(${CNAME}${BINEXT} m ${CMAKE_DL_LIBS})
	TARGET_LINK_LIBRARIES(${DNAME}${BINEXT} m ${CMAKE_DL_LIBS})
ENDIF()
