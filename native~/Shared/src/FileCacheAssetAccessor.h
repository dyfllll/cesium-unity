#pragma once

#include "CesiumAsync/CachingAssetAccessor.h"
#include "CesiumAsync/IAssetAccessor.h"
#include "CesiumAsync/IAssetRequest.h"
#include "CesiumAsync/ICacheDatabase.h"
#include "CesiumAsync/ThreadPool.h"


#include <spdlog/fwd.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>

namespace CesiumAsync {
class AsyncSystem;

class FileCacheAssetAccessor : public IAssetAccessor {
public:
  FileCacheAssetAccessor(
      const std::shared_ptr<spdlog::logger>& pLogger,
      const std::shared_ptr<IAssetAccessor>& pCacheDatabase,
      const std::shared_ptr<ThreadPool>& pThreadPool,
      const std::string& pLocalCachePath,
      const std::string& pRemoteCachePath,
      int64_t pLocalCacheTime);
  virtual ~FileCacheAssetAccessor() noexcept override;

  /** @copydoc IAssetAccessor::get */
  virtual Future<std::shared_ptr<IAssetRequest>>
  get(const AsyncSystem& asyncSystem,
      const std::string& url,
      const std::vector<THeader>& headers) override;

  virtual Future<std::shared_ptr<IAssetRequest>> request(
      const AsyncSystem& asyncSystem,
      const std::string& verb,
      const std::string& url,
      const std::vector<THeader>& headers,
      const gsl::span<const std::byte>& contentPayload) override;

  /** @copydoc IAssetAccessor::tick */
  virtual void tick() noexcept override;

private:
  std::shared_ptr<spdlog::logger> _pLogger;
  std::shared_ptr<ThreadPool> _pThreadPool;
  int64_t _pLocalCacheTime;
  std::string _pLocalCachePath;
  std::string _pRemoteCachePath;
  std::shared_ptr<IAssetAccessor> _pAssetAccessor;
};

} // namespace CesiumAsync
