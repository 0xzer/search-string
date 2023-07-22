#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <chrono>
#include <iomanip>
#include <thread>
#include <future>
#include <queue>
#include <condition_variable>

using namespace std;
using namespace std::chrono;

const string GREEN_COLOR = "\033[32m";
const string RESET_COLOR = "\033[0m";
const string RED_COLOR = "\e[0;31m";

class ThreadPool {
public:
    ThreadPool(size_t threads) : stop(false) {
        for(size_t i = 0; i<threads; ++i)
            workers.emplace_back([this] {
                    for(;;) {
                        function<void()> task;

                        {
                            unique_lock<mutex> lock(this->queue_mutex);
                            this->condition.wait(lock, [this]{ return this->stop || !this->tasks.empty(); });
                            if(this->stop && this->tasks.empty())
                                return;
                            task = std::move(this->tasks.front());
                            this->tasks.pop();
                        }

                        task();
                    }
                }
            );
    }

    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) -> future<typename std::invoke_result_t<F, Args...>> {
        using return_type = typename std::invoke_result_t<F, Args...>;

        auto task = make_shared<packaged_task<return_type()>>(
            bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        future<return_type> res = task->get_future();
        {
            unique_lock<mutex> lock(queue_mutex);

            if(stop)
                throw runtime_error("enqueue on stopped ThreadPool");

            tasks.emplace([task](){ (*task)(); });
        }
        condition.notify_one();
        return res;
    }

    ~ThreadPool() {
        {
            unique_lock<mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for(thread &worker: workers)
            worker.join();
    }

private:
    vector<thread> workers;
    queue<function<void()>> tasks;

    mutex queue_mutex;
    condition_variable condition;
    bool stop;
};

vector<filesystem::path> listFiles(const filesystem::path& path) {
    vector<filesystem::path> fileList;

    if (!exists(path)) {
        cerr << "Path does not exist: " << path << '\n';
        return fileList;
    }

    for (const auto& entry : filesystem::recursive_directory_iterator(path)) {
        if (is_regular_file(entry)) {
            fileList.push_back(entry.path());
        }
    }

    return fileList;
}

bool readFile(const filesystem::path& filePath, const std::string& target, string& log) {
    std::ifstream inputFile(filePath);

    if (!inputFile) {
        std::cerr << "Failed to open the file: " << filePath << '\n';
        return false;
    }

    std::ostringstream oss;
    std::string line;
    int lineNumber = 1;
    bool foundMatch = false;

    while (std::getline(inputFile, line)) {
        if (line.find(target) != std::string::npos) {
            foundMatch = true;
            oss << filePath.filename().string() << ":" << lineNumber << " ";

            if (line.length() > 32) {
                oss << line.substr(0, 29) << "...";
            } else {
                oss << line;
            }

            oss << '\n';
        }

        lineNumber++;
    }

    log = oss.str();
    return foundMatch;
}

int main(int argc, char* args[]) {
    string path;
    string search;
    int matchCount = 0;
    string log;

    for (int i = 1; i < argc; ++i) {
        std::string arg = args[i];

        if (arg == "-p" && i + 1 < argc) {
            path = args[i + 1];
            ++i;
        } else if (arg == "-s" && i + 1 < argc) {
            search = args[i + 1];
            ++i;
        }
    }

    if (search.empty()) {
        std::cout << "[" + RED_COLOR + "-" + RESET_COLOR + "] Invalid Usage: search-string [-p] path/to/dir -s string_to_search_for\n";
        return 0;
    };

    if (path.empty()) {
        path = std::filesystem::current_path();
    }

    auto start = high_resolution_clock::now();

    auto files = listFiles(path);

    ThreadPool pool(thread::hardware_concurrency());
    vector<future<bool>> futures;
    for (const auto& file : files) {
        futures.emplace_back(pool.enqueue(readFile, file, cref(search), ref(log)));
    }

    for (auto &f : futures) {
        if (f.get()) matchCount++;
    }

    auto end = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(end - start);

    double durationSeconds = duration.count() / 1'000'000.0;

    std::ofstream outputFile("logs.txt");
    outputFile << log;

    std::cout << fixed << setprecision(6);
    std::cout << "[" + GREEN_COLOR + "+" + RESET_COLOR + "] Found ";
    std::cout << GREEN_COLOR << matchCount << RESET_COLOR + " matches in ";
    std::cout << GREEN_COLOR << files.size() << RESET_COLOR + " files (took ";
    std::cout << GREEN_COLOR << durationSeconds << RESET_COLOR + " seconds)\n";

    return 0;
}