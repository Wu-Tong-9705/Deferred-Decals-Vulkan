#pragma once
#include "stdafx.h"
#include "../scene/camera.h"
#include "../support/input.h"
#include "../scene/model.h"
#include "support/dynamicBufferHelper.h"
#include "decalSettings.h"

#pragma region struct
struct DeferredConstants
{
    vec2 RTSize;
};

struct MVPUniform
{
    alignas(16) mat4 model;
    alignas(16) mat4 view;
    alignas(16) mat4 proj;
};

struct IndexUniform
{
    uint decalIndices[N_MAX_STORED_DECALS];
    uint numIntersectingDecals;
};

struct ZBoundsUniform
{
    uvec2 ZBounds[N_MAX_STORED_DECALS];
};

struct ClusterStorage
{
    uint data[N_MAX_STORED_DECALS / 32 * ((1920 + Tile_Size - 1) / Tile_Size) * ((1080 + Tile_Size - 1) / Tile_Size) * NUM_Z_TILES];
};

struct SunLightUniform
{
    vec3 SunDirectionWS;
    vec3 SunIrradiance;
};

struct CameraUniform
{
    vec3 CameraPosWS;
};

struct PickingStorage
{
    vec3 Position;
    vec3 Normal;
};
#pragma endregion


class Engine
{
public:
    //单例模式，懒汉，线程不安全
    static unique_ptr<Engine>& Instance();

    //单例模式：删除外部的构造函数
    Engine(Engine const& r) = delete;
    Engine(Engine const&& r) = delete;
    Engine& operator=(Engine const& r) = delete;

    void run ();

    BaseDevice* getDevice();
    PipelineLayout* getPineLine(int id = 0);
    Sampler* getSampler();
    vector<DescriptorSet::CombinedImageSamplerBindingElement>* getTextureCombinedImageSamplersBinding();
    float getAspect();

    ~Engine();
private:
    #pragma region init
    Engine ();
    void init();
    void init_vulkan();
    void init_window        ();
    void init_swapchain     ();

    void init_buffers       ();
    void init_image         ();
    void init_sampler       ();
    void init_dsgs          ();

    void init_render_pass    ();
    void init_shaders        ();
    void init_gfx_pipelines  ();
    void init_compute_pipelines();

    void init_framebuffers   ();
    void init_command_buffers();

    void init_semaphores     ();

    void update_data(uint32_t in_n_swapchain_image);
    void update_decal        ();
    void draw_frame          ();

    void recreate_swapchain();

    void cleanup_swapwhain   ();
    void deinit              ();
    #pragma endregion

    #pragma region tools
    ShaderModuleStageEntryPoint* create_shader (string file, ShaderStage type, string name);
    void create_image_source(ImageUniquePtr& image, ImageViewUniquePtr&image_view, string name, Format format, bool isDepthImage = false);
    void create_cluster_pipeline(GraphicsPipelineManager* gfxPipelineManager, uint mode);
    void cluster(PrimaryCommandBuffer* cmd_buffer_ptr, uint mode, uint n_command_buffer);
    void make_box(float scale);
    Format SelectSupportedFormat(
        const vector<Format>& candidates,
        ImageTiling tiling,
        FormatFeatureFlags features);
    bool Intersects(const BoundingOrientedBox& boxA, const BoundingOrientedBox& boxB);
    #pragma endregion

    #pragma region callback
    void on_validation_callback(Anvil::DebugMessageSeverityFlags in_severity, const char* in_message_ptr);
    void mouse_move_callback(CallbackArgument* argumentPtr);
    void mouse_click_LButton_callback(CallbackArgument* argumentPtr);
    void mouse_click_RButton_callback(CallbackArgument* argumentPtr);
    void scroll_callback(CallbackArgument* argumentPtr);
    void key_press_callback(CallbackArgument* argumentPtr);
    void key_release_callback(CallbackArgument* argumentPtr);
    #pragma endregion





    #pragma region vulkan_resource
    BaseDeviceUniquePtr       m_device_ptr;
    InstanceUniquePtr         m_instance_ptr;
    const PhysicalDevice*     m_physical_device_ptr;
    Queue*                    m_present_queue_ptr;
    RenderingSurfaceUniquePtr m_rendering_surface_ptr;
    SwapchainUniquePtr        m_swapchain_ptr;
    WindowUniquePtr           m_window_ptr;
    DescriptorSetGroupUniquePtr                  m_dsg_ptr;
    FramebufferUniquePtr                         m_fbo;
    PrimaryCommandBufferUniquePtr                m_command_buffers[N_SWAPCHAIN_IMAGES];

    uint32_t       m_n_last_semaphore_used;
    vector<SemaphoreUniquePtr> m_frame_signal_semaphores;
    vector<SemaphoreUniquePtr> m_frame_wait_semaphores;
    #pragma endregion

    #pragma region custom
    shared_ptr<Model>         m_model;
    shared_ptr<Camera>        m_camera;
    shared_ptr<Key>           m_key;
    shared_ptr<Mouse>         m_mouse;
    DecalSettings             m_decal_settings;
    #pragma endregion

    #pragma region image
    vector<DescriptorSet::CombinedImageSamplerBindingElement>   m_texture_combined_image_samplers_binding;
    vector<DescriptorSet::StorageImageBindingElement>           m_color_image_ptr;
    ImageUniquePtr                                              m_depth_image_ptr;
    ImageViewUniquePtr                                          m_depth_image_view_ptr;
    ImageUniquePtr                                              m_depth_image2_ptr;
    ImageViewUniquePtr                                          m_depth_image_view2_ptr;
    ImageUniquePtr                                              m_tangent_frame_image_ptr;
    ImageViewUniquePtr                                          m_tangent_frame_image_view_ptr;
    ImageUniquePtr                                              m_uv_and_depth_gradient_image_ptr;
    ImageViewUniquePtr                                          m_uv_and_depth_gradient_image_view_ptr;
    ImageUniquePtr                                              m_uv_gradient_image_ptr;
    ImageViewUniquePtr                                          m_uv_gradient_image_view_ptr;
    ImageUniquePtr                                              m_material_id_image_ptr;
    ImageViewUniquePtr                                          m_material_id_image_view_ptr;
    SamplerUniquePtr                                            m_sampler;
    #pragma endregion

    #pragma region buffer
    BufferUniquePtr                         m_texture_indices_uniform_buffer_ptr;

    Decal                                   m_decals[N_MAX_STORED_DECALS];
    BufferUniquePtr                         m_decals_uniform_buffer_ptr;
    VkDeviceSize                            m_decals_buffer_size;
    BufferUniquePtr                         m_box_vertex_buffer_ptr;
    BufferUniquePtr                         m_box_index_buffer_ptr;

    BufferUniquePtr                         m_picking_storage_buffer_ptr;
    VkDeviceSize                            m_picking_buffer_size;

    BufferUniquePtr                         m_cluster_storage_buffer_ptr;
    VkDeviceSize                            m_cluster_buffer_size;

    DynamicBufferHelper<MVPUniform>*        m_mvp_dynamic_buffer_helper;
    DynamicBufferHelper<SunLightUniform>*   m_sunLight_dynamic_buffer_helper;
    DynamicBufferHelper<CameraUniform>*     m_camera_dynamic_buffer_helper;
    DynamicBufferHelper<DecalSettingUniform>*       m_cursor_decal_dynamic_buffer_helper;
    IndexUniform                            m_indexUniform;
    DynamicBufferHelper<IndexUniform>*      m_decal_indices_dynamic_buffer_helper;
    ZBoundsUniform                          m_zBoundsUniform;
    DynamicBufferHelper<ZBoundsUniform>*    m_decal_ZBounds_dynamic_buffer_helper;
    #pragma endregion

    #pragma region shader
    unique_ptr<ShaderModuleStageEntryPoint>      m_GBuffer_vs_ptr;
    unique_ptr<ShaderModuleStageEntryPoint>      m_GBuffer_fs_ptr;
    unique_ptr<ShaderModuleStageEntryPoint>      m_picking_cs_ptr;
    unique_ptr<ShaderModuleStageEntryPoint>      m_deferred_cs_ptr;
    unique_ptr<ShaderModuleStageEntryPoint>      m_cluster_vs_ptr;
    unique_ptr<ShaderModuleStageEntryPoint>      m_cluster_fs_ptr;
    #pragma endregion

    #pragma region pipeline
    RenderPassUniquePtr                          m_renderpass_ptr;
    SubPassID                                    m_render_pass_subpass_GBuffer_id;
    SubPassID                                    m_render_pass_subpass_cluster_id[3];
    PipelineID                                   m_cluster_gfx_pipeline_id[3];
    PipelineID                                   m_GBuffer_gfx_pipeline_id;
    PipelineID                                   m_picking_compute_pipeline_id;
    PipelineID                                   m_deferred_compute_pipeline_id;
    #pragma endregion

    #pragma region other
    int m_width;
    int m_height;
    bool m_is_full_screen;
    RECT m_rect_before_full_screen;
    Format m_depth_format;
    int m_n_decal;
    DeferredConstants m_deferred_constants;
    int m_num_x_tiles;
    int m_num_y_tiles;
    #pragma endregion
};
