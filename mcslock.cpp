#if 0
#include <atomic>

struct qnode {
    std::atomic<qnode *> next;
    std::atomic<bool> wait;
};

class MCSLock
{
private:
    std::atomic<qnode *> _tail;

public:
    MCSLock() : _tail(nullptr){};
    ~MCSLock(){};

    void lock(qnode *p)
    {
        p->next.store(nullptr, std::memory_order_relaxed);
        p->wait.store(true, std::memory_order_relaxed);

        // exchange _tail with p, return old value of _tail
        qnode *prev = _tail.exchange(p, std::memory_order_acquire);

        // append p to list if list is non-empty and wait until p is head of list
        if (prev != nullptr) {
            prev->next.store(p, std::memory_order_relaxed);
            while (p->wait.load(std::memory_order_relaxed)) {
                // yield/sleep, otherwise does not scale if num_threads > hw_threads
                // std::this_thread::yield();
            }
        }
        std::atomic_thread_fence(std::memory_order_acq_rel);
    }

    // lock can be acquired only when p is head of list
    // => release is only called when p is head of list
    void unlock(qnode *p)
    {
        qnode *succ = p->next.load(std::memory_order_release);

        if (succ == nullptr) {
            qnode *old_p = p; // need to keep p
            if (_tail.compare_exchange_strong(old_p,
                                              nullptr,
                                              std::memory_order_relaxed,
                                              std::memory_order_relaxed)) {
                // _tail was p and is now nullptr
                return;
            }
            // another thread changed _tail (new qnode enters queue)
            // wait until other thread has appended his qnode!
            do {
                succ = p->next.load(std::memory_order_relaxed);
            } while (succ == nullptr);
        }

        succ->wait.store(false, std::memory_order_relaxed);
    }
};
#endif