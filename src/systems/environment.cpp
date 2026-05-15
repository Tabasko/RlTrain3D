#include <stdlib.h>
#include "../types.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "../colors.h"

#define RLIGHTS_IMPLEMENTATION
#include "../external/rlights.h"

#include "raymath.h"
#include "environment.h"


static Mesh          mesh;
static Shader        shader;
static Texture       texture;
static Texture       light;
static Material      material;
static RenderTexture lightmap;
static Shader lightShader;
static Shader fogShader;
static Vector2 lightmapScroll = {0.0f, 0.0f};

void EnvironmentInit(void) {

    //CreateLight();
    //CreateFog();

    mesh = GenMeshPlane((float)MAP_SIZE, (float)MAP_SIZE, 1, 1);

    // Tile the albedo texture every 1 world unit instead of stretching across the plane
    for (int i = 0; i < mesh.vertexCount * 2; i++)
        mesh.texcoords[i] *= (float)MAP_SIZE;
    rlUpdateVertexBuffer(mesh.vboId[1], mesh.texcoords, mesh.vertexCount * 2 * sizeof(float), 0);

    mesh.texcoords2 = (float *)RL_MALLOC(mesh.vertexCount * 2 * sizeof(float));
    mesh.texcoords2[0] = 0.0f; mesh.texcoords2[1] = 0.0f;
    mesh.texcoords2[2] = 1.0f; mesh.texcoords2[3] = 0.0f;
    mesh.texcoords2[4] = 0.0f; mesh.texcoords2[5] = 1.0f;
    mesh.texcoords2[6] = 1.0f; mesh.texcoords2[7] = 1.0f;

    mesh.vboId[SHADER_LOC_VERTEX_TEXCOORD02] = rlLoadVertexBuffer(mesh.texcoords2, mesh.vertexCount * 2 * sizeof(float), false);
    rlEnableVertexArray(mesh.vaoId);
    rlSetVertexAttribute(5, 2, RL_FLOAT, 0, 0, 0);
    rlEnableVertexAttribute(5);
    rlDisableVertexArray();

    shader = LoadShader(
        TextFormat("resources/shaders/glsl%i/lightmap.vs", GLSL_VERSION),
        TextFormat("resources/shaders/glsl%i/lightmap.fs", GLSL_VERSION));

    // texture = LoadTexture("resources/cubicmap_atlas.png");
    texture = LoadTexture("resources/kenny/kenny_pattern_pack/Tiles (Color)/tile_0050.png");
    light   = LoadTexture("resources/kenny/kenney_particle-pack/PNG (Transparent)/smoke_01.png");
    GenTextureMipmaps(&texture);
    SetTextureFilter(texture, TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(texture, TEXTURE_WRAP_REPEAT);

    lightmap = LoadRenderTexture(MAP_SIZE, MAP_SIZE);

    material = LoadMaterialDefault();
    material.shader = shader;
    material.maps[MATERIAL_MAP_ALBEDO].texture   = texture;
    material.maps[MATERIAL_MAP_METALNESS].texture = lightmap.texture;

    BeginTextureMode(lightmap);
        ClearBackground(BLACK);
        BeginBlendMode(BLEND_ADDITIVE);
            // DrawTexturePro(light,
            //     Rectangle{0, 0, (float)light.width, (float)light.height},
            //     Rectangle{0, 0, 2.0f * MAP_SIZE, 2.0f * MAP_SIZE},
            //     Vector2{(float)MAP_SIZE, (float)MAP_SIZE}, 0.0f, RED);
            // DrawTexturePro(light,
            //     Rectangle{0, 0, (float)light.width, (float)light.height},
            //     Rectangle{(float)MAP_SIZE * 0.8f, (float)MAP_SIZE / 2.0f, 2.0f * MAP_SIZE, 2.0f * MAP_SIZE},
            //     Vector2{(float)MAP_SIZE, (float)MAP_SIZE}, 0.0f, BLUE);
            // DrawTexturePro(light,
            //     Rectangle{0, 0, (float)light.width, (float)light.height},
            //     Rectangle{(float)MAP_SIZE * 0.8f, (float)MAP_SIZE * 0.8f, (float)MAP_SIZE, (float)MAP_SIZE},
            //     Vector2{(float)MAP_SIZE / 2.0f, (float)MAP_SIZE / 2.0f}, 0.0f, GREEN);
            DrawTexturePro(light,
                Rectangle{0, 0, (float)light.width, (float)light.height},
                Rectangle{(float)MAP_SIZE * 0.8f, (float)MAP_SIZE / 2.0f, 2.0f * MAP_SIZE, 2.0f * MAP_SIZE},
                Vector2{(float)MAP_SIZE, (float)MAP_SIZE}, 0.0f, BLUE);
        BeginBlendMode(BLEND_ALPHA);
    EndTextureMode();

    GenTextureMipmaps(&lightmap.texture);
    SetTextureFilter(lightmap.texture, TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(lightmap.texture, TEXTURE_WRAP_REPEAT);
}


void EnvironmentGroundDraw3D(void) {
    float dt = GetFrameTime();
    lightmapScroll.x = fmodf(lightmapScroll.x + 0.005f * dt, 1.0f);
    lightmapScroll.y = fmodf(lightmapScroll.y + 0.002f * dt, 1.0f);

    float uvs[8] = {
        lightmapScroll.x,        lightmapScroll.y,
        1.0f + lightmapScroll.x, lightmapScroll.y,
        lightmapScroll.x,        1.0f + lightmapScroll.y,
        1.0f + lightmapScroll.x, 1.0f + lightmapScroll.y,
    };
    rlUpdateVertexBuffer(mesh.vboId[SHADER_LOC_VERTEX_TEXCOORD02], uvs, sizeof(uvs), 0);

    DrawMesh(mesh, material, MatrixIdentity());

}

void EnvironmentDestroy(void) {
    UnloadMesh(mesh);
    UnloadShader(shader);
    UnloadShader(lightShader);   // Unload shader
    UnloadShader(fogShader);   // Unload shader
    UnloadTexture(texture);
    UnloadTexture(light);
}
