#include "ecs_impl_types.h"
#include "ecs_query.h"
#include "ecs_world.h"
#include "ecs_table.h"
#include "ecs_observer.h"
#include "ecs_iter.h"

namespace ECS
{
	bool NextFilterIter(Iterator* it)
	{
		ECS_ASSERT(it != nullptr);
		return FilterNextInstanced(it);
	}

	bool FilterIteratorNext(Iterator* it)
	{
		return NextFilterIter(it);
	}

	void InitFilterIter(WorldImpl* world, const void* iterable, Iterator* it, Term* filter)
	{
	}

	void InitQueryIter(WorldImpl* world, const void* iterable, Iterator* it, Term* filter)
	{
	}

	////////////////////////////////////////////////////////////////////////////////
	//// Term
	////////////////////////////////////////////////////////////////////////////////

	bool IsTermInited(const Term& term)
	{
		return term.compID != 0 || term.first.id != 0;
	}

	bool IsTermMatchThis(const Term& term)
	{
		return (term.src.flags & TermFlagIsVariable) && (term.src.id == EcsPropertyThis);
	}

	bool FinalizeTermIDs(Term& term)
	{
		TermID& src = term.src;

		// If source is null, set defaults
		if (src.id == 0 && !(src.flags & TermFlagIsEntity))
		{
			src.id = EcsPropertyThis;
			src.flags |= TermFlagIsVariable;
		}

		auto FinializeTermIDFlags = [&](TermID& termID)
		{
			// Set entity is Entity or Variable
			if (!(termID.flags & TermFlagIsEntity) && !(termID.flags & TermFlagIsVariable))
			{
				if (termID.id) 
				{
					if (termID.id == EcsPropertyThis ||
						termID.id == EcsPropertyNone ||
						termID.id == EcsPropertyAny)
					{
						termID.flags |= TermFlagIsVariable;
					}
					else {
						termID.flags |= TermFlagIsEntity;
					}
				}
			}

			if (termID.flags & TermFlagParent)
			{
				termID.traverseRelation = EcsRelationChildOf;
			}
		};

		FinializeTermIDFlags(src);
		FinializeTermIDFlags(term.first);
		FinializeTermIDFlags(term.second);

		return true;
	}

	bool PopulateFromTermID(Term& term)
	{
		EntityID first = 0;
		EntityID second = 0;
		EntityID role = term.compID & ECS_ROLE_MASK;

		if (!role && term.role)
		{
			role = term.role;
			term.compID |= role;
		}

		if (term.role && term.role != role)
		{
			ECS_ERROR("Missing role between term.id and term.role");
			return false;
		}

		term.role = role;

		if (ECS_HAS_ROLE(term.compID, EcsRolePair))
		{
			first = ECS_GET_PAIR_FIRST(term.compID);
			second = ECS_GET_PAIR_SECOND(term.compID);

			if (!first)
			{
				ECS_ERROR("Missing pred of component id");
				return false;
			}

			if (!second)
			{
				ECS_ERROR("Missing obj of component id");
				return false;
			}
		}
		else
		{
			first = term.compID & ECS_COMPONENT_MASK;
			if (!first)
			{
				ECS_ERROR("Missing pred of component id");
				return false;
			}
		}

		term.first.id = first;
		term.second.id = second;
		return true;
	}

	bool PopulateToTermID(Term& term)
	{
		// Get component id from first, second and role

		EntityID first = term.first.id;
		EntityID second = term.second.id;
		EntityID role = term.role;

		if (first & ECS_ROLE_MASK)
			return false;

		if (second & ECS_ROLE_MASK)
			return false;

		if (second != INVALID_ENTITY && role == INVALID_ENTITY)
			role = term.role = EcsRolePair;

		if (second == INVALID_ENTITY && !ECS_HAS_ROLE(role, EcsRolePair))
		{
			term.compID = first | role;
		}
		else
		{
			// Role must is EcsRolePair
			if (!ECS_HAS_ROLE(role, EcsRolePair))
			{
				ECS_ASSERT(false);
				return false;
			}

			term.compID = ECS_MAKE_PAIR(first, second);
		}
		return true;
	}

	bool FinalizeTerm(Term& term)
	{
		// If comp id is set, populate term from the comp id
		if (term.compID != INVALID_ENTITY)
		{
			if (!PopulateFromTermID(term))
				return false;
		}

		if (!FinalizeTermIDs(term))
			return false;

		if (term.compID == INVALID_ENTITY && !PopulateToTermID(term))
			return false;

		return true;
	}

	void InitTermIterNoData(TermIterator& iter)
	{
		iter.term = {};
		iter.term.index = -1;
		iter.current = nullptr;
	}

	void InitTermIter(WorldImpl* world, Term& term, TermIterator& iter, bool emptyTables)
	{
		iter.term = term;
		iter.index = 0;
		iter.current = GetComponentRecord(world, term.compID);

		if (iter.current)
		{
			// Empty tables
			if (emptyTables)
			{
				iter.tableCacheIter.cur = nullptr;
				iter.tableCacheIter.next = iter.current->cache.emptyTables.first;
				emptyTables = iter.tableCacheIter.next != nullptr;
				if (emptyTables)
					iter.emptyTables = true;
			}

			// Non-empty tables
			if (!emptyTables)
			{
				iter.tableCacheIter.cur = nullptr;
				iter.tableCacheIter.next = iter.current->cache.tables.first;
			}
		}
		else
		{
			InitTermIterNoData(iter);
		}
	}

	////////////////////////////////////////////////////////////////////////////////
	//// Filter
	////////////////////////////////////////////////////////////////////////////////

	bool FinalizeFilter(Filter& filter)
	{
		ECS_BIT_SET(filter.flags, FilterFlagMatchThis);
		ECS_BIT_SET(filter.flags, FilterFlagIsFilter);

		for (int i = 0; i < filter.termCount; i++)
		{
			Term& term = filter.terms[i];
			if (!FinalizeTerm(term))
				return false;

			term.index = i;
		}

		return true;
	}

	bool InitFilter(const FilterCreateDesc& desc, Filter& outFilter)
	{
		Filter filter;
		I32 termCount = 0;
		for (int i = 0; i < MAX_QUERY_ITEM_COUNT; i++)
		{
			if (!IsTermInited(desc.terms[i]))
				break;

			termCount++;
		}

		filter.terms = nullptr;
		filter.termCount = termCount;

		// Copy terms from the desc
		if (termCount > 0)
		{
			Term* terms = (Term*)desc.terms;
			if (termCount <= QUERY_ITEM_SMALL_CACHE_SIZE)
			{
				terms = filter.termSmallCache;
				filter.useSmallCache = true;
			}
			else
			{
				terms = ECS_MALLOC_T_N(Term, termCount);
			}

			if (terms != desc.terms)
				memcpy(terms, desc.terms, sizeof(Term) * termCount);

			filter.terms = terms;
		}

		if (!FinalizeFilter(filter))
		{
			FiniFilter(filter);
			return false;
		}

		outFilter = filter;
		if (outFilter.useSmallCache)
			outFilter.terms = outFilter.termSmallCache;
		outFilter.iterable.init = InitFilterIter;

		return true;
	}

	void FiniFilter(Filter& filter)
	{
		if (filter.terms != nullptr)
		{
			if (!filter.useSmallCache)
				ECS_FREE(filter.terms);
			filter.terms = nullptr;
		}
	}

	////////////////////////////////////////////////////////////////////////////////
	//// Query
	////////////////////////////////////////////////////////////////////////////////

	// Compute group id by cascade
	// Traverse the hierarchy of an component type by parent relation (Parent-Children)
	// to get depth, return the depth as group id
	U64 ComputeGroupIDByCascade(QueryImpl* query, QueryTableMatch* node)
	{
		I32 depth = 0;
		if (TableSearchRelationLast(
			node->table,
			query->groupByID,
			query->groupByItem->src.traverseRelation,
			0, 0,
			&depth) != -1)
			return (U64)depth;
		else
			return 0;
	}

	// Compute group id
	U64 ComputeGroupID(QueryImpl* query, QueryTableMatch* node)
	{
		// TODO: add group type
		return ComputeGroupIDByCascade(query, node);
	}

	// Find the insertion node of the group which has the closest groupID
	QueryTableMatch* QueryFindGroupInsertionNode(QueryImpl* query, U64 groupID)
	{
		ECS_ASSERT(query->groupByID != INVALID_ENTITY);

		QueryTableMatchList* closedList = nullptr;
		U64 closedGroupID = 0;
		for (auto& kvp : query->groups)
		{
			U64 curGroupID = kvp.first;
			if (curGroupID >= groupID)
				continue;

			QueryTableMatchList& list = kvp.second;
			if (list.last == nullptr)
				continue;

			if (closedList == nullptr || (groupID - curGroupID) < (groupID - closedGroupID))
			{
				closedList = &list;
				closedGroupID = curGroupID;
			}
		}
		return closedList != nullptr ? closedList->last->Cast() : nullptr;
	}

	// Create group for the matched table node and inster into the query list
	void QueryCreateGroup(QueryImpl* query, QueryTableMatch* node)
	{
		// Find the insertion point for current group
		QueryTableMatch* insertPoint = QueryFindGroupInsertionNode(query, node->groupID);
		if (insertPoint == nullptr)
		{
			// No insertion point, just insert into the orderdTableList
			QueryTableMatchList& list = query->tableList;
			if (list.first)
			{
				node->next = list.first;
				list.first->prev = node;
				list.first = node;
			}
			else
			{
				list.first = node;
				list.last = node;
			}
		}
		else
		{
			Util::ListNode<QueryTableMatch>* next = insertPoint->next;
			node->prev = insertPoint;
			insertPoint->next = node;
			node->next = next;
			if (next)
				next->prev = node;
			else
				query->tableList.last = node;
		}
	}

	void QueryInsertTableMatchNode(QueryImpl* query, QueryTableMatch* node)
	{
		ECS_ASSERT(node->prev == nullptr && node->next == nullptr);

		// Compute group id
		bool groupByID = query->groupByID != INVALID_ENTITY;
		if (groupByID)
			node->groupID = ComputeGroupID(query, node);
		else
			node->groupID = 0;

		// Get target match list
		QueryTableMatchList* list = nullptr;
		if (groupByID)
			list = &query->groups[node->groupID];
		else
			list = &query->tableList;

		// Insert into list
		if (list->last)
		{
			// Insert at the end of list if list is init
			Util::ListNode<QueryTableMatch>* last = list->last;
			Util::ListNode<QueryTableMatch>* lastNext = last->next;
			node->prev = last;
			node->next = lastNext;
			last->next = node;
			if (lastNext != nullptr)
				lastNext->prev = node;
			list->last = node;

			if (groupByID)
			{
				// If group by id, is need to update the default match list
				if (query->tableList.last == last)
					query->tableList.last = node;
			}
		}
		else
		{
			list->first = node;
			list->last = node;

			if (groupByID)
			{
				// If the table needs to be grouped, manage the group list, 
				// and the nodes of group list are sorted according to the GroupID
				QueryCreateGroup(query, node);
			}
		}

		if (groupByID)
			query->tableList.count++;

		list->count++;
		query->matchingCount++;
	}

	void QueryRemoveTableMatchNode(QueryImpl* query, QueryTableMatch* node)
	{
		Util::ListNode<QueryTableMatch>* next = node->next;
		Util::ListNode<QueryTableMatch>* prev = node->prev;
		if (prev)
			prev->next = next;
		if (next)
			next->prev = prev;

		QueryTableMatchList& list = query->tableList;
		ECS_ASSERT(list.count > 0);
		list.count--;

		if (list.first == node)
			list.first = next;
		if (list.last == node)
			list.last = prev;

		node->prev = nullptr;
		node->next = nullptr;

		query->matchingCount--;
	}

	void UpdateQueryTableMatch(QueryImpl* query, EntityTable* table, bool isEmpty)
	{
		I32 prevCount = query->cache.GetTableCount();
		query->cache.SetTableCacheState(table, isEmpty);
		I32 curCount = query->cache.GetTableCount();

		// The count of matching table is changed, need to update the tableList
		if (prevCount != curCount)
		{
			QueryTableCache* qt = (QueryTableCache*)query->cache.GetTableCache(table);
			ECS_ASSERT(qt != nullptr);

			QueryTableMatch* cur, * next;
			for (cur = qt->data.first; cur != nullptr; cur = next)
			{
				next = cur->nextMatch;

				if (isEmpty)
					QueryRemoveTableMatchNode(query, cur);
				else
					QueryInsertTableMatchNode(query, cur);
			}
		}
	}

	QueryTableMatch* QueryCreateTableMatchNode(QueryTableCache* cache)
	{
		QueryTableMatch* tableMatch = ECS_CALLOC_T(QueryTableMatch);

		ECS_ASSERT(tableMatch);
		if (cache->data.first == nullptr)
		{
			cache->data.first = tableMatch;
			cache->data.last = tableMatch;
		}
		else
		{
			cache->data.last->next = tableMatch;
			cache->data.last = tableMatch;
		}
		return tableMatch;
	}

	QueryTableMatch* QueryAddTableMatch(QueryImpl* query, QueryTableCache* qt, EntityTable* table)
	{
		U32 termCount = query->filter.termCount;

		QueryTableMatch* qm = QueryCreateTableMatchNode(qt);
		ECS_ASSERT(qm);
		qm->table = table;
		qm->termCount = termCount;
		qm->ids = ECS_CALLOC_T_N(EntityID, termCount);
		qm->columns = ECS_CALLOC_T_N(I32, termCount);
		qm->sizes = ECS_CALLOC_T_N(size_t, termCount);

		if (table != nullptr && GetTableCount(table) != 0)
			QueryInsertTableMatchNode(query, qm);

		return qm;
	}

	void QuerySetTableMatch(QueryImpl* query, QueryTableMatch* qm, EntityTable* table, Iterator& it)
	{
		I32 termCount = query->filter.termCount;
		memcpy(qm->columns, it.columns, sizeof(I32) * termCount);
		memcpy(qm->ids, it.ids, sizeof(EntityID) * termCount);
		memcpy(qm->sizes, it.sizes, sizeof(size_t) * termCount);
	}

	void QueryFreeTableCache(QueryImpl* query, QueryTableCache* queryTable)
	{
		QueryTableMatch* cur, * next;
		for (cur = queryTable->data.first; cur != nullptr; cur = next)
		{
			QueryTableMatch* match = cur->Cast();
			ECS_FREE(match->ids);
			ECS_FREE(match->columns);
			ECS_FREE(match->sizes);

			// Remove non-emtpy table
			if (!queryTable->empty)
				QueryRemoveTableMatchNode(query, match);

			next = cur->nextMatch;
			ECS_FREE(cur);
		}
		ECS_FREE(queryTable);
	}

	// Match target table for query
	bool MatchTable(QueryImpl* query, EntityTable* table)
	{
		Filter& filter = query->filter;
		I32 varID = -1;
		if (ECS_BIT_IS_SET(filter.flags, FilterFlagMatchThis))
			varID = 0;

		if (varID == -1)
			return false;

		WorldImpl* world = query->world;
		ECS_ASSERT(world != nullptr);

		Iterator it = GetFilterIterator(world, query->filter);
		ECS_BIT_SET(it.flags, IteratorFlagIsInstanced);
		ECS_BIT_SET(it.flags, IteratorFlagIsFilter);

		TableRange range = {};
		range.table = table;
		SetIteratorVar(it, varID, range);

		QueryTableCache* queryTable = nullptr;
		while (NextFilterIter(&it))
		{
			ECS_ASSERT(it.table == table);
			if (queryTable == nullptr)
			{
				queryTable = ECS_CALLOC_T(QueryTableCache);
				query->cache.InsertTableIntoCache(it.table, queryTable);
				table = it.table;
			}

			QueryTableMatch* qm = QueryAddTableMatch(query, queryTable, table);
			QuerySetTableMatch(query, qm, table, it);
		}
		return queryTable != nullptr;
	}

	void UnmatchTable(QueryImpl* query, EntityTable* table)
	{
		QueryTableCache* qt = (QueryTableCache*)query->cache.RemoveTableFromCache(table);
		if (qt != nullptr)
			QueryFreeTableCache(query, qt);
	}

	// Match exsiting tables for query
	void MatchTables(QueryImpl* query)
	{
		if (query->filter.termCount <= 0)
			return;

		Iterator it = GetFilterIterator(query->world, query->filter);
		ECS_BIT_SET(it.flags, IteratorFlagIsFilter); // Just match metadata only
		ECS_BIT_SET(it.flags, IteratorFlagIsInstanced);

		QueryTableCache* queryTable = nullptr;
		EntityTable* table = nullptr;
		while (NextFilterIter(&it))
		{
			if (table != it.table || (table != nullptr && queryTable == nullptr))
			{
				queryTable = ECS_CALLOC_T(QueryTableCache);
				query->cache.InsertTableIntoCache(it.table, queryTable);
				table = it.table;
			}

			QueryTableMatch* qm = QueryAddTableMatch(query, queryTable, table);
			QuerySetTableMatch(query, qm, table, it);
		}
	}

	void FiniQuery(QueryImpl* query)
	{
		if (query == nullptr || query->queryID == 0)
			return;

		WorldImpl* world = query->world;
		ECS_ASSERT(world != nullptr);

		// Delete the observer
		if (!world->isFini)
		{
			if (query->observer != INVALID_ENTITY)
				DeleteEntity(world, query->observer);
		}

		// TODO: reinterpret_cast 
		// Free query table cache 
		QueryTableCache* cache = nullptr;
		EntityTableCacheIterator iter = GetTableCacheListIter(&query->cache, false);
		while (cache = (QueryTableCache*)GetTableCacheListIterNext(iter))
			QueryFreeTableCache(query, cache);

		iter = GetTableCacheListIter(&query->cache, true);
		while (cache = (QueryTableCache*)GetTableCacheListIterNext(iter))
			QueryFreeTableCache(query, cache);

		world->queryPool.Remove(query->queryID);
	}

	static void QueryNotifyTrigger(Iterator* it)
	{
		WorldImpl* world = (WorldImpl*)it->world;
		Observer* observer = (Observer*)it->ctx;

		// Check if this event is already handled
		if (observer->eventID == world->eventID)
			return;
		observer->eventID = world->eventID;

		QueryImpl* query = (QueryImpl*)observer->ctx;
		ECS_ASSERT(query != nullptr);
		ECS_ASSERT(it->table != nullptr);

		// Check the table is matching for query
		if (GetTableRecordFromCache(&query->cache, it->table) == nullptr)
			return;

		if (it->event == EcsEventTableFill)
			UpdateQueryTableMatch(query, it->table, false);
		else if (it->event == EcsEventTableEmpty)
			UpdateQueryTableMatch(query, it->table, true);
	}

	void ProcessQueryFlags(QueryImpl* query)
	{
		auto ChckTermIDValid = [&](TermID& term) {
			if ((term.flags & TermFlagIsVariable) != 0)
				return false;

			return true;
		};

		for (int i = 0; i < query->filter.termCount; i++)
		{
			Term& queryItem = query->filter.terms[i];
			bool isSrcValid = ChckTermIDValid(queryItem.src);
			bool isFirstValid = ChckTermIDValid(queryItem.first);
			ECS_ASSERT(isSrcValid || IsTermMatchThis(queryItem));
			ECS_ASSERT(isFirstValid);

			if (queryItem.src.flags & TermFlagCascade)
			{
				ECS_ASSERT(query->sortByItemIndex == 0);
				query->sortByItemIndex = i + 1;
			}
		}
	}

	QueryImpl* CreateQuery(WorldImpl* world, const QueryCreateDesc& desc)
	{
		ECS_ASSERT(world->isFini == false);

		QueryImpl* ret = world->queryPool.Requset();
		ECS_ASSERT(ret != nullptr);
		ret->queryID = world->queryPool.GetLastID();
		ret->world = world;

		// Init query filter
		if (!InitFilter(desc.filter, ret->filter))
			goto error;

		ret->iterable.init = InitQueryIter;
		ret->prevMatchingCount = -1;

		// Create event observer
		if (ret->filter.termCount > 0)
		{
			ObserverDesc observerDesc = {};
			observerDesc.callback = QueryNotifyTrigger;
			observerDesc.events[0] = EcsEventTableEmpty;
			observerDesc.events[1] = EcsEventTableFill;
			observerDesc.filterDesc = desc.filter;
			observerDesc.ctx = ret;

			ret->observer = CreateObserver(world, observerDesc);
			if (ret->observer == INVALID_ENTITY)
				goto error;
		}

		// Process query flags
		ProcessQueryFlags(ret);

		// Group before matching
		if (ret->sortByItemIndex > 0)
		{
			ret->groupByID = ret->filter.terms[ret->sortByItemIndex - 1].compID;
			ret->groupByItem = &ret->filter.terms[ret->sortByItemIndex - 1];
		}

		// Match exsiting tables and add into cache if query cached
		MatchTables(ret);

		return ret;

	error:
		if (ret != nullptr)
			FiniQuery(ret);
		return nullptr;
	}

	void DestroyQuery(QueryImpl* query)
	{
		if (query != nullptr)
			FiniQuery(query);
	}

	Iterator GetFilterIterator(WorldImpl* world, Filter& filter)
	{
		FlushPendingTables(world);

		Iterator iter = {};
		iter.world = world;
		iter.terms = filter.terms;
		iter.termCount = filter.termCount;
		iter.next = NextFilterIter;

		FilterIterator& filterIter = iter.priv.iter.filter;
		filterIter.pivotTerm = -1;
		filterIter.filter = filter;

		if (filter.useSmallCache)
			filterIter.filter.terms = filterIter.filter.termSmallCache;

		bool valid = FinalizeFilter(filterIter.filter);
		ECS_ASSERT(valid == true);

		// Find the pivot term with the smallest number of table
		auto GetPivotItem = [&](Iterator& iter)->I32
		{
			I32 pivotItem = -1;
			I32 minTableCount = -1;
			for (int i = 0; i < iter.termCount; i++)
			{
				Term& term = iter.terms[i];
				EntityID compID = term.compID;

				ComponentRecord* compRecord = GetComponentRecord(world, compID);
				if (compRecord == nullptr)
					return -2;

				I32 tableCount = compRecord->cache.GetTableCount();
				if (minTableCount == -1 || tableCount < minTableCount)
				{
					pivotItem = i;
					minTableCount = tableCount;
				}
			}
			return pivotItem;
		};
		filterIter.pivotTerm = GetPivotItem(iter);
		if (filterIter.pivotTerm == -2)
			InitTermIterNoData(filterIter.termIter);
		else
			InitTermIter(world, filter.terms[filterIter.pivotTerm], filterIter.termIter, true);

		// Finally init iterator
		InitIterator(iter, ITERATOR_CACHE_MASK_ALL);

		if (ECS_BIT_IS_SET(filter.flags, FilterFlagIsFilter))
		{
			// Make space for one variable if the filter has terms for This var 
			iter.variableCount = 1;
		}

		return iter;
	}

	void FiniQueries(WorldImpl* world)
	{
		size_t queryCount = world->queryPool.Count();
		for (size_t i = 0; i < queryCount; i++)
		{
			QueryImpl* query = world->queryPool.Get(i);
			FiniQuery(query);
		}
	}

	Iterator GetQueryIterator(QueryImpl* query)
	{
		ECS_ASSERT(query != nullptr);
		ECS_ASSERT(query->world != nullptr);

		WorldImpl* world = query->world;
		FlushPendingTables(world);

		query->prevMatchingCount = query->matchingCount;

		QueryIterator queryIt = {};
		queryIt.query = query;
		queryIt.node = (QueryTableMatch*)query->tableList.first;

		Iterator iter = {};
		iter.world = world;
		iter.terms = query->filter.terms;
		iter.termCount = query->filter.termCount;
		iter.tableCount = query->cache.GetTableCount();
		iter.priv.iter.query = queryIt;
		iter.next = NextQueryIter;

		Filter& filter = query->filter;
		I32 termCount = filter.termCount;
		Iterator fit = GetFilterIterator(world, filter);
		if (!NextFilterIter(&fit))
		{
			FiniIterator(fit);
			goto noresults;
		}

		InitIterator(iter, ITERATOR_CACHE_MASK_ALL);

		// Copy data from fit
		if (termCount > 0)
		{
			memcpy(iter.columns, fit.columns, sizeof(I32) * termCount);
			memcpy(iter.ids, fit.ids, sizeof(EntityID) * termCount);
			memcpy(iter.sizes, fit.sizes, sizeof(size_t) * termCount);
			memcpy(iter.ptrs, fit.ptrs, sizeof(void*) * termCount);
		}
		FiniIterator(fit);
		return iter;

	noresults:
		Iterator ret = {};
		ret.flags = IteratorFlagNoResult;
		ret.next = NextQueryIter;
		return ret;
	}

	void NotifyQuery(QueryImpl* query, const QueryEvent& ent)
	{
		switch (ent.type)
		{
		case QueryEventType::MatchTable:
			MatchTable(query, ent.table);
			break;
		case QueryEventType::UnmatchTable:
			UnmatchTable(query, ent.table);
			break;
		}
	}

	void NotifyQueriss(WorldImpl* world, const QueryEvent& ent)
	{
		for (int i = 1; i < world->queryPool.Count(); i++)
		{
			QueryImpl* query = world->queryPool.GetByDense(i);
			if (query == nullptr)
				continue;

			NotifyQuery(query, ent);
		}
	}

	////////////////////////////////////////////////////////////////////////////////
	//// Iterator
	////////////////////////////////////////////////////////////////////////////////

	bool NextQueryIter(Iterator* it)
	{
		ECS_ASSERT(it != nullptr);
		return QueryNextInstanced(it);
	}

	bool TermMatchTable(WorldImpl* world, EntityTable* table, Term& term, EntityID* outID, I32* outColumn)
	{
		I32 column = TableSearchType(table, term.compID);
		if (column == -1)
			return false;

		if (outID)
			*outID = term.compID;
		if (outColumn)
			*outColumn = column;

		return true;
	}

	bool FilterMatchTable(WorldImpl* world, EntityTable* table, Iterator& iter, I32 pivotItem, EntityID* ids, I32* columns)
	{
		// Check current table includes all compsIDs from terms
		for (int i = 0; i < iter.termCount; i++)
		{
			if (i == pivotItem)
				continue;

			if (!TermMatchTable(world, table, iter.terms[i], &ids[i], &columns[i]))
				return false;
		}

		return true;
	}

	bool SetTermIterator(WorldImpl* world, TermIterator* iter, EntityTable* table)
	{
		TableComponentRecord* tr = nullptr;
		ComponentRecord* cr = iter->current;
		if (cr != nullptr)
		{
			tr = GetTableRecordFromCache(&cr->cache, table);
			if (tr != nullptr)
			{
				iter->matchCount = tr->data.count;
				iter->column = tr->data.column;
				iter->id = table->type[tr->data.column];
			}
		}

		if (tr == nullptr)
			return false;

		iter->table = table;
		iter->curMatch = 0;
		return true;
	}

	bool TermIteratorNext(WorldImpl* world, TermIterator* termIter)
	{
		auto GetNextTable = [&](TermIterator* termIter)->TableComponentRecord*
		{
			if (termIter->current == nullptr)
				return nullptr;

			EntityTableCacheItem* term = nullptr;
			term = GetTableCacheListIterNext(termIter->tableCacheIter);
			if (term == nullptr)
			{
				if (termIter->emptyTables)
				{
					termIter->emptyTables = false;
					termIter->tableCacheIter.cur = nullptr;
					termIter->tableCacheIter.next = termIter->current->cache.tables.first;

					term = GetTableCacheListIterNext(termIter->tableCacheIter);
				}
			}

			return (TableComponentRecord*)term;
		};

		TableComponentRecord* tableRecord = nullptr;
		EntityTable* table = termIter->table;
		do
		{
			if (table != nullptr)
			{
				termIter->curMatch++;
				if (termIter->curMatch >= termIter->matchCount)
				{
					table = nullptr;
				}
				else
				{
					// TODO
					ECS_ASSERT(0);
				}
			}

			if (table == nullptr)
			{
				tableRecord = GetNextTable(termIter);
				if (tableRecord == nullptr)
					return false;

				EntityTable* table = tableRecord->table;
				if (table == nullptr)
					return false;

				if (table->flags & TableFlagIsPrefab)
					continue;

				if (table->flags & TableFlagDisabled)
					continue;

				termIter->table = table;
				termIter->curMatch = 0;
				termIter->matchCount = tableRecord->data.count;
				termIter->column = tableRecord->data.column;
				termIter->id = table->type[termIter->column];
				break;
			}
		} while (true);

		return true;
	}

	bool FilterNextInstanced(Iterator* it)
	{
		ECS_ASSERT(it != nullptr);
		ECS_ASSERT(it->world != nullptr);
		ECS_ASSERT(it->next == NextFilterIter);
		ECS_ASSERT(it->chainIter != it);

		WorldImpl* world = static_cast<WorldImpl*>(it->world);
		FilterIterator& iter = it->priv.iter.filter;
		Filter& filter = iter.filter;
		Iterator* chain = it->chainIter;
		EntityTable* table = nullptr;

		if (filter.termCount <= 0)
			goto done;

		ValidateInteratorCache(*it);

		if (filter.terms == nullptr)
			filter.terms = filter.termSmallCache;

		{
			TermIterator& termIter = iter.termIter;
			Term& term = termIter.term;
			I32 pivotTerm = iter.pivotTerm;
			bool match = false;
			do
			{
				EntityTable* targetTable = nullptr;
				// Process target from variables
				if (it->variableCount > 0)
				{
					if (IsIteratorVarConstrained(*it, 0))
					{
						targetTable = it->variables->range.table;
						ECS_ASSERT(targetTable != nullptr);
					}
				}

				// We need to match a new table for current id when matching count equal to zero
				bool first = iter.matchesLeft == 0;
				if (first)
				{
					if (targetTable != nullptr)
					{
						// If it->table equal target table, match failed
						if (targetTable == it->table)
							goto done;

						if (!SetTermIterator(it->world, &termIter, targetTable))
							goto done;

						ECS_ASSERT(termIter.table == targetTable);
					}
					else
					{
						if (!TermIteratorNext(it->world, &termIter))
							goto done;
					}

					ECS_ASSERT(termIter.matchCount != 0);

					iter.matchesLeft = termIter.matchCount;
					table = termIter.table;

					if (pivotTerm != -1)
					{
						I32 index = term.index;
						it->ids[index] = termIter.id;
						it->columns[index] = termIter.column;
					}

					match = FilterMatchTable(world, table, *it, pivotTerm, it->ids, it->columns);
					if (match == false)
					{
						it->table = table;
						iter.matchesLeft = 0;
						continue;
					}
				}

				// If this is not the first result for the table, and the table
				// is matched more than once, iterate remaining matches
				if (!first && iter.matchesLeft > 0)
				{
					// TODO...
					ECS_ASSERT(false);
				}

				match = iter.matchesLeft != 0;
				iter.matchesLeft--;
			} while (!match);

			goto yield;
		}

	done:
		FiniIterator(*it);
		return false;

	yield:
		IteratorPopulateData(world, *it, table, 0, it->sizes, it->ptrs);
		return true;
	}

	struct QueryIterCursor
	{
		int32_t first;
		int32_t count;
	};
	bool QueryNextInstanced(Iterator* it)
	{
		ECS_ASSERT(it != nullptr);
		ECS_ASSERT(it->next == NextQueryIter);

		WorldImpl* world = static_cast<WorldImpl*>(it->world);

		if (ECS_BIT_IS_SET(it->flags, IteratorFlagNoResult))
		{
			FiniIterator(*it);
			return false;
		}

		ECS_BIT_SET(it->flags, IteratorFlagIsValid);

		QueryIterator* iter = &it->priv.iter.query;
		QueryImpl* query = iter->query;

		// Validate interator
		ValidateInteratorCache(*it);

		QueryIterCursor cursor;
		QueryTableMatch* node, * next;
		for (node = iter->node; node != nullptr; node = next)
		{
			EntityTable* table = node->table;
			next = (QueryTableMatch*)node->next;

			if (table != nullptr)
			{
				cursor.first = 0;
				cursor.count = GetTableCount(table);
				ECS_ASSERT(cursor.count != 0);

				I32 termCount = query->filter.termCount;
				for (int i = 0; i < termCount; i++)
				{
					Term& term = query->filter.terms[i];
					I32 index = term.index;
					it->ids[index] = node->ids[index];
					it->columns[index] = node->columns[index];
					it->sizes[index] = node->sizes[index];
				}
			}
			else
			{
				cursor.first = 0;
				cursor.count = 0;
			}

			IteratorPopulateData(world, *it, table, cursor.first, nullptr, it->ptrs);

			iter->node = next;
			iter->prev = node;
			goto yield;
		}

		FiniIterator(*it);
		return false;

	yield:
		return true;
	}

}