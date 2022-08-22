#pragma once

#include "ecs_def.h"

namespace ECS
{
	bool FinalizeTerm(Term& term);
	bool InitFilter(const FilterCreateDesc& desc, Filter& outFilter);
	void FiniFilter(Filter& filter);
	Iterator GetFilterIterator(WorldImpl* world, Filter& filter);
	bool FilterIteratorNext(Iterator* it);
	QueryImpl* CreateQuery(WorldImpl* world, const QueryCreateDesc& desc);
	Iterator GetQueryIterator(WorldImpl* stage, QueryImpl* query);
	void NotifyQueriss(WorldImpl* world, const QueryEvent& ent);
	void FiniQuery(QueryImpl* query);
	void FiniQueries(WorldImpl* world);
	bool NextQueryIter(Iterator* it);
}