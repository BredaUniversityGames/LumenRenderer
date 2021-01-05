#include "lmnpch.h"
#include "SceneManager.h"
#include "Node.h"
#include "Transform.h"
#include "../Renderer/ILumenMesh.h"

//#include <string>
#include <memory>

namespace hlp
{
	// Helped class to allow viewing a vectors data in another format
	// without having to copy the vector

	template<typename ViewType, typename VectorType>
	class VectorView
	{
	public:
		VectorView(std::vector<VectorType>& a_Vector);
		~VectorView();

		uint64_t Size();

		ViewType* operator[](uint64_t a_Index);

	private:
		std::vector<VectorType> m_Vector;
	};

	template <typename ViewType, typename VectorType>
	VectorView<ViewType, VectorType>::VectorView(std::vector<VectorType>& a_Vector)
		: m_Vector(a_Vector)
	{

	}

	template <typename ViewType, typename VectorType>
	VectorView<ViewType, VectorType>::~VectorView()
	{
	}

	template <typename ViewType, typename VectorType>
	uint64_t VectorView<ViewType, VectorType>::Size()
	{
		return (m_Vector.size() * sizeof(VectorType)) / sizeof(ViewType);
	}

	template <typename ViewType, typename VectorType>
	ViewType* VectorView<ViewType, VectorType>::operator[](uint64_t a_Index)
	{
		assert(a_Index < Size());
		ViewType* data = reinterpret_cast<ViewType*>(m_Vector.data());

		data += a_Index;

		return data;
	}
}


Lumen::SceneManager::Scene* Lumen::SceneManager::LoadGLTF(std::string a_Path, glm::mat4& a_TransformMat)
{
	auto findIter = m_LoadedScenes.find(a_Path);

	if(findIter != m_LoadedScenes.end())
	{
		return &(*findIter).second;
	}

	auto& res = m_LoadedScenes[a_Path];		// create new scene at path key
	auto doc = fx::gltf::LoadFromText(a_Path);

	LoadMeshes(doc, res);
	LoadNodes(doc, res, a_TransformMat);
	
	return& res;
}

void Lumen::SceneManager::LoadNodes(fx::gltf::Document& a_Doc, Scene& a_Scene, glm::mat4& a_TransformMat)
{
	//store offsets in the case there is somehow already data loaded in the scene object
	const int nodeOffset = static_cast<int>(a_Scene.m_NodePool.size()) + 1;
	const int meshOffset = static_cast<int>(a_Scene.m_MeshPool.size());
	
	std::vector<std::shared_ptr<Node>> nodes;	//only supporting one scene per file. Seems to work fine 99% of the time

	std::shared_ptr<Node> baseNode = std::make_shared<Node>();
	baseNode->m_NodeID = nodeOffset - 1;
	baseNode->m_LocalTransform = std::make_unique<Transform>(a_TransformMat);
	nodes.push_back(baseNode);
	
	for (auto& fxNodeIdx : a_Doc.scenes.at(0).nodes)
	{
		const fx::gltf::Node& fxNode = a_Doc.nodes.at(fxNodeIdx);
		
		std::shared_ptr<Node> newNode = std::make_shared<Node>();
		newNode->m_MeshID = -1 ? -1 : (fxNode.mesh + meshOffset);
		newNode->m_Name = fxNode.name;
		newNode->m_NodeID = static_cast<int>(nodes.size());

		for (int i = 0; i < static_cast<int>(fxNode.children.size()); i++)
		{
			newNode->m_ChilIndices.push_back(fxNode.children.at(i) + nodeOffset);
		}

		if(fxNode.translation.size() == 3)
		{
			newNode->m_LocalTransform->SetPosition(glm::vec3(
				fxNode.translation[0],
				fxNode.translation[1],
				fxNode.translation[2]
			));
		}

		if (fxNode.rotation.size() == 4)
		{
			newNode->m_LocalTransform->SetRotation(glm::quat(
				fxNode.rotation[0],
				fxNode.rotation[1],
				fxNode.rotation[2],
				fxNode.rotation[3]
			));
		}

		if (fxNode.scale.size() == 3)
		{
			newNode->m_LocalTransform->SetScale(glm::vec3(
				fxNode.scale[0],
				fxNode.scale[1],
				fxNode.scale[2]
			));
		}
		
		a_Scene.m_NodePool.push_back(newNode);
	}
}

void Lumen::SceneManager::LoadMeshes(fx::gltf::Document& a_Doc, Scene& a_Scene, glm::mat4& a_TransformMat)
{
	const int meshOffset = static_cast<int>(a_Scene.m_MeshPool.size());

	std::vector<std::shared_ptr<ILumenMesh>> meshes;

	// pass binary data into mesh
	
	for (auto& fxMesh : a_Doc.meshes)
	{

		
		for (auto& fxPrim : fxMesh.primitives)
		{
			for (auto& fxAttribute : fxPrim.attributes)
			{
				std::vector<uint8_t> binary;
				
				if(fxAttribute.first == "POSITION")
				{
					// store position accessor
					//fxAttribute.second

					binary = LoadBinary(a_Doc, fxAttribute.second);
					//make vertex buffer of this, make function
					
				}
				else if (fxAttribute.first == "TEXCOORD_0")
				{
					//binary = 
				}
				else if (fxAttribute.first == "COLOR_0")
				{
					
				}
				else if (fxAttribute.first == "NORMAL")
				{
					
				}

				
			}
			
			//index to accessor
			auto& acc = fxPrim.attributes["POSITION"];
			auto& accessprYe = a_Doc.accessors[acc];
			auto& bufferView = a_Doc.bufferViews[accessprYe.bufferView];

			auto binary = a_Doc.buffers[bufferView.buffer];


			
			
			
		}
		//meshes.push_back(static_cast<ILumenMesh>(fxMesh));
	}
	
	//a_Scene.m_MeshPool = 
	
}

std::vector<uint8_t> Lumen::SceneManager::LoadBinary(fx::gltf::Document& a_Doc, uint32_t a_AccessorIndx)
{
	std::vector<unsigned char> data;
	// Load raw data at accessor index
	auto bufferAccessor = a_Doc.accessors[a_AccessorIndx];
	auto bufferView = a_Doc.bufferViews[bufferAccessor.bufferView];
	auto buffer = a_Doc.buffers[bufferView.buffer];

	uint32_t compCount = GetComponentCount(bufferAccessor);
	uint32_t compSize = GetComponentSize(bufferAccessor);
	uint32_t attSize = compCount * compSize;

	auto stride = std::max(attSize, bufferView.byteStride);

	data.resize(bufferView.byteLength / stride * attSize);

	auto bufferOffset = 0;
	auto gltfBufferOffset = bufferView.byteOffset + bufferAccessor.byteOffset;

	for (uint32_t i = 0; i < bufferAccessor.count; i++)
	{
		memcpy(data.data() + bufferOffset, buffer.data.data() + gltfBufferOffset + bufferOffset, attSize);
		bufferOffset += stride;
	}

	return data;
}

uint32_t Lumen::SceneManager::GetComponentSize(fx::gltf::Accessor& a_Accessor)
{

	switch (a_Accessor.componentType)
	{
	case fx::gltf::Accessor::ComponentType::Byte:
	case fx::gltf::Accessor::ComponentType::UnsignedByte:
		return 1;
		break;
	case fx::gltf::Accessor::ComponentType::Short:
	case fx::gltf::Accessor::ComponentType::UnsignedShort:
		return 2;
		break;
	case fx::gltf::Accessor::ComponentType::Float:
	case fx::gltf::Accessor::ComponentType::UnsignedInt:
		return 4;
		break;
	default:
		LMN_ASSERT(0 && "Failed to load GLTF file");
		return 0;
	}

}

uint32_t Lumen::SceneManager::GetComponentCount(fx::gltf::Accessor& a_Accessor)
{
	switch (a_Accessor.type)
	{
	case fx::gltf::Accessor::Type::Scalar:
		return 1;
		break;
	case fx::gltf::Accessor::Type::Vec2:
		return 2;
		break;
	case fx::gltf::Accessor::Type::Vec3:
		return 3;
		break;
	case fx::gltf::Accessor::Type::Mat2:
		return 4;
		break;
	case fx::gltf::Accessor::Type::Mat3:
		return 9;
		break;
	case fx::gltf::Accessor::Type::Mat4:
		return 16;
		break;
	default:
		LMN_ASSERT(0 && "Failed to load GLTF file");
		return 0;
	}
}

