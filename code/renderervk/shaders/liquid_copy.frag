#version 450

layout(set = 0, binding = 0) uniform sampler2D current_scene;

layout(location = 0) in vec2 frag_tex_coord;
layout(location = 0) out vec4 out_color;

void main()
{
	out_color = texture(current_scene, frag_tex_coord);
}
