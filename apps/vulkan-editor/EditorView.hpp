#pragma once

#include "ge3d/Audio.hpp"
#include "ge3d/History.hpp"
#include "ge3d/Physics.hpp"
#include "ge3d/Materials.hpp"
#include "ge3d/Scene.hpp"
#include "ge3d/ScriptHost.hpp"
#include <glm/glm.hpp>
#include <imgui.h>

#include <cstdint>
#include <filesystem>
#include <array>
#include <map>
#include <memory>
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

    // Estilo pixel art de la vista: lo aplican el shader de la escena (luz) y la pasada de post (contornos).
    struct RenderStyle {
        int pixelSize = 3;              // píxeles de pantalla por píxel de la escena (1 = sin pixelar)
        bool outlines = true;
        float outlineDarken = 0.45f;    // cuánto oscurece la silueta
        float edgeLighten = 0.30f;      // cuánto aclara las aristas interiores
        float outlineThreshold = 0.10f; // diferencia relativa de profundidad para la silueta
        float edgeThreshold = 0.30f;    // cambio de normal para una arista
        int lightBands = 3;             // escalones de luz (0 = luz continua)
        int colorLevels = 0;            // niveles por canal (0 = todos los colores)
        float colorDither = 0.7f;
        bool snapCamera = true;         // la cámara avanza de píxel en píxel: sin parpadeo al desplazarse
    };
    // Cámara y tamaño con los que dibujar la escena este fotograma.
    struct SceneView {
        glm::mat4 viewProj{1.0f};
        glm::uvec2 extent{322, 182};    // resolución interna con 1 píxel de margen por lado
        float nearPlane{};
        float farPlane{};
    };

    explicit EditorView(float uiScale = 1.0f, std::filesystem::path initialScene = {});
    ~EditorView();
    void draw(ImTextureID sceneTexture);
    [[nodiscard]] SceneView sceneView() const;
    [[nodiscard]] glm::vec3 cameraPosition() const;
    // Jugador en primera persona: la ventana captura el ratón y pasa su movimiento al editor.
    [[nodiscard]] bool wantsMouseCapture() const { return mouseCaptured_; }
    void setCapturedMouse(float dx, float dy, bool click) {
        capturedDelta_ += glm::vec2{dx, dy};
        capturedClick_ = capturedClick_ || click;
    }
    void releaseMouse() { mouseCaptured_ = false; }
    [[nodiscard]] const RenderStyle& renderStyle() const { return style_; }
    // Tamaño que la vista necesita (puede superar la capacidad actual de las imágenes).
    [[nodiscard]] glm::uvec2 requestedSceneExtent() const { return requestedExtent_; }
    // Tamaño real de las imágenes de la escena (para las coordenadas de textura de la vista).
    void setSceneCapacity(glm::uvec2 capacity) { capacity_ = capacity; }
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
    enum class TerrainBrush { add, remove, paint, flatten, smooth, water };
    struct Hit {
        ge3d::EntityId entity{};
        std::uint32_t face{};       // pieza: número de cara; terreno: normal 0-5 (-X +X -Y +Y -Z +Z)
        float distance{};
        glm::vec3 point{};
        glm::vec3 normal{};
        bool terrain{};             // terreno de vóxeles o de malla
        bool heightmap{};           // terreno de malla
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
    // Ajuste de la cámara a la rejilla de píxeles: desplazamiento del mundo y resto en píxeles.
    struct CameraSnap {
        glm::vec3 offset{0.0f};
        glm::vec2 subpixel{0.0f};
    };
    [[nodiscard]] CameraSnap cameraSnap() const;
    [[nodiscard]] Ray mouseRay(ImVec2 mouse, ImVec2 origin, ImVec2 size) const;
    [[nodiscard]] std::optional<Hit> raycast(ImVec2 mouse, ImVec2 origin, ImVec2 size) const;
    [[nodiscard]] std::optional<Hit> raycastTerrain(const ge3d::Entity& entity, const Ray& ray) const;
    [[nodiscard]] std::optional<Hit> raycastHeightmap(const ge3d::Entity& entity, const Ray& ray) const;
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
    void createHeightmap();
    void drawTerrainComponent(ge3d::Entity& entity, const ge3d::Entity& frameStart);
    void drawHeightmapComponent(ge3d::Entity& entity, const ge3d::Entity& frameStart);
    void applyHeightBrush(const Hit& hit, float seconds);
    // Agua: simulación por terreno (caudales y chunks despiertos), no se guarda.
    ge3d::WaterSimulation& waterSimulation(ge3d::EntityId entity) { return water_[entity]; }
    void simulateWater(float seconds);
    void editWater(ge3d::Entity& entity, const char* label, bool fill, float level);
    void drawStylePopup();
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

    // Scripts (EditorScripts.cpp): pestaña Código, componente Script y ejecución en Play.
    struct CodeDocument {
        std::string path;
        std::string text;
        std::string saved;
        std::vector<ge3d::ScriptDiagnostic> diagnostics;
        double editedAt = -1.0;       // última edición sin comprobar (se compila al pararse de escribir)
        int jumpLine = 0;             // línea a la que llevar el cursor
        int cursorLine = 0;
        bool scrollToCursor = false;
        [[nodiscard]] bool dirty() const { return text != saved; }
    };
    struct ScriptCacheEntry {
        std::filesystem::file_time_type time;
        ge3d::ScriptInfo info;
    };
    void drawCode();
    void drawCodeDocument(CodeDocument& document);
    void drawScriptApi();
    void openScript(const std::string& path, int line = 0);
    bool saveScript(CodeDocument& document);
    void createScript(const std::string& name);
    [[nodiscard]] const ge3d::ScriptInfo* scriptInfo(const std::string& path);
    void watchScripts(bool force = false);
    void drawScriptComponent(ge3d::Entity& entity, const ge3d::Entity& frameStart);
    [[nodiscard]] ge3d::ScriptInput gatherInput() const;
    [[nodiscard]] std::optional<std::string> readScript(const std::string& path) const;
    [[nodiscard]] std::filesystem::path scriptFile(const std::string& path) const;

    // Play (EditorPlay.cpp): física, audio y jugador en primera persona.
    enum class Special { player, zone, sound };
    void startPlaySystems();
    void stopPlaySystems();
    void updatePlay(float dt);
    void interactAhead();
    void drawPlayOverlay(ImDrawList* draw, ImVec2 origin, ImVec2 size);
    void createSpecial(Special kind);
    [[nodiscard]] glm::vec3 fpForward() const;
    bool editFloat(const char* label, float& value, float speed, float minimum, float maximum, const char* format,
                   const ge3d::Entity& frameStart, float labelWidth);
    bool editBool(const char* label, bool& value, const ge3d::Entity& frameStart, float labelWidth);
    void drawBodyComponent(ge3d::Entity& entity, const ge3d::Entity& frameStart);
    void drawAudioComponent(ge3d::Entity& entity, const ge3d::Entity& frameStart);
    void drawPlayerComponent(ge3d::Entity& entity, const ge3d::Entity& frameStart);

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
    // Selección múltiple como en Unity. selected_ es el objeto activo (el que muestra el Inspector).
    enum class SelectMode { replace, toggle, add, range };
    [[nodiscard]] bool isSelected(ge3d::EntityId id) const;
    void select(ge3d::EntityId id, SelectMode mode = SelectMode::replace);
    void clearSelection();
    void selectAll();
    void copySelection();
    void pasteClipboard();
    void setActive(bool active);
    void flyCamera();
    void drawSelectionOutline(ImDrawList* draw, ImVec2 origin, ImVec2 size);
    void applyComponentAction(ge3d::Entity& entity, const ge3d::Entity& frameStart, int kind, int action);
    [[nodiscard]] bool editInProgress() const {
        return !gizmoEdit_.empty() || inspectorEdit_.has_value() || !paintStroke_.empty() || building_ || erasing_ ||
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
    [[nodiscard]] const ProjectNode* findProjectNode(const ProjectNode& node, const std::filesystem::path& path) const;
    [[nodiscard]] std::string assetPath(const std::filesystem::path& path) const;
    // Arrastrar un recurso del Proyecto sobre un objeto: material (a toda la pieza o, con face, a esa cara),
    // script (componente Script) o sonido (Fuente de audio).
    void applyAsset(const std::string& asset, ge3d::EntityId entity, std::optional<std::uint32_t> face);

    // Preferencias del editor (EditorSettings.cpp), en <proyecto>/UserSettings.
    [[nodiscard]] std::filesystem::path settingsFolder() const;
    void loadSettings();
    void saveSettings();
    void addRecentScene(const std::filesystem::path& path);
    void rememberCamera();
    void restoreCamera();

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
    std::vector<ge3d::Entity> gizmoEdit_;          // todo lo que mueve el gizmo (selección múltiple)
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
    int terrainRadius_ = 5;
    float brushStrength_ = 0.5f;                  // terreno de malla: velocidad del pincel (0-1)
    int paintLayer_ = 0;                          // terreno de malla: capa que pinta el pincel
    float flattenLevel_ = 0.0f;                   // terreno de malla: altura local donde empezó Aplanar
    std::map<int, ge3d::Heightmap::Patch> heightBefore_;
    std::vector<float> waterBefore_;              // agua de todo el terreno al empezar el trazo (fluye fuera del pincel)
    std::map<ge3d::EntityId, ge3d::WaterSimulation> water_;
    ge3d::HeightmapSettings newHeightmap_{};
    ge3d::HeightmapSettings heightDraft_{};
    ge3d::EntityId heightDraftOwner_{};
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
    bool showCode_ = true;
    int focusSceneFrames_ = 0;
    bool codeFocused_ = false;                    // Ctrl+S guarda el script, no la escena                    // fotogramas en los que traer la Escena al frente

    std::unique_ptr<ge3d::ScriptHost> scripts_;
    std::unique_ptr<ge3d::PhysicsWorld> physics_;
    std::unique_ptr<ge3d::AudioSystem> audio_;
    bool firstPerson_ = false;
    bool mouseCaptured_ = false;
    glm::vec3 fpEye_{0.0f};
    float fpYaw_ = 0.0f;
    float fpPitch_ = 0.0f;
    glm::vec2 capturedDelta_{0.0f};
    bool capturedClick_ = false;
    float footstepTimer_ = 0.0f;
    std::vector<std::string> soundList_;
    std::vector<CodeDocument> documents_;
    int selectDocument_ = -1;                     // pestaña a seleccionar en el siguiente fotograma
    int closingDocument_ = -1;                    // pestaña con cambios que se pidió cerrar
    std::map<std::string, ScriptCacheEntry> scriptCache_;
    std::map<std::string, std::filesystem::file_time_type> scriptTimes_;
    std::vector<std::string> scriptList_;
    double lastScriptWatch_ = -10.0;
    ImFont* codeFont_ = nullptr;
    char newScriptName_[64]{};

    // Preferencias recordadas entre sesiones.
    std::string iniPath_;
    std::filesystem::path lastScene_;
    std::vector<std::filesystem::path> recentScenes_;
    std::map<std::string, std::array<float, 6>> cameras_;   // escena -> pivote, yaw, pitch, distancia
    bool clearOnPlay_ = false;
    bool pauseOnError_ = false;
    int lastErrorCount_ = 0;

    // Panel Proyecto en dos columnas.
    std::filesystem::path projectFolder_;
    bool projectMaterials_ = false;               // carpeta virtual con el catálogo de materiales
    char projectFilter_[64]{};
    float projectTile_ = 64.0f;
    std::string projectSelected_;
    std::filesystem::path projectRename_;
    char projectRenameBuffer_[128]{};
    std::filesystem::path projectDelete_;
    bool showInfo_ = true;
    bool showWarnings_ = true;
    bool showErrors_ = true;
    std::size_t consoleRevision_ = 0;

    RenderStyle style_;
    glm::uvec2 sceneSize_{320, 180};      // resolución interna visible (sin margen)
    glm::uvec2 requestedExtent_{322, 182};
    glm::uvec2 capacity_{1280, 768};
    float aspect_ = 320.0f / 180.0f;

    ge3d::EntityId selected_{};
    std::vector<ge3d::EntityId> selection_;
    ge3d::EntityId rangeAnchor_{};
    std::vector<ge3d::Entity> clipboard_;
    ge3d::EntityId renaming_{};                   // entidad que se renombra en la Jerarquía (F2)
    bool startRename_ = false;
    char renameBuffer_[128]{};
    bool marquee_ = false;                        // rectángulo de selección en la vista
    ImVec2 marqueeStart_{};
    bool flying_ = false;                         // clic derecho: mirar y volar con WASD
    float flySpeed_ = 8.0f;
    double flySpeedShown_ = -10.0;
    int componentClipboardKind_ = 0;              // Copiar / Pegar valores de un componente
    ge3d::Entity componentClipboard_{};
    char addComponentFilter_[64]{};
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
