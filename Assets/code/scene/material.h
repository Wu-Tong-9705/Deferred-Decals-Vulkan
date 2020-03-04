#pragma once
#include "stdafx.h"
#include "texture.h"

class Material
{
public:
	Material(aiMaterial* material);
	void set_texture(shared_ptr<Texture>);
	uint32_t* get_texture_id();
	~Material();

private:
	glm::vec4 m_albedo;
	shared_ptr<Texture> m_diffuse_texture;

};

