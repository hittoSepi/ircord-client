#include "file/file_transfer.hpp"
#include <spdlog/spdlog.h>
#include <fstream>
#include <uuid.h>

namespace ircord::client::file {

FileTransferManager::FileTransferManager() {
    // Start upload worker threads
    for (size_t i = 0; i < MAX_CONCURRENT_UPLOADS; ++i) {
        upload_workers_.emplace_back(&FileTransferManager::process_uploads, this);
    }
    spdlog::info("FileTransferManager initialized with {} upload workers", MAX_CONCURRENT_UPLOADS);
}

FileTransferManager::~FileTransferManager() {
    shutdown_ = true;
    upload_cv_.notify_all();
    
    for (auto& worker : upload_workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    
    spdlog::info("FileTransferManager shutdown");
}

std::string FileTransferManager::upload(
    const std::filesystem::path& local_path,
    const std::string& recipient_id,
    const std::string& channel_id,
    ProgressCallback on_progress,
    CompletionCallback on_complete) {
    
    // Validate file exists
    if (!std::filesystem::exists(local_path)) {
        spdlog::error("Upload failed: file not found: {}", local_path.string());
        return "";
    }
    
    // Get file info
    auto file_size = std::filesystem::file_size(local_path);
    auto filename = local_path.filename().string();
    
    // Determine MIME type (simplified)
    std::string mime_type = "application/octet-stream";
    auto ext = local_path.extension().string();
    if (ext == ".txt") mime_type = "text/plain";
    else if (ext == ".jpg" || ext == ".jpeg") mime_type = "image/jpeg";
    else if (ext == ".png") mime_type = "image/png";
    else if (ext == ".gif") mime_type = "image/gif";
    else if (ext == ".pdf") mime_type = "application/pdf";
    
    // Create transfer record
    TransferInfo info;
    info.transfer_id = generate_transfer_id();
    info.file_id = uuids::to_string(uuids::uuid_random_generator{}());  // Generate server file ID
    info.filename = filename;
    info.mime_type = mime_type;
    info.file_size = file_size;
    info.direction = TransferDirection::UPLOAD;
    info.state = TransferState::PENDING;
    info.local_path = local_path;
    info.recipient_id = recipient_id;
    info.channel_id = channel_id;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        transfers_[info.transfer_id] = info;
        file_id_to_transfer_[info.file_id] = info.transfer_id;
    }
    
    // Queue for upload
    {
        std::lock_guard<std::mutex> lock(upload_queue_mutex_);
        pending_uploads_.push(info.transfer_id);
    }
    upload_cv_.notify_one();
    
    spdlog::info("File upload queued: {} ({} bytes)", filename, file_size);
    return info.transfer_id;
}

std::string FileTransferManager::download(
    const std::string& file_id,
    const std::filesystem::path& local_path,
    ProgressCallback on_progress,
    CompletionCallback on_complete) {
    
    // Create transfer record
    TransferInfo info;
    info.transfer_id = generate_transfer_id();
    info.file_id = file_id;
    info.filename = local_path.filename().string();
    info.direction = TransferDirection::DOWNLOAD;
    info.state = TransferState::PENDING;
    info.local_path = local_path;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        transfers_[info.transfer_id] = info;
        file_id_to_transfer_[file_id] = info.transfer_id;
    }
    
    // Send download request
    FileDownloadRequest request;
    request.set_file_id(file_id);
    request.set_chunk_index(0);
    
    if (send_fn_) {
        send_fn_(MT_FILE_DOWNLOAD, request);
        info.state = TransferState::DOWNLOADING;
    } else {
        spdlog::error("Cannot download: send function not set");
        info.state = TransferState::FAILED;
        info.error_message = "Network not available";
    }
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        transfers_[info.transfer_id] = info;
    }
    
    spdlog::info("File download started: {}", file_id);
    return info.transfer_id;
}

void FileTransferManager::cancel(const std::string& transfer_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = transfers_.find(transfer_id);
    if (it != transfers_.end()) {
        it->second.state = TransferState::CANCELLED;
        spdlog::info("Transfer cancelled: {}", transfer_id);
    }
}

std::optional<TransferInfo> FileTransferManager::get_transfer(const std::string& transfer_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = transfers_.find(transfer_id);
    if (it != transfers_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<TransferInfo> FileTransferManager::get_active_transfers() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TransferInfo> result;
    for (const auto& [id, info] : transfers_) {
        if (info.state == TransferState::UPLOADING || 
            info.state == TransferState::DOWNLOADING ||
            info.state == TransferState::PENDING) {
            result.push_back(info);
        }
    }
    return result;
}

void FileTransferManager::clear_completed() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = transfers_.begin(); it != transfers_.end();) {
        const auto& info = it->second;
        if (info.state == TransferState::COMPLETED ||
            info.state == TransferState::FAILED ||
            info.state == TransferState::CANCELLED) {
            file_id_to_transfer_.erase(info.file_id);
            it = transfers_.erase(it);
        } else {
            ++it;
        }
    }
}

void FileTransferManager::set_send_function(SendFunction send_fn) {
    send_fn_ = send_fn;
}

void FileTransferManager::on_file_chunk(const FileChunk& chunk) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = file_id_to_transfer_.find(chunk.file_id);
    if (it == file_id_to_transfer_.end()) return;
    
    auto transfer_it = transfers_.find(it->second);
    if (transfer_it == transfers_.end()) return;
    
    auto& info = transfer_it->second;
    if (info.state == TransferState::CANCELLED) return;
    
    // Write chunk to file
    std::ofstream file(info.local_path, std::ios::binary | std::ios::app);
    if (!file) {
        spdlog::error("Failed to open file for writing: {}", info.local_path.string());
        info.state = TransferState::FAILED;
        info.error_message = "Failed to write file";
        return;
    }
    
    file.write(reinterpret_cast<const char*>(chunk.data.data()), chunk.data.size());
    file.close();
    
    info.bytes_transferred += chunk.data.size();
    info.progress = static_cast<float>(info.bytes_transferred) / info.file_size;
    
    if (chunk.is_last) {
        info.state = TransferState::COMPLETED;
        info.progress = 1.0f;
        spdlog::info("File download completed: {} -> {}", chunk.file_id, info.local_path.string());
    }
}

void FileTransferManager::on_file_progress(const FileProgress& progress) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = file_id_to_transfer_.find(progress.file_id());
    if (it == file_id_to_transfer_.end()) return;
    
    auto transfer_it = transfers_.find(it->second);
    if (transfer_it == transfers_.end()) return;
    
    auto& info = transfer_it->second;
    info.bytes_transferred = progress.bytes_transferred();
    info.progress = progress.percent_complete() / 100.0f;
    
    if (progress.status() == "uploading") {
        info.state = TransferState::UPLOADING;
    } else if (progress.status() == "downloading") {
        info.state = TransferState::DOWNLOADING;
    }
}

void FileTransferManager::on_file_complete(const FileComplete& complete) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = file_id_to_transfer_.find(complete.file_id());
    if (it == file_id_to_transfer_.end()) return;
    
    auto transfer_it = transfers_.find(it->second);
    if (transfer_it == transfers_.end()) return;
    
    auto& info = transfer_it->second;
    info.state = TransferState::COMPLETED;
    info.bytes_transferred = complete.total_bytes();
    info.progress = 1.0f;
    
    spdlog::info("File transfer completed: {}", complete.file_id());
}

void FileTransferManager::on_file_error(const FileError& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = file_id_to_transfer_.find(error.file_id());
    if (it == file_id_to_transfer_.end()) return;
    
    auto transfer_it = transfers_.find(it->second);
    if (transfer_it == transfers_.end()) return;
    
    auto& info = transfer_it->second;
    info.state = TransferState::FAILED;
    info.error_message = error.error_message();
    
    spdlog::error("File transfer error: {} - {}", error.file_id(), error.error_message());
}

void FileTransferManager::process_uploads() {
    while (!shutdown_) {
        std::string transfer_id;
        
        {
            std::unique_lock<std::mutex> lock(upload_queue_mutex_);
            upload_cv_.wait(lock, [this] { return !pending_uploads_.empty() || shutdown_; });
            
            if (shutdown_) break;
            
            transfer_id = pending_uploads_.front();
            pending_uploads_.pop();
        }
        
        // Get transfer info
        TransferInfo info;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = transfers_.find(transfer_id);
            if (it == transfers_.end()) continue;
            info = it->second;
        }
        
        if (info.state == TransferState::CANCELLED) continue;
        
        do_upload(transfer_id, info);
    }
}

void FileTransferManager::do_upload(const std::string& transfer_id, TransferInfo& info) {
    spdlog::info("Starting upload: {} ({} bytes)", info.filename, info.file_size);
    
    // Send upload request
    send_file_request(info);
    
    // Read and send file in chunks
    std::ifstream file(info.local_path, std::ios::binary);
    if (!file) {
        spdlog::error("Failed to open file for upload: {}", info.local_path.string());
        info.state = TransferState::FAILED;
        info.error_message = "Failed to read file";
        return;
    }
    
    std::vector<uint8_t> buffer(CHUNK_SIZE);
    uint32_t chunk_index = 0;
    uint64_t total_sent = 0;
    
    while (file.good() && !shutdown_) {
        // Check if cancelled
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = transfers_.find(transfer_id);
            if (it != transfers_.end() && it->second.state == TransferState::CANCELLED) {
                spdlog::info("Upload cancelled: {}", transfer_id);
                return;
            }
        }
        
        file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        size_t bytes_read = file.gcount();
        
        if (bytes_read == 0) break;
        
        bool is_last = (bytes_read < CHUNK_SIZE) || (total_sent + bytes_read >= info.file_size);
        
        send_file_chunk(transfer_id, chunk_index++,
            std::vector<uint8_t>(buffer.begin(), buffer.begin() + bytes_read),
            is_last);
        
        total_sent += bytes_read;
        info.bytes_transferred = total_sent;
        info.progress = static_cast<float>(total_sent) / info.file_size;
        
        // Small delay to avoid overwhelming the network
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    file.close();
    spdlog::info("Upload chunks sent: {} ({} bytes)", info.filename, total_sent);
}

void FileTransferManager::send_file_request(const TransferInfo& info) {
    if (!send_fn_) return;
    
    FileUploadRequest request;
    request.set_file_id(info.file_id);
    request.set_filename(info.filename);
    request.set_file_size(info.file_size);
    request.set_mime_type(info.mime_type);
    request.set_chunk_size(CHUNK_SIZE);
    
    if (!info.recipient_id.empty()) {
        request.set_recipient_id(info.recipient_id);
    }
    if (!info.channel_id.empty()) {
        request.set_channel_id(info.channel_id);
    }
    
    send_fn_(MT_FILE_UPLOAD, request);
}

void FileTransferManager::send_file_chunk(const std::string& transfer_id, uint32_t chunk_index,
                                          const std::vector<uint8_t>& data, bool is_last) {
    if (!send_fn_) return;
    
    FileUploadChunk chunk;
    chunk.set_file_id(transfers_[transfer_id].file_id);
    chunk.set_chunk_index(chunk_index);
    chunk.set_data(data.data(), data.size());
    chunk.set_is_last(is_last);
    
    send_fn_(MT_FILE_UPLOAD, chunk);
}

std::string FileTransferManager::generate_transfer_id() {
    return "xfer_" + std::to_string(next_transfer_id_++);
}

} // namespace ircord::client::file
