#include <mutex>
#include "chord/chord.hpp"
#include "chord/network.hpp"
#include "chord_protos/node.grpc.pb.h"
#include "spdlog/spdlog.h"

namespace chord::core {
    tl::expected<std::unique_ptr<NodeServer>, Error>
    NodeServer::create(const std::string &address, Config config, std::shared_ptr<ChordNetwork> network) {
        if (network == nullptr) {
            spdlog::error("Network is null");
            return tl::unexpected(Error::Unexpected);
        }

        std::unique_ptr<NodeServer> server = std::make_unique<NodeServer>(address, config, std::move(network));
        return server;
    }

    NodeServer::NodeServer(std::string address, chord::core::Config config,
                           std::shared_ptr<ChordNetwork> network) : address_(address),
                                                                    config_(config),
                                                                    network_(network),
                                                                    id_(generateKeyId(address)) {
        network_->setGetSuccessorListCallback([this] {
            return getSuccessorList();
        });
        network_->setFindSuccessorCallback([this](KeyId keyId) {
            return findSuccessor(keyId);
        });
        network->setGetPredecessorCallback([this] {
            return getPredecessor();
        });
        network_->setNotifyCallback([this](const Node &node) {
            notify(node);
        });
    }

    Node NodeServer::closestPrecedingFinger(KeyId keyId, std::optional<KeyId> ignoreId) const {
        // TODO: replace with binary search if not efficient enough
        for (auto it = successorList_.rbegin(); it != successorList_.rend(); ++it) {
            KeyId id = it->id;
            if (keyIdBetween(id_, keyId, id) && (!ignoreId || id != ignoreId)) {
                return *it;
            }
        }
        spdlog::warn("No possible options for finger table"); // Should never happen
        return thisNode();
    }

    FindSuccessorReply NodeServer::findSuccessor(KeyId keyId) const {
        std::lock_guard lock(mtx_);
        if (successorList_.empty()) {
            spdlog::warn("No available successors");
            return {true, thisNode()}; // Basically should never happen?
        }

        Node firstSuccessor = successorList_.front();
        if (keyIdBetween(id_, firstSuccessor.id, keyId)) {
            return {true, firstSuccessor};
        }

        // Otherwise, search the finger table
        return {false, closestPrecedingFinger(keyId)};
    }

    void NodeServer::handleCoreShutdown() { {
            std::lock_guard lock(mtx_);
            shouldTerminate_ = true;
        }

        cv_.notify_all();
        if (stabilizationThread_.has_value() && stabilizationThread_->joinable()) {
            stabilizationThread_->join();
        }
    }

    NodeServer::~NodeServer() {
        if (network_->isRunning()) {
            spdlog::warn("Stopping server without leaving Chord: {}", address_);

            handleCoreShutdown();
            network_->stopServer();
        }
    }

    tl::expected<void, Error> NodeServer::start(std::optional<Node> entryPoint) {
        if (network_->isRunning()) {
            return tl::unexpected(Error::ServerAlreadyRunning);
        }
        auto result = network_->startServer(address_);
        if (!result) {
            return tl::unexpected(result.error());
        }

        // TODO: handle network joins
        if (!entryPoint) {
            fingerTable_ = std::vector<Node>(KEY_BITS, thisNode());

            return {};
        }

        stabilizationThread_ = std::jthread(stabilizationLoop);
    }

    tl::expected<void, Error> NodeServer::leave() {
        // TODO: handle leaving the network before core shutdown
        handleCoreShutdown();
        return {};
    }

    tl::expected<Node, Error> NodeServer::lookup(KeyId id) {
        std::unique_lock lock(mtx_);
        if (successorList_.empty()) {
            return thisNode();
        }

        /*
         * Our approach: start at this node, and call findSuccessor in the RPC. If it's found,
         * then we can return the node immediately. If not, then we should go to the closest preceding finger
         * returned by findSuccessor.
         *
         * We should be able to tolerate one failure per step in our search path.
         * That is, if the closest preceding finger that we have been returned isn't alive, then we should be able to
         * return to our previous node to ask for the best closest preceding finger that isn't the node we found.
         * If at this point, our previous node has failed, then we can return an error.
         *
         *
         */

        Node lastAliveNode = thisNode();
        Node lastNode = thisNode();

        lock.unlock();

        while (true) {
            tl::expected<FindSuccessorReply, Error> reply = network_->findSuccessor(
                lastNode.address, lastNode.id);
            if (!reply) {
                lastNode = lastAliveNode;
                // TODO: handle going to other entry in finger table
                continue;
            }
            if (reply->found) {
                return reply->node;
            }

            lastAliveNode = lastNode;
            lastNode = reply->node;
        }
    }
} // namespace chord::core
