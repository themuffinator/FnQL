#version 450

// Menu soft focus.
//
// One tap set serves the whole effect. The kernel is the 3-tap linear-sampling
// form of a 5-tap binomial (weights 5, 6, 5 at -1.2, 0, +1.2 spacings), which is
// what renderercommon/tr_menu_blur.h tabulates as
// MENU_BLUR_KERNEL_VARIANCE_LINEAR3; changing it here without changing that
// constant would desynchronise this backend from the GL one.
//
// A zero offset collapses the three taps onto the same coordinate and the
// weights still sum to one, so the same shader is a plain bilinear resample.
// That is exactly what the two pyramid downsample steps and the final composite
// want, so they reuse it rather than needing a copy shader of their own.

layout(set = 0, binding = 0) uniform sampler2D texture0;

layout(location = 0) in vec2 tex_coord0;

layout(location = 0) out vec4 out_color;

layout(push_constant) uniform Push {
	// xy: tap offset in normalized texture coordinates; zero resamples.
	// z: weight written to alpha, which the composite pipeline blends with.
	vec4 params;
} pc;

void main()
{
	vec2 offset = pc.params.xy;

	vec3 color = texture( texture0, tex_coord0 ).rgb * ( 6.0 / 16.0 )
		+ texture( texture0, tex_coord0 + offset ).rgb * ( 5.0 / 16.0 )
		+ texture( texture0, tex_coord0 - offset ).rgb * ( 5.0 / 16.0 );

	out_color = vec4( color, pc.params.z );
}
