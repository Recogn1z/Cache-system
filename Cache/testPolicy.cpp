#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <random>
#include <iomanip>
#include <chrono>

#include "CachePolicy.h"
#include "LruCache.h"
#include "LfuCache.h"

class Timer {
    public:
        Timer() : start_(std::chrono::high_resolution_clock::now()){}

        double elapsed() {
            auto now = std::chrono::high_resolution_clock::now();
            return std::chrono::duration_cast<std::chrono::milliseconds>(now - start_).count();
        }
    private:
        std::chrono::time_point<std::chrono::high_resolution_clock> start_;
};

void printResults(const std::string& testName, int capacity, const std::vector<int>& get_operations, const std::vector<int>& hits) {
    std::cout<< "--- " <<testName << "result ---" <<std::endl;
    std::cout<< "cache size: " << capacity <<std::endl;

    std::vector<std::string> names;
    names = {"LRU", "LFU", "KLRU", "LFU Aging"};
    for(size_t i = 0; i< hits.size(); i++) {
        double hitRate = 100.0 * hits[i] / get_operations[i];
        std::cout<< (i < names.size()? names[i]: "Algorithm " + std::to_string(i+1)) << " - hit rate: " << std::fixed << std::setprecision(2) << hitRate << "%";
        std::cout<< "(" << hits[i] << "/" << get_operations[i] << ")" <<std::endl;
    }
    std::cout << std::endl;

}

void testHotDataAccess() {
    std::cout << "\n--- test1: HotDataAccess ---" << std::endl;

    const int CAPACITY = 20;  //cache capacity
    const int OPERATIONS = 500000;  //total operation times
    const int HOT_KEYS = 20;       // hot data number
    const int COLD_KEYS = 5000;    //cold data number

    Cache::LruCache<int, std::string> lru(CAPACITY);
    Cache::LfuCache<int, std::string> lfu(CAPACITY);
    Cache::LruKCache<int, std::string> klru(CAPACITY, HOT_KEYS + COLD_KEYS, 2);
    Cache::LfuCache<int, std::string> lfuAging(CAPACITY, 20000);

    std::random_device rd;
    std::mt19937 gen(rd());
    
    std::vector<Cache::CachePolicy<int, std::string>*> caches = {&lru, &lfu, &klru, &lfuAging};
    std::vector<int> hits(4, 0);
    std::vector<int> get_operations(4, 0);
    std::vector<std::string> names = {"LRU", "LFU", "KLRU", "LFU Aging"};

    for(int i = 0; i < caches.size(); i++) {
        for(int key = 0; key< HOT_KEYS; key++) {
            std::string value = "value" + std::to_string(key);
            caches[i]->put(key, value);
        }

        for(int op = 0; op < OPERATIONS; op++) {
            bool isPut = (gen() % 100 < 30);
            int key;

            // 70% access hotdata, 30% access colddata
            if(gen() % 100 < 70) {
                key = gen() % HOT_KEYS;
            }
            else {
                key = HOT_KEYS + (gen()% COLD_KEYS);
            }

            if(isPut) {
                std::string value = "value" +  std::to_string(key) + "_v" + std::to_string(op % 100);
                caches[i]->put(key, value);
            }
            else {
                std::string result;
                get_operations[i]++;
                if(caches[i]->get(key, result)) {
                    hits[i]++;
                }
            }
        }
    }
    printResults("HotDataAccessTest", CAPACITY, get_operations, hits);
}


void testLoopPattern() {
    std::cout<< "\n--- test2: LoopPatternTest ---" << std::endl;

    const int CAPACITY = 50;
    const int LOOP_SIZE = 500;
    const int OPERATIONS = 200000;

    Cache::LruCache<int, std::string> lru(CAPACITY);
    Cache::LfuCache<int, std::string> lfu(CAPACITY);
    Cache::LruKCache<int, std::string> klru(CAPACITY, LOOP_SIZE * 2, 2);
    Cache::LfuCache<int, std::string> lfuAging(CAPACITY, 3000);

    std::vector<Cache::CachePolicy<int, std::string>*> caches = {&lru, &lfu, &klru, &lfuAging};
    std::vector<int> hits(4, 0);
    std::vector<int> get_operations(4, 0);
    std::vector<std::string> names = {"LRU", "LFU", "KLRU", "LFU Aging"};

    std::random_device rd;
    std::mt19937 gen(rd());

    for(int i =0; i < caches.size(); i++) {
        for(int key = 0; key < LOOP_SIZE / 5; key++) {
            std::string value = "loop" + std::to_string(key);
            caches[i]->put(key, value);
        }
        int current_pos = 0;
        // alternate read and write to simulate real operations
        for(int op = 0; op < OPERATIONS; op++) {
            // set 20% to write, 80% to read, cuz read is usually more than read in real world
            bool isPut = (gen() % 100 < 20);
            int key;

            if(op % 100 < 60) {
                key = current_pos;
                current_pos = (current_pos + 1) % LOOP_SIZE;
            }
            else if(op % 100 < 90) {
                key = gen() % LOOP_SIZE;
            }
            else {
                key = LOOP_SIZE + (gen() % LOOP_SIZE);
            }

            if(isPut) {
                std::string value = "loop" + std::to_string(key) + "_v" +std::to_string(op % 100);
                caches[i]->put(key, value);
            }
            else {
                std::string result;
                get_operations[i]++;
                if(caches[i]->get(key, result)) {
                    hits[i]++;
                }
            }

        }
    }
    printResults("LoopPatternTest", CAPACITY, get_operations, hits);

}

int main() {
    testHotDataAccess();
    testLoopPattern();
    //testWorkloadShift();
    return 0;
}