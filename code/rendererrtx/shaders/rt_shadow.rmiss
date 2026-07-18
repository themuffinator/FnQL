#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 1) rayPayloadInEXT uint payloadShadow;

void main()
{
	payloadShadow = 1u;
}
