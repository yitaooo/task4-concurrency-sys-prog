#include <atomic>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "chashmap.h"
#define DEBUG 0

struct Node_HM_t {
    long m_val;  // value of the node
    char padding[PAD];
    std::atomic<struct Node_HM_t*>
        m_next;  // pointer to next node in the bucket
};

static std::mutex console_mutex;

// by Timothy M. Harris
static bool is_marked_reference(struct Node_HM_t* addr) {
    return (reinterpret_cast<unsigned long>(addr) & 1) == 1;
}

static struct Node_HM_t* get_unmarked_reference(struct Node_HM_t* addr) {
    return (struct Node_HM_t*)(reinterpret_cast<unsigned long>(addr) & ~1);
}

static struct Node_HM_t* get_marked_reference(struct Node_HM_t* addr) {
    return (struct Node_HM_t*)(reinterpret_cast<unsigned long>(addr) | 1);
}

static bool search(struct Node_HM_t* head, long val,
                   struct Node_HM_t** out_prev, struct Node_HM_t** out_curr,
                   int* out_flag = nullptr) {
try_again:
    do {
#if DEBUG
        std::stringstream ss = std::stringstream();
#endif
        Node_HM_t* prev = head;
        Node_HM_t* prev_next = prev->m_next;
        Node_HM_t* cur = prev->m_next;
        // first find the node with the value
        Node_HM_t* tmp = head;
        Node_HM_t* tmp_next = head->m_next;
#if DEBUG
        ss << "[remove] search for " << val << ": ";
#endif
        while (true) {
            if (!is_marked_reference(tmp_next)) {
                // tmp_next is not marked means node tmp is not deleted, record
                // it
                prev = tmp;
                prev_next = tmp_next;
            }
            // move to next node
            tmp = get_unmarked_reference(tmp_next);
            if (tmp == nullptr) {
                break;
            }
            tmp_next = tmp->m_next;
#if DEBUG
            ss << tmp->m_val << " ";
#endif
            if (tmp->m_val == val) {
                break;
            }
        }
        cur = tmp;
#if DEBUG
        ss << cur << std::endl;
#endif
        // std::cout << "found cur" << std::endl;

        if (prev_next == cur) {
            if (cur != nullptr && is_marked_reference(cur->m_next)) {
                // node cur is deleted by another thread
                // std::cout << "cur->m_next is marked" << std::endl;
                goto try_again;
            } else {
                *out_prev = prev;
                *out_curr = cur;
                if (out_flag != nullptr) {
                    *out_flag = 0;
                }
#if DEBUG
                console_mutex.lock();
                std::cout << ss.str();
                console_mutex.unlock();
#endif
                return cur != nullptr;
            }
        } else {
            // if there are dead nodes between prev and cur, try delete them
            if (!std::atomic_compare_exchange_strong(&prev->m_next, &prev_next,
                                                     cur)) {
                // if fail, try again
                // std::cout << "CAS failed" << std::endl;
                goto try_again;
            } else {
                if (cur != nullptr && is_marked_reference(cur->m_next)) {
                    // node cur is deleted by another thread
                    // std::cout << __LINE__<<": cur->m_next is marked" <<
                    // std::endl;
                    goto try_again;
                } else {
                    *out_prev = prev;
                    *out_curr = cur;
                    if (out_flag != nullptr) {
                        *out_flag = 1;
                    }
#if DEBUG
                    console_mutex.lock();
                    std::cout << ss.str();
                    console_mutex.unlock();
#endif
                    return cur != nullptr;
                }
            }
        }
    } while (true);
    // make compiler happy
    return false;
}

struct List_t {
    struct Node_HM_t* sentinel;  // list of nodes in a bucket
};

struct hm_t {
    struct List_t** buckets;  // list of buckets in the hashmap
    size_t n_buckets;
};

HM* alloc_hashmap(size_t n_buckets) {
    HM* hm = new HM();
    hm->buckets = new List*[n_buckets];
    hm->n_buckets = n_buckets;
    for (size_t i = 0; i < n_buckets; i++) {
        hm->buckets[i] = new List();
        hm->buckets[i]->sentinel = new Node_HM();
        hm->buckets[i]->sentinel->m_next = NULL;
    }
    return hm;
}

void free_hashmap(HM* hm) {
    for (size_t i = 0; i < hm->n_buckets; i++) {
        Node_HM* curr = hm->buckets[i]->sentinel;
        while (curr->m_next != NULL) {
            Node_HM* temp = curr->m_next;
            delete curr;
            curr = temp;
        }
        delete curr;
        delete hm->buckets[i];
    }
    delete[] hm->buckets;
    delete hm;
}

static size_t hash(long val, size_t n_buckets) { return val % n_buckets; }

int insert_item(HM* hm, long val) {
    size_t bucket_idx = hash(val, hm->n_buckets);
    Node_HM* new_node = new Node_HM();
    new_node->m_val = val;
try_again:
    Node_HM* head = hm->buckets[bucket_idx]->sentinel;
    Node_HM* first = head->m_next.load();
#if DEBUG
    std::stringstream ss = std::stringstream();
    ss << "head: " << head << ", first: " << first;
    if (first) {
        ss << ", first->m_next: " << first->m_next.load();
    }
    ss << std::endl;
#endif
    Node_HM* new_node_next = get_unmarked_reference(first);
    int skip = 0;
    while (new_node_next != NULL &&
           is_marked_reference(new_node_next->m_next)) {
        new_node_next = get_unmarked_reference(new_node_next->m_next);
        skip++;
    }

    new_node->m_next = new_node_next;
    // thread safe version of `head->m_next = new_node`
    if (std::atomic_compare_exchange_strong(&head->m_next, &first, new_node)) {
#if DEBUG
        console_mutex.lock();
        ss << "inserted " << val << " at " << new_node << ", with prev=" << head
           << "(" << head->m_val << ")"
           << ", next=" << new_node_next << "("
           << (new_node_next ? new_node_next->m_val : -1) << ")" << std::endl;
        fprintf(stdout, "%s", ss.str().c_str());
        console_mutex.unlock();
#endif
        return 0;
    } else {
        // if fail, try again
        goto try_again;
    }
}

int remove_item(HM* hm, long val) {
    // std::cout << "remove_item" << std::endl;
    size_t bucket_idx = hash(val, hm->n_buckets);
    Node_HM* first = hm->buckets[bucket_idx]->sentinel;

#if DEBUG
    std::stringstream ss = std::stringstream();
#endif
    Node_HM* curr;
    Node_HM* prev;
    Node_HM* curr_next;
    while (true) {  // retry until successfully mark `curr` as deleted
        int flag = -111;
        bool success = search(first, val, &prev, &curr, &flag);
#if DEBUG
        ss = std::stringstream();
        ss << "[remove] search: " << success << ", prev: " << prev
           << ", curr: " << curr << ", flag: " << flag;
#endif
        if (!success || curr->m_val != val) {
#if DEBUG
            std::cout << ss.str() << std::endl;
            std::cout << "remove_item: search result: " << success
                      << ", actual val: " << (curr ? curr->m_val : -1)
                      << ", expected: " << val << ", addr " << curr
                      << std::endl;
#endif
            return 1;
        }
        curr_next = curr->m_next;
        if (!is_marked_reference(curr_next)) {
            // mark node `curr` as deleted
            // thread safe version of `curr->m_next =
            // get_marked_reference(curr_next)`
            if (std::atomic_compare_exchange_strong(
                    &curr->m_next, &curr_next,
                    get_marked_reference(curr_next))) {
                // success, stop infinite retrying
                break;
            }
        }
    }

    // set `prev->m_next` to `curr_next`
    // try removing `curr` from the list directly
    bool success =
        std::atomic_compare_exchange_strong(&prev->m_next, &curr, curr_next);
    if (!success) {
        // if fail, perform a search operation to update
        Node_HM* curr_2;
        search(first, val, &prev, &curr_2);
    }
#if DEBUG
    ss << "\n[remove] removed " << val << " at " << curr
       << ", with prev=" << prev << "(" << prev->m_val << ")"
       << ", next=" << curr_next << "(" << (curr_next ? curr_next->m_val : -1)
       << ")";
    console_mutex.lock();
    std::cout << ss.str() << std::endl;
    console_mutex.unlock();
#endif
    // we cannot just simply delete `curr` here, because it might being used by
    // other threads.
    // there are some algorithms to delete them safely, but too complicated for
    // an assignment.
    // delete curr;
    return 0;
}

int lookup_item(HM* hm, long val) {
    size_t bucket_idx = hash(val, hm->n_buckets);
    Node_HM* first = hm->buckets[bucket_idx]->sentinel;
    Node_HM* curr;
    Node_HM* prev;
    bool success = search(first, val, &prev, &curr);
    return success ? 0 : 1;
}

void print_hashmap(HM* hm) {
    for (size_t i = 0; i < hm->n_buckets; i++) {
        std::cout << "Bucket " << i;
        Node_HM* curr = hm->buckets[i]->sentinel;
        while (curr->m_next != NULL) {
            std::cout << " - val"
                      << get_unmarked_reference(curr->m_next)->m_val;
            curr = get_unmarked_reference(curr->m_next);
        }
        std::cout << std::endl;
    }
}