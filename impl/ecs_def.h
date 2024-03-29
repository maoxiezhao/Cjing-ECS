﻿#pragma once

#include "ecs_api.h"
#include "ecs_util.h"

namespace ECS
{
	////////////////////////////////////////////////////////////////////////////////
	//// Common
	////////////////////////////////////////////////////////////////////////////////

	struct WorldImpl;
	struct EntityTable;
	struct QueryImpl;
	struct Iterator;

	// -----------------------------------------
	//            EntityID: 64                 |
	//__________________________________________
	// 	 32            |          32           |
	// ----------------------------------------|
	//   FFffFFff      |   FFFFffff            |
	// ----------------------------------------|
	//   generation    |    entity             |
	// ----------------------------------------|
	// 
	// -----------------------------------------
	//            ID: 64                       |
	//__________________________________________
	// 	 8     |     24          |    32       |
	// ----------------------------------------|
	//   ff    |     ffFFff      |   FFFFffff  |
	// ----------------------------------------|
	//  role   |   generation    |    entity   |
	// ----------------------------------------|
	//  role   |          component            |
	//------------------------------------------

	// Roles is EcsRolePair
	// -----------------------------------------
	//            EntityID: 64                 |
	//__________________________________________
	// 	 8     |     24          |    32       |
	// ----------------------------------------|
	//   ff    |     ffFFff      |   FFFFffff  |
	// ------------------------------------------
	//  Pair   |    Relation     |   Object    |
	// -----------------------------------------
	// 
	// Usage:
	// ECS_GET_PAIR_FIRST to get relation
	// ECS_GET_PAIR_SECOND to get object
	// ECS_MAKE_PAIR to make pair of relation and object

	using EntityID = U64;
	using EntityIDs = Vector<EntityID>;
	using EntityType = Vector<EntityID>;

	#define ECS_NAME_BUFFER_LENGTH 64

	#define ECS_BIT_SET(flags, bit) (flags) |= (bit)
	#define ECS_BIT_CLEAR(flags, bit) (flags) &= ~(bit) 
	#define ECS_BIT_COND(flags, bit, cond) ((cond) \
			? (ECS_BIT_SET(flags, bit)) \
			: (ECS_BIT_CLEAR(flags, bit)))
	#define ECS_BIT_IS_SET(flags, bit) ((flags) & (bit))

	#define ECS_TERM_CACHE_SIZE (4)

	#define ITERATOR_CACHE_MASK_IDS           (1u << 0u)
	#define ITERATOR_CACHE_MASK_COLUMNS       (1u << 1u)
	#define ITERATOR_CACHE_MASK_SIZES         (1u << 2u)
	#define ITERATOR_CACHE_MASK_PTRS          (1u << 3u)
	#define ITERATOR_CACHE_MASK_ALL            (255)

	#define ECS_ENTITY_ID(e) (ECS_ID_##e)

	#define ECS_ENTITY_MASK               (0xFFFFffffull)	// 32
	#define ECS_ROLE_MASK                 (0xFFull << 56)
	#define ECS_COMPONENT_MASK            (~ECS_ROLE_MASK)	// 56
	#define ECS_GENERATION_MASK           (0xFFFFull << 32)
	#define ECS_GENERATION(e)             ((e & ECS_GENERATION_MASK) >> 32)

	static const EntityID INVALID_ENTITYID = 0;
	static const EntityType EMPTY_ENTITY_TYPE = EntityType();
	static const size_t MAX_QUERY_ITEM_COUNT = 16;

	// Pair
	extern const EntityID EcsRoleShared;
	extern const EntityID EcsRolePair;
	// properties
	extern const EntityID EcsPropertyTag;
	extern const EntityID EcsPropertyNone;
	extern const EntityID EcsPropertyThis;
	extern const EntityID EcsPropertyAny;
	// Tags
	extern const EntityID EcsTagPrefab;
	extern const EntityID EcsTagDisabled;
	// Events
	extern const EntityID EcsEventTableEmpty;
	extern const EntityID EcsEventTableFill;
	extern const EntityID EcsEventOnAdd;
	extern const EntityID EcsEventOnRemove;
	// Relations
	extern const EntityID EcsRelationIsA;
	extern const EntityID EcsRelationChildOf;

	// Builtin components
	extern const EntityID EcsCompSystem;

	#define ECS_ENTITY_HI(e) (static_cast<U32>((e) >> 32))
	#define ECS_ENTITY_LOW(e) (static_cast<U32>(e))
	#define ECS_ENTITY_COMBO(lo, hi) ((static_cast<U64>(hi) << 32) + static_cast<U32>(lo))
	#define ECS_MAKE_PAIR(re, obj) (EcsRolePair | ECS_ENTITY_COMBO(obj, re))
	#define ECS_HAS_ROLE(e, p) ((e & ECS_ROLE_MASK) == p)
	#define ECS_GET_PAIR_FIRST(e) (ECS_ENTITY_HI(e & ECS_COMPONENT_MASK))
	#define ECS_GET_PAIR_SECOND(e) (ECS_ENTITY_LOW(e))
	#define ECS_HAS_RELATION(e, rela) (ECS_HAS_ROLE(e, EcsRolePair) && ECS_GET_PAIR_FIRST(e) == rela)

	inline U64 EntityTypeHash(const EntityType& entityType)
	{
		return Util::HashFunc(entityType.data(), entityType.size() * sizeof(EntityID));
	}

	////////////////////////////////////////////////////////////////////////////////
	//// Table cache
	////////////////////////////////////////////////////////////////////////////////

	// Dual link list to manage TableRecords
	struct EntityTableCacheItem : Util::ListNode<EntityTableCacheItem>
	{
		struct EntityTableCacheBase* tableCache = nullptr;
		EntityTable* table = nullptr;	// -> Owned table
		bool empty = false;
	};

	struct EntityTableCacheIterator
	{
		Util::ListNode<EntityTableCacheItem>* cur = nullptr;
		Util::ListNode<EntityTableCacheItem>* next = nullptr;
	};

	template<typename T>
	struct EntityTableCacheItemInst : EntityTableCacheItem
	{
		T data;
	};

	////////////////////////////////////////////////////////////////////////////////
	//// Query
	////////////////////////////////////////////////////////////////////////////////

	struct TableRange
	{
		EntityTable* table;
		int32_t offset;       /* Leave both members to 0 to cover entire table */
		int32_t count;
	};

	struct QueryVariable
	{
		TableRange range;
	};

	enum TermFlag
	{
		TermFlagParent = 1 << 0,
		TermFlagCascade = 1 << 1,
		TermFlagSelf = 1 << 2,
		TermFlagIsEntity = 1 << 3,
		TermFlagIsVariable =  1 << 4
	};

	enum TypeInOutKind 
	{
		InOutDefault,
		InOutNone,
		InOut,
		In,
		Out
	};

	struct TermID
	{
		EntityID id;
		U32 flags;
		EntityID traverseRelation;
	};

	struct Term
	{
		EntityID compID;
		TermID src;			// Source of term
		TermID first;		// First element of pair
		TermID second;		// Second element of pair

		TypeInOutKind inout;// Inout kind of contents
		EntityID role;
		U32 index;			// Index of term
	};

	typedef void (*IterInitAction)(WorldImpl* world, const void* iterable, Iterator* it, Term* filter);
	typedef bool (*IterNextAction)(Iterator* it);
	typedef void (*IterCallbackAction)(Iterator* it);
	
	struct Iterable
	{
		IterInitAction init;
	};

	enum FilterFlag
	{
		FilterFlagMatchThis   = 1 << 0,
		FilterFlagIsFilter    = 1 << 1,
		FilterFlagIsInstanced = 1 << 2,
		FilterFlagMatchDisabled =  1 << 3,
	};

	struct Filter
	{
		I32 termCount;
		Term* terms;
		Term termSmallCache[ECS_TERM_CACHE_SIZE];
		bool useSmallCache = false;
		Iterable iterable;
		U32 flags = 0;
	};

	struct TermIterator
	{
		Term term;
		struct ComponentRecord* current;

		EntityTableCacheIterator tableCacheIter;
		bool emptyTables = false;
		EntityTable* table;

		EntityID id;
		I32 curMatch;
		I32 matchCount;
		I32 column;
		I32 index;
		size_t size;
		void* ptr;
	};

	struct FilterIterator
	{
		Filter filter;
		I32 pivotTerm;
		TermIterator termIter;
		I32 matchesLeft;
	};

	struct QueryTableNode;

	struct QueryIterator
	{
		QueryImpl* query = nullptr;
		QueryTableNode* node = nullptr;
		QueryTableNode* prev = nullptr;
	};

	struct WorkerIterator
	{
		I32 index;
		I32 count;
	};

	struct IteratorCache 
	{
		EntityID ids[ECS_TERM_CACHE_SIZE];
		int32_t columns[ECS_TERM_CACHE_SIZE];
		size_t sizes[ECS_TERM_CACHE_SIZE];
		void* ptrs[ECS_TERM_CACHE_SIZE];

		U8 used;       // For which fields is the cache used
		U8 allocated;  // Which fields are allocated
	};

	struct IteratorPrivate 
	{
		union {
			TermIterator term;
			FilterIterator filter;
			QueryIterator query;
			WorkerIterator worker;
		} iter;
		IteratorCache cache;
	};

	enum IteratorFlag
	{
		IteratorFlagIsValid     = 1 << 0u,
		IteratorFlagIsFilter    = 1 << 1u,
		IteratorFlagIsInstanced = 1 << 2u,
		IteratorFlagNoResult    = 1 << 3u
	};

	struct Iterator
	{
	public:
		WorldImpl* world = nullptr;

		// Matched data
		size_t count = 0;
		EntityID* entities = nullptr;
		EntityID* ids = nullptr;
		size_t* sizes = nullptr;
		I32* columns = nullptr;
		void** ptrs = nullptr;
		EntityTable* table = nullptr;
		I32 offset = 0;
		void* ctx = nullptr;

		// Variable
		I32 variableCount = 0;
		QueryVariable variables[ECS_TERM_CACHE_SIZE];
		U32 variableMask = 0;

		// Query infomation
		Term* terms = nullptr;
		I32 termIndex = 0;
		I32 termCount = 0;
		I32 tableCount = 0;

		// Event
		EntityID event;

		// Context
		U32 flags = 0;

		// Impl datas
		IteratorPrivate priv;

		// Chained iters
		Iterator* chainIter = nullptr;
		IterNextAction next = nullptr;

		void* invoker = nullptr;
	};

	////////////////////////////////////////////////////////////////////////////////
	//// Create desc
	////////////////////////////////////////////////////////////////////////////////

	struct EntityCreateDesc
	{
		EntityID entity = INVALID_ENTITYID;
		const char* name = nullptr;
		bool useComponentID = false;	// For component id (0~256)
	};

	struct ComponentCreateDesc
	{
		EntityCreateDesc entity = {};
		size_t alignment = 0;
		size_t size = 0;
	};

	struct FilterCreateDesc
	{
		Term terms[MAX_QUERY_ITEM_COUNT];
	};

	/** Callback used for comparing components */
	typedef int (*QueryOrderByAction)(
		EntityID e1,
		const void* ptr1,
		EntityID e2,
		const void* ptr2);

	struct QueryCreateDesc
	{
		FilterCreateDesc filter;
		QueryOrderByAction orderBy;
	};

	using InvokerDeleter = void(*)(void* ptr);
	using SystemAction = void(*)(Iterator* it);

	struct SystemCreateDesc
	{
		EntityID entity = {};
		QueryCreateDesc query = {};
		SystemAction action;
		void* invoker;
		InvokerDeleter invokerDeleter;
		bool multiThreaded = false;
	};

	struct PipelineCreateDesc
	{
		QueryCreateDesc query = {};
	};
	
	bool FilterNextInstanced(Iterator* it);
	bool QueryNextInstanced(Iterator* it);

	////////////////////////////////////////////////////////////////////////////////
	//// Event
	////////////////////////////////////////////////////////////////////////////////

	#define ECS_TRIGGER_MAX_EVENT_COUNT (8)

	struct Observable;

	struct TriggerDesc
	{
		Term term;
		IterCallbackAction callback;
		void* ctx = nullptr;
		EntityID events[ECS_TRIGGER_MAX_EVENT_COUNT];
		I32 eventCount = 0;
		Observable* observable = nullptr;

		// Used if this trigger is part of Observer
		I32* eventID = nullptr;
	};

	struct ObserverDesc
	{
		EntityID events[ECS_TRIGGER_MAX_EVENT_COUNT];
		IterCallbackAction callback;
		FilterCreateDesc filterDesc;
		void* ctx;
	};

	struct Observer
	{
		EntityID events[ECS_TRIGGER_MAX_EVENT_COUNT];
		I32 eventCount = 0;
		IterCallbackAction callback;
		Filter filter;
		U64 id;
		I32 eventID;
		std::vector<EntityID> triggers;
		void* ctx;
		WorldImpl* world;
	};

	struct EventDesc
	{
		EntityID event;
		EntityType ids;
		EntityTable* table;
		Observable* observable;
	};

	enum class QueryEventType
	{
		Invalid,
		MatchTable,
		UnmatchTable
	};

	struct QueryEvent
	{
		QueryEventType type = QueryEventType::Invalid;
		EntityTable* table;
	};

	struct ThreadContext
	{
		void* payload = nullptr;
	};

	////////////////////////////////////////////////////////////////////////////////
	//// Components
	////////////////////////////////////////////////////////////////////////////////

	struct ComponentTypeInfo;

	using CompXtorFunc = void(*)(void* ptr, size_t count, const ComponentTypeInfo* info);
	using CompCopyFunc = void(*)(const void* srcPtr, void* dstPtr, size_t count, const ComponentTypeInfo* info);
	using CompMoveFunc = void(*)(void* srcPtr, void* dstPtr, size_t count, const ComponentTypeInfo* info);
	using CompCopyCtorFunc = void(*)(const void* srcPtr, void* dstPtr, size_t count,  const ComponentTypeInfo* info);
	using CompMoveCtorFunc = void(*)(void* srcPtr, void* dstPtr, size_t count,  const ComponentTypeInfo* info);

	struct ComponentTypeHooks
	{
		CompXtorFunc ctor;
		CompXtorFunc dtor;
		CompCopyFunc copy;
		CompMoveFunc move;
		CompCopyCtorFunc copyCtor;
		CompMoveCtorFunc moveCtor;
		CompMoveFunc moveDtor;

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
}