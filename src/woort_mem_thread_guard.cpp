/*
woort_mem_thread_guard.cpp

Provides automatic ThreadContext release at thread exit via C++ thread_local.
All release logic is implemented in C (woort_mem_thread_context_on_exit);
this file only leverages C++ thread_local destructor semantics.
*/

#include <cstddef>

extern "C" void woort_mem_thread_context_on_exit(void);

namespace
{
struct WoortMemThreadGuard
{
    ~WoortMemThreadGuard()
    {
        woort_mem_thread_context_on_exit();
    }
};

thread_local WoortMemThreadGuard g_woort_mem_thread_guard;
}

extern "C" void woort_mem_thread_guard_touch(void)
{
    /*
     * Taking the address of the thread_local guard forces its initialization
     * on the current thread, ensuring its destructor runs at thread exit.
     */
    volatile std::byte* p =
        reinterpret_cast<volatile std::byte*>(&g_woort_mem_thread_guard);
    (void)p;
}
