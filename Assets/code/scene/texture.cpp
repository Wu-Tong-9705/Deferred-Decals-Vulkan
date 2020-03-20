#include "stdafx.h"
#include "texture.h"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image/stb_image.h"
#include "stb_image/stb_image_resize.h"

Texture::Texture(const char* path, string directory, uint32_t id)
{
	m_texture_id = id;
	m_path = path;
	load_texture(directory + "/" + m_path);
}

void Texture::load_texture(string path)
{
	auto allocator_ptr = MemoryAllocator::create_oneshot(Engine::Instance()->getDevice());

	#pragma region 从文件获取纹理图像数据
	int texWidth, texHeight, texChannels;
	stbi_uc* pixels = stbi_load(path.data(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
	/*m_mipLevels = static_cast<uint32_t>(
		std::floor(std::log2((texWidth > texHeight) ? texWidth : texHeight))) + 1;*/
	if (!pixels)
	{
		throw std::runtime_error("failed to load texture image!");
	}
	#pragma endregion

	#pragma region 生成mipmap数据（已关闭）
	/*vector<Anvil::MipmapRawData> mipmapRawDatas(m_mipLevels);
	vector<stbi_uc*> mipPixels(m_mipLevels);
	int32_t mipWidth = texWidth;
	int32_t mipHeight = texHeight;
	int32_t imageSize = mipWidth * mipHeight * 4;
	for (int i = 0; i < m_mipLevels; i++)
	{
		if (i == 0)
		{
			mipPixels[0] = pixels;
		}
		else
		{
			int32_t nextMipWidth = mipWidth > 1 ? mipWidth / 2 : 1;
			int32_t nextMipHeight = mipHeight > 1 ? mipHeight / 2 : 1;
			imageSize = nextMipWidth * nextMipHeight * 4;
			mipPixels[i] = (stbi_uc*)malloc(imageSize + 1);
			stbir_resize_uint8(
				mipPixels[i - 1],
				mipWidth,
				mipHeight,
				0,
				mipPixels[i],
				nextMipWidth,
				nextMipHeight,
				0,
				4);
			mipWidth = nextMipWidth;
			mipHeight = nextMipHeight;
		}

		mipmapRawDatas[i] = MipmapRawData::create_2D_from_uchar_ptr(
			ImageAspectFlagBits::COLOR_BIT,
			i,
			mipPixels[i],
			imageSize,
			mipWidth * 4);
	}*/
	#pragma endregion

	#pragma region 创建纹理图像
	vector<Anvil::MipmapRawData> mipmapRawDatas(1);
	mipmapRawDatas[0] = MipmapRawData::create_2D_from_uchar_ptr(
		ImageAspectFlagBits::COLOR_BIT,
		0,
		pixels,
		texWidth * texHeight * 4,
		texWidth * 4);

	auto image_create_info_ptr = ImageCreateInfo::create_no_alloc(
		Engine::Instance()->getDevice(),
		ImageType::_2D,
		Format::R8G8B8A8_UNORM,
		ImageTiling::OPTIMAL,
		ImageUsageFlagBits::SAMPLED_BIT,
		texWidth,
		texHeight,
		1,
		1,
		SampleCountFlagBits::_1_BIT,
		QueueFamilyFlagBits::COMPUTE_BIT | QueueFamilyFlagBits::GRAPHICS_BIT,
		SharingMode::EXCLUSIVE,
		false,
		ImageCreateFlagBits::NONE,
		ImageLayout::SHADER_READ_ONLY_OPTIMAL,
		&mipmapRawDatas);

	m_texture_image_ptr = Image::create(move(image_create_info_ptr));
	m_texture_image_ptr->set_name_formatted("Texture #%s", m_path.c_str());

	allocator_ptr->add_image_whole(
		m_texture_image_ptr.get(),
		MemoryFeatureFlagBits::NONE);
	#pragma endregion

	#pragma region 创建纹理图像视图
	auto image_view_create_info_ptr = ImageViewCreateInfo::create_2D(
		Engine::Instance()->getDevice(),
		m_texture_image_ptr.get(),
		0,
		0,
		1,
		ImageAspectFlagBits::COLOR_BIT,
		Format::R8G8B8A8_UNORM,
		ComponentSwizzle::R,
		ComponentSwizzle::G,
		ComponentSwizzle::B,
		ComponentSwizzle::A);

	m_texture_image_view_ptr = ImageView::create(move(image_view_create_info_ptr));
	#pragma endregion
}

const char* Texture::get_path()
{
	return m_path.data();
}

uint32_t Texture::get_texture_id()
{
	return m_texture_id;
}

void Texture::add_combined_image_sampler()
{
	Engine::Instance()->getTextureCombinedImageSamplersBinding()->push_back(
		DescriptorSet::CombinedImageSamplerBindingElement(
			ImageLayout::SHADER_READ_ONLY_OPTIMAL,
			m_texture_image_view_ptr.get(),
			Engine::Instance()->getSampler()));
}

Texture::~Texture()
{
	m_texture_image_view_ptr.reset();
	m_texture_image_ptr.reset();
}
