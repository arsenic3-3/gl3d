// gl3d - tiny 3D endless runner in OpenGL 3.1
// Controls: A/D or arrows = move, SPACE = jump, R = restart, ESC = quit
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include <string>

static std::string lastGlfwError;
[[noreturn]] static void die(const char* m) {
    std::string msg = m;
    if (!lastGlfwError.empty()) msg += "\n\nGLFW: " + lastGlfwError;
    const GLubyte* v = glGetString(GL_VERSION);
    if (v) msg += std::string("\n\nOpenGL: ") + (const char*)v;
    std::fprintf(stderr, "%s\n", msg.c_str());
#ifdef _WIN32
    MessageBoxA(nullptr, msg.c_str(), "gl3d error", MB_OK | MB_ICONERROR);
#endif
    std::exit(1);
}

static const char* VERT = R"(#version 140
uniform mat4 uMvp;
out float vShade;
out float vDepth;
const vec2 Q[6] = vec2[](vec2(-1,-1), vec2(1,-1), vec2(1,1), vec2(-1,-1), vec2(1,1), vec2(-1,1));
void main() {
    // Unit cube (-1..1) generated from the vertex id: 6 faces * 6 vertices
    int face = gl_VertexID / 6;
    int axis = face / 2;
    float s  = (face % 2 == 0) ? 1.0 : -1.0;
    vec2 q   = Q[gl_VertexID % 6];
    vec3 p, n;
    if (axis == 0)      { p = vec3(s, q.x, q.y); n = vec3(s, 0.0, 0.0); }
    else if (axis == 1) { p = vec3(q.y, s, q.x); n = vec3(0.0, s, 0.0); }
    else                { p = vec3(q.x, q.y, s); n = vec3(0.0, 0.0, s); }
    vShade = 0.35 + 0.65 * max(dot(n, normalize(vec3(0.4, 0.9, 0.3))), 0.0);
    gl_Position = uMvp * vec4(p, 1.0);
    vDepth = gl_Position.w;
}
)";

static const char* FRAG = R"(#version 140
uniform vec4 uColor;
in float vShade;
in float vDepth;
out vec4 outColor;
const vec3 FOG = vec3(0.01, 0.015, 0.05);
void main() {
    vec3 c = uColor.rgb * vShade;
    float f = clamp((vDepth - 30.0) / 70.0, 0.0, 1.0);
    outColor = vec4(mix(c, FOG, f), 1.0);
}
)";

static GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(s, sizeof log, nullptr, log); lastGlfwError = log; die("Shader compile failed"); }
    return s;
}

// ---------------------------------------------------------------- game ----
static GLFWwindow* win;
struct Obstacle { glm::vec3 pos, half, color; };
struct Game {
    float px = 0, py = 0, vy = 0, t = 0, dist = 0, speed = 15, spawn = 1.0f;
    bool dead = false;
    std::vector<Obstacle> obs;
} g;
static std::mt19937 rng{std::random_device{}()};
static float frand(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); }

static void updateGame(float dt) {
    if (g.dead) return;
    auto key = [](int k) { return glfwGetKey(win, k) == GLFW_PRESS; };
    float ax = (float)((key(GLFW_KEY_D) || key(GLFW_KEY_RIGHT)) - (key(GLFW_KEY_A) || key(GLFW_KEY_LEFT)));
    g.px = std::clamp(g.px + ax * 14.0f * dt, -8.0f, 8.0f);
    if (g.py <= 0.0f && key(GLFW_KEY_SPACE)) g.vy = 9.0f;
    g.vy -= 25.0f * dt;
    g.py += g.vy * dt;
    if (g.py < 0.0f) { g.py = 0.0f; g.vy = 0.0f; }

    g.t += dt;
    g.speed = 15.0f + std::min(g.t * 0.5f, 30.0f);
    g.dist += g.speed * dt;

    g.spawn -= dt;
    if (g.spawn <= 0.0f) {
        g.spawn = std::max(0.3f, 0.9f - g.t * 0.012f);
        float hy = frand(0.4f, 1.5f);
        bool low = hy * 2.0f < 1.4f;  // low ones (yellow) can be jumped over
        g.obs.push_back({{frand(-8.0f, 8.0f), hy, -100.0f},
                         {frand(0.6f, 1.6f), hy, frand(0.6f, 1.2f)},
                         low ? glm::vec3(1.0f, 0.75f, 0.05f) : glm::vec3(1.0f, 0.08f, 0.1f)});
    }
    for (auto& o : g.obs) o.pos.z += g.speed * dt;
    g.obs.erase(std::remove_if(g.obs.begin(), g.obs.end(), [](auto& o) { return o.pos.z > 12.0f; }), g.obs.end());

    glm::vec3 p(g.px, g.py + 0.5f, 0.0f);
    for (auto& o : g.obs)
        if (std::abs(p.x - o.pos.x) < 0.5f + o.half.x && std::abs(p.y - o.pos.y) < 0.5f + o.half.y &&
            std::abs(p.z - o.pos.z) < 0.5f + o.half.z)
            g.dead = true;
}

int main() {
    glfwSetErrorCallback([](int, const char* d) { lastGlfwError = d; });
    if (!glfwInit()) die("glfwInit failed");
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);  // profiles only exist from 3.2, so none requested
    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);
    win = glfwCreateWindow(1024, 640, "gl3d", nullptr, nullptr);
    if (!win) die("Could not create an OpenGL 3.1 window");
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    glewExperimental = GL_TRUE;
    GLenum ge = glewInit();
    if (ge != GLEW_OK) { lastGlfwError = (const char*)glewGetErrorString(ge); die("glewInit failed"); }
    std::printf("GPU: %s\n", (const char*)glGetString(GL_RENDERER));

    GLuint prog = glCreateProgram();
    GLuint vs = compile(GL_VERTEX_SHADER, VERT), fs = compile(GL_FRAGMENT_SHADER, FRAG);
    glAttachShader(prog, vs); glAttachShader(prog, fs);
    glLinkProgram(prog);
    GLint ok; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { char log[1024]; glGetProgramInfoLog(prog, sizeof log, nullptr, log); lastGlfwError = log; die("Program link failed"); }
    glDeleteShader(vs); glDeleteShader(fs);
    GLint locMvp = glGetUniformLocation(prog, "uMvp"), locColor = glGetUniformLocation(prog, "uColor");

    GLuint vao; glGenVertexArrays(1, &vao); glBindVertexArray(vao);  // empty VAO (3.1 requires a VAO bound even with no attributes)
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_FRAMEBUFFER_SRGB);
    glClearColor(0.01f, 0.015f, 0.05f, 1.0f);
    glUseProgram(prog);

    double last = glfwGetTime(), titleTimer = 0;
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;
        if (glfwGetKey(win, GLFW_KEY_R) == GLFW_PRESS) g = Game();

        int fw, fh; glfwGetFramebufferSize(win, &fw, &fh);
        if (fw == 0 || fh == 0) { glfwWaitEvents(); last = glfwGetTime(); continue; }

        double now = glfwGetTime();
        float dt = std::min((float)(now - last), 0.05f);
        last = now;
        updateGame(dt);

        titleTimer += dt;
        if (titleTimer > 0.2) {
            titleTimer = 0;
            char buf[128];
            std::snprintf(buf, sizeof buf, g.dead ? "gl3d - GAME OVER  score %d  (press R)" : "gl3d - score %d",
                          (int)g.dist);
            glfwSetWindowTitle(win, buf);
        }

        glViewport(0, 0, fw, fh);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 proj = glm::perspective(glm::radians(60.0f), (float)fw / (float)fh, 0.5f, 200.0f);
        float cx = g.px * 0.5f;
        glm::mat4 view = glm::lookAt(glm::vec3(cx, 4.5f, 9.0f), glm::vec3(cx, 0.5f, -12.0f), glm::vec3(0, 1, 0));
        glm::mat4 vp = proj * view;

        auto cube = [&](glm::vec3 pos, glm::vec3 half, glm::vec3 color) {
            glm::mat4 m = vp * glm::scale(glm::translate(glm::mat4(1.0f), pos), half);
            glUniformMatrix4fv(locMvp, 1, GL_FALSE, &m[0][0]);
            glUniform4f(locColor, color.r, color.g, color.b, 1.0f);
            glDrawArrays(GL_TRIANGLES, 0, 36);
        };

        cube({0, -0.05f, -50}, {10, 0.05f, 100}, {0.02f, 0.06f, 0.08f});                 // floor
        float off = std::fmod(g.dist, 10.0f);
        for (int i = 0; i < 10; i++) {
            cube({0, 0.02f, off - i * 10.0f}, {0.1f, 0.01f, 2.0f}, {0.5f, 0.6f, 0.7f});  // lane dashes
            float pz = std::fmod(g.dist, 15.0f) - i * 15.0f;
            cube({-11, 3, pz}, {0.5f, 3, 0.5f}, {0.1f, 0.9f, 1.0f});                    // pillars
            cube({11, 3, pz}, {0.5f, 3, 0.5f}, {1.0f, 0.2f, 0.9f});
        }
        for (auto& o : g.obs) cube(o.pos, o.half, o.color);
        cube({g.px, g.py + 0.5f, 0}, {0.5f, 0.5f, 0.5f}, g.dead ? glm::vec3(0.3f) : glm::vec3(0.1f, 1.0f, 0.4f));

        glfwSwapBuffers(win);
    }
    glfwDestroyWindow(win);
    glfwTerminate();
}
