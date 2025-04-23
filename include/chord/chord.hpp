#pragma once

#include <array>
#include <condition_variable>
#include <functional>
#include <memory>
#include <optional>

#include "stdint.h"
#include <stop_token>
#include <string>
#include <utility>
#include <vector>
#include <tl/expected.hpp>
#include <mutex>
#include <set>
#include <thread>
#include <bits/random.h>

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

    class NodeServerCallbacksImpl;

    /**
    * @brief Represents a server for a Chord node.
    */
    class NodeServer final {
    public:
        static tl::expected<std::unique_ptr<NodeServer>, Error> create(
            const std::string &address,
            Config config,
            std::shared_ptr<ChordNetwork> network = createFullNetwork());

        ~NodeServer();

        NodeServer(NodeServer const &) = delete;

        NodeServer(NodeServer &&) = delete;


        [[nodiscard]] KeyId getId() const {
            std::lock_guard lock(mtx_);
            return id_;
        }

        [[nodiscard]] std::string getAddress() const {
            std::lock_guard lock(mtx_);
            return address_;
        }

        tl::expected<void, Error> start(std::optional<Node> entryPoint);

        tl::expected<void, Error> leave();

        tl::expected<Node, Error> lookup(KeyId id);

        [[nodiscard]] std::vector<Node> getSuccessorList() const {
            std::lock_guard lock(mtx_);
            return successorList_;
        }

        using PredecessorLeaveCallback = std::function<void(Node)>;
        using SuccessorListChangeCallback = std::function<void(const std::vector<Node> &)>;

        void setPredecessorLeaveCallback(PredecessorLeaveCallback callback) {
            std::lock_guard lock(mtx_);
            predecessorLeaveCallback_ = std::move(callback);
        }

        void setSuccessorListChangeCallback(SuccessorListChangeCallback callback) {
            std::lock_guard lock(mtx_);
            successorListChangeCallback_ = std::move(callback);
        }

        [[nodiscard]] std::optional<Node> getPredecessor() const {
            std::lock_guard lock(mtx_);
            return predecessor_;
        }

        [[nodiscard]] std::shared_ptr<ChordNetwork> getNetwork() const {
            std::lock_guard lock(mtx_);
            return network_;
        }

        [[nodiscard]] ChordApplicationNetwork &getApplicationNetwork() const {
            return *network_;
        }

    private:
        explicit NodeServer(std::string address, Config config, std::shared_ptr<ChordNetwork> network);

        tl::expected<void, Error> initFingerTable(const Node &entryPoint);

        std::vector<Node> getSuccessorList();

        FindSuccessorReply findSuccessor(KeyId id);

        std::optional<Node> getPredecessor();

        void notify(const Node &node);

        void updateFingerTable(int index, const Node &node);

        void predecessorLeave(const Node &node);

        /**
         * NOTE: not thread safe
         * @param keyId id of the key for which we want to find the closest preceding finger
         * @param ignoreId specify this if we want to find a finger with an id not equal to ignoreId
         */
        [[nodiscard]] Node closestPrecedingFinger(KeyId keyId, std::optional<KeyId> ignoreId = std::nullopt) const;

        /**
         * NOTE: not thread safe.
         * @return id and address in a Node struct (for use in RPCs)
         */
        [[nodiscard]] Node thisNode() const {
            return Node{.id = id_, .address = address_};
        }

        [[nodiscard]] KeyId getFingerTableID(size_t index) const;

        FindSuccessorReply findSuccessor(KeyId keyId) const;

        void stabilizationLoop(std::stop_token stopToken);

        void updateSuccessorListAfterFailure();

        // Called during non-failures (i.e. successor informed us of a new successor)
        void addNewSuccessor(const Node &node);

        void refreshFingerTable();


        /**
         * Shuts down threads and necessary resources.
         */
        void handleCoreShutdown();


        mutable std::mutex mtx_;

        std::string address_;
        Config config_;
        std::shared_ptr<ChordNetwork> network_;
        KeyId id_;
        std::vector<Node> successorList_{};
        std::vector<Node> fingerTable_{};
        std::optional<Node> predecessor_{};
        PredecessorLeaveCallback predecessorLeaveCallback_;
        SuccessorListChangeCallback successorListChangeCallback_;

        std::condition_variable cv_;
        bool shouldTerminate_ = false;
        std::optional<std::jthread> stabilizationThread_;

        std::shared_ptr<NodeServerCallbacksImpl> callbacks_;

        friend class NodeServerCallbacksImpl;

        std::random_device rd_{};
        std::mt19937 gen_;
    };
} // namespace chord::core
