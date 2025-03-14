#pragma once

#include <array>
#include <functional>
#include <optional>

#include "stdint.h"
#include <string>
#include <vector>

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


    enum class Error
    {
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

    /**
     * @brief Configuration settings for the Chord node.
     */
    struct Config
    {
        uint64_t successorListSize = 1; ///< Size of the successor list
        uint64_t stabilizationInterval = 200; ///< Interval for stabilization in milliseconds
        uint64_t fingerFixingInterval = 200; ///< Interval for fixing fingers in milliseconds
    };

    /**
    * @brief Represents a server for a Chord node.
    */
    class NodeServer
    {
    public:
        explicit NodeServer(Node node);
        ~NodeServer();

        void start(std::optional<Node> entryPoint);
        void leave();

        Node lookup(KeyId id);

        [[nodiscard]] Node getSuccessor() const;
        [[nodiscard]] std::vector<Node> getSuccessorList() const;
        [[nodiscard]] KeyRange getKeyRange() const;

        using PredecessorLeaveCallback = std::function<void(Node)>;
        using SuccessorListChangeCallback = std::function<void(const std::vector<Node>&)>;
    };
} // namespace chord::core
