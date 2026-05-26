#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifndef __EMSCRIPTEN__
#include <exec/async_scope.hpp>
#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>
#endif

namespace boxpacker_private {

struct BackgroundJobSnapshot {
    std::uint64_t id{};
    float         progress{};
    bool          done{};
    bool          cancelled{};
    std::string   status;
};

class BackgroundWorkSketch {
  public:
    BackgroundWorkSketch() = default;

    ~BackgroundWorkSketch() {
        cancel_all();
#ifndef __EMSCRIPTEN__
        (void)stdexec::sync_wait(scope_.on_empty());
#endif
    }

    BackgroundWorkSketch(const BackgroundWorkSketch &)            = delete;
    BackgroundWorkSketch &operator=(const BackgroundWorkSketch &) = delete;

    auto start_demo_job() -> std::uint64_t {
        const std::uint64_t job_id = next_job_id_.fetch_add(1, std::memory_order_relaxed);
        auto                job    = std::make_shared<JobState>(job_id);

        {
            std::lock_guard lock(jobs_mutex_);
            jobs_.push_back(job);
        }

#ifndef __EMSCRIPTEN__
        auto sender = stdexec::starts_on(
            worker_pool_.get_scheduler(),
            stdexec::just(job) | stdexec::then([this](JobHandle running_job) { run_demo_job(std::move(running_job)); }));
        scope_.spawn(std::move(sender));
#else
        job->progress.store(1.0f, std::memory_order_relaxed);
        job->done.store(true, std::memory_order_release);
        set_status(*job, "Completed on the main thread in the web build");
        append_completed_message("Job #" + std::to_string(job_id) + " completed without worker threads.");
#endif

        return job_id;
    }

    void cancel_all() {
        std::vector<JobHandle> jobs;
        {
            std::lock_guard lock(jobs_mutex_);
            jobs = jobs_;
        }

        for (const JobHandle &job : jobs) {
            job->cancel_requested.store(true, std::memory_order_release);
        }
    }

    auto snapshot_jobs() const -> std::vector<BackgroundJobSnapshot> {
        std::vector<JobHandle> jobs;
        {
            std::lock_guard lock(jobs_mutex_);
            jobs = jobs_;
        }

        std::vector<BackgroundJobSnapshot> snapshots;
        snapshots.reserve(jobs.size());

        for (const JobHandle &job : jobs) {
            BackgroundJobSnapshot snapshot;
            snapshot.id        = job->id;
            snapshot.progress  = job->progress.load(std::memory_order_relaxed);
            snapshot.done      = job->done.load(std::memory_order_acquire);
            snapshot.cancelled = job->cancelled.load(std::memory_order_acquire);

            {
                std::lock_guard lock(job->mutex);
                snapshot.status = job->status;
            }

            snapshots.push_back(std::move(snapshot));
        }

        return snapshots;
    }

    auto drain_completed_messages() -> std::vector<std::string> {
        std::lock_guard lock(completed_messages_mutex_);
        std::vector<std::string> drained;
        drained.swap(completed_messages_);
        return drained;
    }

  private:
    struct JobState {
        explicit JobState(std::uint64_t job_id) : id(job_id) {}

        std::uint64_t      id;
        std::atomic<float> progress{0.0f};
        std::atomic<bool>  done{false};
        std::atomic<bool>  cancelled{false};
        std::atomic<bool>  cancel_requested{false};
        mutable std::mutex mutex;
        std::string        status{"Queued"};
    };

    using JobHandle = std::shared_ptr<JobState>;

    static void set_status(JobState &job, std::string_view status) {
        std::lock_guard lock(job.mutex);
        job.status.assign(status);
    }

    void append_completed_message(std::string message) {
        std::lock_guard lock(completed_messages_mutex_);
        completed_messages_.push_back(std::move(message));
    }

#ifndef __EMSCRIPTEN__
    static auto choose_worker_count() -> unsigned {
        const unsigned detected = std::thread::hardware_concurrency();
        return detected > 1 ? detected - 1 : 1;
    }

    void run_demo_job(JobHandle job) {
        using namespace std::chrono_literals;

        set_status(*job, "Preparing inputs");

        for (int step = 0; step <= 100; ++step) {
            if (job->cancel_requested.load(std::memory_order_acquire)) {
                job->cancelled.store(true, std::memory_order_release);
                job->done.store(true, std::memory_order_release);
                set_status(*job, "Cancelled");
                append_completed_message("Job #" + std::to_string(job->id) + " cancelled.");
                return;
            }

            if (step == 25) {
                set_status(*job, "Packing candidate layout");
            } else if (step == 55) {
                set_status(*job, "Evaluating constraints");
            } else if (step == 85) {
                set_status(*job, "Finalizing result");
            }

            job->progress.store(static_cast<float>(step) / 100.0f, std::memory_order_relaxed);
            std::this_thread::sleep_for(35ms);
        }

        job->done.store(true, std::memory_order_release);
        set_status(*job, "Completed");
        append_completed_message("Job #" + std::to_string(job->id) + " completed.");
    }

    exec::static_thread_pool worker_pool_{choose_worker_count()};
    exec::async_scope        scope_;
#endif

    mutable std::mutex         jobs_mutex_;
    std::vector<JobHandle>     jobs_;
    std::atomic<std::uint64_t> next_job_id_{1};

    std::mutex               completed_messages_mutex_;
    std::vector<std::string> completed_messages_;
};

} // namespace boxpacker_private