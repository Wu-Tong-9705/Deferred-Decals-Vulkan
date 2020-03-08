#pragma once
#include "stdafx.h"

class Texture
{
public:
	Texture(const char* path, string directory, uint32_t id);
	void load_texture(string path);
	const char* get_path();
	uint32_t get_texture_id();
	void add_combined_image_sampler();

	~Texture();

private:
	string m_path;
	uint32_t m_texture_id;
	ImageUniquePtr m_texture_image_ptr;
	ImageViewUniquePtr m_texture_image_view_ptr;
};

