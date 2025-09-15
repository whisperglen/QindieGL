
sampler2D tex : s0;

struct PS_INPUT {
	float4 position : POSITION;
	float4 color : COLOR;
	float2 texcoord : TEXCOORD0;
};

float4 main(PS_INPUT input) : COLOR
{
	return tex2D(tex, input.texcoord.xy);
}
