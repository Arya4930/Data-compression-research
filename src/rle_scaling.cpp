#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ============================================================
// CONFIGURATION
// ============================================================

static const int MAX_THREADS = 22;

static const std::string INPUT_CSV =
    "../results/rle_all_runs.csv";

static const std::string OUTPUT_DIR =
    "../results/rle/scaling/";

// ============================================================
// RESULT FROM ONE BENCHMARK RUN
// ============================================================

struct Result
{
    std::string dataset;
    std::string algorithm;

    int threads = 0;

    long long original_bytes = 0;
    long long compressed_bytes = 0;

    double entropy = 0.0;
    double compression_ratio = 0.0;
    double space_saved = 0.0;

    double compression_time = 0.0;
    double decompression_time = 0.0;

    double compression_throughput = 0.0;
    double decompression_throughput = 0.0;

    double memory_mb = 0.0;

    bool lossless = false;
};

// ============================================================
// MEDIAN
// ============================================================

double median(std::vector<double> values)
{
    if (values.empty())
        return 0.0;

    std::sort(values.begin(), values.end());

    size_t n = values.size();

    if (n % 2 == 1)
        return values[n / 2];

    return (
        values[n / 2 - 1] +
        values[n / 2]
    ) / 2.0;
}

// ============================================================
// AVERAGE
// ============================================================

double average(
    const std::vector<double>& values
)
{
    if (values.empty())
        return 0.0;

    double sum = 0.0;

    for (double x : values)
        sum += x;

    return sum /
        static_cast<double>(values.size());
}

// ============================================================
// STANDARD DEVIATION
// ============================================================

double standardDeviation(
    const std::vector<double>& values
)
{
    if (values.size() <= 1)
        return 0.0;

    double mean =
        average(values);

    double sum = 0.0;

    for (double x : values)
    {
        double d = x - mean;
        sum += d * d;
    }

    return std::sqrt(
        sum /
        static_cast<double>(
            values.size() - 1
        )
    );
}

// ============================================================
// CSV SPLITTER
// ============================================================

std::vector<std::string> splitCSV(
    const std::string& line
)
{
    std::vector<std::string> fields;

    std::string current;
    bool inQuotes = false;

    for (char c : line)
    {
        if (c == '"')
        {
            inQuotes = !inQuotes;
        }
        else if (c == ',' && !inQuotes)
        {
            fields.push_back(current);
            current.clear();
        }
        else
        {
            current += c;
        }
    }

    fields.push_back(current);

    return fields;
}

// ============================================================
// CLEAN CSV FIELD
// ============================================================

std::string clean(
    const std::string& s
)
{
    if (
        s.size() >= 2 &&
        s.front() == '"' &&
        s.back() == '"'
    )
    {
        return s.substr(
            1,
            s.size() - 2
        );
    }

    return s;
}

// ============================================================
// CONVERSION HELPERS
// ============================================================

double toDouble(
    const std::string& s
)
{
    try
    {
        return std::stod(clean(s));
    }
    catch (...)
    {
        return 0.0;
    }
}

long long toLongLong(
    const std::string& s
)
{
    try
    {
        return std::stoll(clean(s));
    }
    catch (...)
    {
        return 0;
    }
}

int toInt(
    const std::string& s
)
{
    try
    {
        return std::stoi(clean(s));
    }
    catch (...)
    {
        return 0;
    }
}

bool toBool(
    const std::string& s
)
{
    std::string x = clean(s);

    std::transform(
        x.begin(),
        x.end(),
        x.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(
                std::tolower(c)
            );
        }
    );

    return
        x == "true" ||
        x == "1" ||
        x == "pass";
}

// ============================================================
// FIND COLUMN
// ============================================================

int findColumn(
    const std::vector<std::string>& headers,
    const std::string& name
)
{
    for (size_t i = 0; i < headers.size(); ++i)
    {
        if (clean(headers[i]) == name)
            return static_cast<int>(i);
    }

    return -1;
}

// ============================================================
// LOAD CSV
// ============================================================

std::vector<Result> loadCSV(
    const std::string& filename
)
{
    std::ifstream file(filename);

    if (!file)
    {
        throw std::runtime_error(
            "Cannot open CSV: " + filename
        );
    }

    std::string line;

    if (!std::getline(file, line))
    {
        throw std::runtime_error(
            "CSV is empty."
        );
    }

    std::vector<std::string> headers =
        splitCSV(line);

    int c_dataset =
        findColumn(headers, "dataset");

    int c_algorithm =
        findColumn(headers, "algorithm");

    int c_threads =
        findColumn(headers, "threads");

    int c_original =
        findColumn(headers, "original_bytes");

    int c_compressed =
        findColumn(headers, "compressed_bytes");

    int c_entropy =
        findColumn(headers, "entropy");

    int c_ratio =
        findColumn(headers, "compression_ratio");

    int c_saved =
        findColumn(
            headers,
            "space_saved_percent"
        );

    int c_comp_time =
        findColumn(
            headers,
            "compression_time"
        );

    int c_decomp_time =
        findColumn(
            headers,
            "decompression_time"
        );

    int c_comp_speed =
        findColumn(
            headers,
            "compression_throughput_MB_s"
        );

    int c_decomp_speed =
        findColumn(
            headers,
            "decompression_throughput_MB_s"
        );

    int c_memory =
        findColumn(
            headers,
            "memory_MB"
        );

    int c_lossless =
        findColumn(
            headers,
            "lossless"
        );

    // --------------------------------------------------------
    // Compatibility with alternative column names
    // --------------------------------------------------------

    if (c_comp_time < 0)
    {
        c_comp_time =
            findColumn(
                headers,
                "compression_time_s"
            );
    }

    if (c_decomp_time < 0)
    {
        c_decomp_time =
            findColumn(
                headers,
                "decompression_time_s"
            );
    }

    if (c_comp_speed < 0)
    {
        c_comp_speed =
            findColumn(
                headers,
                "compression_throughput"
            );
    }

    if (c_decomp_speed < 0)
    {
        c_decomp_speed =
            findColumn(
                headers,
                "decompression_throughput"
            );
    }

    if (c_memory < 0)
    {
        c_memory =
            findColumn(
                headers,
                "memory"
            );
    }

    std::vector<Result> results;

    while (std::getline(file, line))
    {
        if (line.empty())
            continue;

        auto f = splitCSV(line);

        if (
            c_dataset < 0 ||
            c_threads < 0 ||
            c_comp_time < 0 ||
            c_decomp_time < 0
        )
        {
            continue;
        }

        int required =
            std::max({
                c_dataset,
                c_threads,
                c_comp_time,
                c_decomp_time
            });

        if (
            static_cast<int>(f.size())
            <= required
        )
        {
            continue;
        }

        Result r;

        r.dataset =
            clean(f[c_dataset]);

        r.algorithm =
            c_algorithm >= 0
                ? clean(f[c_algorithm])
                : "RLE";

        r.threads =
            toInt(f[c_threads]);

        if (c_original >= 0)
            r.original_bytes =
                toLongLong(
                    f[c_original]
                );

        if (c_compressed >= 0)
            r.compressed_bytes =
                toLongLong(
                    f[c_compressed]
                );

        if (c_entropy >= 0)
            r.entropy =
                toDouble(
                    f[c_entropy]
                );

        if (c_ratio >= 0)
            r.compression_ratio =
                toDouble(
                    f[c_ratio]
                );

        if (c_saved >= 0)
            r.space_saved =
                toDouble(
                    f[c_saved]
                );

        r.compression_time =
            toDouble(
                f[c_comp_time]
            );

        r.decompression_time =
            toDouble(
                f[c_decomp_time]
            );

        if (c_comp_speed >= 0)
            r.compression_throughput =
                toDouble(
                    f[c_comp_speed]
                );

        if (c_decomp_speed >= 0)
            r.decompression_throughput =
                toDouble(
                    f[c_decomp_speed]
                );

        if (c_memory >= 0)
            r.memory_mb =
                toDouble(
                    f[c_memory]
                );

        if (c_lossless >= 0)
            r.lossless =
                toBool(
                    f[c_lossless]
                );

        results.push_back(r);
    }

    return results;
}

// ============================================================
// DATASET MEDIAN
// ============================================================

struct DatasetMedian
{
    std::string dataset;

    int threads = 0;

    long long original_bytes = 0;
    long long compressed_bytes = 0;

    double entropy = 0.0;
    double compression_ratio = 0.0;
    double space_saved = 0.0;

    double compression_time = 0.0;
    double decompression_time = 0.0;

    double compression_throughput = 0.0;
    double decompression_throughput = 0.0;

    double memory_mb = 0.0;

    double compression_stddev = 0.0;
    double decompression_stddev = 0.0;

    bool lossless = false;
};

// ============================================================
// THREAD SUMMARY
// ============================================================

struct ThreadSummary
{
    int threads = 0;

    int datasets = 0;

    double compression_time = 0.0;
    double decompression_time = 0.0;

    double compression_throughput = 0.0;
    double decompression_throughput = 0.0;

    double compression_speedup = 0.0;
    double decompression_speedup = 0.0;

    double compression_efficiency = 0.0;
    double decompression_efficiency = 0.0;

    double compression_ratio = 0.0;
    double space_saved = 0.0;

    double memory_mb = 0.0;

    double compression_stddev = 0.0;
    double decompression_stddev = 0.0;
};

// ============================================================
// CALCULATE DATASET MEDIANS
// ============================================================

std::vector<DatasetMedian> calculateDatasetMedians(
    const std::vector<Result>& results
)
{
    using Key =
        std::pair<std::string, int>;

    std::map<
        Key,
        std::vector<Result>
    > groups;

    for (const auto& r : results)
    {
        groups[
            {
                r.dataset,
                r.threads
            }
        ].push_back(r);
    }

    std::vector<DatasetMedian> output;

    for (const auto& entry : groups)
    {
        const auto& runs =
            entry.second;

        DatasetMedian d;

        d.dataset =
            entry.first.first;

        d.threads =
            entry.first.second;

        d.original_bytes =
            runs[0].original_bytes;

        d.compressed_bytes =
            runs[0].compressed_bytes;

        std::vector<double> ct;
        std::vector<double> dt;
        std::vector<double> ctp;
        std::vector<double> dtp;
        std::vector<double> ratios;
        std::vector<double> saved;
        std::vector<double> memory;
        std::vector<double> entropy;

        for (const auto& r : runs)
        {
            ct.push_back(
                r.compression_time
            );

            dt.push_back(
                r.decompression_time
            );

            ctp.push_back(
                r.compression_throughput
            );

            dtp.push_back(
                r.decompression_throughput
            );

            ratios.push_back(
                r.compression_ratio
            );

            saved.push_back(
                r.space_saved
            );

            memory.push_back(
                r.memory_mb
            );

            entropy.push_back(
                r.entropy
            );

            if (!r.lossless)
                d.lossless = false;
            else if (runs.size() == 1)
                d.lossless = true;
        }

        // Correct lossless aggregation
        d.lossless = true;

        for (const auto& r : runs)
        {
            if (!r.lossless)
            {
                d.lossless = false;
                break;
            }
        }

        d.entropy =
            median(entropy);

        d.compression_ratio =
            median(ratios);

        d.space_saved =
            median(saved);

        d.compression_time =
            median(ct);

        d.decompression_time =
            median(dt);

        d.compression_throughput =
            median(ctp);

        d.decompression_throughput =
            median(dtp);

        d.memory_mb =
            median(memory);

        d.compression_stddev =
            standardDeviation(ct);

        d.decompression_stddev =
            standardDeviation(dt);

        output.push_back(d);
    }

    return output;
}

// ============================================================
// THREAD AGGREGATION
// ============================================================

std::vector<ThreadSummary> aggregateThreads(
    const std::vector<DatasetMedian>& medians
)
{
    std::map<
        int,
        std::vector<DatasetMedian>
    > groups;

    for (const auto& d : medians)
    {
        groups[d.threads].push_back(d);
    }

    std::vector<ThreadSummary> summaries;

    for (const auto& entry : groups)
    {
        ThreadSummary s;

        s.threads =
            entry.first;

        const auto& datasets =
            entry.second;

        s.datasets =
            static_cast<int>(
                datasets.size()
            );

        std::vector<double> ct;
        std::vector<double> dt;
        std::vector<double> ctp;
        std::vector<double> dtp;
        std::vector<double> ratio;
        std::vector<double> saved;
        std::vector<double> memory;
        std::vector<double> cstd;
        std::vector<double> dstd;

        for (const auto& d : datasets)
        {
            ct.push_back(
                d.compression_time
            );

            dt.push_back(
                d.decompression_time
            );

            ctp.push_back(
                d.compression_throughput
            );

            dtp.push_back(
                d.decompression_throughput
            );

            ratio.push_back(
                d.compression_ratio
            );

            saved.push_back(
                d.space_saved
            );

            memory.push_back(
                d.memory_mb
            );

            cstd.push_back(
                d.compression_stddev
            );

            dstd.push_back(
                d.decompression_stddev
            );
        }

        s.compression_time =
            average(ct);

        s.decompression_time =
            average(dt);

        s.compression_throughput =
            average(ctp);

        s.decompression_throughput =
            average(dtp);

        s.compression_ratio =
            average(ratio);

        s.space_saved =
            average(saved);

        s.memory_mb =
            average(memory);

        s.compression_stddev =
            average(cstd);

        s.decompression_stddev =
            average(dstd);

        summaries.push_back(s);
    }

    return summaries;
}

// ============================================================
// WRITE DATASET MEDIANS
// ============================================================

void writeDatasetMedians(
    const fs::path& path,
    const std::vector<DatasetMedian>& data
)
{
    std::ofstream file(path);

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

    for (const auto& d : data)
    {
        file
            << d.dataset << ","
            << d.threads << ","
            << d.original_bytes << ","
            << d.compressed_bytes << ","
            << std::setprecision(12)
            << d.entropy << ","
            << d.compression_ratio << ","
            << d.space_saved << ","
            << d.compression_time << ","
            << d.decompression_time << ","
            << d.compression_throughput << ","
            << d.decompression_throughput << ","
            << d.memory_mb << ","
            << d.compression_stddev << ","
            << d.decompression_stddev << ","
            << (d.lossless ? "true" : "false")
            << "\n";
    }
}

// ============================================================
// WRITE THREAD SUMMARY
// ============================================================

void writeThreadSummary(
    const fs::path& path,
    const std::vector<ThreadSummary>& summaries
)
{
    std::ofstream file(path);

    file
        << "threads,"
        << "datasets,"
        << "avg_compression_time,"
        << "avg_decompression_time,"
        << "avg_compression_throughput_MB_s,"
        << "avg_decompression_throughput_MB_s,"
        << "compression_speedup,"
        << "decompression_speedup,"
        << "compression_efficiency,"
        << "decompression_efficiency,"
        << "avg_compression_ratio,"
        << "avg_space_saved_percent,"
        << "avg_memory_MB,"
        << "avg_compression_stddev,"
        << "avg_decompression_stddev\n";

    for (const auto& s : summaries)
    {
        file
            << s.threads << ","
            << s.datasets << ","
            << std::setprecision(12)
            << s.compression_time << ","
            << s.decompression_time << ","
            << s.compression_throughput << ","
            << s.decompression_throughput << ","
            << s.compression_speedup << ","
            << s.decompression_speedup << ","
            << s.compression_efficiency << ","
            << s.decompression_efficiency << ","
            << s.compression_ratio << ","
            << s.space_saved << ","
            << s.memory_mb << ","
            << s.compression_stddev << ","
            << s.decompression_stddev
            << "\n";
    }
}

// ============================================================
// GENERATE PYTHON GRAPH SCRIPT
// ============================================================

void generateGraphScript(
    const fs::path& outputDir
)
{
    fs::path pythonPath =
        outputDir /
        "generate_graphs.py";

    std::ofstream py(pythonPath);

    if (!py)
    {
        std::cerr
            << "WARNING: Cannot create graph script.\n";

        return;
    }

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
        for x, value in zip(threads, y):
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
    "RLE Compression Time vs Threads",
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
    "RLE Decompression Time vs Threads",
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
    "RLE Compression Throughput vs Threads",
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
    "RLE Decompression Throughput vs Threads",
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
    "RLE Compression Speedup vs Threads",
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
    "RLE Decompression Speedup vs Threads",
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
    "RLE Parallel Compression Efficiency vs Threads",
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
    "RLE Parallel Decompression Efficiency vs Threads",
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
    "RLE Compression Ratio vs Threads",
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
    "RLE Space Saved vs Threads",
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
    "RLE Memory Usage vs Threads",
    "Memory Usage (MB)",
    "11_memory_usage.png"
)


# ============================================================
# 12 COMPRESSION TIMING VARIATION
# ============================================================

graph(
    values(
        "avg_compression_stddev"
    ),
    "RLE Compression Timing Variation vs Threads",
    "Average Standard Deviation (seconds)",
    "12_compression_variation.png"
)


print()
print("All RLE graphs generated.")
)PY";

    py.close();
}

// ============================================================
// RUN GRAPH GENERATOR
// ============================================================

bool runGraphGenerator(
    const fs::path& outputDir
)
{
#ifdef _WIN32

    std::string command =
        "cd /d \"" +
        outputDir.string() +
        "\" && python generate_graphs.py";

#else

    std::string command =
        "cd \"" +
        outputDir.string() +
        "\" && python3 generate_graphs.py";

#endif

    int result =
        std::system(
            command.c_str()
        );

    return result == 0;
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
            << "RLE THREAD SCALING ANALYZER\n"
            << "============================================================\n\n";

        std::cout
            << "Reading existing results only.\n"
            << "No compression will be performed.\n"
            << "No decompression will be performed.\n"
            << "No benchmark will be executed.\n\n";

        std::cout
            << "CSV:\n  "
            << fs::absolute(INPUT_CSV).string()
            << "\n\n";

        // ----------------------------------------------------
        // LOAD
        // ----------------------------------------------------

        std::vector<Result> results =
            loadCSV(INPUT_CSV);

        if (results.empty())
        {
            std::cerr
                << "ERROR: No valid RLE results found.\n";

            return 1;
        }

        std::cout
            << "Total result rows: "
            << results.size()
            << "\n\n";

        // ----------------------------------------------------
        // THREAD COUNTS
        // ----------------------------------------------------

        std::map<int, int> threadCounts;

        for (const auto& r : results)
            threadCounts[r.threads]++;

        std::cout
            << "Thread counts found:\n";

        for (const auto& [threads, count] :
             threadCounts)
        {
            std::cout
                << "  "
                << threads
                << " threads : "
                << count
                << " rows\n";
        }

        // ----------------------------------------------------
        // DATASET MEDIANS
        // ----------------------------------------------------

        std::vector<DatasetMedian> medians =
            calculateDatasetMedians(
                results
            );

        // ----------------------------------------------------
        // THREAD SUMMARIES
        // ----------------------------------------------------

        std::vector<ThreadSummary> summaries =
            aggregateThreads(
                medians
            );

        // ----------------------------------------------------
        // BASELINES
        //
        // Speedup is based on the 1-thread result.
        // ----------------------------------------------------

        double baselineCompression = 0.0;
        double baselineDecompression = 0.0;

        for (const auto& s : summaries)
        {
            if (s.threads == 1)
            {
                baselineCompression =
                    s.compression_time;

                baselineDecompression =
                    s.decompression_time;

                break;
            }
        }

        if (
            baselineCompression <= 0.0 ||
            baselineDecompression <= 0.0
        )
        {
            std::cerr
                << "\nERROR: 1-thread baseline missing.\n";

            return 1;
        }

        // ----------------------------------------------------
        // SPEEDUP + EFFICIENCY
        // ----------------------------------------------------

        for (auto& s : summaries)
        {
            s.compression_speedup =
                baselineCompression /
                s.compression_time;

            s.decompression_speedup =
                baselineDecompression /
                s.decompression_time;

            s.compression_efficiency =
                s.compression_speedup /
                static_cast<double>(
                    s.threads
                );

            s.decompression_efficiency =
                s.decompression_speedup /
                static_cast<double>(
                    s.threads
                );
        }

        // ----------------------------------------------------
        // OUTPUT DIRECTORY
        // ----------------------------------------------------

        fs::path outputDir =
            OUTPUT_DIR;

        fs::create_directories(
            outputDir
        );

        fs::path medianFile =
            outputDir /
            "dataset_medians.csv";

        fs::path summaryFile =
            outputDir /
            "thread_scaling_summary.csv";

        fs::path finalFile =
            outputDir /
            "FINAL_RLE_RESULT.csv";

        fs::path allRunsFile =
            outputDir /
            "all_existing_runs.csv";

        // ----------------------------------------------------
        // COPY ORIGINAL RESULTS
        // ----------------------------------------------------

        {
            std::ifstream src(
                INPUT_CSV
            );

            std::ofstream dst(
                allRunsFile
            );

            if (src && dst)
                dst << src.rdbuf();
        }

        // ----------------------------------------------------
        // WRITE MEDIANS
        // ----------------------------------------------------

        writeDatasetMedians(
            medianFile,
            medians
        );

        // ----------------------------------------------------
        // WRITE SUMMARY
        // ----------------------------------------------------

        writeThreadSummary(
            summaryFile,
            summaries
        );

        // ----------------------------------------------------
        // CONSOLE TABLE
        // ----------------------------------------------------

        std::cout
            << "\n\n"
            << "====================================================================================================\n"
            << "RLE THREAD SCALING ANALYSIS\n"
            << "====================================================================================================\n";

        std::cout
            << std::left
            << std::setw(10)
            << "Threads"
            << std::setw(12)
            << "Datasets"
            << std::setw(15)
            << "Comp(s)"
            << std::setw(15)
            << "Decomp(s)"
            << std::setw(17)
            << "Comp MB/s"
            << std::setw(17)
            << "Decomp MB/s"
            << std::setw(16)
            << "Comp Speedup"
            << std::setw(16)
            << "Comp Eff."
            << std::setw(14)
            << "Ratio"
            << "\n";

        std::cout
            << "----------------------------------------------------------------------------------------------------\n";

        for (const auto& s : summaries)
        {
            std::cout
                << std::left
                << std::setw(10)
                << s.threads
                << std::setw(12)
                << s.datasets
                << std::fixed
                << std::setprecision(4)
                << std::setw(15)
                << s.compression_time
                << std::setw(15)
                << s.decompression_time
                << std::setw(17)
                << s.compression_throughput
                << std::setw(17)
                << s.decompression_throughput
                << std::setw(16)
                << s.compression_speedup
                << std::setw(16)
                << s.compression_efficiency * 100.0
                << std::setw(14)
                << s.compression_ratio
                << "\n";
        }

        std::cout
            << "====================================================================================================\n";

        // ----------------------------------------------------
        // BEST RESULTS
        // ----------------------------------------------------

        int bestCompThreads = -1;
        int bestDecompThreads = -1;
        int maxSpeedupThreads = -1;

        double bestCompTime =
            std::numeric_limits<double>::max();

        double bestDecompTime =
            std::numeric_limits<double>::max();

        double maxSpeedup = 0.0;

        for (const auto& s : summaries)
        {
            if (
                s.compression_time <
                bestCompTime
            )
            {
                bestCompTime =
                    s.compression_time;

                bestCompThreads =
                    s.threads;
            }

            if (
                s.decompression_time <
                bestDecompTime
            )
            {
                bestDecompTime =
                    s.decompression_time;

                bestDecompThreads =
                    s.threads;
            }

            if (
                s.compression_speedup >
                maxSpeedup
            )
            {
                maxSpeedup =
                    s.compression_speedup;

                maxSpeedupThreads =
                    s.threads;
            }
        }

        // ----------------------------------------------------
        // FINAL RESULT
        // ----------------------------------------------------

        {
            std::ofstream finalOut(
                finalFile
            );

            finalOut
                << "metric,value\n"

                << "algorithm,RLE\n"

                << "total_raw_result_rows,"
                << results.size()
                << "\n"

                << "total_dataset_medians,"
                << medians.size()
                << "\n"

                << "thread_counts,"
                << summaries.size()
                << "\n"

                << "best_compression_threads,"
                << bestCompThreads
                << "\n"

                << "best_decompression_threads,"
                << bestDecompThreads
                << "\n"

                << "maximum_compression_speedup,"
                << std::setprecision(12)
                << maxSpeedup
                << "\n"

                << "maximum_speedup_threads,"
                << maxSpeedupThreads
                << "\n"

                << "baseline_compression_time_s,"
                << baselineCompression
                << "\n"

                << "baseline_decompression_time_s,"
                << baselineDecompression
                << "\n"

                << "best_compression_time_s,"
                << bestCompTime
                << "\n"

                << "best_decompression_time_s,"
                << bestDecompTime
                << "\n";
        }

        // ----------------------------------------------------
        // GENERATE GRAPH SCRIPT
        // ----------------------------------------------------

        std::cout
            << "\nGenerating graph script...\n";

        generateGraphScript(
            outputDir
        );

        // ----------------------------------------------------
        // RUN PYTHON
        // ----------------------------------------------------

        std::cout
            << "Generating graphs...\n\n";

        bool graphSuccess =
            runGraphGenerator(
                outputDir
            );

        if (!graphSuccess)
        {
            std::cout
                << "\nWARNING: Could not generate graphs.\n"
                << "Install matplotlib with:\n"
                << "  pip install matplotlib\n";
        }

        // ----------------------------------------------------
        // FINAL OUTPUT
        // ----------------------------------------------------

        std::cout
            << "\n"
            << "============================================================\n"
            << "ANALYSIS COMPLETE\n"
            << "============================================================\n\n"

            << "No datasets were recompressed.\n"
            << "No datasets were redecompressed.\n"
            << "No benchmark results were overwritten.\n\n"

            << "Output directory:\n"
            << "  "
            << fs::absolute(outputDir).string()
            << "\n\n"

            << "CSV files:\n"
            << "  dataset_medians.csv\n"
            << "  thread_scaling_summary.csv\n"
            << "  FINAL_RLE_RESULT.csv\n"
            << "  all_existing_runs.csv\n\n"

            << "Graphs:\n"
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
    catch (const std::exception& e)
    {
        std::cerr
            << "\nERROR: "
            << e.what()
            << "\n";

        return 1;
    }
}