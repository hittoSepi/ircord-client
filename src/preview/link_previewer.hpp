#pragma once
#include "db/local_store.hpp"
#include <curl/curl.h>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <queue>
#include <condition_variable>

namespace ircord {

struct PreviewResult {
    std::string url;
    std::string title;
    std::string description;
    bool        success = false;
};

using PreviewCallback = std::function<void(PreviewResult)>;

// Asynchronous link previewer. Fetches OG metadata via libcurl.
// Callbacks are invoked on the preview thread; callers must post_ui() themselves.
class LinkPreviewer {
public:
    explicit LinkPreviewer(db::LocalStore& store, int fetch_timeout_s = 5,
                           int max_cache = 200);
    ~LinkPreviewer();

    // Enqueue a URL for fetching. Callback is called when done (or on error).
    void fetch(const std::string& url, PreviewCallback callback);

    // Start the background fetch thread.
    void start();

    // Stop and join the background thread.
    void stop();

private:
    struct FetchJob {
        std::string     url;
        PreviewCallback callback;
    };

    void worker_loop();
    PreviewResult fetch_sync(const std::string& url);

    // OG tag extraction
    static std::string extract_og(const std::string& html, const std::string& property);

    // libcurl write callback
    static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata);

    db::LocalStore&    store_;
    int                fetch_timeout_s_;
    int                max_cache_;

    std::thread              thread_;
    std::mutex               mu_;
    std::condition_variable  cv_;
    std::queue<FetchJob>     queue_;
    bool                     stopping_ = false;

    static constexpr size_t kMaxBodyBytes = 512 * 1024;  // 512 KB
    static constexpr int    kMaxRedirects = 3;
};

} // namespace ircord
