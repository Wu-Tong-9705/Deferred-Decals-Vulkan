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
	string m_directory;//ģ���ļ�·��
	vector<shared_ptr<Mesh>> m_meshes;//�������񣨰������㡢����������ָ�룩����
	vector<shared_ptr<Material>> m_materials;//���в��ʣ���������ָ�룩����
	vector<shared_ptr<Texture>> m_textures;//��������

	void load_model(string const& path);
	shared_ptr<Texture> load_texture_for_material(aiMaterial* mat, aiTextureType type);
};

