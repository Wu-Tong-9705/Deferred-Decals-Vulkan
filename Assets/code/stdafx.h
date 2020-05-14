#pragma once
//C++
#include <string>
#include <iostream>
#include <cmath>
#include <memory>
#include <io.h>
using namespace std;

//Anvil
#include "config.h"
#include "misc/window_factory.h"
#include "misc/io.h"
#include "misc/time.h"
#include "misc/memory_allocator.h"
#include "misc/object_tracker.h"
#include "misc/glsl_to_spirv.h"
#include "misc/buffer_create_info.h"
#include "misc/framebuffer_create_info.h"
#include "misc/graphics_pipeline_create_info.h"
#include "misc/compute_pipeline_create_info.h"
#include "misc/image_create_info.h"
#include "misc/image_view_create_info.h"
#include "misc/sampler_create_info.h"
#include "misc/instance_create_info.h"
#include "misc/render_pass_create_info.h"
#include "misc/rendering_surface_create_info.h"
#include "misc/semaphore_create_info.h"
#include "misc/swapchain_create_info.h"
#include "wrappers/buffer.h"
#include "wrappers/command_buffer.h"
#include "wrappers/command_pool.h"
#include "wrappers/descriptor_set_group.h"
#include "wrappers/descriptor_set_layout.h"
#include "wrappers/device.h"
#include "wrappers/event.h"
#include "wrappers/graphics_pipeline_manager.h"
#include "wrappers/compute_pipeline_manager.h"
#include "wrappers/framebuffer.h"
#include "wrappers/image.h"
#include "wrappers/image_view.h"
#include "wrappers/sampler.h"
#include "wrappers/instance.h"
#include "wrappers/physical_device.h"
#include "wrappers/query_pool.h"
#include "wrappers/rendering_surface.h"
#include "wrappers/render_pass.h"
#include "wrappers/semaphore.h"
#include "wrappers/shader_module.h"
#include "wrappers/swapchain.h"
using namespace Anvil;

//surport
#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_RADIANS//统一使用弧度
#define GLM_FORCE_DEPTH_ZERO_TO_ONE//使用vulkan深度范围(0,1)，默认使用的是OpenGL范围(-1,1)。
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES//强制默认对齐
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/hash.hpp"
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"
using namespace glm;

//struct
struct Vertex
{
    alignas(16) vec3 pos;
    alignas(16) vec3 normal;
    alignas(16) vec2 texCoord;
    alignas(16) vec3 tangent;
    alignas(16) vec3 bitangent;

    //属性描述：顶点着色器如何从顶点数据中提取顶点属性
    static array<VertexInputAttribute, 5> getVertexInputAttribute()
    {
        array<VertexInputAttribute, 5> vertexInputAttributes = {};

        vertexInputAttributes[0] = VertexInputAttribute(
            0, /* in_location */
            Format::R32G32B32_SFLOAT,
            offsetof(Vertex, pos));

        vertexInputAttributes[1] = VertexInputAttribute(
            1, /* in_location */
            Format::R32G32B32_SFLOAT,
            offsetof(Vertex, normal));

        vertexInputAttributes[2] = VertexInputAttribute(
            2, /* in_location */
            Format::R32G32_SFLOAT,
            offsetof(Vertex, texCoord));

        vertexInputAttributes[3] = VertexInputAttribute(
            3, /* in_location */
            Format::R32G32B32_SFLOAT,
            offsetof(Vertex, tangent));

        vertexInputAttributes[4] = VertexInputAttribute(
            4, /* in_location */
            Format::R32G32B32_SFLOAT,
            offsetof(Vertex, bitangent));

        return vertexInputAttributes;
    }

    //重载赋值运算符
    bool operator==(const Vertex& other) const
    {
        return
            pos == other.pos &&
            normal == other.normal &&
            texCoord == other.texCoord &&
            tangent == other.tangent &&
            bitangent == other.bitangent;
    }
};
struct VertexOnlyPos
{
    vec3 pos;

    //属性描述：顶点着色器如何从顶点数据中提取顶点属性
    static array<VertexInputAttribute, 1> getVertexInputAttribute()
    {
        array<VertexInputAttribute, 1> vertexInputAttributes = {};

        vertexInputAttributes[0] = VertexInputAttribute(
            0, /* in_location */
            Format::R32G32B32_SFLOAT,
            offsetof(Vertex, pos));

        return vertexInputAttributes;
    }
};
struct TextureIndicesUniform
{
    uint32_t albedo;
    uint32_t normal;
    uint32_t roughness;
    uint32_t metallic;
};
struct CursorDecal
{
    alignas(16) vec3 size;
    alignas(4) float rotation;
    alignas(4) float angle_fade;
    alignas(4) float intensity;
    alignas(4) float albedo;
    alignas(4) uint32_t albedoTexIdx;
    alignas(4) uint32_t normalTexIdx;
};
struct Decal
{
    alignas(16) vec3 position;
    alignas(16) vec3 normal;
    alignas(16) vec3 size;
    alignas(4) float rotation;
    alignas(4) float angle_fade;
    alignas(4) float intensity;
    alignas(4) float albedo;
    alignas(4) uint32_t albedoTexIdx;
    alignas(4) uint32_t normalTexIdx;

    Decal(vec3 position, vec3 normal, CursorDecal& cursorDecal)
    {
        this->position = position;
        this->normal = normal;
        size = cursorDecal.size;
        rotation = cursorDecal.rotation;
        angle_fade = cursorDecal.angle_fade;
        intensity = cursorDecal.intensity;
        albedo = cursorDecal.albedo;
        albedoTexIdx = cursorDecal.albedoTexIdx;
        normalTexIdx = cursorDecal.normalTexIdx;
    }

    Decal() = default;
};
struct BoundingOrientedBox
{
    vec3 Center;            // Center of the box.
    vec3 Extents;           // Distance from the center to each side.
    mat3 Orientation;       // rotation matrix(box -> world).
};

//core
#define N_SWAPCHAIN_IMAGES (3)
#define N_MAX_STORED_DECALS (64)
#define NUM_Z_TILES (16)
#define Tile_Size (16)
#include "core/engine.h"
