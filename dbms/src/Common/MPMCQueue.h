// Copyright 2023 PingCAP, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <Common/SimpleIntrusiveNode.h>
#include <common/defines.h>
#include <common/types.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <type_traits>

namespace DB
{
namespace MPMCQueueDetail
{
/// WaitingNode is used to construct a double-linked waiting list so that
/// every time a push/pop succeeds, it can notify next reader/writer in fifo.
///
/// Double link is to support remove self from the mid of the list when timeout.
struct WaitingNode : public SimpleIntrusiveNode<WaitingNode>
{
<<<<<<< HEAD
=======
public:
    ALWAYS_INLINE void notifyNext()
    {
        if (next != this)
        {
            next->cv.notify_one();
            next->detach(); // avoid being notified more than once
        }
    }

    ALWAYS_INLINE void notifyAll()
    {
        for (auto * p = this; p->next != this; p = p->next)
            p->next->cv.notify_one();
    }

    template <typename Pred>
    ALWAYS_INLINE void wait(std::unique_lock<std::mutex> & lock, Pred pred)
    {
#ifdef __APPLE__
        WaitingNode node;
#else
        thread_local WaitingNode node;
#endif
        while (!pred())
        {
            node.prependTo(this);
            node.cv.wait(lock);
            node.detach();
        }
    }

    template <typename Pred>
    ALWAYS_INLINE bool waitFor(
        std::unique_lock<std::mutex> & lock,
        Pred pred,
        const std::chrono::steady_clock::time_point & deadline)
    {
#ifdef __APPLE__
        WaitingNode node;
#else
        thread_local WaitingNode node;
#endif
        while (!pred())
        {
            node.prependTo(this);
            auto res = node.cv.wait_until(lock, deadline);
            node.detach();
            if (res == std::cv_status::timeout)
                return false;
        }
        return true;
    }

private:
>>>>>>> 6638f2067b (Fix license and format coding style (#7962))
    std::condition_variable cv;
};
} // namespace MPMCQueueDetail

enum class MPMCQueueStatus
{
    NORMAL,
    CANCELLED,
    FINISHED,
};

/// MPMCQueue is a FIFO queue which supports concurrent operations from
/// multiple producers and consumers.
///
/// MPMCQueue is thread-safe and exception-safe.
///
/// It is inspired by MCSLock that all blocking readers/writers construct a
/// waiting list and everyone only wait on its own condition_variable.
///
/// This can significantly reduce contentions and avoid "thundering herd" problem.
template <typename T>
class MPMCQueue
{
public:
    using Status = MPMCQueueStatus;

<<<<<<< HEAD
    explicit MPMCQueue(Int64 capacity_)
        : capacity(capacity_)
        , data(capacity * sizeof(T))
    {
    }

    ~MPMCQueue()
    {
        std::unique_lock lock(mu);
        for (; read_pos < write_pos; ++read_pos)
            destruct(getObj(read_pos));
    }

    /// Block util:
    /// 1. Pop succeeds with a valid T: return true.
    /// 2. The queue is cancelled or finished: return false.
    bool pop(T & obj)
    {
        return popObj(obj);
    }
=======
    MPMCQueue(
        const CapacityLimits & capacity_limits_,
        ElementAuxiliaryMemoryUsageFunc && get_auxiliary_memory_usage_ = [](const T &) { return 0; })
        : capacity_limits(capacity_limits_)
        , get_auxiliary_memory_usage(
              capacity_limits.max_bytes == std::numeric_limits<Int64>::max()
                  ? [](const T &) {
                        return 0;
                    }
                  : std::move(get_auxiliary_memory_usage_))
        , element_auxiliary_memory(capacity_limits.max_size, 0)
        , data(capacity_limits.max_size * sizeof(T))
    {}

    ~MPMCQueue() { drain(); }

    // Cannot to use copy/move constructor,
    // because MPMCQueue maybe used by different threads.
    // Copy and move it is dangerous.
    DISALLOW_COPY_AND_MOVE(MPMCQueue);

    /*
    * | Queue Status     | Empty      | Behavior                 |
    * |------------------|------------|--------------------------|
    * | Normal           | Yes        | Block                    |
    * | Normal           | No         | Pop and return OK        |
    * | Finished         | Yes        | return FINISHED          |
    * | Finished         | No         | Pop and return OK        |
    * | Cancelled        | Yes/No     | return CANCELLED         |
    * */
    ALWAYS_INLINE Result pop(T & obj) { return popObj<true>(obj); }
>>>>>>> 6638f2067b (Fix license and format coding style (#7962))

    /// Besides all conditions mentioned at `pop`, `tryPop` will return false if `timeout` is exceeded.
    template <typename Duration>
    bool tryPop(T & obj, const Duration & timeout)
    {
        /// std::condition_variable::wait_until will always use system_clock.
        auto deadline = std::chrono::system_clock::now() + timeout;
        return popObj(obj, &deadline);
    }

<<<<<<< HEAD
    /// Block util:
    /// 1. Push succeeds and return true.
    /// 2. The queue is cancelled and return false.
    /// 3. The queue has finished and return false.
=======
    /// Non-blocking function.
    /// Besides all conditions mentioned at `pop`, `tryPop` will immediately return EMPTY if queue is `NORMAL` but empty.
    ALWAYS_INLINE Result tryPop(T & obj) { return popObj<false>(obj); }

    /*
    * | Queue Status     | Empty      | Behavior                 |
    * |------------------|------------|--------------------------|
    * | Normal           | Yes        | Block                    |
    * | Normal           | No         | Pop and return OK        |
    * | Finished         | Yes/No     | return FINISHED          |
    * | Cancelled        | Yes/No     | return CANCELLED         |
    * */
>>>>>>> 6638f2067b (Fix license and format coding style (#7962))
    template <typename U>
    ALWAYS_INLINE bool push(U && u)
    {
        return pushObj(std::forward<U>(u));
    }

    /// Besides all conditions mentioned at `push`, `tryPush` will return false if `timeout` is exceeded.
    template <typename U, typename Duration>
    ALWAYS_INLINE bool tryPush(U && u, const Duration & timeout)
    {
        /// std::condition_variable::wait_until will always use system_clock.
        auto deadline = std::chrono::system_clock::now() + timeout;
        return pushObj(std::forward<U>(u), &deadline);
    }

    /// The same as `push` except it will construct the object in place.
    template <typename... Args>
    ALWAYS_INLINE bool emplace(Args &&... args)
    {
        return emplaceObj(nullptr, std::forward<Args>(args)...);
    }

    /// The same as `tryPush` except it will construct the object in place.
    template <typename... Args, typename Duration>
    ALWAYS_INLINE bool tryEmplace(Args &&... args, const Duration & timeout)
    {
        /// std::condition_variable::wait_until will always use system_clock.
        auto deadline = std::chrono::system_clock::now() + timeout;
        return emplaceObj(&deadline, std::forward<Args>(args)...);
    }

    /// Cancel a NORMAL queue will wake up all blocking readers and writers.
    /// After `cancel()` the queue can't be pushed or popped any more.
    /// That means some objects may leave at the queue without poped.
<<<<<<< HEAD
    void cancel()
    {
        std::unique_lock lock(mu);
        if (isNormal())
        {
=======
    bool cancel() { return cancelWith(""); }

    bool cancelWith(String reason)
    {
        return changeStatus([&] {
>>>>>>> 6638f2067b (Fix license and format coding style (#7962))
            status = Status::CANCELLED;
            notifyAll();
        }
    }

    /// Finish a NORMAL queue will wake up all blocking readers and writers.
    /// After `finish()` the queue can't be pushed any more while `pop` is allowed
    /// the queue is empty.
    /// Return true if the previous status is NORMAL.
    bool finish()
    {
<<<<<<< HEAD
        std::unique_lock lock(mu);
        if (isNormal())
        {
            status = Status::FINISHED;
            notifyAll();
            return true;
        }
        else
            return false;
=======
        return changeStatus([&] { status = Status::FINISHED; });
>>>>>>> 6638f2067b (Fix license and format coding style (#7962))
    }

    bool isNextPopNonBlocking() const
    {
        std::unique_lock lock(mu);
        return read_pos < write_pos || !isNormal();
    }

    bool isNextPushNonBlocking() const
    {
        std::unique_lock lock(mu);
        return write_pos - read_pos < capacity || !isNormal();
    }

    MPMCQueueStatus getStatus() const
    {
        std::unique_lock lock(mu);
        return status;
    }

    size_t size() const
    {
        std::unique_lock lock(mu);
        assert(write_pos >= read_pos);
        return static_cast<size_t>(write_pos - read_pos);
    }

private:
    using TimePoint = std::chrono::time_point<std::chrono::system_clock>;
    using WaitingNode = MPMCQueueDetail::WaitingNode;

    void notifyAll()
    {
        for (auto * p = &reader_head; p->next != &reader_head; p = p->next)
            p->next->cv.notify_one();
        for (auto * p = &writer_head; p->next != &writer_head; p = p->next)
            p->next->cv.notify_one();
    }

    template <typename Pred>
    ALWAYS_INLINE void wait(
        std::unique_lock<std::mutex> & lock,
        WaitingNode & head,
        WaitingNode & node,
        Pred pred,
        const TimePoint * deadline)
    {
        if (deadline)
        {
            while (!pred())
            {
                node.prependTo(&head);
                auto res = node.cv.wait_until(lock, *deadline);
                node.detach();
                if (res == std::cv_status::timeout)
                    break;
            }
        }
        else
        {
            while (!pred())
            {
                node.prependTo(&head);
                node.cv.wait(lock);
                node.detach();
            }
        }
    }

    ALWAYS_INLINE void notifyNext(WaitingNode & head)
    {
        auto * next = head.next;
        if (next != &head)
        {
            next->cv.notify_one();
            next->detach(); // avoid being notified more than once
        }
    }

    bool popObj(T & res, const TimePoint * deadline = nullptr)
    {
#ifdef __APPLE__
        WaitingNode node;
#else
        thread_local WaitingNode node;
#endif
        {
            /// read_pos < write_pos means the queue isn't empty
            auto pred = [&] {
                return read_pos < write_pos || !isNormal();
            };

            std::unique_lock lock(mu);

            wait(lock, reader_head, node, pred, deadline);

            if (!isCancelled() && read_pos < write_pos)
            {
                auto & obj = getObj(read_pos);
                res = std::move(obj);
                destruct(obj);

                /// update pos only after all operations that may throw an exception.
                ++read_pos;

                /// Notify next writer within the critical area because:
                /// 1. If we remove the next writer node and notify it later,
                ///    it may find itself can't obtain the lock while not being in the list.
                ///    This need carefully procesing in `assignObj`.
                /// 2. If we do not remove the next writer, only obtain its pointer and notify it later,
                ///    deadlock can be possible because different readers may notify one writer.
                notifyNext(writer_head);
                return true;
            }
        }
        return false;
    }

    template <typename F>
    bool assignObj(const TimePoint * deadline, F && assigner)
    {
#ifdef __APPLE__
        WaitingNode node;
#else
        thread_local WaitingNode node;
#endif
        auto pred = [&] {
            return write_pos - read_pos < capacity || !isNormal();
        };

<<<<<<< HEAD
        std::unique_lock lock(mu);

        wait(lock, writer_head, node, pred, deadline);

        /// double check status after potential wait
        /// check write_pos because timeouted will also reach here.
        if (isNormal() && write_pos - read_pos < capacity)
=======
        if constexpr (need_wait)
        {
            auto pred = [&] {
                return (write_pos - read_pos < capacity_limits.max_size
                        && current_auxiliary_memory_usage < capacity_limits.max_bytes)
                    || !isNormal();
            };
            if (!wait(lock, writer_head, pred, deadline))
                is_timeout = true;
        }

        /// double check status after potential wait
        /// check write_pos because timeouted will also reach here.
        if (isNormal()
            && (write_pos - read_pos < capacity_limits.max_size
                && current_auxiliary_memory_usage < capacity_limits.max_bytes))
>>>>>>> 6638f2067b (Fix license and format coding style (#7962))
        {
            void * addr = getObjAddr(write_pos);
            assigner(addr);

            /// update pos only after all operations that may throw an exception.
            ++write_pos;

            /// See comments in `popObj`.
<<<<<<< HEAD
            notifyNext(reader_head);
            return true;
=======
            reader_head.notifyNext();
            if (capacity_limits.max_bytes != std::numeric_limits<Int64>::max()
                && current_auxiliary_memory_usage < capacity_limits.max_bytes
                && write_pos - read_pos < capacity_limits.max_size)
                writer_head.notifyNext();
            return Result::OK;
        }
        if constexpr (need_wait)
        {
            if (is_timeout)
                return Result::TIMEOUT;
        }
        switch (status)
        {
        case Status::NORMAL:
            return Result::FULL;
        case Status::CANCELLED:
            return Result::CANCELLED;
        case Status::FINISHED:
            return Result::FINISHED;
>>>>>>> 6638f2067b (Fix license and format coding style (#7962))
        }
        return false;
    }

    template <typename U>
    ALWAYS_INLINE bool pushObj(U && u, const TimePoint * deadline = nullptr)
    {
        return assignObj(deadline, [&](void * addr) { new (addr) T(std::forward<U>(u)); });
    }

    template <typename... Args>
    ALWAYS_INLINE bool emplaceObj(const TimePoint * deadline, Args &&... args)
    {
        return assignObj(deadline, [&](void * addr) { new (addr) T(std::forward<Args>(args)...); });
    }

    ALWAYS_INLINE bool isNormal() const { return likely(status == Status::NORMAL); }

    ALWAYS_INLINE bool isCancelled() const { return unlikely(status == Status::CANCELLED); }

    ALWAYS_INLINE void * getObjAddr(Int64 pos)
    {
        pos = (pos % capacity) * sizeof(T);
        return &data[pos];
    }

    ALWAYS_INLINE T & getObj(Int64 pos) { return *reinterpret_cast<T *>(getObjAddr(pos)); }

    ALWAYS_INLINE void destruct(T & obj)
    {
        if constexpr (!std::is_trivially_destructible_v<T>)
            obj.~T();
    }

private:
    const Int64 capacity;

    mutable std::mutex mu;
    WaitingNode reader_head;
    WaitingNode writer_head;
    Int64 read_pos = 0;
    Int64 write_pos = 0;
    Status status = Status::NORMAL;

    std::vector<UInt8> data;
};

} // namespace DB
