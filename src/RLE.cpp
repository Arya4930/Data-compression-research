#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
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

// ============================================================
// DATASETS
//
// These match the 15 datasets from your current RLE results.
// ============================================================

const std::vector<std::string> DATASETS = {
    "../datasets/aaa.txt",
    "../datasets/alphabet.txt",
    "../datasets/dickens",
    "../datasets/mozilla",
    "../datasets/mr",
    "../datasets/nci",
    "../datasets/ooffice",
    "../datasets/osdb",
    "../datasets/random.txt",
    "../datasets/reymont",
    "../datasets/samba",
    "../datasets/sao",
    "../datasets/webster",
    "../datasets/x-ray",
    "../datasets/xml"
};

// ============================================================
// RLE RUN
// ============================================================

struct RLEPair {
    uint8_t value;
    uint64_t count;
};

// ============================================================
// COMPRESSED DATA
// ============================================================

struct RLECompressed {
    std::vector<RLEPair> runs;
};

// ============================================================
// RESULT
// ============================================================

struct Result {

    std::string dataset;
    std::string algorithm;

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
// READ FILE
// ============================================================

std::vector<uint8_t> readFile(
    const std::string& path
) {
    std::ifstream file(
        path,
        std::ios::binary
    );

    if (!file) {
        throw std::runtime_error(
            "Cannot open file: " + path
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

    if (size < 0) {
        throw std::runtime_error(
            "Invalid file size: " + path
        );
    }

    std::vector<uint8_t> data(
        static_cast<size_t>(size)
    );

    if (size > 0) {

        file.read(
            reinterpret_cast<char*>(data.data()),
            size
        );
    }

    return data;
}

// ============================================================
// ENTROPY
// ============================================================

double calculateEntropy(
    const std::vector<uint8_t>& data
) {
    if (data.empty()) {
        return 0.0;
    }

    uint64_t frequency[256] = {};

    for (uint8_t byte : data) {
        frequency[byte]++;
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
            std::log2(probability);
    }

    return entropy;
}

// ============================================================
// SERIAL RLE
// ============================================================

RLECompressed rleCompressSerial(
    const std::vector<uint8_t>& data
) {
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
// CHUNK RESULT
// ============================================================

struct ChunkResult {

    size_t start = 0;
    size_t end = 0;

    std::vector<RLEPair> runs;
};

// ============================================================
// COMPRESS ONE CHUNK
// ============================================================

void compressChunk(
    const std::vector<uint8_t>& data,
    size_t start,
    size_t end,
    ChunkResult& result
) {
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
) {
    RLECompressed result;

    size_t estimatedRuns = 0;

    for (const auto& chunk : chunks) {
        estimatedRuns += chunk.runs.size();
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
// PERSISTENT COMPRESSION POOL
// ============================================================

class CompressionPool {

private:

    const std::vector<uint8_t>& data;

    int threadCount;

    std::vector<ChunkResult> chunks;
    std::vector<std::thread> workers;

    std::mutex mutex;

    std::condition_variable startCV;
    std::condition_variable doneCV;
    std::condition_variable idleCV;

    bool stop = false;

    uint64_t generation = 0;

    int finishedWorkers = 0;

public:

    CompressionPool(
        const std::vector<uint8_t>& input,
        int threads
    )
        : data(input),
          threadCount(
              std::max(1, threads)
          ),
          chunks(
              std::max(1, threads)
          )
    {
        workers.reserve(
            threadCount
        );

        for (
            int i = 0;
            i < threadCount;
            ++i
        ) {

            workers.emplace_back(
                &CompressionPool::workerLoop,
                this,
                i
            );
        }
    }

    ~CompressionPool()
    {
        {
            std::lock_guard<std::mutex> lock(
                mutex
            );

            stop = true;
            ++generation;
        }

        startCV.notify_all();

        for (auto& worker : workers) {

            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    RLECompressed compress()
    {
        uint64_t currentGeneration;

        {
            std::lock_guard<std::mutex> lock(
                mutex
            );

            finishedWorkers = 0;

            ++generation;

            currentGeneration =
                generation;
        }

        startCV.notify_all();

        {
            std::unique_lock<std::mutex> lock(
                mutex
            );

            doneCV.wait(
                lock,
                [&]() {
                    return
                        finishedWorkers
                        ==
                        threadCount;
                }
            );
        }

        return mergeChunks(
            chunks
        );
    }

private:

    void workerLoop(
        int workerId
    )
    {
        uint64_t lastGeneration = 0;

        while (true) {

            uint64_t myGeneration;

            {
                std::unique_lock<std::mutex> lock(
                    mutex
                );

                startCV.wait(
                    lock,
                    [&]() {
                        return
                            stop
                            ||
                            generation
                                !=
                            lastGeneration;
                    }
                );

                if (stop) {
                    return;
                }

                myGeneration =
                    generation;

                lastGeneration =
                    generation;
            }

            // ------------------------------------------------
            // Calculate chunk boundaries
            // ------------------------------------------------

            size_t start =
                (
                    data.size()
                    *
                    static_cast<size_t>(
                        workerId
                    )
                )
                /
                static_cast<size_t>(
                    threadCount
                );

            size_t end =
                (
                    data.size()
                    *
                    static_cast<size_t>(
                        workerId + 1
                    )
                )
                /
                static_cast<size_t>(
                    threadCount
                );

            // ------------------------------------------------
            // Compress chunk
            // ------------------------------------------------

            compressChunk(
                data,
                start,
                end,
                chunks[workerId]
            );

            // ------------------------------------------------
            // Notify completion
            // ------------------------------------------------

            {
                std::lock_guard<std::mutex> lock(
                    mutex
                );

                if (
                    generation
                    ==
                    myGeneration
                ) {

                    ++finishedWorkers;

                    if (
                        finishedWorkers
                        ==
                        threadCount
                    ) {

                        doneCV.notify_one();
                    }
                }
            }
        }
    }
};

// ============================================================
// SERIAL DECOMPRESSION
// ============================================================

std::vector<uint8_t> rleDecompressSerial(
    const RLECompressed& compressed
) {
    uint64_t totalSize = 0;

    for (const auto& run : compressed.runs) {

        totalSize +=
            run.count;
    }

    std::vector<uint8_t> output;

    output.resize(
        static_cast<size_t>(
            totalSize
        )
    );

    size_t position = 0;

    for (const auto& run : compressed.runs) {

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
// PERSISTENT DECOMPRESSION POOL
// ============================================================

class DecompressionPool {

private:

    const RLECompressed& compressed;

    int threadCount;

    std::vector<uint8_t> output;

    std::vector<size_t> runStarts;

    std::vector<std::thread> workers;

    std::mutex mutex;

    std::condition_variable startCV;
    std::condition_variable doneCV;

    bool stop = false;

    uint64_t generation = 0;

    int finishedWorkers = 0;

public:

    DecompressionPool(
        const RLECompressed& input,
        int threads
    )
        : compressed(input),
          threadCount(
              std::max(1, threads)
          )
    {
        // ----------------------------------------------------
        // Calculate total output size
        // ----------------------------------------------------

        uint64_t totalSize = 0;

        for (
            const auto& run :
            compressed.runs
        ) {

            totalSize +=
                run.count;
        }

        output.resize(
            static_cast<size_t>(
                totalSize
            )
        );

        if (compressed.runs.empty()) {

            threadCount = 1;
        }
        else {

            threadCount =
                std::min<int>(
                    threadCount,
                    static_cast<int>(
                        compressed.runs.size()
                    )
                );
        }

        runStarts.resize(
            threadCount + 1
        );

        for (
            int i = 0;
            i <= threadCount;
            ++i
        ) {

            runStarts[i] =
                (
                    compressed.runs.size()
                    *
                    static_cast<size_t>(i)
                )
                /
                static_cast<size_t>(
                    threadCount
                );
        }

        workers.reserve(
            threadCount
        );

        for (
            int i = 0;
            i < threadCount;
            ++i
        ) {

            workers.emplace_back(
                &DecompressionPool::workerLoop,
                this,
                i
            );
        }
    }

    ~DecompressionPool()
    {
        {
            std::lock_guard<std::mutex> lock(
                mutex
            );

            stop = true;
            ++generation;
        }

        startCV.notify_all();

        for (auto& worker : workers) {

            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    const std::vector<uint8_t>& decompress()
    {
        {
            std::lock_guard<std::mutex> lock(
                mutex
            );

            finishedWorkers = 0;

            ++generation;
        }

        startCV.notify_all();

        {
            std::unique_lock<std::mutex> lock(
                mutex
            );

            doneCV.wait(
                lock,
                [&]() {
                    return
                        finishedWorkers
                        ==
                        threadCount;
                }
            );
        }

        return output;
    }

private:

    void workerLoop(
        int workerId
    )
    {
        uint64_t lastGeneration = 0;

        while (true) {

            uint64_t myGeneration;

            {
                std::unique_lock<std::mutex> lock(
                    mutex
                );

                startCV.wait(
                    lock,
                    [&]() {
                        return
                            stop
                            ||
                            generation
                                !=
                            lastGeneration;
                    }
                );

                if (stop) {
                    return;
                }

                myGeneration =
                    generation;

                lastGeneration =
                    generation;
            }

            size_t runStart =
                runStarts[workerId];

            size_t runEnd =
                runStarts[
                    workerId + 1
                ];

            // ------------------------------------------------
            // Find output position of first run
            // ------------------------------------------------

            size_t outputPosition = 0;

            for (
                size_t i = 0;
                i < runStart;
                ++i
            ) {

                outputPosition +=
                    static_cast<size_t>(
                        compressed.runs[i].count
                    );
            }

            // ------------------------------------------------
            // Decompress assigned runs
            // ------------------------------------------------

            for (
                size_t i = runStart;
                i < runEnd;
                ++i
            ) {

                const RLEPair& run =
                    compressed.runs[i];

                std::fill(
                    output.begin()
                        +
                        outputPosition,

                    output.begin()
                        +
                        outputPosition
                        +
                        static_cast<size_t>(
                            run.count
                        ),

                    run.value
                );

                outputPosition +=
                    static_cast<size_t>(
                        run.count
                    );
            }

            // ------------------------------------------------
            // Notify completion
            // ------------------------------------------------

            {
                std::lock_guard<std::mutex> lock(
                    mutex
                );

                if (
                    generation
                    ==
                    myGeneration
                ) {

                    ++finishedWorkers;

                    if (
                        finishedWorkers
                        ==
                        threadCount
                    ) {

                        doneCV.notify_one();
                    }
                }
            }
        }
    }
};

// ============================================================
// VARIABLE LENGTH INTEGER SIZE
//
// Count encoding:
// 7 bits of data per byte.
// High bit indicates continuation.
//
// Examples:
//
// 1       -> 1 byte
// 127     -> 1 byte
// 128     -> 2 bytes
// 16383   -> 2 bytes
// 16384   -> 3 bytes
// ============================================================

size_t encodedCountSize(
    uint64_t value
) {
    size_t size = 1;

    while (value >= 128) {

        value >>= 7;

        ++size;
    }

    return size;
}

// ============================================================
// COMPRESSED SIZE
//
// Each run:
//
//     1 byte  -> value
//     N bytes -> variable-length count
//
// ============================================================

uint64_t calculateCompressedSize(
    const RLECompressed& compressed
) {
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
// PEAK WORKING SET
// ============================================================

double getPeakWorkingSetMB()
{
#ifdef _WIN32

    PROCESS_MEMORY_COUNTERS counters{};

    if (
        GetProcessMemoryInfo(
            GetCurrentProcess(),
            &counters,
            sizeof(counters)
        )
    ) {

        return
            static_cast<double>(
                counters.PeakWorkingSetSize
            )
            /
            (1024.0 * 1024.0);
    }

#endif

    return 0.0;
}

// ============================================================
// PROGRESS BAR
// ============================================================

void showProgress(
    int current,
    int total
) {
    constexpr int WIDTH = 40;

    double progress =
        static_cast<double>(
            current
        )
        /
        static_cast<double>(
            total
        );

    int filled =
        static_cast<int>(
            progress * WIDTH
        );

    std::cout << "[";

    for (
        int i = 0;
        i < WIDTH;
        ++i
    ) {

        if (i < filled) {
            std::cout << '#';
        }
        else {
            std::cout << '-';
        }
    }

    std::cout
        << "] "
        << std::fixed
        << std::setprecision(2)
        << progress * 100.0
        << "%"
        << std::flush;
}

// ============================================================
// CSV
// ============================================================

void appendMasterCSV(
    const Result& r
) {
    fs::path csvPath =
        "../results/rle_all_runs.csv";

    fs::create_directories(
        csvPath.parent_path()
    );

    bool exists =
        fs::exists(csvPath)
        &&
        fs::file_size(csvPath) > 0;

    std::ofstream file(
        csvPath,
        std::ios::app
    );

    if (!file) {

        throw std::runtime_error(
            "Cannot open CSV: "
            +
            csvPath.string()
        );
    }

    if (!exists) {

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

// ============================================================
// BENCHMARK ONE DATASET
// ============================================================

Result benchmark(
    const std::string& path,
    int threads
) {
    // --------------------------------------------------------
    // LOAD DATA
    // --------------------------------------------------------

    std::vector<uint8_t> data =
        readFile(path);

    Result result;

    result.dataset =
        fs::path(path).filename().string();

    result.algorithm =
        "RLE";

    result.threads =
        threads;

    result.originalBytes =
        data.size();

    result.entropy =
        calculateEntropy(data);

    // --------------------------------------------------------
    // CREATE PERSISTENT COMPRESSION POOL
    // --------------------------------------------------------

    CompressionPool compressionPool(
        data,
        threads
    );

    // --------------------------------------------------------
    // WARM-UP
    // --------------------------------------------------------

    std::cout
        << "  Warming up        ";

    showProgress(0, 1);

    RLECompressed warmCompressed =
        compressionPool.compress();

    DecompressionPool warmDecompressionPool(
        warmCompressed,
        threads
    );

    const auto& warmDecoded =
        warmDecompressionPool.decompress();

    (void)warmDecoded;

    std::cout
        << "\r  Warming up        ";

    showProgress(1, 1);

    std::cout << "\n";

    // --------------------------------------------------------
    // REFERENCE COMPRESSION
    // --------------------------------------------------------

    RLECompressed reference =
        compressionPool.compress();

    result.compressedBytes =
        calculateCompressedSize(
            reference
        );

    // --------------------------------------------------------
    // COMPRESSION RATIO
    // --------------------------------------------------------

    if (
        result.compressedBytes > 0
        &&
        result.originalBytes > 0
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
    //
    // 100 consecutive compressions.
    // Worker threads are persistent.
    // --------------------------------------------------------

    std::cout
        << "  Compressing      ";

    showProgress(0, TIMING_ITERATIONS);

    auto compressionStart =
        std::chrono::steady_clock::now();

    RLECompressed timedCompressed;

    for (
        int iteration = 0;
        iteration < TIMING_ITERATIONS;
        ++iteration
    ) {

        timedCompressed =
            compressionPool.compress();

        if (
            iteration % 2 == 0
            ||
            iteration
                ==
            TIMING_ITERATIONS - 1
        ) {

            std::cout
                << "\r  Compressing      ";

            showProgress(
                iteration + 1,
                TIMING_ITERATIONS
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

    std::cout << "\n";

    // --------------------------------------------------------
    // CREATE DECOMPRESSION POOL
    //
    // This pool persists across all 100 iterations.
    // --------------------------------------------------------

    DecompressionPool decompressionPool(
        timedCompressed,
        threads
    );

    // --------------------------------------------------------
    // DECOMPRESSION TIMING
    //
    // 100 consecutive decompressions.
    // Worker threads are persistent.
    // --------------------------------------------------------

    std::cout
        << "  Decompressing    ";

    showProgress(0, TIMING_ITERATIONS);

    auto decompressionStart =
        std::chrono::steady_clock::now();

    const std::vector<uint8_t>* decoded =
        nullptr;

    for (
        int iteration = 0;
        iteration < TIMING_ITERATIONS;
        ++iteration
    ) {

        decoded =
            &decompressionPool.decompress();

        if (
            iteration % 2 == 0
            ||
            iteration
                ==
            TIMING_ITERATIONS - 1
        ) {

            std::cout
                << "\r  Decompressing    ";

            showProgress(
                iteration + 1,
                TIMING_ITERATIONS
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

    std::cout << "\n";

    // --------------------------------------------------------
    // LOSSLESS VERIFICATION
    // --------------------------------------------------------

    if (decoded != nullptr) {

        result.lossless =
            (
                *decoded
                ==
                data
            );
    }

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
        result.compressionTime > 0
    ) {

        result.compressionThroughput =
            sizeMB
            /
            result.compressionTime;
    }

    if (
        result.decompressionTime > 0
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
        getPeakWorkingSetMB();

    return result;
}

// ============================================================
// MAIN
// ============================================================

int main()
{
    try {

        std::cout
            << "\n"
            << "============================================================\n"
            << "RLE COMPRESSION BENCHMARK\n"
            << "============================================================\n\n";

        std::cout
            << "Timing iterations per measurement: "
            << TIMING_ITERATIONS
            << "\n";

        std::cout
            << "Datasets: "
            << DATASETS.size()
            << "\n";

        std::cout
            << "Parallel threads: 1-"
            << MAX_THREADS
            << "\n\n";

        int threads;

        std::cout
            << "Enter number of threads [1-"
            << MAX_THREADS
            << "]: ";

        std::cin
            >> threads;

        if (
            threads < 1
            ||
            threads > MAX_THREADS
        ) {

            std::cerr
                << "Invalid thread count.\n";

            return 1;
        }

        // ----------------------------------------------------
        // CREATE RESULTS DIRECTORY
        // ----------------------------------------------------

        fs::create_directories(
            "../results/rle"
        );

        // ----------------------------------------------------
        // PROCESS DATASETS
        // ----------------------------------------------------

        size_t completed = 0;

        for (
            const auto& dataset :
            DATASETS
        ) {

            ++completed;

            std::cout
                << "\n["
                << completed
                << "/"
                << DATASETS.size()
                << "] Processing \""
                << fs::path(dataset).filename().string()
                << "\"\n";

            std::cout
                << "------------------------------------------------------------\n";

            try {

                Result result =
                    benchmark(
                        dataset,
                        threads
                    );

                std::cout
                    << "\n"
                    << "  Threads: "
                    << result.threads
                    << "\n"

                    << "  Original size: "
                    << result.originalBytes
                    << " bytes\n"

                    << "  Compressed size: "
                    << result.compressedBytes
                    << " bytes\n"

                    << "  Compression ratio: "
                    << std::fixed
                    << std::setprecision(6)
                    << result.compressionRatio
                    << "\n"

                    << "  Space saved: "
                    << std::setprecision(2)
                    << result.spaceSaved
                    << "%\n"

                    << "  Entropy: "
                    << std::setprecision(6)
                    << result.entropy
                    << " bits/symbol\n"

                    << "  Compression time (per iteration): "
                    << std::setprecision(6)
                    << result.compressionTime
                    << " s\n"

                    << "  Decompression time (per iteration): "
                    << result.decompressionTime
                    << " s\n"

                    << "  Compression throughput: "
                    << std::setprecision(2)
                    << result.compressionThroughput
                    << " MB/s\n"

                    << "  Decompression throughput: "
                    << result.decompressionThroughput
                    << " MB/s\n"

                    << "  Peak working set: "
                    << result.memoryMB
                    << " MB\n"

                    << "  Lossless verification: "
                    << (
                        result.lossless
                            ? "PASS"
                            : "FAIL"
                    )
                    << "\n";

                appendMasterCSV(
                    result
                );
            }
            catch (
                const std::exception& e
            ) {

                std::cerr
                    << "\nERROR: "
                    << e.what()
                    << "\n";
            }
        }

        std::cout
            << "\n"
            << "============================================================\n"
            << "RLE BENCHMARK COMPLETE\n"
            << "============================================================\n\n"

            << "Results:\n"
            << "  ../results/rle_all_runs.csv\n\n"

            << "Timing methodology:\n"
            << "  100 compression iterations\n"
            << "  100 decompression iterations\n"
            << "  Persistent worker threads\n"
            << "  Reported time = total time / 100\n\n"

            << "============================================================\n";

        return 0;
    }
    catch (
        const std::exception& e
    ) {

        std::cerr
            << "\nFATAL ERROR: "
            << e.what()
            << "\n";

        return 1;
    }
}