#include "../state/game_state.h"
#include "../types.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "../colors.h"

#define RLIGHTS_IMPLEMENTATION
#include "../external/rlights.h"

#include "environment.h"

static Mesh          s_ground_mesh;
static Shader        s_shader;
static Texture       s_ground_tex;
static Texture       s_light_tex;
static Material      s_ground_mat;
static RenderTexture s_lightmap;
static Light         s_sun;
static Vector2       s_lightmap_scroll = { 0.0f, 0.0f };

EnvironmentSystem environment_system;

void EnvironmentSystem::Init() {
    s_shader = LoadShader("resources/shaders/glsl330/lightmap_lit.vs",
                           "resources/shaders/glsl330/lightmap_lit.fs");
    s_shader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(s_shader, "viewPos");

    // The fragment shader divides ambient by 10 internally, so 4.0 → effective 0.4.
    int ambientLoc = GetShaderLocation(s_shader, "ambient");
    float ambient[4] = { 2.0f, 2.0f, 2.0f, 1.0f };
    SetShaderValue(s_shader, ambientLoc, ambient, SHADER_UNIFORM_VEC4);

    // Directional sun light angled from above.
    s_sun = CreateLight(LIGHT_DIRECTIONAL,
                        Vector3{ -50.0f, 100.0f, -50.0f },
                        Vector3{   0.0f,   0.0f,   0.0f },
                        WHITE,
                        s_shader);

    s_ground_mesh = GenMeshPlane((float)MAP_SIZE, (float)MAP_SIZE, 1, 1);

    // Tile the albedo texture every 1 world unit instead of stretching it.
    for (int i = 0; i < s_ground_mesh.vertexCount * 2; i++)
        s_ground_mesh.texcoords[i] *= (float)MAP_SIZE;
    rlUpdateVertexBuffer(s_ground_mesh.vboId[1], s_ground_mesh.texcoords,
                         s_ground_mesh.vertexCount * 2 * sizeof(float), 0);

    // Second UV channel for the scrolling lightmap — one quad covering the whole plane.
    s_ground_mesh.texcoords2 = (float *)RL_MALLOC(s_ground_mesh.vertexCount * 2 * sizeof(float));
    s_ground_mesh.texcoords2[0] = 0.0f; s_ground_mesh.texcoords2[1] = 0.0f;
    s_ground_mesh.texcoords2[2] = 1.0f; s_ground_mesh.texcoords2[3] = 0.0f;
    s_ground_mesh.texcoords2[4] = 0.0f; s_ground_mesh.texcoords2[5] = 1.0f;
    s_ground_mesh.texcoords2[6] = 1.0f; s_ground_mesh.texcoords2[7] = 1.0f;

    s_ground_mesh.vboId[SHADER_LOC_VERTEX_TEXCOORD02] = rlLoadVertexBuffer(
        s_ground_mesh.texcoords2, s_ground_mesh.vertexCount * 2 * sizeof(float), false);
    rlEnableVertexArray(s_ground_mesh.vaoId);
    rlSetVertexAttribute(5, 2, RL_FLOAT, 0, 0, 0);
    rlEnableVertexAttribute(5);
    rlDisableVertexArray();

    s_ground_tex = LoadTexture("resources/kenny/kenny_pattern_pack/Tiles (Color)/tile_0050.png");
    GenTextureMipmaps(&s_ground_tex);
    SetTextureFilter(s_ground_tex, TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(s_ground_tex, TEXTURE_WRAP_REPEAT);

    s_light_tex = LoadTexture("resources/kenny/kenney_particle-pack/PNG (Transparent)/smoke_01.png");

    // Render colored light blobs into the lightmap texture once at startup.
    s_lightmap = LoadRenderTexture(MAP_SIZE, MAP_SIZE);
    BeginTextureMode(s_lightmap);
        ClearBackground(BLACK);
        BeginBlendMode(BLEND_ADDITIVE);
            DrawTexturePro(s_light_tex,
                Rectangle{ 0, 0, (float)s_light_tex.width, (float)s_light_tex.height },
                Rectangle{ (float)MAP_SIZE * 0.4f, (float)MAP_SIZE / 4.0f, MAP_SIZE, 2.0f },
                Vector2{ (float)MAP_SIZE, (float)MAP_SIZE }, 0.0f, RED);
            DrawTexturePro(s_light_tex,
                Rectangle{ 0, 0, (float)s_light_tex.width, (float)s_light_tex.height },
                Rectangle{ (float)MAP_SIZE * 0.8f, (float)MAP_SIZE / 2.0f, 2.0f * MAP_SIZE, 2.0f * MAP_SIZE },
                Vector2{ (float)MAP_SIZE, (float)MAP_SIZE }, 0.0f, BLUE);
            DrawTexturePro(s_light_tex,
                Rectangle{ 0, 0, (float)s_light_tex.width, (float)s_light_tex.height },
                Rectangle{ (float)MAP_SIZE * 0.8f, (float)MAP_SIZE * 0.8f, (float)MAP_SIZE, (float)MAP_SIZE },
                Vector2{ (float)MAP_SIZE, (float)MAP_SIZE }, 0.0f, GREEN);
        BeginBlendMode(BLEND_ALPHA);
    EndTextureMode();
    GenTextureMipmaps(&s_lightmap.texture);
    SetTextureFilter(s_lightmap.texture, TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(s_lightmap.texture, TEXTURE_WRAP_REPEAT);

    s_ground_mat = LoadMaterialDefault();
    s_ground_mat.shader = s_shader;
    s_ground_mat.maps[MATERIAL_MAP_ALBEDO].texture   = s_ground_tex;
    s_ground_mat.maps[MATERIAL_MAP_METALNESS].texture = s_lightmap.texture;
}

void EnvironmentSystem::Update() {
    float cam_pos[3] = {
        gs.camera.cam.position.x,
        gs.camera.cam.position.y,
        gs.camera.cam.position.z,
    };
    SetShaderValue(s_shader, s_shader.locs[SHADER_LOC_VECTOR_VIEW], cam_pos, SHADER_UNIFORM_VEC3);
}

void EnvironmentSystem::Draw3D() {
    float dt = GetFrameTime();
    s_lightmap_scroll.x = fmodf(s_lightmap_scroll.x + 0.0075f * dt, 1.0f);
    s_lightmap_scroll.y = fmodf(s_lightmap_scroll.y + 0.004f  * dt, 1.0f);

    float uvs[8] = {
        s_lightmap_scroll.x,        s_lightmap_scroll.y,
        1.0f + s_lightmap_scroll.x, s_lightmap_scroll.y,
        s_lightmap_scroll.x,        1.0f + s_lightmap_scroll.y,
        1.0f + s_lightmap_scroll.x, 1.0f + s_lightmap_scroll.y,
    };
    rlUpdateVertexBuffer(s_ground_mesh.vboId[SHADER_LOC_VERTEX_TEXCOORD02], uvs, sizeof(uvs), 0);

    DrawMesh(s_ground_mesh, s_ground_mat, MatrixIdentity());
}

void EnvironmentSystem::Destroy() {
    UnloadMesh(s_ground_mesh);
    UnloadShader(s_shader);
    UnloadTexture(s_ground_tex);
    UnloadTexture(s_light_tex);
    UnloadRenderTexture(s_lightmap);
}
