#include "HlsRelay.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <thread>
#include <utility>
#include <vector>

#include "StringUtil.h"

namespace tv {
namespace {

const int64_t kProbeBytes = 256 * 1024;    // first piece of a segment: learns its size
const int64_t kMinPartBytes = 128 * 1024;  // smaller pieces are not worth a connection
const int64_t kPieceBytes = 768 * 1024;    // target size of one parallel piece
const double kFastRatio = 2.0;             // this much faster than realtime needs no start buffer
const double kMaxLeadSeconds = 90;         // stop fetching ahead past this much unplayed video
const double kIdleSeconds = 120;           // the player stopped asking: end the session
const size_t kMaxReadySegments = 40;
// Segments behind the live edge to begin at. Kept small: on a slow server the
// older ones can leave the server's window before their turn comes.
const size_t kStartBack = 2;

double now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

#if defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
void logf(bool enabled, const char* format, ...) {
    if (!enabled) return;
    va_list args;
    va_start(args, format);
    std::fputs("[relay] ", stderr);
    std::vfprintf(stderr, format, args);
    std::fputc('\n', stderr);
    va_end(args);
}

std::string optionValue(const Channel& channel, const char* key) {
    for (const auto& option : channel.options)
        if (option.first == key) return option.second;
    return std::string();
}

// RFC 3986 reference resolution, the parts HLS playlists actually use.
std::string resolveUrl(const std::string& base, const std::string& ref) {
    const size_t refScheme = ref.find("://");
    if (refScheme != std::string::npos && ref.find_first_of("/?#") > refScheme) return ref;
    const size_t schemeEnd = base.find("://");
    if (schemeEnd == std::string::npos) return ref;
    if (startsWith(ref, "//")) return base.substr(0, schemeEnd + 1) + ref;
    const size_t hostEnd = base.find_first_of("/?#", schemeEnd + 3);
    const std::string origin = hostEnd == std::string::npos ? base : base.substr(0, hostEnd);
    if (startsWith(ref, "/")) return origin + ref;
    const std::string path = base.substr(0, base.find_first_of("?#"));
    const size_t slash = path.rfind('/');
    if (slash == std::string::npos || slash < schemeEnd + 3) return origin + "/" + ref;
    return path.substr(0, slash + 1) + ref;
}

std::string withAbsoluteUri(const std::string& tag, const std::string& base) {
    const size_t key = tag.find("URI=\"");
    if (key == std::string::npos) return tag;
    const size_t start = key + 5;
    const size_t end = tag.find('"', start);
    if (end == std::string::npos) return tag;
    return tag.substr(0, start) + resolveUrl(base, tag.substr(start, end - start)) + tag.substr(end);
}

std::string extensionOf(const std::string& url) {
    const std::string path = url.substr(0, url.find_first_of("?#"));
    const size_t slash = path.rfind('/');
    const size_t dot = path.rfind('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return ".ts";
    const std::string ext = toLower(path.substr(dot));
    return ext.size() >= 2 && ext.size() <= 5 ? ext : ".ts";
}

const char* contentTypeFor(const std::string& ext) {
    if (ext == ".ts") return "video/mp2t";
    if (ext == ".aac") return "audio/aac";
    if (ext == ".mp4" || ext == ".m4s" || ext == ".m4v") return "video/mp4";
    return "application/octet-stream";
}

struct SegmentInfo {
    uint64_t sequence = 0;
    double duration = 0;
    std::string url;
    bool discontinuity = false;
    std::string keyTag;  // #EXT-X-KEY in force, URI made absolute; empty for none
    std::string mapTag;  // #EXT-X-MAP in force, likewise
};

struct Playlist {
    bool valid = false;
    bool master = false;
    bool endList = false;
    bool byteRange = false;
    bool alternateRenditions = false;  // #EXT-X-MEDIA with a URI of its own
    int version = 3;
    double targetDuration = 10;
    std::vector<SegmentInfo> segments;
    std::vector<std::string> variants;
};

Playlist parsePlaylist(const std::string& text, const std::string& base) {
    Playlist playlist;
    uint64_t sequence = 0;
    double duration = -1;
    bool discontinuity = false;
    bool variantNext = false;
    bool sawHeader = false;
    std::string keyTag;
    std::string mapTag;

    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty()) continue;
        if (!sawHeader) {
            if (!startsWith(line, "#EXTM3U")) return playlist;
            sawHeader = true;
            continue;
        }

        auto value = [&line](const char* tag) { return line.c_str() + std::strlen(tag); };
        if (line[0] == '#') {
            if (startsWith(line, "#EXT-X-STREAM-INF")) {
                playlist.master = true;
                variantNext = true;
            } else if (startsWith(line, "#EXT-X-MEDIA:")) {
                if (line.find("URI=\"") != std::string::npos) playlist.alternateRenditions = true;
            } else if (startsWith(line, "#EXT-X-TARGETDURATION:")) {
                playlist.targetDuration = std::max(1.0, std::atof(value("#EXT-X-TARGETDURATION:")));
            } else if (startsWith(line, "#EXT-X-MEDIA-SEQUENCE:")) {
                sequence = std::strtoull(value("#EXT-X-MEDIA-SEQUENCE:"), nullptr, 10);
            } else if (startsWith(line, "#EXT-X-VERSION:")) {
                playlist.version = std::max(1, std::atoi(value("#EXT-X-VERSION:")));
            } else if (startsWith(line, "#EXTINF:")) {
                duration = std::atof(value("#EXTINF:"));
            } else if (line == "#EXT-X-DISCONTINUITY") {
                discontinuity = true;
            } else if (startsWith(line, "#EXT-X-KEY:")) {
                keyTag = withAbsoluteUri(line, base);
            } else if (startsWith(line, "#EXT-X-MAP:")) {
                mapTag = withAbsoluteUri(line, base);
            } else if (startsWith(line, "#EXT-X-BYTERANGE")) {
                playlist.byteRange = true;
            } else if (line == "#EXT-X-ENDLIST") {
                playlist.endList = true;
            }
            continue;
        }

        if (variantNext) {
            playlist.variants.push_back(resolveUrl(base, line));
            variantNext = false;
        } else if (duration >= 0) {
            SegmentInfo segment;
            segment.sequence = sequence++;
            segment.duration = duration;
            segment.url = resolveUrl(base, line);
            segment.discontinuity = discontinuity;
            segment.keyTag = keyTag;
            segment.mapTag = mapTag;
            playlist.segments.push_back(std::move(segment));
            duration = -1;
            discontinuity = false;
        }
    }
    playlist.valid = playlist.master ? !playlist.variants.empty() : !playlist.segments.empty();
    return playlist;
}

std::string newSessionId() {
    static std::atomic<unsigned> counter{0};
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    char id[40];
    std::snprintf(id, sizeof id, "%llx%x", static_cast<unsigned long long>(ticks) & 0xffffffffULL,
                  ++counter);
    return id;
}

void sendAll(int fd, const char* data, size_t size) {
    while (size > 0) {
        const ssize_t sent = ::send(fd, data, size, 0);
        if (sent < 0 && errno == EINTR) continue;
        if (sent <= 0) return;
        data += sent;
        size -= static_cast<size_t>(sent);
    }
}

}  // namespace

// ---------------------------------------------------------------- session

struct RelayReply {
    int status = 404;
    const char* contentType = "text/plain";
    std::shared_ptr<const std::string> body;
};

// One relayed stream. Its threads each hold a reference, so a stopped session
// lives until the last in-flight transfer has noticed and returned.
class RelaySession : public std::enable_shared_from_this<RelaySession> {
public:
    RelaySession(std::shared_ptr<HttpClient> http, const RelaySettings& settings, std::string id,
                 std::string playlistUrl, const Channel& channel,
                 HlsRelay::ProgressCallback progress)
        : http_(std::move(http)),
          settings_(settings),
          id_(std::move(id)),
          playlistUrl_(std::move(playlistUrl)),
          userAgent_(optionValue(channel, "http-user-agent")),
          referrer_(optionValue(channel, "http-referrer")),
          progress_(std::move(progress)) {}

    const std::string& id() const { return id_; }

    void start(const Playlist& playlist) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            mergeLocked(playlist);
            // A few segments back from the live edge: room to get ahead, and far
            // enough from the oldest ones that none expires before it is fetched.
            const size_t count = playlist.segments.size();
            nextSequence_ = playlist.segments[count - std::min(count, kStartBack)].sequence;
            lastRequestAt_ = now();
        }
        const int workers = std::max(1, settings_.connections);
        {
            std::lock_guard<std::mutex> lock(threadMutex_);
            threads_ = workers + 2;
        }
        auto self = shared_from_this();
        std::thread([self] { self->pollLoop(); }).detach();
        std::thread([self] { self->downloadLoop(); }).detach();
        for (int i = 0; i < workers; ++i) std::thread([self] { self->workerLoop(); }).detach();
    }

    void stop() {
        stopping_.store(true);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        changed_.notify_all();
        {
            std::lock_guard<std::mutex> lock(jobMutex_);
        }
        jobCv_.notify_all();
    }

    // Bounded: an aborted transfer returns within about a second.
    void waitForThreads(double seconds) {
        std::unique_lock<std::mutex> lock(threadMutex_);
        threadsCv_.wait_for(lock, std::chrono::duration<double>(seconds),
                            [this] { return threads_ == 0; });
    }

    RelayReply serve(const std::string& target) {
        const std::string path = target.substr(0, target.find_first_of("?#"));
        const std::string prefix = "/" + id_ + "/";
        if (!startsWith(path, prefix)) return RelayReply();
        const std::string name = path.substr(prefix.size());
        if (name == "index.m3u8") return playlistReply();

        char* end = nullptr;
        const uint64_t index = std::strtoull(name.c_str(), &end, 10);
        if (end == name.c_str()) return RelayReply();

        std::lock_guard<std::mutex> lock(mutex_);
        if (index < readyBase_ || index - readyBase_ >= ready_.size()) return RelayReply();
        const Ready& item = ready_[static_cast<size_t>(index - readyBase_)];
        RelayReply reply;
        reply.status = 200;
        reply.contentType = contentTypeFor(item.ext);
        reply.body = item.data;
        if (!requestedAny_ || index > highestRequested_) highestRequested_ = index;
        requestedAny_ = true;
        lastRequestAt_ = now();
        trimLocked();
        changed_.notify_all();
        return reply;
    }

private:
    struct Ready {
        double duration = 0;
        bool discontinuity = false;
        std::string keyTag;
        std::string mapTag;
        std::string ext;
        std::shared_ptr<const std::string> data;
    };

    // One byte range of a segment. Shared with the worker fetching it, so a piece
    // nobody waits for any more - a duplicate that lost its race, or a stopped
    // session's - can still wind down on its own.
    struct Job {
        enum State { Queued, Running, Done };
        std::string url;
        int64_t first = 0;
        int64_t last = 0;               // -1: to the end of the segment
        std::atomic<bool> cancel{false};
        // The rest is guarded by jobMutex_.
        State state = Queued;
        bool ok = false;
        bool won = false;               // this piece's bytes are in `data`
        long status = 0;
        int64_t total = -1;             // whole segment size, from a 206 reply
        double startedAt = 0;
        double seconds = 0;
        int tries = 1;
        std::string data;
        std::shared_ptr<Job> hedge;     // a duplicate racing this piece
    };
    using JobPtr = std::shared_ptr<Job>;

    struct PieceStats {
        int hedges = 0;
        int hedgeWins = 0;
    };

    HttpRequest request(const std::string& url, long timeoutSeconds) const {
        HttpRequest r;
        r.url = url;
        r.userAgent = userAgent_;
        r.referrer = referrer_;
        r.timeoutSeconds = timeoutSeconds;
        r.shouldAbort = [this] { return stopping_.load(); };
        return r;
    }

    // ----- the player's side

    RelayReply playlistReply() {
        std::unique_lock<std::mutex> lock(mutex_);
        lastRequestAt_ = now();
        if (!released_) {
            // Hold the player's first request until there is a real lead to play
            // from. A player started on one segment would stall at the next.
            const double since = now();
            while (!stop_ && failure_.empty() && !drainedLocked()) {
                if (leadSecondsLocked() >= settings_.startBufferSeconds) break;
                if (!ready_.empty() && throughputLocked() >= kFastRatio) break;
                if (!ready_.empty() && now() - since >= settings_.maxStartWaitSeconds) break;
                changed_.wait_for(lock, std::chrono::milliseconds(250));
            }
            if (stop_ || ready_.empty()) {
                RelayReply reply;
                reply.status = 502;
                return reply;
            }
            released_ = true;
            lastRequestAt_ = now();
            logf(settings_.verbose, "%s: player starts with %.1f s ready, fetching at x%.2f realtime",
                 id_.c_str(), leadSecondsLocked(), throughputLocked());
        }

        std::string text = "#EXTM3U\n#EXT-X-VERSION:" + std::to_string(version_) +
                           "\n#EXT-X-TARGETDURATION:" +
                           std::to_string(static_cast<int>(std::ceil(targetDuration_))) +
                           "\n#EXT-X-MEDIA-SEQUENCE:" + std::to_string(readyBase_) + "\n";
        std::string key;
        std::string map;
        for (size_t i = 0; i < ready_.size(); ++i) {
            const Ready& item = ready_[i];
            if (item.keyTag != key) {
                key = item.keyTag;
                if (!key.empty()) text += key + "\n";
            }
            if (item.mapTag != map) {
                map = item.mapTag;
                if (!map.empty()) text += map + "\n";
            }
            if (item.discontinuity && i > 0) text += "#EXT-X-DISCONTINUITY\n";
            char extinf[48];
            std::snprintf(extinf, sizeof extinf, "#EXTINF:%.3f,\n", item.duration);
            text += extinf;
            text += std::to_string(readyBase_ + i) + item.ext + "\n";
        }
        if (drainedLocked()) text += "#EXT-X-ENDLIST\n";

        RelayReply reply;
        reply.status = 200;
        reply.contentType = "application/vnd.apple.mpegurl";
        reply.body = std::make_shared<const std::string>(std::move(text));
        return reply;
    }

    // Seconds of finished video the player has not asked for yet.
    double leadSecondsLocked() const {
        double seconds = 0;
        for (size_t i = 0; i < ready_.size(); ++i)
            if (!requestedAny_ || readyBase_ + i > highestRequested_) seconds += ready_[i].duration;
        return seconds;
    }

    // Seconds of video fetched per second of wall time, over the last few segments.
    double throughputLocked() const {
        double content = 0;
        double wall = 0;
        for (const auto& sample : samples_) {
            content += sample.first;
            wall += sample.second;
        }
        return wall > 0 ? content / wall : 0;
    }

    bool drainedLocked() const {
        return ended_ && (known_.empty() || nextSequence_ > known_.rbegin()->first);
    }

    // Start-buffer progress for the UI. Finished pieces of the segment being
    // fetched count too, so the number moves every few seconds rather than once
    // per segment (ten seconds apart on a slow stream).
    void reportStartProgress(double partialSeconds) {
        int percent = -1;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (released_ || settings_.startBufferSeconds <= 0) return;
            const int filled = static_cast<int>(std::min(
                100.0, (leadSecondsLocked() + partialSeconds) * 100.0 / settings_.startBufferSeconds));
            if (filled > lastPercent_) percent = lastPercent_ = filled;
        }
        if (percent >= 0 && progress_) progress_(percent);
    }

    void trimLocked() {
        // A couple of segments stay behind the player for a late re-request.
        while (!ready_.empty() && ((requestedAny_ && readyBase_ + 2 < highestRequested_) ||
                                   ready_.size() > kMaxReadySegments)) {
            ready_.pop_front();
            ++readyBase_;
        }
    }

    // ----- the server's side

    void mergeLocked(const Playlist& playlist) {
        targetDuration_ = playlist.targetDuration;
        version_ = playlist.version;
        const size_t count = playlist.segments.size();
        const uint64_t first = playlist.segments.front().sequence;
        const uint64_t last = playlist.segments.back().sequence;
        // A restarted encoder numbers from scratch; follow it instead of waiting
        // for sequence numbers that will never come.
        if (appendedAny_ && last + 20 < nextSequence_) {
            logf(settings_.verbose, "%s: sequence restarted, following it", id_.c_str());
            known_.clear();
            nextSequence_ = playlist.segments[count - std::min(count, kStartBack)].sequence;
        }
        for (const SegmentInfo& segment : playlist.segments) known_.emplace(segment.sequence, segment);
        known_.erase(known_.begin(), known_.lower_bound(first));
    }

    void pollLoop() {
        int failures = 0;
        for (;;) {
            bool idle = false;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                const double interval = std::max(1.0, std::min(5.0, targetDuration_ / 2));
                if (changed_.wait_for(lock, std::chrono::duration<double>(interval),
                                      [this] { return stop_; }))
                    break;
                idle = released_ && now() - lastRequestAt_ > kIdleSeconds;
            }
            if (idle) {
                logf(settings_.verbose, "%s: player went quiet, ending", id_.c_str());
                stop();
                break;
            }

            HttpResponse response = http_->get(request(playlistUrl_, 15));
            if (stopping_.load()) break;
            Playlist playlist;
            if (response.ok())
                playlist = parsePlaylist(response.body, response.effectiveUrl.empty()
                                                            ? playlistUrl_
                                                            : response.effectiveUrl);

            std::lock_guard<std::mutex> lock(mutex_);
            if (!playlist.valid || playlist.master) {
                if (++failures >= 20) {
                    failure_ = response.error.empty() ? "playlist no longer available" : response.error;
                    logf(settings_.verbose, "%s: giving up on the playlist (%s)", id_.c_str(),
                         failure_.c_str());
                    changed_.notify_all();
                    break;
                }
                continue;
            }
            failures = 0;
            mergeLocked(playlist);
            if (playlist.endList) ended_ = true;
            changed_.notify_all();
            if (ended_) break;
        }
        threadExited();
    }

    void downloadLoop() {
        int failures = 0;
        for (;;) {
            SegmentInfo segment;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                changed_.wait(lock, [this] {
                    return stop_ || !failure_.empty() ||
                           (!known_.empty() && known_.rbegin()->first >= nextSequence_ &&
                            leadSecondsLocked() < kMaxLeadSeconds);
                });
                if (stop_ || !failure_.empty()) break;
                if (known_.begin()->first > nextSequence_) {
                    logf(settings_.verbose, "%s: fell out of the server's window, skipping to #%llu",
                         id_.c_str(), static_cast<unsigned long long>(known_.begin()->first));
                    nextSequence_ = known_.begin()->first;
                    // Before the player has started, the gap need not reach it: drop
                    // what was buffered ahead of the gap and buffer afresh after it.
                    if (!released_ && !ready_.empty()) {
                        readyBase_ += ready_.size();
                        ready_.clear();
                        samples_.clear();
                        appendedAny_ = false;
                        lastPercent_ = -1;
                    }
                }
                auto it = known_.find(nextSequence_);
                if (it == known_.end()) {
                    ++nextSequence_;  // a hole in the numbering
                    continue;
                }
                segment = it->second;
            }

            fetchingDuration_ = segment.duration;
            const double started = now();
            std::string data;
            std::string detail;
            bool gone = false;
            const bool ok = fetchSegment(segment, &data, &gone, &detail);
            const double wall = std::max(0.001, now() - started);

            bool retryLater = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (stop_) break;
                if (ok) {
                    failures = 0;
                    Ready item;
                    item.duration = segment.duration;
                    item.discontinuity = segment.discontinuity ||
                                         (appendedAny_ && segment.sequence != lastAppended_ + 1);
                    item.keyTag = segment.keyTag;
                    item.mapTag = segment.mapTag;
                    item.ext = extensionOf(segment.url);
                    const double megabytes = static_cast<double>(data.size()) / 1048576.0;
                    item.data = std::make_shared<const std::string>(std::move(data));
                    ready_.push_back(std::move(item));
                    appendedAny_ = true;
                    lastAppended_ = segment.sequence;
                    nextSequence_ = segment.sequence + 1;
                    samples_.emplace_back(segment.duration, wall);
                    while (samples_.size() > 6) samples_.pop_front();
                    trimLocked();
                    logf(settings_.verbose, "%s: #%llu %.1f s, %.2f MB in %.1f s (x%.2f), lead %.1f s; %s",
                         id_.c_str(), static_cast<unsigned long long>(segment.sequence),
                         segment.duration, megabytes, wall, segment.duration / wall,
                         leadSecondsLocked(), detail.c_str());
                } else if (gone || ++failures >= 3) {
                    logf(settings_.verbose, "%s: #%llu dropped (%s)", id_.c_str(),
                         static_cast<unsigned long long>(segment.sequence),
                         gone ? "no longer on the server" : "failed three times");
                    failures = 0;
                    nextSequence_ = segment.sequence + 1;
                } else {
                    retryLater = true;
                }
                changed_.notify_all();
            }
            if (ok) reportStartProgress(0);
            if (retryLater) {
                std::unique_lock<std::mutex> lock(mutex_);
                changed_.wait_for(lock, std::chrono::seconds(1), [this] { return stop_; });
            }
        }
        threadExited();
    }

    // Segments of one live stream come out much the same size, so the last
    // segment's size is a good enough guess to split the next one on straight
    // away: every connection starts at once, and the final piece asks for "the
    // rest" so a larger segment still arrives whole. Only the first segment,
    // with nothing to guess from, is probed for its size first.
    bool fetchSegment(const SegmentInfo& segment, std::string* out, bool* gone, std::string* detail) {
        detail->clear();
        const bool ok = lastSegmentBytes_ > 0 ? fetchEstimated(segment, out, gone, detail)
                                              : fetchProbed(segment, out, gone, detail);
        if (ok) lastSegmentBytes_ = static_cast<int64_t>(out->size());
        return ok;
    }

    // About kPieceBytes each, at least one piece per connection and at most three:
    // a connection that turns slow then holds up one small piece, not a quarter
    // of the segment.
    int64_t pieceCount(int64_t bytes) const {
        if (bytes < 2 * kMinPartBytes) return 1;
        const int64_t connections = std::max(1, settings_.connections);
        const int64_t wanted = (bytes + kPieceBytes - 1) / kPieceBytes;
        const int64_t count = std::min(std::max(wanted, connections), connections * 3);
        return std::max<int64_t>(1, std::min(count, bytes / kMinPartBytes));
    }

    std::vector<JobPtr> splitRange(const std::string& url, int64_t from, int64_t bytes,
                                   bool openEnded) const {
        const int64_t count = pieceCount(bytes);
        const int64_t step = bytes / count;
        std::vector<JobPtr> jobs;
        for (int64_t i = 0; i < count; ++i) {
            JobPtr job = std::make_shared<Job>();
            job->url = url;
            job->first = from + i * step;
            job->last = i < count - 1 ? job->first + step - 1 : (openEnded ? -1 : from + bytes - 1);
            jobs.push_back(std::move(job));
        }
        return jobs;
    }

    bool fetchEstimated(const SegmentInfo& segment, std::string* out, bool* gone,
                        std::string* detail) {
        std::vector<JobPtr> jobs = splitRange(segment.url, 0, lastSegmentBytes_, true);
        PieceStats stats;
        const double started = now();
        if (!runPieces(jobs, gone, &stats)) return false;
        describePieces(jobs, now() - started, stats, detail);

        for (const JobPtr& job : jobs) {
            if (job->status == 200) {  // ranges ignored: that reply is the whole segment
                out->swap(job->data);
                return true;
            }
        }
        int64_t total = -1;
        for (const JobPtr& job : jobs) {
            if (job->status == 206 && job->total >= 0) {
                total = job->total;
                break;
            }
        }
        if (total < 0) return false;

        std::string data;
        data.reserve(static_cast<size_t>(total));
        for (const JobPtr& job : jobs) {
            if (job->first >= total) continue;  // guessed past the end; the server said 416
            const int64_t last = job->last < 0 ? total - 1 : std::min(job->last, total - 1);
            if (job->status != 206 || static_cast<int64_t>(job->data.size()) != last - job->first + 1)
                return false;
            data.append(job->data);
        }
        if (static_cast<int64_t>(data.size()) != total) return false;
        out->swap(data);
        return true;
    }

    // The first piece comes over this thread's own connection and tells how big
    // the segment is and whether the server takes ranges at all; the rest is
    // split across the workers.
    bool fetchProbed(const SegmentInfo& segment, std::string* out, bool* gone, std::string* detail) {
        HttpRequest probe = request(segment.url, 30);
        probe.rangeFirst = 0;
        probe.rangeLast = kProbeBytes - 1;
        const double probeStarted = now();
        HttpResponse head = http_->get(probe);
        char note[64];
        std::snprintf(note, sizeof note, "probe %.1f s", now() - probeStarted);
        *detail = note;
        if (stopping_.load()) return false;
        if (head.status == 404 || head.status == 410) {
            *gone = true;
            return false;
        }
        if (!head.ok()) return false;
        if (head.status == 200) {  // ranges ignored: that was the whole segment
            out->swap(head.body);
            return true;
        }

        const int64_t have = static_cast<int64_t>(head.body.size());
        if (head.totalLength < 0) {  // a range reply without a total: take the rest in one go
            HttpRequest rest = request(segment.url, 60);
            rest.rangeFirst = have;
            HttpResponse tail = http_->get(rest);
            if (tail.status != 206 || !tail.ok()) return false;
            out->swap(head.body);
            out->append(tail.body);
            return true;
        }
        const int64_t total = head.totalLength;
        if (have >= total) {
            out->swap(head.body);
            return true;
        }

        std::vector<JobPtr> jobs = splitRange(segment.url, have, total - have, false);
        PieceStats stats;
        const double started = now();
        if (!runPieces(jobs, gone, &stats)) return false;
        *detail += ", ";
        describePieces(jobs, now() - started, stats, detail);

        std::string data;
        data.reserve(static_cast<size_t>(total));
        data.append(head.body);
        for (const JobPtr& job : jobs) {
            if (job->status != 206 ||
                static_cast<int64_t>(job->data.size()) != job->last - job->first + 1)
                return false;
            data.append(job->data);
        }
        if (static_cast<int64_t>(data.size()) != total) return false;
        out->swap(data);
        return true;
    }

    // Fetches every piece over the workers. A failed piece is retried, three
    // tries in all. A piece still running well after most others are in gets a
    // duplicate on an idle connection, and whichever finishes first is kept: one
    // connection gone slow would otherwise hold up the whole segment. Returns
    // false when the session stops, a piece keeps failing, or the segment has
    // left the server.
    bool runPieces(const std::vector<JobPtr>& jobs, bool* gone, PieceStats* stats) {
        std::unique_lock<std::mutex> lock(jobMutex_);
        for (const JobPtr& job : jobs) queue_.push_back(job);
        jobCv_.notify_all();

        auto abandon = [&jobs] {
            for (const JobPtr& job : jobs) {
                job->cancel.store(true);
                if (job->hedge) job->hedge->cancel.store(true);
            }
            return false;
        };

        size_t reported = 0;
        for (;;) {
            if (stopping_.load()) return abandon();

            size_t resolved = 0;
            std::vector<double> times;
            for (const JobPtr& job : jobs) {
                if (!job->won) {
                    const JobPtr hedge = job->hedge;
                    if (job->state == Job::Done && job->ok) {
                        job->won = true;
                        if (hedge) hedge->cancel.store(true);
                    } else if (hedge && hedge->state == Job::Done && hedge->ok) {
                        // The duplicate won. The original gives up at its next
                        // check and, being cancelled, leaves these fields alone.
                        job->cancel.store(true);
                        job->data.swap(hedge->data);
                        job->status = hedge->status;
                        job->total = hedge->total;
                        job->seconds = now() - job->startedAt;
                        job->won = true;
                        ++stats->hedgeWins;
                    } else if (job->state == Job::Done && (!hedge || hedge->state == Job::Done)) {
                        if (job->status == 404 || job->status == 410) {
                            *gone = true;
                            return abandon();
                        }
                        if (job->tries >= 3) return abandon();
                        ++job->tries;
                        job->hedge.reset();
                        job->state = Job::Queued;
                        queue_.push_back(job);
                        jobCv_.notify_all();
                    }
                }
                if (job->won) {
                    ++resolved;
                    times.push_back(job->seconds);
                }
            }
            if (resolved > reported) {
                reported = resolved;
                lock.unlock();  // the progress callback must not run under jobMutex_
                reportStartProgress(fetchingDuration_ * static_cast<double>(resolved) /
                                    static_cast<double>(jobs.size()));
                lock.lock();
            }
            if (resolved == jobs.size()) return true;

            // One straggler at a time gets a duplicate, once at least half the
            // pieces are in and a connection sits idle.
            if (queue_.empty() && busyWorkers_ < std::max(1, settings_.connections) &&
                resolved * 2 >= jobs.size()) {
                std::sort(times.begin(), times.end());
                double longest = std::max(1.5, 1.5 * times[times.size() / 2]);
                const double t = now();
                JobPtr slowest;
                for (const JobPtr& job : jobs) {
                    if (job->won || job->hedge || job->state != Job::Running) continue;
                    if (t - job->startedAt > longest) {
                        longest = t - job->startedAt;
                        slowest = job;
                    }
                }
                if (slowest) {
                    JobPtr hedge = std::make_shared<Job>();
                    hedge->url = slowest->url;
                    hedge->first = slowest->first;
                    hedge->last = slowest->last;
                    slowest->hedge = hedge;
                    queue_.push_back(std::move(hedge));
                    jobCv_.notify_all();
                    ++stats->hedges;
                }
            }
            jobCv_.wait_for(lock, std::chrono::milliseconds(200));
        }
    }

    static void describePieces(const std::vector<JobPtr>& jobs, double seconds,
                               const PieceStats& stats, std::string* detail) {
        std::vector<double> times;
        for (const JobPtr& job : jobs) times.push_back(job->seconds);
        std::sort(times.begin(), times.end());
        char note[128];
        std::snprintf(note, sizeof note, "%zu pieces in %.1f s (median %.1f, slowest %.1f)",
                      jobs.size(), seconds, times[times.size() / 2], times.back());
        *detail += note;
        if (stats.hedges > 0) {
            std::snprintf(note, sizeof note, ", %d raced, %d won by the duplicate", stats.hedges,
                          stats.hedgeWins);
            *detail += note;
        }
    }

    void workerLoop() {
        for (;;) {
            JobPtr job;
            {
                std::unique_lock<std::mutex> lock(jobMutex_);
                jobCv_.wait(lock, [this] { return stopping_.load() || !queue_.empty(); });
                if (stopping_.load()) break;
                job = std::move(queue_.front());
                queue_.pop_front();
                if (job->cancel.load()) {  // its race was decided while it waited
                    job->state = Job::Done;
                    continue;
                }
                job->state = Job::Running;
                job->startedAt = now();
                ++busyWorkers_;
            }
            HttpRequest part = request(job->url, 60);
            part.rangeFirst = job->first;
            part.rangeLast = job->last;
            part.shouldAbort = [this, job] { return stopping_.load() || job->cancel.load(); };
            HttpResponse response = http_->get(part);
            {
                std::lock_guard<std::mutex> lock(jobMutex_);
                --busyWorkers_;
                job->state = Job::Done;
                if (!job->cancel.load()) {
                    // 416: a guessed piece that starts past the end of the segment.
                    job->ok = ((response.status == 206 || response.status == 200) &&
                               response.error.empty()) ||
                              response.status == 416;
                    job->status = response.status;
                    job->total = response.totalLength;
                    job->seconds = now() - job->startedAt;
                    job->data.swap(response.body);
                }
            }
            jobCv_.notify_all();
        }
        threadExited();
    }

    void threadExited() {
        std::lock_guard<std::mutex> lock(threadMutex_);
        --threads_;
        threadsCv_.notify_all();
    }

    const std::shared_ptr<HttpClient> http_;
    const RelaySettings settings_;
    const std::string id_;
    const std::string playlistUrl_;
    const std::string userAgent_;
    const std::string referrer_;
    const HlsRelay::ProgressCallback progress_;

    std::atomic<bool> stopping_{false};  // lock-free copy of stop_ for transfers to poll

    std::mutex mutex_;
    std::condition_variable changed_;
    bool stop_ = false;
    bool ended_ = false;
    std::string failure_;
    double targetDuration_ = 10;
    int version_ = 3;
    std::map<uint64_t, SegmentInfo> known_;  // the server's current window, by sequence
    uint64_t nextSequence_ = 0;
    bool appendedAny_ = false;
    uint64_t lastAppended_ = 0;
    std::deque<Ready> ready_;                // finished segments, in play order
    uint64_t readyBase_ = 0;                 // relay index of ready_.front()
    uint64_t highestRequested_ = 0;
    bool requestedAny_ = false;
    bool released_ = false;
    double lastRequestAt_ = 0;
    std::deque<std::pair<double, double>> samples_;  // (video seconds, wall seconds)
    int64_t lastSegmentBytes_ = 0;                   // downloader thread only
    int lastPercent_ = -1;
    double fetchingDuration_ = 0;                    // downloader thread only

    std::mutex jobMutex_;
    std::condition_variable jobCv_;
    std::deque<JobPtr> queue_;
    int busyWorkers_ = 0;

    std::mutex threadMutex_;
    std::condition_variable threadsCv_;
    int threads_ = 0;
};

// ----------------------------------------------------------------- server

// A deliberately small HTTP/1.1 server: GET and HEAD, one request per
// connection, bound to the loopback interface only.
class RelayServer {
public:
    explicit RelayServer(bool verbose) : verbose_(verbose) {}

    ~RelayServer() {
        stop_.store(true);
        if (acceptThread_.joinable()) acceptThread_.join();
        setSession(nullptr);
        std::unique_lock<std::mutex> lock(mutex_);
        idle_.wait(lock, [this] { return active_ == 0; });
        if (listenFd_ >= 0) ::close(listenFd_);
    }

    bool start(std::string* error) {
        // A player hanging up in the middle of a segment must not kill the app.
        std::signal(SIGPIPE, SIG_IGN);

        listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd_ < 0) {
            *error = std::strerror(errno);
            return false;
        }
        int yes = 1;
        ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
        sockaddr_in address;
        std::memset(&address, 0, sizeof address);
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;  // any free port
        if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&address), sizeof address) < 0 ||
            ::listen(listenFd_, 16) < 0) {
            *error = std::strerror(errno);
            ::close(listenFd_);
            listenFd_ = -1;
            return false;
        }
        socklen_t length = sizeof address;
        ::getsockname(listenFd_, reinterpret_cast<sockaddr*>(&address), &length);
        port_ = ntohs(address.sin_port);
        acceptThread_ = std::thread([this] { acceptLoop(); });
        logf(verbose_, "listening on 127.0.0.1:%d", port_);
        return true;
    }

    int port() const { return port_; }

    // Installs a session and stops the one it replaces, which is returned.
    std::shared_ptr<RelaySession> setSession(std::shared_ptr<RelaySession> session) {
        std::shared_ptr<RelaySession> old;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            old.swap(session_);
            session_ = std::move(session);
        }
        if (old) old->stop();
        return old;
    }

private:
    void acceptLoop() {
        while (!stop_.load()) {
            pollfd poller;
            poller.fd = listenFd_;
            poller.events = POLLIN;
            poller.revents = 0;
            if (::poll(&poller, 1, 250) <= 0) continue;
            int fd = ::accept(listenFd_, nullptr, nullptr);
            if (fd < 0) continue;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                ++active_;
            }
            std::thread([this, fd] {
                handle(fd);
                ::close(fd);
                std::lock_guard<std::mutex> lock(mutex_);
                --active_;
                idle_.notify_all();
            }).detach();
        }
    }

    void handle(int fd) {
        timeval timeout;
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);

        std::string header;
        char buffer[2048];
        while (header.find("\r\n\r\n") == std::string::npos && header.size() < 16384) {
            const ssize_t got = ::recv(fd, buffer, sizeof buffer, 0);
            if (got < 0 && errno == EINTR) continue;
            if (got <= 0) return;
            header.append(buffer, static_cast<size_t>(got));
        }
        const std::string line = header.substr(0, header.find("\r\n"));
        const size_t space1 = line.find(' ');
        const size_t space2 = space1 == std::string::npos ? space1 : line.find(' ', space1 + 1);
        if (space2 == std::string::npos) return;
        const std::string method = line.substr(0, space1);
        const std::string target = line.substr(space1 + 1, space2 - space1 - 1);

        RelayReply reply;
        if (method != "GET" && method != "HEAD") {
            reply.status = 405;
        } else {
            std::shared_ptr<RelaySession> session;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                session = session_;
            }
            if (session) reply = session->serve(target);
        }

        const size_t length = reply.body ? reply.body->size() : 0;
        // Which requests a player actually makes, and when, is the first thing to
        // look at when a backend stalls on relayed streams.
        logf(verbose_, "serve %s %s -> %d, %zu bytes", method.c_str(), target.c_str(), reply.status,
             length);
        const char* reason = reply.status == 200   ? "OK"
                             : reply.status == 404 ? "Not Found"
                             : reply.status == 405 ? "Method Not Allowed"
                                                   : "Bad Gateway";
        char head[256];
        const int headLength = std::snprintf(
            head, sizeof head,
            "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
            "Cache-Control: no-cache\r\nConnection: close\r\n\r\n",
            reply.status, reason, reply.contentType, length);
        sendAll(fd, head, static_cast<size_t>(headLength));
        if (method == "GET" && length > 0) sendAll(fd, reply.body->data(), length);
    }

    const bool verbose_;
    int listenFd_ = -1;
    int port_ = 0;
    std::atomic<bool> stop_{false};
    std::thread acceptThread_;
    std::mutex mutex_;
    std::condition_variable idle_;
    int active_ = 0;
    std::shared_ptr<RelaySession> session_;
};

// ------------------------------------------------------------------ relay

RelaySettings RelaySettings::fromEnvironment() {
    RelaySettings settings;
    if (const char* value = std::getenv("RTV_RELAY_CONNECTIONS"))
        settings.connections = std::max(1, std::min(16, std::atoi(value)));
    if (const char* value = std::getenv("RTV_RELAY_BUFFER"))
        settings.startBufferSeconds = std::max(0.0, std::atof(value));
    settings.verbose = std::getenv("RTV_VERBOSE") != nullptr;
    return settings;
}

HlsRelay::HlsRelay(std::shared_ptr<HttpClient> http, RelaySettings settings)
    : http_(std::move(http)), settings_(settings) {}

HlsRelay::~HlsRelay() {
    std::shared_ptr<RelaySession> session;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (server_) session = server_->setSession(nullptr);
    }
    if (session) session->waitForThreads(3.0);
    server_.reset();
}

bool HlsRelay::looksLikeHls(const std::string& url) {
    const std::string lower = toLower(url);
    return (startsWith(lower, "http://") || startsWith(lower, "https://")) &&
           lower.find(".m3u8") != std::string::npos;
}

void HlsRelay::setProgressCallback(ProgressCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    progress_ = std::move(callback);
}

void HlsRelay::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (server_) server_->setSession(nullptr);
}

std::string HlsRelay::open(const Channel& channel, const std::function<bool()>& cancelled,
                           bool* relayed) {
    if (relayed) *relayed = false;
    close();
    if (!looksLikeHls(channel.url)) return channel.url;

    auto fetch = [&](const std::string& url, std::string* base) {
        HttpRequest request;
        request.url = url;
        request.userAgent = optionValue(channel, "http-user-agent");
        request.referrer = optionValue(channel, "http-referrer");
        request.timeoutSeconds = 15;
        request.shouldAbort = cancelled;
        HttpResponse response = http_->get(request);
        *base = response.effectiveUrl.empty() ? url : response.effectiveUrl;
        return response.ok() ? parsePlaylist(response.body, *base) : Playlist();
    };

    std::string base;
    std::string mediaUrl = channel.url;
    Playlist playlist = fetch(mediaUrl, &base);
    if (cancelled()) return channel.url;
    if (playlist.valid && playlist.master) {
        if (playlist.variants.size() != 1 || playlist.alternateRenditions) {
            logf(settings_.verbose, "%s: adaptive (%zu variants), played directly",
                 channel.name.c_str(), playlist.variants.size());
            return channel.url;
        }
        mediaUrl = playlist.variants.front();
        playlist = fetch(mediaUrl, &base);
        if (cancelled()) return channel.url;
    }
    if (!playlist.valid || playlist.master || playlist.endList || playlist.byteRange) {
        logf(settings_.verbose, "%s: not a live single-rendition playlist, played directly",
             channel.name.c_str());
        return channel.url;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!server_) {
        std::unique_ptr<RelayServer> server(new RelayServer(settings_.verbose));
        std::string error;
        if (!server->start(&error)) {
            logf(settings_.verbose, "cannot listen on 127.0.0.1 (%s), playing directly", error.c_str());
            return channel.url;
        }
        server_ = std::move(server);
    }

    const std::string id = newSessionId();
    auto session = std::make_shared<RelaySession>(http_, settings_, id, mediaUrl, channel, progress_);
    session->start(playlist);
    server_->setSession(session);
    if (relayed) *relayed = true;
    logf(settings_.verbose, "%s: relaying %s as session %s", channel.name.c_str(), mediaUrl.c_str(),
         id.c_str());
    return "http://127.0.0.1:" + std::to_string(server_->port()) + "/" + id + "/index.m3u8";
}

}  // namespace tv
