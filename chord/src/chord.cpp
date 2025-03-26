#include "chord/chord.hpp"
#include "chord/network.hpp"
#include "chord_protos/node.grpc.pb.h"
#include "spdlog/spdlog.h"

namespace chord::core {


    tl::expected<NodeServer, Error>
    NodeServer::create(const std::string &address, Config config, std::shared_ptr<ChordNetwork> network) {
        if (network == nullptr) {
            spdlog::error("Network is null");
            return tl::unexpected(Error::Unexpected);
        }

        NodeServer *nodeServer = new NodeServer(address, config, network);
    }

    NodeServer::NodeServer(std::string address, chord::core::Config config,
                           std::shared_ptr<ChordNetwork> network) :
            address_(address),
            config_(config),
            network_(network),
            id_(generateKeyId(address)) {

    }

    NodeServer::~NodeServer() {
        if (network_->isRunning()) {
            spdlog::warn("Stopping server without leaving Chord: {}", address_);
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
    }

    tl::expected<void, Error> NodeServer::leave() {
        // TODO: handle leaving the network
    }

    tl::expected<Node, Error> NodeServer::lookup(KeyId id) {
    }

} // namespace chord::core