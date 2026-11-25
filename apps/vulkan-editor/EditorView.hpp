#pragma once

#include "ge3d/History.hpp"
#include "ge3d/Materials.hpp"
#include "ge3d/Scene.hpp"
#include <glm/glm.hpp>
#include <imgui.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

class EditorView {
public:
    // Dónde y a qué escala dibujar la cuadrícula del suelo este fotograma.
    struct GridView {
        glm::vec2 offset;   // origen en el mundo (x, z), múltiplo de spacing
        glm::vec2 focus;    // punto que mira la cámara (x, z): centro del desvanecimiento
        float spacing;      // metros entre líneas: 1, 10 o 100 según el zoom
        float fadeRadius;   // metros hasta los que se ve la cuadrícula
    };

    explicit EditorView(float uiScale = 1.0f);
    void draw(ImTextureID sceneTexture);
    [[nodiscard]] glm::mat4 renderCamera() const;
    [[nodiscard]] const ge3d::Scene& scene() const { return scene_; }
    [[nodiscard]] bool showGrid() const { return showGrid_; }
    [[nodiscard]] GridView gridView() const;

    // Catálogo de materiales y sus miniaturas (una por material, en el mismo orden).
    void setMaterials(const ge3d::MaterialLibrary& library, const std::vector<ImTextureID>& thumbnails);
    // Estadísticas del renderer de terreno para el Inspector y la barra de estado.
    struct TerrainViewStats {
        int terrains{};
        int chunks{};
        int visibleChunks{};
        int pendingChunks{};
        std::size_t quads{};
        std::size_t visibleQuads{};
        std::size_t gpuBytes{};
    };
    void setTerrainStats(const TerrainViewStats& stats) { terrainStats_ = stats; }
    [[nodiscard]] bool isPlaying() const { return playSnapshot_.has_value(); }
    [[nodiscard]] bool hasUnsavedChanges() const { return history_.state() != savedState_; }
    [[nodiscard]] std::string windowTitle() const;
    // Cierre de la ventana: si hay cambios sin guardar, el editor pregunta antes de aprobarlo.
    void requestQuit();
    [[nodiscard]] bool quitApproved() const { return quitApproved_; }
    static glm::mat4 modelMatrix(const ge3d::Transform& transform);

private:
    enum class Tool { hand, move, rotate, scale, paint, build, terrain };
    enum class TerrainBrush { add, remove, paint, flatten };
    struct Hit {
        ge3d::EntityId entity{};
        std::uint32_t face{};       // pieza: número de cara; terreno: normal 0-5 (-X +X -Y +Y -Z +Z)
        float distance{};
        glm::vec3 point{};
        glm::vec3 normal{};
        bool terrain{};
        glm::ivec3 voxel{};         // terreno: vóxel tocado
    };
    struct Ray {
        glm::vec3 start;
        glm::vec3 direction;
    };
    // Celda de la cuadrícula de construcción, en múltiplos de buildCell_.
    using Cell = std::tuple<int, int, int>;
    enum class SceneChange { none, newScene, open, reload, quit };
    enum class PathPopup { none, open, saveAs };

    struct ProjectNode {
        std::string name;
        std::filesystem::path path;
        bool directory{};
        std::vector<ProjectNode> children;
    };

    [[nodiscard]] glm::mat4 view() const;
    [[nodiscard]] glm::mat4 projection() const;
    [[nodiscard]] Ray mouseRay(ImVec2 mouse, ImVec2 origin, ImVec2 size) const;
    [[nodiscard]] std::optional<Hit> raycast(ImVec2 mouse, ImVec2 origin, ImVec2 size) const;
    [[nodiscard]] std::optional<Hit> raycastTerrain(const ge3d::Entity& entity, const Ray& ray) const;
    [[nodiscard]] static glm::mat4 terrainMatrix(const ge3d::Entity& entity);
    void pick(ImVec2 mouse, ImVec2 origin, ImVec2 size);
    // Dibuja el contorno (y opcionalmente el relleno) de una pieza proyectada en la vista.
    // face < 0 dibuja la pieza entera; si no, solo esa cara.
    void drawPieceOutline(ImDrawList* draw, ge3d::Primitive primitive, const glm::mat4& model, ImVec2 origin,
                          ImVec2 size, ImU32 line, ImU32 fill, int face = -1) const;
    void paintAt(const Hit& hit, bool wholePiece);
    void finishPaintStroke();

    // Construcción
    [[nodiscard]] std::optional<Cell> buildCellAt(ImVec2 mouse, ImVec2 origin, ImVec2 size) const;
    [[nodiscard]] ge3d::Transform cellTransform(const Cell& cell) const;
    [[nodiscard]] bool cellOccupied(const Cell& cell) const;
    [[nodiscard]] std::vector<Cell> pendingCells() const;
    void handleBuildTool(ImDrawList* draw, ImVec2 origin, ImVec2 size, bool canUseMouse);
    void finishBuildStroke();
    void finishEraseStroke();

    // Terreno
    void createTerrain();
    void drawTerrainComponent(ge3d::Entity& entity, const ge3d::Entity& frameStart);
    void handleTerrainTool(ImDrawList* draw, ImVec2 origin, ImVec2 size, bool canUseMouse);
    void applyTerrainBrush(const Hit& hit);
    void finishSculpt();
    // Materiales en la interfaz
    bool materialSwatch(const char* id, const ge3d::MaterialId& material, bool selected, float size);
    bool materialPicker(ge3d::MaterialId& material, float size, int perRow);
    void materialStrip();
    [[nodiscard]] const char* materialName(const ge3d::MaterialId& material) const;
    void focusSelection();
    void setViewFromMatrix(const glm::mat4& view);

    // Paneles
    void drawMenuBar();
    void drawToolbar();
    void drawStatusBar();
    void drawHierarchy();
    void drawInspector();
    void drawViewport(ImTextureID sceneTexture);
    void drawProject();
    void drawConsole();
    void drawPathPopup();
    void drawUnsavedPopup();
    void drawShortcutsPopup();
    void handleShortcuts();
    void buildDefaultLayout(ImGuiID dock);

    // Edición
    [[nodiscard]] ge3d::History& activeHistory() { return isPlaying() ? playHistory_ : history_; }
    void execute(ge3d::EditCommand command);
    void commitModification(std::optional<ge3d::Entity>& before, const char* label);
    void createPrimitive(ge3d::Primitive primitive);
    void duplicateSelection();
    void deleteSelection();
    void undo();
    void redo();
    void validateSelection();
    [[nodiscard]] bool editInProgress() const {
        return gizmoEdit_.has_value() || inspectorEdit_.has_value() || !paintStroke_.empty() || building_ || erasing_ ||
               sculpting_;
    }

    // Escena y archivos
    void requestSceneChange(SceneChange change, std::filesystem::path path = {});
    void performSceneChange();
    void newScene();
    bool saveScene();
    void loadScene();
    void refreshProject();
    void drawProjectNode(const ProjectNode& node, bool root);

    // Modo Play: se juega sobre una copia de la escena que se descarta al detener.
    void togglePlay();
    void togglePause();
    void step();

    void log(std::string message, bool error = false);

    static constexpr std::uint64_t notSaved = UINT64_MAX;

    float scale_ = 1.0f;
    ImFont* boldFont_ = nullptr;

    ge3d::Scene scene_ = ge3d::makeStarterScene();
    ge3d::History history_;
    std::uint64_t savedState_ = notSaved;
    std::filesystem::path scenePath_;
    std::filesystem::path contentRoot_;
    ProjectNode project_;
    bool projectFound_ = false;

    std::optional<ge3d::Scene> playSnapshot_;
    ge3d::History playHistory_;
    bool paused_ = false;
    std::uint64_t playFrames_ = 0;

    SceneChange pendingChange_ = SceneChange::none;
    std::filesystem::path pendingPath_;
    bool openUnsavedPopup_ = false;
    bool quitApproved_ = false;
    PathPopup pathPopup_ = PathPopup::none;
    bool openShortcutsPopup_ = false;

    // Estado previo de la entidad al empezar un arrastre del gizmo o del Inspector.
    std::optional<ge3d::Entity> gizmoEdit_;
    std::optional<ge3d::Entity> inspectorEdit_;
    // Trazo de pintura en curso: estado previo de cada pieza tocada. Se guarda como una sola operación.
    std::map<ge3d::EntityId, ge3d::Entity> paintStroke_;
    bool painting_ = false;
    ge3d::MaterialId paintMaterial_ = "piedra";   // material activo de Pintar, Construir y Terreno
    ge3d::MaterialLibrary library_;
    std::vector<ImTextureID> thumbnails_;
    TerrainViewStats terrainStats_;

    // Pinceles de terreno. El trazo guarda el contenido previo de cada chunk tocado.
    TerrainBrush terrainBrush_ = TerrainBrush::add;
    int terrainRadius_ = 3;
    bool sculpting_ = false;
    ge3d::EntityId sculptTarget_{};
    int flattenHeight_ = 0;
    std::map<int, std::vector<ge3d::VoxelGrid::Voxel>> sculptBefore_;
    double lastSculptTime_ = 0.0;
    ge3d::TerrainSettings newTerrain_{};          // configuración de "Crear terreno"
    ge3d::TerrainSettings terrainDraft_{};        // ajustes del Inspector aún sin aplicar
    ge3d::EntityId draftOwner_{};

    // Pincel de construcción. El trazo se acumula y se crea al soltar, como una sola operación.
    ge3d::Primitive buildShape_ = ge3d::Primitive::box;
    int buildRotation_ = 0;          // giros de 90 grados alrededor de Y
    float buildCell_ = 1.0f;         // metros por celda
    bool building_ = false;
    bool buildRect_ = false;         // Shift al empezar: rellenar un rectángulo
    Cell buildStart_{};
    Cell buildCurrent_{};
    std::set<Cell> buildStroke_;
    bool erasing_ = false;
    std::set<ge3d::EntityId> eraseStroke_;
    char nameBuffer_[128]{};
    bool nameEditing_ = false;
    char pathBuffer_[512]{};
    char hierarchyFilter_[64]{};
    char consoleFilter_[64]{};

    bool showHierarchy_ = true;
    bool showInspector_ = true;
    bool showProject_ = true;
    bool showConsole_ = true;
    bool showInfo_ = true;
    bool showWarnings_ = true;
    bool showErrors_ = true;
    std::size_t consoleRevision_ = 0;

    ge3d::EntityId selected_{};
    glm::vec3 pivot_{0.0f, 0.35f, 0.0f};
    float yaw_ = 0.75f;
    float pitch_ = 0.48f;
    float distance_ = 7.0f;
    Tool tool_ = Tool::move;
    bool localAxes_ = false;
    bool snap_ = false;
    bool showGrid_ = true;
    bool orbiting_ = false;
    bool panning_ = false;
    bool layoutCreated_ = false;
};
