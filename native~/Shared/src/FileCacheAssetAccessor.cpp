#include "FileCacheAssetAccessor.h"

#include "CesiumAsync/AsyncSystem.h"
#include "CesiumAsync/CacheItem.h"
#include "CesiumAsync/CachingAssetAccessor.h"
#include "CesiumAsync/IAssetResponse.h"
#include "CesiumUtility/Uri.h"

#include <spdlog/spdlog.h>
#include <uriparser/Uri.h>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace CesiumAsync {

class FileCacheAssetResponse : public IAssetResponse {
public:
  FileCacheAssetResponse(
      std::vector<std::byte>&& cacheData,
      int64_t localCacheTime,
      const std::string& etag) noexcept
      : _pData(std::move(cacheData)),
        _pLocalCacheTime(localCacheTime),
        _Etag(etag),
        _pHeaders{{"Etag", etag}} {}

  virtual uint16_t statusCode() const noexcept override { return 200; }

  virtual std::string contentType() const override { return std::string(); }

  virtual const HttpHeaders& headers() const noexcept override {

    return this->_pHeaders;
  }

  virtual gsl::span<const std::byte> data() const noexcept override {
    return gsl::span<const std::byte>(_pData.data(), _pData.size());
  }

private:
  HttpHeaders _pHeaders;
  int64_t _pLocalCacheTime;
  std::string _Etag;
  std::vector<std::byte> _pData;
};

class FileCacheAssetRequest : public IAssetRequest {
public:
  FileCacheAssetRequest(
      const std::string& url,
      const std::string& method,
      std::vector<std::byte>&& cacheData,
      int64_t localCacheTime,
      const std::string& etag)
      : _url(url),
        _method(method),
        _response(std::move(cacheData), localCacheTime, etag) {}

  virtual const std::string& method() const noexcept override {
    return this->_method;
  }

  virtual const std::string& url() const noexcept override {
    return this->_url;
  }

  virtual const HttpHeaders& headers() const noexcept override {
    return _headers;
  }

  virtual const IAssetResponse* response() const noexcept override {
    return &this->_response;
  }

private:
  std::string _method;
  std::string _url;
  HttpHeaders _headers;
  FileCacheAssetResponse _response;
};

static std::string UnescapeUrl(const std::string& url, size_t headSize) {
  std::string unescapeUrl(url);
  const char* pTerminator = uriUnescapeInPlaceExA(
      unescapeUrl.data(),
      URI_FALSE,
      UriBreakConversionEnum::URI_BR_DONT_TOUCH);
  unescapeUrl.resize(size_t(pTerminator - unescapeUrl.data()));

  if (headSize > 0 && unescapeUrl.size() >= headSize) {
    unescapeUrl = unescapeUrl.substr(headSize, unescapeUrl.size() - headSize);
  }

  return unescapeUrl;
}
static std::string EscapeUrl(const std::string& s) {
  std::string result(s.size() * 3, '\0');
  char* pTerminator = uriEscapeA(s.data(), result.data(), URI_FALSE, URI_FALSE);
  result.resize(size_t(pTerminator - result.data()));
  return result;
}

static std::vector<std::string>
StringSplit(const std::string& str, char delim) {
  std::stringstream ss(str);
  std::string item;
  std::vector<std::string> elems;
  while (std::getline(ss, item, delim)) {
    elems.push_back(item);
  }
  if (str.size() > 0 && str[str.size() - 1] == delim)
    elems.emplace_back();
  return elems;
}

static inline std::filesystem::path ToPath(const std::string& path) {
  return std::filesystem::path(std::filesystem::u8path(path));
}

static inline bool Exists(const std::string& path) {
  return std::filesystem::exists(ToPath(path));
}

static inline void
Rename(const std::string& src, const std::string& dst, std::error_code& error) {
  std::filesystem::rename(ToPath(src), ToPath(dst), error);
}

static inline bool Remove(const std::string& path, std::error_code& error) {
  return std::filesystem::remove(ToPath(path), error);
}

static std::string ReadFileText(const std::string& path) {

  std::fstream fs(ToPath(path), std::ios::binary | std::ios::in);
  fs.seekg(0, std::ios::end);
  auto size = fs.tellg();
  fs.seekg(0, std::ios::beg);
  std::string str;
  if (size > 0) {
    str.resize(size, '\0');
    fs.read(str.data(), size);
  }
  fs.close();
  return str;
}

static void WriteFileText(const std::string& path, const std::string& content) {
  std::fstream fs(ToPath(path), std::ios::binary | std::ios::out);
  if (fs.is_open()) {
    fs.write(content.data(), content.size());
    fs.flush();
  }
  fs.close();
}

static std::vector<std::byte> ReadFileData(const std::string& path) {
  std::ifstream fs(ToPath(path), std::ios::binary);
  fs.seekg(0, std::ios::end);
  auto size = fs.tellg();
  fs.seekg(0, std::ios::beg);
  std::vector<std::byte> bytes;
  if (size > 0) {
    // bytes.reserve(size);
    bytes.resize(size);
    fs.read(reinterpret_cast<char*>(bytes.data()), size);
  }
  fs.close();
  return bytes;
}

static void WriteFileData(
    const std::string& path,
    const gsl::span<const std::byte>& content) {
  if (content.size() == 0)
    return;
  std::fstream fs(ToPath(path), std::ios::binary | std::ios::out);
  if (fs.is_open()) {
    fs.write(reinterpret_cast<const char*>(content.data()), content.size());
    fs.flush();
  }
  fs.close();
}

CesiumAsync::FileCacheAssetAccessor::FileCacheAssetAccessor(
    const std::shared_ptr<spdlog::logger>& pLogger,
    const std::shared_ptr<CachingAssetAccessor>& pCacheDatabase,
    const std::string& pLocalCachePath,
    const std::string& pRemoteCachePath,
    int64_t pLocalCacheTime)
    : _pLogger(pLogger),
      _pAssetAccessor(pCacheDatabase),

      _pLocalCachePath(pLocalCachePath),
      _pRemoteCachePath(pRemoteCachePath),
      _pLocalCacheTime(pLocalCacheTime) {}

CesiumAsync::FileCacheAssetAccessor::~FileCacheAssetAccessor() noexcept {}

Future<std::shared_ptr<IAssetRequest>> CesiumAsync::FileCacheAssetAccessor::get(
    const AsyncSystem& asyncSystem,
    const std::string& url,
    const std::vector<THeader>& headers) {

  //_pLogger->info("unescapeUrl: " + unescapeUrl);

  const ThreadPool& threadPool = _pAssetAccessor->getThreadPool();
  if (_pLocalCacheTime != 0) {

    std::string unescapeUrl = UnescapeUrl(url, _pRemoteCachePath.size());

    return asyncSystem
        .runInThreadPool(
            threadPool,
            [asyncSystem,
             url,
             headers,
             pAssetAccessor = this->_pAssetAccessor,
             localCahceRoot = this->_pLocalCachePath,
             localCacheTime = this->_pLocalCacheTime,
             unescapeUrl,
             threadPool,
             pLogger =
                 this->_pLogger]() -> Future<std::shared_ptr<IAssetRequest>> {
      

              std::string localFilePath = localCahceRoot + unescapeUrl;
              std::string localInfoPath = localFilePath + ".info";

              std::filesystem::create_directories(
                  ToPath(localFilePath).remove_filename());

              std::string etag;
              std::string encodeUrl;

              if (Exists(localFilePath) && Exists(localInfoPath)) {

                std::string str = ReadFileText(localInfoPath);
                auto strs = StringSplit(str, '|');
                if (strs.size() >= 3) {
                  int64_t time = std::atoll(strs[0].c_str());
                  etag = strs[1];
                  encodeUrl = strs[2];
                  if (time == localCacheTime) {
                    std::vector<std::byte> bytes = ReadFileData(localFilePath);
                    std::shared_ptr<IAssetRequest> pRequest =
                        std::make_shared<FileCacheAssetRequest>(
                            encodeUrl,
                            "GET",
                            std::move(bytes),
                            time,
                            etag);
                    return asyncSystem.createResolvedFuture(
                        std::move(pRequest));
                  }
                }
              }

              std::vector<THeader> newHeaders = headers;
              if (!etag.empty()) {
                newHeaders.emplace_back("If-None-Match", etag);
              }

              return pAssetAccessor->getNoCache(asyncSystem, url, newHeaders)
                  .thenInThreadPool(
                      threadPool,
                      [etag,
                       localCacheTime,
                       localFilePath,
                       localInfoPath,
                       pLogger](std::shared_ptr<IAssetRequest>&&
                                    pCompletedRequest) mutable {
                        if (!pCompletedRequest) {
                          return std::move(pCompletedRequest);
                        }

                        const HttpHeaders& responseHeaders =
                            pCompletedRequest->response()->headers();

                        std::string newEtag;
                        HttpHeaders::const_iterator etagHeader =
                            responseHeaders.find("Etag");
                        if (etagHeader != responseHeaders.end()) {
                          newEtag = etagHeader->second;
                        }

                        std::ostringstream stringStream;
                        stringStream << localCacheTime << '|' << newEtag << '|'
                                     << pCompletedRequest->url();

                        std::shared_ptr<IAssetRequest> pRequestToStore;
                        uint16_t statusCode =
                            pCompletedRequest->response()->statusCode();
                        if (statusCode == 304) { // status Not-Modified

                          WriteFileText(localInfoPath, stringStream.str());
                          std::vector<std::byte> bytes =
                              ReadFileData(localFilePath);

                          pRequestToStore =
                              std::make_shared<FileCacheAssetRequest>(
                                  pCompletedRequest->url(),
                                  "GET",
                                  std::move(bytes),
                                  localCacheTime,
                                  etag);
                        } else if (
                            statusCode >= 200 && statusCode <= 299) { // success

                          WriteFileText(localInfoPath, stringStream.str());
                          std::string tempPath =
                              localFilePath + "." +
                              std::to_string(std::time(nullptr));

                          WriteFileData(
                              tempPath,
                              pCompletedRequest->response()->data());

                          std::error_code error;
                          Rename(tempPath, localFilePath, error);

                          if (error) {
                            pLogger->info(
                                "rename error:" + tempPath + " " +
                                pCompletedRequest->url());
                          }

                          Remove(tempPath, error);

                          pRequestToStore = pCompletedRequest;
                        } else {

                          // offine cache
                          if (Exists(localFilePath)) {

                            std::vector<std::byte> bytes =
                                ReadFileData(localFilePath);
                            pRequestToStore =
                                std::make_shared<FileCacheAssetRequest>(
                                    pCompletedRequest->url(),
                                    "GET",
                                    std::move(bytes),
                                    localCacheTime,
                                    etag);

                          } else {
                            pRequestToStore = pCompletedRequest;
                          }
                        }

                        return pRequestToStore;
                      });
            })
        .thenImmediately(
            [](std::shared_ptr<IAssetRequest>&& pRequest) noexcept {
              CESIUM_TRACE_END_IN_TRACK("IAssetAccessor::get (cached)");
              return std::move(pRequest);
            });

  } else {

    return _pAssetAccessor->get(asyncSystem, url, headers);
  }
}

Future<std::shared_ptr<IAssetRequest>>
CesiumAsync::FileCacheAssetAccessor::request(
    const AsyncSystem& asyncSystem,
    const std::string& verb,
    const std::string& url,
    const std::vector<THeader>& headers,
    const gsl::span<const std::byte>& contentPayload) {
  return _pAssetAccessor
      ->request(asyncSystem, verb, url, headers, contentPayload);
}

void CesiumAsync::FileCacheAssetAccessor::tick() noexcept {
  _pAssetAccessor->tick();
}

} // namespace CesiumAsync
