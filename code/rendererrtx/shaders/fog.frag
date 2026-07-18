#version 450

layout(set = 0, binding = 0) uniform UBO {
	// light/env parameters:
	vec4 eyePos;				// vertex
	vec4 lightPos;				// vertex: light origin
	vec4 lightColor;			// fragment: rgb + 1/(r*r)
	vec4 lightVector;			// fragment: linear dynamic light
//#ifdef USE_FOG
	// fog parameters:
	vec4 fogDistanceVector;		// vertex
	vec4 fogDepthVector;		// vertex
	vec4 fogEyeT;				// vertex
	vec4 fogColor;				// fragment
//#endif
};

layout(set = 2, binding = 0) uniform sampler2D fog_texture;

//layout(location = 0) in vec4 frag_color;
//layout(location = 1) in vec2 frag_tex_coord0;
//layout(location = 2) in vec2 frag_tex_coord1;
//layout(location = 3) in vec2 frag_tex_coord2;
layout(location = 4) in vec2 fog_tex_coord;

layout(location = 0) out vec4 out_color;

float FogFactor(vec2 fogCoord)
{
	if (fogEyeT.z < 0.5)
		return texture(fog_texture, fogCoord).a;

	float fogDistance = fogCoord.x - 0.001953125;
	if (fogDistance <= 0.0 || fogCoord.y < 0.03125)
		return 0.0;
	if (fogCoord.y < 0.96875)
		fogDistance *= (fogCoord.y - 0.03125) / 0.9375;
	return sqrt(clamp(fogDistance * 8.0, 0.0, 1.0));
}

void main() {
	float fogFactor = FogFactor(fog_tex_coord);
	out_color = vec4(fogColor.rgb, fogColor.a * fogFactor);
}
