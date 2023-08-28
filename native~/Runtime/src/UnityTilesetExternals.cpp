#include "UnityTilesetExternals.h"

#include "FileCacheAssetAccessor.h"
#include "UnityAssetAccessor.h"
#include "UnityPrepareRendererResources.h"
#include "UnityTaskProcessor.h"

#include <Cesium3DTilesSelection/CreditSystem.h>
#include <CesiumAsync/CachingAssetAccessor.h>
#include <CesiumAsync/GunzipAssetAccessor.h>
#include <CesiumAsync/SqliteCache.h>
#include <CesiumAsync/ThreadPool.h>

#include <DotNet/CesiumForUnity/CesiumCreditSystem.h>
#include <DotNet/CesiumForUnity/CesiumRuntimeSettings.h>
#include <DotNet/System/String.h>
#include <DotNet/UnityEngine/Application.h>

#include <memory>

using namespace Cesium3DTilesSelection;
using namespace CesiumAsync;
using namespace DotNet;

namespace CesiumForUnityNative {

namespace {

std::shared_ptr<GunzipAssetAccessor> pAccessor = nullptr;
std::shared_ptr<UnityTaskProcessor> pTaskProcessor = nullptr;
std::shared_ptr<CreditSystem> pCreditSystem = nullptr;
std::shared_ptr<ThreadPool> pThreadPool = nullptr;

const std::shared_ptr<GunzipAssetAccessor>
getAssetAccessor(const DotNet::CesiumForUnity::Cesium3DTileset& unityTileset) {

  if (!pThreadPool) {
    pThreadPool = std::make_shared<ThreadPool>(4);
  }

  if (!unityTileset.useFileCache()) {
    if (!pAccessor) {
      std::string tempPath =
          UnityEngine::Application::temporaryCachePath().ToStlString();
      std::string cacheDBPath = tempPath + "/cesium-request-cache.sqlite";

      int32_t requestsPerCachePrune =
          CesiumForUnity::CesiumRuntimeSettings::requestsPerCachePrune();
      uint64_t maxItems = CesiumForUnity::CesiumRuntimeSettings::maxItems();

      pAccessor = std::make_shared<GunzipAssetAccessor>(
          std::make_shared<CachingAssetAccessor>(
              spdlog::default_logger(),
              std::make_shared<UnityAssetAccessor>(),
              std::make_shared<SqliteCache>(
                  spdlog::default_logger(),
                  cacheDBPath,
                  maxItems),
              requestsPerCachePrune));
    }
    return pAccessor;
  } else {
    int64_t localCacheTime = unityTileset.localCacheTime();
    std::string localCachePath = unityTileset.localCachePath().ToStlString();
    std::string remoteCachePath = unityTileset.remoteCachePath().ToStlString();
    return std::make_shared<GunzipAssetAccessor>(
        std::make_shared<FileCacheAssetAccessor>(
            spdlog::default_logger(),
            std::make_shared<UnityAssetAccessor>(),
            pThreadPool,
            localCachePath,
            remoteCachePath,
            localCacheTime));
  }
}

const std::shared_ptr<UnityTaskProcessor>& getTaskProcessor() {
  if (!pTaskProcessor) {
    pTaskProcessor = std::make_shared<UnityTaskProcessor>();
  }
  return pTaskProcessor;
}

const std::shared_ptr<CreditSystem>&
getCreditSystem(const CesiumForUnity::Cesium3DTileset& tileset) {
  // Get the credit system associated with the tileset.
  Cesium3DTilesetImpl& tilesetImpl = tileset.NativeImplementation();
  CesiumForUnity::CesiumCreditSystem creditSystem =
      tilesetImpl.getCreditSystem();

  // If the tileset does not already reference a credit system,
  // get the default one.
  if (creditSystem == nullptr) {
    creditSystem = CesiumForUnity::CesiumCreditSystem::GetDefaultCreditSystem();
    tilesetImpl.setCreditSystem(creditSystem);
  }

  CesiumCreditSystemImpl& creditSystemImpl =
      creditSystem.NativeImplementation();
  pCreditSystem = creditSystemImpl.getExternalCreditSystem();

  return pCreditSystem;
}

} // namespace

Cesium3DTilesSelection::TilesetExternals
createTilesetExternals(const CesiumForUnity::Cesium3DTileset& tileset) {
  return TilesetExternals{
      getAssetAccessor(tileset),
      std::make_shared<UnityPrepareRendererResources>(tileset.gameObject()),
      AsyncSystem(getTaskProcessor()),
      getCreditSystem(tileset),
      spdlog::default_logger()};
}

} // namespace CesiumForUnityNative
