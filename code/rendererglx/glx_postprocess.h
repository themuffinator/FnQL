#ifndef GLX_POSTPROCESS_H
#define GLX_POSTPROCESS_H

#include "glx_local.h"
#include "glx_render_ir.h"
#include "glx_post_shader_plan.h"
#include "../renderercommon/tr_glx_public.h"

namespace glx {

struct PostProcessState {
	cvar_t *r_glxPostProcessDebug;
	cvar_t *r_glxColorPipelineDebug;
	cvar_t *r_hdr;
	cvar_t *r_hdrPrecision;
	cvar_t *r_hdrBloomFormat;
	cvar_t *r_srgbTextures;
	cvar_t *r_framebufferSRGB;
	cvar_t *r_tonemap;
	cvar_t *r_tonemapExposure;
	cvar_t *r_glxAutoExposure;
	cvar_t *r_glxAutoExposurePercentile;
	cvar_t *r_glxAutoExposureTargetLuma;
	cvar_t *r_glxAutoExposureMin;
	cvar_t *r_glxAutoExposureMax;
	cvar_t *r_glxAutoExposureAdapt;
	cvar_t *r_colorGrade;
	cvar_t *r_colorGradeLift;
	cvar_t *r_colorGradeGamma;
	cvar_t *r_colorGradeGain;
	cvar_t *r_colorGradeWhitePoint;
	cvar_t *r_colorGradeAdaptWhitePoint;
	cvar_t *r_colorGradeLUT;
	cvar_t *r_colorGradeLUTScale;
	cvar_t *r_hdrDisplayPaperWhite;
	cvar_t *r_hdrDisplayMaxLuminance;
	cvar_t *r_outputBackend;
	cvar_t *r_outputAllowExperimentalLinuxHDR;
	cvar_t *r_screenshotCaptureMode;
	cvar_t *r_bloom_threshold;
	cvar_t *r_bloom_threshold_mode;
	cvar_t *r_bloom_soft_knee;
	cvar_t *r_crt;
	cvar_t *r_crtAmount;
	cvar_t *r_crtScanlineStrength;
	cvar_t *r_crtMaskStrength;
	cvar_t *r_crtCurvature;
	cvar_t *r_crtChromatic;
	qboolean glReady;
	RenderProductTier tier;
	qboolean fboRequested;
	qboolean fboReady;
	qboolean programReady;
	qboolean framebufferFnsReady;
	qboolean multiSampled;
	qboolean superSampled;
	qboolean windowAdjusted;
	int vidWidth;
	int vidHeight;
	int captureWidth;
	int captureHeight;
	int windowWidth;
	int windowHeight;
	int internalFormat;
	int textureFormat;
	int textureType;
	int blitFilter;
	int hdrMode;
	int hdrPrecisionRequestedMode;
	int hdrPrecisionMode;
	int toneMapMode;
	int renderScaleMode;
	int bloomMode;
	int bloomThresholdMode;
	int lastFboReadIndex;
	int lastScreenshotMask;
	int lastResult;
	qboolean textureSrgbAvailable;
	qboolean textureSrgbDecode;
	qboolean textureSrgbDecodeDesired;
	qboolean textureSrgbDecodeConsistent;
	unsigned int textureSrgbMissingDecode;
	unsigned int textureSrgbStaleDecode;
	qboolean framebufferSrgbAvailable;
	qboolean framebufferSrgbEnabled;
	qboolean sceneTargetFloat;
	qboolean finalShaderSrgbEncode;
	qboolean outputContractValid;
	CaptureExportPolicy lastCaptureRequest;
	CaptureExportPolicy lastCaptureSelected;
	qboolean lastCaptureHdrAware;
	qboolean lastCaptureSupported;
	rendererDisplayOutput_t displayOutput;
	rendererDisplayOutput_t previousDisplayOutput;
	OutputTransform lastOutput;
	qboolean lastBloomAvailable;
	qboolean lastBloomFinalStage;
	float lastGreyscale;
	float lastExposure;
	qboolean lastAutoExposureEnabled;
	qboolean lastAutoExposureFallback;
	qboolean lastAutoExposureSamplesValid;
	ExposureReductionAlgorithm lastExposureAlgorithm;
	int lastAutoExposureMode;
	int lastAutoExposureSampleWidth;
	int lastAutoExposureSampleHeight;
	int lastAutoExposureSampleCount;
	int lastAutoExposureHistogramBin;
	float lastManualExposure;
	float lastAutoExposureScale;
	float lastAutoExposureTargetExposure;
	float lastAutoExposureLogLuma;
	float lastAutoExposureLuma;
	float lastAutoExposurePercentile;
	float lastAutoExposureTargetLuma;
	float lastBloomSoftKnee;
	float lastLegacyGamma;
	float lastLegacyOverbright;
	float lastCrtAmount;
	float lastCrtScanlineStrength;
	float lastCrtMaskStrength;
	float lastCrtCurvature;
	float lastCrtChromatic;
	float lastPaperWhiteNits;
	float lastMaxOutputNits;
	float lastGradeLift[3];
	float lastGradeGamma[3];
	float lastGradeGain[3];
	float lastWhitePointSourceKelvin;
	float lastWhitePointTargetKelvin;
	float lastColorGradeLutSize;
	float lastColorGradeLutScale;
	rendererOutputRequest_t lastRequestedBackend;
	rendererOutputBackend_t lastSelectedBackend;
	rendererOutputBackend_t lastNativeBackend;
	qboolean lastOutputHardwareActive;
	qboolean lastOutputExperimental;
	qboolean lastDisplayHdrEnabled;
	qboolean lastDisplayHdrHeadroomValid;
	qboolean lastDisplayIccProfileAvailable;
	int lastDisplayIccProfileBytes;
	float lastDisplayHdrHeadroom;
	float lastDisplaySdrWhiteNits;
	float lastDisplayMaxNits;
	int lastBloomMode;
	int lastBloomRequestedPasses;
	int lastBloomEffectivePasses;
	int lastBloomBlendBase;
	int lastBloomFilterSize;
	int lastBloomTextureUnits;
	int lastBloomFormatMode;
	int lastBloomInternalFormat;
	int lastBloomTextureFormat;
	int lastBloomTextureType;
	int lastBloomThresholdMode;
	int lastBloomModulate;
	float lastBloomThreshold;
	float lastBloomIntensity;
	float lastBloomReflection;
	int lastBloomCreateResult;
	int lastBloomResult;
	qboolean colorGradeLutKnown;
	qboolean colorGradeLutActive;
	int colorGradeLutSize;
	int colorGradeLutModificationCount;
	float colorGradeLutScale;
	unsigned int lastDisplayOutputQueryFrame;
	unsigned int displayOutputQueries;
	unsigned int displayOutputStateChanges;
	unsigned int displayOutputCapabilityChanges;
	unsigned int displayOutputBackendChanges;
	unsigned int displayOutputHdrChanges;
	unsigned int displayOutputHeadroomChanges;
	unsigned int displayOutputLuminanceChanges;
	unsigned int displayOutputIccChanges;
	unsigned int lastDisplayOutputChangeFrame;
	unsigned int lastDisplayOutputChangeMask;
	unsigned int lastDisplayOutputHash;
	unsigned int previousDisplayOutputHash;
	int lastOutputBackendModificationCount;
	int lastOutputAllowExperimentalModificationCount;
	int lastDisplayPaperWhiteModificationCount;
	int lastDisplayMaxLuminanceModificationCount;
	char reason[128];
	unsigned int fboInitAttempts;
	unsigned int fboInitSuccesses;
	unsigned int fboInitFailures;
	unsigned int fboDisabledInits;
	unsigned int fboShutdowns;
	unsigned int frames;
	unsigned int minimizedFrames;
	unsigned int renderScaleFrames;
	unsigned int hdrFrames;
	unsigned int sceneLinearFrames;
	unsigned int toneMappedFrames;
	unsigned int gradedFrames;
	unsigned int greyscaleFrames;
	unsigned int windowAdjustedFrames;
	unsigned int screenshotFrames;
	unsigned int captureSdrFrames;
	unsigned int captureHdrRequestFrames;
	unsigned int captureUnsupportedRequestFrames;
	unsigned int bloomAvailableFrames;
	unsigned int bloomFinalFrames;
	unsigned int gammaDirectFrames;
	unsigned int gammaBlitFrames;
	unsigned int minimizedOutputFrames;
	unsigned int copyScreenCalls;
	unsigned int msaaBlits;
	unsigned int msaaDepthBlits;
	unsigned int ssaaBlits;
	unsigned int bloomCreateAttempts;
	unsigned int bloomCreateSuccesses;
	unsigned int bloomCreateTextureUnitFailures;
	unsigned int bloomCreateFboFailures;
	unsigned int bloomCalls;
	unsigned int bloomSkips;
	unsigned int bloomRendered;
	unsigned int bloomIntermediatePasses;
	unsigned int bloomFinalPasses;
	unsigned int bloomFailures;
	unsigned int bloomMode1Passes;
	unsigned int bloomMode2Passes;
	unsigned int bloomReflectionPasses;
	unsigned int autoExposureFrames;
	unsigned int autoExposureHistogramFrames;
	unsigned int autoExposureSimpleFrames;
	unsigned int autoExposureFallbackFrames;
	unsigned int autoExposureSampleFailures;
	unsigned int colorPipelineDumpFrames;
	qboolean colorPipelineCsvHeaderPrinted;
	unsigned int postOutputPlanFrames;
	unsigned int postOutputOwnedFrames;
	unsigned int postOutputFallbackFrames;
	unsigned int postOutputPlanNodes;
	unsigned int postOutputPlanOutputs;
	unsigned int postOutputExecutableNodes;
	unsigned int postOutputExecutableOutputs;
	unsigned int postOutputImplementationFallbackFrames;
	unsigned int postOutputExecutorRejects;
	unsigned int postOutputResultMismatches;
	unsigned int postShaderPlanFrames;
	unsigned int postShaderPlanInvalidFrames;
	unsigned int lastPostOutputNodeCount;
	unsigned int lastPostOutputOutputCount;
	unsigned int lastPostOutputExecutableNodeCount;
	unsigned int lastPostOutputExecutableOutputCount;
	unsigned int lastPostOutputPlanHash;
	unsigned int lastPostOutputFallbackReasons;
	unsigned int lastPostShaderFeatureMask;
	unsigned int lastPostShaderPlanHash;
	unsigned int lastPostShaderTextureCount;
	unsigned int lastPostShaderUniformVec4Count;
	int lastPostOutputPredictedResult;
	int lastPostOutputActualResult;
	qboolean lastPostOutputExecutorImplemented;
	qboolean lastPostOutputGlxOwned;
	qboolean lastPostShaderPlanValid;
	unsigned int imageColorSpaceCounts[GLX_IMAGE_COLORSPACE_COUNT];
	unsigned int imageSrgbDecodeCounts[GLX_IMAGE_COLORSPACE_COUNT];
	unsigned int imageUnexpectedSrgbDecode;
	qboolean autoExposureInitialized;
	float autoExposureSmoothedExposure;
};

void GLX_PostProcess_RegisterCvars( PostProcessState *state );
void GLX_PostProcess_OnOpenGLReady( PostProcessState *state, const Capabilities &caps );
void GLX_PostProcess_Shutdown( PostProcessState *state );
void GLX_PostProcess_RecordFboInit( PostProcessState *state, qboolean requested, qboolean ready,
	qboolean programReady, qboolean framebufferFnsReady, int vidWidth, int vidHeight,
	int captureWidth, int captureHeight, int windowWidth, int windowHeight,
	int internalFormat, int textureFormat, int textureType, qboolean multiSampled,
	qboolean superSampled, qboolean windowAdjusted, int blitFilter, int hdrMode,
	int renderScaleMode, int bloomMode, qboolean textureSrgbAvailable,
	qboolean framebufferSrgbAvailable, qboolean framebufferSrgbEnabled );
void GLX_PostProcess_RecordFboShutdown( PostProcessState *state );
void GLX_PostProcess_RecordFrame( PostProcessState *state, qboolean minimized, qboolean bloomAvailable,
	qboolean programReady, int screenshotMask, qboolean windowAdjusted, int fboReadIndex,
	int hdrMode, int renderScaleMode, float greyscale, float legacyGamma, float legacyOverbright );
qboolean GLX_PostProcess_AutoExposureNeedsSamples( const PostProcessState *state,
	int *width, int *height );
float GLX_PostProcess_UpdateAutoExposure( PostProcessState *state, float manualExposure,
	const float *rgba, int width, int height );
void GLX_PostProcess_RecordPostOutputPlan( PostProcessState *state, const PostOutputPlan &plan,
	qboolean executorConsumed );
void GLX_PostProcess_RecordPostOutputExecutionFallback( PostProcessState *state,
	unsigned int fallbackReason );
void GLX_PostProcess_RecordPostShaderPlan( PostProcessState *state, const PostShaderPlan &plan );
void GLX_PostProcess_RecordFrameResult( PostProcessState *state, int result );
void GLX_PostProcess_RecordColorGradeLut( PostProcessState *state, qboolean active,
	int size, float scale );
void GLX_PostProcess_RecordBloomCreate( PostProcessState *state, int result,
	int requestedPasses, int effectivePasses, int textureUnits, int formatMode,
	int internalFormat, int textureFormat, int textureType );
void GLX_PostProcess_RecordBloom( PostProcessState *state, int result, qboolean finalStage,
	int bloomMode, int requestedPasses, int effectivePasses, int blendBase, int filterSize,
	int textureUnits, int thresholdMode, int modulate, float threshold, float intensity,
	float reflection );
void GLX_PostProcess_RecordCopyScreen( PostProcessState *state, int viewportWidth, int viewportHeight );
void GLX_PostProcess_RecordBlit( PostProcessState *state, int kind, qboolean depthOnly,
	int srcWidth, int srcHeight, int dstWidth, int dstHeight );
void GLX_PostProcess_ResetImageColorAudit( PostProcessState *state );
void GLX_PostProcess_RecordImageColorAudit( PostProcessState *state, int colorSpace,
	qboolean srgbDecode );
void GLX_PostProcess_PrintInfo( const PostProcessState &state );
const char *GLX_PostProcess_ResultName( int result );
const char *GLX_PostProcess_BloomCreateResultName( int result );
const char *GLX_PostProcess_BloomResultName( int result );
const char *GLX_PostProcess_BloomFormatModeName( int mode );
const char *GLX_PostProcess_PostOutputModeName( qboolean glxOwned );

} // namespace glx

#endif // GLX_POSTPROCESS_H
