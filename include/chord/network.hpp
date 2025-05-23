#pragma once

#include <cstddef>
#include <compare>
#include <array>
#include <cstdint>
#include <tl/expected.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <optional>

namespace chord::core {
    constexpr size_t KEY_BITS = 160;
    constexpr size_t KEY_BYTES = KEY_BITS / 8;

    static_assert(KEY_BYTES == 20, "KeyId size must be 20 bytes");

    using KeyId = std::array<uint8_t, KEY_BYTES>;

    std::strong_ordering compareKeyId(KeyId first, KeyId second);


    /**
     *
     * @param start The start key
     * @param end The end key
     * @param betweenKey The query key - i.e. is betweenKey in the range (start, end]
     * @return
     */
    bool keyIdBetween(KeyId start, KeyId end, KeyId betweenKey);

    // Uses SHA-1 hash function to generate a KeyId from a string
    KeyId generateKeyId(const std::string &address);


    enum class Error : uint8_t {
        Timeout,
        NodeNotFound,
        ServerAlreadyRunning,
        ServerStartFailed,
        Unexpected,
        InvalidArgument,
        LookupFailed,
    };

    struct Node {
        KeyId id;
        std::string address;

        bool operator==(const Node &other) const {
            return id == other.id;
        }
    };

    struct FindSuccessorReply {
        bool found = false;
        Node node;
    };

    struct RequestConfig {
        uint64_t timeout = 1000; // Timeout in milliseconds
    };

#define CHORD_CORE_DEFINE_DEFAULT_ARG_1_PARAM(func, type1, name1) \
    auto func(type1 name1) { \
        return func(name1, {}); \
    }

#define CHORD_CORE_DEFINE_DEFAULT_ARG_2_PARAM(func, type1, name1, type2, name2) \
    auto func(type1 name1, type2 name2) { \
        return func(name1, name2, {}); \
    }

#define CHORD_CORE_DEFINE_DEFAULT_ARG_3_PARAM(func, type1, name1, type2, name2, type3, name3) \
    auto func(type1 name1, type2 name2, type3 name3) { \
        return func(name1, name2, name3, {}); \
    }

    class ChordCallbacks {
    public:
        virtual ~ChordCallbacks() = default;

        virtual std::vector<Node> getSuccessorList() = 0;

        virtual tl::expected<Node, Error> lookup(KeyId id) = 0;

        virtual FindSuccessorReply findSuccessor(KeyId id) = 0;

        virtual std::optional<Node> getPredecessor() = 0;

        virtual void notify(const Node &node) = 0;

        virtual void updateFingerTable(int index, const Node &node) = 0;

        virtual void predecessorLeave(const Node &node) = 0;
    };


    class ChordApplicationNetwork {
    public:
        virtual ~ChordApplicationNetwork() = default;

        virtual tl::expected<std::vector<Node>, Error>
        getSuccessorList(const std::string &remoteAddress, RequestConfig config) = 0;

        CHORD_CORE_DEFINE_DEFAULT_ARG_1_PARAM(getSuccessorList, const std::string &, remoteAddress);
    };


    class ChordNetwork : public ChordApplicationNetwork {
    public:
        // Inherits all application-facing methods
        virtual tl::expected<void, Error> startServer(const std::string &address) = 0;

        virtual void stopServer() = 0;

        [[nodiscard]] virtual bool isRunning() const = 0;

        virtual tl::expected<Node, Error> lookup(const std::string &remoteAddress, KeyId id,
                                                 RequestConfig config) = 0;

        CHORD_CORE_DEFINE_DEFAULT_ARG_2_PARAM(lookup, const std::string &, remoteAddress, KeyId, id);

        virtual tl::expected<FindSuccessorReply, Error>
        findSuccessor(const std::string &remoteAddress, KeyId id, RequestConfig config) = 0;

        CHORD_CORE_DEFINE_DEFAULT_ARG_2_PARAM(findSuccessor, const std::string &, remoteAddress, KeyId, id);


        virtual tl::expected<std::optional<Node>, Error>
        getPredecessor(const std::string &remoteAddress, RequestConfig config) = 0;

        CHORD_CORE_DEFINE_DEFAULT_ARG_1_PARAM(getPredecessor, const std::string &, remoteAddress);

        virtual tl::expected<void, Error>
        notify(const std::string &remoteAddress, const Node &node, RequestConfig config) = 0;

        CHORD_CORE_DEFINE_DEFAULT_ARG_2_PARAM(notify, const std::string &, remoteAddress, const Node &, node);

        virtual tl::expected<void, Error> updateFingerTable(const std::string &remoteAddress, int index,
                                                            const Node &node, RequestConfig config) = 0;

        CHORD_CORE_DEFINE_DEFAULT_ARG_3_PARAM(updateFingerTable, const std::string &, remoteAddress, int, index,
                                              const Node &, node);

        virtual tl::expected<void, Error>
        predecessorLeave(const std::string &remoteAddress, const Node &predecessor, RequestConfig config) =
        0;

        CHORD_CORE_DEFINE_DEFAULT_ARG_2_PARAM(predecessorLeave, const std::string &, remoteAddress,
                                              const Node &, predecessor);

        virtual void setCallbacks(std::shared_ptr<ChordCallbacks> callbacks) = 0;
    };

    std::shared_ptr<ChordNetwork> createFullNetwork();

    std::shared_ptr<ChordApplicationNetwork> createApplicationNetwork();
} // namespace chord::core
