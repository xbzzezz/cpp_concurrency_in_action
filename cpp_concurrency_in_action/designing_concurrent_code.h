///    Copyright (C) 2018 DG.C, DGCHOW, deguangchow
///        deguangchow@qq.com
///
///    \brief    chapter8: Designing concurrent code
///
///    \author   deguangchow
///    \version  1.0
///    \2018/12/20
#pragma once
#ifndef DESIGNING_CONCURRENT_CODE_H
#define DESIGNING_CONCURRENT_CODE_H

#include "lock_based_concurrent_data_structures.h"
#include "lock_free_concurrent_data_structures.h"

namespace design_conc_code {

//8.1.1 Dividing data between threads before processing begins
//8.1.2 Dividing date recursively
//Listing 8.1 Parallel Quicksort using a stack of pending chunks to sort
template<typename T>
struct sorter {
    struct chunk_to_sort {
        list<T> data;
        promise<list<T>> promise;
    };
    lock_based_conc_data::thread_safe_stack<chunk_to_sort>  m_stChunks;
    vector<thread>                                          m_vctThreads;
    unsigned const                                          m_uMaxThreadCount;
    atomic<bool>                                            m_bDone_a;
    sorter() : m_uMaxThreadCount(thread::hardware_concurrency() - 1), m_bDone_a(false) {
        TICK();
        INFO("max thread count is %d.", m_uMaxThreadCount);
    }
    ~sorter() {
        TICK();
        m_bDone_a = true;

        for (unsigned i = 0; i < m_vctThreads.size(); ++i) {
            m_vctThreads[i].join();
        }
    }
    void try_sort_chunk() {
        TICK();
        shared_ptr<chunk_to_sort> chunk = m_stChunks.pop();
        if (chunk) {
            sort_chunk(chunk);
        }
    }
    list<T> do_sort(list<T>& chunk_data) {
        TICK();
        if (chunk_data.empty()) {
            return chunk_data;
        }
        list<T> result;
        result.splice(result.begin(), chunk_data, chunk_data.begin());
        T const& tPartitionValue = *result.begin();

        auto posDivide = partition(chunk_data.begin(), chunk_data.end(),
            [&](T const& val) {
            return val < tPartitionValue;
        });

        chunk_to_sort chunkNewLower;
        chunkNewLower.data.splice(chunkNewLower.data.end(), chunk_data, chunk_data.begin(), posDivide);
        future<list<T>> lstNewLower_f = chunkNewLower.promise.get_future();
        m_stChunks.push(move(chunkNewLower));
        if (m_vctThreads.size() < m_uMaxThreadCount) {
            m_vctThreads.push_back(thread(&sorter<T>::sort_thread, this));
        }
        list<T> lstNewHigher(do_sort(chunk_data));
        result.splice(result.end(), lstNewHigher);
        while (lstNewLower_f.wait_for(seconds(0)) == future_status::timeout) {
            WARN("do_sort() loop...");
            try_sort_chunk();
        }
        result.splice(result.begin(), lstNewLower_f.get());
        return result;
    }
    void sort_chunk(shared_ptr<chunk_to_sort> const& chunk) {
        TICK();
        chunk->promise.set_value(do_sort(chunk->data));
    }
    void sort_thread() {
        TICK();
        while (!m_bDone_a) {
            DEBUG("sort_thread() loop...");
            try_sort_chunk();
            yield();
        }
    }
};
template<typename T>
list<T> parallel_quick_sort(list<T> input) {
    TICK();
    if (input.empty()) {
        return input;
    }
    sorter<T> s;
    return s.do_sort(input);
}
void test_parallel_quick_sort();

//8.1.3 Dividing work by task type

//8.2 Factors affecting the performance of concurrent code
//8.2.1 How many processors?
//8.2.2 Data contention and cache ping-pang
void processing_loop();
void processing_loop_test();

void processing_loop_with_mutex();
void processing_loop_with_mutex_test();

//8.2.3 False sharing

//8.2.4 How close is your data?
//8.2.5 Oversubscription and excessive task switching
typedef unsigned my_data;

//8.3 Designing data structures for multithreaded performance
//8.3.1 Dividing array elements for complex operations
//8.3.2 Data access patterns in other data structures
#if 1
struct protected_data {
    mutex m;
    char padding[CACHE_LINE];//65536 bytes is orders of magnitude larger than a cache line
    my_data data_to_protect;
};
#else
typedef unsigned data_item1;
typedef unsigned data_item2;
struct my_data {
    data_item1 d1;
    data_item2 d2;
    char padding[CACHE_LINE];
};
my_data some_array[256];
#endif
void processing_loop_protect();
void processing_loop_protect_test();

//8.4 Additional considerations when designing for concurrency
//8.4.1 Exception safety in parallel algorithms
//Listing 8.2 A naive parallel version of accumulate(from listing 2.8)

//Listing 8.3 A parallel version of accumulate using packaged_task
template<typename Iterator, typename T>
struct accumulate_block {
    T operator()(Iterator first, Iterator last) {
        //TICK();
        return accumulate(first, last, T());
    }
};
template<typename Iterator, typename T>
T parallel_accumulate(Iterator first, Iterator last, T init) {
    TICK();
    unsigned long const length = distance(first, last);
    if (!length) {
        return init;
    }
    unsigned long const min_per_thread = 25;
    unsigned long const max_threads = (length + min_per_thread - 1) / min_per_thread;
    unsigned long const hardware_threads = thread::hardware_concurrency();
    unsigned long const num_threads = min(hardware_threads != 0 ? hardware_threads : 2, max_threads);
    unsigned long const block_size = length / num_threads;

    vector<future<T>> futures(num_threads - 1);
    vector<thread> threads(num_threads - 1);

    Iterator block_start = first;
    T last_result;
    try {
        for (unsigned long i = 0; i < (num_threads - 1); ++i) {
            Iterator block_end = block_start;
            advance(block_end, block_size);
            //be careful about the ()!!!
            packaged_task<T(Iterator, Iterator)> task((accumulate_block<Iterator, T>()));
            futures[i] = task.get_future();
            threads[i] = thread(move(task), block_start, block_end);
            block_start = block_end;
        }
        last_result = accumulate_block<Iterator, T>()(block_start, last);

        for_each(threads.begin(), threads.end(), mem_fn(&thread::join));
    }
    catch (...) {
        for (unsigned i = 0; i < (num_threads - 1); ++i) {
            if (threads[i].joinable()) {
                threads[i].join();
            }
        }
    }

    T result = init;
    for (unsigned long i = 0; i < (num_threads - 1); ++i) {
        result += futures[i].get();
    }
    result += last_result;
    return result;
}
void parallel_accumulate_test();

//Listing 8.4 An exception-safe parallel version of accumulate
class join_threads {
    vector<thread>& threads;
public:
    explicit join_threads(vector<thread>& threads_) : threads(threads_) {}
    ~join_threads() {
        TICK();
        for (unsigned i = 0; i < threads.size(); ++i) {
            if (threads[i].joinable()) {
                threads[i].join();
            }
        }
    }
};
template<typename Iterator, typename T>
T parallel_accumulate_join(Iterator first, Iterator last, T init) {
    TICK();
    unsigned long const length = distance(first, last);
    if (!length) {
        return init;
    }
    unsigned long const min_per_thread = 25;
    unsigned long const max_threads = (length + min_per_thread - 1) / min_per_thread;
    unsigned long const hardware_threads = thread::hardware_concurrency();
    unsigned long const num_threads = min(hardware_threads != 0 ? hardware_threads : 2, max_threads);
    unsigned long const block_size = length / num_threads;

    vector<future<T>> futures(num_threads - 1);
    vector<thread> threads(num_threads - 1);
    join_threads joiner(threads);

    Iterator block_start = first;

    for (unsigned long i = 0; i < (num_threads - 1); ++i) {
        Iterator block_end = block_start;
        advance(block_end, block_size);
        //be careful about the ()!!!
        packaged_task<T(Iterator, Iterator)> task((accumulate_block<Iterator, T>()));
        futures[i] = task.get_future();
        threads[i] = thread(move(task), block_start, block_end);
        block_start = block_end;
    }
    T last_result = accumulate_block<Iterator, T>()(block_start, last);

    for_each(threads.begin(), threads.end(), mem_fn(&thread::join));

    T result = init;
    for (unsigned long i = 0; i < (num_threads - 1); ++i) {
        result += futures[i].get();
    }
    result += last_result;
    return result;
}
void parallel_accumulate_join_test();

//Listing 8.5 An exception-safe parallel version of accumulate using async
template<typename Iterator, typename T>
T parallel_accumulate_async(Iterator first, Iterator last, T init) {
    TICK();
    unsigned long const length = distance(first, last);
    unsigned long const max_chunk_size = 25;
    if (length <= max_chunk_size) {
        return accumulate(first, last, init);
    } else {
        Iterator mid_point = first;
        advance(mid_point, length / 2);
        future<T> first_half_result = async(&parallel_accumulate_async<Iterator, T>, first, mid_point, init);
        T second_half_result = parallel_accumulate_async(mid_point, last, T());
        INFO("second_half_result=%d", second_half_result);
        return first_half_result.get() + second_half_result;
    }
}
void parallel_accumulate_async_test();

//8.4.2 Scalability and Amdahl`s law

//8.4.3 Hiding latenct with multiple threads

//8.4.4 Improving responsiveness with concurrency
//Listing 8.6 Separating GUI thread from task thread
struct event_data {
    enum event_type {
        start_task,
        stop_task,
        task_complete,
        quit
    };
    event_type type;
    event_data() : type(start_task) {}
};
event_data get_event();
void process(event_data const& event);
void gui_thread();
bool task_complete();
void do_next_operation();
void perform_cleanup();
void post_gui_event(event_data::event_type const& type);
void task();

//Listing 8.7 A parallel version of for_each
template<typename Iterator, typename Func>
void parallel_for_each(Iterator first, Iterator last, Func f) {
    TICK();
    unsigned long const length = distance(first, last);
    if (!length) {
        return;
    }
    unsigned long const min_per_thread = 25;
    unsigned long const max_threads = (length + min_per_thread - 1) / min_per_thread;
    unsigned long const hardware_threads = thread::hardware_concurrency();
    unsigned long const num_threads = min(hardware_threads != 0 ? hardware_threads : 2, max_threads);
    unsigned long const block_size = length / num_threads;

    vector<future<void>> futures(num_threads - 1);
    vector<thread> threads(num_threads - 1);
    join_threads joiner(threads);

    Iterator block_start = first;
    for (unsigned long i = 0; i < (num_threads - 1); ++i) {
        Iterator block_end = block_start;
        advance(block_end, block_size);
        packaged_task<void(void)> task([=]() {for_each(block_start, block_end, f); });
        futures[i] = task.get_future();
        threads[i] = thread(move(task));
        block_start = block_end;
    }
    for_each(block_start, last, f);
    for (unsigned long i = 0; i < (num_threads - 1); ++i) {
        futures[i].get();
    }
}
void parallel_for_each_test();

//Listing 8.8 A parallel version of for_each using async
template<typename Iterator, typename Func>
void parallel_for_each_async(Iterator first, Iterator last, Func f) {
    TICK();
    unsigned long const length = distance(first, last);
    if (!length) {
        return;
    }
    unsigned long const min_per_thread = 25;
    if (length < (2 * min_per_thread)) {
        for_each(first, last, f);
    } else {
        Iterator const mid_point = first + length / 2;
        future<void> first_half = async(&parallel_for_each_async<Iterator, Func>, first, mid_point, f);
        parallel_for_each_async(mid_point, last, f);
        first_half.get();
    }
}
void parallel_for_each_async_test();

//8.5.2 A parallel implementation of find
//Listing 8.9 An implementation of a parallel find algorithm
template<typename Iterator, typename MatchType>
Iterator parallel_find(Iterator first, Iterator last, MatchType match) {
    struct find_element {
        void operator()(Iterator begin, Iterator end, MatchType match,
            promise<Iterator>* result, atomic<bool>* done_flag) {
            TICK();
            try {
                for (; (begin != end) && !done_flag->load(); ++begin) {
                    if (*begin == match) {
                        result->set_value(begin);
                        done_flag->store(true);
                        return;
                    }
                }
            } catch (...) {
                try {
                    result->set_exception(current_exception());
                    done_flag->store(true);
                } catch (...) {
                }
            }
        }
    };
    unsigned long const length = distance(first, last);
    if (!length) {
        return last;
    }
    unsigned long const min_per_thread = 25;
    unsigned long const max_threads = (length + min_per_thread - 1) / min_per_thread;
    unsigned long const hardware_threads = thread::hardware_concurrency();
    unsigned long const num_threads = min(hardware_threads != 0 ? hardware_threads : 2, max_threads);
    unsigned long const block_size = length / num_threads;

    promise<Iterator> result;
    atomic<bool> done_flag(false);
    vector<thread> threads(num_threads - 1);
    {
        join_threads joiner(threads);
        Iterator block_start = first;
        for (unsigned long i = 0; i < (num_threads - 1); ++i) {
            Iterator block_end = block_start;
            advance(block_end, block_size);
            threads[i] = thread(find_element(), block_start, block_end, match, &result, &done_flag);
            block_start = block_end;
        }
        find_element()(block_start, last, match, &result, &done_flag);
    }
    if (!done_flag.load()) {
        return last;
    }
    return result.get_future().get();
}
void parallel_find_test();

//Listing 8.10 An implementation of a parallel find algorithm using async
template<typename Iterator, typename MatchType>
Iterator parallel_find_async(Iterator first, Iterator last, MatchType match, atomic<bool>& done) {
    try {
        unsigned long const length = distance(first, last);
        unsigned long const min_per_thread = 25;
        if (length < (2 * min_per_thread)) {
            for (; (first != last) && !done.load(); ++first) {
                if (*first == match) {
                    done = true;
                    return first;
                }
            }
            return last;
        } else {
            Iterator const mid_point = first + (length / 2);
            future<Iterator> async_result = async(&parallel_find_async<Iterator, MatchType>,
                mid_point, last, match, ref(done));
            Iterator const direct_result = parallel_find_async(first, mid_point, match, done);
            return (direct_result == mid_point) ? async_result.get() : direct_result;
        }
    } catch (...) {
        done = true;
        throw;
    }
}
void parallel_find_async_test();

//8.5.3 A parallel implementation of partial_sum
//Listing 8.11 Calculating partial sums in parallel by dividing the problem
template<typename Iterator>
void parallel_partial_sum(Iterator first, Iterator last) {
    TICK();
    typedef typename Iterator::value_type value_type;
    struct process_chunk {
        void operator()(Iterator begin, Iterator last,
            future<value_type>* previous_end_value, promise<value_type>* end_value) {
            TICK();
            try {
                Iterator end = last;
                ++end;
                partial_sum(begin, end, begin);
                if (previous_end_value) {
                    value_type const& addend = previous_end_value->get();
                    *last += addend;
                    if (end_value) {
                        end_value->set_value(*last);
                    }
                    for_each(begin, last, [addend](value_type& item) {item += addend; });
                } else {
                    end_value->set_value(*last);
                }
            } catch (...) {
                if (end_value) {
                    end_value->set_exception(current_exception());
                } else {
                    throw;
                }
            }
        }
    };
    unsigned long const length = distance(first, last);
    if (!length) {
        return;
    }
    unsigned long const min_per_thread = 25;
    unsigned long const max_threads = (length + min_per_thread - 1) / min_per_thread;
    unsigned long const hardware_threads = thread::hardware_concurrency();
    unsigned long const num_threads = min(hardware_threads != 0 ? hardware_threads : 2, max_threads);
    unsigned long const block_size = length / num_threads;

    typedef typename Iterator::value_type value_type;
    vector<thread> threads(num_threads - 1);
    vector<promise<value_type>> end_value(num_threads - 1);
    vector<future<value_type>> previous_end_values;
    previous_end_values.reserve(num_threads - 1);

    join_threads joiner(threads);
    Iterator block_start = first;
    for (unsigned long i = 0; i < (num_threads - 1); ++i) {
        Iterator block_last = block_start;
        advance(block_last, block_size);
        threads[i] = thread(process_chunk(), block_start, block_last,
            (i != 0) ? &previous_end_values[i - 1] : 0, &end_value[i]);
        block_start = block_last;
        ++block_start;
        previous_end_values.push_back(end_value[i].get_future());
    }
    Iterator final_element = block_start;
    advance(final_element, distance(block_start, last) - 1);
    process_chunk()(block_start, final_element,
        (num_threads > 1) ? &previous_end_values.back() : 0, 0);
}
void parallel_partial_sum_test();

#if 0
//Listing 8.12 A simple barrier class
class barrier {
    unsigned const count;
    atomic<unsigned> spaces;
    atomic<unsigned> generation;
public:
    explicit barrier(unsigned count_) : count(count_), spaces(count), generation(0) {}
    void wait() {
        TICK();
        unsigned const my_generation = generation;
        if (!--spaces) {
            spaces = count;
            ++generation;
        } else {
            while (generation == my_generation) {
                this_thread::yield();
            }
        }
    }
};
#else
//Listing 8.13 A parallel implementation of partial_sum by pairwise updates
struct barrier {
    atomic<unsigned> count;
    atomic<unsigned> spaces;
    atomic<unsigned> generation;
    explicit barrier(unsigned count_) : count(count_), spaces(count_), generation(0) {}
    void wait() {
        unsigned const gen = generation.load();
        if (!--spaces) {
            spaces = count.load();
            ++generation;
        } else {
            while (generation.load() == gen) {
                yield();
            }
        }
    }
    void done_waiting() {
        TICK();
        --count;
        if (!--spaces) {
            spaces = count.load();
            ++generation;
        }
    }
};
#endif
template<typename Iterator>
void parallel_partial_sum_pairwise(Iterator first, Iterator last) {
    TICK();
    typedef typename Iterator::value_type value_type;
    struct process_element {
        void operator()(Iterator first, Iterator last, vector<value_type>& buffer, unsigned i, barrier& b) {
            TICK();
            value_type& ith_element = *(first + i);
            bool update_source = false;

            for (unsigned step = 0, stride = 1; stride <= i; ++step, stride *= 2) {
                value_type const& source = (step % 2) ? buffer[i] : ith_element;
                value_type &dest = (step % 2) ? ith_element : buffer[i];
                value_type const& addend = (step % 2) ? buffer[i - stride] : *(first + i - stride);

                dest = source + addend;
                update_source = !(step % 2);
                b.wait();
            }
            if (update_source) {
                ith_element = buffer[i];
            }
            b.done_waiting();
        }
    };
    unsigned long const length = distance(first, last);
    if (length <= 1) {
        return;
    }
    vector<value_type> buffer(length);
    barrier b(length);

    vector<thread> threads(length - 1);
    join_threads joiner(threads);

    Iterator block_start = first;
    for (unsigned long i = 0; i < (length - 1); ++i) {
        threads[i] = thread(process_element(), first, last, ref(buffer), i, ref(b));
    }
    process_element()(first, last, buffer, length - 1, b);
}
void parallel_partial_sum_pairwise_test();

}//namespace design_conc_code

#endif  //DESIGNING_CONCURRENT_CODE_H

