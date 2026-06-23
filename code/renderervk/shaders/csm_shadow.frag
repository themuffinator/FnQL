#version 450

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

layout(set = 1, binding = 0) uniform sampler2D csm_shadow_texture;

layout(location = 0) in vec3 world_pos;
layout(location = 1) in float view_dist;

layout(location = 0) out vec4 out_color;

float sample_shadow(vec2 atlas_uv, float receiver_depth, vec2 texel, vec2 filter_a, vec2 filter_b) {
	float sample0 = texture(csm_shadow_texture, atlas_uv + texel * vec2(-filter_b.y, -filter_a.x)).r;
	float sample1 = texture(csm_shadow_texture, atlas_uv + texel * vec2( filter_a.x, -filter_b.y)).r;
	float sample2 = texture(csm_shadow_texture, atlas_uv + texel * vec2(-filter_a.x,  filter_b.y)).r;
	float sample3 = texture(csm_shadow_texture, atlas_uv + texel * vec2( filter_b.y,  filter_a.x)).r;

	float forward =
		(1.0 - step(receiver_depth, sample0)) +
		(1.0 - step(receiver_depth, sample1)) +
		(1.0 - step(receiver_depth, sample2)) +
		(1.0 - step(receiver_depth, sample3));
	float reversed =
		step(receiver_depth, sample0) +
		step(receiver_depth, sample1) +
		step(receiver_depth, sample2) +
		step(receiver_depth, sample3);

	return 0.25 * mix(forward, reversed, csmShadowColor.w);
}

void main() {
	if (view_dist < csmSplitAtlas.x || view_dist > csmSplitAtlas.y) {
		discard;
	}

	vec3 light_coord;
	light_coord.x = (dot(world_pos, csmAxisX.xyz) - csmAxisX.w) * csmInvExtents.x;
	light_coord.y = (dot(world_pos, csmAxisY.xyz) - csmAxisY.w) * csmInvExtents.y;
	light_coord.z = (dot(world_pos, csmAxisZ.xyz) - csmAxisZ.w) * csmInvExtents.z;

	if (any(lessThan(light_coord, vec3(0.0))) || any(greaterThan(light_coord, vec3(1.0)))) {
		discard;
	}

	float forward_depth = clamp(light_coord.x - csmInvExtents.w, 0.0, 1.0);
	float reversed_depth = clamp(1.0 - light_coord.x + csmInvExtents.w, 0.0, 1.0);
	float receiver_depth = mix(forward_depth, reversed_depth, csmShadowColor.w);
	vec2 texel = 1.0 / vec2(textureSize(csm_shadow_texture, 0));
	float atlas_x = csmSplitAtlas.z + light_coord.y * csmSplitAtlas.w;
	float atlas_x_min = csmSplitAtlas.z + texel.x;
	float atlas_x_max = csmSplitAtlas.z + csmSplitAtlas.w - texel.x;
	vec2 atlas_uv = vec2(
		clamp(atlas_x, atlas_x_min, atlas_x_max),
		clamp(1.0 - light_coord.z, texel.y, 1.0 - texel.y));
	vec2 filter_a = vec2(texFactors.z);
	vec2 filter_b = vec2(texFactors.w);
	float occlusion = sample_shadow(atlas_uv, receiver_depth, texel, filter_a, filter_b);

	out_color = vec4(vec3(1.0) - occlusion * csmShadowColor.rgb, 1.0);
}
