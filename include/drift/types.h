#ifndef DRIFT_TYPES_H
#define DRIFT_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Export macros ---- */
#ifdef _WIN32
    #define DRIFT_API __declspec(dllexport)
#else
    #define DRIFT_API __attribute__((visibility("default")))
#endif

/* ---- Result ---- */
typedef enum DriftResult {
    DRIFT_OK = 0,
    DRIFT_ERROR_INIT_FAILED,
    DRIFT_ERROR_INVALID_PARAM,
    DRIFT_ERROR_NOT_FOUND,
    DRIFT_ERROR_OUT_OF_MEMORY,
    DRIFT_ERROR_IO,
    DRIFT_ERROR_FORMAT,
    DRIFT_ERROR_GPU,
    DRIFT_ERROR_AUDIO,
    DRIFT_ERROR_ALREADY_EXISTS,
} DriftResult;

/* ---- Math types ---- */
typedef struct DriftVec2 {
    float x, y;
} DriftVec2;

typedef struct DriftVec3 {
    float x, y, z;
} DriftVec3;

typedef struct DriftVec4 {
    float x, y, z, w;
} DriftVec4;

typedef struct DriftRect {
    float x, y, w, h;
} DriftRect;

typedef struct DriftColor {
    uint8_t r, g, b, a;
} DriftColor;

typedef struct DriftColorF {
    float r, g, b, a;
} DriftColorF;

typedef struct DriftMat3 {
    float m[9]; /* column-major 3x3 */
} DriftMat3;

/* ---- Commonly used constants ---- */
#define DRIFT_COLOR_WHITE   ((DriftColor){255, 255, 255, 255})
#define DRIFT_COLOR_BLACK   ((DriftColor){0, 0, 0, 255})
#define DRIFT_COLOR_RED     ((DriftColor){255, 0, 0, 255})
#define DRIFT_COLOR_GREEN   ((DriftColor){0, 255, 0, 255})
#define DRIFT_COLOR_BLUE    ((DriftColor){0, 0, 255, 255})

#define DRIFT_VEC2_ZERO     ((DriftVec2){0.0f, 0.0f})
#define DRIFT_VEC2_ONE      ((DriftVec2){1.0f, 1.0f})

/* ---- Handle types ---- */
typedef struct DriftAssetHandle { uint32_t id; } DriftAssetHandle;
typedef struct DriftTexture { uint32_t id; } DriftTexture;
typedef struct DriftSpritesheet { uint32_t id; } DriftSpritesheet;
typedef struct DriftAnimation { uint32_t id; } DriftAnimation;
typedef struct DriftSound { uint32_t id; } DriftSound;
typedef struct DriftPlayingSound { uint32_t id; } DriftPlayingSound;
typedef struct DriftFont { uint32_t id; } DriftFont;
typedef struct DriftTilemap { uint32_t id; } DriftTilemap;
typedef struct DriftEmitter { uint32_t id; } DriftEmitter;
typedef struct DriftPlugin { uint32_t id; } DriftPlugin;
typedef struct DriftCamera { uint32_t id; } DriftCamera;

/* ---- ECS types ---- */
typedef uint64_t DriftEntity;
typedef uint64_t DriftComponentId;
typedef uint64_t DriftSystemId;

#define DRIFT_INVALID_ENTITY ((DriftEntity)0)
#define DRIFT_INVALID_HANDLE_ID 0

/* ---- Flip flags ---- */
typedef enum DriftFlip {
    DRIFT_FLIP_NONE = 0,
    DRIFT_FLIP_H    = 1 << 0,
    DRIFT_FLIP_V    = 1 << 1,
} DriftFlip;

/* ---- Body types ---- */
typedef enum DriftBodyType {
    DRIFT_BODY_STATIC = 0,
    DRIFT_BODY_DYNAMIC,
    DRIFT_BODY_KINEMATIC,
} DriftBodyType;

/* ---- Physics handle ---- */
typedef struct DriftBody { int32_t index; int16_t world; uint16_t revision; } DriftBody;
typedef struct DriftShape { int32_t index; int16_t world; uint16_t revision; } DriftShape;

/* ---- Transform2D (ECS built-in component) ---- */
typedef struct DriftTransform2D {
    DriftVec2 position;
    float rotation; /* radians */
    DriftVec2 scale;
} DriftTransform2D;

static inline DriftTransform2D drift_transform2d_default(void) {
    DriftTransform2D t;
    t.position = DRIFT_VEC2_ZERO;
    t.rotation = 0.0f;
    t.scale = DRIFT_VEC2_ONE;
    return t;
}

/* ---- Asset types enum ---- */
typedef enum DriftAssetType {
    DRIFT_ASSET_TEXTURE = 0,
    DRIFT_ASSET_SOUND,
    DRIFT_ASSET_FONT,
    DRIFT_ASSET_SPRITESHEET,
    DRIFT_ASSET_TILEMAP,
    DRIFT_ASSET_COUNT_,
} DriftAssetType;

/* ---- Shape def ---- */
typedef struct DriftCollisionFilter {
    uint32_t category;
    uint32_t mask;
} DriftCollisionFilter;

typedef struct DriftShapeDef {
    float density;
    float friction;
    float restitution;
    bool is_sensor;
    DriftCollisionFilter filter;
} DriftShapeDef;

static inline DriftShapeDef drift_shape_def_default(void) {
    DriftShapeDef d;
    d.density = 1.0f;
    d.friction = 0.3f;
    d.restitution = 0.0f;
    d.is_sensor = false;
    d.filter.category = 0x0001;
    d.filter.mask = 0xFFFF;
    return d;
}

/* ---- Body def ---- */
typedef struct DriftBodyDef {
    DriftBodyType type;
    DriftVec2 position;
    float rotation;
    bool fixed_rotation;
    float linear_damping;
    float angular_damping;
    float gravity_scale;
} DriftBodyDef;

static inline DriftBodyDef drift_body_def_default(void) {
    DriftBodyDef d;
    d.type = DRIFT_BODY_DYNAMIC;
    d.position = DRIFT_VEC2_ZERO;
    d.rotation = 0.0f;
    d.fixed_rotation = false;
    d.linear_damping = 0.0f;
    d.angular_damping = 0.0f;
    d.gravity_scale = 1.0f;
    return d;
}

/* ---- Raycast ---- */
typedef struct DriftRaycastHit {
    DriftShape shape;
    DriftVec2 point;
    DriftVec2 normal;
    float fraction;
    bool hit;
} DriftRaycastHit;

/* ---- Contact events ---- */
typedef struct DriftContactEvent {
    DriftShape shape_a;
    DriftShape shape_b;
} DriftContactEvent;

typedef struct DriftHitEvent {
    DriftShape shape_a;
    DriftShape shape_b;
    DriftVec2 normal;
    float speed;
} DriftHitEvent;

/* ---- Particle emitter def ---- */
typedef struct DriftEmitterDef {
    DriftTexture texture;
    DriftRect src_rect;
    float spawn_rate;       /* particles per second */
    float lifetime_min;
    float lifetime_max;
    float speed_min;
    float speed_max;
    float angle_min;        /* radians */
    float angle_max;        /* radians */
    DriftColorF color_start;
    DriftColorF color_end;
    float size_start;
    float size_end;
    float gravity;
    uint32_t max_particles;
} DriftEmitterDef;

static inline DriftEmitterDef drift_emitter_def_default(void) {
    DriftEmitterDef d;
    d.texture.id = DRIFT_INVALID_HANDLE_ID;
    d.src_rect = (DriftRect){0, 0, 1, 1};
    d.spawn_rate = 10.0f;
    d.lifetime_min = 0.5f;
    d.lifetime_max = 1.5f;
    d.speed_min = 10.0f;
    d.speed_max = 50.0f;
    d.angle_min = 0.0f;
    d.angle_max = 6.2831853f; /* 2*PI */
    d.color_start = (DriftColorF){1, 1, 1, 1};
    d.color_end = (DriftColorF){1, 1, 1, 0};
    d.size_start = 4.0f;
    d.size_end = 1.0f;
    d.gravity = 0.0f;
    d.max_particles = 256;
    return d;
}

/* ---- UI theme ---- */
typedef struct DriftUITheme {
    DriftColorF primary;
    DriftColorF secondary;
    DriftColorF background;
    DriftColorF text;
    DriftColorF button;
    DriftColorF button_hover;
    DriftColorF button_active;
    float rounding;
    float padding;
} DriftUITheme;

static inline DriftUITheme drift_ui_theme_default(void) {
    DriftUITheme t;
    t.primary       = (DriftColorF){0.2f, 0.6f, 0.9f, 1.0f};
    t.secondary     = (DriftColorF){0.1f, 0.1f, 0.15f, 1.0f};
    t.background    = (DriftColorF){0.05f, 0.05f, 0.08f, 0.9f};
    t.text          = (DriftColorF){0.95f, 0.95f, 0.95f, 1.0f};
    t.button        = (DriftColorF){0.2f, 0.2f, 0.3f, 1.0f};
    t.button_hover  = (DriftColorF){0.3f, 0.3f, 0.45f, 1.0f};
    t.button_active = (DriftColorF){0.15f, 0.15f, 0.25f, 1.0f};
    t.rounding = 4.0f;
    t.padding = 8.0f;
    return t;
}

/* ---- Config ---- */
typedef struct DriftConfig {
    const char* title;
    int32_t width;
    int32_t height;
    bool fullscreen;
    bool vsync;
    bool resizable;
} DriftConfig;

static inline DriftConfig drift_config_default(void) {
    DriftConfig c;
    c.title = "Drift";
    c.width = 1280;
    c.height = 720;
    c.fullscreen = false;
    c.vsync = true;
    c.resizable = true;
    return c;
}

/* ---- ECS system phases ---- */
typedef enum DriftPhase {
    DRIFT_PHASE_STARTUP = 0,
    DRIFT_PHASE_PRE_UPDATE,
    DRIFT_PHASE_UPDATE,
    DRIFT_PHASE_POST_UPDATE,
    DRIFT_PHASE_RENDER,
} DriftPhase;

/* ---- Anchor for UI positioning ---- */
typedef enum DriftAnchor {
    DRIFT_ANCHOR_TOP_LEFT = 0,
    DRIFT_ANCHOR_TOP_CENTER,
    DRIFT_ANCHOR_TOP_RIGHT,
    DRIFT_ANCHOR_CENTER_LEFT,
    DRIFT_ANCHOR_CENTER,
    DRIFT_ANCHOR_CENTER_RIGHT,
    DRIFT_ANCHOR_BOTTOM_LEFT,
    DRIFT_ANCHOR_BOTTOM_CENTER,
    DRIFT_ANCHOR_BOTTOM_RIGHT,
} DriftAnchor;

#ifdef __cplusplus
}
#endif

#endif /* DRIFT_TYPES_H */
