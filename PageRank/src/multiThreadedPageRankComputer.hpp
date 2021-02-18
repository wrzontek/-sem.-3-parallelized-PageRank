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

static void joinAndClearThreads (std::vector<std::thread> &threads) {
    for (std::thread &thread : threads) {
        if (thread.joinable())
            thread.join();
    }
    threads.clear();
}

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

static void preparePages(const PageRankComputerState &state,
                         const std::vector<const Page*> &pagesToId, const IdGenerator &generator,
                         std::vector<PageId> &myManagedPages, std::vector<PageId> &myDanglingNodes,
                         std::mutex &mutex, const double startValue) {

    for (auto const &page : pagesToId) {
        page->generateId(generator);

        myManagedPages.push_back(page->getId());
        if (page->getLinks().size() == 0) {
            myDanglingNodes.push_back(page->getId());
        }

    }

    std::lock_guard<std::mutex> lock(mutex);
    for (auto const &page : pagesToId) {
        PageId pageId = page->getId();

        state.numLinks[pageId] = page->getLinks().size();

        for (auto link : page->getLinks()) {
            state.edges[link].push_back(pageId);
        }

        state.pageHashMap[pageId] = startValue;
    }
}

static void calculateDangleSum(std::unordered_map<PageId, PageRank, PageIdHash> &previousPageHashMap,
                               const std::vector<PageId> &danglingNodes, double &dangleSum, std::mutex &mutex) {
    double myDangleSum = 0;
    for (PageId const &danglingNode : danglingNodes) {
        myDangleSum += previousPageHashMap[danglingNode];
    }

    std::lock_guard<std::mutex> lock(mutex);
    dangleSum += myDangleSum;
}

static void calculatePageRank(const PageRankComputerState &state,
                              std::unordered_map<PageId, PageRank, PageIdHash> &previousPageHashMap,
                              const std::vector<PageId> &pages, std::mutex &mutex, double &difference) {
    double myDifference = 0;
    for (PageId const &pageId : pages) {
        auto pageMapElem = state.pageHashMap.find(pageId);
        pageMapElem->second = state.baseValue;

        if (state.edges.count(pageId) > 0) {
            for (auto link : state.edges[pageId]) {
                pageMapElem->second += state.alpha * previousPageHashMap[link] / state.numLinks[link];
            }
        }

        myDifference += std::abs(previousPageHashMap[pageId] - pageMapElem->second);
    }

    std::lock_guard<std::mutex> lock(mutex);
    difference += myDifference;
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
        std::vector<std::vector<const Page*>> managedPagesGenerateId; // also for generating ids for Pages

        managedPages.resize(numThreads);
        managedDanglingNodes.resize(numThreads);
        managedPagesGenerateId.resize(numThreads);

        std::unordered_map<PageId, PageRank, PageIdHash> pageHashMap;
        uint32_t manager = 0;
        auto &generator = network.getGenerator();
        for (auto &page : network.getPages()) {
            managedPagesGenerateId[manager].push_back(&page);
            manager = (manager + 1) % numThreads;
        }

        std::unordered_map<PageId, uint32_t, PageIdHash> numLinks;
        std::unordered_map<PageId, std::vector<PageId>, PageIdHash> edges;
        PageRankComputerState state = PageRankComputerState(pageHashMap, edges, numLinks, alpha);

        double startValue = 1.0 / networkSize;
        for (uint32_t t = 0; t < numThreads; t++) {
            std::vector<const Page*> &myPagesToID = managedPagesGenerateId[t];
            std::vector<PageId> &myPages = managedPages[t];
            std::vector<PageId> &myDanglingNodes = managedDanglingNodes[t];

            threads.push_back(std::thread{preparePages, std::ref(state),
                                          std::ref(myPagesToID), std::ref(generator),
                                          std::ref(myPages), std::ref(myDanglingNodes),
                                          std::ref(mutex), startValue});
        }

        joinAndClearThreads(threads);

        double danglingWeight = 1.0 / networkSize;
        double base = (1.0 - alpha) / networkSize;

        for (uint32_t i = 0; i < iterations; ++i) {
            std::unordered_map<PageId, PageRank, PageIdHash> previousPageHashMap = pageHashMap;

            double dangleSum = 0;

            for (uint32_t t = 0; t < numThreads; t++) {
                std::vector<PageId> &myDanglingNodes = managedDanglingNodes[t];

                threads.push_back(std::thread{calculateDangleSum,std::ref(previousPageHashMap),
                                              std::ref(myDanglingNodes), std::ref(dangleSum),
                                              std::ref(mutex)});
            }

            joinAndClearThreads(threads);

            dangleSum = dangleSum * alpha;

            double difference = 0;
            state.baseValue = dangleSum * danglingWeight + base;

            for (uint32_t t = 0; t < numThreads; t++) {
                std::vector<PageId> &myPages = managedPages[t];

                threads.push_back(std::thread{calculatePageRank, std::ref(state),
                                              std::ref(previousPageHashMap), std::ref(myPages),
                                              std::ref(mutex), std::ref(difference)});
            }

            joinAndClearThreads(threads);

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
};

#endif /* SRC_MULTITHREADEDPAGERANKCOMPUTER_HPP_ */
