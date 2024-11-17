#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>

// Очередь задач с кражей работы
class WorkStealingQueue {
public:
    WorkStealingQueue() : stop(false) {}
    void enqueue(std::function<void()> task) {
        std::unique_lock<std::mutex> lock(mutex);
        tasks.push(task);
        condition.notify_one();
    }

    std::function<void()> dequeue() {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [this] { return !tasks.empty() || stop; });
        if (stop && tasks.empty()) {
            return nullptr;
        }
        std::function<void()> task = tasks.front();
        tasks.pop();
        return task;
    }

    void stopQueue() {
        {
            std::unique_lock<std::mutex> lock(mutex);
            stop = true;
        }
        condition.notify_all();
    }

private:
    std::queue<std::function<void()>> tasks;
    std::mutex mutex;
    std::condition_variable condition;
    bool stop;
};

// Пул потоков
class ThreadPool {
public:
    ThreadPool(size_t numThreads) : stop(false) {
        queues.resize(numThreads);
        for (size_t i = 0; i < numThreads; ++i) {
            queues[i] = std::make_unique<WorkStealingQueue>();
            threads.emplace_back([this, i] { worker(i); });
        }
    }

    ~ThreadPool() {
        stop = true;
        for (auto& queue : queues) {
            queue->stopQueue();
        }
        for (auto& thread : threads) {
            thread.join();
        }
    }

    void enqueue(std::function<void()> task) {
        size_t index = nextQueueIndex.fetch_add(1) % queues.size();
        queues[index]->enqueue(task);
    }

private:
    void worker(size_t index) {
        while (!stop) {
            std::function<void()> task = queues[index]->dequeue();
            if (task) {
                task();
            }
            else {
                for (size_t i = 0; i < queues.size(); ++i) {
                    if (i != index) {
                        task = queues[i]->dequeue();
                        if (task) {
                            task();
                            break;
                        }
                    }
                }
            }
        }
    }

    std::vector<std::unique_ptr<WorkStealingQueue>> queues;
    std::vector<std::thread> threads;
    std::atomic<bool> stop;
    std::atomic<size_t> nextQueueIndex{ 0 };
};

// Функция быстрой сортировки
void quicksort(std::vector<int>& array, int left, int right, std::shared_ptr<std::promise<void>> parentPromise = nullptr) {
    if (left >= right) return;

    int pivot = array[(left + right) / 2];
    int left_bound = left;
    int right_bound = right;

    while (left_bound <= right_bound) {
        while (array[left_bound] < pivot) left_bound++;
        while (array[right_bound] > pivot) right_bound--;
        if (left_bound <= right_bound) {
            std::swap(array[left_bound], array[right_bound]);
            left_bound++;
            right_bound--;
        }
    }

    if (right_bound - left > 100000) {
        auto leftPromise = std::make_shared<std::promise<void>>();
        auto leftFuture = leftPromise->get_future();
        auto leftTask = [&array, left, right_bound, leftPromise]() {
            quicksort(array, left, right_bound, leftPromise);
            };

        auto rightPromise = std::make_shared<std::promise<void>>();
        auto rightFuture = rightPromise->get_future();
        auto rightTask = [&array, left_bound, right, rightPromise]() {
            quicksort(array, left_bound, right, rightPromise);
            };

        static ThreadPool pool(4); // Создаем пул потоков
        pool.enqueue(leftTask);
        pool.enqueue(rightTask);

        leftFuture.wait();
        rightFuture.wait();

        if (parentPromise) {
            parentPromise->set_value();
        }
    }
    else {
        quicksort(array, left, right_bound);
        quicksort(array, left_bound, right);
        if (parentPromise) {
            parentPromise->set_value();
        }
    }
}

int main() {
    std::vector<int> array(200000);
    for (int i = 0; i < array.size(); ++i) {
        array[i] = rand() % 200000;
    }

    auto start = std::chrono::high_resolution_clock::now();
    quicksort(array, 0, array.size() - 1);
    auto end = std::chrono::high_resolution_clock::now();
    /*for (int i : array) {
        std::cout << array[i]<<" ";
    }*/
    std::chrono::duration<double> duration = end - start;
    std::cout << "Time taken: " << duration.count() << " seconds" << std::endl;

    return 0;
}