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

PipelineLayout* Engine::getPineLine(bool is_gfx)
{
    if (is_gfx) 
    {
        auto gfx_pipeline_manager_ptr(m_device_ptr->get_graphics_pipeline_manager());
        return gfx_pipeline_manager_ptr->get_pipeline_layout(m_gfx_pipeline_id);    
    }
    else
    {
        auto compute_pipeline_manager_ptr(m_device_ptr->get_compute_pipeline_manager());
        return compute_pipeline_manager_ptr->get_pipeline_layout(m_compute_pipeline_id);
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
     m_mvp_buffer_size_per_swapchain_image(0),
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


    m_model = make_shared<Model>("assets/models/Sponza/Sponza.fbx");
    init_buffers();
    init_image();
    init_image_view();
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
    auto allocator_ptr = MemoryAllocator::create_oneshot(m_device_ptr.get());
    const auto ub_data_alignment_requirement = m_device_ptr->get_physical_device_properties().core_vk1_0_properties_ptr->limits.min_uniform_buffer_offset_alignment;
    
    #pragma region 创建texture_indices缓冲
    {
        VkDeviceSize size = Utils::round_up(
            16 * m_model->get_texture_indices()->size(),
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
    }
    #pragma endregion

    #pragma region 创建mvp缓冲
    {
        m_mvp_buffer_size_per_swapchain_image = Utils::round_up(sizeof(MVPUniform), ub_data_alignment_requirement);
        const auto ub_data_size_total = N_SWAPCHAIN_IMAGES * m_mvp_buffer_size_per_swapchain_image;

        auto create_info_ptr = BufferCreateInfo::create_no_alloc(
            m_device_ptr.get(),
            ub_data_size_total,
            QueueFamilyFlagBits::GRAPHICS_BIT,
            SharingMode::EXCLUSIVE,
            BufferCreateFlagBits::NONE,
            BufferUsageFlagBits::UNIFORM_BUFFER_BIT);
        m_mvp_uniform_buffer_ptr = Buffer::create(move(create_info_ptr));
        m_mvp_uniform_buffer_ptr->set_name("MVP unfiorm buffer");

        allocator_ptr->add_buffer(
            m_mvp_uniform_buffer_ptr.get(),
            MemoryFeatureFlagBits::NONE); /* in_required_memory_features */
    }
    #pragma endregion

    #pragma region 创建sunLight缓冲
    {
        m_sunLight_buffer_size_per_swapchain_image = Utils::round_up(sizeof(SunLightUniform), ub_data_alignment_requirement);
        const auto ub_data_size_total = N_SWAPCHAIN_IMAGES * m_sunLight_buffer_size_per_swapchain_image;

        auto create_info_ptr = BufferCreateInfo::create_no_alloc(
            m_device_ptr.get(),
            ub_data_size_total,
            QueueFamilyFlagBits::GRAPHICS_BIT | QueueFamilyFlagBits::COMPUTE_BIT,
            SharingMode::EXCLUSIVE,
            BufferCreateFlagBits::NONE,
            BufferUsageFlagBits::UNIFORM_BUFFER_BIT);
        m_sunLight_uniform_buffer_ptr = Buffer::create(move(create_info_ptr));
        m_sunLight_uniform_buffer_ptr->set_name("SunLight unfiorm buffer");

        allocator_ptr->add_buffer(
            m_sunLight_uniform_buffer_ptr.get(),
            MemoryFeatureFlagBits::NONE); /* in_required_memory_features */
    }
    #pragma endregion

    #pragma region 创建camera缓冲
    {
        m_camera_buffer_size_per_swapchain_image = Utils::round_up(sizeof(CameraUniform), ub_data_alignment_requirement);
        const auto ub_data_size_total = N_SWAPCHAIN_IMAGES * m_camera_buffer_size_per_swapchain_image;

        auto create_info_ptr = BufferCreateInfo::create_no_alloc(
            m_device_ptr.get(),
            ub_data_size_total,
            QueueFamilyFlagBits::GRAPHICS_BIT | QueueFamilyFlagBits::COMPUTE_BIT,
            SharingMode::EXCLUSIVE,
            BufferCreateFlagBits::NONE,
            BufferUsageFlagBits::UNIFORM_BUFFER_BIT);
        m_camera_uniform_buffer_ptr = Buffer::create(move(create_info_ptr));
        m_camera_uniform_buffer_ptr->set_name("Camera unfiorm buffer");

        allocator_ptr->add_buffer(
            m_camera_uniform_buffer_ptr.get(),
            MemoryFeatureFlagBits::NONE); /* in_required_memory_features */
    }
    #pragma endregion


    m_texture_indices_uniform_buffer_ptr->write(
        0, /* start_offset */
        16 * m_model->get_texture_indices()->size(),
        m_model->get_texture_indices()->data(),
        m_device_ptr->get_universal_queue(0));
}

void Engine::init_image()
{
    auto allocator_ptr = MemoryAllocator::create_oneshot(m_device_ptr.get());

    #pragma region 创建深度图像
    {
        m_depth_format = SelectSupportedFormat(
            { Format::D32_SFLOAT,  Format::D32_SFLOAT_S8_UINT,  Format::D24_UNORM_S8_UINT },
            ImageTiling::OPTIMAL,
            FormatFeatureFlagBits::DEPTH_STENCIL_ATTACHMENT_BIT);

        auto image_create_info_ptr = ImageCreateInfo::create_no_alloc(
            m_device_ptr.get(),
            ImageType::_2D,
            m_depth_format,
            ImageTiling::OPTIMAL,
            ImageUsageFlagBits::DEPTH_STENCIL_ATTACHMENT_BIT
            | ImageUsageFlagBits::SAMPLED_BIT,
            m_width,
            m_height,
            1,
            1,
            SampleCountFlagBits::_1_BIT,
            QueueFamilyFlagBits::COMPUTE_BIT | QueueFamilyFlagBits::GRAPHICS_BIT,
            SharingMode::EXCLUSIVE,
            false,
            ImageCreateFlagBits::NONE,
            ImageLayout::SHADER_READ_ONLY_OPTIMAL);

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
            ImageUsageFlagBits::COLOR_ATTACHMENT_BIT
            | ImageUsageFlagBits::SAMPLED_BIT,
            m_width,
            m_height,
            1,
            1,
            SampleCountFlagBits::_1_BIT,
            QueueFamilyFlagBits::COMPUTE_BIT | QueueFamilyFlagBits::GRAPHICS_BIT,
            SharingMode::EXCLUSIVE,
            false,
            ImageCreateFlagBits::NONE,
            ImageLayout::SHADER_READ_ONLY_OPTIMAL);

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
            ImageUsageFlagBits::COLOR_ATTACHMENT_BIT
            | ImageUsageFlagBits::SAMPLED_BIT,
            m_width,
            m_height,
            1,
            1,
            SampleCountFlagBits::_1_BIT,
            QueueFamilyFlagBits::COMPUTE_BIT | QueueFamilyFlagBits::GRAPHICS_BIT,
            SharingMode::EXCLUSIVE,
            false,
            ImageCreateFlagBits::NONE,
            ImageLayout::SHADER_READ_ONLY_OPTIMAL);

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
            ImageUsageFlagBits::COLOR_ATTACHMENT_BIT
            | ImageUsageFlagBits::SAMPLED_BIT,
            m_width,
            m_height,
            1,
            1,
            SampleCountFlagBits::_1_BIT,
            QueueFamilyFlagBits::COMPUTE_BIT | QueueFamilyFlagBits::GRAPHICS_BIT,
            SharingMode::EXCLUSIVE,
            false,
            ImageCreateFlagBits::NONE,
            ImageLayout::SHADER_READ_ONLY_OPTIMAL);

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
            ImageUsageFlagBits::COLOR_ATTACHMENT_BIT
            | ImageUsageFlagBits::SAMPLED_BIT,
            m_width,
            m_height,
            1,
            1,
            SampleCountFlagBits::_1_BIT,
            QueueFamilyFlagBits::COMPUTE_BIT | QueueFamilyFlagBits::GRAPHICS_BIT,
            SharingMode::EXCLUSIVE,
            false,
            ImageCreateFlagBits::NONE,
            ImageLayout::SHADER_READ_ONLY_OPTIMAL);

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

    #pragma region 创建UV梯度图像视图
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
            ComponentSwizzle::IDENTITY,
            ComponentSwizzle::IDENTITY,
            ComponentSwizzle::IDENTITY);

        m_material_id_image_view_ptr = ImageView::create(move(image_view_create_info_ptr));
    }
    #pragma endregion

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
    auto dsg_create_info_ptrs = vector<DescriptorSetCreateInfoUniquePtr>(7);

    //0:模型纹理及其材质
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

    //1:顶点着色器所需的MVP
    dsg_create_info_ptrs[1] = DescriptorSetCreateInfo::create();
    dsg_create_info_ptrs[1]->add_binding(
        0, /* n_binding */
        DescriptorType::UNIFORM_BUFFER_DYNAMIC,
        1, /* n_elements */
        ShaderStageFlagBits::VERTEX_BIT);

    //2:计算着色器所需的参数
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

    //3:用于计算着色器读取的GBuffer
    dsg_create_info_ptrs[3] = DescriptorSetCreateInfo::create();
    for (int i = 0; i < 5; i++)
    {
        dsg_create_info_ptrs[3]->add_binding(
            i, /* n_binding */
            DescriptorType::COMBINED_IMAGE_SAMPLER,
            1, /* n_elements */
            ShaderStageFlagBits::COMPUTE_BIT);
    }
    
    //4:交换链图像
    for (int i = 4; i < 4 + N_SWAPCHAIN_IMAGES; i++)
    {
        dsg_create_info_ptrs[i] = DescriptorSetCreateInfo::create();
        dsg_create_info_ptrs[i]->add_binding(
            0, /* n_binding */
            DescriptorType::STORAGE_IMAGE,
            1, /* n_elements */
            ShaderStageFlagBits::COMPUTE_BIT);
    }

    m_dsg_ptr = DescriptorSetGroup::create(
        m_device_ptr.get(),
        dsg_create_info_ptrs);
    #pragma endregion

    #pragma region 为描述符集绑定具体资源

    #pragma region 0:模型纹理及其材质索引信息
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

    #pragma region 1:顶点着色器所需的的MVP
    m_dsg_ptr->set_binding_item(
        1, /* n_set:用于dsg标识内部的描述符集，与dsg_create_info_ptrs下标一一对应，与shader里的set无关*/
        0, /* n_binding */
        DescriptorSet::DynamicUniformBufferBindingElement(
            m_mvp_uniform_buffer_ptr.get(),
            0, /* in_start_offset */
            m_mvp_buffer_size_per_swapchain_image));
    #pragma endregion

    #pragma region 2:计算着色器所需的参数
    m_dsg_ptr->set_binding_item(
        2, /* n_set:用于dsg标识内部的描述符集，与dsg_create_info_ptrs下标一一对应，与shader里的set无关*/
        0, /* n_binding */
        DescriptorSet::DynamicUniformBufferBindingElement(
            m_sunLight_uniform_buffer_ptr.get(),
            0, /* in_start_offset */
            m_sunLight_buffer_size_per_swapchain_image));

    m_dsg_ptr->set_binding_item(
        2, /* n_set:用于dsg标识内部的描述符集，与dsg_create_info_ptrs下标一一对应，与shader里的set无关*/
        1, /* n_binding */
        DescriptorSet::DynamicUniformBufferBindingElement(
            m_camera_uniform_buffer_ptr.get(),
            0, /* in_start_offset */
            m_camera_buffer_size_per_swapchain_image));
    #pragma endregion

    #pragma region 3:用于计算着色器读取的GBuffer
    m_dsg_ptr->set_binding_item(
        3, /* n_set:用于dsg标识内部的描述符集，与dsg_create_info_ptrs下标一一对应，与shader里的set无关*/
        0, /* n_binding */
        DescriptorSet::CombinedImageSamplerBindingElement(
            ImageLayout::SHADER_READ_ONLY_OPTIMAL,
            m_depth_image_view_ptr.get(),
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
    
    #pragma endregion
}



void Engine::init_render_pass()
{
    RenderPassCreateInfoUniquePtr render_pass_create_info_ptr(new RenderPassCreateInfo(m_device_ptr.get()));
    
    #pragma region 添加附件描述
    RenderPassAttachmentID 
        tangent_frame_color_attachment_id, 
        uv_and_depth_gradient_color_attachment_id, 
        uv_gradient_color_attachment_id, 
        material_id_color_attachment_id, 
        render_pass_depth_attachment_id;
    
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
            tangent_frame_color_attachment_id,
            0,        /* location                      */
            nullptr); /* opt_attachment_resolve_id_ptr */

        render_pass_create_info_ptr->add_subpass_color_attachment(
            m_render_pass_subpass_GBuffer_id,
            ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
            uv_and_depth_gradient_color_attachment_id,
            1,        /* location                      */
            nullptr); /* opt_attachment_resolve_id_ptr */

        render_pass_create_info_ptr->add_subpass_color_attachment(
            m_render_pass_subpass_GBuffer_id,
            ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
            uv_gradient_color_attachment_id,
            2,        /* location                      */
            nullptr); /* opt_attachment_resolve_id_ptr */

        render_pass_create_info_ptr->add_subpass_color_attachment(
            m_render_pass_subpass_GBuffer_id,
            ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
            material_id_color_attachment_id,
            3,        /* location                      */
            nullptr); /* opt_attachment_resolve_id_ptr */
   
        render_pass_create_info_ptr->add_subpass_depth_stencil_attachment(
            m_render_pass_subpass_GBuffer_id,
            ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            render_pass_depth_attachment_id);
    }
    #pragma endregion

    m_renderpass_ptr = RenderPass::create(
        move(render_pass_create_info_ptr),
        m_swapchain_ptr.get());
    m_renderpass_ptr->set_name("GBuffer renderpass");

}

void Engine::init_shaders()
{
    GLSLShaderToSPIRVGeneratorUniquePtr vertex_shader_ptr;
    ShaderModuleUniquePtr               vertex_shader_module_ptr;
    GLSLShaderToSPIRVGeneratorUniquePtr fragment_shader_ptr;
    ShaderModuleUniquePtr               fragment_shader_module_ptr;
    GLSLShaderToSPIRVGeneratorUniquePtr compute_shader_ptr;
    ShaderModuleUniquePtr               compute_shader_module_ptr;

    vertex_shader_ptr = GLSLShaderToSPIRVGenerator::create(
        m_device_ptr.get(),
        GLSLShaderToSPIRVGenerator::MODE_LOAD_SOURCE_FROM_FILE,
        "Assets/code/shader/GBuffer.vert",
        ShaderStage::VERTEX);
    fragment_shader_ptr = GLSLShaderToSPIRVGenerator::create(
        m_device_ptr.get(),
        GLSLShaderToSPIRVGenerator::MODE_LOAD_SOURCE_FROM_FILE,
        "Assets/code/shader/GBuffer.frag",
        ShaderStage::FRAGMENT);
    compute_shader_ptr = GLSLShaderToSPIRVGenerator::create(
        m_device_ptr.get(),
        GLSLShaderToSPIRVGenerator::MODE_LOAD_SOURCE_FROM_FILE,
        "Assets/code/shader/deferred.comp",
        ShaderStage::COMPUTE);

    vertex_shader_module_ptr = ShaderModule::create_from_spirv_generator(
        m_device_ptr.get(),
        vertex_shader_ptr.get());
    fragment_shader_module_ptr = ShaderModule::create_from_spirv_generator(
        m_device_ptr.get(),
        fragment_shader_ptr.get());
    compute_shader_module_ptr = ShaderModule::create_from_spirv_generator(
        m_device_ptr.get(),
        compute_shader_ptr.get());

    vertex_shader_module_ptr->set_name("Vertex shader module");
    fragment_shader_module_ptr->set_name("Fragment shader module");
    compute_shader_module_ptr->set_name("Compute shader module");

    m_vs_ptr.reset(
        new ShaderModuleStageEntryPoint(
            "main",
            move(vertex_shader_module_ptr),
            ShaderStage::VERTEX)
    );
    m_fs_ptr.reset(
        new ShaderModuleStageEntryPoint(
            "main",
            move(fragment_shader_module_ptr),
            ShaderStage::FRAGMENT)
    );
    m_cs_ptr.reset(
        new ShaderModuleStageEntryPoint(
            "main",
            move(compute_shader_module_ptr),
            ShaderStage::COMPUTE)
    );
}

void Engine::init_gfx_pipelines()
{
    
    GraphicsPipelineCreateInfoUniquePtr gfx_pipeline_create_info_ptr;
    
    gfx_pipeline_create_info_ptr = GraphicsPipelineCreateInfo::create(
        PipelineCreateFlagBits::NONE,
        m_renderpass_ptr.get(), 
        m_render_pass_subpass_GBuffer_id,
        *m_fs_ptr,
        ShaderModuleStageEntryPoint(), /* in_geometry_shader        */
        ShaderModuleStageEntryPoint(), /* in_tess_control_shader    */
        ShaderModuleStageEntryPoint(), /* in_tess_evaluation_shader */
        *m_vs_ptr);

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

    auto gfx_pipeline_manager_ptr(m_device_ptr->get_graphics_pipeline_manager());
    gfx_pipeline_manager_ptr->add_pipeline(
        move(gfx_pipeline_create_info_ptr),
        &m_gfx_pipeline_id);
    
}

void Engine::init_compute_pipelines()
{
    ComputePipelineCreateInfoUniquePtr compute_pipeline_create_info_ptr;

    compute_pipeline_create_info_ptr = ComputePipelineCreateInfo::create(
        PipelineCreateFlagBits::NONE,
        *m_cs_ptr);

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

    auto compute_pipeline_manager_ptr(m_device_ptr->get_compute_pipeline_manager());
    compute_pipeline_manager_ptr->add_pipeline(
        move(compute_pipeline_create_info_ptr),
        &m_compute_pipeline_id);
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

    image_subresource_range2.aspect_mask = ImageAspectFlagBits::DEPTH_BIT;
    image_subresource_range2.base_array_layer = 0;
    image_subresource_range2.base_mip_level = 0;
    image_subresource_range2.layer_count = 1;
    image_subresource_range2.level_count = 1;

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

        #pragma region 确保uniform缓冲已经写入
        BufferBarrier buffer_barrier(
            AccessFlagBits::HOST_WRITE_BIT,                 /* in_source_access_mask      */
            AccessFlagBits::UNIFORM_READ_BIT,               /* in_destination_access_mask */
            universal_queue_ptr->get_queue_family_index(),         /* in_src_queue_family_index  */
            universal_queue_ptr->get_queue_family_index(),         /* in_dst_queue_family_index  */
            m_mvp_uniform_buffer_ptr.get(),
            m_mvp_buffer_size_per_swapchain_image * n_command_buffer, /* in_offset                  */
            m_mvp_buffer_size_per_swapchain_image);

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
        #pragma endregion

        #pragma region 改变深度图像布局用于深度测试
        ImageBarrier depth_test_image_barrier(
            AccessFlagBits::SHADER_READ_BIT,                   /* source_access_mask       */
            AccessFlagBits::DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                AccessFlagBits::DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,  /* destination_access_mask  */
            ImageLayout::SHADER_READ_ONLY_OPTIMAL,              /* old_image_layout */
            ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL,      /* new_image_layout */
            universal_queue_ptr->get_queue_family_index(),
            universal_queue_ptr->get_queue_family_index(),
            m_depth_image_ptr.get(),
            image_subresource_range2);

        cmd_buffer_ptr->record_pipeline_barrier(
            PipelineStageFlagBits::COMPUTE_SHADER_BIT,                  /* src_stage_mask                 */
            PipelineStageFlagBits::EARLY_FRAGMENT_TESTS_BIT,            /* dst_stage_mask                 */
            DependencyFlagBits::NONE,
            0,                                                        /* in_memory_barrier_count        */
            nullptr,                                                  /* in_memory_barrier_ptrs         */
            0,                                                        /* in_buffer_memory_barrier_count */
            nullptr,                                                  /* in_buffer_memory_barrier_ptrs  */
            1,                                                        /* in_image_memory_barrier_count  */
            &depth_test_image_barrier);
        #pragma endregion

        #pragma region 改变附件图像布局用于片元着色器输出
        vector<ImageBarrier> image_barriers;

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
        #pragma endregion

        #pragma region 渲染GBuffer
        array<VkClearValue, 5>            attachment_clear_value;
        VkRect2D                          render_area;
        VkShaderStageFlags                shaderStageFlags = 0;

        attachment_clear_value[0].color = { 0.0f, 0.0f, 0.0f, 0.0f };
        attachment_clear_value[1].color = { 0.0f, 0.0f, 0.0f, 0.0f };
        attachment_clear_value[2].color = { 0.0f, 0.0f, 0.0f, 0.0f };
        attachment_clear_value[3].color = { uint32(255), uint32(0), uint32(0), uint32(0) };
        attachment_clear_value[4].depthStencil = { 1.0f, 0 };

        render_area.extent.height = m_height;
        render_area.extent.width = m_width;
        render_area.offset.x = 0;
        render_area.offset.y = 0;

        cmd_buffer_ptr->record_begin_render_pass(
            5, /* in_n_clear_values */
            attachment_clear_value.data(),
            m_fbo.get(),
            render_area,
            m_renderpass_ptr.get(),
            SubpassContents::INLINE);
        
        const uint32_t data_ub_offset = static_cast<uint32_t>(m_mvp_buffer_size_per_swapchain_image * n_command_buffer);
        DescriptorSet* ds_ptr[1] = { m_dsg_ptr->get_descriptor_set(1) };

        cmd_buffer_ptr->record_bind_pipeline(
            PipelineBindPoint::GRAPHICS,
            m_gfx_pipeline_id);

        cmd_buffer_ptr->record_bind_descriptor_sets(
            PipelineBindPoint::GRAPHICS,
            Engine::Instance()->getPineLine(),
            0, /* firstSet */
            1, /* setCount：传入的描述符集与shader中的set一一对应 */
            ds_ptr,
            1,                /* dynamicOffsetCount */
            &data_ub_offset); /* pDynamicOffsets    */

        m_model->draw(cmd_buffer_ptr.get());
        cmd_buffer_ptr->record_end_render_pass();
        #pragma endregion

        #pragma region 改变附件图像布局用于计算着色器读取
        vector<ImageBarrier> image_barriers2;

        image_barriers2.push_back(
            ImageBarrier(
                AccessFlagBits::COLOR_ATTACHMENT_WRITE_BIT, /* source_access_mask       */
                AccessFlagBits::SHADER_READ_BIT,           /* destination_access_mask  */
                ImageLayout::COLOR_ATTACHMENT_OPTIMAL,      /* old_image_layout */
                ImageLayout::SHADER_READ_ONLY_OPTIMAL,      /* new_image_layout */
                universal_queue_ptr->get_queue_family_index(),
                universal_queue_ptr->get_queue_family_index(),
                m_tangent_frame_image_ptr.get(),
                image_subresource_range));

        image_barriers2.push_back(
            ImageBarrier(
                AccessFlagBits::COLOR_ATTACHMENT_WRITE_BIT, /* source_access_mask       */
                AccessFlagBits::SHADER_READ_BIT,           /* destination_access_mask  */
                ImageLayout::COLOR_ATTACHMENT_OPTIMAL,      /* old_image_layout */
                ImageLayout::SHADER_READ_ONLY_OPTIMAL,      /* new_image_layout */
                universal_queue_ptr->get_queue_family_index(),
                universal_queue_ptr->get_queue_family_index(),
                m_uv_and_depth_gradient_image_ptr.get(),
                image_subresource_range));

        image_barriers2.push_back(
            ImageBarrier(
                AccessFlagBits::COLOR_ATTACHMENT_WRITE_BIT, /* source_access_mask       */
                AccessFlagBits::SHADER_READ_BIT,           /* destination_access_mask  */
                ImageLayout::COLOR_ATTACHMENT_OPTIMAL,      /* old_image_layout */
                ImageLayout::SHADER_READ_ONLY_OPTIMAL,      /* new_image_layout */
                universal_queue_ptr->get_queue_family_index(),
                universal_queue_ptr->get_queue_family_index(),
                m_uv_gradient_image_ptr.get(),
                image_subresource_range));

        image_barriers2.push_back(
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
            image_barriers2.size(),                                  /* in_image_memory_barrier_count  */
            image_barriers2.data());
        #pragma endregion

        #pragma region 改变深度图像布局用于计算着色器读取
        ImageBarrier depth_read_image_barrier(
            AccessFlagBits::DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                AccessFlagBits::DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,  /* source_access_mask       */
            AccessFlagBits::SHADER_READ_BIT,                         /* destination_access_mask  */  
            ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL,                   /* old_image_layout */
            ImageLayout::SHADER_READ_ONLY_OPTIMAL,           /* new_image_layout */
            universal_queue_ptr->get_queue_family_index(),
            universal_queue_ptr->get_queue_family_index(),
            m_depth_image_ptr.get(),
            image_subresource_range2);

        cmd_buffer_ptr->record_pipeline_barrier(
            PipelineStageFlagBits::LATE_FRAGMENT_TESTS_BIT,           /* src_stage_mask                 */
            PipelineStageFlagBits::COMPUTE_SHADER_BIT,                /* dst_stage_mask                 */
            DependencyFlagBits::NONE,
            0,                                                        /* in_memory_barrier_count        */
            nullptr,                                                  /* in_memory_barrier_ptrs         */
            0,                                                        /* in_buffer_memory_barrier_count */
            nullptr,                                                  /* in_buffer_memory_barrier_ptrs  */
            1,                                                        /* in_image_memory_barrier_count  */
            &depth_read_image_barrier);
        #pragma endregion

        #pragma region 改变交换链图像布局用于计算着色器写入
        ImageBarrier write_image_barrier(
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
            &write_image_barrier);
        #pragma endregion

        #pragma region 延迟纹理采样和光照
        const uint32_t data_ub_offset2[] = {
            static_cast<uint32_t>(m_sunLight_buffer_size_per_swapchain_image * n_command_buffer),
            static_cast<uint32_t>(m_camera_buffer_size_per_swapchain_image * n_command_buffer)
        };

        cmd_buffer_ptr->record_bind_pipeline(
            PipelineBindPoint::COMPUTE,
            m_compute_pipeline_id);

        DescriptorSet* ds2_ptr[4] = {
            m_dsg_ptr->get_descriptor_set(0),
            m_dsg_ptr->get_descriptor_set(2),
            m_dsg_ptr->get_descriptor_set(3),
            m_dsg_ptr->get_descriptor_set(4 + n_command_buffer) };

        cmd_buffer_ptr->record_bind_descriptor_sets(
            PipelineBindPoint::COMPUTE,
            Engine::Instance()->getPineLine(false),
            0, /* firstSet */
            4, /* setCount：传入的描述符集与shader中的set一一对应 */
            ds2_ptr,
            2,                /* dynamicOffsetCount */
            data_ub_offset2); /* pDynamicOffsets    */

        m_deferred_constants.RTSize.x = m_width;
        m_deferred_constants.RTSize.y = m_height;
        cmd_buffer_ptr->record_push_constants(
            Engine::Instance()->getPineLine(false),
            ShaderStageFlagBits::COMPUTE_BIT,
            0, /* in_offset */
            sizeof(DeferredConstants),
            &m_deferred_constants);

        cmd_buffer_ptr->record_dispatch(
            (m_width + 7) / 8,
            (m_height + 7) / 8,
            1);
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
    init_image();
    init_image_view();
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
        else if (acquire_result != SwapchainOperationErrorCode::SUCCESS 
            && acquire_result != SwapchainOperationErrorCode::SUBOPTIMAL)
        {
            throw runtime_error("failed to acquire swap chain image!");
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
    static auto startTime = chrono::high_resolution_clock::now();
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

    MVPUniform mvp;
    mvp.model = rotate(mat4(1.0f), /*time * */radians(30.0f), vec3(0.0f, 1.0f, 0.0f))
        *scale(mat4(1.0f), vec3(0.015f, 0.015f, 0.015f));
    mvp.view = m_camera->GetViewMatrix();
    mvp.proj = m_camera->GetProjMatrix();

    m_mvp_uniform_buffer_ptr->write(
        in_n_swapchain_image * m_mvp_buffer_size_per_swapchain_image, /* start_offset */
        sizeof(mvp),
        &mvp,
        m_device_ptr->get_universal_queue(0));

    SunLightUniform sun_light;
    sun_light.SunDirectionWS = vec3(0.5f, 0.1f, 0.5f);
    sun_light.SunIrradiance = vec3(10.0f, 10.0f, 10.0f);
    m_sunLight_uniform_buffer_ptr->write(
        in_n_swapchain_image * m_sunLight_buffer_size_per_swapchain_image, /* start_offset */
        sizeof(sun_light),
        &sun_light,
        m_device_ptr->get_universal_queue(0));

    CameraUniform camera;
    camera.CameraPosWS = m_camera->m_position;
    camera.InvViewProj = inverse(mvp.proj * mvp.view);
    m_camera_uniform_buffer_ptr->write(
        in_n_swapchain_image * m_camera_buffer_size_per_swapchain_image, /* start_offset */
        sizeof(camera),
        &camera,
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
    m_tangent_frame_image_view_ptr.reset();
    m_uv_and_depth_gradient_image_view_ptr.reset();
    m_uv_gradient_image_view_ptr.reset();
    m_material_id_image_view_ptr.reset();

    m_depth_image_ptr.reset();
    m_tangent_frame_image_ptr.reset();
    m_uv_and_depth_gradient_image_ptr.reset();
    m_uv_gradient_image_ptr.reset();
    m_material_id_image_ptr.reset();

    for (uint32_t n_swapchain_image = 0; n_swapchain_image < N_SWAPCHAIN_IMAGES; ++n_swapchain_image)
    {
        m_command_buffers[n_swapchain_image].reset();
    }
    
    m_fbo.reset();

    if (m_gfx_pipeline_id != UINT32_MAX)
    {
        auto gfx_pipeline_manager_ptr = m_device_ptr->get_graphics_pipeline_manager();
        gfx_pipeline_manager_ptr->delete_pipeline(m_gfx_pipeline_id);
        m_gfx_pipeline_id = UINT32_MAX;
    }

    if (m_compute_pipeline_id != UINT32_MAX)
    {
        auto compute_pipeline_manager_ptr(m_device_ptr->get_compute_pipeline_manager());
        compute_pipeline_manager_ptr->delete_pipeline(m_compute_pipeline_id);
        m_compute_pipeline_id = UINT32_MAX;
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
    m_mvp_uniform_buffer_ptr.reset();
    m_sunLight_uniform_buffer_ptr.reset();
    m_camera_uniform_buffer_ptr.reset();
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
            "[!] %s\n\n",
            in_message_ptr);
    }
}

Anvil::Format Engine::SelectSupportedFormat(
    const vector<Anvil::Format>& candidates,
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
    throw runtime_error("failed to find supported format!");
}
#pragma endregion