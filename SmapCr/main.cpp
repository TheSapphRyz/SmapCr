#define RAYLIB_VULKAN
#include "raylib.h"
#include "raymath.h"
#define RAYGUI_IMPLEMENTATION 
#include "raygui.h"
#include "nfd.h"
#include "json.hpp"
#include <vector>
#include <map>
#include <cmath>
#include <fstream>
#include <string>
#include <cstring>

using json = nlohmann::json;

// --- КОНСТАНТЫ И СТРУКТУРЫ ---
const int TILE_SIZE = 64;
const int JSON_BUFFER_SIZE = 4096;
const float CIRCLE_RADIUS = 15.0f;

struct MapEntity {
    int textureId;
    Vector2 worldPos;
    Vector2 size;
    bool isTile;
    std::string jsonProperties;
    Color circleColor;
};

struct EditorAsset {
    std::string name;
    Texture2D texture;
};

// --- ГЛОБАЛЬНЫЕ ДАННЫЕ РЕДАКТОРА ---
std::vector<MapEntity> entities;
std::vector<EditorAsset> assetLibrary;
int selectedEntityIdx = -1;
int activeTextureIdx = 0;
bool isIsometric = false;
bool snapToGrid = true;
int currentTool = 0; // 0: Tile, 1: Object (Circle), 2: Select
char jsonEditBuffer[JSON_BUFFER_SIZE] = "{}";
int mapWidth = 50;
int mapHeight = 50;
bool showJsonEditor = false;
bool isEditingJson = false;

// Цвета для кругов объектов
Color circleColors[] = {
    ORANGE, RED, GREEN, BLUE, PURPLE, YELLOW, PINK, SKYBLUE
};

// --- МАТЕМАТИКА ПРОЕКЦИЙ ---
Vector2 WorldToIso(Vector2 worldPos) {
    // Конвертируем мировые координаты в изометрические
    float isoX = (worldPos.x - worldPos.y);
    float isoY = (worldPos.x + worldPos.y) * 0.5f;
    return { isoX, isoY };
}

Vector2 IsoToWorld(Vector2 isoPos) {
    // Конвертируем изометрические координаты в мировые
    float worldX = (2.0f * isoPos.y + isoPos.x) * 0.5f;
    float worldY = (2.0f * isoPos.y - isoPos.x) * 0.5f;
    return { worldX, worldY };
}

// --- ФУНКЦИИ ЗАГРУЗКИ/СОХРАНЕНИЯ ---
void SaveMap(const std::string& path) {
    json j;
    j["mapInfo"]["width"] = mapWidth;
    j["mapInfo"]["height"] = mapHeight;
    j["mapInfo"]["isIsometric"] = isIsometric;
    j["mapInfo"]["tileSize"] = TILE_SIZE;

    json entitiesArray = json::array();
    for (const auto& ent : entities) {
        json props;
        try {
            props = json::parse(ent.jsonProperties.empty() ? "{}" : ent.jsonProperties);
        }
        catch (...) {
            props = json::object();
        }

        json entityJson = {
            {"x", ent.worldPos.x},
            {"y", ent.worldPos.y},
            {"textureId", ent.textureId},
            {"isTile", ent.isTile},
            {"size", {ent.size.x, ent.size.y}},
            {"circleColor", {ent.circleColor.r, ent.circleColor.g, ent.circleColor.b, ent.circleColor.a}},
            {"properties", props},
            {"json", ent.jsonProperties}
        };
        entitiesArray.push_back(entityJson);
    }
    j["entities"] = entitiesArray;

    std::ofstream o(path);
    if (o.is_open()) o << j.dump(4);
}

void LoadMap(const std::string& path) {
    std::ifstream i(path);
    if (!i.is_open()) return;

    try {
        json j = json::parse(i);

        if (j.contains("mapInfo")) {
            mapWidth = j["mapInfo"]["width"];
            mapHeight = j["mapInfo"]["height"];
            if (j["mapInfo"].contains("isIsometric")) {
                isIsometric = j["mapInfo"]["isIsometric"];
            }
        }

        entities.clear();
        if (j.contains("entities")) {
            for (const auto& ent_json : j["entities"]) {
                MapEntity ent;
                ent.textureId = ent_json["textureId"];
                ent.worldPos = { ent_json["x"], ent_json["y"] };

                if (ent_json.contains("size") && ent_json["size"].is_array() && ent_json["size"].size() >= 2) {
                    ent.size = { ent_json["size"][0], ent_json["size"][1] };
                }
                else {
                    ent.size = { (float)TILE_SIZE, (float)TILE_SIZE };
                }

                ent.isTile = ent_json["isTile"];

                if (ent_json.contains("circleColor") && ent_json["circleColor"].is_array() && ent_json["circleColor"].size() >= 4) {
                    ent.circleColor = {
                        (unsigned char)ent_json["circleColor"][0],
                        (unsigned char)ent_json["circleColor"][1],
                        (unsigned char)ent_json["circleColor"][2],
                        (unsigned char)ent_json["circleColor"][3]
                    };
                }
                else {
                    ent.circleColor = circleColors[ent.textureId % (sizeof(circleColors) / sizeof(circleColors[0]))];
                }

                if (ent_json.contains("properties")) {
                    ent.jsonProperties = ent_json["properties"].dump();
                }
                else {
                    ent.jsonProperties = "{}";
                }

                entities.push_back(ent);
            }
        }
    }
    catch (...) {
        TraceLog(LOG_WARNING, "Failed to parse map file");
    }
}

void LoadTextureToLibrary(const std::string& path) {
    Texture2D tex = LoadTexture(path.c_str());
    if (tex.id > 0) {
        EditorAsset asset;
        asset.name = GetFileName(path.c_str());
        asset.texture = tex;
        assetLibrary.push_back(asset);
    }
}

bool IsMouseOverUI(Vector2 mousePos) {
    if (CheckCollisionPointRec(mousePos, { 10, 10, 250, 450 })) return true;
    if (CheckCollisionPointRec(mousePos, { (float)GetScreenWidth() - 260, 10, 250, 300 })) return true;
    if (showJsonEditor && CheckCollisionPointRec(mousePos, { (float)GetScreenWidth() - 260, 320, 250, 300 })) return true;
    return false;
}

int main() {
    InitWindow(1280, 720, "Map Editor 2025 - Circles & JSON");
    SetTargetFPS(60);
    NFD_Init();

    enum GameState { STATE_MENU, STATE_EDITOR };
    GameState currentState = STATE_MENU;
    Camera2D camera = {
        {640, 360},
        { 0, 0 },
        0.0f, 1.0f
    };

    if (assetLibrary.empty()) {
        Image placeholderImg = GenImageChecked(64, 64, 16, 16, LIGHTGRAY, DARKGRAY);
        Texture2D placeholderTex = LoadTextureFromImage(placeholderImg);
        assetLibrary.push_back({ "Default Tile", placeholderTex });
        UnloadImage(placeholderImg);
    }

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground({ 30, 30, 35, 255 });

        if (currentState == STATE_MENU) {
            DrawText("Map Editor", 550, 100, 40, WHITE);
            GuiWindowBox({ 490, 150, 300, 200 }, "New Map Settings");
            GuiLabel({ 500, 180, 80, 20 }, "Width (tiles):");
            GuiValueBox({ 650, 180, 120, 20 }, NULL, &mapWidth, 1, 500, true);
            GuiLabel({ 500, 210, 80, 20 }, "Height (tiles):");
            GuiValueBox({ 650, 210, 120, 20 }, NULL, &mapHeight, 1, 500, true);
            if (GuiButton({ 540, 280, 200, 40 }, "Create New Map")) {
                entities.clear();
                selectedEntityIdx = -1;
                showJsonEditor = false;
                currentState = STATE_EDITOR;
            }
            if (GuiButton({ 540, 350, 200, 40 }, "Open Map")) {
                nfdchar_t* outPath = nullptr;
                nfdresult_t result = NFD_OpenDialog(&outPath, NULL, 0, NULL);
                if (result == NFD_OKAY) {
                    LoadMap(outPath);
                    NFD_FreePath(outPath);
                    selectedEntityIdx = -1;
                    showJsonEditor = false;
                    currentState = STATE_EDITOR;
                }
            }
        }
        else {
            Vector2 mousePos = GetMousePosition();
            bool mouseOverUI = IsMouseOverUI(mousePos);

            // Управление камерой
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
                Vector2 delta = GetMouseDelta();
                camera.target = Vector2Subtract(camera.target, Vector2Scale(delta, 1.0f / camera.zoom));
            }

            float wheel = GetMouseWheelMove();
            if (wheel != 0) {
                Vector2 mouseWorldBefore = GetScreenToWorld2D(mousePos, camera);
                camera.zoom += wheel * 0.1f;
                if (camera.zoom < 0.1f) camera.zoom = 0.1f;
                if (camera.zoom > 5.0f) camera.zoom = 5.0f;

                Vector2 mouseWorldAfter = GetScreenToWorld2D(mousePos, camera);
                camera.target = Vector2Add(camera.target,
                    Vector2Subtract(mouseWorldBefore, mouseWorldAfter));
            }

            // Логика редактирования
            if (!mouseOverUI) {
                Vector2 mouseWorld = GetScreenToWorld2D(mousePos, camera);

                // Обрабатываем координаты в зависимости от режима
                Vector2 gridPos;
                if (isIsometric) {
                    // Для изометрии сначала конвертируем в мировые координаты
                    gridPos = IsoToWorld(mouseWorld);
                }
                else {
                    gridPos = mouseWorld;
                }

                // Преобразуем в координаты сетки
                int gridX = (int)(gridPos.x / TILE_SIZE);
                int gridY = (int)(gridPos.y / TILE_SIZE);

                if (gridX >= 0 && gridX < mapWidth && gridY >= 0 && gridY < mapHeight) {
                    Vector2 tilePos = { (float)gridX * TILE_SIZE, (float)gridY * TILE_SIZE };
                    Vector2 circlePos = { tilePos.x + TILE_SIZE / 2.0f, tilePos.y + TILE_SIZE / 2.0f };

                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                        if (currentTool == 2) { // Select Tool
                            selectedEntityIdx = -1;
                            float closestDist = CIRCLE_RADIUS * 2.0f;

                            // Преобразуем позицию для поиска в нужной системе координат
                            Vector2 searchPos = isIsometric ? gridPos : mouseWorld;

                            for (int i = (int)entities.size() - 1; i >= 0; i--) {
                                if (entities[i].isTile) {
                                    // Для тайлов проверяем попадание в сетку
                                    Vector2 entityGridPos = {
                                        entities[i].worldPos.x / TILE_SIZE,
                                        entities[i].worldPos.y / TILE_SIZE
                                    };

                                    if ((int)entityGridPos.x == gridX && (int)entityGridPos.y == gridY) {
                                        selectedEntityIdx = i;
                                        strncpy(jsonEditBuffer, entities[i].jsonProperties.c_str(), JSON_BUFFER_SIZE - 1);
                                        showJsonEditor = true;
                                        break;
                                    }
                                }
                                else {
                                    // Для объектов проверяем расстояние
                                    float dist = Vector2Distance(searchPos, entities[i].worldPos);
                                    if (dist < CIRCLE_RADIUS && dist < closestDist) {
                                        selectedEntityIdx = i;
                                        closestDist = dist;
                                        strncpy(jsonEditBuffer, entities[i].jsonProperties.c_str(), JSON_BUFFER_SIZE - 1);
                                        showJsonEditor = true;
                                        break;
                                    }
                                }
                            }
                        }
                        else if (currentTool == 0) { // Tile Brush
                            // Проверяем, есть ли уже тайл в этой позиции
                            bool tileExists = false;
                            int existingTileIdx = -1;

                            for (int i = 0; i < (int)entities.size(); i++) {
                                if (entities[i].isTile) {
                                    Vector2 entityGridPos = {
                                        entities[i].worldPos.x / TILE_SIZE,
                                        entities[i].worldPos.y / TILE_SIZE
                                    };

                                    if ((int)entityGridPos.x == gridX && (int)entityGridPos.y == gridY) {
                                        tileExists = true;
                                        existingTileIdx = i;
                                        break;
                                    }
                                }
                            }

                            if (tileExists) {
                                // Редактируем существующий тайл
                                entities[existingTileIdx].textureId = activeTextureIdx;
                                selectedEntityIdx = existingTileIdx;
                                strncpy(jsonEditBuffer, entities[existingTileIdx].jsonProperties.c_str(), JSON_BUFFER_SIZE - 1);
                                showJsonEditor = true;
                            }
                            else {
                                // Создаем новый тайл
                                MapEntity newEnt = {
                                    activeTextureIdx,
                                    tilePos,
                                    { (float)TILE_SIZE, (float)TILE_SIZE },
                                    true,
                                    "{}",
                                    WHITE
                                };
                                entities.push_back(newEnt);
                            }
                        }
                        else if (currentTool == 1) { // Circle Object
                            MapEntity newEnt = {
                                activeTextureIdx,
                                circlePos,
                                { CIRCLE_RADIUS * 2.0f, CIRCLE_RADIUS * 2.0f },
                                false,
                                "{}",
                                circleColors[activeTextureIdx % (sizeof(circleColors) / sizeof(circleColors[0]))]
                            };

                            entities.push_back(newEnt);
                            selectedEntityIdx = (int)entities.size() - 1;
                            strncpy(jsonEditBuffer, "{}", JSON_BUFFER_SIZE);
                            showJsonEditor = true;
                        }
                    }

                    // Перемещение объекта
                    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && selectedEntityIdx != -1 &&
                        !entities[selectedEntityIdx].isTile && currentTool == 2) {

                        if (snapToGrid) {
                            entities[selectedEntityIdx].worldPos = circlePos;
                        }
                        else {
                            entities[selectedEntityIdx].worldPos = isIsometric ? gridPos : mouseWorld;
                        }
                    }

                    // Удаление тайла
                    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && currentTool == 0) {
                        for (int i = (int)entities.size() - 1; i >= 0; i--) {
                            if (entities[i].isTile) {
                                Vector2 entityGridPos = {
                                    entities[i].worldPos.x / TILE_SIZE,
                                    entities[i].worldPos.y / TILE_SIZE
                                };

                                if ((int)entityGridPos.x == gridX && (int)entityGridPos.y == gridY) {
                                    entities.erase(entities.begin() + i);
                                    if (selectedEntityIdx == i) {
                                        selectedEntityIdx = -1;
                                        showJsonEditor = false;
                                    }
                                    else if (selectedEntityIdx > i) {
                                        selectedEntityIdx--;
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            // Удаление объекта
            if (IsKeyPressed(KEY_DELETE) && selectedEntityIdx != -1) {
                entities.erase(entities.begin() + selectedEntityIdx);
                selectedEntityIdx = -1;
                showJsonEditor = false;
            }

            // Отрисовка мира
            BeginMode2D(camera);
            {
                // Отрисовка сетки и фона
                if (isIsometric) {
                    // Изометрическая сетка
                    for (int y = 0; y < mapHeight; y++) {
                        for (int x = 0; x < mapWidth; x++) {
                            Vector2 worldPos = { (float)x * TILE_SIZE, (float)y * TILE_SIZE };
                            Vector2 isoPos = WorldToIso(worldPos);

                            // Ромб
                            Vector2 points[4] = {
                                { isoPos.x, isoPos.y - TILE_SIZE / 2 },
                                { isoPos.x + TILE_SIZE / 2, isoPos.y },
                                { isoPos.x, isoPos.y + TILE_SIZE / 2 },
                                { isoPos.x - TILE_SIZE / 2, isoPos.y }
                            };

                            // Заливка
                            DrawTriangle(points[0], points[1], points[2], ColorAlpha(DARKGRAY, 0.3f));
                            DrawTriangle(points[0], points[2], points[3], ColorAlpha(DARKGRAY, 0.3f));

                            // Контур
                            DrawLineV(points[0], points[1], DARKGRAY);
                            DrawLineV(points[1], points[2], DARKGRAY);
                            DrawLineV(points[2], points[3], DARKGRAY);
                            DrawLineV(points[3], points[0], DARKGRAY);
                        }
                    }
                }
                else {
                    // Ортогональная сетка
                    DrawRectangle(0, 0, mapWidth * TILE_SIZE, mapHeight * TILE_SIZE, ColorAlpha(GRAY, 0.1f));

                    for (int i = 0; i <= mapWidth; i++) {
                        DrawLine(i * TILE_SIZE, 0, i * TILE_SIZE, mapHeight * TILE_SIZE, DARKGRAY);
                    }
                    for (int i = 0; i <= mapHeight; i++) {
                        DrawLine(0, i * TILE_SIZE, mapWidth * TILE_SIZE, i * TILE_SIZE, DARKGRAY);
                    }
                }

                // Отрисовка сущностей
                for (int i = 0; i < (int)entities.size(); i++) {
                    Vector2 drawPos = entities[i].worldPos;

                    if (isIsometric && entities[i].isTile) {
                        // Для тайлов в изометрии преобразуем координаты
                        drawPos = WorldToIso(drawPos);
                    }

                    if (entities[i].isTile) {
                        if (entities[i].textureId >= 0 && entities[i].textureId < (int)assetLibrary.size()) {
                            if (isIsometric) {
                                // В изометрии рисуем текстуру с центром в позиции тайла
                                Rectangle source = { 0, 0, (float)assetLibrary[entities[i].textureId].texture.width,
                                                   (float)assetLibrary[entities[i].textureId].texture.height };
                                Rectangle dest = { drawPos.x, drawPos.y, (float)TILE_SIZE, (float)TILE_SIZE };
                                Vector2 origin = { (float)TILE_SIZE / 2, (float)TILE_SIZE / 2 };
                                DrawTexturePro(assetLibrary[entities[i].textureId].texture, source, dest, origin, 0.0f,
                                    (i == selectedEntityIdx) ? ColorAlpha(RED, 0.7f) : WHITE);
                            }
                            else {
                                DrawTextureV(assetLibrary[entities[i].textureId].texture, drawPos,
                                    (i == selectedEntityIdx) ? ColorAlpha(RED, 0.7f) : WHITE);
                            }
                        }
                        else {
                            if (isIsometric) {
                                // Ромб для тайла без текстуры
                                Vector2 points[4] = {
                                    { drawPos.x, drawPos.y - TILE_SIZE / 2 },
                                    { drawPos.x + TILE_SIZE / 2, drawPos.y },
                                    { drawPos.x, drawPos.y + TILE_SIZE / 2 },
                                    { drawPos.x - TILE_SIZE / 2, drawPos.y }
                                };
                                Color fillColor = ColorAlpha((i == selectedEntityIdx) ? RED : BLUE, 0.5f);
                                DrawTriangle(points[0], points[1], points[2], fillColor);
                                DrawTriangle(points[0], points[2], points[3], fillColor);
                            }
                            else {
                                DrawRectangleV(drawPos, entities[i].size,
                                    ColorAlpha((i == selectedEntityIdx) ? RED : BLUE, 0.5f));
                            }
                        }
                    }
                    else {
                        // Объекты (круги)
                        Color drawColor = entities[i].circleColor;
                        if (i == selectedEntityIdx) {
                            drawColor = ColorAlpha(RED, 0.7f);
                        }

                        DrawCircleV(drawPos, CIRCLE_RADIUS, ColorAlpha(drawColor, 0.8f));
                        DrawCircleLinesV(drawPos, CIRCLE_RADIUS, WHITE);
                        DrawCircleV(drawPos, 3.0f, WHITE);
                    }
                }

                // Предпросмотр
                if (!mouseOverUI && currentTool != 2) {
                    Vector2 mouseWorld = GetScreenToWorld2D(mousePos, camera);

                    // Определяем позицию в сетке
                    Vector2 gridPos;
                    if (isIsometric) {
                        gridPos = IsoToWorld(mouseWorld);
                    }
                    else {
                        gridPos = mouseWorld;
                    }

                    int gridX = (int)(gridPos.x / TILE_SIZE);
                    int gridY = (int)(gridPos.y / TILE_SIZE);

                    if (gridX >= 0 && gridX < mapWidth && gridY >= 0 && gridY < mapHeight) {
                        Vector2 tilePos = { (float)gridX * TILE_SIZE, (float)gridY * TILE_SIZE };
                        Vector2 circlePos = { tilePos.x + TILE_SIZE / 2.0f, tilePos.y + TILE_SIZE / 2.0f };

                        Vector2 previewPos = tilePos;
                        if (isIsometric && currentTool == 0) {
                            previewPos = WorldToIso(tilePos);
                        }
                        else if (isIsometric && currentTool == 1) {
                            previewPos = WorldToIso(circlePos);
                        }

                        if (currentTool == 0) { // Предпросмотр тайла
                            if (activeTextureIdx >= 0 && activeTextureIdx < (int)assetLibrary.size()) {
                                if (isIsometric) {
                                    Rectangle source = { 0, 0, (float)assetLibrary[activeTextureIdx].texture.width,
                                                       (float)assetLibrary[activeTextureIdx].texture.height };
                                    Rectangle dest = { previewPos.x, previewPos.y, (float)TILE_SIZE, (float)TILE_SIZE };
                                    Vector2 origin = { (float)TILE_SIZE / 2, (float)TILE_SIZE / 2 };
                                    DrawTexturePro(assetLibrary[activeTextureIdx].texture, source, dest, origin, 0.0f, ColorAlpha(WHITE, 0.5f));
                                }
                                else {
                                    DrawTextureV(assetLibrary[activeTextureIdx].texture, previewPos, ColorAlpha(WHITE, 0.5f));
                                }
                            }

                            if (isIsometric) {
                                Vector2 points[4] = {
                                    { previewPos.x, previewPos.y - TILE_SIZE / 2 },
                                    { previewPos.x + TILE_SIZE / 2, previewPos.y },
                                    { previewPos.x, previewPos.y + TILE_SIZE / 2 },
                                    { previewPos.x - TILE_SIZE / 2, previewPos.y }
                                };
                                DrawLineV(points[0], points[1], ColorAlpha(GREEN, 0.7f));
                                DrawLineV(points[1], points[2], ColorAlpha(GREEN, 0.7f));
                                DrawLineV(points[2], points[3], ColorAlpha(GREEN, 0.7f));
                                DrawLineV(points[3], points[0], ColorAlpha(GREEN, 0.7f));
                            }
                            else {
                                DrawRectangleLinesEx({ previewPos.x, previewPos.y, (float)TILE_SIZE, (float)TILE_SIZE },
                                    2.0f, ColorAlpha(GREEN, 0.7f));
                            }
                        }
                        else if (currentTool == 1) { // Предпросмотр круга
                            Vector2 drawPos = isIsometric ? WorldToIso(circlePos) : circlePos;
                            DrawCircleLinesV(drawPos, CIRCLE_RADIUS, ColorAlpha(GREEN, 0.7f));
                        }
                    }
                }
            }
            EndMode2D();

            // --- ИНТЕРФЕЙС ---
            // Панель инструментов слева
            GuiWindowBox({ 10, 10, 250, 450 }, "Toolbar");

            // Инструменты
            GuiLabel({ 20, 40, 100, 20 }, "Tools:");

            // Отдельные кнопки для каждого инструмента
            static bool toolButtons[3] = { false, false, false };

            // Обработка нажатий кнопок инструментов
            if (GuiToggle({ 20, 65, 230, 25 }, "Tile Brush", &toolButtons[0])) {
                if (toolButtons[0]) {
                    currentTool = 0;
                    toolButtons[1] = false;
                    toolButtons[2] = false;
                }
            }

            if (GuiToggle({ 20, 95, 230, 25 }, "Circle Object", &toolButtons[1])) {
                if (toolButtons[1]) {
                    currentTool = 1;
                    toolButtons[0] = false;
                    toolButtons[2] = false;
                }
            }

            if (GuiToggle({ 20, 125, 230, 25 }, "Select/Edit", &toolButtons[2])) {
                if (toolButtons[2]) {
                    currentTool = 2;
                    toolButtons[0] = false;
                    toolButtons[1] = false;
                }
            }

            // Устанавливаем правильное состояние кнопок
            toolButtons[0] = (currentTool == 0);
            toolButtons[1] = (currentTool == 1);
            toolButtons[2] = (currentTool == 2);

            // Отображение текущего инструмента
            const char* toolNames[] = { "Tile Brush", "Circle Object", "Select/Edit" };
            GuiLabel({ 20, 160, 230, 20 }, TextFormat("Current: %s", toolNames[currentTool]));

            // Чекбоксы
            bool tempIsometric = isIsometric;
            if (GuiCheckBox({ 20, 185, 20, 20 }, NULL, &tempIsometric)) {
                isIsometric = tempIsometric;
            }
            GuiLabel({ 45, 185, 200, 20 }, "Isometric View");

            bool tempSnapToGrid = snapToGrid;
            if (GuiCheckBox({ 20, 210, 20, 20 }, NULL, &tempSnapToGrid)) {
                snapToGrid = tempSnapToGrid;
            }
            GuiLabel({ 45, 210, 200, 20 }, "Snap to Grid");

            // Текстуры
            GuiLabel({ 20, 240, 100, 20 }, "Active Texture:");
            if (activeTextureIdx >= 0 && activeTextureIdx < (int)assetLibrary.size()) {
                GuiLabel({ 120, 240, 130, 20 }, assetLibrary[activeTextureIdx].name.c_str());
            }

            if (GuiButton({ 20, 265, 110, 25 }, "< Prev Texture")) {
                if (activeTextureIdx > 0) activeTextureIdx--;
                else activeTextureIdx = (int)assetLibrary.size() - 1;
            }
            if (GuiButton({ 135, 265, 110, 25 }, "Next Texture >")) {
                if (activeTextureIdx < (int)assetLibrary.size() - 1) activeTextureIdx++;
                else activeTextureIdx = 0;
            }

            // Основные кнопки
            if (GuiButton({ 20, 300, 230, 25 }, "Load Texture")) {
                nfdchar_t* outPath = nullptr;
                nfdresult_t result = NFD_OpenDialog(&outPath, NULL, 0, NULL);
                if (result == NFD_OKAY) {
                    LoadTextureToLibrary(outPath);
                    NFD_FreePath(outPath);
                }
            }

            if (GuiButton({ 20, 330, 230, 25 }, "Save Map")) {
                nfdchar_t* outPath = nullptr;
                nfdresult_t result = NFD_SaveDialog(&outPath, NULL, 0, NULL, "map.json");
                if (result == NFD_OKAY) {
                    SaveMap(outPath);
                    NFD_FreePath(outPath);
                }
            }

            if (GuiButton({ 20, 360, 230, 25 }, "Exit")) {
                currentState = STATE_MENU;
                selectedEntityIdx = -1;
                showJsonEditor = false;
            }

            // Статистика
            GuiLabel({ 20, 390, 230, 20 }, TextFormat("Entities: %d", (int)entities.size()));
            if (selectedEntityIdx != -1) {
                GuiLabel({ 20, 410, 230, 20 }, TextFormat("Selected: %d", selectedEntityIdx));
                GuiLabel({ 20, 430, 230, 20 }, TextFormat("Type: %s",
                    entities[selectedEntityIdx].isTile ? "Tile" : "Circle"));
            }

            // Библиотека текстур
            GuiWindowBox({ (float)GetScreenWidth() - 260, 10, 250, 300 }, "Asset Library");
            for (int i = 0; i < (int)assetLibrary.size(); i++) {
                Rectangle btnRect = { (float)GetScreenWidth() - 250, 40.0f + i * 55, 230, 50 };
                if (GuiButton(btnRect, assetLibrary[i].name.c_str())) {
                    activeTextureIdx = i;
                }

                if (i == activeTextureIdx) {
                    DrawRectangleLinesEx(btnRect, 2, GREEN);
                }
            }

            // Редактор JSON
            if (showJsonEditor && selectedEntityIdx != -1) {
                GuiWindowBox({ (float)GetScreenWidth() - 260, 320, 250, 300 },
                    entities[selectedEntityIdx].isTile ? "Tile Inspector" : "Circle Inspector");

                if (GuiButton({ (float)GetScreenWidth() - 40, 320, 30, 30 }, "X")) {
                    showJsonEditor = false;
                    isEditingJson = false;
                }

                GuiLabel({ (float)GetScreenWidth() - 250, 350, 230, 20 },
                    TextFormat("ID: %d", selectedEntityIdx));

                if (entities[selectedEntityIdx].isTile) {
                    GuiLabel({ (float)GetScreenWidth() - 250, 375, 230, 20 },
                        TextFormat("Grid: (%d, %d)",
                            (int)(entities[selectedEntityIdx].worldPos.x / TILE_SIZE),
                            (int)(entities[selectedEntityIdx].worldPos.y / TILE_SIZE)));

                    GuiLabel({ (float)GetScreenWidth() - 250, 400, 100, 20 }, "Texture:");

                    if (entities[selectedEntityIdx].textureId >= 0 &&
                        entities[selectedEntityIdx].textureId < (int)assetLibrary.size()) {
                        GuiLabel({ (float)GetScreenWidth() - 150, 400, 130, 20 },
                            assetLibrary[entities[selectedEntityIdx].textureId].name.c_str());
                    }

                    if (GuiButton({ (float)GetScreenWidth() - 250, 425, 110, 25 }, "< Prev")) {
                        if (entities[selectedEntityIdx].textureId > 0)
                            entities[selectedEntityIdx].textureId--;
                        else
                            entities[selectedEntityIdx].textureId = (int)assetLibrary.size() - 1;
                    }
                    if (GuiButton({ (float)GetScreenWidth() - 135, 425, 110, 25 }, "Next >")) {
                        if (entities[selectedEntityIdx].textureId < (int)assetLibrary.size() - 1)
                            entities[selectedEntityIdx].textureId++;
                        else
                            entities[selectedEntityIdx].textureId = 0;
                    }

                    GuiLabel({ (float)GetScreenWidth() - 250, 460, 230, 20 }, "JSON Properties (optional):");

                    char displayJson[128];
                    snprintf(displayJson, 128, "%.120s", entities[selectedEntityIdx].jsonProperties.c_str());
                    GuiLabel({ (float)GetScreenWidth() - 250, 485, 230, 20 }, displayJson);

                    if (GuiButton({ (float)GetScreenWidth() - 250, 510, 230, 25 }, "Edit JSON")) {
                        isEditingJson = true;
                    }

                    // Кнопки управления
                    if (GuiButton({ (float)GetScreenWidth() - 250, 540, 110, 30 }, "Delete")) {
                        entities.erase(entities.begin() + selectedEntityIdx);
                        selectedEntityIdx = -1;
                        showJsonEditor = false;
                        isEditingJson = false;
                    }

                    if (GuiButton({ (float)GetScreenWidth() - 130, 540, 110, 30 }, "Close")) {
                        showJsonEditor = false;
                        isEditingJson = false;
                    }
                }
                else {
                    GuiLabel({ (float)GetScreenWidth() - 250, 375, 230, 20 },
                        TextFormat("Position: (%.0f, %.0f)",
                            entities[selectedEntityIdx].worldPos.x,
                            entities[selectedEntityIdx].worldPos.y));

                    GuiLabel({ (float)GetScreenWidth() - 250, 400, 230, 20 }, "JSON Properties:");

                    char displayJson[128];
                    snprintf(displayJson, 128, "%.120s", entities[selectedEntityIdx].jsonProperties.c_str());
                    GuiLabel({ (float)GetScreenWidth() - 250, 425, 230, 20 }, displayJson);

                    if (GuiButton({ (float)GetScreenWidth() - 250, 450, 230, 25 }, "Edit JSON")) {
                        isEditingJson = true;
                    }

                    // Кнопки управления
                    if (GuiButton({ (float)GetScreenWidth() - 250, 480, 110, 30 }, "Delete")) {
                        entities.erase(entities.begin() + selectedEntityIdx);
                        selectedEntityIdx = -1;
                        showJsonEditor = false;
                        isEditingJson = false;
                    }

                    if (GuiButton({ (float)GetScreenWidth() - 130, 480, 110, 30 }, "Close")) {
                        showJsonEditor = false;
                        isEditingJson = false;
                    }
                }

                // Модальное окно редактирования JSON
                if (isEditingJson) {
                    int result = GuiTextInputBox(
                        { (float)GetScreenWidth() / 2 - 200, (float)GetScreenHeight() / 2 - 150, 400, 300 },
                        "Edit JSON",
                        "Enter JSON properties:",
                        "Save",
                        jsonEditBuffer,
                        JSON_BUFFER_SIZE,
                        nullptr
                    );

                    if (result == 1) { // Save
                        try {
                            json test = json::parse(jsonEditBuffer);
                            entities[selectedEntityIdx].jsonProperties = jsonEditBuffer;
                        }
                        catch (...) {
                            TraceLog(LOG_WARNING, "Invalid JSON");
                            strncpy(jsonEditBuffer, entities[selectedEntityIdx].jsonProperties.c_str(), JSON_BUFFER_SIZE - 1);
                        }
                        isEditingJson = false;
                    }
                    else if (result == 0) { // Cancel
                        isEditingJson = false;
                    }
                }
            }

            // Подсказки
            DrawText("LMB: Place/Select | RMB: Move Camera/Delete Tiles | DEL: Delete Selected",
                10, GetScreenHeight() - 30, 10, LIGHTGRAY);
        }
        EndDrawing();
    }

    NFD_Quit();
    for (auto& asset : assetLibrary) {
        UnloadTexture(asset.texture);
    }
    CloseWindow();
    return 0;
}