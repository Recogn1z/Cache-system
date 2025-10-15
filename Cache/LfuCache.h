#pragma once

#include<cmath>
#include<memory>
#include<mutex>
#include<unordered_map>
#include<vector>
#include<thread>

#include "CachePolicy.h"


namespace Cache{
    template<typename Key, typename Value> class LfuCache;
    template<typename Key, typename Value> class FreqList {
        private:
            struct Node{
                int freq;
                Key key;
                Value value;
                std::weak_ptr<Node> pre;
                std::shared_ptr<Node> next;

                Node():freq(1), next(nullptr){}
                Node(Key key, Value value): freq(1), key(key), value(value), next(nullptr){}
            };

            using NodePtr = std::shared_ptr<Node>;
            int freq_;
            NodePtr head_;
            NodePtr tail_;

        public:
            explicit FreqList(int n): freq_(n) {
                head_ = std::make_shared<Node>();
                tail_ = std::make_shared<Node>();
                head_->next = tail_;
                tail_->pre = head_;
            }

            bool isEmpty() const {
                return head_->next == tail_;
            }

            void addNode(NodePtr node) {
                if(!node || !head_ || !tail_) {
                    return;
                }
                node->pre = tail_->pre;
                node->next = tail_;
                tail_->pre.lock()->next = node;
                tail_->pre = node;
            }

            void removeNode(NodePtr node) {
                if(!node || !head_ || !tail_) {
                    return;
                }
                if(node->pre.expired() || !node->next) {
                    return;
                }

                auto pre = node->pre.lock();
                pre->next = node->next;
                node->next->pre = pre;
                node->next = nullptr;
            }

            NodePtr getFirstNode() const {return head_->next;}
            friend class LfuCache<Key, Value>;
    };


    template<typename Key, typename Value> class LfuCache : public CachePolicy<Key, Value> {
        public:
            using Node = typename FreqList<Key, Value>::Node;
            using NodePtr = std::shared_ptr<Node>;
            using NodeMap = std::unordered_map<Key, NodePtr>;

            LfuCache(int capacity, int maxAverageNum = 1000000): capacity_(capacity), minFreq_(INT8_MAX), maxAverageNum_(maxAverageNum), curAverageNum_(0),curTotalNum_(0) {}
            ~LfuCache() override = default;

            void put(Key key, Value value) override {
                if(capacity_ == 0) {
                    return;
                }
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = NodeMap_.find(key);
                if(it != NodeMap_.end()) {
                    it->second->value = value;
                    getInternal(it->second, value);
                    return;
                }
                putInternal(key, value);
            }

            bool get(Key key, Value& value) override {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = NodeMap_.find(key);
                if(it!= NodeMap_.end()) {
                    getInternal(it->second, value);
                    return true;
                }
                return false;
            }

            Value get(Key key) override {
                Value value;
                get(key, value);
                return value;
            }

            void purge(){
                NodeMap_.clear();
                freqToFreqList_.clear();
            }

        private:
            void putInternal(Key key, Value value);  // add cache
            void getInternal(NodePtr node, Value& value); // get cache
            
            void kickOut();  // move expired data

            void removeFromFreqList(NodePtr node);
            void addToFreqList(NodePtr node);

            void addFreqNum();   // add avg access freq
            void decreaseFreqNum(int num);  // decrease avg acess freq
            void handleOverMaxAverageNum(); // handle cur acess freq over upper bound
            void updateMinFreq();



        private:
            int capacity_;
            int minFreq_;
            int maxAverageNum_;
            int curAverageNum_;
            int curTotalNum_;
            std::mutex mutex_;
            NodeMap NodeMap_;
            std::unordered_map<int, FreqList<Key,Value>*> freqToFreqList_;

    };

    template<typename Key, typename Value> void LfuCache<Key, Value>::getInternal(NodePtr node, Value& value) {
        value = node->value;
        removeFromFreqList(node);
        node->freq++;
        addToFreqList(node);
        if(node->freq - 1 == minFreq_ && freqToFreqList_[node->freq -1]->isEmpty()) {
            minFreq_++;
        }
        addFreqNum();
    }

    template<typename Key, typename Value> void LfuCache<Key, Value>::putInternal(Key key, Value value) {
        // if not in cache, check if cache is full
        if(NodeMap_.size() == capacity_) {
            // if the cache is full, delete least freq used and update avg access and total access
            kickOut();
        }
        NodePtr node = std::make_shared<Node>(key, value);
        NodeMap_[key] = node;
        addToFreqList(node);
        addFreqNum();
        minFreq_ = std::min(minFreq_, 1);
    }

    template<typename Key, typename Value> void LfuCache<Key, Value>::kickOut() {
        NodePtr node = freqToFreqList_[minFreq_]->getFirstNode();
        removeFromFreqList(node);
        NodeMap_.erase(node->key);
        decreaseFreqNum(node->freq);
    }

    template<typename Key, typename Value> void LfuCache<Key, Value>::removeFromFreqList(NodePtr node) {
        if(!node) {
            return;
        }
        auto freq = node->freq;
        freqToFreqList_[freq]->removeNode(node);
    }

    template<typename Key, typename Value> void LfuCache<Key, Value>::addToFreqList(NodePtr node) {
        if(!node) {
            return;
        }

        //check if freqList exist before adding
        auto freq = node->freq;
        if(freqToFreqList_.find(node->freq) == freqToFreqList_.end()) {
            freqToFreqList_[node->freq] = new FreqList<Key, Value>(node->freq);
        }
        freqToFreqList_[freq]->addNode(node);
    }

    template<typename Key, typename Value> void LfuCache<Key, Value>::addFreqNum(){
        curTotalNum_++;
        if(NodeMap_.empty()){
            curAverageNum_ = 0;
        }
        else {
            curAverageNum_ = curTotalNum_ / NodeMap_.size();
        }
        if(curAverageNum_>maxAverageNum_){
            handleOverMaxAverageNum();
        }
    }

    template<typename Key, typename Value> void LfuCache<Key, Value>::decreaseFreqNum(int num) {
        curTotalNum_ -= num;
        if(NodeMap_.empty()) {
            curAverageNum_ = 0;
        }
        else {
            curAverageNum_ = curTotalNum_ / NodeMap_.size();
        }
    }

    template<typename Key, typename Value> void LfuCache<Key, Value>::handleOverMaxAverageNum() {
        if(NodeMap_.empty()) {
            return;
        }

        for(auto it = NodeMap_.begin(); it !=NodeMap_.end(); it++) {
            if(!it->second) {
                continue;
            }
            NodePtr node = it->second;
            removeFromFreqList(node);

            node->freq -= maxAverageNum_ /2;
            if(node->freq < 1) {
                node->freq = 1;
            }
            addToFreqList(node);
        }
        updateMinFreq();
    }

    template<typename Key, typename Value> void LfuCache<Key, Value>::updateMinFreq() {
        minFreq_ = INT8_MAX;
        for(const auto& pair : freqToFreqList_) {
            if(pair.second && !pair.second->isEmpty()) {
                minFreq_ = std::min(minFreq_, pair.first);
            }
        }
        if(minFreq_ == INT8_MAX) {
            minFreq_ =1;
        }
    }


    template<typename Key, typename Value> class HashLfuCache {
        public:
            HashLfuCache(size_t capacity, int sliceNum, int maxAverageNum = 10)
            : sliceNum_(sliceNum > 0 ? sliceNum : std::thread::hardware_concurrency())
            , capacity_(capacity){
                size_t sliceSize = std::ceil(capacity_ / static_cast<double>(sliceNum_)); 
                for (int i = 0; i < sliceNum_; i++)
                {
                    lfuSliceCaches_.emplace_back(new LfuCache<Key, Value>(sliceSize, maxAverageNum));
                }
            }


            void put(Key key, Value value)
            {

                size_t sliceIndex = Hash(key) % sliceNum_;
                lfuSliceCaches_[sliceIndex]->put(key, value);
            }

            bool get(Key key, Value& value)
            {

                size_t sliceIndex = Hash(key) % sliceNum_;
                return lfuSliceCaches_[sliceIndex]->get(key, value);
            }

            Value get(Key key)
            {
                Value value;
                get(key, value);
                return value;
            }

            void purge()
            {
                for (auto& lfuSliceCache : lfuSliceCaches_)
                {
                    lfuSliceCache->purge();
                }
            }

        private:
            size_t Hash(Key key)
            {
                std::hash<Key> hashFunc;
                return hashFunc(key);
            }

        private:
            size_t capacity_;
            int sliceNum_;
            std::vector<std::unique_ptr<LfuCache<Key,Value>>> lfuSliceCaches_;
    };
}