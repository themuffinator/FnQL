#version 460
#extension GL_EXT_ray_tracing : require

struct RtRadiancePayload {
	vec3 color;
	float hitT;
	vec3 normal;
	uint flags;
	float shadowFactor;
	float shadowResponse;
	vec3 shadowLoss;
};

layout(location = 0) rayPayloadInEXT RtRadiancePayload payloadRadiance;

layout(push_constant) uniform RtPushConstants {
	vec4 cameraOriginTanHalfFovX;
	vec4 cameraForwardTanHalfFovY;
	vec4 cameraRightWidth;
	vec4 cameraUpHeight;
	vec4 sunDirection;
	vec4 sunColorIntensity;
	uint debugMode;
	uint frameIndex;
	uint activeInstances;
	uint lightCount;
	uint worldMaterialCount;
	uint dynamicMaterialCount;
	uint shadowMode;
	float shadowSoftness;
	float indirectStrength;
	float reflectionStrength;
	float skyIntensity;
	float refractionIor;
	float sunIntensity;
	uint refractiveMode;
	uint indirectBounce;
} pc;

vec3 safe_normalize(vec3 value, vec3 fallback)
{
	float lengthSquared = dot(value, value);
	if (!(lengthSquared > 1e-12) || any(isnan(value)) || any(isinf(value))) {
		return fallback;
	}
	return value * inversesqrt(lengthSquared);
}

void main()
{
	vec3 rayDir = safe_normalize(gl_WorldRayDirectionEXT, vec3(1.0, 0.0, 0.0));
	float up = clamp(rayDir.z * 0.5 + 0.5, 0.0, 1.0);
	vec3 skyBottom = vec3(0.055, 0.070, 0.090);
	vec3 skyTop = vec3(0.32, 0.44, 0.67);
	vec3 skyColor = mix(skyBottom, skyTop, up) * pc.skyIntensity;
	vec3 sunDir = safe_normalize(pc.sunDirection.xyz, vec3(0.0, 0.0, 1.0));
	vec3 sunColor = pc.sunColorIntensity.rgb * pc.sunIntensity;
	float sunDisk = pow(max(dot(rayDir, sunDir), 0.0), 320.0);
	vec3 environment = skyColor + sunColor * sunDisk;

	payloadRadiance.color = environment;
	payloadRadiance.hitT = 0.0;
	payloadRadiance.normal = vec3(0.0, 0.0, 1.0);
	payloadRadiance.flags = 0u;
	payloadRadiance.shadowFactor = 1.0;
	payloadRadiance.shadowResponse = 0.0;
	payloadRadiance.shadowLoss = vec3(0.0);
}
