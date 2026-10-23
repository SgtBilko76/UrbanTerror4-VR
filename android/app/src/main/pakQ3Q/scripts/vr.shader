
//
// VR Hud Shader Sprite - the image is just a placeholder and is replaced in code
//

sprites/vr/hud
{
	cull disable
	{
		map $whiteimage	// placeholder, replaced by the HUD buffer in code (Q3's sprites/plasmaa.tga is not in standalone games)
		blendfunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
	}
}