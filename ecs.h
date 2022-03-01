#pragma once

#include "common.h"
#include "ecs_util.h"

namespace ECS
{
	// EntityID
	// FFFFffff   | FFFFffff
	// Generation |  ID
#define ECS_ENTITY_MASK               (0xFFFFffffull)
#define ECS_GENERATION_MASK           (0xFFFFull << 32)
#define ECS_GENERATION(e)             ((e & ECS_GENERATION_MASK) >> 32)

	class World;

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

	struct ComponentColumnData
	{
		Util::StorageVector data;    /* Column element data */
		I64 size = 0;                /* Column element size */
		I64 alignment = 0;           /* Column element alignment */
	};

	using CompXtorFunc = void(*)(World* world, EntityID* entities, size_t count, void* ptr);
	using CompCopyFunc = void(*)(World* world, EntityID* srcEntities, EntityID* dstEntities, size_t count, const void* srcPtr, void* dstPtr);
	using CompMoveFunc = void(*)(World* world, EntityID* srcEntities, EntityID* dstEntities, size_t count, void* srcPtr, void* dstPtr);

	namespace Reflect
	{
		struct ReflectInfo
		{
			CompXtorFunc ctor;
			CompXtorFunc dtor;
			CompCopyFunc copy;
			CompMoveFunc move;
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
	//// Entity
	////////////////////////////////////////////////////////////////////////////////

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

	class EntityBuilder
	{
	public:
		EntityBuilder(World* world_);
		EntityBuilder() = delete;

		template <class C>
		const EntityBuilder& with(const C& comp) const;

		EntityID entity = INVALID_ENTITY;

	private:
		friend class World;
		World* world;
	};

	struct EntityTable;
	struct EntityInfo;

	struct EntityGraphEdge
	{
		EntityTable* next = nullptr;
		I32 diffIndex = -1;	  // mapping to EntityGraphNode diffBuffer
	};

	struct EntityGraphEdges
	{
		EntityGraphEdge loEdges[HI_COMPONENT_ID];
		Hashmap<EntityGraphEdge> hiEdges;	// Use IntrusiveHashMap
	};

	struct EntityTableDiff 
	{
		EntityIDs added;	// Components added between tables
		EntityIDs removed;	// Components removed between tables
	};

	struct EntityGraphNode
	{
		EntityGraphEdges add;
		EntityGraphEdges remove;
		std::vector<EntityTableDiff> diffs;
	};

	// Archetype for entity, archtype is a set of componentIDs
	struct EntityTable
	{
	public:
		U64 tableID = 0;
		EntityType type;
		EntityGraphNode node;
		bool isInitialized = false;
		I32* dirtyState = nullptr;
		I32 refCount = 0;
		U32 flags = 0;

		// Storage
		EntityTable* storageTable = nullptr;
		EntityType storageType;
		std::vector<I32> typeToStorageMap;
		std::vector<I32> storageToTypeMap;

		// Entities
		std::vector<EntityID> entities;
		std::vector<EntityInfo*> entityInfos;
		std::vector<ComponentColumnData> storageColumns; // Comp1, Comp2, Comp3
		std::vector<ComponentTypeInfo*> compTypeInfos;   // CompTypeInfo1, CompTypeInfo2, CompTypeInfo3
	};

	struct EntityTableRecord
	{
		bool empty = false;
		EntityTable* table = nullptr;	// -> Owned table
		I32 column = 0;
		I32 count = 0;
	};

	struct EntityTableCache
	{
		Hashmap<I32> tableIDMap;
		std::vector<EntityTableRecord> records;
		std::vector<EntityTableRecord> emptyRecords;
	};

	// EntityID <-> EntityInfo
	struct EntityInfo
	{
		EntityTable* table = nullptr;
		I32 row = 0;
	};

	struct IDRecord
	{
		EntityTableCache cache;
		Hashmap<EntityTable*> addRefs;
		Hashmap<EntityTable*> removeRefs;
		U64 recordID;
	};

	////////////////////////////////////////////////////////////////////////////////
	//// World
	////////////////////////////////////////////////////////////////////////////////

	class World
	{
	public:
		World();
		~World();

		const EntityBuilder& CreateEntity(const char* name)
		{
			entityBuilder.entity = CreateEntityID(name);
			return entityBuilder;
		}

		EntityID CreateEntityID(const char* name)
		{
			EntityCreateDesc desc = {};
			desc.name = name;
			desc.useComponentID = false;
			return CreateEntityID(desc);
		}

		EntityID FindEntityIDByName(const char* name);
		EntityID EntityIDAlive(EntityID id);
		void DeleteEntity(EntityID id);
		void EnsureEntity(EntityID id);

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

		InfoComponent* GetComponentInfo(EntityID compID);
		void* GetComponent(EntityID entity, EntityID compID);
		void* GetComponentFromTable(EntityTable& table, I32 row, EntityID compID);
		void* GetComponentWFromTable(EntityTable& table, I32 row, I32 column);

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

		bool HasComponentTypeAction(EntityID compID)const;
		ComponentTypeInfo* GetComponentTypInfo(EntityID compID);
		const ComponentTypeInfo* GetComponentTypInfo(EntityID compID)const;
		void SetComponentTypeAction(EntityID compID, Reflect::ReflectInfo& info);

	private:
		// BuildIn components
		void RegisterBuildInComponents()
		{
			RegisterComponent<InfoComponent>();
			RegisterComponent<NameComponent>();
		}
		void InitBuildInComponents();

	private:
		struct EntityInternalInfo
		{
			EntityTable* table = nullptr;
			I32 row = 0;
			EntityInfo* entityInfo = nullptr;
		};

		// Entity methods
		EntityID CreateEntityID(const EntityCreateDesc& desc);
		EntityID CreateNewEntityID();
		bool EntityTraverseAdd(EntityID entity, const EntityCreateDesc& desc, bool nameAssigned, bool isNewEntity);
		IDRecord* EnsureIDRecord(EntityID id);
		EntityID CreateNewComponentID();
		bool MergeIDToEntityType(EntityType& entityType, EntityID compID);
		IDRecord* FindIDRecord(EntityID id);
		void MoveTableEntities(EntityID srcEntity, EntityTable* srcTable, I32 srcRow, EntityID dstEntity, EntityTable* dstTable, I32 dstRow, bool construct);
		bool GetEntityInternalInfo(EntityInternalInfo& internalInfo, EntityID entity);
		void SetEntityName(EntityID entity, const char* name);

		// Component methods
		EntityID InitNewComponent(const ComponentCreateDesc& desc);
		void SetComponent(EntityID entity, EntityID compID, size_t size, const void* ptr, bool isMove);

		template<typename C>
		C* GetComponentMutableByID(EntityID entity, EntityID compID, bool* added)
		{
			return static_cast<C*>(GetComponentMutableByID(entity, compID, added));
		}
		void* GetComponentMutableByID(EntityID entity, EntityID compID, bool* added);
		void* GetOrCreateComponent(EntityID entity, EntityID compID);
		void* GetComponentMutable(EntityID entity, EntityID compID, EntityInternalInfo* info, bool* isAdded);
		void AddComponentForEntity(EntityID entity, EntityInternalInfo* info, EntityID compID);

		// Table methods
		EntityTable* FindOrCreateTableWithIDs(const std::vector<EntityID>& compIDs);
		EntityTable* FindOrCreateTableWithID(EntityTable* node, EntityID compID, EntityGraphEdge* edge);
		EntityTable* CreateNewTable(EntityType entityType);
		EntityGraphEdge* FindOrCreateEdge(EntityGraphEdges& egdes, EntityID compID);
		void DeleteEntityFromTable(EntityTable* table, U32 index, bool destruct);
		void SetTableEmpty(EntityTable* table);
		void TableRemoveColumnLast(EntityTable* table);
		void TableRemoveColumn(EntityTable* table, U32 index);
		void TableGrowColumn(std::vector<EntityID>& entities, ComponentColumnData& columnData, ComponentTypeInfo& compTypeInfo, size_t addCount, size_t newCapacity, bool construct);

		EntityTable* TableAppend(EntityTable* table, EntityID compID, EntityTableDiff& diff);
		EntityTable* TableTraverseAdd(EntityTable* table, EntityID compID, EntityTableDiff& diff);
		EntityTableRecord* GetTableRecord(EntityTable* table, EntityID compID);
		EntityTableRecord* GetTableRecrodFromCache(EntityTableCache* cache, const EntityTable& table);
		EntityTableRecord* InsertTableCache(EntityTableCache* cache, const EntityTable& table);
		void CommitTables(EntityID entity, EntityInternalInfo* info, EntityTable* dstTable, EntityTableDiff& diff, bool construct);
		U32 TableAppendNewEntity(EntityTable* table, EntityID entity, EntityInfo* info, bool construct);
		void TableRegisterAddRef(EntityTable* table, EntityID id);
		void TableRegisterRemoveRef(EntityTable* table, EntityID id);
		bool InitTable(EntityTable* table);
		void AppendTableDiff(EntityTableDiff& dst, EntityTableDiff& src);
		void ComputeTableDiff(EntityTable* t1, EntityTable* t2, EntityGraphEdge* edge);
		void PopulateTableDiff(EntityTable* table, EntityGraphEdge* edge, EntityID addID, EntityID removeID, EntityTableDiff& diff);

		// Util methods
		using EntityIDAction = bool(*)(World*, EntityTable*, EntityID, I32);
		bool ForEachEntityID(EntityTable* table, EntityIDAction action);

		static bool RegisterTable(World* world, EntityTable* table, EntityID id, I32 column);

	private:
		EntityBuilder entityBuilder = EntityBuilder(this);
		EntityID lastComponentID = 0;
		EntityID lastID = 0;

		// Entity
		Util::SparseArray<EntityInfo> entityPool;
		Hashmap<EntityID> entityNameMap;

		// Tables
		EntityTable root;
		Util::SparseArray<EntityTable> tables;
		Util::SparseArray<EntityTable*> pendingTables;
		Hashmap<EntityTable*> tableMap;

		// Component
		Hashmap<IDRecord*> idRecordMap;
		Util::SparseArray<IDRecord> idRecordPool;
		Util::SparseArray<ComponentTypeInfo> compTypePool;
	};

	////////////////////////////////////////////////////////////////////////////////
	//// Static function
	////////////////////////////////////////////////////////////////////////////////

	template<class C>
	inline const EntityBuilder& EntityBuilder::with(const C& comp) const
	{
		world->AddComponent(entity, comp);
		return *this;
	}

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

	namespace Reflect
	{
		/// ////////////////////////////////////////////////////////////////////
		/// Constructor
		////////////////////////////////////////////////////////////////////////
		template <typename T>
		void DefaultCtor(World* world, EntityID* entities, size_t count, void* ptr)
		{
			T* objArr = static_cast<T*>(ptr);
			for (size_t i = 0; i < count; i++)
				new (&objArr[i]) T();
		}

		inline void IllegalCtor(World* world, EntityID* entities, size_t count, void* ptr)
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
		void DefaultDtor(World* world, EntityID* entities, size_t count, void* ptr)
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
		void DefaultCopy(World* world, EntityID* srcEntities, EntityID* dstEntities, size_t count, const void* srcPtr, void* dstPtr)
		{
			const T* srcArr = static_cast<const T*>(srcPtr);
			T* dstArr = static_cast<T*>(dstPtr);
			for (size_t i = 0; i < count; i++)
				dstArr[i] = srcArr[i];
		}

		inline void IllegalCopy(World* world, EntityID* srcEntities, EntityID* dstEntities, size_t count, const void* srcPtr, void* dstPtr)
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
		void DefaultCopyMove(World* world, EntityID* srcEntities, EntityID* dstEntities, size_t count, void* srcPtr, void* dstPtr)
		{
			T* srcArr = static_cast<T*>(srcPtr);
			T* dstArr = static_cast<T*>(dstPtr);
			for (size_t i = 0; i < count; i++)
				dstArr[i] = std::move(srcArr[i]);
		}

		/// ////////////////////////////////////////////////////////////////////
		/// Move
		////////////////////////////////////////////////////////////////////////
		template <typename T>
		void DefaultMove(World* world, EntityID* srcEntities, EntityID* dstEntities, size_t count, void* srcPtr, void* dstPtr)
		{
			T* srcArr = static_cast<T*>(srcPtr);
			T* dstArr = static_cast<T*>(dstPtr);
			for (size_t i = 0; i < count; i++)
				dstArr[i] = std::move(srcArr[i]);
		}

		inline void IllegalMove(World* world, EntityID* srcEntities, EntityID* dstEntities, size_t count, void* srcPtr, void* dstPtr)
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
				world.SetComponentTypeAction(compID, info);
			}
		}

		template<typename T, typename std::enable_if_t<std::is_trivial<T>::value == true>*>
		void Register(World& world, EntityID compID) {}
	}
}