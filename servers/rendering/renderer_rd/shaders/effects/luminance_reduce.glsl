#[compute]

#version 450

#VERSION_DEFINES

#define BLOCK_SIZE 8

layout(local_size_x = BLOCK_SIZE, local_size_y = BLOCK_SIZE, local_size_z = 1) in;

shared float avg_data[BLOCK_SIZE * BLOCK_SIZE];
shared float min_data[BLOCK_SIZE * BLOCK_SIZE];
shared float max_data[BLOCK_SIZE * BLOCK_SIZE];

#ifdef READ_TEXTURE

//use for main texture
layout(set = 0, binding = 0) uniform sampler2D source_texture;

#else

//use for intermediate textures
layout(r32f, set = 0, binding = 0) uniform restrict readonly image2DArray source_luminance;

#endif

layout(r32f, set = 1, binding = 0) uniform restrict writeonly image2DArray dest_luminance;

#ifdef WRITE_LUMINANCE
layout(set = 2, binding = 0) uniform sampler2DArray prev_luminance;
#endif

layout(push_constant, std430) uniform Params {
	ivec2 source_size;
	float max_luminance;
	float min_luminance;
	float exposure_adjust;
	float pad[3];
}
params;

void main() {
	uint t = gl_LocalInvocationID.y * BLOCK_SIZE + gl_LocalInvocationID.x;
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);

	if (any(lessThan(pos, params.source_size))) {
#ifdef READ_TEXTURE
		vec3 v = texelFetch(source_texture, pos, 0).rgb;
		avg_data[t] = max(v.r, max(v.g, v.b));
		min_data[t] = min(v.r, min(v.g, v.b));
		max_data[t] = max(v.r, max(v.g, v.b));
#else
		avg_data[t] = imageLoad(source_luminance, ivec3(pos, 0)).r;
		min_data[t] = imageLoad(source_luminance, ivec3(pos, 1)).r;
		max_data[t] = imageLoad(source_luminance, ivec3(pos, 2)).r;
#endif
	} else {
		avg_data[t] = 0.0;
		min_data[t] = 0.0;
		max_data[t] = 0.0;
	}

	groupMemoryBarrier();
	barrier();

	uint size = (BLOCK_SIZE * BLOCK_SIZE) >> 1;

	do {
		if (t < size) {
			avg_data[t] += avg_data[t + size];
			min_data[t] = min(min_data[t], min_data[t + size]);
			max_data[t] = max(max_data[t], max_data[t + size]);
		}
		groupMemoryBarrier();
		barrier();

		size >>= 1;
	} while (size >= 1);

	if (t == 0) {
		//compute rect size
		ivec2 rect_size = min(params.source_size - pos, ivec2(BLOCK_SIZE));
		float avg = avg_data[0] / float(rect_size.x * rect_size.y);
		//float avg = avg_data[0] / float(BLOCK_SIZE*BLOCK_SIZE);
		float min = min_data[0];
		float max = max_data[0];
		pos /= ivec2(BLOCK_SIZE);
#ifdef WRITE_LUMINANCE
		float prev_lum = texelFetch(prev_luminance, ivec3(0, 0, 0), 0).r; //1 pixel previous exposure
		avg = clamp(prev_lum + (avg - prev_lum) * params.exposure_adjust, params.min_luminance, params.max_luminance);
		float prev_min = texelFetch(prev_luminance, ivec3(0, 0, 1), 0).r;
		min = prev_min + (min - prev_min) * params.exposure_adjust;
		float prev_max = texelFetch(prev_luminance, ivec3(0, 0, 2), 0).r;
		max = prev_max + (max - prev_max) * params.exposure_adjust;
#endif
		imageStore(dest_luminance, ivec3(pos, 0), vec4(avg));
		imageStore(dest_luminance, ivec3(pos, 1), vec4(min));
		imageStore(dest_luminance, ivec3(pos, 2), vec4(max));
	}
}
