uniform sampler2D u_DiffuseMap;

uniform int       u_AlphaTest;
uniform vec4      u_TextureScale;

#if defined(USE_DEPTH_FADE)
uniform sampler2D u_ScreenDepthMap;
uniform vec2      u_InvTexRes;
uniform vec4      u_DepthFadeInfo;
uniform vec4      u_DepthFadeScale;
uniform vec4      u_DepthFadeBias;
#endif

varying vec2      var_DiffuseTex;

varying vec4      var_Color;
#if defined(USE_RGBAGEN)
uniform int       u_ColorGen;
uniform vec3      u_AmbientLight;
uniform vec3      u_DirectedLight;
uniform vec3      u_ModelLightDir;
uniform vec4      u_CelShadeInfo;

varying vec3      var_Position;
varying vec3      var_Normal;

float QuantizeCelLighting( float intensity )
{
	float bands = max( u_CelShadeInfo.y, 1.0 );

	if ( u_CelShadeInfo.x <= 0.0 || bands <= 1.0 ) {
		return intensity;
	}

	intensity = clamp( intensity, 0.0, 1.0 );

	{
		float scaled = intensity * bands;
		float baseBand = floor( scaled );
		float denom = max( bands - 1.0, 1.0 );
		float bandValue = min( baseBand, bands - 1.0 ) / denom;
		float nextBandValue = min( baseBand + 1.0, bands - 1.0 ) / denom;
		float edge = fract( scaled );
		float width = max( fwidth( scaled ), 0.0001 );
		float blend = smoothstep( 1.0 - width, 1.0 + width, edge );

		return mix( bandValue, nextBandValue, blend );
	}
}
#endif

#if defined(USE_DEPTH_FADE)
float DepthFadeLinearDepth(float depth)
{
	return u_DepthFadeInfo.y / mix(u_DepthFadeInfo.x, 1.0, depth);
}

vec4 ApplyDepthFade(vec4 color)
{
	if (u_DepthFadeInfo.z <= 0.0)
	{
		return color;
	}

	float sceneDepth = texture2D(u_ScreenDepthMap, gl_FragCoord.xy * u_InvTexRes).r;
	float sceneLinear = DepthFadeLinearDepth(sceneDepth);
	float fragLinear = DepthFadeLinearDepth(gl_FragCoord.z);
	float fade = clamp((sceneLinear - fragLinear + u_DepthFadeInfo.w) * u_DepthFadeInfo.z, 0.0, 1.0);

	fade = fade * fade * (3.0 - 2.0 * fade);
	return mix(color * u_DepthFadeScale + u_DepthFadeBias, color, fade);
}
#endif

void main()
{
	vec4 color  = texture2D(u_DiffuseMap, var_DiffuseTex);
	color.rgb *= u_TextureScale.x;

	float alpha = color.a * var_Color.a;
	if (u_AlphaTest == 1)
	{
		if (alpha == 0.0)
			discard;
	}
	else if (u_AlphaTest == 2)
	{
		if (alpha >= 0.5)
			discard;
	}
	else if (u_AlphaTest == 3)
	{
		if (alpha < 0.5)
			discard;
	}
	
	{
		vec3 shadeColor = var_Color.rgb;

#if defined(USE_RGBAGEN)
		if ( u_ColorGen == CGEN_LIGHTING_DIFFUSE && u_CelShadeInfo.x > 0.0 ) {
			float incoming = clamp( dot( normalize( var_Normal ), normalize( u_ModelLightDir ) ), 0.0, 1.0 );

			incoming = QuantizeCelLighting( incoming );
			shadeColor = clamp( u_DirectedLight * incoming + u_AmbientLight, 0.0, 1.0 );
		}
#endif

		gl_FragColor.rgb = color.rgb * shadeColor;
	}
	gl_FragColor.a = alpha;

#if defined(USE_DEPTH_FADE)
	gl_FragColor = ApplyDepthFade(gl_FragColor);
#endif
}
