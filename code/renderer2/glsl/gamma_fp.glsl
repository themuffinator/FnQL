uniform sampler2D u_TextureMap;
uniform vec4      u_Color;

varying vec2      var_TexCoords;


void main()
{
	vec4 color = texture2D(u_TextureMap, var_TexCoords);

	color.rgb = clamp(color.rgb * u_Color.rgb, 0.0, 1.0);
	color.rgb = pow(color.rgb, vec3(u_Color.a));

	gl_FragColor = color;
}
