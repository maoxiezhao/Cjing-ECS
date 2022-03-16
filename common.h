#pragma once

#include <iostream>
#include <vector>
#include <algorithm>
#include <type_traits>
#include <assert.h>
#include <unordered_map>
#include <array>

using U8  = uint8_t;
using U16 = uint16_t;
using U32 = uint32_t;
using U64 = uint64_t;
using I16 = int16_t;
using I32 = int32_t;
using I64 = int64_t;

template<typename Value>
using Hashmap = std::unordered_map<U64, Value>;

template<typename Value>
using Vector = std::vector<Value>;

#define ECS_MALLOC(n) malloc(n)
#define ECS_MALLOC_T(T) (T*)malloc(sizeof(T))
#define ECS_MALLOC_T_N(T, n) (T*)malloc(sizeof(T) * n)
#define ECS_NEW_PLACEMENT(mem, T) new (mem) T()
#define ECS_FREE(ptr) free(ptr)

#define ECS_MOV(...) static_cast<std::remove_reference_t<decltype(__VA_ARGS__)>&&>(__VA_ARGS__)
#define ECS_FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

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