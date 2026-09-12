
float4x4 projectionMatrix	: register(c0);
float4x4 viewMatrix			: register(c4);
float4x4 worldMatrix		: register(c8);
uniform float2 texel_offset : register(c12);

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

	// Swapping the order mathematically transposes the matrices during multiplication
	position = mul(worldMatrix, position);
	position = mul(viewMatrix, position);
	position = mul(projectionMatrix, position);

	position.xy += texel_offset * position.ww;
	output.position = position;

	output.color = input.color;
	output.texcoord = input.texcoord;

	return output;
}