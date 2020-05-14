#include "stdafx.h"
#include "material.h"

Material::Material(uint32_t id):
	m_material_id(id)
{
}

void Material::set_texture(shared_ptr<Texture> texture, aiTextureType type)
{
	switch (type)
	{
		case aiTextureType_DIFFUSE:
			m_albedo_texture = texture;
			break;
		case aiTextureType_HEIGHT:
			m_normal_texture = texture;
			break;
		case aiTextureType_SHININESS:
			m_roughness_texture = texture;
			break;
		case aiTextureType_AMBIENT:
			m_metallic_texture = texture;
			break;
	}
}

uint32_t Material::get_texture_id(aiTextureType type)
{
	switch (type)
	{
		case aiTextureType_DIFFUSE:
			return m_albedo_texture->get_texture_id();
			break;
		case aiTextureType_HEIGHT:
			return m_normal_texture->get_texture_id();
			break;
		case aiTextureType_SHININESS:
			return m_roughness_texture->get_texture_id();
			break;
		case aiTextureType_AMBIENT:
			return m_metallic_texture->get_texture_id();
			break;
	}
	
}

uint32_t* Material::get_material_id()
{
	return &m_material_id;
}

Material::~Material()
{
}
