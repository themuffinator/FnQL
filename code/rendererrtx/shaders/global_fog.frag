#version 450

layout(set = 0, binding = 0) uniform sampler2D depth_texture;

layout(location = 0) in vec2 frag_tex_coord;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform GlobalFogPushConstants {
	vec4 fog_color; // RGB plus final opacity multiplier
	vec4 fog_params; // start, end, density, mode (exp/exp2/linear)
	vec4 depth_params; // near, far, fog sky, reversed depth
} pc;

float scene_distance(float depth) {
	float near_plane = max(pc.depth_params.x, 0.0001);
	float far_plane = max(pc.depth_params.y, near_plane + 0.0001);
	float span = far_plane - near_plane;
	float denominator = pc.depth_params.w > 0.5 ?
		near_plane + depth * span : far_plane - depth * span;
	return near_plane * far_plane / max(denominator, 0.0001);
}

void main() {
	float depth = texture(depth_texture, frag_tex_coord).r;
	float distance;
	float amount;
	float alpha;

	if (pc.fog_color.a <= 0.0 || pc.fog_params.z <= 0.0) {
		out_color = vec4(0.0);
		return;
	}
	if (pc.depth_params.z < 0.5) {
		bool clear_depth = pc.depth_params.w > 0.5 ? depth <= 0.00001 : depth >= 0.99999;
		if (clear_depth) {
			out_color = vec4(0.0);
			return;
		}
	}

	distance = max(scene_distance(depth) - pc.fog_params.x, 0.0);
	if (pc.fog_params.w < 0.5) {
		amount = 1.0 - exp(-pc.fog_params.z * distance);
	} else if (pc.fog_params.w < 1.5) {
		float fog_distance = pc.fog_params.z * distance;
		amount = 1.0 - exp(-(fog_distance * fog_distance));
	} else {
		amount = clamp(distance / max(pc.fog_params.y - pc.fog_params.x, 0.0001), 0.0, 1.0);
	}

	alpha = clamp(amount * pc.fog_color.a, 0.0, 1.0);
	out_color = vec4(pc.fog_color.rgb, alpha);
}
