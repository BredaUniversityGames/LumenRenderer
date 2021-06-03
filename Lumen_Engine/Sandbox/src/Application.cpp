#include <Lumen.h>
#include <string>
#include <filesystem>
#include <algorithm>

#include "OutputLayer.h"

#include "imgui/imgui.h"
#include "ModelLoading/Node.h"

#include "GLFW/include/GLFW/glfw3.h"
#include "Lumen/ModelLoading/SceneManager.h"

#include "AppConfiguration.h"
#include "Framework/CudaUtilities.h"

#include <chrono>

#ifdef WAVEFRONT
#include "../../LumenPT/src/Framework/WaveFrontRenderer.h"
#else
#include "../../LumenPT/src/Framework/OptiXRenderer.h"
#endif


//#include "imgui/imgui.h"

namespace Lumen {
    class WindowsWindow;
}

namespace LumenPTConsts
{
	const static std::string ShaderPath = "whatever";
}

class ExampleLayer : public Lumen::Layer
{
public:
	ExampleLayer()
		: Layer("Example")
	{
		
	}
	
	void OnUpdate() override
	{
		if(Lumen::Input::IsKeyPressed(LMN_KEY_TAB))
			LMN_INFO("Tab  key is pressed");
	}

	virtual void OnImGuiRender() override
	{
		ImGui::Begin("Test");
		ImGui::Text("Hello Lumen Renderer Engine, We gon' path trace the hell outta you.");
		ImGui::End();
	}

	void OnEvent(Lumen::Event& event) override
	{

		if(event.GetEventType() == Lumen::EventType::KeyPressed)
		{
			Lumen::KeyPressedEvent& e = static_cast<Lumen::KeyPressedEvent&>(event);
			//LMN_TRACE("{0}", static_cast<char>(e.GetKeyCode()));
		}
	}
};


class Sandbox : public Lumen::LumenApp
{
public:
	Sandbox()
	{
		glfwMakeContextCurrent(reinterpret_cast<GLFWwindow*>(GetWindow().GetNativeWindow()));
		//PushOverlay(new Lumen::ImGuiLayer());

		const std::filesystem::path configFilePath = std::filesystem::current_path() += "/Config.json";

		AppConfiguration& config = AppConfiguration::GetInstance();
		config.Load(configFilePath, true, true);

		std::shared_ptr<LumenRenderer> renderer = nullptr;

#ifdef WAVEFRONT

		renderer = std::make_shared<WaveFront::WaveFrontRenderer>();

		WaveFront::WaveFrontSettings settings{};

		settings.m_ShadersFilePathSolids = config.GetFileShaderSolids();
		settings.m_ShadersFilePathVolumetrics = config.GetFileShaderVolumetrics();

		settings.depth = 5;
		settings.minIntersectionT = 0.1f;
		settings.maxIntersectionT = 5000.f;
		settings.renderResolution = { 800, 600 };
		settings.outputResolution = { 800, 600 };
		settings.blendOutput = false;	//When true will blend output instead of overwriting it (high res image over time if static scene).

		std::static_pointer_cast<WaveFront::WaveFrontRenderer>(renderer)->Init(settings);

		CHECKLASTCUDAERROR;

#else

		OptiXRenderer::InitializationData initData;
		initData.m_AssetDirectory = config.GetDirectoryAssets();
		initData.m_ShaderDirectory = config.GetDirectoryShaders();

		initData.m_MaxDepth = 5;
		initData.m_RaysPerPixel = 1;
		initData.m_ShadowRaysPerPixel = 1;

		initData.m_RenderResolution = { 800, 600 };
		initData.m_OutputResolution = { 800, 600 };

		renderer = std::make_unique<OptiXRenderer>(initData);
#endif

		OutputLayer* contextLayer = new OutputLayer;
		contextLayer->SetPipeline(renderer);
		PushLayer(contextLayer);

		//temporary stuff to avoid absolute paths to gltf cube
		std::filesystem::path p = std::filesystem::current_path();

		while (p.has_parent_path())
		{
			bool found = false;
			for (auto child : std::filesystem::directory_iterator(p))
			{
				if (child.is_directory() && child.path().filename() == "Sandbox")
				{
					found = true;
					p = child.path().parent_path();
				}
			}

			if (found) break;

			p = p.parent_path();
		}

		std::filesystem::current_path(p);
		std::string p_string{ p.string() };
		std::replace(p_string.begin(), p_string.end(), '\\', '/');

		std::filesystem::path p2 = std::filesystem::current_path();
		std::string p_string2{ p2.string() };
		std::replace(p_string2.begin(), p_string2.end(), '\\', '/');

		const std::string meshPath = p_string.append("/Sandbox/assets/models/Sponza/");
		const std::string meshPath2 = p_string2.append("/Sandbox/assets/models/EmissiveSphere/");
		//Base path for meshes.

		//Mesh name
		const std::string meshName = "Sponza.gltf";
		const std::string meshName2 = "EmissiveSphere.gltf";

		//p_string.append("/Sandbox/assets/models/Sponza/Sponza.gltf");
		LMN_TRACE(p_string);

	    m_SceneManager->SetPipeline(*contextLayer->GetPipeline());

		auto begin = std::chrono::high_resolution_clock::now();

		auto res = m_SceneManager->LoadGLTF(meshName, meshPath);

		auto end = std::chrono::high_resolution_clock::now();

		auto milli = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();

		printf("\n\nTime elapsed to load model: %li milliseconds\n\n", milli);

		//__debugbreak();

		auto res2 = m_SceneManager->LoadGLTF(meshName2, meshPath2);

		auto lumenPT = contextLayer->GetPipeline();

		LumenRenderer::SceneData scData = {};
		
		lumenPT->m_Scene = lumenPT->CreateScene(scData);
		
		//Loop over the nodes in the scene, and add their meshes if they have one.

		float xOffset = -1200.f;

		uint32_t seed = 38947987;
		seed = RandomInt(seed);

		lumenPT->m_Scene = res->m_Scenes[0];

		//make sponza normal sized.
		lumenPT->m_Scene->m_MeshInstances[0]->m_Transform.SetScale({ 1.f, 1.f, 1.f });

		for(int i = 0; i < 30; ++i)
		{
			for (auto& node : res2->m_NodePool)
			{
				auto meshId = node->m_MeshID;
				if (meshId >= 0)
				{
					//auto mesh = lumenPT->m_Scene->AddMesh();
					//mesh->SetMesh(res2->m_MeshPool[meshId]);
					////mesh->m_Transform.CopyTransform(*node->m_LocalTransform);
					//float p = i;
					//mesh->m_Transform.SetPosition(glm::vec3(xOffset, 100.f + (p*p), 0.f));
					//mesh->m_Transform.SetScale(glm::vec3(2.0f * (static_cast<float>((i + 1) * 2) / 4.f)));
					//glm::vec3 rgb = glm::vec3(RandomFloat(seed), RandomFloat(seed), RandomFloat(seed));
					//mesh->SetEmissiveness(Lumen::EmissionMode::OVERRIDE, rgb, 50.f);
					//mesh->UpdateAccelRemoveThis();
				}
			}
			auto mesh = lumenPT->m_Scene->AddMesh();
			mesh->SetMesh(res2->m_MeshPool[0]);
			//mesh->m_Transform.CopyTransform(*node->m_LocalTransform);
			float p = i;
			mesh->m_Transform.SetPosition(glm::vec3(xOffset, 100.f + (p * p), 0.f));
			mesh->m_Transform.SetScale(glm::vec3(2.0f * (static_cast<float>((i + 1) * 2) / 4.f)));
			mesh->SetEmissiveness(Lumen::EmissionMode::OVERRIDE, glm::vec3(1.f, 1.f, 1.f), 50.f);
			mesh->UpdateAccelRemoveThis();
			xOffset += 50;
		}
		
		//
		//for(auto& node: res->m_NodePool)
		//{
		//	auto meshId = node->m_MeshID;
		//	if(meshId >= 0)
		//	{
		//		auto mesh = lumenPT->m_Scene->AddMesh();
		//		mesh->SetMesh(res->m_MeshPool[meshId]);
		//		mesh->m_Transform.CopyTransform(*node->m_LocalTransform);
		//		mesh->SetEmissiveness(Lumen::EmissionMode::ENABLED, glm::vec3(1.f, 1.f, 1.f), 3000.f);	//Make more bright
		//		mesh->UpdateAccelRemoveThis();
		//	    //mesh->m_Transform.SetPosition(glm::vec3(0.f, 0.f, 15.0f));
		//		//mesh->m_Transform.SetScale(glm::vec3(1.0f));
		//	}

		//}

		//auto mesh = lumenPT->m_Scene->AddMesh();
		//mesh->SetMesh(res->m_MeshPool[0]);
		////mesh->m_Transform.CopyTransform(*node->m_LocalTransform);
		//mesh->m_Transform.SetPosition(glm::vec3(0.f, 0.f, 15.0f));
		//mesh->m_Transform.SetScale(glm::vec3(1.0f));
		//for (auto& node : res2->m_NodePool)
		//{
		//	auto meshId = node->m_MeshID;
		//	//if (meshId >= 0)
		//	{
		//		auto mesh = lumenPT->m_Scene->AddMesh();
		//		mesh->SetMesh(res2->m_MeshPool[meshId]);
		//		//mesh->m_Transform.CopyTransform(*node->m_LocalTransform);
		//		mesh->m_Transform.SetPosition(glm::vec3(100.f, 500.f, 0.0f));
        //		mesh->m_Transform.SetScale(glm::vec3(3000.0f));
		//	}
		//}

		//lumenPT->m_Scene = lumenPT->CreateScene(scData);
		/*auto mesh = lumenPT->m_Scene->AddMesh();
		mesh->SetMesh(res->m_MeshPool[0]);*/

		//mesh->m_Transform.SetPosition(glm::vec3(0.f, 0.f, 15.0f));
		//mesh->m_Transform.SetScale(glm::vec3(1.0f));

		//auto mesh2 = lumenPT->m_Scene->AddMesh();
		//mesh2->SetMesh(res2->m_MeshPool[0]);
		//mesh2->m_Transform.SetPosition(glm::vec3(0.f, 30.f, 15.0f));
		//mesh2->m_Transform.SetScale(glm::vec3(1.0f));
		//
		
		//meshLight->m_Transform.SetPosition(glm::vec3(0.f, 0.f, 15.0f));
		//meshLight->m_Transform.SetScale(glm::vec3(1.0f));

		//TODO uncomment but also destroyu
		//std::string vndbFilePath = { p.string() };
		////vndbFilePath.append("/Sandbox/assets/volume/Sphere.vndb");
		//vndbFilePath.append("/Sandbox/assets/volume/bunny.vdb");
		//auto volumeRes = m_SceneManager->m_VolumeManager.LoadVDB(vndbFilePath);

		//auto volume = lumenPT->m_Scene->AddVolume();
		//volume->SetVolume(volumeRes->m_Volume);


		contextLayer->GetPipeline()->StartRendering();

	}

	~Sandbox()
	{
		
	}
};

Lumen::LumenApp* Lumen::CreateApplication()
{
	return new Sandbox();
}
