#ifndef SRC_SHA256IDGENERATOR_HPP_
#define SRC_SHA256IDGENERATOR_HPP_

#include <unistd.h>
#include <exception>
#include "immutable/idGenerator.hpp"
#include "immutable/pageId.hpp"

#define HASH_LENGTH 64

class Sha256IdGenerator : public IdGenerator {
public:
    virtual PageId generateId(std::string const& content) const
    {
        char id_buffer[HASH_LENGTH + 1];
        FILE *content_file = fopen("temp_content", "w");

        fputs(content.data(), content_file);
        fclose(content_file);

        FILE *result_file = popen("sha256sum temp_content", "r");

        fgets(id_buffer, HASH_LENGTH + 1, result_file);

        fclose(result_file);
        std::system("rm temp_content");

        return PageId(std::string(id_buffer));
    }
};

#endif /* SRC_SHA256IDGENERATOR_HPP_ */
