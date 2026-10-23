#include "vr_base.h"
#include "vr_renderer.h"

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../client/client.h"

#include "vr_clientinfo.h"
#include "vr_input.h"
#include "vr_types.h"

extern int vr_extRefreshRate;

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

    // Get the supported display refresh rates for the system (XR_FB_display_refresh_rate:
    // Meta only; PICO runs at its own fixed rate).
    if (vr_extRefreshRate) {
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

/*
================================================================================

Menu panel and laser pointer

The menu is a flat quad or a curved (cylinder) compositor layer centred on the head. The
pointer is a ray from the menu hand's aim pose; where it hits the panel becomes the UI cursor,
and a beam is drawn on its own projection layer on top of the panel.

================================================================================
*/
extern qboolean vr_cylinderSupported;
extern cvar_t *vr_righthanded;

#define MENU_FLAT_DISTANCE 14.0f
#define MENU_FLAT_WIDTH (8.0f * (MATH_PI * 0.5f) * 1.15f)

typedef struct {
    qboolean curved;
    float radius;       // curved: cylinder radius; flat: distance
    float width;        // flat: width; curved: arc length
    float height;
    float centralAngle; // curved only
} menuPanel_t;

static void VR_GetMenuPanel(menuPanel_t* p, int width, int height) {
    const float aspect = width / (float)height / 0.75f;
    const float flatHeight = MENU_FLAT_WIDTH / aspect;
    p->curved = vr_cylinderSupported && Cvar_VariableIntegerValue("vr_menuCurved");
    if (p->curved) {
        // same angular size as the flat panel
        float r = Cvar_VariableValue("vr_menuRadius");
        if (r < 1.0f) r = 1.0f;
        p->radius = r;
        p->centralAngle = 2.0f * atanf(MENU_FLAT_WIDTH * 0.5f / MENU_FLAT_DISTANCE);
        p->width = r * p->centralAngle;
        p->height = 2.0f * r * (flatHeight * 0.5f / MENU_FLAT_DISTANCE);
    } else {
        p->radius = MENU_FLAT_DISTANCE;
        p->width = MENU_FLAT_WIDTH;
        p->height = flatHeight;
        p->centralAngle = 0.0f;
    }
}

static void VR_QuatRotate(const XrQuaternionf* q, const float v[3], float out[3]) {
    // out = q * v * q^-1
    float tx = 2.0f * (q->y * v[2] - q->z * v[1]);
    float ty = 2.0f * (q->z * v[0] - q->x * v[2]);
    float tz = 2.0f * (q->x * v[1] - q->y * v[0]);
    out[0] = v[0] + q->w * tx + (q->y * tz - q->z * ty);
    out[1] = v[1] + q->w * ty + (q->z * tx - q->x * tz);
    out[2] = v[2] + q->w * tz + (q->x * ty - q->y * tx);
}

static qboolean menuLaserVisible;
static float menuLaserStart[3], menuLaserEnd[3];
static qboolean menuLaserHit;
static int menuPointerLastX = -1, menuPointerLastY = -1;

static void VR_UpdateMenuPointer(engine_t* engine, const XrPosef* head, qboolean screenLayer) {
    menuLaserVisible = qfalse;
    menuLaserHit = qfalse;
    if (!screenLayer || !(Key_GetCatcher() & KEYCATCH_UI) || (Key_GetCatcher() & KEYCATCH_CONSOLE)) {
        menuPointerLastX = menuPointerLastY = -1;
        return;
    }

    // menu hand: the dominant hand unless the menu was switched to the other one
    qboolean right = (vr_righthanded->integer != 0);
    if (vr.menuLeftHanded) right = !right;
    ovrTrackedController* c = &engine->appState.TrackedController[right ? 1 : 0];
    if (!c->Active) return;

    const float fwd[3] = {0.0f, 0.0f, -1.0f};
    float dir[3], o[3], d[3];
    VR_QuatRotate(&c->Pose.orientation, fwd, dir);

    // into the panel frame: centred on the head, rotated by the menu yaw about +Y
    const float th = radians(vr.menuYaw), cs = cosf(th), sn = sinf(th);
    const float px = c->Pose.position.x - head->position.x;
    const float py = c->Pose.position.y - head->position.y;
    const float pz = c->Pose.position.z - head->position.z;
    o[0] = px * cs - pz * sn; o[1] = py; o[2] = px * sn + pz * cs;
    d[0] = dir[0] * cs - dir[2] * sn; d[1] = dir[1]; d[2] = dir[0] * sn + dir[2] * cs;

    menuPanel_t panel;
    VR_GetMenuPanel(&panel, engine->appState.Renderer.FrameBuffer.ColorSwapChain.Width,
                    engine->appState.Renderer.FrameBuffer.ColorSwapChain.Height);

    float t = -1.0f, u = 0.0f, v = 0.0f;
    if (panel.curved) {
        float a = d[0] * d[0] + d[2] * d[2];
        float b = 2.0f * (o[0] * d[0] + o[2] * d[2]);
        float cc = o[0] * o[0] + o[2] * o[2] - panel.radius * panel.radius;
        float disc = b * b - 4.0f * a * cc;
        if (a > 1e-6f && disc >= 0.0f) {
            t = (-b + sqrtf(disc)) / (2.0f * a);
            float hx = o[0] + t * d[0], hz = o[2] + t * d[2];
            u = atan2f(hx, -hz) / panel.centralAngle + 0.5f;
        }
    } else if (d[2] < -1e-4f) {
        t = (-panel.radius - o[2]) / d[2];
        u = (o[0] + t * d[0]) / panel.width + 0.5f;
    }
    if (t > 0.0f) {
        v = 0.5f - (o[1] + t * d[1]) / panel.height;
    }

    for (int k = 0; k < 3; k++) {
        menuLaserStart[k] = (&c->Pose.position.x)[k];
    }
    if (t > 0.0f && u >= -0.02f && u <= 1.02f && v >= -0.02f && v <= 1.02f) {
        menuLaserHit = qtrue;
        for (int k = 0; k < 3; k++) menuLaserEnd[k] = menuLaserStart[k] + dir[k] * t;

        int x = (int)(Com_Clamp(0.0f, 1.0f, u) * SCREEN_WIDTH + 0.5f);
        int y = (int)(Com_Clamp(0.0f, 1.0f, v) * SCREEN_HEIGHT + 0.5f);
        {   // for the virtual keyboard (cl_scrn.c), in 640x480 units
            extern int vr_menuPointerX, vr_menuPointerY, vr_menuPointerTime;
            vr_menuPointerX = x; vr_menuPointerY = y; vr_menuPointerTime = cls.realtime;
        }
        if (vr.menuCursorX && vr.menuCursorY) {
            // a VR-aware UI hands us its cursor
            *vr.menuCursorX = x;
            *vr.menuCursorY = y;
            Com_QueueEvent(Sys_Milliseconds(), SE_MOUSE, 0, 0, 0, NULL);
        } else {
            // QVM menus (Urban Terror's ui.qvm) only take relative mouse moves, and the engine
            // merges queued mouse moves, so the cursor is steered by difference: from where the
            // UI last drew it (centre of its cursor picture) to the beam's point.
            extern int cl_uiCursorShader, cl_uiCursorDrawnTime;
            extern float cl_uiCursorDrawn[4];
            static int lastSend;
            qboolean seen = cl_uiCursorShader && cls.realtime - cl_uiCursorDrawnTime < 250;
            int obsX = (int)(cl_uiCursorDrawn[0] + cl_uiCursorDrawn[2] * 0.5f + 0.5f);
            int obsY = (int)(cl_uiCursorDrawn[1] + cl_uiCursorDrawn[3] * 0.5f + 0.5f);
            if (menuPointerLastX < 0) {
                if (seen) {
                    menuPointerLastX = obsX; menuPointerLastY = obsY;
                } else {
                    Com_QueueEvent(Sys_Milliseconds(), SE_MOUSE, -4 * SCREEN_WIDTH, -4 * SCREEN_HEIGHT, 0, NULL);
                    menuPointerLastX = 0; menuPointerLastY = 0;
                    lastSend = cls.realtime;
                }
            } else if (seen && cls.realtime - lastSend > 100 &&
                       (abs(obsX - menuPointerLastX) > 1 || abs(obsY - menuPointerLastY) > 1)) {
                // the UI's cursor is not where we think (e.g. it moved it itself): resync
                menuPointerLastX = obsX; menuPointerLastY = obsY;
            }
            if (x != menuPointerLastX || y != menuPointerLastY) {
                Com_QueueEvent(Sys_Milliseconds(), SE_MOUSE, x - menuPointerLastX, y - menuPointerLastY, 0, NULL);
                menuPointerLastX = x;
                menuPointerLastY = y;
                lastSend = cls.realtime;
            }
        }
    } else {
        for (int k = 0; k < 3; k++) menuLaserEnd[k] = menuLaserStart[k] + dir[k] * 3.0f;
    }
    menuLaserVisible = Cvar_VariableIntegerValue("vr_menuLaser") != 0;
}

// --- laser beam layer: own colour-only stereo swapchain, drawn with a tiny shader ---
#define LASER_SCALE 0.6f
static XrSwapchain laserSwapchain = XR_NULL_HANDLE;
static XrSwapchainImageOpenGLESKHR* laserImages;
static uint32_t laserImageCount;
static GLuint* laserFbos;   // [image * 2 + eye]
static int laserW, laserH;
static GLuint laserProgram, laserVao, laserVbo;

static GLuint VR_LaserCompile(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    GLint ok = 0;
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        ALOGE("Laser shader: %s", log);
    }
    return sh;
}

static qboolean VR_LaserInit(engine_t* engine) {
    if (laserSwapchain != XR_NULL_HANDLE) return qtrue;
    laserW = (int)(engine->appState.Renderer.FrameBuffer.ColorSwapChain.Width * LASER_SCALE);
    laserH = (int)(engine->appState.Renderer.FrameBuffer.ColorSwapChain.Height * LASER_SCALE);

    XrSwapchainCreateInfo ci = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    ci.format = GL_RGBA8;
    ci.sampleCount = 1;
    ci.width = laserW;
    ci.height = laserH;
    ci.faceCount = 1;
    ci.arraySize = 2;
    ci.mipCount = 1;
    if (xrCreateSwapchain(engine->appState.Session, &ci, &laserSwapchain) != XR_SUCCESS) {
        laserSwapchain = XR_NULL_HANDLE;
        return qfalse;
    }
    xrEnumerateSwapchainImages(laserSwapchain, 0, &laserImageCount, NULL);
    laserImages = calloc(laserImageCount, sizeof(XrSwapchainImageOpenGLESKHR));
    for (uint32_t i = 0; i < laserImageCount; i++) laserImages[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
    xrEnumerateSwapchainImages(laserSwapchain, laserImageCount, &laserImageCount, (XrSwapchainImageBaseHeader*)laserImages);
    laserFbos = calloc(laserImageCount * 2, sizeof(GLuint));
    glGenFramebuffers(laserImageCount * 2, laserFbos);
    for (uint32_t i = 0; i < laserImageCount; i++) {
        for (int eye = 0; eye < 2; eye++) {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, laserFbos[i * 2 + eye]);
            glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, laserImages[i].image, 0, eye);
        }
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    const char* vs = "#version 300 es\nlayout(location=0) in vec4 aPos;\nlayout(location=1) in vec4 aCol;\nout vec4 vCol;\nvoid main(){ gl_Position = aPos; vCol = aCol; }\n";
    const char* fs = "#version 300 es\nprecision mediump float;\nin vec4 vCol;\nout vec4 o;\nvoid main(){ o = vCol; }\n";
    laserProgram = glCreateProgram();
    glAttachShader(laserProgram, VR_LaserCompile(GL_VERTEX_SHADER, vs));
    glAttachShader(laserProgram, VR_LaserCompile(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(laserProgram);
    glGenVertexArrays(1, &laserVao);
    glGenBuffers(1, &laserVbo);
    ALOGV("Laser layer %dx%d, %u images", laserW, laserH, laserImageCount);
    return qtrue;
}

// view-space point -> clip space for an OpenXR fov (z forward is -z)
static qboolean VR_LaserProject(const XrFovf* fov, const float p[3], float out[4]) {
    float w = -p[2];
    if (w < 0.02f) return qfalse;
    float l = tanf(fov->angleLeft), r = tanf(fov->angleRight);
    float d = tanf(fov->angleDown), u = tanf(fov->angleUp);
    out[0] = (2.0f * p[0] - (r + l) * w) / (r - l);
    out[1] = (2.0f * p[1] - (u + d) * w) / (u - d);
    out[2] = 0.0f;
    out[3] = w;
    return qtrue;
}

static void VR_LaserToView(const XrPosef* eye, const float p[3], float out[3]) {
    XrQuaternionf inv = {-eye->orientation.x, -eye->orientation.y, -eye->orientation.z, eye->orientation.w};
    float d[3] = {p[0] - eye->position.x, p[1] - eye->position.y, p[2] - eye->position.z};
    VR_QuatRotate(&inv, d, out);
}

// one quad = 2 triangles = 6 vertices of (pos4, col4)
static int VR_LaserQuad(float* vb, const float a[4], const float b[4], const float c[4], const float d[4], const float col[4]) {
    const float* v[6] = {a, b, c, a, c, d};
    for (int i = 0; i < 6; i++) {
        memcpy(vb + i * 8, v[i], 4 * sizeof(float));
        memcpy(vb + i * 8 + 4, col, 4 * sizeof(float));
    }
    return 6;
}

static qboolean VR_RenderLaserLayer(engine_t* engine, const XrView* views, XrCompositionLayerProjectionView* outViews) {
    if (!menuLaserVisible || !VR_LaserInit(engine)) return qfalse;

    uint32_t index = 0;
    XrSwapchainImageAcquireInfo acq = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (xrAcquireSwapchainImage(laserSwapchain, &acq, &index) != XR_SUCCESS) return qfalse;
    XrSwapchainImageWaitInfo wait = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout = 1000000000;
    if (xrWaitSwapchainImage(laserSwapchain, &wait) != XR_SUCCESS) return qfalse;

    // save the GL state the engine's renderer caches
    GLint prevFbo, prevProgram, prevVao, prevBuffer, prevViewport[4];
    GLboolean prevBlend = glIsEnabled(GL_BLEND), prevDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevCull = glIsEnabled(GL_CULL_FACE), prevScissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prevStencil = glIsEnabled(GL_STENCIL_TEST), prevMask[4];
    GLint prevSrcRGB, prevDstRGB, prevSrcA, prevDstA;
    GLfloat prevClear[4];
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevBuffer);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevSrcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevDstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevSrcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevDstA);
    glGetBooleanv(GL_COLOR_WRITEMASK, prevMask);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, prevClear);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);   // premultiplied
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(laserProgram);
    glBindVertexArray(laserVao);
    glBindBuffer(GL_ARRAY_BUFFER, laserVbo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(4 * sizeof(float)));

    for (int eye = 0; eye < 2; eye++) {
        float vb[8 * 6 * 3];
        int n = 0;
        float a[3], b[3];
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, laserFbos[index * 2 + eye]);
        glViewport(0, 0, laserW, laserH);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);

        VR_LaserToView(&views[eye].pose, menuLaserStart, a);
        VR_LaserToView(&views[eye].pose, menuLaserEnd, b);
        if (a[2] > -0.03f) {   // start behind the eye plane: begin a bit in front of it
            float t = (-0.03f - a[2]) / (b[2] - a[2]);
            if (b[2] < -0.03f && t > 0.0f && t < 1.0f) {
                for (int k = 0; k < 3; k++) a[k] += (b[k] - a[k]) * t;
            }
        }
        // beam: a ribbon facing the eye, 1 mm wide at the hand, 5 mm at the panel
        float line[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        float mid[3] = {(a[0] + b[0]) * 0.5f, (a[1] + b[1]) * 0.5f, (a[2] + b[2]) * 0.5f};
        float side[3] = {line[1] * mid[2] - line[2] * mid[1], line[2] * mid[0] - line[0] * mid[2], line[0] * mid[1] - line[1] * mid[0]};
        float len = sqrtf(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
        if (len > 1e-6f) {
            float wa = 0.0005f / len, wb = 0.0025f / len;
            float p0[3], p1[3], p2[3], p3[3], c0[4], c1[4], c2[4], c3[4];
            for (int k = 0; k < 3; k++) {
                p0[k] = a[k] - side[k] * wa; p1[k] = a[k] + side[k] * wa;
                p2[k] = b[k] + side[k] * wb; p3[k] = b[k] - side[k] * wb;
            }
            const float col[4] = {0.9f, 0.1f, 0.1f, 0.9f};   // premultiplied red
            if (VR_LaserProject(&views[eye].fov, p0, c0) && VR_LaserProject(&views[eye].fov, p1, c1) &&
                VR_LaserProject(&views[eye].fov, p2, c2) && VR_LaserProject(&views[eye].fov, p3, c3)) {
                n += VR_LaserQuad(vb + n * 8, c0, c1, c2, c3, col);
            }
        }
        // dot where the beam meets the panel
        if (menuLaserHit) {
            const float s = 0.012f * (-b[2]) / 5.0f + 0.003f;
            float q0[3] = {b[0] - s, b[1] - s, b[2]}, q1[3] = {b[0] + s, b[1] - s, b[2]};
            float q2[3] = {b[0] + s, b[1] + s, b[2]}, q3[3] = {b[0] - s, b[1] + s, b[2]};
            float c0[4], c1[4], c2[4], c3[4];
            const float col[4] = {1.0f, 0.3f, 0.3f, 1.0f};
            if (VR_LaserProject(&views[eye].fov, q0, c0) && VR_LaserProject(&views[eye].fov, q1, c1) &&
                VR_LaserProject(&views[eye].fov, q2, c2) && VR_LaserProject(&views[eye].fov, q3, c3)) {
                n += VR_LaserQuad(vb + n * 8, c0, c1, c2, c3, col);
            }
        }
        if (n) {
            glBufferData(GL_ARRAY_BUFFER, n * 8 * sizeof(float), vb, GL_STREAM_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, n);
        }

        memset(&outViews[eye], 0, sizeof(outViews[eye]));
        outViews[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
        outViews[eye].pose = views[eye].pose;
        outViews[eye].fov = views[eye].fov;
        outViews[eye].subImage.swapchain = laserSwapchain;
        outViews[eye].subImage.imageRect.extent.width = laserW;
        outViews[eye].subImage.imageRect.extent.height = laserH;
        outViews[eye].subImage.imageArrayIndex = eye;
    }

    // restore
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glBindVertexArray(prevVao);
    glBindBuffer(GL_ARRAY_BUFFER, prevBuffer);
    glUseProgram(prevProgram);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevFbo);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glBlendFuncSeparate(prevSrcRGB, prevDstRGB, prevSrcA, prevDstA);
    if (!prevBlend) glDisable(GL_BLEND);
    if (prevDepth) glEnable(GL_DEPTH_TEST);
    if (prevCull) glEnable(GL_CULL_FACE);
    if (prevScissor) glEnable(GL_SCISSOR_TEST);
    if (prevStencil) glEnable(GL_STENCIL_TEST);
    glColorMask(prevMask[0], prevMask[1], prevMask[2], prevMask[3]);
    glClearColor(prevClear[0], prevClear[1], prevClear[2], prevClear[3]);

    XrSwapchainImageReleaseInfo rel = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(laserSwapchain, &rel);
    return qtrue;
}

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

    // Stereo separation from the runtime's eye poses, as QuakeQuest's VR_GetIPD does,
    // instead of a fixed 64 mm (r_stereoSeparation is in millimetres)
    {
        const XrVector3f *l = &projections[0].pose.position;
        const XrVector3f *r = &projections[1].pose.position;
        float dx = r->x - l->x, dy = r->y - l->y, dz = r->z - l->z;
        float ipd = sqrtf(dx * dx + dy * dy + dz * dz);
        if (ipd > 0.02f && ipd < 0.1f && fabsf(ipd * 1000.0f - Cvar_VariableValue("r_stereoSeparation")) > 0.5f) {
            Cvar_SetValue("r_stereoSeparation", ipd * 1000.0f);
        }
    }

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
        // Averaging makes left/right symmetric (they are mirrored between the eyes), but the
        // vertical asymmetry is the same in both eyes and survives. The 2D overlay is moved to the
        // optical axis (GLSL_PrepareUniformBuffers), so on Quest 3 the whole menu slid ~9% up and
        // UrT's top menu bar fell off the panel. Make it symmetric too: the panel is a flat image.
        {
            float halfV = (fabsf(centred.angleUp) + fabsf(centred.angleDown)) * 0.5f;
            centred.angleUp = halfV;
            centred.angleDown = -halfV;
        }
        eyeFov[0] = eyeFov[1] = centred;
        fov = centred;
    }
    vr.fov_x = (fabs(fov.angleLeft) + fabs(fov.angleRight)) * 180.0f / M_PI;
    vr.fov_y = (fabs(fov.angleUp) + fabs(fov.angleDown)) * 180.0f / M_PI;

    // Update HMD and controllers
    IN_VRUpdateHMD( invViewTransform[0] );
    IN_VRUpdateControllers( invViewTransform[0], frameState.predictedDisplayTime );
    VR_UpdateMenuPointer( engine, &invViewTransform[0], screenLayer );
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
    static XrCompositionLayerProjectionView laser_layer_elements[2];
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

        // The menu panel: a curved cylinder centred on the head (XR_KHR_composition_layer_cylinder)
        // or, without the extension / with vr_menuCurved 0, the flat quad 14 m ahead.
        int width = engine->appState.Renderer.FrameBuffer.ColorSwapChain.Width;
        int height = engine->appState.Renderer.FrameBuffer.ColorSwapChain.Height;
        menuPanel_t panel;
        VR_GetMenuPanel(&panel, width, height);
        const XrVector3f axis = {0.0f, 1.0f, 0.0f};

        if (panel.curved) {
            XrCompositionLayerCylinderKHR cylinder_layer = {};
            cylinder_layer.type = XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR;
            cylinder_layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            cylinder_layer.space = engine->appState.CurrentSpace;
            cylinder_layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            cylinder_layer.subImage.swapchain = engine->appState.Renderer.FrameBuffer.ColorSwapChain.Handle;
            cylinder_layer.subImage.imageRect.extent.width = width;
            cylinder_layer.subImage.imageRect.extent.height = height;
            cylinder_layer.subImage.imageArrayIndex = 0;
            cylinder_layer.pose.orientation = XrQuaternionf_CreateFromVectorAngle(axis, radians(vr.menuYaw));
            cylinder_layer.pose.position = invViewTransform[0].position;
            cylinder_layer.radius = panel.radius;
            cylinder_layer.centralAngle = panel.centralAngle;
            cylinder_layer.aspectRatio = panel.width / panel.height;
            engine->appState.Layers[engine->appState.LayerCount++].Cylinder = cylinder_layer;
        } else {
            XrCompositionLayerQuad quad_layer = {};
            quad_layer.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
            quad_layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            quad_layer.space = engine->appState.CurrentSpace;
            quad_layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            memset(&quad_layer.subImage, 0, sizeof(XrSwapchainSubImage));
            quad_layer.subImage.swapchain = engine->appState.Renderer.FrameBuffer.ColorSwapChain.Handle;
            quad_layer.subImage.imageRect.extent.width = width;
            quad_layer.subImage.imageRect.extent.height = height;
            quad_layer.subImage.imageArrayIndex = 0;
            XrVector3f pos = {
                    invViewTransform[0].position.x - sin(radians(vr.menuYaw)) * panel.radius,
                    invViewTransform[0].position.y,
                    invViewTransform[0].position.z - cos(radians(vr.menuYaw)) * panel.radius
            };
            quad_layer.pose.orientation = XrQuaternionf_CreateFromVectorAngle(axis, radians(vr.menuYaw));
            quad_layer.pose.position = pos;
            quad_layer.size.width = panel.width;
            quad_layer.size.height = panel.height;
            engine->appState.Layers[engine->appState.LayerCount++].Quad = quad_layer;
        }

        // laser pointer on top of the panel
        if (VR_RenderLaserLayer(engine, projections, laser_layer_elements)) {
            XrCompositionLayerProjection laser_layer = {};
            laser_layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
            laser_layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            laser_layer.space = engine->appState.CurrentSpace;
            laser_layer.viewCount = ovrMaxNumEyes;
            laser_layer.views = laser_layer_elements;
            engine->appState.Layers[engine->appState.LayerCount++].Projection = laser_layer;
        }
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
