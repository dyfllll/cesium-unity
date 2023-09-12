#pragma once

#include <Cesium3DTilesSelection/ViewUpdateResult.h>

#include <DotNet/CesiumForUnity/CesiumCreditSystem.h>
#include <DotNet/CesiumForUnity/CesiumGeoreference.h>
#include <DotNet/System/Action.h>
#include <DotNet/System/Collections/Generic/List1.h>
#include <DotNet/UnityEngine/Vector3.h>
#include <DotNet/UnityEngine/GameObject.h>

#include <memory>

#if UNITY_EDITOR
#include <DotNet/UnityEditor/CallbackFunction.h>
#endif

namespace DotNet::CesiumForUnity {
class Cesium3DTileset;
class CesiumCreditSystem;
} // namespace DotNet::CesiumForUnity

namespace Cesium3DTilesSelection {
class Tileset;
}

namespace CesiumForUnityNative {

class Cesium3DTilesetImpl {
public:
  Cesium3DTilesetImpl(const DotNet::CesiumForUnity::Cesium3DTileset& tileset);
  ~Cesium3DTilesetImpl();

  void SetShowCreditsOnScreen(
      const DotNet::CesiumForUnity::Cesium3DTileset& tileset,
      bool value);
  void Start(const DotNet::CesiumForUnity::Cesium3DTileset& tileset);
  void Update(const DotNet::CesiumForUnity::Cesium3DTileset& tileset);
  void OnValidate(const DotNet::CesiumForUnity::Cesium3DTileset& tileset);
  void OnEnable(const DotNet::CesiumForUnity::Cesium3DTileset& tileset);
  void OnDisable(const DotNet::CesiumForUnity::Cesium3DTileset& tileset);

  void RecreateTileset(const DotNet::CesiumForUnity::Cesium3DTileset& tileset);
  void FocusTileset(const DotNet::CesiumForUnity::Cesium3DTileset& tileset);
  bool RaycastIfNeedLoad(
      const DotNet::CesiumForUnity::Cesium3DTileset& tileset,
      const DotNet::UnityEngine::Vector3& origin,
      const DotNet::UnityEngine::Vector3& direction,
      DotNet::System::Collections::Generic::List1<DotNet::UnityEngine::GameObject> result,
      double maxGeometricError);
  bool IntersectIfNeedLoad(
      const DotNet::CesiumForUnity::Cesium3DTileset& tileset,
      const DotNet::UnityEngine::Vector3& minPosition,
      const DotNet::UnityEngine::Vector3& maxPosition,
      DotNet::System::Collections::Generic::List1<
          DotNet::UnityEngine::GameObject> result,
      double maxGeometricError);

  int32_t UnloadForceLoadTiles(const DotNet::CesiumForUnity::Cesium3DTileset& tileset);

  float
  ComputeLoadProgress(const DotNet::CesiumForUnity::Cesium3DTileset& tileset);

  Cesium3DTilesSelection::Tileset* getTileset();
  const Cesium3DTilesSelection::Tileset* getTileset() const;

  const DotNet::CesiumForUnity::CesiumCreditSystem& getCreditSystem() const;
  void setCreditSystem(
      const DotNet::CesiumForUnity::CesiumCreditSystem& creditSystem);

private:
  void DestroyTileset(const DotNet::CesiumForUnity::Cesium3DTileset& tileset);
  void LoadTileset(const DotNet::CesiumForUnity::Cesium3DTileset& tileset);
  void updateLastViewUpdateResultState(
      const DotNet::CesiumForUnity::Cesium3DTileset& tileset,
      const Cesium3DTilesSelection::ViewUpdateResult& currentResult);
  bool RaycastIfNeedLoad(
      const DotNet::CesiumForUnity::Cesium3DTileset& tileset,
      const Cesium3DTilesSelection::Tile* tile,
      const glm::dvec3& origin,
      const glm::dvec3& direction,
      DotNet::System::Collections::Generic::List1<
          DotNet::UnityEngine::GameObject> result,
      double maxGeometricError);
  bool IntersectIfNeedLoad(
      const DotNet::CesiumForUnity::Cesium3DTileset& tileset,
      const Cesium3DTilesSelection::Tile* tile,
      const glm::dvec3& minPosition,
      const glm::dvec3& maxPosition,
      DotNet::System::Collections::Generic::List1<
          DotNet::UnityEngine::GameObject> result,
      double maxGeometricError
    );

  std::unique_ptr<Cesium3DTilesSelection::Tileset> _pTileset;
  Cesium3DTilesSelection::ViewUpdateResult _lastUpdateResult;
#if UNITY_EDITOR
  DotNet::UnityEditor::CallbackFunction _updateInEditorCallback;
#endif
  DotNet::CesiumForUnity::CesiumCreditSystem _creditSystem;
  bool _destroyTilesetOnNextUpdate;
  int32_t _lastOpaqueMaterialHash;
};

} // namespace CesiumForUnityNative
