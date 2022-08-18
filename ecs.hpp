#include "impl\ecs_impl.hpp"

namespace ECS
{
	struct Entity;

	template<typename... Args>
	class SystemBuilder;
	template<typename... Comps>
	class Query;
	template<typename... Comps>
	struct QueryBuilder;

	// Component id register
	namespace _
	{
		template<typename C>
		struct ComponentTypeRegister
		{
			static size_t size;
			static size_t alignment;
			static EntityID componentID;

			static EntityID ID(WorldImpl& world)
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

		private:
			static EntityID RegisterComponent(WorldImpl& world, size_t size, size_t alignment, const char* name = nullptr)
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
				return InitNewComponent(&world, desc);
			}

			static bool Registered(WorldImpl& world)
			{
				return componentID != INVALID_ENTITY && EntityExists(&world, componentID);
			}
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
		static EntityID ID(WorldImpl& world)
		{
			EntityID relation = _::ComponentTypeRegister<C::First>::ID(world);
			EntityID object = _::ComponentTypeRegister<C::Second>::ID(world);
			return ECS_MAKE_PAIR(relation, object);
		}
	};

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

	struct EntityIterator
	{
		using Iterator = IndexIterator;

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
				func(ECS::Entity(iter->world, entity), (EachColumn<std::remove_reference_t<Comps>>(args, row).Get())...);
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

	/// <summary>
	/// The world class manage all ecs data
	/// </summary>
	struct World
	{
	public:
		explicit World() :
			world(InitWorld())
		{
		}

		~World()
		{
			if (world)
				FiniWorld(world);
		}

		World(const World& obj) = delete;
		World& operator=(const World& obj) = delete;

		World(World&& obj) noexcept
		{
			world = obj.world;
			obj.world = nullptr;
		}

		World& operator=(World&& obj) noexcept
		{
			this->~World();
			world = obj.world;
			obj.world = nullptr;
			return *this;
		}

		template<typename C>
		C* GetSingletonComponent()
		{
			EntityID compID = ComponentType<C>::ID(*world);
			return static_cast<C*>(GetOrCreateComponent(world, compID, compID));
		}

		template <typename Func>
		inline void EachComponent(EntityID entity, const Func& func) const
		{
			const auto& entityType = GetEntityType(world, entity);
			if (entityType.empty())
				return;

			for (const auto& type : entityType)
				func(type);
		}

		template<typename Func>
		inline void EachChildren(EntityID entity, Func&& func)
		{
			Filter filter;
			FilterCreateDesc desc = {};
			desc.terms[0].compID = ECS_MAKE_PAIR(EcsRelationChildOf, entity);
			InitFilter(desc, filter);

			Iterator it = GetFilterIterator(world, filter);
			while (FilterIteratorNext(&it)) {
				EachInvoker<Func>(ECS_MOV(func)).Invoke(&it);
			}
		}

		template<typename T, typename Func>
		inline void SetComponenetOnAdded(Func&& func)
		{
			EntityID compID = ComponentType<T>::ID(*world);
			auto h = GetComponentTypeHooks(world, compID);
			ComponentTypeHooks hooks = h ? *h : ComponentTypeHooks();
			ECS_ASSERT(hooks.onAdd == nullptr);

			using Invoker = EachInvoker<decay_t<Func>, T>;
			hooks.onAdd = Invoker::Run;
			hooks.invoker = ECS_NEW_OBJECT<Invoker>(ECS_FWD(func));
			hooks.invokerDeleter = reinterpret_cast<InvokerDeleter>(ECS_DELETE_OBJECT<Invoker>);
			SetComponentTypeInfo(world, compID, hooks);
		}

		template<typename T, typename Func>
		inline void SetComponenetOnRemoved(Func&& func)
		{
			EntityID compID = ComponentType<T>::ID(*world);
			auto h = GetComponentTypeHooks(world, compID);
			ComponentTypeHooks hooks = h ? *h : ComponentTypeHooks();
			ECS_ASSERT(hooks.onRemove == nullptr);

			using Invoker = EachInvoker<decay_t<Func>, T>;
			hooks.onRemove = Invoker::Run;
			hooks.invoker = ECS_NEW_OBJECT<Invoker>(ECS_FWD(func));
			hooks.invokerDeleter = reinterpret_cast<InvokerDeleter>(ECS_DELETE_OBJECT<Invoker>);
			SetComponentTypeInfo(world, compID, hooks);
		}

		ECS::Entity Entity(const char* name)const;
		ECS::Entity Prefab(const char* name)const;

		template<typename... Args>
		SystemBuilder<Args...> CreateSystem();

		template<typename... Comps>
		QueryBuilder<Comps...> CreateQuery();

	private:
		WorldImpl* world;
	};

	/// <summary>
	/// Class that stores a ecs entity id
	/// </summary>
	struct IDView
	{
	public:
		IDView() :
			world(nullptr),
			entityID(INVALID_ENTITY)
		{}

		explicit IDView(EntityID id) : 
			world(nullptr),
			entityID(id) 
		{}

		explicit IDView(WorldImpl* world, EntityID id = 0) :
			world(world),
			entityID(id) 
		{}

		WorldImpl* GetWorld() {
			return world;
		}

		bool operator ==(const IDView& rhs)const {
			return world == rhs.world && entityID == rhs.entityID;
		}

		bool operator !=(const IDView& rhs)const {
			return world != rhs.world || entityID != rhs.entityID;
		}

		operator ECS::EntityID() const {
			return entityID;
		}

	protected:
		WorldImpl* world = nullptr;
		EntityID entityID = INVALID_ENTITY;
	};

	/// <summary>
	/// Entity view class
	/// </summary>
	struct EntityView : public IDView
	{
		EntityView() : IDView()  { }

		bool IsValid()const {
			return world && entityID != INVALID_ENTITY && EntityExists(world, entityID);
		}

		explicit operator bool()const {
			return IsValid();
		}
	};


	template<typename C>
	static void SetComponent(WorldImpl* world, EntityID entity, EntityID compID, const C& comp)
	{
		C& dstComp = *static_cast<C*>(GetOrCreateComponent(world, entity, compID));
		dstComp = comp;
	}

	template<typename C>
	static void SetComponent(WorldImpl* world, EntityID entity, EntityID compID, C&& comp)
	{
		C& dstComp = *static_cast<C*>(GetOrCreateComponent(world, entity, compID));
		dstComp = ECS_MOV(comp);
	}

	template<typename T, typename C>
	static void SetComponent(WorldImpl* world, EntityID entity, const C& comp)
	{
		EntityID compID = ComponentType<T>::ID(*world);
		SetComponent(world, entity, compID, comp);
	}

	template<typename T, typename C>
	static void SetComponent(WorldImpl* world, EntityID entity, C&& comp)
	{
		EntityID compID = ComponentType<T>::ID(*world);
		SetComponent(world, entity, compID, ECS_FWD(comp));
	}

	/// <summary>
	/// Entity builder class
	/// </summary>
	template<typename Self>
	struct EntityBuilderInst : public EntityView
	{
	public:
		using EntityView::EntityView;

		template<typename T>
		Self& Add()
		{
			AddComponent(world, entityID, ComponentType<T>::ID(*world));
			return ToBase();
		}

		Self& Add(EntityID compID)
		{
			AddComponent(world, entityID, compID);
			return ToBase();
		}

		Self& Add(EntityID first, EntityID second)
		{
			AddComponent(world, entityID, ECS_MAKE_PAIR(first, second));
			return ToBase();
		}

		template<typename First, typename Second>
		Self& Add()
		{
			return this->Add<First>(ComponentType<Second>::ID(*world));
		}

		template<typename First>
		Self& Add(EntityID second)
		{
			return this->Add(ComponentType<First>::ID(*world), second);
		}

		Self& ChildOf(EntityID parent)
		{
			ECS::ChildOf(world, entityID, parent);
			return ToBase();
		}

		Self& Instantiate(EntityID prefabID)
		{
			ECS::Instantiate(world, entityID, prefabID);
			return ToBase();
		}

		template <typename T, Util::if_t<!std::is_function_v<T> && Util::is_actual<T>::value> = 0>
		Self& Set(const T& value)
		{
			EntityID compID = ComponentType<T>::ID(*world);
			SetComponent(entityID, compID, value);
			return ToBase();
		}

		template <typename T, Util::if_t<!std::is_function_v<T>&& Util::is_actual<T>::value> = 0>
		Self& Set(T&& value)
		{
			EntityID compID = ComponentType<T>::ID(*world);
			SetComponent(entityID, compID, ECS_FWD(value));
			return ToBase();
		}

		template <typename First, typename Second, typename P = Util::Pair<First, Second>, typename A = Util::RealType_t<P>>
		Self& Set(const A& value)
		{
			SetComponent<P>(world, entityID, value);
			return ToBase();
		}

		template <typename T>
		Self& Remove() 
		{
			RemoveComponent(world, entityID, ComponentType<T>::ID(*world));
			return ToBase();
		}

		Self& Remove(EntityID compID) 
		{
			RemoveComponent(world, entityID, compID);
			return ToBase();
		}

		Self& Remove(EntityID first, EntityID second)
		{
			RemoveComponent(world, entityID, ECS_MAKE_PAIR(first, second));
			return ToBase();
		}

		template<typename First, typename Second>
		Self& Remove() 
		{
			return this->Remove<First>(ComponentType<Second>::ID(*world));
		}

		template<typename First>
		Self& Remove(EntityID second)
		{
			return this->Remove(ComponentType<First>::ID(*world), second);
		}

		void SetName(const char* name)
		{
			SetEntityName(world, entityID, name);
			return ToBase();
		}

		const char* GetName()
		{
			return GetEntityName(world, entityID);
		}

		void Enable()
		{
		}

		void Disable()
		{
		}

	protected:
		Self& ToBase() {
			return *static_cast<Self*>(this);
		}
	};

	/// <summary>
	/// Entity class
	/// </summary>
	struct Entity : public EntityBuilderInst<Entity>
	{
		Entity() = default;

		explicit Entity(WorldImpl* world_, const char* name)
		{
			world = world_;

			EntityCreateDesc desc = {};
			desc.name = name;
			entityID = CreateEntityID(world, desc);
		}

		explicit Entity(WorldImpl* world_, EntityID entityID_)
		{
			world = world_;
			entityID = entityID_;
		}

		template<typename T>
		T* Get()const
		{
			EntityID compID = ComponentType<T>::ID(*world);
			return static_cast<T*>(GetComponent(world, entityID, compID));
		}

		void* Get(EntityID compID)const
		{
			return GetComponent(world, entityID, compID);
		}

		template<typename First>
		First* Get(EntityID second)const
		{
			EntityID compID = ComponentType<First>::ID(*world);
			return static_cast<First*>(GetComponent(world, entityID, ECS_MAKE_PAIR(compID, second)));
		}

		template<typename First, typename Second>
		First* Get()const
		{
			return this->Get<First>(ComponentType<Second>::ID(*world));
		}

		template <typename R, typename O, typename P = Util::Pair<R, O>, typename C = typename Util::RealType<P>::type>
		C* Get(EntityID entity)
		{
			EntityID compID = ComponentType<P>::ID(*world);
			return static_cast<C*>(GetComponent(world, entity, compID));
		}

		Entity GetParent()
		{
			EntityID parent = ECS::GetParent(world, entityID);
			return Entity(world, parent);
		}

		template<typename C>
		bool Has()
		{
			EntityID compID = ComponentType<C>::ID(*world);
			return HasComponent(world, entityID, compID);
		}

		void Clear()
		{
			
		}

		void Destroy()
		{
			DeleteEntity(world, entityID);
			entityID = INVALID_ENTITY;
		}

		static Entity Null() {
			return Entity();
		}
	};

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
			this->Iterate<EachInvoker>(ECS_FWD(func));
		}

		template<typename Func>
		void Iter(Func&& func)
		{
			this->Iterate<IterInvoker>(ECS_FWD(func));
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
		QueryBuilderBase(WorldImpl* world_) :
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
		WorldImpl* world;
		Array<U64, sizeof...(Comps)> ids;
		QueryCreateDesc queryDesc = {};
		Term* currentTerm = nullptr;
	};

	template<typename... Comps>
	struct QueryBuilder : QueryBuilderBase<QueryBuilder<Comps...>, Comps...>
	{
		QueryBuilder(WorldImpl* world_) : QueryBuilderBase<QueryBuilder<Comps...>, Comps...>(world_) {}
	};

	template<typename... Comps>
	class Query final : public QueryIteratorBase<Comps...>
	{
	public:
		Query() :
			world(nullptr),
			impl(nullptr)
		{}

		Query(WorldImpl* world_, const QueryCreateDesc& desc_) :
			world(world_),
			queryDesc(desc_),
			impl(nullptr)
		{
			impl = CreateQuery(world_, desc_);
		}

		bool Valid()const
		{
			return impl != nullptr;
		}

		void Free()
		{
			if (impl != nullptr)
			{
				FiniQuery(impl);
				impl = nullptr;
			}
		}

		Iterator GetQueryIterator()override
		{
			return ECS::GetQueryIterator(impl);
		}

		bool NextQueryIterator(Iterator& iter)override
		{
			return QueryNextInstanced(&iter);
		}

	private:
		WorldImpl* world;
		QueryCreateDesc queryDesc;
		QueryImpl* impl;
	};

	class System final : public Entity
	{
	public:
		explicit System()
		{
			world = nullptr;
			entityID = INVALID_ENTITY;
		}

		explicit System(WorldImpl* world, const SystemCreateDesc& desc) :
			Entity(world, InitNewSystem(world, desc))
		{
		}

		void Run()
		{
			if (entityID != INVALID_ENTITY)
				RunSystem(world, entityID);
		}
	};

	template<typename... Comps>
	class SystemBuilder : public QueryBuilderBase<SystemBuilder<Comps...>, Comps...>
	{
	public:
		SystemBuilder(WorldImpl* world_) :
			QueryBuilderBase<SystemBuilder<Comps...>, Comps...>(world_),
			world(world_)
		{}

		template<typename Func>
		System ForEach(Func&& func)
		{
			using Invoker = EachInvoker<decay_t<Func>, Comps...>;
			return Build<Invoker>(ECS_FWD(func));
		}

		template<typename Func>
		System Iter(Func&& func)
		{
			using Invoker = IterInvoker<decay_t<Func>, Comps...>;
			return Build<Invoker>(ECS_FWD(func));
		}

	private:
		template<typename Invoker, typename Func>
		System Build(Func&& func)
		{
			Invoker* invoker = ECS_NEW_OBJECT<Invoker>(ECS_FWD(func));
			sysDesc.action = Invoker::Run;
			sysDesc.invoker = invoker;
			sysDesc.invokerDeleter = reinterpret_cast<InvokerDeleter>(ECS_DELETE_OBJECT<Invoker>);
			sysDesc.query = this->queryDesc;
			return System(world, sysDesc);
		}

	private:
		SystemCreateDesc sysDesc = {};
		WorldImpl* world;
	};

	inline ECS::Entity World::Entity(const char* name) const
	{
		return ECS::Entity(world, name);
	}

	inline ECS::Entity World::Prefab(const char* name) const
	{
		ECS::Entity entity = ECS::Entity(world, name);
		// entity.Add()
		return entity;
	}

	template<typename... Args>
	inline SystemBuilder<Args...> World::CreateSystem()
	{
		return SystemBuilder<Args...>(world);
	}

	template<typename... Comps>
	inline QueryBuilder<Comps...> World::CreateQuery()
	{
		return QueryBuilder<Comps...>(world);
	}
}