#include "Cesium3DTilesetImpl.h"

#include "CameraManager.h"
#include "CesiumGeoreferenceImpl.h"
#include "UnityPrepareRendererResources.h"
#include "UnityTileExcluderAdaptor.h"
#include "UnityTilesetExternals.h"
#include "UnityTransforms.h"

#include <Cesium3DTilesSelection/IonRasterOverlay.h>
#include <Cesium3DTilesSelection/Tileset.h>
#include <CesiumGeospatial/Ellipsoid.h>
#include <CesiumGeospatial/GlobeTransforms.h>
#include <CesiumUtility/Math.h>

#include <DotNet/CesiumForUnity/Cesium3DTileset.h>
#include <DotNet/CesiumForUnity/Cesium3DTilesetLoadFailureDetails.h>
#include <DotNet/CesiumForUnity/Cesium3DTilesetLoadType.h>
#include <DotNet/CesiumForUnity/CesiumDataSource.h>
#include <DotNet/CesiumForUnity/CesiumGeoreference.h>
#include <DotNet/CesiumForUnity/CesiumGlobeAnchor.h>
#include <DotNet/CesiumForUnity/CesiumRasterOverlay.h>
#include <DotNet/CesiumForUnity/CesiumRuntimeSettings.h>
#include <DotNet/CesiumForUnity/CesiumTileExcluder.h>
#include <DotNet/System/Action.h>
#include <DotNet/System/Array1.h>
#include <DotNet/System/Object.h>
#include <DotNet/System/String.h>
#include <DotNet/UnityEngine/Application.h>
#include <DotNet/UnityEngine/Camera.h>
#include <DotNet/UnityEngine/Debug.h>
#include <DotNet/UnityEngine/Experimental/Rendering/FormatUsage.h>
#include <DotNet/UnityEngine/Experimental/Rendering/GraphicsFormat.h>
#include <DotNet/UnityEngine/GameObject.h>
#include <DotNet/UnityEngine/Material.h>
#include <DotNet/UnityEngine/Matrix4x4.h>
#include <DotNet/UnityEngine/MeshCollider.h>
#include <DotNet/UnityEngine/Quaternion.h>
#include <DotNet/UnityEngine/SystemInfo.h>
#include <DotNet/UnityEngine/Time.h>
#include <DotNet/UnityEngine/Transform.h>
#include <DotNet/UnityEngine/Vector3.h>

#include <variant>

#if UNITY_EDITOR
#include <DotNet/UnityEditor/CallbackFunction.h>
#include <DotNet/UnityEditor/EditorApplication.h>
#include <DotNet/UnityEditor/SceneView.h>
#endif
using namespace DotNet::CesiumForUnity;

using namespace CesiumGeospatial;
using namespace CesiumUtility;
using namespace Cesium3DTilesSelection;
using namespace DotNet;

namespace CesiumForUnityNative {

Cesium3DTilesetImpl::Cesium3DTilesetImpl(
    const DotNet::CesiumForUnity::Cesium3DTileset& tileset)
    : _pTileset(),
      _lastUpdateResult(),
#if UNITY_EDITOR
      _updateInEditorCallback(nullptr),
#endif
      _creditSystem(nullptr),
      _destroyTilesetOnNextUpdate(false),
      _lastOpaqueMaterialHash(0) {
}

Cesium3DTilesetImpl::~Cesium3DTilesetImpl() {}

void Cesium3DTilesetImpl::SetShowCreditsOnScreen(
    const DotNet::CesiumForUnity::Cesium3DTileset& tileset,
    bool value) {
  if (this->_pTileset) {
    this->_pTileset->setShowCreditsOnScreen(value);
  }
}

void Cesium3DTilesetImpl::Start(
    const DotNet::CesiumForUnity::Cesium3DTileset& tileset) {}

void Cesium3DTilesetImpl::Update(
    const DotNet::CesiumForUnity::Cesium3DTileset& tileset) {
  assert(tileset.enabled());

  // If "Suspend Update" is true, return early.
  if (tileset.suspendUpdate()) {
    return;
  }

  if (this->_destroyTilesetOnNextUpdate) {
    this->_destroyTilesetOnNextUpdate = false;
    this->DestroyTileset(tileset);
  }

#if UNITY_EDITOR
  if (UnityEngine::Application::isEditor() &&
      !UnityEditor::EditorApplication::isPlaying()) {
    // If "Update In Editor" is false, return early.
    if (!tileset.updateInEditor()) {
      return;
    }

    // If the opaque material or any of its properties have changed, recreate
    // the tileset to reflect those changes.
    if (tileset.opaqueMaterial() != nullptr) {
      int32_t opaqueMaterialHash = tileset.opaqueMaterial().ComputeCRC();
      if (_lastOpaqueMaterialHash != opaqueMaterialHash) {
        this->DestroyTileset(tileset);
        _lastOpaqueMaterialHash = opaqueMaterialHash;
      }
    }
  }

#endif

  if (!this->_pTileset) {
    this->LoadTileset(tileset);
    if (!this->_pTileset)
      return;
  }

  std::vector<ViewState> viewStates =
      CameraManager::getAllCameras(tileset.gameObject());

  const ViewUpdateResult& updateResult = this->_pTileset->updateView(
      viewStates,
      DotNet::UnityEngine::Time::deltaTime());
  this->updateLastViewUpdateResultState(tileset, updateResult);

  for (auto pTile : updateResult.tilesFadingOut) {
    if (pTile->getState() != TileLoadState::Done) {
      continue;
    }

    const Cesium3DTilesSelection::TileContent& content = pTile->getContent();
    const Cesium3DTilesSelection::TileRenderContent* pRenderContent =
        content.getRenderContent();
    if (pRenderContent) {
      CesiumGltfGameObject* pCesiumGameObject =
          static_cast<CesiumGltfGameObject*>(
              pRenderContent->getRenderResources());
      if (pCesiumGameObject && pCesiumGameObject->pGameObject) {
        pCesiumGameObject->pGameObject->SetActive(false);
      }
    }
  }

  for (auto pTile : updateResult.tilesToRenderThisFrame) {
    if (pTile->getState() != TileLoadState::Done) {
      continue;
    }

    const Cesium3DTilesSelection::TileContent& content = pTile->getContent();
    const Cesium3DTilesSelection::TileRenderContent* pRenderContent =
        content.getRenderContent();
    if (pRenderContent) {
      CesiumGltfGameObject* pCesiumGameObject =
          static_cast<CesiumGltfGameObject*>(
              pRenderContent->getRenderResources());
      if (pCesiumGameObject && pCesiumGameObject->pGameObject) {
        pCesiumGameObject->pGameObject->SetActive(true);
      }
    }
  }
}

void Cesium3DTilesetImpl::OnValidate(
    const DotNet::CesiumForUnity::Cesium3DTileset& tileset) {
  // Check if "Suspend Update" was the modified value.
  if (tileset.suspendUpdate() != tileset.previousSuspendUpdate()) {
    // If so, don't destroy the tileset.
    tileset.previousSuspendUpdate(tileset.suspendUpdate());
  } else {
    // Otherwise, destroy the tileset so it can be recreated with new settings.
    // Unity does not allow us to destroy GameObjects and MonoBehaviours in this
    // callback, so instead it is marked to happen later.
    this->_destroyTilesetOnNextUpdate = true;
  }
}

void Cesium3DTilesetImpl::OnEnable(
    const DotNet::CesiumForUnity::Cesium3DTileset& tileset) {
#if UNITY_EDITOR
  // In the Editor, Update will only be called when something
  // changes. We need to call it continuously to allow tiles to
  // load.
  if (UnityEngine::Application::isEditor() &&
      !UnityEditor::EditorApplication::isPlaying()) {
    this->_updateInEditorCallback = UnityEditor::CallbackFunction(
        [this, tileset]() { this->Update(tileset); });
    UnityEditor::EditorApplication::update(
        UnityEditor::EditorApplication::update() +
        this->_updateInEditorCallback);
  }
#endif
}

void Cesium3DTilesetImpl::OnDisable(
    const DotNet::CesiumForUnity::Cesium3DTileset& tileset) {
#if UNITY_EDITOR
  if (this->_updateInEditorCallback != nullptr) {
    UnityEditor::EditorApplication::update(
        UnityEditor::EditorApplication::update() -
        this->_updateInEditorCallback);
    this->_updateInEditorCallback = nullptr;
  }
#endif

  this->_creditSystem = nullptr;

  this->DestroyTileset(tileset);
}

void Cesium3DTilesetImpl::RecreateTileset(
    const DotNet::CesiumForUnity::Cesium3DTileset& tileset) {
  this->DestroyTileset(tileset);
}

namespace {

struct CalculateECEFCameraPosition {
  const CesiumGeospatial::Ellipsoid& ellipsoid;

  glm::dvec3 operator()(const CesiumGeometry::BoundingSphere& sphere) {
    const glm::dvec3& center = sphere.getCenter();
    glm::dmat4 enuToEcef =
        glm::dmat4(CesiumGeospatial::GlobeTransforms::eastNorthUpToFixedFrame(
            center,
            ellipsoid));
    glm::dvec3 offset = sphere.getRadius() * glm::normalize(
                                                 glm::dvec3(enuToEcef[0]) +
                                                 glm::dvec3(enuToEcef[1]) +
                                                 glm::dvec3(enuToEcef[2]));
    glm::dvec3 position = center + offset;
    return position;
  }

  glm::dvec3
  operator()(const CesiumGeometry::OrientedBoundingBox& orientedBoundingBox) {
    const glm::dvec3& center = orientedBoundingBox.getCenter();
    glm::dmat4 enuToEcef =
        glm::dmat4(CesiumGeospatial::GlobeTransforms::eastNorthUpToFixedFrame(
            center,
            ellipsoid));
    const glm::dmat3& halfAxes = orientedBoundingBox.getHalfAxes();
    glm::dvec3 offset =
        glm::length(halfAxes[0] + halfAxes[1] + halfAxes[2]) *
        glm::normalize(
            glm::dvec3(enuToEcef[0]) + glm::dvec3(enuToEcef[1]) +
            glm::dvec3(enuToEcef[2]));
    glm::dvec3 position = center + offset;
    return position;
  }

  glm::dvec3
  operator()(const CesiumGeospatial::BoundingRegion& boundingRegion) {
    return (*this)(boundingRegion.getBoundingBox());
  }

  glm::dvec3
  operator()(const CesiumGeospatial::BoundingRegionWithLooseFittingHeights&
                 boundingRegionWithLooseFittingHeights) {
    return (*this)(boundingRegionWithLooseFittingHeights.getBoundingRegion()
                       .getBoundingBox());
  }

  glm::dvec3 operator()(const CesiumGeospatial::S2CellBoundingVolume& s2) {
    return (*this)(s2.computeBoundingRegion());
  }
};
} // namespace

void Cesium3DTilesetImpl::FocusTileset(
    const DotNet::CesiumForUnity::Cesium3DTileset& tileset) {

#if UNITY_EDITOR
  UnityEditor::SceneView lastActiveEditorView =
      UnityEditor::SceneView::lastActiveSceneView();
  if (!this->_pTileset || !this->_pTileset->getRootTile() ||
      lastActiveEditorView == nullptr) {
    return;
  }

  UnityEngine::Camera editorCamera = lastActiveEditorView.camera();
  if (editorCamera == nullptr) {
    return;
  }

  DotNet::CesiumForUnity::CesiumGeoreference georeferenceComponent =
      tileset.gameObject()
          .GetComponentInParent<DotNet::CesiumForUnity::CesiumGeoreference>();

  const CesiumGeospatial::LocalHorizontalCoordinateSystem& georeferenceCrs =
      georeferenceComponent.NativeImplementation().getCoordinateSystem(
          georeferenceComponent);
  const glm::dmat4& ecefToUnityWorld =
      georeferenceCrs.getEcefToLocalTransformation();

  const BoundingVolume& boundingVolume =
      this->_pTileset->getRootTile()->getBoundingVolume();
  glm::dvec3 ecefCameraPosition = std::visit(
      CalculateECEFCameraPosition{CesiumGeospatial::Ellipsoid::WGS84},
      boundingVolume);
  glm::dvec3 unityCameraPosition =
      glm::dvec3(ecefToUnityWorld * glm::dvec4(ecefCameraPosition, 1.0));

  glm::dvec3 ecefCenter =
      Cesium3DTilesSelection::getBoundingVolumeCenter(boundingVolume);
  glm::dvec3 unityCenter =
      glm::dvec3(ecefToUnityWorld * glm::dvec4(ecefCenter, 1.0));
  glm::dvec3 unityCameraFront =
      glm::normalize(unityCenter - unityCameraPosition);
  glm::dvec3 unityCameraRight =
      glm::normalize(glm::cross(glm::dvec3(0.0, 0.0, 1.0), unityCameraFront));
  glm::dvec3 unityCameraUp =
      glm::normalize(glm::cross(unityCameraFront, unityCameraRight));

  UnityEngine::Vector3 unityCameraPositionf;
  unityCameraPositionf.x = static_cast<float>(unityCameraPosition.x);
  unityCameraPositionf.y = static_cast<float>(unityCameraPosition.y);
  unityCameraPositionf.z = static_cast<float>(unityCameraPosition.z);

  UnityEngine::Vector3 unityCameraFrontf;
  unityCameraFrontf.x = static_cast<float>(unityCameraFront.x);
  unityCameraFrontf.y = static_cast<float>(unityCameraFront.y);
  unityCameraFrontf.z = static_cast<float>(unityCameraFront.z);

  lastActiveEditorView.pivot(unityCameraPositionf);
  lastActiveEditorView.rotation(UnityEngine::Quaternion::LookRotation(
      unityCameraFrontf,
      UnityEngine::Vector3::up()));
#endif
}

bool Cesium3DTilesetImpl::RaycastIfNeedLoad(
    const DotNet::CesiumForUnity::Cesium3DTileset& tileset,
    const Cesium3DTilesSelection::Tile* tile,
    const glm::dvec3& origin,
    const glm::dvec3& direction,
    DotNet::System::Collections::Generic::List1<DotNet::UnityEngine::GameObject>
        result) {

  const BoundingVolume& boundingVolume = tile->getBoundingVolume();

  struct Operation {
    const glm::dvec3& origin;
    const glm::dvec3& direction;

    //aabb
    bool operator()(
        const CesiumGeometry::OrientedBoundingBox& boundingBox) noexcept {
      CesiumGeometry::AxisAlignedBox aabb = boundingBox.toAxisAligned();
      glm::dvec3 pMin = glm::dvec3(aabb.minimumX, aabb.minimumY, aabb.minimumZ);
      glm::dvec3 pMax = glm::dvec3(aabb.maximumX, aabb.maximumY, aabb.maximumZ);

      glm::dvec3 invDir = glm::dvec3(
          direction.x != 0 ? (1.0 / direction.x) : 1e10,
          direction.y != 0 ? (1.0 / direction.y) : 1e10,
          direction.z != 0 ? (1.0 / direction.z) : 1e10);

      glm::dvec3 minResult = (pMin - origin) * invDir;
      glm::dvec3 maxResult = (pMax - origin) * invDir;

      glm::dvec3 dirIsNeg = glm::dvec3(
          (int)(direction.x > 0),
          (int)(direction.y > 0),
          (int)(direction.z > 0));

      for (int i = 0; i < 3; i++) {
        if (dirIsNeg[i] == 0) {
          double temp = minResult[i];
          minResult[i] = maxResult[i];
          maxResult[i] = temp;
        }
      }
      double enter = glm::max(glm::max(minResult.x, minResult.y), minResult.z);
      double exit = glm::min(glm::min(maxResult.x, maxResult.y), maxResult.z);
      return enter <= exit && exit >= 0;
    }

    bool operator()(
        const CesiumGeospatial::BoundingRegion& boundingRegion) noexcept {
      return false;
    }
    bool
    operator()(const CesiumGeometry::BoundingSphere& boundingSphere) noexcept {
      const glm::dvec3& center = boundingSphere.getCenter();
      double radius = boundingSphere.getRadius();
      glm::dvec3 l = center - origin;
      double s = glm::dot(l, direction);
      double l2 = glm::dot(l, l);
      double r2 = radius * radius;
      if (s < 0 && l2 > r2)
        return false;
      double m2 = l2 - s * s;
      if (m2 > r2)
        return false;
      // float q = Mathf.Sqrt(r2 - m2);
      ////float t;
      // if (l2 > r2)
      //     result = s - q;
      // else
      //     result = s + q;
      return true;
    }

    bool
    operator()(const CesiumGeospatial::BoundingRegionWithLooseFittingHeights&
                   boundingRegion) noexcept {
      return false;
    }

    bool
    operator()(const CesiumGeospatial::S2CellBoundingVolume& s2Cell) noexcept {
      return false;
    }
  };

  bool hasAssetLoad = false;

  if (std::visit(Operation{origin, direction}, boundingVolume)) {

    hasAssetLoad |=
        this->_pTileset->addTileToForceLoadQueue(const_cast<Tile&>(*tile));
    if (tile->getState() == TileLoadState::ContentLoading)
      hasAssetLoad |= true;
  
    gsl::span<const Tile> children = tile->getChildren();

    if (children.size() == 0 && tile->getState() == TileLoadState::Done &&
        tile->isRenderContent()) {

      const Cesium3DTilesSelection::TileContent& content = tile->getContent();
      const Cesium3DTilesSelection::TileRenderContent* pRenderContent =
          content.getRenderContent();

      if (pRenderContent) {
        CesiumGltfGameObject* pCesiumGameObject =
            static_cast<CesiumGltfGameObject*>(
                pRenderContent->getRenderResources());

        if (pCesiumGameObject && pCesiumGameObject->pGameObject &&
            pCesiumGameObject->pGameObject
                    ->GetComponentInChildren<DotNet::UnityEngine::MeshCollider>(
                        true) != nullptr) {

          result.Add(*pCesiumGameObject->pGameObject);
        }
      }
    }

    for (const Tile& child : children) {
      hasAssetLoad |=
          RaycastIfNeedLoad(tileset, &child, origin, direction, result);
    }
  }

  return hasAssetLoad;
}

bool Cesium3DTilesetImpl::RaycastIfNeedLoad(
    const DotNet::CesiumForUnity::Cesium3DTileset& tileset,
    const DotNet::UnityEngine::Vector3& origin,
    const DotNet::UnityEngine::Vector3& direction,
    DotNet::System::Collections::Generic::List1<DotNet::UnityEngine::GameObject>
        result) {

  if (!this->_pTileset)
    return false;

  const LocalHorizontalCoordinateSystem* pCoordinateSystem = nullptr;
  const DotNet::UnityEngine::GameObject& context = tileset.gameObject();
  glm::dmat4 unityWorldToTileset =
      UnityTransforms::fromUnity(context.transform().worldToLocalMatrix());
  CesiumGeoreference georeferenceComponent =
      context.GetComponentInParent<CesiumGeoreference>();
  if (georeferenceComponent != nullptr) {
    CesiumGeoreferenceImpl& georeference =
        georeferenceComponent.NativeImplementation();
    pCoordinateSystem =
        &georeference.getCoordinateSystem(georeferenceComponent);
  }

  glm::dvec3 rayPosition = glm::dvec3(
      unityWorldToTileset * glm::dvec4(origin.x, origin.y, origin.z, 1.0));

  glm::dvec3 rayDirection = glm::dvec3(
      unityWorldToTileset *
      glm::dvec4(direction.x, direction.y, direction.z, 0.0));

  if (pCoordinateSystem) {
    rayPosition = pCoordinateSystem->localPositionToEcef(rayPosition);
    rayDirection = pCoordinateSystem->localDirectionToEcef(rayDirection);
  }

  Cesium3DTilesSelection::Tile* pRootTile = _pTileset->getRootTile();
  if (!pRootTile) {
    return false;
  }
  return RaycastIfNeedLoad(
      tileset,
      pRootTile,
      rayPosition,
      rayDirection,
      result);
}

int32_t Cesium3DTilesetImpl::UnloadForceLoadTiles(
    const DotNet::CesiumForUnity::Cesium3DTileset& tileset) {
  if (!this->_pTileset)
    return 0;
  return this->_pTileset->unloadForceLoadTiles();
}

float Cesium3DTilesetImpl::ComputeLoadProgress(
    const DotNet::CesiumForUnity::Cesium3DTileset& tileset) {
  if (getTileset() == nullptr) {
    return 0;
  }
  return getTileset()->computeLoadProgress();
}

Tileset* Cesium3DTilesetImpl::getTileset() { return this->_pTileset.get(); }

const Tileset* Cesium3DTilesetImpl::getTileset() const {
  return this->_pTileset.get();
}
const DotNet::CesiumForUnity::CesiumCreditSystem&
Cesium3DTilesetImpl::getCreditSystem() const {
  return this->_creditSystem;
}

void Cesium3DTilesetImpl::setCreditSystem(
    const DotNet::CesiumForUnity::CesiumCreditSystem& creditSystem) {
  this->_creditSystem = creditSystem;
}

void Cesium3DTilesetImpl::updateLastViewUpdateResultState(
    const DotNet::CesiumForUnity::Cesium3DTileset& tileset,
    const Cesium3DTilesSelection::ViewUpdateResult& currentResult) {
  if (!tileset.logSelectionStats())
    return;

  const ViewUpdateResult& previousResult = this->_lastUpdateResult;
  if (currentResult.tilesToRenderThisFrame.size() !=
          previousResult.tilesToRenderThisFrame.size() ||
      currentResult.workerThreadTileLoadQueueLength !=
          previousResult.workerThreadTileLoadQueueLength ||
      currentResult.mainThreadTileLoadQueueLength !=
          previousResult.mainThreadTileLoadQueueLength ||
      currentResult.tilesVisited != previousResult.tilesVisited ||
      currentResult.culledTilesVisited != previousResult.culledTilesVisited ||
      currentResult.tilesCulled != previousResult.tilesCulled ||
      currentResult.maxDepthVisited != previousResult.maxDepthVisited) {
    SPDLOG_LOGGER_INFO(
        this->_pTileset->getExternals().pLogger,
        "{0}: Visited {1}, Culled Visited {2}, Rendered {3}, Culled {4}, Max "
        "Depth Visited {5}, Loading-Worker {6}, Loading-Main {7} "
        "Total Tiles Resident {8}, Frame {9}",
        tileset.gameObject().name().ToStlString(),
        currentResult.tilesVisited,
        currentResult.culledTilesVisited,
        currentResult.tilesToRenderThisFrame.size(),
        currentResult.tilesCulled,
        currentResult.maxDepthVisited,
        currentResult.workerThreadTileLoadQueueLength,
        currentResult.mainThreadTileLoadQueueLength,
        this->_pTileset->getNumberOfTilesLoaded(),
        currentResult.frameNumber);
  }

  this->_lastUpdateResult = currentResult;
}

void Cesium3DTilesetImpl::DestroyTileset(
    const DotNet::CesiumForUnity::Cesium3DTileset& tileset) {
  // Remove any existing raster overlays
  System::Array1<CesiumForUnity::CesiumRasterOverlay> overlays =
      tileset.gameObject().GetComponents<CesiumForUnity::CesiumRasterOverlay>();
  for (int32_t i = 0, len = overlays.Length(); i < len; ++i) {
    CesiumForUnity::CesiumRasterOverlay overlay = overlays[i];
    overlay.RemoveFromTileset();
  }

  this->_pTileset.reset();
}

void Cesium3DTilesetImpl::LoadTileset(
    const DotNet::CesiumForUnity::Cesium3DTileset& tileset) {
  TilesetOptions options{};
  options.maximumScreenSpaceError = tileset.maximumScreenSpaceError();
  options.preloadAncestors = tileset.preloadAncestors();
  options.preloadSiblings = tileset.preloadSiblings();
  options.forbidHoles = tileset.forbidHoles();
  options.maximumSimultaneousTileLoads = tileset.maximumSimultaneousTileLoads();
  options.maximumCachedBytes = tileset.maximumCachedBytes();
  options.loadingDescendantLimit = tileset.loadingDescendantLimit();
  options.enableFrustumCulling = tileset.enableFrustumCulling();
  options.enableFogCulling = tileset.enableFogCulling();
  options.enforceCulledScreenSpaceError =
      tileset.enforceCulledScreenSpaceError();
  options.culledScreenSpaceError = tileset.culledScreenSpaceError();
  // options.enableLodTransitionPeriod = tileset.useLodTransitions();
  // options.lodTransitionLength = tileset.lodTransitionLength();
  options.showCreditsOnScreen = tileset.showCreditsOnScreen();
  options.loadErrorCallback =
      [tileset](const TilesetLoadFailureDetails& details) {
        int typeValue = (int)details.type;
        CesiumForUnity::Cesium3DTilesetLoadFailureDetails unityDetails(
            tileset,
            CesiumForUnity::Cesium3DTilesetLoadType(typeValue),
            details.statusCode,
            System::String(details.message));

        CesiumForUnity::Cesium3DTileset::BroadcastCesium3DTilesetLoadFailure(
            unityDetails);
      };

  // Generous per-frame time limits for loading / unloading on main thread.
  options.mainThreadLoadingTimeLimit = 5.0;
  options.tileCacheUnloadTimeLimit = 5.0;

  TilesetContentOptions contentOptions{};
  contentOptions.generateMissingNormalsSmooth = tileset.generateSmoothNormals();

  CesiumGltf::SupportedGpuCompressedPixelFormats supportedFormats;
  supportedFormats.ETC2_RGBA = UnityEngine::SystemInfo::IsFormatSupported(
      DotNet::UnityEngine::Experimental::Rendering::GraphicsFormat::
          RGBA_ETC2_SRGB,
      DotNet::UnityEngine::Experimental::Rendering::FormatUsage::Sample);
  supportedFormats.ETC1_RGB = UnityEngine::SystemInfo::IsFormatSupported(
      DotNet::UnityEngine::Experimental::Rendering::GraphicsFormat::
          RGB_ETC_UNorm,
      DotNet::UnityEngine::Experimental::Rendering::FormatUsage::Sample);
  supportedFormats.BC1_RGB = DotNet::UnityEngine::SystemInfo::IsFormatSupported(
      DotNet::UnityEngine::Experimental::Rendering::GraphicsFormat::
          RGBA_DXT1_SRGB,
      DotNet::UnityEngine::Experimental::Rendering::FormatUsage::Sample);
  supportedFormats.BC3_RGBA =
      DotNet::UnityEngine::SystemInfo::IsFormatSupported(
          DotNet::UnityEngine::Experimental::Rendering::GraphicsFormat::
              RGBA_DXT5_SRGB,
          DotNet::UnityEngine::Experimental::Rendering::FormatUsage::Sample);
  supportedFormats.BC4_R = DotNet::UnityEngine::SystemInfo::IsFormatSupported(
      DotNet::UnityEngine::Experimental::Rendering::GraphicsFormat::R_BC4_SNorm,
      DotNet::UnityEngine::Experimental::Rendering::FormatUsage::Sample);
  supportedFormats.BC5_RG = DotNet::UnityEngine::SystemInfo::IsFormatSupported(
      DotNet::UnityEngine::Experimental::Rendering::GraphicsFormat::
          RG_BC5_SNorm,
      DotNet::UnityEngine::Experimental::Rendering::FormatUsage::Sample);
  supportedFormats.BC7_RGBA =
      DotNet::UnityEngine::SystemInfo::IsFormatSupported(
          DotNet::UnityEngine::Experimental::Rendering::GraphicsFormat::
              RGBA_BC7_SRGB,
          DotNet::UnityEngine::Experimental::Rendering::FormatUsage::Sample);
  supportedFormats.ASTC_4x4_RGBA =
      DotNet::UnityEngine::SystemInfo::IsFormatSupported(
          DotNet::UnityEngine::Experimental::Rendering::GraphicsFormat::
              RGBA_ASTC4X4_SRGB,
          DotNet::UnityEngine::Experimental::Rendering::FormatUsage::Sample);
  supportedFormats.PVRTC1_4_RGB =
      DotNet::UnityEngine::SystemInfo::IsFormatSupported(
          DotNet::UnityEngine::Experimental::Rendering::GraphicsFormat::
              RGB_PVRTC_4Bpp_SRGB,
          DotNet::UnityEngine::Experimental::Rendering::FormatUsage::Sample);
  supportedFormats.PVRTC1_4_RGBA =
      DotNet::UnityEngine::SystemInfo::IsFormatSupported(
          DotNet::UnityEngine::Experimental::Rendering::GraphicsFormat::
              RGBA_PVRTC_4Bpp_SRGB,
          DotNet::UnityEngine::Experimental::Rendering::FormatUsage::Sample);
  supportedFormats
      .ETC2_EAC_R11 = DotNet::UnityEngine::SystemInfo::IsFormatSupported(
      DotNet::UnityEngine::Experimental::Rendering::GraphicsFormat::R_EAC_UNorm,
      DotNet::UnityEngine::Experimental::Rendering::FormatUsage::Sample);
  supportedFormats.ETC2_EAC_RG11 =
      DotNet::UnityEngine::SystemInfo::IsFormatSupported(
          DotNet::UnityEngine::Experimental::Rendering::GraphicsFormat::
              RG_EAC_UNorm,
          DotNet::UnityEngine::Experimental::Rendering::FormatUsage::Sample);

  contentOptions.ktx2TranscodeTargets =
      CesiumGltf::Ktx2TranscodeTargets(supportedFormats, false);

  options.contentOptions = contentOptions;

  this->_lastUpdateResult = ViewUpdateResult();

  if (tileset.tilesetSource() ==
      CesiumForUnity::CesiumDataSource::FromCesiumIon) {
    System::String ionAccessToken = tileset.ionAccessToken();
    if (System::String::IsNullOrEmpty(ionAccessToken)) {
      ionAccessToken =
          CesiumForUnity::CesiumRuntimeSettings::defaultIonAccessToken();
    }

    this->_pTileset = std::make_unique<Tileset>(
        createTilesetExternals(tileset),
        tileset.ionAssetID(),
        ionAccessToken.ToStlString(),
        options);
  } else {
    this->_pTileset = std::make_unique<Tileset>(
        createTilesetExternals(tileset),
        tileset.url().ToStlString(),
        options);
  }

  // Add any overlay components
  System::Array1<CesiumForUnity::CesiumRasterOverlay> overlays =
      tileset.gameObject().GetComponents<CesiumForUnity::CesiumRasterOverlay>();
  for (int32_t i = 0, len = overlays.Length(); i < len; ++i) {
    CesiumForUnity::CesiumRasterOverlay overlay = overlays[i];
    overlay.AddToTileset();
  }

  // Add any tile excluder components
  System::Array1<CesiumForUnity::CesiumTileExcluder> excluders =
      tileset.gameObject()
          .GetComponentsInParent<CesiumForUnity::CesiumTileExcluder>();
  for (int32_t i = 0, len = excluders.Length(); i < len; ++i) {
    CesiumForUnity::CesiumTileExcluder excluder = excluders[i];
    if (!excluder.enabled()) {
      continue;
    }

    excluder.AddToTileset(tileset);
  }

  // If the tileset has an opaque material, set its hash here to avoid
  // destroying it on the first tick after creation.
  if (tileset.opaqueMaterial() != nullptr) {
    int32_t opaqueMaterialHash = tileset.opaqueMaterial().ComputeCRC();
    _lastOpaqueMaterialHash = opaqueMaterialHash;
  }
}

} // namespace CesiumForUnityNative
