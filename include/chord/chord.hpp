#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>

#include "stdint.h"
#include <string>
#include <utility>
#include <vector>
#include <tl/expected.hpp>
#include <mutex>
#include <set>

#include "network.hpp"

namespace chord::core {
    /**
     * @brief Configuration settings for the Chord node.
     */
    struct Config {
        uint64_t successorListSize = 1; ///< Size of the successor list
        uint64_t stabilizationInterval = 200; ///< Interval for stabilization in milliseconds
        uint64_t fingerFixingInterval = 200; ///< Interval for fixing fingers in milliseconds
        // uint64_t retryLimit = 3; ///< Number of retries for RPC calls
        // uint64_t initialRetryInterval = 100; ///< Initial interval for retries in milliseconds
    };

    /**
    * @brief Represents a server for a Chord node.
    */
    class NodeServer {
    public:
        static tl::expected<NodeServer, Error> create(
                const std::string &address,
                Config config,
                std::shared_ptr<ChordNetwork> network = createFullNetwork());

        ~NodeServer();

        [[nodiscard]] KeyId getId() const {
            std::lock_guard<std::mutex> lock(mtx_);
            return id_;
        }

        [[nodiscard]] std::string getAddress() const {
            std::lock_guard<std::mutex> lock(mtx_);
            return address_;
        }

        tl::expected<void, Error> start(std::optional<Node> entryPoint);

        tl::expected<void, Error> leave();

        tl::expected<Node, Error> lookup(KeyId id);

        [[nodiscard]] std::vector<Node> getSuccessorList() const {
            std::lock_guard<std::mutex> lock(mtx_);
            return successorList_;
        }

        using PredecessorLeaveCallback = std::function<void(Node)>;
        using SuccessorListChangeCallback = std::function<void(const std::vector<Node> &)>;

        void setPredecessorLeaveCallback(PredecessorLeaveCallback callback) {
            std::lock_guard<std::mutex> lock(mtx_);
            predecessorLeaveCallback_ = std::move(callback);
        }

        void setSuccessorListChangeCallback(SuccessorListChangeCallback callback) {
            std::lock_guard<std::mutex> lock(mtx_);
            successorListChangeCallback_ = std::move(callback);
        }

        [[nodiscard]] std::optional<Node> getPredecessor() const {
            std::lock_guard<std::mutex> lock(mtx_);
            return predecessor_;
        }

        [[nodiscard]] std::shared_ptr<ChordNetwork> getNetwork() const {
            std::lock_guard<std::mutex> lock(mtx_);
            return network_;
        }

        [[nodiscard]] ChordApplicationNetwork &getApplicationNetwork() {
            return *network_;
        }

    private:
        explicit NodeServer(std::string address, Config config, std::shared_ptr<ChordNetwork> network);


        mutable std::mutex mtx_;

        std::string address_;
        Config config_;
        std::shared_ptr<ChordNetwork> network_;
        KeyId id_;
        std::vector<Node> successorList_{};
        std::optional<Node> predecessor_{};
        PredecessorLeaveCallback predecessorLeaveCallback_;
        SuccessorListChangeCallback successorListChangeCallback_;

    };
} // namespace chord::core
