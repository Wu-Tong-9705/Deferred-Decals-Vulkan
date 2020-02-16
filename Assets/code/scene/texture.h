#pragma once
#include "stdafx.h"

class Texture
{
public:
	Texture(const char* path, string directory);
	void load_texture(string path);
	const char* get_path();
	int get_n_set();
	void set_dsg_binding_item(int n_set);

	~Texture();

private:
	string m_path;
	uint32_t m_mipLevels;
	int m_n_set;
	Anvil::ImageUniquePtr m_texture_image_ptr;
	Anvil::ImageViewUniquePtr m_texture_image_view_ptr;
	Anvil::SamplerUniquePtr m_texture_sampler_ptr;
};

