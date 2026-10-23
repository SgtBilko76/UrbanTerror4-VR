#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../client/client.h"
#include "vr_base.h"
#include "vr_clientinfo.h"

//#if __ANDROID__

#include <assert.h>
#include <unistd.h>

static engine_t vr_engine;
qboolean vr_initialized = qfalse;
qboolean vr_cylinderSupported = qfalse;
extern vr_clientinfo_t vr;

// Only these two are needed on every runtime. The rest are enabled when the runtime lists
// them: PICO's runtime has no XR_FB_display_refresh_rate, and requiring it made
// xrCreateInstance fail there.
const char* const requiredExtensionNames[] = {
        XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
        XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME};
int vr_extPerfSettings = 0;
int vr_extThreadSettings = 0;
int vr_extRefreshRate = 0;
int vr_extBDController = 0;
const uint32_t numRequiredExtensions =
        sizeof(requiredExtensionNames) / sizeof(requiredExtensionNames[0]);

cvar_t *vr_worldscale = NULL;
cvar_t *vr_worldscaleScaler = NULL;
cvar_t *vr_hudDepth = NULL;
cvar_t *vr_righthanded = NULL;
cvar_t *vr_switchThumbsticks = NULL;
cvar_t *vr_snapturn = NULL;
cvar_t *vr_heightAdjust = NULL;
cvar_t *vr_extralatencymode = NULL;
cvar_t *vr_directionMode = NULL;
cvar_t *vr_weaponPitch = NULL;
cvar_t *vr_twoHandedWeapons = NULL;
cvar_t *vr_showItemInHand = NULL;
cvar_t *vr_refreshrate = NULL;
cvar_t *vr_superSampling = NULL;
cvar_t *vr_weaponScope = NULL;
cvar_t *vr_rollWhenHit = NULL;
cvar_t *vr_hudYOffset = NULL;
cvar_t *vr_2dYOffset = NULL;
cvar_t *vr_menuTitleYOffset = NULL;
cvar_t *vr_thumbstickPressThreshold = NULL;
cvar_t *vr_thumbstickReleaseThreshold = NULL;
cvar_t *vr_sendRollToServer = NULL;
cvar_t *vr_lasersight = NULL;
cvar_t *vr_hapticIntensity = NULL;
cvar_t *vr_comfortVignette = NULL;
cvar_t *vr_weaponSelectorMode = NULL;
cvar_t *vr_weaponSelectorWithHud = NULL;
cvar_t *vr_goreLevel = NULL;
cvar_t *vr_hudDrawStatus = NULL;
cvar_t *vr_showConsoleMessages = NULL;

engine_t* VR_Init( ovrJava java )
{
    if (vr_initialized)
        return &vr_engine;

    ovrApp_Clear(&vr_engine.appState);

    PFN_xrInitializeLoaderKHR xrInitializeLoaderKHR;
    xrGetInstanceProcAddr(
            XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*)&xrInitializeLoaderKHR);
    if (xrInitializeLoaderKHR != NULL) {
        XrLoaderInitInfoAndroidKHR loaderInitializeInfoAndroid;
        memset(&loaderInitializeInfoAndroid, 0, sizeof(loaderInitializeInfoAndroid));
        loaderInitializeInfoAndroid.type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR;
        loaderInitializeInfoAndroid.next = NULL;
        loaderInitializeInfoAndroid.applicationVM = java.Vm;
        loaderInitializeInfoAndroid.applicationContext = java.ActivityObject;
        xrInitializeLoaderKHR((XrLoaderInitInfoBaseHeaderKHR*)&loaderInitializeInfoAndroid);
    }

    // Create the OpenXR instance.
    XrApplicationInfo appInfo;
    memset(&appInfo, 0, sizeof(appInfo));
    strcpy(appInfo.applicationName, "Quake 3 Arena");
    appInfo.applicationVersion = 0;
    strcpy(appInfo.engineName, "Quake 3 Arena");
    appInfo.engineVersion = 0;
    // Pinned to 1.0: the Khronos headers declare a 1.1 XR_CURRENT_API_VERSION, and asking a
    // runtime for 1.1 is a behaviour change this port has never needed.
    appInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);

    XrInstanceCreateInfoAndroidKHR instanceCreateInfoAndroid;
    memset(&instanceCreateInfoAndroid, 0, sizeof(instanceCreateInfoAndroid));
    instanceCreateInfoAndroid.type = XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR;
    instanceCreateInfoAndroid.next = NULL;
    instanceCreateInfoAndroid.applicationVM = java.Vm;
    instanceCreateInfoAndroid.applicationActivity = java.ActivityObject;

    XrInstanceCreateInfo instanceCreateInfo;
    memset(&instanceCreateInfo, 0, sizeof(instanceCreateInfo));
    instanceCreateInfo.type = XR_TYPE_INSTANCE_CREATE_INFO;
    instanceCreateInfo.next = &instanceCreateInfoAndroid;
    instanceCreateInfo.createFlags = 0;
    instanceCreateInfo.applicationInfo = appInfo;
    instanceCreateInfo.enabledApiLayerCount = 0;
    instanceCreateInfo.enabledApiLayerNames = NULL;
    // The curved menu panel needs XR_KHR_composition_layer_cylinder; only ask for it if the
    // runtime has it, otherwise xrCreateInstance would fail.
    const char* extensionNames[16];
    uint32_t extensionCount = 0;
    for (uint32_t i = 0; i < numRequiredExtensions; i++) {
        extensionNames[extensionCount++] = requiredExtensionNames[i];
    }
    {
        uint32_t propCount = 0;
        vr_cylinderSupported = qfalse;
        if (xrEnumerateInstanceExtensionProperties(NULL, 0, &propCount, NULL) == XR_SUCCESS && propCount > 0) {
            XrExtensionProperties* props = calloc(propCount, sizeof(XrExtensionProperties));
            for (uint32_t i = 0; i < propCount; i++) props[i].type = XR_TYPE_EXTENSION_PROPERTIES;
            if (xrEnumerateInstanceExtensionProperties(NULL, propCount, &propCount, props) == XR_SUCCESS) {
                for (uint32_t i = 0; i < propCount; i++) {
                    const char* n = props[i].extensionName;
                    if (!strcmp(n, XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME)) vr_cylinderSupported = qtrue;
                    if (!strcmp(n, XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME)) vr_extPerfSettings = 1;
                    if (!strcmp(n, XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME)) vr_extThreadSettings = 1;
                    if (!strcmp(n, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME)) vr_extRefreshRate = 1;
                    if (!strcmp(n, XR_BD_CONTROLLER_INTERACTION_EXTENSION_NAME)) vr_extBDController = 1;
                }
            }
            free(props);
        }
        if (vr_cylinderSupported) extensionNames[extensionCount++] = XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME;
        if (vr_extPerfSettings) extensionNames[extensionCount++] = XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME;
        if (vr_extThreadSettings) extensionNames[extensionCount++] = XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME;
        if (vr_extRefreshRate) extensionNames[extensionCount++] = XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME;
        if (vr_extBDController) extensionNames[extensionCount++] = XR_BD_CONTROLLER_INTERACTION_EXTENSION_NAME;
        ALOGV("OpenXR extensions: cylinder %d perf %d thread %d refresh %d bd_controller %d",
              vr_cylinderSupported, vr_extPerfSettings, vr_extThreadSettings, vr_extRefreshRate, vr_extBDController);
    }
    instanceCreateInfo.enabledExtensionCount = extensionCount;
    instanceCreateInfo.enabledExtensionNames = extensionNames;

    XrResult initResult;
    OXR(initResult = xrCreateInstance(&instanceCreateInfo, &vr_engine.appState.Instance));
    if (initResult != XR_SUCCESS) {
        ALOGE("Failed to create XR instance: %d.", initResult);
        exit(1);
    }

    XrInstanceProperties instanceInfo;
    instanceInfo.type = XR_TYPE_INSTANCE_PROPERTIES;
    instanceInfo.next = NULL;
    OXR(xrGetInstanceProperties(vr_engine.appState.Instance, &instanceInfo));
    ALOGV(
            "Runtime %s: Version : %u.%u.%u",
            instanceInfo.runtimeName,
            XR_VERSION_MAJOR(instanceInfo.runtimeVersion),
            XR_VERSION_MINOR(instanceInfo.runtimeVersion),
            XR_VERSION_PATCH(instanceInfo.runtimeVersion));

    XrSystemGetInfo systemGetInfo;
    memset(&systemGetInfo, 0, sizeof(systemGetInfo));
    systemGetInfo.type = XR_TYPE_SYSTEM_GET_INFO;
    systemGetInfo.next = NULL;
    systemGetInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    XrSystemId systemId;
    OXR(initResult = xrGetSystem(vr_engine.appState.Instance, &systemGetInfo, &systemId));
    if (initResult != XR_SUCCESS) {
        ALOGE("Failed to get system.");
        exit(1);
    }

    // Get the graphics requirements.
    PFN_xrGetOpenGLESGraphicsRequirementsKHR pfnGetOpenGLESGraphicsRequirementsKHR = NULL;
    OXR(xrGetInstanceProcAddr(
            vr_engine.appState.Instance,
            "xrGetOpenGLESGraphicsRequirementsKHR",
            (PFN_xrVoidFunction*)(&pfnGetOpenGLESGraphicsRequirementsKHR)));

    XrGraphicsRequirementsOpenGLESKHR graphicsRequirements = {};
    graphicsRequirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR;
    OXR(pfnGetOpenGLESGraphicsRequirementsKHR(vr_engine.appState.Instance, systemId, &graphicsRequirements));

    vr_engine.appState.MainThreadTid = gettid();
    vr_engine.appState.SystemId = systemId;

    vr_engine.java = java;
    vr_initialized = qtrue;
    return &vr_engine;
}

void VR_InitCvars( void )
{
	Cvar_Get ("skip_ioq3_credits", "0.0", CVAR_ARCHIVE);
	// QuakeQuest puts Quake's eye (46 units above the floor) at 1.75 m (26.2467 units/m);
	// the same for Urban Terror's eye (60 units) gives 34.3 units/m
	vr_worldscale = Cvar_Get ("vr_worldscale", "34.3", CVAR_ARCHIVE);
	vr_worldscaleScaler = Cvar_Get ("vr_worldscaleScaler", "1.0", CVAR_ARCHIVE);
	Cvar_Get ("vr_urtLeanCamera", "0", CVAR_ARCHIVE); // 0 = head movement moves the player (QuakeQuest), 1 = camera slides
	Cvar_Get ("vr_urtControllerAim", "1", CVAR_ARCHIVE); // aim with the dominant controller (UrT)
	Cvar_Get ("vr_urtLaser", "1", CVAR_ARCHIVE); // laser sight from the hand
	Cvar_Get ("vr_urtCrosshair", "1", CVAR_ARCHIVE); // 3D crosshair where the hand aims
	Cvar_Get ("vr_menuCurved", "1", CVAR_ARCHIVE); // curved menu panel (cylinder layer)
	Cvar_Get ("vr_menuRadius", "5", CVAR_ARCHIVE); // distance of the curved menu panel (metres)
	Cvar_Get ("vr_menuLaser", "1", CVAR_ARCHIVE); // laser pointer in menus
	Cvar_Get ("vr_keyboard", "1", CVAR_ARCHIVE); // virtual keyboard while a menu text field is edited
	Cvar_Get ("vr_urtCrosshairSize", "2.5", CVAR_ARCHIVE); // crosshair size in degrees
	Cvar_Get ("vr_urtArms", "1", CVAR_ARCHIVE); // UrT arms: right hand with the gun, left arm on the left controller
	Cvar_Get ("vr_urtLeftHandForward", "0", CVAR_ARCHIVE); // left hand offset from the left controller (units)
	Cvar_Get ("vr_urtLeftHandLeft", "0", CVAR_ARCHIVE);
	Cvar_Get ("vr_urtLeftHandUp", "0", CVAR_ARCHIVE);
	Cvar_Get ("vr_urtLeftArmPitch", "0", CVAR_ARCHIVE); // left arm rotation in the controller frame (degrees)
	Cvar_Get ("vr_urtLeftArmYaw", "0", CVAR_ARCHIVE);
	Cvar_Get ("vr_urtLeftArmRoll", "0", CVAR_ARCHIVE);
	Cvar_Get ("vr_urtGunScale", "0.4", CVAR_ARCHIVE); // size of the gun in the hand (UrT view model is flat-screen sized)
	Cvar_Get ("vr_urtArmsCutoff", "16", CVAR_ARCHIVE); // first-person models closer to the eye than this are the arms
	Cvar_Get ("vr_urtRealHeight", "1", CVAR_ARCHIVE); // camera height = real head height (UrT)
	Cvar_Get ("vr_urtEyeHeight", "62", CVAR_ARCHIVE); // UrT standing eye height above the floor (units)
	// where UrT's view model has its grip, relative to the eye (game units): moved onto the controller
	Cvar_Get ("vr_urtGunForward", "18", CVAR_ARCHIVE);
	Cvar_Get ("vr_urtGunLeft", "-8", CVAR_ARCHIVE);
	Cvar_Get ("vr_urtGunUp", "-14", CVAR_ARCHIVE);
	vr_hudDepth = Cvar_Get ("vr_hudDepth", "3", CVAR_ARCHIVE);
	vr_righthanded = Cvar_Get ("vr_righthanded", "1", CVAR_ARCHIVE);
	vr_switchThumbsticks = Cvar_Get ("vr_switchThumbsticks", "0", CVAR_ARCHIVE);
	vr_snapturn = Cvar_Get ("vr_snapturn", "45", CVAR_ARCHIVE);
	vr_extralatencymode = Cvar_Get ("vr_extralatencymode", "1", CVAR_ARCHIVE);
	vr_directionMode = Cvar_Get ("vr_directionMode", "0", CVAR_ARCHIVE); // 0 = HMD, 1 = Off-hand
	vr_weaponPitch = Cvar_Get ("vr_weaponPitch", "-20", CVAR_ARCHIVE);
	vr_heightAdjust = Cvar_Get ("vr_heightAdjust", "0.0", CVAR_ARCHIVE);
    vr_twoHandedWeapons = Cvar_Get ("vr_twoHandedWeapons", "1", CVAR_ARCHIVE);
    vr_showItemInHand = Cvar_Get ("vr_showItemInHand", "1", CVAR_ARCHIVE);
	vr_refreshrate = Cvar_Get ("vr_refreshrate", "90", CVAR_ARCHIVE);
	vr_superSampling = Cvar_Get ("vr_superSampling", "1.2", CVAR_ARCHIVE);
    vr_weaponScope = Cvar_Get ("vr_weaponScope", "1", CVAR_ARCHIVE);
	vr_rollWhenHit = Cvar_Get ("vr_rollWhenHit", "0", CVAR_ARCHIVE);
	vr_hudYOffset = Cvar_Get ("vr_hudYOffset", "0", CVAR_ARCHIVE);
	// Fine vertical alignment of the 2D overlays, in NDC units. The eye-centre
	// correction should land the reticle on the aim point; this trims what is left.
	vr_2dYOffset = Cvar_Get ("vr_2dYOffset", "-0.02", CVAR_ARCHIVE);
	// Menu titles sit at y=16 of 480, hard against the top of the VR view. Shift them
	// down, in 640x480 units; see UI_DrawBannerString.
	vr_menuTitleYOffset = Cvar_Get ("vr_menuTitleYOffset", "24", CVAR_ARCHIVE);
	// Thumbstick hysteresis. A direction fires once the stick passes the press value and
	// only re-arms once it falls back below the release value, so the gap between them is
	// what stops a half-relaxed thumb triggering a second snap turn.
	vr_thumbstickPressThreshold = Cvar_Get ("vr_thumbstickPressThreshold", "0.6", CVAR_ARCHIVE);
	vr_thumbstickReleaseThreshold = Cvar_Get ("vr_thumbstickReleaseThreshold", "0.2", CVAR_ARCHIVE);
	vr_sendRollToServer = Cvar_Get ("vr_sendRollToServer", "1", CVAR_ARCHIVE);
	vr_lasersight = Cvar_Get ("vr_lasersight", "0", CVAR_ARCHIVE);
    vr_hapticIntensity = Cvar_Get ("vr_hapticIntensity", "1.0", CVAR_ARCHIVE);
    vr_comfortVignette = Cvar_Get ("vr_comfortVignette", "0.0", CVAR_ARCHIVE);
	vr_weaponSelectorMode = Cvar_Get ("vr_weaponSelectorMode", "2", CVAR_ARCHIVE);
	vr_weaponSelectorWithHud = Cvar_Get ("vr_weaponSelectorWithHud", "0", CVAR_ARCHIVE);
	vr_goreLevel = Cvar_Get ("vr_goreLevel", "2", CVAR_ARCHIVE);
	vr_hudDrawStatus = Cvar_Get ("vr_hudDrawStatus", "1", CVAR_ARCHIVE); // 0 - no hud, 1 - in-world hud, 2 - performance (static HUD)
	vr_showConsoleMessages = Cvar_Get ("vr_showConsoleMessages", "1", CVAR_ARCHIVE);

	// Values are:  scale,right,up,forward,pitch,yaw,roll
	// VALUES PROVIDED BY SkillFur - Thank-you!
	Cvar_Get ("vr_weapon_adjustment_1", "1,-4.0,7,-10,-20,-15,0", CVAR_ARCHIVE);
	Cvar_Get ("vr_weapon_adjustment_2", "0.8,-3.0,5.5,0,0,0,0", CVAR_ARCHIVE);
	Cvar_Get ("vr_weapon_adjustment_3", "0.8,-3.3,8,3.7,0,0,0", CVAR_ARCHIVE); // shotgun
	Cvar_Get ("vr_weapon_adjustment_4", "0.75,-5.4,6.5,-4,0,0,0", CVAR_ARCHIVE);
	Cvar_Get ("vr_weapon_adjustment_5", "0.8,-5.2,6,7.5,0,0,0", CVAR_ARCHIVE);
	Cvar_Get ("vr_weapon_adjustment_6", "0.8,-3.3,6,7,0,0,0", CVAR_ARCHIVE);
	Cvar_Get ("vr_weapon_adjustment_7", "0.8,-5.5,6,0,0,0,0", CVAR_ARCHIVE);
	Cvar_Get ("vr_weapon_adjustment_8", "0.8,-4.5,6,1.5,0,0,0", CVAR_ARCHIVE);
	Cvar_Get ("vr_weapon_adjustment_9", "0.8,-5.5,6,0,0,0,0", CVAR_ARCHIVE);

	//Team Arena Weapons
	Cvar_Get ("vr_weapon_adjustment_11", "0.8,-5.5,6,0,0,0,0", CVAR_ARCHIVE);
	Cvar_Get ("vr_weapon_adjustment_12", "0.8,-5.5,6,0,0,0,0", CVAR_ARCHIVE);
	Cvar_Get ("vr_weapon_adjustment_13", "0.8,-5.5,6,0,0,0,0", CVAR_ARCHIVE);

	// Control schemas
	Cvar_Get ("vr_uturn", "0", CVAR_ARCHIVE);
	Cvar_Get ("vr_controlSchema", "0", CVAR_ARCHIVE);
	qboolean uturnEnabled = Cvar_VariableValue( "vr_uturn" ) != 0;
	int controlSchema = (int)Cvar_VariableValue( "vr_controlSchema" ) % 3;
	if (controlSchema == 0) {
		// Default schema (weapon wheel on grip)
        Cvar_Get ("vr_button_map_RTHUMBLEFT", "turnleft", CVAR_ARCHIVE); // turn left
        Cvar_Get ("vr_button_map_RTHUMBRIGHT", "turnright", CVAR_ARCHIVE); // turn right
        Cvar_Get ("vr_button_map_RTHUMBFORWARD", "", CVAR_ARCHIVE); // unmapped
        if (uturnEnabled) {
            Cvar_Get ("vr_button_map_RTHUMBBACK", "uturn", CVAR_ARCHIVE); // u-turn
        } else {
            Cvar_Get ("vr_button_map_RTHUMBBACK", "", CVAR_ARCHIVE); // unmapped
        }
        Cvar_Get ("vr_button_map_PRIMARYGRIP", "+button5", CVAR_ARCHIVE); // Reload (UrT)
        Cvar_Get ("vr_button_map_PRIMARYTHUMBSTICK", "+button6", CVAR_ARCHIVE); // Bandage (UrT)
       	Cvar_Get ("vr_button_map_RTHUMBFORWARD_ALT", "", CVAR_ARCHIVE); // unmapped
       	Cvar_Get ("vr_button_map_RTHUMBRIGHT_ALT", "", CVAR_ARCHIVE); // unmapped
       	Cvar_Get ("vr_button_map_RTHUMBBACK_ALT", "", CVAR_ARCHIVE); // unmapped
       	Cvar_Get ("vr_button_map_RTHUMBLEFT_ALT", "", CVAR_ARCHIVE); // unmapped
    } else if (controlSchema == 1) {
	    // Weapon wheel on thumbstick - all directions as weapon select (useful for HMD wheel)
		Cvar_Get ("vr_button_map_RTHUMBFORWARD", "+weapon_select", CVAR_ARCHIVE);
    	Cvar_Get ("vr_button_map_RTHUMBRIGHT", "+weapon_select", CVAR_ARCHIVE);
    	Cvar_Get ("vr_button_map_RTHUMBBACK", "+weapon_select", CVAR_ARCHIVE);
    	Cvar_Get ("vr_button_map_RTHUMBLEFT", "+weapon_select", CVAR_ARCHIVE);
    	Cvar_Get ("vr_button_map_PRIMARYTHUMBSTICK", "+weapon_select", CVAR_ARCHIVE);
    	Cvar_Get ("vr_button_map_PRIMARYGRIP", "+alt", CVAR_ARCHIVE); // switch to alt layout
        Cvar_Get ("vr_button_map_RTHUMBLEFT_ALT", "turnleft", CVAR_ARCHIVE); // turn left
        Cvar_Get ("vr_button_map_RTHUMBRIGHT_ALT", "turnright", CVAR_ARCHIVE); // turn right
        Cvar_Get ("vr_button_map_RTHUMBFORWARD_ALT", "blank", CVAR_ARCHIVE);
        if (uturnEnabled) {
            Cvar_Get ("vr_button_map_RTHUMBBACK_ALT", "uturn", CVAR_ARCHIVE);
        } else {
            Cvar_Get ("vr_button_map_RTHUMBBACK_ALT", "blank", CVAR_ARCHIVE);
        }
	} else {
		// Weapon wheel disabled - only prev/next weapon switch is active
		Cvar_Get ("vr_button_map_RTHUMBLEFT", "turnleft", CVAR_ARCHIVE); // turn left
		Cvar_Get ("vr_button_map_RTHUMBRIGHT", "turnright", CVAR_ARCHIVE); // turn right
		Cvar_Get ("vr_button_map_RTHUMBFORWARD", "weapnext", CVAR_ARCHIVE); // next weapon
		if (uturnEnabled) {
			Cvar_Get ("vr_button_map_RTHUMBBACK", "uturn", CVAR_ARCHIVE); // u-turn
		} else {
			Cvar_Get ("vr_button_map_RTHUMBBACK", "weapprev", CVAR_ARCHIVE); // previous weapon
		}
		Cvar_Get ("vr_button_map_PRIMARYGRIP", "+alt", CVAR_ARCHIVE); // switch to alt layout
		Cvar_Get ("vr_button_map_PRIMARYTHUMBSTICK", "+button6", CVAR_ARCHIVE); // Bandage (UrT)
		Cvar_Get ("vr_button_map_RTHUMBFORWARD_ALT", "", CVAR_ARCHIVE); // unmapped
		Cvar_Get ("vr_button_map_RTHUMBRIGHT_ALT", "", CVAR_ARCHIVE); // unmapped
		Cvar_Get ("vr_button_map_RTHUMBBACK_ALT", "", CVAR_ARCHIVE); // unmapped
		Cvar_Get ("vr_button_map_RTHUMBLEFT_ALT", "", CVAR_ARCHIVE); // unmapped
	}

	// Map face buttons based on thumbstick assigned to movement
	// (cannot move and jump/crouch with same thumb at same time)
	qboolean switchThumbsticks = Cvar_VariableValue( "vr_switchThumbsticks" ) != 0;
	if (switchThumbsticks) {
		Cvar_Get ("vr_button_map_A", "+button3", CVAR_ARCHIVE); // Fire mode (UrT)
		Cvar_Get ("vr_button_map_B", "+button5", CVAR_ARCHIVE); // Reload (UrT)
		Cvar_Get ("vr_button_map_X", "+moveup", CVAR_ARCHIVE); // Jump
		Cvar_Get ("vr_button_map_Y", "+movedown", CVAR_ARCHIVE); // Crouch
	} else {
		Cvar_Get ("vr_button_map_A", "+movedown", CVAR_ARCHIVE); // Crouch
		Cvar_Get ("vr_button_map_B", "+moveup", CVAR_ARCHIVE); // Jump (B, the user prefers it)
		Cvar_Get ("vr_button_map_X", "+button3", CVAR_ARCHIVE); // Fire mode (UrT)
		Cvar_Get ("vr_button_map_Y", "+scores", CVAR_ARCHIVE); // Scoreboard (reload is on the right grip)
	}


	//Remaining button mapping (buttons not affected by schemas)
	Cvar_Get ("vr_button_map_A_ALT", "", CVAR_ARCHIVE); // unmapped
	Cvar_Get ("vr_button_map_B_ALT", "", CVAR_ARCHIVE); // unmapped
	Cvar_Get ("vr_button_map_X_ALT", "", CVAR_ARCHIVE); // unmapped
	Cvar_Get ("vr_button_map_Y_ALT", "", CVAR_ARCHIVE); // unmapped
	Cvar_Get ("vr_button_map_SECONDARYTHUMBSTICK", "+button8", CVAR_ARCHIVE); // Sprint (UrT)
	Cvar_Get ("vr_button_map_SECONDARYTHUMBSTICK_ALT", "", CVAR_ARCHIVE); // unmapped
	Cvar_Get ("vr_button_map_PRIMARYTHUMBSTICK_ALT", "", CVAR_ARCHIVE); // unmapped
	Cvar_Get ("vr_button_map_SECONDARYTRIGGER", "+button7", CVAR_ARCHIVE); // Interact / pick up: doors, defuse (UrT)
	Cvar_Get ("vr_button_map_SECONDARYTRIGGER_ALT", "", CVAR_ARCHIVE); // unmapped
	Cvar_Get ("vr_button_map_PRIMARYTRIGGER", "+attack", CVAR_ARCHIVE); // Fire
	Cvar_Get ("vr_button_map_PRIMARYTRIGGER_ALT", "", CVAR_ARCHIVE); // unmapped
	Cvar_Get ("vr_button_map_SECONDARYGRIP", "+weapon_stabilise", CVAR_ARCHIVE); // Weapon stabilisation
	Cvar_Get ("vr_button_map_SECONDARYGRIP_ALT", "", CVAR_ARCHIVE); // unmapped

	// Urban Terror: +button5 = reload ("R"), +button3 = weapon fire mode. Quake 3's +button2
	// (use item) does nothing in UrT, so that button switches the fire mode. Migrates configs
	// saved by earlier builds.
	if ( !strcmp( Cvar_VariableString( "vr_button_map_Y" ), "+button3" ) ) Cvar_Set( "vr_button_map_Y", "+button5" );
	if ( !strcmp( Cvar_VariableString( "vr_button_map_B" ), "+button3" ) ) Cvar_Set( "vr_button_map_B", "+button5" );
	if ( !strcmp( Cvar_VariableString( "vr_button_map_X" ), "+button2" ) ) Cvar_Set( "vr_button_map_X", "+button3" );
	if ( !strcmp( Cvar_VariableString( "vr_button_map_A" ), "+button2" ) ) Cvar_Set( "vr_button_map_A", "+button3" );
	// left trigger = interact (+button7), right stick click = bandage (+button6), left stick
	// click = sprint (+button8); the old second jump / unused / scoreboard bindings migrate
	if ( !strcmp( Cvar_VariableString( "vr_button_map_SECONDARYTRIGGER" ), "+moveup" ) ) Cvar_Set( "vr_button_map_SECONDARYTRIGGER", "+button7" );
	if ( !Cvar_VariableString( "vr_button_map_PRIMARYTHUMBSTICK" )[0] ) Cvar_Set( "vr_button_map_PRIMARYTHUMBSTICK", "+button6" );
	if ( !strcmp( Cvar_VariableString( "vr_button_map_SECONDARYTHUMBSTICK" ), "+scores" ) ) Cvar_Set( "vr_button_map_SECONDARYTHUMBSTICK", "+button8" );
	// reload on the right grip (ioq3quest's weapon wheel there does nothing in UrT); Y, which had
	// reload, becomes free. Only migrates configs that still have the old grip binding.
	if ( !strcmp( Cvar_VariableString( "vr_button_map_PRIMARYGRIP" ), "+weapon_select" ) ) {
		Cvar_Set( "vr_button_map_PRIMARYGRIP", "+button5" );
		if ( !strcmp( Cvar_VariableString( "vr_button_map_Y" ), "+button5" ) ) Cvar_Set( "vr_button_map_Y", "+scores" );
	}
	// Y = scoreboard (it was briefly left empty)
	if ( !Cvar_VariableIntegerValue( "vr_switchThumbsticks" ) && !Cvar_VariableString( "vr_button_map_Y" )[0] ) {
		Cvar_Set( "vr_button_map_Y", "+scores" );
	}
	// B = jump, A = crouch (swapped from ioq3quest; saved configs with the old layout migrate)
	if ( !Cvar_VariableIntegerValue( "vr_switchThumbsticks" ) &&
			!strcmp( Cvar_VariableString( "vr_button_map_A" ), "+moveup" ) &&
			!strcmp( Cvar_VariableString( "vr_button_map_B" ), "+movedown" ) ) {
		Cvar_Set( "vr_button_map_A", "+movedown" );
		Cvar_Set( "vr_button_map_B", "+moveup" );
	}
	// weapon switching: ioq3quest's weapon wheel (+weapon_select on the right grip) lives in its
	// own Quake 3 cgame and does nothing in UrT, so right stick up/down = next/previous weapon
	{
		const char *f = Cvar_VariableString( "vr_button_map_RTHUMBFORWARD" );
		const char *b = Cvar_VariableString( "vr_button_map_RTHUMBBACK" );
		if ( !f[0] || !strcmp( f, "+weapon_select" ) ) Cvar_Set( "vr_button_map_RTHUMBFORWARD", "weapnext" );
		if ( !b[0] || !strcmp( b, "+weapon_select" ) || !strcmp( b, "uturn" ) ) Cvar_Set( "vr_button_map_RTHUMBBACK", "weapprev" );
	}

	Cvar_Get ("vr_button_map_PRIMARYGRIP_ALT", "", CVAR_ARCHIVE); // unmapped

    vr.menuYaw = 0;
    vr.recenterYaw = 0;
}

void VR_Destroy( engine_t* engine )
{
    if (engine == &vr_engine) {
        xrDestroyInstance(engine->appState.Instance);
        ovrApp_Destroy(&engine->appState);
    }
}

void VR_EnterVR( engine_t* engine, ovrJava java ) {

    if (engine->appState.Session) {
        Com_Printf("VR_EnterVR called with existing session");
        return;
    }

    // Create the OpenXR Session.
    XrGraphicsBindingOpenGLESAndroidKHR graphicsBindingAndroidGLES = {};
    graphicsBindingAndroidGLES.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR;
    graphicsBindingAndroidGLES.next = NULL;
    graphicsBindingAndroidGLES.display = eglGetCurrentDisplay();
    graphicsBindingAndroidGLES.config = eglGetCurrentSurface(EGL_DRAW);
    graphicsBindingAndroidGLES.context = eglGetCurrentContext();

    XrSessionCreateInfo sessionCreateInfo = {};
    memset(&sessionCreateInfo, 0, sizeof(sessionCreateInfo));
    sessionCreateInfo.type = XR_TYPE_SESSION_CREATE_INFO;
    sessionCreateInfo.next = &graphicsBindingAndroidGLES;
    sessionCreateInfo.createFlags = 0;
    sessionCreateInfo.systemId = engine->appState.SystemId;

    XrResult initResult;
    OXR(initResult = xrCreateSession(engine->appState.Instance, &sessionCreateInfo, &engine->appState.Session));
    if (initResult != XR_SUCCESS) {
        ALOGE("Failed to create XR session: %d.", initResult);
        exit(1);
    }

    // Create a space to the first path
    XrReferenceSpaceCreateInfo spaceCreateInfo = {};
    spaceCreateInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    spaceCreateInfo.poseInReferenceSpace.orientation.w = 1.0f;
    OXR(xrCreateReferenceSpace(engine->appState.Session, &spaceCreateInfo, &engine->appState.HeadSpace));
}

void VR_LeaveVR( engine_t* engine ) {
    if (engine->appState.Session) {
        OXR(xrDestroySpace(engine->appState.HeadSpace));
        // StageSpace is optional.
        if (engine->appState.StageSpace != XR_NULL_HANDLE) {
            OXR(xrDestroySpace(engine->appState.StageSpace));
        }
        OXR(xrDestroySpace(engine->appState.FakeStageSpace));
        engine->appState.CurrentSpace = XR_NULL_HANDLE;
        OXR(xrDestroySession(engine->appState.Session));
        engine->appState.Session = NULL;
    }
}

engine_t* VR_GetEngine( void ) {
	return &vr_engine;
}

int VR_isPauseable( void )
{
	return ( clc.state == CA_ACTIVE) && !Cvar_VariableValue ("cl_paused");
}

int VR_useScreenLayer( void )
{
	//intermission is never full screen
    if ( cl.snap.ps.pm_type == PM_INTERMISSION )
    {
        return 0;
    }

    int keyCatcher = Key_GetCatcher( );
	return ( clc.state == CA_CINEMATIC ||
			( keyCatcher & (KEYCATCH_UI | KEYCATCH_CONSOLE) ));
}
//#endif
