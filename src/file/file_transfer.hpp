#pragma once

#include "ircord.pb.h"
#include <string>
#include <vector>
#include <functional>
#include <filesystem>
#include <optional>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>

namespace ircord::client::file {

// Transfer direction
enum class TransferDirection {
    UPLOAD,
    DOWNLOAD
};

// Transfer state
enum class TransferState {
    PENDING,      // Waiting to start
    UPLOADING,    // Currently uploading
    DOWNLOADING,  // Currently downloading
    COMPLETED,    // Transfer finished
    FAILED,       // Transfer failed
    CANCELLED     // User cancelled
};

// File transfer info
struct TransferInfo {
    std::string transfer_id;      // Local transfer ID
    std::string file_id;          // Server file ID
    std::string filename;
    std::string mime_type;
    uint64_t file_size = 0;
    uint64_t bytes_transferred = 0;
    TransferDirection direction;
    TransferState state = TransferState::PENDING;
    float progress = 0.0f;
    std::string error_message;
    std::filesystem::path local_path;
    std::string recipient_id;     // For uploads
    std::string channel_id;       // For uploads
};

// Progress callback
using ProgressCallback = std::function<void(const TransferInfo&)>;
using CompletionCallback = std::function<void(const TransferInfo&, bool success)>;

// Chunk info
struct FileChunk {
    std::string file_id;
    uint32_t chunk_index = 0;
    std::vector<uint8_t> data;
    bool is_last = false;
};

/**
 * Manages file transfers (uploads and downloads).
 */
class FileTransferManager {
public:
    FileTransferManager();
    ~FileTransferManager();

    // Non-copyable
    FileTransferManager(const FileTransferManager&) = delete;
    FileTransferManager& operator=(const FileTransferManager&) = delete;

    /**
     * Upload a file.
     * @param local_path Path to local file
     * @param recipient_id Target user (empty for channel)
     * @param channel_id Target channel (empty for DM)
     * @param on_progress Progress callback
     * @param on_complete Completion callback
     * @return Transfer ID
     */
    std::string upload(
        const std::filesystem::path& local_path,
        const std::string& recipient_id = "",
        const std::string& channel_id = "",
        ProgressCallback on_progress = nullptr,
        CompletionCallback on_complete = nullptr);

    /**
     * Download a file.
     * @param file_id Server file ID
     * @param local_path Where to save the file
     * @param on_progress Progress callback
     * @param on_complete Completion callback
     * @return Transfer ID
     */
    std::string download(
        const std::string& file_id,
        const std::filesystem::path& local_path,
        ProgressCallback on_progress = nullptr,
        CompletionCallback on_complete = nullptr);

    /**
     * Cancel an active transfer.
     */
    void cancel(const std::string& transfer_id);

    /**
     * Get transfer info.
     */
    std::optional<TransferInfo> get_transfer(const std::string& transfer_id) const;

    /**
     * Get all active transfers.
     */
    std::vector<TransferInfo> get_active_transfers() const;

    /**
     * Clear completed/failed transfers from history.
     */
    void clear_completed();

    /**
     * Set the send function for sending messages to server.
     */
    using SendFunction = std::function<void(uint32_t msg_type, const google::protobuf::Message&)>;
    void set_send_function(SendFunction send_fn);

    /**
     * Handle incoming file chunk from server.
     * Called by network layer.
     */
    void on_file_chunk(const FileChunk& chunk);

    /**
     * Handle file progress update.
     */
    void on_file_progress(const FileProgress& progress);

    /**
     * Handle file completion.
     */
    void on_file_complete(const FileComplete& complete);

    /**
     * Handle file error.
     */
    void on_file_error(const FileError& error);

    /**
     * Maximum concurrent transfers.
     */
    static constexpr size_t MAX_CONCURRENT_UPLOADS = 3;
    static constexpr size_t MAX_CONCURRENT_DOWNLOADS = 5;
    static constexpr size_t CHUNK_SIZE = 64 * 1024;  // 64 KB

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, TransferInfo> transfers_;
    std::unordered_map<std::string, std::string> file_id_to_transfer_;  // Map server file_id to local transfer_id
    
    SendFunction send_fn_;
    std::atomic<uint64_t> next_transfer_id_{1};

    // Upload workers
    std::vector<std::thread> upload_workers_;
    std::queue<std::string> pending_uploads_;
    std::mutex upload_queue_mutex_;
    std::condition_variable upload_cv_;
    std::atomic<bool> shutdown_{false};

    void process_uploads();
    void do_upload(const std::string& transfer_id, TransferInfo& info);
    void send_file_request(const TransferInfo& info);
    void send_file_chunk(const std::string& transfer_id, uint32_t chunk_index, 
                         const std::vector<uint8_t>& data, bool is_last);
    
    std::string generate_transfer_id();
};

} // namespace ircord::client::file
