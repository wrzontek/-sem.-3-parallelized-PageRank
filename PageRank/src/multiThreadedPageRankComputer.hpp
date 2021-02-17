#ifndef SRC_MULTITHREADEDPAGERANKCOMPUTER_HPP_
#define SRC_MULTITHREADEDPAGERANKCOMPUTER_HPP_

#include <atomic>
#include <mutex>
#include <thread>

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "immutable/network.hpp"
#include "immutable/pageIdAndRank.hpp"
#include "immutable/pageRankComputer.hpp"


struct PageRankComputerState {
    PageRankComputerState(std::unordered_map<PageId, PageRank, PageIdHash> &pageHashMap,
                          std::unordered_map<PageId, std::vector<PageId>, PageIdHash> &edges,
                          std::unordered_map<PageId, uint32_t, PageIdHash> &numLinks,
                          double alpha):
            pageHashMap(pageHashMap), edges(edges), numLinks(numLinks),
            alpha(alpha), baseValue(0)
    {}

    std::unordered_map<PageId, PageRank, PageIdHash> &pageHashMap;
    std::unordered_map<PageId, std::vector<PageId>, PageIdHash> &edges;
    std::unordered_map<PageId, uint32_t, PageIdHash> &numLinks;
    double alpha;
    double baseValue;
};

static void calculateDangleSum (std::unordered_map<PageId, PageRank, PageIdHash> &previousPageHashMap,
                                std::vector<PageId> &danglingNodes, double &dangleSum, std::mutex &mutex) {
    double dSum = 0;
    for (PageId &danglingNode : danglingNodes) {
        dSum += previousPageHashMap[danglingNode];
    }

    std::lock_guard<std::mutex> lock(mutex);
    dangleSum += dSum;
}

static void calculatePageRank (PageRankComputerState &state,
                               std::unordered_map<PageId, PageRank, PageIdHash> &previousPageHashMap,
                               std::vector<PageId> &pages, std::mutex &mutex, double &difference) {
    double dif = 0;
    for (PageId &pageId : pages) {
        auto pageMapElem = state.pageHashMap.find(pageId);
        pageMapElem->second = state.baseValue;

        if (state.edges.count(pageId) > 0) {
            for (auto link : state.edges[pageId]) {
                pageMapElem->second += state.alpha * previousPageHashMap[link] / state.numLinks[link];
            }
        }

        dif += std::abs(previousPageHashMap[pageId] - pageMapElem->second);
    }

    std::lock_guard<std::mutex> lock(mutex);
    difference += dif;
}

class MultiThreadedPageRankComputer : public PageRankComputer {
public:
    MultiThreadedPageRankComputer(uint32_t numThreadsArg)
        : numThreads(numThreadsArg) {};

    std::vector<PageIdAndRank> computeForNetwork(Network const& network, double alpha,
                                                 uint32_t iterations, double tolerance) const
    {
        size_t networkSize = network.getSize();
        std::vector<std::thread> threads;
        std::vector<std::vector<PageId>> managedPages; // each helper thread will have a vector of Pages that it manages
        std::vector<std::vector<PageId>> managedDanglingNodes; // same for dangling nodes

        managedPages.resize(numThreads);
        managedDanglingNodes.resize(numThreads);

        std::unordered_map<PageId, PageRank, PageIdHash> pageHashMap;
        uint32_t manager = 0;
        for (auto const& page : network.getPages()) {
            page.generateId(network.getGenerator()); //todo współbieżnie
            pageHashMap[page.getId()] = 1.0 / networkSize;

            managedPages[manager].push_back(page.getId());
            manager = (manager + 1) % numThreads;
        }

        std::unordered_map<PageId, uint32_t, PageIdHash> numLinks;
        for (auto page : network.getPages()) {
            numLinks[page.getId()] = page.getLinks().size();
        }

        manager = 0;
        std::unordered_set<PageId, PageIdHash> danglingNodes;
        for (auto page : network.getPages()) {
            if (page.getLinks().size() == 0) {
                danglingNodes.insert(page.getId());
                managedDanglingNodes[manager].push_back(page.getId());
                manager = (manager + 1) % numThreads;
            }
        }

        std::unordered_map<PageId, std::vector<PageId>, PageIdHash> edges;
        for (auto page : network.getPages()) {
            for (auto link : page.getLinks()) {
                edges[link].push_back(page.getId());
            }
        }

        double danglingWeight = 1.0 / networkSize;
        double base = (1.0 - alpha) / networkSize;

        PageRankComputerState state = PageRankComputerState(pageHashMap, edges, numLinks, alpha);

        for (uint32_t i = 0; i < iterations; ++i) {
            std::unordered_map<PageId, PageRank, PageIdHash> previousPageHashMap = pageHashMap;

            double dangleSum = 0;

            for (uint32_t t = 0; t < numThreads; t++) {
                std::vector<PageId> &myDanglingNodes = managedDanglingNodes[t];

                threads.push_back(std::thread{calculateDangleSum,std::ref(previousPageHashMap),
                                              std::ref(myDanglingNodes), std::ref(dangleSum),
                                              std::ref(mutex)});
            }

            for (std::thread &thread : threads) {
                if (thread.joinable())
                    thread.join();
            }
            threads.clear();

            dangleSum = dangleSum * alpha;

            double difference = 0;
            state.baseValue = dangleSum * danglingWeight + base;

            for (uint32_t t = 0; t < numThreads; t++) {
                std::vector<PageId> &myPages = managedPages[t];

                threads.push_back(std::thread{calculatePageRank, std::ref(state),
                                              std::ref(previousPageHashMap), std::ref(myPages),
                                              std::ref(mutex), std::ref(difference)});
            }

            for (std::thread &thread : threads) {
                if (thread.joinable())
                    thread.join();
            }
            threads.clear();

            if (difference < tolerance) {
                std::vector<PageIdAndRank> result;
                for (auto iter : pageHashMap) {
                    result.push_back(PageIdAndRank(iter.first, iter.second));
                }

                ASSERT(result.size() == network.getSize(), "Invalid result size=" << result.size() << ", for network" << network);

                return result;
            }
        }

        ASSERT(false, "Not able to find result in iterations=" << iterations);
    }

    std::string getName() const
    {
        return "MultiThreadedPageRankComputer[" + std::to_string(this->numThreads) + "]";
    }

    //todo destruktor moze

private:
    uint32_t numThreads;
    mutable std::mutex mutex;

    void test() const {
        std::cout << std::this_thread::get_id() << std::endl;
    }


};

#endif /* SRC_MULTITHREADEDPAGERANKCOMPUTER_HPP_ */
