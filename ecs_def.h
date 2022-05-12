#pragma once

#include "common.h"
#include "ecs_util.h"

namespace ECS
{
	////////////////////////////////////////////////////////////////////////////////
	//// Common
	////////////////////////////////////////////////////////////////////////////////

	class World;
	class EntityBuilder;
	struct EntityTable;
	struct QueryImpl;
	struct Iterator;

	using EntityID = U64;
	using EntityIDs = Vector<EntityID>;
	using EntityType = Vector<EntityID>;

	static const EntityID INVALID_ENTITY = 0;
	static const EntityType EMPTY_ENTITY_TYPE = EntityType();
	static const size_t MAX_QUERY_ITEM_COUNT = 16;
	extern const size_t ENTITY_PAIR_FLAG;

	#define ECS_ENTITY_HI(e) (static_cast<U32>((e) >> 32))
	#define ECS_ENTITY_LOW(e) (static_cast<U32>(e))
	#define ECS_ENTITY_COMBO(lo, hi) ((static_cast<U64>(hi) << 32) + static_cast<U32>(lo))
	#define ECS_MAKE_PAIR(re, obj) (ENTITY_PAIR_FLAG | ECS_ENTITY_COMBO(obj, re))

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
		TermFlagCascade = 1 << 1
	};

	struct Term
	{
		EntityID pred;
		EntityID obj;
		EntityID compID;
		U64 role;
		U32 index;

		struct TermSet
		{
			U32 flags;
			U64 relation;
		} set;
	};

	typedef void (*IterInitAction)(World* world, const void* iterable, Iterator* it, Term* filter);
	typedef bool (*IterNextAction)(Iterator* it);
	
	struct Iterable
	{
		IterInitAction init;
	};

	enum FilterFlag
	{
		FilterFlagMatchThis   = 1 << 0,
		FilterFlagIsFilter    = 1 << 1,
		FilterFlagIsInstanced = 1 << 2,
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
	};

	struct FilterIterator
	{
		Filter filter;
		I32 pivotTerm;
		TermIterator termIter;
		I32 matchesLeft;
	};

	struct QueryTableMatch;

	struct QueryIterator
	{
		QueryImpl* query = nullptr;
		QueryTableMatch* node = nullptr;
		QueryTableMatch* prev = nullptr;
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
		World* world = nullptr;

		// Matched data
		size_t count = 0;
		EntityID* entities = nullptr;
		EntityID* ids = nullptr;
		size_t* sizes = nullptr;
		I32* columns = nullptr;
		void** ptrs = nullptr;
		EntityTable* table = nullptr;
		I32 offset = 0;
		
		// Variable
		I32 variableCount = 0;
		QueryVariable variables[ECS_TERM_CACHE_SIZE];
		U32 variableMask = 0;

		// Query infomation
		Term* terms = nullptr;
		I32 termIndex = 0;
		I32 termCount = 0;
		I32 tableCount = 0;

		// Context
		void* invoker = nullptr;

		// Chained iters
		Iterator* chainIter = nullptr;
		IterNextAction next = nullptr;
		
		IteratorPrivate priv;
		U32 flags = 0;
	};

	////////////////////////////////////////////////////////////////////////////////
	//// Create desc
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

	struct FilterCreateDesc
	{
		Term terms[MAX_QUERY_ITEM_COUNT];
	};

	struct QueryCreateDesc
	{
		FilterCreateDesc filter;
	};

	using InvokerDeleter = void(*)(void* ptr);
	using SystemAction = void(*)(Iterator* iter);

	struct SystemCreateDesc
	{
		EntityCreateDesc entity = {};
		QueryCreateDesc query = {};
		SystemAction action;
		void* invoker;
		InvokerDeleter invokerDeleter;
	};

	bool FilterNextInstanced(Iterator* it);
	bool QueryNextInstanced(Iterator* it);
}