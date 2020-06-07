#include <assert.h>
#include <chrono>
#include <future>
#include <limits>
#include <math.h>
#include <utility>
#include <vector>

using std::move;

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>

#include <random>

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

using glm::vec3;
using glm::ivec3;

#include "SDL2/SDL.h"
#include "SDL2/SDL_opengl.h"

static int screen_x = 1280;
static int screen_y = 960;
constexpr float fovy_radians = 1.0f;
constexpr float near_plane = 0.01f;
constexpr float far_plane = 400.0f;
constexpr float radius_speed = 10.0f;

static SDL_GLContext gl_context = nullptr;
static SDL_Window* window = nullptr;
static std::string argv0;

static glm::mat4 view;
static glm::mat4 projection;
static vec3 eye;

// Structure for holding the position, color, and radius of an
// on-screen particle. These are produced by interpolating the state
// between simulation steps.
//
// DO NOT add, remove, reorder, or otherwise mess with this
// struct. This is/will be used to communicate between C++, C#, Unity,
// and OpenGL and assumptions about the memory layout and size of
// this class are all over the place in this code.
struct visual_particle
{
    // Formerly struct particle_render_info
    float x = 0;
    float y = 0;
    float z = 0;
    float red = 0;
    float green = 0;
    float blue = 0;
    float radius = 0.0f;
};



// *** Boring OpenGL utility functions. ***

#define PANIC_IF_GL_ERROR(gl) do { \
    if (auto PANIC_error = gl.GetError()) { \
        char PANIC_msg[30]; \
        sprintf(PANIC_msg, "line %i: code %i", __LINE__, (int)PANIC_error); \
        panic("OpenGL error", PANIC_msg); \
    } \
} while (0)


static void panic(const char* message, const char* reason) {
    fprintf(stderr, "%s: %s %s\n", argv0.c_str(), message, reason);
    fflush(stderr);
    fflush(stdout);
    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_ERROR, message, reason, nullptr);
    exit(1);
    abort();
}

// OpenGL functions will be accessed through function pointers stored in a
// struct named gl, instead of using the typical C gl* forms (e.g. glEnable
// becomes gl.Enable). SDL2 is tasked with loading these function pointers.
// I do this because I don't feel like dealing with cross-platform OpenGL
// context setup and loading (especially not on Win Doze).
struct OpenGL_Functions;
typedef struct OpenGL_Functions const& GL;

static void* get_gl_function(const char* name) {
    static bool initialized = false;

    if (!initialized) {
        window = SDL_CreateWindow(
            "Bedrock Particles",
            SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
            screen_x, screen_y,
            SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

        if (window == nullptr) {
            panic("Could not initialize window", SDL_GetError());
        }
        // OpenGL 3.3 needed for delicious instanced rendering.
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        gl_context = SDL_GL_CreateContext(window);

        if (gl_context == nullptr) {
            panic("Could not initialize OpenGL 3.3", SDL_GetError());
        }
        initialized = true;
    }

    void* result = SDL_GL_GetProcAddress(name);
    if (result == nullptr) panic(name, "Missing OpenGL function");
    return result;
}

// Bunch of OpenGL functions that I (maybe) need.
struct OpenGL_Functions {

#define GL_FUNCTION(type, name, prototype) \
    type(APIENTRY * name) prototype = \
        (type(APIENTRY *) prototype)(get_gl_function("gl" #name))

GL_FUNCTION(GLenum, GetError, (void));
GL_FUNCTION(void, Enable, (GLenum));
GL_FUNCTION(void, Clear, (GLbitfield));
GL_FUNCTION(void, Disable, (GLenum));
GL_FUNCTION(void, FrontFace, (GLenum));
GL_FUNCTION(void, CullFace, (GLenum));
GL_FUNCTION(void, ClearColor, (GLclampf, GLclampf, GLclampf, GLclampf));
GL_FUNCTION(GLint, GetUniformLocation, (GLuint, const GLchar*));
GL_FUNCTION(void, Viewport, (GLint, GLint, GLsizei, GLsizei));

GL_FUNCTION(void, GenVertexArrays, (GLsizei, GLuint*));
GL_FUNCTION(void, GenBuffers, (GLsizei, GLuint*));
GL_FUNCTION(void, BindVertexArray, (GLuint));
GL_FUNCTION(void, BindBuffer, (GLenum, GLuint));
GL_FUNCTION(void, BufferData, (GLenum, GLsizeiptr, const GLvoid*, GLenum));
GL_FUNCTION(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const GLvoid*));
GL_FUNCTION(void, VertexAttribDivisor, (GLuint, GLuint));
GL_FUNCTION(void, EnableVertexAttribArray, (GLuint));
GL_FUNCTION(void, DisableVertexAttribArray, (GLuint));
GL_FUNCTION(void, Uniform1i, (GLint, GLint));
GL_FUNCTION(void, Uniform1f, (GLint, GLfloat));
GL_FUNCTION(void, Uniform3fv, (GLint, GLsizei, GLfloat const*));
GL_FUNCTION(void, Uniform4fv, (GLint, GLsizei, GLfloat const*));
GL_FUNCTION(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat*));
GL_FUNCTION(void, DrawElements, (GLenum, GLsizei, GLenum, const GLvoid*));
GL_FUNCTION(void, DrawElementsInstanced, (GLenum, GLsizei, GLenum, const GLvoid*, GLsizei));

GL_FUNCTION(GLuint, CreateProgram, (void));
GL_FUNCTION(GLuint, CreateShader, (GLenum));
GL_FUNCTION(void, ShaderSource, (GLuint, GLsizei, const GLchar**, const GLint*));
GL_FUNCTION(void, CompileShader, (GLuint));
GL_FUNCTION(void, LinkProgram, (GLuint));
GL_FUNCTION(void, AttachShader, (GLuint, GLuint));
GL_FUNCTION(void, GetShaderiv, (GLuint, GLenum, GLint*));
GL_FUNCTION(void, GetProgramiv, (GLuint, GLenum, GLint*));
GL_FUNCTION(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*));
GL_FUNCTION(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*));
GL_FUNCTION(void, UseProgram, (GLuint));

GL_FUNCTION(void, GenTextures, (GLsizei, GLuint*));
GL_FUNCTION(void, BindTexture, (GLenum, GLuint));
GL_FUNCTION(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid*));
GL_FUNCTION(void, ActiveTexture, (GLenum));
GL_FUNCTION(void, GenerateMipmap, (GLenum));
GL_FUNCTION(void, TexParameteri, (GLenum, GLenum, GLint));
GL_FUNCTION(void, BlendFunc, (GLenum, GLenum));

};

// Create OpenGL shader (return handle)
static GLuint make_program(GL gl, const char* vs_code, const char* fs_code) {
    static GLchar log[1024];
    GLuint program_id = gl.CreateProgram();
    GLuint vs_id = gl.CreateShader(GL_VERTEX_SHADER);
    GLuint fs_id = gl.CreateShader(GL_FRAGMENT_SHADER);

    const GLchar* string_array[1];
    string_array[0] = (GLchar*)vs_code;
    gl.ShaderSource(vs_id, 1, string_array, nullptr);
    string_array[0] = (GLchar*)fs_code;
    gl.ShaderSource(fs_id, 1, string_array, nullptr);

    gl.CompileShader(vs_id);
    gl.CompileShader(fs_id);

    GLint okay = 0;
    GLsizei length = 0;
    const GLuint shader_id_array[2] = { vs_id, fs_id };
    for (auto id : shader_id_array) {
        gl.GetShaderiv(id, GL_COMPILE_STATUS, &okay);
        if (okay) {
            gl.AttachShader(program_id, id);
        } else {
            gl.GetShaderInfoLog(id, sizeof log, &length, log);
            fprintf(stderr, "%s\n", id == vs_id ? vs_code : fs_code);
            panic("Shader compilation error", log);
        }
    }

    gl.LinkProgram(program_id);
    gl.GetProgramiv(program_id, GL_LINK_STATUS, &okay);
    if (!okay) {
        gl.GetProgramInfoLog(program_id, sizeof log, &length, log);
        panic("Shader link error", log);
    }

    PANIC_IF_GL_ERROR(gl);
    return program_id;
}




// *** Code for drawing particles. ***
//
// Each particle is drawn as a regular icosahedron. It's the
// lowest-poly shape I can think of for drawing roughly spherical
// objects.
//
// To reduce overhead for drawing thousands of particles, I'm using
// instanced rendering.  Each vertex of a single icosahedron has four
// attributes:
//
// 0. Its position (which is also its normal vector) in the coordinate
// system of a single icosahedron (center at origin).
//
// 1. The position of the origin of the icosahedron, which is used to
// position the particle in the scene. (Now the sum of
// instance_position and uniform_position).
//
// 2. The color of the particle.
//
// 3. The radius of the particle.
//
// The first attribute comes from icosahedron vertex data defined in
// this file (particle vertices). The latter attributes come from the
// array passed to draw_particles. These will be stored in the vertex
// buffer with id [instanced_buffer_id], and will have their attribute
// divisor set to 1 so that the color and position in space changes
// once per icosahedron, not once per icosahedron vertex.
static const GLuint vertex_position_index = 0;
// static const GLuint vertex_normal_index = 1;
static const GLuint instance_position_index = 1;
static const GLuint instance_color_index = 2;
static const GLuint instance_radius_index = 3;

static const char particle_vs_source[] =
"#version 330\n"
"precision mediump float;\n"
"layout(location=0) in vec3 vertex_position;\n"
"layout(location=1) in vec3 instance_position;\n"
"layout(location=2) in vec3 instance_color;\n"
"layout(location=3) in float instance_radius;\n"
"out vec3 material_color;\n"
"out vec4 varying_normal;\n"
"uniform mat4 view_matrix;\n"
"uniform mat4 proj_matrix;\n"
"uniform vec3 uniform_position;\n"
"void main() {\n"
    "mat4 VP = proj_matrix * view_matrix;\n"
    "vec3 vertex_position_scaled = vertex_position * instance_radius;\n"
    "vec3 offset = instance_position + uniform_position;\n"
    "gl_Position = VP * vec4(vertex_position_scaled + offset, 1.0);\n"
    "material_color = instance_color;\n"
    "varying_normal = view_matrix * vec4(vertex_position, 0.0);\n"
    // The vertex normal is the same as its position for spherical objects.
"}\n";

static const char particle_fs_source[] =
"#version 330\n"
"precision mediump float;\n"
"in vec3 material_color;\n"
"in vec4 varying_normal;\n"
"out vec4 pixel_color;\n"
"void main() {\n"
    "float z = normalize(varying_normal.xyz).z;\n"
    "pixel_color = vec4(material_color * sqrt(z*.8 + .2), 1.0);\n"
"}\n";

// Each particle will be a sphere approximated by a regular icosahedron.
static const int particle_vertex_count = 12;
static const int particle_element_count = 60;
static const float phi = 1.618034f;

// So that radius=1.
static const float particle_scale = (float)(1.0 / hypot(1, phi));

// 12 vertices of a regular icosahedron.
static const float particle_vertices[3 * particle_vertex_count] = {
     phi*particle_scale,  particle_scale, 0,
    -phi*particle_scale,  particle_scale, 0,
    -phi*particle_scale, -particle_scale, 0,
     phi*particle_scale, -particle_scale, 0,

     particle_scale, 0,  phi*particle_scale,
    -particle_scale, 0,  phi*particle_scale,
    -particle_scale, 0, -phi*particle_scale,
     particle_scale, 0, -phi*particle_scale,

     0,  phi*particle_scale,  particle_scale,
     0, -phi*particle_scale,  particle_scale,
     0, -phi*particle_scale, -particle_scale,
     0,  phi*particle_scale, -particle_scale,
};

// Connect up the 12 vertices to form the 20 triangular faces of a
// regular icosahedron.
static const GLushort particle_elements[particle_element_count] = {
    5, 4, 8,
    5, 8, 1,
    5, 1, 2,
    5, 2, 9,
    5, 9, 4,

    7, 6, 11,
    7, 11, 0,
    7, 0, 3,
    7, 3, 10,
    7, 10, 6,

    2, 1, 6,
    6, 1, 11,
    1, 8, 11,
    11, 8, 0,
    8, 4, 0,
    0, 4, 3,
    4, 9, 3,
    3, 9, 10,
    9, 2, 10,
    10, 2, 6,
};

static void draw_particles(
    GL gl,
    const std::vector<visual_particle>& vp_list,
    vec3 position_offset
) {
    const visual_particle* particle_ptr = vp_list.data();
    const auto particle_count = vp_list.size();

    static_assert(sizeof particle_ptr[0] == 28, "Did someone mess with struct visual_particle?");

    static GLuint vao = 0;
    static GLuint program_id;
    static GLuint vertex_buffer_id;
    static GLuint element_buffer_id;
    static GLuint instance_buffer_id;
    static GLint view_matrix_id;
    static GLint proj_matrix_id;
    static GLint uniform_position_id;

    static auto particle_vertex_stride = sizeof(particle_ptr[0]);

    // Create vertex array object for instanced rendering of
    // icosahedrons if it hasn't been created yet.
    if (vao == 0) {
        // Compile the shader and configure uniform shader inputs.
        program_id = make_program(gl, particle_vs_source, particle_fs_source);
        view_matrix_id = gl.GetUniformLocation(program_id, "view_matrix");
        proj_matrix_id = gl.GetUniformLocation(program_id, "proj_matrix");
        uniform_position_id = gl.GetUniformLocation(program_id, "uniform_position");

        // Create vertex array object and vertex buffers needed later.
        gl.GenVertexArrays(1, &vao);
        gl.BindVertexArray(vao);
        gl.GenBuffers(1, &vertex_buffer_id);
        gl.GenBuffers(1, &element_buffer_id);
        gl.GenBuffers(1, &instance_buffer_id);

        // Create vertex buffer of 12 icosahedron vertices.
        gl.BindBuffer(GL_ARRAY_BUFFER, vertex_buffer_id);
        gl.BufferData(
            GL_ARRAY_BUFFER,
            sizeof particle_vertices,
            particle_vertices,
            GL_STATIC_DRAW);

        // Create element buffer.
        gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, element_buffer_id);
        gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof particle_elements,
            particle_elements, GL_STATIC_DRAW);

        // Configure shader vertex position input.
        gl.VertexAttribPointer(
            vertex_position_index,
            3,
            GL_FLOAT,
            false,
            3 * sizeof(float),
            (void*)0 );

        gl.EnableVertexAttribArray(vertex_position_index);

        // Create a buffer for instance data.
        gl.BindBuffer(GL_ARRAY_BUFFER, instance_buffer_id);

        // Configure instance position shader input.
        gl.VertexAttribPointer(
            instance_position_index,
            3,
            GL_FLOAT,
            false,
            particle_vertex_stride,
            (void*) offsetof(visual_particle, x));

        gl.VertexAttribDivisor(instance_position_index, 1);
        gl.EnableVertexAttribArray(instance_position_index);

        // Configure instance color shader input.
        gl.VertexAttribPointer(
            instance_color_index,
            3,
            GL_FLOAT,
            false,
            particle_vertex_stride,
            (void*) offsetof(visual_particle, red));

        gl.VertexAttribDivisor(instance_color_index, 1);
        gl.EnableVertexAttribArray(instance_color_index);

        // Configure instance radius shader input.
        gl.VertexAttribPointer(
            instance_radius_index,
            1,
            GL_FLOAT,
            false,
            particle_vertex_stride,
            (void*) offsetof(visual_particle, radius));

        gl.VertexAttribDivisor(instance_radius_index, 1);
        gl.EnableVertexAttribArray(instance_radius_index);

        PANIC_IF_GL_ERROR(gl);
    }

    // Use the shader program compiled earlier, fill in uniforms and
    // instance vertex buffer data (note that the other vertex
    // buffers, used for one icosahedron's vertices, are unchanged),
    // and render.
    gl.UseProgram(program_id);

    gl.UniformMatrix4fv(view_matrix_id, 1, 0, &view[0][0]);
    gl.UniformMatrix4fv(proj_matrix_id, 1, 0, &projection[0][0]);
    gl.Uniform3fv(uniform_position_id, 1, &position_offset[0]);

    gl.BindVertexArray(vao);
    gl.BindBuffer(GL_ARRAY_BUFFER, instance_buffer_id);

    gl.BufferData(
        GL_ARRAY_BUFFER,
        particle_vertex_stride * particle_count,
        particle_ptr,
        GL_DYNAMIC_DRAW);

    gl.DrawElementsInstanced(
        GL_TRIANGLES,
        particle_element_count,
        GL_UNSIGNED_SHORT,
        (void*)0,
        particle_count);

    gl.BindVertexArray(0);
    PANIC_IF_GL_ERROR(gl);
}

// *** Misc junk ***

static void update_window_title(float fps)
{
    std::string title = "Thing | ";
    title += std::to_string(int(rintf(fps)));
    title += " FPS";
    SDL_SetWindowTitle(window, title.c_str());
}

/// *** Controls ***

void add_random_particle(std::vector<visual_particle>& vp_list, std::mt19937& rng)
{
    float x = rng() * 1e-9;
    float y = rng() * 1e-9;
    float z = rng() * 1e-9;
    float r = rng() * 2.5e-10;
    float g = rng() * 2.5e-10;
    float b = rng() * 2.5e-10;
    float radius = 0.1f;

    auto vp = visual_particle { x,y,z,r,g,b,radius };
    vp_list.push_back(vp);
}

static bool handle_controls(
    float dt,
    std::vector<visual_particle>& vp_list,
    std::mt19937& rng)
{
    static bool orbit_mode = true;
    static bool perspective = true;
    static bool w, a, s, d, q, e, look_around;
    static float theta = 1.5707f, phi = 1.8f, radius = 25.0f;
    static float mouse_x, mouse_y;

    bool no_quit = true;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
          default:
          break; case SDL_KEYDOWN:
            switch (event.key.keysym.scancode) {
              default:
              break; case SDL_SCANCODE_W: case SDL_SCANCODE_C: w = true;
              break; case SDL_SCANCODE_A: case SDL_SCANCODE_H: a = true;
              break; case SDL_SCANCODE_S: case SDL_SCANCODE_T: s = true;
              break; case SDL_SCANCODE_D: case SDL_SCANCODE_N: d = true;
              break; case SDL_SCANCODE_Q: case SDL_SCANCODE_G: q = true;
              break; case SDL_SCANCODE_E: case SDL_SCANCODE_R: e = true;
              break; case SDL_SCANCODE_Z: add_random_particle(vp_list, rng);
              break; case SDL_SCANCODE_SPACE:
                look_around = true;
                orbit_mode = false;
              break; case SDL_SCANCODE_X:
                orbit_mode = !orbit_mode;
              break; case SDL_SCANCODE_P:
                perspective = !perspective;
              break; case SDL_SCANCODE_ESCAPE:
                no_quit = false;
            }

          break; case SDL_KEYUP:
            switch (event.key.keysym.scancode) {
              default:
              break; case SDL_SCANCODE_W: case SDL_SCANCODE_C: w = false;
              break; case SDL_SCANCODE_A: case SDL_SCANCODE_H: a = false;
              break; case SDL_SCANCODE_S: case SDL_SCANCODE_T: s = false;
              break; case SDL_SCANCODE_D: case SDL_SCANCODE_N: d = false;
              break; case SDL_SCANCODE_Q: case SDL_SCANCODE_G: q = false;
              break; case SDL_SCANCODE_E: case SDL_SCANCODE_R: e = false;
              break; case SDL_SCANCODE_SPACE: look_around = false;
            }
          break; case SDL_MOUSEWHEEL:
            phi += (orbit_mode ? 1 : -1) * event.wheel.y * 0.04f;
            theta += (orbit_mode ? 1 : -1) * event.wheel.x * 0.04f;
          break; case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP:
            mouse_x = event.button.x;
            mouse_y = event.button.y;
            orbit_mode = false;
            look_around = (event.type == SDL_MOUSEBUTTONDOWN);
          break; case SDL_MOUSEMOTION:
            mouse_x = event.motion.x;
            mouse_y = event.motion.y;
          break; case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                event.window.event == SDL_WINDOWEVENT_RESIZED) {

                screen_x = event.window.data1;
                screen_y = event.window.data2;
            }
          break; case SDL_QUIT:
            no_quit = false;
        }
    }

    vec3 forward_normal_vector(
        sinf(phi) * cosf(theta),
        cosf(phi),
        sinf(phi) * sinf(theta));

    if (orbit_mode) {
        theta += dt * 2.0f * (a-d);
        phi += dt * 1.75f * (e-q);
        radius += dt * radius_speed * (s-w);

        vec3 center(0, 0, 0);

        eye = center - radius * forward_normal_vector;

        view = glm::lookAt(eye, center, vec3(0,1,0));
    } else {
        // Free-camera mode.
        auto right_vector = glm::cross(forward_normal_vector, vec3(0,1,0));
        right_vector = glm::normalize(right_vector);
        auto up_vector = glm::cross(right_vector, forward_normal_vector);

        eye += dt * radius_speed * right_vector * (float)(d - a);
        eye += dt * radius_speed * forward_normal_vector * (float)(w - s);
        eye += dt * radius_speed * up_vector * (float)(e - q);

        if (look_around) {
            theta += 6.0f * dt / float(screen_x) * (mouse_x - screen_x*0.5f);
            phi +=   6.0f * dt / float(screen_x) * (mouse_y - screen_y*0.5f);
        }

        view = glm::lookAt(eye, eye+forward_normal_vector, vec3(0,1,0));
    }
    phi = glm::clamp(phi, 0.01f, 3.13f);

    auto screen_xy_ratio = float(screen_x)/screen_y;

    if (perspective) {
        projection = glm::perspective(
            fovy_radians,
            screen_xy_ratio,
            near_plane,
            far_plane);
    }
    else {
        projection =
            glm::ortho(
                -radius * screen_xy_ratio,
                +radius * screen_xy_ratio,
                -radius,
                +radius,
                near_plane,
                far_plane);
    }

    return no_quit;
}

// *** Main loop ***

int main(int, char** argv) {
    argv0 = argv[0];

    OpenGL_Functions gl;
    gl.Enable(GL_CULL_FACE);
    gl.Enable(GL_DEPTH_TEST);
    gl.ClearColor(0.1f, 0.5f, 1.0f, 1);

    bool no_quit = true;
    int frames = 0;

    int previous_fps_update_ticks = 0;
    int previous_frame_ticks = 0;
    int current_ticks = 0;

    std::vector<visual_particle> visual_particles;
    std::mt19937 rng;

    while (no_quit) {
        // Show FPS and update window title every now and then.
        previous_frame_ticks = current_ticks;
        current_ticks = SDL_GetTicks();

        ++frames;
        int fps_delta_ms = current_ticks - previous_fps_update_ticks;
        int ms_per_fps_update = 200;
        if (fps_delta_ms >= ms_per_fps_update) {
            float fps = frames / (fps_delta_ms * 0.001f);
            frames = 0;
            previous_fps_update_ticks = current_ticks;
            update_window_title(fps);
        }

        // Update the camera and prepare the screen for drawing particles.
        static int64_t previous_control_handle_ticks = 0;
        int64_t current_control_handle_ticks = SDL_GetTicks();
        float dt = 0.001f * (current_control_handle_ticks
                            - previous_control_handle_ticks);
        previous_control_handle_ticks = current_control_handle_ticks;
        no_quit = handle_controls(dt, visual_particles, rng);
        gl.Viewport(0, 0, screen_x, screen_y);

        auto delta_ms = current_ticks - previous_frame_ticks;

        gl.Clear(GL_COLOR_BUFFER_BIT);
        gl.Clear(GL_DEPTH_BUFFER_BIT);
        draw_particles(gl, visual_particles, vec3(0,0,0));

        SDL_GL_SwapWindow(window);
        PANIC_IF_GL_ERROR(gl);
    }
}
