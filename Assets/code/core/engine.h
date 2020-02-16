#pragma once
#include "stdafx.h"
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
    DescriptorSetGroup* getDsg();

    ~Engine();
private:
    Engine ();

    void init();
    void init_vulkan();
    void on_validation_callback(Anvil::DebugMessageSeverityFlags in_severity,
        const char* in_message_ptr);
    void init_window         ();
    void init_swapchain      ();

    void init_buffers        ();
    void init_dsgs           ();

    void init_render_pass    ();
    void init_shaders        ();
    void init_gfx_pipelines  ();
    
    Anvil::Format SelectSupportedFormat(
        const std::vector<Anvil::Format>& candidates,
        Anvil::ImageTiling tiling,
        Anvil::FormatFeatureFlags features);
    void init_depth          ();
    void init_framebuffers   ();
    void init_command_buffers();

    void init_semaphores     ();
    void update_data_ub_contents(uint32_t in_n_swapchain_image);
    void draw_frame            ();

    void deinit              ();

    void init_events         ();


    shared_ptr<Model> m_model;


    Anvil::BaseDeviceUniquePtr       m_device_ptr;
    Anvil::InstanceUniquePtr         m_instance_ptr;
    const Anvil::PhysicalDevice*     m_physical_device_ptr;
    Anvil::Queue*                    m_present_queue_ptr;
    Anvil::RenderingSurfaceUniquePtr m_rendering_surface_ptr;
    Anvil::SwapchainUniquePtr        m_swapchain_ptr;
    VkDeviceSize                     m_ub_data_size_per_swapchain_image;
    Anvil::WindowUniquePtr           m_window_ptr;

    Anvil::BufferUniquePtr                              m_uniform_buffer_ptr;
    Anvil::DescriptorSetGroupUniquePtr                  m_dsg_ptr;


    Anvil::RenderPassUniquePtr                          m_renderpass_ptr;
    Anvil::SubPassID                                    m_render_pass_subpass_id;
    std::unique_ptr<Anvil::ShaderModuleStageEntryPoint> m_vs_ptr;
    std::unique_ptr<Anvil::ShaderModuleStageEntryPoint> m_fs_ptr;
    Anvil::PipelineID                                   m_pipeline_id;


    Anvil::ImageUniquePtr                               m_depth_image_ptr;
    Anvil::ImageViewUniquePtr                           m_depth_image_view_ptr;
    Anvil::FramebufferUniquePtr                         m_fbos[N_SWAPCHAIN_IMAGES];
    Anvil::PrimaryCommandBufferUniquePtr                m_command_buffers[N_SWAPCHAIN_IMAGES];


    uint32_t       m_n_last_semaphore_used;
    const uint32_t m_n_swapchain_images;
    uint32_t       m_mipLevels;
    Anvil::Format  m_depth_format;

    std::vector<Anvil::SemaphoreUniquePtr> m_frame_signal_semaphores;
    std::vector<Anvil::SemaphoreUniquePtr> m_frame_wait_semaphores;
};
