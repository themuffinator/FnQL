#version 450

layout(push_constant) uniform Transform {
	mat4 mvp;
};

/* Prefix-compatible with vkUniform_t. Liquid draws reuse otherwise idle
 * generic-light/fog/CSM slots for a bounded impulse list, the reflection
 * color/weight, the depth-reject toggle, and the model-to-clip matrix. */
layout(set = 0, binding = 0) uniform LiquidUniforms {
	vec4 eye_pos;
	vec4 liquid_params;              // wrapped time, warp pixels, pass strength, inverse target width
	vec4 liquid_info;                // type scale, impulse count, inverse target height, refraction pass
	vec4 liquid_impulse[8];          // model-space xyz, expanding ring radius
	vec4 liquid_amplitude[2];        // eight packed ripple pixel amplitudes
	vec4 liquid_reflect;             // material sheen color, reflection weight
	vec4 liquid_depth;               // x: depth reject enabled
	mat4 liquid_mvp;                 // model space -> clip space for reflection reprojection
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
