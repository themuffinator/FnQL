#version 450

layout(set = 0, binding = 0) uniform sampler2D texture0;

layout(location = 0) in vec2 frag_tex_coord;

layout(location = 0) out vec4 out_color;

//layout(constant_id = 0) const float gamma = 1.0;
//layout(constant_id = 1) const float obScale = 2.0;
//layout(constant_id = 2) const float greyscale = 0.0;
layout(constant_id = 3) const float threshold = 0.6;
//layout(constant_id = 4) const float factor = 0.5;
layout(constant_id = 5) const int extract_mode = 0;
layout(constant_id = 6) const int base_modulate = 0;
layout(constant_id = 14) const int toneMapMode = 0;
layout(constant_id = 15) const float toneMapExposure = 1.0;
layout(constant_id = 16) const float softKnee = 0.0;

//const vec3 sRGB = { 0.2126, 0.7152, 0.0722 };

const vec3 lumaWeights = { 0.2126, 0.7152, 0.0722 };

float bloomMetric(vec3 color) {
	if ( extract_mode == 1 ) {
		return (color.r + color.g + color.b) * 0.33333333;
	}
	if ( extract_mode == 2 ) {
		return dot(lumaWeights, color);
	}
	return max(max(color.r, color.g), color.b);
}

float bloomWeight(float metric) {
	float exposedMetric = metric * (toneMapMode == 0 ? 1.0 : max(toneMapExposure, 0.0));
	if ( softKnee <= 0.0 ) {
		return step(threshold, exposedMetric);
	}
	float knee = max(threshold * softKnee, 0.0001);
	return smoothstep(threshold - knee, threshold + knee, exposedMetric);
}

vec3 applyBaseModulation(vec3 color) {
	if ( base_modulate == 1 ) {
		return color * color;
	}
	if ( base_modulate != 0 ) {
		return color * dot( lumaWeights, color );
	}
	return color;
}

void main() {
	vec3 base = texture(texture0, frag_tex_coord).rgb;
	float metric = bloomMetric(base);
	float weight = bloomWeight(metric);

	out_color = vec4(applyBaseModulation(base) * weight, weight);
}
