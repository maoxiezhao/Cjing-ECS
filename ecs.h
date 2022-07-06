#pragma once

#include "common.h"
#include "ecs_def.h"
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

	struct ComponentTypeHooks
	{
		CompXtorFunc ctor;
		CompXtorFunc dtor;
		CompCopyFunc copy;
		CompMoveFunc move;
		CompCopyCtorFunc copyCtor;
		CompMoveCtorFunc moveCtor;

		IterCallbackAction onAdd;
		IterCallbackAction onRemove;
		IterCallbackAction onSet;

		void* invoker = nullptr;
		InvokerDeleter invokerDeleter = nullptr;
	};

	struct ComponentTypeInfo
	{
		ComponentTypeHooks hooks;
		EntityID compID;
		size_t alignment;
		size_t size;
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
		virtual bool EntityExists(EntityID entity)const = 0;
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

		template <typename Func>
		void EachChildren(EntityID entity, Func&& func);

		template<typename T, typename Func>
		void SetComponenetOnAdded(Func&& func);
		template<typename T, typename Func>
		void SetComponenetOnRemoved(Func&& func);

		virtual void* GetComponent(EntityID entity, EntityID compID) = 0;
		virtual bool HasComponent(EntityID entity, EntityID compID) = 0;
		virtual void AddRelation(EntityID entity, EntityID relation, EntityID compID) = 0;
		virtual bool HasComponentTypeInfo(EntityID compID)const = 0;
		virtual ComponentTypeInfo* GetComponentTypeInfo(EntityID compID) = 0;
		virtual const ComponentTypeInfo* GetComponentTypeInfo(EntityID compID)const = 0;
		virtual const ComponentTypeHooks* GetComponentTypeHooks(EntityID compID)const = 0;
		virtual void SetComponentTypeInfo(EntityID compID, const ComponentTypeHooks& info) = 0;
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
		// Filter
		virtual bool InitFilter(const FilterCreateDesc& desc, Filter& outFilter)const = 0;
		virtual Iterator GetFilterIterator(Filter& filter) = 0;
		virtual bool FilterIteratorNext(Iterator* it)const = 0;

		///////////////////////////////////////////////////////////////////////////
		// Query
		template<typename... Comps>
		QueryBuilder<Comps...> CreateQuery();
		virtual QueryImpl* CreateQuery(const QueryCreateDesc& desc) = 0;
		virtual void DestroyQuery(QueryImpl* query) = 0;
		virtual Iterator GetQueryIterator(QueryImpl* query) = 0;
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
			return componentID != INVALID_ENTITY && world.EntityExists(componentID);
		}

		struct IndexIterator
		{
			explicit IndexIterator(size_t i) : index(i) {}

			bool operator!=(IndexIterator const& other) const
			{
				return index != other.index;
			}

			size_t const& operator*() const
			{
				return index;
			}

			IndexIterator& operator++()
			{
				++index;
				return *this;
			}

		private:
			size_t index;
		};
	}

	struct EntityIterator
	{
		using Iterator = _::IndexIterator;

		EntityIterator(EntityID* entities_, size_t count_) :
			entities(entities_),
			count(count_)
		{}

		bool Empty()const
		{
			return count <= 0 || entities == nullptr;
		}

		size_t Count()const
		{
			return count;
		}

		EntityID At(size_t index)
		{
			ECS_ASSERT(index >= 0 || index < count);
			return entities[index];
		}

		Iterator begin()
		{
			return Iterator(0);
		}

		Iterator end()
		{
			return Iterator(count);
		}

	private:
		EntityID* entities = nullptr;
		size_t count = 0;
	};

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

		template<typename T, typename = int>
		struct IterColumn {};

		struct IterColumnBase
		{
			IterColumnBase(void* ptr_) : ptr(ptr_) {}

		protected:
			void* ptr;
		};

		template<typename T>
		struct IterColumn<T, enable_if_t<!std::is_pointer_v<T>, int>> : IterColumnBase
		{
			IterColumn(void* ptr) : IterColumnBase(ptr) {};

			T* Get()
			{
				return (static_cast<T*>(ptr));
			}
		};

		template<typename... Comps>
		struct CompTuple
		{
			using Array = Array<void*, sizeof...(Comps)>;
			Array ptrs;

			void Populate(Iterator* iter)
			{
				return PopulateImpl(iter, 0, static_cast<decay_t<Comps>*>(nullptr)...);
			}

		private:
			void PopulateImpl(Iterator* iter, size_t) { return; }

			template<typename CompPtr, typename... Comps>
			void PopulateImpl(Iterator* iter, size_t index, CompPtr, Comps... comps)
			{
				ptrs[index] = static_cast<CompPtr>(iter->ptrs[index]);
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
			static void Run(Iterator* iter)
			{
				EachInvoker* invoker = static_cast<EachInvoker*>(iter->invoker);
				ECS_ASSERT(invoker != nullptr);
				invoker->Invoke(iter);
			}

			// Get entity and components for func
			void Invoke(Iterator* iter)
			{
				CompTuple<Comps...> compTuple;
				compTuple.Populate(iter);
				InvokeImpl(iter, func, 0, compTuple.ptrs);
			}

		private:

			template<typename... Args, enable_if_t<sizeof...(Comps) == sizeof...(Args), int> = 0>
			static void InvokeImpl(Iterator* iter, const Func& func, size_t index, CompArray&, Args... args)
			{
				for (I32 row = 0; row < iter->count; row++)
				{
					EntityID entity = iter->entities[row];
					func(entity, (EachColumn<std::remove_reference_t<Comps>>(args, row).Get())...);
				}
			}

			template<typename... Args, enable_if_t<sizeof...(Comps) != sizeof...(Args), int> = 0>
			static void InvokeImpl(Iterator* iter, const Func& func, size_t index, CompArray& compArr, Args... args)
			{
				InvokeImpl(iter, func, index + 1, compArr, args..., compArr[index]);
			}
			
			Func func;
		};


		template<typename Func, typename... Comps>
		struct IterInvoker
		{
		public:
			using CompArray = typename CompTuple<Comps ...>::Array;

			explicit IterInvoker(Func&& func_) noexcept : func(ECS_MOV(func_)) {}
			explicit IterInvoker(const Func& func_) noexcept : func(func_) {}

			// SystemCreateDesc.invoker => IterInvoker
			static void Run(Iterator* iter)
			{
				IterInvoker* invoker = static_cast<IterInvoker*>(iter->invoker);
				ECS_ASSERT(invoker != nullptr);
				invoker->Invoke(iter);
			}

			// Get entity and components for func
			void Invoke(Iterator* iter)
			{
				CompTuple<Comps...> compTuple;
				compTuple.Populate(iter);
				InvokeImpl(iter, func, 0, compTuple.ptrs);
			}

		private:

			template<typename... Args, enable_if_t<sizeof...(Comps) == sizeof...(Args), int> = 0>
			static void InvokeImpl(Iterator* iter, const Func& func, size_t index, CompArray&, Args... args)
			{
				EntityIterator entityIter(iter->entities, iter->count);
				func(entityIter, (IterColumn<std::remove_reference_t<Comps>>(args).Get())...);
			}

			template<typename... Args, enable_if_t<sizeof...(Comps) != sizeof...(Args), int> = 0>
			static void InvokeImpl(Iterator* iter, const Func& func, size_t index, CompArray& compArr, Args... args)
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

		virtual Iterator GetQueryIterator() = 0;
		virtual bool NextQueryIterator(Iterator& iter) = 0;

		template<typename Func>
		void ForEach(Func&& func)
		{
			this->Iterate<_::EachInvoker>(ECS_FWD(func));
		}

		template<typename Func>
		void Iter(Func&& func)
		{
			this->Iterate<_::IterInvoker>(ECS_FWD(func));
		}

	protected:
		template<template<typename Func, typename ... Args> class Invoker, typename Func>
		void Iterate(Func&& func)
		{
			Iterator iter = this->GetQueryIterator();
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
			ids({ (ComponentType<Comps>::ID(*world_))... })
		{
			for (int i = 0; i < ids.size(); i++)
			{
				Term& term = queryDesc.filter.terms[i];
				term.pred = ids[i];
			}
		}

		Base& TermIndex(I32 index)
		{
			currentTerm = &queryDesc.filter.terms[index];
			return *this;
		}

		template<typename C>
		Base& Obj()
		{
			ECS_ASSERT(currentTerm != nullptr);
			currentTerm->obj = ComponentType<C>::ID(*world);
			return *this;
		}

		Base& Set(U32 flags)
		{
			ECS_ASSERT(currentTerm != nullptr);
			currentTerm->set.flags = flags;
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
		Array<U64, sizeof...(Comps)> ids;
		QueryCreateDesc queryDesc = {};
		Term* currentTerm = nullptr;
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
		Query() :
			world(nullptr),
			impl(nullptr)
		{}

		Query(World* world_, const QueryCreateDesc& desc_) :
			world(world_), 
			queryDesc(desc_),
			impl(nullptr)
		{
			impl = world_->CreateQuery(desc_);
		}

		bool Valid()const 
		{
			return impl != nullptr;
		}

		void Free()
		{
			if (impl != nullptr)
			{
				this->world->DestroyQuery(impl);
				impl = nullptr;
			}
		}

		Iterator GetQueryIterator()override
		{
			return this->world->GetQueryIterator(impl);
		}

		bool NextQueryIterator(Iterator& iter)override
		{
			return QueryNextInstanced(&iter);
		}

	private:
		World* world;
		QueryCreateDesc queryDesc;
		QueryImpl* impl;
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

		template<typename Func>
		EntityID Iter(Func&& func)
		{
			using Invoker = typename _::IterInvoker<decay_t<Func>, Comps...>;
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
			return this->world->InitNewSystem(sysDesc);
		}

	private:
		SystemCreateDesc sysDesc = {};
	};

	template<typename Func>
	inline void World::EachChildren(EntityID entity, Func&& func)
	{
		Filter filter;
		FilterCreateDesc desc = {};
		desc.terms[0].compID = ECS_MAKE_PAIR(EcsRelationChildOf, entity);
		InitFilter(desc, filter);

		Iterator it = GetFilterIterator(filter);
		while (FilterIteratorNext(&it)) {
			_::EachInvoker<Func>(ECS_MOV(func)).Invoke(&it);
		}
	}

	template<typename T, typename Func>
	inline void World::SetComponenetOnAdded(Func&& func)
	{
		EntityID compID = ComponentType<T>::ID(*this);
		auto h = GetComponentTypeHooks(compID);
		ComponentTypeHooks hooks = h ? *h : ComponentTypeHooks();
		ECS_ASSERT(hooks.onAdd == nullptr);

		using Invoker = typename _::EachInvoker<decay_t<Func>, T>;
		hooks.onAdd = Invoker::Run;
		hooks.invoker = ECS_NEW_OBJECT<Invoker>(ECS_FWD(func));
		hooks.invokerDeleter = reinterpret_cast<InvokerDeleter>(ECS_DELETE_OBJECT<Invoker>);
		SetComponentTypeInfo(compID, hooks);
	}

	template<typename T, typename Func>
	inline void World::SetComponenetOnRemoved(Func&& func)
	{
		EntityID compID = ComponentType<T>::ID(*this);
		auto h = GetComponentTypeHooks(compID);
		ComponentTypeHooks hooks = h ? *h : ComponentTypeHooks();
		ECS_ASSERT(hooks.onRemove == nullptr);

		using Invoker = typename _::EachInvoker<decay_t<Func>, T>;
		hooks.onRemove = Invoker::Run;
		hooks.invoker = ECS_NEW_OBJECT<Invoker>(ECS_FWD(func));
		hooks.invokerDeleter = reinterpret_cast<InvokerDeleter>(ECS_DELETE_OBJECT<Invoker>);
		SetComponentTypeInfo(compID, hooks);
	}

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
				ComponentTypeHooks info = {};
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