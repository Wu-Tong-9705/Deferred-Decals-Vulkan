#pragma once
#include "stdafx.h"
#include "../scene/camera.h"
#include "../support/keys.h"
#include "../scene/model.h"
#include "support/dynamicBufferHelper.h"
#include "appSettings.h"

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

struct SunLightUniform
{
    vec3 SunDirectionWS;
    vec3 SunIrradiance;
};

struct CameraUniform
{
    vec3 CameraPosWS;
    mat4 InvViewProj;
};

struct PickingStorage
{
    vec3 Position;
    vec3 Normal;
};

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
    Engine ();

    void init();
    void init_vulkan();

    void on_validation_callback(Anvil::DebugMessageSeverityFlags in_severity,
        const char* in_message_ptr);
    void mouse_callback(CallbackArgument* argumentPtr);
    void scroll_callback(CallbackArgument* argumentPtr);
    void key_press_callback(CallbackArgument* argumentPtr);
    void key_release_callback(CallbackArgument* argumentPtr);
    void recreate_swapchain();

    void init_window        ();
    void init_swapchain     ();

    void init_buffers       ();
    void create_image_source(ImageUniquePtr& image, ImageViewUniquePtr&image_view, string name, Format format, bool isDepthImage = false);
    void init_image         ();
    void init_sampler       ();
    void init_dsgs          ();

    void init_render_pass    ();
    ShaderModuleStageEntryPoint* create_shader (string file, ShaderStage type, string name);
    void init_shaders        ();
    void init_gfx_pipelines  ();
    void init_compute_pipelines();
    
    Format SelectSupportedFormat(
        const vector<Format>& candidates,
        ImageTiling tiling,
        FormatFeatureFlags features);

    void init_framebuffers   ();
    void init_command_buffers();

    void init_semaphores     ();
    void update_data(uint32_t in_n_swapchain_image);
    void draw_frame          ();

    void cleanup_swapwhain   ();
    void deinit              ();

    void init_events         ();

    shared_ptr<Model>         m_model;
    shared_ptr<Camera>        m_camera;
    shared_ptr<Key>           m_key;
    AppSettings               m_appsettings;

    BaseDeviceUniquePtr       m_device_ptr;
    InstanceUniquePtr         m_instance_ptr;
    const PhysicalDevice*     m_physical_device_ptr;
    Queue*                    m_present_queue_ptr;
    RenderingSurfaceUniquePtr m_rendering_surface_ptr;
    SwapchainUniquePtr        m_swapchain_ptr;
    WindowUniquePtr           m_window_ptr;

    vector<DescriptorSet::CombinedImageSamplerBindingElement> m_texture_combined_image_samplers_binding;
    
    BufferUniquePtr m_texture_indices_uniform_buffer_ptr;

    DescriptorSetGroupUniquePtr                  m_dsg_ptr;


    RenderPassUniquePtr                          m_renderpass_ptr;
    SubPassID                                    m_render_pass_subpass_GBuffer_id;
    unique_ptr<ShaderModuleStageEntryPoint>      m_vs_ptr;
    unique_ptr<ShaderModuleStageEntryPoint>      m_fs_ptr;
    unique_ptr<ShaderModuleStageEntryPoint>      m_picking_cs_ptr;
    unique_ptr<ShaderModuleStageEntryPoint>      m_deferred_cs_ptr;

    PipelineID                                   m_gfx_pipeline_id;
    PipelineID                                   m_picking_compute_pipeline_id;
    PipelineID                                   m_deferred_compute_pipeline_id;


    vector<DescriptorSet::StorageImageBindingElement>   m_color_image_ptr;
    ImageUniquePtr                               m_depth_image_ptr;
    ImageViewUniquePtr                           m_depth_image_view_ptr;
    ImageUniquePtr                               m_depth_image2_ptr;
    ImageViewUniquePtr                           m_depth_image_view2_ptr;
    ImageUniquePtr                               m_tangent_frame_image_ptr;
    ImageViewUniquePtr                           m_tangent_frame_image_view_ptr;
    ImageUniquePtr                               m_uv_and_depth_gradient_image_ptr;
    ImageViewUniquePtr                           m_uv_and_depth_gradient_image_view_ptr;
    ImageUniquePtr                               m_uv_gradient_image_ptr;
    ImageViewUniquePtr                           m_uv_gradient_image_view_ptr;
    ImageUniquePtr                               m_material_id_image_ptr;
    ImageViewUniquePtr                           m_material_id_image_view_ptr;
    SamplerUniquePtr                             m_sampler;

    FramebufferUniquePtr                         m_fbo;
    PrimaryCommandBufferUniquePtr                m_command_buffers[N_SWAPCHAIN_IMAGES];


    uint32_t       m_n_last_semaphore_used;
    Format         m_depth_format;

    vector<SemaphoreUniquePtr> m_frame_signal_semaphores;
    vector<SemaphoreUniquePtr> m_frame_wait_semaphores;

    int m_width;
    int m_height;
    bool m_is_full_screen;
    RECT m_rect_before_full_screen;

    DeferredConstants         m_deferred_constants;

    DynamicBufferHelper<MVPUniform>* m_mvp_dynamic_buffer_helper;
    DynamicBufferHelper<SunLightUniform>* m_sunLight_dynamic_buffer_helper;
    DynamicBufferHelper<CameraUniform>* m_camera_dynamic_buffer_helper;
    DynamicBufferHelper<CursorDecal>* m_cursor_decal_dynamic_buffer_helper;

    BufferUniquePtr           m_picking_storage_buffer_ptr;
    VkDeviceSize              m_picking_buffer_size;

};
