// 4-byte handles for the pointers the game keeps inside mission-script commands
// (GTA2_SCR_PTR, native patch "script commands keep 4-byte pointer slots").
//
// Handle 0 is the null pointer. Each distinct pointer gets one handle, kept for the
// whole run: the pointers are slots of the game's fixed object pools, so the table stays
// small (it grows by the number of different objects scripts ever referred to).
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <unordered_map>
#include <vector>

// Present only in sanitizer builds (weak: null otherwise).
extern "C" void __sanitizer_print_stack_trace() __attribute__((weak));

namespace
{
std::vector<void*>& Pointers()
{
    static std::vector<void*> table{nullptr}; // handle 0 = nullptr
    return table;
}

std::unordered_map<void*, uint32_t>& Handles()
{
    static std::unordered_map<void*, uint32_t> map;
    return map;
}
} // namespace

extern "C" uint32_t Gta2Native_ScrHandleFromPtr(void* p)
{
    if (!p)
    {
        return 0;
    }
    auto& handles = Handles();
    const auto it = handles.find(p);
    if (it != handles.end())
    {
        return it->second;
    }
    auto& table = Pointers();
    const uint32_t handle = static_cast<uint32_t>(table.size());
    table.push_back(p);
    handles.emplace(p, handle);
    return handle;
}

extern "C" void* Gta2Native_ScrPtrFromHandle(uint32_t handle)
{
    auto& table = Pointers();
    if (handle >= table.size())
    {
        // A slot the game never wrote through GTA2_SCR_PTR (raw script data, or a value
        // written through another member of the same union). Stop loudly rather than
        // follow a garbage pointer.
        fprintf(stderr, "gta2: script pointer slot holds unknown handle %u (0x%x, %zu known)\n", handle, handle, table.size());
        if (__sanitizer_print_stack_trace)
        {
            __sanitizer_print_stack_trace();
        }
        abort();
    }
    return table[handle];
}
