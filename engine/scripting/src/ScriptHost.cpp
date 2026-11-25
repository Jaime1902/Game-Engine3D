#include "ge3d/ScriptHost.hpp"

#include "lua.h"
#include "luacode.h"
#include "lualib.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <map>
#include <string_view>

namespace ge3d {

namespace {

using Clock = std::chrono::steady_clock;
constexpr const char* entityType = "ge3d.Entity";
constexpr std::size_t maxMessagesPerFrame = 1024;
constexpr const char* callbackNames[] = {"onStart", "onUpdate", "onInteract", "onMessage", "onTriggerEnter", "onTriggerExit",
                                         "onCollision", "onStop"};

// "scripts/puerta.luau:12: mensaje" o ":12: mensaje" -> línea 12 y "mensaje".
ScriptDiagnostic parseDiagnostic(const std::string& path, const std::string& text) {
    ScriptDiagnostic diagnostic{path, 0, text};
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != ':') continue;
        std::size_t j = i + 1;
        int line = 0;
        while (j < text.size() && std::isdigit(static_cast<unsigned char>(text[j])) && line < 10'000'000) {
            line = line * 10 + (text[j] - '0');
            ++j;
        }
        if (j > i + 1 && j < text.size() && text[j] == ':') {
            diagnostic.line = line;
            diagnostic.message = text.substr(j + 1);
            while (!diagnostic.message.empty() && diagnostic.message.front() == ' ') diagnostic.message.erase(0, 1);
            break;
        }
    }
    return diagnostic;
}

struct Compiled {
    std::string bytecode;
    std::optional<ScriptDiagnostic> error;
};

Compiled compile(const std::string& source, const std::string& path) {
    lua_CompileOptions options{};
    options.optimizationLevel = 1;
    options.debugLevel = 1;   // líneas en los errores y las trazas
    std::size_t size = 0;
    char* data = luau_compile(source.data(), source.size(), &options, &size);
    std::string bytecode(data, size);
    std::free(data);
    if (!bytecode.empty() && bytecode[0] == 0) return {{}, parseDiagnostic(path, bytecode.substr(1))};
    return {std::move(bytecode), std::nullopt};
}

std::optional<ScriptValue> toValue(lua_State* L, int index) {
    switch (lua_type(L, index)) {
    case LUA_TNUMBER: return ScriptValue{lua_tonumber(L, index)};
    case LUA_TBOOLEAN: return ScriptValue{lua_toboolean(L, index) != 0};
    case LUA_TSTRING: return ScriptValue{std::string(lua_tostring(L, index))};
    default: return std::nullopt;
    }
}

void pushValue(lua_State* L, const std::optional<ScriptValue>& value) {
    if (!value) lua_pushnil(L);
    else if (const auto* number = std::get_if<double>(&*value)) lua_pushnumber(L, *number);
    else if (const auto* flag = std::get_if<bool>(&*value)) lua_pushboolean(L, *flag);
    else lua_pushstring(L, std::get<std::string>(*value).c_str());
}

}

// --- Estado de la VM -----------------------------------------------------------------------------

struct ScriptHost::Impl {
    SourceLoader loader;
    LogSink log;
    lua_State* L{};
    Scene* scene{};
    std::size_t memory{};
    Clock::time_point deadline{};
    bool armed{};
    bool timedOut{};
    Clock::time_point started{};
    ScriptInput input;

    struct Module {
        int ref{LUA_NOREF};
    };
    std::map<std::string, Module> modules;
    std::map<std::string, std::string> failedModules;   // ruta -> error ya mostrado

    struct Instance {
        EntityId entity{};
        std::string path;
        std::string name;
        int self{LUA_NOREF};
        int module{LUA_NOREF};
        bool disabled{};
    };
    std::vector<Instance> instances;
    const Instance* current{};

    struct Message {
        EntityId target{};
        EntityId sender{};
        std::string event;
        std::optional<ScriptValue> value;
    };
    std::vector<Message> messages;
    ScriptServices services;
    std::size_t nextInstance{};
    double frameMs{};

    void report(bool error, const std::string& message) const {
        if (log) log(error, message);
    }
    void arm(double budgetMs) {
        timedOut = false;
        deadline = Clock::now() + std::chrono::microseconds(static_cast<long long>(budgetMs * 1000.0));
        armed = true;
    }
    bool open();
    void close();
    bool callProtected(lua_State* S, int args, int results, double budgetMs, std::string& error);
    std::optional<int> loadModule(const std::string& path, std::vector<ScriptDiagnostic>* diagnostics);
    std::optional<int> moduleFor(const std::string& path);
    void createInstance(const Entity& entity);
    bool call(Instance& instance, const char* callback, int args);
    void disable(Instance& instance, const char* callback, const std::string& error);
};

namespace {

ScriptHost::Impl* implOf(lua_State* L) {
    return static_cast<ScriptHost::Impl*>(lua_callbacks(L)->userdata);
}

void* allocate(void* userdata, void* pointer, std::size_t oldSize, std::size_t newSize) {
    auto* impl = static_cast<ScriptHost::Impl*>(userdata);
    if (!pointer) oldSize = 0;
    if (newSize == 0) {
        std::free(pointer);
        impl->memory -= oldSize;
        return nullptr;
    }
    // Límite de memoria del entorno de scripts: al superarlo, Luau lanza un error de memoria.
    if (newSize > oldSize && impl->memory - oldSize + newSize > ScriptHost::memoryLimit) return nullptr;
    void* result = std::realloc(pointer, newSize);
    if (result) impl->memory = impl->memory - oldSize + newSize;
    return result;
}

// Interrupción cooperativa: Luau la llama en los saltos de bucle y las llamadas.
void interrupt(lua_State* L, int gc) {
    if (gc >= 0) return;
    auto* impl = implOf(L);
    if (!impl || !impl->armed || Clock::now() < impl->deadline) return;
    impl->armed = false;
    impl->timedOut = true;
    lua_rawcheckstack(L, 1);
    luaL_error(L, "el script supero su presupuesto de tiempo (un bucle sin fin?)");
}

int traceback(lua_State* L) {
    std::string message = lua_isstring(L, 1) ? lua_tostring(L, 1) : "error sin mensaje";
    if (const char* trace = lua_debugtrace(L); trace && *trace) {
        message += "\n";
        message += trace;
    }
    lua_pushstring(L, message.c_str());
    return 1;
}

// --- Entity ------------------------------------------------------------------------------------

struct EntityRef {
    EntityId id;
};

void pushEntity(lua_State* L, EntityId id) {
    auto* ref = static_cast<EntityRef*>(lua_newuserdata(L, sizeof(EntityRef)));
    ref->id = id;
    luaL_getmetatable(L, entityType);
    lua_setmetatable(L, -2);
}

Entity& checkEntity(lua_State* L, int index) {
    const auto* ref = static_cast<EntityRef*>(luaL_checkudata(L, index, entityType));
    auto* impl = implOf(L);
    Entity* entity = impl && impl->scene ? impl->scene->find(ref->id) : nullptr;
    if (!entity) luaL_error(L, "la entidad %llu ya no existe", static_cast<unsigned long long>(ref->id));
    return *entity;
}

Vec3 checkVector(lua_State* L, int index, const char* field) {
    const float* v = lua_tovector(L, index);
    if (!v) luaL_error(L, "%s debe ser un vector (usa vector.create(x, y, z))", field);
    return {v[0], v[1], v[2]};
}

void queueMessage(lua_State* L, EntityId target, int eventIndex, int valueIndex) {
    auto* impl = implOf(L);
    if (impl->messages.size() >= maxMessagesPerFrame) luaL_error(L, "demasiados mensajes en un fotograma");
    const char* event = luaL_checkstring(L, eventIndex);
    std::optional<ScriptValue> value;
    if (!lua_isnoneornil(L, valueIndex)) {
        value = toValue(L, valueIndex);
        if (!value) luaL_error(L, "el valor de un mensaje debe ser numero, booleano o texto");
    }
    impl->messages.push_back({target, impl->current ? impl->current->entity : EntityId{}, event, std::move(value)});
}

int entitySend(lua_State* L) {
    const auto& entity = checkEntity(L, 1);
    queueMessage(L, entity.id, 2, 3);
    return 0;
}

int entityApplyImpulse(lua_State* L) {
    const auto& entity = checkEntity(L, 1);
    const auto impulse = checkVector(L, 2, "el impulso");
    if (auto* impl = implOf(L); impl->services.applyImpulse) impl->services.applyImpulse(entity.id, impulse);
    return 0;
}

int entityPlaySound(lua_State* L) {
    const auto& entity = checkEntity(L, 1);
    const std::string clip = luaL_checkstring(L, 2);
    const double volume = luaL_optnumber(L, 3, 1.0);
    if (auto* impl = implOf(L); impl->services.playSound) {
        impl->services.playSound(clip, static_cast<float>(volume), entity.transform.position);
    }
    return 0;
}

int entityPlayAudio(lua_State* L) {
    const auto& entity = checkEntity(L, 1);
    if (auto* impl = implOf(L); impl->services.emitter) impl->services.emitter(entity.id, true);
    return 0;
}

int entityStopAudio(lua_State* L) {
    const auto& entity = checkEntity(L, 1);
    if (auto* impl = implOf(L); impl->services.emitter) impl->services.emitter(entity.id, false);
    return 0;
}

int entityIndex(lua_State* L) {
    auto& entity = checkEntity(L, 1);
    const std::string key = luaL_checkstring(L, 2);
    const auto push = [&](const Vec3& v) { lua_pushvector(L, v.x, v.y, v.z); };
    if (key == "position") push(entity.transform.position);
    else if (key == "rotation") push(entity.transform.rotationDegrees);
    else if (key == "scale") push(entity.transform.scale);
    else if (key == "name") lua_pushstring(L, entity.name.c_str());
    else if (key == "id") lua_pushnumber(L, static_cast<double>(entity.id));
    else if (key == "send") lua_pushcfunction(L, entitySend, "send");
    else if (key == "velocity") {
        auto* impl = implOf(L);
        const auto v = impl->services.velocity ? impl->services.velocity(entity.id) : std::nullopt;
        if (v) push(*v);
        else lua_pushvector(L, 0, 0, 0);
    } else if (key == "applyImpulse") lua_pushcfunction(L, entityApplyImpulse, "applyImpulse");
    else if (key == "playSound") lua_pushcfunction(L, entityPlaySound, "playSound");
    else if (key == "playAudio") lua_pushcfunction(L, entityPlayAudio, "playAudio");
    else if (key == "stopAudio") lua_pushcfunction(L, entityStopAudio, "stopAudio");
    else luaL_error(L, "Entity no tiene el campo '%s' (position, rotation, scale, velocity, name, id, send, applyImpulse, "
                       "playSound, playAudio, stopAudio)", key.c_str());
    return 1;
}

int entityNewIndex(lua_State* L) {
    auto& entity = checkEntity(L, 1);
    const std::string key = luaL_checkstring(L, 2);
    if (key == "position") entity.transform.position = checkVector(L, 3, "position");
    else if (key == "rotation") entity.transform.rotationDegrees = checkVector(L, 3, "rotation");
    else if (key == "scale") {
        const auto scale = checkVector(L, 3, "scale");
        if (!(scale.x > 0 && scale.y > 0 && scale.z > 0)) luaL_error(L, "la escala debe ser mayor que cero");
        entity.transform.scale = scale;
    } else if (key == "velocity") {
        const auto v = checkVector(L, 3, "velocity");
        if (auto* impl = implOf(L); impl->services.setVelocity) impl->services.setVelocity(entity.id, v);
    } else if (key == "name") entity.name = luaL_checkstring(L, 3);
    else luaL_error(L, "no se puede cambiar '%s' de una Entity", key.c_str());
    return 0;
}

int entityToString(lua_State* L) {
    const auto* ref = static_cast<EntityRef*>(luaL_checkudata(L, 1, entityType));
    auto* impl = implOf(L);
    const Entity* entity = impl && impl->scene ? impl->scene->find(ref->id) : nullptr;
    lua_pushstring(L, ("Entity(" + (entity ? entity->name : std::string("eliminada")) + ")").c_str());
    return 1;
}

int entityEquals(lua_State* L) {
    const auto* a = static_cast<EntityRef*>(luaL_checkudata(L, 1, entityType));
    const auto* b = static_cast<EntityRef*>(luaL_checkudata(L, 2, entityType));
    lua_pushboolean(L, a->id == b->id);
    return 1;
}

// --- Globales ----------------------------------------------------------------------------------

int logMessage(lua_State* L) {
    std::string text;
    const int count = lua_gettop(L);
    for (int i = 1; i <= count; ++i) {
        if (i > 1) text += ' ';
        std::size_t length = 0;
        const char* part = luaL_tolstring(L, i, &length);
        text.append(part, length);
        lua_pop(L, 1);
    }
    auto* impl = implOf(L);
    if (impl->current) text = "[" + impl->current->name + "] " + text;
    impl->report(false, text);
    return 0;
}

const Entity* findByName(lua_State* L, const char* name) {
    auto* impl = implOf(L);
    if (!impl->scene) return nullptr;
    const auto& entities = impl->scene->entities();
    const auto it = std::find_if(entities.begin(), entities.end(), [&](const Entity& e) { return e.name == name; });
    return it == entities.end() ? nullptr : &*it;
}

int sceneFind(lua_State* L) {
    if (const auto* entity = findByName(L, luaL_checkstring(L, 1))) pushEntity(L, entity->id);
    else lua_pushnil(L);
    return 1;
}

int sceneSend(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const auto* entity = findByName(L, name);
    if (!entity) luaL_error(L, "no hay ninguna entidad llamada '%s'", name);
    queueMessage(L, entity->id, 2, 3);
    return 0;
}

int sceneTime(lua_State* L) {
    auto* impl = implOf(L);
    lua_pushnumber(L, std::chrono::duration<double>(Clock::now() - impl->started).count());
    return 1;
}

int scenePlayer(lua_State* L) {
    auto* impl = implOf(L);
    const EntityId id = impl->services.player ? impl->services.player() : EntityId{};
    if (id && impl->scene && impl->scene->find(id)) pushEntity(L, id);
    else lua_pushnil(L);
    return 1;
}

int audioPlay(lua_State* L) {
    const std::string clip = luaL_checkstring(L, 1);
    const double volume = luaL_optnumber(L, 2, 1.0);
    if (auto* impl = implOf(L); impl->services.playSound) impl->services.playSound(clip, static_cast<float>(volume), std::nullopt);
    return 0;
}

int inputCheck(lua_State* L, bool pressed) {
    const std::string key = luaL_checkstring(L, 1);
    const auto& list = pressed ? implOf(L)->input.pressed : implOf(L)->input.down;
    lua_pushboolean(L, std::find(list.begin(), list.end(), key) != list.end());
    return 1;
}
int inputIsDown(lua_State* L) { return inputCheck(L, false); }
int inputWasPressed(lua_State* L) { return inputCheck(L, true); }

void setFunction(lua_State* L, const char* name, lua_CFunction function) {
    lua_pushcfunction(L, function, name);
    lua_setfield(L, -2, name);
}

}

bool ScriptHost::Impl::open() {
    memory = 0;
    L = lua_newstate(allocate, this);
    if (!L) return false;
    lua_callbacks(L)->userdata = this;
    lua_callbacks(L)->interrupt = interrupt;
    luaL_openlibs(L);
    // Sin depuración ni carga de código en tiempo de ejecución (Luau ya no trae io ni os.execute).
    for (const char* name : {"debug", "loadstring", "getfenv", "setfenv"}) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }
    lua_pushcfunction(L, logMessage, "log");
    lua_setglobal(L, "log");
    lua_pushcfunction(L, logMessage, "print");
    lua_setglobal(L, "print");

    luaL_newmetatable(L, entityType);
    setFunction(L, "__index", entityIndex);
    setFunction(L, "__newindex", entityNewIndex);
    setFunction(L, "__tostring", entityToString);
    setFunction(L, "__eq", entityEquals);
    lua_pushstring(L, "Entity");
    lua_setfield(L, -2, "__type");
    lua_pop(L, 1);

    lua_newtable(L);
    setFunction(L, "find", sceneFind);
    setFunction(L, "send", sceneSend);
    setFunction(L, "time", sceneTime);
    setFunction(L, "player", scenePlayer);
    lua_setglobal(L, "scene");
    lua_newtable(L);
    setFunction(L, "play", audioPlay);
    lua_setglobal(L, "audio");
    lua_newtable(L);
    setFunction(L, "isDown", inputIsDown);
    setFunction(L, "wasPressed", inputWasPressed);
    lua_setglobal(L, "input");

    // Las bibliotecas y globales del motor quedan de solo lectura; cada script tiene sus propias globales.
    luaL_sandbox(L);
    started = Clock::now();
    return true;
}

void ScriptHost::Impl::close() {
    if (L) lua_close(L);
    L = nullptr;
    modules.clear();
    failedModules.clear();
    instances.clear();
    messages.clear();
    current = nullptr;
    nextInstance = 0;
    memory = 0;
}

bool ScriptHost::Impl::callProtected(lua_State* S, int args, int results, double budgetMs, std::string& error) {
    const int base = lua_gettop(S) - args;
    lua_pushcfunction(S, traceback, "traceback");
    lua_insert(S, base);
    arm(budgetMs);
    const int status = lua_pcall(S, args, results, base);
    armed = false;
    lua_remove(S, base);
    if (status == LUA_OK) return true;
    error = lua_isstring(S, -1) ? lua_tostring(S, -1) : "error sin mensaje";
    if (status == LUA_ERRMEM) error = "el script supero el limite de memoria de los scripts";
    lua_pop(S, 1);
    return false;
}

std::optional<int> ScriptHost::Impl::loadModule(const std::string& path, std::vector<ScriptDiagnostic>* diagnostics) {
    const auto fail = [&](const std::string& message) -> std::optional<int> {
        if (diagnostics) diagnostics->push_back(parseDiagnostic(path, message));
        return std::nullopt;
    };
    const auto source = loader ? loader(path) : std::nullopt;
    if (!source) return fail("no se encuentra el script " + path);
    auto compiled = compile(*source, path);
    if (compiled.error) {
        if (diagnostics) diagnostics->push_back(*compiled.error);
        return std::nullopt;
    }
    lua_State* thread = lua_newthread(L);
    luaL_sandboxthread(thread);
    const std::string chunk = "=" + path;
    if (luau_load(thread, chunk.c_str(), compiled.bytecode.data(), compiled.bytecode.size(), 0) != 0) {
        const std::string message = lua_isstring(thread, -1) ? lua_tostring(thread, -1) : "no se pudo cargar";
        lua_pop(L, 1);
        return fail(message);
    }
    std::string error;
    // El cuerpo del script solo declara funciones y parámetros: tiene un presupuesto más holgado.
    if (!callProtected(thread, 0, 1, startBudgetMs, error)) {
        lua_pop(L, 1);
        return fail(error);
    }
    if (!lua_istable(thread, -1)) {
        lua_pop(L, 1);
        return fail(path + ":1: el script debe terminar con 'return' de una tabla (por ejemplo: return Puerta)");
    }
    lua_xmove(thread, L, 1);
    const int ref = lua_ref(L, -1);
    lua_pop(L, 2);   // tabla y hilo
    return ref;
}

std::optional<int> ScriptHost::Impl::moduleFor(const std::string& path) {
    if (const auto it = modules.find(path); it != modules.end()) return it->second.ref;
    if (failedModules.contains(path)) return std::nullopt;
    std::vector<ScriptDiagnostic> diagnostics;
    const auto ref = loadModule(path, &diagnostics);
    if (!ref) {
        std::string message = "Script " + path + ": ";
        for (const auto& d : diagnostics) message += (d.line ? "linea " + std::to_string(d.line) + ": " : "") + d.message;
        failedModules[path] = message;
        report(true, message);
        return std::nullopt;
    }
    modules[path] = {*ref};
    return ref;
}

void ScriptHost::Impl::createInstance(const Entity& entity) {
    const auto& component = *entity.script;
    const auto module = moduleFor(component.path);
    if (!module) return;
    // self: parámetros del script (con los valores del Inspector si son del mismo tipo) y la entidad.
    lua_newtable(L);
    lua_getref(L, *module);
    lua_getfield(L, -1, "params");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            if (lua_type(L, -2) == LUA_TSTRING) {
                const std::string name = lua_tostring(L, -2);
                const auto defaultValue = toValue(L, -1);
                const auto chosen = component.parameters.find(name);
                if (defaultValue && chosen != component.parameters.end() && chosen->second.index() == defaultValue->index()) {
                    pushValue(L, chosen->second);
                } else {
                    lua_pushvalue(L, -1);
                }
                lua_setfield(L, -6, name.c_str());   // self está debajo de: módulo, params, clave, valor
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 2);   // params y módulo
    pushEntity(L, entity.id);
    lua_setfield(L, -2, "entity");
    Instance instance;
    instance.entity = entity.id;
    instance.path = component.path;
    instance.name = entity.name;
    instance.self = lua_ref(L, -1);
    instance.module = *module;
    lua_pop(L, 1);
    instances.push_back(std::move(instance));
    call(instances.back(), "onStart", 0);
}

void ScriptHost::Impl::disable(Instance& instance, const char* callback, const std::string& error) {
    instance.disabled = true;
    if (scene) {
        if (auto* entity = scene->find(instance.entity); entity && entity->script) entity->script->enabled = false;
    }
    report(true, "Script " + instance.path + " (" + instance.name + ") en " + callback + ": " + error +
                     "\nEl script de esta entidad queda desactivado hasta detener Play.");
}

bool ScriptHost::Impl::call(Instance& instance, const char* callback, int args) {
    // Los argumentos ya están en la pila; debajo se colocan la función y self.
    if (instance.disabled || !scene || !scene->find(instance.entity)) {
        lua_pop(L, args);
        return false;
    }
    lua_getref(L, instance.module);
    lua_getfield(L, -1, callback);
    lua_remove(L, -2);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1 + args);
        return false;
    }
    // Orden final: función, self, argumentos.
    lua_insert(L, -1 - args);
    lua_getref(L, instance.self);
    lua_insert(L, -1 - args);
    const Instance* previous = current;
    current = &instance;
    std::string error;
    const double budget = std::string_view(callback) == "onStart" ? startBudgetMs : callBudgetMs;
    const bool ok = callProtected(L, 1 + args, 0, budget, error);
    current = previous;
    if (!ok) disable(instance, callback, error);
    return true;
}

// --- ScriptHost ----------------------------------------------------------------------------------

ScriptHost::ScriptHost(SourceLoader loader, LogSink log) : impl_(std::make_unique<Impl>()) {
    impl_->loader = std::move(loader);
    impl_->log = std::move(log);
}

ScriptHost::~ScriptHost() {
    impl_->close();
}

bool ScriptHost::running() const noexcept {
    return impl_->L != nullptr;
}

void ScriptHost::start(Scene& scene) {
    auto& impl = *impl_;
    impl.close();
    impl.scene = &scene;
    if (!impl.open()) {
        impl.report(true, "Scripts: no se pudo crear la maquina virtual de Luau");
        return;
    }
    impl.instances.reserve(scene.entities().size());
    // Se copian los ids primero: onStart puede cambiar la escena.
    std::vector<EntityId> ids;
    for (const auto& entity : scene.entities()) {
        if (entity.script && entity.script->enabled && !entity.script->path.empty()) ids.push_back(entity.id);
    }
    for (const auto id : ids) {
        if (const auto* entity = scene.find(id)) impl.createInstance(*entity);
    }
}

void ScriptHost::update(Scene& scene, float dt, const ScriptInput& input) {
    auto& impl = *impl_;
    if (!impl.L) return;
    impl.scene = &scene;
    impl.input = input;
    const auto frameStart = Clock::now();
    const auto elapsed = [&] { return std::chrono::duration<double, std::milli>(Clock::now() - frameStart).count(); };
    const std::size_t count = impl.instances.size();
    for (std::size_t k = 0; k < count; ++k) {
        const std::size_t index = (impl.nextInstance + k) % count;
        if (elapsed() > frameBudgetMs) {
            // Presupuesto agotado: el resto empieza el siguiente fotograma.
            impl.nextInstance = index;
            break;
        }
        lua_pushnumber(impl.L, dt);
        impl.call(impl.instances[index], "onUpdate", 1);
    }
    // Mensajes enviados durante este fotograma (los que se envíen al procesarlos, el siguiente).
    auto messages = std::move(impl.messages);
    impl.messages.clear();
    for (const auto& message : messages) {
        for (auto& instance : impl.instances) {
            if (instance.entity != message.target) continue;
            lua_pushstring(impl.L, message.event.c_str());
            pushValue(impl.L, message.value);
            if (message.sender && scene.find(message.sender)) pushEntity(impl.L, message.sender);
            else lua_pushnil(impl.L);
            impl.call(instance, "onMessage", 3);
        }
    }
    lua_gc(impl.L, LUA_GCSTEP, 0);
    impl.frameMs = elapsed();
}

bool ScriptHost::interact(Scene& scene, EntityId entity) {
    auto& impl = *impl_;
    if (!impl.L) return false;
    impl.scene = &scene;
    bool handled = false;
    for (auto& instance : impl.instances) {
        if (instance.entity == entity) handled = impl.call(instance, "onInteract", 0) || handled;
    }
    return handled;
}

void ScriptHost::stop(Scene& scene) {
    auto& impl = *impl_;
    if (impl.L) {
        impl.scene = &scene;
        for (auto& instance : impl.instances) impl.call(instance, "onStop", 0);
    }
    impl.close();
    impl.scene = nullptr;
}

bool ScriptHost::reload(Scene& scene, const std::string& path, std::vector<ScriptDiagnostic>* diagnostics) {
    auto& impl = *impl_;
    if (!impl.L) {
        // Sin Play: solo se comprueba que compila.
        const auto source = impl.loader ? impl.loader(path) : std::nullopt;
        if (!source) return false;
        const auto info = describeScript(*source, path);
        if (diagnostics) *diagnostics = info.diagnostics;
        return info.ok;
    }
    impl.scene = &scene;
    const auto ref = impl.loadModule(path, diagnostics);
    if (!ref) {
        impl.report(true, "Script " + path + ": no se recarga, se mantiene la version anterior");
        return false;
    }
    if (const auto old = impl.modules.find(path); old != impl.modules.end()) lua_unref(impl.L, old->second.ref);
    impl.modules[path] = {*ref};
    impl.failedModules.erase(path);
    // Instancias nuevas con estado privado nuevo y los mismos parámetros del Inspector.
    std::vector<EntityId> recreate;
    for (auto it = impl.instances.begin(); it != impl.instances.end();) {
        if (it->path == path) {
            recreate.push_back(it->entity);
            lua_unref(impl.L, it->self);
            it = impl.instances.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& entity : scene.entities()) {
        if (entity.script && entity.script->path == path && entity.script->enabled &&
            std::find(recreate.begin(), recreate.end(), entity.id) == recreate.end() &&
            std::none_of(impl.instances.begin(), impl.instances.end(), [&](const Impl::Instance& i) { return i.entity == entity.id; })) {
            recreate.push_back(entity.id);   // entidades cuyo script antes no compilaba
        }
    }
    for (const auto id : recreate) {
        if (const auto* entity = scene.find(id); entity && entity->script) impl.createInstance(*entity);
    }
    impl.nextInstance = 0;
    impl.report(false, "Script " + path + " recargado (" + std::to_string(recreate.size()) + " instancias)");
    return true;
}

void ScriptHost::setServices(ScriptServices services) {
    impl_->services = std::move(services);
}

void ScriptHost::notify(Scene& scene, EntityId self, const char* callback, EntityId other, double value) {
    auto& impl = *impl_;
    if (!impl.L) return;
    impl.scene = &scene;
    for (auto& instance : impl.instances) {
        if (instance.entity != self) continue;
        if (other && scene.find(other)) pushEntity(impl.L, other);
        else lua_pushnil(impl.L);
        lua_pushnumber(impl.L, value);
        impl.call(instance, callback, 2);
    }
}

void ScriptHost::setParameter(EntityId entity, const std::string& name, const ScriptValue& value) {
    auto& impl = *impl_;
    if (!impl.L) return;
    for (const auto& instance : impl.instances) {
        if (instance.entity != entity) continue;
        lua_getref(impl.L, instance.self);
        pushValue(impl.L, value);
        lua_setfield(impl.L, -2, name.c_str());
        lua_pop(impl.L, 1);
    }
}

ScriptHost::Stats ScriptHost::stats() const noexcept {
    Stats stats;
    stats.instances = static_cast<int>(impl_->instances.size());
    stats.disabled = static_cast<int>(std::count_if(impl_->instances.begin(), impl_->instances.end(),
                                                    [](const Impl::Instance& i) { return i.disabled; }));
    stats.frameMs = impl_->frameMs;
    stats.memory = impl_->memory;
    return stats;
}

// --- Descripción para el Inspector ---------------------------------------------------------------

ScriptInfo describeScript(const std::string& source, const std::string& path) {
    ScriptInfo info;
    ScriptHost::Impl impl;
    impl.loader = [&](const std::string&) { return std::optional<std::string>(source); };
    if (!impl.open()) {
        info.diagnostics.push_back({path, 0, "no se pudo crear la maquina virtual de Luau"});
        return info;
    }
    const auto ref = impl.loadModule(path, &info.diagnostics);
    if (ref) {
        info.ok = true;
        lua_State* L = impl.L;
        lua_getref(L, *ref);
        lua_getfield(L, -1, "params");
        std::map<std::string, ScriptValue> parameters;
        if (lua_istable(L, -1)) {
            lua_pushnil(L);
            while (lua_next(L, -2) != 0) {
                if (lua_type(L, -2) == LUA_TSTRING) {
                    if (const auto value = toValue(L, -1)) parameters[lua_tostring(L, -2)] = *value;
                    else info.diagnostics.push_back({path, 0, std::string("el parametro '") + lua_tostring(L, -2) +
                                                                   "' debe ser numero, booleano o texto"});
                }
                lua_pop(L, 1);
            }
        } else if (!lua_isnil(L, -1)) {
            info.diagnostics.push_back({path, 0, "'params' debe ser una tabla"});
        }
        lua_pop(L, 1);
        for (auto& [name, value] : parameters) info.parameters.push_back({name, value});
        for (const char* name : callbackNames) {
            lua_getfield(L, -1, name);
            if (lua_isfunction(L, -1)) info.callbacks.emplace_back(name);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    impl.close();
    return info;
}

}
