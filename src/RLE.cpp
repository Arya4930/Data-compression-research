#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

namespace fs = std::filesystem;

// ============================================================
// CONFIGURATION
// ============================================================

constexpr int MAX_THREADS = 22;
constexpr int TIMING_ITERATIONS = 100;
constexpr int PROGRESS_WIDTH = 40;

// ============================================================
// DATASETS
// ============================================================

const std::vector<std::string> DATASETS = {
    "aaa.txt",
    "alphabet.txt",
    "dickens",
    "mozilla",
    "mr",
    "nci",
    "ooffice",
    "osdb",
    "random.txt",
    "reymont",
    "samba",
    "sao",
    "webster",
    "x-ray",
    "xml"
};

// ============================================================
// PATHS
// ============================================================

fs::path DATASET_DIR;
fs::path RESULT_ROOT;
fs::path RUN_DIR;
fs::path COMPRESSED_DIR;
fs::path LOG_DIR;

// ============================================================
// RLE STRUCTURES
// ============================================================

struct RLEPair {

    uint8_t value;
    uint64_t count;
};

struct RLECompressed {

    std::vector<RLEPair> runs;
};

// ============================================================
// RESULT
// ============================================================

struct Result {

    std::string dataset;

    std::string algorithm = "RLE";

    int threads = 1;

    uint64_t originalBytes = 0;
    uint64_t compressedBytes = 0;

    double entropy = 0.0;

    double compressionRatio = 0.0;
    double spaceSaved = 0.0;

    double compressionTime = 0.0;
    double decompressionTime = 0.0;

    double compressionThroughput = 0.0;
    double decompressionThroughput = 0.0;

    double memoryMB = 0.0;

    bool lossless = false;
};

// ============================================================
// TIMESTAMP
// ============================================================

std::string getTimestamp()
{
    auto now =
        std::chrono::system_clock::now();

    std::time_t time =
        std::chrono::system_clock::to_time_t(
            now
        );

    std::tm tm{};

#ifdef _WIN32

    localtime_s(
        &tm,
        &time
    );

#else

    localtime_r(
        &time,
        &tm
    );

#endif

    std::ostringstream out;

    out
        << std::put_time(
            &tm,
            "%Y%m%d_%H%M%S"
        );

    return out.str();
}

// ============================================================
// INITIALIZE PATHS
// ============================================================

void initializePaths()
{
    /*
        RLE executable is expected to be run from:

            research-paper/src

        Therefore:

            ../datasets
            ../results
    */

    DATASET_DIR =
        fs::absolute(
            fs::path("../datasets")
        );

    RESULT_ROOT =
        fs::absolute(
            fs::path("../results")
        );

    if (!fs::exists(DATASET_DIR)) {

        throw std::runtime_error(
            "Dataset directory does not exist:\n"
            + DATASET_DIR.string()
        );
    }

    fs::create_directories(
        RESULT_ROOT
    );
}

// ============================================================
// READ FILE
// ============================================================

std::vector<uint8_t> readFile(
    const fs::path& path
)
{
    std::ifstream file(
        path,
        std::ios::binary
    );

    if (!file) {

        throw std::runtime_error(
            "Cannot open file:\n"
            + path.string()
        );
    }

    file.seekg(
        0,
        std::ios::end
    );

    std::streamsize size =
        file.tellg();

    file.seekg(
        0,
        std::ios::beg
    );

    if (size <= 0) {

        return {};
    }

    std::vector<uint8_t> data(
        static_cast<size_t>(size)
    );

    if (
        !file.read(
            reinterpret_cast<char*>(
                data.data()
            ),
            size
        )
    ) {

        throw std::runtime_error(
            "Failed to read file:\n"
            + path.string()
        );
    }

    return data;
}

// ============================================================
// ENTROPY
// ============================================================

double calculateEntropy(
    const std::vector<uint8_t>& data
)
{
    if (data.empty()) {
        return 0.0;
    }

    uint64_t frequency[256] = {};

    for (uint8_t byte : data) {

        ++frequency[byte];
    }

    double entropy = 0.0;

    double total =
        static_cast<double>(
            data.size()
        );

    for (int i = 0; i < 256; ++i) {

        if (frequency[i] == 0) {
            continue;
        }

        double probability =
            static_cast<double>(
                frequency[i]
            )
            /
            total;

        entropy -=
            probability *
            std::log2(
                probability
            );
    }

    return entropy;
}

// ============================================================
// SERIAL COMPRESSION
// ============================================================

RLECompressed rleCompressSerial(
    const std::vector<uint8_t>& data
)
{
    RLECompressed result;

    if (data.empty()) {
        return result;
    }

    result.runs.reserve(
        data.size() / 2 + 1
    );

    uint8_t current =
        data[0];

    uint64_t count = 1;

    for (
        size_t i = 1;
        i < data.size();
        ++i
    ) {

        if (data[i] == current) {

            ++count;
        }
        else {

            result.runs.push_back({
                current,
                count
            });

            current =
                data[i];

            count = 1;
        }
    }

    result.runs.push_back({
        current,
        count
    });

    return result;
}

// ============================================================
// CHUNK
// ============================================================

struct ChunkResult {

    size_t start = 0;
    size_t end = 0;

    std::vector<RLEPair> runs;
};

// ============================================================
// COMPRESS CHUNK
// ============================================================

void compressChunk(
    const std::vector<uint8_t>& data,
    size_t start,
    size_t end,
    ChunkResult& result
)
{
    result.start = start;
    result.end = end;

    result.runs.clear();

    if (start >= end) {
        return;
    }

    uint8_t current =
        data[start];

    uint64_t count = 1;

    for (
        size_t i = start + 1;
        i < end;
        ++i
    ) {

        if (data[i] == current) {

            ++count;
        }
        else {

            result.runs.push_back({
                current,
                count
            });

            current =
                data[i];

            count = 1;
        }
    }

    result.runs.push_back({
        current,
        count
    });
}

// ============================================================
// MERGE CHUNKS
// ============================================================

RLECompressed mergeChunks(
    const std::vector<ChunkResult>& chunks
)
{
    RLECompressed result;

    size_t estimatedRuns = 0;

    for (const auto& chunk : chunks) {

        estimatedRuns +=
            chunk.runs.size();
    }

    result.runs.reserve(
        estimatedRuns
    );

    for (const auto& chunk : chunks) {

        for (const auto& run : chunk.runs) {

            if (
                !result.runs.empty()
                &&
                result.runs.back().value
                    ==
                run.value
            ) {

                result.runs.back().count +=
                    run.count;
            }
            else {

                result.runs.push_back(
                    run
                );
            }
        }
    }

    return result;
}

// ============================================================
// PARALLEL COMPRESSION
// ============================================================

RLECompressed rleCompressParallel(
    const std::vector<uint8_t>& data,
    int threads
)
{
    if (data.empty()) {
        return {};
    }

    threads =
        std::max(
            1,
            threads
        );

    threads =
        std::min<int>(
            threads,
            static_cast<int>(
                data.size()
            )
        );

    if (threads == 1) {

        return rleCompressSerial(
            data
        );
    }

    std::vector<ChunkResult> chunks(
        static_cast<size_t>(threads)
    );

    std::vector<std::thread> workers;

    workers.reserve(
        threads
    );

    size_t n =
        data.size();

    for (
        int t = 0;
        t < threads;
        ++t
    ) {

        size_t start =
            (
                n *
                static_cast<size_t>(t)
            )
            /
            static_cast<size_t>(
                threads
            );

        size_t end =
            (
                n *
                static_cast<size_t>(t + 1)
            )
            /
            static_cast<size_t>(
                threads
            );

        workers.emplace_back(
            [
                &data,
                &chunks,
                t,
                start,
                end
            ] {

                compressChunk(
                    data,
                    start,
                    end,
                    chunks[t]
                );
            }
        );
    }

    for (auto& worker : workers) {

        worker.join();
    }

    return mergeChunks(
        chunks
    );
}

// ============================================================
// VARIABLE-LENGTH COUNT SIZE
// ============================================================

size_t encodedCountSize(
    uint64_t value
)
{
    size_t size = 1;

    while (value >= 128) {

        value >>= 7;
        ++size;
    }

    return size;
}

// ============================================================
// COMPRESSED SIZE
// ============================================================

uint64_t calculateCompressedSize(
    const RLECompressed& compressed
)
{
    uint64_t size = 0;

    for (
        const auto& run :
        compressed.runs
    ) {

        size += 1;

        size +=
            encodedCountSize(
                run.count
            );
    }

    return size;
}

// ============================================================
// WRITE COMPRESSED FILE
//
// Format:
//
//     [1 byte value]
//     [variable-length count]
//
// repeated for every run.
// ============================================================

void writeCompressedFile(
    const fs::path& path,
    const RLECompressed& compressed
)
{
    std::ofstream file(
        path,
        std::ios::binary
    );

    if (!file) {

        throw std::runtime_error(
            "Cannot create compressed file:\n"
            + path.string()
        );
    }

    for (
        const auto& run :
        compressed.runs
    ) {

        file.put(
            static_cast<char>(
                run.value
            )
        );

        uint64_t value =
            run.count;

        while (value >= 128) {

            uint8_t byte =
                static_cast<uint8_t>(
                    (value & 0x7F)
                    | 0x80
                );

            file.put(
                static_cast<char>(
                    byte
                )
            );

            value >>= 7;
        }

        file.put(
            static_cast<char>(
                value
            )
        );
    }

    if (!file) {

        throw std::runtime_error(
            "Failed writing compressed file:\n"
            + path.string()
        );
    }
}

// ============================================================
// SERIAL DECOMPRESSION
// ============================================================

std::vector<uint8_t> rleDecompressSerial(
    const RLECompressed& compressed
)
{
    uint64_t totalSize = 0;

    for (
        const auto& run :
        compressed.runs
    ) {

        totalSize +=
            run.count;
    }

    std::vector<uint8_t> output(
        static_cast<size_t>(
            totalSize
        )
    );

    size_t position = 0;

    for (
        const auto& run :
        compressed.runs
    ) {

        std::fill(
            output.begin() + position,
            output.begin()
                +
                position
                +
                static_cast<size_t>(
                    run.count
                ),
            run.value
        );

        position +=
            static_cast<size_t>(
                run.count
            );
    }

    return output;
}

// ============================================================
// PARALLEL DECOMPRESSION
// ============================================================

std::vector<uint8_t> rleDecompressParallel(
    const RLECompressed& compressed,
    int threads
)
{
    uint64_t totalSize = 0;

    for (
        const auto& run :
        compressed.runs
    ) {

        totalSize +=
            run.count;
    }

    std::vector<uint8_t> output(
        static_cast<size_t>(
            totalSize
        )
    );

    if (
        compressed.runs.empty()
    ) {

        return output;
    }

    threads =
        std::max(
            1,
            threads
        );

    threads =
        std::min<int>(
            threads,
            static_cast<int>(
                compressed.runs.size()
            )
        );

    if (threads == 1) {

        return rleDecompressSerial(
            compressed
        );
    }

    std::vector<size_t> runStarts(
        static_cast<size_t>(
            threads + 1
        )
    );

    for (
        int t = 0;
        t <= threads;
        ++t
    ) {

        runStarts[t] =
            (
                compressed.runs.size()
                *
                static_cast<size_t>(t)
            )
            /
            static_cast<size_t>(
                threads
            );
    }

    std::vector<size_t> outputOffsets(
        static_cast<size_t>(
            threads + 1
        )
    );

    outputOffsets[0] = 0;

    for (
        int t = 0;
        t < threads;
        ++t
    ) {

        size_t bytes = 0;

        for (
            size_t i = runStarts[t];
            i < runStarts[t + 1];
            ++i
        ) {

            bytes +=
                static_cast<size_t>(
                    compressed.runs[i].count
                );
        }

        outputOffsets[t + 1] =
            outputOffsets[t]
            +
            bytes;
    }

    std::vector<std::thread> workers;

    workers.reserve(
        threads
    );

    for (
        int t = 0;
        t < threads;
        ++t
    ) {

        size_t begin =
            runStarts[t];

        size_t end =
            runStarts[t + 1];

        size_t outputStart =
            outputOffsets[t];

        workers.emplace_back(
            [
                &compressed,
                &output,
                begin,
                end,
                outputStart
            ] {

                size_t position =
                    outputStart;

                for (
                    size_t i = begin;
                    i < end;
                    ++i
                ) {

                    const auto& run =
                        compressed.runs[i];

                    std::fill(
                        output.begin()
                            + position,
                        output.begin()
                            +
                            position
                            +
                            static_cast<size_t>(
                                run.count
                            ),
                        run.value
                    );

                    position +=
                        static_cast<size_t>(
                            run.count
                        );
                }
            }
        );
    }

    for (auto& worker : workers) {

        worker.join();
    }

    return output;
}

// ============================================================
// CURRENT MEMORY
// ============================================================

uint64_t getCurrentWorkingSet()
{
#ifdef _WIN32

    PROCESS_MEMORY_COUNTERS counters{};

    counters.cb =
        sizeof(counters);

    if (
        GetProcessMemoryInfo(
            GetCurrentProcess(),
            &counters,
            sizeof(counters)
        )
    ) {

        return
            static_cast<uint64_t>(
                counters.WorkingSetSize
            );
    }

#endif

    return 0;
}

// ============================================================
// PEAK MEMORY
// ============================================================

uint64_t getPeakWorkingSet()
{
#ifdef _WIN32

    PROCESS_MEMORY_COUNTERS counters{};

    counters.cb =
        sizeof(counters);

    if (
        GetProcessMemoryInfo(
            GetCurrentProcess(),
            &counters,
            sizeof(counters)
        )
    ) {

        return
            static_cast<uint64_t>(
                counters.PeakWorkingSetSize
            );
    }

#endif

    return 0;
}

// ============================================================
// PROGRESS
// ============================================================

void showProgress(
    int current,
    int total,
    const std::string& label
)
{
    double percent =
        total == 0
        ? 100.0
        :
        (
            static_cast<double>(
                current
            )
            /
            static_cast<double>(
                total
            )
            *
            100.0
        );

    int filled =
        total == 0
        ? PROGRESS_WIDTH
        :
        static_cast<int>(
            (
                static_cast<double>(
                    current
                )
                /
                static_cast<double>(
                    total
                )
            )
            *
            PROGRESS_WIDTH
        );

    filled =
        std::min(
            filled,
            PROGRESS_WIDTH
        );

    std::cout
        << "\r  "
        << std::left
        << std::setw(16)
        << label
        << " [";

    for (
        int i = 0;
        i < PROGRESS_WIDTH;
        ++i
    ) {

        std::cout
            << (
                i < filled
                    ? '#'
                    : '-'
            );
    }

    std::cout
        << "] "
        << std::right
        << std::fixed
        << std::setprecision(2)
        << std::setw(6)
        << percent
        << "%";

    std::cout.flush();

    if (
        current >= total
    ) {

        std::cout << '\n';
    }
}

// ============================================================
// CSV HEADER
// ============================================================

void writeCSVHeader(
    std::ofstream& file
)
{
    file
        << "dataset,"
        << "algorithm,"
        << "threads,"
        << "original_bytes,"
        << "compressed_bytes,"
        << "entropy,"
        << "compression_ratio,"
        << "space_saved_percent,"
        << "compression_time,"
        << "decompression_time,"
        << "compression_throughput_MB_s,"
        << "decompression_throughput_MB_s,"
        << "memory_MB,"
        << "lossless\n";
}

// ============================================================
// WRITE RUN CSV
// ============================================================

void writeCSV(
    const std::vector<Result>& results
)
{
    fs::path path =
        RUN_DIR / "results.csv";

    std::ofstream file(
        path
    );

    if (!file) {

        throw std::runtime_error(
            "Cannot create results CSV:\n"
            + path.string()
        );
    }

    writeCSVHeader(
        file
    );

    for (
        const auto& r :
        results
    ) {

        file
            << r.dataset
            << ","
            << r.algorithm
            << ","
            << r.threads
            << ","
            << r.originalBytes
            << ","
            << r.compressedBytes
            << ","
            << std::setprecision(12)
            << r.entropy
            << ","
            << r.compressionRatio
            << ","
            << r.spaceSaved
            << ","
            << r.compressionTime
            << ","
            << r.decompressionTime
            << ","
            << r.compressionThroughput
            << ","
            << r.decompressionThroughput
            << ","
            << r.memoryMB
            << ","
            << (
                r.lossless
                    ? "true"
                    : "false"
            )
            << "\n";
    }
}

// ============================================================
// APPEND MASTER CSV
// ============================================================

void appendMasterCSV(
    const std::vector<Result>& results
)
{
    fs::path path =
        RESULT_ROOT
        /
        "rle_all_runs.csv";

    bool exists =
        fs::exists(path)
        &&
        fs::file_size(path) > 0;

    std::ofstream file(
        path,
        std::ios::app
    );

    if (!file) {

        throw std::runtime_error(
            "Cannot open master CSV:\n"
            + path.string()
        );
    }

    if (!exists) {

        writeCSVHeader(
            file
        );
    }

    for (
        const auto& r :
        results
    ) {

        file
            << r.dataset
            << ","
            << r.algorithm
            << ","
            << r.threads
            << ","
            << r.originalBytes
            << ","
            << r.compressedBytes
            << ","
            << std::setprecision(12)
            << r.entropy
            << ","
            << r.compressionRatio
            << ","
            << r.spaceSaved
            << ","
            << r.compressionTime
            << ","
            << r.decompressionTime
            << ","
            << r.compressionThroughput
            << ","
            << r.decompressionThroughput
            << ","
            << r.memoryMB
            << ","
            << (
                r.lossless
                    ? "true"
                    : "false"
            )
            << "\n";
    }
}

// ============================================================
// WRITE JSON
// ============================================================

void writeJSON(
    const std::vector<Result>& results
)
{
    fs::path path =
        RUN_DIR / "results.json";

    std::ofstream file(
        path
    );

    if (!file) {

        throw std::runtime_error(
            "Cannot create results JSON:\n"
            + path.string()
        );
    }

    file
        << "{\n"
        << "  \"algorithm\": \"RLE\",\n"
        << "  \"threads\": "
        << (
            results.empty()
                ? 0
                : results[0].threads
        )
        << ",\n"
        << "  \"timing_iterations\": "
        << TIMING_ITERATIONS
        << ",\n"
        << "  \"datasets\": [\n";

    for (
        size_t i = 0;
        i < results.size();
        ++i
    ) {

        const auto& r =
            results[i];

        file
            << "    {\n"
            << "      \"dataset\": \""
            << r.dataset
            << "\",\n"

            << "      \"threads\": "
            << r.threads
            << ",\n"

            << "      \"original_bytes\": "
            << r.originalBytes
            << ",\n"

            << "      \"compressed_bytes\": "
            << r.compressedBytes
            << ",\n"

            << "      \"entropy\": "
            << std::setprecision(12)
            << r.entropy
            << ",\n"

            << "      \"compression_ratio\": "
            << r.compressionRatio
            << ",\n"

            << "      \"space_saved_percent\": "
            << r.spaceSaved
            << ",\n"

            << "      \"compression_time\": "
            << r.compressionTime
            << ",\n"

            << "      \"decompression_time\": "
            << r.decompressionTime
            << ",\n"

            << "      \"compression_throughput_MB_s\": "
            << r.compressionThroughput
            << ",\n"

            << "      \"decompression_throughput_MB_s\": "
            << r.decompressionThroughput
            << ",\n"

            << "      \"memory_MB\": "
            << r.memoryMB
            << ",\n"

            << "      \"lossless\": "
            << (
                r.lossless
                    ? "true"
                    : "false"
            )
            << "\n"

            << "    }";

        if (
            i + 1 < results.size()
        ) {

            file << ",";
        }

        file << "\n";
    }

    file
        << "  ]\n"
        << "}\n";
}

// ============================================================
// WRITE INDIVIDUAL LOG
// ============================================================

void writeLog(
    const Result& r
)
{
    fs::path logPath =
        LOG_DIR
        /
        (
            r.dataset
            +
            ".log"
        );

    std::ofstream file(
        logPath
    );

    if (!file) {

        throw std::runtime_error(
            "Cannot create log:\n"
            + logPath.string()
        );
    }

    file
        << "============================================================\n"
        << "RLE COMPRESSION EXPERIMENT\n"
        << "============================================================\n\n"

        << "Dataset: "
        << r.dataset
        << "\n"

        << "Algorithm: "
        << r.algorithm
        << "\n"

        << "Threads: "
        << r.threads
        << "\n"

        << "Timing iterations: "
        << TIMING_ITERATIONS
        << "\n\n"

        << "Original size: "
        << r.originalBytes
        << " bytes\n"

        << "Compressed size: "
        << r.compressedBytes
        << " bytes\n"

        << "Entropy: "
        << std::setprecision(12)
        << r.entropy
        << " bits/symbol\n"

        << "Compression ratio: "
        << r.compressionRatio
        << "\n"

        << "Space saved: "
        << r.spaceSaved
        << "%\n\n"

        << "Compression time per iteration: "
        << r.compressionTime
        << " seconds\n"

        << "Decompression time per iteration: "
        << r.decompressionTime
        << " seconds\n"

        << "Compression throughput: "
        << r.compressionThroughput
        << " MB/s\n"

        << "Decompression throughput: "
        << r.decompressionThroughput
        << " MB/s\n"

        << "Peak working set: "
        << r.memoryMB
        << " MB\n"

        << "Lossless verification: "
        << (
            r.lossless
                ? "PASS"
                : "FAIL"
        )
        << "\n\n"

        << "============================================================\n";
}

// ============================================================
// BENCHMARK
// ============================================================

Result runExperiment(
    const std::string& filename,
    int threads
)
{
    Result result;

    result.dataset =
        filename;

    result.threads =
        threads;

    fs::path inputPath =
        DATASET_DIR
        /
        filename;

    fs::path compressedPath =
        COMPRESSED_DIR
        /
        (
            filename
            +
            ".rle"
        );

    // --------------------------------------------------------
    // READ
    // --------------------------------------------------------

    std::vector<uint8_t> data =
        readFile(
            inputPath
        );

    result.originalBytes =
        data.size();

    result.entropy =
        calculateEntropy(
            data
        );

    std::cout
        << "  Original size: "
        << result.originalBytes
        << " bytes\n";

    // --------------------------------------------------------
    // WARMUP
    // --------------------------------------------------------

    std::cout
        << "  Warming up        ";

    showProgress(
        0,
        1,
        "Warming up"
    );

    RLECompressed warmCompressed =
        rleCompressParallel(
            data,
            threads
        );

    std::vector<uint8_t> warmDecoded =
        rleDecompressParallel(
            warmCompressed,
            threads
        );

    (void)warmDecoded;

    showProgress(
        1,
        1,
        "Warming up"
    );

    // --------------------------------------------------------
    // REFERENCE COMPRESSION
    // --------------------------------------------------------

    RLECompressed reference =
        rleCompressParallel(
            data,
            threads
        );

    result.compressedBytes =
        calculateCompressedSize(
            reference
        );

    // --------------------------------------------------------
    // COMPRESSION RATIO
    // --------------------------------------------------------

    if (
        result.originalBytes > 0
        &&
        result.compressedBytes > 0
    ) {

        result.compressionRatio =
            static_cast<double>(
                result.originalBytes
            )
            /
            static_cast<double>(
                result.compressedBytes
            );

        result.spaceSaved =
            (
                1.0
                -
                static_cast<double>(
                    result.compressedBytes
                )
                /
                static_cast<double>(
                    result.originalBytes
                )
            )
            *
            100.0;
    }

    // --------------------------------------------------------
    // COMPRESSION TIMING
    // --------------------------------------------------------

    std::cout
        << "  Compressing      ";

    showProgress(
        0,
        TIMING_ITERATIONS,
        "Compressing"
    );

    auto compressionStart =
        std::chrono::steady_clock::now();

    RLECompressed finalCompressed;

    for (
        int iteration = 0;
        iteration < TIMING_ITERATIONS;
        ++iteration
    ) {

        finalCompressed =
            rleCompressParallel(
                data,
                threads
            );

        if (
            iteration % 2 == 0
            ||
            iteration ==
                TIMING_ITERATIONS - 1
        ) {

            showProgress(
                iteration + 1,
                TIMING_ITERATIONS,
                "Compressing"
            );
        }
    }

    auto compressionEnd =
        std::chrono::steady_clock::now();

    double totalCompressionTime =
        std::chrono::duration<double>(
            compressionEnd
            -
            compressionStart
        ).count();

    result.compressionTime =
        totalCompressionTime
        /
        static_cast<double>(
            TIMING_ITERATIONS
        );

    // --------------------------------------------------------
    // SAVE COMPRESSED FILE
    // --------------------------------------------------------

    writeCompressedFile(
        compressedPath,
        finalCompressed
    );

    // --------------------------------------------------------
    // DECOMPRESSION
    // --------------------------------------------------------

    std::cout
        << "  Decompressing    ";

    showProgress(
        0,
        TIMING_ITERATIONS,
        "Decompressing"
    );

    auto decompressionStart =
        std::chrono::steady_clock::now();

    std::vector<uint8_t> finalDecoded;

    for (
        int iteration = 0;
        iteration < TIMING_ITERATIONS;
        ++iteration
    ) {

        finalDecoded =
            rleDecompressParallel(
                finalCompressed,
                threads
            );

        if (
            iteration % 2 == 0
            ||
            iteration ==
                TIMING_ITERATIONS - 1
        ) {

            showProgress(
                iteration + 1,
                TIMING_ITERATIONS,
                "Decompressing"
            );
        }
    }

    auto decompressionEnd =
        std::chrono::steady_clock::now();

    double totalDecompressionTime =
        std::chrono::duration<double>(
            decompressionEnd
            -
            decompressionStart
        ).count();

    result.decompressionTime =
        totalDecompressionTime
        /
        static_cast<double>(
            TIMING_ITERATIONS
        );

    // --------------------------------------------------------
    // LOSSLESS
    // --------------------------------------------------------

    result.lossless =
        (
            finalDecoded
            ==
            data
        );

    // --------------------------------------------------------
    // THROUGHPUT
    // --------------------------------------------------------

    double sizeMB =
        static_cast<double>(
            result.originalBytes
        )
        /
        (1024.0 * 1024.0);

    if (
        result.compressionTime > 0.0
    ) {

        result.compressionThroughput =
            sizeMB
            /
            result.compressionTime;
    }

    if (
        result.decompressionTime > 0.0
    ) {

        result.decompressionThroughput =
            sizeMB
            /
            result.decompressionTime;
    }

    // --------------------------------------------------------
    // MEMORY
    // --------------------------------------------------------

    result.memoryMB =
        static_cast<double>(
            getPeakWorkingSet()
        )
        /
        (1024.0 * 1024.0);

    return result;
}

// ============================================================
// PRINT RESULT
// ============================================================

void printResult(
    const Result& r
)
{
    std::cout
        << "\n"
        << "  Threads: "
        << r.threads
        << "\n"

        << "  Original size: "
        << r.originalBytes
        << " bytes\n"

        << "  Compressed size: "
        << r.compressedBytes
        << " bytes\n"

        << "  Compression ratio: "
        << std::fixed
        << std::setprecision(6)
        << r.compressionRatio
        << "\n"

        << "  Space saved: "
        << std::setprecision(2)
        << r.spaceSaved
        << "%\n"

        << "  Entropy: "
        << std::setprecision(6)
        << r.entropy
        << " bits/symbol\n"

        << "  Compression time (per iteration): "
        << std::setprecision(6)
        << r.compressionTime
        << " s\n"

        << "  Decompression time (per iteration): "
        << r.decompressionTime
        << " s\n"

        << "  Compression throughput: "
        << std::setprecision(2)
        << r.compressionThroughput
        << " MB/s\n"

        << "  Decompression throughput: "
        << r.decompressionThroughput
        << " MB/s\n"

        << "  Peak working set: "
        << r.memoryMB
        << " MB\n"

        << "  Lossless verification: "
        << (
            r.lossless
                ? "PASS"
                : "FAIL"
        )
        << "\n";
}

// ============================================================
// MAIN
// ============================================================

int main()
{
    try {

        // ----------------------------------------------------
        // PATHS
        // ----------------------------------------------------

        initializePaths();

        // ----------------------------------------------------
        // HARDWARE
        // ----------------------------------------------------

        unsigned int hardwareThreads =
            std::thread::hardware_concurrency();

        if (
            hardwareThreads == 0
        ) {

            hardwareThreads = 1;
        }

        int maxThreads =
            std::min(
                MAX_THREADS,
                static_cast<int>(
                    hardwareThreads
                )
            );

        // ----------------------------------------------------
        // HEADER
        // ----------------------------------------------------

        std::cout
            << "\n"
            << "============================================================\n"
            << "RLE COMPRESSION RESEARCH BENCHMARK\n"
            << "============================================================\n\n"

            << "Dataset directory:\n  "
            << DATASET_DIR
            << "\n\n"

            << "Detected hardware threads: "
            << hardwareThreads
            << "\n"

            << "Maximum benchmark threads: "
            << maxThreads
            << "\n";

        // ----------------------------------------------------
        // THREAD INPUT
        // ----------------------------------------------------

        int threads = 1;

        std::cout
            << "\n"
            << "How many threads should be used? [1-"
            << maxThreads
            << "]: ";

        std::cin
            >> threads;

        if (
            !std::cin
            ||
            threads < 1
        ) {

            threads = 1;
        }

        threads =
            std::min(
                threads,
                maxThreads
            );

        // ----------------------------------------------------
        // UNIQUE RUN DIRECTORY
        // ----------------------------------------------------

        std::string timestamp =
            getTimestamp();

        RUN_DIR =
            RESULT_ROOT
            /
            "rle"
            /
            (
                std::to_string(
                    threads
                )
                +
                "_threads_"
                +
                timestamp
            );

        COMPRESSED_DIR =
            RUN_DIR
            /
            "compressed";

        LOG_DIR =
            RUN_DIR
            /
            "logs";

        fs::create_directories(
            COMPRESSED_DIR
        );

        fs::create_directories(
            LOG_DIR
        );

        // ----------------------------------------------------
        // CONFIGURATION
        // ----------------------------------------------------

        std::cout
            << "\n"
            << "Configuration\n"
            << "------------------------------------------------------------\n"

            << "Algorithm:         RLE\n"

            << "Threads:           "
            << threads
            << "\n"

            << "Parallel mode:     "
            << (
                threads == 1
                    ? "Single-threaded"
                    : "Parallel chunks"
            )
            << "\n"

            << "Timing iterations: "
            << TIMING_ITERATIONS
            << "\n"

            << "Timing method:     Total time / iterations\n"

            << "Run directory:\n  "
            << RUN_DIR
            << "\n"

            << "------------------------------------------------------------\n";

        // ----------------------------------------------------
        // RESULTS
        // ----------------------------------------------------

        std::vector<Result> results;

        results.reserve(
            DATASETS.size()
        );

        // ----------------------------------------------------
        // PROCESS
        // ----------------------------------------------------

        for (
            size_t i = 0;
            i < DATASETS.size();
            ++i
        ) {

            const std::string& filename =
                DATASETS[i];

            std::cout
                << "\n["
                << i + 1
                << "/"
                << DATASETS.size()
                << "] Processing \""
                << filename
                << "\"\n";

            std::cout
                << "------------------------------------------------------------\n";

            fs::path datasetPath =
                DATASET_DIR
                /
                filename;

            if (
                !fs::exists(datasetPath)
            ) {

                std::cout
                    << "  WARNING: Dataset not found. Skipping.\n";

                continue;
            }

            try {

                Result result =
                    runExperiment(
                        filename,
                        threads
                    );

                printResult(
                    result
                );

                // Per-dataset log.
                writeLog(
                    result
                );

                results.push_back(
                    result
                );
            }
            catch (
                const std::exception& e
            ) {

                std::cerr
                    << "\nERROR processing "
                    << filename
                    << ": "
                    << e.what()
                    << "\n";
            }
        }

        // ----------------------------------------------------
        // WRITE RUN RESULTS
        // ----------------------------------------------------

        writeCSV(
            results
        );

        writeJSON(
            results
        );

        // ----------------------------------------------------
        // APPEND MASTER
        // ----------------------------------------------------

        appendMasterCSV(
            results
        );

        // ----------------------------------------------------
        // FINAL
        // ----------------------------------------------------

        std::cout
            << "\n"
            << "============================================================\n"
            << "RLE BENCHMARK COMPLETE\n"
            << "============================================================\n\n"

            << "Datasets successfully processed: "
            << results.size()
            << "/"
            << DATASETS.size()
            << "\n\n"

            << "Run directory:\n  "
            << RUN_DIR
            << "\n\n"

            << "Files created:\n"
            << "  results.csv\n"
            << "  results.json\n"
            << "  logs/*.log\n"
            << "  compressed/*.rle\n\n"

            << "Master CSV:\n  "
            << RESULT_ROOT
            << "/rle_all_runs.csv\n\n"

            << "Timing methodology:\n"
            << "  "
            << TIMING_ITERATIONS
            << " compression iterations\n"

            << "  "
            << TIMING_ITERATIONS
            << " decompression iterations\n"

            << "  Warm-up before timing\n"

            << "  Reported time = total time / "
            << TIMING_ITERATIONS
            << "\n\n"

            << "Previous runs were NOT overwritten.\n"

            << "============================================================\n";

        return 0;
    }
    catch (
        const std::exception& e
    ) {

        std::cerr
            << "\nFATAL ERROR:\n"
            << e.what()
            << "\n";

        return 1;
    }
}