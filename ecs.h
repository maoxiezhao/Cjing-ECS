#pragma once

#include "common.h"
#include "ecs_util.h"

namespace ECS
{
	class World;
	class EntityBuilder;

	template<typename... Args>
	class SystemBuilder;
	template<typename... Comps>
	class Query;

	using EntityID = U64;
	using EntityIDs = Vector<EntityID>;
	using EntityType = Vector<EntityID>;
	using QueryID = U64;

	static const EntityID INVALID_ENTITY = 0;
	static const EntityType EMPTY_ENTITY_TYPE = EntityType();
	static const size_t MAX_QUERY_ITEM_COUNT = 16;

	////////////////////////////////////////////////////////////////////////////////
	//// Components
	////////////////////////////////////////////////////////////////////////////////

#define COMPONENT_INTERNAL(CLAZZ)                         \
	static inline ECS::EntityID componentID = UINT32_MAX; \
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
		static bool Registered(World& world);
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

	struct QueryItem
	{
		EntityID compID;
	};

	struct QueryIteratorImpl;
	// TODO: remove impl
	struct QueryIterator
	{
	public:
		World* world = nullptr;
		QueryItem* items = nullptr;
		size_t itemCount = 0;
		void* invoker = nullptr;

		size_t entityCount = 0;
		EntityID* entities = nullptr;
		Vector<void*> compDatas;

	public:
		QueryIterator();
		~QueryIterator();
		QueryIterator(QueryIterator&& rhs)noexcept;
		void operator=(QueryIterator&& rhs)noexcept;

		QueryIterator(const QueryIterator& rhs) = delete;
		void operator=(const QueryIterator& rhs) = delete;

	private:
		friend class World;
		friend struct WorldImpl; // TODO
		QueryIteratorImpl* impl = nullptr;
	};

	struct QueryCreateDesc
	{
		QueryItem items[MAX_QUERY_ITEM_COUNT];
		bool cached = false; // is heavy
	};

	using InvokerDeleter = void(*)(void* ptr);
	using SystemAction = void(*)(QueryIterator* iter);

	struct SystemCreateDesc
	{
		EntityCreateDesc entity = {};
		QueryCreateDesc query = {};
		SystemAction action;
		void* invoker;
		InvokerDeleter invokerDeleter;
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

		static ECS_UNIQUE_PTR<World> Create();

		virtual const EntityBuilder& CreateEntity(const char* name) = 0;
		virtual const EntityBuilder& CreatePrefab(const char* name) = 0;
		virtual EntityID CreateEntityID(const char* name) = 0;
		virtual EntityID FindEntityIDByName(const char* name) = 0;
		virtual EntityID IsEntityAlive(EntityID entity)const = 0;
		virtual EntityType GetEntityType(EntityID entity)const = 0;
		virtual void DeleteEntity(EntityID entity) = 0;
		virtual void SetEntityName(EntityID entity, const char* name) = 0;
		virtual void EnsureEntity(EntityID entity) = 0;
		virtual void Instantiate(EntityID entity, EntityID prefab) = 0;

		virtual void* GetComponent(EntityID entity, EntityID compID) = 0;
		virtual bool HasComponent(EntityID entity, EntityID compID) = 0;

		template<typename C>
		bool HasComponent(EntityID entity)
		{
			EntityID compID = ComponentTypeRegister<C>::ComponentID(*this);
			return HasComponent(compID, compID);
		}

		template<typename C>
		C* GetComponent(EntityID entity)
		{
			return static_cast<C*>(GetComponent(entity, C::GetComponentID()));
		}

		template<typename C>
		C* GetSingletonComponent()
		{
			EntityID compID = ComponentTypeRegister<C>::ComponentID(*this);
			return static_cast<C*>(GetOrCreateComponent(compID, compID));
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

		template<typename C>
		void AddRelation(EntityID entity, EntityID relation)
		{
			EntityID compID = ComponentTypeRegister<C>::ComponentID(*this);
			AddRelation(entity, relation, compID);
		}

		virtual void AddRelation(EntityID entity, EntityID relation, EntityID compID) = 0;
		virtual bool HasComponentTypeAction(EntityID compID)const = 0;
		virtual ComponentTypeInfo* GetComponentTypInfo(EntityID compID) = 0;
		virtual const ComponentTypeInfo* GetComponentTypInfo(EntityID compID)const = 0;
		virtual void SetComponentAction(EntityID compID, const Reflect::ReflectInfo& info) = 0;
		virtual EntityID InitNewComponent(const ComponentCreateDesc& desc) = 0;
		virtual void* GetOrCreateComponent(EntityID entity, EntityID compID) = 0;
		virtual void AddComponent(EntityID entity, EntityID compID) = 0;

		template<typename... Args>
		SystemBuilder<Args...> CreateSystem();
		virtual EntityID InitNewSystem(const SystemCreateDesc& desc) = 0;
		virtual void RunSystem(EntityID entity) = 0;

		template<typename... Comps>
		Query<Comps...> CreateQuery();
		virtual QueryID CreateQuery(const QueryCreateDesc& desc) = 0;
		virtual void DestroyQuery(QueryID queryID) = 0;
		virtual QueryIterator GetQueryIterator(QueryID queryID) = 0;
		virtual bool QueryIteratorNext(QueryIterator& iter) = 0;
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
		const EntityBuilder& With(const C& comp) const
		{
			world->AddComponent(entity, comp);
			return *this;
		}

		template <class C>
		const EntityBuilder& With() const
		{
			world->AddComponent<C>(entity);
			return *this;
		}

		const EntityBuilder& Instantiate(EntityID prefabID) const
		{
			world->Instantiate(entity, prefabID);
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
		if (!Registered(world))
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
	bool ComponentTypeRegister<C>::Registered(World& world)
	{
		return componentID != INVALID_ENTITY && world.IsEntityAlive(componentID);
	}

	////////////////////////////////////////////////////////////////////////////////
	//// Invoker
	////////////////////////////////////////////////////////////////////////////////

	namespace _
	{
		template<typename T, typename = int>
		struct EachColumn {};

		struct EachColumnBase
		{
			EachColumnBase(void* ptr_, I32 row_) : ptr(ptr_), row(row_) {}

		protected:
			void* ptr;
			I32 row;
		};

		template<typename T>
		struct EachColumn<T, std::enable_if_t<!std::is_pointer_v<T>, int>> : EachColumnBase
		{
			EachColumn(void* ptr, I32 row) : EachColumnBase(ptr, row) {};

			T& Get()
			{
				return *(static_cast<T*>(ptr) + row);
			}
		};

		template<typename... Comps>
		struct CompTuple
		{
			using Array = std::array<void*, sizeof...(Comps)>;
			Array compsArray;

			void Populate(QueryIterator* iter)
			{
				return PopulateImpl(iter, 0, static_cast<std::decay_t<Comps>*>(nullptr)...);
			}

		private:
			void PopulateImpl(QueryIterator* iter, size_t) { return; }

			template<typename CompPtr, typename... Comps>
			void PopulateImpl(QueryIterator* iter, size_t index, CompPtr, Comps... comps)
			{
				compsArray[index] = static_cast<CompPtr>(iter->compDatas[index]);
				return PopulateImpl(iter, index + 1, comps...);
			}
		};

		template<typename Func, typename... Comps>
		struct EachInvoker
		{
		public:
			using CompArray = typename CompTuple<Comps ...>::Array;

			explicit EachInvoker(Func&& func_) noexcept :
				func(std::move(func_)) {}

			explicit EachInvoker(const Func& func_) noexcept :
				func(func_) {}

			// SystemCreateDesc.invoker => EachInvoker
			static void Run(QueryIterator* iter)
			{
				EachInvoker* invoker = static_cast<EachInvoker*>(iter->invoker);
				assert(invoker != nullptr);
				invoker->Invoke(iter);
			}

			// Get entity and components for func
			void Invoke(QueryIterator* iter)
			{
				CompTuple<Comps...> compTuple;
				compTuple.Populate(iter);
				InvokeImpl(iter, func, 0, compTuple.compsArray);
			}

		private:

			template<typename... Args, std::enable_if_t<sizeof...(Comps) == sizeof...(Args), int> = 0>
			static void InvokeImpl(QueryIterator* iter, const Func& func, size_t index, CompArray&, Args... comps)
			{
				for (I32 row = 0; row < iter->entityCount; row++)
				{
					EntityID entity = iter->entities[row];
					func(entity, (EachColumn<std::remove_reference_t<Comps>>(comps, row).Get())...);
				}
			}

			template<typename... Args, std::enable_if_t<sizeof...(Comps) != sizeof...(Args), int> = 0>
			static void InvokeImpl(QueryIterator* iter, const Func& func, size_t index, CompArray& compArr, Args... comps)
			{
				InvokeImpl(iter, func, index + 1, compArr, comps..., compArr[index]);
			}
			
			Func func;
		};
	}

	////////////////////////////////////////////////////////////////////////////////
	//// Query
	////////////////////////////////////////////////////////////////////////////////
	template<typename... Comps>
	class Query
	{
	public:
		Query(World* world_) :
			world(world_),
			compIDs({ (ComponentTypeRegister<Comps>::ComponentID(*world_))... })
		{
			for (int i = 0; i < compIDs.size(); i++)
			{
				QueryItem& item = desc.items[i];
				item.compID = compIDs[i];
			}
		}
		Query() = delete;

		Query& Build()
		{
			if (queryID > 0)
				Free();

			queryID = world->CreateQuery(desc);
			return *this;
		}

		bool Valid()const 
		{
			return queryID > 0;
		}

		void Free()
		{
			if (queryID > 0)
			{
				world->DestroyQuery(queryID);
				queryID = 0;
			}
		}

		Query& Cached()
		{
			desc.cached = true;
			return *this;
		}

		template<typename Func>
		void ForEach(Func&& func)
		{
			using Invoker = typename _::EachInvoker<typename std::decay_t<Func>, Comps...>;
			QueryIterator iter = world->GetQueryIterator(queryID);
			while (world->QueryIteratorNext(iter))
				Invoker(ECS_FWD(func)).Invoke(&iter);
		}

	private:
		World* world;
		std::array<U64, sizeof...(Comps)> compIDs;
		QueryID queryID = 0;
		QueryCreateDesc desc = {};
	};

	template<typename... Comps>
	inline Query<Comps...> World::CreateQuery()
	{
		return Query<Comps...>(this);
	}

	////////////////////////////////////////////////////////////////////////////////
	//// SystemBuilder
	////////////////////////////////////////////////////////////////////////////////

	template<typename... Comps>
	class SystemBuilder
	{
	public:
		SystemBuilder(World* world_) : 
			world(world_),
			compIDs({ (ComponentTypeRegister<Comps>::ComponentID(*world_))... })
		{
			auto& queryDesc = desc.query;
			for (int i = 0; i < compIDs.size(); i++)
			{
				QueryItem& item = queryDesc.items[i];
				item.compID = compIDs[i];
			}
		}
		SystemBuilder() = delete;

		template<typename Func>
		EntityID ForEach(Func&& func)
		{
			using Invoker = typename _::EachInvoker<typename std::decay_t<Func>, Comps...>;
			return Build<Invoker>(ECS_FWD(func));
		}

	private:
		template<typename Invoker, typename Func>
		EntityID Build(Func&& func)
		{
			Invoker* invoker = ECS_NEW_OBJECT<Invoker>(ECS_FWD(func));
			desc.action = Invoker::Run;
			desc.invoker = invoker;
			desc.invokerDeleter = reinterpret_cast<InvokerDeleter>(ECS_DELETE_OBJECT<Invoker>);
			desc.query.cached = true;
			return world->InitNewSystem(desc);
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


	////////////////////////////////////////////////////////////////////////////////
	//// Reflection
	////////////////////////////////////////////////////////////////////////////////

	namespace Reflect
	{
		/// ////////////////////////////////////////////////////////////////////
		/// Constructor
		////////////////////////////////////////////////////////////////////////
		template <typename T>
		void DefaultCtor(World* world, EntityID* entities, size_t size, size_t count, void* ptr)
		{
			T* objArr = static_cast<T*>(ptr);
			for (size_t i = 0; i < count; i++)
				new (&objArr[i]) T();
		}

		inline void IllegalCtor(World* world, EntityID* entities, size_t size, size_t count, void* ptr)
		{
			assert(0);
		}

		template<typename T, std::enable_if_t<std::is_trivially_constructible_v<T>, int> = 0>
		CompXtorFunc Ctor()
		{
			return nullptr;
		}

		template<typename T, std::enable_if_t<!std::is_default_constructible_v<T>, int> = 0>
		CompXtorFunc Ctor()
		{
			return IllegalCtor;
		}

		template<typename T, std::enable_if_t<std::is_default_constructible_v<T> &&
			!std::is_trivially_constructible_v<T>, int> = 0>
			CompXtorFunc Ctor()
		{
			return DefaultCtor<T>;
		}

		/// ////////////////////////////////////////////////////////////////////
		/// Destructor
		////////////////////////////////////////////////////////////////////////
		template <typename T>
		void DefaultDtor(World* world, EntityID* entities, size_t size, size_t count, void* ptr)
		{
			T* objArr = static_cast<T*>(ptr);
			for (size_t i = 0; i < count; i++)
				objArr[i].~T();
		}

		template<typename T, std::enable_if_t<std::is_trivially_destructible_v<T>, int> = 0>
		CompXtorFunc Dtor()
		{
			return nullptr;
		}

		template<typename T, std::enable_if_t<!std::is_trivially_destructible_v<T>, int> = 0>
		CompXtorFunc Dtor()
		{
			return DefaultDtor<T>;
		}

		/// ////////////////////////////////////////////////////////////////////
		/// Copy
		////////////////////////////////////////////////////////////////////////
		template <typename T>
		void DefaultCopy(World* world, EntityID* srcEntities, EntityID* dstEntities, size_t size, size_t count, const void* srcPtr, void* dstPtr)
		{
			const T* srcArr = static_cast<const T*>(srcPtr);
			T* dstArr = static_cast<T*>(dstPtr);
			for (size_t i = 0; i < count; i++)
				dstArr[i] = srcArr[i];
		}

		inline void IllegalCopy(World* world, EntityID* srcEntities, EntityID* dstEntities, size_t size, size_t count, const void* srcPtr, void* dstPtr)
		{
			assert(0);
		}

		template<typename T, std::enable_if_t<std::is_trivially_copyable_v<T>, int> = 0>
		CompCopyFunc Copy()
		{
			return nullptr;
		}

		template<typename T, std::enable_if_t<!std::is_copy_assignable_v<T>, int> = 0>
		CompCopyFunc Copy()
		{
			return IllegalCopy;
		}

		template<typename T, std::enable_if_t<std::is_copy_assignable_v<T> &&
			!std::is_trivially_copyable_v<T>, int> = 0>
			CompCopyFunc Copy()
		{
			return DefaultCopy<T>;
		}

		/// ////////////////////////////////////////////////////////////////////
		/// Copy ctor
		////////////////////////////////////////////////////////////////////////
		template <typename T>
		void DefaultCopyCtor(World* world, EntityID* srcEntities, EntityID* dstEntities, size_t size, size_t count, const void* srcPtr, void* dstPtr)
		{
			const T* srcArr = static_cast<const T*>(srcPtr);
			T* dstArr = static_cast<T*>(dstPtr);
			for (size_t i = 0; i < count; i++)
				new (&dstArr[i]) T(srcArr[i]);
		}

		template<typename T, std::enable_if_t<std::is_trivially_copy_constructible_v<T>, int> = 0>
		CompCopyCtorFunc CopyCtor()
		{
			return nullptr;
		}

		template<typename T, std::enable_if_t<!std::is_copy_constructible_v<T>, int> = 0>
		CompCopyCtorFunc CopyCtor()
		{
			return IllegalCopy;
		}

		template<typename T, std::enable_if_t<std::is_copy_constructible_v<T> &&
			!std::is_trivially_copy_constructible_v<T>, int> = 0>
			CompCopyCtorFunc CopyCtor()
		{
			return DefaultCopyCtor<T>;
		}

		/// ////////////////////////////////////////////////////////////////////
		/// Move
		////////////////////////////////////////////////////////////////////////
		template <typename T>
		void DefaultMove(World* world, EntityID* srcEntities, EntityID* dstEntities, size_t size, size_t count, void* srcPtr, void* dstPtr)
		{
			T* srcArr = static_cast<T*>(srcPtr);
			T* dstArr = static_cast<T*>(dstPtr);
			for (size_t i = 0; i < count; i++)
				dstArr[i] = std::move(srcArr[i]);
		}

		inline void IllegalMove(World* world, EntityID* srcEntities, EntityID* dstEntities, size_t size, size_t count, void* srcPtr, void* dstPtr)
		{
			assert(0);
		}

		template<typename T, std::enable_if_t<std::is_trivially_move_assignable_v<T>, int> = 0>
		CompMoveFunc Move()
		{
			return nullptr;
		}

		template<typename T, std::enable_if_t<!std::is_move_assignable_v<T>, int> = 0>
		CompMoveFunc Move()
		{
			return IllegalMove;
		}

		template<typename T, std::enable_if_t<std::is_move_assignable_v<T> &&
			!std::is_trivially_move_assignable_v<T>, int> = 0>
			CompMoveFunc Move()
		{
			return DefaultMove<T>;
		}

		/// ////////////////////////////////////////////////////////////////////
		/// Move ctor
		////////////////////////////////////////////////////////////////////////
		template <typename T>
		void DefaultMoveCtor(World* world, EntityID* srcEntities, EntityID* dstEntities, size_t size, size_t count, void* srcPtr, void* dstPtr)
		{
			T* srcArr = static_cast<T*>(srcPtr);
			T* dstArr = static_cast<T*>(dstPtr);
			for (size_t i = 0; i < count; i++)
				new (&dstArr[i]) T(std::move(srcArr[i]));
		}

		template<typename T, std::enable_if_t<std::is_trivially_move_constructible_v<T>, int> = 0>
		CompMoveCtorFunc MoveCtor()
		{
			return nullptr;
		}

		template<typename T, std::enable_if_t<!std::is_move_constructible_v<T>, int> = 0>
		CompMoveCtorFunc MoveCtor()
		{
			return IllegalMove;
		}

		template<typename T, std::enable_if_t<std::is_move_constructible_v<T> &&
			!std::is_trivially_move_constructible_v<T>, int> = 0>
			CompMoveCtorFunc MoveCtor()
		{
			return DefaultMoveCtor<T>;
		}

		template<typename T, typename std::enable_if_t<std::is_trivial<T>::value == false>*>
		void Register(World& world, EntityID compID)
		{
			if (!world.HasComponentTypeAction(compID))
			{
				ReflectInfo info = {};
				info.ctor = Ctor<T>();
				info.dtor = Dtor<T>();
				info.copy = Copy<T>();
				info.move = Move<T>();
				info.copyCtor = CopyCtor<T>();
				info.moveCtor = MoveCtor<T>();
				world.SetComponentAction(compID, info);
			}
		}

		template<typename T, typename std::enable_if_t<std::is_trivial<T>::value == true>*>
		void Register(World& world, EntityID compID) {}
	}
}