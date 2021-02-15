#ifndef SRC_SINGLETHREADEDPAGERANKCOMPUTER_HPP_
#define SRC_SINGLETHREADEDPAGERANKCOMPUTER_HPP_

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "immutable/network.hpp"
#include "immutable/pageIdAndRank.hpp"
#include "immutable/pageRankComputer.hpp"

class SingleThreadedPageRankComputer : public PageRankComputer {
public:
    SingleThreadedPageRankComputer() {};

    std::vector<PageIdAndRank> computeForNetwork(Network const& network, double alpha,
                                                 uint32_t iterations, double tolerance) const
    {
        std::unordered_map<PageId, PageRank, PageIdHash> pageHashMap;
        size_t networkSize = network.getSize();
        for (auto const& page : network.getPages()) {
            page.generateId(network.getGenerator());
            pageHashMap[page.getId()] = 1.0 / networkSize;
        }

        std::unordered_map<PageId, uint32_t, PageIdHash> numLinks;
        for (auto page : network.getPages()) {
            numLinks[page.getId()] = page.getLinks().size();
        }

        std::unordered_set<PageId, PageIdHash> danglingNodes;
        for (auto page : network.getPages()) {
            if (page.getLinks().size() == 0) {
                danglingNodes.insert(page.getId());
            }
        }

        std::unordered_map<PageId, std::vector<PageId>, PageIdHash> edges;
        for (auto page : network.getPages()) {
            for (auto link : page.getLinks()) {
                edges[link].push_back(page.getId());
            }
        }

        for (uint32_t i = 0; i < iterations; ++i) {
            std::unordered_map<PageId, PageRank, PageIdHash> previousPageHashMap = pageHashMap;

            double dangleSum = 0;
            for (auto danglingNode : danglingNodes) {
                dangleSum += previousPageHashMap[danglingNode];
            }
            dangleSum = dangleSum * alpha;

            double danglingWeight = 1.0 / networkSize;
            double baseValue = dangleSum * danglingWeight + (1.0 - alpha) / networkSize;
            double difference = 0;

            for (auto& pageMapElem : pageHashMap) {
                PageId pageId = pageMapElem.first;

                pageMapElem.second = baseValue;

                if (edges.count(pageId) > 0) {
                    for (auto link : edges[pageId]) {
                        pageMapElem.second += alpha * previousPageHashMap[link] / numLinks[link];
                    }
                }
                difference += std::abs(previousPageHashMap[pageId] - pageMapElem.second);
            }


            if (difference < tolerance) {
                std::vector<PageIdAndRank> result;
                for (auto iter : pageHashMap) {
                    result.push_back(PageIdAndRank(iter.first, iter.second));
                }

                ASSERT(result.size() == networkSize, "Invalid result size=" << result.size() << ", for network" << network);

                return result;
            }
        }

        ASSERT(false, "Not able to find result in iterations=" << iterations);
    }

    std::string getName() const
    {
        return "SingleThreadedPageRankComputer";
    }
};

#endif /* SRC_SINGLETHREADEDPAGERANKCOMPUTER_HPP_ */
