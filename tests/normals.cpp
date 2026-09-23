

#include <stdlib.h>
#include <search.h>
#include <stdint.h>
#include <math.h>
#include <iostream>
#include <string>
#include <fstream>

#define MAX_VERTEXES 1000
#define MAX_INDEXES (6*MAX_VERTEXES)

typedef float vec_t;
typedef float vec3_t[3];
typedef float vec4_t[4];
typedef unsigned int uint;

#define DotProduct(x,y)			(x[0]*y[0]+x[1]*y[1]+x[2]*y[2])
#define VectorSubtract(a,b,c)	(c[0]=a[0]-b[0],c[1]=a[1]-b[1],c[2]=a[2]-b[2])
#define VectorAdd(a,b,c)		(c[0]=a[0]+b[0],c[1]=a[1]+b[1],c[2]=a[2]+b[2])
#define VectorCopy(a,b)			(b[0]=a[0],b[1]=a[1],b[2]=a[2])
#define VectorScale(v,s,o)      ((o)[0]=(v)[0]*(s),(o)[1]=(v)[1]*(s),(o)[2]=(v)[2]*(s))
#define VectorClear(a)			(a[0]=a[1]=a[2]=0)
#define VectorNegate(a,b)		(b[0]=-a[0],b[1]=-a[1],b[2]=-a[2])
#define VectorSet(v, x, y, z)	(v[0]=(x), v[1]=(y), v[2]=(z))
#define VectorMA( v, s, b, o )  ( ( o )[0] = ( v )[0] + ( b )[0] * ( s ),( o )[1] = ( v )[1] + ( b )[1] * ( s ),( o )[2] = ( v )[2] + ( b )[2] * ( s ) )

inline void CrossProduct(const vec3_t v1, const vec3_t v2, vec3_t cross) {
	cross[0] = v1[1] * v2[2] - v1[2] * v2[1];
	cross[1] = v1[2] * v2[0] - v1[0] * v2[2];
	cross[2] = v1[0] * v2[1] - v1[1] * v2[0];
}

void VectorNormalize(vec3_t v)
{
	float length, ilength;

	length = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];

	if (length)
	{
#if 1
		_mm_store_ss(&ilength, _mm_rsqrt_ss(_mm_set_ss(length)));
#else
		ilength = 1 / sqrtf(length);
#endif
		v[0] *= ilength;
		v[1] *= ilength;
		v[2] *= ilength;
	}
}

struct vertexData_s
{
	float xyz[3];
	float normal[3];
	float uv[2];
};

struct drawbuff_s
{
	int numVertexes;
	int numIndexes;
	struct vertexData_s vertexes[MAX_VERTEXES];
	unsigned short indexes[MAX_INDEXES];
} g_drawBuff;

struct indexes_s
{
	uint16_t idx;
	uint16_t pos;
	uint16_t cor;
} indexes[1000];

uint16_t left[1000];
uint16_t idxrepl[1000];

static int vcomp = 0;

static inline int vertexes_compare2(const struct vertexData_s* a, const struct vertexData_s* b)
{
	const float epsilon = 1e-9f;

	vcomp++;

	const float dx = a->xyz[0] - b->xyz[0];
	if (fabsf(dx) < epsilon)
	{
		const float dy = a->xyz[1] - b->xyz[1];
		if (fabsf(dy) < epsilon)
		{
			const float dz = a->xyz[2] - b->xyz[2];
			if (fabsf(dz) < epsilon)
			{
				return 0;
			}
			else return (signbit(dz) ? -1 : 1);
		}
		else return (signbit(dy) ? -1 : 1);
	}
	else return (signbit(dx) ? -1 : 1);
}

static int ncomp = 0;

static inline int normals_compare2(const struct vertexData_s* a, const struct vertexData_s* b)
{
	const float epsilon = 1e-9f;

	ncomp++;

	const float dx = a->normal[0] - b->normal[0];
	if (fabsf(dx) < epsilon)
	{
		const float dy = a->normal[1] - b->normal[1];
		if (fabsf(dy) < epsilon)
		{
			const float dz = a->normal[2] - b->normal[2];
			if (fabsf(dz) < epsilon)
			{
				return 0;
			}
			else return (signbit(dz) ? -1 : 1);
		}
		else return (signbit(dy) ? -1 : 1);
	}
	else return (signbit(dx) ? -1 : 1);
}

static int vertexes_compare(void const* x0, void const* x1)
{
	const struct vertexData_s* a = &g_drawBuff.vertexes[*((uint16_t*)x0)];
	const struct vertexData_s* b = &g_drawBuff.vertexes[*((uint16_t*)x1)];
	int res = vertexes_compare2(a, b);

	if (res == 0)
	{
		if (a < b)
			return -1;
		return 1;
	}
	return res;
}

static int normals_compare(void const* x0, void const* x1)
{
	const struct vertexData_s* a = &g_drawBuff.vertexes[*((uint16_t*)x0)];
	const struct vertexData_s* b = &g_drawBuff.vertexes[*((uint16_t*)x1)];
	int res = normals_compare2(a, b);

	if (res == 0)
	{
		if (a < b)
			return -1;
		return 1;
	}
	return res;
}

inline int IsVectorZero(const vec3_t v)
{
	return (v[0] == 0.f) && (v[1] == 0.f) && (v[2] == 0.f);
}

inline void MergeNormal(const vec3_t normal, uint index, const uint16_t* remapvpos)
{
	float* ndst, dot;
	int vzero;
	if (remapvpos)
		index = remapvpos[index];
	ndst = g_drawBuff.vertexes[index].normal;

	VectorAdd(normal, ndst, ndst);
	VectorNormalize(ndst);
}

static uint16_t sortvpos[MAX_VERTEXES];
static uint16_t remapvpos[MAX_VERTEXES];
static uint16_t dupvpos[2 * MAX_VERTEXES];

void calculate_normals()
{

	for (int i = 0; i < g_drawBuff.numVertexes; i++)
	{
		sortvpos[i] = i;
		remapvpos[i] = i;
	}

	if (1)
	{
		qsort(sortvpos, g_drawBuff.numVertexes, sizeof(uint16_t), vertexes_compare);

		uint16_t* it = dupvpos;
		int count = 0;
		int backi = sortvpos[0];
		const struct vertexData_s* backv = &g_drawBuff.vertexes[backi];
		for (int i = 1; i < g_drawBuff.numVertexes; i++)
		{
			int fronti = sortvpos[i];
			const struct vertexData_s* frontv = &g_drawBuff.vertexes[fronti];
			if (0 == vertexes_compare2(backv, frontv))
			{
				remapvpos[fronti] = backi;
				if (count == 0)
				{
					count++;
					it[count] = backi;
				}
				count++;
				it[count] = fronti;
			}
			else
			{
				backv = frontv;
				backi = fronti;
				if (count)
				{
					it[0] = count;
					it += count + 1;
					count = 0;
				}
			}
		}
		it[0] = count;
		it[count + 1] = 0;
	}

	int comb = g_drawBuff.numVertexes;
	comb = comb * (comb - 1) / 2;
	printf("Vertex Comparisons %d vs %d for %d\n", vcomp, comb, g_drawBuff.numVertexes);

	struct vertexData_s* v = g_drawBuff.vertexes;
	for (int i = 0; i < g_drawBuff.numVertexes; i++, v++)
	{
		VectorClear(v->normal);
	}

	for (int i = 0; i < g_drawBuff.numIndexes; i += 3)
	{
		uint i0 = g_drawBuff.indexes[i];
		uint i1 = g_drawBuff.indexes[i + 1];
		uint i2 = g_drawBuff.indexes[i + 2];

		const float* v0 = g_drawBuff.vertexes[i0].xyz;
		const float* v1 = g_drawBuff.vertexes[i1].xyz;
		const float* v2 = g_drawBuff.vertexes[i2].xyz;

		vec3_t e1, e2, normal;
		VectorSubtract(v0, v1, e1);
		VectorSubtract(v2, v1, e2);
		CrossProduct(e1, e2, normal);
		VectorNormalize(normal);

		const uint16_t* param = 1 ? NULL : remapvpos;

		MergeNormal(normal, i0, param);
		MergeNormal(normal, i1, param);
		MergeNormal(normal, i2, param);
	}

	uint16_t* it = dupvpos;
	while (it[0])
	{
		int count = it[0];
		it++;

		ncomp = 0;

		uint32_t visited = 0;
		uint32_t visitend = (1 << count) - 1;
		uint32_t skipadd = 0;
		for (int i = 0; i < count; i++)
		{
			const struct vertexData_s* backv = &g_drawBuff.vertexes[i];
			for (int j = i + 1; j < count; j++)
			{
				const struct vertexData_s* frontv = &g_drawBuff.vertexes[j];
				if (0 == normals_compare2(backv, frontv))
				{
					skipadd |= 1 << j;
				}
			}
		}
		printf("ncomp %d vs %d for %d\n", ncomp, count * (count - 1) / 2, count);
		for (int i = 0; i < count && visited != visitend; i++)
		{
			vec3_t sum;
			uint32_t visitnow = 0;
			int indexi = it[i];
			visited |= 1 << i;
			float* ni = g_drawBuff.vertexes[indexi].normal;
			VectorCopy(ni, sum);
			for (int j = i + 1; j < count; j++)
			{
				uint32_t visitid = 1 << j;
				if (visitid & visited)
					continue;

				int indexj = it[j];
				//if (indexj == 109 || indexj == 107)
				//{
				//	__debugbreak();
				//}
				float* nj = g_drawBuff.vertexes[indexj].normal;
				float dot = DotProduct(nj, ni);
				if (dot > 0.01f)
				{
					if (0 == (skipadd & visitid))
						VectorAdd(nj, sum, sum);
					remapvpos[indexj] = indexi;
					visitnow |= visitid;
				}
				else if (remapvpos[indexj] == indexi)
				{
					remapvpos[indexj] = indexj;
				}
			}

			VectorNormalize(sum);
			VectorCopy(sum, ni);
			for (int j = i + 1; j < count; j++)
			{
				uint32_t visitid = 1 << j;
				if (visitid & visitnow)
				{
					int indexj = it[j];
					float* nj = g_drawBuff.vertexes[indexj].normal;
					VectorCopy(sum, nj);
				}
			}

			visited |= visitnow;
		}

		//for (int i = 0; i < count; i++)
		//{
		//	int index = it[i];
		//	if (remapvpos[index] != index)
		//	{
		//		float* ni = g_drawBuff.vertexes[index].normal;
		//		MergeNormal(ni, remapvpos[index], NULL);
		//	}
		//}

		//for (int i = 0; i < count; i++)
		//{
		//	int index = it[i];
		//	if (remapvpos[index] != index)
		//	{
		//		float* ni = g_drawBuff.vertexes[index].normal;
		//		float* nr = g_drawBuff.vertexes[remapvpos[index]].normal;
		//		VectorCopy(nr, ni);
		//	}
		//}

		it += count;
	}
}

const struct indexes_s* find_index(int pos)
{
	for (int i = 0; i < sizeof(indexes) / sizeof(indexes[0]); i++)
	{
		if (pos == indexes[i].pos)
			return &indexes[i];
	}

	static const struct indexes_s fail = { 0xFFFF, 0xFFFF, 0xFFFF };
	return &fail;
}

void do_normals_test()
{
	memset(&g_drawBuff, 0, sizeof(g_drawBuff));
	memset(left, 0, sizeof(left));
	memset(idxrepl, 0, sizeof(idxrepl));

	std::fstream vfile;
	int count = 0;
	int vcount = 0;
	int maxvindex = -1;

	//memset(g_drawBuff.vertexes, 0xFF, sizeof(g_drawBuff.vertexes));

	vec3_t sum;
	VectorClear(sum);
	vec3_t vup = { 0, 0, 1 };
	VectorAdd(vup, sum, sum);
	vec3_t vfirst = { 1, 0, 1 };
	VectorNormalize(vfirst);
	VectorAdd(vfirst, sum, sum);
	VectorNormalize(sum);
	VectorAdd(vup, sum, sum);
	VectorNormalize(sum);
	VectorAdd(vup, sum, sum);
	VectorNormalize(sum);


	vfile.open("./data/mesh_concrete_pillars.csv", std::ios::in);
	if (vfile.is_open()) {
		std::string sa;
		std::getline(vfile, sa);

		while (std::getline(vfile, sa) && count < MAX_INDEXES && vcount < MAX_VERTEXES) {
			struct vertexData_s vd;
			int index;
			sscanf_s(sa.c_str(), "%*d,%d,%f,%f,%f,%*f,\"%*[^\"]\",%f,%f", &index, &vd.xyz[0], &vd.xyz[1], &vd.xyz[2], &vd.uv[0], &vd.uv[1]);
			if (IsVectorZero(g_drawBuff.vertexes[index].xyz))
			{
				memcpy(&g_drawBuff.vertexes[index], &vd, sizeof(vd));
				vcount++;
				if (index > maxvindex)
					maxvindex = index;
			}
			else if (0 != vertexes_compare2(&g_drawBuff.vertexes[index], &vd))
			{
				__debugbreak();
			}
			g_drawBuff.indexes[count] = index;
			//indexes[count].idx = index;
			//indexes[count].pos = count;
			//indexes[count].cor = index;
			count++;
		}

		g_drawBuff.numIndexes = count;
		g_drawBuff.numVertexes = vcount;

		if (maxvindex + 1 != vcount)
		{
			__debugbreak();
		}

		// Close the file object.
		vfile.close();
	}

	for (int i = 0; i < g_drawBuff.numVertexes; i++)
	{
		if (IsVectorZero(g_drawBuff.vertexes[i].xyz))
		{
			__debugbreak();
		}
	}

	calculate_normals();

	printf("-----------------------------------------------\n");

	for (int i = 0; i < g_drawBuff.numVertexes; i++)
	{
		int index = sortvpos[i];
		struct vertexData_s* v = &g_drawBuff.vertexes[index];
		printf("%d: %f %f %f\n", index, v->xyz[0], v->xyz[1], v->xyz[2]);
	}

	printf("-----------------------------------------------\n");

	uint16_t* it = dupvpos;
	while (it[0])
	{
		int count = it[0];
		it++;

		printf("%d: ", count);
		for (int i = 0; i < count; i++)
		{
			printf("%d ", it[i]);
		}
		printf("\n");

		it += count;
	}

	//for (int i = 0; i < count; i++)
	//{
	//	const struct indexes_s* inn = &indexes[i];
	//	printf("%d,%d,%d   %f %f %f\n", inn->pos, inn->idx, inn->cor, vertexes[inn->idx].x, vertexes[inn->idx].y, vertexes[inn->idx].z);
	//}

	//printf("-----------------------------------------------\n");

	//for (int i = 0; i < count; i++)
	//{
	//	const struct indexes_s* inn = find_index(i);
	//	printf("%d,%d,%d,%d   %f %f %f\n", inn->pos, inn->idx, inn->cor, idxrepl[inn->idx], vertexes[inn->idx].x, vertexes[inn->idx].y, vertexes[inn->idx].z);
	//}

	//printf("-----------------------------------------------\n");

	//for (int i = 0; i < count; i++)
	//{
	//	printf("%d %d\n", i, left[i]);
	//}

	printf("\n");
}
