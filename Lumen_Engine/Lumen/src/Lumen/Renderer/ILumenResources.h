#pragma once
#include <glm/fwd.hpp>
#include <memory>
#include <vector>

namespace Lumen
{
	class ILumenTexture
	{
	public:
		virtual ~ILumenTexture(){};
	};

    class ILumenMaterial
    {
    public:
        virtual void SetDiffuseColor(const glm::vec4& a_NewDiffuseColor) = 0;
        virtual void SetDiffuseTexture(std::shared_ptr<ILumenTexture> a_NewDiffuseTexture) = 0;

        virtual glm::vec4 GetDiffuseColor() const = 0;
        virtual ILumenTexture& GetDiffuseTexture() const = 0;
    };

    class ILumenPrimitive
    {
    public:
        virtual ~ILumenPrimitive() {};
        std::shared_ptr<ILumenMaterial> m_Material;
    };

    class ILumenMesh
    {
    public:
        ILumenMesh(std::vector<std::unique_ptr<ILumenPrimitive>>& a_Primitives)
            : m_Primitives(std::move(a_Primitives)) {};

        std::vector<std::unique_ptr<ILumenPrimitive>> m_Primitives;
    };

}