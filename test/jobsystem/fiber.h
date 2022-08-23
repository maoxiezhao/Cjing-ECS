#ifdef TEST_ECS

#pragma once

#include "common.h"

// Fiber is only available in win32
namespace VulkanTest
{
namespace Fiber
{
    using Handle = void*;
    constexpr Handle INVALID_HANDLE = nullptr;
    using JobFunc = void(__stdcall *)(void*);

    enum ThisThread
    {
        THIS_THREAD
    };
    
    Handle Create(ThisThread);
    Handle Create(int stackSize, JobFunc proc, void* parameter);
    void Destroy(Handle fiber);
    void SwitchTo(Handle from, Handle to);
    bool IsValid(Handle fiber);
}
}

#endif