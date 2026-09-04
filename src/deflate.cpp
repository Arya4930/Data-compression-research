#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <zlib.h>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

namespace fs = std::filesystem;

// ============================================================
// CONFIGURATION
// ============================================================

static constexpr int MAX_THREADS = 22;
static constexpr int TIMING_ITERATIONS = 100;
static constexpr size_t BLOCK_SIZE = 1 * 1024 * 1024; // 1 MiB
static constexpr int COMPRESSION_LEVEL = Z_DEFAULT_COMPRESSION;

static const fs::path DATASET_DIR = "../datasets";
static const fs::path RESULT_ROOT = "../results";

static const std::vector<std::string> DATASETS = {
    "aaa.txt", "alphabet.txt", "dickens", "mozilla", "mr",
    "nci", "ooffice", "osdb", "random.txt", "reymont",
    "samba", "sao", "webster", "x-ray", "xml"
};

// ============================================================
// DATA STRUCTURES
// ============================================================

struct CompressedBlock {
    size_t originalSize = 0;
    std::vector<uint8_t> compressed;
};

struct DeflateCompressed {
    size_t originalSize = 0;
    std::vector<CompressedBlock> blocks;
};

struct Result {
    std::string dataset;
    std::string algorithm = "Deflate";
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
// UTILITIES
// ============================================================

double nowSeconds()
{
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

double getPeakWorkingSetMB()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);

    if (GetProcessMemoryInfo(
            GetCurrentProcess(),
            &pmc,
            sizeof(pmc)))
    {
        return static_cast<double>(pmc.PeakWorkingSetSize)
            / (1024.0 * 1024.0);
    }
#endif
    return 0.0;
}

std::vector<uint8_t> readFile(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);

    if (!file)
        throw std::runtime_error(
            "Cannot open dataset: " + path.string());

    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size < 0)
        throw std::runtime_error(
            "Invalid file size: " + path.string());

    std::vector<uint8_t> data(static_cast<size_t>(size));

    if (size > 0)
    {
        file.read(
            reinterpret_cast<char*>(data.data()),
            size);

        if (!file)
            throw std::runtime_error(
                "Failed reading dataset: " + path.string());
    }

    return data;
}

double calculateEntropy(const std::vector<uint8_t>& data)
{
    if (data.empty())
        return 0.0;

    std::array<uint64_t, 256> frequency{};

    for (uint8_t b : data)
        ++frequency[b];

    const double total = static_cast<double>(data.size());
    double entropy = 0.0;

    for (uint64_t count : frequency)
    {
        if (count == 0)
            continue;

        const double p = static_cast<double>(count) / total;
        entropy -= p * std::log2(p);
    }

    return entropy;
}

void showProgress(size_t current, size_t total)
{
    constexpr int WIDTH = 40;

    const double progress =
        total == 0
            ? 1.0
            : std::clamp(
                  static_cast<double>(current) /
                  static_cast<double>(total),
                  0.0,
                  1.0);

    const int filled =
        static_cast<int>(progress * WIDTH);

    std::cout << "[";

    for (int i = 0; i < WIDTH; ++i)
        std::cout << (i < filled ? '#' : '-');

    std::cout
        << "] "
        << std::fixed
        << std::setprecision(1)
        << progress * 100.0
        << "%"
        << std::flush;
}

std::string zlibError(int code)
{
    std::ostringstream out;
    out << "zlib error " << code;

    const char* text = zError(code);
    if (text)
        out << ": " << text;

    return out.str();
}

// ============================================================
// RAW DEFLATE COMPRESSION
// ============================================================
//
// Each block is an independent raw-DEFLATE stream.
// -15 means no zlib/gzip wrapper.
//
// This is intentional: it makes each block independently
// compressible and therefore independently parallelizable.
// ============================================================

CompressedBlock compressBlock(
    const std::vector<uint8_t>& data,
    size_t begin,
    size_t end)
{
    CompressedBlock block;
    block.originalSize = end - begin;

    if (begin >= end)
        return block;

    const size_t inputSize = end - begin;

    if (inputSize > static_cast<size_t>(
                        std::numeric_limits<uLong>::max()))
    {
        throw std::runtime_error(
            "Block too large for zlib.");
    }

    const uLong sourceLength =
        static_cast<uLong>(inputSize);

    const uLong bound =
        deflateBound(nullptr, sourceLength);

    block.compressed.resize(
        static_cast<size_t>(bound));

    z_stream stream{};

    int rc = deflateInit2(
        &stream,
        COMPRESSION_LEVEL,
        Z_DEFLATED,
        -15, // raw DEFLATE
        8,
        Z_DEFAULT_STRATEGY);

    if (rc != Z_OK)
        throw std::runtime_error(
            "deflateInit2 failed: " + zlibError(rc));

    stream.next_in =
        const_cast<Bytef*>(
            reinterpret_cast<const Bytef*>(
                data.data() + begin));

    stream.avail_in = sourceLength;

    stream.next_out =
        reinterpret_cast<Bytef*>(
            block.compressed.data());

    stream.avail_out = bound;

    rc = deflate(&stream, Z_FINISH);

    if (rc != Z_STREAM_END)
    {
        deflateEnd(&stream);

        throw std::runtime_error(
            "deflate failed: " + zlibError(rc));
    }

    const size_t produced =
        static_cast<size_t>(stream.total_out);

    rc = deflateEnd(&stream);

    if (rc != Z_OK)
        throw std::runtime_error(
            "deflateEnd failed: " + zlibError(rc));

    block.compressed.resize(produced);

    return block;
}

// ============================================================
// PARALLEL COMPRESSION
// ============================================================

DeflateCompressed compressDeflate(
    const std::vector<uint8_t>& data,
    int threads)
{
    DeflateCompressed result;
    result.originalSize = data.size();

    if (data.empty())
        return result;

    const size_t blockCount =
        (data.size() + BLOCK_SIZE - 1) / BLOCK_SIZE;

    result.blocks.resize(blockCount);

    const int workerCount =
        std::max(
            1,
            std::min(
                threads,
                static_cast<int>(blockCount)));

    std::atomic<size_t> nextBlock{0};
    std::atomic<bool> failed{false};

    std::mutex errorMutex;
    std::string errorMessage;

    std::vector<std::thread> workers;
    workers.reserve(workerCount);

    for (int t = 0; t < workerCount; ++t)
    {
        workers.emplace_back(
            [&]()
            {
                while (!failed.load(std::memory_order_relaxed))
                {
                    const size_t index =
                        nextBlock.fetch_add(1);

                    if (index >= blockCount)
                        break;

                    try
                    {
                        const size_t begin =
                            index * BLOCK_SIZE;

                        const size_t end =
                            std::min(
                                begin + BLOCK_SIZE,
                                data.size());

                        result.blocks[index] =
                            compressBlock(
                                data,
                                begin,
                                end);
                    }
                    catch (const std::exception& e)
                    {
                        {
                            std::lock_guard<std::mutex> lock(
                                errorMutex);

                            if (errorMessage.empty())
                                errorMessage = e.what();
                        }

                        failed.store(true);
                        break;
                    }
                }
            });
    }

    for (auto& worker : workers)
        worker.join();

    if (failed.load())
        throw std::runtime_error(
            errorMessage.empty()
                ? "Parallel Deflate compression failed."
                : errorMessage);

    return result;
}

// ============================================================
// RAW DEFLATE DECOMPRESSION
// ============================================================

std::vector<uint8_t> decompressBlock(
    const CompressedBlock& block)
{
    if (block.originalSize == 0)
        return {};

    if (block.originalSize >
        static_cast<size_t>(
            std::numeric_limits<uInt>::max()))
    {
        throw std::runtime_error(
            "Block too large for zlib inflate API.");
    }

    if (block.compressed.size() >
        static_cast<size_t>(
            std::numeric_limits<uInt>::max()))
    {
        throw std::runtime_error(
            "Compressed block too large for zlib inflate API.");
    }

    std::vector<uint8_t> output(
        block.originalSize);

    z_stream stream{};

    int rc = inflateInit2(
        &stream,
        -15); // raw DEFLATE

    if (rc != Z_OK)
        throw std::runtime_error(
            "inflateInit2 failed: " + zlibError(rc));

    stream.next_in =
        const_cast<Bytef*>(
            reinterpret_cast<const Bytef*>(
                block.compressed.data()));

    stream.avail_in =
        static_cast<uInt>(
            block.compressed.size());

    stream.next_out =
        reinterpret_cast<Bytef*>(
            output.data());

    stream.avail_out =
        static_cast<uInt>(
            output.size());

    rc = inflate(
        &stream,
        Z_FINISH);

    const size_t produced =
        static_cast<size_t>(stream.total_out);

    const size_t consumed =
        static_cast<size_t>(stream.total_in);

    const int endRc =
        inflateEnd(&stream);

    if (rc != Z_STREAM_END)
        throw std::runtime_error(
            "inflate failed: " + zlibError(rc));

    if (endRc != Z_OK)
        throw std::runtime_error(
            "inflateEnd failed: " + zlibError(endRc));

    if (produced != block.originalSize)
        throw std::runtime_error(
            "Inflated block size does not match original size.");

    if (consumed != block.compressed.size())
        throw std::runtime_error(
            "Unexpected trailing data in Deflate block.");

    return output;
}

// ============================================================
// PARALLEL DECOMPRESSION
// ============================================================

std::vector<uint8_t> decompressDeflate(
    const DeflateCompressed& compressed,
    int threads)
{
    if (compressed.blocks.empty())
        return {};

    const size_t blockCount =
        compressed.blocks.size();

    std::vector<std::vector<uint8_t>> outputs(
        blockCount);

    const int workerCount =
        std::max(
            1,
            std::min(
                threads,
                static_cast<int>(blockCount)));

    std::atomic<size_t> nextBlock{0};
    std::atomic<bool> failed{false};

    std::mutex errorMutex;
    std::string errorMessage;

    std::vector<std::thread> workers;
    workers.reserve(workerCount);

    for (int t = 0; t < workerCount; ++t)
    {
        workers.emplace_back(
            [&]()
            {
                while (!failed.load(std::memory_order_relaxed))
                {
                    const size_t index =
                        nextBlock.fetch_add(1);

                    if (index >= blockCount)
                        break;

                    try
                    {
                        outputs[index] =
                            decompressBlock(
                                compressed.blocks[index]);
                    }
                    catch (const std::exception& e)
                    {
                        {
                            std::lock_guard<std::mutex> lock(
                                errorMutex);

                            if (errorMessage.empty())
                                errorMessage = e.what();
                        }

                        failed.store(true);
                        break;
                    }
                }
            });
    }

    for (auto& worker : workers)
        worker.join();

    if (failed.load())
        throw std::runtime_error(
            errorMessage.empty()
                ? "Parallel Deflate decompression failed."
                : errorMessage);

    std::vector<uint8_t> output;
    output.reserve(compressed.originalSize);

    for (const auto& block : outputs)
    {
        output.insert(
            output.end(),
            block.begin(),
            block.end());
    }

    if (output.size() != compressed.originalSize)
        throw std::runtime_error(
            "Total decompressed size mismatch.");

    return output;
}

// ============================================================
// COMPRESSED SIZE
// ============================================================
//
// Custom container:
//
//   magic       4 bytes
//   version     4 bytes
//   original    8 bytes
//   blocks      8 bytes
//
// Per block:
//
//   original    8 bytes
//   compressed  8 bytes
//   data        N bytes
// ============================================================

uint64_t calculateCompressedSize(
    const DeflateCompressed& compressed)
{
    uint64_t total = 24;

    for (const auto& block : compressed.blocks)
    {
        total += 16;
        total += static_cast<uint64_t>(
            block.compressed.size());
    }

    return total;
}

void writeU64(
    std::ofstream& file,
    uint64_t value)
{
    file.write(
        reinterpret_cast<const char*>(&value),
        sizeof(value));

    if (!file)
        throw std::runtime_error(
            "Failed writing compressed file.");
}

void saveCompressedFile(
    const fs::path& path,
    const DeflateCompressed& compressed)
{
    std::ofstream file(
        path,
        std::ios::binary);

    if (!file)
        throw std::runtime_error(
            "Cannot create compressed file: " +
            path.string());

    const char magic[4] = {'D', 'F', 'L', 'T'};
    const uint32_t version = 1;

    file.write(magic, 4);

    file.write(
        reinterpret_cast<const char*>(&version),
        sizeof(version));

    writeU64(
        file,
        static_cast<uint64_t>(
            compressed.originalSize));

    writeU64(
        file,
        static_cast<uint64_t>(
            compressed.blocks.size()));

    for (const auto& block : compressed.blocks)
    {
        writeU64(
            file,
            static_cast<uint64_t>(
                block.originalSize));

        writeU64(
            file,
            static_cast<uint64_t>(
                block.compressed.size()));

        if (!block.compressed.empty())
        {
            file.write(
                reinterpret_cast<const char*>(
                    block.compressed.data()),
                static_cast<std::streamsize>(
                    block.compressed.size()));

            if (!file)
                throw std::runtime_error(
                    "Failed writing compressed block.");
        }
    }
}

// ============================================================
// LOGGING
// ============================================================

void writeLog(
    const Result& r,
    const fs::path& path)
{
    std::ofstream file(path);

    if (!file)
        return;

    file
        << "DEFLATE COMPRESSION BENCHMARK\n"
        << "=============================\n\n"
        << "Dataset: " << r.dataset << "\n"
        << "Algorithm: " << r.algorithm << "\n"
        << "Threads: " << r.threads << "\n"
        << "Parallel mode: "
        << (r.threads == 1
                ? "Single-threaded"
                : "Independent parallel Deflate blocks")
        << "\n\n"
        << "Original size: " << r.originalBytes << " bytes\n"
        << "Compressed size: " << r.compressedBytes << " bytes\n"
        << std::setprecision(12)
        << "Compression ratio: " << r.compressionRatio << "\n"
        << "Space saved: " << r.spaceSaved << "%\n"
        << "Entropy: " << r.entropy << " bits/symbol\n\n"
        << "Compression time (per iteration): "
        << r.compressionTime << " s\n"
        << "Decompression time (per iteration): "
        << r.decompressionTime << " s\n"
        << "Compression throughput: "
        << r.compressionThroughput << " MB/s\n"
        << "Decompression throughput: "
        << r.decompressionThroughput << " MB/s\n"
        << "Peak working set: "
        << r.memoryMB << " MB\n"
        << "Timing iterations: "
        << TIMING_ITERATIONS << "\n"
        << "Block size: "
        << BLOCK_SIZE << " bytes\n"
        << "Compression level: "
        << COMPRESSION_LEVEL << "\n\n"
        << "Lossless verification: "
        << (r.lossless ? "PASS" : "FAIL")
        << "\n";
}

void appendMasterCSV(const Result& r)
{
    const fs::path csv =
        RESULT_ROOT / "deflate_all_runs.csv";

    fs::create_directories(csv.parent_path());

    const bool exists =
        fs::exists(csv) &&
        fs::file_size(csv) > 0;

    std::ofstream file(
        csv,
        std::ios::app);

    if (!file)
        throw std::runtime_error(
            "Cannot open master CSV: " +
            csv.string());

    if (!exists)
    {
        file
            << "dataset,algorithm,threads,"
            << "original_bytes,compressed_bytes,entropy,"
            << "compression_ratio,space_saved_percent,"
            << "compression_time,decompression_time,"
            << "compression_throughput_MB_s,"
            << "decompression_throughput_MB_s,"
            << "memory_MB,lossless\n";
    }

    file
        << r.dataset << ","
        << r.algorithm << ","
        << r.threads << ","
        << r.originalBytes << ","
        << r.compressedBytes << ","
        << std::setprecision(12)
        << r.entropy << ","
        << r.compressionRatio << ","
        << r.spaceSaved << ","
        << r.compressionTime << ","
        << r.decompressionTime << ","
        << r.compressionThroughput << ","
        << r.decompressionThroughput << ","
        << r.memoryMB << ","
        << (r.lossless ? "true" : "false")
        << "\n";
}

// ============================================================
// BENCHMARK
// ============================================================

Result benchmarkDataset(
    const std::string& filename,
    int threads,
    const fs::path& compressedPath)
{
    const std::vector<uint8_t> data =
        readFile(DATASET_DIR / filename);

    Result result;

    result.dataset = filename;
    result.algorithm = "Deflate";
    result.threads = threads;
    result.originalBytes = data.size();
    result.entropy = calculateEntropy(data);

    std::cout
        << "  Original size: "
        << result.originalBytes
        << " bytes\n";

    // --------------------------------------------------------
    // WARM-UP
    // --------------------------------------------------------

    std::cout << "  Warming up        ";
    showProgress(0, 1);

    const DeflateCompressed warmCompressed =
        compressDeflate(data, threads);

    const std::vector<uint8_t> warmDecoded =
        decompressDeflate(warmCompressed, threads);

    if (warmDecoded != data)
        throw std::runtime_error(
            "Warm-up lossless verification failed.");

    std::cout << "\r  Warming up        ";
    showProgress(1, 1);
    std::cout << "\n";

    // --------------------------------------------------------
    // REFERENCE COMPRESSION
    // --------------------------------------------------------

    const DeflateCompressed reference =
        compressDeflate(data, threads);

    result.compressedBytes =
        calculateCompressedSize(reference);

    if (
        result.originalBytes > 0 &&
        result.compressedBytes > 0)
    {
        result.compressionRatio =
            static_cast<double>(result.originalBytes) /
            static_cast<double>(result.compressedBytes);

        result.spaceSaved =
            (1.0 -
             static_cast<double>(result.compressedBytes) /
             static_cast<double>(result.originalBytes)) *
            100.0;
    }

    // --------------------------------------------------------
    // COMPRESSION TIMING
    // --------------------------------------------------------

    std::cout << "  Compressing      ";
    showProgress(0, TIMING_ITERATIONS);

    const double compressionStart = nowSeconds();

    DeflateCompressed timedCompressed;

    for (int i = 0; i < TIMING_ITERATIONS; ++i)
    {
        timedCompressed =
            compressDeflate(data, threads);

        if (i % 2 == 0 ||
            i == TIMING_ITERATIONS - 1)
        {
            std::cout << "\r  Compressing      ";
            showProgress(
                static_cast<size_t>(i + 1),
                TIMING_ITERATIONS);
        }
    }

    const double compressionEnd = nowSeconds();

    result.compressionTime =
        (compressionEnd - compressionStart) /
        static_cast<double>(TIMING_ITERATIONS);

    std::cout << "\n";

    // --------------------------------------------------------
    // SAVE FINAL COMPRESSED FILE
    // --------------------------------------------------------

    saveCompressedFile(
        compressedPath,
        timedCompressed);

    // --------------------------------------------------------
    // DECOMPRESSION TIMING
    // --------------------------------------------------------

    std::cout << "  Decompressing    ";
    showProgress(0, TIMING_ITERATIONS);

    const double decompressionStart = nowSeconds();

    std::vector<uint8_t> finalDecoded;

    for (int i = 0; i < TIMING_ITERATIONS; ++i)
    {
        finalDecoded =
            decompressDeflate(
                timedCompressed,
                threads);

        if (i % 2 == 0 ||
            i == TIMING_ITERATIONS - 1)
        {
            std::cout << "\r  Decompressing    ";
            showProgress(
                static_cast<size_t>(i + 1),
                TIMING_ITERATIONS);
        }
    }

    const double decompressionEnd = nowSeconds();

    result.decompressionTime =
        (decompressionEnd - decompressionStart) /
        static_cast<double>(TIMING_ITERATIONS);

    std::cout << "\n";

    // --------------------------------------------------------
    // LOSSLESS VERIFICATION
    // --------------------------------------------------------

    result.lossless =
        (data == finalDecoded);

    // --------------------------------------------------------
    // THROUGHPUT
    // --------------------------------------------------------

    constexpr double MB =
        1024.0 * 1024.0;

    if (result.compressionTime > 0.0)
    {
        result.compressionThroughput =
            (static_cast<double>(result.originalBytes) / MB) /
            result.compressionTime;
    }

    if (result.decompressionTime > 0.0)
    {
        result.decompressionThroughput =
            (static_cast<double>(result.originalBytes) / MB) /
            result.decompressionTime;
    }

    result.memoryMB =
        getPeakWorkingSetMB();

    return result;
}

// ============================================================
// OUTPUT
// ============================================================

void printResult(const Result& r)
{
    std::cout
        << "\n"
        << "  Threads: " << r.threads << "\n"
        << "  Original size: " << r.originalBytes << " bytes\n"
        << "  Compressed size: " << r.compressedBytes << " bytes\n"
        << "  Compression ratio: "
        << std::fixed << std::setprecision(6)
        << r.compressionRatio << "\n"
        << "  Space saved: "
        << std::setprecision(2)
        << r.spaceSaved << "%\n"
        << "  Entropy: "
        << std::setprecision(6)
        << r.entropy << " bits/symbol\n"
        << "  Compression time (per iteration): "
        << r.compressionTime << " s\n"
        << "  Decompression time (per iteration): "
        << r.decompressionTime << " s\n"
        << "  Compression throughput: "
        << r.compressionThroughput << " MB/s\n"
        << "  Decompression throughput: "
        << r.decompressionThroughput << " MB/s\n"
        << "  Peak working set: "
        << r.memoryMB << " MB\n"
        << "  Lossless verification: "
        << (r.lossless ? "PASS" : "FAIL")
        << "\n";
}

std::string getTimestamp()
{
    const auto now =
        std::chrono::system_clock::now();

    const std::time_t time =
        std::chrono::system_clock::to_time_t(now);

    std::tm tm{};

#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif

    std::ostringstream out;

    out << std::put_time(
        &tm,
        "%Y%m%d_%H%M%S");

    return out.str();
}

// ============================================================
// MAIN
// ============================================================

int main()
{
    try
    {
        std::cout
            << "\n"
            << "============================================================\n"
            << "DEFLATE COMPRESSION BENCHMARK\n"
            << "============================================================\n\n";

        std::cout
            << "Timing iterations per measurement: "
            << TIMING_ITERATIONS << "\n"
            << "Datasets: "
            << DATASETS.size() << "\n"
            << "Parallel threads: 1-"
            << MAX_THREADS << "\n"
            << "Block size: "
            << BLOCK_SIZE / 1024 << " KiB\n"
            << "Compression level: "
            << COMPRESSION_LEVEL << "\n\n";

        int threads = 0;

        std::cout
            << "Enter number of threads [1-"
            << MAX_THREADS
            << "]: ";

        std::cin >> threads;

        if (
            !std::cin ||
            threads < 1 ||
            threads > MAX_THREADS)
        {
            std::cerr
                << "\nERROR: Invalid thread count.\n";

            return 1;
        }

        const fs::path runDirectory =
            RESULT_ROOT /
            "deflate" /
            (std::to_string(threads) +
             "_threads_" +
             getTimestamp());

        const fs::path compressedDirectory =
            runDirectory / "compressed";

        const fs::path logDirectory =
            runDirectory / "logs";

        fs::create_directories(compressedDirectory);
        fs::create_directories(logDirectory);

        std::cout
            << "\nConfiguration\n"
            << "------------------------------------------------------------\n"
            << "Algorithm:        Deflate\n"
            << "Threads:          " << threads << "\n"
            << "Parallel mode:    "
            << (threads == 1
                    ? "Single-threaded"
                    : "Independent parallel blocks")
            << "\n"
            << "Block size:       "
            << BLOCK_SIZE / 1024 << " KiB\n"
            << "Timing runs:      "
            << TIMING_ITERATIONS << "\n"
            << "Run directory:\n  "
            << runDirectory << "\n"
            << "------------------------------------------------------------\n";

        std::vector<Result> results;
        results.reserve(DATASETS.size());

        for (size_t i = 0; i < DATASETS.size(); ++i)
        {
            std::cout
                << "\n["
                << i + 1
                << "/"
                << DATASETS.size()
                << "] Processing \""
                << DATASETS[i]
                << "\"\n"
                << "------------------------------------------------------------\n";

            const fs::path datasetPath =
                DATASET_DIR / DATASETS[i];

            if (!fs::exists(datasetPath))
            {
                std::cout
                    << "  WARNING: Dataset not found. Skipping.\n";

                continue;
            }

            try
            {
                const fs::path compressedPath =
                    compressedDirectory /
                    (DATASETS[i] + ".deflate");

                Result result =
                    benchmarkDataset(
                        DATASETS[i],
                        threads,
                        compressedPath);

                printResult(result);

                writeLog(
                    result,
                    logDirectory /
                        (DATASETS[i] + ".log"));

                appendMasterCSV(result);

                results.push_back(result);
            }
            catch (const std::exception& e)
            {
                std::cerr
                    << "\nERROR processing "
                    << DATASETS[i]
                    << ": "
                    << e.what()
                    << "\n";
            }
        }

        // ----------------------------------------------------
        // FINAL TABLE
        // ----------------------------------------------------

        std::cout
            << "\n\n"
            << "============================================================================================================\n"
            << "FINAL DEFLATE RESULTS\n"
            << "============================================================================================================\n";

        std::cout
            << std::left
            << std::setw(18) << "Dataset"
            << std::right
            << std::setw(12) << "Original"
            << std::setw(14) << "Compressed"
            << std::setw(12) << "Ratio"
            << std::setw(14) << "Comp(s)"
            << std::setw(14) << "Decomp(s)"
            << std::setw(14) << "Comp MB/s"
            << std::setw(14) << "Decomp MB/s"
            << "\n";

        std::cout
            << "------------------------------------------------------------------------------------------------------------\n";

        for (const Result& r : results)
        {
            std::string name = r.dataset;

            if (name.length() > 17)
                name = name.substr(0, 17);

            std::cout
                << std::left
                << std::setw(18) << name
                << std::right
                << std::setw(12) << r.originalBytes
                << std::setw(14) << r.compressedBytes
                << std::setw(12)
                << std::fixed
                << std::setprecision(4)
                << r.compressionRatio
                << std::setw(14) << r.compressionTime
                << std::setw(14) << r.decompressionTime
                << std::setw(14) << r.compressionThroughput
                << std::setw(14) << r.decompressionThroughput
                << "\n";
        }

        // ----------------------------------------------------
        // SUMMARY
        // ----------------------------------------------------

        if (!results.empty())
        {
            const double averageCompression =
                std::accumulate(
                    results.begin(),
                    results.end(),
                    0.0,
                    [](
                        double sum,
                        const Result& r)
                    {
                        return sum + r.compressionTime;
                    }) /
                results.size();

            const double averageDecompression =
                std::accumulate(
                    results.begin(),
                    results.end(),
                    0.0,
                    [](
                        double sum,
                        const Result& r)
                    {
                        return sum + r.decompressionTime;
                    }) /
                results.size();

            const int losslessCount =
                static_cast<int>(
                    std::count_if(
                        results.begin(),
                        results.end(),
                        [](
                            const Result& r)
                        {
                            return r.lossless;
                        }));

            std::cout
                << "\n"
                << "============================================================\n"
                << "SUMMARY\n"
                << "============================================================\n"
                << "Datasets processed: "
                << results.size()
                << "/"
                << DATASETS.size()
                << "\n"
                << "Threads: "
                << threads
                << "\n"
                << "Average compression time: "
                << averageCompression
                << " s\n"
                << "Average decompression time: "
                << averageDecompression
                << " s\n"
                << "Lossless datasets: "
                << losslessCount
                << "/"
                << results.size()
                << "\n"
                << "\nMaster CSV:\n  "
                << fs::absolute(
                       RESULT_ROOT /
                       "deflate_all_runs.csv")
                << "\n"
                << "\nRun directory:\n  "
                << fs::absolute(runDirectory)
                << "\n";
        }

        std::cout
            << "\nBenchmark complete.\n";

        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr
            << "\nFATAL ERROR: "
            << e.what()
            << "\n";

        return 1;
    }
}
