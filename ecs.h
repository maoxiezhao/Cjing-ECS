#pragma once

#include "common.h"
#include "ecs_util.h"

// TODO
// * support actions, implement an observer system
// * work pipeline
// * work with a jobSystem

namespace ECS
{
	class World;
	class EntityBuilder;

	using EntityID = U64;
	static const EntityID INVALID_ENTITY = 0;
	using EntityIDs = std::vector<EntityID>;
	using EntityType = std::vector<EntityID>;
	
	template<typename Value>
	using Hashmap = std::unordered_map<U64, Value>;

	////////////////////////////////////////////////////////////////////////////////
	//// Components
	////////////////////////////////////////////////////////////////////////////////

	static const EntityID HI_COMPONENT_ID = 256;

#define COMPONENT_INTERNAL(CLAZZ)                         \
	static inline ECS::EntityID componentID = UINT32_MAX;      \
public:                                                   \
	static ECS::EntityID GetComponentID() { return componentID; }                                

#define COMPONENT(CLAZZ)								  \
	COMPONENT_INTERNAL(CLAZZ)							  \
	CLAZZ() = default;                                    \
	CLAZZ(const CLAZZ &) = default;                       \
	friend class World;

	using CompXtorFunc = void(*)(World* world, EntityID* entities, size_t size, size_t count, void* ptr);
	using CompCopyFunc = void(*)(World* world, EntityID* srcEntities, EntityID* dstEntities, size_t size, size_t count, const void* srcPtr, void* dstPtr);
	using CompMoveFunc = void(*)(World* world, EntityID* srcEntities, EntityID* dstEntities, size_t size, size_t count, void* srcPtr, void* dstPtr);
	using CompCopyCtorFunc = void(*)(World* world, EntityID* srcEntities, EntityID* dstEntities, size_t size, size_t count, const void* srcPtr, void* dstPtr);
	using CompMoveCtorFunc = void(*)(World* world, EntityID* srcEntities, EntityID* dstEntities, size_t size, size_t count, void* srcPtr, void* dstPtr);

	namespace Reflect
	{
		struct ReflectInfo
		{
			CompXtorFunc ctor;
			CompXtorFunc dtor;
			CompCopyFunc copy;
			CompMoveFunc move;
			CompCopyCtorFunc copyCtor;
			CompMoveCtorFunc moveCtor;
		};

		template<typename T, typename std::enable_if_t<std::is_trivial<T>::value == false>* = nullptr>
		static void Register(World& world, EntityID compID);

		template<typename T, typename std::enable_if_t<std::is_trivial<T>::value == true>* = nullptr>
		static void Register(World& world, EntityID compID);
	}

	template<typename C>
	struct ComponentTypeRegister
	{
		static EntityID componentID;
		static EntityID ComponentID(World& world);
		static bool Registered();
	};
	template<typename C>
	EntityID ComponentTypeRegister<C>::componentID = INVALID_ENTITY;

	struct ComponentTypeInfo
	{
		Reflect::ReflectInfo reflectInfo;
		EntityID compID;
		size_t alignment;
		size_t size;
		bool isSet;
	};

    struct EntityCreateDesc 
	{
        EntityID entity = INVALID_ENTITY;
        const char* name = nullptr;
		bool useComponentID = false;	// For component id (0~256)
    };

	struct ComponentCreateDesc
	{
		EntityCreateDesc entity = {};
		size_t alignment = 0;
		size_t size = 0;
	};

	//////////////////////////////////////////////
	// BuildIn components
	struct InfoComponent
	{
		COMPONENT(InfoComponent)
		size_t size = 0;
		size_t algnment = 0;
	};

	class NameComponent
	{
	public:
		COMPONENT(NameComponent)
			const char* name = nullptr;
		U64 hash = 0;
	};

	////////////////////////////////////////////////////////////////////////////////
	//// World
	////////////////////////////////////////////////////////////////////////////////

	class World
	{
	public:
		World() = default;
		virtual ~World() {}

		World(const World& obj) = delete;
		void operator=(const World& obj) = delete;

		static std::unique_ptr<World> Create();

		virtual const EntityBuilder& CreateEntity(const char* name) = 0;
		virtual EntityID CreateEntityID(const char* name) = 0;
		virtual EntityID FindEntityIDByName(const char* name) = 0;
		virtual EntityID EntityIDAlive(EntityID id) = 0;
		virtual void DeleteEntity(EntityID id) = 0;
		virtual void SetEntityName(EntityID entity, const char* name) = 0;
		virtual void EnsureEntity(EntityID id) = 0;
		virtual void* GetComponent(EntityID entity, EntityID compID) = 0;

		template<typename C>
		C* GetComponent(EntityID entity)
		{
			return static_cast<C*>(GetComponent(entity, C::GetComponentID()));
		}

		template<typename C>
		void AddComponent(EntityID entity, const C& comp)
		{
			EntityID compID = ComponentTypeRegister<C>::ComponentID(*this);
			C* dstComp = static_cast<C*>(GetOrCreateComponent(entity, compID));
			*dstComp = comp;
		}

		template<typename C>
		EntityID RegisterComponent(const char* name = nullptr)
		{
			const char* n = name;
			if (n == nullptr)
				n = Util::Typename<C>();

			ComponentCreateDesc desc = {};
			desc.entity.entity = INVALID_ENTITY;
			desc.entity.name = n;
			desc.entity.useComponentID = true;
			desc.size = sizeof(C);
			desc.alignment = alignof(C);
			EntityID ret = InitNewComponent(desc);
			C::componentID = ret;
			return ret;
		}

		virtual bool HasComponentTypeAction(EntityID compID)const = 0;
		virtual ComponentTypeInfo* GetComponentTypInfo(EntityID compID) = 0;
		virtual const ComponentTypeInfo* GetComponentTypInfo(EntityID compID)const = 0;
		virtual void SetComponentAction(EntityID compID, const Reflect::ReflectInfo& info) = 0;
		virtual EntityID InitNewComponent(const ComponentCreateDesc& desc) = 0;
		virtual void* GetOrCreateComponent(EntityID entity, EntityID compID) = 0;
	};

	////////////////////////////////////////////////////////////////////////////////
	//// EntityBuilder
	////////////////////////////////////////////////////////////////////////////////

	class EntityBuilder
	{
	public:
		EntityBuilder(World* world_) : world(world_) {}
		EntityBuilder() = delete;

		template <class C>
		const EntityBuilder& with(const C& comp) const
		{
			world->AddComponent(entity, comp);
			return *this;
		}

		EntityID entity = INVALID_ENTITY;

	private:
		friend class World;
		World* world;
	};

	template<typename C>
	inline EntityID ComponentTypeRegister<C>::ComponentID(World& world)
	{
		if (!Registered())
		{
			// Register component
			componentID = world.RegisterComponent<C>();
			// Register reflect info
			Reflect::Register<C>(world, componentID);
		}
		return componentID;
	}

	template<typename C>
	bool ComponentTypeRegister<C>::Registered()
	{
		return componentID != INVALID_ENTITY;
	}

#include "ecs.inl"
}