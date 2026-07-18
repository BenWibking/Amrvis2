#include <amrvis/query/LineQuery.hpp>
#include <amrvis/query/SliceQuery.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view realDescriptor =
    "((8, (64 11 52 0 1 12 0 1023)),(8, (8 7 6 5 4 3 2 1)))";

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

// The tests/data fixtures carry metadata only; this adds FAB payloads at the
// exact FabOnDisk offsets their Cell_H indices record, with analytic values
// consistent with the per-grid min/max statistics in those indices.
void writeFab(const std::filesystem::path& path, std::uint64_t offset,
    std::string_view box, int components, const std::vector<double>& values)
{
    if (offset == 0) {
        std::ofstream create(path, std::ios::binary | std::ios::trunc);
        require(static_cast<bool>(create), "could not create line fixture FAB");
    }
    std::fstream output(path, std::ios::binary | std::ios::in | std::ios::out);
    require(static_cast<bool>(output), "could not open line fixture FAB");
    output.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
    output << "FAB " << realDescriptor << box << " " << components << "\n";
    output.write(reinterpret_cast<const char*>(values.data()),
        static_cast<std::streamsize>(values.size() * sizeof(double)));
    require(static_cast<bool>(output), "could not write line fixture FAB payload");
}

// 2-D fixture fields: density(i, j) = (i + j) / 2, temperature = 100 + density,
// where i and j are cell indices at the level storing the grid.
std::vector<double> field2d(int i0, int i1, int j0, int j1, int component)
{
    std::vector<double> values;
    for (int j = j0; j <= j1; ++j) {
        for (int i = i0; i <= i1; ++i) {
            const auto density = 0.5 * static_cast<double>(i + j);
            values.push_back(component == 0 ? density : 100.0 + density);
        }
    }
    return values;
}

std::vector<double> bothComponents2d(int i0, int i1, int j0, int j1)
{
    auto values = field2d(i0, i1, j0, j1, 0);
    const auto temperature = field2d(i0, i1, j0, j1, 1);
    values.insert(values.end(), temperature.begin(), temperature.end());
    return values;
}

// 3-D fixture field: q(i, j, k) = (i + j + k) / 9.
std::vector<double> field3d()
{
    std::vector<double> values;
    for (int k = 0; k <= 3; ++k) {
        for (int j = 0; j <= 3; ++j) {
            for (int i = 0; i <= 3; ++i) {
                values.push_back(static_cast<double>(i + j + k) / 9.0);
            }
        }
    }
    return values;
}

std::filesystem::path materializeFixture(
    const std::filesystem::path& source, const std::filesystem::path& root)
{
    std::filesystem::copy(source, root, std::filesystem::copy_options::recursive);
    return root;
}

void test2d(const std::filesystem::path& source, const std::filesystem::path& work)
{
    const auto root = materializeFixture(source, work / "plotfile_2d");
    writeFab(root / "Level_0" / "Cell_D_00000", 0,
        "((0,0) (1,3) (0,0))", 2, bothComponents2d(0, 1, 0, 3));
    writeFab(root / "Level_0" / "Cell_D_00000", 4096,
        "((2,0) (3,3) (0,0))", 2, bothComponents2d(2, 3, 0, 3));
    writeFab(root / "Level_1" / "Cell_D_00000", 0,
        "((2,2) (5,5) (0,0))", 2, bothComponents2d(2, 5, 2, 5));
    const auto payloadBytes = std::filesystem::file_size(root / "Level_0" / "Cell_D_00000")
        + std::filesystem::file_size(root / "Level_1" / "Cell_D_00000");

    amrvis::PlotfileDataset dataset(root, amrvis::DatasetId{11}, 1024 * 1024);
    amrvis::LineQuery lines(dataset);

    amrvis::LineRequest request;
    request.dataset.value = 11;
    request.field.value = 0;
    request.axis = 0;
    request.fixedCoordinates = {0.0, 0.5, 0.0};
    request.maximumLevel = 1;

    const auto composite = lines.execute(request);
    require(composite.line.axis == 0, "line result axis mismatch");
    require(composite.line.positions.size() == 8, "line sample count mismatch");
    require(composite.line.values.size() == 8 && composite.line.valid.size() == 8
            && composite.line.sourceLevel.size() == 8,
        "line result arrays disagree in size");
    require(composite.metrics.candidateBlocks == 3,
        "line query did not consider all three intersecting blocks");
    require(composite.metrics.blocksRead == 3 && composite.metrics.cacheHits == 0,
        "first line query did not read the three intersecting blocks");
    require(composite.metrics.payloadBytesRead > 0
            && composite.metrics.payloadBytesRead * 2 < payloadBytes,
        "line query payload accounting is not far below the full dataset");
    for (std::size_t sample = 0; sample < 8; ++sample) {
        const auto s = static_cast<double>(sample);
        require(composite.line.positions[sample] == (s + 0.5) * 0.125,
            "line sample is not at a fine cell center");
        const bool fine = sample >= 2 && sample <= 5;
        const auto expected = static_cast<float>(
            fine ? 0.5 * (s + 4.0) : 0.5 * (std::floor(s / 2.0) + 2.0));
        require(composite.line.valid[sample] == 1, "composite line left a coarse hole");
        require(composite.line.values[sample] == expected,
            "fine-over-coarse line value mismatch");
        require(composite.line.sourceLevel[sample] == (fine ? 1 : 0),
            "line source-level mask mismatch");
    }

    const auto cached = lines.execute(request);
    require(cached.metrics.blocksRead == 0 && cached.metrics.cacheHits == 3,
        "repeated line query did not reuse all three blocks");
    require(cached.metrics.payloadBytesRead == 0, "cached line query performed payload I/O");

    // Cross-check against a slice whose single pixel row covers the same cells:
    // pixel centers fall at y = 0.5625, inside fine cell j = 4 / coarse cell j = 2,
    // matching the line's fixed y = 0.5.
    amrvis::SliceQuery slices(dataset);
    amrvis::SliceRequest sliceRequest;
    sliceRequest.dataset.value = 11;
    sliceRequest.field.value = 0;
    sliceRequest.normalDirection = 1;
    sliceRequest.visibleRegion = {{{0.0, 0.5, 0.0}}, {{1.0, 0.625, 0.0}}};
    sliceRequest.maximumLevel = 1;
    sliceRequest.outputSize = {8, 1};
    const auto plane = slices.execute(sliceRequest);
    for (std::size_t sample = 0; sample < 8; ++sample) {
        require(plane.plane.valid[sample] == composite.line.valid[sample]
                && plane.plane.values[sample] == composite.line.values[sample]
                && plane.plane.sourceLevel[sample] == composite.line.sourceLevel[sample],
            "line and slice disagree on their common row");
    }

    // The second field shares the grids with an offset of 100.
    request.field.value = 1;
    const auto temperature = lines.execute(request);
    for (std::size_t sample = 0; sample < 8; ++sample) {
        require(temperature.line.values[sample] == 100.0F + composite.line.values[sample],
            "second field line value mismatch");
    }
    request.field.value = 0;

    // A line outside the physical domain is entirely invalid.
    request.fixedCoordinates = {0.0, 1.5, 0.0};
    const auto outside = lines.execute(request);
    require(outside.line.positions.size() == 8, "outside line lost its positions");
    for (std::size_t sample = 0; sample < 8; ++sample) {
        require(outside.line.valid[sample] == 0, "outside line reported coverage");
        require(outside.line.values[sample] == 0.0F, "outside line invented a value");
        require(outside.line.sourceLevel[sample] == -1,
            "outside line reported a source level");
    }

    // Exact-level composition on level 1 leaves holes outside the fine grid.
    request.fixedCoordinates = {0.0, 0.5, 0.0};
    request.composition = amrvis::CompositionPolicy::ExactLevel;
    const auto exactFine = lines.execute(request);
    for (std::size_t sample = 0; sample < 8; ++sample) {
        const bool fine = sample >= 2 && sample <= 5;
        require(exactFine.line.valid[sample] == (fine ? 1 : 0),
            "exact-level line filled a fine-level hole");
        if (fine) {
            require(exactFine.line.sourceLevel[sample] == 1,
                "exact-level line missed fine data");
        }
    }

    // Exact-level composition on level 0 samples the coarse mesh and ignores
    // the fine grid even where it covers the line.
    request.maximumLevel = 0;
    const auto exactCoarse = lines.execute(request);
    require(exactCoarse.line.positions.size() == 4, "exact coarse line sample count");
    for (std::size_t sample = 0; sample < 4; ++sample) {
        const auto s = static_cast<double>(sample);
        require(exactCoarse.line.positions[sample] == (s + 0.5) * 0.25,
            "exact coarse line position mismatch");
        require(exactCoarse.line.valid[sample] == 1
                && exactCoarse.line.sourceLevel[sample] == 0,
            "exact coarse line coverage mismatch");
        require(exactCoarse.line.values[sample] == static_cast<float>(0.5 * (s + 2.0)),
            "exact coarse line used fine data");
    }

    // A second dataset over the same files tracks selective reads independently.
    amrvis::PlotfileDataset selective(root, amrvis::DatasetId{12}, 1024 * 1024);
    amrvis::LineQuery selectiveLines(selective);
    amrvis::LineRequest alongY;
    alongY.dataset.value = 12;
    alongY.field.value = 0;
    alongY.axis = 1;
    alongY.fixedCoordinates = {0.125, 0.0, 0.0};
    alongY.maximumLevel = 1;
    const auto vertical = selectiveLines.execute(alongY);
    require(vertical.metrics.candidateBlocks == 1 && vertical.metrics.blocksRead == 1,
        "line along y touched grids away from the line");
    require(vertical.metrics.payloadBytesRead > 0
            && vertical.metrics.payloadBytesRead * 8 < payloadBytes,
        "line along y did not read far below the full dataset");
    for (std::size_t sample = 0; sample < 8; ++sample) {
        const auto s = static_cast<double>(sample);
        require(vertical.line.positions[sample] == (s + 0.5) * 0.125,
            "line along y position mismatch");
        require(vertical.line.valid[sample] == 1 && vertical.line.sourceLevel[sample] == 0,
            "line along y unexpectedly used the fine grid");
        require(vertical.line.values[sample] == static_cast<float>(0.5 * std::floor(s / 2.0)),
            "line along y value mismatch");
    }

    std::stop_source stopped;
    stopped.request_stop();
    bool cancelled = false;
    try {
        [[maybe_unused]] auto ignored = selectiveLines.execute(alongY, stopped.get_token());
    } catch (const amrvis::ReadCancelled&) {
        cancelled = true;
    }
    require(cancelled, "pre-cancelled line query proceeded");
}

void test3d(const std::filesystem::path& source, const std::filesystem::path& work)
{
    const auto root = materializeFixture(source, work / "plotfile_3d");
    writeFab(root / "Level_0" / "Cell_D_00000", 0,
        "((0,0,0) (3,3,3) (0,0,0))", 1, field3d());

    amrvis::PlotfileDataset dataset(root, amrvis::DatasetId{13}, 1024 * 1024);
    amrvis::LineQuery lines(dataset);
    amrvis::SliceQuery slices(dataset);

    // Line along x through y = z = 0.5 (coarse cells j = k = 2).
    amrvis::LineRequest request;
    request.dataset.value = 13;
    request.field.value = 0;
    request.axis = 0;
    request.fixedCoordinates = {0.0, 0.5, 0.5};
    request.maximumLevel = 0;
    const auto alongX = lines.execute(request);
    require(alongX.line.positions.size() == 4, "3-D line sample count mismatch");
    require(alongX.metrics.blocksRead == 1 && alongX.metrics.payloadBytesRead > 0,
        "3-D line did not read its single block");
    for (std::size_t sample = 0; sample < 4; ++sample) {
        const auto s = static_cast<double>(sample);
        require(alongX.line.positions[sample] == (s + 0.5) * 0.25,
            "3-D line position mismatch");
        require(alongX.line.valid[sample] == 1 && alongX.line.sourceLevel[sample] == 0,
            "3-D line coverage mismatch");
        require(alongX.line.values[sample] == static_cast<float>((s + 4.0) / 9.0),
            "3-D line value mismatch");
    }

    // Cross-check against the y-normal slice whose single z row covers the line.
    amrvis::SliceRequest sliceRequest;
    sliceRequest.dataset.value = 13;
    sliceRequest.field.value = 0;
    sliceRequest.normalDirection = 1;
    sliceRequest.physicalPosition = 0.5;
    sliceRequest.visibleRegion = {{{0.0, 0.0, 0.5}}, {{1.0, 1.0, 0.75}}};
    sliceRequest.maximumLevel = 0;
    sliceRequest.outputSize = {4, 1};
    const auto planeX = slices.execute(sliceRequest);
    for (std::size_t sample = 0; sample < 4; ++sample) {
        require(planeX.plane.values[sample] == alongX.line.values[sample]
                && planeX.plane.valid[sample] == 1,
            "3-D line and y-normal slice disagree");
    }

    // Line along z through x = 0.25, y = 0.75 (coarse cells i = 1, j = 3).
    request.axis = 2;
    request.fixedCoordinates = {0.25, 0.75, 0.0};
    const auto alongZ = lines.execute(request);
    require(alongZ.line.axis == 2 && alongZ.line.positions.size() == 4,
        "3-D z line shape mismatch");
    for (std::size_t sample = 0; sample < 4; ++sample) {
        const auto s = static_cast<double>(sample);
        require(alongZ.line.positions[sample] == (s + 0.5) * 0.25,
            "3-D z line position mismatch");
        require(alongZ.line.valid[sample] == 1 && alongZ.line.sourceLevel[sample] == 0,
            "3-D z line coverage mismatch");
        require(alongZ.line.values[sample] == static_cast<float>((s + 4.0) / 9.0),
            "3-D z line value mismatch");
    }

    // Cross-check against the x-normal slice whose single y column covers the line.
    sliceRequest.normalDirection = 0;
    sliceRequest.physicalPosition = 0.25;
    sliceRequest.visibleRegion = {{{0.0, 0.75, 0.0}}, {{1.0, 1.0, 1.0}}};
    sliceRequest.outputSize = {1, 4};
    const auto planeZ = slices.execute(sliceRequest);
    for (std::size_t sample = 0; sample < 4; ++sample) {
        require(planeZ.plane.values[sample] == alongZ.line.values[sample]
                && planeZ.plane.valid[sample] == 1,
            "3-D line and x-normal slice disagree");
    }

    // A line outside the domain in z is entirely invalid.
    request.axis = 0;
    request.fixedCoordinates = {0.0, 0.5, 2.0};
    const auto outside = lines.execute(request);
    for (std::size_t sample = 0; sample < 4; ++sample) {
        require(outside.line.valid[sample] == 0 && outside.line.sourceLevel[sample] == -1,
            "3-D outside line reported coverage");
    }
}

} // namespace

int main(int argc, char* argv[])
{
    require(argc == 3, "two test data path arguments are required");
    const std::filesystem::path plotfile2d(argv[1]);
    const std::filesystem::path plotfile3d(argv[2]);
    require(std::filesystem::is_regular_file(plotfile2d / "Header"),
        "2-D fixture is missing its Header");
    require(std::filesystem::is_regular_file(plotfile3d / "Header"),
        "3-D fixture is missing its Header");

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto work = std::filesystem::temp_directory_path()
        / ("amrvis2-line-query-" + std::to_string(unique));
    std::filesystem::create_directories(work);

    test2d(plotfile2d, work);
    test3d(plotfile3d, work);

    std::filesystem::remove_all(work);
    return 0;
}
