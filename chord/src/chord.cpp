#include <mutex>
#include <random>
#include <utility>
#include "chord/chord.hpp"
#include "chord/network.hpp"
#include "chord_protos/node.grpc.pb.h"
#include "spdlog/spdlog.h"
#include <boost/multiprecision/cpp_int.hpp>

namespace chord::core {
    namespace {
        namespace mp = boost::multiprecision;

        constinit const mp::uint256_t MODULUS = mp::uint256_t(1) << KEY_BITS; // 2^KEY_BITS

        mp::uint256_t keyIdToInt(const KeyId &keyId) {
            mp::uint256_t result = 0;
            boost::multiprecision::import_bits(result, keyId.begin(), keyId.end(), CHAR_BIT, true);
            return result;
        }

        KeyId intToKeyId(const mp::uint256_t &value) {
            KeyId result{};
            mp::uint256_t temp = value;

            for (int i = 19; i >= 0; --i) {
                result[i] = static_cast<unsigned char>(temp & 0xFF);
                temp >>= 8;
            }
            return result;
        }
    }


    class NodeServerCallbacksImpl : public ChordCallbacks {
    public:
        NodeServerCallbacksImpl(NodeServer *server) : server_(server) {
        }

        ~NodeServerCallbacksImpl() override = default;

        std::vector<Node> getSuccessorList() override {
            return server_->getSuccessorList();
        }

        tl::expected<Node, Error> lookup(KeyId id) override {
            return server_->lookup(id);
        }

        FindSuccessorReply findSuccessor(KeyId id) override {
            return server_->findSuccessor(id);
        }

        std::optional<Node> getPredecessor() override {
            return server_->getPredecessor();
        }

        void notify(const Node &node) override {
            return server_->notify(node);
        }

        void updateFingerTable(int index, const Node &node) override {
            return server_->updateFingerTable(index, node);
        }

        void predecessorLeave(const Node &node) override {
            return server_->predecessorLeave(node);
        }

    private:
        NodeServer *server_;
    };

    tl::expected<std::unique_ptr<NodeServer>, Error>
    NodeServer::create(const std::string &address, Config config, std::shared_ptr<ChordNetwork> network) {
        if (network == nullptr) {
            spdlog::error("Network is null");
            return tl::unexpected(Error::Unexpected);
        }

        auto server = std::unique_ptr<NodeServer>(new NodeServer(address, config, network));;
        return server;
    }

    NodeServer::NodeServer(std::string address, chord::core::Config config,
                           std::shared_ptr<ChordNetwork> network) : address_(address),
                                                                    config_(config),
                                                                    network_(std::move(network)),
                                                                    id_(generateKeyId(address)),
                                                                    callbacks_(
                                                                        std::make_shared<NodeServerCallbacksImpl>(
                                                                            this)),
                                                                    gen_(rd_()) {
        network_->setCallbacks(callbacks_);
    }

    // Note that initFingerTable is not thread-safe
    tl::expected<void, Error> NodeServer::initFingerTable(const Node &entryPoint) {
        auto thisId = keyIdToInt(id_);
        for (int i = 0; i < KEY_BITS; ++i) {
            auto fingerId = (thisId + (mp::uint256_t(1) << i)) % MODULUS;
            auto fingerKeyId = intToKeyId(fingerId);

            // TODO: handle timeout
            auto node = network_->lookup(entryPoint.address, fingerKeyId);
            if (!node) {
                return tl::unexpected(node.error());
            }

            fingerTable_[i] = *node;
        }

        return {};
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

    KeyId NodeServer::getFingerTableID(size_t index) const {
        auto thisId = keyIdToInt(id_);

        auto fingerId = (thisId + (mp::uint256_t(1) << index)) % MODULUS;
        auto fingerKeyId = intToKeyId(fingerId);

        return fingerKeyId;
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
            network_->setCallbacks(nullptr);
        }
    }

    tl::expected<void, Error> NodeServer::start(std::optional<Node> entryPoint) {
        if (network_->isRunning()) {
            return tl::unexpected(Error::ServerAlreadyRunning);
        }

        std::unique_lock lock(mtx_);

        // TODO: handle network joins
        if (!entryPoint) {
            fingerTable_ = std::vector<Node>(KEY_BITS, thisNode());

            return {};
        } {
            auto result = initFingerTable(*entryPoint);
            if (!result) {
                return tl::unexpected(result.error());
            }
        }

        auto result = network_->startServer(address_);
        if (!result) {
            return tl::unexpected(result.error());
        }

        stabilizationThread_ = std::jthread([this](std::stop_token stopToken) {
            stabilizationLoop(std::move(stopToken));
        });
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

    void NodeServer::stabilizationLoop(std::stop_token stopToken) {
        while (!stopToken.stop_requested()) {
            std::unique_lock lock(mtx_);

            // Wait for termination or timeout
            bool terminated = cv_.wait_for(lock,
                                           std::chrono::milliseconds(config_.stabilizationInterval),
                                           [this] { return shouldTerminate_; });

            if (terminated) {
                break;
            }

            if (successorList_.empty()) {
                lock.unlock();
                continue;
            }

            Node successor = successorList_.front();
            lock.unlock();

            auto predecessorResult = network_->getPredecessor(successor.address);
            if (!predecessorResult) {
                // Successor failed, need to update successor list
                updateSuccessorListAfterFailure();
                continue;
            }

            auto successorPredecessor = *predecessorResult;
            if (successorPredecessor) {
                if (keyIdBetween(id_, successor.id, successorPredecessor->id)) {
                    // Update successor
                    addNewSuccessor(*successorPredecessor);
                }
            }

            auto notifyResult = network_->notify(successor.address, thisNode());
            if (!notifyResult) {
                updateSuccessorListAfterFailure();
            }

            refreshFingerTable();
        }
    }

    void NodeServer::updateSuccessorListAfterFailure() {
        std::unique_lock lock{mtx_, std::defer_lock};

        while (true) {
            lock.lock();
            if (successorList_.empty() || successorList_.size() == 1) {
                spdlog::warn("Successor list empty (or will be empty) after failure");
                return;
            }

            successorList_.erase(successorList_.begin());

            auto firstSuccessor = successorList_.front();
            lock.unlock();

            auto result = network_->getSuccessorList(firstSuccessor.address);
            if (!result) {
                continue;
            }

            lock.lock();
            if (!successorList_.empty() && successorList_.front() != firstSuccessor) {
                spdlog::warn("Successor list changed");
                return;
            }


        }
    }

    void NodeServer::addNewSuccessor(const Node &node) {
        std::lock_guard lock(mtx_);
        successorList_.insert(successorList_.begin(), node);
        if (successorList_.size() > config_.successorListSize) {
            successorList_.pop_back();
        }

        if (successorListChangeCallback_) {
            successorListChangeCallback_(successorList_);
        }
    }

    void NodeServer::refreshFingerTable() {
        std::unique_lock lock{mtx_};
        auto distribution = std::uniform_int_distribution<size_t>(fingerTable_.size());
        auto i = distribution(gen_);
        auto startID = getFingerTableID(i);

        lock.unlock();

        auto found = lookup(startID);
        if (!found) {
            spdlog::warn("Finger table refresh failed");
            return;
        }

        lock.lock();
        fingerTable_[i] = *found;
    }
} // namespace chord::core
