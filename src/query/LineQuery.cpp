#include <amrvis/query/LineQuery.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace amrvis {
namespace {

struct LoadedBlock {
    IntBox validBox;
    PlotfileDataset::BlockCache::Handle data;
};

bool intersects(const IntBox& left, const IntBox& right, int dimension)
{
    for (int axis = 0; axis < dimension; ++axis) {
        const auto i = static_cast<std::size_t>(axis);
        if (left.upper[i] < right.lower[i] || right.upper[i] < left.lower[i]) {
            return false;
        }
    }
    return true;
}

bool contains(const IntBox& box, const Int3& point, int dimension)
{
    for (int axis = 0; axis < dimension; ++axis) {
        const auto i = static_cast<std::size_t>(axis);
        if (point[i] < box.lower[i] || point[i] > box.upper[i]) {
            return false;
        }
    }
    return true;
}

int physicalToIndex(double position, const DatasetMetadata& metadata,
    const LevelMetadata& level, int axis)
{
    const auto i = static_cast<std::size_t>(axis);
    const auto relative = (position - metadata.physicalDomain.lower[i]) / level.cellSize[i];
    const auto offset = std::floor(relative);
    if (offset < static_cast<double>(std::numeric_limits<int>::min())
        || offset > static_cast<double>(std::numeric_limits<int>::max())) {
        throw std::out_of_range("line coordinate exceeds index range");
    }
    const auto index = static_cast<std::int64_t>(level.domain.lower[i])
        + static_cast<std::int64_t>(offset);
    if (index < std::numeric_limits<int>::min()
        || index > std::numeric_limits<int>::max()) {
        throw std::out_of_range("line coordinate plus domain offset exceeds index range");
    }
    return static_cast<int>(index);
}

std::size_t valueOffset(const IntBox& box, const Int3& point, int dimension)
{
    const auto extent = [&box](std::size_t axis) {
        const auto value = static_cast<std::int64_t>(box.upper[axis])
            - box.lower[axis] + 1;
        if (value <= 0) {
            throw std::overflow_error("FAB extent is not positive");
        }
        return static_cast<std::uint64_t>(value);
    };
    const auto relative = [&box, &point](std::size_t axis) {
        const auto value = static_cast<std::int64_t>(point[axis]) - box.lower[axis];
        if (value < 0) {
            throw std::overflow_error("FAB point precedes its indexed box");
        }
        return static_cast<std::uint64_t>(value);
    };
    const auto nx = extent(0);
    const auto x = relative(0);
    if (dimension == 1) {
        return static_cast<std::size_t>(x);
    }
    const auto ny = extent(1);
    const auto y = relative(1);
    if (dimension == 2) {
        if (y > (std::numeric_limits<std::uint64_t>::max() - x) / nx) {
            throw std::overflow_error("2-D FAB offset overflows");
        }
        const auto offset = x + nx * y;
        if (offset > std::numeric_limits<std::size_t>::max()) {
            throw std::overflow_error("2-D FAB offset exceeds addressable memory");
        }
        return static_cast<std::size_t>(offset);
    }
    const auto z = relative(2);
    if (z > (std::numeric_limits<std::uint64_t>::max() - y) / ny) {
        throw std::overflow_error("3-D FAB row offset overflows");
    }
    const auto row = y + ny * z;
    if (row > (std::numeric_limits<std::uint64_t>::max() - x) / nx) {
        throw std::overflow_error("3-D FAB offset overflows");
    }
    const auto offset = x + nx * row;
    if (offset > std::numeric_limits<std::size_t>::max()) {
        throw std::overflow_error("3-D FAB offset exceeds addressable memory");
    }
    return static_cast<std::size_t>(offset);
}

} // namespace

LineQueryResult LineQuery::execute(
    const LineRequest& request, std::stop_token cancellation)
{
    const auto& metadata = m_dataset.metadata();
    const auto errors = validateLineRequest(request, metadata.dimension);
    if (!errors.empty()) {
        throw std::invalid_argument(errors.front());
    }
    if (request.dataset != m_dataset.id()) {
        throw std::invalid_argument("line request targets a different dataset");
    }
    if (request.field.value >= metadata.fields.size()) {
        throw std::invalid_argument("line field is unavailable");
    }
    if (request.component != 0) {
        throw std::invalid_argument("the initial plotfile fields are scalar");
    }

    const auto maximumLevel = std::min(request.maximumLevel, metadata.finestLevel);
    const auto minimumLevel = request.composition == CompositionPolicy::ExactLevel
        ? maximumLevel : 0;
    const auto& samplingLevel = metadata.levels[static_cast<std::size_t>(maximumLevel)];
    const auto lineAxis = static_cast<std::size_t>(request.axis);

    const auto cellCount = static_cast<std::int64_t>(samplingLevel.domain.upper[lineAxis])
        - samplingLevel.domain.lower[lineAxis] + 1;
    if (cellCount <= 0
        || static_cast<std::uint64_t>(cellCount)
            > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::overflow_error("line sample count exceeds addressable memory");
    }
    const auto sampleCount = static_cast<std::size_t>(cellCount);

    LineQueryResult result;
    result.line.axis = request.axis;
    result.line.positions.resize(sampleCount);
    result.line.values.assign(sampleCount, 0.0F);
    result.line.valid.assign(sampleCount, 0);
    result.line.sourceLevel.assign(sampleCount, -1);
    for (std::size_t sample = 0; sample < sampleCount; ++sample) {
        result.line.positions[sample] = metadata.physicalDomain.lower[lineAxis]
            + (static_cast<double>(sample) + 0.5) * samplingLevel.cellSize[lineAxis];
    }

    for (int levelIndex = maximumLevel; levelIndex >= minimumLevel; --levelIndex) {
        if (cancellation.stop_requested()) {
            throw ReadCancelled();
        }
        const auto& level = metadata.levels[static_cast<std::size_t>(levelIndex)];
        auto lineBox = level.domain;
        for (int axis = 0; axis < metadata.dimension; ++axis) {
            if (axis == request.axis) {
                continue;
            }
            const auto i = static_cast<std::size_t>(axis);
            const auto index = physicalToIndex(
                request.fixedCoordinates[i], metadata, level, axis);
            lineBox.lower[i] = index;
            lineBox.upper[i] = index;
        }

        std::vector<LoadedBlock> loaded;
        for (std::size_t grid = 0; grid < level.blocks.size(); ++grid) {
            const auto& block = level.blocks[grid];
            if (!intersects(block.box, lineBox, metadata.dimension)) {
                continue;
            }
            ++result.metrics.candidateBlocks;
            BlockRequest blockRequest;
            blockRequest.dataset = request.dataset;
            blockRequest.level = levelIndex;
            blockRequest.gridIndex = static_cast<int>(grid);
            blockRequest.field = request.field;
            auto access = m_dataset.requestBlock(blockRequest, cancellation);
            if (access.cacheHit) {
                ++result.metrics.cacheHits;
            } else {
                ++result.metrics.blocksRead;
                result.metrics.payloadBytesRead += access.io.bytesRead;
            }
            loaded.push_back({block.box, std::move(access.handle)});
        }

        for (std::size_t sample = 0; sample < sampleCount; ++sample) {
            if ((sample & 31U) == 0U && cancellation.stop_requested()) {
                throw ReadCancelled();
            }
            if (result.line.valid[sample] != 0) {
                continue;
            }

            Real3 position;
            for (int axis = 0; axis < metadata.dimension; ++axis) {
                position[static_cast<std::size_t>(axis)] =
                    request.fixedCoordinates[static_cast<std::size_t>(axis)];
            }
            position[lineAxis] = result.line.positions[sample];

            Int3 point;
            for (int axis = 0; axis < metadata.dimension; ++axis) {
                point[static_cast<std::size_t>(axis)] = physicalToIndex(
                    position[static_cast<std::size_t>(axis)], metadata, level, axis);
            }
            for (const auto& block : loaded) {
                if (!contains(block.validBox, point, metadata.dimension)) {
                    continue;
                }
                const auto offset = valueOffset(block.data->box, point, metadata.dimension);
                if (offset >= block.data->values.size()) {
                    throw std::runtime_error("composed FAB index exceeds loaded block");
                }
                result.line.values[sample] = static_cast<float>(block.data->values[offset]);
                result.line.valid[sample] = 1;
                result.line.sourceLevel[sample] = static_cast<std::int16_t>(levelIndex);
                break;
            }
        }
    }
    return result;
}

} // namespace amrvis
