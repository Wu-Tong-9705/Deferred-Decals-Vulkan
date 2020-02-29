#include "stdafx.h"
#include "engine.h"

#define APP_NAME      "Deferred Decals App"

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

PipelineLayout* Engine::getPineLine()
{
    auto gfx_pipeline_manager_ptr(m_device_ptr->get_graphics_pipeline_manager());
    return gfx_pipeline_manager_ptr->get_pipeline_layout(m_pipeline_id);
}

vector<DescriptorSet::CombinedImageSamplerBindingElement>* Engine::getCombinedImageSamplers()
{
    return &m_combined_image_samplers;
}

float Engine::getAspect()
{
    return (float)m_width / m_height;
}
#pragma endregion

#pragma region 初始化
Engine::Engine()
    :m_n_last_semaphore_used           (0),
     m_n_swapchain_images              (N_SWAPCHAIN_IMAGES),
     m_ub_data_size_per_swapchain_image(0),
     m_is_full_screen                  (false),
     m_width                           (1280),
     m_height                          (720)
{
    // ..
}

void Engine::init()
{
    m_camera = make_shared<Camera>(vec3(0.0f, 0.0f, 3.0f));
    m_camera->SetAsActive();
    m_key = make_shared<Key>();
    
    
    init_vulkan();
    init_window();
    init_swapchain();

    m_model = make_shared<Model>("assets/models/girls/3/girl.obj");

    init_buffers();
    init_dsgs();

    init_render_pass();
    init_shaders();
    init_gfx_pipelines();

    init_image();
    init_image_view();
    init_framebuffers();
    init_command_buffers();

    init_semaphores();


}

void Engine::init_vulkan()
{
    /* Create a Vulkan instance */
    {
        auto create_info_ptr = InstanceCreateInfo::create(APP_NAME,  /* in_app_name    */
            APP_NAME,  /* in_engine_name */
            #ifdef ENABLE_VALIDATION
            bind(
                &Engine::on_validation_callback,
                this,
                placeholders::_1,
                placeholders::_2),
            #else
            DebugCallbackFunction(),
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
        bind(&Engine::mouse_callback,
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
    Anvil::SGPUDevice* device_ptr(reinterpret_cast<Anvil::SGPUDevice*>(m_device_ptr.get()));

    {
        auto create_info_ptr = Anvil::RenderingSurfaceCreateInfo::create(m_instance_ptr.get(),
            m_device_ptr.get(),
            m_window_ptr.get());

        m_rendering_surface_ptr = Anvil::RenderingSurface::create(std::move(create_info_ptr));
    }

    m_rendering_surface_ptr->set_name("Main rendering surface");


    m_swapchain_ptr = device_ptr->create_swapchain(
        m_rendering_surface_ptr.get(),
        m_window_ptr.get(),
        Anvil::Format::B8G8R8A8_UNORM,
        Anvil::ColorSpaceKHR::SRGB_NONLINEAR_KHR,
        Anvil::PresentModeKHR::FIFO_KHR,
        Anvil::ImageUsageFlagBits::COLOR_ATTACHMENT_BIT,
        m_n_swapchain_images);

    m_swapchain_ptr->set_name("Main swapchain");
    m_width = m_swapchain_ptr->get_width();
    m_height = m_swapchain_ptr->get_height();

    /* Cache the queue we are going to use for presentation */
    const std::vector<uint32_t>* present_queue_fams_ptr = nullptr;

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
    auto allocator_ptr = Anvil::MemoryAllocator::create_oneshot(m_device_ptr.get());

    #pragma region 创建uniform缓冲
    const VkDeviceSize ub_data_size_per_swapchain_image = sizeof(MVP);
    const auto ub_data_alignment_requirement = m_device_ptr->get_physical_device_properties().core_vk1_0_properties_ptr->limits.min_uniform_buffer_offset_alignment;
    m_ub_data_size_per_swapchain_image = Anvil::Utils::round_up(ub_data_size_per_swapchain_image, ub_data_alignment_requirement);
    const auto ub_data_size_total = N_SWAPCHAIN_IMAGES * m_ub_data_size_per_swapchain_image;

    auto create_info_ptr = Anvil::BufferCreateInfo::create_no_alloc(
        m_device_ptr.get(),
        ub_data_size_total,
        Anvil::QueueFamilyFlagBits::COMPUTE_BIT | Anvil::QueueFamilyFlagBits::GRAPHICS_BIT,
        Anvil::SharingMode::EXCLUSIVE,
        Anvil::BufferCreateFlagBits::NONE,
        Anvil::BufferUsageFlagBits::UNIFORM_BUFFER_BIT);
    m_uniform_buffer_ptr = Anvil::Buffer::create(std::move(create_info_ptr));
    m_uniform_buffer_ptr->set_name("Unfiorm data buffer");

    allocator_ptr->add_buffer(
        m_uniform_buffer_ptr.get(),
        Anvil::MemoryFeatureFlagBits::NONE); /* in_required_memory_features */
    #pragma endregion
}

void Engine::init_dsgs()
{
    #pragma region 创建描述符集群
    auto dsg_create_info_ptrs = vector<DescriptorSetCreateInfoUniquePtr>(2);

    dsg_create_info_ptrs[0] = DescriptorSetCreateInfo::create();
    dsg_create_info_ptrs[0]->add_binding(
        0, /* n_binding */
        DescriptorType::COMBINED_IMAGE_SAMPLER,
        m_model->get_texture_num(), /* n_elements */
        ShaderStageFlagBits::FRAGMENT_BIT);

    dsg_create_info_ptrs[1] = DescriptorSetCreateInfo::create();
    dsg_create_info_ptrs[1]->add_binding(
        0, /* n_binding */
        DescriptorType::UNIFORM_BUFFER_DYNAMIC,
        1, /* n_elements */
        ShaderStageFlagBits::VERTEX_BIT);

    m_dsg_ptr = DescriptorSetGroup::create(
        m_device_ptr.get(),
        dsg_create_info_ptrs);
    #pragma endregion

    #pragma region 为描述符集绑定具体资源
    m_model->add_combined_image_samplers();

    m_dsg_ptr->set_binding_array_items(
        0, /* n_set:用于dsg标识内部的描述符集，与dsg_create_info_ptrs下标一一对应，与shader里的set无关*/
        0, /* n_binding */
        BindingElementArrayRange(
            0,                                  /* StartBindingElementIndex */
            m_combined_image_samplers.size()),  /* NumberOfBindingElements  */
        m_combined_image_samplers.data());

    m_dsg_ptr->set_binding_item(
        1, /* n_set:用于dsg标识内部的描述符集，与dsg_create_info_ptrs下标一一对应，与shader里的set无关*/
        0, /* n_binding */
        DescriptorSet::DynamicUniformBufferBindingElement(
            m_uniform_buffer_ptr.get(),
            0, /* in_start_offset */
            m_ub_data_size_per_swapchain_image));
    #pragma endregion

}



void Engine::init_render_pass()
{
    RenderPassCreateInfoUniquePtr render_pass_create_info_ptr(new RenderPassCreateInfo(m_device_ptr.get()));
    
    #pragma region 添加附件描述
    RenderPassAttachmentID 
        render_pass_color_attachment_id, 
        tangent_frame_color_attachment_id, 
        uv_and_depth_gradient_color_attachment_id, 
        uv_gradient_color_attachment_id, 
        material_id_color_attachment_id, 
        render_pass_depth_attachment_id;

    render_pass_create_info_ptr->add_color_attachment(
        m_swapchain_ptr->get_create_info_ptr()->get_format(),
        SampleCountFlagBits::_1_BIT,
        AttachmentLoadOp::CLEAR,
        AttachmentStoreOp::STORE,
        ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
        ImageLayout::PRESENT_SRC_KHR,
        false, /* may_alias */
        &render_pass_color_attachment_id);
    
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
        Format::A2B10G10R10_UNORM_PACK32,
        SampleCountFlagBits::_1_BIT,
        AttachmentLoadOp::CLEAR,
        AttachmentStoreOp::STORE,
        ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
        ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
        false, /* may_alias */
        &uv_and_depth_gradient_color_attachment_id);

    render_pass_create_info_ptr->add_color_attachment(
        Format::A2B10G10R10_UNORM_PACK32,
        SampleCountFlagBits::_1_BIT,
        AttachmentLoadOp::CLEAR,
        AttachmentStoreOp::STORE,
        ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
        ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
        false, /* may_alias */
        &uv_gradient_color_attachment_id);

    render_pass_create_info_ptr->add_color_attachment(
        Format::A2B10G10R10_UNORM_PACK32,
        SampleCountFlagBits::_1_BIT,
        AttachmentLoadOp::CLEAR,
        AttachmentStoreOp::STORE,
        ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
        ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
        false, /* may_alias */
        &material_id_color_attachment_id);
    
    m_depth_format = SelectSupportedFormat(
        { Format::D32_SFLOAT,  Format::D32_SFLOAT_S8_UINT,  Format::D24_UNORM_S8_UINT },
        ImageTiling::OPTIMAL,
        FormatFeatureFlagBits::DEPTH_STENCIL_ATTACHMENT_BIT);

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

    #pragma region 为子流程添加附件引用
    SubPassID m_render_pass_subpass_id;
    render_pass_create_info_ptr->add_subpass(&m_render_pass_subpass_id);

    render_pass_create_info_ptr->add_subpass_color_attachment(
        m_render_pass_subpass_id,
        ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
        render_pass_color_attachment_id,
        0,        /* location                      */
        nullptr); /* opt_attachment_resolve_id_ptr */

    render_pass_create_info_ptr->add_subpass_color_attachment(
        m_render_pass_subpass_id,
        ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
        tangent_frame_color_attachment_id,
        1,        /* location                      */
        nullptr); /* opt_attachment_resolve_id_ptr */

    render_pass_create_info_ptr->add_subpass_color_attachment(
        m_render_pass_subpass_id,
        ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
        uv_and_depth_gradient_color_attachment_id,
        2,        /* location                      */
        nullptr); /* opt_attachment_resolve_id_ptr */

    render_pass_create_info_ptr->add_subpass_color_attachment(
        m_render_pass_subpass_id,
        ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
        uv_gradient_color_attachment_id,
        3,        /* location                      */
        nullptr); /* opt_attachment_resolve_id_ptr */

    render_pass_create_info_ptr->add_subpass_color_attachment(
        m_render_pass_subpass_id,
        ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
        material_id_color_attachment_id,
        4,        /* location                      */
        nullptr); /* opt_attachment_resolve_id_ptr */
   
    render_pass_create_info_ptr->add_subpass_depth_stencil_attachment(
        m_render_pass_subpass_id,
        ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        render_pass_depth_attachment_id);
    #pragma endregion

    m_renderpass_ptr = RenderPass::create(
        move(render_pass_create_info_ptr),
        m_swapchain_ptr.get());
    m_renderpass_ptr->set_name("Main renderpass");

}

void Engine::init_shaders()
{
    Anvil::GLSLShaderToSPIRVGeneratorUniquePtr fragment_shader_ptr;
    Anvil::ShaderModuleUniquePtr               fragment_shader_module_ptr;
    Anvil::GLSLShaderToSPIRVGeneratorUniquePtr vertex_shader_ptr;
    Anvil::ShaderModuleUniquePtr               vertex_shader_module_ptr;

    fragment_shader_ptr = Anvil::GLSLShaderToSPIRVGenerator::create(
        m_device_ptr.get(),
        Anvil::GLSLShaderToSPIRVGenerator::MODE_LOAD_SOURCE_FROM_FILE,
        "Assets/code/shader/GBuffer.frag",
        Anvil::ShaderStage::FRAGMENT);
    vertex_shader_ptr = Anvil::GLSLShaderToSPIRVGenerator::create(
        m_device_ptr.get(),
        Anvil::GLSLShaderToSPIRVGenerator::MODE_LOAD_SOURCE_FROM_FILE,
        "Assets/code/shader/GBuffer.vert",
        Anvil::ShaderStage::VERTEX);

    fragment_shader_module_ptr = Anvil::ShaderModule::create_from_spirv_generator(
        m_device_ptr.get(),
        fragment_shader_ptr.get());
    vertex_shader_module_ptr = Anvil::ShaderModule::create_from_spirv_generator(
        m_device_ptr.get(),
        vertex_shader_ptr.get());

    fragment_shader_module_ptr->set_name("Fragment shader module");
    vertex_shader_module_ptr->set_name("Vertex shader module");

    m_fs_ptr.reset(
        new Anvil::ShaderModuleStageEntryPoint(
            "main",
            std::move(fragment_shader_module_ptr),
            Anvil::ShaderStage::FRAGMENT)
    );
    m_vs_ptr.reset(
        new Anvil::ShaderModuleStageEntryPoint(
            "main",
            std::move(vertex_shader_module_ptr),
            Anvil::ShaderStage::VERTEX)
    );
}

void Engine::init_gfx_pipelines()
{
    GraphicsPipelineCreateInfoUniquePtr gfx_pipeline_create_info_ptr;
    
    gfx_pipeline_create_info_ptr = GraphicsPipelineCreateInfo::create(
        PipelineCreateFlagBits::NONE,
        m_renderpass_ptr.get(), 
        m_render_pass_subpass_id,
        *m_fs_ptr,
        ShaderModuleStageEntryPoint(), /* in_geometry_shader        */
        ShaderModuleStageEntryPoint(), /* in_tess_control_shader    */
        ShaderModuleStageEntryPoint(), /* in_tess_evaluation_shader */
        *m_vs_ptr);

    gfx_pipeline_create_info_ptr->set_descriptor_set_create_info(m_dsg_ptr->get_descriptor_set_create_info());
   
    gfx_pipeline_create_info_ptr->attach_push_constant_range(
        0, /* in_offset */
        1, /* in_size */
        Anvil::ShaderStageFlagBits::FRAGMENT_BIT);

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

    auto gfx_pipeline_manager_ptr(m_device_ptr->get_graphics_pipeline_manager());
    gfx_pipeline_manager_ptr->add_pipeline(
        move(gfx_pipeline_create_info_ptr),
        &m_pipeline_id);
}



void Engine::init_image()
{
    auto allocator_ptr = MemoryAllocator::create_oneshot(m_device_ptr.get());

    #pragma region 创建深度图像
    {
        auto image_create_info_ptr = ImageCreateInfo::create_no_alloc(
            m_device_ptr.get(),
            ImageType::_2D,
            m_depth_format,
            ImageTiling::OPTIMAL,
            ImageUsageFlagBits::DEPTH_STENCIL_ATTACHMENT_BIT,
            m_width,
            m_height,
            1,
            1,
            SampleCountFlagBits::_1_BIT,
            QueueFamilyFlagBits::COMPUTE_BIT | QueueFamilyFlagBits::GRAPHICS_BIT,
            SharingMode::EXCLUSIVE,
            false,
            ImageCreateFlagBits::NONE,
            ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        m_depth_image_ptr = Image::create(move(image_create_info_ptr));
        m_depth_image_ptr->set_name("Depth image");

        allocator_ptr->add_image_whole(
            m_depth_image_ptr.get(),
            MemoryFeatureFlagBits::DEVICE_LOCAL_BIT);
    }
    #pragma endregion

    #pragma region 创建切线框架图像
    {
        auto image_create_info_ptr = ImageCreateInfo::create_no_alloc(
            m_device_ptr.get(),
            ImageType::_2D,
            Format::A2B10G10R10_UNORM_PACK32,
            ImageTiling::OPTIMAL,
            ImageUsageFlagBits::COLOR_ATTACHMENT_BIT,
            m_width,
            m_height,
            1,
            1,
            SampleCountFlagBits::_1_BIT,
            QueueFamilyFlagBits::COMPUTE_BIT | QueueFamilyFlagBits::GRAPHICS_BIT,
            SharingMode::EXCLUSIVE,
            false,
            ImageCreateFlagBits::NONE,
            ImageLayout::COLOR_ATTACHMENT_OPTIMAL);

        m_tangent_frame_image_ptr = Image::create(move(image_create_info_ptr));
        m_tangent_frame_image_ptr->set_name("Tangent frame image");

        allocator_ptr->add_image_whole(
            m_tangent_frame_image_ptr.get(),
            MemoryFeatureFlagBits::DEVICE_LOCAL_BIT);
    }
    #pragma endregion

    #pragma region 创建UV和深度梯度图像
    {
        auto image_create_info_ptr = ImageCreateInfo::create_no_alloc(
            m_device_ptr.get(),
            ImageType::_2D,
            Format::R16G16B16A16_SNORM,
            ImageTiling::OPTIMAL,
            ImageUsageFlagBits::COLOR_ATTACHMENT_BIT,
            m_width,
            m_height,
            1,
            1,
            SampleCountFlagBits::_1_BIT,
            QueueFamilyFlagBits::COMPUTE_BIT | QueueFamilyFlagBits::GRAPHICS_BIT,
            SharingMode::EXCLUSIVE,
            false,
            ImageCreateFlagBits::NONE,
            ImageLayout::COLOR_ATTACHMENT_OPTIMAL);

        m_uv_and_depth_gradient_image_ptr = Image::create(move(image_create_info_ptr));
        m_uv_and_depth_gradient_image_ptr->set_name("UV and depth gradient image");

        allocator_ptr->add_image_whole(
            m_uv_and_depth_gradient_image_ptr.get(),
            MemoryFeatureFlagBits::DEVICE_LOCAL_BIT);
    }
    #pragma endregion

    #pragma region 创建UV梯度图像
    {
        auto image_create_info_ptr = ImageCreateInfo::create_no_alloc(
            m_device_ptr.get(),
            ImageType::_2D,
            Format::R16G16B16A16_SNORM,
            ImageTiling::OPTIMAL,
            ImageUsageFlagBits::COLOR_ATTACHMENT_BIT,
            m_width,
            m_height,
            1,
            1,
            SampleCountFlagBits::_1_BIT,
            QueueFamilyFlagBits::COMPUTE_BIT | QueueFamilyFlagBits::GRAPHICS_BIT,
            SharingMode::EXCLUSIVE,
            false,
            ImageCreateFlagBits::NONE,
            ImageLayout::COLOR_ATTACHMENT_OPTIMAL);

        m_uv_gradient_image_ptr = Image::create(move(image_create_info_ptr));
        m_uv_gradient_image_ptr->set_name("UV gradient image");

        allocator_ptr->add_image_whole(
            m_uv_gradient_image_ptr.get(),
            MemoryFeatureFlagBits::DEVICE_LOCAL_BIT);
    }
    #pragma endregion

    #pragma region 创建材质ID图像
    {
        auto image_create_info_ptr = ImageCreateInfo::create_no_alloc(
            m_device_ptr.get(),
            ImageType::_2D,
            Format::R8_UINT,
            ImageTiling::OPTIMAL,
            ImageUsageFlagBits::COLOR_ATTACHMENT_BIT,
            m_width,
            m_height,
            1,
            1,
            SampleCountFlagBits::_1_BIT,
            QueueFamilyFlagBits::COMPUTE_BIT | QueueFamilyFlagBits::GRAPHICS_BIT,
            SharingMode::EXCLUSIVE,
            false,
            ImageCreateFlagBits::NONE,
            ImageLayout::COLOR_ATTACHMENT_OPTIMAL);

        m_material_id_image_ptr = Image::create(move(image_create_info_ptr));
        m_material_id_image_ptr->set_name("material id image");

        allocator_ptr->add_image_whole(
            m_material_id_image_ptr.get(),
            MemoryFeatureFlagBits::DEVICE_LOCAL_BIT);
    }
    #pragma endregion
}

void Engine::init_image_view()
{
    #pragma region 创建深度图像视图
    {
        auto image_view_create_info_ptr = ImageViewCreateInfo::create_2D(
            m_device_ptr.get(),
            m_depth_image_ptr.get(),
            0,
            0,
            1,
            ImageAspectFlagBits::DEPTH_BIT,
            m_depth_format,
            ComponentSwizzle::R,
            ComponentSwizzle::G,
            ComponentSwizzle::B,
            ComponentSwizzle::A);

        m_depth_image_view_ptr = ImageView::create(move(image_view_create_info_ptr));
    }
    #pragma endregion

    #pragma region 创建切线框架图像视图
    {
        auto image_view_create_info_ptr = ImageViewCreateInfo::create_2D(
            m_device_ptr.get(),
            m_tangent_frame_image_ptr.get(),
            0,
            0,
            1,
            ImageAspectFlagBits::COLOR_BIT,
            Format::A2B10G10R10_UNORM_PACK32,
            ComponentSwizzle::R,
            ComponentSwizzle::G,
            ComponentSwizzle::B,
            ComponentSwizzle::A);

        m_tangent_frame_image_view_ptr = ImageView::create(move(image_view_create_info_ptr));
    }
    #pragma endregion

    #pragma region 创建UV和深度梯度图像视图
    {
        auto image_view_create_info_ptr = ImageViewCreateInfo::create_2D(
            m_device_ptr.get(),
            m_uv_and_depth_gradient_image_ptr.get(),
            0,
            0,
            1,
            ImageAspectFlagBits::COLOR_BIT,
            Format::R16G16B16A16_SNORM,
            ComponentSwizzle::R,
            ComponentSwizzle::G,
            ComponentSwizzle::B,
            ComponentSwizzle::A);

        m_uv_and_depth_gradient_image_view_ptr = ImageView::create(move(image_view_create_info_ptr));
    }
    #pragma endregion

    #pragma region 创建UV和深度梯度图像视图
    {
        auto image_view_create_info_ptr = ImageViewCreateInfo::create_2D(
            m_device_ptr.get(),
            m_uv_gradient_image_ptr.get(),
            0,
            0,
            1,
            ImageAspectFlagBits::COLOR_BIT,
            Format::R16G16B16A16_SNORM,
            ComponentSwizzle::R,
            ComponentSwizzle::G,
            ComponentSwizzle::B,
            ComponentSwizzle::A);

        m_uv_gradient_image_view_ptr = ImageView::create(move(image_view_create_info_ptr));
    }
    #pragma endregion

    #pragma region 创建材质ID图像视图
    {
        auto image_view_create_info_ptr = ImageViewCreateInfo::create_2D(
            m_device_ptr.get(),
            m_material_id_image_ptr.get(),
            0,
            0,
            1,
            ImageAspectFlagBits::COLOR_BIT,
            Format::R8_UINT,
            ComponentSwizzle::R,
            ComponentSwizzle::ZERO,
            ComponentSwizzle::ZERO,
            ComponentSwizzle::ZERO);

        m_material_id_image_view_ptr = ImageView::create(move(image_view_create_info_ptr));
    }
    #pragma endregion
}

void Engine::init_framebuffers()
{
    bool result;

    /* We need to instantiate 1 framebuffer object per each used swap-chain image */
    for (uint32_t n_fbo = 0; n_fbo < N_SWAPCHAIN_IMAGES; ++n_fbo)
    {
        ImageView* attachment_image_view_ptr = nullptr;

        attachment_image_view_ptr = m_swapchain_ptr->get_image_view(n_fbo);

        /* Create the internal framebuffer object */
        {
            auto create_info_ptr = FramebufferCreateInfo::create(
                m_device_ptr.get(),
                m_width,
                m_height,
                1 /* n_layers */);

            result = create_info_ptr->add_attachment(
                attachment_image_view_ptr,
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

            m_fbos[n_fbo] = Framebuffer::create(move(create_info_ptr));
        }

        m_fbos[n_fbo]->set_name_formatted(
            "Framebuffer for swapchain image [%d]",
            n_fbo);
    }
}

void Engine::init_command_buffers()
{
    auto                   gfx_pipeline_manager_ptr(m_device_ptr->get_graphics_pipeline_manager());
    ImageSubresourceRange  image_subresource_range;
    Queue*                 universal_queue_ptr(m_device_ptr->get_universal_queue(0));

    image_subresource_range.aspect_mask = ImageAspectFlagBits::COLOR_BIT;
    image_subresource_range.base_array_layer = 0;
    image_subresource_range.base_mip_level = 0;
    image_subresource_range.layer_count = 1;
    image_subresource_range.level_count = 1;

    for (uint32_t n_command_buffer = 0; n_command_buffer < N_SWAPCHAIN_IMAGES; ++n_command_buffer)
    {
        PrimaryCommandBufferUniquePtr cmd_buffer_ptr;

        cmd_buffer_ptr = m_device_ptr->
            get_command_pool_for_queue_family_index(m_device_ptr->get_universal_queue(0)->get_queue_family_index())
            ->alloc_primary_level_command_buffer();

        /* Start recording commands */
        cmd_buffer_ptr->start_recording(false, /* one_time_submit          */
                                        true); /* simultaneous_use_allowed */

        /* Switch the swap-chain image to the color_attachment_optimal image layout */
        {
            ImageBarrier image_barrier(
                AccessFlagBits::NONE,                       /* source_access_mask       */
                AccessFlagBits::COLOR_ATTACHMENT_WRITE_BIT, /* destination_access_mask  */
                ImageLayout::UNDEFINED,                  /* old_image_layout */
                ImageLayout::COLOR_ATTACHMENT_OPTIMAL,   /* new_image_layout */
                universal_queue_ptr->get_queue_family_index(),
                universal_queue_ptr->get_queue_family_index(),
                m_swapchain_ptr->get_image(n_command_buffer),
                image_subresource_range);

            cmd_buffer_ptr->record_pipeline_barrier(
                PipelineStageFlagBits::TOP_OF_PIPE_BIT,            /* src_stage_mask                 */
                PipelineStageFlagBits::COLOR_ATTACHMENT_OUTPUT_BIT,/* dst_stage_mask                 */
                DependencyFlagBits::NONE,
                0,                                                        /* in_memory_barrier_count        */
                nullptr,                                                  /* in_memory_barrier_ptrs         */
                0,                                                        /* in_buffer_memory_barrier_count */
                nullptr,                                                  /* in_buffer_memory_barrier_ptrs  */
                1,                                                        /* in_image_memory_barrier_count  */
                &image_barrier);
        }

        /* Make sure CPU-written data is flushed before we start rendering */
        BufferBarrier buffer_barrier(
            AccessFlagBits::HOST_WRITE_BIT,                 /* in_source_access_mask      */
            AccessFlagBits::UNIFORM_READ_BIT,               /* in_destination_access_mask */
            universal_queue_ptr->get_queue_family_index(),         /* in_src_queue_family_index  */
            universal_queue_ptr->get_queue_family_index(),         /* in_dst_queue_family_index  */
            m_uniform_buffer_ptr.get(),
            m_ub_data_size_per_swapchain_image * n_command_buffer, /* in_offset                  */
            m_ub_data_size_per_swapchain_image);

        cmd_buffer_ptr->record_pipeline_barrier(
            PipelineStageFlagBits::HOST_BIT,
            PipelineStageFlagBits::VERTEX_SHADER_BIT,
            DependencyFlagBits::NONE,
            0,               /* in_memory_barrier_count        */
            nullptr,         /* in_memory_barriers_ptr         */
            1,               /* in_buffer_memory_barrier_count */
            &buffer_barrier,
            0,               /* in_image_memory_barrier_count  */
            nullptr);        /* in_image_memory_barriers_ptr   */

        /* 2. Render the geometry. */
        array<VkClearValue, 6>            attachment_clear_value;
        VkRect2D                          render_area;
        VkShaderStageFlags                shaderStageFlags = 0;

        attachment_clear_value[0].color = { 0.0f, 0.0f, 0.0f, 1.0f };
        attachment_clear_value[1].color = { 0.0f, 0.0f, 0.0f, 0.0f };
        attachment_clear_value[2].color = { 0.0f, 0.0f, 0.0f, 0.0f };
        attachment_clear_value[3].color = { 0.0f, 0.0f, 0.0f, 0.0f };
        attachment_clear_value[4].color = { uint32(0), uint32(0), uint32(0), uint32(0) };
        attachment_clear_value[5].depthStencil = { 1.0f, 0 };

        render_area.extent.height = m_height;
        render_area.extent.width = m_width;
        render_area.offset.x = 0;
        render_area.offset.y = 0;

        cmd_buffer_ptr->record_begin_render_pass(
            6, /* in_n_clear_values */
            attachment_clear_value.data(),
            m_fbos[n_command_buffer].get(),
            render_area,
            m_renderpass_ptr.get(),
            SubpassContents::INLINE);
        {
            const uint32_t data_ub_offset = static_cast<uint32_t>(m_ub_data_size_per_swapchain_image * n_command_buffer);
            DescriptorSet* ds_ptr[2] = { m_dsg_ptr->get_descriptor_set(0) ,m_dsg_ptr->get_descriptor_set(1) };

            cmd_buffer_ptr->record_bind_pipeline(
                PipelineBindPoint::GRAPHICS,
                m_pipeline_id);

            cmd_buffer_ptr->record_bind_descriptor_sets(
                PipelineBindPoint::GRAPHICS,
                Engine::Instance()->getPineLine(),
                0, /* firstSet */
                2, /* setCount：传入的描述符集与shader中的set一一对应 */
                ds_ptr,
                1,                /* dynamicOffsetCount */
                &data_ub_offset); /* pDynamicOffsets    */

            m_model->draw(cmd_buffer_ptr.get());
            
        }
        cmd_buffer_ptr->record_end_render_pass();

        /* Close the recording process */
        cmd_buffer_ptr->stop_recording();

        m_command_buffers[n_command_buffer] = move(cmd_buffer_ptr);
    }
}

void Engine::init_semaphores()
{
    for (uint32_t n_semaphore = 0; n_semaphore < m_n_swapchain_images; ++n_semaphore)
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

void Engine::init_events()
{
    /* Stub */
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
    init_render_pass();
    init_gfx_pipelines();
    init_image();
    init_image_view();
    init_framebuffers();
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
    m_n_last_semaphore_used = (m_n_last_semaphore_used + 1) % m_n_swapchain_images;

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
        else if (acquire_result != SwapchainOperationErrorCode::SUCCESS 
            && acquire_result != SwapchainOperationErrorCode::SUBOPTIMAL)
        {
            throw runtime_error("failed to acquire swap chain image!");
        }
    }

    /* Submit work chunk and present */
    update_data_ub_contents(n_swapchain_image);

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

        present_queue_ptr->present(m_swapchain_ptr.get(),
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
            throw std::runtime_error("failed to present swap chain image!");
        }
    }
}

void Engine::update_data_ub_contents(uint32_t in_n_swapchain_image)
{
    static auto lastTime = chrono::high_resolution_clock::now();
    auto currentTime = chrono::high_resolution_clock::now();
    float time = chrono::duration<float, chrono::seconds::period>(currentTime - lastTime).count();
    lastTime = currentTime;


    if (m_key->IsPressed(KeyID::KEY_ID_FORWARD))
        m_camera->ProcessKeyboard(FORWARD, time);
    if (m_key->IsPressed(KeyID::KEY_ID_BACKWARD))
        m_camera->ProcessKeyboard(BACKWARD, time);
    if (m_key->IsPressed(KeyID::KEY_ID_LEFT))
        m_camera->ProcessKeyboard(LEFT, time);
    if (m_key->IsPressed(KeyID::KEY_ID_RIGHT))
        m_camera->ProcessKeyboard(RIGHT, time);

    MVP mvp = {};
    mvp.model = rotate(mat4(1.0f), /*time * */radians(30.0f), vec3(0.0f, 1.0f, 0.0f))
        *scale(mat4(1.0f), vec3(0.015f, 0.015f, 0.015f));
    mvp.view = Camera::Active()->GetViewMatrix();
    mvp.proj = Camera::Active()->GetProjMatrix();

    m_uniform_buffer_ptr->write(
        in_n_swapchain_image * m_ub_data_size_per_swapchain_image, /* start_offset */
        sizeof(mvp),
        &mvp,
        m_device_ptr->get_universal_queue(0));

}

void Engine::mouse_callback(CallbackArgument* argumentPtr)
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
    m_uv_and_depth_gradient_image_view_ptr.reset();
    m_uv_gradient_image_view_ptr.reset();
    m_tangent_frame_image_view_ptr.reset();
    m_material_id_image_view_ptr.reset();

    m_depth_image_ptr.reset();
    m_tangent_frame_image_ptr.reset();
    m_uv_and_depth_gradient_image_ptr.reset();
    m_uv_gradient_image_ptr.reset();
    m_material_id_image_ptr.reset();

    for (uint32_t n_swapchain_image = 0; n_swapchain_image < N_SWAPCHAIN_IMAGES; ++n_swapchain_image)
    {
        m_command_buffers[n_swapchain_image].reset();
        m_fbos[n_swapchain_image].reset();
    }

    if (m_pipeline_id != UINT32_MAX)
    {
        gfx_pipeline_manager_ptr->delete_pipeline(m_pipeline_id);
        m_pipeline_id = UINT32_MAX;
    }
    m_renderpass_ptr.reset();
    m_swapchain_ptr.reset();
}

void Engine::deinit()
{
    cleanup_swapwhain();

    m_frame_signal_semaphores.clear();
    m_frame_wait_semaphores.clear();

    m_rendering_surface_ptr.reset();
    
    m_dsg_ptr.reset();
    m_uniform_buffer_ptr.reset();
    m_fs_ptr.reset();
    m_vs_ptr.reset();

    m_model.reset();

    m_device_ptr.reset();
    m_instance_ptr.reset();

    m_window_ptr.reset();
}
#pragma endregion

#pragma region 工具
void Engine::on_validation_callback(Anvil::DebugMessageSeverityFlags in_severity, const char* in_message_ptr)
{
    if ((in_severity & Anvil::DebugMessageSeverityFlagBits::ERROR_BIT) != 0)
    {
        fprintf(stderr,
            "[!] %s\n",
            in_message_ptr);
    }
}

Anvil::Format Engine::SelectSupportedFormat(
    const std::vector<Anvil::Format>& candidates,
    Anvil::ImageTiling tiling,
    Anvil::FormatFeatureFlags features)
{
    Anvil::SGPUDevice* device_ptr(reinterpret_cast<Anvil::SGPUDevice*>(m_device_ptr.get()));
    for (Anvil::Format format : candidates)
    {
        Anvil::FormatProperties props = device_ptr->get_physical_device_format_properties(format);
        if (tiling == Anvil::ImageTiling::LINEAR && (props.linear_tiling_capabilities & features) == features)
        {
            return format;
        }
        else if (tiling == Anvil::ImageTiling::OPTIMAL && (props.optimal_tiling_capabilities & features) == features)
        {
            return format;
        }
    }
    throw std::runtime_error("failed to find supported format!");
}
#pragma endregion