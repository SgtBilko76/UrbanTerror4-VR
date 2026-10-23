#include "vr_base.h"
#include "vr_renderer.h"

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../client/client.h"

#include "vr_clientinfo.h"
#include "vr_input.h"
#include "vr_types.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define ENABLE_GL_DEBUG 0
#define ENABLE_GL_DEBUG_VERBOSE 0
#if ENABLE_GL_DEBUG
#include <GLES3/gl32.h>
#endif

#define DEFAULT_SUPER_SAMPLING  1.1f

extern vr_clientinfo_t vr;
extern cvar_t *vr_heightAdjust;

XrView* projections;
GLboolean stageSupported = GL_FALSE;
qboolean fullscreenMode = qfalse;
qboolean needRecenter = qtrue;

void VR_UpdateStageBounds(ovrApp* pappState) {
    XrExtent2Df stageBounds = {};

    XrResult result;
    OXR(result = xrGetReferenceSpaceBoundsRect(
            pappState->Session, XR_REFERENCE_SPACE_TYPE_STAGE, &stageBounds));
    if (result != XR_SUCCESS) {
        ALOGV("Stage bounds query failed: using small defaults");
        stageBounds.width = 1.0f;
        stageBounds.height = 1.0f;

        pappState->CurrentSpace = pappState->FakeStageSpace;
    }

    ALOGV("Stage bounds: width = %f, depth %f", stageBounds.width, stageBounds.height);
}


void APIENTRY VR_GLDebugLog(GLenum source, GLenum type, GLuint id,
	GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
{
	if (type == GL_DEBUG_TYPE_ERROR || type == GL_DEBUG_TYPE_PERFORMANCE || ENABLE_GL_DEBUG_VERBOSE)
	{
		char typeStr[128];
		switch (type) {
			case GL_DEBUG_TYPE_ERROR: sprintf(typeStr, "ERROR"); break;
			case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: sprintf(typeStr, "DEPRECATED_BEHAVIOR"); break;
			case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: sprintf(typeStr, "UNDEFINED_BEHAVIOR"); break;
			case GL_DEBUG_TYPE_PORTABILITY: sprintf(typeStr, "PORTABILITY"); break;
			case GL_DEBUG_TYPE_PERFORMANCE: sprintf(typeStr, "PERFORMANCE"); break;
			case GL_DEBUG_TYPE_MARKER: sprintf(typeStr, "MARKER"); break;
			case GL_DEBUG_TYPE_PUSH_GROUP: sprintf(typeStr, "PUSH_GROUP"); break;
			case GL_DEBUG_TYPE_POP_GROUP: sprintf(typeStr, "POP_GROUP"); break;
			default: sprintf(typeStr, "OTHER"); break;
		}

		char severinityStr[128];
		switch (severity) {
			case GL_DEBUG_SEVERITY_HIGH: sprintf(severinityStr, "HIGH"); break;
			case GL_DEBUG_SEVERITY_MEDIUM: sprintf(severinityStr, "MEDIUM"); break;
			case GL_DEBUG_SEVERITY_LOW: sprintf(severinityStr, "LOW"); break;
			default: sprintf(severinityStr, "VERBOSE"); break;
		}

		Com_Printf("[%s] GL issue - %s: %s\n", severinityStr, typeStr, message);
	}
}

void VR_GetResolution(engine_t* engine, int *pWidth, int *pHeight)
{
	static int width = 0;
	static int height = 0;
	float superSampling = 0.0f;

	float configuredSuperSampling = Cvar_VariableValue("vr_superSampling");
	if (vr.superSampling == 0.0f || configuredSuperSampling != vr.superSampling) {
		vr.superSampling = configuredSuperSampling;
		if (vr.superSampling != 0.0f) {
			Cbuf_AddText( "vid_restart\n" );
		}
	}

	if (vr.superSampling == 0.0f) {
		superSampling = DEFAULT_SUPER_SAMPLING;
	} else {
		superSampling = vr.superSampling;
	}

	if (engine)
	{
        // Enumerate the viewport configurations.
        uint32_t viewportConfigTypeCount = 0;
        OXR(xrEnumerateViewConfigurations(
                engine->appState.Instance, engine->appState.SystemId, 0, &viewportConfigTypeCount, NULL));

        XrViewConfigurationType* viewportConfigurationTypes =
                (XrViewConfigurationType*)malloc(viewportConfigTypeCount * sizeof(XrViewConfigurationType));

        OXR(xrEnumerateViewConfigurations(
                engine->appState.Instance,
                engine->appState.SystemId,
                viewportConfigTypeCount,
                &viewportConfigTypeCount,
                viewportConfigurationTypes));

        ALOGV("Available Viewport Configuration Types: %d", viewportConfigTypeCount);

        for (uint32_t i = 0; i < viewportConfigTypeCount; i++) {
            const XrViewConfigurationType viewportConfigType = viewportConfigurationTypes[i];

            ALOGV(
                    "Viewport configuration type %d : %s",
                    viewportConfigType,
                    viewportConfigType == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO ? "Selected" : "");

            XrViewConfigurationProperties viewportConfig;
            viewportConfig.type = XR_TYPE_VIEW_CONFIGURATION_PROPERTIES;
            OXR(xrGetViewConfigurationProperties(
                    engine->appState.Instance, engine->appState.SystemId, viewportConfigType, &viewportConfig));
            ALOGV(
                    "FovMutable=%s ConfigurationType %d",
                    viewportConfig.fovMutable ? "true" : "false",
                    viewportConfig.viewConfigurationType);

            uint32_t viewCount;
            OXR(xrEnumerateViewConfigurationViews(
                    engine->appState.Instance, engine->appState.SystemId, viewportConfigType, 0, &viewCount, NULL));

            if (viewCount > 0) {
                XrViewConfigurationView* elements =
                        (XrViewConfigurationView*)malloc(viewCount * sizeof(XrViewConfigurationView));

                for (uint32_t e = 0; e < viewCount; e++) {
                    elements[e].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
                    elements[e].next = NULL;
                }

                OXR(xrEnumerateViewConfigurationViews(
                        engine->appState.Instance,
                        engine->appState.SystemId,
                        viewportConfigType,
                        viewCount,
                        &viewCount,
                        elements));

                // Cache the view config properties for the selected config type.
                if (viewportConfigType == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
                    assert(viewCount == ovrMaxNumEyes);
                    for (uint32_t e = 0; e < viewCount; e++) {
                        engine->appState.ViewConfigurationView[e] = elements[e];
                    }
                }

                free(elements);
            } else {
                ALOGE("Empty viewport configuration type: %d", viewCount);
            }
        }

        free(viewportConfigurationTypes);

        *pWidth = width = engine->appState.ViewConfigurationView[0].recommendedImageRectWidth * superSampling;
        *pHeight = height = engine->appState.ViewConfigurationView[0].recommendedImageRectHeight * superSampling;
	}
	else
	{
		//use cached values
		*pWidth = width;
		*pHeight = height;
	}
}

void VR_Recenter(engine_t* engine) {

    // Calculate recenter reference
    XrReferenceSpaceCreateInfo spaceCreateInfo = {};
    spaceCreateInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    spaceCreateInfo.poseInReferenceSpace.orientation.w = 1.0f;
    if (engine->appState.CurrentSpace != XR_NULL_HANDLE) {
        vec3_t rotation = {0, 0, 0};
        XrSpaceLocation loc = {};
        loc.type = XR_TYPE_SPACE_LOCATION;
        OXR(xrLocateSpace(engine->appState.HeadSpace, engine->appState.CurrentSpace, engine->predictedDisplayTime, &loc));
        QuatToYawPitchRoll(loc.pose.orientation, rotation, vr.hmdorientation);

        vr.recenterYaw += radians(vr.hmdorientation[YAW]);
        spaceCreateInfo.poseInReferenceSpace.orientation.x = 0;
        spaceCreateInfo.poseInReferenceSpace.orientation.y = sin(vr.recenterYaw / 2);
        spaceCreateInfo.poseInReferenceSpace.orientation.z = 0;
        spaceCreateInfo.poseInReferenceSpace.orientation.w = cos(vr.recenterYaw / 2);
    }

    // Delete previous space instances
    if (engine->appState.StageSpace != XR_NULL_HANDLE) {
        OXR(xrDestroySpace(engine->appState.StageSpace));
    }
    if (engine->appState.FakeStageSpace != XR_NULL_HANDLE) {
        OXR(xrDestroySpace(engine->appState.FakeStageSpace));
    }

    // Create a default stage space to use if SPACE_TYPE_STAGE is not
    // supported, or calls to xrGetReferenceSpaceBoundsRect fail.
    spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceCreateInfo.poseInReferenceSpace.position.y = -1.6750f;
    OXR(xrCreateReferenceSpace(engine->appState.Session, &spaceCreateInfo, &engine->appState.FakeStageSpace));
    ALOGV("Created fake stage space from local space with offset");
    engine->appState.CurrentSpace = engine->appState.FakeStageSpace;

    if (stageSupported) {
        spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
        spaceCreateInfo.poseInReferenceSpace.position.y = 0.0f;
        OXR(xrCreateReferenceSpace(engine->appState.Session, &spaceCreateInfo, &engine->appState.StageSpace));
        ALOGV("Created stage space");
        engine->appState.CurrentSpace = engine->appState.StageSpace;
    }

    // Update menu orientation
    vr.menuYaw = 0;
}

void VR_InitRenderer( engine_t* engine ) {
#if ENABLE_GL_DEBUG
	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(VR_GLDebugLog, 0);
#endif

	int eyeW, eyeH;
    VR_GetResolution(engine, &eyeW, &eyeH);

    // Get the viewport configuration info for the chosen viewport configuration type.
    engine->appState.ViewportConfig.type = XR_TYPE_VIEW_CONFIGURATION_PROPERTIES;

    OXR(xrGetViewConfigurationProperties(
            engine->appState.Instance, engine->appState.SystemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, &engine->appState.ViewportConfig));

    // Get the supported display refresh rates for the system.
    {
        PFN_xrEnumerateDisplayRefreshRatesFB pfnxrEnumerateDisplayRefreshRatesFB = NULL;
        OXR(xrGetInstanceProcAddr(
                engine->appState.Instance,
                "xrEnumerateDisplayRefreshRatesFB",
                (PFN_xrVoidFunction*)(&pfnxrEnumerateDisplayRefreshRatesFB)));

        OXR(pfnxrEnumerateDisplayRefreshRatesFB(
                engine->appState.Session, 0, &engine->appState.NumSupportedDisplayRefreshRates, NULL));

        engine->appState.SupportedDisplayRefreshRates =
                (float*)malloc(engine->appState.NumSupportedDisplayRefreshRates * sizeof(float));
        OXR(pfnxrEnumerateDisplayRefreshRatesFB(
                engine->appState.Session,
                engine->appState.NumSupportedDisplayRefreshRates,
                &engine->appState.NumSupportedDisplayRefreshRates,
                engine->appState.SupportedDisplayRefreshRates));
        ALOGV("Supported Refresh Rates:");
        for (uint32_t i = 0; i < engine->appState.NumSupportedDisplayRefreshRates; i++) {
            ALOGV("%d:%f", i, engine->appState.SupportedDisplayRefreshRates[i]);
        }

        OXR(xrGetInstanceProcAddr(
                engine->appState.Instance,
                "xrGetDisplayRefreshRateFB",
                (PFN_xrVoidFunction*)(&engine->appState.pfnGetDisplayRefreshRate)));

        float currentDisplayRefreshRate = 0.0f;
        OXR(engine->appState.pfnGetDisplayRefreshRate(engine->appState.Session, &currentDisplayRefreshRate));
        ALOGV("Current System Display Refresh Rate: %f", currentDisplayRefreshRate);

        OXR(xrGetInstanceProcAddr(
                engine->appState.Instance,
                "xrRequestDisplayRefreshRateFB",
                (PFN_xrVoidFunction*)(&engine->appState.pfnRequestDisplayRefreshRate)));

        // Test requesting the system default.
        OXR(engine->appState.pfnRequestDisplayRefreshRate(engine->appState.Session, 0.0f));
        ALOGV("Requesting system default display refresh rate");
    }

    uint32_t numOutputSpaces = 0;
    OXR(xrEnumerateReferenceSpaces(engine->appState.Session, 0, &numOutputSpaces, NULL));

    XrReferenceSpaceType* referenceSpaces =
            (XrReferenceSpaceType*)malloc(numOutputSpaces * sizeof(XrReferenceSpaceType));

    OXR(xrEnumerateReferenceSpaces(
            engine->appState.Session, numOutputSpaces, &numOutputSpaces, referenceSpaces));

    for (uint32_t i = 0; i < numOutputSpaces; i++) {
        if (referenceSpaces[i] == XR_REFERENCE_SPACE_TYPE_STAGE) {
            stageSupported = qtrue;
            break;
        }
    }

    free(referenceSpaces);

    if (engine->appState.CurrentSpace == XR_NULL_HANDLE) {
        VR_Recenter(engine);
    }

    projections = (XrView*)(malloc(ovrMaxNumEyes * sizeof(XrView)));

    ovrRenderer_Create(
            engine->appState.Session,
            &engine->appState.Renderer,
            eyeW,
            eyeH);
}

void VR_DestroyRenderer( engine_t* engine )
{
    ovrRenderer_Destroy(&engine->appState.Renderer);
    free(projections);
}

void VR_ReInitRenderer()
{
    VR_DestroyRenderer( VR_GetEngine() );
    VR_InitRenderer( VR_GetEngine() );
}

void VR_ClearFrameBuffer( int width, int height)
{
    glEnable( GL_SCISSOR_TEST );
    glViewport( 0, 0, width, height );

    if (Cvar_VariableIntegerValue("vr_thirdPersonSpectator"))
    {
        //Blood red.. ish
        glClearColor( 0.12f, 0.0f, 0.05f, 1.0f );
    }
    else
    {
        //Black
        glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
    }

    glScissor( 0, 0, width, height );
    glClear( GL_COLOR_BUFFER_BIT );

    glScissor( 0, 0, 0, 0 );
    glDisable( GL_SCISSOR_TEST );
}

// Zoom by scaling the frustum tangents rather than the angles. Scaling the angles changes
// (tanRight + tanLeft) / (tanRight - tanLeft), which moves the eye's optical axis within the
// image -- so the zoomed view drifts off axis, and every 2D overlay drawn while zoomed (the
// weapon scope above all) is corrected against a principal point the compositor is not using.
// Scaling the tangents leaves that term untouched and magnifies about the axis, as a scope should.
static float VR_ZoomAngle( float angle, float zoomLevel ) {
    if (zoomLevel <= 1.0f) {
        return angle;
    }
    return atanf(tanf(angle) / zoomLevel);
}

void VR_DrawFrame( engine_t* engine ) {
	if (vr.weapon_zoomed) {
		vr.weapon_zoomLevel += 0.05;
		if (vr.weapon_zoomLevel > 2.5f)
			vr.weapon_zoomLevel = 2.5f;
	}
	else {
		//Zoom back out quicker
		vr.weapon_zoomLevel -= 0.25f;
		if (vr.weapon_zoomLevel < 1.0f)
			vr.weapon_zoomLevel = 1.0f;
	}

    GLboolean stageBoundsDirty = GL_TRUE;
    if (ovrApp_HandleXrEvents(&engine->appState)) {
        // Recentering tears down and rebuilds the reference spaces. Doing that before the
        // first xrWaitFrame hangs: predictedDisplayTime is still 0, so xrLocateSpace fails
        // with XR_ERROR_TIME_INVALID and the runtime spins on the space teardown. Defer it
        // to the end of a frame that has a valid predicted display time.
        if (engine->predictedDisplayTime == 0) {
            needRecenter = qtrue;
        } else {
            VR_Recenter(engine);
        }
    }
    if (engine->appState.SessionActive == GL_FALSE) {
        return;
    }

    if (stageBoundsDirty) {
        VR_UpdateStageBounds(&engine->appState);
        stageBoundsDirty = GL_FALSE;
    }

    // NOTE: OpenXR does not use the concept of frame indices. Instead,
    // XrWaitFrame returns the predicted display time.
    XrFrameWaitInfo waitFrameInfo = {};
    waitFrameInfo.type = XR_TYPE_FRAME_WAIT_INFO;
    waitFrameInfo.next = NULL;

    XrFrameState frameState = {};
    frameState.type = XR_TYPE_FRAME_STATE;
    frameState.next = NULL;

    OXR(xrWaitFrame(engine->appState.Session, &waitFrameInfo, &frameState));
    engine->predictedDisplayTime = frameState.predictedDisplayTime;

    // Get the HMD pose, predicted for the middle of the time period during which
    // the new eye images will be displayed. The number of frames predicted ahead
    // depends on the pipeline depth of the engine and the synthesis rate.
    // The better the prediction, the less black will be pulled in at the edges.
    XrFrameBeginInfo beginFrameDesc = {};
    beginFrameDesc.type = XR_TYPE_FRAME_BEGIN_INFO;
    beginFrameDesc.next = NULL;
    OXR(xrBeginFrame(engine->appState.Session, &beginFrameDesc));

    // Every xrWaitFrame must still be paired with begin/end even when the runtime asks
    // for no content. Returning here instead stalls the session at READY forever: the
    // runtime waits for a frame, and shouldRender never becomes true without one.
    if (!frameState.shouldRender) {
        XrFrameEndInfo endFrameInfo = {};
        endFrameInfo.type = XR_TYPE_FRAME_END_INFO;
        endFrameInfo.displayTime = frameState.predictedDisplayTime;
        endFrameInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        endFrameInfo.layerCount = 0;
        endFrameInfo.layers = NULL;
        OXR(xrEndFrame(engine->appState.Session, &endFrameInfo));
        return;
    }

    XrViewLocateInfo projectionInfo = {};
    projectionInfo.type = XR_TYPE_VIEW_LOCATE_INFO;
    projectionInfo.viewConfigurationType = engine->appState.ViewportConfig.viewConfigurationType;
    projectionInfo.displayTime = frameState.predictedDisplayTime;
    projectionInfo.space = engine->appState.CurrentSpace;

    XrViewState viewState = {XR_TYPE_VIEW_STATE, NULL};

    uint32_t projectionCapacityInput = ovrMaxNumEyes;
    uint32_t projectionCountOutput = projectionCapacityInput;

    OXR(xrLocateViews(
            engine->appState.Session,
            &projectionInfo,
            &viewState,
            projectionCapacityInput,
            &projectionCountOutput,
            projections));
    //

    // The screen layer is a single flat image taken from array index 0 and shown to both eyes,
    // so it has to be rendered through a centred frustum. Averaging the two eyes gives exactly
    // that: the headset's asymmetry is mirrored, so the average is symmetric.
    qboolean screenLayer = VR_useScreenLayer() ? qtrue : qfalse;

    // Each eye has its own asymmetric frustum: more field of view outward than nasally. Keep
    // them separate for rendering, and take their union for anything that has to cover both.
    XrFovf eyeFov[ovrMaxNumEyes];
    XrFovf fov = projections[0].fov;
    XrPosef invViewTransform[2];
    for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
        invViewTransform[eye] = projections[eye].pose;
        eyeFov[eye] = projections[eye].fov;

        if (projections[eye].fov.angleLeft < fov.angleLeft)
            fov.angleLeft = projections[eye].fov.angleLeft;
        if (projections[eye].fov.angleRight > fov.angleRight)
            fov.angleRight = projections[eye].fov.angleRight;
        if (projections[eye].fov.angleUp > fov.angleUp)
            fov.angleUp = projections[eye].fov.angleUp;
        if (projections[eye].fov.angleDown < fov.angleDown)
            fov.angleDown = projections[eye].fov.angleDown;
    }

    if (screenLayer) {
        XrFovf centred = {};
        for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
            centred.angleLeft  += projections[eye].fov.angleLeft  / (float)ovrMaxNumEyes;
            centred.angleRight += projections[eye].fov.angleRight / (float)ovrMaxNumEyes;
            centred.angleUp    += projections[eye].fov.angleUp    / (float)ovrMaxNumEyes;
            centred.angleDown  += projections[eye].fov.angleDown  / (float)ovrMaxNumEyes;
        }
        eyeFov[0] = eyeFov[1] = centred;
        fov = centred;
    }
    vr.fov_x = (fabs(fov.angleLeft) + fabs(fov.angleRight)) * 180.0f / M_PI;
    vr.fov_y = (fabs(fov.angleUp) + fabs(fov.angleDown)) * 180.0f / M_PI;

    // Update HMD and controllers
    IN_VRUpdateHMD( invViewTransform[0] );
    IN_VRUpdateControllers( invViewTransform[0], frameState.predictedDisplayTime );
    IN_VRSyncActions();

    //Projection used for drawing HUD models etc
    float hudScale = M_PI * 15.0f / 180.0f;
    const ovrMatrix4f monoVRMatrix = ovrMatrix4f_CreateProjectionFov(
            -hudScale, hudScale, hudScale, -hudScale, 1.0f, 0.0f );
    ovrMatrix4f eyeProjectionMatrix[ovrMaxNumEyes];
    for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
        eyeProjectionMatrix[eye] = ovrMatrix4f_CreateProjectionFov(
                VR_ZoomAngle(eyeFov[eye].angleLeft, vr.weapon_zoomLevel),
                VR_ZoomAngle(eyeFov[eye].angleRight, vr.weapon_zoomLevel),
                VR_ZoomAngle(eyeFov[eye].angleUp, vr.weapon_zoomLevel),
                VR_ZoomAngle(eyeFov[eye].angleDown, vr.weapon_zoomLevel),
                1.0f, 0.0f );
    }

    // Union of both eyes, used for CPU-side culling so nothing visible to one eye is culled.
    const ovrMatrix4f cullProjectionMatrix = ovrMatrix4f_CreateProjectionFov(
            VR_ZoomAngle(fov.angleLeft, vr.weapon_zoomLevel),
            VR_ZoomAngle(fov.angleRight, vr.weapon_zoomLevel),
            VR_ZoomAngle(fov.angleUp, vr.weapon_zoomLevel),
            VR_ZoomAngle(fov.angleDown, vr.weapon_zoomLevel),
            1.0f, 0.0f );

    engine->appState.LayerCount = 0;
    memset(engine->appState.Layers, 0, sizeof(ovrCompositorLayer_Union) * ovrMaxLayerCount);

    ovrFramebuffer* frameBuffer = &engine->appState.Renderer.FrameBuffer;
    int swapchainIndex = engine->appState.Renderer.FrameBuffer.TextureSwapChainIndex;
    int glFramebuffer = engine->appState.Renderer.FrameBuffer.FrameBuffers[swapchainIndex];
    re.SetVRHeadsetParms(eyeProjectionMatrix[0].M, eyeProjectionMatrix[1].M,
                         cullProjectionMatrix.M, monoVRMatrix.M, glFramebuffer);

    ovrFramebuffer_Acquire(frameBuffer);
    ovrFramebuffer_SetCurrent(frameBuffer);
    VR_ClearFrameBuffer(frameBuffer->ColorSwapChain.Width, frameBuffer->ColorSwapChain.Height);
    Com_Frame();

    // Clear the alpha channel, other way OpenXR would not transfer the framebuffer fully
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    ovrFramebuffer_Resolve(frameBuffer);
    ovrFramebuffer_Release(frameBuffer);
    ovrFramebuffer_SetNone();

    XrCompositionLayerProjectionView projection_layer_elements[2] = {};
    // Reuse the value the projections were built from, so the layer choice cannot disagree with
    // them if the key catcher changes during Com_Frame.
    if (!screenLayer && !(VR_PS_FOLLOWING(cl.snap.ps) && vr.follow_mode == VRFM_FIRSTPERSON)) {
        vr.menuYaw = vr.hmdorientation[YAW];

        if (fullscreenMode) {
            VR_ReInitRenderer();
            fullscreenMode = qfalse;
        }

        for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
            ovrFramebuffer* frameBuffer = &engine->appState.Renderer.FrameBuffer;

            memset(&projection_layer_elements[eye], 0, sizeof(XrCompositionLayerProjectionView));
            projection_layer_elements[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            projection_layer_elements[eye].pose = invViewTransform[eye];
            projection_layer_elements[eye].fov = projections[eye].fov;

            memset(&projection_layer_elements[eye].subImage, 0, sizeof(XrSwapchainSubImage));
            projection_layer_elements[eye].subImage.swapchain = frameBuffer->ColorSwapChain.Handle;
            projection_layer_elements[eye].subImage.imageRect.offset.x = 0;
            projection_layer_elements[eye].subImage.imageRect.offset.y = 0;
            projection_layer_elements[eye].subImage.imageRect.extent.width = frameBuffer->ColorSwapChain.Width;
            projection_layer_elements[eye].subImage.imageRect.extent.height = frameBuffer->ColorSwapChain.Height;
            projection_layer_elements[eye].subImage.imageArrayIndex = eye;
        }

        XrCompositionLayerProjection projection_layer = {};
        projection_layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
        projection_layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        projection_layer.layerFlags |= XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT;
        projection_layer.space = engine->appState.CurrentSpace;
        projection_layer.viewCount = ovrMaxNumEyes;
        projection_layer.views = projection_layer_elements;

        engine->appState.Layers[engine->appState.LayerCount++].Projection = projection_layer;
    } else {

        fullscreenMode = qtrue;

        // Build the quad layer. This replaced a cylinder of radius 8 whose axis sat 6 metres
        // ahead, so its image was 14 metres out and 8 * (pi/2) metres of arc wide. The quad
        // keeps that distance and aspect; menuScale is the only knob for its size.
        int width = engine->appState.Renderer.FrameBuffer.ColorSwapChain.Width;
        int height = engine->appState.Renderer.FrameBuffer.ColorSwapChain.Height;

        const float menuDistance = 14.0f;
        const float menuScale = 1.15f;
        const float menuWidth = 8.0f * (MATH_PI * 0.5f) * menuScale;
        const float menuAspect = width / (float)height / 0.75f;

        XrCompositionLayerQuad quad_layer = {};
        quad_layer.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
        quad_layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        quad_layer.space = engine->appState.CurrentSpace;
        quad_layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        memset(&quad_layer.subImage, 0, sizeof(XrSwapchainSubImage));
        quad_layer.subImage.swapchain = engine->appState.Renderer.FrameBuffer.ColorSwapChain.Handle;
        quad_layer.subImage.imageRect.offset.x = 0;
        quad_layer.subImage.imageRect.offset.y = 0;
        quad_layer.subImage.imageRect.extent.width = width;
        quad_layer.subImage.imageRect.extent.height = height;
        quad_layer.subImage.imageArrayIndex = 0;
        const XrVector3f axis = {0.0f, 1.0f, 0.0f};
        XrVector3f pos = {
                invViewTransform[0].position.x - sin(radians(vr.menuYaw)) * menuDistance,
                invViewTransform[0].position.y,
                invViewTransform[0].position.z - cos(radians(vr.menuYaw)) * menuDistance
        };
        quad_layer.pose.orientation = XrQuaternionf_CreateFromVectorAngle(axis, radians(vr.menuYaw));
        quad_layer.pose.position = pos;
        quad_layer.size.width = menuWidth;
        quad_layer.size.height = menuWidth / menuAspect;

        engine->appState.Layers[engine->appState.LayerCount++].Quad = quad_layer;
    }

    // Compose the layers for this frame.
    const XrCompositionLayerBaseHeader* layers[ovrMaxLayerCount] = {};
    for (int i = 0; i < engine->appState.LayerCount; i++) {
        layers[i] = (const XrCompositionLayerBaseHeader*)&engine->appState.Layers[i];
    }

    XrFrameEndInfo endFrameInfo = {};
    endFrameInfo.type = XR_TYPE_FRAME_END_INFO;
    endFrameInfo.displayTime = frameState.predictedDisplayTime;
    endFrameInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endFrameInfo.layerCount = engine->appState.LayerCount;
    endFrameInfo.layers = layers;

    OXR(xrEndFrame(engine->appState.Session, &endFrameInfo));
    frameBuffer->TextureSwapChainIndex++;
    frameBuffer->TextureSwapChainIndex %= frameBuffer->TextureSwapChainLength;

    if (needRecenter)
    {
        VR_Recenter(engine);
        needRecenter = qfalse;
    }
}
