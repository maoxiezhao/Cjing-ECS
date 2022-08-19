#include "ecs_impl_types.h"
#include "ecs_observer.h"
#include "ecs_world.h"
#include "ecs_query.h"

namespace ECS
{
	////////////////////////////////////////////////////////////////////////////////
	//// Trigger
	////////////////////////////////////////////////////////////////////////////////

	const Map<EventRecord>* GetTriggers(Observable* observable, EntityID event)
	{
		EventRecords* records = observable->events.Get(event);
		if (records != nullptr)
			return &records->eventIds;
		return nullptr;
	}

	void NotifyTriggers(Iterator& it, const Map<Trigger*>* triggers)
	{
		ECS_ASSERT(triggers != nullptr);
		ECS_ASSERT(it.world != nullptr);

		auto IsTriggerValid = [&](const Trigger& trigger, EntityTable* table) {
			if (trigger.eventID && *trigger.eventID == it.world->eventID)
				return false;

			if (table == nullptr)
				return false;

			if (table->flags & TableFlagIsPrefab)
				return false;

			if (table->flags & TableFlagDisabled)
				return false;

			return true;
		};

		for (const auto& kvp : *triggers)
		{
			Trigger* trigger = (Trigger*)(kvp.second);
			if (!IsTriggerValid(*trigger, it.table))
				continue;

			it.terms = &trigger->term;
			it.ctx = trigger->ctx;
			trigger->callback(&it);
		}
	}

	void NotifyTriggersForID(Iterator& it, const Map<EventRecord>* eventMap, EntityID id)
	{
		auto kvp = eventMap->find(id);
		if (kvp == eventMap->end())
			return;

		const EventRecord& record = kvp->second;
		if (record.triggers.size() > 0)
			NotifyTriggers(it, &record.triggers);
	}

	void RegisterTriggerForID(Observable& observable, Trigger* trigger, EntityID id)
	{
		// EventID -> EventRecords -> CompID -> EventRecord -> TriggerID
		ECS_ASSERT(trigger != nullptr);

		for (int i = 0; i < trigger->eventCount; i++)
		{
			EntityID event = trigger->events[i];
			ECS_ASSERT(event != INVALID_ENTITY);

			EventRecords* records = observable.events.Ensure(event);
			EventRecord& record = records->eventIds[id];
			record.triggers[trigger->id] = trigger;
			record.triggerCount++;
		}
	}

	void RegisterTrigger(Observable& observable, Trigger* trigger)
	{
		Term& term = trigger->term;
		RegisterTriggerForID(observable, trigger, term.compID);
	}

	void UnregisterTriggerForID(Observable& observable, Trigger* trigger, EntityID id)
	{
		// EventID -> EventRecords -> CompID -> EventRecord -> TriggerID
		ECS_ASSERT(trigger != nullptr);

		for (int i = 0; i < trigger->eventCount; i++)
		{
			EntityID event = trigger->events[i];
			ECS_ASSERT(event != INVALID_ENTITY);

			EventRecords* records = observable.events.Get(event);
			if (records == nullptr)
				continue;

			auto it = records->eventIds.find(id);
			if (it == records->eventIds.end())
				continue;

			EventRecord& record = it->second;
			if (record.triggers.find(trigger->id) != record.triggers.end())
			{
				record.triggers.erase(trigger->id);
				record.triggerCount--;
			}
		}
	}

	void UnregisterTrigger(Observable& observable, Trigger* trigger)
	{
		Term& term = trigger->term;
		UnregisterTriggerForID(observable, trigger, term.compID);
	}

	EntityID CreateTrigger(WorldImpl* world, const TriggerDesc& desc)
	{
		ECS_ASSERT(world->isFini == false);
		ECS_ASSERT(desc.callback != nullptr);

		Observable* observable = desc.observable;
		if (observable == nullptr)
			observable = &world->observable;

		EntityCreateDesc entityDesc = {};
		EntityID ret = CreateEntityID(world, entityDesc);
		bool newAdded = false;
		TriggerComponent* comp = static_cast<TriggerComponent*>(GetOrCreateMutableByID(world, ret, ECS_ENTITY_ID(TriggerComponent), &newAdded));
		if (newAdded)
		{
			Term term = desc.term;
			if (!FinalizeTerm(term))
				goto error;

			Trigger* trigger = world->triggers.Requset();
			ECS_ASSERT(trigger != nullptr);
			trigger->id = (I32)world->triggers.GetLastID();
			trigger->world = world;

			comp->trigger = trigger;

			trigger->entity = ret;
			trigger->term = term;
			trigger->callback = desc.callback;
			trigger->ctx = desc.ctx;
			memcpy(trigger->events, desc.events, sizeof(EntityID) * desc.eventCount);
			trigger->eventCount = desc.eventCount;
			trigger->eventID = desc.eventID;
			trigger->observable = observable;

			RegisterTrigger(*observable, trigger);
		}
		return ret;
	error:
		if (ret != INVALID_ENTITY)
			DeleteEntity(world, ret);
		return INVALID_ENTITY;
	}

	void FiniTrigger(Trigger* trigger)
	{
		UnregisterTrigger(*trigger->observable, trigger);
		trigger->world->triggers.Remove(trigger->id);
	}

	////////////////////////////////////////////////////////////////////////////////
	//// Observer
	////////////////////////////////////////////////////////////////////////////////

	static void ObserverTriggerCallback(Iterator* it)
	{
		Observer* observer = (Observer*)it->ctx;
		if (observer->callback)
			observer->callback(it);
	}

	EntityID CreateObserver(WorldImpl* world, const ObserverDesc& desc)
	{
		ECS_ASSERT(world->isFini == false);
		ECS_ASSERT(desc.callback != nullptr);

		// ObserverComp => Observer => Trigger for each term
		EntityCreateDesc entityDesc = {};
		EntityID ret = CreateEntityID(world, entityDesc);
		bool newAdded = false;
		ObserverComponent* comp = static_cast<ObserverComponent*>(GetOrCreateMutableByID(world, ret, ECS_ENTITY_ID(ObserverComponent), &newAdded));
		if (newAdded)
		{
			Observer* observer = world->observers.Requset();
			ECS_ASSERT(observer != nullptr);
			observer->id = world->observers.GetLastID();
			observer->world = world;

			comp->observer = observer;

			for (int i = 0; i < ECS_TRIGGER_MAX_EVENT_COUNT; i++)
			{
				if (desc.events[i] == INVALID_ENTITY)
					continue;

				observer->events[observer->eventCount] = desc.events[i];
				observer->eventCount++;
			}

			ECS_ASSERT(observer->eventCount > 0);

			observer->callback = desc.callback;
			observer->ctx = desc.ctx;

			// Init the filter of observer
			if (!InitFilter(desc.filterDesc, observer->filter))
			{
				FiniObserver(observer);
				return INVALID_ENTITY;
			}

			// Create a trigger for each term
			TriggerDesc triggerDesc = {};
			triggerDesc.callback = ObserverTriggerCallback;
			triggerDesc.ctx = observer;
			triggerDesc.eventID = &observer->eventID;
			memcpy(triggerDesc.events, observer->events, sizeof(EntityID) * observer->eventCount);
			triggerDesc.eventCount = observer->eventCount;

			const Filter& filter = observer->filter;
			for (int i = 0; i < filter.termCount; i++)
			{
				triggerDesc.term = filter.terms[i];
				/*if (IsCompIDTag(triggerDesc.term.compID))
					ECS_ASSERT(false);*/

				EntityID trigger = CreateTrigger(world, triggerDesc);
				if (trigger == INVALID_ENTITY)
					goto error;

				observer->triggers.push_back(trigger);
			}
		}
		return ret;

	error:
		if (ret != INVALID_ENTITY)
			DeleteEntity(world, ret);
		return INVALID_ENTITY;
	}

	void FiniObserver(Observer* observer)
	{
		for (auto trigger : observer->triggers)
		{
			if (trigger != INVALID_ENTITY)
				DeleteEntity(observer->world, trigger);
		}
		observer->triggers.clear();

		FiniFilter(observer->filter);

		observer->world->observers.Remove(observer->id);
	}

	////////////////////////////////////////////////////////////////////////////////
	//// Events
	////////////////////////////////////////////////////////////////////////////////

	void NotifyEvents(WorldImpl* world, Observable* observable, Iterator& it, const EntityType& ids, EntityID event)
	{
		ECS_ASSERT(event != INVALID_ENTITY);
		ECS_ASSERT(!ids.empty());

		const Map<EventRecord>* eventMap = GetTriggers(observable, event);
		if (eventMap == nullptr)
			return;

		for (int i = 0; i < ids.size(); i++)
		{
			EntityID id = ids[i];
			NotifyTriggersForID(it, eventMap, id);
		}
	}

	void EmitEvent(WorldImpl* world, const EventDesc& desc)
	{
		ECS_ASSERT(desc.event != INVALID_ENTITY);
		ECS_ASSERT(!desc.ids.empty());
		ECS_ASSERT(desc.table != nullptr);

		Iterator it = {};
		it.world = world;
		it.table = desc.table;
		it.termCount = 1;
		it.count = desc.table->Count();
		it.event = desc.event;

		// Inc unique event id
		world->eventID++;

		Observable* observable = desc.observable;
		ECS_ASSERT(observable != nullptr);
		NotifyEvents(world, observable, it, desc.ids, desc.event);
	}
}