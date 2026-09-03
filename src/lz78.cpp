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
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

namespace fs = std::filesystem;

// ============================================================
// CONFIGURATION
// ============================================================

static constexpr int MAX_THREADS = 22;

// Same benchmark philosophy as Huffman / RLE / LZ77.
static constexpr int TIMING_ITERATIONS = 100;

// LZ78 parameters.
static constexpr size_t BLOCK_SIZE = 1 * 1024 * 1024; // 1 MiB

// Dataset directory.
static const fs::path DATASET_DIR =
    "../datasets";

// Results.
static const fs::path RESULT_ROOT =
    "../results";

// ============================================================
// DATASETS
// ============================================================

static const std::vector<std::string> DATASETS = {
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
// LZ78 TOKEN
// ============================================================
//
// Standard LZ78 emits:
//
//     (dictionary_index, next_literal)
//
// dictionary_index = 0 means that the phrase starts directly
// with the literal.
//
// For the final phrase, if the input ends exactly after an
// existing dictionary phrase, we emit:
//
//     (dictionary_index, 0)
//
// with hasLiteral = 0.
//
// This implementation uses a fixed 6-byte serialized token:
//
//     hasLiteral : 1 byte
//     index      : 4 bytes
//     literal    : 1 byte
//
// The representation is intentionally simple and deterministic.
//
// ============================================================

struct Token
{
    uint8_t hasLiteral = 0;

    uint32_t index = 0;

    uint8_t literal = 0;
};

// ============================================================
// DICTIONARY ENTRY
// ============================================================
//
// Each dictionary phrase is represented as:
//
//     parent phrase + one byte
//
// This avoids storing strings for every dictionary entry.
//
// Entry 0 is the implicit empty phrase.
// ============================================================

struct DictionaryEntry
{
    uint32_t parent = 0;

    uint8_t character = 0;
};

// ============================================================
// COMPRESSED BLOCK
// ============================================================

struct CompressedBlock
{
    size_t originalSize = 0;

    std::vector<Token> tokens;
};

// ============================================================
// COMPRESSED DATA
// ============================================================

struct LZ78Compressed
{
    size_t originalSize = 0;

    std::vector<CompressedBlock> blocks;
};

// ============================================================
// BENCHMARK RESULT
// ============================================================

struct Result
{
    std::string dataset;

    std::string algorithm = "LZ78";

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
// TIME
// ============================================================

double nowSeconds()
{
    using clock =
        std::chrono::steady_clock;

    static const auto start =
        clock::now();

    auto current =
        clock::now();

    return std::chrono::duration<double>(
               current - start)
        .count();
}

// ============================================================
// MEMORY
// ============================================================

double getPeakWorkingSetMB()
{
#ifdef _WIN32

    PROCESS_MEMORY_COUNTERS pmc{};

    if (
        GetProcessMemoryInfo(
            GetCurrentProcess(),
            &pmc,
            sizeof(pmc)))
    {
        return static_cast<double>(
                   pmc.PeakWorkingSetSize) /
               (1024.0 * 1024.0);
    }

#endif

    return 0.0;
}

// ============================================================
// FILE READING
// ============================================================

std::vector<uint8_t> readFile(
    const fs::path &path)
{
    std::ifstream file(
        path,
        std::ios::binary);

    if (!file)
    {
        throw std::runtime_error(
            "Cannot open dataset: " +
            path.string());
    }

    file.seekg(
        0,
        std::ios::end);

    std::streamsize size =
        file.tellg();

    file.seekg(
        0,
        std::ios::beg);

    if (size < 0)
    {
        throw std::runtime_error(
            "Invalid file size: " +
            path.string());
    }

    std::vector<uint8_t> data(
        static_cast<size_t>(size));

    if (size > 0)
    {
        file.read(
            reinterpret_cast<char *>(
                data.data()),
            size);
    }

    return data;
}

// ============================================================
// ENTROPY
// ============================================================

double calculateEntropy(
    const std::vector<uint8_t> &data)
{
    if (data.empty())
        return 0.0;

    std::array<uint64_t, 256> frequency{};

    for (uint8_t byte : data)
    {
        frequency[byte]++;
    }

    double entropy = 0.0;

    const double total =
        static_cast<double>(
            data.size());

    for (uint64_t count : frequency)
    {
        if (count == 0)
            continue;

        double p =
            static_cast<double>(count) /
            total;

        entropy -=
            p * std::log2(p);
    }

    return entropy;
}

// ============================================================
// PROGRESS BAR
// ============================================================

void showProgress(
    size_t current,
    size_t total)
{
    constexpr int WIDTH = 40;

    double progress =
        total == 0
            ? 1.0
            : static_cast<double>(current) /
                  static_cast<double>(total);

    progress =
        std::clamp(
            progress,
            0.0,
            1.0);

    int filled =
        static_cast<int>(
            progress * WIDTH);

    std::cout << "[";

    for (int i = 0; i < WIDTH; ++i)
    {
        if (i < filled)
            std::cout << '#';
        else
            std::cout << '-';
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
// LZ78 DICTIONARY KEY
// ============================================================
//
// A dictionary transition is:
//
//     (parent_index, byte)
//
// parent_index is uint32_t and byte is uint8_t.
//
// Both fit safely into a uint64_t:
//
//     key = (parent_index << 8) | byte
//
// ============================================================

inline uint64_t makeKey(
    uint32_t parent,
    uint8_t byte)
{
    return
        (static_cast<uint64_t>(parent) << 8) |
        static_cast<uint64_t>(byte);
}

// ============================================================
// COMPRESS ONE BLOCK
// ============================================================

CompressedBlock compressBlock(
    const std::vector<uint8_t> &data,
    size_t begin,
    size_t end)
{
    CompressedBlock block;

    block.originalSize =
        end - begin;

    if (begin >= end)
        return block;

    const size_t size =
        end - begin;

    // key(parent, byte) -> dictionary index
    std::unordered_map<uint64_t, uint32_t> dictionary;

    // Reserve enough space to reduce repeated rehashing.
    dictionary.reserve(
        std::max<size_t>(
            16,
            size / 2));

    block.tokens.reserve(
        size);

    uint32_t nextIndex = 1;

    size_t position = 0;

    while (position < size)
    {
        uint32_t currentIndex = 0;

        // --------------------------------------------------------
        // Find the longest dictionary phrase beginning here.
        // --------------------------------------------------------

        while (position < size)
        {
            uint8_t byte =
                data[begin + position];

            uint64_t key =
                makeKey(
                    currentIndex,
                    byte);

            auto it =
                dictionary.find(key);

            if (it == dictionary.end())
                break;

            currentIndex =
                it->second;

            position++;
        }

        // --------------------------------------------------------
        // Case 1:
        // The input has ended exactly at an existing phrase.
        //
        // Emit the phrase index without a literal.
        // --------------------------------------------------------

        if (position >= size)
        {
            Token token;

            token.hasLiteral = 0;
            token.index = currentIndex;
            token.literal = 0;

            block.tokens.push_back(
                token);

            break;
        }

        // --------------------------------------------------------
        // Case 2:
        // We found an existing phrase followed by a new byte.
        //
        // Emit:
        //
        //     (currentIndex, newByte)
        //
        // and add that new phrase to the dictionary.
        // --------------------------------------------------------

        uint8_t nextByte =
            data[begin + position];

        Token token;

        token.hasLiteral = 1;
        token.index = currentIndex;
        token.literal = nextByte;

        block.tokens.push_back(
            token);

        // Add the newly discovered phrase.
        dictionary.emplace(
            makeKey(
                currentIndex,
                nextByte),
            nextIndex);

        nextIndex++;

        // The emitted token consumes the new byte.
        position++;
    }

    return block;
}

// ============================================================
// PARALLEL COMPRESSION
// ============================================================
//
// Each block has an independent LZ78 dictionary.
//
// This makes blocks independently compressible and allows
// different blocks to be processed by different threads.
//
// ============================================================

LZ78Compressed compressLZ78(
    const std::vector<uint8_t> &data,
    int threads,
    bool showBar = false)
{
    LZ78Compressed compressed;

    compressed.originalSize =
        data.size();

    if (data.empty())
        return compressed;

    size_t blockCount =
        (data.size() +
         BLOCK_SIZE -
         1) /
        BLOCK_SIZE;

    compressed.blocks.resize(
        blockCount);

    int workerCount =
        std::max(
            1,
            std::min(
                threads,
                static_cast<int>(
                    blockCount)));

    std::atomic<size_t> nextBlock{0};

    std::atomic<size_t> completed{0};

    std::vector<std::thread> workers;

    workers.reserve(
        workerCount);

    for (
        int worker = 0;
        worker < workerCount;
        ++worker)
    {
        workers.emplace_back(
            [&]()
            {
                while (true)
                {
                    size_t index =
                        nextBlock.fetch_add(
                            1);

                    if (
                        index >= blockCount)
                    {
                        break;
                    }

                    size_t begin =
                        index * BLOCK_SIZE;

                    size_t end =
                        std::min(
                            begin + BLOCK_SIZE,
                            data.size());

                    compressed.blocks[index] =
                        compressBlock(
                            data,
                            begin,
                            end);

                    completed.fetch_add(
                        1);
                }
            });
    }

    if (showBar)
    {
        while (
            completed.load() <
            blockCount)
        {
            std::cout
                << "\r  Compressing      ";

            showProgress(
                completed.load(),
                blockCount);

            std::this_thread::sleep_for(
                std::chrono::milliseconds(50));
        }
    }

    for (auto &worker : workers)
    {
        worker.join();
    }

    return compressed;
}

// ============================================================
// DECOMPRESS ONE BLOCK
// ============================================================

std::vector<uint8_t> decompressBlock(
    const CompressedBlock &block)
{
    std::vector<uint8_t> output;

    output.reserve(
        block.originalSize);

    // Entry 0 = empty phrase.
    std::vector<DictionaryEntry> dictionary;

    dictionary.reserve(
        block.tokens.size() + 1);

    dictionary.push_back(
        DictionaryEntry{0, 0});

    for (
        const Token &token :
        block.tokens)
    {
        if (
            token.index >=
            dictionary.size())
        {
            throw std::runtime_error(
                "Invalid LZ78 dictionary index.");
        }

        // --------------------------------------------------------
        // Reconstruct the referenced dictionary phrase.
        // --------------------------------------------------------

        std::vector<uint8_t> phrase;

        uint32_t index =
            token.index;

        while (index != 0)
        {
            const DictionaryEntry &entry =
                dictionary[index];

            phrase.push_back(
                entry.character);

            index =
                entry.parent;
        }

        std::reverse(
            phrase.begin(),
            phrase.end());

        output.insert(
            output.end(),
            phrase.begin(),
            phrase.end());

        // --------------------------------------------------------
        // If the token has a literal, append it and add the
        // resulting phrase to the dictionary.
        // --------------------------------------------------------

        if (token.hasLiteral)
        {
            output.push_back(
                token.literal);

            DictionaryEntry entry;

            entry.parent =
                token.index;

            entry.character =
                token.literal;

            dictionary.push_back(
                entry);
        }
    }

    if (
        output.size() !=
        block.originalSize)
    {
        throw std::runtime_error(
            "LZ78 block size mismatch.");
    }

    return output;
}

// ============================================================
// PARALLEL DECOMPRESSION
// ============================================================

std::vector<uint8_t> decompressLZ78(
    const LZ78Compressed &compressed,
    int threads,
    bool showBar = false)
{
    std::vector<
        std::vector<uint8_t>>
        blockOutputs(
            compressed.blocks.size());

    if (
        compressed.blocks.empty())
    {
        return {};
    }

    size_t blockCount =
        compressed.blocks.size();

    int workerCount =
        std::max(
            1,
            std::min(
                threads,
                static_cast<int>(
                    blockCount)));

    std::atomic<size_t> nextBlock{0};

    std::atomic<size_t> completed{0};

    std::vector<std::thread> workers;

    workers.reserve(
        workerCount);

    for (
        int worker = 0;
        worker < workerCount;
        ++worker)
    {
        workers.emplace_back(
            [&]()
            {
                while (true)
                {
                    size_t index =
                        nextBlock.fetch_add(
                            1);

                    if (
                        index >= blockCount)
                    {
                        break;
                    }

                    blockOutputs[index] =
                        decompressBlock(
                            compressed.blocks[index]);

                    completed.fetch_add(
                        1);
                }
            });
    }

    if (showBar)
    {
        while (
            completed.load() <
            blockCount)
        {
            std::cout
                << "\r  Decompressing    ";

            showProgress(
                completed.load(),
                blockCount);

            std::this_thread::sleep_for(
                std::chrono::milliseconds(50));
        }
    }

    for (auto &worker : workers)
    {
        worker.join();
    }

    std::vector<uint8_t> output;

    output.reserve(
        compressed.originalSize);

    for (
        const auto &block :
        blockOutputs)
    {
        output.insert(
            output.end(),
            block.begin(),
            block.end());
    }

    return output;
}

// ============================================================
// COMPRESSED SIZE
// ============================================================

uint64_t calculateCompressedSize(
    const LZ78Compressed &compressed)
{
    // Header:
    //
    // original size : 8 bytes
    // block count   : 8 bytes
    //
    // Per block:
    //
    // original size : 8 bytes
    // token count   : 8 bytes
    //
    // Per token:
    //
    // hasLiteral : 1 byte
    // index      : 4 bytes
    // literal    : 1 byte
    //
    // Total = exactly 6 bytes per token.

    uint64_t size = 16;

    for (
        const auto &block :
        compressed.blocks)
    {
        size += 16;

        size +=
            static_cast<uint64_t>(
                block.tokens.size()) *
            6;
    }

    return size;
}

// ============================================================
// SAVE COMPRESSED FILE
// ============================================================

void saveCompressedFile(
    const fs::path &path,
    const LZ78Compressed &compressed)
{
    std::ofstream file(
        path,
        std::ios::binary);

    if (!file)
    {
        throw std::runtime_error(
            "Cannot create compressed file: " +
            path.string());
    }

    const char magic[4] = {
        'L',
        'Z',
        '7',
        '8'};

    file.write(
        magic,
        4);

    uint64_t originalSize =
        compressed.originalSize;

    uint64_t blockCount =
        compressed.blocks.size();

    file.write(
        reinterpret_cast<const char *>(
            &originalSize),
        sizeof(originalSize));

    file.write(
        reinterpret_cast<const char *>(
            &blockCount),
        sizeof(blockCount));

    for (
        const auto &block :
        compressed.blocks)
    {
        uint64_t blockSize =
            block.originalSize;

        uint64_t tokenCount =
            block.tokens.size();

        file.write(
            reinterpret_cast<const char *>(
                &blockSize),
            sizeof(blockSize));

        file.write(
            reinterpret_cast<const char *>(
                &tokenCount),
            sizeof(tokenCount));

        for (
            const Token &token :
            block.tokens)
        {
            file.write(
                reinterpret_cast<
                    const char *>(
                    &token.hasLiteral),
                sizeof(token.hasLiteral));

            file.write(
                reinterpret_cast<
                    const char *>(
                    &token.index),
                sizeof(token.index));

            file.write(
                reinterpret_cast<
                    const char *>(
                    &token.literal),
                sizeof(token.literal));
        }
    }
}

// ============================================================
// WRITE LOG
// ============================================================

void writeLog(
    const Result &r,
    const fs::path &logPath)
{
    std::ofstream file(
        logPath);

    if (!file)
        return;

    file
        << "LZ78 COMPRESSION BENCHMARK\n"
        << "==========================\n\n"

        << "Dataset: "
        << r.dataset
        << "\n"

        << "Algorithm: "
        << r.algorithm
        << "\n"

        << "Threads: "
        << r.threads
        << "\n"

        << "Parallel mode: "
        << (r.threads == 1
                ? "Single-threaded"
                : "Parallel blocks")
        << "\n\n"

        << "Original size: "
        << r.originalBytes
        << " bytes\n"

        << "Compressed size: "
        << r.compressedBytes
        << " bytes\n"

        << "Compression ratio: "
        << std::setprecision(12)
        << r.compressionRatio
        << "\n"

        << "Space saved: "
        << r.spaceSaved
        << "%\n"

        << "Entropy: "
        << r.entropy
        << " bits/symbol\n\n"

        << "Compression time (per iteration): "
        << r.compressionTime
        << " s\n"

        << "Decompression time (per iteration): "
        << r.decompressionTime
        << " s\n"

        << "Compression throughput: "
        << r.compressionThroughput
        << " MB/s\n"

        << "Decompression throughput: "
        << r.decompressionThroughput
        << " MB/s\n"

        << "Peak working set: "
        << r.memoryMB
        << " MB\n"

        << "Timing iterations: "
        << TIMING_ITERATIONS
        << "\n\n"

        << "Lossless verification: "
        << (r.lossless
                ? "PASS"
                : "FAIL")
        << "\n";
}

// ============================================================
// MASTER CSV
// ============================================================

void appendMasterCSV(
    const Result &r)
{
    fs::path csvPath =
        RESULT_ROOT /
        "lz78_all_runs.csv";

    fs::create_directories(
        csvPath.parent_path());

    bool exists =
        fs::exists(csvPath) &&
        fs::file_size(csvPath) > 0;

    std::ofstream file(
        csvPath,
        std::ios::app);

    if (!file)
    {
        throw std::runtime_error(
            "Cannot open master CSV: " +
            csvPath.string());
    }

    if (!exists)
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
        << (r.lossless
                ? "true"
                : "false")
        << "\n";
}

// ============================================================
// BENCHMARK ONE DATASET
// ============================================================

Result benchmarkDataset(
    const std::string &filename,
    int threads,
    const fs::path &compressedPath)
{
    std::vector<uint8_t> data =
        readFile(
            DATASET_DIR / filename);

    Result result;

    result.dataset =
        filename;

    result.algorithm =
        "LZ78";

    result.threads =
        threads;

    result.originalBytes =
        data.size();

    result.entropy =
        calculateEntropy(
            data);

    std::cout
        << "  Original size: "
        << result.originalBytes
        << " bytes\n";

    // --------------------------------------------------------
    // WARM-UP
    // --------------------------------------------------------

    std::cout
        << "  Warming up        ";

    showProgress(0, 1);

    LZ78Compressed warmCompressed =
        compressLZ78(
            data,
            threads,
            false);

    std::vector<uint8_t>
        warmDecoded =
            decompressLZ78(
                warmCompressed,
                threads,
                false);

    (void)warmDecoded;

    std::cout
        << "\r  Warming up        ";

    showProgress(1, 1);

    std::cout << "\n";

    // --------------------------------------------------------
    // REFERENCE COMPRESSION
    // --------------------------------------------------------

    LZ78Compressed reference =
        compressLZ78(
            data,
            threads,
            false);

    result.compressedBytes =
        calculateCompressedSize(
            reference);

    // --------------------------------------------------------
    // COMPRESSION RATIO
    // --------------------------------------------------------

    if (
        result.originalBytes > 0 &&
        result.compressedBytes > 0)
    {
        result.compressionRatio =
            static_cast<double>(
                result.originalBytes) /
            static_cast<double>(
                result.compressedBytes);

        result.spaceSaved =
            (1.0 -
             static_cast<double>(
                 result.compressedBytes) /
                 static_cast<double>(
                     result.originalBytes)) *
            100.0;
    }

    // --------------------------------------------------------
    // COMPRESSION TIMING
    // --------------------------------------------------------

    std::cout
        << "  Compressing      ";

    showProgress(
        0,
        TIMING_ITERATIONS);

    double compressionStart =
        nowSeconds();

    LZ78Compressed timedCompressed;

    for (
        int iteration = 0;
        iteration < TIMING_ITERATIONS;
        ++iteration)
    {
        timedCompressed =
            compressLZ78(
                data,
                threads,
                iteration == 0);

        if (
            iteration % 2 == 0 ||
            iteration ==
                TIMING_ITERATIONS - 1)
        {
            std::cout
                << "\r  Compressing      ";

            showProgress(
                iteration + 1,
                TIMING_ITERATIONS);
        }
    }

    double compressionEnd =
        nowSeconds();

    result.compressionTime =
        (compressionEnd -
         compressionStart) /
        static_cast<double>(
            TIMING_ITERATIONS);

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

    std::cout
        << "  Decompressing    ";

    showProgress(
        0,
        TIMING_ITERATIONS);

    double decompressionStart =
        nowSeconds();

    std::vector<uint8_t>
        finalDecoded;

    for (
        int iteration = 0;
        iteration < TIMING_ITERATIONS;
        ++iteration)
    {
        finalDecoded =
            decompressLZ78(
                timedCompressed,
                threads,
                iteration == 0);

        if (
            iteration % 2 == 0 ||
            iteration ==
                TIMING_ITERATIONS - 1)
        {
            std::cout
                << "\r  Decompressing    ";

            showProgress(
                iteration + 1,
                TIMING_ITERATIONS);
        }
    }

    double decompressionEnd =
        nowSeconds();

    result.decompressionTime =
        (decompressionEnd -
         decompressionStart) /
        static_cast<double>(
            TIMING_ITERATIONS);

    std::cout << "\n";

    // --------------------------------------------------------
    // LOSSLESS VERIFICATION
    // --------------------------------------------------------

    result.lossless =
        (data ==
         finalDecoded);

    // --------------------------------------------------------
    // THROUGHPUT
    // --------------------------------------------------------

    constexpr double MB =
        1024.0 * 1024.0;

    if (
        result.compressionTime > 0.0)
    {
        result.compressionThroughput =
            (static_cast<double>(
                 result.originalBytes) /
             MB) /
            result.compressionTime;
    }

    if (
        result.decompressionTime > 0.0)
    {
        result.decompressionThroughput =
            (static_cast<double>(
                 result.originalBytes) /
             MB) /
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
// PRINT RESULT
// ============================================================

void printResult(
    const Result &r)
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
        << r.compressionThroughput
        << " MB/s\n"

        << "  Decompression throughput: "
        << r.decompressionThroughput
        << " MB/s\n"

        << "  Peak working set: "
        << r.memoryMB
        << " MB\n"

        << "  Lossless verification: "
        << (r.lossless
                ? "PASS"
                : "FAIL")
        << "\n";
}

// ============================================================
// TIMESTAMP
// ============================================================

std::string getTimestamp()
{
    auto now =
        std::chrono::system_clock::now();

    std::time_t time =
        std::chrono::system_clock::to_time_t(
            now);

    std::tm tm{};

#ifdef _WIN32
    localtime_s(
        &tm,
        &time);
#else
    localtime_r(
        &time,
        &tm);
#endif

    std::ostringstream out;

    out
        << std::put_time(
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
            << "LZ78 COMPRESSION BENCHMARK\n"
            << "============================================================\n\n";

        std::cout
            << "Timing iterations per measurement: "
            << TIMING_ITERATIONS
            << "\n"

            << "Datasets: "
            << DATASETS.size()
            << "\n"

            << "Parallel threads: 1-"
            << MAX_THREADS
            << "\n\n";

        int threads;

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

        // ----------------------------------------------------
        // CREATE UNIQUE RUN DIRECTORY
        // ----------------------------------------------------

        fs::path runDirectory =
            RESULT_ROOT /
            "lz78" /
            (std::to_string(
                 threads) +
             "_threads_" +
             getTimestamp());

        fs::path compressedDirectory =
            runDirectory /
            "compressed";

        fs::path logDirectory =
            runDirectory /
            "logs";

        fs::create_directories(
            compressedDirectory);

        fs::create_directories(
            logDirectory);

        // ----------------------------------------------------
        // CONFIGURATION
        // ----------------------------------------------------

        std::cout
            << "\n"
            << "Configuration\n"
            << "------------------------------------------------------------\n"

            << "Algorithm:        LZ78\n"

            << "Threads:          "
            << threads
            << "\n"

            << "Parallel mode:    "
            << (threads == 1
                    ? "Single-threaded"
                    : "Independent parallel blocks")
            << "\n"

            << "Block size:       "
            << BLOCK_SIZE / 1024
            << " KiB\n"

            << "Timing runs:      "
            << TIMING_ITERATIONS
            << "\n"

            << "Run directory:\n  "
            << runDirectory
            << "\n"

            << "------------------------------------------------------------\n";

        std::vector<Result> results;

        results.reserve(
            DATASETS.size());

        // ----------------------------------------------------
        // PROCESS DATASETS
        // ----------------------------------------------------

        for (
            size_t i = 0;
            i < DATASETS.size();
            ++i)
        {
            std::cout
                << "\n["
                << i + 1
                << "/"
                << DATASETS.size()
                << "] Processing \""
                << DATASETS[i]
                << "\"\n";

            std::cout
                << "------------------------------------------------------------\n";

            fs::path datasetPath =
                DATASET_DIR /
                DATASETS[i];

            if (
                !fs::exists(
                    datasetPath))
            {
                std::cout
                    << "  WARNING: Dataset not found. Skipping.\n";

                continue;
            }

            try
            {
                fs::path compressedPath =
                    compressedDirectory /
                    (DATASETS[i] +
                     ".lz78");

                Result result =
                    benchmarkDataset(
                        DATASETS[i],
                        threads,
                        compressedPath);

                printResult(
                    result);

                fs::path logPath =
                    logDirectory /
                    (DATASETS[i] +
                     ".log");

                writeLog(
                    result,
                    logPath);

                appendMasterCSV(
                    result);

                results.push_back(
                    result);
            }
            catch (
                const std::exception &e)
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
            << "FINAL LZ78 RESULTS\n"
            << "============================================================================================================\n";

        std::cout
            << std::left
            << std::setw(18)
            << "Dataset"
            << std::right
            << std::setw(10)
            << "Original"
            << std::setw(12)
            << "Compressed"
            << std::setw(12)
            << "Ratio"
            << std::setw(14)
            << "Comp(s)"
            << std::setw(14)
            << "Decomp(s)"
            << std::setw(14)
            << "Comp MB/s"
            << std::setw(14)
            << "Decomp MB/s"
            << "\n";

        std::cout
            << "------------------------------------------------------------------------------------------------------------\n";

        for (
            const Result &r :
            results)
        {
            std::string name =
                r.dataset;

            if (
                name.length() > 17)
            {
                name =
                    name.substr(
                        0,
                        17);
            }

            std::cout
                << std::left
                << std::setw(18)
                << name

                << std::right
                << std::setw(10)
                << r.originalBytes

                << std::setw(12)
                << r.compressedBytes

                << std::setw(12)
                << std::fixed
                << std::setprecision(4)
                << r.compressionRatio

                << std::setw(14)
                << r.compressionTime

                << std::setw(14)
                << r.decompressionTime

                << std::setw(14)
                << r.compressionThroughput

                << std::setw(14)
                << r.decompressionThroughput

                << "\n";
        }

        // ----------------------------------------------------
        // SUMMARY
        // ----------------------------------------------------

        if (!results.empty())
        {
            double averageCompression =
                std::accumulate(
                    results.begin(),
                    results.end(),
                    0.0,
                    [](
                        double sum,
                        const Result &r)
                    {
                        return sum +
                               r.compressionTime;
                    }) /
                results.size();

            double averageDecompression =
                std::accumulate(
                    results.begin(),
                    results.end(),
                    0.0,
                    [](
                        double sum,
                        const Result &r)
                    {
                        return sum +
                               r.decompressionTime;
                    }) /
                results.size();

            int losslessCount =
                static_cast<int>(
                    std::count_if(
                        results.begin(),
                        results.end(),
                        [](
                            const Result &r)
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
                       "lz78_all_runs.csv")
                << "\n"

                << "\nRun directory:\n  "
                << fs::absolute(
                       runDirectory)
                << "\n";

            if (
                losslessCount !=
                static_cast<int>(
                    results.size()))
            {
                std::cout
                    << "\nWARNING: One or more datasets "
                    << "failed lossless verification.\n";
            }
        }

        std::cout
            << "\nBenchmark complete.\n";
    }
    catch (
        const std::exception &e)
    {
        std::cerr
            << "\nFATAL ERROR: "
            << e.what()
            << "\n";

        return 1;
    }

    return 0;
}
