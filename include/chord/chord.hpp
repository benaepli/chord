#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>

#include "stdint.h"
#include <string>
#include <vector>
#include <tl/expected.hpp>

#include "network.hpp"

namespace chord::core
{
    /**
     * @brief Configuration settings for the Chord node.
     */
    struct Config
    {
        uint64_t successorListSize = 1; ///< Size of the successor list
        uint64_t stabilizationInterval = 200; ///< Interval for stabilization in milliseconds
        uint64_t fingerFixingInterval = 200; ///< Interval for fixing fingers in milliseconds
        uint64_t retryLimit = 3; ///< Number of retries for RPC calls
        uint64_t initialRetryInterval = 100; ///< Initial interval for retries in milliseconds
    };

    /**
    * @brief Represents a server for a Chord node.
    */
    class NodeServer
    {
    public:
        explicit NodeServer(std::string address, Config config, std::shared_ptr<ChordNetwork> network);
        ~NodeServer();

        [[nodiscard]] NodeId getId() const;
        [[nodiscard]] std::string getAddress() const;

        tl::expected<void, Error> start(std::optional<Node> entryPoint);
        tl::expected<void, Error> leave();

        tl::expected<Node, Error> lookup(KeyId id);

        [[nodiscard]] Node getSuccessor() const;
        [[nodiscard]] std::vector<Node> getSuccessorList() const;
        [[nodiscard]] KeyRange getKeyRange() const;

        using PredecessorLeaveCallback = std::function<void(Node)>;
        using SuccessorListChangeCallback = std::function<void(const std::vector<Node>&)>;

        void setPredecessorLeaveCallback(PredecessorLeaveCallback callback);
        void setSuccessorListChangeCallback(SuccessorListChangeCallback callback);

        [[nodiscard]] ChordApplicationNetwork& getApplicationNetwork();
    };
} // namespace chord::core
