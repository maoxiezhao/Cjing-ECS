#pragma once

#include "ecs_api.h"

namespace ECS
{
namespace Util
{
    namespace _ {
        char* GetTypeName(char* typeName, const char* funcName, size_t len);
    }

    template <size_t N>
    static constexpr size_t GetStringLength(char const (&)[N]) {
        return N - 1;
    }

#if defined(_WIN32)
#define ECS_FUNC_NAME_FRONT(type, name) ((sizeof(#type) + sizeof(" __cdecl ECS::Util::<") + sizeof(#name)) - 3u)
#define ECS_FUNC_NAME_BACK (sizeof(">(void)") - 1u)
#define ECS_FUNC_NAME __FUNCSIG__
#else
#error "implicit component registration not supported"
#endif

#define ECS_FUNC_TYPE_LEN(type, name, str)\
    (GetStringLength(str) - (ECS_FUNC_NAME_FRONT(type, name) + ECS_FUNC_NAME_BACK))

#if defined(__GNUC__) || defined(_WIN32)
    template <typename T>
    inline static const char* Typename() 
    {
        static const size_t len = ECS_FUNC_TYPE_LEN(const char*, Typename, ECS_FUNC_NAME);
        static char result[len + 1] = {};
        return _::GetTypeName(result, ECS_FUNC_NAME, len);
    }
#else
#error "Not supported"
#endif

	I64 AtomicDecrement(volatile I64* pw);
	I64 AtomicIncrement(volatile I64* pw);

	template <bool V>
	using if_t = std::enable_if_t<V, int>;

	template <bool V>
	using if_not_t = std::enable_if_t<false == V, int>;

	namespace _ {
		struct PairBase {};
	}

	template<typename T, typename U>
	struct Pair : _::PairBase
	{
		using First = T;
		using Second = U;
		using RealType = std::conditional_t<!std::is_empty_v<T>&& std::is_empty_v<U>, T, U>;
	};

	template<typename T>
	struct IsPair
	{
		static constexpr bool value = std::is_base_of<_::PairBase, std::remove_cv_t<T>>::value;
	};

	template<typename T, typename U = int>
	struct RealType;

	template<typename T>
	struct RealType<T, std::enable_if_t<IsPair<T>::value, int>>
	{
		using type = typename T::RealType;
	};

	template<typename T>
	struct RealType<T, std::enable_if_t<!IsPair<T>::value, int>>
	{
		using type = T;
	};

	template <typename T>
	using RealType_t = typename RealType<T>::type;

	template <typename T>
	struct is_actual {
		static constexpr bool value =
			std::is_same<T, RealType_t<T> >::value && !std::is_enum<T>::value;
	};


	inline size_t NextPowOf2(size_t n)
	{
		n--;
		n |= n >> 1;
		n |= n >> 2;
		n |= n >> 4;
		n |= n >> 8;
		n |= n >> 16;
		n++;
		return n;
	}

    U64 HashFunc(const void* data, size_t length);

    template<typename T>
    T* GetTypeFromVectorData(std::vector<U8>& vec, size_t index)
    {
        return reinterpret_cast<T*>(vec.data() + sizeof(T) * index);
    }

#define PTR_OFFSET(o, offset) (void*)(((uintptr_t)(o)) + ((uintptr_t)(offset)))

	template<typename T>
	struct ListNode
	{
		ListNode<T>* prev = nullptr;
		ListNode<T>* next = nullptr;

		T* Cast()
		{
			return static_cast<T*>(this);
		}
	};

	template<typename T>
	struct List
	{
		using Node = typename ListNode<T>;
		Node* first = nullptr;
		Node* last = nullptr;
		I32 count = 0;
	};

	struct Stackpage 
	{
		void* data;
		struct Stackpage* next;
		size_t sp;
	};

#define ECS_STACK_PAGE_SIZE (4096)
#define ECS_ALIGN(size, alignment) (size_t)((((((size_t)size) - 1) / ((size_t)alignment)) + 1) * ((size_t)alignment))
#define ECS_OFFSET(o, offset) reinterpret_cast<void*>((reinterpret_cast<uintptr_t>(o)) + (static_cast<uintptr_t>(offset)))

	struct Stack 
	{
		Stackpage first;
		Stackpage* cur = nullptr;

		void Init()
		{
			memset(&first, 0, sizeof(Stackpage));
			cur = &first;
		}

		void Uninit()
		{
			Stackpage* next, *cur = &first;
			do {
				next = cur->next;
				if (cur == &first)
					ECS_FREE(cur->data);
				else
					ECS_FREE(cur);
			} 
			while ((cur = next));
		}

		void Reset()
		{
			cur = &first;
			first.sp = 0;
		}

		void* Alloc(size_t size, size_t align)
		{
			Stackpage* page = cur;
			if (page == &first && page->data == nullptr)
				page->data = ECS_MALLOC(ECS_STACK_PAGE_SIZE);

			size_t sp = ECS_ALIGN(page->sp, align);
			size_t newSp = sp + size;

			if (newSp > ECS_STACK_PAGE_SIZE)
			{
				if (size > ECS_STACK_PAGE_SIZE)
					ECS_ASSERT(false);

				if (page->next)
					page = page->next;
				else
					page = page->next = NewPage();

				sp = 0;
				newSp = size;
				cur = page;
			}

			page->sp = newSp;
			return ECS_OFFSET(page->data, sp);
		}

	private:

#define STACK_PAGE_OFFSET ECS_ALIGN(sizeof(Stackpage), 16)

		Stackpage* NewPage() 
		{
			Stackpage* newPage = (Stackpage*)ECS_MALLOC(STACK_PAGE_OFFSET + ECS_STACK_PAGE_SIZE);
			newPage->data = ECS_OFFSET(newPage, STACK_PAGE_OFFSET);
			newPage->next = nullptr;
			return newPage;
		}
	};

    class StorageVector
    {
    private:
		size_t count = 0;
		size_t capacity = 0;
		size_t elemSize_ = 0;
        void* data = nullptr;

        static const size_t INITIAL_ELEM_COUNT = 2;
    
		void ReserveData(size_t elemSize, size_t offset, size_t elemCount)
		{
			assert(elemSize != 0);
			if (data == nullptr)
			{
				data = malloc(offset + elemSize * elemCount);
			}
			else
			{
				void* newPtr = realloc(data, offset + elemSize * elemCount);
				assert(newPtr);
				data = newPtr;
			}
			assert(data != NULL);
			capacity = elemCount;
			elemSize_ = elemSize;
		}
	
	public:
        StorageVector() = default;

		~StorageVector()
		{
			if (data != nullptr)
			{
				free(data);
				data = nullptr;
			}
		}

        void Clear()
        {
            count = 0;
            capacity = 0;
			if (data != nullptr)
			{
				free(data);
				data = nullptr;
			}
		}

        void* PushBack(size_t elemSize, size_t offset)
        {
            if (data == nullptr)
            {
				ReserveData(elemSize, offset, INITIAL_ELEM_COUNT);
                count = 1;
				elemSize_ = elemSize;
                return PTR_OFFSET(data, offset);
            }

			assert(elemSize_ == elemSize);

            if (count >= capacity)
            {
                size_t newCapacity = capacity * 2;
                if (!newCapacity)
                    newCapacity = 2;
                
				ReserveData(elemSize, offset, newCapacity);
            }

            count++;
            return PTR_OFFSET(data, offset + (count - 1) * elemSize);
        }


        void* PushBackN(size_t elemSize, size_t offset, size_t num)
        {
            if (num == 1)
                return PushBack(elemSize, offset);

			assert(elemSize_ == elemSize);

            size_t maxCount = capacity;
            size_t oldCount = count;
            size_t newCount = oldCount + num;

            if ((newCount - 1) >= maxCount)
            {
                if (maxCount == 0) 
                {
                    maxCount = num;
                }
                else 
                {
                    while (maxCount < newCount)
                        maxCount *= 2;
                }

				ReserveData(elemSize, offset, maxCount);
            }

            count = newCount;
            return PTR_OFFSET(data, offset + oldCount * elemSize);
        }

		void* Get(size_t elemSize, size_t offset, size_t index)
		{
			assert(index >= 0 && index < count);
			assert(elemSize_ == elemSize);
			return PTR_OFFSET(data, offset + index * elemSize);
		}

		bool Popback(size_t elemSize, size_t offset, void* ptr)
		{
			assert(elemSize_ == elemSize);
			if (count <= 0)
				return false;

			if (ptr)
			{
				void* elem = PTR_OFFSET(data, offset + (count - 1) * elemSize);
				memcpy(ptr, elem, elemSize);
			}

			RemoveLast();
			return true;
		}

		void Remove(size_t elemSize, size_t offset, size_t index)
		{
			assert(index >= 0 && index < count);
			assert(elemSize_ == elemSize);
			count--;
			if (index != count) 
			{
				void* lastElem = PTR_OFFSET(data, elemSize * count);
				memcpy(data, lastElem, elemSize);
			}
		}

		size_t Reserve(size_t elemSize, size_t offset, size_t elemCount)
		{
			if (!data) 
			{
				ReserveData(elemSize, offset, elemCount);
				return elemCount;
			}
			else 
			{
				assert(elemSize_ == elemSize);
				size_t result = capacity;
				if (elemCount < count)
					elemCount = count;
				
				if (result < elemCount) 
				{
					elemCount = NextPowOf2(elemCount);
					ReserveData(elemSize, offset, elemCount);
					result = elemCount;
				}
				return result;
			}
		}

		void* Data() {
			return data;
		}

		size_t GetCount()const
		{
			return count;
		}

		size_t GetCapacity()const
		{
			return capacity;
		}

		void RemoveLast()
		{
			if (count > 0)
				count--;
		}

		bool Empty()
		{
			return count == 0;
		}

        template<typename T>
        T* PushBack()
        {
            return reinterpret_cast<T*>(PushBack(sizeof(T), alignof(T)));
        }

		template<typename T>
		T* Get(size_t index)
		{
			return reinterpret_cast<T*>(Get(sizeof(T), alignof(T), index));
		}

		template<typename T>
		void Remove(size_t index)
		{
			Remove(sizeof(T), alignof(T), index);
		}
    
		template<typename T>
		bool Popback(T* value)
		{
			Popback(sizeof(T), alignof(T), value);
		}

		template<typename T>
		size_t Reserve(size_t elemCount)
		{
			return Reserve(sizeof(T), alignof(T), elemCount);
		}
	};

	template<typename T>
	class SparseArray
	{
	private:
		static const I32 DEFAULT_BLOCK_COUNT = 4096;
		static const U64 ENTITY_MASK = 0xFFFFffffull;
		static const U64 GENERATION_MASK = 0xFFffull << 32; // [32 - 48]

	public:
		struct Chunk
		{
			size_t* sparse = nullptr; // -> SparseArray::denseArray
			T* data = nullptr;
			U8* flag = nullptr;
		};

		SparseArray()
		{
			localMaxID = UINT64_MAX;
			maxID = &localMaxID;
			denseArray.push_back(0);
			count = 1;
		}

		~SparseArray()
		{
			Clear();
		}

		void Clear()
		{
			// Free all chunks
			for (auto& chunk : chunks)
			{
				for (int i = 0; i < DEFAULT_BLOCK_COUNT; i++)
				{
					if (chunk.flag[i])
					{
						if (!__has_trivial_destructor(T)) {
							(chunk.data[i]).~T();
						}
					}
				}

				if (chunk.sparse != nullptr) free(chunk.sparse);
				if (chunk.data != nullptr) free(chunk.data);
				if (chunk.flag != nullptr) free(chunk.flag);
			}
			chunks.clear();

			// Clear denseArray
			denseArray.resize(1);
			denseArray[0] = 0;
			count = 1;
			localMaxID = 0;
		}

		size_t Count()const
		{
			return count - 1;
		}

		U64 NewIndex()
		{
			size_t denseCount = denseArray.size();
			size_t index = count++;
			assert(index <= denseCount);
			if (index < denseCount)
				return denseArray[index];
			else
				return CreateKey(index);
		}

		T* Requset()
		{
			U64 index = NewIndex();
			Chunk* chunk = GetChunk(GetChunkIndexFromIndex(index));
			assert(chunk != nullptr);
			return GetChunkOffset(chunk, GetOffsetFromIndex(index));
		}

		T* GetByDense(size_t dense)
		{
			assert(dense < count);
			dense++;
			return Get(dense, denseArray[dense]);
		}

		U64 GetAliveIndex(U64 index)
		{
			Chunk* chunk = GetChunk(GetChunkIndexFromIndex(index));
			if (chunk == nullptr)
				return 0;

			size_t offset = GetOffsetFromIndex(index);
			size_t dense = chunk->sparse[offset];
			return denseArray[dense];
		}

		T* Get(size_t dense, U64 index)
		{
			Chunk* chunk = GetChunk(GetChunkIndexFromIndex(index));
			if (chunk == nullptr)
				return nullptr;

			size_t offset = GetOffsetFromIndex(index);
			assert(dense == chunk->sparse[offset]);
			return GetChunkOffset(chunk, offset);
		}

		T* Get(U64 index)
		{
			Chunk* chunk = GetChunk(GetChunkIndexFromIndex(index));
			if (chunk == nullptr)
				return nullptr;

			size_t offset = GetOffsetFromIndex(index);
			size_t dense = chunk->sparse[offset];
			if (!(dense && (dense < count)))
				return nullptr;

			U64 gen = index & GENERATION_MASK;
			U64 curGen = denseArray[dense] & GENERATION_MASK;
			if (curGen != gen)
				return nullptr;

			return GetChunkOffset(chunk, offset);
		}

		const T* Get(U64 index)const
		{
			const Chunk* chunk = GetChunk(GetChunkIndexFromIndex(index));
			if (chunk == nullptr)
				return nullptr;

			size_t offset = GetOffsetFromIndex(index);
			size_t dense = chunk->sparse[offset];
			if (!(dense && (dense < count)))
				return nullptr;

			U64 gen = index & GENERATION_MASK;
			U64 curGen = denseArray[dense] & GENERATION_MASK;
			if (curGen != gen)
				return nullptr;

			return GetChunkOffset(chunk, offset);
		}

		T* Ensure(U64 index)
		{
			U64 gen = StripGeneration(&index);
			Chunk* chunk = GetOrCreateChunk(GetChunkIndexFromIndex(index));
			size_t offset = GetOffsetFromIndex(index);
			size_t dense = chunk->sparse[offset];
			if (dense > 0)
			{
				if (dense == count) 
				{
					count++;
				}
				else if (dense > count)
				{
					// Dense is not alive
					SwapDense(chunk, dense, count);
					count++;
				}
				assert(denseArray[dense] == (index | gen));
			}
			else
			{
				denseArray.emplace_back(0);

				size_t denseCount = denseArray.size() - 1;	// skip new adding dense
				size_t newCount = count++;
				if (index >= *maxID)
					*maxID = index;

				if (newCount < denseCount)
				{
					// If there are unused elements in the list, move the first unused element to the end of the list
					U64 unused = denseArray[newCount];
					Chunk* unusedChunk = GetOrCreateChunk(GetChunkIndexFromIndex(unused));
					AssignIndex(unusedChunk, unused, denseCount);
				}
				AssignIndex(chunk, index, newCount);
				denseArray[newCount] |= gen;
			}
			return GetChunkOffset(chunk, offset);
		}

		void Remove(U64 index)
		{
			size_t offset = GetOffsetFromIndex(index);
			void* ptr = RemoveAndGet(index);
			if (ptr != nullptr)
			{
				if (!__has_trivial_destructor(T))
				{
					static_cast<T*>(ptr)->~T();
				}
				else
				{
					memset(ptr, 0, sizeof(T));
				}
			}
		}

		bool CheckExsist(U64 index)const
		{
			return Get(index) != nullptr;
		}

		U64 GetLastID()
		{
			return denseArray[count - 1];
		}

		void SetSourceID(U64* source)
		{
			maxID = source;
		}

	private:
		T* GetChunkOffset(Chunk* chunk, size_t offset)
		{
			assert(chunk != nullptr);
			assert(offset >= 0);
			T* mem = chunk->data + offset;
			if (!__has_trivial_constructor(T))
			{
				if (chunk->flag[offset] == 0)
				{
					chunk->flag[offset] = 1;
					new (mem) T();
				}
			}
			return mem;
		}

		const T* GetChunkOffset(const Chunk* chunk, size_t offset)const
		{
			assert(chunk != nullptr);
			assert(offset >= 0);
			return chunk->data + offset;
		}

		U64 StripGeneration(uint64_t* indexOut)
		{
			U64 index = *indexOut;
			U64 gen = index & GENERATION_MASK;
			*indexOut -= gen;
			return gen;
		}

		U64 CreateKey(size_t dense)
		{
			U64 index = IncID();
			GrowDense();
			Chunk* chunk = GetOrCreateChunk(GetChunkIndexFromIndex(index));
			assert(chunk->sparse[GetOffsetFromIndex(index)] == 0);
			AssignIndex(chunk, index, dense);
			return index;
		}

		void AssignIndex(Chunk* chunk, U64 index, size_t dense)
		{
			chunk->sparse[GetOffsetFromIndex(index)] = dense;
			denseArray[dense] = index;
		}

		void GrowDense()
		{
			denseArray.push_back(0);
		}

		void SwapDense(Chunk* chunkA, size_t denseA, size_t denseB)
		{
			assert(denseA != denseB);
			assert(denseA < denseArray.size() && denseB < denseArray.size());
			U64 indexA = denseArray[denseA];
			U64 indexB = denseArray[denseB];
			Chunk* chunkB = GetOrCreateChunk(GetChunkIndexFromIndex(indexB));
			AssignIndex(chunkA, indexA, denseB);
			AssignIndex(chunkB, indexB, denseA);
		}

		void* RemoveAndGet(U64 index)
		{
			U64 gen = StripGeneration(&index);
			Chunk* chunk = GetOrCreateChunk(GetChunkIndexFromIndex(index));
			size_t offset = GetOffsetFromIndex(index);
			size_t dense = chunk->sparse[offset];
			if (dense == 0)
				return nullptr;

			U64 curGen = denseArray[dense] & GENERATION_MASK;
			if (curGen != gen)
				return nullptr;

			// Inc generation
			denseArray[dense] = index | IncGeneration(curGen);

			// Decrease count
			if (dense == (count - 1))
			{
				// Dense point to last used element
				count--;
			}
			else if (dense < count)
			{
				// Move current elment to unused element
				SwapDense(chunk, dense, count - 1);
				count--;
			}
			// TODO
			chunk->flag[offset] = 0;
			return chunk->data + offset;
		}

		U64 IncID()
		{
			assert(maxID != nullptr);
			return ++maxID[0];
		}

		U64 IncGeneration(U64 gen)
		{
			return ((((gen & GENERATION_MASK) >> 32) + 1) & 0xffff) << 32;
		}

		size_t GetChunkIndexFromIndex(U64 index) const
		{
			return (U32)index >> 12;	// ~0xfff 4096
		}

		size_t GetOffsetFromIndex(U64 index) const
		{
			return (size_t)index & 0xfff;  // 4096
		}

		Chunk* GetOrCreateChunk(size_t chunkIndex)
		{
			Chunk* chunk = GetChunk(chunkIndex);
			if (chunk == nullptr)
				chunk = CreateNewChunk(chunkIndex);
			return chunk;
		}

		Chunk* GetChunk(size_t chunkIndex)
		{
			if (chunkIndex >= chunks.size())
				return nullptr;
			return &chunks[chunkIndex];
		}

		const Chunk* GetChunk(size_t chunkIndex)const
		{
			if (chunkIndex >= chunks.size())
				return nullptr;
			return &chunks[chunkIndex];
		}

		Chunk* CreateNewChunk(size_t chunkIndex)
		{
			size_t oldSize = chunks.size();
			if (chunkIndex >= chunks.size())
			{
				chunks.resize(chunkIndex + 1);
				memset(chunks.data() + oldSize, 0, (chunkIndex - oldSize + 1) * sizeof(Chunk));
			}

			Chunk* chunk = &chunks[chunkIndex];
			assert(chunk->sparse == nullptr);
			assert(chunk->data == nullptr);
			chunk->sparse = (size_t*)malloc(sizeof(size_t) * DEFAULT_BLOCK_COUNT);
			chunk->data = (T*)malloc(sizeof(T) * DEFAULT_BLOCK_COUNT);
			chunk->flag = (U8*)malloc(sizeof(U8) * DEFAULT_BLOCK_COUNT);

			assert(chunk->sparse != nullptr);
			assert(chunk->data != nullptr);
			assert(chunk->flag != nullptr);

			memset(chunk->sparse, 0, sizeof(size_t) * DEFAULT_BLOCK_COUNT);
			memset(chunk->data, 0, sizeof(T) * DEFAULT_BLOCK_COUNT);
			memset(chunk->flag, 0, sizeof(U8) * DEFAULT_BLOCK_COUNT);
			return chunk;
		}

	private:
		std::vector<U64> denseArray; // dense => index
		std::vector<Chunk> chunks;   // index => chunkIndex | offset, chunk.sparse[offset] = dense
		size_t count = 0;
		U64* maxID = nullptr;
		U64 localMaxID = 0;
	};
}
}