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
		//std::vector<U8> data;
		Util::StorageVector data;    /* Column element data */
		I64 size = 0;                /* Column element size */
		I64 alignment = 0;           /* Column element alignment */
	};

	using CompXtorFunc = void(*)(World* world, EntityID compID, size_t count, void* ptr);

	namespace Reflect
	{
		struct ObjReflectInfo
		{
			CompXtorFunc ctor;
			CompXtorFunc dtor;
		};

		template <typename T>
		void DefaultCtor()
		{

		}

		template <typename T>
		void DefaultDtor()
		{

		}
	}

	template<typename C>
	struct ComponentTypeInfo
	{
		static EntityID componentID;

		static EntityID ComponentID(World& world);
		static bool Registered();
	};
	template<typename C>
	EntityID ComponentTypeInfo<C>::componentID = INVALID_ENTITY;

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
		EntityIDs added;         // Components added between tables
		EntityIDs removed;       // Components removed between tables
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
		std::vector<ComponentColumnData> storageColumns;
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
			EntityID compID = ComponentTypeInfo<C>::ComponentID(*this);
			C* dstComp = static_cast<C*>(GetOrCreateComponent(entity, compID));
			*dstComp = comp;
		}

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
		void MoveTableEntities(EntityID srcEntity, EntityTable* srcTable, EntityID dstEntity, EntityTable* dstTable, bool construct);
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
		void MoveTable(EntityTable* srcTable, EntityTable* dstTable);
		void DeleteEntityFromTable(EntityTable* table, U32 index, bool destruct);
		void SetTableEmpty(EntityTable* table);
		void TableRemoveColumnLast(EntityTable* table);
		void TableRemoveColumn(EntityTable* table, U32 index);

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

		// Component id
		Hashmap<IDRecord*> idRecordMap;
		Util::SparseArray<IDRecord> idRecordPool;
	};

	template<class C>
	inline const EntityBuilder& EntityBuilder::with(const C& comp) const
	{
		world->AddComponent(entity, comp);
		return *this;
	}

	template<typename C>
	inline EntityID ComponentTypeInfo<C>::ComponentID(World& world)
	{
		if (!Registered())
		{
			// Register component
			componentID = world.RegisterComponent<C>();

			// Init type reflect
		}

		return componentID;
	}

	template<typename C>
	bool ComponentTypeInfo<C>::Registered()
	{
		return componentID != INVALID_ENTITY;
	}
}