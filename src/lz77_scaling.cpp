#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

// ============================================================
// CONFIGURATION
// ============================================================

const int EXPECTED_RUNS = 3;

// ============================================================
// RESULT FROM ONE BENCHMARK RUN
// ============================================================

struct Result {

    std::string dataset;

    std::string algorithm;

    int threads = 0;

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
// MEDIAN
// ============================================================

double median(
    std::vector<double> values
) {
    if (values.empty()) {
        return 0.0;
    }

    std::sort(
        values.begin(),
        values.end()
    );

    size_t n =
        values.size();

    if (n % 2 == 1) {

        return values[
            n / 2
        ];
    }

    return (
        values[n / 2 - 1]
        +
        values[n / 2]
    ) / 2.0;
}

// ============================================================
// AVERAGE
// ============================================================

double average(
    const std::vector<double>& values
) {
    if (values.empty()) {
        return 0.0;
    }

    double sum = 0.0;

    for (
        double value :
        values
    ) {

        sum += value;
    }

    return
        sum /
        static_cast<double>(
            values.size()
        );
}

// ============================================================
// STANDARD DEVIATION
// ============================================================

double standardDeviation(
    const std::vector<double>& values
) {
    if (values.size() <= 1) {
        return 0.0;
    }

    double mean =
        average(values);

    double sum = 0.0;

    for (
        double value :
        values
    ) {

        double difference =
            value - mean;

        sum +=
            difference *
            difference;
    }

    return std::sqrt(
        sum /
        static_cast<double>(
            values.size() - 1
        )
    );
}

// ============================================================
// SPLIT CSV
// ============================================================

std::vector<std::string> splitCSV(
    const std::string& line
) {
    std::vector<std::string> fields;

    std::stringstream ss(line);

    std::string field;

    while (
        std::getline(
            ss,
            field,
            ','
        )
    ) {

        fields.push_back(
            field
        );
    }

    return fields;
}

// ============================================================
// PARSE RESULT
// ============================================================

bool parseResult(
    const std::string& line,
    Result& r
) {
    auto f =
        splitCSV(line);

    /*
        LZ77 CSV:

        0  dataset
        1  algorithm
        2  threads
        3  original_bytes
        4  compressed_bytes
        5  entropy
        6  compression_ratio
        7  space_saved_percent
        8  compression_time
        9  decompression_time
        10 compression_throughput_MB_s
        11 decompression_throughput_MB_s
        12 memory_MB
        13 lossless
    */

    if (f.size() < 14) {
        return false;
    }

    try {

        r.dataset =
            f[0];

        r.algorithm =
            f[1];

        r.threads =
            std::stoi(
                f[2]
            );

        r.originalBytes =
            std::stoull(
                f[3]
            );

        r.compressedBytes =
            std::stoull(
                f[4]
            );

        r.entropy =
            std::stod(
                f[5]
            );

        r.compressionRatio =
            std::stod(
                f[6]
            );

        r.spaceSaved =
            std::stod(
                f[7]
            );

        r.compressionTime =
            std::stod(
                f[8]
            );

        r.decompressionTime =
            std::stod(
                f[9]
            );

        r.compressionThroughput =
            std::stod(
                f[10]
            );

        r.decompressionThroughput =
            std::stod(
                f[11]
            );

        r.memoryMB =
            std::stod(
                f[12]
            );

        r.lossless =
            (
                f[13] == "true"
                ||
                f[13] == "1"
            );

        return true;
    }
    catch (...) {

        return false;
    }
}

// ============================================================
// READ CSV
// ============================================================

std::vector<Result> readCSV(
    const fs::path& path
) {
    std::vector<Result> results;

    std::ifstream file(
        path
    );

    if (!file) {

        throw std::runtime_error(
            "Cannot open CSV:\n" +
            path.string()
        );
    }

    std::string line;

    // Skip header
    std::getline(
        file,
        line
    );

    while (
        std::getline(
            file,
            line
        )
    ) {

        if (line.empty()) {
            continue;
        }

        Result r;

        if (
            parseResult(
                line,
                r
            )
        ) {

            results.push_back(
                r
            );
        }
    }

    return results;
}

// ============================================================
// MAIN
// ============================================================

int main()
{
    try {

        // ====================================================
        // FIND MASTER CSV
        // ====================================================

        fs::path current =
            fs::current_path();

        /*
            You run this from:

                research-paper/src

            Therefore:

                ../results/lz77_all_runs.csv

            points to:

                research-paper/results/lz77_all_runs.csv
        */

        fs::path masterCSV =
            current
            /
            "../results/lz77_all_runs.csv";

        masterCSV =
            fs::weakly_canonical(
                masterCSV
            );

        // ----------------------------------------------------
        // FALLBACK
        // ----------------------------------------------------

        if (
            !fs::exists(
                masterCSV
            )
        ) {

            fs::path alternative =
                current
                /
                "../results/lz77/lz77_all_runs.csv";

            if (
                fs::exists(
                    alternative
                )
            ) {

                masterCSV =
                    fs::weakly_canonical(
                        alternative
                    );
            }
        }

        // ====================================================
        // HEADER
        // ====================================================

        std::cout
            << "\n"
            << "============================================================\n"
            << "LZ77 THREAD SCALING ANALYZER\n"
            << "============================================================\n\n";

        std::cout
            << "Using existing LZ77 benchmark results only.\n";

        std::cout
            << "No compression will be performed.\n";

        std::cout
            << "No decompression will be performed.\n";

        std::cout
            << "No benchmark will be executed.\n\n";

        std::cout
            << "Master CSV:\n"
            << "  "
            << masterCSV
            << "\n\n";

        // ====================================================
        // CHECK CSV
        // ====================================================

        if (
            !fs::exists(
                masterCSV
            )
        ) {

            std::cerr
                << "ERROR: Cannot find LZ77 master CSV.\n\n"

                << "Expected one of:\n"
                << "  "
                << (
                    current
                    /
                    "../results/lz77_all_runs.csv"
                )
                << "\n"

                << "  "
                << (
                    current
                    /
                    "../results/lz77/lz77_all_runs.csv"
                )
                << "\n";

            return 1;
        }

        // ====================================================
        // READ RESULTS
        // ====================================================

        std::vector<Result> results =
            readCSV(
                masterCSV
            );

        std::cout
            << "Total rows loaded: "
            << results.size()
            << "\n";

        // ====================================================
        // GROUP BY DATASET + THREAD
        // ====================================================

        using DatasetThreadKey =
            std::pair<
                std::string,
                int
            >;

        std::map<
            DatasetThreadKey,
            std::vector<Result>
        > grouped;

        for (
            const auto& r :
            results
        ) {

            grouped[
                {
                    r.dataset,
                    r.threads
                }
            ].push_back(
                r
            );
        }

        // ====================================================
        // PRINT THREAD COUNTS
        // ====================================================

        std::map<
            int,
            int
        > datasetsPerThread;

        for (
            const auto& pair :
            grouped
        ) {

            datasetsPerThread[
                pair.first.second
            ]++;
        }

        std::cout
            << "\nThread counts found:\n";

        for (
            const auto& pair :
            datasetsPerThread
        ) {

            std::cout
                << "  "
                << pair.first
                << " threads : "
                << pair.second
                << " datasets\n";
        }

        // ====================================================
        // VERIFY THREE RUNS
        // ====================================================

        bool allThreeRuns =
            true;

        std::cout
            << "\nChecking repetitions...\n";

        for (
            const auto& pair :
            grouped
        ) {

            if (
                pair.second.size()
                !=
                EXPECTED_RUNS
            ) {

                allThreeRuns =
                    false;

                std::cout
                    << "WARNING: "
                    << pair.first.first
                    << " / "
                    << pair.first.second
                    << " threads has "
                    << pair.second.size()
                    << " runs instead of "
                    << EXPECTED_RUNS
                    << ".\n";
            }
        }

        if (allThreeRuns) {

            std::cout
                << "All dataset/thread combinations "
                << "have exactly 3 runs.\n";
        }

        // ====================================================
        // CREATE OUTPUT DIRECTORY
        // ====================================================

        fs::path outputDir =
            current
            /
            "../results/lz77/scaling";

        fs::create_directories(
            outputDir
        );

        // ====================================================
        // DATASET-LEVEL MEDIAN RESULTS
        // ====================================================

        struct DatasetMedian {

            std::string dataset;

            int threads;

            uint64_t originalBytes;

            uint64_t compressedBytes;

            double entropy;

            double compressionRatio;

            double spaceSaved;

            double compressionTime;

            double decompressionTime;

            double compressionThroughput;

            double decompressionThroughput;

            double memoryMB;

            double compressionTimeStdDev;

            double decompressionTimeStdDev;

            bool lossless;
        };

        std::vector<
            DatasetMedian
        > datasetMedians;

        // ====================================================
        // CALCULATE MEDIAN OF THREE
        // ====================================================

        for (
            const auto& pair :
            grouped
        ) {

            const auto& rows =
                pair.second;

            std::vector<double>
                compressionTimes;

            std::vector<double>
                decompressionTimes;

            std::vector<double>
                compressionThroughputs;

            std::vector<double>
                decompressionThroughputs;

            std::vector<double>
                compressionRatios;

            std::vector<double>
                spaceSaved;

            std::vector<double>
                memory;

            std::vector<double>
                entropies;

            for (
                const auto& r :
                rows
            ) {

                compressionTimes.push_back(
                    r.compressionTime
                );

                decompressionTimes.push_back(
                    r.decompressionTime
                );

                compressionThroughputs.push_back(
                    r.compressionThroughput
                );

                decompressionThroughputs.push_back(
                    r.decompressionThroughput
                );

                compressionRatios.push_back(
                    r.compressionRatio
                );

                spaceSaved.push_back(
                    r.spaceSaved
                );

                memory.push_back(
                    r.memoryMB
                );

                entropies.push_back(
                    r.entropy
                );
            }

            DatasetMedian dm;

            dm.dataset =
                pair.first.first;

            dm.threads =
                pair.first.second;

            // File characteristics
            dm.originalBytes =
                rows[0].originalBytes;

            dm.compressedBytes =
                rows[0].compressedBytes;

            // Median values
            dm.entropy =
                median(
                    entropies
                );

            dm.compressionRatio =
                median(
                    compressionRatios
                );

            dm.spaceSaved =
                median(
                    spaceSaved
                );

            dm.compressionTime =
                median(
                    compressionTimes
                );

            dm.decompressionTime =
                median(
                    decompressionTimes
                );

            dm.compressionThroughput =
                median(
                    compressionThroughputs
                );

            dm.decompressionThroughput =
                median(
                    decompressionThroughputs
                );

            dm.memoryMB =
                median(
                    memory
                );

            // Lossless only if ALL runs passed
            dm.lossless =
                true;

            for (
                const auto& r :
                rows
            ) {

                if (!r.lossless) {

                    dm.lossless =
                        false;
                }
            }

            // Timing variation
            dm.compressionTimeStdDev =
                standardDeviation(
                    compressionTimes
                );

            dm.decompressionTimeStdDev =
                standardDeviation(
                    decompressionTimes
                );

            datasetMedians.push_back(
                dm
            );
        }

        // ====================================================
        // SORT MEDIANS
        // ====================================================

        std::sort(
            datasetMedians.begin(),
            datasetMedians.end(),
            [](
                const DatasetMedian& a,
                const DatasetMedian& b
            ) {

                if (
                    a.threads
                    !=
                    b.threads
                ) {

                    return
                        a.threads
                        <
                        b.threads;
                }

                return
                    a.dataset
                    <
                    b.dataset;
            }
        );

        // ====================================================
        // SAVE DATASET MEDIANS
        // ====================================================

        fs::path medianPath =
            outputDir
            /
            "dataset_medians.csv";

        {
            std::ofstream file(
                medianPath
            );

            file
                << "dataset,"
                << "threads,"
                << "original_bytes,"
                << "compressed_bytes,"
                << "entropy,"
                << "compression_ratio,"
                << "space_saved_percent,"
                << "median_compression_time,"
                << "median_decompression_time,"
                << "median_compression_throughput,"
                << "median_decompression_throughput,"
                << "median_memory_MB,"
                << "compression_time_stddev,"
                << "decompression_time_stddev,"
                << "lossless\n";

            for (
                const auto& d :
                datasetMedians
            ) {

                file
                    << d.dataset
                    << ","
                    << d.threads
                    << ","
                    << d.originalBytes
                    << ","
                    << d.compressedBytes
                    << ","

                    << std::setprecision(12)

                    << d.entropy
                    << ","

                    << d.compressionRatio
                    << ","

                    << d.spaceSaved
                    << ","

                    << d.compressionTime
                    << ","

                    << d.decompressionTime
                    << ","

                    << d.compressionThroughput
                    << ","

                    << d.decompressionThroughput
                    << ","

                    << d.memoryMB
                    << ","

                    << d.compressionTimeStdDev
                    << ","

                    << d.decompressionTimeStdDev
                    << ","

                    << (
                        d.lossless
                            ? "true"
                            : "false"
                    )

                    << "\n";
            }
        }

        std::cout
            << "\nGenerated:\n  "
            << medianPath
            << "\n";

        // ====================================================
        // FIND SINGLE-THREAD BASELINES
        // ====================================================

        std::map<
            std::string,
            double
        > compressionBaseline;

        std::map<
            std::string,
            double
        > decompressionBaseline;

        for (
            const auto& d :
            datasetMedians
        ) {

            if (
                d.threads == 1
            ) {

                compressionBaseline[
                    d.dataset
                ] =
                    d.compressionTime;

                decompressionBaseline[
                    d.dataset
                ] =
                    d.decompressionTime;
            }
        }

        // ====================================================
        // THREAD SUMMARY
        // ====================================================

        struct ThreadSummary {

            int threads;

            int datasetCount;

            double avgCompressionTime;

            double medianCompressionTime;

            double avgDecompressionTime;

            double medianDecompressionTime;

            double avgCompressionThroughput;

            double avgDecompressionThroughput;

            double compressionSpeedup;

            double decompressionSpeedup;

            double compressionEfficiency;

            double decompressionEfficiency;

            double avgCompressionRatio;

            double avgSpaceSaved;

            double avgEntropy;

            double avgMemory;

            double avgCompressionStdDev;

            double avgDecompressionStdDev;

            bool allLossless;
        };

        std::map<
            int,
            std::vector<DatasetMedian>
        > medianByThread;

        for (
            const auto& d :
            datasetMedians
        ) {

            medianByThread[
                d.threads
            ].push_back(
                d
            );
        }

        std::vector<
            ThreadSummary
        > summaries;

        // ====================================================
        // CALCULATE THREAD SUMMARIES
        // ====================================================

        for (
            const auto& pair :
            medianByThread
        ) {

            int threads =
                pair.first;

            const auto& datasets =
                pair.second;

            std::vector<double>
                compressionTimes;

            std::vector<double>
                decompressionTimes;

            std::vector<double>
                compressionThroughputs;

            std::vector<double>
                decompressionThroughputs;

            std::vector<double>
                compressionRatios;

            std::vector<double>
                saved;

            std::vector<double>
                entropies;

            std::vector<double>
                memories;

            std::vector<double>
                compStdDev;

            std::vector<double>
                decompStdDev;

            std::vector<double>
                compSpeedups;

            std::vector<double>
                decompSpeedups;

            ThreadSummary s;

            s.threads =
                threads;

            s.datasetCount =
                static_cast<int>(
                    datasets.size()
                );

            s.allLossless =
                true;

            // ------------------------------------------------
            // COLLECT DATA
            // ------------------------------------------------

            for (
                const auto& d :
                datasets
            ) {

                compressionTimes.push_back(
                    d.compressionTime
                );

                decompressionTimes.push_back(
                    d.decompressionTime
                );

                compressionThroughputs.push_back(
                    d.compressionThroughput
                );

                decompressionThroughputs.push_back(
                    d.decompressionThroughput
                );

                compressionRatios.push_back(
                    d.compressionRatio
                );

                saved.push_back(
                    d.spaceSaved
                );

                entropies.push_back(
                    d.entropy
                );

                memories.push_back(
                    d.memoryMB
                );

                compStdDev.push_back(
                    d.compressionTimeStdDev
                );

                decompStdDev.push_back(
                    d.decompressionTimeStdDev
                );

                if (
                    !d.lossless
                ) {

                    s.allLossless =
                        false;
                }

                // ------------------------------------------------
                // COMPRESSION SPEEDUP
                // ------------------------------------------------

                auto compIt =
                    compressionBaseline.find(
                        d.dataset
                    );

                if (
                    compIt
                    !=
                    compressionBaseline.end()
                    &&
                    d.compressionTime > 0
                ) {

                    compSpeedups.push_back(
                        compIt->second
                        /
                        d.compressionTime
                    );
                }

                // ------------------------------------------------
                // DECOMPRESSION SPEEDUP
                // ------------------------------------------------

                auto decompIt =
                    decompressionBaseline.find(
                        d.dataset
                    );

                if (
                    decompIt
                    !=
                    decompressionBaseline.end()
                    &&
                    d.decompressionTime > 0
                ) {

                    decompSpeedups.push_back(
                        decompIt->second
                        /
                        d.decompressionTime
                    );
                }
            }

            // =================================================
            // AGGREGATE
            // =================================================

            s.avgCompressionTime =
                average(
                    compressionTimes
                );

            s.medianCompressionTime =
                median(
                    compressionTimes
                );

            s.avgDecompressionTime =
                average(
                    decompressionTimes
                );

            s.medianDecompressionTime =
                median(
                    decompressionTimes
                );

            s.avgCompressionThroughput =
                average(
                    compressionThroughputs
                );

            s.avgDecompressionThroughput =
                average(
                    decompressionThroughputs
                );

            s.compressionSpeedup =
                average(
                    compSpeedups
                );

            s.decompressionSpeedup =
                average(
                    decompSpeedups
                );

            // =================================================
            // PARALLEL EFFICIENCY
            // =================================================

            s.compressionEfficiency =
                threads > 0
                    ?
                    s.compressionSpeedup
                    /
                    static_cast<double>(
                        threads
                    )
                    :
                    0.0;

            s.decompressionEfficiency =
                threads > 0
                    ?
                    s.decompressionSpeedup
                    /
                    static_cast<double>(
                        threads
                    )
                    :
                    0.0;

            // =================================================
            // OTHER METRICS
            // =================================================

            s.avgCompressionRatio =
                average(
                    compressionRatios
                );

            s.avgSpaceSaved =
                average(
                    saved
                );

            s.avgEntropy =
                average(
                    entropies
                );

            s.avgMemory =
                average(
                    memories
                );

            s.avgCompressionStdDev =
                average(
                    compStdDev
                );

            s.avgDecompressionStdDev =
                average(
                    decompStdDev
                );

            summaries.push_back(
                s
            );
        }

        // ====================================================
        // SAVE THREAD SUMMARY
        // ====================================================

        fs::path summaryPath =
            outputDir
            /
            "thread_scaling_summary.csv";

        {
            std::ofstream file(
                summaryPath
            );

            file
                << "threads,"
                << "dataset_count,"
                << "avg_compression_time,"
                << "median_of_dataset_compression_times,"
                << "avg_decompression_time,"
                << "median_of_dataset_decompression_times,"
                << "avg_compression_throughput_MB_s,"
                << "avg_decompression_throughput_MB_s,"
                << "compression_speedup,"
                << "decompression_speedup,"
                << "compression_efficiency,"
                << "decompression_efficiency,"
                << "avg_compression_ratio,"
                << "avg_space_saved_percent,"
                << "avg_entropy,"
                << "avg_memory_MB,"
                << "avg_compression_stddev,"
                << "avg_decompression_stddev,"
                << "all_lossless\n";

            for (
                const auto& s :
                summaries
            ) {

                file
                    << s.threads
                    << ","
                    << s.datasetCount
                    << ","

                    << std::setprecision(12)

                    << s.avgCompressionTime
                    << ","

                    << s.medianCompressionTime
                    << ","

                    << s.avgDecompressionTime
                    << ","

                    << s.medianDecompressionTime
                    << ","

                    << s.avgCompressionThroughput
                    << ","

                    << s.avgDecompressionThroughput
                    << ","

                    << s.compressionSpeedup
                    << ","

                    << s.decompressionSpeedup
                    << ","

                    << s.compressionEfficiency
                    << ","

                    << s.decompressionEfficiency
                    << ","

                    << s.avgCompressionRatio
                    << ","

                    << s.avgSpaceSaved
                    << ","

                    << s.avgEntropy
                    << ","

                    << s.avgMemory
                    << ","

                    << s.avgCompressionStdDev
                    << ","

                    << s.avgDecompressionStdDev
                    << ","

                    << (
                        s.allLossless
                            ? "true"
                            : "false"
                    )

                    << "\n";
            }
        }

        std::cout
            << "Generated:\n  "
            << summaryPath
            << "\n";

        // ====================================================
        // PRINT SUMMARY TABLE
        // ====================================================

        std::cout
            << "\n\n"
            << "============================================================================================================\n"

            << std::left
            << std::setw(10)
            << "Threads"

            << std::right
            << std::setw(12)
            << "Datasets"

            << std::setw(18)
            << "Comp Time"

            << std::setw(18)
            << "Decomp Time"

            << std::setw(18)
            << "Comp MB/s"

            << std::setw(18)
            << "Decomp MB/s"

            << std::setw(14)
            << "Comp Speedup"

            << std::setw(14)
            << "Decomp Speedup"

            << std::setw(14)
            << "Comp Eff %"

            << std::setw(14)
            << "Decomp Eff %"

            << "\n";

        std::cout
            << "------------------------------------------------------------------------------------------------------------\n";

        for (
            const auto& s :
            summaries
        ) {

            std::cout
                << std::left
                << std::setw(10)
                << s.threads

                << std::right
                << std::fixed
                << std::setprecision(6)

                << std::setw(12)
                << s.datasetCount

                << std::setw(18)
                << s.avgCompressionTime

                << std::setw(18)
                << s.avgDecompressionTime

                << std::setw(18)
                << s.avgCompressionThroughput

                << std::setw(18)
                << s.avgDecompressionThroughput

                << std::setw(14)
                << s.compressionSpeedup

                << std::setw(14)
                << s.decompressionSpeedup

                << std::setw(13)
                << s.compressionEfficiency * 100.0
                << "%"

                << std::setw(13)
                << s.decompressionEfficiency * 100.0
                << "%"

                << "\n";
        }

        std::cout
            << "============================================================================================================\n";

        // ====================================================
        // FIND BEST THREAD COUNTS
        // ====================================================

        int bestCompressionThreads =
            -1;

        int bestDecompressionThreads =
            -1;

        double bestCompressionTime =
            1e100;

        double bestDecompressionTime =
            1e100;

        double bestCompressionSpeedup =
            0.0;

        double bestDecompressionSpeedup =
            0.0;

        for (
            const auto& s :
            summaries
        ) {

            if (
                s.avgCompressionTime
                <
                bestCompressionTime
            ) {

                bestCompressionTime =
                    s.avgCompressionTime;

                bestCompressionThreads =
                    s.threads;
            }

            if (
                s.avgDecompressionTime
                <
                bestDecompressionTime
            ) {

                bestDecompressionTime =
                    s.avgDecompressionTime;

                bestDecompressionThreads =
                    s.threads;
            }

            if (
                s.compressionSpeedup
                >
                bestCompressionSpeedup
            ) {

                bestCompressionSpeedup =
                    s.compressionSpeedup;
            }

            if (
                s.decompressionSpeedup
                >
                bestDecompressionSpeedup
            ) {

                bestDecompressionSpeedup =
                    s.decompressionSpeedup;
            }
        }

        // ====================================================
        // FINAL RESULT
        // ====================================================

        fs::path finalPath =
            outputDir
            /
            "FINAL_LZ77_RESULT.csv";

        {
            std::ofstream file(
                finalPath
            );

            file
                << "metric,value\n"

                << "algorithm,LZ77\n"

                << "runs_per_dataset_thread_combination,"
                << EXPECTED_RUNS
                << "\n"

                << "total_raw_result_rows,"
                << results.size()
                << "\n"

                << "total_dataset_medians,"
                << datasetMedians.size()
                << "\n"

                << "thread_counts,"
                << summaries.size()
                << "\n"

                << "best_compression_threads,"
                << bestCompressionThreads
                << "\n"

                << "best_decompression_threads,"
                << bestDecompressionThreads
                << "\n"

                << "best_compression_time,"
                << std::setprecision(12)
                << bestCompressionTime
                << "\n"

                << "best_decompression_time,"
                << bestDecompressionTime
                << "\n"

                << "maximum_compression_speedup,"
                << bestCompressionSpeedup
                << "\n"

                << "maximum_decompression_speedup,"
                << bestDecompressionSpeedup
                << "\n";
        }

        std::cout
            << "\nGenerated:\n  "
            << finalPath
            << "\n";

        // ====================================================
        // GENERATE PYTHON GRAPH SCRIPT
        // ====================================================

        fs::path pythonPath =
            outputDir
            /
            "generate_graphs.py";

        std::ofstream py(
            pythonPath
        );

        if (!py) {

            throw std::runtime_error(
                "Cannot create Python graph script."
            );
        }

        py << R"PY(
import csv
import matplotlib.pyplot as plt

CSV_FILE = "thread_scaling_summary.csv"

rows = []

with open(
    CSV_FILE,
    "r",
    newline=""
) as f:

    reader = csv.DictReader(f)

    for row in reader:
        rows.append(row)


threads = [
    int(row["threads"])
    for row in rows
]


def values(column):

    return [
        float(row[column])
        for row in rows
    ]


def graph(
    y,
    title,
    ylabel,
    filename,
    percent=False
):

    plt.figure(
        figsize=(10, 6)
    )

    plt.plot(
        threads,
        y,
        marker="o",
        linewidth=2
    )

    plt.xlabel(
        "Number of Threads"
    )

    plt.ylabel(
        ylabel
    )

    plt.title(
        title
    )

    plt.xticks(
        threads
    )

    plt.grid(
        True,
        alpha=0.3
    )

    if percent:

        for x, value in zip(
            threads,
            y
        ):

            plt.annotate(
                f"{value:.1f}%",
                (x, value),
                xytext=(0, 8),
                textcoords="offset points",
                ha="center"
            )

    plt.tight_layout()

    plt.savefig(
        filename,
        dpi=300,
        bbox_inches="tight"
    )

    plt.close()

    print(
        "Generated:",
        filename
    )


# ============================================================
# 01 COMPRESSION TIME
# ============================================================

graph(
    values(
        "avg_compression_time"
    ),
    "LZ77 Compression Time vs Threads",
    "Compression Time (seconds)",
    "01_compression_time.png"
)


# ============================================================
# 02 DECOMPRESSION TIME
# ============================================================

graph(
    values(
        "avg_decompression_time"
    ),
    "LZ77 Decompression Time vs Threads",
    "Decompression Time (seconds)",
    "02_decompression_time.png"
)


# ============================================================
# 03 COMPRESSION THROUGHPUT
# ============================================================

graph(
    values(
        "avg_compression_throughput_MB_s"
    ),
    "LZ77 Compression Throughput vs Threads",
    "Compression Throughput (MB/s)",
    "03_compression_throughput.png"
)


# ============================================================
# 04 DECOMPRESSION THROUGHPUT
# ============================================================

graph(
    values(
        "avg_decompression_throughput_MB_s"
    ),
    "LZ77 Decompression Throughput vs Threads",
    "Decompression Throughput (MB/s)",
    "04_decompression_throughput.png"
)


# ============================================================
# 05 COMPRESSION SPEEDUP
# ============================================================

graph(
    values(
        "compression_speedup"
    ),
    "LZ77 Compression Speedup vs Threads",
    "Speedup (x)",
    "05_compression_speedup.png"
)


# ============================================================
# 06 DECOMPRESSION SPEEDUP
# ============================================================

graph(
    values(
        "decompression_speedup"
    ),
    "LZ77 Decompression Speedup vs Threads",
    "Speedup (x)",
    "06_decompression_speedup.png"
)


# ============================================================
# 07 COMPRESSION EFFICIENCY
# ============================================================

graph(
    [
        x * 100
        for x in values(
            "compression_efficiency"
        )
    ],
    "LZ77 Parallel Efficiency vs Threads",
    "Parallel Efficiency (%)",
    "07_parallel_efficiency.png",
    True
)


# ============================================================
# 08 DECOMPRESSION EFFICIENCY
# ============================================================

graph(
    [
        x * 100
        for x in values(
            "decompression_efficiency"
        )
    ],
    "LZ77 Decompression Efficiency vs Threads",
    "Parallel Efficiency (%)",
    "08_decompression_efficiency.png",
    True
)


# ============================================================
# 09 COMPRESSION RATIO
# ============================================================

graph(
    values(
        "avg_compression_ratio"
    ),
    "LZ77 Compression Ratio vs Threads",
    "Compression Ratio",
    "09_compression_ratio.png"
)


# ============================================================
# 10 SPACE SAVED
# ============================================================

graph(
    values(
        "avg_space_saved_percent"
    ),
    "LZ77 Space Saved vs Threads",
    "Space Saved (%)",
    "10_space_saved.png"
)


# ============================================================
# 11 MEMORY
# ============================================================

graph(
    values(
        "avg_memory_MB"
    ),
    "LZ77 Memory Usage vs Threads",
    "Memory Usage (MB)",
    "11_memory_usage.png"
)


# ============================================================
# 12 TIMING VARIATION
# ============================================================

graph(
    values(
        "avg_compression_stddev"
    ),
    "LZ77 Compression Timing Variation vs Threads",
    "Average Standard Deviation (seconds)",
    "12_compression_variation.png"
)


print()
print("All LZ77 graphs generated.")
)PY";

        py.close();

        std::cout
            << "\nGenerated Python graph script:\n  "
            << pythonPath
            << "\n";

        // ====================================================
        // RUN PYTHON
        // ====================================================

#ifdef _WIN32

        std::string command =
            "cd /d \""
            +
            outputDir.string()
            +
            "\" && python generate_graphs.py";

#else

        std::string command =
            "cd \""
            +
            outputDir.string()
            +
            "\" && python3 generate_graphs.py";

#endif

        std::cout
            << "\nGenerating LZ77 graphs...\n\n";

        int graphResult =
            std::system(
                command.c_str()
            );

        if (
            graphResult != 0
        ) {

            std::cout
                << "\nWARNING: Could not generate graphs.\n"
                << "Make sure matplotlib is installed:\n"
                << "  pip install matplotlib\n";
        }

        // ====================================================
        // FINAL MESSAGE
        // ====================================================

        std::cout
            << "\n"
            << "============================================================\n"
            << "LZ77 ANALYSIS COMPLETE\n"
            << "============================================================\n\n"

            << "Method:\n"
            << "  3 runs per dataset/thread combination\n"
            << "  Median taken across the 3 runs\n"
            << "  Dataset medians aggregated across datasets\n"
            << "  Speedup calculated against 1-thread median\n"
            << "  Parallel efficiency = speedup / thread count\n\n"

            << "Output directory:\n"
            << "  "
            << outputDir
            << "\n\n"

            << "Files:\n"
            << "  dataset_medians.csv\n"
            << "  thread_scaling_summary.csv\n"
            << "  FINAL_LZ77_RESULT.csv\n"
            << "  generate_graphs.py\n"

            << "\nGraphs:\n"
            << "  01_compression_time.png\n"
            << "  02_decompression_time.png\n"
            << "  03_compression_throughput.png\n"
            << "  04_decompression_throughput.png\n"
            << "  05_compression_speedup.png\n"
            << "  06_decompression_speedup.png\n"
            << "  07_parallel_efficiency.png\n"
            << "  08_decompression_efficiency.png\n"
            << "  09_compression_ratio.png\n"
            << "  10_space_saved.png\n"
            << "  11_memory_usage.png\n"
            << "  12_compression_variation.png\n"

            << "\n============================================================\n";

        return 0;
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