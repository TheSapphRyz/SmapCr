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

std::vector<MapEntity> entities;
std::vector<EditorAsset> assetLibrary;
int selectedEntityIdx = -1;
int activeTextureIdx = 0;
bool isIsometric = false;
bool snapToGrid = true;
int currentTool = 0;
char jsonEditBuffer[JSON_BUFFER_SIZE] = "{}";
int mapWidth = 50;
int mapHeight = 50;
bool showJsonEditor = false;
bool isEditingJson = false;

Color circleColors[] = {
    ORANGE, RED, GREEN, BLUE, PURPLE, YELLOW, PINK, SKYBLUE
};

Vector2 WorldToIso(Vector2 worldPos) {
    float isoX = (worldPos.x - worldPos.y);
    float isoY = (worldPos.x + worldPos.y) * 0.5f;
    return { isoX, isoY };
}

Vector2 IsoToWorld(Vector2 isoPos) {
    float worldX = (2.0f * isoPos.y + isoPos.x) * 0.5f;
    float worldY = (2.0f * isoPos.y - isoPos.x) * 0.5f;
    return { worldX, worldY };
}

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
            {"properties", props}
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
    static char widthText[10] = "50";
    static char heightText[10] = "50";
    static bool widthEditMode = false;
    static bool heightEditMode = false;
    static int editMapWidth = mapWidth;
    static int editMapHeight = mapHeight;
    InitWindow(1920, 1080, "SmapCr: ALpha");
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

    static char toolNames[3][32] = { "Tile Brush", "Circle Object", "Select/Edit" };

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground({ 30, 30, 35, 255 });

        if (currentState == STATE_MENU) {
            DrawText("Map Editor", 550, 100, 40, WHITE);

            GuiWindowBox({ 490, 150, 300, 200 }, "New Map Settings");

            GuiLabel({ 500, 180, 80, 20 }, "Width (tiles):");
            static char widthText[10] = "50";
            static bool widthEditMode = false;

            Rectangle widthBox = { 650, 180, 120, 20 };
            if (GuiTextBox(widthBox, widthText, 10, widthEditMode)) {
                widthEditMode = !widthEditMode;
            }

            int tempWidth = atoi(widthText);
            if (tempWidth < 1) {
                tempWidth = 1;
                snprintf(widthText, 10, "%d", tempWidth);
            }
            if (tempWidth > 500) {
                tempWidth = 500;
                snprintf(widthText, 10, "%d", tempWidth);
            }
            mapWidth = tempWidth;

            GuiLabel({ 500, 210, 80, 20 }, "Height (tiles):");
            static char heightText[10] = "50";
            static bool heightEditMode = false;

            Rectangle heightBox = { 650, 210, 120, 20 };
            if (GuiTextBox(heightBox, heightText, 10, heightEditMode)) {
                heightEditMode = !heightEditMode;
            }

            int tempHeight = atoi(heightText);
            if (tempHeight < 1) {
                tempHeight = 1;
                snprintf(heightText, 10, "%d", tempHeight);
            }
            if (tempHeight > 500) {
                tempHeight = 500;
                snprintf(heightText, 10, "%d", tempHeight);
            }
            mapHeight = tempHeight;

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

                    snprintf(widthText, 10, "%d", mapWidth);
                    snprintf(heightText, 10, "%d", mapHeight);

                    selectedEntityIdx = -1;
                    showJsonEditor = false;
                    currentState = STATE_EDITOR;
                }
            }
        }
        else {
            Vector2 mousePos = GetMousePosition();
            bool mouseOverUI = IsMouseOverUI(mousePos);

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

            if (!mouseOverUI && !isEditingJson) {
                Vector2 mouseWorld = GetScreenToWorld2D(mousePos, camera);

                Vector2 gridPos;
                if (isIsometric) {
                    gridPos = IsoToWorld(mouseWorld);
                }
                else {
                    gridPos = mouseWorld;
                }

                if (gridPos.x >= 0 && gridPos.x < mapWidth * TILE_SIZE &&
                    gridPos.y >= 0 && gridPos.y < mapHeight * TILE_SIZE) {

                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                        if (currentTool == 2) {
                            selectedEntityIdx = -1;
                            float closestDist = CIRCLE_RADIUS * 2.0f;

                            for (int i = (int)entities.size() - 1; i >= 0; i--) {
                                if (entities[i].isTile) {
                                    int gridX = (int)(gridPos.x / TILE_SIZE);
                                    int gridY = (int)(gridPos.y / TILE_SIZE);
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
                                    Vector2 entityPos = entities[i].worldPos;
                                    Vector2 searchPos = isIsometric ? gridPos : mouseWorld;
                                    float dist = Vector2Distance(searchPos, entityPos);
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
                        else if (currentTool == 0) {
                            int gridX = (int)(gridPos.x / TILE_SIZE);
                            int gridY = (int)(gridPos.y / TILE_SIZE);

                            if (gridX >= 0 && gridX < mapWidth && gridY >= 0 && gridY < mapHeight) {
                                Vector2 tilePos = { (float)gridX * TILE_SIZE, (float)gridY * TILE_SIZE };

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
                                    entities[existingTileIdx].textureId = activeTextureIdx;
                                    selectedEntityIdx = existingTileIdx;
                                    strncpy(jsonEditBuffer, entities[existingTileIdx].jsonProperties.c_str(), JSON_BUFFER_SIZE - 1);
                                    showJsonEditor = true;
                                }
                                else {
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
                        }
                        else if (currentTool == 1) {
                            Vector2 objectPos = isIsometric ? gridPos : mouseWorld;

                            MapEntity newEnt = {
                                activeTextureIdx,
                                objectPos,
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

                    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && selectedEntityIdx != -1 &&
                        !entities[selectedEntityIdx].isTile && currentTool == 2) {

                        Vector2 newPos = isIsometric ? gridPos : mouseWorld;
                        entities[selectedEntityIdx].worldPos = newPos;
                    }

                    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && currentTool == 0) {
                        int gridX = (int)(gridPos.x / TILE_SIZE);
                        int gridY = (int)(gridPos.y / TILE_SIZE);

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

            if (IsKeyPressed(KEY_DELETE) && selectedEntityIdx != -1) {
                entities.erase(entities.begin() + selectedEntityIdx);
                selectedEntityIdx = -1;
                showJsonEditor = false;
            }

            BeginMode2D(camera);
            {
                if (isIsometric) {
                    for (int y = 0; y < mapHeight; y++) {
                        for (int x = 0; x < mapWidth; x++) {
                            Vector2 worldPos = { (float)x * TILE_SIZE, (float)y * TILE_SIZE };
                            Vector2 isoPos = WorldToIso(worldPos);

                            Vector2 points[4] = {
                                { isoPos.x, isoPos.y - TILE_SIZE / 2 },
                                { isoPos.x + TILE_SIZE / 2, isoPos.y },
                                { isoPos.x, isoPos.y + TILE_SIZE / 2 },
                                { isoPos.x - TILE_SIZE / 2, isoPos.y }
                            };

                            DrawTriangle(points[0], points[1], points[2], ColorAlpha(DARKGRAY, 0.3f));
                            DrawTriangle(points[0], points[2], points[3], ColorAlpha(DARKGRAY, 0.3f));

                            DrawLineV(points[0], points[1], DARKGRAY);
                            DrawLineV(points[1], points[2], DARKGRAY);
                            DrawLineV(points[2], points[3], DARKGRAY);
                            DrawLineV(points[3], points[0], DARKGRAY);
                        }
                    }
                }
                else {
                    DrawRectangle(0, 0, mapWidth * TILE_SIZE, mapHeight * TILE_SIZE, ColorAlpha(GRAY, 0.1f));

                    for (int i = 0; i <= mapWidth; i++) {
                        DrawLine(i * TILE_SIZE, 0, i * TILE_SIZE, mapHeight * TILE_SIZE, DARKGRAY);
                    }
                    for (int i = 0; i <= mapHeight; i++) {
                        DrawLine(0, i * TILE_SIZE, mapWidth * TILE_SIZE, i * TILE_SIZE, DARKGRAY);
                    }
                }

                for (int i = 0; i < (int)entities.size(); i++) {
                    Vector2 drawPos = entities[i].worldPos;

                    if (isIsometric && entities[i].isTile) {
                        drawPos = WorldToIso(drawPos);
                    }

                    if (entities[i].isTile) {
                        if (entities[i].textureId >= 0 && entities[i].textureId < (int)assetLibrary.size()) {
                            if (isIsometric) {
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
                        if (isIsometric) {
                            drawPos = WorldToIso(drawPos);
                        }

                        Color drawColor = entities[i].circleColor;
                        if (i == selectedEntityIdx) {
                            drawColor = ColorAlpha(RED, 0.7f);
                        }

                        DrawCircleV(drawPos, CIRCLE_RADIUS, ColorAlpha(drawColor, 0.8f));
                        DrawCircleLinesV(drawPos, CIRCLE_RADIUS, WHITE);
                        DrawCircleV(drawPos, 3.0f, WHITE);
                    }
                }

                if (!mouseOverUI && currentTool != 2) {
                    Vector2 mouseWorld = GetScreenToWorld2D(mousePos, camera);

                    Vector2 gridPos;
                    if (isIsometric) {
                        gridPos = IsoToWorld(mouseWorld);
                    }
                    else {
                        gridPos = mouseWorld;
                    }

                    if (gridPos.x >= 0 && gridPos.x < mapWidth * TILE_SIZE &&
                        gridPos.y >= 0 && gridPos.y < mapHeight * TILE_SIZE) {

                        if (currentTool == 0) {
                            int gridX = (int)(gridPos.x / TILE_SIZE);
                            int gridY = (int)(gridPos.y / TILE_SIZE);

                            if (gridX >= 0 && gridX < mapWidth && gridY >= 0 && gridY < mapHeight) {
                                Vector2 tilePos = { (float)gridX * TILE_SIZE, (float)gridY * TILE_SIZE };

                                Vector2 previewPos = tilePos;
                                if (isIsometric) {
                                    previewPos = WorldToIso(tilePos);
                                }

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
                        }
                        else if (currentTool == 1) {
                            Vector2 drawPos = isIsometric ? WorldToIso(gridPos) : mouseWorld;
                            DrawCircleLinesV(drawPos, CIRCLE_RADIUS, ColorAlpha(GREEN, 0.7f));
                        }
                    }
                }
            }
            EndMode2D();

            GuiWindowBox({ 10, 10, 250, 450 }, "Toolbar");

            GuiLabel({ 20, 40, 100, 20 }, "Tools:");

            if (GuiButton({ 20, 65, 230, 25 }, toolNames[0])) {
                currentTool = 0;
            }
            if (GuiButton({ 20, 95, 230, 25 }, toolNames[1])) {
                currentTool = 1;
            }
            if (GuiButton({ 20, 125, 230, 25 }, toolNames[2])) {
                currentTool = 2;
            }

            Color highlightColor = { 100, 100, 150, 255 };
            Rectangle highlightRect;
            switch (currentTool) {
            case 0: highlightRect = { 20, 65, 230, 25 }; break;
            case 1: highlightRect = { 20, 95, 230, 25 }; break;
            case 2: highlightRect = { 20, 125, 230, 25 }; break;
            }
            DrawRectangleLinesEx(highlightRect, 2, highlightColor);

            GuiLabel({ 20, 160, 230, 20 }, TextFormat("Current: %s", toolNames[currentTool]));

            if (GuiToggle({ 20, 185, 20, 20 }, NULL, &isIsometric)) {
                isIsometric = !isIsometric;
            }
            GuiLabel({ 45, 185, 200, 20 }, "Isometric View");

            if (GuiToggle({ 20, 210, 20, 20 }, NULL, &snapToGrid)) {
                snapToGrid = !snapToGrid;
            }
            GuiLabel({ 45, 210, 200, 20 }, "Snap to Grid");

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

            GuiLabel({ 20, 390, 230, 20 }, TextFormat("Entities: %d", (int)entities.size()));
            if (selectedEntityIdx != -1) {
                GuiLabel({ 20, 410, 230, 20 }, TextFormat("Selected: %d", selectedEntityIdx));
                GuiLabel({ 20, 430, 230, 20 }, TextFormat("Type: %s",
                    entities[selectedEntityIdx].isTile ? "Tile" : "Circle"));
            }

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

                    if (result == 1) {
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
                    else if (result == 0) {
                        isEditingJson = false;
                    }
                }
            }

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
