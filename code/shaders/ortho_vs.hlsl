
float4x4 projectionMatrix;
float4x4 viewMatrix;
float4x4 worldMatrix;
uniform float2 texel_offset : register(c0);

struct VS_INPUT {
	float4 position : POSITION;
	float4 color : COLOR;
	float2 texcoord : TEXCOORD0;
};

struct VS_OUTPUT {
	float4 position : POSITION;
	float4 color : COLOR;
	float2 texcoord : TEXCOORD0;
};

VS_OUTPUT main(VS_INPUT input) {
	VS_OUTPUT output;

	float4 position = input.position;
	position = mul(position, worldMatrix);
	position = mul(position, viewMatrix);
	position = mul(position, projectionMatrix);

	position.xy += texel_offset * position.ww;
	output.position = position;
	
	output.color = input.color;

	output.texcoord = input.texcoord;
	
	return output;
}