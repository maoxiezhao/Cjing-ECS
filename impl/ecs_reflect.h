#pragma once

#include "ecs_def.h"
#include "ecs_world.h"

namespace ECS
{
namespace Reflect
{
	template<typename T, typename enable_if_t<std::is_trivial<T>::value == false>* = nullptr>
	static void Register(WorldImpl& world, EntityID compID);

	template<typename T, typename enable_if_t<std::is_trivial<T>::value == true>* = nullptr>
	static void Register(WorldImpl& world, EntityID compID);

	/// ////////////////////////////////////////////////////////////////////
	/// Constructor
	////////////////////////////////////////////////////////////////////////
	template <typename T>
	void DefaultCtor(WorldImpl* world, EntityID* entities, size_t size, size_t count, void* ptr)
	{
		T* objArr = static_cast<T*>(ptr);
		for (size_t i = 0; i < count; i++)
			new (&objArr[i]) T();
	}

	inline void IllegalCtor(WorldImpl* world, EntityID* entities, size_t size, size_t count, void* ptr)
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
	void DefaultDtor(WorldImpl* world, EntityID* entities, size_t size, size_t count, void* ptr)
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
	void DefaultCopy(WorldImpl* world, EntityID* srcEntities, EntityID* dstEntities, size_t size, size_t count, const void* srcPtr, void* dstPtr)
	{
		const T* srcArr = static_cast<const T*>(srcPtr);
		T* dstArr = static_cast<T*>(dstPtr);
		for (size_t i = 0; i < count; i++)
			dstArr[i] = srcArr[i];
	}

	inline void IllegalCopy(WorldImpl* world, EntityID* srcEntities, EntityID* dstEntities, size_t size, size_t count, const void* srcPtr, void* dstPtr)
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
	void DefaultCopyCtor(WorldImpl* world, EntityID* srcEntities, EntityID* dstEntities, size_t size, size_t count, const void* srcPtr, void* dstPtr)
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
	void DefaultMove(WorldImpl* world, EntityID* srcEntities, EntityID* dstEntities, size_t size, size_t count, void* srcPtr, void* dstPtr)
	{
		T* srcArr = static_cast<T*>(srcPtr);
		T* dstArr = static_cast<T*>(dstPtr);
		for (size_t i = 0; i < count; i++)
			dstArr[i] = ECS_MOV(srcArr[i]);
	}

	inline void IllegalMove(WorldImpl* world, EntityID* srcEntities, EntityID* dstEntities, size_t size, size_t count, void* srcPtr, void* dstPtr)
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
	void DefaultMoveCtor(WorldImpl* world, EntityID* srcEntities, EntityID* dstEntities, size_t size, size_t count, void* srcPtr, void* dstPtr)
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

	template<typename T, typename enable_if_t<std::is_trivial<T>::value == true>*>
	void Register(WorldImpl& world, EntityID compID) {}

	template<typename T, typename enable_if_t<std::is_trivial<T>::value == false>*>
	void Register(WorldImpl& world, EntityID compID)
	{
		if (!HasComponentTypeInfo(&world, compID))
		{
			ComponentTypeHooks info = {};
			info.ctor = Ctor<T>();
			info.dtor = Dtor<T>();
			info.copy = Copy<T>();
			info.move = Move<T>();
			info.copyCtor = CopyCtor<T>();
			info.moveCtor = MoveCtor<T>();
			SetComponentTypeInfo(&world, compID, info);
		}
	}
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
			desc.entity.entity = INVALID_ENTITYID;
			desc.entity.name = n;
			desc.entity.useComponentID = true;
			desc.size = size;
			desc.alignment = alignment;
			return InitNewComponent(&world, desc);
		}

		static bool Registered(WorldImpl& world)
		{
			return componentID != INVALID_ENTITYID && EntityExists(&world, componentID);
		}
	};
	template<typename C>
	size_t ComponentTypeRegister<C>::size = 0;
	template<typename C>
	size_t ComponentTypeRegister<C>::alignment = 0;
	template<typename C>
	EntityID ComponentTypeRegister<C>::componentID = INVALID_ENTITYID;

	template <typename T>
	using is_const_p = std::is_const< std::remove_pointer_t<T> >;

	template <typename T, Util::if_t< is_const_p<T>::value > = 0>
	static constexpr TypeInOutKind TypeToInout() {
		return TypeInOutKind::In;
	}

	template <typename T, Util::if_t< std::is_reference<T>::value > = 0>
	static constexpr TypeInOutKind TypeToInout() {
		return TypeInOutKind::Out;
	}

	template <typename T, Util::if_not_t<is_const_p<T>::value || std::is_reference<T>::value > = 0>
	static constexpr TypeInOutKind TypeToInout() {
		return TypeInOutKind::InOutDefault;
	}
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
}