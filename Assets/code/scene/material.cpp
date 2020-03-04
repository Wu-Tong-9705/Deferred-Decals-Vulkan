#include "stdafx.h"
#include "material.h"

Material::Material(aiMaterial* material)
{
	vec3 color;
	material->Get(AI_MATKEY_COLOR_AMBIENT, color);
	m_albedo = vec4(color, 1.0);

}

void Material::set_texture(shared_ptr<Texture> texture)
{
	m_diffuse_texture = texture;
}

uint32_t* Material::get_texture_id()
{
	return m_diffuse_texture->get_texture_id();
}

Material::~Material()
{
}
