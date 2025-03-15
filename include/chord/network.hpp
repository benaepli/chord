#pragma once

#include <cstddef>
#include <array>
#include <cstdint>
#include <tl/expected.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <optional>

namespace chord::core
{
    constexpr size_t KEY_BITS = 160;
    constexpr size_t KEY_BYTES = KEY_BITS / 8;

    using NodeId = std::array<uint8_t, KEY_BYTES>;
    using KeyId = std::array<uint8_t, KEY_BYTES>;

    struct KeyRange
    {
        KeyId start; // Inclusive
        KeyId end; // Exclusive
    };


    enum class Error: uint8_t
    {
        RPCFailed,
        NodeNotFound,
    };

    struct Node
    {
        NodeId id;
        std::string address;

        bool operator==(const Node& other) const
        {
            return id == other.id;
        }
    };

    class ChordApplicationNetwork
    {
    public:
        virtual ~ChordApplicationNetwork() = default;

        virtual tl::expected<std::vector<Node>, Error> getSuccessorList(const std::string& remoteAddress) = 0;
    };

    class ChordNetwork : public ChordApplicationNetwork
    {
    public:
        // Inherits all application-facing methods
        virtual tl::expected<void, Error> startServer(const std::string& address) = 0;
        virtual void stopServer() = 0;

        virtual tl::expected<Node, Error> findSuccessor(const std::string& remoteAddress, KeyId id) = 0;
        virtual tl::expected<Node, Error> getPredecessor(const std::string& remoteAddress) = 0;
        virtual tl::expected<void, Error> notify(const std::string& remoteAddress, const Node& node) = 0;
        virtual tl::expected<void, Error> updateFingerTable(const std::string& remoteAddress, int index,
                                                            const Node& node) = 0;
        virtual tl::expected<void, Error> predecessorLeave(const std::string& remoteAddress, const Node& predecessor) =
        0;

        using FindSuccessorCallback = std::function<Node(KeyId)>;
        using GetPredecessorCallback = std::function<std::optional<Node>()>;
        using NotifyCallback = std::function<void(const Node&)>;
        using GetSuccessorListCallback = std::function<std::vector<Node>()>;
        using UpdateFingerTableCallback = std::function<void(int, const Node&)>;
        using PredecessorLeaveCallback = std::function<void(const Node&)>;

        virtual void setFindSuccessorCallback(FindSuccessorCallback callback) = 0;
        virtual void setGetPredecessorCallback(GetPredecessorCallback callback) = 0;
        virtual void setNotifyCallback(NotifyCallback callback) = 0;
        virtual void setGetSuccessorListCallback(GetSuccessorListCallback callback) = 0;
        virtual void setUpdateFingerTableCallback(UpdateFingerTableCallback callback) = 0;
        virtual void setPredecessorLeaveCallback(PredecessorLeaveCallback callback) = 0;
    };

    std::shared_ptr<ChordNetwork> createFullNetwork();
    std::shared_ptr<ChordApplicationNetwork> createApplicationNetwork();
} // namespace chord::core
