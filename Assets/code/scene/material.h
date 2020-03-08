#pragma once
#include "stdafx.h"
#include "texture.h"



class Material
{
public:
	Material(uint32_t id);
	void set_texture(shared_ptr<Texture>, aiTextureType type);
	uint32_t* get_material_id();
	uint32_t get_texture_id(aiTextureType type);
	~Material();

private:
	uint32_t m_material_id;
	shared_ptr<Texture> m_albedo_texture;
	shared_ptr<Texture> m_normal_texture;
	shared_ptr<Texture> m_roughness_texture;
	shared_ptr<Texture> m_metallic_texture;

};

