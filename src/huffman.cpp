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
#include <mutex>
#include <queue>
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

constexpr int TIMING_RUNS = 100;

constexpr uint64_t PROGRESS_STEP =
    1024ULL * 1024ULL; // 1 MB

constexpr int PROGRESS_WIDTH = 40;

// ============================================================
// DATASETS
// ============================================================

const std::vector<std::string> DATASETS = {
    "a.txt",
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
// HUFFMAN NODE
// ============================================================

struct HuffmanNode {

    uint64_t frequency = 0;

    int symbol = -1;

    HuffmanNode* left = nullptr;
    HuffmanNode* right = nullptr;

    bool isLeaf() const {
        return symbol >= 0;
    }
};

// ============================================================
// PRIORITY QUEUE COMPARATOR
// ============================================================

struct NodeComparator {

    bool operator()(
        const HuffmanNode* a,
        const HuffmanNode* b
    ) const {

        if (a->frequency != b->frequency) {
            return a->frequency > b->frequency;
        }

        return a->symbol > b->symbol;
    }
};

// ============================================================
// HUFFMAN CODE
//
// We deliberately do NOT use uint64_t for the code.
//
// A valid Huffman tree for 256 symbols can theoretically
// contain codewords longer than 64 bits.
// ============================================================

struct HuffmanCode {

    std::vector<uint8_t> bits;
};

// ============================================================
// ENCODED BLOCK
// ============================================================

struct EncodedBlock {

    std::vector<uint8_t> data;

    uint64_t validBits = 0;
};

// ============================================================
// ENCODE RESULT
// ============================================================

struct EncodeResult {

    std::array<uint64_t, 256> frequencies{};

    std::vector<EncodedBlock> blocks;

    uint64_t totalBits = 0;

    uint64_t payloadBytes = 0;
};

// ============================================================
// EXPERIMENT RESULT
// ============================================================

struct ExperimentResult {

    std::string dataset;

    std::string algorithm = "Huffman";

    int threads = 1;

    uint64_t originalBytes = 0;

    uint64_t compressedBytes = 0;

    uint64_t payloadBytes = 0;

    uint64_t headerBytes = 0;

    int uniqueSymbols = 0;

    double entropy = 0.0;

    double achievedBitsPerByte = 0.0;

    double compressionRatio = 0.0;

    double spaceSavedPercent = 0.0;

    double compressionTimeSeconds = 0.0;

    double decompressionTimeSeconds = 0.0;

    double compressionThroughputMBs = 0.0;

    double decompressionThroughputMBs = 0.0;

    double peakWorkingSetMB = 0.0;

    bool lossless = false;
};

// ============================================================
// GLOBAL CODE TABLE
// ============================================================

std::array<HuffmanCode, 256> codeTable;

// ============================================================
// FILE FORMAT
//
// HUF2:
//
// Fixed header:
//
// magic                    4 bytes
// original size            8 bytes
// threads                  4 bytes
// block count              4 bytes
// frequencies              256 * 8 bytes
//
// Per block:
//
// valid bits               8 bytes
// payload bytes            8 bytes
//
// Then all block payloads.
//
// ============================================================

constexpr uint64_t FIXED_HEADER_SIZE =
    4 +
    8 +
    4 +
    4 +
    (256 * 8);

// ============================================================
// FIND PROJECT DIRECTORIES
// ============================================================

void initializePaths()
{
    fs::path cwd =
        fs::current_path();

    fs::path candidate1 =
        cwd / "datasets";

    fs::path candidate2 =
        cwd / "../datasets";

    fs::path candidate3 =
        cwd / "../../datasets";

    if (fs::exists(candidate1)) {

        DATASET_DIR =
            fs::weakly_canonical(candidate1);

        RESULT_ROOT =
            cwd / "results";
    }
    else if (fs::exists(candidate2)) {

        DATASET_DIR =
            fs::weakly_canonical(candidate2);

        RESULT_ROOT =
            fs::weakly_canonical(
                cwd / "../results"
            );
    }
    else if (fs::exists(candidate3)) {

        DATASET_DIR =
            fs::weakly_canonical(candidate3);

        RESULT_ROOT =
            fs::weakly_canonical(
                cwd / "../../results"
            );
    }
    else {

        throw std::runtime_error(
            "Could not find datasets/silesia.\n"
            "Run the program from the project root or src folder."
        );
    }
}

// ============================================================
// TIMESTAMP
// ============================================================

std::string getTimestamp()
{
    auto now =
        std::chrono::system_clock::now();

    std::time_t time =
        std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};

#ifdef _WIN32
    localtime_s(
        &localTime,
        &time
    );
#else
    localtime_r(
        &time,
        &localTime
    );
#endif

    std::ostringstream out;

    out << std::put_time(
        &localTime,
        "%Y%m%d_%H%M%S"
    );

    return out.str();
}

// ============================================================
// HIGH RESOLUTION TIMER
// ============================================================

double nowSeconds()
{
#ifdef _WIN32

    static LARGE_INTEGER frequency;
    static bool initialized = false;

    if (!initialized) {

        QueryPerformanceFrequency(
            &frequency
        );

        initialized = true;
    }

    LARGE_INTEGER counter;

    QueryPerformanceCounter(
        &counter
    );

    return
        static_cast<double>(
            counter.QuadPart
        )
        /
        static_cast<double>(
            frequency.QuadPart
        );

#else

    using Clock =
        std::chrono::high_resolution_clock;

    return
        std::chrono::duration<double>(
            Clock::now()
                .time_since_epoch()
        ).count();

#endif
}

// ============================================================
// CURRENT WORKING SET
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
// PEAK WORKING SET
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
            "Cannot open file: "
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
            "Failed to read file: "
            + path.string()
        );
    }

    return data;
}

// ============================================================
// WRITE BINARY
// ============================================================

void writeBytes(
    std::ofstream& file,
    const void* data,
    size_t size
)
{
    if (size == 0)
        return;

    file.write(
        reinterpret_cast<const char*>(
            data
        ),
        static_cast<std::streamsize>(
            size
        )
    );

    if (!file) {

        throw std::runtime_error(
            "Write operation failed."
        );
    }
}

// ============================================================
// FREE TREE
// ============================================================

void freeTree(
    HuffmanNode* node
)
{
    if (!node)
        return;

    freeTree(
        node->left
    );

    freeTree(
        node->right
    );

    delete node;
}

// ============================================================
// BUILD HUFFMAN TREE
// ============================================================

HuffmanNode* buildTree(
    const std::array<uint64_t, 256>& frequencies
)
{
    std::priority_queue<
        HuffmanNode*,
        std::vector<HuffmanNode*>,
        NodeComparator
    > pq;

    for (
        int symbol = 0;
        symbol < 256;
        ++symbol
    ) {

        if (
            frequencies[symbol]
            == 0
        ) {

            continue;
        }

        auto* node =
            new HuffmanNode();

        node->frequency =
            frequencies[symbol];

        node->symbol =
            symbol;

        pq.push(
            node
        );
    }

    if (pq.empty()) {

        return nullptr;
    }

    if (pq.size() == 1) {

        HuffmanNode* only =
            pq.top();

        pq.pop();

        return only;
    }

    while (
        pq.size() > 1
    ) {

        HuffmanNode* left =
            pq.top();

        pq.pop();

        HuffmanNode* right =
            pq.top();

        pq.pop();

        auto* parent =
            new HuffmanNode();

        parent->frequency =
            left->frequency
            +
            right->frequency;

        parent->left =
            left;

        parent->right =
            right;

        pq.push(
            parent
        );
    }

    return pq.top();
}

// ============================================================
// GENERATE CODES
// ============================================================

void generateCodesRecursive(
    HuffmanNode* node,
    std::vector<uint8_t>& path
)
{
    if (!node)
        return;

    if (node->isLeaf()) {

        if (path.empty()) {

            codeTable[
                node->symbol
            ].bits = {0};

        }
        else {

            codeTable[
                node->symbol
            ].bits = path;
        }

        return;
    }

    path.push_back(0);

    generateCodesRecursive(
        node->left,
        path
    );

    path.pop_back();

    path.push_back(1);

    generateCodesRecursive(
        node->right,
        path
    );

    path.pop_back();
}

// ============================================================
// BUILD CODE TABLE
// ============================================================

void buildCodeTable(
    const std::array<uint64_t, 256>& frequencies
)
{
    for (
        auto& code :
        codeTable
    ) {

        code.bits.clear();
    }

    HuffmanNode* root =
        buildTree(
            frequencies
        );

    if (!root)
        return;

    std::vector<uint8_t> path;

    generateCodesRecursive(
        root,
        path
    );

    freeTree(
        root
    );
}

// ============================================================
// FREQUENCY COUNT
// ============================================================

std::array<uint64_t, 256>
calculateFrequencies(
    const std::vector<uint8_t>& data,
    int threads
)
{
    std::array<uint64_t, 256>
        global{};

    if (data.empty())
        return global;

    int actualThreads =
        std::max(
            1,
            std::min(
                threads,
                static_cast<int>(
                    data.size()
                )
            )
        );

    std::vector<
        std::array<uint64_t, 256>
    > local(
        actualThreads
    );

    std::vector<std::thread>
        workers;

    workers.reserve(
        actualThreads
    );

    size_t chunk =
        (
            data.size()
            +
            actualThreads
            -
            1
        )
        /
        actualThreads;

    for (
        int t = 0;
        t < actualThreads;
        ++t
    ) {

        size_t begin =
            static_cast<size_t>(t)
            * chunk;

        size_t end =
            std::min(
                begin + chunk,
                data.size()
            );

        workers.emplace_back(
            [
                &data,
                &local,
                begin,
                end,
                t
            ] {

                for (
                    size_t i = begin;
                    i < end;
                    ++i
                ) {

                    local[t][
                        data[i]
                    ]++;
                }
            }
        );
    }

    for (
        auto& worker :
        workers
    ) {

        worker.join();
    }

    for (
        const auto& localFreq :
        local
    ) {

        for (
            int symbol = 0;
            symbol < 256;
            ++symbol
        ) {

            global[symbol] +=
                localFreq[symbol];
        }
    }

    return global;
}

// ============================================================
// ENCODE ONE BLOCK
// ============================================================

EncodedBlock encodeBlock(
    const std::vector<uint8_t>& input,
    size_t begin,
    size_t end
)
{
    EncodedBlock block;

    if (
        begin >= end
    ) {

        return block;
    }

    // Estimate payload capacity.

    size_t inputSize =
        end - begin;

    block.data.reserve(
        std::max<size_t>(
            1,
            inputSize / 2
        )
    );

    uint8_t currentByte = 0;

    int bitsInCurrentByte = 0;

    for (
        size_t i = begin;
        i < end;
        ++i
    ) {

        const auto& code =
            codeTable[
                input[i]
            ].bits;

        for (
            uint8_t bit :
            code
        ) {

            currentByte =
                static_cast<uint8_t>(
                    (
                        currentByte
                        << 1
                    )
                    |
                    bit
                );

            bitsInCurrentByte++;

            block.validBits++;

            if (
                bitsInCurrentByte
                ==
                8
            ) {

                block.data.push_back(
                    currentByte
                );

                currentByte = 0;

                bitsInCurrentByte = 0;
            }
        }
    }

    if (
        bitsInCurrentByte
        > 0
    ) {

        currentByte =
            static_cast<uint8_t>(
                currentByte
                <<
                (
                    8
                    -
                    bitsInCurrentByte
                )
            );

        block.data.push_back(
            currentByte
        );
    }

    return block;
}

// ============================================================
// PROGRESS DISPLAY
// ============================================================

void showProgress(
    uint64_t current,
    uint64_t total,
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
// PARALLEL ENCODE
// ============================================================

EncodeResult encode(
    const std::vector<uint8_t>& input,
    int threads,
    bool showBar
)
{
    EncodeResult result;

    // --------------------------------------------------------
    // Frequency calculation
    // --------------------------------------------------------

    result.frequencies =
        calculateFrequencies(
            input,
            threads
        );

    // --------------------------------------------------------
    // Build Huffman tree/code table
    // --------------------------------------------------------

    buildCodeTable(
        result.frequencies
    );

    if (
        input.empty()
    ) {

        return result;
    }

    // --------------------------------------------------------
    // Determine number of blocks
    // --------------------------------------------------------

    int blockCount =
        std::max(
            1,
            std::min(
                threads,
                static_cast<int>(
                    input.size()
                )
            )
        );

    size_t chunk =
        (
            input.size()
            +
            blockCount
            -
            1
        )
        /
        blockCount;

    result.blocks.resize(
        blockCount
    );

    // --------------------------------------------------------
    // Progress state
    // --------------------------------------------------------

    std::atomic<uint64_t>
        processedBytes{0};

    std::atomic<int>
        nextBlock{0};

    // --------------------------------------------------------
    // Worker threads
    // --------------------------------------------------------

    std::vector<std::thread>
        workers;

    workers.reserve(
        blockCount
    );

    for (
        int t = 0;
        t < blockCount;
        ++t
    ) {

        workers.emplace_back(
            [
                &input,
                &result,
                &processedBytes,
                chunk,
                blockCount,
                t
            ] {

                size_t begin =
                    static_cast<size_t>(t)
                    * chunk;

                size_t end =
                    std::min(
                        begin + chunk,
                        input.size()
                    );

                result.blocks[t] =
                    encodeBlock(
                        input,
                        begin,
                        end
                    );

                processedBytes.fetch_add(
                    end - begin,
                    std::memory_order_relaxed
                );
            }
        );
    }

    // --------------------------------------------------------
    // Display progress while workers run
    //
    // No extra progress thread is created.
    // The main thread handles the display.
    // --------------------------------------------------------

    if (
        showBar
        &&
        input.size()
        >=
        PROGRESS_STEP
    ) {

        uint64_t lastShown = 0;

        bool workersFinished = false;

        while (!workersFinished) {

            workersFinished = true;

            for (
                auto& worker :
                workers
            ) {

                // Can't query join status directly,
                // so we only use this loop to keep the
                // console responsive before joining.
            }

            uint64_t current =
                processedBytes.load(
                    std::memory_order_relaxed
                );

            if (
                current
                >=
                lastShown
                +
                PROGRESS_STEP
            ) {

                showProgress(
                    current,
                    input.size(),
                    "Compressing"
                );

                lastShown =
                    current;
            }

            /*
                We don't have a portable way to ask
                std::thread whether it has completed.

                Therefore we break when all bytes have
                been processed.
            */

            if (
                current
                >=
                input.size()
            ) {

                break;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(30)
            );
        }
    }

    // --------------------------------------------------------
    // Join
    // --------------------------------------------------------

    for (
        auto& worker :
        workers
    ) {

        worker.join();
    }

    if (showBar) {

        showProgress(
            input.size(),
            input.size(),
            "Compressing"
        );
    }

    // --------------------------------------------------------
    // Statistics
    // --------------------------------------------------------

    for (
        const auto& block :
        result.blocks
    ) {

        result.totalBits +=
            block.validBits;

        result.payloadBytes +=
            block.data.size();
    }

    return result;
}

// ============================================================
// DECODE ONE BLOCK
// ============================================================

std::vector<uint8_t> decodeBlock(
    const EncodedBlock& block,
    HuffmanNode* root
)
{
    std::vector<uint8_t> output;

    if (
        !root
        ||
        block.validBits == 0
    ) {

        return output;
    }

    // --------------------------------------------------------
    // Single-symbol dataset
    // --------------------------------------------------------

    if (
        root->isLeaf()
    ) {

        /*
            Every symbol has a 1-bit code.

            Therefore number of output symbols =
            number of valid bits.
        */

        output.resize(
            static_cast<size_t>(
                block.validBits
            ),
            static_cast<uint8_t>(
                root->symbol
            )
        );

        return output;
    }

    // --------------------------------------------------------
    // Normal decoding
    // --------------------------------------------------------

    HuffmanNode* current =
        root;

    uint64_t processedBits = 0;

    for (
        size_t byteIndex = 0;
        byteIndex < block.data.size();
        ++byteIndex
    ) {

        uint8_t byte =
            block.data[byteIndex];

        int bitsInThisByte = 8;

        uint64_t remaining =
            block.validBits
            -
            processedBits;

        if (
            remaining < 8
        ) {

            bitsInThisByte =
                static_cast<int>(
                    remaining
                );
        }

        for (
            int shift = 7;
            shift >=
                8 - bitsInThisByte;
            --shift
        ) {

            int bit =
                (
                    byte
                    >>
                    shift
                )
                &
                1;

            if (bit == 0) {

                current =
                    current->left;
            }
            else {

                current =
                    current->right;
            }

            if (!current) {

                throw std::runtime_error(
                    "Invalid Huffman bitstream."
                );
            }

            if (
                current->isLeaf()
            ) {

                output.push_back(
                    static_cast<uint8_t>(
                        current->symbol
                    )
                );

                current =
                    root;
            }

            processedBits++;
        }
    }

    return output;
}

// ============================================================
// PARALLEL DECODE
// ============================================================

std::vector<uint8_t> decode(
    const EncodeResult& encoded,
    uint64_t originalSize,
    int threads,
    bool showBar
)
{
    if (
        originalSize == 0
    ) {

        return {};
    }

    HuffmanNode* root =
        buildTree(
            encoded.frequencies
        );

    if (!root) {

        return {};
    }

    int blockCount =
        static_cast<int>(
            encoded.blocks.size()
        );

    int actualThreads =
        std::max(
            1,
            std::min(
                threads,
                blockCount
            )
        );

    std::vector<
        std::vector<uint8_t>
    > decodedBlocks(
        blockCount
    );

    std::atomic<int>
        nextBlock{0};

    std::atomic<uint64_t>
        processedBytes{0};

    std::vector<std::thread>
        workers;

    workers.reserve(
        actualThreads
    );

    // --------------------------------------------------------
    // Workers
    // --------------------------------------------------------

    for (
        int t = 0;
        t < actualThreads;
        ++t
    ) {

        workers.emplace_back(
            [
                &encoded,
                &decodedBlocks,
                &nextBlock,
                &processedBytes,
                root
            ] {

                while (true) {

                    int index =
                        nextBlock.fetch_add(
                            1,
                            std::memory_order_relaxed
                        );

                    if (
                        index
                        >=
                        static_cast<int>(
                            encoded.blocks.size()
                        )
                    ) {

                        break;
                    }

                    decodedBlocks[index] =
                        decodeBlock(
                            encoded.blocks[index],
                            root
                        );

                    processedBytes.fetch_add(
                        encoded.blocks[index]
                            .data
                            .size(),
                        std::memory_order_relaxed
                    );
                }
            }
        );
    }

    // --------------------------------------------------------
    // Progress
    // --------------------------------------------------------

    if (
        showBar
        &&
        encoded.payloadBytes
        >=
        PROGRESS_STEP
    ) {

        uint64_t lastShown = 0;

        while (
            processedBytes.load(
                std::memory_order_relaxed
            )
            <
            encoded.payloadBytes
        ) {

            uint64_t current =
                processedBytes.load(
                    std::memory_order_relaxed
                );

            if (
                current
                >=
                lastShown
                +
                PROGRESS_STEP
            ) {

                showProgress(
                    current,
                    encoded.payloadBytes,
                    "Decompressing"
                );

                lastShown =
                    current;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(30)
            );
        }
    }

    // --------------------------------------------------------
    // Join
    // --------------------------------------------------------

    for (
        auto& worker :
        workers
    ) {

        worker.join();
    }

    if (showBar) {

        showProgress(
            encoded.payloadBytes,
            encoded.payloadBytes,
            "Decompressing"
        );
    }

    // --------------------------------------------------------
    // Reassemble
    // --------------------------------------------------------

    std::vector<uint8_t> output;

    output.reserve(
        static_cast<size_t>(
            originalSize
        )
    );

    for (
        const auto& block :
        decodedBlocks
    ) {

        output.insert(
            output.end(),
            block.begin(),
            block.end()
        );
    }

    freeTree(
        root
    );

    return output;
}

// ============================================================
// ENTROPY
// ============================================================

double calculateEntropy(
    const std::vector<uint8_t>& data
)
{
    if (data.empty())
        return 0.0;

    std::array<uint64_t, 256>
        frequencies{};

    for (
        uint8_t byte :
        data
    ) {

        frequencies[byte]++;
    }

    double entropy = 0.0;

    double n =
        static_cast<double>(
            data.size()
        );

    for (
        uint64_t frequency :
        frequencies
    ) {

        if (frequency == 0)
            continue;

        double p =
            static_cast<double>(
                frequency
            )
            /
            n;

        entropy -=
            p
            *
            std::log2(p);
    }

    return entropy;
}

// ============================================================
// UNIQUE SYMBOL COUNT
// ============================================================

int countUniqueSymbols(
    const std::vector<uint8_t>& data
)
{
    std::array<bool, 256>
        seen{};

    int count = 0;

    for (
        uint8_t byte :
        data
    ) {

        if (!seen[byte]) {

            seen[byte] = true;

            count++;
        }
    }

    return count;
}

// ============================================================
// MEDIAN
// ============================================================

double calculateMedian(
    std::vector<double> values
)
{
    if (values.empty())
        return 0.0;

    std::sort(
        values.begin(),
        values.end()
    );

    size_t n =
        values.size();

    if (
        n % 2
        ==
        1
    ) {

        return values[
            n / 2
        ];
    }

    return
        (
            values[
                n / 2 - 1
            ]
            +
            values[
                n / 2
            ]
        )
        /
        2.0;
}

// ============================================================
// SAVE COMPRESSED FILE
// ============================================================

void saveCompressedFile(
    const fs::path& path,
    const EncodeResult& encoded,
    uint64_t originalSize,
    int threads
)
{
    std::ofstream file(
        path,
        std::ios::binary
    );

    if (!file) {

        throw std::runtime_error(
            "Cannot create file: "
            + path.string()
        );
    }

    // --------------------------------------------------------
    // Magic = HUF2
    // --------------------------------------------------------

    uint32_t magic =
        0x32554648;

    uint32_t threadCount =
        static_cast<uint32_t>(
            threads
        );

    uint32_t blockCount =
        static_cast<uint32_t>(
            encoded.blocks.size()
        );

    writeBytes(
        file,
        &magic,
        sizeof(magic)
    );

    writeBytes(
        file,
        &originalSize,
        sizeof(originalSize)
    );

    writeBytes(
        file,
        &threadCount,
        sizeof(threadCount)
    );

    writeBytes(
        file,
        &blockCount,
        sizeof(blockCount)
    );

    // --------------------------------------------------------
    // Frequencies
    // --------------------------------------------------------

    for (
        uint64_t frequency :
        encoded.frequencies
    ) {

        writeBytes(
            file,
            &frequency,
            sizeof(frequency)
        );
    }

    // --------------------------------------------------------
    // Block metadata
    // --------------------------------------------------------

    for (
        const auto& block :
        encoded.blocks
    ) {

        uint64_t validBits =
            block.validBits;

        uint64_t payloadBytes =
            block.data.size();

        writeBytes(
            file,
            &validBits,
            sizeof(validBits)
        );

        writeBytes(
            file,
            &payloadBytes,
            sizeof(payloadBytes)
        );
    }

    // --------------------------------------------------------
    // Payload
    // --------------------------------------------------------

    for (
        const auto& block :
        encoded.blocks
    ) {

        if (
            !block.data.empty()
        ) {

            writeBytes(
                file,
                block.data.data(),
                block.data.size()
            );
        }
    }
}

// ============================================================
// WRITE CSV HEADER
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
        << "payload_bytes,"
        << "header_bytes,"
        << "unique_symbols,"
        << "entropy_bits_per_symbol,"
        << "achieved_payload_bits_per_byte,"
        << "compression_ratio,"
        << "space_saved_percent,"
        << "compression_time_seconds,"
        << "decompression_time_seconds,"
        << "compression_throughput_MB_s,"
        << "decompression_throughput_MB_s,"
        << "peak_working_set_MB,"
        << "lossless\n";
}

// ============================================================
// WRITE CSV
// ============================================================

void writeCSV(
    const std::vector<ExperimentResult>& results
)
{
    fs::path filePath =
        RUN_DIR / "results.csv";

    std::ofstream file(
        filePath
    );

    if (!file) {

        throw std::runtime_error(
            "Cannot create results CSV."
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
            << r.payloadBytes
            << ","
            << r.headerBytes
            << ","
            << r.uniqueSymbols
            << ","
            << std::setprecision(12)
            << r.entropy
            << ","
            << r.achievedBitsPerByte
            << ","
            << r.compressionRatio
            << ","
            << r.spaceSavedPercent
            << ","
            << r.compressionTimeSeconds
            << ","
            << r.decompressionTimeSeconds
            << ","
            << r.compressionThroughputMBs
            << ","
            << r.decompressionThroughputMBs
            << ","
            << r.peakWorkingSetMB
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
// APPEND TO MASTER CSV
// ============================================================

void appendMasterCSV(
    const std::vector<ExperimentResult>& results
)
{
    fs::path masterPath =
        RESULT_ROOT
        /
        "huffman_all_runs.csv";

    bool exists =
        fs::exists(masterPath);

    std::ofstream file(
        masterPath,
        std::ios::app
    );

    if (!file) {

        throw std::runtime_error(
            "Cannot open master CSV."
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
            << r.payloadBytes
            << ","
            << r.headerBytes
            << ","
            << r.uniqueSymbols
            << ","
            << std::setprecision(12)
            << r.entropy
            << ","
            << r.achievedBitsPerByte
            << ","
            << r.compressionRatio
            << ","
            << r.spaceSavedPercent
            << ","
            << r.compressionTimeSeconds
            << ","
            << r.decompressionTimeSeconds
            << ","
            << r.compressionThroughputMBs
            << ","
            << r.decompressionThroughputMBs
            << ","
            << r.peakWorkingSetMB
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
    const std::vector<ExperimentResult>& results
)
{
    fs::path path =
        RUN_DIR / "results.json";

    std::ofstream file(
        path
    );

    if (!file) {

        throw std::runtime_error(
            "Cannot create JSON."
        );
    }

    file << "[\n";

    for (
        size_t i = 0;
        i < results.size();
        ++i
    ) {

        const auto& r =
            results[i];

        file
            << "  {\n"

            << "    \"dataset\": \""
            << r.dataset
            << "\",\n"

            << "    \"algorithm\": \""
            << r.algorithm
            << "\",\n"

            << "    \"threads\": "
            << r.threads
            << ",\n"

            << "    \"original_bytes\": "
            << r.originalBytes
            << ",\n"

            << "    \"compressed_bytes\": "
            << r.compressedBytes
            << ",\n"

            << "    \"payload_bytes\": "
            << r.payloadBytes
            << ",\n"

            << "    \"header_bytes\": "
            << r.headerBytes
            << ",\n"

            << "    \"unique_symbols\": "
            << r.uniqueSymbols
            << ",\n"

            << std::setprecision(12)

            << "    \"entropy_bits_per_symbol\": "
            << r.entropy
            << ",\n"

            << "    \"achieved_payload_bits_per_byte\": "
            << r.achievedBitsPerByte
            << ",\n"

            << "    \"compression_ratio\": "
            << r.compressionRatio
            << ",\n"

            << "    \"space_saved_percent\": "
            << r.spaceSavedPercent
            << ",\n"

            << "    \"compression_time_seconds\": "
            << r.compressionTimeSeconds
            << ",\n"

            << "    \"decompression_time_seconds\": "
            << r.decompressionTimeSeconds
            << ",\n"

            << "    \"compression_throughput_MB_s\": "
            << r.compressionThroughputMBs
            << ",\n"

            << "    \"decompression_throughput_MB_s\": "
            << r.decompressionThroughputMBs
            << ",\n"

            << "    \"peak_working_set_MB\": "
            << r.peakWorkingSetMB
            << ",\n"

            << "    \"lossless\": "
            << (
                r.lossless
                    ? "true"
                    : "false"
            )
            << "\n"

            << "  }";

        if (
            i + 1
            <
            results.size()
        ) {

            file << ",";
        }

        file << "\n";
    }

    file << "]\n";
}

// ============================================================
// WRITE LOG
// ============================================================

void writeLog(
    const ExperimentResult& r
)
{
    fs::path path =
        LOG_DIR
        /
        (
            r.dataset
            + "_"
            + std::to_string(
                r.threads
            )
            + "threads.log"
        );

    std::ofstream file(
        path
    );

    if (!file) {

        throw std::runtime_error(
            "Cannot create log."
        );
    }

    file
        << "============================================================\n"
        << "HUFFMAN COMPRESSION EXPERIMENT\n"
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

        << "Parallel block encoding: YES\n"

        << "Parallel block decoding: YES\n\n"

        << "Original bytes: "
        << r.originalBytes
        << "\n"

        << "Compressed bytes: "
        << r.compressedBytes
        << "\n"

        << "Payload bytes: "
        << r.payloadBytes
        << "\n"

        << "Header bytes: "
        << r.headerBytes
        << "\n"

        << "Unique symbols: "
        << r.uniqueSymbols
        << "\n"

        << std::setprecision(12)

        << "Entropy bits/symbol: "
        << r.entropy
        << "\n"

        << "Achieved payload bits/byte: "
        << r.achievedBitsPerByte
        << "\n"

        << "Compression ratio: "
        << r.compressionRatio
        << "\n"

        << "Space saved percent: "
        << r.spaceSavedPercent
        << "\n"

        << "Compression time seconds: "
        << r.compressionTimeSeconds
        << "\n"

        << "Decompression time seconds: "
        << r.decompressionTimeSeconds
        << "\n"

        << "Compression throughput MB/s: "
        << r.compressionThroughputMBs
        << "\n"

        << "Decompression throughput MB/s: "
        << r.decompressionThroughputMBs
        << "\n"

        << "Peak working set MB: "
        << r.peakWorkingSetMB
        << "\n"

        << "Timing iterations: "
        << TIMING_RUNS
        << "\n"
        << "Timing methodology: total time for all iterations / iterations"
        << "\n"

        << "Lossless verification: "
        << (
            r.lossless
                ? "PASS"
                : "FAIL"
        )
        << "\n";
}

// ============================================================
// RUN EXPERIMENT
// ============================================================

ExperimentResult runExperiment(
    const std::string& filename,
    int threads
)
{
    ExperimentResult result;

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
            + ".huff"
        );

    // --------------------------------------------------------
    // Read
    // --------------------------------------------------------

    std::vector<uint8_t> original =
        readFile(
            inputPath
        );

    result.originalBytes =
        original.size();

    result.uniqueSymbols =
        countUniqueSymbols(
            original
        );

    result.entropy =
        calculateEntropy(
            original
        );

    std::cout
        << "  Original size: "
        << result.originalBytes
        << " bytes\n";

    // --------------------------------------------------------
    // Memory baseline
    // --------------------------------------------------------

    uint64_t memoryBefore =
        getCurrentWorkingSet();

    // --------------------------------------------------------
    // Warm-up
    //
    // Prevents first-run initialization from dominating
    // benchmark measurements.
    // --------------------------------------------------------

    encode(
        original,
        threads,
        false
    );

    // --------------------------------------------------------
    // Compression benchmark
    //
    // Each timing measurement performs the complete compression
    // operation TIMING_RUNS times in the SAME process.
    //
    // We record the total elapsed time and divide by the number
    // of iterations. This greatly reduces timing noise for
    // short-running datasets.
    // --------------------------------------------------------

    EncodeResult finalEncoded;

    double compressionTotalSeconds = 0.0;

    for (
        int run = 0;
        run < TIMING_RUNS;
        ++run
    ) {

        bool showBar =
            (
                run == 0
                &&
                original.size()
                >=
                PROGRESS_STEP
            );

        double start =
            nowSeconds();

        EncodeResult encoded =
            encode(
                original,
                threads,
                showBar
            );

        double end =
            nowSeconds();

        compressionTotalSeconds +=
            end - start;

        finalEncoded =
            std::move(
                encoded
            );
    }

    result.compressionTimeSeconds =
        compressionTotalSeconds
        /
        static_cast<double>(
            TIMING_RUNS
        );

    // --------------------------------------------------------
    // Save compressed file
    // --------------------------------------------------------

    saveCompressedFile(
        compressedPath,
        finalEncoded,
        original.size(),
        threads
    );

    // --------------------------------------------------------
    // Decompression benchmark
    //
    // The same final encoded representation is decoded
    // TIMING_RUNS times in the same process.
    //
    // Again, total elapsed time is divided by the number
    // of iterations.
    // --------------------------------------------------------

    std::vector<uint8_t>
        finalDecoded;

    double decompressionTotalSeconds = 0.0;

    for (
        int run = 0;
        run < TIMING_RUNS;
        ++run
    ) {

        bool showBar =
            (
                run == 0
                &&
                finalEncoded.payloadBytes
                >=
                PROGRESS_STEP
            );

        double start =
            nowSeconds();

        std::vector<uint8_t> decoded =
            decode(
                finalEncoded,
                original.size(),
                threads,
                showBar
            );

        double end =
            nowSeconds();

        decompressionTotalSeconds +=
            end - start;

        finalDecoded =
            std::move(
                decoded
            );
    }

    result.decompressionTimeSeconds =
        decompressionTotalSeconds
        /
        static_cast<double>(
            TIMING_RUNS
        );

    // --------------------------------------------------------
    // Lossless verification
    // --------------------------------------------------------

    result.lossless =
        (
            original
            ==
            finalDecoded
        );

    // --------------------------------------------------------
    // Memory
    // --------------------------------------------------------

    uint64_t peakMemory =
        getPeakWorkingSet();

    /*
        The value is the process peak working set.

        Because every dataset is measured inside the same
        process, this is a process-level peak rather than a
        perfectly isolated per-dataset allocation.

        The final research harness can run each experiment
        as a child process for fully isolated memory results.
    */

    result.peakWorkingSetMB =
        static_cast<double>(
            peakMemory
        )
        /
        (
            1024.0
            *
            1024.0
        );

    (void)memoryBefore;

    // --------------------------------------------------------
    // Size statistics
    // --------------------------------------------------------

    result.payloadBytes =
        finalEncoded.payloadBytes;

    result.headerBytes =
        FIXED_HEADER_SIZE
        +
        (
            finalEncoded.blocks.size()
            *
            16
        );

    result.compressedBytes =
        result.headerBytes
        +
        result.payloadBytes;

    // --------------------------------------------------------
    // Compression ratio
    // --------------------------------------------------------

    if (
        result.compressedBytes
        > 0
    ) {

        result.compressionRatio =
            static_cast<double>(
                result.originalBytes
            )
            /
            static_cast<double>(
                result.compressedBytes
            );
    }

    // --------------------------------------------------------
    // Space saved
    // --------------------------------------------------------

    if (
        result.originalBytes
        > 0
    ) {

        result.spaceSavedPercent =
            (
                (
                    static_cast<double>(
                        result.originalBytes
                    )
                    -
                    static_cast<double>(
                        result.compressedBytes
                    )
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
    // Payload bits per original byte
    // --------------------------------------------------------

    if (
        result.originalBytes
        > 0
    ) {

        result.achievedBitsPerByte =
            (
                static_cast<double>(
                    result.payloadBytes
                )
                *
                8.0
            )
            /
            static_cast<double>(
                result.originalBytes
            );
    }

    // --------------------------------------------------------
    // Timing
    //
    // compressionTimeSeconds and decompressionTimeSeconds were
    // calculated above as total_time / TIMING_RUNS.
    // --------------------------------------------------------

    // --------------------------------------------------------
    // Throughput
    // --------------------------------------------------------

    if (
        result.compressionTimeSeconds
        > 0
    ) {

        result.compressionThroughputMBs =
            static_cast<double>(
                result.originalBytes
            )
            /
            (
                result.compressionTimeSeconds
                *
                1024.0
                *
                1024.0
            );
    }

    if (
        result.decompressionTimeSeconds
        > 0
    ) {

        result.decompressionThroughputMBs =
            static_cast<double>(
                result.originalBytes
            )
            /
            (
                result.decompressionTimeSeconds
                *
                1024.0
                *
                1024.0
            );
    }

    // --------------------------------------------------------
    // Console output
    // --------------------------------------------------------

    std::cout
        << "\n"

        << "  Threads: "
        << result.threads
        << "\n"

        << "  Compressed size: "
        << result.compressedBytes
        << " bytes\n"

        << "  Payload size: "
        << result.payloadBytes
        << " bytes\n"

        << "  Header size: "
        << result.headerBytes
        << " bytes\n"

        << "  Compression ratio: "
        << std::fixed
        << std::setprecision(6)
        << result.compressionRatio
        << "\n"

        << "  Space saved: "
        << std::fixed
        << std::setprecision(2)
        << result.spaceSavedPercent
        << "%\n"

        << "  Entropy: "
        << std::fixed
        << std::setprecision(6)
        << result.entropy
        << " bits/symbol\n"

        << "  Achieved payload: "
        << std::fixed
        << std::setprecision(6)
        << result.achievedBitsPerByte
        << " bits/byte\n"

        << "  Compression time (per iteration): "
        << std::fixed
        << std::setprecision(6)
        << result.compressionTimeSeconds
        << " s\n"

        << "  Decompression time (per iteration): "
        << std::fixed
        << std::setprecision(6)
        << result.decompressionTimeSeconds
        << " s\n"

        << "  Compression throughput: "
        << std::fixed
        << std::setprecision(2)
        << result.compressionThroughputMBs
        << " MB/s\n"

        << "  Decompression throughput: "
        << std::fixed
        << std::setprecision(2)
        << result.decompressionThroughputMBs
        << " MB/s\n"

        << "  Peak working set: "
        << std::fixed
        << std::setprecision(2)
        << result.peakWorkingSetMB
        << " MB\n"

        << "  Lossless verification: "
        << (
            result.lossless
                ? "PASS"
                : "FAIL"
        )
        << "\n";

    return result;
}

// ============================================================
// MAIN
// ============================================================

int main()
{
    try {

        // ----------------------------------------------------
        // Paths
        // ----------------------------------------------------

        initializePaths();

        // ----------------------------------------------------
        // Hardware
        // ----------------------------------------------------

        unsigned int hardwareThreads =
            std::thread::hardware_concurrency();

        if (
            hardwareThreads == 0
        ) {

            hardwareThreads = 1;
        }

        // ----------------------------------------------------
        // Banner
        // ----------------------------------------------------

        std::cout
            << "\n"
            << "============================================================\n"
            << "HUFFMAN COMPRESSION RESEARCH BENCHMARK\n"
            << "============================================================\n\n";

        std::cout
            << "Dataset directory:\n  "
            << DATASET_DIR
            << "\n\n";

        std::cout
            << "Detected hardware threads: "
            << hardwareThreads
            << "\n";

        // ----------------------------------------------------
        // Thread selection
        // ----------------------------------------------------

        int threads = 1;

        std::cout
            << "\n"
            << "How many threads should be used? [1-"
            << hardwareThreads
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
                static_cast<int>(
                    hardwareThreads
                )
            );

        // ----------------------------------------------------
        // Create unique run directory
        // ----------------------------------------------------

        RESULT_ROOT =
            fs::absolute(
                RESULT_ROOT
            );

        std::string timestamp =
            getTimestamp();

        RUN_DIR =
            RESULT_ROOT
            /
            "huffman"
            /
            (
                std::to_string(
                    threads
                )
                +
                "_threads"
                +
                "_"
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
        // Configuration
        // ----------------------------------------------------

        std::cout
            << "\n"
            << "Configuration\n"
            << "------------------------------------------------------------\n"

            << "Algorithm:        Huffman\n"

            << "Threads:          "
            << threads
            << "\n"

            << "Parallel mode:    "
            << (
                threads == 1
                    ? "Single-threaded"
                    : "Parallel blocks"
            )
            << "\n"

            << "Timing iterations: "
            << TIMING_RUNS
            << "\n"
            << "Timing method:    Total time / iterations"
            << "\n"

            << "Run directory:\n  "
            << RUN_DIR
            << "\n"

            << "------------------------------------------------------------\n";

        // ----------------------------------------------------
        // Results
        // ----------------------------------------------------

        std::vector<ExperimentResult>
            results;

        results.reserve(
            DATASETS.size()
        );

        // ----------------------------------------------------
        // Process datasets
        // ----------------------------------------------------

        for (
            size_t i = 0;
            i < DATASETS.size();
            ++i
        ) {

            std::cout
                << "\n"
                << "["
                << i + 1
                << "/"
                << DATASETS.size()
                << "] Processing "
                << DATASETS[i]
                << "\n";

            std::cout
                << "------------------------------------------------------------\n";

            fs::path path =
                DATASET_DIR
                /
                DATASETS[i];

            if (
                !fs::exists(path)
            ) {

                std::cout
                    << "  WARNING: Dataset not found. Skipping.\n";

                continue;
            }

            ExperimentResult result =
                runExperiment(
                    DATASETS[i],
                    threads
                );

            writeLog(
                result
            );

            results.push_back(
                std::move(
                    result
                )
            );
        }

        // ----------------------------------------------------
        // Save results
        // ----------------------------------------------------

        writeCSV(
            results
        );

        writeJSON(
            results
        );

        appendMasterCSV(
            results
        );

        // ----------------------------------------------------
        // Final table
        // ----------------------------------------------------

        std::cout
            << "\n\n"
            << "============================================================================================================\n"
            << "FINAL RESULTS\n"
            << "============================================================================================================\n";

        std::cout
            << std::left
            << std::setw(15)
            << "Dataset"

            << std::right
            << std::setw(8)
            << "Threads"

            << std::setw(13)
            << "Original"

            << std::setw(13)
            << "Compressed"

            << std::setw(9)
            << "Ratio"

            << std::setw(10)
            << "Saved%"

            << std::setw(11)
            << "Comp(s)"

            << std::setw(11)
            << "Decomp(s)"

            << std::setw(10)
            << "Result"

            << "\n";

        std::cout
            << "------------------------------------------------------------------------------------------------------------\n";

        int failures = 0;

        for (
            const auto& r :
            results
        ) {

            std::cout
                << std::left
                << std::setw(15)
                << r.dataset

                << std::right
                << std::setw(8)
                << r.threads

                << std::setw(13)
                << r.originalBytes

                << std::setw(13)
                << r.compressedBytes

                << std::setw(9)
                << std::fixed
                << std::setprecision(3)
                << r.compressionRatio

                << std::setw(10)
                << std::setprecision(2)
                << r.spaceSavedPercent

                << std::setw(11)
                << std::setprecision(4)
                << r.compressionTimeSeconds

                << std::setw(11)
                << r.decompressionTimeSeconds

                << std::setw(10)
                << (
                    r.lossless
                        ? "PASS"
                        : "FAIL"
                )

                << "\n";

            if (
                !r.lossless
            ) {

                failures++;
            }
        }

        std::cout
            << "============================================================================================================\n";

        // ----------------------------------------------------
        // Summary
        // ----------------------------------------------------

        std::cout
            << "\n";

        if (
            failures == 0
        ) {

            std::cout
                << "ALL DATASETS PASSED LOSSLESS VERIFICATION.\n";
        }
        else {

            std::cout
                << "WARNING: "
                << failures
                << " DATASET(S) FAILED.\n";
        }

        // ----------------------------------------------------
        // Output paths
        // ----------------------------------------------------

        std::cout
            << "\n"
            << "This run was saved to:\n"
            << "  "
            << RUN_DIR
            << "\n\n"

            << "Files:\n"
            << "  results.csv\n"
            << "  results.json\n"
            << "  compressed/*.huff\n"
            << "  logs/*.log\n\n"

            << "Master accumulated CSV:\n"
            << "  "
            << (
                RESULT_ROOT
                /
                "huffman_all_runs.csv"
            )
            << "\n\n"

            << "Experiment complete.\n";

        return (
            failures == 0
                ? 0
                : 1
        );
    }
    catch (
        const std::exception& e
    ) {

        std::cerr
            << "\nERROR: "
            << e.what()
            << "\n";

        return 1;
    }
}