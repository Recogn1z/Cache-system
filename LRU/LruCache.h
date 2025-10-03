#pragma once

#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "CachePolicy.h"

namespace Cache{

    template<typename Key, typename Value> class LruCache;

    template<typename Key, typename Value> class LruNode {
        private:
            Key key_;
            Value value_;
            size_t accessCount_;
            std::weak_ptr<LruNode<Key, Value>> prev_;
            std::shared_ptr<LruNode<Key, Value>> next_;

        public:
            LruNode(Key key, Value value): key_(key), value_(value), accessCount_(1){}
            Key getKey() const { return key_; }
            Value getValue() const { return value_; }
            void setValue(const Value& value) { value_ = value; }
            size_t getAccessCount() const {return accessCount_;}
            void incrementAccessCount() {accessCount_++;}
            friend class LruCache<Key, Value>;
    };

    template<typename Key, typename Value> class LruCache : public CachePolicy<Key, Value> {
        public:
            using LruNodeType = LruNode<Key, Value>;
            using NodePtr = std::shared_ptr<LruNodeType>;
            using NodeMap = std::unordered_map<Key, NodePtr>;
            LruCache(int capacity): capacity_(capacity) {initializeList();}
            ~LruCache() override = default;

            void put(Key key, Value value) override {
                if(capacity_ <=0) {return;}
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = NodeMap_.find(key);
                if (it!= NodeMap_.end()) {
                    updateExistingNode(it->second, value);
                    return;
                }
                addNewNode(key, value);
            }

            bool get(Key key, Value& value) override {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = NodeMap_.find(key);
                if(it != NodeMap_.end()) {
                    moveToMostRecent(it->second);
                    value = it->second->getValue();
                    return true;
                }
                return false;
            }

            Value get(Key key) override {
                Value value{};
                get(key, value);
                return value;
            }

            void remove(Key key) {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = NodeMap_.find(key);
                if(it != NodeMap_.end()) {
                    removeNode(it->second);
                    NodeMap_.erase(it);
                }
            }

            private:
                void initializeList() {
                    dummyHead_ = std::make_shared<LruNodeType>(Key(), Value());
                    dummyTail_ = std::make_shared<LruNodeType>(Key(), Value());
                    dummyHead_->next_ = dummyTail_;
                    dummyTail_->prev_ = dummyHead_;
                }

                void updateExistingNode(NodePtr node, const Value& value) {
                    node->setValue(value);
                    moveToMostRecent(node);
                }

                void addNewNode(const Key&key, const Value& value) {
                    if(NodeMap_.size()>=capacity_) {
                        evictLeastRecent();
                    }

                    NodePtr newNode = std::make_shared<LruNodeType>(key, value);
                    insertNode(newNode);
                    NodeMap_[key] = newNode;
                }
                
                void moveToMostRecent(NodePtr node) {
                    removeNode(node);
                    insertNode(node);
                }

                void removeNode(NodePtr node) {
                    if(!node->prev_.expired() && node->next_) {
                        auto prev = node->prev_.lock();
                        prev->next_ = node->next_;
                        node->next_->prev_ = prev;
                        node->next_ = nullptr;
                    }
                }

                void insertNode(NodePtr node) {
                    node->next_ = dummyTail_;
                    node->prev_ = dummyTail_->prev_;
                    dummyTail_->prev_.lock()->next_ = node; 
                    dummyTail_->prev_ = node;
                }

                void evictLeastRecent() {
                    NodePtr leastRecent = dummyHead_->next_;
                    removeNode(leastRecent);
                    NodeMap_.erase(leastRecent->getKey());
                }

            private:
                int capacity_;
                NodeMap NodeMap_;
                std::mutex mutex_;
                NodePtr dummyHead_;
                NodePtr dummyTail_;
    };

    // k-lru
    template<typename Key, typename Value> class LruKCache : public LruCache<Key, Value> {
        public:
            LruKCache(int capacity, int historyCapacity, int k)
            : LruCache<Key, Value>(capacity)
            , historyList_(std::make_unique<LruCache<Key,size_t>>(historyCapacity))
            , k_(k){}

            Value get(Key key) {
                Value value{};
                bool inMainCache = LruCache<Key, Value>::get(key, value);

                size_t historyCount = historyList_->get(key);
                historyCount++;
                historyList_->put(key, historyCount);

                if(inMainCache) {return value;}
                if(historyCount>=k_) {
                    auto it = historyValueMap_.find(key);
                    if(it !=historyValueMap_.end()) {
                        Value storedValue = it->second;
                        historyList_->remove(key);
                        historyValueMap_.erase(it);
                        LruCache<Key, Value>::put(key, storedValue);
                        return storedValue;
                    }
                }
                return value;
            }

            void put(Key key, Value value) {
                Value existingValue{};
                bool inMainCache = LruCache<Key, Value>::get(key, existingValue);
                if(inMainCache) {
                    LruCache<Key, Value>::put(key, value);
                    return;
                }
                size_t historyCount = historyList_->get(key);
                historyCount++;
                historyList_->put(key, historyCount);

                historyValueMap_[key] = value;
                if(historyCount>=k_) {
                    historyList_->remove(key);
                    historyValueMap_.erase(key);
                    LruCache<Key, Value>::put(key, value)
                }
            }

        private:
            int k_;
            std::unique_ptr<LruCache<Key, size_t>> historyList_;
            std::unordered_map<Key, Value> historyValueMap_;
    };

    template<typename Key, typename Value> class HashLruCaches {
        public:
            HashLruCaches(size_t capacity, int sliceNum)
            : capacity_(capacity)
            , sliceNum_(sliceNum > 0 ? sliceNum : std::thread::hardware_concurrency()){
                size_t sliceSize = std::ceil(capacity / static_cast<double>(sliceNum_));
                for(int i = 0; i < sliceNum_; i++) {
                    lruSliceCaches_.emplace_back(new LruCache<Key, Value>(sliceSize));
                }
            }
        
            void put(Key key, Value value) {
                size_t sliceIndex = Hash(key)% sliceNum_;
                lruSliceCaches_[sliceIndex]->put(key, value);
            }

            bool get(Key key, Value& value) {
                size_t sliceIndex = Hash(key)% sliceNum_;
                return lruSliceCaches_[sliceIndex]->get(key, value);
            }

            Value get(Key key) {
                Value value;
                memset(&value, 0 , sizeof(value));
                get(key, value);
                return value;
            }

        private:
            size_t capacity_;
            int sliceNum_;
            std::vector<std::unique_ptr<LruCache<Key,Value>>> lruSliceCaches_;
    };
}