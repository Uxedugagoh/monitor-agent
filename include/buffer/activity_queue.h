#pragma once

#include "models/activity_record.h"

#include <condition_variable>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <deque>
#include <stdexcept>
#include <utility>
#include <vector>

class ActivityQueue
{
public:
    // Single-consumer protocol: peek, retry as needed, then acknowledge.
    // Records remain counted against capacity while HTTP is in progress.
    bool waitPeekBatch(std::vector<ActivityRecord>& batch,
        std::chrono::milliseconds interval = std::chrono::seconds(30))
    {
        batch.clear();
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait_for(lock, interval,
            [this] { return closed_ || records_.size() >= batchSize; });
        if (closed_) return false;
        for (const auto& record : records_)
        {
            if (batch.size() == batchSize) break;
            batch.push_back(record);
        }
        return true;
    }

    void acknowledge(std::size_t count)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (count > records_.size()) throw std::logic_error("Invalid acknowledgement");
        while (count-- > 0) records_.pop_front();
    }

    // Returns true if the producer closed the queue during the retry delay.
    bool waitClosed(std::chrono::milliseconds delay)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return ready_.wait_for(lock, delay, [this] { return closed_; });
    }

    std::vector<ActivityRecord> snapshot()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return {records_.begin(), records_.end()};
    }

    // Wait for 10 queued records, a deadline, or producer completion.
    // An empty result on timeout is allowed; false means closed and drained.
    bool waitBatch(std::vector<ActivityRecord>& batch,
        std::chrono::milliseconds interval = std::chrono::seconds(30))
    {
        batch.clear();
        batch.reserve(batchSize);
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait_until(lock, std::chrono::steady_clock::now() + interval,
            [this] { return closed_ || records_.size() >= batchSize; });
        if (closed_ && records_.empty())
        {
            return false;
        }
        while (!records_.empty() && batch.size() < batchSize)
        {
            batch.push_back(std::move(records_.front()));
            records_.pop_front();
        }
        return true;
    }

    // Does not wait for free space. Existing records are preserved when full.
    bool tryPush(ActivityRecord record)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_ || records_.size() >= capacity)
            {
                return false;
            }
            records_.push_back(std::move(record));
        }
        ready_.notify_one();
        return true;
    }

    // False means the producer has finished and all records have been read.
    bool waitPop(ActivityRecord& record)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [this] { return closed_ || !records_.empty(); });
        if (records_.empty())
        {
            return false;
        }
        record = std::move(records_.front());
        records_.pop_front();
        return true;
    }

    void close()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        ready_.notify_all();
    }

    static constexpr std::size_t capacity = 100;
    static constexpr std::size_t batchSize = 10;

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<ActivityRecord> records_;
    bool closed_ = false;
};
