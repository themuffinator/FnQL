#version 450

layout(push_constant) uniform Transform {
	mat4 mvp;
};

layout(set = 0, binding = 0) uniform UBO {
	vec4 eyePos;
	vec4 lightPos;
	vec4 lightColor;
	vec4 lightVector;
	vec4 fogDistanceVector;
	vec4 fogDepthVector;
	vec4 fogEyeT;
	vec4 fogColor;
	vec4 texFactors;
	vec4 depthFadeInfo;
	vec4 depthFadeScale;
	vec4 depthFadeBias;
	vec4 dlightFactors;
	vec4 csmModelX;
	vec4 csmModelY;
	vec4 csmModelZ;
	vec4 csmAxisX;
	vec4 csmAxisY;
	vec4 csmAxisZ;
	vec4 csmInvExtents;
	vec4 csmSplitAtlas;
	vec4 csmShadowColor;
	vec4 csmView;
};

layout(location = 0) in vec3 in_position;

layout(location = 0) out vec3 world_pos;
layout(location = 1) out float view_dist;

out gl_PerVertex {
	vec4 gl_Position;
};

void main() {
	vec4 local_pos = vec4(in_position, 1.0);
	vec3 wp = vec3(dot(csmModelX, local_pos), dot(csmModelY, local_pos), dot(csmModelZ, local_pos));
	world_pos = wp;
	view_dist = dot(csmView.xyz, wp) + csmView.w;
	gl_Position = mvp * local_pos;
}
