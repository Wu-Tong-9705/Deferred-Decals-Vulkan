#pragma once
#include "stdafx.h"
#include "../scene/camera.h"
#include "../support/keys.h"
#include "../scene/model.h"
#define N_SWAPCHAIN_IMAGES (3)
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
    PipelineLayout* getPineLine();
    vector<DescriptorSet::CombinedImageSamplerBindingElement>* getCombinedImageSamplers();
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

    void init_window         ();
    void init_swapchain      ();

    void init_buffers        ();
    void init_dsgs           ();

    void init_render_pass    ();
    void init_shaders        ();
    void init_gfx_pipelines  ();
    
    Format SelectSupportedFormat(
        const vector<Format>& candidates,
        ImageTiling tiling,
        FormatFeatureFlags features);
    void init_depth          ();
    void init_framebuffers   ();
    void init_command_buffers();

    void init_semaphores     ();
    void update_data_ub_contents(uint32_t in_n_swapchain_image);
    void draw_frame          ();

    void cleanup_swapwhain   ();
    void deinit              ();

    void init_events         ();


    shared_ptr<Model>         m_model;
    shared_ptr<Camera>        m_camera;
    shared_ptr<Key>           m_key;


    BaseDeviceUniquePtr       m_device_ptr;
    InstanceUniquePtr         m_instance_ptr;
    const PhysicalDevice*     m_physical_device_ptr;
    Queue*                    m_present_queue_ptr;
    RenderingSurfaceUniquePtr m_rendering_surface_ptr;
    SwapchainUniquePtr        m_swapchain_ptr;
    VkDeviceSize              m_ub_data_size_per_swapchain_image;
    WindowUniquePtr           m_window_ptr;

    vector<DescriptorSet::CombinedImageSamplerBindingElement> m_combined_image_samplers;
    BufferUniquePtr                              m_uniform_buffer_ptr;
    DescriptorSetGroupUniquePtr                  m_dsg_ptr;


    RenderPassUniquePtr                          m_renderpass_ptr;
    SubPassID                                    m_render_pass_subpass_id;
    unique_ptr<ShaderModuleStageEntryPoint>      m_vs_ptr;
    unique_ptr<ShaderModuleStageEntryPoint>      m_fs_ptr;
    PipelineID                                   m_pipeline_id;


    ImageUniquePtr                               m_depth_image_ptr;
    ImageViewUniquePtr                           m_depth_image_view_ptr;
    FramebufferUniquePtr                         m_fbos[N_SWAPCHAIN_IMAGES];
    PrimaryCommandBufferUniquePtr                m_command_buffers[N_SWAPCHAIN_IMAGES];


    uint32_t       m_n_last_semaphore_used;
    const uint32_t m_n_swapchain_images;
    uint32_t       m_mipLevels;
    Format  m_depth_format;

    vector<SemaphoreUniquePtr> m_frame_signal_semaphores;
    vector<SemaphoreUniquePtr> m_frame_wait_semaphores;

    int m_width;
    int m_height;
    bool m_is_full_screen;
    RECT m_rect_before_full_screen;
};
