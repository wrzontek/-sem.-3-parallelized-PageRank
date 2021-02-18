#ifndef SRC_SHA256IDGENERATOR_HPP_
#define SRC_SHA256IDGENERATOR_HPP_

#include <unistd.h>
#include <exception>
#include <thread>
#include <atomic>
#include "immutable/idGenerator.hpp"
#include "immutable/pageId.hpp"

#define HASH_LENGTH 64

class Sha256IdGenerator : public IdGenerator {
public:
    virtual PageId generateId(std::string const& content) const
    {
        char idBuffer[HASH_LENGTH + 1];

        auto myid = std::this_thread::get_id();
        std::stringstream ss;
        ss << myid;
        std::string threadId = ss.str();

        std::string tempFilename = "temp_sha256_" + threadId;

        FILE *contentFile = fopen(tempFilename.data(), "w");

        fputs(content.data(), contentFile);
        fclose(contentFile);

        std::string command = "sha256sum " + tempFilename;
        FILE *resultFile = popen(command.data(), "r");

        fgets(idBuffer, HASH_LENGTH + 1, resultFile);

        fclose(resultFile);

        command = "rm " + tempFilename;
        std::system(command.data());

        return PageId(std::string(idBuffer));
    }
};

#endif /* SRC_SHA256IDGENERATOR_HPP_ */
