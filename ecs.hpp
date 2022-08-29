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
	template<typename... Comps>
	struct PieplineBuilder;

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

		WorldImpl* GetPtr()const {
			return world;
		}

		template<typename C>
		EntityID GetComponentID()
		{
			return ComponentType<C>::ID(*world);
		}

		template<typename C>
		C* GetSingletonComponent()
		{
			EntityID compID = ComponentType<C>::ID(*world);
			return static_cast<C*>(GetMutableComponent(world, compID, compID));
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
		ECS::Entity FindEntity(const char* name)const;

		template<typename... Args>
		SystemBuilder<Args...> CreateSystem();

		template<typename... Comps>
		QueryBuilder<Comps...> CreateQuery();

		PieplineBuilder<> CreatePipeline();

		void SetThreads(I32 threads, bool startThreads = false)
		{
			ECS::SetThreads(world, threads, startThreads);
		}

		void RunPipeline(EntityID pipeline)
		{
			ECS::RunPipeline(world, pipeline);
		}

		template<typename T>
		I32 Count()const
		{
			EntityID compID = ComponentType<T>::ID(*world);
			return Count(compID);
		}

		I32 Count(EntityID compID)const 
		{
			return CountComponent(world, compID);
		}

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
			entityID(INVALID_ENTITYID)
		{}

		explicit IDView(EntityID id) :
			world(nullptr),
			entityID(id)
		{}

		explicit IDView(WorldImpl* world, EntityID id = 0) :
			world(world),
			entityID(id)
		{}

		bool IsPair()const {
			return (entityID & ECS_ROLE_MASK) == EcsRolePair;
		}

		WorldImpl* GetWorld() {
			return world;
		}

		bool operator ==(const IDView& rhs)const {
			return entityID == rhs.entityID;
		}

		bool operator !=(const IDView& rhs)const {
			return entityID != rhs.entityID;
		}

		operator ECS::EntityID() const {
			return entityID;
		}

	protected:
		WorldImpl* world = nullptr;
		EntityID entityID = INVALID_ENTITYID;
	};

	/// <summary>
	/// Entity view class
	/// </summary>
	struct EntityView : public IDView
	{
		EntityView() : IDView() { }

		bool IsValid()const {
			return world && entityID != INVALID_ENTITYID && EntityExists(world, entityID);
		}

		explicit operator bool()const {
			return IsValid();
		}

		template<typename Func>
		inline void Each(const Func& func)const
		{
			auto type = GetEntityType(world, entityID);
			if (type.empty())
				return;

			for (const auto& id : type)
			{
				IDView idView(world, id);
				func(idView);
			}
		}
	};


	template<typename C>
	static void SetComponent(WorldImpl* world, EntityID entity, EntityID compID, const C& comp)
	{
		C& dstComp = *static_cast<C*>(GetMutableComponent(world, entity, compID));
		dstComp = comp;
	}

	template<typename C>
	static void SetComponent(WorldImpl* world, EntityID entity, EntityID compID, C&& comp)
	{
		C& dstComp = *static_cast<C*>(GetMutableComponent(world, entity, compID));
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

		template <typename T, Util::if_t<!std::is_function_v<T>&& Util::is_actual<T>::value> = 0>
		Self& Set(const T& value)
		{
			EntityID compID = ComponentType<T>::ID(*world);
			SetComponent(entityID, compID, value);
			ModifiedComponent(world, entityID, compID);
			return ToBase();
		}

		template <typename T, Util::if_t<!std::is_function_v<T>&& Util::is_actual<T>::value> = 0>
		Self& Set(T&& value)
		{
			EntityID compID = ComponentType<T>::ID(*world);
			SetComponent(entityID, compID, ECS_FWD(value));
			ModifiedComponent(world, entityID, compID);
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
		}

		const char* GetName()
		{
			return GetEntityName(world, entityID);
		}

		void Enable()
		{
			EnableEntity(world, entityID, true);
		}

		void Disable()
		{
			EnableEntity(world, entityID, false);
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
		const T* Get()const
		{
			EntityID compID = ComponentType<T>::ID(*world);
			return static_cast<const T*>(GetComponent(world, entityID, compID));
		}

		const void* Get(EntityID compID)const
		{
			return GetComponent(world, entityID, compID);
		}

		template<typename First>
		const First* Get(EntityID second)const
		{
			EntityID compID = ComponentType<First>::ID(*world);
			return static_cast<const First*>(GetComponent(world, entityID, ECS_MAKE_PAIR(compID, second)));
		}

		template<typename First, typename Second>
		const First* Get()const
		{
			return this->Get<First>(ComponentType<Second>::ID(*world));
		}

		template <typename R, typename O, typename P = Util::Pair<R, O>, typename C = typename Util::RealType<P>::type>
		const C* Get()
		{
			EntityID compID = ComponentType<P>::ID(*world);
			return static_cast<const C*>(GetComponent(world, entityID, compID));
		}


		template<typename T>
		T* GetMut()const
		{
			EntityID compID = ComponentType<T>::ID(*world);
			return static_cast<T*>(GetMutableComponent(world, entityID, compID));
		}

		void* GetMut(EntityID compID)const
		{
			return GetMutableComponent(world, entityID, compID);
		}

		template<typename First>
		First* GetMut(EntityID second)const
		{
			EntityID compID = ComponentType<First>::ID(*world);
			return static_cast<First*>(GetMutableComponent(world, entityID, ECS_MAKE_PAIR(compID, second)));
		}

		template<typename First, typename Second>
		First* GetMut()const
		{
			return this->GetMut<First>(ComponentType<Second>::ID(*world));
		}

		template <typename R, typename O, typename P = Util::Pair<R, O>, typename C = typename Util::RealType<P>::type>
		C* GetMut()
		{
			EntityID compID = ComponentType<P>::ID(*world);
			return static_cast<C*>(GetMutableComponent(world, entityID, compID));
		}

		Entity GetParent()
		{
			EntityID parent = ECS::GetParent(world, entityID);
			return Entity(world, parent);
		}

		void RemoveParent()
		{
			EntityID parent = ECS::GetParent(world, entityID);
			if (parent != ECS::INVALID_ENTITYID)
				RemoveComponent(world, entityID, ECS_MAKE_PAIR(EcsRelationChildOf, parent));
		}

		template<typename C>
		bool Has()const
		{
			EntityID compID = ComponentType<C>::ID(*world);
			return HasComponent(world, entityID, compID);
		}

		bool Has(EntityID compID)const
		{
			return HasComponent(world, entityID, compID);
		}

		void Clear()
		{
			ClearEntity(world, entityID);
		}

		void Destroy()
		{
			DeleteEntity(world, entityID);
			entityID = INVALID_ENTITYID;
		}

		static Entity Null() {
			return Entity();
		}
	};

	const static ECS::Entity INVALID_ENTITY = ECS::Entity::Null();

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

	template<typename Base>
	struct TermBuilder
	{
	public:
		TermBuilder() = default;
		TermBuilder(Term* term_) {
			SetTerm(term_);
		}

		Base& Src() {
			ECS_ASSERT(term != nullptr);
			current = &term->src;
			return *this;
		}

		Base& First()
		{
			ECS_ASSERT(term != nullptr);
			current = &term->first;
			return *this;
		}

		Base& Second()
		{
			ECS_ASSERT(term != nullptr);
			current = &term->second;
			return *this;
		}

		Base& SetID(EntityID id)
		{
			ECS_ASSERT(current != nullptr);
			current->id = id;
			return *this;
		}

		Base& Src(EntityID id)
		{
			Src();
			SetID(id);
			return *this;
		}

		Base& First(EntityID id)
		{
			First();
			SetID(id);
			return *this;
		}

		Base& Second(EntityID id)
		{
			Second();
			SetID(id);
			return *this;
		}

		template<typename T>
		Base& Src()
		{
			Src(ComponentType<T>::ID(*GetWorld()));
			return *this;
		}

		template<typename T>
		Base& First()
		{
			First(ComponentType<T>::ID(*GetWorld()));
			return *this;
		}

		template<typename T>
		Base& Second()
		{
			Second(ComponentType<T>::ID(*GetWorld()));
			return *this;
		}

		Base& CompID(EntityID id)
		{
			ECS_ASSERT(term != nullptr);
			if (id & ECS_ROLE_MASK)
				term->compID = id;
			else
				term->first.id = id;
			return *this;
		}

		Base& InOut(TypeInOutKind inout)
		{
			ECS_ASSERT(term != nullptr);
			term->inout = inout;
			return *this;
		}

		Base& InOutNone()
		{
			ECS_ASSERT(term != nullptr);
			term->inout = TypeInOutKind::InOutNone;
			return *this;
		}

		Base& Role(EntityID role)
		{
			ECS_ASSERT(term != nullptr);
			term->role = role;
		}

		Base& Parent()
		{
			ECS_ASSERT(current != nullptr);
			current->flags |= TermFlagParent;
			return *this;
		}

		Base& Cascade()
		{
			ECS_ASSERT(current != nullptr);
			current->flags |= TermFlagCascade;
			return *this;
		}

	protected:
		virtual ECS::WorldImpl* GetWorld() = 0;

		void SetTerm(Term* term_)
		{
			term = term_;
			if (term)
				current = &term->src;
			else
				current = nullptr;
		}

	private:
		operator Base& () {
			return *static_cast<Base*>(this);
		}

		TermID* current = nullptr;
		Term* term = nullptr;
	};

	template<typename Base, typename... Comps>
	struct QueryBuilderBase : public TermBuilder<Base>
	{
	public:
		QueryBuilderBase(WorldImpl* world_) :
			world(world_)
		{
		}

		Base& Term()
		{
			ECS_ASSERT(termIndex < MAX_QUERY_ITEM_COUNT);
			this->SetTerm(&queryDesc.filter.terms[termIndex]);
			termIndex++;
			return*this;
		}

		Base& Term(EntityID id)
		{
			this->Term();
			this->CompID(id);
			return *this;
		}

		template<typename T>
		Base& Term()
		{
			this->Term();
			this->CompID(ComponentType<T>::ID(*world));
			this->InOut(_::TypeToInout<T>());
			return *this;
		}

		Base& TermAT(I32 index)
		{
			ECS_ASSERT(index >= 0);
			I32 prevTermIndex = termIndex;
			termIndex = index;
			this->Term();
			termIndex = prevTermIndex;
			return *this;
		}

		Base& Arg(I32 index)
		{
			return TermAT(index);
		}

	protected:
		ECS::WorldImpl* GetWorld()override {
			return world;
		}

		operator Base& () {
			return *static_cast<Base*>(this);
		}

	protected:
		WorldImpl* world;
		QueryCreateDesc queryDesc = {};
		ECS::Term* currentTerm = nullptr;
		I32 termIndex = 0;
	};

	template<typename... Comps>
	struct TermSig
	{
		Array<U64, sizeof...(Comps)> ids;
		Array<TypeInOutKind, sizeof...(Comps)> inout;

		TermSig(WorldImpl* world) :
			ids({ (ComponentType<Comps>::ID(*world))... }),
			inout({ (_::TypeToInout<Comps>())... })
		{}

		template<typename Builder>
		void Populate(Builder b)
		{
			for (U32 i = 0; i < ids.size(); i++)
				b->TermAT(i).CompID(ids[i]).InOut(inout[i]);
		}
	};

	template<typename... Comps>
	struct QueryBuilder : public QueryBuilderBase<QueryBuilder<Comps...>, Comps...>
	{
		QueryBuilder(WorldImpl* world_) : QueryBuilderBase<QueryBuilder<Comps...>, Comps...>(world_)
		{
			TermSig<Comps...>(world_).Populate(this);
		}

		Query<Comps...> Build()
		{
			return Query<Comps...>(this->world, this->queryDesc);
		}
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
			return ECS::GetQueryIterator(world, impl);
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
			entityID = INVALID_ENTITYID;
		}

		explicit System(WorldImpl* world, const SystemCreateDesc& desc) :
			Entity(world, InitNewSystem(world, desc))
		{
		}

		void Run()
		{
			if (entityID != INVALID_ENTITYID)
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
		{
			TermSig<Comps...>(world_).Populate(this);

			EntityCreateDesc entityDesc = {};
			sysDesc.entity = CreateEntityID(world_, entityDesc);
		}

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

		template<typename T>
		SystemBuilder& Kind()
		{
			EntityID kindType = ComponentType<T>::ID(*world);
			AddComponent(world, sysDesc.entity, kindType);
			return *this;
		}

		SystemBuilder& MultiThread(bool multiThreaded)
		{
			sysDesc.multiThreaded = multiThreaded;
			return *this;
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

	class Pipeline final : public Entity
	{
	public:
		explicit Pipeline()
		{
			world = nullptr;
			entityID = INVALID_ENTITYID;
		}

		explicit Pipeline(WorldImpl* world, const PipelineCreateDesc& desc) :
			Entity(world, ECS::InitPipeline(world, desc))
		{
		}
	};

	template<typename... Comps>
	struct PieplineBuilder : public QueryBuilderBase<PieplineBuilder<Comps...>, Comps...>
	{
		PieplineBuilder(WorldImpl* world_) : QueryBuilderBase<PieplineBuilder<Comps...>, Comps...>(world_)
		{
			TermSig<Comps...>(world_).Populate(this);
		}

		Pipeline Build()
		{
			desc.query = this->queryDesc;
			return Pipeline(this->world, desc);
		}

	private:
		PipelineCreateDesc desc;
	};

	inline ECS::Entity World::Entity(const char* name) const
	{
		return ECS::Entity(world, name);
	}

	inline ECS::Entity World::Prefab(const char* name) const
	{
		ECS::Entity entity = ECS::Entity(world, name);
		entity.Add(EcsTagPrefab);
		return entity;
	}

	inline ECS::Entity World::FindEntity(const char* name) const
	{
		EntityID entityID = ECS::FindEntityIDByName(world, name);
		return ECS::Entity(world, entityID);
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

	inline PieplineBuilder<> World::CreatePipeline()
	{
		return PieplineBuilder<>(world);
	}
}