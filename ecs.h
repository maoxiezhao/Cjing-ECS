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
	template<typename... Comps>
	struct QueryBuilder;

	using EntityID = U64;
	using EntityIDs = Vector<EntityID>;
	using EntityType = Vector<EntityID>;
	using QueryID = U64;

	static const EntityID INVALID_ENTITY = 0;
	static const EntityType EMPTY_ENTITY_TYPE = EntityType();
	static const size_t MAX_QUERY_ITEM_COUNT = 16;
	extern const size_t ENTITY_PAIR_FLAG;

	#define ECS_ENTITY_HI(e) (static_cast<U32>((e) >> 32))
	#define ECS_ENTITY_LOW(e) (static_cast<U32>(e))
	#define ECS_ENTITY_COMBO(lo, hi) ((static_cast<U64>(hi) << 32) + static_cast<U32>(lo))
	#define ECS_MAKE_PAIR(re, obj) (ENTITY_PAIR_FLAG | ECS_ENTITY_COMBO(obj, re))

	////////////////////////////////////////////////////////////////////////////////
	//// Components
	////////////////////////////////////////////////////////////////////////////////

	using CompXtorFunc = void(*)(World* world, EntityID* entities, size_t size, size_t count, void* ptr);
	using CompCopyFunc = void(*)(World* world, EntityID* srcEntities, EntityID* dstEntities, size_t size, size_t count, const void* srcPtr, void* dstPtr);
	using CompMoveFunc = void(*)(World* world, EntityID* srcEntities, EntityID* dstEntities, size_t size, size_t count, void* srcPtr, void* dstPtr);
	using CompCopyCtorFunc = void(*)(World* world, EntityID* srcEntities, EntityID* dstEntities, size_t size, size_t count, const void* srcPtr, void* dstPtr);
	using CompMoveCtorFunc = void(*)(World* world, EntityID* srcEntities, EntityID* dstEntities, size_t size, size_t count, void* srcPtr, void* dstPtr);

	// Component reflect info
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

		template<typename T, typename enable_if_t<std::is_trivial<T>::value == false>* = nullptr>
		static void Register(World& world, EntityID compID);

		template<typename T, typename enable_if_t<std::is_trivial<T>::value == true>* = nullptr>
		static void Register(World& world, EntityID compID);
	}

	// Component id register
	namespace _
	{
		template<typename C>
		struct ComponentTypeRegister
		{
			static size_t size;
			static size_t alignment;
			static EntityID componentID;

			static EntityID ID(World& world);

		private:
			static EntityID RegisterComponent(World& world, size_t size, size_t alignment, const char* name = nullptr);
			static bool Registered(World& world);
		};
		template<typename C>
		size_t ComponentTypeRegister<C>::size = 0;
		template<typename C>
		size_t ComponentTypeRegister<C>::alignment = 0;
		template<typename C>
		EntityID ComponentTypeRegister<C>::componentID = INVALID_ENTITY;
	}

	template<typename C, typename U = int>
	struct ComponentType;

	template<typename C>
	struct ComponentType<C, enable_if_t<!Util::IsPair<C>::value, int>> : _::ComponentTypeRegister<C> {};

	template<typename C>
	struct ComponentType<C, enable_if_t<Util::IsPair<C>::value, int>>
	{
		static EntityID ID(World& world)
		{
			EntityID relation = _::ComponentTypeRegister<C::First>::ID(world);
			EntityID object = _::ComponentTypeRegister<C::Second>::ID(world);
			return ECS_MAKE_PAIR(relation, object);
		}
	};

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

	enum QueryItemFlag
	{
		QueryItemFlagParent = 1 << 0,
		QueryItemFlagCascade = 1 << 1
	};

	struct QueryItem
	{
		EntityID pred;
		EntityID obj;
		EntityID compID;
		U64 role;

		struct QueryItemSet
		{
			U32 flags;
			U64 relation;
		} set;
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
		bool cached = false; // TODO
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

		///////////////////////////////////////////////////////////////////////////
		// Entity
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
		virtual void ChildOf(EntityID entity, EntityID parent) = 0;
		virtual EntityID GetParent(EntityID entity) = 0;
		virtual EntityID GetRelationObject(EntityID entity, EntityID relation, U32 index = 0) = 0;

		///////////////////////////////////////////////////////////////////////////
		// Component
		template<typename C>
		bool HasComponent(EntityID entity)
		{
			EntityID compID = ComponentType<C>::ID(*this);
			return HasComponent(compID, compID);
		}

		template<typename C>
		C* GetComponent(EntityID entity)
		{
			return static_cast<C*>(GetComponent(entity, ComponentType<C>::ID(*this)));
		}

		template <typename R, typename O, typename P = Util::Pair<R, O>, typename C = typename Util::RealType<P>::type>
		C* GetComponent(EntityID entity)
		{
			EntityID compID = ComponentType<P>::ID(*this);
			return static_cast<C*>(GetComponent(entity, compID));
		}

		template<typename C>
		C* GetSingletonComponent()
		{
			EntityID compID = ComponentType<C>::ID(*this);
			return static_cast<C*>(GetOrCreateComponent(compID, compID));
		}

		template<typename C>
		void AddComponent(EntityID entity, const C& comp)
		{
			EntityID compID = ComponentType<C>::ID(*this);
			C* dstComp = static_cast<C*>(GetOrCreateComponent(entity, compID));
			*dstComp = comp;
		}

		template<typename T, typename C>
		void AddComponent(EntityID entity, const C& comp)
		{
			EntityID compID = ComponentType<T>::ID(*this);
			C* dstComp = static_cast<C*>(GetOrCreateComponent(entity, compID));
			*dstComp = comp;
		}

		template<typename C>
		void AddComponent(EntityID entity)
		{
			EntityID compID = ComponentType<C>::ID(*this);
			AddComponent(entity, compID);
		}

		template<typename C>
		void AddRelation(EntityID entity, EntityID relation)
		{
			EntityID compID = ComponentType<C>::ID(*this);
			AddRelation(entity, relation, compID);
		}

		template<typename C>
		void RemoveComponent(EntityID entity)
		{
			EntityID compID = ComponentType<C>::ID(*this);
			RemoveComponent(entity, compID);
		}

		virtual void* GetComponent(EntityID entity, EntityID compID) = 0;
		virtual bool HasComponent(EntityID entity, EntityID compID) = 0;
		virtual void AddRelation(EntityID entity, EntityID relation, EntityID compID) = 0;
		virtual bool HasComponentTypeInfo(EntityID compID)const = 0;
		virtual ComponentTypeInfo* GetComponentTypeInfo(EntityID compID) = 0;
		virtual const ComponentTypeInfo* GetComponentTypeInfo(EntityID compID)const = 0;
		virtual void SetComponentTypeInfo(EntityID compID, const Reflect::ReflectInfo& info) = 0;
		virtual EntityID InitNewComponent(const ComponentCreateDesc& desc) = 0;
		virtual void* GetOrCreateComponent(EntityID entity, EntityID compID) = 0;
		virtual void AddComponent(EntityID entity, EntityID compID) = 0;
		virtual void RemoveComponent(EntityID entity, EntityID compID) = 0;

		///////////////////////////////////////////////////////////////////////////
		// System
		template<typename... Args>
		SystemBuilder<Args...> CreateSystem();
		virtual EntityID InitNewSystem(const SystemCreateDesc& desc) = 0;
		virtual void RunSystem(EntityID entity) = 0;

		///////////////////////////////////////////////////////////////////////////
		// Query
		template<typename... Comps>
		QueryBuilder<Comps...> CreateQuery();
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

		template <typename R, typename O>
		const EntityBuilder& With() const
		{
			EntityID relation = ComponentType<R>::ID(*world);
			world->AddRelation<O>(entity, relation);
			return *this;
		}

		template <typename R, typename O>
		const EntityBuilder& With(const R& comp) const
		{
			using Pair = Util::Pair<R, O>;
			world->AddComponent<Pair>(entity, comp);
			return *this;
		}

		const EntityBuilder& ChildOf(EntityID parent)const
		{
			world->ChildOf(entity, parent);
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

	namespace _
	{
		template<typename C>
		inline EntityID ComponentTypeRegister<C>::ID(World& world)
		{
			if (!Registered(world))
			{
				size = sizeof(C);
				alignment = alignof(C);

				// In default case, the size of empty struct is 1
				if (std::is_empty_v<C>)
				{
					size = 0;
					alignment = 0;
				}

				// Register component
				componentID = RegisterComponent(world, size, alignment);
				// Register reflect info

				if (size > 0)
					Reflect::Register<C>(world, componentID);
			}
			return componentID;
		}

		template<typename C>
		inline EntityID ComponentTypeRegister<C>::RegisterComponent(World& world, size_t size, size_t alignment, const char* name)
		{
			const char* n = name;
			if (n == nullptr)
				n = Util::Typename<C>();

			ComponentCreateDesc desc = {};
			desc.entity.entity = INVALID_ENTITY;
			desc.entity.name = n;
			desc.entity.useComponentID = true;
			desc.size = size;
			desc.alignment = alignment;
			return world.InitNewComponent(desc);
		}

		template<typename C>
		bool ComponentTypeRegister<C>::Registered(World& world)
		{
			return componentID != INVALID_ENTITY && world.IsEntityAlive(componentID);
		}
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
		struct EachColumn<T, enable_if_t<!std::is_pointer_v<T>, int>> : EachColumnBase
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
			using Array = Array<void*, sizeof...(Comps)>;
			Array compsArray;

			void Populate(QueryIterator* iter)
			{
				return PopulateImpl(iter, 0, static_cast<decay_t<Comps>*>(nullptr)...);
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

			explicit EachInvoker(Func&& func_) noexcept : func(ECS_MOV(func_)) {}
			explicit EachInvoker(const Func& func_) noexcept : func(func_) {}

			// SystemCreateDesc.invoker => EachInvoker
			static void Run(QueryIterator* iter)
			{
				EachInvoker* invoker = static_cast<EachInvoker*>(iter->invoker);
				ECS_ASSERT(invoker != nullptr);
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

			template<typename... Args, enable_if_t<sizeof...(Comps) == sizeof...(Args), int> = 0>
			static void InvokeImpl(QueryIterator* iter, const Func& func, size_t index, CompArray&, Args... args)
			{
				for (I32 row = 0; row < iter->entityCount; row++)
				{
					EntityID entity = iter->entities[row];
					func(entity, (EachColumn<std::remove_reference_t<Comps>>(args, row).Get())...);
				}
			}

			template<typename... Args, enable_if_t<sizeof...(Comps) != sizeof...(Args), int> = 0>
			static void InvokeImpl(QueryIterator* iter, const Func& func, size_t index, CompArray& compArr, Args... args)
			{
				InvokeImpl(iter, func, index + 1, compArr, args..., compArr[index]);
			}
			
			Func func;
		};
	}

	////////////////////////////////////////////////////////////////////////////////
	//// Query
	////////////////////////////////////////////////////////////////////////////////

	template<typename... Comps>
	struct QueryIteratorBase
	{
		QueryIteratorBase() {};
		virtual ~QueryIteratorBase() {}

		virtual QueryIterator GetQueryIterator() = 0;
		virtual bool NextQueryIterator(QueryIterator& iter) = 0;

		template<typename Func>
		void ForEach(Func&& func)
		{
			this->Iterate<_::EachInvoker>(ECS_FWD(func));
		}

	protected:
		template<template<typename Func, typename ... Args> class Invoker, typename Func>
		void Iterate(Func&& func)
		{
			QueryIterator iter = this->GetQueryIterator();
			while (this->NextQueryIterator(iter))
				Invoker<decay_t<Func>, Comps...>(ECS_FWD(func)).Invoke(&iter);
		}
	};

	template<typename Base, typename... Comps>
	struct QueryBuilderBase
	{
	public:
		QueryBuilderBase(World* world_) :
			world(world_),
			compIDs({ (ComponentType<Comps>::ID(*world_))... })
		{
			for (int i = 0; i < compIDs.size(); i++)
			{
				QueryItem& item = queryDesc.items[i];
				item.pred = compIDs[i];
			}
		}

		Base& Cached()
		{
			queryDesc.cached = true;
			return *this;
		}

		Base& Item(I32 index)
		{
			currentItem = &queryDesc.items[index];
			return *this;
		}

		template<typename C>
		Base& Obj()
		{
			ECS_ASSERT(currentItem != nullptr);
			currentItem->obj = ComponentType<C>::ID(*world);
			return *this;
		}

		Base& Set(U32 flags)
		{
			ECS_ASSERT(currentItem != nullptr);
			currentItem->set.flags = flags;
			return *this;
		}

		operator Base& () {
			return *static_cast<Base*>(this);
		}

		Query<Comps...> Build()
		{
			return Query<Comps...>(world, queryDesc);
		}

	protected:
		World* world;
		Array<U64, sizeof...(Comps)> compIDs;
		QueryCreateDesc queryDesc = {};
		QueryItem* currentItem = nullptr;
	};

	template<typename... Comps>
	struct QueryBuilder : QueryBuilderBase<QueryBuilder<Comps...>, Comps...>
	{
		QueryBuilder(World* world_) : QueryBuilderBase<QueryBuilder<Comps...>, Comps...>(world_) {}
	};

	template<typename... Comps>
	class Query final : public QueryIteratorBase<Comps...>
	{
	public:
		Query(World* world_, const QueryCreateDesc& desc_) :
			world(world_), 
			queryDesc(desc_),
			queryID(0) 
		{
			queryID = world_->CreateQuery(desc_);
		}

		bool Valid()const 
		{
			return queryID > 0;
		}

		void Free()
		{
			if (queryID > 0)
			{
				this->world->DestroyQuery(queryID);
				queryID = 0;
			}
		}

		QueryIterator GetQueryIterator()override
		{
			return std::move(this->world->GetQueryIterator(queryID));
		}

		bool NextQueryIterator(QueryIterator& iter)override
		{
			return this->world->QueryIteratorNext(iter);
		}

	private:
		World* world;
		QueryCreateDesc queryDesc;
		QueryID queryID;
	};

	template<typename... Comps>
	inline QueryBuilder<Comps...> World::CreateQuery()
	{
		return QueryBuilder<Comps...>(this);
	}

	////////////////////////////////////////////////////////////////////////////////
	//// SystemBuilder
	////////////////////////////////////////////////////////////////////////////////

	template<typename... Comps>
	class SystemBuilder : public QueryBuilderBase<SystemBuilder<Comps...>, Comps...>
	{
	public:
		SystemBuilder(World* world_) : QueryBuilderBase<SystemBuilder<Comps...>, Comps...>(world_) {}
		
		template<typename Func>
		EntityID ForEach(Func&& func)
		{
			using Invoker = typename _::EachInvoker<decay_t<Func>, Comps...>;
			return Build<Invoker>(ECS_FWD(func));
		}

	private:
		template<typename Invoker, typename Func>
		EntityID Build(Func&& func)
		{
			Invoker* invoker = ECS_NEW_OBJECT<Invoker>(ECS_FWD(func));	
			sysDesc.action = Invoker::Run;
			sysDesc.invoker = invoker;
			sysDesc.invokerDeleter = reinterpret_cast<InvokerDeleter>(ECS_DELETE_OBJECT<Invoker>);
			sysDesc.query = this->queryDesc;
			sysDesc.query.cached = true;
			return this->world->InitNewSystem(sysDesc);
		}

	private:
		SystemCreateDesc sysDesc = {};
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
			ECS_ASSERT(0);
		}

		template<typename T, enable_if_t<std::is_trivially_constructible_v<T>, int> = 0>
		CompXtorFunc Ctor()
		{
			return nullptr;
		}

		template<typename T, enable_if_t<!std::is_default_constructible_v<T>, int> = 0>
		CompXtorFunc Ctor()
		{
			return IllegalCtor;
		}

		template<typename T, enable_if_t<std::is_default_constructible_v<T> &&
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

		template<typename T, enable_if_t<std::is_trivially_destructible_v<T>, int> = 0>
		CompXtorFunc Dtor()
		{
			return nullptr;
		}

		template<typename T, enable_if_t<!std::is_trivially_destructible_v<T>, int> = 0>
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
			ECS_ASSERT(0);
		}

		template<typename T, enable_if_t<std::is_trivially_copyable_v<T>, int> = 0>
		CompCopyFunc Copy()
		{
			return nullptr;
		}

		template<typename T, enable_if_t<!std::is_copy_assignable_v<T>, int> = 0>
		CompCopyFunc Copy()
		{
			return IllegalCopy;
		}

		template<typename T, enable_if_t<std::is_copy_assignable_v<T> &&
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

		template<typename T, enable_if_t<std::is_trivially_copy_constructible_v<T>, int> = 0>
		CompCopyCtorFunc CopyCtor()
		{
			return nullptr;
		}

		template<typename T, enable_if_t<!std::is_copy_constructible_v<T>, int> = 0>
		CompCopyCtorFunc CopyCtor()
		{
			return IllegalCopy;
		}

		template<typename T, enable_if_t<std::is_copy_constructible_v<T> &&
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
				dstArr[i] = ECS_MOV(srcArr[i]);
		}

		inline void IllegalMove(World* world, EntityID* srcEntities, EntityID* dstEntities, size_t size, size_t count, void* srcPtr, void* dstPtr)
		{
			ECS_ASSERT(0);
		}

		template<typename T, enable_if_t<std::is_trivially_move_assignable_v<T>, int> = 0>
		CompMoveFunc Move()
		{
			return nullptr;
		}

		template<typename T, enable_if_t<!std::is_move_assignable_v<T>, int> = 0>
		CompMoveFunc Move()
		{
			return IllegalMove;
		}

		template<typename T, enable_if_t<std::is_move_assignable_v<T> &&
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
				new (&dstArr[i]) T(ECS_MOV(srcArr[i]));
		}

		template<typename T, enable_if_t<std::is_trivially_move_constructible_v<T>, int> = 0>
		CompMoveCtorFunc MoveCtor()
		{
			return nullptr;
		}

		template<typename T, enable_if_t<!std::is_move_constructible_v<T>, int> = 0>
		CompMoveCtorFunc MoveCtor()
		{
			return IllegalMove;
		}

		template<typename T, enable_if_t<std::is_move_constructible_v<T> &&
			!std::is_trivially_move_constructible_v<T>, int> = 0>
			CompMoveCtorFunc MoveCtor()
		{
			return DefaultMoveCtor<T>;
		}

		template<typename T, typename enable_if_t<std::is_trivial<T>::value == false>*>
		void Register(World& world, EntityID compID)
		{
			if (!world.HasComponentTypeInfo(compID))
			{
				ReflectInfo info = {};
				info.ctor = Ctor<T>();
				info.dtor = Dtor<T>();
				info.copy = Copy<T>();
				info.move = Move<T>();
				info.copyCtor = CopyCtor<T>();
				info.moveCtor = MoveCtor<T>();
				world.SetComponentTypeInfo(compID, info);
			}
		}

		template<typename T, typename enable_if_t<std::is_trivial<T>::value == true>*>
		void Register(World& world, EntityID compID) {}
	}
}