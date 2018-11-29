///    Copyright (C) 2018 DG.C, DGCHOW, deguangchow
///        deguangchow@qq.com
///
///    \brief    chapter3: Sharing data between threads.
///
///    \author   deguangchow
///    \version  1.0
///    \2018/11/26
#include "stdafx.h"
#include "thread_sharing_data.h"

namespace thread_sharing_data {

std::list<int> some_list;
std::mutex some_mutex;

void add_to_list(int new_value) {
    TICK();
    std::lock_guard<std::mutex> guard(some_mutex);
    some_list.push_back(new_value);
}

bool list_contains(int value_to_find) {
    TICK();
    std::lock_guard<std::mutex> gurad(some_mutex);
    return std::find(some_list.begin(), some_list.end(), value_to_find) != some_list.end();
}

some_data *unprotected;
void malicious_function(some_data &protected_data) {
    TICK();
    unprotected = &protected_data;
}
data_wrapper x;
void foo() {
    TICK();
    x.process_data(malicious_function); //Pass in a malicious function
    unprotected->do_something();        //Unprotected access to protected data
}

void do_something(int val) {
    TICK();
}

void stack_test() {
    TICK();
    stack<int> s;
    s.push(1);
    s.push(2);
    s.push(3);
    while (!s.empty()) {
        int const value = s.top();
        s.pop();
        do_something(value);
    }
}

thread_safe_stack<int> s;
void thread_safe_stack_test() {
    TICK();

    unsigned const push_thread_num = THREAD_NUM_128 - 1;
    unsigned const pop_thread_num = THREAD_NUM_128;

    std::vector<std::thread> push_threads(push_thread_num);
    std::vector<std::thread> pop_threads(pop_thread_num);

    for (unsigned i = 0; i < push_thread_num; ++i) {
        push_threads[i] = std::thread(&thread_safe_stack<int>::push, &s, i);
    }
    for (unsigned i = 0; i < pop_thread_num; ++i) {
        pop_threads[i] = std::thread(&thread_safe_stack<int>::pop, &s);
    }

    for (unsigned i = 0; i < push_thread_num; ++i) {
        push_threads[i].join();
    }
    for (unsigned i = 0; i < pop_thread_num; ++i) {
        pop_threads[i].join();  //When the num of threads to pop data from the stack is more than to push,
                                //there must be an "empty_stack" assert!
    }
}

void std_lock_test() {
    TICK();
    X<int> x1(some_big_object<int>(1));
    X<int> x2(some_big_object<int>(2));
    swap(x1, x2);
}

//3.2.5 Further guidelines for avoiding deadlock
//Listing 3.7 Using a lock hierarchy to prevent deadlock
thread_local unsigned long hierarchical_mutex::this_thread_hierarchy_value(ULONG_MAX);
hierarchical_mutex high_level_mutex(TEN_THOUSAND);
hierarchical_mutex low_level_mutex(THOUSAND * 5);
hierarchical_mutex other_mutex(HUNDRED);

int do_low_level_stuff() {
    TICK();
    return 0;
}

int low_level_func() {
    TICK();
    std::lock_guard<hierarchical_mutex> lk(low_level_mutex);
    return do_low_level_stuff();
}

void do_high_level_stuff(int some_param) {
    TICK();
}

void high_level_func() {
    TICK();
    std::lock_guard<hierarchical_mutex> lk(high_level_mutex);
    return do_high_level_stuff(42);
}

void thread_a() {
    TICK();
    high_level_func();
}

void do_other_stuff() {
    TICK();
}

void other_stuff() {
    TICK();
    high_level_func();//it`s thus violating the hierarchy: high_level_func() tries to acquire the high_level_mutex
    do_other_stuff();
}

void thread_b() {
    TICK();
    std::lock_guard<hierarchical_mutex> lk(other_mutex);
    other_stuff();
}

void hierarchical_mutex_test() {
    TICK();
    std::thread t1(thread_a);
    std::thread t2(thread_b);
    t1.join();
    t2.join();
}

void std_lock_ex_test() {
    TICK();
    X_EX<int> x1(some_big_object<int>(1));
    X_EX<int> x2(some_big_object<int>(2));
    swap(x1, x2);
}

void prepare_data() {
    TICK();
}

std::unique_lock<std::mutex> get_lock() {
    TICK();
    extern std::mutex some_mutex;
    std::unique_lock<std::mutex> lk(some_mutex);
    prepare_data();
    return lk;
}


void do_something() {
    TICK();
}

void process_data() {
    TICK();
    std::unique_lock<std::mutex> lk(get_lock());
    do_something();
}

//3.2.8 Locking at an appropriate granularity
std::mutex the_mutex;
thread_sharing_data::some_class get_next_data_chunk() {
    TICK();
    return some_class();
}

thread_sharing_data::result_type process(some_class data) {
    TICK();
    return 0;
}

void write_result(some_class const& data, result_type &result) {
    TICK();
}

void get_and_process_data() {
    TICK();

    std::unique_lock<std::mutex> my_lock(the_mutex);
    some_class data_to_process = get_next_data_chunk();

    INFO("Don`t need mutex locked across call to process()");
    my_lock.unlock();
    result_type result = process(data_to_process);

    INFO("Relock mutex to write result");
    my_lock.lock();
    write_result(data_to_process, result);
}

void compare_operator_test() {
    TICK();
    int i1 = 1;
    int i2 = 2;
    Y<int> y1(i1);
    Y<int> y2(i2);
    INFO("%d %s %d", i1, y1 == y2 ? "==" : "!=", i2);
}

//3.3 Alternative facilities for protecting shared data
//3.3.1 Protecting shared data during initialization
std::shared_ptr<some_resource> resource_ptr = nullptr;
void RAII_test() {
    TICK();
    if (!resource_ptr) {
        resource_ptr.reset(new some_resource);
    }
    resource_ptr->do_something();
}

std::mutex resource_mutex;
void RAII_lock_test() {
    TICK();
    std::unique_lock<std::mutex> lk(resource_mutex);    //All threads are serialized here
    if (!resource_ptr) {
        resource_ptr.reset(new some_resource);  //Only the initialization needs protection
    }
    lk.unlock();
    resource_ptr->do_something();
}

void undefined_behaviour_with_double_checked_locking() {
    TICK();
    if (!resource_ptr) {
        std::lock_guard<std::mutex> lk(resource_mutex);
        if (!resource_ptr) {
            resource_ptr.reset(new some_resource);
        }
    }
    resource_ptr->do_something();
}

std::once_flag resource_flag;
void init_resource() {
    TICK();
    INFO("Initialization is called exactly once");
    resource_ptr.reset(new some_resource);
}
void once_flag_test() {
    TICK();
    std::call_once(resource_flag, init_resource);   //Initialization is called exactly once
}

void call_once_test() {
    TICK();
    unsigned const &uNumThread = THREAD_NUM_8;
    std::vector<std::thread> vctThreads(uNumThread);
    for (unsigned i = 0; i < uNumThread; ++i) {
        vctThreads[i] = std::thread(once_flag_test);
    }
    for (unsigned i = 0; i < uNumThread; ++i) {
        vctThreads[i].join();
    }
}

void Connection_call_once_test() {
    TICK();
    connection_info conn_info;
    Connection conn(conn_info);
    data_packet data1;
    data_packet data2;
    conn.send_data(data1);
    conn.send_data(data2);
    data_packet const &data3 = conn.receive_data();
    data_packet const &data4 = conn.receive_data();
}

void Connection_concurrency_call_once_test() {
    TICK();
    connection_info conn_info;
    Connection conn(conn_info);
    data_packet data1;
    data_packet data2;
    std::thread t1(&Connection::send_data, &conn, data1);
    std::thread t2(&Connection::send_data, &conn, data2);
    std::thread t3(&Connection::receive_data, &conn);
    std::thread t4(&Connection::receive_data, &conn);
    t1.join();
    t2.join();
    t3.join();
    t4.join();
}

my_class& get_my_class_instance() {
    static my_class instance;   //Initialization guaranteed to be thread-safe
    return instance;
}

}//namespace thread_sharing_data


