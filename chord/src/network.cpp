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
            void setGetSuccessorListCallback(GetSuccessorListCallback callback) {
                std::lock_guard lock(mutex_);
                getSuccessorListCallback_ = std::move(callback);
            }

            void setFindSuccessorCallback(FindSuccessorCallback callback) {
                std::lock_guard lock(mutex_);
                class ChordServiceImpl final : public chord_protos::NodeService::Service {
                public:
                    void setGetSuccessorListCallback(GetSuccessorListCallback callback) {
                        std::lock_guard lock(serverMutex_);
                        getSuccessorListCallback_ = std::move(callback);
                    }

                    void setFindSuccessorCallback(FindSuccessorCallback callback) {
                        std::lock_guard lock(serverMutex_);
                        findSuccessorCallback_ = std::move(callback);
                    }

                    void setGetPredecessorCallback(GetPredecessorCallback callback) {
                        std::lock_guard lock(serverMutex_);
                        getPredecessorCallback_ = std::move(callback);
                    }

                    void setNotifyCallback(NotifyCallback callback) {
                        std::lock_guard lock(serverMutex_);
                        notifyCallback_ = std::move(callback);
                    }

                    void setUpdateFingerTableCallback(UpdateFingerTableCallback callback) {
                        std::lock_guard lock(serverMutex_);
                        updateFingerTableCallback_ = std::move(callback);
                    }

                    void setPredecessorLeaveCallback(PredecessorLeaveCallback callback) {
                        std::lock_guard lock(serverMutex_);
                        predecessorLeaveCallback_ = std::move(callback);
                    }

                private:
                    grpc::Status getSuccessorList(grpc::ServerContext *context,
                                                  const chord_protos::GetSuccessorListRequest *
                                                  request,
                                                  chord_protos::GetSuccessorListReply *response);

                    grpc::Status findSuccessor(grpc::ServerContext *context,
                                               const chord_protos::FindSuccessorRequest *request,
                                               chord_protos::FindSuccessorReply *response);

                    grpc::Status getPredecessor(grpc::ServerContext *context,
                                                const chord_protos::GetPredecessorRequest *request,
                                                chord_protos::GetPredecessorReply *response);

                    grpc::Status notify(grpc::ServerContext *context,
                                        const chord_protos::NotifyRequest *request,
                                        chord_protos::NotifyReply *response);

                    grpc::Status updateFingerTable(grpc::ServerContext *context,
                                                   const chord_protos::UpdateFingerTableRequest *
                                                   request,
                                                   chord_protos::UpdateFingerTableReply *response);

                    grpc::Status predecessorLeave(grpc::ServerContext *context,
                                                  const chord_protos::PredecessorLeaveRequest *
                                                  request,
                                                  chord_protos::PredecessorLeaveReply *response);


                    std::mutex serverMutex_;

                    GetSuccessorListCallback getSuccessorListCallback_;

                    FindSuccessorCallback findSuccessorCallback_;
                    GetPredecessorCallback getPredecessorCallback_;
                    NotifyCallback notifyCallback_;
                    UpdateFingerTableCallback updateFingerTableCallback_;
                    PredecessorLeaveCallback predecessorLeaveCallback_;
                };
                findSuccessorCallback_ = std::move(callback);
            }

            void setGetPredecessorCallback(GetPredecessorCallback callback) {
                std::lock_guard lock(mutex_);
                getPredecessorCallback_ = std::move(callback);
            }

            void setNotifyCallback(NotifyCallback callback) {
                std::lock_guard lock(mutex_);
                notifyCallback_ = std::move(callback);
            }

            void setUpdateFingerTableCallback(UpdateFingerTableCallback callback) {
                std::lock_guard lock(mutex_);
                updateFingerTableCallback_ = std::move(callback);
            }

            void setPredecessorLeaveCallback(PredecessorLeaveCallback callback) {
                std::lock_guard lock(mutex_);
                predecessorLeaveCallback_ = std::move(callback);
            }

        private:
            grpc::Status GetSuccessorList(grpc::ServerContext *context,
                                          const chord_protos::GetSuccessorListRequest *request,
                                          chord_protos::GetSuccessorListReply *response) override;

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


            std::mutex mutex_;

            GetSuccessorListCallback getSuccessorListCallback_;

            FindSuccessorCallback findSuccessorCallback_;
            GetPredecessorCallback getPredecessorCallback_;
            NotifyCallback notifyCallback_;
            UpdateFingerTableCallback updateFingerTableCallback_;
            PredecessorLeaveCallback predecessorLeaveCallback_;
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

        void setGetSuccessorListCallback(GetSuccessorListCallback callback) override {
            service_.setGetSuccessorListCallback(std::move(callback));
        }

        void setFindSuccessorCallback(FindSuccessorCallback callback) override {
            service_.setFindSuccessorCallback(std::move(callback));
        }

        void setGetPredecessorCallback(GetPredecessorCallback callback) override {
            service_.setGetPredecessorCallback(std::move(callback));
        }

        void setNotifyCallback(NotifyCallback callback) override {
            service_.setNotifyCallback(std::move(callback));
        }

        void setUpdateFingerTableCallback(UpdateFingerTableCallback callback) override {
            service_.setUpdateFingerTableCallback(std::move(callback));
        }

        void setPredecessorLeaveCallback(PredecessorLeaveCallback callback) override {
            service_.setPredecessorLeaveCallback(std::move(callback));
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

    grpc::Status GrpcChordNetwork::ChordServiceImpl::GetSuccessorList(grpc::ServerContext *context,
                                                                      const chord_protos::GetSuccessorListRequest *
                                                                      request,
                                                                      chord_protos::GetSuccessorListReply *response) {
        if (!getSuccessorListCallback_) {
            return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "Callback not set");
        }

        try {
            auto nodes = getSuccessorListCallback_();
            for (const auto &node: nodes) {
                auto *replyNode = response->add_nodes();
                fromNode(node, replyNode);
            }

            return grpc::Status::OK;
        } catch (const std::exception &e) {
            return {grpc::StatusCode::INTERNAL, e.what()};
        }
    }

    grpc::Status GrpcChordNetwork::ChordServiceImpl::FindSuccessor(grpc::ServerContext *context,
                                                                   const chord_protos::FindSuccessorRequest *request,
                                                                   chord_protos::FindSuccessorReply *response) {
        if (!findSuccessorCallback_) {
            return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "Callback not set");
        }

        try {
            auto address = toId(request->keyid());
            if (!address) {
                return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Invalid ID");
            }

            auto node = findSuccessorCallback_(*address);
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
        if (!getPredecessorCallback_) {
            return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "Callback not set");
        }

        try {
            if (auto node = getPredecessorCallback_()) {
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
        if (!notifyCallback_) {
            return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "Callback not set");
        }

        try {
            auto node = toNode(request->node());
            if (!node) {
                return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Invalid node");
            }

            notifyCallback_(*node);

            return grpc::Status::OK;
        } catch (const std::exception &e) {
            return {grpc::StatusCode::INTERNAL, e.what()};
        }
    }

    grpc::Status GrpcChordNetwork::ChordServiceImpl::UpdateFingerTable(grpc::ServerContext *context,
                                                                       const chord_protos::UpdateFingerTableRequest *
                                                                       request,
                                                                       chord_protos::UpdateFingerTableReply *response) {
        if (!updateFingerTableCallback_) {
            return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "Callback not set");
        }

        try {
            auto node = toNode(request->node());
            if (!node) {
                return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Invalid node");
            }

            updateFingerTableCallback_(request->index(), *node);

            return grpc::Status::OK;
        } catch (const std::exception &e) {
            return {grpc::StatusCode::INTERNAL, e.what()};
        }
    }

    grpc::Status GrpcChordNetwork::ChordServiceImpl::PredecessorLeave(grpc::ServerContext *context,
                                                                      const chord_protos::PredecessorLeaveRequest *
                                                                      request,
                                                                      chord_protos::PredecessorLeaveReply *response) {
        if (!predecessorLeaveCallback_) {
            return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "Callback not set");
        }

        try {
            auto node = toNode(request->node());
            if (!node) {
                return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Invalid node");
            }

            predecessorLeaveCallback_(*node);

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
