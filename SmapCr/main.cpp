#define _CRT_SECURE_NO_WARNINGS
#define RAYGUI_IMPLEMENTATION
#define GRAPHICS_API_VULKAN
#include <raylib.h>
#include <raygui.h>
#include <cstddef>
#include <map>
#include <vector>
#include <string>
#include "json.hpp"
#include <functional>
#include <nfd.h>
#include <rlgl.h>
#include <float.h>
#include <cmath>
#include <random>
#include <raymath.h>
using json = nlohmann::json;

json r;
float seed;
std::map<std::string, std::pair<Texture2D, std::string>> texs; // текстуры и их base64 данные для сохранения в json
std::vector<const char*> texs_for_list;
enum TOOL {TILE, OBJP, SELECT};

struct Tile {
	int id;
	int bid; // 0 - ничего ++++++ 1 - холм +++++++++ 2 - лесок ++++++ 3 - лес ++++++++ 4 - море +++++++++ 5 - речка
	std::string tid; // texs 
	float h;
	json j;
};
struct OBJ {
	int id;
	int tid;
	int proch; // 0-100 прочность предмета. Подходит и для еды
	Vector3 vec3; // x, Y(выс), z 
	float pov; // поворот 
	float razm; // размер
	void (*onclck)(OBJ& self); // колбек на клик 
	json j;
	std::string anim; //потом заменить на нормальную анимацию объекта, а пока это просто текстуры по кадрам 1_3_5 (tid)
};
Vector3 playerPos = { 0.0f, 10.0f, 0.0f };
std::vector<OBJ>OBJS;
std::vector<Tile>tiles;
TOOL ct;
bool sjw = false;
char jb[4096] = "{\n\tur json\n}";
Tile* st = nullptr;
OBJ* so = nullptr;
int MAP_W = 1000;
int MAP_H = 1000;
int tool_s;
int sc_idx;
int act_idx;
int foc_idx;

struct generator_set {
	// 1. Общий масштаб (насколько всё крупное: и горы, и равнины)
	float gen_frequency = 0.05f;  // Чем меньше, тем больше объекты

	// 2. Интенсивность высот (насколько горы высокие)
	float gen_amplitude = 15.0f;

	// 3. Порог гор (при каком значении биома начинаются горы)
	float gen_mountain_cutoff = 0.7f;

	// 4. Кривизна (экспонента для холмов, делает их пологими или резкими)
	float gen_hill_exp = 2.0f;

	// 5. Сила искривления рек (насколько они "пьяные")
	float gen_river_warp = 10.0f;
	float gen_scale = 0.02f;      // Масштаб всего (меньше = больше объектов)
	float gen_roughness = 0.5f;   // Насколько поверхность "шершавая"
	float gen_lacunarity = 2.0f;  // Детализация между слоями
	float gen_distort = 4.0f;     // Сила искривления (убирает параллельность)
	float gen_octaves = 4;
};
generator_set g_set;
float GetVertexHeight(int x, int z) {
	if (x < 0 || x >= MAP_W || z < 0 || z >= MAP_H) return 0.0f;
	return (float)tiles[z * MAP_W + x].h;
}
float GetInterpolatedHeight(float x, float z) {
	int x0 = (int)std::floor(x);
	int z0 = (int)std::floor(z);
	float h00 = GetVertexHeight(x0, z0);
	float h10 = GetVertexHeight(x0 + 1, z0);
	float h01 = GetVertexHeight(x0, z0 + 1);
	float h11 = GetVertexHeight(x0 + 1, z0 + 1);
	float sx = x - (float)x0;
	float sz = z - (float)z0;
	float lerpTop = h00 + sx * (h10 - h00);
	float lerpBottom = h01 + sx * (h11 - h01);
	return lerpTop + sz * (lerpBottom - lerpTop);
}
void DrawMap(Camera3D camera) {
	int viewDist = (int)(camera.fovy * 1.5f);

	int startX = (int)camera.target.x - viewDist;
	int endX = (int)camera.target.x + viewDist;
	int startZ = (int)camera.target.z - viewDist;
	int endZ = (int)camera.target.z + viewDist;
	startX = std::max(0, startX);
	endX = std::min(MAP_W - 1, endX);
	startZ = std::max(0, startZ);
	endZ = std::min(MAP_H - 1, endZ);

	for (int z = startZ; z < endZ; z++) {
		for (int x = startX; x < endX; x++) {
			int idx = z * MAP_W + x;
			Tile& t = tiles[idx];
			float h00 = GetVertexHeight(x, z);        
			float h10 = GetVertexHeight(x + 1, z);    
			float h11 = GetVertexHeight(x + 1, z + 1); 
			float h01 = GetVertexHeight(x, z + 1);    
			Texture2D texture = { 0 };
			auto it = texs.find(t.tid);	
			if (it != texs.end()) texture = it->second.first;
			rlSetTexture(texture.id);
			rlBegin(RL_QUADS);
			rlColor4ub(255, 255, 255, 255);
			rlTexCoord2f(0.0f, 0.0f);
			rlVertex3f((float)x, h00, (float)z);
			rlTexCoord2f(0.0f, 1.0f);
			rlVertex3f((float)x, h01, (float)z + 1.0f);
			rlTexCoord2f(1.0f, 1.0f);
			rlVertex3f((float)x + 1.0f, h11, (float)z + 1.0f);
			rlTexCoord2f(1.0f, 0.0f);
			rlVertex3f((float)x + 1.0f, h10, (float)z);
			rlEnd();
			rlSetTexture(0);
			DrawLine3D({ (float)x, h00, (float)z }, { (float)x + 1, h10, (float)z }, DARKGRAY);
			DrawLine3D({ (float)x, h00, (float)z }, { (float)x, h01, (float)z + 1 }, DARKGRAY);
		}
	}
}
float rnd_seed() {
	std::random_device dev;
	std::mt19937 rng(dev());
	std::uniform_real_distribution<float> dist(-10.0f, 100000.0f);
	return dist(rng);
}

float get_noise(float x, float z) {
	float h = std::sin(x * 12.9898f + z * 78.233f + seed) * PI;
	return h - std::floor(h);
}
float smooth_noise(float x, float z) {
	float ix = std::floor(x);
	float iz = std::floor(z);
	float fx = x - ix;
	float fz = z - iz;
	float ux = fx * fx * (3.0f - 2.0f * fx);
	float uz = fz * fz * (3.0f - 2.0f * fz);
	float res =
		(1.0f - ux) * (1.0f - uz) * get_noise(ix, iz) +
		ux * (1.0f - uz) * get_noise(ix + 1.0f, iz) +
		(1.0f - ux) * uz * get_noise(ix, iz + 1.0f) +
		ux * uz * get_noise(ix + 1.0f, iz + 1.0f);
	return res;
}
float fbm(float x, float z) {
	float total = 0.0f;
	float amplitude = 1.0f;
	float freq = g_set.gen_scale;

	for (int i = 0; i < g_set.gen_octaves; i++) {
		total += smooth_noise(x * freq, z * freq) * amplitude;
		amplitude *= g_set.gen_roughness;
		freq *= g_set.gen_lacunarity;
	}
	return total;
}
float warped_noise(float x, float z) {
	// Мы создаем смещение координат с помощью самого шума
	float offsetX = fbm(x + 0.0f, z + 0.0f) * g_set.gen_distort;
	float offsetZ = fbm(x + 5.2f, z + 1.3f) * g_set.gen_distort;

	// Рисуем шум уже по смещенным координатам
	return fbm(x + offsetX, z + offsetZ);
}
// Функция для плавного смешивания двух значений (можно добавить smoothstep для лучшего вида)
float smooth_lerp(float a, float b, float t) {
	// Используем smoothstep (опционально)
	t = t * t * (3.0f - 2.0f * t);
	return a + t * (b - a);
}


void gen_l() {
	seed = rnd_seed();
	// Глобальный уровень моря
	const float SEA_LEVEL = 0.5f;

	for (int i = 0; i < MAP_W * MAP_H; i++) {
		float x = (float)(i % MAP_W);
		float z = (float)(i / MAP_W);

		// Используем старый шум (пока вы не перешли на Perlin/stb_perlin)
		float n = warped_noise(x, z);
		float biome = smooth_noise(x * 0.01f, z * 0.01f + seed * 0.1f);

		// 1. Вычисляем потенциальные высоты для каждого биома
		float h_plains = n * 2.0f;

		// Внимание: ваш шум возвращает [0, 1], но abs(n - 0.5) даст [0, 0.5]
		float h_river = h_plains - 6.0f;

		float h_hills = std::pow(n, g_set.gen_hill_exp) * (g_set.gen_amplitude * 0.5f);

		float ridge = 1.0f - std::abs(n * 2.0f - 1.0f); // Range [0, 1]
		float h_mounts = ridge * g_set.gen_amplitude;

		float finalHeight = 0.0f;

		// 2. Плавное смешивание биомов
		// Мы используем 3 основных зоны смешивания: Равнины-Холмы, Холмы-Горы

		if (biome < 0.3f) {
			// Переход от Равнин к Холмам
			float t = (biome - 0.08f) / (0.3f - 0.08f); // Нормализуем 't'
			finalHeight = smooth_lerp(h_plains, h_hills, t);
		}
		else {
			// Переход от Холмов к Горам
			float t = (biome - 0.3f) / (g_set.gen_mountain_cutoff - 0.3f);
			finalHeight = smooth_lerp(h_hills, h_mounts, t);
		}

		// 3. Отдельная логика для рек (прорезаем их в уже смешанном ландшафте)
		float riverLine = std::abs(n - 0.5f);
		if (riverLine < 0.04f) {
			// Используем lerp, чтобы края реки были пологими, а не резкими
			float river_t = riverLine / 0.04f; // t от 0 (центр реки) до 1 (край)
			finalHeight = smooth_lerp(h_river, finalHeight, river_t);
		}

		tiles[i].h = finalHeight;
		// Все, что ниже уровня моря - вода
		tiles[i].tid = (finalHeight < SEA_LEVEL) ? "water" : "grass";
	}
}

int main() {
	SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
	InitWindow(1920, 1000, "S-maps");
	SetTargetFPS(120);
	tiles.resize(MAP_W * MAP_H);
	//gen_l();
	Image img = GenImageColor(64, 64, WHITE);
	texs["default"] = { LoadTextureFromImage(img), "" };
	UnloadImage(img);
	Camera3D camera = { 0 };
	camera.position = { (float)MAP_W * 0.8f, (float)MAP_W * 0.8f, (float)MAP_H * 0.8f };
	camera.target = { MAP_W / 2.0f, 0.0f, MAP_H / 2.0f }; 
	camera.up = { 0.0f, 1.0f, 0.0f };
	camera.fovy = 30.0f; 
	camera.projection = CAMERA_ORTHOGRAPHIC;  
	Vector2 move;
	while (!WindowShouldClose()) {
		Vector2 ws = {GetScreenWidth(), GetScreenHeight()};
		float dt = GetFrameTime();
		if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
			Vector2 mouseDelta = GetMouseDelta();
			if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
				float rotationAngle = mouseDelta.x * -0.2f * DEG2RAD;
				float tiltAngle = mouseDelta.y * -0.2f * DEG2RAD;
				camera.position = Vector3RotateByAxisAngle(camera.position, { 0, 1, 0 }, rotationAngle);
				camera.up = Vector3RotateByAxisAngle(camera.up, { 0, 1, 0 }, rotationAngle);
				Vector3 camRight = Vector3CrossProduct(camera.up, Vector3Subtract(camera.target, camera.position));
				camRight = Vector3Normalize(camRight);
				camera.position = Vector3RotateByAxisAngle(camera.position, camRight, tiltAngle);
				camera.up = Vector3RotateByAxisAngle(camera.up, camRight, tiltAngle);
			}
			else {
				float panSpeed = camera.fovy / 100.0f;
				Vector3 forward = Vector3Subtract(camera.target, camera.position);
				Vector3 right = Vector3CrossProduct(forward, camera.up);
				right = Vector3Normalize(right);
				Vector3 up = Vector3CrossProduct(right, forward);
				up = Vector3Normalize(up);
				Vector3 rightMove = Vector3Scale(right, -mouseDelta.x * panSpeed * dt * 50.0f);
				Vector3 upMove = Vector3Scale(up, mouseDelta.y * panSpeed * dt * 50.0f); 
				camera.position = Vector3Add(camera.position, rightMove);
				camera.target = Vector3Add(camera.target, rightMove);
				camera.position = Vector3Add(camera.position, upMove);
				camera.target = Vector3Add(camera.target, upMove);
			}
		}
		camera.fovy = std::clamp(camera.fovy - GetMouseWheelMove() * 2.0f, 2.0f, 1000.0f);
		 

		BeginDrawing();
		BeginMode3D(camera);
		ClearBackground(SKYBLUE);
		// тут рисовка
		DrawMap(camera);

		EndMode3D();
		GuiToggleGroup({ 10.0f, 10.0f, ws.x * 0.1f, ws.y * 0.03f }, "TILE;OBJ;SLCT", &tool_s);
		if (GuiButton({ 10.0f, 50.0f, ws.x * 0.05f, ws.y * 0.03f }, "Generate")) gen_l();
		if (GuiButton({ 10.0f, 90.0f, ws.x * 0.05f, ws.y * 0.03f }, "Load height map"));
		GuiSlider({ 10.0f, 130.0f, ws.x * 0.05f, ws.y * 0.03f }, "", "", &g_set.gen_amplitude, 1.0f, 100.0f);
		GuiSlider({ 10.0f, 170.0f, ws.x * 0.05f, ws.y * 0.03f }, "", "", &g_set.gen_distort, 1.0f, 100.0f);
		GuiSlider({ 10.0f, 210.0f, ws.x * 0.05f, ws.y * 0.03f }, "", "", &g_set.gen_frequency, 0.00001f, 0.1f);
		GuiSlider({ 10.0f, 250.0f, ws.x * 0.05f, ws.y * 0.03f }, "", "", &g_set.gen_hill_exp, 1.0f, 100.0f);
		GuiSlider({ 10.0f, 290.0f, ws.x * 0.05f, ws.y * 0.03f }, "", "", &g_set.gen_lacunarity, 1.0f, 100.0f);
		GuiSlider({ 10.0f, 330.0f, ws.x * 0.05f, ws.y * 0.03f }, "", "", &g_set.gen_mountain_cutoff, 0.01f, 0.7f);
		GuiSlider({ 10.0f, 370.0f, ws.x * 0.05f, ws.y * 0.03f }, "", "", &g_set.gen_octaves, 1.0f, 100.0f);
		GuiSlider({ 10.0f, 410.0f, ws.x * 0.05f, ws.y * 0.03f }, "", "", &g_set.gen_river_warp, 1.0f, 100.0f);
		GuiSlider({ 10.0f, 450.0f, ws.x * 0.05f, ws.y * 0.03f }, "", "", &g_set.gen_roughness, 0.01f, 1.0f);
		GuiSlider({ 10.0f, 490.0f, ws.x * 0.05f, ws.y * 0.03f }, "", "", &g_set.gen_scale, 0.000001f, 1.0f);
		GuiPanel({ ws.x - ws.x * 0.1f, 10.0f, ws.x * 0.1f, ws.y * 0.3f }, "Textures");
		if (GuiButton({ ws.x - ws.x * 0.1f + 1.0f, 30.0f, ws.x * 0.1f - 1.0f, ws.y * 0.03f }, "Load texture"));
		GuiListViewEx({ ws.x - ws.x * 0.1f + 1.0f, 70.0f, ws.x * 0.1f - 1.0f, ws.y * 0.26f }, texs_for_list.data(), texs_for_list.size(), &sc_idx, &act_idx, &foc_idx);



		EndDrawing();
	}
	CloseWindow();
	return 0;
}