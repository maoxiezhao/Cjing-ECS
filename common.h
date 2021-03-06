#pragma once

#include <iostream>
#include <vector>
#include <algorithm>
#include <type_traits>
#include <assert.h>
#include <map>
#include <unordered_map>
#include <array>
#include <type_traits>

#ifndef ECS_STATIC
#if ECS_EXPORTS && (defined(_MSC_VER) || defined(__MINGW32__))
#define ECS_API __declspec(dllexport)
#elif ECS_EXPORTS
#define ECS_API __attribute__((__visibility__("default")))
#elif defined _MSC_VER
#define ECS_API __declspec(dllimport)
#else
#define ECS_API
#endif
#else
#define ECS_API
#endif

namespace ECS
{
	using U8 = uint8_t;
	using U16 = uint16_t;
	using U32 = uint32_t;
	using U64 = uint64_t;
	using I16 = int16_t;
	using I32 = int32_t;
	using I64 = int64_t;

	// Container
	template<typename Value>
	using Map = std::map<U64, Value>;

	template<typename Value>
	using Hashmap = std::unordered_map<U64, Value>;

	template<typename Value>
	using Vector = std::vector<Value>;

	template<typename T, size_t N>
	using Array = std::array<T, N>;

	// Typetraits
	template <bool _Test, class _Ty = void>
	using enable_if_t = std::enable_if_t<_Test, _Ty>;

	template<typename T>
	using decay_t = std::decay_t<T>;
}

#define ECS_MALLOC(n) malloc(n)
#define ECS_MALLOC_T(T) (T*)malloc(sizeof(T))
#define ECS_MALLOC_T_N(T, n) (T*)malloc(sizeof(T) * n)
#define ECS_CALLOC(n) calloc(1, n)
#define ECS_CALLOC_T(T) (T*)calloc(1, sizeof(T))
#define ECS_CALLOC_T_N(T, n) (T*)calloc(n, sizeof(T))
#define ECS_NEW_PLACEMENT(mem, T) new (mem) T()
#define ECS_FREE(ptr) free(ptr)

#define ECS_MOV(...) static_cast<std::remove_reference_t<decltype(__VA_ARGS__)>&&>(__VA_ARGS__)
#define ECS_FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define ECS_ASSERT(...) assert(__VA_ARGS__)
#define ECS_HAS_FLAG(flags, flag) (flags & (U32)flag)

inline void ECS_ERROR(const char* err)
{
	std::cout << "[ECS_ERROR]" << err << std::endl;
}

template<typename T, typename... Args>
inline T* ECS_NEW_OBJECT(Args&&... args)
{
	return new(malloc(sizeof(T))) T(std::forward<Args>(args)...);
}

template<typename T>
inline void ECS_DELETE_OBJECT(T* ptr)
{
	if (ptr != nullptr)
	{
		if (!__has_trivial_destructor(T)) {
			ptr->~T();
		}
		free(ptr);
	}
}

template<typename T>
using ECS_UNIQUE_PTR = std::unique_ptr<T>;

template<typename T>
ECS_UNIQUE_PTR<T> ECS_MAKE_UNIQUE()
{
	return std::make_unique<T>();
}