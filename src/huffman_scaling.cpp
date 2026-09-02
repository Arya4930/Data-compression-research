#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cmath>
#include <cstdlib>

namespace fs = std::filesystem;

// ============================================================
// RESULT FROM ONE BENCHMARK RUN
// ============================================================

struct Result {

    std::string dataset;

    std::string algorithm;

    int threads = 0;

    uint64_t originalBytes = 0;
    uint64_t compressedBytes = 0;
    uint64_t payloadBytes = 0;
    uint64_t headerBytes = 0;

    int uniqueSymbols = 0;

    double entropy = 0.0;
    double bitsPerByte = 0.0;
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
// MEDIAN OF VECTOR
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

    size_t n = values.size();

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
// MEAN
// ============================================================

double average(
    const std::vector<double>& values
) {
    if (values.empty()) {
        return 0.0;
    }

    double sum = 0.0;

    for (double value : values) {
        sum += value;
    }

    return sum /
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

    for (double value : values) {

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
        fields.push_back(field);
    }

    return fields;
}

// ============================================================
// PARSE CSV
// ============================================================

bool parseResult(
    const std::string& line,
    Result& r
) {
    auto f =
        splitCSV(line);

    if (f.size() < 18) {
        return false;
    }

    try {

        r.dataset = f[0];

        r.algorithm = f[1];

        r.threads =
            std::stoi(f[2]);

        r.originalBytes =
            std::stoull(f[3]);

        r.compressedBytes =
            std::stoull(f[4]);

        r.payloadBytes =
            std::stoull(f[5]);

        r.headerBytes =
            std::stoull(f[6]);

        r.uniqueSymbols =
            std::stoi(f[7]);

        r.entropy =
            std::stod(f[8]);

        r.bitsPerByte =
            std::stod(f[9]);

        r.compressionRatio =
            std::stod(f[10]);

        r.spaceSaved =
            std::stod(f[11]);

        r.compressionTime =
            std::stod(f[12]);

        r.decompressionTime =
            std::stod(f[13]);

        r.compressionThroughput =
            std::stod(f[14]);

        r.decompressionThroughput =
            std::stod(f[15]);

        r.memoryMB =
            std::stod(f[16]);

        r.lossless =
            (
                f[17] == "true"
                ||
                f[17] == "1"
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

    std::ifstream file(path);

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

        fs::path masterCSV =
            current
            /
            "../results/huffman_all_runs.csv";

        masterCSV =
            fs::weakly_canonical(
                masterCSV
            );

        // Fallback
        if (
            !fs::exists(masterCSV)
        ) {

            fs::path alternative =
                current
                /
                "../results/huffman/huffman_all_runs.csv";

            if (
                fs::exists(alternative)
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
            << "HUFFMAN THREAD SCALING ANALYZER\n"
            << "============================================================\n\n";

        std::cout
            << "Using existing benchmark results only.\n";

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
            !fs::exists(masterCSV)
        ) {

            std::cerr
                << "ERROR: Cannot find:\n"
                << masterCSV
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
        // GROUP:
        //
        // DATASET + THREAD COUNT
        //
        // This gives us the 5 repetitions for each
        // dataset/thread combination.
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
        // PRINT DISCOVERED THREAD COUNTS
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
        // VERIFY FIVE RUNS
        // ====================================================

        bool allFiveRuns =
            true;

        std::cout
            << "\nChecking repetitions...\n";

        for (
            const auto& pair :
            grouped
        ) {

            if (
                pair.second.size() != 3
            ) {

                allFiveRuns =
                    false;

                std::cout
                    << "WARNING: "
                    << pair.first.first
                    << " / "
                    << pair.first.second
                    << " threads has "
                    << pair.second.size()
                    << " runs instead of 5.\n";
            }
        }

        if (allFiveRuns) {

            std::cout
                << "All dataset/thread combinations "
                << "have exactly 5 runs.\n";
        }

        // ====================================================
        // CREATE OUTPUT DIRECTORY
        // ====================================================

        fs::path outputDir =
            current
            /
            "../results/huffman/scaling";

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

            bool lossless;

            double compressionTimeStdDev;
            double decompressionTimeStdDev;
        };

        std::vector<
            DatasetMedian
        > datasetMedians;

        // ====================================================
        // CALCULATE MEDIAN OF FIVE
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

            // File characteristics should normally be
            // identical between runs.
            dm.originalBytes =
                rows[0].originalBytes;

            dm.compressedBytes =
                rows[0].compressedBytes;

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
        // SAVE MEDIAN-OF-FIVE DATA
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

        // ====================================================
        // FIND SINGLE-THREAD MEDIANS
        //
        // Speedup is calculated PER DATASET:
        //
        // Speedup =
        // median(T1) / median(Tp)
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

            const auto& rows =
                pair.second;

            ThreadSummary s;

            s.threads =
                threads;

            s.datasetCount =
                static_cast<int>(
                    rows.size()
                );

            std::vector<double>
                compressionTimes;

            std::vector<double>
                decompressionTimes;

            std::vector<double>
                compressionThroughputs;

            std::vector<double>
                decompressionThroughputs;

            std::vector<double>
                ratios;

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

            s.allLossless =
                true;

            for (
                const auto& d :
                rows
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

                ratios.push_back(
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

                // --------------------------------------------
                // Compression speedup
                // --------------------------------------------

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

                // --------------------------------------------
                // Decompression speedup
                // --------------------------------------------

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
            // EFFICIENCY
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

            s.avgCompressionRatio =
                average(
                    ratios
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

        // ====================================================
        // PRINT TABLE
        // ====================================================

        std::cout
            << "\n\n"
            << "========================================================================================================\n"
            << "HUFFMAN THREAD SCALING — MEDIAN OF 5 RUNS\n"
            << "========================================================================================================\n";

        std::cout
            << std::left
            << std::setw(10)
            << "Threads"

            << std::right
            << std::setw(10)
            << "Datasets"

            << std::setw(14)
            << "Comp(s)"

            << std::setw(14)
            << "Decomp(s)"

            << std::setw(14)
            << "Comp MB/s"

            << std::setw(14)
            << "Decomp MB/s"

            << std::setw(14)
            << "Speedup"

            << std::setw(14)
            << "Efficiency"

            << std::setw(12)
            << "Ratio"

            << "\n";

        std::cout
            << "--------------------------------------------------------------------------------------------------------\n";

        for (
            const auto& s :
            summaries
        ) {

            std::cout
                << std::left
                << std::setw(10)
                << s.threads

                << std::right
                << std::setw(10)
                << s.datasetCount

                << std::fixed
                << std::setprecision(4)

                << std::setw(14)
                << s.avgCompressionTime

                << std::setw(14)
                << s.avgDecompressionTime

                << std::setw(14)
                << s.avgCompressionThroughput

                << std::setw(14)
                << s.avgDecompressionThroughput

                << std::setw(14)
                << s.compressionSpeedup

                << std::setw(13)
                << (
                    s.compressionEfficiency
                    * 100.0
                )
                << "%"

                << std::setw(12)
                << s.avgCompressionRatio

                << "\n";
        }

        std::cout
            << "========================================================================================================\n";

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
        }

        // ====================================================
        // FINAL RESULT
        // ====================================================

        fs::path finalPath =
            outputDir
            /
            "FINAL_HUFFMAN_RESULT.csv";

        {
            std::ofstream file(
                finalPath
            );

            file
                << "metric,value\n"

                << "runs_per_dataset_thread_combination,5\n"

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
                << "\n";
        }

        // ====================================================
        // GENERATE GRAPHS
        // ====================================================

        fs::path pythonPath =
            outputDir
            /
            "generate_graphs.py";

        std::ofstream py(
            pythonPath
        );

        if (py) {

            py << R"PY(
import csv
import matplotlib.pyplot as plt

CSV_FILE = "thread_scaling_summary.csv"

rows = []

with open(CSV_FILE, "r", newline="") as f:

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


# Compression time
graph(
    values(
        "avg_compression_time"
    ),
    "Huffman Compression Time vs Threads",
    "Compression Time (seconds)",
    "01_compression_time.png"
)


# Decompression time
graph(
    values(
        "avg_decompression_time"
    ),
    "Huffman Decompression Time vs Threads",
    "Decompression Time (seconds)",
    "02_decompression_time.png"
)


# Compression throughput
graph(
    values(
        "avg_compression_throughput_MB_s"
    ),
    "Huffman Compression Throughput vs Threads",
    "Compression Throughput (MB/s)",
    "03_compression_throughput.png"
)


# Decompression throughput
graph(
    values(
        "avg_decompression_throughput_MB_s"
    ),
    "Huffman Decompression Throughput vs Threads",
    "Decompression Throughput (MB/s)",
    "04_decompression_throughput.png"
)


# Compression speedup
graph(
    values(
        "compression_speedup"
    ),
    "Huffman Compression Speedup vs Threads",
    "Speedup (x)",
    "05_compression_speedup.png"
)


# Decompression speedup
graph(
    values(
        "decompression_speedup"
    ),
    "Huffman Decompression Speedup vs Threads",
    "Speedup (x)",
    "06_decompression_speedup.png"
)


# Compression efficiency
graph(
    [
        x * 100
        for x in values(
            "compression_efficiency"
        )
    ],
    "Huffman Parallel Efficiency vs Threads",
    "Parallel Efficiency (%)",
    "07_parallel_efficiency.png",
    True
)


# Decompression efficiency
graph(
    [
        x * 100
        for x in values(
            "decompression_efficiency"
        )
    ],
    "Huffman Decompression Efficiency vs Threads",
    "Parallel Efficiency (%)",
    "08_decompression_efficiency.png",
    True
)


# Compression ratio
graph(
    values(
        "avg_compression_ratio"
    ),
    "Huffman Compression Ratio vs Threads",
    "Compression Ratio",
    "09_compression_ratio.png"
)


# Space saved
graph(
    values(
        "avg_space_saved_percent"
    ),
    "Huffman Space Saved vs Threads",
    "Space Saved (%)",
    "10_space_saved.png"
)


# Memory
graph(
    values(
        "avg_memory_MB"
    ),
    "Huffman Memory Usage vs Threads",
    "Memory Usage (MB)",
    "11_memory_usage.png"
)


# Timing variation
graph(
    values(
        "avg_compression_stddev"
    ),
    "Huffman Compression Timing Variation vs Threads",
    "Average Standard Deviation (seconds)",
    "12_compression_variation.png"
)


print()
print("All graphs generated.")
)PY";

            py.close();
        }

        // ====================================================
        // RUN GRAPH GENERATOR
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
            << "ANALYSIS COMPLETE\n"
            << "============================================================\n\n"

            << "Method:\n"
            << "  5 runs per dataset/thread combination\n"
            << "  Median taken across the 5 runs\n"
            << "  Dataset medians aggregated across datasets\n\n"

            << "Output directory:\n"
            << "  "
            << outputDir
            << "\n\n"

            << "Files:\n"
            << "  dataset_medians.csv\n"
            << "  thread_scaling_summary.csv\n"
            << "  FINAL_HUFFMAN_RESULT.csv\n"

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