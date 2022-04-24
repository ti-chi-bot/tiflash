#include <Common/DynamicThreadPool.h>
#include <TestUtils/TiFlashTestBasic.h>

namespace DB::tests
{
namespace
{
class DynamicThreadPoolTest : public ::testing::Test
{
};

TEST_F(DynamicThreadPoolTest, testAutoExpanding)
try
{
    DynamicThreadPool pool(1, std::chrono::milliseconds(10));

    std::atomic<int> a = 0;

    auto f0 = pool.schedule(true, [&] {
        while (true)
        {
            if (a.load())
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    auto cnt = pool.threadCount();
    ASSERT_EQ(cnt.fixed, 1);
    ASSERT_EQ(cnt.dynamic, 0);

    std::atomic<int> b = 0;

    auto f1 = pool.schedule(true, [&] {
        while (!b.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        a.store(1);
    });

    cnt = pool.threadCount();
    ASSERT_EQ(cnt.fixed, 1);
    ASSERT_EQ(cnt.dynamic, 1);

    b.store(1);
    f0.wait();
    f1.wait();
}
CATCH

TEST_F(DynamicThreadPoolTest, testDynamicShrink)
try
{
    DynamicThreadPool pool(0, std::chrono::milliseconds(50));

    auto f0 = pool.schedule(true, [] {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return 0; });
    pool.schedule(true, [] { return 0; });

    auto cnt = pool.threadCount();
    ASSERT_EQ(cnt.fixed, 0);
    ASSERT_EQ(cnt.dynamic, 2);

    for (int i = 0; i < 10; ++i)
    {
        f0.wait();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        f0 = pool.schedule(true, [] { return 0; });
    }
    cnt = pool.threadCount();
    ASSERT_EQ(cnt.fixed, 0);
    ASSERT_EQ(cnt.dynamic, 1);

    std::this_thread::sleep_for(std::chrono::seconds(1));
    cnt = pool.threadCount();
    ASSERT_EQ(cnt.fixed, 0);
    ASSERT_EQ(cnt.dynamic, 0);
}
CATCH

TEST_F(DynamicThreadPoolTest, testFixedAlwaysWorking)
try
{
    DynamicThreadPool pool(4, std::chrono::milliseconds(10));

    auto cnt = pool.threadCount();
    ASSERT_EQ(cnt.fixed, 4);
    ASSERT_EQ(cnt.dynamic, 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    cnt = pool.threadCount();
    ASSERT_EQ(cnt.fixed, 4);
    ASSERT_EQ(cnt.dynamic, 0);
}
CATCH

TEST_F(DynamicThreadPoolTest, testExceptionSafe)
try
{
    DynamicThreadPool pool(1, std::chrono::milliseconds(10));

    auto f0 = pool.schedule(true, [] { throw Exception("test"); });
    ASSERT_THROW(f0.get(), Exception);

    auto cnt = pool.threadCount();
    ASSERT_EQ(cnt.fixed, 1);
    ASSERT_EQ(cnt.dynamic, 0);

    auto f1 = pool.schedule(true, [] { return 1; });
    ASSERT_EQ(f1.get(), 1);
}
CATCH

TEST_F(DynamicThreadPoolTest, testMemoryTracker)
try
{
    MemoryTracker t0, t1, t2;

    current_memory_tracker = &t2;

    auto getter = [] {
        return current_memory_tracker;
    };

    auto setter = [](MemoryTracker * p) {
        current_memory_tracker = p;
    };

    DynamicThreadPool pool(1, std::chrono::milliseconds(10));

    auto f = pool.schedule(false, getter);
    ASSERT_EQ(f.get(), nullptr);

    auto f0 = pool.schedule(false, setter, &t0);
    f0.wait();

    auto f1 = pool.schedule(false, getter);
    // f0 didn't pollute memory_tracker
    ASSERT_EQ(f1.get(), nullptr);

    current_memory_tracker = &t1;

    auto f2 = pool.schedule(true, getter);
    // set propagate = true and it did propagate
    ASSERT_EQ(f2.get(), &t1);

    auto f3 = pool.schedule(false, getter);
    // set propagate = false and it didn't propagate
    ASSERT_EQ(f3.get(), nullptr);
}
CATCH

struct X
{
    std::mutex * mu;
    std::condition_variable * cv;
    bool * destructed;

    X(std::mutex * mu_, std::condition_variable * cv_, bool * destructed_)
        : mu(mu_)
        , cv(cv_)
        , destructed(destructed_)
    {}

    ~X()
    {
        std::unique_lock lock(*mu);
        *destructed = true;
        cv->notify_all();
    }
};

TEST_F(DynamicThreadPoolTest, testTaskDestruct)
try
{
    std::mutex mu;
    std::condition_variable cv;
    bool destructed = false;

    DynamicThreadPool pool(0, std::chrono::minutes(1));
    auto tmp = std::make_shared<X>(&mu, &cv, &destructed);
    pool.schedule(true, [x = tmp] {});
    tmp.reset();

    {
        std::unique_lock lock(mu);
        auto ret = cv.wait_for(lock, std::chrono::seconds(1), [&] { return destructed; });
        ASSERT_TRUE(ret);
    }
}
CATCH

} // namespace
} // namespace DB::tests
