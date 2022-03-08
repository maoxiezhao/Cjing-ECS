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

	template<typename... Args>
	class SystemBuilder;

	using EntityID = U64;
	using EntityIDs = std::vector<EntityID>;
	using EntityType = std::vector<EntityID>;
	
	static const EntityID INVALID_ENTITY = 0;
	static const EntityType EMPTY_ENTITY_TYPE = EntityType();

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
		static EntityID RegisterComponent(World& world, const char* name = nullptr);
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

	typedef void (*InvokerDeleter)(void* ptr);

	struct SystemCreateDesc
	{
		EntityCreateDesc entity = {};
		void* invoker;
		InvokerDeleter invokerDeleter;
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
		virtual EntityID IsEntityAlive(EntityID entity)const = 0;
		virtual EntityType GetEntityType(EntityID entity)const = 0;
		virtual void DeleteEntity(EntityID entity) = 0;
		virtual void SetEntityName(EntityID entity, const char* name) = 0;
		virtual void EnsureEntity(EntityID entity) = 0;

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
		void AddComponent(EntityID entity)
		{
			EntityID compID = ComponentTypeRegister<C>::ComponentID(*this);
			AddComponent(entity, compID);
		}

		template<typename C>
		void RegisterComponent()
		{
			ComponentTypeRegister<C>::ComponentID(*this);
		}

		virtual bool HasComponentTypeAction(EntityID compID)const = 0;
		virtual ComponentTypeInfo* GetComponentTypInfo(EntityID compID) = 0;
		virtual const ComponentTypeInfo* GetComponentTypInfo(EntityID compID)const = 0;
		virtual void SetComponentAction(EntityID compID, const Reflect::ReflectInfo& info) = 0;
		virtual EntityID InitNewComponent(const ComponentCreateDesc& desc) = 0;
		virtual void* GetOrCreateComponent(EntityID entity, EntityID compID) = 0;
		virtual void AddComponent(EntityID entity, EntityID compID) = 0;

		template<typename... Args>
		SystemBuilder<Args...> CreateSystem();

		virtual EntityID InitSystem(const SystemCreateDesc& info) = 0;
		virtual void RunSystem(EntityID entity) = 0;
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

		template <class C>
		const EntityBuilder& with() const
		{
			world->AddComponent<C>(entity);
			return *this;
		}

		EntityID entity = INVALID_ENTITY;

	private:
		friend class World;
		World* world;
	};

	////////////////////////////////////////////////////////////////////////////////
	//// ComponentTypeRegister
	////////////////////////////////////////////////////////////////////////////////

	template<typename C>
	inline EntityID ComponentTypeRegister<C>::ComponentID(World& world)
	{
		if (!Registered())
		{
			// Register component
			componentID = RegisterComponent(world);
			// Register reflect info
			Reflect::Register<C>(world, componentID);
		}
		return componentID;
	}

	template<typename C>
	inline EntityID ComponentTypeRegister<C>::RegisterComponent(World& world, const char* name)
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
		EntityID ret = world.InitNewComponent(desc);
		C::componentID = ret;
		return ret;
	}

	template<typename C>
	bool ComponentTypeRegister<C>::Registered()
	{
		return componentID != INVALID_ENTITY;
	}

	////////////////////////////////////////////////////////////////////////////////
	//// SystemBuilder
	////////////////////////////////////////////////////////////////////////////////

	namespace _
	{
		template<typename Func, typename... Comps>
		struct EachInvoker
		{
		public:
			explicit EachInvoker(Func&& func_) noexcept :
				func(std::move(func_)) {}

			explicit EachInvoker(const Func& func_) noexcept :
				func(func_) {}

		private:
			Func func;
		};
	}

	template<typename... Comps>
	class SystemBuilder
	{
	public:
		SystemBuilder(World* world_) : 
			world(world_),
			compIDs({ (ComponentTypeRegister<Comps>::ComponentID(*world_))... })
		{
		}
		SystemBuilder() = delete;

		template<typename Func>
		EntityID ForEach(Func&& func)
		{
			using Invoker = typename _::EachInvoker<typename std::decay_t<Func>, Comps...>;
			return Build<Invoker>(std::forward<Func>(func));
		}

	private:
		template<typename Invoker, typename Func>
		EntityID Build(Func&& func)
		{
			Invoker* invoker = Util::NewObject<Invoker>(std::forward<Func>(func));
			desc.invoker = invoker;
			desc.invokerDeleter = reinterpret_cast<InvokerDeleter>(Util::DeleteObject<Invoker>);
			return world->InitNewComponent(desc);
		}

	private:
		World* world;
		SystemCreateDesc desc = {};
		std::array<U64, sizeof...(Comps)> compIDs;
	};

	template<typename... Args>
	inline SystemBuilder<Args...> World::CreateSystem()
	{
		return SystemBuilder<Args...>(this);
	}

#include "ecs.inl"
}