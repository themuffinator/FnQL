uniform sampler2D u_TextureMap;

uniform vec4      u_Color;

uniform vec2      u_InvTexRes;
varying vec2      var_TexCoords;

const vec3  LUMINANCE_VECTOR =   vec3(0.2125, 0.7154, 0.0721); //vec3(0.299, 0.587, 0.114);

vec3 GetValues(vec2 offset, vec3 current)
{
	vec2 tc = var_TexCoords + u_InvTexRes * offset;
	vec3 minAvgMax = texture2D(u_TextureMap, tc).rgb;

#ifdef FIRST_PASS

  #if defined(USE_PBR)
	minAvgMax *= minAvgMax;
  #endif

	float lumi = max(dot(LUMINANCE_VECTOR, minAvgMax), 0.000001);
	float loglumi = clamp(log2(lumi), -10.0, 10.0);
	minAvgMax = vec3(loglumi * 0.05 + 0.5);
#endif

	return vec3(min(current.x, minAvgMax.x), current.y + minAvgMax.y, max(current.z, minAvgMax.z));
}

#ifdef HISTOGRAM_PERCENTILE
float GetPercentileChannelSample(vec2 offset, int channel)
{
	vec3 value = texture2D(u_TextureMap, var_TexCoords + u_InvTexRes * offset).rgb;

	if (channel == 0)
		return value.x;
	if (channel == 1)
		return value.y;

	return value.z;
}

void SortPercentileSamples(inout float s0, inout float s1, inout float s2, inout float s3,
	inout float s4, inout float s5, inout float s6, inout float s7,
	inout float s8, inout float s9, inout float s10, inout float s11,
	inout float s12, inout float s13, inout float s14, inout float s15)
{
	float tmp;

#define SORT_PAIR(a, b) tmp = min(a, b); b = max(a, b); a = tmp
	SORT_PAIR(s0, s1); SORT_PAIR(s2, s3); SORT_PAIR(s4, s5); SORT_PAIR(s6, s7);
	SORT_PAIR(s8, s9); SORT_PAIR(s10, s11); SORT_PAIR(s12, s13); SORT_PAIR(s14, s15);
	SORT_PAIR(s0, s2); SORT_PAIR(s1, s3); SORT_PAIR(s4, s6); SORT_PAIR(s5, s7);
	SORT_PAIR(s8, s10); SORT_PAIR(s9, s11); SORT_PAIR(s12, s14); SORT_PAIR(s13, s15);
	SORT_PAIR(s1, s2); SORT_PAIR(s5, s6); SORT_PAIR(s9, s10); SORT_PAIR(s13, s14);
	SORT_PAIR(s0, s4); SORT_PAIR(s1, s5); SORT_PAIR(s2, s6); SORT_PAIR(s3, s7);
	SORT_PAIR(s8, s12); SORT_PAIR(s9, s13); SORT_PAIR(s10, s14); SORT_PAIR(s11, s15);
	SORT_PAIR(s2, s4); SORT_PAIR(s3, s5); SORT_PAIR(s10, s12); SORT_PAIR(s11, s13);
	SORT_PAIR(s1, s2); SORT_PAIR(s3, s4); SORT_PAIR(s5, s6);
	SORT_PAIR(s9, s10); SORT_PAIR(s11, s12); SORT_PAIR(s13, s14);
	SORT_PAIR(s0, s8); SORT_PAIR(s1, s9); SORT_PAIR(s2, s10); SORT_PAIR(s3, s11);
	SORT_PAIR(s4, s12); SORT_PAIR(s5, s13); SORT_PAIR(s6, s14); SORT_PAIR(s7, s15);
	SORT_PAIR(s4, s8); SORT_PAIR(s5, s9); SORT_PAIR(s6, s10); SORT_PAIR(s7, s11);
	SORT_PAIR(s2, s4); SORT_PAIR(s3, s5); SORT_PAIR(s6, s8); SORT_PAIR(s7, s9);
	SORT_PAIR(s10, s12); SORT_PAIR(s11, s13);
	SORT_PAIR(s1, s2); SORT_PAIR(s3, s4); SORT_PAIR(s5, s6); SORT_PAIR(s7, s8);
	SORT_PAIR(s9, s10); SORT_PAIR(s11, s12); SORT_PAIR(s13, s14);
#undef SORT_PAIR
}

float SelectPercentileSample(int index, float s0, float s1, float s2, float s3,
	float s4, float s5, float s6, float s7,
	float s8, float s9, float s10, float s11,
	float s12, float s13, float s14, float s15)
{
	if (index <= 0) return s0;
	if (index == 1) return s1;
	if (index == 2) return s2;
	if (index == 3) return s3;
	if (index == 4) return s4;
	if (index == 5) return s5;
	if (index == 6) return s6;
	if (index == 7) return s7;
	if (index == 8) return s8;
	if (index == 9) return s9;
	if (index == 10) return s10;
	if (index == 11) return s11;
	if (index == 12) return s12;
	if (index == 13) return s13;
	if (index == 14) return s14;

	return s15;
}

float ReducePercentileChannel(int channel, int index)
{
	float s0 = GetPercentileChannelSample(vec2(-1.5, -1.5), channel);
	float s1 = GetPercentileChannelSample(vec2(-0.5, -1.5), channel);
	float s2 = GetPercentileChannelSample(vec2( 0.5, -1.5), channel);
	float s3 = GetPercentileChannelSample(vec2( 1.5, -1.5), channel);
	float s4 = GetPercentileChannelSample(vec2(-1.5, -0.5), channel);
	float s5 = GetPercentileChannelSample(vec2(-0.5, -0.5), channel);
	float s6 = GetPercentileChannelSample(vec2( 0.5, -0.5), channel);
	float s7 = GetPercentileChannelSample(vec2( 1.5, -0.5), channel);
	float s8 = GetPercentileChannelSample(vec2(-1.5,  0.5), channel);
	float s9 = GetPercentileChannelSample(vec2(-0.5,  0.5), channel);
	float s10 = GetPercentileChannelSample(vec2( 0.5,  0.5), channel);
	float s11 = GetPercentileChannelSample(vec2( 1.5,  0.5), channel);
	float s12 = GetPercentileChannelSample(vec2(-1.5,  1.5), channel);
	float s13 = GetPercentileChannelSample(vec2(-0.5,  1.5), channel);
	float s14 = GetPercentileChannelSample(vec2( 0.5,  1.5), channel);
	float s15 = GetPercentileChannelSample(vec2( 1.5,  1.5), channel);

	SortPercentileSamples(s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, s12, s13, s14, s15);

	return SelectPercentileSample(index, s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, s12, s13, s14, s15);
}

vec3 GetHistogramPercentileValues()
{
	return vec3(
		ReducePercentileChannel(0, 1),
		ReducePercentileChannel(1, 8),
		ReducePercentileChannel(2, 14));
}
#endif

void main()
{
#ifdef HISTOGRAM_PERCENTILE
	gl_FragColor = vec4(GetHistogramPercentileValues(), 1.0);
#else
	vec3 current = vec3(1.0, 0.0, 0.0);

#ifdef FIRST_PASS
	current = GetValues(vec2( 0.0,  0.0), current);
#else
	current = GetValues(vec2(-1.5, -1.5), current);
	current = GetValues(vec2(-0.5, -1.5), current);
	current = GetValues(vec2( 0.5, -1.5), current);
	current = GetValues(vec2( 1.5, -1.5), current);
	
	current = GetValues(vec2(-1.5, -0.5), current);
	current = GetValues(vec2(-0.5, -0.5), current);
	current = GetValues(vec2( 0.5, -0.5), current);
	current = GetValues(vec2( 1.5, -0.5), current);
	
	current = GetValues(vec2(-1.5,  0.5), current);
	current = GetValues(vec2(-0.5,  0.5), current);
	current = GetValues(vec2( 0.5,  0.5), current);
	current = GetValues(vec2( 1.5,  0.5), current);

	current = GetValues(vec2(-1.5,  1.5), current);
	current = GetValues(vec2(-0.5,  1.5), current);
	current = GetValues(vec2( 0.5,  1.5), current);
	current = GetValues(vec2( 1.5,  1.5), current);

	current.y *= 0.0625;
#endif

	gl_FragColor = vec4(current, 1.0);
#endif
}
