#version 450

layout(push_constant) uniform Transform {
	mat4 mvp;
};

/* Prefix-compatible with vkUniform_t. Liquid draws reuse otherwise idle
 * generic-light/fog slots for the impulse list and reflection controls. */
layout(set = 0, binding = 0) uniform LiquidUniforms {
	vec4 eye_pos;
	vec4 liquid_params;
	vec4 liquid_info;
	vec4 liquid_impulse[8];
	vec4 liquid_amplitude[2];
	vec4 liquid_reflect;
	vec4 liquid_depth;
	mat4 liquid_mvp;
};

layout(location = 0) in vec3 in_position;
layout(location = 5) in vec3 in_normal;

layout(location = 0) out vec4 frag_screen;
layout(location = 1) out vec3 frag_position;
layout(location = 2) out vec3 frag_normal;
layout(location = 3) out vec3 frag_view;

out gl_PerVertex {
	vec4 gl_Position;
};

void main()
{
	vec4 clip = mvp * vec4(in_position, 1.0);
	gl_Position = clip;
	frag_screen = vec4(clip.xy * 0.5 + clip.ww * 0.5, 0.0, clip.w);
	frag_position = in_position;
	frag_normal = in_normal;
	frag_view = eye_pos.xyz - in_position;
}
