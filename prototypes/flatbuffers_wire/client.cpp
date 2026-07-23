#include "Frame.hpp"
#include "amrvis_wire_generated.h"

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

#include <algorithm>
#include <csignal>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace fb = amrvis::wire;
namespace transport = amrvis::wire::prototype;

struct Received {
    std::vector<std::uint8_t> bytes;
    const fb::Envelope* envelope = nullptr;
};

[[nodiscard]] std::uint16_t parsePort(const char* text) {
    const auto value = std::stoul(text);
    if (value == 0 || value > 65535U) {
        throw std::invalid_argument("port must be in the range 1..65535");
    }
    return static_cast<std::uint16_t>(value);
}

void sendBuilder(const transport::Socket& socket, const flatbuffers::FlatBufferBuilder& builder) {
    transport::writeFrame(
        socket, std::span<const std::uint8_t>(builder.GetBufferPointer(), builder.GetSize()));
}

[[nodiscard]] Received receive(const transport::Socket& socket, std::uint64_t requestId) {
    Received result;
    result.bytes = transport::readFrame(socket);
    if (result.bytes.size() < 8 || !fb::EnvelopeBufferHasIdentifier(result.bytes.data())) {
        throw std::runtime_error("response is not an AVR2 FlatBuffer");
    }
    flatbuffers::Verifier verifier(result.bytes.data(), result.bytes.size());
    if (!fb::VerifyEnvelopeBuffer(verifier)) {
        throw std::runtime_error("response FlatBuffer failed verification");
    }
    result.envelope = fb::GetEnvelope(result.bytes.data());
    if (result.envelope->request_id() != requestId) {
        throw std::runtime_error("response request ID does not match");
    }
    if (result.envelope->protocol_major() != transport::protocolMajor) {
        throw std::runtime_error("server selected an unsupported protocol");
    }
    if (const auto* error = result.envelope->payload_as_ErrorResponse()) {
        throw std::runtime_error(
            "remote error " + std::to_string(error->code()) + ": " +
            (error->message() == nullptr ? std::string("unspecified") : error->message()->str()));
    }
    return result;
}

void printVector(const char* name, const flatbuffers::Vector<double>* values) {
    std::cout << "  " << name << ": [";
    if (values != nullptr) {
        for (flatbuffers::uoffset_t index = 0; index < values->size(); ++index) {
            if (index != 0) {
                std::cout << ", ";
            }
            std::cout << values->Get(index);
        }
    }
    std::cout << "]\n";
}

} // namespace

int main(int argc, char* argv[]) {
    std::signal(SIGPIPE, SIG_IGN);
    try {
        if (argc != 4) {
            throw std::invalid_argument("usage: amrvis_wire_client HOST PORT PLOTFILE");
        }
        const std::string host(argv[1]);
        const auto port = parsePort(argv[2]);
        const std::filesystem::path plotfile(argv[3]);
        auto socket = transport::connectTo(host, port);

        constexpr std::uint64_t helloId = 1;
        {
            flatbuffers::FlatBufferBuilder builder;
            const auto name = builder.CreateString("amrvis_wire_client prototype");
            const auto request = fb::CreateHelloRequest(builder, name);
            const auto envelope =
                fb::CreateEnvelope(builder, transport::protocolMajor, transport::protocolMinor,
                                   helloId, fb::Payload_HelloRequest, request.Union());
            fb::FinishEnvelopeBuffer(builder, envelope);
            sendBuilder(socket, builder);
        }
        const auto hello = receive(socket, helloId);
        const auto* helloResponse = hello.envelope->payload_as_HelloResponse();
        if (helloResponse == nullptr || helloResponse->server_name() == nullptr) {
            throw std::runtime_error("server did not return a hello response");
        }
        std::cout << "HANDSHAKE\n"
                  << "  protocol: " << hello.envelope->protocol_major() << '.'
                  << hello.envelope->protocol_minor() << '\n'
                  << "  server: " << helloResponse->server_name()->str() << '\n';

        constexpr std::uint64_t openId = 2;
        {
            flatbuffers::FlatBufferBuilder builder;
            const auto path = builder.CreateString(plotfile.string());
            const auto request = fb::CreateOpenDatasetRequest(builder, path);
            const auto envelope =
                fb::CreateEnvelope(builder, transport::protocolMajor, transport::protocolMinor,
                                   openId, fb::Payload_OpenDatasetRequest, request.Union());
            fb::FinishEnvelopeBuffer(builder, envelope);
            sendBuilder(socket, builder);
        }
        const auto opened = receive(socket, openId);
        const auto* metadata = opened.envelope->payload_as_DatasetOpened();
        if (metadata == nullptr || metadata->domain_lower() == nullptr ||
            metadata->domain_upper() == nullptr || metadata->domain_lower()->size() != 3 ||
            metadata->domain_upper()->size() != 3) {
            throw std::runtime_error("server returned malformed metadata");
        }
        std::cout << "DATASET\n"
                  << "  id: " << metadata->dataset_id() << '\n'
                  << "  dimension: " << metadata->dimension() << '\n'
                  << "  finest_level: " << metadata->finest_level() << '\n'
                  << "  time: " << metadata->time() << '\n'
                  << "  fields: "
                  << (metadata->fields() == nullptr ? 0 : metadata->fields()->size()) << '\n';
        printVector("domain_lower", metadata->domain_lower());
        printVector("domain_upper", metadata->domain_upper());

        constexpr std::uint64_t sliceId = 3;
        const auto normal = std::max(0, metadata->dimension() - 1);
        const auto normalIndex = static_cast<flatbuffers::uoffset_t>(normal);
        const auto position = 0.5 * (metadata->domain_lower()->Get(normalIndex) +
                                     metadata->domain_upper()->Get(normalIndex));
        {
            flatbuffers::FlatBufferBuilder builder;
            const auto lower = builder.CreateVector(metadata->domain_lower()->data(),
                                                    metadata->domain_lower()->size());
            const auto upper = builder.CreateVector(metadata->domain_upper()->data(),
                                                    metadata->domain_upper()->size());
            const auto request = fb::CreateSliceRequest(
                builder, metadata->dataset_id(), 0, normal, position, lower, upper,
                metadata->finest_level(), 8, 6, fb::SamplingPolicy_PiecewiseConstant,
                fb::CompositionPolicy_FinestAvailable);
            const auto envelope =
                fb::CreateEnvelope(builder, transport::protocolMajor, transport::protocolMinor,
                                   sliceId, fb::Payload_SliceRequest, request.Union());
            fb::FinishEnvelopeBuffer(builder, envelope);
            sendBuilder(socket, builder);
        }
        const auto sliced = receive(socket, sliceId);
        const auto* plane = sliced.envelope->payload_as_ScalarPlane();
        if (plane == nullptr || plane->values() == nullptr || plane->valid() == nullptr ||
            plane->values()->size() != plane->valid()->size()) {
            throw std::runtime_error("server returned a malformed scalar plane");
        }

        auto minimum = std::numeric_limits<float>::infinity();
        auto maximum = -std::numeric_limits<float>::infinity();
        std::size_t validCount = 0;
        for (flatbuffers::uoffset_t index = 0; index < plane->values()->size(); ++index) {
            if (plane->valid()->Get(index) == 0) {
                continue;
            }
            ++validCount;
            minimum = std::min(minimum, plane->values()->Get(index));
            maximum = std::max(maximum, plane->values()->Get(index));
        }
        std::cout << "SLICE\n"
                  << "  shape: " << plane->width() << " x " << plane->height() << '\n'
                  << "  valid_values: " << validCount << '\n'
                  << std::setprecision(7) << "  minimum: " << minimum << '\n'
                  << "  maximum: " << maximum << '\n'
                  << "  blocks_read: " << plane->blocks_read() << '\n'
                  << "  cache_hits: " << plane->cache_hits() << '\n'
                  << "  payload_bytes_read: " << plane->payload_bytes_read() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "client error: " << error.what() << '\n';
        return 1;
    }
}
