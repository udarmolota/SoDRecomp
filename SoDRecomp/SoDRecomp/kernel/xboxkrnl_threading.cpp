// xboxkrnl: threads, dispatcher objects, waits, critical sections, spinlocks, TLS.
#include "stdafx.h"
#include "function.h"
#include "thread.h"

namespace sod::imp
{
    using namespace sod::kernel;

    constexpr uint32_t kTitleProcessType = 1;
    constexpr uint64_t kTimebaseFrequency = 49875000;

    // ExCreateThread flags
    constexpr uint32_t kCreateSuspended = 0x1;
    constexpr uint32_t kReturnKThreadPtr = 0x80;

    static std::string ObjectName(const X_OBJECT_ATTRIBUTES* attributes)
    {
        if (!attributes || !attributes->objectName)
            return {};
        auto* str = g_memory.Translate<X_ANSI_STRING>(attributes->objectName);
        if (!str->buffer || !str->length)
            return {};
        return std::string(g_memory.Translate<char>(str->buffer), str->length);
    }

    // Common NtCreateX logic: named lookup, creation, guest header, handle.
    template<typename T, typename Factory>
    static uint32_t CreateObject(u32* handleOut, const X_OBJECT_ATTRIBUTES* attributes, ObjectType type,
        uint32_t headerSize, Factory&& factory)
    {
        const std::string name = ObjectName(attributes);
        if (!name.empty())
        {
            if (auto existing = LookupNamedObject(name))
            {
                if (existing->type != type)
                    return X_STATUS_OBJECT_NAME_COLLISION;
                if (handleOut)
                    *handleOut = CreateHandle(existing);
                return X_STATUS_OBJECT_NAME_EXISTS;
            }
        }

        std::shared_ptr<T> object = factory();
        if (!AttachGuestHeader(object, headerSize))
            return X_STATUS_NO_MEMORY;
        if (!name.empty())
            RegisterNamedObject(name, object);
        if (handleOut)
            *handleOut = CreateHandle(object);
        return X_STATUS_SUCCESS;
    }

    static std::shared_ptr<GuestThread> ThreadArg(uint32_t kthread)
    {
        return kthread ? ThreadFromKThread(kthread) : GuestThread::CurrentRef();
    }

    static uint32_t PcrOf(PPCContext& ctx) { return ctx.r13.u32; }

    // ------------------------------------------------------------------ threads

    uint32_t ExCreateThread(u32* handleOut, uint32_t stackSize, u32* threadIdOut, uint32_t xapiStartup,
        uint32_t startAddress, uint32_t startContext, uint32_t flags)
    {
        ThreadParams params;
        params.stackSize = stackSize ? stackSize : 0x40000;
        params.startAddress = startAddress;
        params.startContext = startContext;
        params.xapiStartup = xapiStartup;
        params.creationFlags = flags;

        auto thread = GuestThread::Create(params);
        if (!thread)
            return X_STATUS_NO_MEMORY;

        LOGI("ExCreateThread {:08X}({:08X}) startup {:08X} stack {:#x} flags {:08X} -> thread {}",
            startAddress, startContext, xapiStartup, params.stackSize, flags, thread->threadId);

        if (handleOut)
            *handleOut = (flags & kReturnKThreadPtr) ? thread->kthread : CreateHandle(thread);
        if (threadIdOut)
            *threadIdOut = thread->threadId;
        return X_STATUS_SUCCESS;
    }

    void ExTerminateThread(uint32_t exitCode)
    {
        GuestThread::Exit(exitCode);
    }

    uint32_t NtResumeThread(uint32_t handle, u32* suspendCountOut)
    {
        auto thread = LookupHandle<GuestThread>(handle, ObjectType::Thread);
        if (!thread)
            return X_STATUS_INVALID_HANDLE;
        const int32_t previous = thread->Resume();
        if (suspendCountOut)
            *suspendCountOut = previous;
        return X_STATUS_SUCCESS;
    }

    uint32_t KeResumeThread(uint32_t kthread)
    {
        auto thread = ThreadArg(kthread);
        return thread ? thread->Resume() : 0;
    }

    uint32_t KeSetAffinityThread(uint32_t kthread, uint32_t affinity, u32* previousOut)
    {
        if (!affinity)
            return X_STATUS_INVALID_PARAMETER;
        auto thread = ThreadArg(kthread);
        if (!thread)
            return X_STATUS_INVALID_HANDLE;
        if (previousOut)
            *previousOut = thread->affinity ? thread->affinity : 1;
        thread->affinity = affinity;
        const uint8_t cpu = uint8_t(std::countr_zero(affinity));
        *g_memory.Translate<uint8_t>(thread->kthread + kKThreadCurrentCpu) = cpu;
        *g_memory.Translate<uint8_t>(thread->pcr + kPcrCurrentCpu) = cpu;
        return X_STATUS_SUCCESS;
    }

    static void ApplyHostPriority(GuestThread& thread)
    {
        int priority = THREAD_PRIORITY_NORMAL;
        const int32_t p = thread.basePriority;
        if (p >= 15) priority = THREAD_PRIORITY_TIME_CRITICAL;
        else if (p >= 2) priority = THREAD_PRIORITY_HIGHEST;
        else if (p == 1) priority = THREAD_PRIORITY_ABOVE_NORMAL;
        else if (p == -1) priority = THREAD_PRIORITY_BELOW_NORMAL;
        else if (p <= -15) priority = THREAD_PRIORITY_IDLE;
        else if (p <= -2) priority = THREAD_PRIORITY_LOWEST;

        HANDLE handle = thread.hostHandle;
        bool close = false;
        if (!handle && thread.hostThreadId)
        {
            handle = OpenThread(THREAD_SET_INFORMATION, FALSE, thread.hostThreadId);
            close = true;
        }
        if (handle)
        {
            ::SetThreadPriority(handle, priority);
            if (close)
                ::CloseHandle(handle);
        }
    }

    int32_t KeQueryBasePriorityThread(uint32_t kthread)
    {
        auto thread = ThreadArg(kthread);
        return thread ? thread->basePriority : 0;
    }

    int32_t KeSetBasePriorityThread(uint32_t kthread, int32_t increment)
    {
        auto thread = ThreadArg(kthread);
        if (!thread)
            return 0;
        const int32_t previous = thread->basePriority;
        thread->basePriority = increment;
        ApplyHostPriority(*thread);
        return previous;
    }

    uint32_t KeGetCurrentProcessType()
    {
        return kTitleProcessType;
    }

    uint64_t KeQueryPerformanceFrequency()
    {
        return kTimebaseFrequency;
    }

    uint32_t KeDelayExecutionThread(uint32_t mode, bool alertable, s64* interval)
    {
        int64_t timeout = interval ? int64_t(*interval) : 0;
        return DelayExecution(interval ? &timeout : nullptr, alertable);
    }

    uint32_t NtYieldExecution()
    {
        SwitchToThread();
        return X_STATUS_SUCCESS;
    }

    void KeQuerySystemTime(u64* time)
    {
        if (time)
            *time = SystemTime100ns();
    }

    uint32_t KeTlsAlloc() { return TlsAlloc(); }
    bool KeTlsFree(uint32_t slot) { return TlsFree(slot); }
    uint32_t KeTlsGetValue(uint32_t slot) { return TlsSlotRef(slot); }
    bool KeTlsSetValue(uint32_t slot, uint32_t value)
    {
        TlsSlotRef(slot) = value;
        return true;
    }

    void KeEnterCriticalRegion() {}
    void KeLeaveCriticalRegion() {}
    void KeEnableFpuExceptions(uint32_t) {}
    void KiApcNormalRoutineNop(uint32_t, uint32_t) {}

    // L2 cache locking is a no-op on the host.
    uint32_t KeLockL2(uint32_t) { return 0; }
    void KeUnlockL2(uint32_t) {}

    // ------------------------------------------------------------------ objects

    template<typename T>
    static std::shared_ptr<T> GuestObject(uint32_t address, ObjectType type)
    {
        auto object = ObjectFromGuestHeader(address);
        if (!object || object->type != type)
            return nullptr;
        return std::static_pointer_cast<T>(object);
    }

    int32_t KeSetEvent(uint32_t event, uint32_t increment, bool wait)
    {
        auto e = GuestObject<Event>(event, ObjectType::Event);
        return e ? e->Set() : 0;
    }

    int32_t KeResetEvent(uint32_t event)
    {
        auto e = GuestObject<Event>(event, ObjectType::Event);
        return e ? e->Reset() : 0;
    }

    uint32_t NtCreateEvent(u32* handleOut, X_OBJECT_ATTRIBUTES* attributes, uint32_t eventType, bool initialState)
    {
        return CreateObject<Event>(handleOut, attributes, ObjectType::Event, sizeof(X_KEVENT),
            [&] { return std::make_shared<Event>(eventType == 0, initialState); });
    }

    uint32_t NtSetEvent(uint32_t handle, u32* previousOut)
    {
        auto e = LookupHandle<Event>(handle, ObjectType::Event);
        if (!e)
            return X_STATUS_INVALID_HANDLE;
        const int32_t previous = e->Set();
        if (previousOut)
            *previousOut = previous;
        return X_STATUS_SUCCESS;
    }

    uint32_t NtClearEvent(uint32_t handle)
    {
        auto e = LookupHandle<Event>(handle, ObjectType::Event);
        if (!e)
            return X_STATUS_INVALID_HANDLE;
        e->Reset();
        return X_STATUS_SUCCESS;
    }

    void KeInitializeSemaphore(X_KSEMAPHORE* semaphore, int32_t count, int32_t limit)
    {
        const uint32_t address = g_memory.ToGuest(semaphore);
        ReleaseGuestHeader(address);
        semaphore->header.type = kTypeSemaphore;
        semaphore->header.absolute = 0;
        semaphore->header.size = sizeof(X_KSEMAPHORE) / 4;
        semaphore->header.inserted = 0;
        semaphore->header.signalState = count;
        semaphore->header.waitList.flink = address + 8;
        semaphore->header.waitList.blink = address + 8;
        semaphore->limit = limit;
        ObjectFromGuestHeader(address);
    }

    int32_t KeReleaseSemaphore(uint32_t semaphore, uint32_t increment, int32_t adjustment, bool wait)
    {
        auto s = GuestObject<Semaphore>(semaphore, ObjectType::Semaphore);
        return s ? s->Release(adjustment) : 0;
    }

    uint32_t NtCreateSemaphore(u32* handleOut, X_OBJECT_ATTRIBUTES* attributes, int32_t count, int32_t limit)
    {
        return CreateObject<Semaphore>(handleOut, attributes, ObjectType::Semaphore, sizeof(X_KSEMAPHORE),
            [&] { return std::make_shared<Semaphore>(count, limit); });
    }

    uint32_t NtReleaseSemaphore(uint32_t handle, int32_t count, s32* previousOut)
    {
        auto s = LookupHandle<Semaphore>(handle, ObjectType::Semaphore);
        if (!s)
            return X_STATUS_INVALID_HANDLE;
        const int32_t previous = s->Release(count);
        if (previous < 0)
            return X_STATUS_SEMAPHORE_LIMIT_EXCEEDED;
        if (previousOut)
            *previousOut = previous;
        return X_STATUS_SUCCESS;
    }

    uint32_t NtCreateMutant(u32* handleOut, X_OBJECT_ATTRIBUTES* attributes, bool initialOwner)
    {
        return CreateObject<Mutant>(handleOut, attributes, ObjectType::Mutant, sizeof(X_KMUTANT),
            [&] { return std::make_shared<Mutant>(initialOwner ? GuestThread::Current() : nullptr); });
    }

    uint32_t NtReleaseMutant(uint32_t handle, s32* previousOut)
    {
        auto m = LookupHandle<Mutant>(handle, ObjectType::Mutant);
        if (!m)
            return X_STATUS_INVALID_HANDLE;
        const int32_t previous = m->Release(GuestThread::Current());
        if (previous < 0)
            return X_STATUS_MUTANT_NOT_OWNED;
        if (previousOut)
            *previousOut = previous;
        return X_STATUS_SUCCESS;
    }

    uint32_t NtCreateTimer(u32* handleOut, X_OBJECT_ATTRIBUTES* attributes, uint32_t timerType)
    {
        return CreateObject<Timer>(handleOut, attributes, ObjectType::Timer, sizeof(X_KTIMER),
            [&] { return std::make_shared<Timer>(timerType == 0); });
    }

    uint32_t NtSetTimerEx(uint32_t handle, s64* dueTime, uint32_t routine, uint32_t mode, uint32_t context,
        bool resume, int32_t periodMs, u32* previousState)
    {
        auto t = LookupHandle<Timer>(handle, ObjectType::Timer);
        if (!t || !dueTime)
            return X_STATUS_INVALID_HANDLE;
        const bool was = t->Set(int64_t(*dueTime), uint32_t(std::max(periodMs, 0)), routine, context,
            routine ? GuestThread::Current() : nullptr);
        if (previousState)
            *previousState = was;
        return X_STATUS_SUCCESS;
    }

    uint32_t NtCancelTimer(uint32_t handle, u32* currentState)
    {
        auto t = LookupHandle<Timer>(handle, ObjectType::Timer);
        if (!t)
            return X_STATUS_INVALID_HANDLE;
        t->Cancel();
        if (currentState)
            *currentState = 0;
        return X_STATUS_SUCCESS;
    }

    uint32_t NtClose(uint32_t handle)
    {
        return CloseHandle(handle) ? X_STATUS_SUCCESS : X_STATUS_INVALID_HANDLE;
    }

    uint32_t NtDuplicateObject(uint32_t handle, u32* newHandle, uint32_t options)
    {
        const uint32_t duplicate = DuplicateHandle(handle);
        if (!duplicate)
            return X_STATUS_INVALID_HANDLE;
        if (newHandle)
            *newHandle = duplicate;
        if (options & 1) // DUPLICATE_CLOSE_SOURCE
            CloseHandle(handle);
        return X_STATUS_SUCCESS;
    }

    // The references ObReferenceObjectByHandle took, by guest object.
    static std::mutex s_obRefsMutex;
    static std::unordered_multimap<uint32_t, ObjectRef> s_obRefs;

    // A reference keeps the object alive (its guest header too) after its handle is
    // closed, until ObDereferenceObject.
    uint32_t ObReferenceObjectByHandle(uint32_t handle, uint32_t objectType, u32* objectOut)
    {
        auto object = LookupHandle(handle);
        if (!object)
            return X_STATUS_INVALID_HANDLE;
        if (!object->guestObject && !AttachGuestHeader(object, 0x40))
            return X_STATUS_NO_MEMORY;
        if (objectOut)
            *objectOut = object->guestObject;
        std::lock_guard lock(s_obRefsMutex);
        s_obRefs.emplace(object->guestObject, std::move(object));
        return X_STATUS_SUCCESS;
    }

    // Drops one reference ObReferenceObjectByHandle took (none taken: nothing to do).
    void ObDereferenceObject(uint32_t object)
    {
        ObjectRef released; // freed after the lock is let go
        std::lock_guard lock(s_obRefsMutex);
        if (auto it = s_obRefs.find(object); it != s_obRefs.end())
        {
            released = std::move(it->second);
            s_obRefs.erase(it);
        }
    }

    // ------------------------------------------------------------------ waits

    static void FinishAlertableWait(uint32_t status)
    {
        if (status == X_STATUS_USER_APC)
        {
            if (auto* self = GuestThread::Current())
                self->DeliverApcs();
        }
    }

    uint32_t KeWaitForSingleObject(uint32_t object, uint32_t reason, uint32_t mode, bool alertable, s64* timeout)
    {
        auto o = ObjectFromGuestHeader(object);
        if (!o)
        {
            LOGE("KeWaitForSingleObject: unknown object {:08X}", object);
            return X_STATUS_ABANDONED_WAIT_0;
        }
        int64_t t = timeout ? int64_t(*timeout) : 0;
        const uint32_t status = WaitSingle(o, timeout ? &t : nullptr, alertable);
        FinishAlertableWait(status);
        return status;
    }

    uint32_t NtWaitForSingleObjectEx(uint32_t handle, uint32_t mode, bool alertable, s64* timeout)
    {
        auto o = LookupHandle(handle);
        if (!o)
            return X_STATUS_INVALID_HANDLE;
        int64_t t = timeout ? int64_t(*timeout) : 0;
        const uint32_t status = WaitSingle(o, timeout ? &t : nullptr, alertable);
        FinishAlertableWait(status);
        return status;
    }

    // (SoD) Signals one object (event: set; semaphore: release 1; mutant: release) and
    // waits on another. Not atomic as on the console: the wait starts right after the
    // signal, which waiters can't tell apart here.
    uint32_t NtSignalAndWaitForSingleObjectEx(uint32_t signalHandle, uint32_t waitHandle, bool alertable, s64* timeout)
    {
        auto signal = LookupHandle(signalHandle);
        auto wait = LookupHandle(waitHandle);
        if (!signal || !wait)
            return X_STATUS_INVALID_HANDLE;
        switch (signal->type)
        {
        case ObjectType::Event:
            std::static_pointer_cast<Event>(signal)->Set();
            break;
        case ObjectType::Semaphore:
            if (std::static_pointer_cast<Semaphore>(signal)->Release(1) < 0)
                return X_STATUS_SEMAPHORE_LIMIT_EXCEEDED;
            break;
        case ObjectType::Mutant:
            if (std::static_pointer_cast<Mutant>(signal)->Release(GuestThread::Current()) < 0)
                return X_STATUS_MUTANT_NOT_OWNED;
            break;
        default:
            return X_STATUS_OBJECT_TYPE_MISMATCH;
        }
        int64_t t = timeout ? int64_t(*timeout) : 0;
        const uint32_t status = WaitSingle(wait, timeout ? &t : nullptr, alertable);
        FinishAlertableWait(status);
        return status;
    }

    uint32_t KeWaitForMultipleObjects(uint32_t count, u32* objects, uint32_t waitType, uint32_t reason,
        uint32_t mode, bool alertable, s64* timeout, uint32_t waitBlocks)
    {
        if (!count || count > 64)
            return X_STATUS_INVALID_PARAMETER;
        std::vector<ObjectRef> list;
        for (uint32_t i = 0; i < count; i++)
        {
            auto o = ObjectFromGuestHeader(objects[i]);
            if (!o)
                return X_STATUS_INVALID_PARAMETER;
            list.push_back(std::move(o));
        }
        int64_t t = timeout ? int64_t(*timeout) : 0;
        const uint32_t status = WaitMultiple(list, waitType == 0, timeout ? &t : nullptr, alertable);
        FinishAlertableWait(status);
        return status;
    }

    uint32_t NtWaitForMultipleObjectsEx(uint32_t count, u32* handles, uint32_t waitType, uint32_t mode,
        bool alertable, s64* timeout)
    {
        if (!count || count > 64 || waitType > 1)
            return X_STATUS_INVALID_PARAMETER;
        std::vector<ObjectRef> list;
        for (uint32_t i = 0; i < count; i++)
        {
            auto o = LookupHandle(handles[i]);
            if (!o)
                return X_STATUS_INVALID_PARAMETER;
            list.push_back(std::move(o));
        }
        int64_t t = timeout ? int64_t(*timeout) : 0;
        const uint32_t status = WaitMultiple(list, waitType == 0, timeout ? &t : nullptr, alertable);
        FinishAlertableWait(status);
        return status;
    }

    // ------------------------------------------------------- critical sections

    static std::atomic_ref<uint32_t> AtomicWord(void* p)
    {
        return std::atomic_ref<uint32_t>(*static_cast<uint32_t*>(p));
    }

    // Adds delta to a big-endian 32-bit word atomically and returns the new value.
    static int32_t AtomicAddBE(s32& word, int32_t delta)
    {
        auto a = AtomicWord(&word);
        uint32_t expected = a.load();
        for (;;)
        {
            const int32_t value = int32_t(ByteSwap(expected)) + delta;
            if (a.compare_exchange_weak(expected, ByteSwap(uint32_t(value))))
                return value;
        }
    }

    static uint32_t CurrentKThread(PPCContext& ctx)
    {
        return *g_memory.Translate<u32>(PcrOf(ctx) + kPcrCurrentThread);
    }

    void RtlInitializeCriticalSection(X_RTL_CRITICAL_SECTION* cs)
    {
        const uint32_t address = g_memory.ToGuest(cs);
        ReleaseGuestHeader(address);
        cs->header.type = kTypeSynchronizationEvent;
        cs->header.absolute = 0;
        cs->header.size = 4;
        cs->header.inserted = 0;
        cs->header.signalState = 0;
        cs->header.waitList.flink = address + 8;
        cs->header.waitList.blink = address + 8;
        cs->lockCount = -1;
        cs->recursionCount = 0;
        cs->owningThread = 0;
    }

    uint32_t RtlInitializeCriticalSectionAndSpinCount(X_RTL_CRITICAL_SECTION* cs, uint32_t spinCount)
    {
        RtlInitializeCriticalSection(cs);
        cs->header.absolute = uint8_t(std::min<uint32_t>((spinCount + 255) / 256, 255));
        return X_STATUS_SUCCESS;
    }

    void RtlEnterCriticalSection(PPCContext& ctx, X_RTL_CRITICAL_SECTION* cs)
    {
        const uint32_t self = CurrentKThread(ctx);

        // Brief spin while the lock looks free-ish. (Read first: a failing exchange still
        // takes the cache line from the thread about to release the lock.)
        for (int spin = 0; spin < 1000; spin++)
        {
            auto a = AtomicWord(&cs->lockCount);
            uint32_t expected = ByteSwap(uint32_t(-1));
            if (a.load(std::memory_order_relaxed) == expected && a.compare_exchange_strong(expected, 0))
            {
                cs->owningThread = self;
                cs->recursionCount = 1;
                return;
            }
            if (cs->owningThread == self)
                break;
            _mm_pause();
        }

        if (AtomicAddBE(cs->lockCount, 1) != 0)
        {
            if (cs->owningThread == self)
            {
                cs->recursionCount = cs->recursionCount + 1;
                return;
            }
            auto event = ObjectFromGuestHeader(g_memory.ToGuest(cs));
            WaitSingle(event, nullptr, false);
        }
        cs->owningThread = self;
        cs->recursionCount = 1;
    }

    void RtlLeaveCriticalSection(X_RTL_CRITICAL_SECTION* cs)
    {
        const int32_t recursion = cs->recursionCount - 1;
        cs->recursionCount = recursion;
        if (recursion != 0)
        {
            AtomicAddBE(cs->lockCount, -1);
            return;
        }
        cs->owningThread = 0;
        if (AtomicAddBE(cs->lockCount, -1) >= 0)
        {
            // Waiters: hand over through the synchronisation event.
            auto object = ObjectFromGuestHeader(g_memory.ToGuest(cs));
            if (object && object->type == ObjectType::Event)
                static_cast<Event*>(object.get())->Set();
        }
    }

    bool RtlTryEnterCriticalSection(PPCContext& ctx, X_RTL_CRITICAL_SECTION* cs)
    {
        const uint32_t self = CurrentKThread(ctx);
        auto a = AtomicWord(&cs->lockCount);
        uint32_t expected = ByteSwap(uint32_t(-1));
        if (a.compare_exchange_strong(expected, 0))
        {
            cs->owningThread = self;
            cs->recursionCount = 1;
            return true;
        }
        if (cs->owningThread == self)
        {
            AtomicAddBE(cs->lockCount, 1);
            cs->recursionCount = cs->recursionCount + 1;
            return true;
        }
        return false;
    }

    // --------------------------------------------------------------- spinlocks

    static uint8_t& Irql(PPCContext& ctx)
    {
        return *g_memory.Translate<uint8_t>(PcrOf(ctx) + kPcrIrql);
    }

    static void AcquireSpin(PPCContext& ctx, u32* lock)
    {
        auto a = AtomicWord(lock);
        const uint32_t owner = ByteSwap(PcrOf(ctx));
        for (uint32_t spins = 0;; spins++)
        {
            uint32_t expected = 0;
            if (a.compare_exchange_weak(expected, owner, std::memory_order_acquire))
                return;
            if (spins < 64)
                _mm_pause();
            else
                SwitchToThread();
        }
    }

    static void ReleaseSpin(u32* lock)
    {
        AtomicWord(lock).store(0, std::memory_order_release);
    }

    uint32_t KfAcquireSpinLock(PPCContext& ctx, u32* lock)
    {
        const uint32_t old = Irql(ctx);
        Irql(ctx) = kDispatchLevel;
        AcquireSpin(ctx, lock);
        return old;
    }

    void KfReleaseSpinLock(PPCContext& ctx, u32* lock, uint32_t oldIrql)
    {
        ReleaseSpin(lock);
        Irql(ctx) = uint8_t(oldIrql);
        if (oldIrql < kDispatchLevel)
            RunPendingDpcs();
    }

    void KeAcquireSpinLockAtRaisedIrql(PPCContext& ctx, u32* lock) { AcquireSpin(ctx, lock); }
    void KeReleaseSpinLockFromRaisedIrql(u32* lock) { ReleaseSpin(lock); }

    uint32_t KeRaiseIrqlToDpcLevel(PPCContext& ctx)
    {
        const uint32_t old = Irql(ctx);
        Irql(ctx) = kDispatchLevel;
        return old;
    }

    void KfLowerIrql(PPCContext& ctx, uint32_t irql)
    {
        Irql(ctx) = uint8_t(irql);
        if (irql < kDispatchLevel)
            RunPendingDpcs();
    }

    // ------------------------------------------------------------------ SLists

    // X_SLIST_HEADER: { u32 next; u16 depth; u16 sequence; } updated with a 64-bit CAS,
    // matching the game's inline ldarx/stdcx. push.
    uint32_t InterlockedPopEntrySList(uint64_t* header)
    {
        std::atomic_ref<uint64_t> a(*header);
        uint64_t expected = a.load();
        for (;;)
        {
            const uint64_t value = ByteSwap(expected);
            const uint32_t next = uint32_t(value >> 32);
            if (!next)
                return 0;
            const uint16_t depth = uint16_t(value >> 16);
            const uint16_t sequence = uint16_t(value);
            const uint32_t nextNext = *g_memory.Translate<u32>(next);
            const uint64_t desired = (uint64_t(nextNext) << 32) | (uint64_t(uint16_t(depth - 1)) << 16) | sequence;
            if (a.compare_exchange_weak(expected, ByteSwap(desired)))
                return next;
        }
    }

    uint32_t InterlockedFlushSList(uint64_t* header)
    {
        std::atomic_ref<uint64_t> a(*header);
        uint64_t expected = a.load();
        for (;;)
        {
            const uint64_t value = ByteSwap(expected);
            const uint32_t next = uint32_t(value >> 32);
            const uint64_t desired = value & 0xFFFF; // keep sequence, clear list and depth
            if (a.compare_exchange_weak(expected, ByteSwap(desired)))
                return next;
        }
    }
}

SOD_IMPORT_AS(ExCreateThread, sod::imp::ExCreateThread)
SOD_IMPORT_AS(ExTerminateThread, sod::imp::ExTerminateThread)
SOD_IMPORT_AS(NtResumeThread, sod::imp::NtResumeThread)
SOD_IMPORT_AS(KeResumeThread, sod::imp::KeResumeThread)
SOD_IMPORT_AS(KeSetAffinityThread, sod::imp::KeSetAffinityThread)
SOD_IMPORT_AS(KeQueryBasePriorityThread, sod::imp::KeQueryBasePriorityThread)
SOD_IMPORT_AS(KeSetBasePriorityThread, sod::imp::KeSetBasePriorityThread)
SOD_IMPORT_AS(KeGetCurrentProcessType, sod::imp::KeGetCurrentProcessType)
SOD_IMPORT_AS(KeQueryPerformanceFrequency, sod::imp::KeQueryPerformanceFrequency)
SOD_IMPORT_AS(KeDelayExecutionThread, sod::imp::KeDelayExecutionThread)
SOD_IMPORT_AS(NtYieldExecution, sod::imp::NtYieldExecution)
SOD_IMPORT_AS(KeQuerySystemTime, sod::imp::KeQuerySystemTime)
SOD_IMPORT_AS(KeTlsAlloc, sod::imp::KeTlsAlloc)
SOD_IMPORT_AS(KeTlsFree, sod::imp::KeTlsFree)
SOD_IMPORT_AS(KeTlsGetValue, sod::imp::KeTlsGetValue)
SOD_IMPORT_AS(KeTlsSetValue, sod::imp::KeTlsSetValue)
SOD_IMPORT_AS(KeEnterCriticalRegion, sod::imp::KeEnterCriticalRegion)
SOD_IMPORT_AS(KeLeaveCriticalRegion, sod::imp::KeLeaveCriticalRegion)
SOD_IMPORT_AS(KeEnableFpuExceptions, sod::imp::KeEnableFpuExceptions)
SOD_IMPORT_AS(KiApcNormalRoutineNop, sod::imp::KiApcNormalRoutineNop)
SOD_IMPORT_AS(KeLockL2, sod::imp::KeLockL2)
SOD_IMPORT_AS(KeUnlockL2, sod::imp::KeUnlockL2)
SOD_IMPORT_AS(KeSetEvent, sod::imp::KeSetEvent)
SOD_IMPORT_AS(KeResetEvent, sod::imp::KeResetEvent)
SOD_IMPORT_AS(NtCreateEvent, sod::imp::NtCreateEvent)
SOD_IMPORT_AS(NtSetEvent, sod::imp::NtSetEvent)
SOD_IMPORT_AS(NtClearEvent, sod::imp::NtClearEvent)
SOD_IMPORT_AS(KeInitializeSemaphore, sod::imp::KeInitializeSemaphore)
SOD_IMPORT_AS(KeReleaseSemaphore, sod::imp::KeReleaseSemaphore)
SOD_IMPORT_AS(NtCreateSemaphore, sod::imp::NtCreateSemaphore)
SOD_IMPORT_AS(NtReleaseSemaphore, sod::imp::NtReleaseSemaphore)
SOD_IMPORT_AS(NtCreateMutant, sod::imp::NtCreateMutant)
SOD_IMPORT_AS(NtReleaseMutant, sod::imp::NtReleaseMutant)
SOD_IMPORT_AS(NtCreateTimer, sod::imp::NtCreateTimer)
SOD_IMPORT_AS(NtSetTimerEx, sod::imp::NtSetTimerEx)
SOD_IMPORT_AS(NtCancelTimer, sod::imp::NtCancelTimer)
SOD_IMPORT_AS(NtClose, sod::imp::NtClose)
SOD_IMPORT_AS(NtDuplicateObject, sod::imp::NtDuplicateObject)
SOD_IMPORT_AS(ObReferenceObjectByHandle, sod::imp::ObReferenceObjectByHandle)
SOD_IMPORT_AS(ObDereferenceObject, sod::imp::ObDereferenceObject)
SOD_IMPORT_AS(KeWaitForSingleObject, sod::imp::KeWaitForSingleObject)
SOD_IMPORT_AS(NtWaitForSingleObjectEx, sod::imp::NtWaitForSingleObjectEx)
SOD_IMPORT_AS(KeWaitForMultipleObjects, sod::imp::KeWaitForMultipleObjects)
SOD_IMPORT_AS(NtWaitForMultipleObjectsEx, sod::imp::NtWaitForMultipleObjectsEx)
SOD_IMPORT_AS(RtlInitializeCriticalSection, sod::imp::RtlInitializeCriticalSection)
SOD_IMPORT_AS(RtlInitializeCriticalSectionAndSpinCount, sod::imp::RtlInitializeCriticalSectionAndSpinCount)
SOD_IMPORT_AS(RtlEnterCriticalSection, sod::imp::RtlEnterCriticalSection)
SOD_IMPORT_AS(RtlLeaveCriticalSection, sod::imp::RtlLeaveCriticalSection)
SOD_IMPORT_AS(RtlTryEnterCriticalSection, sod::imp::RtlTryEnterCriticalSection)
SOD_IMPORT_AS(KfAcquireSpinLock, sod::imp::KfAcquireSpinLock)
SOD_IMPORT_AS(KfReleaseSpinLock, sod::imp::KfReleaseSpinLock)
SOD_IMPORT_AS(NtSignalAndWaitForSingleObjectEx, sod::imp::NtSignalAndWaitForSingleObjectEx)
SOD_IMPORT_AS(KeAcquireSpinLockAtRaisedIrql, sod::imp::KeAcquireSpinLockAtRaisedIrql)
SOD_IMPORT_AS(KeReleaseSpinLockFromRaisedIrql, sod::imp::KeReleaseSpinLockFromRaisedIrql)
SOD_IMPORT_AS(KeRaiseIrqlToDpcLevel, sod::imp::KeRaiseIrqlToDpcLevel)
SOD_IMPORT_AS(KfLowerIrql, sod::imp::KfLowerIrql)
SOD_IMPORT_AS(InterlockedPopEntrySList, sod::imp::InterlockedPopEntrySList)
SOD_IMPORT_AS(InterlockedFlushSList, sod::imp::InterlockedFlushSList)
