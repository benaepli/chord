#include <cassert>
#include <chrono>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <vector>
#include "chord/network.hpp"
#include <grpcpp/completion_queue.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/status.h>
#include <grpcpp/grpcpp.h>
#include <tl/expected.hpp>
#include "chord_protos/node.grpc.pb.h"
#include "chord_protos/node.pb.h"
#include <openssl/sha.h>
#include <spdlog/spdlog.h>

namespace chord::core {
    inline tl::expected<KeyId, Error> toId(const std::string &id) {
        if (id.size() != KEY_BYTES) {
            return tl::make_unexpected(Error::Unexpected);
        }

        KeyId nodeId{};
        std::ranges::copy(id, nodeId.begin());
        return nodeId;
    }

    inline std::string fromId(const KeyId &id) {
        return std::string(id.begin(), id.end());
    }

    inline tl::expected<Node, Error> toNode(const chord_protos::NodeInfo &nodeInfo) {
        Node node{};
        auto nodeId = toId(nodeInfo.id());
        if (!nodeId) {
            return tl::make_unexpected(nodeId.error());
        }
        node.id = *nodeId;

        node.address = nodeInfo.address();
        return node;
    }

    inline Error fromStatus(const grpc::Status &status) {
        switch (status.error_code()) {
            case grpc::StatusCode::DEADLINE_EXCEEDED:
            case grpc::StatusCode::ABORTED:
            case grpc::StatusCode::UNAVAILABLE:
                return Error::Timeout;
            case grpc::StatusCode::INVALID_ARGUMENT:
                return Error::InvalidArgument;
            default:
                return Error::Unexpected;
        }
    }

    inline void setClientContext(grpc::ClientContext &context, RequestConfig config) {
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(config.timeout));
    }

#define RETURN_IF_STATUS_ERROR(status) \
    if (!status.ok()) { return tl::make_unexpected(fromStatus(status)); }

    inline void fromNode(const Node &node, chord_protos::NodeInfo *nodeInfo) {
        nodeInfo->set_id(node.id.data(), node.id.size());
        nodeInfo->set_address(node.address);
    }

    class GrpcChordNetwork final : public ChordNetwork {
        class ChordServiceImpl final : public chord_protos::NodeService::Service {
        public:
            void setCallbacks(std::shared_ptr<ChordCallbacks> callbacks) {
                std::lock_guard lock(mutex_);
                callbacks_ = std::move(callbacks);
            }

        private:
            grpc::Status GetSuccessorList(grpc::ServerContext *context,
                                          const chord_protos::GetSuccessorListRequest *request,
                                          chord_protos::GetSuccessorListReply *response) override;

            grpc::Status Lookup(grpc::ServerContext *context, const chord_protos::LookupRequest *request,
                                chord_protos::LookupReply *response) override;

            grpc::Status FindSuccessor(grpc::ServerContext *context,
                                       const chord_protos::FindSuccessorRequest *request,
                                       chord_protos::FindSuccessorReply *response) override;

            grpc::Status GetPredecessor(grpc::ServerContext *context,
                                        const chord_protos::GetPredecessorRequest *request,
                                        chord_protos::GetPredecessorReply *response) override;

            grpc::Status Notify(grpc::ServerContext *context,
                                const chord_protos::NotifyRequest *request,
                                chord_protos::NotifyReply *response) override;

            grpc::Status UpdateFingerTable(grpc::ServerContext *context,
                                           const chord_protos::UpdateFingerTableRequest *request,
                                           chord_protos::UpdateFingerTableReply *response) override;

            grpc::Status PredecessorLeave(grpc::ServerContext *context,
                                          const chord_protos::PredecessorLeaveRequest *request,
                                          chord_protos::PredecessorLeaveReply *response) override;


            std::mutex mutex_{};

            std::shared_ptr<ChordCallbacks> callbacks_;
        };

    public:
        GrpcChordNetwork() = default;

        tl::expected<void, Error> startServer(const std::string &address) override;

        void stopServer() override;

        [[nodiscard]] bool isRunning() const override {
            std::lock_guard lock(serverMutex_);
            return running_;
        }

        tl::expected<std::vector<Node>, Error>
        getSuccessorList(const std::string &remoteAddress, RequestConfig config) override;

        tl::expected<Node, Error> lookup(const std::string &remoteAddress, KeyId id, RequestConfig config) override;

        tl::expected<FindSuccessorReply, Error>
        findSuccessor(const std::string &remoteAddress, KeyId id, RequestConfig config) override;

        tl::expected<std::optional<Node>, Error>
        getPredecessor(const std::string &remoteAddress, RequestConfig config) override;

        tl::expected<void, Error>
        notify(const std::string &remoteAddress, const Node &node, RequestConfig config) override;

        tl::expected<void, Error>
        updateFingerTable(const std::string &remoteAddress,
                          int index,
                          const Node &node, RequestConfig config) override;

        tl::expected<void, Error> predecessorLeave(const std::string &remoteAddress,
                                                   const Node &predecessor, RequestConfig config) override;

        void setCallbacks(std::shared_ptr<ChordCallbacks> callbacks) override {
            service_.setCallbacks(std::move(callbacks));
        }

    private:
        std::shared_ptr<grpc::Channel> createChannel(const std::string &remoteAddress) {
            return grpc::CreateChannel(remoteAddress, grpc::InsecureChannelCredentials());
        }

        mutable std::mutex serverMutex_;
        ChordServiceImpl service_;
        std::unique_ptr<grpc::Server> server_;

        bool running_ = false;
    };

#define CHECK_CALLBACKS() \
    if (!callbacks_) { \
        spdlog::error("Callbacks not set"); \
        return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "Callbacks not set"); \
    }


    grpc::Status GrpcChordNetwork::ChordServiceImpl::GetSuccessorList(grpc::ServerContext *context,
                                                                      const chord_protos::GetSuccessorListRequest *
                                                                      request,
                                                                      chord_protos::GetSuccessorListReply *response) {
        CHECK_CALLBACKS();

        try {
            auto nodes = callbacks_->getSuccessorList();
            for (const auto &node: nodes) {
                auto *replyNode = response->add_nodes();
                fromNode(node, replyNode);
            }

            return grpc::Status::OK;
        } catch (const std::exception &e) {
            return {grpc::StatusCode::INTERNAL, e.what()};
        }
    }

    grpc::Status GrpcChordNetwork::ChordServiceImpl::Lookup(grpc::ServerContext *context,
                                                            const chord_protos::LookupRequest *request,
                                                            chord_protos::LookupReply *response) {
        CHECK_CALLBACKS();

        try {
            auto id = toId(request->keyid());
            if (!id) {
                return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Invalid ID");
            }

            auto node = callbacks_->lookup(*id);
            if (!node) {
                return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Node not found");
            }

            fromNode(*node, response->mutable_node());

            return grpc::Status::OK;
        } catch (const std::exception &e) {
            return {grpc::StatusCode::INTERNAL, e.what()};
        }
    }


    grpc::Status GrpcChordNetwork::ChordServiceImpl::FindSuccessor(grpc::ServerContext *context,
                                                                   const chord_protos::FindSuccessorRequest *request,
                                                                   chord_protos::FindSuccessorReply *response) {
        CHECK_CALLBACKS();

        try {
            auto address = toId(request->keyid());
            if (!address) {
                return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Invalid ID");
            }

            auto node = callbacks_->findSuccessor(*address);
            response->set_found(node.found);
            fromNode(node.node, response->mutable_node());

            return grpc::Status::OK;
        } catch (const std::exception &e) {
            return {grpc::StatusCode::INTERNAL, e.what()};
        }
    }

    grpc::Status GrpcChordNetwork::ChordServiceImpl::GetPredecessor(grpc::ServerContext *context,
                                                                    const chord_protos::GetPredecessorRequest *request,
                                                                    chord_protos::GetPredecessorReply *response) {
        CHECK_CALLBACKS();

        try {
            if (auto node = callbacks_->getPredecessor()) {
                fromNode(*node, response->mutable_node());
            }

            return grpc::Status::OK;
        } catch (const std::exception &e) {
            return {grpc::StatusCode::INTERNAL, e.what()};
        }
    }

    grpc::Status GrpcChordNetwork::ChordServiceImpl::Notify(grpc::ServerContext *context,
                                                            const chord_protos::NotifyRequest *
                                                            request,
                                                            chord_protos::NotifyReply *response) {
        CHECK_CALLBACKS();

        try {
            auto node = toNode(request->node());
            if (!node) {
                return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Invalid node");
            }

            callbacks_->notify(*node);

            return grpc::Status::OK;
        } catch (const std::exception &e) {
            return {grpc::StatusCode::INTERNAL, e.what()};
        }
    }

    grpc::Status GrpcChordNetwork::ChordServiceImpl::UpdateFingerTable(grpc::ServerContext *context,
                                                                       const chord_protos::UpdateFingerTableRequest *
                                                                       request,
                                                                       chord_protos::UpdateFingerTableReply *response) {
        CHECK_CALLBACKS();

        try {
            auto node = toNode(request->node());
            if (!node) {
                return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Invalid node");
            }

            callbacks_->updateFingerTable(request->index(), *node);

            return grpc::Status::OK;
        } catch (const std::exception &e) {
            return {grpc::StatusCode::INTERNAL, e.what()};
        }
    }

    grpc::Status GrpcChordNetwork::ChordServiceImpl::PredecessorLeave(grpc::ServerContext *context,
                                                                      const chord_protos::PredecessorLeaveRequest *
                                                                      request,
                                                                      chord_protos::PredecessorLeaveReply *response) {
        CHECK_CALLBACKS();

        try {
            auto node = toNode(request->node());
            if (!node) {
                return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Invalid node");
            }

            callbacks_->predecessorLeave(*node);

            return grpc::Status::OK;
        } catch (const std::exception &e) {
            return {grpc::StatusCode::INTERNAL, e.what()};
        }
    }

    tl::expected<void, Error> GrpcChordNetwork::startServer(const std::string &address) {
        std::lock_guard lock(serverMutex_);
        if (running_) {
            return tl::make_unexpected(Error::ServerAlreadyRunning);
        }

        grpc::ServerBuilder builder;
        builder.AddListeningPort(address, grpc::InsecureServerCredentials());
        builder.RegisterService(&service_);

        server_ = builder.BuildAndStart();
        if (!server_) {
            return tl::make_unexpected(Error::ServerStartFailed);
        }

        running_ = true;
        return {};
    }

    void GrpcChordNetwork::stopServer() {
        std::lock_guard lock(serverMutex_);
        if (!running_) { return; } // Calling stop on a stopped server is a no-op

        if (server_) {
            server_->Shutdown();
            server_ = nullptr;
        }
    }

    tl::expected<std::vector<Node>, Error> GrpcChordNetwork::getSuccessorList(
        const std::string &remoteAddress, RequestConfig config) {
        auto channel = createChannel(remoteAddress);
        auto stub = chord_protos::NodeService::NewStub(channel);

        chord_protos::GetSuccessorListRequest request;
        chord_protos::GetSuccessorListReply reply;
        grpc::ClientContext context;
        setClientContext(context, config);

        auto status = stub->GetSuccessorList(&context, request, &reply);
        RETURN_IF_STATUS_ERROR(status)

        std::vector<Node> nodes;
        for (const auto &nodeInfo: reply.nodes()) {
            auto node = toNode(nodeInfo);
            if (!node) {
                return tl::make_unexpected(node.error());
            }

            nodes.push_back(*node);
        }
        return nodes;
    }

    tl::expected<Node, Error> GrpcChordNetwork::
    lookup(const std::string &remoteAddress, KeyId id, RequestConfig config) {
        auto channel = createChannel(remoteAddress);
        auto stub = chord_protos::NodeService::NewStub(channel);

        chord_protos::LookupRequest request;
        request.set_keyid(fromId(id));

        chord_protos::LookupReply reply;
        grpc::ClientContext context;
        setClientContext(context, config);

        auto status = stub->Lookup(&context, request, &reply);
        RETURN_IF_STATUS_ERROR(status);

        return toNode(reply.node());
    }

    tl::expected<FindSuccessorReply, Error> GrpcChordNetwork::findSuccessor(
        const std::string &remoteAddress,
        KeyId id, RequestConfig config) {
        auto channel = createChannel(remoteAddress);
        auto stub = chord_protos::NodeService::NewStub(channel);

        chord_protos::FindSuccessorRequest request;
        request.set_keyid(fromId(id));

        chord_protos::FindSuccessorReply reply;
        grpc::ClientContext context;
        setClientContext(context, config);

        auto status = stub->FindSuccessor(&context, request, &reply);
        RETURN_IF_STATUS_ERROR(status);

        auto node = toNode(reply.node());
        if (!node) {
            return tl::make_unexpected(node.error());
        }

        return FindSuccessorReply{reply.found(), *node};
    }

    tl::expected<std::optional<Node>, Error> GrpcChordNetwork::getPredecessor(
        const std::string &remoteAddress, RequestConfig config) {
        auto channel = createChannel(remoteAddress);
        auto stub = chord_protos::NodeService::NewStub(channel);

        chord_protos::GetPredecessorRequest request;
        chord_protos::GetPredecessorReply reply;
        grpc::ClientContext context;
        setClientContext(context, config);

        auto status = stub->GetPredecessor(&context, request, &reply);
        RETURN_IF_STATUS_ERROR(status);

        if (reply.has_node()) {
            auto node = toNode(reply.node());
            if (!node) {
                return tl::make_unexpected(node.error());
            }
            return std::optional<Node>{*node};
        }

        return std::nullopt;
    }

    tl::expected<void, Error> GrpcChordNetwork::notify(const std::string &remoteAddress,
                                                       const Node &node, RequestConfig config) {
        auto channel = createChannel(remoteAddress);
        auto stub = chord_protos::NodeService::NewStub(channel);

        chord_protos::NotifyRequest request;
        chord_protos::NotifyReply reply;
        grpc::ClientContext context;
        setClientContext(context, config);

        fromNode(node, request.mutable_node());

        auto status = stub->Notify(&context, request, &reply);
        RETURN_IF_STATUS_ERROR(status);

        return {};
    }

    tl::expected<void, Error> GrpcChordNetwork::updateFingerTable(
        const std::string &remoteAddress,
        int index,
        const Node &node, RequestConfig config) {
        auto channel = createChannel(remoteAddress);
        auto stub = chord_protos::NodeService::NewStub(channel);

        chord_protos::UpdateFingerTableRequest request;
        chord_protos::UpdateFingerTableReply reply;
        grpc::ClientContext context;
        setClientContext(context, config);

        request.set_index(index);
        fromNode(node, request.mutable_node());

        auto status = stub->UpdateFingerTable(&context, request, &reply);
        RETURN_IF_STATUS_ERROR(status);

        return {};
    }

    tl::expected<void, Error> GrpcChordNetwork::predecessorLeave(
        const std::string &remoteAddress,
        const Node &predecessor, RequestConfig config) {
        auto channel = createChannel(remoteAddress);
        auto stub = chord_protos::NodeService::NewStub(channel);

        chord_protos::PredecessorLeaveRequest request;
        chord_protos::PredecessorLeaveReply reply;
        grpc::ClientContext context;
        setClientContext(context, config);

        fromNode(predecessor, request.mutable_node());

        auto status = stub->PredecessorLeave(&context, request, &reply);
        RETURN_IF_STATUS_ERROR(status);

        return {};
    }

    std::shared_ptr<ChordNetwork> createFullNetwork() {
        return std::make_shared<GrpcChordNetwork>();
    }

    std::shared_ptr<ChordApplicationNetwork> createApplicationNetwork() {
        return createFullNetwork();
    }

    std::strong_ordering compareKeyId(KeyId first, KeyId second) {
        int cmp = std::memcmp(first.data(), second.data(), KEY_BYTES);

        if (cmp < 0) {
            return std::strong_ordering::less;
        }

        if (cmp > 0) {
            return std::strong_ordering::less;
        }

        return std::strong_ordering::equal;
    }

    bool keyIdBetween(KeyId start, KeyId end, KeyId betweenKey) {
        auto ordering = compareKeyId(start, end);
        bool greaterThanStart = compareKeyId(start, betweenKey) == std::strong_ordering::less;

        bool lessOrEqualToEnd = compareKeyId(betweenKey, end) == std::strong_ordering::equal
                                || compareKeyId(betweenKey, end) == std::strong_ordering::less;

        if (ordering == std::strong_ordering::greater) {
            // Wrap around
            return greaterThanStart || lessOrEqualToEnd;
        }

        return greaterThanStart && lessOrEqualToEnd;
    }

    KeyId generateKeyId(const std::string &address) {
        KeyId id{};
        SHA1(reinterpret_cast<const unsigned char *>(address.data()), address.size(), id.data());
        return id;
    }
} // namespace chord::core
