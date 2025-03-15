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

namespace chord::core
{
    inline tl::expected<KeyId, Error> toId(const std::string& id)
    {
        if (id.size() != KEY_BYTES)
        {
            return tl::make_unexpected(Error::Unexpected);
        }

        KeyId nodeId{};
        std::copy(id.begin(), id.end(), nodeId.begin());
        return nodeId;
    }

    inline tl::expected<Node, Error> toNode(const chord_protos::NodeInfo& nodeInfo)
    {
        Node node{};
        auto nodeId = toId(nodeInfo.id());
        if (!nodeId)
        {
            return tl::make_unexpected(nodeId.error());
        }
        node.id = *nodeId;

        node.address = nodeInfo.address();
        return node;
    }

    inline void fromNode(const Node& node, chord_protos::NodeInfo* nodeInfo)
    {
        nodeInfo->set_id(node.id.data(), node.id.size());
        nodeInfo->set_address(node.address);
    }

    class GrpcChordNetwork final : public ChordNetwork
    {
        class ChordServiceImpl final : public chord_protos::NodeService::Service
        {
        public:
            void setGetSuccessorListCallback(GetSuccessorListCallback callback)
            {
                std::lock_guard lock(mutex_);
                getSuccessorListCallback_ = std::move(callback);
            }

            void setFindSuccessorCallback(FindSuccessorCallback callback)
            {
                std::lock_guard lock(mutex_);
                class ChordServiceImpl final : public chord_protos::NodeService::Service
                {
                public:
                    void setGetSuccessorListCallback(GetSuccessorListCallback callback)
                    {
                        std::lock_guard lock(serverMutex_);
                        getSuccessorListCallback_ = std::move(callback);
                    }

                    void setFindSuccessorCallback(FindSuccessorCallback callback)
                    {
                        std::lock_guard lock(serverMutex_);
                        findSuccessorCallback_ = std::move(callback);
                    }

                    void setGetPredecessorCallback(GetPredecessorCallback callback)
                    {
                        std::lock_guard lock(serverMutex_);
                        getPredecessorCallback_ = std::move(callback);
                    }

                    void setNotifyCallback(NotifyCallback callback)
                    {
                        std::lock_guard lock(serverMutex_);
                        notifyCallback_ = std::move(callback);
                    }

                    void setUpdateFingerTableCallback(UpdateFingerTableCallback callback)
                    {
                        std::lock_guard lock(serverMutex_);
                        updateFingerTableCallback_ = std::move(callback);
                    }

                    void setPredecessorLeaveCallback(PredecessorLeaveCallback callback)
                    {
                        std::lock_guard lock(serverMutex_);
                        predecessorLeaveCallback_ = std::move(callback);
                    }

                private:
                    grpc::Status getSuccessorList(grpc::ServerContext* context,
                                                  const chord_protos::GetSuccessorListRequest*
                                                  request,
                                                  chord_protos::GetSuccessorListReply* response);

                    grpc::Status findSuccessor(grpc::ServerContext* context,
                                               const chord_protos::FindSuccessorRequest* request,
                                               chord_protos::FindSuccessorReply* response);

                    grpc::Status getPredecessor(grpc::ServerContext* context,
                                                const chord_protos::GetPredecessorRequest* request,
                                                chord_protos::GetPredecessorReply* response);

                    grpc::Status notify(grpc::ServerContext* context,
                                        const chord_protos::NotifyRequest* request,
                                        chord_protos::NotifyReply* response);

                    grpc::Status updateFingerTable(grpc::ServerContext* context,
                                                   const chord_protos::UpdateFingerTableRequest*
                                                   request,
                                                   chord_protos::UpdateFingerTableReply* response);

                    grpc::Status predecessorLeave(grpc::ServerContext* context,
                                                  const chord_protos::PredecessorLeaveRequest*
                                                  request,
                                                  chord_protos::PredecessorLeaveReply* response);


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

            void setGetPredecessorCallback(GetPredecessorCallback callback)
            {
                std::lock_guard lock(mutex_);
                getPredecessorCallback_ = std::move(callback);
            }

            void setNotifyCallback(NotifyCallback callback)
            {
                std::lock_guard lock(mutex_);
                notifyCallback_ = std::move(callback);
            }

            void setUpdateFingerTableCallback(UpdateFingerTableCallback callback)
            {
                std::lock_guard lock(mutex_);
                updateFingerTableCallback_ = std::move(callback);
            }

            void setPredecessorLeaveCallback(PredecessorLeaveCallback callback)
            {
                std::lock_guard lock(mutex_);
                predecessorLeaveCallback_ = std::move(callback);
            }

        private:
            grpc::Status GetSuccessorList(grpc::ServerContext* context,
                                          const chord_protos::GetSuccessorListRequest* request,
                                          chord_protos::GetSuccessorListReply* response) override;

            grpc::Status FindSuccessor(grpc::ServerContext* context,
                                       const chord_protos::FindSuccessorRequest* request,
                                       chord_protos::FindSuccessorReply* response) override;

            grpc::Status GetPredecessor(grpc::ServerContext* context,
                                        const chord_protos::GetPredecessorRequest* request,
                                        chord_protos::GetPredecessorReply* response) override;

            grpc::Status Notify(grpc::ServerContext* context,
                                const chord_protos::NotifyRequest* request,
                                chord_protos::NotifyReply* response) override;

            grpc::Status UpdateFingerTable(grpc::ServerContext* context,
                                           const chord_protos::UpdateFingerTableRequest* request,
                                           chord_protos::UpdateFingerTableReply* response) override;

            grpc::Status PredecessorLeave(grpc::ServerContext* context,
                                          const chord_protos::PredecessorLeaveRequest* request,
                                          chord_protos::PredecessorLeaveReply* response) override;


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

        tl::expected<void, Error> startServer(const std::string& address) override;
        void stopServer() override;

        tl::expected<std::vector<Node>, Error>
        getSuccessorList(const std::string& remoteAddress) override;

        tl::expected<Node, Error>
        findSuccessor(const std::string& remoteAddress, KeyId id) override;
        tl::expected<Node, Error> getPredecessor(const std::string& remoteAddress) override;
        tl::expected<void, Error>
        notify(const std::string& remoteAddress, const Node& node) override;
        tl::expected<void, Error>
        updateFingerTable(const std::string& remoteAddress,
                          int index,
                          const Node& node) override;
        tl::expected<void, Error> predecessorLeave(const std::string& remoteAddress,
                                                   const Node& predecessor) override;

        void setGetSuccessorListCallback(GetSuccessorListCallback callback) override
        {
            service_.setGetSuccessorListCallback(std::move(callback));
        }

        void setFindSuccessorCallback(FindSuccessorCallback callback) override
        {
            service_.setFindSuccessorCallback(std::move(callback));
        }

        void setGetPredecessorCallback(GetPredecessorCallback callback) override
        {
            service_.setGetPredecessorCallback(std::move(callback));
        }

        void setNotifyCallback(NotifyCallback callback) override
        {
            service_.setNotifyCallback(std::move(callback));
        }

        void setUpdateFingerTableCallback(UpdateFingerTableCallback callback) override
        {
            service_.setUpdateFingerTableCallback(std::move(callback));
        }

        void setPredecessorLeaveCallback(PredecessorLeaveCallback callback) override
        {
            service_.setPredecessorLeaveCallback(std::move(callback));
        }

    private:
        std::shared_ptr<grpc::Channel> createChannel(const std::string& remoteAddress)
        {
            return grpc::CreateChannel(remoteAddress, grpc::InsecureChannelCredentials());
        }

        std::mutex serverMutex_;
        ChordServiceImpl service_;
        std::unique_ptr<grpc::Server> server_;

        bool running_ = false;
    };

    grpc::Status GrpcChordNetwork::ChordServiceImpl::GetSuccessorList(grpc::ServerContext* context,
        const chord_protos::GetSuccessorListRequest* request,
        chord_protos::GetSuccessorListReply* response)
    {
        if (!getSuccessorListCallback_)
        {
            return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "Callback not set");
        }

        try
        {
            auto nodes = getSuccessorListCallback_();
            for (const auto& node : nodes)
            {
                auto* replyNode = response->add_nodes();
                fromNode(node, replyNode);
            }

            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            return {grpc::StatusCode::INTERNAL, e.what()};
        }
    }

    grpc::Status GrpcChordNetwork::ChordServiceImpl::FindSuccessor(grpc::ServerContext* context,
        const chord_protos::FindSuccessorRequest* request,
        chord_protos::FindSuccessorReply* response)
    {
        if (!findSuccessorCallback_)
        {
            return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "Callback not set");
        }

        try
        {
            auto address = toId(request->keyid());
            if (!address)
            {
                return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Invalid ID");
            }

            auto node = findSuccessorCallback_(*address);
            response->set_found(node.found);
            fromNode(node.node, response->mutable_node());

            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            return {grpc::StatusCode::INTERNAL, e.what()};
        }
    }

    grpc::Status GrpcChordNetwork::ChordServiceImpl::GetPredecessor(grpc::ServerContext* context,
        const chord_protos::GetPredecessorRequest* request,
        chord_protos::GetPredecessorReply* response)
    {
        if (!getPredecessorCallback_)
        {
            return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "Callback not set");
        }

        try
        {
            if (auto node = getPredecessorCallback_())
            {
                fromNode(*node, response->mutable_node());
            }

            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            return {grpc::StatusCode::INTERNAL, e.what()};
        }
    }

    grpc::Status GrpcChordNetwork::ChordServiceImpl::Notify(grpc::ServerContext* context,
                                                            const chord_protos::NotifyRequest*
                                                            request,
                                                            chord_protos::NotifyReply* response)
    {
        if (!notifyCallback_)
        {
            return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "Callback not set");
        }

        try
        {
            auto node = toNode(request->node());
            if (!node)
            {
                return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Invalid node");
            }

            notifyCallback_(*node);

            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            return {grpc::StatusCode::INTERNAL, e.what()};
        }
    }

    grpc::Status GrpcChordNetwork::ChordServiceImpl::UpdateFingerTable(grpc::ServerContext* context,
        const chord_protos::UpdateFingerTableRequest* request,
        chord_protos::UpdateFingerTableReply* response)
    {
        if (!updateFingerTableCallback_)
        {
            return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "Callback not set");
        }

        try
        {
            auto node = toNode(request->node());
            if (!node)
            {
                return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Invalid node");
            }

            updateFingerTableCallback_(request->index(), *node);

            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            return {grpc::StatusCode::INTERNAL, e.what()};
        }
    }

    grpc::Status GrpcChordNetwork::ChordServiceImpl::PredecessorLeave(grpc::ServerContext* context,
        const chord_protos::PredecessorLeaveRequest* request,
        chord_protos::PredecessorLeaveReply* response)
    {
        if (!predecessorLeaveCallback_)
        {
            return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "Callback not set");
        }

        try
        {
            auto node = toNode(request->node());
            if (!node)
            {
                return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Invalid node");
            }

            predecessorLeaveCallback_(*node);

            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            return {grpc::StatusCode::INTERNAL, e.what()};
        }
    }

    tl::expected<void, Error> GrpcChordNetwork::startServer(const std::string& address)
    {
        std::lock_guard lock(serverMutex_);
        if (running_)
        {
            return tl::make_unexpected(Error::ServerAlreadyRunning);
        }

        grpc::ServerBuilder builder;
        builder.AddListeningPort(address, grpc::InsecureServerCredentials());
        builder.RegisterService(&service_);

        server_ = builder.BuildAndStart();
        if (!server_)
        {
            return tl::make_unexpected(Error::ServerStartFailed);
        }

        running_ = true;
        return {};
    }

    void GrpcChordNetwork::stopServer()
    {
        std::lock_guard lock(serverMutex_);
        if (!running_) { return; } // Calling stop on a stopped server is a no-op

        if (server_)
        {
            server_->Shutdown();
            server_ = nullptr;
        }
    }

    tl::expected<std::vector<Node>, Error> GrpcChordNetwork::getSuccessorList(
        const std::string& remoteAddress)
    {
        auto channel = createChannel(remoteAddress);
        auto stub = chord_protos::NodeService::NewStub(channel);

        chord_protos::GetSuccessorListRequest request;
        chord_protos::GetSuccessorListReply reply;
        grpc::ClientContext context;

        auto status = stub->GetSuccessorList(&context, request, &reply);
    }


    std::shared_ptr<ChordNetwork> createFullNetwork()
    {
        return std::make_shared<GrpcChordNetwork>();
    }

    std::shared_ptr<ChordApplicationNetwork> createApplicationNetwork()
    {
        return createFullNetwork();
    }
}