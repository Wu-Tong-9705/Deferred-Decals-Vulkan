#include "stdafx.h"
#include "engine.h"

#define APP_NAME "Deferred Decals App"

#pragma region 接口
unique_ptr<Engine>& Engine::Instance()
{
    static std::unique_ptr<Engine> instance = nullptr;

    if (!instance)
    {
        instance.reset(new Engine());
        instance->init();
    }

    return instance;
}

BaseDevice* Engine::getDevice()
{
    return m_device_ptr.get();
}

PipelineLayout* Engine::getPineLine(int id)
{
    auto gfx_pipeline_manager_ptr(m_device_ptr->get_graphics_pipeline_manager());
    auto compute_pipeline_manager_ptr(m_device_ptr->get_compute_pipeline_manager());
    switch(id)
    {
    case 0:
        return gfx_pipeline_manager_ptr->get_pipeline_layout(m_GBuffer_gfx_pipeline_id);
    case 1:
        return gfx_pipeline_manager_ptr->get_pipeline_layout(m_cluster_gfx_pipeline_id[0]);
    case 2:
        return gfx_pipeline_manager_ptr->get_pipeline_layout(m_cluster_gfx_pipeline_id[1]);
    case 3:
        return gfx_pipeline_manager_ptr->get_pipeline_layout(m_cluster_gfx_pipeline_id[2]);
    case 4:
        return compute_pipeline_manager_ptr->get_pipeline_layout(m_picking_compute_pipeline_id);
    case 5:
        return compute_pipeline_manager_ptr->get_pipeline_layout(m_deferred_compute_pipeline_id);
    }

}

Sampler* Engine::getSampler()
{
    return m_sampler.get();
}

vector<DescriptorSet::CombinedImageSamplerBindingElement>* Engine::getTextureCombinedImageSamplersBinding()
{
    return &m_texture_combined_image_samplers_binding;
}

float Engine::getAspect()
{
    return (float)m_width / m_height;
}
#pragma endregion

#pragma region 初始化
Engine::Engine()
    :m_n_last_semaphore_used           (0),
     m_is_full_screen                  (false),
     m_width                           (1280),
     m_height                          (720),
     m_n_decal                         (0)
{
    // ..
}

void Engine::init()
{
    m_camera = make_shared<Camera>(vec3(-11.5f, 1.85f, -0.45f));
    m_camera->SetAsActive();
    m_key = make_shared<Key>();
    m_mouse = make_shared<Mouse>();
    m_appsettings = AppSettings();
    
    init_vulkan();
    init_window();
    init_swapchain();


    m_model = make_shared<Model>("assets/models/Sponza/Sponza.fbx");
    make_box(2);
    init_buffers();
    init_image();
    init_sampler();
    m_model->add_combined_image_samplers();
    init_dsgs();

    init_render_pass();
    init_shaders();
    init_gfx_pipelines();
    init_compute_pipelines();

    init_framebuffers();
    init_command_buffers();

    init_semaphores();
    
}

void Engine::init_vulkan()
{
    /* Create a Vulkan instance */
    {
        auto create_info_ptr = InstanceCreateInfo::create(
            APP_NAME,  /* in_app_name    */
            APP_NAME,  /* in_engine_name */
            #ifdef NDEBUG
            Anvil::DebugCallbackFunction(),
            #else
            std::bind(&Engine::on_validation_callback,
                this,
                std::placeholders::_1,
                std::placeholders::_2),
            #endif           
            false); /* in_mt_safe */

        m_instance_ptr = Instance::create(move(create_info_ptr));
    }

    m_physical_device_ptr = m_instance_ptr->get_physical_device(0);

    /* Create a Vulkan device */
    {
        auto create_info_ptr = DeviceCreateInfo::create_sgpu(
            m_physical_device_ptr,
            true,                       /* in_enable_shader_module_cache */
            DeviceExtensionConfiguration(),
            vector<string>(), /* in_layers */
            CommandPoolCreateFlagBits::NONE,
            false);                     /* in_mt_safe */

        m_device_ptr = SGPUDevice::create(move(create_info_ptr));
    }
}

void Engine::init_window()
{
#ifdef ENABLE_OFFSCREEN_RENDERING
    const WindowPlatform platform = WINDOW_PLATFORM_DUMMY_WITH_PNG_SNAPSHOTS;
#else
#ifdef _WIN32
    const WindowPlatform platform = WINDOW_PLATFORM_SYSTEM;
#else
    const WindowPlatform platform = WINDOW_PLATFORM_XCB;
#endif
#endif

    m_window_ptr = WindowFactory::create_window(
            platform,
            APP_NAME,
            m_width,
            m_height,
            true, /* in_closable */
            bind(&Engine::draw_frame, this)
    );

    ShowCursor(FALSE);

    m_window_ptr->register_for_callbacks(
        WINDOW_CALLBACK_ID_MOUSE_MOVE,
        bind(&Engine::mouse_move_callback,
            this,
            placeholders::_1),
        this
    );

    m_window_ptr->register_for_callbacks(
        WINDOW_CALLBACK_ID_MOUSE_LBUTTON_UP,
        bind(&Engine::mouse_click_callback,
            this,
            placeholders::_1),
        this
    );
    
    m_window_ptr->register_for_callbacks(
        WINDOW_CALLBACK_ID_MOUSE_WHEEL,
        bind(&Engine::scroll_callback,
            this,
            placeholders::_1),
        this
    );

    m_window_ptr->register_for_callbacks(
        WINDOW_CALLBACK_ID_KEY_PRESS,
        bind(&Engine::key_press_callback,
            this,
            placeholders::_1),
        this
    );

    m_window_ptr->register_for_callbacks(
        WINDOW_CALLBACK_ID_KEY_RELEASE,
        bind(&Engine::key_release_callback,
            this,
            placeholders::_1),
        this
    );
}

void Engine::init_swapchain()
{
    SGPUDevice* device_ptr(reinterpret_cast<SGPUDevice*>(m_device_ptr.get()));

    {
        auto create_info_ptr = RenderingSurfaceCreateInfo::create(
            m_instance_ptr.get(),
            m_device_ptr.get(),
            m_window_ptr.get());

        m_rendering_surface_ptr = RenderingSurface::create(std::move(create_info_ptr));
    }

    m_rendering_surface_ptr->set_name("Main rendering surface");


    m_swapchain_ptr = device_ptr->create_swapchain(
        m_rendering_surface_ptr.get(),
        m_window_ptr.get(),
        Format::B8G8R8A8_UNORM,
        ColorSpaceKHR::SRGB_NONLINEAR_KHR,
        PresentModeKHR::FIFO_KHR,
        ImageUsageFlagBits::COLOR_ATTACHMENT_BIT | ImageUsageFlagBits::STORAGE_BIT,
        N_SWAPCHAIN_IMAGES);

    m_swapchain_ptr->set_name("Main swapchain");
    m_width = m_swapchain_ptr->get_width();
    m_height = m_swapchain_ptr->get_height();

    /* Cache the queue we are going to use for presentation */
    const vector<uint32_t>* present_queue_fams_ptr = nullptr;

    if (!m_rendering_surface_ptr->get_queue_families_with_present_support(
            device_ptr->get_physical_device(),
            &present_queue_fams_ptr))
    {
        anvil_assert_fail();
    }

    m_present_queue_ptr = device_ptr->get_queue_for_queue_family_index(
        present_queue_fams_ptr->at(0),
        0); /* in_n_queue */
}



void Engine::init_buffers()
{
    const auto ub_data_alignment_requirement = 
        m_device_ptr->get_physical_device_properties().core_vk1_0_properties_ptr->limits.min_uniform_buffer_offset_alignment;
    
    #pragma region 创建texture_indices缓冲
    {
        auto allocator_ptr = MemoryAllocator::create_oneshot(m_device_ptr.get());

        VkDeviceSize size = Utils::round_up(
            sizeof(TextureIndicesUniform) * m_model->get_texture_indices()->size(),
            ub_data_alignment_requirement);
        auto create_info_ptr = BufferCreateInfo::create_no_alloc(
            m_device_ptr.get(),
            size,
            QueueFamilyFlagBits::GRAPHICS_BIT | QueueFamilyFlagBits::COMPUTE_BIT,
            SharingMode::EXCLUSIVE,
            BufferCreateFlagBits::NONE,
            BufferUsageFlagBits::UNIFORM_BUFFER_BIT);
        m_texture_indices_uniform_buffer_ptr = Buffer::create(move(create_info_ptr));
        m_texture_indices_uniform_buffer_ptr->set_name_formatted("Texture indices unfiorm buffer");

        allocator_ptr->add_buffer(
            m_texture_indices_uniform_buffer_ptr.get(),
            MemoryFeatureFlagBits::NONE); /* in_required_memory_features */

        m_texture_indices_uniform_buffer_ptr->write(
            0, /* start_offset */
            size,
            m_model->get_texture_indices()->data(),
            m_device_ptr->get_universal_queue(0));
    }
    #pragma endregion

    #pragma region 创建decal缓冲
    {
        auto allocator_ptr = MemoryAllocator::create_oneshot(m_device_ptr.get());

        m_decals_buffer_size = Utils::round_up(
            sizeof(Decal) * N_MAX_STORED_DECALS,
            ub_data_alignment_requirement);
        auto create_info_ptr = BufferCreateInfo::create_no_alloc(
            m_device_ptr.get(),
            m_decals_buffer_size,
            QueueFamilyFlagBits::GRAPHICS_BIT | QueueFamilyFlagBits::COMPUTE_BIT,
            SharingMode::EXCLUSIVE,
            BufferCreateFlagBits::NONE,
            BufferUsageFlagBits::UNIFORM_BUFFER_BIT);
        m_decals_uniform_buffer_ptr = Buffer::create(move(create_info_ptr));
        m_decals_uniform_buffer_ptr->set_name_formatted("Decal unfiorm buffer");

        allocator_ptr->add_buffer(
            m_decals_uniform_buffer_ptr.get(),
            MemoryFeatureFlagBits::NONE); /* in_required_memory_features */
    }
    #pragma endregion

    #pragma region 创建cluster缓冲
    {
        auto allocator_ptr = MemoryAllocator::create_oneshot(m_device_ptr.get());

        m_cluster_buffer_size = Utils::round_up(sizeof(ClusterStorage), ub_data_alignment_requirement);

        auto create_info_ptr = BufferCreateInfo::create_no_alloc(
            m_device_ptr.get(),
            m_cluster_buffer_size,
            QueueFamilyFlagBits::GRAPHICS_BIT | QueueFamilyFlagBits::COMPUTE_BIT,
            SharingMode::EXCLUSIVE,
            BufferCreateFlagBits::NONE,
            BufferUsageFlagBits::STORAGE_BUFFER_BIT);
        m_cluster_storage_buffer_ptr = Buffer::create(move(create_info_ptr));
        m_cluster_storage_buffer_ptr->set_name("Cluster storage buffer");

        allocator_ptr->add_buffer(
            m_cluster_storage_buffer_ptr.get(),
            MemoryFeatureFlagBits::NONE); /* in_required_memory_features */
    }
    #pragma endregion

    #pragma region 创建picking缓冲
    {
        auto allocator_ptr = MemoryAllocator::create_oneshot(m_device_ptr.get());

        m_picking_buffer_size = Utils::round_up(sizeof(PickingStorage), ub_data_alignment_requirement);

        auto create_info_ptr = BufferCreateInfo::create_no_alloc(
            m_device_ptr.get(),
            m_picking_buffer_size,
            QueueFamilyFlagBits::GRAPHICS_BIT | QueueFamilyFlagBits::COMPUTE_BIT,
            SharingMode::EXCLUSIVE,
            BufferCreateFlagBits::NONE,
            BufferUsageFlagBits::STORAGE_BUFFER_BIT);
        m_picking_storage_buffer_ptr = Buffer::create(move(create_info_ptr));
        m_picking_storage_buffer_ptr->set_name("Picking storage buffer");

        allocator_ptr->add_buffer(
            m_picking_storage_buffer_ptr.get(),
            MemoryFeatureFlagBits::NONE); /* in_required_memory_features */
    }
    #pragma endregion

    #pragma region 创建动态缓冲
    m_mvp_dynamic_buffer_helper = new DynamicBufferHelper<MVPUniform>(m_device_ptr.get(), "MVP");
    m_sunLight_dynamic_buffer_helper = new DynamicBufferHelper<SunLightUniform>(m_device_ptr.get(), "SunLight");
    m_camera_dynamic_buffer_helper = new DynamicBufferHelper<CameraUniform>(m_device_ptr.get(), "Camera");
    m_cursor_decal_dynamic_buffer_helper = new DynamicBufferHelper<CursorDecal>(m_device_ptr.get(), "CursorDecal");
    m_decal_indices_dynamic_buffer_helper = new DynamicBufferHelper<IndexUniform>(m_device_ptr.get(), "Decal Indices");
    m_decal_ZBounds_dynamic_buffer_helper = new DynamicBufferHelper<ZBoundsUniform>(m_device_ptr.get(), "Decal ZBounds");
    #pragma endregion
}

void Engine::init_image()
{
    auto allocator_ptr = MemoryAllocator::create_oneshot(m_device_ptr.get());

    m_depth_format = SelectSupportedFormat(
        { Format::D32_SFLOAT,  Format::D32_SFLOAT_S8_UINT,  Format::D24_UNORM_S8_UINT },
        ImageTiling::OPTIMAL,
        FormatFeatureFlagBits::DEPTH_STENCIL_ATTACHMENT_BIT | FormatFeatureFlagBits::SAMPLED_IMAGE_BIT);

    create_image_source(m_depth_image_ptr, m_depth_image_view_ptr, "Depth", m_depth_format, true);
    create_image_source(m_depth_image2_ptr, m_depth_image_view2_ptr, "Depth2", Format::R16_SNORM);
    create_image_source(m_tangent_frame_image_ptr, m_tangent_frame_image_view_ptr, "Tangent", Format::A2B10G10R10_UNORM_PACK32);
    create_image_source(m_uv_and_depth_gradient_image_ptr, m_uv_and_depth_gradient_image_view_ptr, "UV and Depth Gradient", Format::R16G16B16A16_SNORM);
    create_image_source(m_uv_gradient_image_ptr, m_uv_gradient_image_view_ptr, "UV Gradient", Format::R16G16B16A16_SNORM);
    create_image_source(m_material_id_image_ptr, m_material_id_image_view_ptr, "Material ID", Format::R8_UINT);
}

void Engine::init_sampler()
{
    auto sampler_create_info_ptr = SamplerCreateInfo::create(
        Engine::Instance()->getDevice(),
        Filter::LINEAR,
        Filter::LINEAR,
        SamplerMipmapMode::LINEAR,
        SamplerAddressMode::MIRRORED_REPEAT,
        SamplerAddressMode::MIRRORED_REPEAT,
        SamplerAddressMode::MIRRORED_REPEAT,
        0.0f,
        16,
        false,
        CompareOp::ALWAYS,
        0.0f,
        1,
        BorderColor::INT_OPAQUE_BLACK,
        false);

    m_sampler = Sampler::create(move(sampler_create_info_ptr));
}

void Engine::init_dsgs()
{
    #pragma region 创建描述符集群
    auto dsg_create_info_ptrs = vector<DescriptorSetCreateInfoUniquePtr>(11);

    #pragma region 0:材质索引及其所有纹理
    dsg_create_info_ptrs[0] = DescriptorSetCreateInfo::create();
    dsg_create_info_ptrs[0]->add_binding(
        0, /* n_binding */
        DescriptorType::UNIFORM_BUFFER,
        1, /* n_elements */
        ShaderStageFlagBits::COMPUTE_BIT);
    dsg_create_info_ptrs[0]->add_binding(
        1, /* n_binding */
        DescriptorType::COMBINED_IMAGE_SAMPLER,
        m_model->get_texture_num(), /* n_elements */
        ShaderStageFlagBits::COMPUTE_BIT);
    #pragma endregion

    #pragma region 1:GBuffer顶点着色器所需的MVP
    dsg_create_info_ptrs[1] = DescriptorSetCreateInfo::create();
    dsg_create_info_ptrs[1]->add_binding(
        0, /* n_binding */
        DescriptorType::UNIFORM_BUFFER_DYNAMIC,
        1, /* n_elements */
        ShaderStageFlagBits::VERTEX_BIT);
    #pragma endregion

    #pragma region 2:deferred所需的参数
    dsg_create_info_ptrs[2] = DescriptorSetCreateInfo::create();
    dsg_create_info_ptrs[2]->add_binding(
        0, /* n_binding */
        DescriptorType::UNIFORM_BUFFER_DYNAMIC,
        1, /* n_elements */
        ShaderStageFlagBits::COMPUTE_BIT);
    dsg_create_info_ptrs[2]->add_binding(
        1, /* n_binding */
        DescriptorType::UNIFORM_BUFFER_DYNAMIC,
        1, /* n_elements */
        ShaderStageFlagBits::COMPUTE_BIT);
    dsg_create_info_ptrs[2]->add_binding(
        2, /* n_binding */
        DescriptorType::STORAGE_BUFFER,
        1, /* n_elements */
        ShaderStageFlagBits::COMPUTE_BIT);
    dsg_create_info_ptrs[2]->add_binding(
        3, /* n_binding */
        DescriptorType::UNIFORM_BUFFER_DYNAMIC,
        1, /* n_elements */
        ShaderStageFlagBits::COMPUTE_BIT);
    #pragma endregion

    #pragma region 3:用于deferred读取的GBuffer
    dsg_create_info_ptrs[3] = DescriptorSetCreateInfo::create();
    for (int i = 0; i < 5; i++)
    {
        dsg_create_info_ptrs[3]->add_binding(
            i, /* n_binding */
            DescriptorType::COMBINED_IMAGE_SAMPLER,
            1, /* n_elements */
            ShaderStageFlagBits::COMPUTE_BIT);
    }
    #pragma endregion

    #pragma region 4:交换链图像
    for (int i = 4; i < 4 + N_SWAPCHAIN_IMAGES; i++)
    {
        dsg_create_info_ptrs[i] = DescriptorSetCreateInfo::create();
        dsg_create_info_ptrs[i]->add_binding(
            0, /* n_binding */
            DescriptorType::STORAGE_IMAGE,
            1, /* n_elements */
            ShaderStageFlagBits::COMPUTE_BIT);
    }
    #pragma endregion

    #pragma region 5:picking所需的参数
    dsg_create_info_ptrs[4 + N_SWAPCHAIN_IMAGES] = DescriptorSetCreateInfo::create();
    dsg_create_info_ptrs[4 + N_SWAPCHAIN_IMAGES]->add_binding(
        0, /* n_binding */
        DescriptorType::STORAGE_BUFFER,
        1, /* n_elements */
        ShaderStageFlagBits::COMPUTE_BIT);
    dsg_create_info_ptrs[4 + N_SWAPCHAIN_IMAGES]->add_binding(
        1, /* n_binding */
        DescriptorType::UNIFORM_BUFFER_DYNAMIC,
        1, /* n_elements */
        ShaderStageFlagBits::COMPUTE_BIT);
    dsg_create_info_ptrs[4 + N_SWAPCHAIN_IMAGES]->add_binding(
        2, /* n_binding */
        DescriptorType::COMBINED_IMAGE_SAMPLER,
        1, /* n_elements */
        ShaderStageFlagBits::COMPUTE_BIT);
    dsg_create_info_ptrs[4 + N_SWAPCHAIN_IMAGES]->add_binding(
        3, /* n_binding */
        DescriptorType::COMBINED_IMAGE_SAMPLER,
        1, /* n_elements */
        ShaderStageFlagBits::COMPUTE_BIT);
    dsg_create_info_ptrs[4 + N_SWAPCHAIN_IMAGES]->add_binding(
        4, /* n_binding */
        DescriptorType::COMBINED_IMAGE_SAMPLER,
        1, /* n_elements */
        ShaderStageFlagBits::COMPUTE_BIT);
    #pragma endregion
    
    #pragma region 6:贴花
    dsg_create_info_ptrs[5 + N_SWAPCHAIN_IMAGES] = DescriptorSetCreateInfo::create();
    dsg_create_info_ptrs[5 + N_SWAPCHAIN_IMAGES]->add_binding(
        0, /* n_binding */
        DescriptorType::UNIFORM_BUFFER,
        1, /* n_elements */
        ShaderStageFlagBits::VERTEX_BIT | ShaderStageFlagBits::FRAGMENT_BIT | ShaderStageFlagBits::COMPUTE_BIT);
    #pragma endregion

    #pragma region 7:cluster所需数据
    dsg_create_info_ptrs[6 + N_SWAPCHAIN_IMAGES] = DescriptorSetCreateInfo::create();
    dsg_create_info_ptrs[6 + N_SWAPCHAIN_IMAGES]->add_binding(
        0, /* n_binding */
        DescriptorType::UNIFORM_BUFFER_DYNAMIC,
        1, /* n_elements */
        ShaderStageFlagBits::VERTEX_BIT);
    dsg_create_info_ptrs[6 + N_SWAPCHAIN_IMAGES]->add_binding(
        1, /* n_binding */
        DescriptorType::UNIFORM_BUFFER_DYNAMIC,
        1, /* n_elements */
        ShaderStageFlagBits::FRAGMENT_BIT);
    #pragma endregion

    #pragma region 8:cluster结果
    dsg_create_info_ptrs[7 + N_SWAPCHAIN_IMAGES] = DescriptorSetCreateInfo::create();
    dsg_create_info_ptrs[7 + N_SWAPCHAIN_IMAGES]->add_binding(
        0, /* n_binding */
        DescriptorType::STORAGE_BUFFER,
        1, /* n_elements */
        ShaderStageFlagBits::FRAGMENT_BIT);
    #pragma endregion

    m_dsg_ptr = DescriptorSetGroup::create(
        m_device_ptr.get(),
        dsg_create_info_ptrs);
    #pragma endregion

    #pragma region 为描述符集绑定具体资源

    #pragma region 0:材质索引及其所有纹理
    m_dsg_ptr->set_binding_item(
        0, /* n_set:用于dsg标识内部的描述符集，与dsg_create_info_ptrs下标一一对应，与shader里的set无关*/
        0, /* n_binding */
        DescriptorSet::UniformBufferBindingElement(
            m_texture_indices_uniform_buffer_ptr.get()));
    m_dsg_ptr->set_binding_array_items(
        0, /* n_set:用于dsg标识内部的描述符集，与dsg_create_info_ptrs下标一一对应，与shader里的set无关*/
        1, /* n_binding */
        BindingElementArrayRange(
            0,                                  /* StartBindingElementIndex */
            m_texture_combined_image_samplers_binding.size()),  /* NumberOfBindingElements  */
        m_texture_combined_image_samplers_binding.data());
    #pragma endregion

    #pragma region 1:GBuffer顶点着色器所需的的MVP
    m_dsg_ptr->set_binding_item(
        1, /* n_set:用于dsg标识内部的描述符集，与dsg_create_info_ptrs下标一一对应，与shader里的set无关*/
        0, /* n_binding */
        DescriptorSet::DynamicUniformBufferBindingElement(
            m_mvp_dynamic_buffer_helper->getUniform(),
            0, /* in_start_offset */
            m_mvp_dynamic_buffer_helper->getSizePerSwapchainImage()));
    #pragma endregion

    #pragma region 2:deferred所需的参数
    m_dsg_ptr->set_binding_item(
        2, /* n_set:用于dsg标识内部的描述符集，与dsg_create_info_ptrs下标一一对应，与shader里的set无关*/
        0, /* n_binding */
        DescriptorSet::DynamicUniformBufferBindingElement(
            m_sunLight_dynamic_buffer_helper->getUniform(),
            0, /* in_start_offset */
            m_sunLight_dynamic_buffer_helper->getSizePerSwapchainImage()));

    m_dsg_ptr->set_binding_item(
        2, /* n_set:用于dsg标识内部的描述符集，与dsg_create_info_ptrs下标一一对应，与shader里的set无关*/
        1, /* n_binding */
        DescriptorSet::DynamicUniformBufferBindingElement(
            m_camera_dynamic_buffer_helper->getUniform(),
            0, /* in_start_offset */
            m_camera_dynamic_buffer_helper->getSizePerSwapchainImage()));

    m_dsg_ptr->set_binding_item(
        2, /* n_set:用于dsg标识内部的描述符集，与dsg_create_info_ptrs下标一一对应，与shader里的set无关*/
        2, /* n_binding */
        DescriptorSet::StorageBufferBindingElement(
            m_picking_storage_buffer_ptr.get(),
            0, /* in_start_offset */
            m_picking_buffer_size));

    m_dsg_ptr->set_binding_item(
        2, /* n_set:用于dsg标识内部的描述符集，与dsg_create_info_ptrs下标一一对应，与shader里的set无关*/
        3, /* n_binding */
        DescriptorSet::DynamicUniformBufferBindingElement(
            m_cursor_decal_dynamic_buffer_helper->getUniform(),
            0, /* in_start_offset */
            m_cursor_decal_dynamic_buffer_helper->getSizePerSwapchainImage()));
    #pragma endregion

    #pragma region 3:用于deferred读取的GBuffer
    m_dsg_ptr->set_binding_item(
        3, /* n_set:用于dsg标识内部的描述符集，与dsg_create_info_ptrs下标一一对应，与shader里的set无关*/
        0, /* n_binding */
        DescriptorSet::CombinedImageSamplerBindingElement(
            ImageLayout::SHADER_READ_ONLY_OPTIMAL,
            m_depth_image_view2_ptr.get(),
            m_sampler.get()));
    
    m_dsg_ptr->set_binding_item(
        3, /* n_set:用于dsg标识内部的描述符集，与dsg_create_info_ptrs下标一一对应，与shader里的set无关*/
        1, /* n_binding */
        DescriptorSet::CombinedImageSamplerBindingElement(
            ImageLayout::SHADER_READ_ONLY_OPTIMAL,
            m_tangent_frame_image_view_ptr.get(),
            m_sampler.get()));

    m_dsg_ptr->set_binding_item(
        3, /* n_set:用于dsg标识内部的描述符集，与dsg_create_info_ptrs下标一一对应，与shader里的set无关*/
        2, /* n_binding */
        DescriptorSet::CombinedImageSamplerBindingElement(
            ImageLayout::SHADER_READ_ONLY_OPTIMAL,
            m_uv_and_depth_gradient_image_view_ptr.get(),
            m_sampler.get()));

    m_dsg_ptr->set_binding_item(
        3, /* n_set:用于dsg标识内部的描述符集，与dsg_create_info_ptrs下标一一对应，与shader里的set无关*/
        3, /* n_binding */
        DescriptorSet::CombinedImageSamplerBindingElement(
            ImageLayout::SHADER_READ_ONLY_OPTIMAL,
            m_uv_gradient_image_view_ptr.get(),
            m_sampler.get()));

    m_dsg_ptr->set_binding_item(
        3, /* n_set:用于dsg标识内部的描述符集，与dsg_create_info_ptrs下标一一对应，与shader里的set无关*/
        4, /* n_binding */
        DescriptorSet::CombinedImageSamplerBindingElement(
            ImageLayout::SHADER_READ_ONLY_OPTIMAL,
            m_material_id_image_view_ptr.get(),
            m_sampler.get()));
    #pragma endregion

    #pragma region 4:交换链图像
    for (int i = 0; i < N_SWAPCHAIN_IMAGES; i++)
    {
        m_dsg_ptr->set_binding_item(
            4 + i, /* n_set:用于dsg标识内部的描述符集，与dsg_create_info_ptrs下标一一对应，与shader里的set无关*/
            0, /* n_binding */
            DescriptorSet::StorageImageBindingElement(
                ImageLayout::GENERAL,
                m_swapchain_ptr->get_image_view(i)));
    }
    #pragma endregion
    
    #pragma region 5:picking所需的参数和GBuffer
    m_dsg_ptr->set_binding_item(
        4 + N_SWAPCHAIN_IMAGES, /* n_set:用于dsg标识内部的描述符集，与dsg_create_info_ptrs下标一一对应，与shader里的set无关*/
        0, /* n_binding */
        DescriptorSet::StorageBufferBindingElement(
            m_picking_storage_buffer_ptr.get(),
            0, /* in_start_offset */
            m_picking_buffer_size));
    m_dsg_ptr->set_binding_item(
        4 + N_SWAPCHAIN_IMAGES, /* n_set:用于dsg标识内部的描述符集，与dsg_create_info_ptrs下标一一对应，与shader里的set无关*/
        1, /* n_binding */
        DescriptorSet::DynamicUniformBufferBindingElement(
            m_camera_dynamic_buffer_helper->getUniform(),
            0, /* in_start_offset */
            m_camera_dynamic_buffer_helper->getSizePerSwapchainImage()));
    m_dsg_ptr->set_binding_item(
        4 + N_SWAPCHAIN_IMAGES, /* n_set:用于dsg标识内部的描述符集，与dsg_create_info_ptrs下标一一对应，与shader里的set无关*/
        2, /* n_binding */
        DescriptorSet::CombinedImageSamplerBindingElement(
            ImageLayout::SHADER_READ_ONLY_OPTIMAL,
            m_depth_image_view2_ptr.get(),
            m_sampler.get()));
    m_dsg_ptr->set_binding_item(
        4 + N_SWAPCHAIN_IMAGES, /* n_set:用于dsg标识内部的描述符集，与dsg_create_info_ptrs下标一一对应，与shader里的set无关*/
        3, /* n_binding */
        DescriptorSet::CombinedImageSamplerBindingElement(
            ImageLayout::SHADER_READ_ONLY_OPTIMAL,
            m_tangent_frame_image_view_ptr.get(),
            m_sampler.get()));
    m_dsg_ptr->set_binding_item(
        4 + N_SWAPCHAIN_IMAGES, /* n_set:用于dsg标识内部的描述符集，与dsg_create_info_ptrs下标一一对应，与shader里的set无关*/
        4, /* n_binding */
        DescriptorSet::CombinedImageSamplerBindingElement(
            ImageLayout::SHADER_READ_ONLY_OPTIMAL,
            m_material_id_image_view_ptr.get(),
            m_sampler.get()));
    #pragma endregion

    #pragma region 6:贴花
    m_dsg_ptr->set_binding_item(
        5 + N_SWAPCHAIN_IMAGES, /* n_set:用于dsg标识内部的描述符集，与dsg_create_info_ptrs下标一一对应，与shader里的set无关*/
        0, /* n_binding */
        DescriptorSet::UniformBufferBindingElement(
            m_decals_uniform_buffer_ptr.get()));
    #pragma endregion

    #pragma region 7:cluster所需数据
    m_dsg_ptr->set_binding_item(
        6 + N_SWAPCHAIN_IMAGES, /* n_set:用于dsg标识内部的描述符集，与dsg_create_info_ptrs下标一一对应，与shader里的set无关*/
        0, /* n_binding */
        DescriptorSet::UniformBufferBindingElement(
            m_decal_indices_dynamic_buffer_helper->getUniform(),
            0, /* in_start_offset */
            m_decal_indices_dynamic_buffer_helper->getSizePerSwapchainImage()));
    m_dsg_ptr->set_binding_item(
        6 + N_SWAPCHAIN_IMAGES, /* n_set:用于dsg标识内部的描述符集，与dsg_create_info_ptrs下标一一对应，与shader里的set无关*/
        1, /* n_binding */
        DescriptorSet::UniformBufferBindingElement(
            m_decal_ZBounds_dynamic_buffer_helper->getUniform(),
            0, /* in_start_offset */
            m_decal_ZBounds_dynamic_buffer_helper->getSizePerSwapchainImage()));
    #pragma endregion

    #pragma region 8:cluster结果
    m_dsg_ptr->set_binding_item(
        7 + N_SWAPCHAIN_IMAGES, /* n_set:用于dsg标识内部的描述符集，与dsg_create_info_ptrs下标一一对应，与shader里的set无关*/
        0, /* n_binding */
        DescriptorSet::StorageBufferBindingElement(
            m_cluster_storage_buffer_ptr.get(),
            0, /* in_start_offset */
            m_cluster_buffer_size));
    #pragma endregion

    #pragma endregion
}



void Engine::init_render_pass()
{
    RenderPassCreateInfoUniquePtr render_pass_create_info_ptr(new RenderPassCreateInfo(m_device_ptr.get()));
    
    #pragma region 添加附件描述
    RenderPassAttachmentID 
        depth_color_attachment_id, 
        tangent_frame_color_attachment_id, 
        uv_and_depth_gradient_color_attachment_id, 
        uv_gradient_color_attachment_id, 
        material_id_color_attachment_id, 
        render_pass_depth_attachment_id;
    
    render_pass_create_info_ptr->add_color_attachment(
        Format::R16_SNORM,
        SampleCountFlagBits::_1_BIT,
        AttachmentLoadOp::CLEAR,
        AttachmentStoreOp::STORE,
        ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
        ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
        false, /* may_alias */
        &depth_color_attachment_id);

    render_pass_create_info_ptr->add_color_attachment(
        Format::A2B10G10R10_UNORM_PACK32,
        SampleCountFlagBits::_1_BIT,
        AttachmentLoadOp::CLEAR,
        AttachmentStoreOp::STORE,
        ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
        ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
        false, /* may_alias */
        &tangent_frame_color_attachment_id);

    render_pass_create_info_ptr->add_color_attachment(
        Format::R16G16B16A16_SNORM,
        SampleCountFlagBits::_1_BIT,
        AttachmentLoadOp::CLEAR,
        AttachmentStoreOp::STORE,
        ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
        ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
        false, /* may_alias */
        &uv_and_depth_gradient_color_attachment_id);

    render_pass_create_info_ptr->add_color_attachment(
        Format::R16G16B16A16_SNORM,
        SampleCountFlagBits::_1_BIT,
        AttachmentLoadOp::CLEAR,
        AttachmentStoreOp::STORE,
        ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
        ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
        false, /* may_alias */
        &uv_gradient_color_attachment_id);

    render_pass_create_info_ptr->add_color_attachment(
        Format::R8_UINT,
        SampleCountFlagBits::_1_BIT,
        AttachmentLoadOp::CLEAR,
        AttachmentStoreOp::STORE,
        ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
        ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
        false, /* may_alias */
        &material_id_color_attachment_id);
    
    render_pass_create_info_ptr->add_depth_stencil_attachment(
        m_depth_format,
        SampleCountFlagBits::_1_BIT,
        AttachmentLoadOp::CLEAR,
        AttachmentStoreOp::DONT_CARE,
        AttachmentLoadOp::DONT_CARE,
        AttachmentStoreOp::DONT_CARE,
        ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        false, /* may_alias */
        &render_pass_depth_attachment_id);
    #pragma endregion

    #pragma region 为子流程添加附件
    {
        render_pass_create_info_ptr->add_subpass(&m_render_pass_subpass_GBuffer_id);

        render_pass_create_info_ptr->add_subpass_color_attachment(
            m_render_pass_subpass_GBuffer_id,
            ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
            depth_color_attachment_id,
            0,        /* location                      */
            nullptr); /* opt_attachment_resolve_id_ptr */

        render_pass_create_info_ptr->add_subpass_color_attachment(
            m_render_pass_subpass_GBuffer_id,
            ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
            tangent_frame_color_attachment_id,
            1,        /* location                      */
            nullptr); /* opt_attachment_resolve_id_ptr */

        render_pass_create_info_ptr->add_subpass_color_attachment(
            m_render_pass_subpass_GBuffer_id,
            ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
            uv_and_depth_gradient_color_attachment_id,
            2,        /* location                      */
            nullptr); /* opt_attachment_resolve_id_ptr */

        render_pass_create_info_ptr->add_subpass_color_attachment(
            m_render_pass_subpass_GBuffer_id,
            ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
            uv_gradient_color_attachment_id,
            3,        /* location                      */
            nullptr); /* opt_attachment_resolve_id_ptr */

        render_pass_create_info_ptr->add_subpass_color_attachment(
            m_render_pass_subpass_GBuffer_id,
            ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
            material_id_color_attachment_id,
            4,        /* location                      */
            nullptr); /* opt_attachment_resolve_id_ptr */
   
        render_pass_create_info_ptr->add_subpass_depth_stencil_attachment(
            m_render_pass_subpass_GBuffer_id,
            ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            render_pass_depth_attachment_id);

        for (int i = 0; i < 3; i++)
        {
            render_pass_create_info_ptr->add_subpass(&m_render_pass_subpass_cluster_id[i]);
            if (i > 0)
            {
                render_pass_create_info_ptr->add_subpass_to_subpass_dependency(
                    m_render_pass_subpass_cluster_id[i - 1],
                    m_render_pass_subpass_cluster_id[i],
                    PipelineStageFlagBits::FRAGMENT_SHADER_BIT,
                    PipelineStageFlagBits::FRAGMENT_SHADER_BIT,
                    AccessFlagBits::MEMORY_WRITE_BIT,
                    AccessFlagBits::MEMORY_READ_BIT,
                    DependencyFlagBits::BY_REGION_BIT);
            }
        }
    }
    #pragma endregion

    m_renderpass_ptr = RenderPass::create(
        move(render_pass_create_info_ptr),
        m_swapchain_ptr.get());
    m_renderpass_ptr->set_name("GBuffer renderpass");

}

void Engine::init_shaders()
{
    m_cluster_vs_ptr.reset(create_shader("Assets/code/shader/cluster.vert", ShaderStage::VERTEX, "Cluster Vertex"));
    m_cluster_fs_ptr.reset(create_shader("Assets/code/shader/cluster.frag", ShaderStage::FRAGMENT, "Cluster Fragment"));
    m_GBuffer_vs_ptr.reset(create_shader("Assets/code/shader/GBuffer.vert", ShaderStage::VERTEX, "GBuffer Vertex"));
    m_GBuffer_fs_ptr.reset(create_shader("Assets/code/shader/GBuffer.frag", ShaderStage::FRAGMENT, "GBuffer Fragment"));
    m_picking_cs_ptr.reset(create_shader("Assets/code/shader/picking.comp", ShaderStage::COMPUTE, "Picking Compute"));
    m_deferred_cs_ptr.reset(create_shader("Assets/code/shader/deferred.comp", ShaderStage::COMPUTE, "Deferred Compute"));
}

void Engine::init_gfx_pipelines()
{
    auto gfx_pipeline_manager_ptr(m_device_ptr->get_graphics_pipeline_manager());

    #pragma region cluster
    for (int i = 0; i < 3; i++)
    {
        create_cluster_pipeline(gfx_pipeline_manager_ptr, i);
    }
    #pragma endregion

    #pragma region GBuffer
    {
        GraphicsPipelineCreateInfoUniquePtr gfx_pipeline_create_info_ptr;
    
        gfx_pipeline_create_info_ptr = GraphicsPipelineCreateInfo::create(
            PipelineCreateFlagBits::NONE,
            m_renderpass_ptr.get(), 
            m_render_pass_subpass_GBuffer_id,
            *m_GBuffer_fs_ptr,
            ShaderModuleStageEntryPoint(), /* in_geometry_shader        */
            ShaderModuleStageEntryPoint(), /* in_tess_control_shader    */
            ShaderModuleStageEntryPoint(), /* in_tess_evaluation_shader */
            *m_GBuffer_vs_ptr);

        vector<const DescriptorSetCreateInfo*> m_desc_create_info;
        m_desc_create_info.push_back(m_dsg_ptr->get_descriptor_set_create_info(1));
        gfx_pipeline_create_info_ptr->set_descriptor_set_create_info(&m_desc_create_info);
   
        gfx_pipeline_create_info_ptr->attach_push_constant_range(
            0, /* in_offset */
            4, /* in_size */
            ShaderStageFlagBits::FRAGMENT_BIT);

        gfx_pipeline_create_info_ptr->set_rasterization_properties(
            PolygonMode::FILL,
            CullModeFlagBits::NONE,
            FrontFace::COUNTER_CLOCKWISE,
            1.0f); /* in_line_width       */
    
        gfx_pipeline_create_info_ptr->toggle_depth_test(true, CompareOp::LESS);
        gfx_pipeline_create_info_ptr->toggle_depth_writes(true);

        gfx_pipeline_create_info_ptr->set_color_blend_attachment_properties(
            0,     /* in_attachment_id    */
            false,  /* in_blending_enabled */
            BlendOp::ADD,
            BlendOp::ADD,
            BlendFactor::SRC_ALPHA,
            BlendFactor::ONE_MINUS_SRC_ALPHA,
            BlendFactor::SRC_ALPHA,
            BlendFactor::ONE_MINUS_SRC_ALPHA,
            ColorComponentFlagBits::R_BIT 
            | ColorComponentFlagBits::B_BIT 
            | ColorComponentFlagBits::G_BIT 
            | ColorComponentFlagBits::R_BIT);
    
        gfx_pipeline_create_info_ptr->add_vertex_binding(
            0, /* in_binding */
            VertexInputRate::VERTEX,
            sizeof(Vertex),
            Vertex::getVertexInputAttribute().size(), /* in_n_attributes */
            Vertex::getVertexInputAttribute().data());

        gfx_pipeline_manager_ptr->add_pipeline(
            move(gfx_pipeline_create_info_ptr),
            &m_GBuffer_gfx_pipeline_id);
    }
    #pragma endregion
}

void Engine::init_compute_pipelines()
{
    auto compute_pipeline_manager_ptr(m_device_ptr->get_compute_pipeline_manager());

    #pragma region picking
    {
        ComputePipelineCreateInfoUniquePtr compute_pipeline_create_info_ptr;

        compute_pipeline_create_info_ptr = ComputePipelineCreateInfo::create(
            PipelineCreateFlagBits::NONE,
            *m_picking_cs_ptr);

        vector<const DescriptorSetCreateInfo*> m_desc_create_info;
        m_desc_create_info.push_back(m_dsg_ptr->get_descriptor_set_create_info(4 + N_SWAPCHAIN_IMAGES));
        compute_pipeline_create_info_ptr->set_descriptor_set_create_info(&m_desc_create_info);
        compute_pipeline_create_info_ptr->attach_push_constant_range(
            0,
            sizeof(m_deferred_constants),
            ShaderStageFlagBits::COMPUTE_BIT);

        compute_pipeline_manager_ptr->add_pipeline(
            move(compute_pipeline_create_info_ptr),
            &m_picking_compute_pipeline_id);
    }
    #pragma endregion

    #pragma region deferred
    {
        ComputePipelineCreateInfoUniquePtr compute_pipeline_create_info_ptr;

        compute_pipeline_create_info_ptr = ComputePipelineCreateInfo::create(
            PipelineCreateFlagBits::NONE,
            *m_deferred_cs_ptr);

        vector<const DescriptorSetCreateInfo*> m_desc_create_info;
        m_desc_create_info.push_back(m_dsg_ptr->get_descriptor_set_create_info(0));
        m_desc_create_info.push_back(m_dsg_ptr->get_descriptor_set_create_info(2));
        m_desc_create_info.push_back(m_dsg_ptr->get_descriptor_set_create_info(3));
        m_desc_create_info.push_back(m_dsg_ptr->get_descriptor_set_create_info(4));
        compute_pipeline_create_info_ptr->set_descriptor_set_create_info(&m_desc_create_info);
        compute_pipeline_create_info_ptr->attach_push_constant_range(
            0,
            sizeof(m_deferred_constants),
            ShaderStageFlagBits::COMPUTE_BIT);

        int SIZE = m_model->get_material_num();
        compute_pipeline_create_info_ptr->add_specialization_constant(0, 4, &SIZE);

        compute_pipeline_manager_ptr->add_pipeline(
            move(compute_pipeline_create_info_ptr),
            &m_deferred_compute_pipeline_id);
    }
    #pragma endregion
}




void Engine::init_framebuffers()
{
    bool result;

    auto create_info_ptr = FramebufferCreateInfo::create(
        m_device_ptr.get(),
        m_width,
        m_height,
        1 /* n_layers */);
            
    result = create_info_ptr->add_attachment(
        m_depth_image_view2_ptr.get(),
        nullptr /* out_opt_attachment_id_ptr */);
    anvil_assert(result);
    
    result = create_info_ptr->add_attachment(
        m_tangent_frame_image_view_ptr.get(),
        nullptr /* out_opt_attachment_id_ptr */);
    anvil_assert(result);

    result = create_info_ptr->add_attachment(
        m_uv_and_depth_gradient_image_view_ptr.get(),
        nullptr /* out_opt_attachment_id_ptr */);
    anvil_assert(result);

    result = create_info_ptr->add_attachment(
        m_uv_gradient_image_view_ptr.get(),
        nullptr /* out_opt_attachment_id_ptr */);
    anvil_assert(result);

    result = create_info_ptr->add_attachment(
        m_material_id_image_view_ptr.get(),
        nullptr /* out_opt_attachment_id_ptr */);
    anvil_assert(result);

    result = create_info_ptr->add_attachment(
        m_depth_image_view_ptr.get(),
        nullptr /* out_opt_attachment_id_ptr */);
    anvil_assert(result);

    m_fbo = Framebuffer::create(move(create_info_ptr));
        
    m_fbo->set_name_formatted("Framebuffer");
}

void Engine::init_command_buffers()
{
    auto                   gfx_pipeline_manager_ptr(m_device_ptr->get_graphics_pipeline_manager());
    ImageSubresourceRange  image_subresource_range, image_subresource_range2;
    Queue*                 universal_queue_ptr(m_device_ptr->get_universal_queue(0));

    image_subresource_range.aspect_mask = ImageAspectFlagBits::COLOR_BIT;
    image_subresource_range.base_array_layer = 0;
    image_subresource_range.base_mip_level = 0;
    image_subresource_range.layer_count = 1;
    image_subresource_range.level_count = 1;

    for (uint32_t n_command_buffer = 0; n_command_buffer < N_SWAPCHAIN_IMAGES; ++n_command_buffer)
    {
        #pragma region 开始记录指令
        PrimaryCommandBufferUniquePtr cmd_buffer_ptr;

        cmd_buffer_ptr = m_device_ptr->
            get_command_pool_for_queue_family_index(m_device_ptr->get_universal_queue(0)->get_queue_family_index())
            ->alloc_primary_level_command_buffer();

        /* Start recording commands */
        cmd_buffer_ptr->start_recording(false, /* one_time_submit          */
                                        true); /* simultaneous_use_allowed */
        #pragma endregion

        #pragma region 改变附件图像布局用于片元着色器输出
        {
            vector<ImageBarrier> image_barriers;
            image_barriers.push_back(
                ImageBarrier(
                    AccessFlagBits::SHADER_READ_BIT,                    /* source_access_mask       */
                    AccessFlagBits::COLOR_ATTACHMENT_WRITE_BIT,         /* destination_access_mask  */
                    ImageLayout::SHADER_READ_ONLY_OPTIMAL,              /* old_image_layout */
                    ImageLayout::COLOR_ATTACHMENT_OPTIMAL,              /* new_image_layout */
                    universal_queue_ptr->get_queue_family_index(),
                    universal_queue_ptr->get_queue_family_index(),
                    m_depth_image2_ptr.get(),
                    image_subresource_range));

            image_barriers.push_back(
                ImageBarrier(
                    AccessFlagBits::SHADER_READ_BIT,                    /* source_access_mask       */
                    AccessFlagBits::COLOR_ATTACHMENT_WRITE_BIT,         /* destination_access_mask  */
                    ImageLayout::SHADER_READ_ONLY_OPTIMAL,              /* old_image_layout */
                    ImageLayout::COLOR_ATTACHMENT_OPTIMAL,              /* new_image_layout */
                    universal_queue_ptr->get_queue_family_index(),
                    universal_queue_ptr->get_queue_family_index(),
                    m_tangent_frame_image_ptr.get(),
                    image_subresource_range));

            image_barriers.push_back(
                ImageBarrier(
                    AccessFlagBits::SHADER_READ_BIT,                    /* source_access_mask       */
                    AccessFlagBits::COLOR_ATTACHMENT_WRITE_BIT,         /* destination_access_mask  */
                    ImageLayout::SHADER_READ_ONLY_OPTIMAL,              /* old_image_layout */
                    ImageLayout::COLOR_ATTACHMENT_OPTIMAL,              /* new_image_layout */
                    universal_queue_ptr->get_queue_family_index(),
                    universal_queue_ptr->get_queue_family_index(),
                    m_uv_and_depth_gradient_image_ptr.get(),
                    image_subresource_range));

            image_barriers.push_back(
                ImageBarrier(
                    AccessFlagBits::SHADER_READ_BIT,                    /* source_access_mask       */
                    AccessFlagBits::COLOR_ATTACHMENT_WRITE_BIT,         /* destination_access_mask  */
                    ImageLayout::SHADER_READ_ONLY_OPTIMAL,              /* old_image_layout */
                    ImageLayout::COLOR_ATTACHMENT_OPTIMAL,              /* new_image_layout */
                    universal_queue_ptr->get_queue_family_index(),
                    universal_queue_ptr->get_queue_family_index(),
                    m_uv_gradient_image_ptr.get(),
                    image_subresource_range));

            image_barriers.push_back(
                ImageBarrier(
                    AccessFlagBits::SHADER_READ_BIT,                    /* source_access_mask       */
                    AccessFlagBits::COLOR_ATTACHMENT_WRITE_BIT,         /* destination_access_mask  */
                    ImageLayout::SHADER_READ_ONLY_OPTIMAL,              /* old_image_layout */
                    ImageLayout::COLOR_ATTACHMENT_OPTIMAL,              /* new_image_layout */
                    universal_queue_ptr->get_queue_family_index(),
                    universal_queue_ptr->get_queue_family_index(),
                    m_material_id_image_ptr.get(),
                    image_subresource_range));

            cmd_buffer_ptr->record_pipeline_barrier(
                PipelineStageFlagBits::COMPUTE_SHADER_BIT,                /* src_stage_mask                 */
                PipelineStageFlagBits::COLOR_ATTACHMENT_OUTPUT_BIT,       /* dst_stage_mask                 */
                DependencyFlagBits::NONE,
                0,                                                        /* in_memory_barrier_count        */
                nullptr,                                                  /* in_memory_barrier_ptrs         */
                0,                                                        /* in_buffer_memory_barrier_count */
                nullptr,                                                  /* in_buffer_memory_barrier_ptrs  */
                image_barriers.size(),                                   /* in_image_memory_barrier_count  */
                image_barriers.data());
        }
        #pragma endregion
        
        #pragma region 确保cluster所需的uniform缓冲已经写入
        {
            BufferBarrier buffer_barrier1(
                AccessFlagBits::HOST_WRITE_BIT,                 /* in_source_access_mask      */
                AccessFlagBits::UNIFORM_READ_BIT,               /* in_destination_access_mask */
                universal_queue_ptr->get_queue_family_index(),         /* in_src_queue_family_index  */
                universal_queue_ptr->get_queue_family_index(),         /* in_dst_queue_family_index  */
                m_mvp_dynamic_buffer_helper->getUniform(),
                m_mvp_dynamic_buffer_helper->getSizePerSwapchainImage() * n_command_buffer, /* in_offset                  */
                m_mvp_dynamic_buffer_helper->getSizePerSwapchainImage());
            
            BufferBarrier buffer_barrier2(
                AccessFlagBits::HOST_WRITE_BIT,                 /* in_source_access_mask      */
                AccessFlagBits::UNIFORM_READ_BIT,               /* in_destination_access_mask */
                universal_queue_ptr->get_queue_family_index(),         /* in_src_queue_family_index  */
                universal_queue_ptr->get_queue_family_index(),         /* in_dst_queue_family_index  */
                m_decals_uniform_buffer_ptr.get(),
                0,                                                      /* in_offset */
                m_decals_buffer_size);

            BufferBarrier buffer_barrier3(
                AccessFlagBits::HOST_WRITE_BIT,                 /* in_source_access_mask      */
                AccessFlagBits::UNIFORM_READ_BIT,               /* in_destination_access_mask */
                universal_queue_ptr->get_queue_family_index(),         /* in_src_queue_family_index  */
                universal_queue_ptr->get_queue_family_index(),         /* in_dst_queue_family_index  */
                m_decal_indices_dynamic_buffer_helper->getUniform(),
                m_decal_indices_dynamic_buffer_helper->getSizePerSwapchainImage() * n_command_buffer, /* in_offset                  */
                m_decal_indices_dynamic_buffer_helper->getSizePerSwapchainImage());

            BufferBarrier buffer_barrier4(
                AccessFlagBits::HOST_WRITE_BIT,                 /* in_source_access_mask      */
                AccessFlagBits::UNIFORM_READ_BIT,               /* in_destination_access_mask */
                universal_queue_ptr->get_queue_family_index(),         /* in_src_queue_family_index  */
                universal_queue_ptr->get_queue_family_index(),         /* in_dst_queue_family_index  */
                m_decal_ZBounds_dynamic_buffer_helper->getUniform(),
                m_decal_ZBounds_dynamic_buffer_helper->getSizePerSwapchainImage() * n_command_buffer, /* in_offset                  */
                m_decal_ZBounds_dynamic_buffer_helper->getSizePerSwapchainImage());

            BufferBarrier buffer_barriers[4] = { buffer_barrier1 , buffer_barrier2, buffer_barrier3, buffer_barrier4 };
            cmd_buffer_ptr->record_pipeline_barrier(
                PipelineStageFlagBits::HOST_BIT,
                PipelineStageFlagBits::VERTEX_SHADER_BIT,
                DependencyFlagBits::NONE,
                0,               /* in_memory_barrier_count        */
                nullptr,         /* in_memory_barriers_ptr         */
                4,               /* in_buffer_memory_barrier_count */
                buffer_barriers,
                0,               /* in_image_memory_barrier_count  */
                nullptr);        /* in_image_memory_barriers_ptr   */
        }
        #pragma endregion

        #pragma region 确保cluster_storage缓冲可以写入
        {
            BufferBarrier buffer_barrier(
                AccessFlagBits::SHADER_READ_BIT,                      /* in_source_access_mask      */
                AccessFlagBits::SHADER_WRITE_BIT,                       /* in_destination_access_mask */
                universal_queue_ptr->get_queue_family_index(),         /* in_src_queue_family_index  */
                universal_queue_ptr->get_queue_family_index(),         /* in_dst_queue_family_index  */
                m_cluster_storage_buffer_ptr.get(),
                0,                                                     /* in_offset                  */
                m_cluster_buffer_size);

            cmd_buffer_ptr->record_pipeline_barrier(
                PipelineStageFlagBits::COMPUTE_SHADER_BIT,
                PipelineStageFlagBits::FRAGMENT_SHADER_BIT,
                DependencyFlagBits::NONE,
                0,               /* in_memory_barrier_count        */
                nullptr,         /* in_memory_barriers_ptr         */
                1,               /* in_buffer_memory_barrier_count */
                &buffer_barrier,
                0,               /* in_image_memory_barrier_count  */
                nullptr);        /* in_image_memory_barriers_ptr   */
        }
        #pragma endregion

        #pragma region 渲染GBuffer
        {
            array<VkClearValue, 6>            attachment_clear_value;
            attachment_clear_value[0].color = { 1.0f, 0.0f, 0.0f, 0.0f };
            attachment_clear_value[1].color = { 0.0f, 0.0f, 0.0f, 0.0f };
            attachment_clear_value[2].color = { 0.0f, 0.0f, 0.0f, 0.0f };
            attachment_clear_value[3].color = { 0.0f, 0.0f, 0.0f, 0.0f };
            attachment_clear_value[4].color = { uint32(255), uint32(0), uint32(0), uint32(0) };
            attachment_clear_value[5].depthStencil = { 1.0f, 0 };

            VkRect2D                          render_area;
            render_area.extent.height = m_height;
            render_area.extent.width = m_width;
            render_area.offset.x = 0;
            render_area.offset.y = 0;

            cmd_buffer_ptr->record_begin_render_pass(
                6, /* in_n_clear_values */
                attachment_clear_value.data(),
                m_fbo.get(),
                render_area,
                m_renderpass_ptr.get(),
                SubpassContents::INLINE);
        
            const uint32_t data_ub_offset = static_cast<uint32_t>(m_mvp_dynamic_buffer_helper->getSizePerSwapchainImage() * n_command_buffer);
            DescriptorSet* ds_ptr[1] = { m_dsg_ptr->get_descriptor_set(1) };

            cmd_buffer_ptr->record_bind_pipeline(
                PipelineBindPoint::GRAPHICS,
                m_GBuffer_gfx_pipeline_id);

            cmd_buffer_ptr->record_bind_descriptor_sets(
                PipelineBindPoint::GRAPHICS,
                getPineLine(),
                0, /* firstSet */
                1, /* setCount：传入的描述符集与shader中的set一一对应 */
                ds_ptr,
                1,                /* dynamicOffsetCount */
                &data_ub_offset); /* pDynamicOffsets    */

            m_model->draw(cmd_buffer_ptr.get());
        }
        #pragma endregion

        #pragma region 对贴花做cluster
        {
            for (int i = 0; i < 3; i++)
            {
                cluster(cmd_buffer_ptr.get(), i, n_command_buffer);
            }

            cmd_buffer_ptr->record_end_render_pass();
        }
        #pragma endregion

        #pragma region 确保计算着色器所需的uniform缓冲已经写入
        {
            BufferBarrier buffer_barrier1(
                AccessFlagBits::HOST_WRITE_BIT,                 /* in_source_access_mask      */
                AccessFlagBits::UNIFORM_READ_BIT,               /* in_destination_access_mask */
                universal_queue_ptr->get_queue_family_index(),         /* in_src_queue_family_index  */
                universal_queue_ptr->get_queue_family_index(),         /* in_dst_queue_family_index  */
                m_sunLight_dynamic_buffer_helper->getUniform(),
                m_sunLight_dynamic_buffer_helper->getSizePerSwapchainImage()* n_command_buffer, /* in_offset                  */
                m_sunLight_dynamic_buffer_helper->getSizePerSwapchainImage());

            BufferBarrier buffer_barrier2(
                AccessFlagBits::HOST_WRITE_BIT,                 /* in_source_access_mask      */
                AccessFlagBits::UNIFORM_READ_BIT,               /* in_destination_access_mask */
                universal_queue_ptr->get_queue_family_index(),         /* in_src_queue_family_index  */
                universal_queue_ptr->get_queue_family_index(),         /* in_dst_queue_family_index  */
                m_camera_dynamic_buffer_helper->getUniform(),
                m_camera_dynamic_buffer_helper->getSizePerSwapchainImage() * n_command_buffer, /* in_offset                  */
                m_camera_dynamic_buffer_helper->getSizePerSwapchainImage());

            BufferBarrier buffer_barrier3(
                AccessFlagBits::HOST_WRITE_BIT,                 /* in_source_access_mask      */
                AccessFlagBits::UNIFORM_READ_BIT,               /* in_destination_access_mask */
                universal_queue_ptr->get_queue_family_index(),         /* in_src_queue_family_index  */
                universal_queue_ptr->get_queue_family_index(),         /* in_dst_queue_family_index  */
                m_cursor_decal_dynamic_buffer_helper->getUniform(),
                m_cursor_decal_dynamic_buffer_helper->getSizePerSwapchainImage() * n_command_buffer, /* in_offset                  */
                m_cursor_decal_dynamic_buffer_helper->getSizePerSwapchainImage());
        
            BufferBarrier buffer_barriers[3] = { buffer_barrier1 , buffer_barrier2, buffer_barrier3 };
            cmd_buffer_ptr->record_pipeline_barrier(
                PipelineStageFlagBits::HOST_BIT,
                PipelineStageFlagBits::COMPUTE_SHADER_BIT,
                DependencyFlagBits::NONE,
                0,               /* in_memory_barrier_count        */
                nullptr,         /* in_memory_barriers_ptr         */
                3,               /* in_buffer_memory_barrier_count */
                buffer_barriers,
                0,               /* in_image_memory_barrier_count  */
                nullptr);        /* in_image_memory_barriers_ptr   */
        }
        #pragma endregion

        #pragma region 改变附件图像布局用于计算着色器读取
        {
            vector<ImageBarrier> image_barriers;
            image_barriers.push_back(
                ImageBarrier(
                    AccessFlagBits::COLOR_ATTACHMENT_WRITE_BIT, /* source_access_mask       */
                    AccessFlagBits::SHADER_READ_BIT,           /* destination_access_mask  */
                    ImageLayout::COLOR_ATTACHMENT_OPTIMAL,      /* old_image_layout */
                    ImageLayout::SHADER_READ_ONLY_OPTIMAL,      /* new_image_layout */
                    universal_queue_ptr->get_queue_family_index(),
                    universal_queue_ptr->get_queue_family_index(),
                    m_depth_image2_ptr.get(),
                    image_subresource_range));

            image_barriers.push_back(
                ImageBarrier(
                    AccessFlagBits::COLOR_ATTACHMENT_WRITE_BIT, /* source_access_mask       */
                    AccessFlagBits::SHADER_READ_BIT,           /* destination_access_mask  */
                    ImageLayout::COLOR_ATTACHMENT_OPTIMAL,      /* old_image_layout */
                    ImageLayout::SHADER_READ_ONLY_OPTIMAL,      /* new_image_layout */
                    universal_queue_ptr->get_queue_family_index(),
                    universal_queue_ptr->get_queue_family_index(),
                    m_tangent_frame_image_ptr.get(),
                    image_subresource_range));

            image_barriers.push_back(
                ImageBarrier(
                    AccessFlagBits::COLOR_ATTACHMENT_WRITE_BIT, /* source_access_mask       */
                    AccessFlagBits::SHADER_READ_BIT,           /* destination_access_mask  */
                    ImageLayout::COLOR_ATTACHMENT_OPTIMAL,      /* old_image_layout */
                    ImageLayout::SHADER_READ_ONLY_OPTIMAL,      /* new_image_layout */
                    universal_queue_ptr->get_queue_family_index(),
                    universal_queue_ptr->get_queue_family_index(),
                    m_uv_and_depth_gradient_image_ptr.get(),
                    image_subresource_range));

            image_barriers.push_back(
                ImageBarrier(
                    AccessFlagBits::COLOR_ATTACHMENT_WRITE_BIT, /* source_access_mask       */
                    AccessFlagBits::SHADER_READ_BIT,           /* destination_access_mask  */
                    ImageLayout::COLOR_ATTACHMENT_OPTIMAL,      /* old_image_layout */
                    ImageLayout::SHADER_READ_ONLY_OPTIMAL,      /* new_image_layout */
                    universal_queue_ptr->get_queue_family_index(),
                    universal_queue_ptr->get_queue_family_index(),
                    m_uv_gradient_image_ptr.get(),
                    image_subresource_range));

            image_barriers.push_back(
                ImageBarrier(
                    AccessFlagBits::COLOR_ATTACHMENT_WRITE_BIT, /* source_access_mask       */
                    AccessFlagBits::SHADER_READ_BIT,           /* destination_access_mask  */
                    ImageLayout::COLOR_ATTACHMENT_OPTIMAL,      /* old_image_layout */
                    ImageLayout::SHADER_READ_ONLY_OPTIMAL,      /* new_image_layout */
                    universal_queue_ptr->get_queue_family_index(),
                    universal_queue_ptr->get_queue_family_index(),
                    m_material_id_image_ptr.get(),
                    image_subresource_range));

            cmd_buffer_ptr->record_pipeline_barrier(
                PipelineStageFlagBits::COLOR_ATTACHMENT_OUTPUT_BIT,       /* src_stage_mask                 */
                PipelineStageFlagBits::COMPUTE_SHADER_BIT,                /* dst_stage_mask                 */
                DependencyFlagBits::NONE,
                0,                                                        /* in_memory_barrier_count        */
                nullptr,                                                  /* in_memory_barrier_ptrs         */
                0,                                                        /* in_buffer_memory_barrier_count */
                nullptr,                                                  /* in_buffer_memory_barrier_ptrs  */
                image_barriers.size(),                                  /* in_image_memory_barrier_count  */
                image_barriers.data());
        }
        #pragma endregion

        #pragma region 改变交换链图像布局用于计算着色器写入
        {
            ImageBarrier image_barrier(
                AccessFlagBits::NONE,                       /* source_access_mask       */
                AccessFlagBits::SHADER_WRITE_BIT,           /* destination_access_mask  */
                ImageLayout::UNDEFINED,                     /* old_image_layout */
                ImageLayout::GENERAL,                       /* new_image_layout */
                universal_queue_ptr->get_queue_family_index(),
                universal_queue_ptr->get_queue_family_index(),
                m_swapchain_ptr->get_image(n_command_buffer),
                image_subresource_range);

            cmd_buffer_ptr->record_pipeline_barrier(
                PipelineStageFlagBits::ALL_COMMANDS_BIT,       /* src_stage_mask                 */
                PipelineStageFlagBits::COMPUTE_SHADER_BIT,                /* dst_stage_mask                 */
                DependencyFlagBits::NONE,
                0,                                                        /* in_memory_barrier_count        */
                nullptr,                                                  /* in_memory_barrier_ptrs         */
                0,                                                        /* in_buffer_memory_barrier_count */
                nullptr,                                                  /* in_buffer_memory_barrier_ptrs  */
                1,                                                        /* in_image_memory_barrier_count  */
                &image_barrier);
        }
        #pragma endregion

        #pragma region 确保picking_storage缓冲可以写入
        {
            BufferBarrier buffer_barrier(
                AccessFlagBits::SHADER_READ_BIT | AccessFlagBits::HOST_READ_BIT,                      /* in_source_access_mask      */
                AccessFlagBits::SHADER_WRITE_BIT,                       /* in_destination_access_mask */
                universal_queue_ptr->get_queue_family_index(),         /* in_src_queue_family_index  */
                universal_queue_ptr->get_queue_family_index(),         /* in_dst_queue_family_index  */
                m_picking_storage_buffer_ptr.get(),
                0,                                                     /* in_offset                  */
                m_picking_buffer_size);

            cmd_buffer_ptr->record_pipeline_barrier(
                PipelineStageFlagBits::COMPUTE_SHADER_BIT | PipelineStageFlagBits::HOST_BIT,
                PipelineStageFlagBits::COMPUTE_SHADER_BIT,
                DependencyFlagBits::NONE,
                0,               /* in_memory_barrier_count        */
                nullptr,         /* in_memory_barriers_ptr         */
                1,               /* in_buffer_memory_barrier_count */
                &buffer_barrier,
                0,               /* in_image_memory_barrier_count  */
                nullptr);        /* in_image_memory_barriers_ptr   */
        }
        #pragma endregion

        #pragma region 获取屏幕中间像素的位置和法线信息
        {
            const uint32_t data_ub_offset[] = { static_cast<uint32_t>(m_camera_dynamic_buffer_helper->getSizePerSwapchainImage() * n_command_buffer) };

            cmd_buffer_ptr->record_bind_pipeline(
                PipelineBindPoint::COMPUTE,
                m_picking_compute_pipeline_id);

            DescriptorSet* ds_ptr[1] = { m_dsg_ptr->get_descriptor_set(4 + N_SWAPCHAIN_IMAGES) };

            cmd_buffer_ptr->record_bind_descriptor_sets(
                PipelineBindPoint::COMPUTE,
                getPineLine(4),
                0, /* firstSet */
                1, /* setCount：传入的描述符集与shader中的set一一对应 */
                ds_ptr,
                1,                /* dynamicOffsetCount */
                data_ub_offset); /* pDynamicOffsets    */

            m_deferred_constants.RTSize.x = m_width;
            m_deferred_constants.RTSize.y = m_height;
            cmd_buffer_ptr->record_push_constants(
                getPineLine(4),
                ShaderStageFlagBits::COMPUTE_BIT,
                0, /* in_offset */
                sizeof(DeferredConstants),
                &m_deferred_constants);

            cmd_buffer_ptr->record_dispatch(1, 1, 1);
        }
        #pragma endregion

        #pragma region 确保picking_storage缓冲已经写入
        {
            BufferBarrier buffer_barrier(
                AccessFlagBits::SHADER_WRITE_BIT,                      /* in_source_access_mask      */
                AccessFlagBits::SHADER_READ_BIT | AccessFlagBits::HOST_READ_BIT,                       /* in_destination_access_mask */
                universal_queue_ptr->get_queue_family_index(),         /* in_src_queue_family_index  */
                universal_queue_ptr->get_queue_family_index(),         /* in_dst_queue_family_index  */
                m_picking_storage_buffer_ptr.get(),
                0,                                                     /* in_offset                  */
                m_picking_buffer_size);

            cmd_buffer_ptr->record_pipeline_barrier(
                PipelineStageFlagBits::COMPUTE_SHADER_BIT,
                PipelineStageFlagBits::COMPUTE_SHADER_BIT | PipelineStageFlagBits::HOST_BIT,
                DependencyFlagBits::NONE,
                0,               /* in_memory_barrier_count        */
                nullptr,         /* in_memory_barriers_ptr         */
                1,               /* in_buffer_memory_barrier_count */
                &buffer_barrier,
                0,               /* in_image_memory_barrier_count  */
                nullptr);        /* in_image_memory_barriers_ptr   */
        }
        #pragma endregion

        #pragma region 确保cluterg_storage缓冲已经写入
        {
            BufferBarrier buffer_barrier(
                AccessFlagBits::SHADER_WRITE_BIT,                      /* in_source_access_mask      */
                AccessFlagBits::SHADER_READ_BIT,                       /* in_destination_access_mask */
                universal_queue_ptr->get_queue_family_index(),         /* in_src_queue_family_index  */
                universal_queue_ptr->get_queue_family_index(),         /* in_dst_queue_family_index  */
                m_cluster_storage_buffer_ptr.get(),
                0,                                                     /* in_offset                  */
                m_cluster_buffer_size);

            cmd_buffer_ptr->record_pipeline_barrier(
                PipelineStageFlagBits::FRAGMENT_SHADER_BIT,
                PipelineStageFlagBits::COMPUTE_SHADER_BIT,
                DependencyFlagBits::NONE,
                0,               /* in_memory_barrier_count        */
                nullptr,         /* in_memory_barriers_ptr         */
                1,               /* in_buffer_memory_barrier_count */
                &buffer_barrier,
                0,               /* in_image_memory_barrier_count  */
                nullptr);        /* in_image_memory_barriers_ptr   */
        }
        #pragma endregion

        #pragma region 延迟纹理采样和光照
        {
            const uint32_t data_ub_offset[3] = {
                static_cast<uint32_t>(m_sunLight_dynamic_buffer_helper->getSizePerSwapchainImage() * n_command_buffer),
                static_cast<uint32_t>(m_camera_dynamic_buffer_helper->getSizePerSwapchainImage() * n_command_buffer),
                static_cast<uint32_t>(m_cursor_decal_dynamic_buffer_helper->getSizePerSwapchainImage() * n_command_buffer)
            };

            cmd_buffer_ptr->record_bind_pipeline(
                PipelineBindPoint::COMPUTE,
                m_deferred_compute_pipeline_id);

            DescriptorSet* ds_ptr[4] = {
                m_dsg_ptr->get_descriptor_set(0),
                m_dsg_ptr->get_descriptor_set(2),
                m_dsg_ptr->get_descriptor_set(3),
                m_dsg_ptr->get_descriptor_set(4 + n_command_buffer) };

            cmd_buffer_ptr->record_bind_descriptor_sets(
                PipelineBindPoint::COMPUTE,
                getPineLine(5),
                0, /* firstSet */
                4, /* setCount：传入的描述符集与shader中的set一一对应 */
                ds_ptr,
                3,                /* dynamicOffsetCount */
                data_ub_offset); /* pDynamicOffsets    */

            cmd_buffer_ptr->record_push_constants(
                getPineLine(5),
                ShaderStageFlagBits::COMPUTE_BIT,
                0, /* in_offset */
                sizeof(DeferredConstants),
                &m_deferred_constants);

            cmd_buffer_ptr->record_dispatch(
                (m_width + 7) / 8,
                (m_height + 7) / 8,
                1);
        }
        #pragma endregion

        #pragma region 改变交换链图像布局用于呈现
        ImageBarrier present_image_barrier(
            AccessFlagBits::SHADER_WRITE_BIT,         /* source_access_mask       */
            AccessFlagBits::NONE,                     /* destination_access_mask  */
            ImageLayout::GENERAL,                     /* old_image_layout */
            ImageLayout::PRESENT_SRC_KHR,             /* new_image_layout */
            universal_queue_ptr->get_queue_family_index(),
            universal_queue_ptr->get_queue_family_index(),
            m_swapchain_ptr->get_image(n_command_buffer),
            image_subresource_range);

        cmd_buffer_ptr->record_pipeline_barrier(
            PipelineStageFlagBits::COMPUTE_SHADER_BIT,                /* src_stage_mask                 */
            PipelineStageFlagBits::BOTTOM_OF_PIPE_BIT,                /* dst_stage_mask                 */
            DependencyFlagBits::NONE,
            0,                                                        /* in_memory_barrier_count        */
            nullptr,                                                  /* in_memory_barrier_ptrs         */
            0,                                                        /* in_buffer_memory_barrier_count */
            nullptr,                                                  /* in_buffer_memory_barrier_ptrs  */
            1,                                                        /* in_image_memory_barrier_count  */
            &present_image_barrier);
        #pragma endregion

        #pragma region 结束记录指令
        cmd_buffer_ptr->stop_recording();
        m_command_buffers[n_command_buffer] = move(cmd_buffer_ptr);
        #pragma endregion
    }
}

void Engine::init_semaphores()
{
    for (uint32_t n_semaphore = 0; n_semaphore < N_SWAPCHAIN_IMAGES; ++n_semaphore)
    {
        Anvil::SemaphoreUniquePtr new_signal_semaphore_ptr;
        Anvil::SemaphoreUniquePtr new_wait_semaphore_ptr;

        {
            auto create_info_ptr = Anvil::SemaphoreCreateInfo::create(m_device_ptr.get());

            new_signal_semaphore_ptr = Anvil::Semaphore::create(move(create_info_ptr));
        }

        {
            auto create_info_ptr = Anvil::SemaphoreCreateInfo::create(m_device_ptr.get());

            new_wait_semaphore_ptr = Anvil::Semaphore::create(move(create_info_ptr));
        }

        new_signal_semaphore_ptr->set_name_formatted("Signal semaphore [%d]",
            n_semaphore);
        new_wait_semaphore_ptr->set_name_formatted("Wait semaphore [%d]",
            n_semaphore);

        m_frame_signal_semaphores.push_back(move(new_signal_semaphore_ptr));
        m_frame_wait_semaphores.push_back(move(new_wait_semaphore_ptr));
    }
}

void Engine::recreate_swapchain()
{
    //当窗口最小化时停止渲染
    tagRECT rect;
    GetClientRect(m_window_ptr->get_handle(), &rect);
    long width = rect.right - rect.left;
    long height = rect.bottom - rect.top;

    while (width == 0 || height == 0)
    {
        GetClientRect(m_window_ptr->get_handle(), &rect);
        width = rect.right - rect.left;
        height = rect.bottom - rect.top;
        MSG msg;
        GetMessage(&msg, nullptr, 0, 0);
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
    }

    cleanup_swapwhain();

    init_swapchain();
    init_image();
    init_dsgs();
    init_framebuffers();
    init_render_pass();
    init_gfx_pipelines();
    init_compute_pipelines();
    init_command_buffers();
}
#pragma endregion

#pragma region 运行
void Engine::run()
{
    m_window_ptr->run();
}

void Engine::draw_frame()
{
    if (m_key->IsPressed(KeyID::KEY_ID_ESCAPE))
    {
        m_window_ptr->close();
        return;
    }

    if (m_key->IsPressed(KeyID::KEY_ID_F1))
    {
        m_is_full_screen = !m_is_full_screen;
        if (m_is_full_screen)
        {
            GetWindowRect(m_window_ptr->get_handle(), &m_rect_before_full_screen);
            
            HWND hDesk = GetDesktopWindow();
            RECT rc;
            GetWindowRect(hDesk, &rc);
            SetWindowLong(m_window_ptr->get_handle(), GWL_STYLE, WS_POPUP);
            SetWindowPos(m_window_ptr->get_handle(), HWND_TOPMOST, 0, 0, rc.right, rc.bottom, SWP_SHOWWINDOW);
            recreate_swapchain();
            return;
        }
        else
        {
            SetWindowLong(m_window_ptr->get_handle(), GWL_STYLE, WS_OVERLAPPEDWINDOW);
            SetWindowPos(
                m_window_ptr->get_handle(), 
                HWND_TOPMOST,
                m_rect_before_full_screen.left,
                m_rect_before_full_screen.top,
                m_rect_before_full_screen.right,
                m_rect_before_full_screen.bottom,
                SWP_SHOWWINDOW);
            recreate_swapchain();
            return;
        }
    }

    Semaphore* curr_frame_signal_semaphore_ptr = nullptr;
    Semaphore* curr_frame_wait_semaphore_ptr = nullptr;
    uint32_t n_swapchain_image;
    Queue* present_queue_ptr = m_device_ptr->get_universal_queue(0);
    Semaphore* present_wait_semaphore_ptr = nullptr;
    const PipelineStageFlags wait_stage_mask = PipelineStageFlagBits::ALL_COMMANDS_BIT;

    /* Determine the signal + wait semaphores to use for drawing this frame */
    m_n_last_semaphore_used = (m_n_last_semaphore_used + 1) % N_SWAPCHAIN_IMAGES;

    curr_frame_signal_semaphore_ptr = m_frame_signal_semaphores[m_n_last_semaphore_used].get();
    curr_frame_wait_semaphore_ptr = m_frame_wait_semaphores[m_n_last_semaphore_used].get();

    present_wait_semaphore_ptr = curr_frame_signal_semaphore_ptr;

    /* Determine the semaphore which the swapchain image */
    {
        const auto acquire_result = m_swapchain_ptr->acquire_image(
            curr_frame_wait_semaphore_ptr,
            &n_swapchain_image,
            true); /* in_should_block */

        if (acquire_result != SwapchainOperationErrorCode::SUCCESS)
        {
            recreate_swapchain();
            return;
        }
    }

    /* Submit work chunk and present */
    update_data(n_swapchain_image);

    present_queue_ptr->submit(
        SubmitInfo::create(
            m_command_buffers[n_swapchain_image].get(),
            1, /* n_semaphores_to_signal */
            &curr_frame_signal_semaphore_ptr,
            1, /* n_semaphores_to_wait_on */
            &curr_frame_wait_semaphore_ptr,
            &wait_stage_mask,
            false /* should_block */)
    );

    {
        SwapchainOperationErrorCode present_result = SwapchainOperationErrorCode::DEVICE_LOST;

        present_queue_ptr->present(
            m_swapchain_ptr.get(),
            n_swapchain_image,
            1, /* n_wait_semaphores */
            &present_wait_semaphore_ptr,
            &present_result);

        if (present_result == SwapchainOperationErrorCode::OUT_OF_DATE
            || present_result == SwapchainOperationErrorCode::SUBOPTIMAL)
        {
            recreate_swapchain();
        }
        else if (present_result != SwapchainOperationErrorCode::SUCCESS)
        {
            throw runtime_error("failed to present swap chain image!");
        }
    }
}

void Engine::update_data(uint32_t in_n_swapchain_image)
{
    static auto lastTime = chrono::high_resolution_clock::now();
    auto currentTime = chrono::high_resolution_clock::now();
    float delta_time = chrono::duration<float, chrono::seconds::period>(currentTime - lastTime).count();
    lastTime = currentTime;

    #pragma region 相机移动
    if (m_key->IsPressed(KeyID::KEY_ID_FORWARD))
    {
        m_camera->ProcessKeyboard(FORWARD, delta_time);
    }
    if (m_key->IsPressed(KeyID::KEY_ID_BACKWARD))
    {
        m_camera->ProcessKeyboard(BACKWARD, delta_time);
    }
    if (m_key->IsPressed(KeyID::KEY_ID_LEFT))
    {
        m_camera->ProcessKeyboard(LEFT, delta_time);
    }
    if (m_key->IsPressed(KeyID::KEY_ID_RIGHT))
    {
        m_camera->ProcessKeyboard(RIGHT, delta_time);
    }
    #pragma endregion

    #pragma region 贴花设置
    //贴花尺寸
    if (m_key->IsPressed(KeyID::KEY_ID_ADD))
    {
        if (m_key->IsPressed(KeyID::KEY_ID_WIDTH))
        {
            m_appsettings.update(ParamType::DECAL_SCALE_X, Direction::UP, delta_time);
        }
        else if (m_key->IsPressed(KeyID::KEY_ID_HEIGHT))
        {
            m_appsettings.update(ParamType::DECAL_SCALE_Y, Direction::UP, delta_time);
        }
        else
        {
            m_appsettings.update(ParamType::DECAL_SCALE_X, Direction::UP, delta_time);
            m_appsettings.update(ParamType::DECAL_SCALE_Y, Direction::UP, delta_time);
        }
    }
    if (m_key->IsPressed(KeyID::KEY_ID_MINUS))
    {
        if (m_key->IsPressed(KeyID::KEY_ID_WIDTH))
        {
            m_appsettings.update(ParamType::DECAL_SCALE_X, Direction::DOWN, delta_time);
        }
        else if (m_key->IsPressed(KeyID::KEY_ID_HEIGHT))
        {
            m_appsettings.update(ParamType::DECAL_SCALE_Y, Direction::DOWN, delta_time);
        }
        else
        {
            m_appsettings.update(ParamType::DECAL_SCALE_X, Direction::DOWN, delta_time);
            m_appsettings.update(ParamType::DECAL_SCALE_Y, Direction::DOWN, delta_time);
        }
    }

    //贴花方向
    if (m_key->IsPressed(KeyID::KEY_ID_ANTICLOCKWIZE))
    {
        m_appsettings.update(ParamType::DECAL_ROTATION, Direction::DOWN, delta_time);
    }
    if (m_key->IsPressed(KeyID::KEY_ID_CLOCKWIZE))
    {
        m_appsettings.update(ParamType::DECAL_ROTATION, Direction::UP, delta_time);
    }

    //贴花属性
    if (m_key->IsPressed(KeyID::KEY_ID_UP))
    {
        if (m_key->IsPressed(KeyID::KEY_ID_F))
        {
            m_appsettings.update(ParamType::DECAL_ANGLE_FADE, Direction::UP, delta_time);
        }
        if (m_key->IsPressed(KeyID::KEY_ID_T))
        {
            m_appsettings.update(ParamType::DECAL_THICKNESS, Direction::UP, delta_time);
        }
        else if (m_key->IsPressed(KeyID::KEY_ID_I))
        {
            m_appsettings.update(ParamType::DECAL_INDENSITY, Direction::UP, delta_time);
        }
        else if (m_key->IsPressed(KeyID::KEY_ID_C))
        {
            m_appsettings.update(ParamType::DECAL_ALBEDO, Direction::UP, delta_time);
        }
    }
    if (m_key->IsPressed(KeyID::KEY_ID_DOWN))
    {

        if (m_key->IsPressed(KeyID::KEY_ID_F))
        {
            m_appsettings.update(ParamType::DECAL_ANGLE_FADE, Direction::DOWN, delta_time);
        }
        if (m_key->IsPressed(KeyID::KEY_ID_T))
        {
            m_appsettings.update(ParamType::DECAL_THICKNESS, Direction::DOWN, delta_time);
        }
        else if (m_key->IsPressed(KeyID::KEY_ID_I))
        {
            m_appsettings.update(ParamType::DECAL_INDENSITY, Direction::DOWN, delta_time);
        }
        else if (m_key->IsPressed(KeyID::KEY_ID_C))
        {
            m_appsettings.update(ParamType::DECAL_ALBEDO, Direction::DOWN, delta_time);
        }
    }

    //选择贴花
    for (int i = 0; i < N_DECALS; i++)
    {
        if (m_key->IsPressed((KeyID)(KeyID::KEY_ID_1 + i)))
        {
            m_appsettings.set_decal_id(i);
        }
    }
    if (m_key->IsPressed(KeyID::KEY_ID_TAB))
    {
        m_appsettings.tab_decal();
        m_key->SetReleased(KeyID::KEY_ID_TAB);
    }
    #pragma endregion

    #pragma region 写入动态uniform
    Queue* queue = m_device_ptr->get_universal_queue(0);

    MVPUniform mvp;
    mvp.model = scale(mat4(1.0f), vec3(0.01f, 0.01f, 0.01f));
    mvp.view = m_camera->GetViewMatrix();
    mvp.proj = m_camera->GetProjMatrix();
    m_mvp_dynamic_buffer_helper->update(queue, &mvp, in_n_swapchain_image);

    SunLightUniform sun_light;
    sun_light.SunDirectionWS = vec3(0.5f, 0.1f, 0.5f);
    sun_light.SunIrradiance = vec3(10.0f, 10.0f, 10.0f);
    m_sunLight_dynamic_buffer_helper->update(queue, &sun_light, in_n_swapchain_image);

    CameraUniform camera;
    camera.CameraPosWS = m_camera->GetCameraWorldPos();
    camera.InvViewProj = inverse(mvp.proj * mvp.view);
    m_camera_dynamic_buffer_helper->update(queue, &camera, in_n_swapchain_image);

    CursorDecal cursorDecal;
    uint decal_id = m_appsettings.get_decal_id();
    vec2 decal_size = m_model->get_texture_size(decal_id * 2);
    cursorDecal.size = vec3(
        decal_size.x * m_appsettings.getParam(ParamType::DECAL_SCALE_X),
        decal_size.y * m_appsettings.getParam(ParamType::DECAL_SCALE_Y),
        m_appsettings.getParam(ParamType::DECAL_THICKNESS));
    cursorDecal.albedoTexIdx = decal_id * 2;
    cursorDecal.normalTexIdx = decal_id * 2 + 1;
    cursorDecal.rotation = m_appsettings.getParam(ParamType::DECAL_ROTATION);
    cursorDecal.angle_fade = m_appsettings.getParam(ParamType::DECAL_ANGLE_FADE);
    cursorDecal.albedo = m_appsettings.getParam(ParamType::DECAL_ALBEDO);
    cursorDecal.intensity = m_appsettings.getParam(ParamType::DECAL_INDENSITY);
    m_cursor_decal_dynamic_buffer_helper->update(queue, &cursorDecal, in_n_swapchain_image);

    update_decal();
    m_decal_indices_dynamic_buffer_helper->update(queue, &m_indexUniform, in_n_swapchain_image);
    m_decal_ZBounds_dynamic_buffer_helper->update(queue, &m_zBoundsUniform, in_n_swapchain_image);
    #pragma endregion

    #pragma region 处理点击事件，存入贴花
    if (m_mouse->isClick())
    {
        PickingStorage pickingStorage;
        m_picking_storage_buffer_ptr->read(
            0,
            sizeof(PickingStorage),
            &pickingStorage,
            queue);
        m_decals[(m_n_decal++) % N_MAX_STORED_DECALS] = Decal(pickingStorage.Position, pickingStorage.Normal, cursorDecal);
        m_decals_uniform_buffer_ptr->write(
            0, /* start_offset */
            m_decals_buffer_size,
            m_decals,
            m_device_ptr->get_universal_queue(0));

        update_decal();
        m_decal_indices_dynamic_buffer_helper->update(queue, &m_indexUniform, in_n_swapchain_image);
        m_decal_ZBounds_dynamic_buffer_helper->update(queue, &m_zBoundsUniform, in_n_swapchain_image);

        for (uint32_t n_swapchain_image = 0; n_swapchain_image < N_SWAPCHAIN_IMAGES; ++n_swapchain_image)
        {
            m_command_buffers[n_swapchain_image].reset();
        }
        init_command_buffers();

        m_mouse->release();
    }
    #pragma endregion
}

void Engine::update_decal()
{
    #pragma region nearClip包围盒
    BoundingOrientedBox nearClipBox;
    mat4 invProjection = inverse(m_camera->GetProjMatrix());
    vec4 nearTopRight_t = invProjection * vec4(1.0f, -1.0f, 0.0f, 1.0f);
    vec3 nearTopRight = vec3(nearTopRight_t.x, nearTopRight_t.y, nearTopRight_t.z) / nearTopRight_t.w;
    nearClipBox.Center = m_camera->GetNearZCenterWorldPos();
    nearClipBox.Extents = vec3(nearTopRight.x, nearTopRight.y, 0.01f);
    nearClipBox.Orientation = m_camera->GetCameraWorldOrientation();
    #pragma endregion

    const vec3 boxVerts[8] = { vec3(-1,  1, -1), vec3(1,  1, -1), vec3(-1,  1, 1), vec3(1,  1, 1),
                               vec3(-1, -1, -1), vec3(1, -1, -1), vec3(-1, -1, 1), vec3(1, -1, 1) };
    const float zRange = m_camera->GetFarZ() - m_camera->GetNearZ();
    const int numDecalsToUpdate = std::min(m_n_decal, N_MAX_STORED_DECALS);
    bool intersectsCamera[N_MAX_STORED_DECALS] = { };
    for (uint decalIdx = 0; decalIdx < numDecalsToUpdate; ++decalIdx)
    {
        #pragma region 计算贴花盒的方向
        const Decal& decal = m_decals[decalIdx];
        vec3 forward = -decal.normal;
        vec3 up = abs(dot(forward, vec3(0.0f, 1.0f, 0.0f))) < 0.99f ? vec3(0.0f, 1.0f, 0.0f) : vec3(0.0f, 0.0f, 1.0f);
        vec3 right = normalize(cross(up, forward));
        up = cross(forward, right);
        mat3 decalOrientation(right, up, forward);
        #pragma endregion

        #pragma region 计算贴花z范围
        float minZ = std::numeric_limits<float>::max();
        float maxZ = -std::numeric_limits<float>::max();
        for (uint i = 0; i < 8; ++i)
        {
            vec3 boxVert = boxVerts[i] * decal.size;
            boxVert = decalOrientation * boxVert;
            boxVert += decal.position;

            vec4 vert = m_camera->GetViewMatrix() * vec4(boxVert, 1.0f);
            vert = vert / vert.w;
            float vertZ = vert.z;
            minZ = std::min(minZ, vertZ);
            maxZ = std::max(maxZ, vertZ);
        }
        minZ = clamp((minZ - m_camera->GetNearZ()) / zRange, 0.0f, 1.0f);
        maxZ = clamp((maxZ - m_camera->GetNearZ()) / zRange, 0.0f, 1.0f);
        uint minZTile = uint(minZ * NUM_Z_TILES);
        uint maxZTile = std::min(int(maxZ * NUM_Z_TILES), NUM_Z_TILES - 1);
        m_zBoundsUniform.ZBpunds[decalIdx] = uvec2(uint32(minZTile), uint32(maxZTile));
        #pragma endregion

        #pragma region 贴花盒与近平面包围盒碰撞检测
        BoundingOrientedBox decalBox;
        decalBox.Center = decal.position;
        decalBox.Extents = decal.size;
        decalBox.Orientation = decalOrientation;
        intersectsCamera[decalIdx] = Intersects(nearClipBox, decalBox);
        #pragma endregion
    }

    #pragma region 根据碰撞检测结果将贴花分为两类
    for (uint64 decalIdx = 0; decalIdx < numDecalsToUpdate; ++decalIdx)
        if (intersectsCamera[decalIdx])
            m_indexUniform.decalIndices[m_indexUniform.numIntersectingDecals++] = uint32(decalIdx);

    uint64 offset = m_indexUniform.numIntersectingDecals;
    for (uint64 decalIdx = 0; decalIdx < numDecalsToUpdate; ++decalIdx)
        if (intersectsCamera[decalIdx] == false)
            m_indexUniform.decalIndices[offset++] = uint32(decalIdx);
    #pragma endregion
}

void Engine::mouse_move_callback(CallbackArgument* argumentPtr)
{
    double mouse_x_pos = reinterpret_cast<OnMouseMoveCallbackArgument*>(argumentPtr)->mouse_x_pos;
    double mouse_y_pos = reinterpret_cast<OnMouseMoveCallbackArgument*>(argumentPtr)->mouse_y_pos;

    static double lastX = mouse_x_pos;
    static double lastY = mouse_y_pos;

    if (mouse_x_pos > m_width * 0.8 || mouse_x_pos < m_width * 0.2 ||
        mouse_y_pos > m_height * 0.8 || mouse_y_pos < m_height * 0.2)
    {
        SetCursorPos(m_width / 2, m_height / 2);
        lastX = m_width / 2;
        lastY = m_height / 2;
        return;
    }


    float x_offset = mouse_x_pos - lastX;
    float y_offset = lastY - mouse_y_pos;

    lastX = mouse_x_pos;
    lastY = mouse_y_pos;

    Camera::Active()->ProcessMouseMovement(x_offset, y_offset);



}

void Engine::mouse_click_callback(CallbackArgument* argumentPtr)
{
    m_mouse->click();
}

void Engine::scroll_callback(CallbackArgument* argumentPtr)
{
    Camera::Active()->ProcessMouseScroll(reinterpret_cast<OnMouseWheelCallbackArgument*>(argumentPtr)->wheel_offset/120.0);
}

void Engine::key_press_callback(CallbackArgument* argumentPtr)
{
    m_key->SetPressed(reinterpret_cast<OnKeyCallbackArgument*>(argumentPtr)->key_id);
}

void Engine::key_release_callback(CallbackArgument* argumentPtr)
{
    m_key->SetReleased(reinterpret_cast<OnKeyCallbackArgument*>(argumentPtr)->key_id);
}
#pragma endregion

#pragma region 卸载
Engine::~Engine()
{
    deinit();
}

void Engine::cleanup_swapwhain()
{
    auto gfx_pipeline_manager_ptr = m_device_ptr->get_graphics_pipeline_manager();
    Vulkan::vkDeviceWaitIdle(m_device_ptr->get_device_vk());
    
    m_depth_image_view_ptr.reset();
    m_depth_image_view2_ptr.reset();
    m_tangent_frame_image_view_ptr.reset();
    m_uv_and_depth_gradient_image_view_ptr.reset();
    m_uv_gradient_image_view_ptr.reset();
    m_material_id_image_view_ptr.reset();

    m_depth_image_ptr.reset();
    m_depth_image2_ptr.reset();
    m_tangent_frame_image_ptr.reset();
    m_uv_and_depth_gradient_image_ptr.reset();
    m_uv_gradient_image_ptr.reset();
    m_material_id_image_ptr.reset();

    for (uint32_t n_swapchain_image = 0; n_swapchain_image < N_SWAPCHAIN_IMAGES; ++n_swapchain_image)
    {
        m_command_buffers[n_swapchain_image].reset();
    }
    
    m_fbo.reset();

    if (m_GBuffer_gfx_pipeline_id != UINT32_MAX)
    {
        auto gfx_pipeline_manager_ptr = m_device_ptr->get_graphics_pipeline_manager();
        gfx_pipeline_manager_ptr->delete_pipeline(m_GBuffer_gfx_pipeline_id);
        m_GBuffer_gfx_pipeline_id = UINT32_MAX;
    }

    if (m_deferred_compute_pipeline_id != UINT32_MAX)
    {
        auto compute_pipeline_manager_ptr(m_device_ptr->get_compute_pipeline_manager());
        compute_pipeline_manager_ptr->delete_pipeline(m_deferred_compute_pipeline_id);
        m_deferred_compute_pipeline_id = UINT32_MAX;
    }
    
    m_renderpass_ptr.reset();
    m_swapchain_ptr.reset();

    m_dsg_ptr.reset();
}

void Engine::deinit()
{
    cleanup_swapwhain();

    m_sampler.reset();

    m_frame_signal_semaphores.clear();
    m_frame_wait_semaphores.clear();

    m_rendering_surface_ptr.reset();
    
    m_texture_indices_uniform_buffer_ptr.reset();

    delete m_mvp_dynamic_buffer_helper;
    delete m_sunLight_dynamic_buffer_helper;
    delete m_camera_dynamic_buffer_helper;
    delete m_cursor_decal_dynamic_buffer_helper;
    delete m_decal_indices_dynamic_buffer_helper;
    delete m_decal_ZBounds_dynamic_buffer_helper;

    m_decals_uniform_buffer_ptr.reset();
    m_picking_storage_buffer_ptr.reset();
    m_box_vertex_buffer_ptr.reset();
    m_box_index_buffer_ptr.reset();
    m_cluster_storage_buffer_ptr.reset();

    m_cluster_vs_ptr.reset();
    m_cluster_fs_ptr.reset();
    m_GBuffer_vs_ptr.reset();
    m_GBuffer_fs_ptr.reset();
    m_picking_cs_ptr.reset();
    m_deferred_cs_ptr.reset();

    m_model.reset();

    m_device_ptr.reset();
    m_instance_ptr.reset();

    m_window_ptr.reset();
}
#pragma endregion

#pragma region 工具
ShaderModuleStageEntryPoint* Engine::create_shader(string file, ShaderStage type, string name)
{
    GLSLShaderToSPIRVGeneratorUniquePtr shader_ptr;
    ShaderModuleUniquePtr               shader_module_ptr;

    shader_ptr = GLSLShaderToSPIRVGenerator::create(
        m_device_ptr.get(),
        GLSLShaderToSPIRVGenerator::MODE_LOAD_SOURCE_FROM_FILE,
        file,
        type);

    shader_module_ptr = ShaderModule::create_from_spirv_generator(
        m_device_ptr.get(),
        shader_ptr.get());

    shader_module_ptr->set_name(name + "shader module");

   return new ShaderModuleStageEntryPoint(
        "main",
        move(shader_module_ptr),
        type);
}

void Engine::create_image_source(ImageUniquePtr& image, ImageViewUniquePtr& image_view, string name, Format format, bool isDepthImage)
{
    auto allocator_ptr = MemoryAllocator::create_oneshot(m_device_ptr.get());

    ImageUsageFlagBits usage = isDepthImage ? ImageUsageFlagBits::DEPTH_STENCIL_ATTACHMENT_BIT : ImageUsageFlagBits::COLOR_ATTACHMENT_BIT;
    ImageAspectFlagBits aspect = isDepthImage ? ImageAspectFlagBits::DEPTH_BIT : ImageAspectFlagBits::COLOR_BIT;
    ImageLayout layout = isDepthImage ? ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL : ImageLayout::SHADER_READ_ONLY_OPTIMAL;
    
    auto image_create_info_ptr = ImageCreateInfo::create_no_alloc(
        m_device_ptr.get(),
        ImageType::_2D,
        format,
        ImageTiling::OPTIMAL,
        usage | ImageUsageFlagBits::SAMPLED_BIT,
        m_width,
        m_height,
        1,
        1,
        SampleCountFlagBits::_1_BIT,
        QueueFamilyFlagBits::COMPUTE_BIT | QueueFamilyFlagBits::GRAPHICS_BIT,
        SharingMode::EXCLUSIVE,
        false,
        ImageCreateFlagBits::NONE,
        layout);

    image = Image::create(move(image_create_info_ptr));
    image->set_name(name + " Image");

    allocator_ptr->add_image_whole(
        image.get(),
        MemoryFeatureFlagBits::DEVICE_LOCAL_BIT);

    auto image_view_create_info_ptr = ImageViewCreateInfo::create_2D(
        m_device_ptr.get(),
        image.get(),
        0,
        0,
        1,
        aspect,
        format,
        ComponentSwizzle::R,
        ComponentSwizzle::G,
        ComponentSwizzle::B,
        ComponentSwizzle::A);

    image_view = ImageView::create(move(image_view_create_info_ptr));
}

void Engine::create_cluster_pipeline(GraphicsPipelineManager* gfxPipelineManager, uint mode)
{
    GraphicsPipelineCreateInfoUniquePtr gfx_pipeline_create_info_ptr;

    gfx_pipeline_create_info_ptr = GraphicsPipelineCreateInfo::create(
        PipelineCreateFlagBits::NONE,
        m_renderpass_ptr.get(),
        m_render_pass_subpass_cluster_id[mode],
        *m_cluster_vs_ptr,
        ShaderModuleStageEntryPoint(), /* in_geometry_shader        */
        ShaderModuleStageEntryPoint(), /* in_tess_control_shader    */
        ShaderModuleStageEntryPoint(), /* in_tess_evaluation_shader */
        *m_cluster_fs_ptr);

    vector<const DescriptorSetCreateInfo*> m_desc_create_info;
    m_desc_create_info.push_back(m_dsg_ptr->get_descriptor_set_create_info(1));
    m_desc_create_info.push_back(m_dsg_ptr->get_descriptor_set_create_info(5 + N_SWAPCHAIN_IMAGES));
    m_desc_create_info.push_back(m_dsg_ptr->get_descriptor_set_create_info(6 + N_SWAPCHAIN_IMAGES));
    m_desc_create_info.push_back(m_dsg_ptr->get_descriptor_set_create_info(7 + N_SWAPCHAIN_IMAGES));
    gfx_pipeline_create_info_ptr->set_descriptor_set_create_info(&m_desc_create_info);

    switch (mode)
    {
    case 0:
    case 1:
        gfx_pipeline_create_info_ptr->set_rasterization_properties(
            PolygonMode::FILL,
            CullModeFlagBits::FRONT_BIT,
            FrontFace::CLOCKWISE,
            1.0f); /* in_line_width       */
        break;
    case 2:
        gfx_pipeline_create_info_ptr->set_rasterization_properties(
            PolygonMode::FILL,
            CullModeFlagBits::BACK_BIT,
            FrontFace::CLOCKWISE,
            1.0f); /* in_line_width       */
        break;
    }

    gfx_pipeline_create_info_ptr->toggle_depth_test(false, CompareOp::LESS);
    gfx_pipeline_create_info_ptr->toggle_depth_writes(false);

    gfx_pipeline_create_info_ptr->add_vertex_binding(
        0, /* in_binding */
        VertexInputRate::VERTEX,
        sizeof(VertexOnlyPos),
        VertexOnlyPos::getVertexInputAttribute().size(), /* in_n_attributes */
        VertexOnlyPos::getVertexInputAttribute().data());

    int32 num_decals = N_MAX_STORED_DECALS;
    float near_clip = m_camera->GetNearZ();
    float far_clip = m_camera->GetFarZ();
    uint num_x_tiles = NUM_X_TILES;
    uint num_y_tiles = NUM_Y_TILES;
    uint num_z_tiles = NUM_Z_TILES;
    uint elements_per_cluster = (N_MAX_STORED_DECALS + 31) / 32;
    gfx_pipeline_create_info_ptr->add_specialization_constant(ShaderStage::VERTEX, 0, 4, &num_decals);
    gfx_pipeline_create_info_ptr->add_specialization_constant(ShaderStage::FRAGMENT, 0, 4, &num_decals);
    gfx_pipeline_create_info_ptr->add_specialization_constant(ShaderStage::FRAGMENT, 1, 4, &near_clip);
    gfx_pipeline_create_info_ptr->add_specialization_constant(ShaderStage::FRAGMENT, 2, 4, &far_clip);
    gfx_pipeline_create_info_ptr->add_specialization_constant(ShaderStage::FRAGMENT, 3, 4, &num_x_tiles);
    gfx_pipeline_create_info_ptr->add_specialization_constant(ShaderStage::FRAGMENT, 4, 4, &num_y_tiles);
    gfx_pipeline_create_info_ptr->add_specialization_constant(ShaderStage::FRAGMENT, 5, 4, &num_z_tiles);
    gfx_pipeline_create_info_ptr->add_specialization_constant(ShaderStage::FRAGMENT, 6, 4, &elements_per_cluster);
    gfx_pipeline_create_info_ptr->add_specialization_constant(ShaderStage::FRAGMENT, 7, 4, &mode);

    gfxPipelineManager->add_pipeline(
        move(gfx_pipeline_create_info_ptr),
        &m_cluster_gfx_pipeline_id[mode]);
}

void Engine::cluster(PrimaryCommandBuffer* cmd_buffer_ptr, uint mode, uint n_command_buffer)
{
    cmd_buffer_ptr->record_next_subpass(SubpassContents::INLINE);

    cmd_buffer_ptr->record_bind_pipeline(
        PipelineBindPoint::GRAPHICS,
        m_cluster_gfx_pipeline_id[mode]);

    const uint32_t data_ub_offset[3] = {
        static_cast<uint32_t>(m_mvp_dynamic_buffer_helper->getSizePerSwapchainImage() * n_command_buffer),
        static_cast<uint32_t>(m_decal_indices_dynamic_buffer_helper->getSizePerSwapchainImage() * n_command_buffer),
        static_cast<uint32_t>(m_decal_ZBounds_dynamic_buffer_helper->getSizePerSwapchainImage() * n_command_buffer)
    };
    DescriptorSet* ds_ptr[4] = {
        m_dsg_ptr->get_descriptor_set(1),
        m_dsg_ptr->get_descriptor_set(5 + N_SWAPCHAIN_IMAGES),
        m_dsg_ptr->get_descriptor_set(6 + N_SWAPCHAIN_IMAGES),
        m_dsg_ptr->get_descriptor_set(7 + N_SWAPCHAIN_IMAGES)
    };

    cmd_buffer_ptr->record_bind_descriptor_sets(
        PipelineBindPoint::GRAPHICS,
        getPineLine(mode + 1),
        0, /* firstSet */
        4, /* setCount：传入的描述符集与shader中的set一一对应 */
        ds_ptr,
        3,                /* dynamicOffsetCount */
        data_ub_offset); /* pDynamicOffsets    */

    Buffer* buffer_raw_ptrs[] = { m_box_vertex_buffer_ptr.get() };
    const VkDeviceSize buffer_offsets[] = { 0 };
    cmd_buffer_ptr->record_bind_vertex_buffers(
        0, /* start_binding */
        1, /* binding_count */
        buffer_raw_ptrs,
        buffer_offsets);

    cmd_buffer_ptr->record_bind_index_buffer(
        m_box_index_buffer_ptr.get(),
        0,
        Anvil::IndexType::UINT16);

    uint num = 0;
    switch(mode)
    {
    case 0:
        num = m_indexUniform.numIntersectingDecals;
        break;
    case 1:
    case 2:
        num = std::min(m_n_decal, N_MAX_STORED_DECALS) - m_indexUniform.numIntersectingDecals;
    }
    cmd_buffer_ptr->record_draw_indexed(
        36,
        num,
        0,
        0,
        0);
}

void Engine::make_box(float scale)
{
    auto allocator_ptr = MemoryAllocator::create_oneshot(Engine::Instance()->getDevice());

    #pragma region 顶点数据
    array<vec3, 24> boxVerts;
    uint64 vIdx = 0;

    // Top
    boxVerts[vIdx++] = vec3(-0.5f, 0.5f, 0.5f) * scale;
    boxVerts[vIdx++] = vec3(0.5f, 0.5f, 0.5f) * scale;
    boxVerts[vIdx++] = vec3(0.5f, 0.5f, -0.5f) * scale;
    boxVerts[vIdx++] = vec3(-0.5f, 0.5f, -0.5f) * scale;

    // Bottom
    boxVerts[vIdx++] = vec3(-0.5f, -0.5f, -0.5f) * scale;
    boxVerts[vIdx++] = vec3(0.5f, -0.5f, -0.5f) * scale;
    boxVerts[vIdx++] = vec3(0.5f, -0.5f, 0.5f) * scale;
    boxVerts[vIdx++] = vec3(-0.5f, -0.5f, 0.5f) * scale;

    // Front
    boxVerts[vIdx++] = vec3(-0.5f, 0.5f, -0.5f) * scale;
    boxVerts[vIdx++] = vec3(0.5f, 0.5f, -0.5f) * scale;
    boxVerts[vIdx++] = vec3(0.5f, -0.5f, -0.5f) * scale;
    boxVerts[vIdx++] = vec3(-0.5f, -0.5f, -0.5f) * scale;

    // Back
    boxVerts[vIdx++] = vec3(0.5f, 0.5f, 0.5f) * scale;
    boxVerts[vIdx++] = vec3(-0.5f, 0.5f, 0.5f) * scale;
    boxVerts[vIdx++] = vec3(-0.5f, -0.5f, 0.5f) * scale;
    boxVerts[vIdx++] = vec3(0.5f, -0.5f, 0.5f) * scale;

    // Left
    boxVerts[vIdx++] = vec3(-0.5f, 0.5f, 0.5f) * scale;
    boxVerts[vIdx++] = vec3(-0.5f, 0.5f, -0.5f) * scale;
    boxVerts[vIdx++] = vec3(-0.5f, -0.5f, -0.5f) * scale;
    boxVerts[vIdx++] = vec3(-0.5f, -0.5f, 0.5f) * scale;

    // Right
    boxVerts[vIdx++] = vec3(0.5f, 0.5f, -0.5f) * scale;
    boxVerts[vIdx++] = vec3(0.5f, 0.5f, 0.5f) * scale;
    boxVerts[vIdx++] = vec3(0.5f, -0.5f, 0.5f) * scale;
    boxVerts[vIdx++] = vec3(0.5f, -0.5f, -0.5f) * scale;

    VkDeviceSize vertex_buffer_size = sizeof(boxVerts[0]) * boxVerts.size();
    auto vertex_buffer_create_info_ptr = BufferCreateInfo::create_no_alloc(
        Engine::Instance()->getDevice(),
        vertex_buffer_size,
        QueueFamilyFlagBits::GRAPHICS_BIT,
        SharingMode::EXCLUSIVE,
        BufferCreateFlagBits::NONE,
        BufferUsageFlagBits::VERTEX_BUFFER_BIT);
    m_box_vertex_buffer_ptr = Buffer::create(move(vertex_buffer_create_info_ptr));
    m_box_vertex_buffer_ptr->set_name_formatted("Box Vertices buffer");

    allocator_ptr->add_buffer(
        m_box_vertex_buffer_ptr.get(),
        MemoryFeatureFlagBits::NONE); /* in_required_memory_features */
    #pragma endregion

    #pragma region 索引数据
    array<uint16, 36> boxIndices;
    uint64 iIdx = 0;

    // Top
    boxIndices[iIdx++] = 0;
    boxIndices[iIdx++] = 1;
    boxIndices[iIdx++] = 2;
    boxIndices[iIdx++] = 2;
    boxIndices[iIdx++] = 3;
    boxIndices[iIdx++] = 0;

    // Bottom
    boxIndices[iIdx++] = 4 + 0;
    boxIndices[iIdx++] = 4 + 1;
    boxIndices[iIdx++] = 4 + 2;
    boxIndices[iIdx++] = 4 + 2;
    boxIndices[iIdx++] = 4 + 3;
    boxIndices[iIdx++] = 4 + 0;

    // Front
    boxIndices[iIdx++] = 8 + 0;
    boxIndices[iIdx++] = 8 + 1;
    boxIndices[iIdx++] = 8 + 2;
    boxIndices[iIdx++] = 8 + 2;
    boxIndices[iIdx++] = 8 + 3;
    boxIndices[iIdx++] = 8 + 0;

    // Back
    boxIndices[iIdx++] = 12 + 0;
    boxIndices[iIdx++] = 12 + 1;
    boxIndices[iIdx++] = 12 + 2;
    boxIndices[iIdx++] = 12 + 2;
    boxIndices[iIdx++] = 12 + 3;
    boxIndices[iIdx++] = 12 + 0;

    // Left
    boxIndices[iIdx++] = 16 + 0;
    boxIndices[iIdx++] = 16 + 1;
    boxIndices[iIdx++] = 16 + 2;
    boxIndices[iIdx++] = 16 + 2;
    boxIndices[iIdx++] = 16 + 3;
    boxIndices[iIdx++] = 16 + 0;

    // Right
    boxIndices[iIdx++] = 20 + 0;
    boxIndices[iIdx++] = 20 + 1;
    boxIndices[iIdx++] = 20 + 2;
    boxIndices[iIdx++] = 20 + 2;
    boxIndices[iIdx++] = 20 + 3;
    boxIndices[iIdx++] = 20 + 0;

    VkDeviceSize index_buffer_size = sizeof(boxIndices[0]) * boxIndices.size();
    auto index_buffer_create_info_ptr = BufferCreateInfo::create_no_alloc(
        Engine::Instance()->getDevice(),
        index_buffer_size,
        QueueFamilyFlagBits::GRAPHICS_BIT,
        SharingMode::EXCLUSIVE,
        BufferCreateFlagBits::NONE,
        BufferUsageFlagBits::INDEX_BUFFER_BIT);
    m_box_index_buffer_ptr = Buffer::create(move(index_buffer_create_info_ptr));
    m_box_index_buffer_ptr->set_name_formatted("Box Indices buffer");

    allocator_ptr->add_buffer(
        m_box_index_buffer_ptr.get(),
        MemoryFeatureFlagBits::NONE); /* in_required_memory_features */
    #pragma endregion

    #pragma region 写入缓冲数据
    m_box_vertex_buffer_ptr->write(
        0, /* start_offset */
        vertex_buffer_size,
        boxVerts.data());

    m_box_index_buffer_ptr->write(
        0, /* start_offset */
        index_buffer_size,
        boxIndices.data());
    #pragma endregion
}

void Engine::on_validation_callback(DebugMessageSeverityFlags in_severity, const char* in_message_ptr)
{
    if ((in_severity & Anvil::DebugMessageSeverityFlagBits::ERROR_BIT) != 0)
    {
        fprintf(stderr,
            "[!] %s\n\n",
            in_message_ptr);
    }
}

Format Engine::SelectSupportedFormat(
    const vector<Format>& candidates,
    ImageTiling tiling,
    FormatFeatureFlags features)
{
    SGPUDevice* device_ptr(reinterpret_cast<SGPUDevice*>(m_device_ptr.get()));
    for (Format format : candidates)
    {
        FormatProperties props = device_ptr->get_physical_device_format_properties(format);
        if (tiling == ImageTiling::LINEAR && (props.linear_tiling_capabilities & features) == features)
        {
            return format;
        }
        else if (tiling == ImageTiling::OPTIMAL && (props.optimal_tiling_capabilities & features) == features)
        {
            return format;
        }
    }
    throw runtime_error("failed to find supported format!");
}

bool Engine::Intersects(const BoundingOrientedBox& boxA, const BoundingOrientedBox& boxB)
{
    // Build the 3x3 rotation matrix that defines the orientation of B relative to A.
    mat3 R = transpose(boxA.Orientation) * boxB.Orientation;

    // Compute the translation of B relative to A.
    vec3 t = transpose(boxA.Orientation) * (boxB.Center - boxA.Center);

    //
    // h(A) = extents of A.
    // h(B) = extents of B.
    //
    // a(u) = axes of A = (1,0,0), (0,1,0), (0,0,1)
    // b(u) = axes of B relative to A = (r00,r10,r20), (r01,r11,r21), (r02,r12,r22)
    //  
    // For each possible separating axis l:
    //   d(A) = sum (for i = u,v,w) h(A)(i) * abs( a(i) dot l )
    //   d(B) = sum (for i = u,v,w) h(B)(i) * abs( b(i) dot l )
    //   if abs( t dot l ) > d(A) + d(B) then disjoint
    //

    // Load extents of A and B.
    vec3 h_A = boxA.Extents;
    vec3 h_B = boxB.Extents;

    // Absolute value of R.
    mat3 AR = { abs(R[0]), abs(R[1]), abs(R[2]) };

    // Test each of the 15 possible seperating axii.
    float d, d_A, d_B;

    // l = a(u) = (1, 0, 0)
    // t dot l = t.x
    // d(A) = h(A).x
    // d(B) = h(B) dot abs(r00, r01, r02)
    d = t.x;
    d_A = h_A.x;
    d_B = dot(h_B, vec3(AR[0][0], AR[1][0], AR[2][0]));
    if(abs(d) > d_A + d_B) return false;

    // l = a(v) = (0, 1, 0)
    // t dot l = t.y
    // d(A) = h(A).y
    // d(B) = h(B) dot abs(r10, r11, r12)
    d = t.y;
    d_A = h_A.y;
    d_B = dot(h_B, vec3(AR[0][1], AR[1][1], AR[2][1]));
    if (abs(d) > d_A + d_B) return false;

    // l = a(w) = (0, 0, 1)
    // t dot l = t.z
    // d(A) = h(A).z
    // d(B) = h(B) dot abs(r20, r21, r22)
    d = t.z;
    d_A = h_A.z;
    d_B = dot(h_B, vec3(AR[0][2], AR[1][2], AR[2][2]));
    if (abs(d) > d_A + d_B) return false;

    // l = b(u) = (r00, r10, r20)
    // d(A) = h(A) dot abs(r00, r10, r20)
    // d(B) = h(B).x
    d = dot(t, R[0]);
    d_A = dot(h_A, AR[0]);
    d_B = h_B.x;
    if (abs(d) > d_A + d_B) return false;

    // l = b(v) = (r01, r11, r21)
    // d(A) = h(A) dot abs(r01, r11, r21)
    // d(B) = h(B).y
    d = dot(t, R[1]);
    d_A = dot(h_A, AR[1]);
    d_B = h_B.y;
    if (abs(d) > d_A + d_B) return false;

    // l = b(w) = (r02, r12, r22)
    // d(A) = h(A) dot abs(r02, r12, r22)
    // d(B) = h(B).z
    d = dot(t, R[2]);
    d_A = dot(h_A, AR[2]);
    d_B = h_B.z;
    if (abs(d) > d_A + d_B) return false;

    // l = a(u) x b(u) = (0, -r20, r10)
    // d(A) = h(A) dot abs(0, r20, r10)
    // d(B) = h(B) dot abs(0, r02, r01)
    d = dot(t, vec3(0, -R[0][2], R[0][1]));
    d_A = dot(h_A, vec3(0, AR[0][2], AR[0][1]));
    d_B = dot(h_B, vec3(0, AR[2][0], AR[1][0]));
    if (abs(d) > d_A + d_B) return false;

    // l = a(u) x b(v) = (0, -r21, r11)
    // d(A) = h(A) dot abs(0, r21, r11)
    // d(B) = h(B) dot abs(r02, 0, r00)
    d = dot(t, vec3(0, -R[1][2], R[1][1]));
    d_A = dot(h_A, vec3(0, AR[1][2], AR[1][1]));
    d_B = dot(h_B, vec3(AR[2][0], 0, AR[0][0]));
    if (abs(d) > d_A + d_B) return false;

    // l = a(u) x b(w) = (0, -r22, r12)
    // d(A) = h(A) dot abs(0, r22, r12)
    // d(B) = h(B) dot abs(r01, r00, 0)
    d = dot(t, vec3(0, -R[2][2], R[2][1]));
    d_A = dot(h_A, vec3(0, AR[2][2], AR[2][1]));
    d_B = dot(h_B, vec3(AR[1][0], AR[0][0], 0));
    if (abs(d) > d_A + d_B) return false;

    // l = a(v) x b(u) = (r20, 0, -r00)
    // d(A) = h(A) dot abs(r20, 0, r00)
    // d(B) = h(B) dot abs(0, r12, r11)
    d = dot(t, vec3(R[0][2], 0, -R[0][0]));
    d_A = dot(h_A, vec3(AR[0][2], 0, AR[0][0]));
    d_B = dot(h_B, vec3(0, AR[2][1], AR[1][1]));
    if (abs(d) > d_A + d_B) return false;

    // l = a(v) x b(v) = (r21, 0, -r01)
    // d(A) = h(A) dot abs(r21, 0, r01)
    // d(B) = h(B) dot abs(r12, 0, r10)
    d = dot(t, vec3(R[1][2], 0, -R[1][0]));
    d_A = dot(h_A, vec3(AR[1][2], 0, AR[1][0]));
    d_B = dot(h_B, vec3(AR[2][1], 0, AR[0][1]));
    if (abs(d) > d_A + d_B) return false;

    // l = a(v) x b(w) = (r22, 0, -r02)
    // d(A) = h(A) dot abs(r22, 0, r02)
    // d(B) = h(B) dot abs(r11, r10, 0)
    d = dot(t, vec3(R[2][2], 0, -R[2][0]));
    d_A = dot(h_A, vec3(AR[2][2], 0, AR[2][0]));
    d_B = dot(h_B, vec3(AR[1][1], AR[0][1], 0));
    if (abs(d) > d_A + d_B) return false;

    // l = a(w) x b(u) = (-r10, r00, 0)
    // d(A) = h(A) dot abs(r10, r00, 0)
    // d(B) = h(B) dot abs(0, r22, r21)
    d = dot(t, vec3(-R[0][1], R[0][0], 0));
    d_A = dot(h_A, vec3(AR[0][1], AR[0][0], 0));
    d_B = dot(h_B, vec3(0, AR[2][2], AR[1][2]));
    if (abs(d) > d_A + d_B) return false;

    // l = a(w) x b(v) = (-r11, r01, 0)
    // d(A) = h(A) dot abs(r11, r01, 0)
    // d(B) = h(B) dot abs(r22, 0, r20)
    d = dot(t, vec3(-R[1][1], R[1][0], 0));
    d_A = dot(h_A, vec3(AR[1][1], AR[1][0], 0));
    d_B = dot(h_B, vec3(AR[2][2], 0, AR[0][2]));
    if (abs(d) > d_A + d_B) return false;

    // l = a(w) x b(w) = (-r12, r02, 0)
    // d(A) = h(A) dot abs(r12, r02, 0)
    // d(B) = h(B) dot abs(r21, r20, 0)
    d = dot(t, vec3(-R[2][1], R[2][0], 0));
    d_A = dot(h_A, vec3(AR[2][1], AR[2][0], 0));
    d_B = dot(h_B, vec3(AR[1][2], AR[0][2], 0));
    if (abs(d) > d_A + d_B) return false;

    // No seperating axis found, boxes must intersect.
    return true;
}
#pragma endregion