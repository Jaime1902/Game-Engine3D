#pragma once

#include "ge3d/History.hpp"
#include "ge3d/Scene.hpp"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ge3d {

// Prefabs: plantillas reutilizables guardadas como archivo (*.prefab.json, con el mismo formato que una
// escena). Su contenido es una entidad raíz (la primera sin padre) con su descendencia. Cada entidad de
// una instancia lleva un PrefabLink a su entidad del prefab. Al cambiar el prefab, las instancias reciben
// los cambios salvo en los componentes que ellas mismas cambiaron (overrides), como en Unity.
// La transformación de la raíz es siempre de la instancia (dónde se colocó). Los datos de un terreno se
// copian al crear la instancia y después son de ella (solo se sincroniza su configuración).

// Componentes que se comparan y sincronizan, por nombre.
inline constexpr const char* prefabComponents[] = {"name", "active", "transform", "shape", "model", "terrain", "heightmap",
                                                   "script", "body", "audio", "player"};

[[nodiscard]] const Entity* prefabRoot(const Scene& prefab);

// Contenido de un prefab nuevo a partir de una entidad de la escena y su descendencia (identidades nuevas,
// la raíz en el origen). mapping recibe, por entidad de la escena, el UUID que tiene en el prefab.
[[nodiscard]] Scene makePrefabContent(const Scene& scene, EntityId root, std::map<EntityId, Uuid>* mapping = nullptr);

// Contenido del prefab tomado de una de sus instancias (Aplicar). Las entidades conservan su UUID del
// prefab; las añadidas en la instancia reciben uno nuevo, que se devuelve en newLinks para enlazarlas.
[[nodiscard]] Scene prefabFromInstance(const Scene& scene, EntityId instanceRoot, std::map<EntityId, Uuid>* newLinks = nullptr);

// Crea una instancia: comando de creación de todas sus entidades. root recibe el id de la raíz.
[[nodiscard]] EditCommand makeInstantiateCommand(Scene& scene, const Scene& prefab, const std::string& asset,
                                                 const Transform& rootTransform, EntityId parent, EntityId* root = nullptr);

// Componentes en los que una entidad de instancia difiere de su entidad del prefab.
[[nodiscard]] std::vector<std::string> prefabDifferences(const Entity& instance, const Entity& source, bool isRoot);
// Recalcula los overrides de todas las instancias de asset comparándolas con el prefab.
void refreshPrefabOverrides(Scene& scene, const std::string& asset, const Scene& prefab);
// Lleva el contenido del prefab a todas sus instancias respetando los overrides guardados.
// Añade a las instancias las entidades nuevas del prefab y quita las que el prefab ya no tiene.
[[nodiscard]] std::optional<EditCommand> makePrefabSyncCommand(Scene& scene, const std::string& asset, const Scene& prefab);
// Devuelve un componente (o todos, con component vacío) de una entidad de instancia al valor del prefab.
[[nodiscard]] std::optional<EditCommand> makeRevertCommand(const Scene& scene, EntityId entity, const Scene& prefab,
                                                          const std::string& component);
// Raíz de la instancia a la que pertenece la entidad (0 si no es de ningún prefab).
[[nodiscard]] EntityId prefabInstanceRoot(const Scene& scene, EntityId entity);
// Entidad del prefab que corresponde a una de instancia.
[[nodiscard]] const Entity* prefabSource(const Scene& prefab, const Entity& instance);

}
