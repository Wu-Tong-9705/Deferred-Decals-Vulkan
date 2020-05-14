#version 450

layout( constant_id = 0 ) const int MAX_CLUSTER_NUM = 64;
layout( constant_id = 1 ) const float NEAR_CLIP = 0.1;
layout( constant_id = 2 ) const float FAR_CLIP = 35.0;
layout( constant_id = 3 ) const uint NUM_X_TILES = 64;
layout( constant_id = 4 ) const uint NUM_Y_TILES = 64;
layout( constant_id = 5 ) const uint NUM_Z_TILES = 16;
layout( constant_id = 6 ) const uint ELEMENTS_PER_CLUSTER = 2;
layout( constant_id = 7 ) const uint TILE_SIZE = 16;
layout( constant_id = 8 ) const uint MODE = 0;

//const int MAX_CLUSTER_NUM = 64;
//const float NEAR_CLIP = 0.1;
//const float FAR_CLIP = 35.0;
//const uint NUM_X_TILES = 64;
//const uint NUM_Y_TILES = 64;
//const uint NUM_Z_TILES = 16;
//const uint ELEMENTS_PER_CLUSTER = 2;
//const uint MODE = 0;

layout(set = 2, binding = 1) buffer BoundUniform
{
	uvec2 zBounds[MAX_CLUSTER_NUM];
}boundUniform;

layout(set = 3, binding = 0) buffer Cluster
{
	uint data[ELEMENTS_PER_CLUSTER * NUM_X_TILES * NUM_Y_TILES * NUM_Z_TILES];
}cluster;

layout(location = 0) flat in uint inDecalIndex;

void main()
{
	float zw = gl_FragCoord.z;
	float zwDX = dFdx(zw);
	float zwDY = dFdy(zw);
	float tileMinZW = zw - abs(0.5f * zwDX) - abs(0.5f * zwDY);
	float tileMaxZW = zw + abs(0.5f * zwDX) + abs(0.5f * zwDY);

	float invClipRange = 1.0f / (FAR_CLIP - NEAR_CLIP);
	float proj33 = -FAR_CLIP * invClipRange;
	float proj43 = -NEAR_CLIP * FAR_CLIP * invClipRange;
	float tileMinDepth = proj43 / (-tileMinZW - proj33);
	float tileMaxDepth = proj43 / (-tileMaxZW - proj33);
	tileMinDepth = clamp((-tileMinDepth - NEAR_CLIP) * invClipRange, 0.0f, 1.0f);
	tileMaxDepth = clamp((-tileMaxDepth - NEAR_CLIP) * invClipRange, 0.0f, 1.0f);
	uint minZTile = uint(tileMinDepth * NUM_Z_TILES);
	uint maxZTile = uint(tileMaxDepth * NUM_Z_TILES);

	uint zTileStart = 0;
	uint zTileEnd = 0;
	uvec2 zTileRange = boundUniform.zBounds[inDecalIndex];

	switch(MODE)
	{
	case 0://intersect
		zTileStart = 0;
		zTileEnd = min(maxZTile, zTileRange.y);
		break;
	case 1://back0
		zTileStart = max(minZTile, zTileRange.x);
		zTileEnd = min(maxZTile, zTileRange.y);
		break;
	case 2://front
		zTileStart = max(minZTile, zTileRange.x);
		zTileEnd = zTileRange.y;
		break;
	};

	uint elemIdx = inDecalIndex / 32;
	uint mask = 1 << (inDecalIndex % 32);
	uvec2 tilePosXY = uvec2(gl_FragCoord.xy / TILE_SIZE);

	for(uint zTile = zTileStart; zTile <= zTileEnd; zTile++)
	{
		uvec3 tileCoords = uvec3(tilePosXY, zTile);
		uint clusterIndex = (tileCoords.z * NUM_X_TILES * NUM_Y_TILES) + (tileCoords.y * NUM_X_TILES) + tileCoords.x;
		uint address = clusterIndex * ELEMENTS_PER_CLUSTER + elemIdx;
		if(MODE == 2 && (cluster.data[address] & mask) != 0)break;
		atomicOr(cluster.data[address], mask);
	}
}