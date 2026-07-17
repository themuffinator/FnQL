#version 450

layout(set = 0, binding = 0) uniform sampler2D current_scene;

layout(location = 0) in vec2 frag_tex_coord;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform PostPushConstants {
	vec4 motion; // normalized blur radius xy, unused zw
	vec4 view_bounds; // min.xy and max.xy sample centers for the 3D viewport
} pc;

void main()
{
	const float offsets[7] = float[]( -1.0, -0.6666667, -0.3333333, 0.0,
		0.3333333, 0.6666667, 1.0 );
	const float weights[7] = float[]( 1.0 / 64.0, 6.0 / 64.0, 15.0 / 64.0,
		20.0 / 64.0, 15.0 / 64.0, 6.0 / 64.0, 1.0 / 64.0 );
	vec3 color = vec3( 0.0 );

	if ( any( lessThan( frag_tex_coord, pc.view_bounds.xy ) ) ||
		any( greaterThan( frag_tex_coord, pc.view_bounds.zw ) ) ) {
		out_color = texture( current_scene, frag_tex_coord );
		return;
	}

	for ( int i = 0; i < 7; ++i ) {
		vec2 uv = clamp( frag_tex_coord + pc.motion.xy * offsets[i],
			pc.view_bounds.xy, pc.view_bounds.zw );
		color += texture( current_scene, uv ).rgb * weights[i];
	}
	out_color = vec4( color, 1.0 );
}
