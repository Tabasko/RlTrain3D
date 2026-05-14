#include <stdlib.h>
#include <stdio.h>
#include "../types.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#define RLIGHTS_IMPLEMENTATION
#include "../external/rlights.h"

#include "raymath.h"

#define MAP_SIZE 128

static Mesh          mesh;
static Shader        shader;
static Texture       texture;
static Texture       light;
static Material      material;
static RenderTexture lightmap;
static Shader lightShader;
static Shader fogShader;
static Vector2 lightmapScroll = {0.0f, 0.0f};
Light lights[MAX_LIGHTS] = { 0 };

void CreateFog(void){

        // Load shader and set up some uniforms
    fogShader = LoadShader(TextFormat("resources/shaders/glsl%i/lighting.vs", GLSL_VERSION),
                               TextFormat("resources/shaders/glsl%i/fog.fs", GLSL_VERSION));
    fogShader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(fogShader, "matModel");
    fogShader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(fogShader, "viewPos");

    // Ambient light level
    Vector4 ambient = (Vector4){ 0.2f, 0.2f, 0.2f, 1.0f };
    int ambientLoc = GetShaderLocation(shader, "ambient");
    SetShaderValue(shader, ambientLoc, &ambient, SHADER_UNIFORM_VEC4);

    Vector4 fogColor = ColorNormalize(GRAY);
    int fogColorLoc = GetShaderLocation(shader, "fogColor");
    SetShaderValue(shader, fogColorLoc, &fogColor, SHADER_UNIFORM_VEC4);

    float fogDensity = 0.15f;
    int fogDensityLoc = GetShaderLocation(shader, "fogDensity");
    SetShaderValue(shader, fogDensityLoc, &fogDensity, SHADER_UNIFORM_FLOAT);

    // Using just 1 point lights
    CreateLight(LIGHT_POINT, (Vector3){ 0, 2, 6 }, Vector3Zero(), BLUE, shader);

}

void CreateLight(){
    lightShader = LoadShader(TextFormat("resources/shaders/glsl%i/lighting.vs", GLSL_VERSION),
                             TextFormat("resources/shaders/glsl%i/lighting.fs", GLSL_VERSION));

    lightShader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(lightShader, "viewPos");

    int ambientLoc = GetShaderLocation(lightShader, "ambient");
    float ambient[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
    SetShaderValue(lightShader, ambientLoc, ambient, SHADER_UNIFORM_VEC4);

    lights[0] = CreateLight(LIGHT_POINT, Vector3{ -2.0f,  1.0f, -2.0f }, Vector3Zero(), YELLOW, lightShader);
    lights[1] = CreateLight(LIGHT_POINT, Vector3{  2.0f,  1.0f,  2.0f }, Vector3Zero(), RED,    lightShader);
    lights[2] = CreateLight(LIGHT_POINT, Vector3{ -2.0f,  1.0f,  2.0f }, Vector3Zero(), GREEN,  lightShader);
    lights[3] = CreateLight(LIGHT_POINT, Vector3{  2.0f,  1.0f, -2.0f }, Vector3Zero(), BLUE,   lightShader);
}

void EnvironmentCreate(void) {

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

    texture = LoadTexture("resources/cubicmap_atlas.png");
    // texture = LoadTexture("resources/Ground048_1K-JPG_Color.jpg");
    light   = LoadTexture("resources/spark_flame.png");
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
            DrawTexturePro(light,
                Rectangle{0, 0, (float)light.width, (float)light.height},
                Rectangle{0, 0, 2.0f * MAP_SIZE, 2.0f * MAP_SIZE},
                Vector2{(float)MAP_SIZE, (float)MAP_SIZE}, 0.0f, RED);
            DrawTexturePro(light,
                Rectangle{0, 0, (float)light.width, (float)light.height},
                Rectangle{(float)MAP_SIZE * 0.8f, (float)MAP_SIZE / 2.0f, 2.0f * MAP_SIZE, 2.0f * MAP_SIZE},
                Vector2{(float)MAP_SIZE, (float)MAP_SIZE}, 0.0f, BLUE);
            DrawTexturePro(light,
                Rectangle{0, 0, (float)light.width, (float)light.height},
                Rectangle{(float)MAP_SIZE * 0.8f, (float)MAP_SIZE * 0.8f, (float)MAP_SIZE, (float)MAP_SIZE},
                Vector2{(float)MAP_SIZE / 2.0f, (float)MAP_SIZE / 2.0f}, 0.0f, GREEN);
        BeginBlendMode(BLEND_ALPHA);
    EndTextureMode();

    GenTextureMipmaps(&lightmap.texture);
    SetTextureFilter(lightmap.texture, TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(lightmap.texture, TEXTURE_WRAP_REPEAT);
}

void EnvironmentGroundDraw(void) {
    DrawText(TextFormat("LIGHTMAP: %ix%i pixels", MAP_SIZE, MAP_SIZE),
             GetRenderWidth() - 130, 20 + MAP_SIZE * 8, 24, GREEN);
    DrawFPS(10, 10);
}

void EnvironmentGroundDraw3D(void) {
    float dt = GetFrameTime();
    lightmapScroll.x = fmodf(lightmapScroll.x + 0.01f * dt, 1.0f);
    lightmapScroll.y = fmodf(lightmapScroll.y + 0.004f * dt, 1.0f);

    float uvs[8] = {
        lightmapScroll.x,        lightmapScroll.y,
        1.0f + lightmapScroll.x, lightmapScroll.y,
        lightmapScroll.x,        1.0f + lightmapScroll.y,
        1.0f + lightmapScroll.x, 1.0f + lightmapScroll.y,
    };
    rlUpdateVertexBuffer(mesh.vboId[SHADER_LOC_VERTEX_TEXCOORD02], uvs, sizeof(uvs), 0);

    DrawMesh(mesh, material, MatrixIdentity());

    // Draw spheres to show where the lights are
    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        if (lights[i].enabled) DrawSphereEx(lights[i].position, 0.2f, 8, 8, lights[i].color);
        else DrawSphereWires(lights[i].position, 0.2f, 8, 8, ColorAlpha(lights[i].color, 0.3f));
    }
}

void EnvironmentDestroy(void) {
    UnloadMesh(mesh);
    UnloadShader(shader);
    UnloadShader(lightShader);   // Unload shader
    UnloadShader(fogShader);   // Unload shader
    UnloadTexture(texture);
    UnloadTexture(light);
}
