#pragma once
#include "stdafx.h"
#include "mesh.h"
#include "texture.h"

class Model
{
public:
	Model(string const& path);
	int get_texture_num();
	void add_combined_image_samplers();
	void draw(PrimaryCommandBuffer* cmd_buffer_ptr, DescriptorSet* ds_ptr[2], int first, uint32_t data_ub_offset);

	~Model();

private:
	string m_directory;//模型文件路径
	vector<shared_ptr<Mesh>> m_meshes;//所有网格（包含顶点、索引、材质指针）数据
	vector<shared_ptr<Material>> m_materials;//所有材质（包含纹理指针）数据
	vector<shared_ptr<Texture>> m_textures;//所有纹理

	void load_model(string const& path);
	shared_ptr<Texture> load_texture_for_material(aiMaterial* mat, aiTextureType type);
};

