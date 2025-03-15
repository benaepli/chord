#include <memory>
#include <string>
#include "chord/network.hpp"
#include <tl/expected.hpp>

namespace chord::core
{
    class GrpcChordNetwork : public ChordNetwork
    {
    public:
        tl::expected<void, Error> startServer(const std::string& address) override;
        void stopServer() override;

        tl::expected<Node, Error>
        findSuccessor(const std::string& remoteAddress, KeyId id) override;
        tl::expected<Node, Error> getPredecessor(const std::string& remoteAddress) override;
        tl::expected<void, Error>
        notify(const std::string& remoteAddress, const Node& node) override;
        tl::expected<void, Error>
        updateFingerTable(const std::string& remoteAddress, int index, const Node& node) override;
        tl::expected<void, Error> predecessorLeave(const std::string& remoteAddress,
                                                   const Node& predecessor) override;
        void setFindSuccessorCallback(FindSuccessorCallback callback) override;
        void setGetPredecessorCallback(GetPredecessorCallback callback) override;
        void setNotifyCallback(NotifyCallback callback) override;
        void setGetSuccessorListCallback(GetSuccessorListCallback callback) override;
        void setUpdateFingerTableCallback(UpdateFingerTableCallback callback) override;
        void setPredecessorLeaveCallback(PredecessorLeaveCallback callback) override;
    };

    std::shared_ptr<ChordNetwork> createFullNetwork()
    {
        return std::make_shared<GrpcChordNetwork>();
    }

    std::shared_ptr<ChordApplicationNetwork> createApplicationNetwork()
    {
        return std::make_shared<GrpcChordNetwork>();
    }
}