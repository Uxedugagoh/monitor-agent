#include "buffer/activity_queue.h"

#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

void require(bool condition)
{
    if (!condition) throw std::runtime_error("Queue check failed");
}

int main()
{
    ActivityQueue queue;
    for (std::size_t i = 0; i < ActivityQueue::capacity; ++i)
    {
        ActivityRecord record;
        record.time = std::to_string(i);
        require(queue.tryPush(std::move(record)));
    }
    require(!queue.tryPush({}));
    queue.close();
    require(!queue.tryPush({}));
    ActivityRecord record;
    for (std::size_t i = 0; i < ActivityQueue::capacity; ++i)
    {
        require(queue.waitPop(record));
        require(record.time == std::to_string(i));
    }
    require(!queue.waitPop(record));
    queue.close();

    ActivityQueue empty;
    auto waiter = std::async(std::launch::async, [&] {
        ActivityRecord item;
        return empty.waitPop(item);
    });
    empty.close();
    require(!waiter.get());

    ActivityQueue concurrent;
    auto reader = std::async(std::launch::async, [&] {
        ActivityRecord item;
        int expected = 0;
        while (concurrent.waitPop(item))
        {
            require(item.time == std::to_string(expected++));
        }
        return expected;
    });
    for (int i = 0; i < 10000; ++i)
    {
        ActivityRecord item;
        item.time = std::to_string(i);
        // Retry only in this test to exercise concurrent transfer of all items.
        while (!concurrent.tryPush(item)) std::this_thread::yield();
    }
    concurrent.close();
    require(reader.get() == 10000);
    std::cout << "PASS: capacity, FIFO, close, wake-up, 10000 concurrent transfers\n";
}
