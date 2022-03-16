
namespace Reflect
{
	/// ////////////////////////////////////////////////////////////////////
	/// Constructor
	////////////////////////////////////////////////////////////////////////
	template <typename T>
	void DefaultCtor(World* world, EntityID* entities, size_t size, size_t count, void* ptr)
	{
		T* objArr = static_cast<T*>(ptr);
		for (size_t i = 0; i < count; i++)
			new (&objArr[i]) T();
	}

	inline void IllegalCtor(World* world, EntityID* entities, size_t size, size_t count, void* ptr)
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
	void DefaultDtor(World* world, EntityID* entities, size_t size, size_t count, void* ptr)
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
	void DefaultCopy(World* world, EntityID* srcEntities, EntityID* dstEntities, size_t size, size_t count, const void* srcPtr, void* dstPtr)
	{
		const T* srcArr = static_cast<const T*>(srcPtr);
		T* dstArr = static_cast<T*>(dstPtr);
		for (size_t i = 0; i < count; i++)
			dstArr[i] = srcArr[i];
	}

	inline void IllegalCopy(World* world, EntityID* srcEntities, EntityID* dstEntities, size_t size, size_t count, const void* srcPtr, void* dstPtr)
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
	void DefaultCopyCtor(World* world, EntityID* srcEntities, EntityID* dstEntities, size_t size, size_t count, const void* srcPtr, void* dstPtr)
	{
		const T* srcArr = static_cast<const T*>(srcPtr);
		T* dstArr = static_cast<T*>(dstPtr);
		for (size_t i = 0; i < count; i++)
			new (&dstArr[i]) T(srcArr[i]);
	}

	template<typename T, std::enable_if_t<std::is_trivially_copy_constructible_v<T>, int> = 0>
	CompCopyCtorFunc CopyCtor()
	{
		return nullptr;
	}

	template<typename T, std::enable_if_t<!std::is_copy_constructible_v<T>, int> = 0>
	CompCopyCtorFunc CopyCtor()
	{
		return IllegalCopy;
	}

	template<typename T, std::enable_if_t<std::is_copy_constructible_v<T> &&
		!std::is_trivially_copy_constructible_v<T>, int> = 0>
		CompCopyCtorFunc CopyCtor()
	{
		return DefaultCopyCtor<T>;
	}

	/// ////////////////////////////////////////////////////////////////////
	/// Move
	////////////////////////////////////////////////////////////////////////
	template <typename T>
	void DefaultMove(World* world, EntityID* srcEntities, EntityID* dstEntities, size_t size, size_t count, void* srcPtr, void* dstPtr)
	{
		T* srcArr = static_cast<T*>(srcPtr);
		T* dstArr = static_cast<T*>(dstPtr);
		for (size_t i = 0; i < count; i++)
			dstArr[i] = std::move(srcArr[i]);
	}

	inline void IllegalMove(World* world, EntityID* srcEntities, EntityID* dstEntities, size_t size, size_t count, void* srcPtr, void* dstPtr)
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
	template <typename T>
	void DefaultMoveCtor(World* world, EntityID* srcEntities, EntityID* dstEntities, size_t size, size_t count, void* srcPtr, void* dstPtr)
	{
		T* srcArr = static_cast<T*>(srcPtr);
		T* dstArr = static_cast<T*>(dstPtr);
		for (size_t i = 0; i < count; i++)
			new (&dstArr[i]) T(std::move(srcArr[i]));
	}

	template<typename T, std::enable_if_t<std::is_trivially_move_constructible_v<T>, int> = 0>
	CompMoveCtorFunc MoveCtor()
	{
		return nullptr;
	}

	template<typename T, std::enable_if_t<!std::is_move_constructible_v<T>, int> = 0>
	CompMoveCtorFunc MoveCtor()
	{
		return IllegalMove;
	}

	template<typename T, std::enable_if_t<std::is_move_constructible_v<T> &&
		!std::is_trivially_move_constructible_v<T>, int> = 0>
		CompMoveCtorFunc MoveCtor()
	{
		return DefaultMoveCtor<T>;
	}

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
			info.copyCtor = CopyCtor<T>();
			info.moveCtor = MoveCtor<T>();
			world.SetComponentAction(compID, info);
		}
	}

	template<typename T, typename std::enable_if_t<std::is_trivial<T>::value == true>*>
	void Register(World& world, EntityID compID) {}
}