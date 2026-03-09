#include "preview/link_previewer.hpp"
#include <spdlog/spdlog.h>
#include <regex>
#include <chrono>

namespace ircord {

LinkPreviewer::LinkPreviewer(db::LocalStore& store, int fetch_timeout_s, int max_cache)
    : store_(store)
    , fetch_timeout_s_(fetch_timeout_s)
    , max_cache_(max_cache)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

LinkPreviewer::~LinkPreviewer() {
    stop();
    curl_global_cleanup();
}

void LinkPreviewer::start() {
    stopping_ = false;
    thread_ = std::thread([this] { worker_loop(); });
}

void LinkPreviewer::stop() {
    {
        std::lock_guard lk(mu_);
        stopping_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void LinkPreviewer::fetch(const std::string& url, PreviewCallback callback) {
    // Check cache first
    auto cached = store_.get_cached_preview(url);
    if (cached) {
        PreviewResult r;
        r.url         = url;
        r.title       = cached->title;
        r.description = cached->description;
        r.success     = true;
        callback(std::move(r));
        return;
    }

    {
        std::lock_guard lk(mu_);
        queue_.push({ url, std::move(callback) });
    }
    cv_.notify_one();
}

void LinkPreviewer::worker_loop() {
    while (true) {
        FetchJob job;
        {
            std::unique_lock lk(mu_);
            cv_.wait(lk, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty()) break;
            job = std::move(queue_.front());
            queue_.pop();
        }

        auto result = fetch_sync(job.url);

        if (result.success) {
            store_.cache_preview(job.url, result.title, result.description);
        }

        job.callback(std::move(result));
    }
}

size_t LinkPreviewer::write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* body = static_cast<std::string*>(userdata);
    size_t bytes = size * nmemb;
    if (body->size() + bytes > kMaxBodyBytes) {
        bytes = kMaxBodyBytes - body->size();
    }
    body->append(ptr, bytes);
    return size * nmemb;  // tell curl we consumed it all
}

PreviewResult LinkPreviewer::fetch_sync(const std::string& url) {
    PreviewResult result;
    result.url = url;

    // Only allow http/https
    if (url.find("http://") != 0 && url.find("https://") != 0) {
        return result;
    }

    CURL* curl = curl_easy_init();
    if (!curl) return result;

    std::string body;
    body.reserve(64 * 1024);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, static_cast<long>(kMaxRedirects));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(fetch_timeout_s_));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ircord-client/0.1 (link preview bot)");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    // Stop once we have enough to find OG tags
    // (write_callback caps at kMaxBodyBytes)

    CURLcode rc = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        spdlog::debug("curl fetch failed for {}: {}", url, curl_easy_strerror(rc));
        return result;
    }

    result.title       = extract_og(body, "og:title");
    result.description = extract_og(body, "og:description");

    if (result.title.empty()) {
        // Fall back to <title>
        static const std::regex title_re("<title[^>]*>([^<]{1,200})</title>",
            std::regex::icase | std::regex::optimize);
        std::smatch m;
        if (std::regex_search(body, m, title_re)) {
            result.title = m[1].str();
        }
    }

    // Truncate
    if (result.title.size() > 100)       result.title.resize(100);
    if (result.description.size() > 200) result.description.resize(200);

    result.success = !result.title.empty();
    return result;
}

std::string LinkPreviewer::extract_og(const std::string& html, const std::string& property) {
    // <meta property="og:title" content="..." />
    // Also handles name= variant
    std::string pattern =
        R"(<meta[^>]+property\s*=\s*["'])" + property +
        R"(["'][^>]+content\s*=\s*["']([^"']{1,300})["'])";
    std::regex re(pattern, std::regex::icase | std::regex::optimize);
    std::smatch m;
    if (std::regex_search(html, m, re)) return m[1].str();

    // Try reversed attribute order: content= before property=
    std::string pattern2 =
        R"(<meta[^>]+content\s*=\s*["']([^"']{1,300})["'][^>]+property\s*=\s*["'])" +
        property + R"(["'])";
    std::regex re2(pattern2, std::regex::icase | std::regex::optimize);
    if (std::regex_search(html, m, re2)) return m[1].str();

    return {};
}

} // namespace ircord
