#include "chashmap.h"
#include "cspinlock.h"
#include <iostream>
// implementation of a hashmap with spinlock

struct Node_HM_t{
    long m_val; //value of the node
    char padding[PAD];
    struct Node_HM_t* m_next; //pointer to next node in the bucket
};

struct List_t{
    struct Node_HM_t* sentinel; //list of nodes in a bucket
    cspinlock_t* lock;
};

struct hm_t{
    struct List_t** buckets; //list of buckets in the hashmap
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
        hm->buckets[i]->lock = cspin_alloc();
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
        cspin_free(hm->buckets[i]->lock);
        delete hm->buckets[i];
    }
    delete[] hm->buckets;
    delete hm;
}

static size_t hash(long val, size_t n_buckets) {
    return val % n_buckets;
}

int insert_item(HM* hm, long val) {
    size_t bucket_idx = hash(val, hm->n_buckets);
    // lock the bucket
    cspin_lock(hm->buckets[bucket_idx]->lock);
    Node_HM* curr = hm->buckets[bucket_idx]->sentinel;
    // insert at the head of the bucket
    Node_HM* new_node = new Node_HM();
    new_node->m_val = val;
    new_node->m_next = curr->m_next;
    curr->m_next = new_node;
    // unlock the bucket
    cspin_unlock(hm->buckets[bucket_idx]->lock);
    return 0;
}

int remove_item(HM* hm, long val) {
    size_t bucket_idx = hash(val, hm->n_buckets);
    // lock the bucket
    cspin_lock(hm->buckets[bucket_idx]->lock);
    Node_HM* curr = hm->buckets[bucket_idx]->sentinel;
    while (curr->m_next != NULL) {
        if (curr->m_next->m_val == val) {
            Node_HM* temp = curr->m_next;
            curr->m_next = curr->m_next->m_next;
            delete temp;
            // job is done, finish
            // unlock the bucket
            cspin_unlock(hm->buckets[bucket_idx]->lock);
            return 0;
        }
        curr = curr->m_next;
    }
    // unlock the bucket
    cspin_unlock(hm->buckets[bucket_idx]->lock);
    return 1;
}

int lookup_item(HM* hm, long val) {
    size_t bucket_idx = hash(val, hm->n_buckets);
    // lock the bucket
    cspin_lock(hm->buckets[bucket_idx]->lock);
    Node_HM* curr = hm->buckets[bucket_idx]->sentinel;
    while (curr->m_next != NULL) {
        if (curr->m_next->m_val == val) {
            // unlock the bucket
            cspin_unlock(hm->buckets[bucket_idx]->lock);
            return 0;
        }
        curr = curr->m_next;
    }
    // unlock the bucket
    cspin_unlock(hm->buckets[bucket_idx]->lock);
    return 1;
}

void print_hashmap(HM* hm) {
    for (size_t i = 0; i < hm->n_buckets; i++) {
        std::cout << "Bucket " << i;
        cspin_lock(hm->buckets[i]->lock);
        Node_HM* curr = hm->buckets[i]->sentinel;
        while (curr->m_next != NULL) {
            std::cout << " - val" << curr->m_next->m_val;
            curr = curr->m_next;
        }
        std::cout << std::endl;
        cspin_unlock(hm->buckets[i]->lock);
    }
}