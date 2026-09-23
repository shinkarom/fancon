#include "console.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <vector>
#include <cstdio>
#include <chrono>

// ==========================================
// MATHEMATICS & 3D PRIMITIVES
// ==========================================
constexpr float PI = 3.14159265358979323846f;

struct Vec3 {
    float x, y, z;
    constexpr Vec3() : x(0), y(0), z(0) {}
    constexpr Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }

    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const {
        return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x };
    }
    Vec3 normalize() const {
        float l = std::sqrt(x * x + y * y + z * z);
        return l > 0.00001f ? Vec3{x / l, y / l, z / l} : Vec3{0, 0, 0};
    }
};

struct Vertex {
    Vec3 pos;       // Model space
    Vec3 normal;    // Normal
    Vec3 view_pos;  // Camera view space
    float sx, sy;   // Screen coords
    float inv_z;    // 1 / Z for depth interpolation
    float light;    // Calculated vertex lighting intensity
};

struct Triangle {
    int v0, v1, v2;
    Vec3 face_normal;
};

// ==========================================
// COLOR SYSTEM
// ==========================================
inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return 0xFF000000 | (b << 16) | (g << 8) | r;
}

inline uint32_t shade_color(uint32_t base, float intensity) {
    intensity = std::max(0.0f, std::min(1.5f, intensity));
    uint8_t r = std::min(255u, (uint32_t)((base & 0xFF) * intensity));
    uint8_t g = std::min(255u, (uint32_t)(((base >> 8) & 0xFF) * intensity));
    uint8_t b = std::min(255u, (uint32_t)(((base >> 16) & 0xFF) * intensity));
    return 0xFF000000 | (b << 16) | (g << 8) | r;
}

// ==========================================
// ENGINE BUFFERS & RESOLUTIONS
// ==========================================
alignas(16) static float z_buffer[MAX_RES_W * MAX_RES_H];

struct ResPreset { int w, h; const char* name; };
const ResPreset RESOLUTIONS[] = {
    {160, 90,   "160x90   (Ultra-Retro)"},
    {320, 180,  "320x180  (Retro 16:9)"},
    {480, 270,  "480x270  (Portable)"},
    {640, 360,  "640x360  (SD Widescreen)"},
    {640, 480,  "640x480  (Classic VGA)"},
    {960, 540,  "960x540  (qHD)"},
    {1280, 720, "1280x720 (720p HD)"},
    {1920, 1080,"1920x1080(1080p Full HD)"}
};
constexpr int NUM_RESOLUTIONS = sizeof(RESOLUTIONS) / sizeof(RESOLUTIONS[0]);
int current_res_idx = 3; // Default: 640x360
int game_w = 640;
int game_h = 360;

// Shading Modes
enum ShadingMode { GOURAUD = 0, FLAT, NORMALS, WIREFRAME, MODE_COUNT };
ShadingMode current_mode = GOURAUD;

// Geometry Mesh Data
std::vector<Vertex> mesh_vertices;
std::vector<Triangle> mesh_triangles;
int mesh_segments_u = 48;
int mesh_segments_v = 16;
int mesh_type = 0; // 0 = Torus Knot, 1 = Classic Torus

// Camera & Light Parameters
float cam_yaw = 0.0f;
float cam_pitch = 0.3f;
float cam_dist = 5.2f;
bool auto_rotate = true;
Vec3 light_dir = Vec3(0.577f, 0.577f, 0.577f).normalize();

int frame_counter = 0;
int triangles_rendered = 0;

// ==========================================
// PROCEDURAL MESH GENERATORS
// ==========================================
void generate_mesh() {
    mesh_vertices.clear();
    mesh_triangles.clear();

    int u_steps = mesh_segments_u;
    int v_steps = mesh_segments_v;

    for (int i = 0; i < u_steps; ++i) {
        float u = (float)i / u_steps * (2.0f * PI);

        // Curve center and tangent
        Vec3 p, p_next;
        if (mesh_type == 0) {
            // Torus Knot (p=2, q=3)
            float r = 1.2f + 0.5f * std::cos(3.0f * u);
            p = { r * std::cos(2.0f * u), r * std::sin(2.0f * u), 0.6f * std::sin(3.0f * u) };
            
            float u2 = u + 0.01f;
            float r2 = 1.2f + 0.5f * std::cos(3.0f * u2);
            p_next = { r2 * std::cos(2.0f * u2), r2 * std::sin(2.0f * u2), 0.6f * std::sin(3.0f * u2) };
        } else {
            // Standard Donut Torus
            p = { 1.6f * std::cos(u), 1.6f * std::sin(u), 0.0f };
            float u2 = u + 0.01f;
            p_next = { 1.6f * std::cos(u2), 1.6f * std::sin(u2), 0.0f };
        }

        Vec3 T = (p_next - p).normalize();
        Vec3 N = (mesh_type == 0) ? Vec3(std::cos(2.0f * u), std::sin(2.0f * u), 0.0f).normalize() : Vec3(0, 0, 1);
        Vec3 B = T.cross(N).normalize();
        N = B.cross(T).normalize();

        float tube_radius = (mesh_type == 0) ? 0.28f : 0.6f;

        for (int j = 0; j < v_steps; ++j) {
            float v = (float)j / v_steps * (2.0f * PI);
            float cx = std::cos(v) * tube_radius;
            float cy = std::sin(v) * tube_radius;

            Vertex vert;
            vert.normal = (N * cx + B * cy).normalize();
            vert.pos = p + vert.normal * tube_radius;
            mesh_vertices.push_back(vert);
        }
    }

    // Build Index Triangles
    for (int i = 0; i < u_steps; ++i) {
        int i_next = (i + 1) % u_steps;
        for (int j = 0; j < v_steps; ++j) {
            int j_next = (j + 1) % v_steps;

            int idx0 = i * v_steps + j;
            int idx1 = i_next * v_steps + j;
            int idx2 = i_next * v_steps + j_next;
            int idx3 = i * v_steps + j_next;

            // Two triangles per quad
            mesh_triangles.push_back({idx0, idx1, idx2, {}});
            mesh_triangles.push_back({idx0, idx2, idx3, {}});
        }
    }

    // Compute Face Normals
    for (auto& tri : mesh_triangles) {
        Vec3 v0 = mesh_vertices[tri.v0].pos;
        Vec3 v1 = mesh_vertices[tri.v1].pos;
        Vec3 v2 = mesh_vertices[tri.v2].pos;
        tri.face_normal = (v1 - v0).cross(v2 - v0).normalize();
    }
}

// ==========================================
// TRIANGLE SCANLINE RASTERIZER
// ==========================================
void draw_line_raw(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        if (x0 >= 0 && x0 < game_w && y0 >= 0 && y0 < game_h) {
            framebuffer[y0 * game_w + x0] = color;
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// Edge walker for scanline fill
struct Edge {
    float x, dx_dy;
    float inv_z, dinv_z_dy;
    float light, dlight_dy;
};

void rasterize_triangle(const Vertex& v0, const Vertex& v1, const Vertex& v2, uint32_t base_color) {
    // 1. Sort vertices by Y: p0.y <= p1.y <= p2.y
    const Vertex* p0 = &v0;
    const Vertex* p1 = &v1;
    const Vertex* p2 = &v2;
    if (p1->sy < p0->sy) std::swap(p0, p1);
    if (p2->sy < p0->sy) std::swap(p0, p2);
    if (p2->sy < p1->sy) std::swap(p1, p2);

    int y_start = std::max(0, (int)std::ceil(p0->sy));
    int y_end   = std::min(game_h - 1, (int)std::ceil(p2->sy) - 1);
    if (y_start > y_end) return;

    float total_height = p2->sy - p0->sy;
    if (total_height < 0.001f) return;

    // Cross product for left/right handedness
    bool right_handed = ((p1->sx - p0->sx) * (p2->sy - p0->sy) - (p1->sy - p0->sy) * (p2->sx - p0->sx)) > 0;

    for (int y = y_start; y <= y_end; ++y) {
        bool second_half = y > p1->sy || p1->sy == p0->sy;
        float segment_height = second_half ? (p2->sy - p1->sy) : (p1->sy - p0->sy);
        if (segment_height < 0.001f) continue;

        float alpha = (float)(y - p0->sy) / total_height;
        float beta  = second_half ? (float)(y - p1->sy) / segment_height : (float)(y - p0->sy) / segment_height;

        // Long Edge (p0 -> p2)
        float x_long = p0->sx + (p2->sx - p0->sx) * alpha;
        float z_long = p0->inv_z + (p2->inv_z - p0->inv_z) * alpha;
        float l_long = p0->light + (p2->light - p0->light) * alpha;

        // Split Edge (p0 -> p1 or p1 -> p2)
        float x_short = second_half ? (p1->sx + (p2->sx - p1->sx) * beta) : (p0->sx + (p1->sx - p0->sx) * beta);
        float z_short = second_half ? (p1->inv_z + (p2->inv_z - p1->inv_z) * beta) : (p0->inv_z + (p1->inv_z - p0->inv_z) * beta);
        float l_short = second_half ? (p1->light + (p2->light - p1->light) * beta) : (p0->light + (p1->light - p0->light) * beta);

        float xa = x_long, xb = x_short;
        float za = z_long, zb = z_short;
        float la = l_long, lb = l_short;

        if (!right_handed) {
            std::swap(xa, xb);
            std::swap(za, zb);
            std::swap(la, lb);
        }

        int x_min = std::max(0, (int)std::ceil(xa));
        int x_max = std::min(game_w - 1, (int)std::ceil(xb) - 1);
        float span = xb - xa;

        if (span > 0.001f && x_min <= x_max) {
            int row = y * game_w;
            float dz_dx = (zb - za) / span;
            float dl_dx = (lb - la) / span;

            float x_prestep = (float)x_min - xa;
            float z_cur = za + dz_dx * x_prestep;
            float l_cur = la + dl_dx * x_prestep;

            for (int x = x_min; x <= x_max; ++x) {
                int idx = row + x;
                // Inverse depth test (greater is closer)
                if (z_cur > z_buffer[idx]) {
                    z_buffer[idx] = z_cur;
                    framebuffer[idx] = (current_mode == GOURAUD) ? 
                                       shade_color(base_color, l_cur) : base_color;
                }
                z_cur += dz_dx;
                l_cur += dl_dx;
            }
        }
    }
}

// ==========================================
// AUDIO SYNTHESIZER (FM CHIPTUNE ARPEGGIO)
// ==========================================
const float NOTES[] = {
    220.00f, 261.63f, 329.63f, 392.00f, 440.00f, 523.25f, 659.25f, 783.99f
};
int note_step = 0;
float note_timer = 0.0f;

void synth_audio() {
    static float carrier_phase = 0.0f;
    static float mod_phase = 0.0f;

    float current_freq = NOTES[note_step];
    note_timer += (float)AUDIO_FRAMES_PER_TICK / SAMPLE_RATE;
    if (note_timer >= 0.12f) { // 120ms arpeggio step
        note_timer = 0.0f;
        note_step = (note_step + 1) % 8;
    }

    for (int i = 0; i < AUDIO_FRAMES_PER_TICK; ++i) {
        // Simple 2-operator FM synth
        float mod = std::sin(mod_phase) * 1.5f;
        float sample_f = std::sin(carrier_phase + mod) * 3500.0f;

        int16_t sample = (int16_t)sample_f;
        audio_buffer[i * 2 + 0] = sample;
        audio_buffer[i * 2 + 1] = sample;

        carrier_phase += (2.0f * PI * current_freq) / SAMPLE_RATE;
        mod_phase += (2.0f * PI * (current_freq * 2.0f)) / SAMPLE_RATE;
        if (carrier_phase > 100.0f) carrier_phase -= 100.0f;
        if (mod_phase > 100.0f) mod_phase -= 100.0f;
    }
}

// ==========================================
// LIFECYCLE HOOKS
// ==========================================
extern "C" {
    WASM_EXPORT("init")
    void init() {
        set_screen_res(game_w, game_h);
        generate_mesh();
        printf("[3D RASTERIZER] Initialized. %zu Vertices, %zu Polygons\n",
               mesh_vertices.size(), mesh_triangles.size());
    }

    WASM_EXPORT("update")
    void update() {
        frame_counter++;
        uint32_t pressed = get_btn_pressed();
        uint32_t just_pressed = get_btn_just_pressed();

        // 1. Resolution Selection
        if (just_pressed & BTN_X) {
            current_res_idx = (current_res_idx + 1) % NUM_RESOLUTIONS;
            game_w = RESOLUTIONS[current_res_idx].w;
            game_h = RESOLUTIONS[current_res_idx].h;
            set_screen_res(game_w, game_h);
            printf("[3D] Mode: %s (%'d Pixels)\n", RESOLUTIONS[current_res_idx].name, game_w * game_h);
        }
        if (just_pressed & BTN_Y) {
            current_res_idx = (current_res_idx - 1 + NUM_RESOLUTIONS) % NUM_RESOLUTIONS;
            game_w = RESOLUTIONS[current_res_idx].w;
            game_h = RESOLUTIONS[current_res_idx].h;
            set_screen_res(game_w, game_h);
            printf("[3D] Mode: %s (%'d Pixels)\n", RESOLUTIONS[current_res_idx].name, game_w * game_h);
        }

        // 2. Shading & Object Toggles
        if (just_pressed & BTN_A) {
            current_mode = (ShadingMode)((current_mode + 1) % MODE_COUNT);
            const char* names[] = {"Gouraud", "Flat", "Normal Colors", "Wireframe"};
            printf("[3D] Shading Mode: %s\n", names[current_mode]);
        }
        if (just_pressed & BTN_B) {
            mesh_type = 1 - mesh_type;
            generate_mesh();
            printf("[3D] Mesh Changed: %s (%zu tris)\n", 
                   mesh_type == 0 ? "Torus Knot" : "Classic Donut", mesh_triangles.size());
        }

        // 3. Tessellation Quality (Triangle Budget)
        if (just_pressed & BTN_R) {
            mesh_segments_u = std::min(mesh_segments_u + 12, 128);
            mesh_segments_v = std::min(mesh_segments_v + 4, 32);
            generate_mesh();
            printf("[3D] Increased Tessellation: %zu triangles\n", mesh_triangles.size());
        }
        if (just_pressed & BTN_L) {
            mesh_segments_u = std::max(mesh_segments_u - 12, 12);
            mesh_segments_v = std::max(mesh_segments_v - 4, 6);
            generate_mesh();
            printf("[3D] Decreased Tessellation: %zu triangles\n", mesh_triangles.size());
        }

        // 4. Camera Orbit Controls
        if (just_pressed & BTN_START) auto_rotate = !auto_rotate;

        if (auto_rotate) cam_yaw += 0.02f;
        if (pressed & BTN_LEFT)  cam_yaw -= 0.04f;
        if (pressed & BTN_RIGHT) cam_yaw += 0.04f;
        if (pressed & BTN_UP)    cam_pitch = std::min(cam_pitch + 0.03f, 1.4f);
        if (pressed & BTN_DOWN)  cam_pitch = std::max(cam_pitch - 0.03f, -1.4f);

        // 5. Synthesize Audio Frame
        synth_audio();
    }

    WASM_EXPORT("draw")
    void draw() {
        auto t_start = std::chrono::high_resolution_clock::now();

        // 1. Clear Screen & Depth Buffer (WASM memory optimized)
        std::fill_n(framebuffer, game_w * game_h, rgb(16, 20, 32));
        std::fill_n(z_buffer, game_w * game_h, 0.0f); // 0.0 is far plane for 1/z

        // 2. Camera View Matrix Transform
        float cos_y = std::cos(cam_yaw), sin_y = std::sin(cam_yaw);
        float cos_p = std::cos(cam_pitch), sin_p = std::sin(cam_pitch);

        Vec3 cam_pos = {
            cam_dist * cos_p * sin_y,
            cam_dist * sin_p,
            cam_dist * cos_p * cos_y
        };

        // Forward, Right, Up coordinate frame
        Vec3 forward = (Vec3(0, 0, 0) - cam_pos).normalize();
        Vec3 right = forward.cross(Vec3(0, 1, 0)).normalize();
        Vec3 up = right.cross(forward).normalize();

        float fov = game_h * 1.15f;
        float cx = game_w * 0.5f;
        float cy = game_h * 0.5f;

        // 3. Transform & Light Vertices
        for (auto& v : mesh_vertices) {
            Vec3 p = v.pos - cam_pos;
            v.view_pos = { p.dot(right), p.dot(up), p.dot(forward) };

            if (v.view_pos.z > 0.1f) {
                v.inv_z = 1.0f / v.view_pos.z;
                v.sx = cx + (v.view_pos.x * v.inv_z) * fov;
                v.sy = cy - (v.view_pos.y * v.inv_z) * fov; // Invert Y for screen coordinates

                // Diffuse + Specular Lighting Calculation
                float diffuse = std::max(0.0f, v.normal.dot(light_dir));
                Vec3 view_dir = (cam_pos - v.pos).normalize();
                Vec3 half_vec = (light_dir + view_dir).normalize();
                float specular = std::pow(std::max(0.0f, v.normal.dot(half_vec)), 16.0f);

                v.light = 0.20f + (diffuse * 0.70f) + (specular * 0.40f);
            }
        }

        // 4. Render Triangles with Backface Culling
        triangles_rendered = 0;
        uint32_t base_color = (mesh_type == 0) ? rgb(240, 90, 40) : rgb(40, 180, 240);

        for (const auto& tri : mesh_triangles) {
            const Vertex& v0 = mesh_vertices[tri.v0];
            const Vertex& v1 = mesh_vertices[tri.v1];
            const Vertex& v2 = mesh_vertices[tri.v2];

            // Near-plane clipping guard
            if (v0.view_pos.z <= 0.1f || v1.view_pos.z <= 0.1f || v2.view_pos.z <= 0.1f) continue;

            // Screen-space 2D Cross Product (Exact Backface Culling)
            float cross2d = (v1.sx - v0.sx) * (v2.sy - v0.sy) - (v1.sy - v0.sy) * (v2.sx - v0.sx);
            if (cross2d <= 0.0f) continue; // Face points away from camera!

            triangles_rendered++;

            if (current_mode == WIREFRAME) {
                uint32_t green = rgb(50, 255, 100);
                draw_line_raw((int)v0.sx, (int)v0.sy, (int)v1.sx, (int)v1.sy, green);
                draw_line_raw((int)v1.sx, (int)v1.sy, (int)v2.sx, (int)v2.sy, green);
                draw_line_raw((int)v2.sx, (int)v2.sy, (int)v0.sx, (int)v0.sy, green);
            } else if (current_mode == NORMALS) {
                // Visualize face normal as RGB color
                uint32_t n_col = rgb((uint8_t)((tri.face_normal.x * 0.5f + 0.5f) * 255),
                                     (uint8_t)((tri.face_normal.y * 0.5f + 0.5f) * 255),
                                     (uint8_t)((tri.face_normal.z * 0.5f + 0.5f) * 255));
                rasterize_triangle(v0, v1, v2, n_col);
            } else if (current_mode == FLAT) {
                float diff = std::max(0.15f, tri.face_normal.dot(light_dir));
                rasterize_triangle(v0, v1, v2, shade_color(base_color, diff));
            } else { // GOURAUD
                rasterize_triangle(v0, v1, v2, base_color);
            }
        }

        // 5. Telemetry & Microsecond Budget Profiler
        auto t_end = std::chrono::high_resolution_clock::now();
        auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start).count();
        if (frame_counter % 120 == 0) {
            printf("[3D PROFILER] %zu Tris | Rasterized: %d | Time: %4lld µs / 16,666 µs (%2.1f%% budget)\n",
                   mesh_triangles.size(), triangles_rendered, duration_us, (duration_us / 16666.0f) * 100.0f);
        }
    }
}