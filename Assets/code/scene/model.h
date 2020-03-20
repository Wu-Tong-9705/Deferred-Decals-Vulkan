#pragma once
#include "stdafx.h"
#include "mesh.h"
#include "texture.h"
#define N_DECALS (8)


class Model
{
public:
	Model(string const& path);
	int get_texture_num();
	int get_material_num();
	void add_combined_image_samplers();
	void init_texture_indices();
	vector<TextureIndicesUniform>* get_texture_indices();
	void draw(PrimaryCommandBuffer* cmd_buffer_ptr);

	~Model();

private:
	string m_directory;//ģ���ļ�·��
	vector<shared_ptr<Mesh>> m_meshes;//�������񣨰������㡢����������ָ�룩����
	vector<shared_ptr<Material>> m_materials;//���в��ʣ���������ָ�룩����
	vector<shared_ptr<Texture>> m_textures;//��������
	vector<TextureIndicesUniform> m_texture_indices_uniform_data;

	void load_model(string const& path);
	shared_ptr<Texture> load_texture_for_material(aiMaterial* mat, aiTextureType type, unsigned int id = 0);
};

