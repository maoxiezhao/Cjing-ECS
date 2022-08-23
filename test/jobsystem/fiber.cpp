#ifdef TEST_ECS

#include "fiber.h"

#include <Windows.h>

namespace VulkanTest
{
namespace Fiber
{
    
Handle Create(ThisThread)
{
    return ::ConvertThreadToFiber(nullptr);
}

Handle Create(int stackSize, JobFunc proc, void* parameter)
{
    return ::CreateFiber(stackSize, proc, parameter);
}

void Destroy(Handle fiber)
{
    ::DeleteFiber(fiber);
}

void SwitchTo(Handle from, Handle to)
{
    ASSERT(from != Fiber::INVALID_HANDLE);
    ASSERT(to != Fiber::INVALID_HANDLE);
    ::SwitchToFiber(to);
}

bool IsValid(Handle fiber)
{
    return fiber != Fiber::INVALID_HANDLE;
}
}
}

#endif