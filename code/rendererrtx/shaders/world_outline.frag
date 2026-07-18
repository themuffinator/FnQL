#version 450

layout(set = 0, binding = 0) uniform sampler2D depth_texture;

layout(location = 0) in vec2 frag_tex_coord;

layout(location = 0) out vec4 out_color;

layout(push_constant) uniform PostPushConstants {
	vec4 outline; // inv width, inv height, depth threshold, alpha
} pc;

void main()
{
	float center = texture(depth_texture, frag_tex_coord).r;
	vec2 texel = pc.outline.xy;

	float left = texture(depth_texture, frag_tex_coord - vec2( texel.x, 0.0 )).r;
	float right = texture(depth_texture, frag_tex_coord + vec2( texel.x, 0.0 )).r;
	float up = texture(depth_texture, frag_tex_coord + vec2( 0.0, texel.y )).r;
	float down = texture(depth_texture, frag_tex_coord - vec2( 0.0, texel.y )).r;

	float edge = abs(left + right - center * 2.0);
	edge = max(edge, abs(up + down - center * 2.0));

	out_color = vec4(0.0, 0.0, 0.0, step(pc.outline.z, edge) * pc.outline.w);
}
