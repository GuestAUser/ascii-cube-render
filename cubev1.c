#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <fcntl.h>

#define PI 3.14159265358979323846
#define MAX_WIDTH 400
#define MAX_HEIGHT 300

typedef struct { double x, y, z; } Vec3;
typedef struct { uint8_t r, g, b; } Color;

typedef struct {
    int width, height;
    Color *top_color;
    Color *bot_color;
    double *top_depth;
    double *bot_depth;
} Buffer;

static Buffer buf;
static struct termios orig_term;
static double time_global = 0;
static double rot_x = 0.7, rot_y = 0.9, rot_z = 0.3;
static double zoom = 0.6;

static const Vec3 CUBE_VERTS[8] = {
    {-1,-1,-1}, { 1,-1,-1}, { 1, 1,-1}, {-1, 1,-1},
    {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1}
};

static const int CUBE_FACES[6][4] = {
    {0,1,2,3}, {5,4,7,6}, {4,0,3,7},
    {1,5,6,2}, {3,2,6,7}, {4,5,1,0}
};

static const Color FACE_COLORS[6] = {
    {255,0,128},
    {0,128,255},
    {0,255,80},
    {255,128,0},
    {200,0,255},
    {255,220,0}
};

static inline Vec3 v3(double x, double y, double z) {
    return (Vec3){x, y, z};
}

static inline Vec3 add(Vec3 a, Vec3 b) {
    return v3(a.x + b.x, a.y + b.y, a.z + b.z);
}

static inline Vec3 sub(Vec3 a, Vec3 b) {
    return v3(a.x - b.x, a.y - b.y, a.z - b.z);
}

static inline Vec3 scale(Vec3 v, double s) {
    return v3(v.x * s, v.y * s, v.z * s);
}

static inline double dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline Vec3 cross(Vec3 a, Vec3 b) {
    return v3(a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x);
}

static inline double length(Vec3 v) {
    return sqrt(dot(v, v));
}

static inline Vec3 normalize(Vec3 v) {
    double len = length(v);
    return len > 1e-8 ? scale(v, 1.0/len) : v3(0,0,0);
}

static inline void rot_x_apply(Vec3 *v, double a) {
    double c = cos(a), s = sin(a);
    double y = v->y * c - v->z * s;
    double z = v->y * s + v->z * c;
    v->y = y; v->z = z;
}

static inline void rot_y_apply(Vec3 *v, double a) {
    double c = cos(a), s = sin(a);
    double x = v->x * c + v->z * s;
    double z = -v->x * s + v->z * c;
    v->x = x; v->z = z;
}

static inline void rot_z_apply(Vec3 *v, double a) {
    double c = cos(a), s = sin(a);
    double x = v->x * c - v->y * s;
    double y = v->x * s + v->y * c;
    v->x = x; v->y = y;
}

static inline Color rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (Color){r, g, b};
}

static inline Color shade(Color c, double brightness) {
    brightness = brightness < 0 ? 0 : (brightness > 1 ? 1 : brightness);
    return rgb(
        (uint8_t)(c.r * brightness),
        (uint8_t)(c.g * brightness),
        (uint8_t)(c.b * brightness)
    );
}

static void buf_init(int w, int h) {
    buf.width = w;
    buf.height = h;
    size_t sz = w * h;
    buf.top_color = malloc(sz * sizeof(Color));
    buf.bot_color = malloc(sz * sizeof(Color));
    buf.top_depth = malloc(sz * sizeof(double));
    buf.bot_depth = malloc(sz * sizeof(double));
}

static void buf_clear(void) {
    size_t sz = buf.width * buf.height;
    Color black = rgb(0,0,0);
    for (size_t i = 0; i < sz; i++) {
        buf.top_color[i] = black;
        buf.bot_color[i] = black;
        buf.top_depth[i] = -1e10;
        buf.bot_depth[i] = -1e10;
    }
}

static inline void put_pixel(int x, int y, int is_top, Color col, double depth) {
    if (x < 0 || x >= buf.width || y < 0 || y >= buf.height) return;
    int idx = y * buf.width + x;
    
    if (is_top) {
        if (depth > buf.top_depth[idx]) {
            buf.top_color[idx] = col;
            buf.top_depth[idx] = depth;
        }
    } else {
        if (depth > buf.bot_depth[idx] - 0.01) {
            buf.bot_color[idx] = col;
            buf.bot_depth[idx] = depth;
        }
    }
}

static void buf_render(void) {
    printf("\033[H\033[0m");
    Color last_fg = (Color){255,255,255};
    Color last_bg = (Color){255,255,255};
    int bg_is_default = 1;
    
    for (int y = 0; y < buf.height; y++) {
        for (int x = 0; x < buf.width; x++) {
            int idx = y * buf.width + x;
            int top_set = buf.top_depth[idx] > -1e9;
            int bot_set = buf.bot_depth[idx] > -1e9;
            Color t_col = buf.top_color[idx];
            Color b_col = buf.bot_color[idx];
            
            if (!top_set && !bot_set) {
                if (!bg_is_default) { printf("\033[49m"); bg_is_default = 1; }
                putchar(' ');
            } else if (top_set && !bot_set) {
                if (!bg_is_default) { printf("\033[49m"); bg_is_default = 1; }
                if (t_col.r != last_fg.r || t_col.g != last_fg.g || t_col.b != last_fg.b) {
                    printf("\033[38;2;%d;%d;%dm", t_col.r, t_col.g, t_col.b);
                    last_fg = t_col;
                }
                printf("▀");
            } else if (!top_set && bot_set) {
                if (!bg_is_default) { printf("\033[49m"); bg_is_default = 1; }
                if (b_col.r != last_fg.r || b_col.g != last_fg.g || b_col.b != last_fg.b) {
                    printf("\033[38;2;%d;%d;%dm", b_col.r, b_col.g, b_col.b);
                    last_fg = b_col;
                }
                printf("▄");
            } else {
                if (t_col.r != last_fg.r || t_col.g != last_fg.g || t_col.b != last_fg.b) {
                    printf("\033[38;2;%d;%d;%dm", t_col.r, t_col.g, t_col.b);
                    last_fg = t_col;
                }
                if (b_col.r != last_bg.r || b_col.g != last_bg.g || b_col.b != last_bg.b) {
                    printf("\033[48;2;%d;%d;%dm", b_col.r, b_col.g, b_col.b);
                    last_bg = b_col;
                }
                bg_is_default = 0;
                printf("▀");
            }
        }
        printf("\033[0m\n");
        bg_is_default = 1;
    }
    fflush(stdout);
}

static void term_init(void) {
    tcgetattr(0, &orig_term);
    struct termios raw = orig_term;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &raw);
    fcntl(0, F_SETFL, O_NONBLOCK);
    printf("\033[?25l\033[2J\033[?1049h");
}

static void term_cleanup(void) {
    tcsetattr(0, TCSANOW, &orig_term);
    printf("\033[?25h\033[0m\033[2J\033[H\033[?1049l");
}

static void get_term_size(int *w, int *h) {
    struct winsize ws;
    if (ioctl(0, TIOCGWINSZ, &ws) == 0) {
        *w = ws.ws_col < MAX_WIDTH ? ws.ws_col : MAX_WIDTH;
        *h = (ws.ws_row - 1) < MAX_HEIGHT ? ws.ws_row - 1 : MAX_HEIGHT;
    } else {
        *w = 80;
        *h = 24;
    }
}

static inline int project(Vec3 p, double *x, double *y, double *z) {
    if (p.z >= -0.5 || p.z <= -100.0) return 0;
    double focal = 5.0;
    double factor = -focal / p.z;
    double pixel_h = buf.height * 2.0;
    double min_dim = buf.width < pixel_h ? buf.width : pixel_h;
    double scale = min_dim * 0.38 * zoom;
    *x = p.x * factor * scale + buf.width * 0.5;
    *y = -p.y * factor * scale + pixel_h * 0.5;
    *z = p.z;
    return 1;
}

static double calc_light(Vec3 normal) {
    Vec3 light1 = normalize(v3(
        sin(time_global * 0.7) * 10,
        cos(time_global * 0.4) * 8 + 10,
        -5
    ));
    Vec3 light2 = normalize(v3(-6, -4, -8));
    Vec3 view_dir = v3(0, 0, -1);
    double ambient = 0.15;
    double diff1 = fmax(0, dot(normal, light1)) * 0.8;
    double diff2 = fmax(0, dot(normal, light2)) * 0.15;
    Vec3 halfway = normalize(add(light1, view_dir));
    double spec = pow(fmax(0, dot(normal, halfway)), 100.0) * 0.55;
    return fmin(ambient + diff1 + diff2 + spec, 1.0);
}

static void draw_line(Vec3 p0, Vec3 p1, Color col) {
    double x0, y0, z0, x1, y1, z1;
    if (!project(p0, &x0, &y0, &z0) || !project(p1, &x1, &y1, &z1)) return;
    
    int dx = abs((int)x1 - (int)x0);
    int dy = abs((int)y1 - (int)y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    
    int x = (int)x0, y = (int)y0;
    double z = z0;
    double steps = fmax(dx, dy);
    double dz = steps > 0 ? (z1 - z0) / steps : 0;
    
    while (1) {
        int py = y / 2;
        int is_top = (y % 2 == 0);
        if (py >= 0 && py < buf.height) put_pixel(x, py, is_top, col, z + 0.01);
        if (x == (int)x1 && y == (int)y1) break;
        
        int e2 = err * 2;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 < dx) { err += dx; y += sy; }
        z += dz;
    }
}

static void fill_tri(Vec3 v0, Vec3 v1, Vec3 v2, Color col, double brightness) {
    double x0, y0, z0, x1, y1, z1, x2, y2, z2;
    if (!project(v0, &x0, &y0, &z0)) return;
    if (!project(v1, &x1, &y1, &z1)) return;
    if (!project(v2, &x2, &y2, &z2)) return;

    int min_x = (int)floor(fmin(x0, fmin(x1, x2))); if (min_x < 0) min_x = 0;
    int max_x = (int)ceil(fmax(x0, fmax(x1, x2)));  if (max_x >= buf.width) max_x = buf.width - 1;
    int min_y = (int)floor(fmin(y0, fmin(y1, y2))); if (min_y < 0) min_y = 0;
    int max_y = (int)ceil(fmax(y0, fmax(y1, y2)));  if (max_y >= buf.height * 2) max_y = buf.height * 2 - 1;

    double area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
    if (fabs(area) < 1e-8) return;
    Color shaded = shade(col, brightness);

    for (int y = min_y; y <= max_y; y++) {
        double py = (double)y + 0.5;
        for (int x = min_x; x <= max_x; x++) {
            double px = (double)x + 0.5;

            double w0 = (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1);
            double w1 = (x0 - x2) * (py - y2) - (y0 - y2) * (px - x2);
            double w2 = (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0);

            if ((area > 0 && w0 >= 0 && w1 >= 0 && w2 >= 0) ||
                (area < 0 && w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                double invA = 1.0 / area;
                double b0 = w0 * invA;
                double b1 = w1 * invA;
                double b2 = w2 * invA;
                double z = b0 * z0 + b1 * z1 + b2 * z2;

                int cell_y = y / 2;
                int is_top = (y % 2 == 0);
                if (cell_y >= 0 && cell_y < buf.height) put_pixel(x, cell_y, is_top, shaded, z);
            }
        }
    }
}

static void render_cube(void) {
    Vec3 verts[8];
    
    double size = 1.0;
    
    for (int i = 0; i < 8; i++) {
        Vec3 v = scale(CUBE_VERTS[i], size);
        rot_x_apply(&v, rot_x);
        rot_y_apply(&v, rot_y);
        rot_z_apply(&v, rot_z);
        v.z -= 5.0;
        verts[i] = v;
    }

    typedef struct {
        int idx;
        double depth;
        Vec3 center;
        Vec3 normal;
        double brightness;
    } Face;
    
    Face faces[6];
    int num_vis = 0;
    
    for (int i = 0; i < 6; i++) {
        const int *f = CUBE_FACES[i];
        Vec3 v0 = verts[f[0]], v1 = verts[f[1]], v2 = verts[f[2]];
        
        Vec3 edge1 = sub(v1, v0);
        Vec3 edge2 = sub(v2, v0);
        Vec3 normal = normalize(cross(edge1, edge2));
        
        Vec3 center = scale(add(add(add(verts[f[0]], verts[f[1]]), verts[f[2]]), verts[f[3]]), 0.25);
        
        Vec3 to_cam = normalize(scale(center, -1.0));
        if (dot(normal, to_cam) > 0) {
            double brightness = calc_light(normal);
            
            faces[num_vis].idx = i;
            faces[num_vis].depth = center.z;
            faces[num_vis].center = center;
            faces[num_vis].normal = normal;
            faces[num_vis].brightness = brightness;
            num_vis++;
        }
    }
    
    for (int i = 0; i < num_vis-1; i++) {
        for (int j = i+1; j < num_vis; j++) {
            if (faces[i].depth > faces[j].depth) {
                Face tmp = faces[i];
                faces[i] = faces[j];
                faces[j] = tmp;
            }
        }
    }

    for (int f = 0; f < num_vis; f++) {
        int idx = faces[f].idx;
        const int *face = CUBE_FACES[idx];
        Color col = FACE_COLORS[idx];
        fill_tri(verts[face[0]], verts[face[1]], verts[face[2]], col, faces[f].brightness);
        fill_tri(verts[face[0]], verts[face[2]], verts[face[3]], col, faces[f].brightness);
    }
    int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}
    };
    for (int e = 0; e < 12; e++) {
        int v0 = edges[e][0], v1 = edges[e][1];
        int shared = 0;
        for (int f = 0; f < num_vis; f++) {
            const int *face = CUBE_FACES[faces[f].idx];
            int has_v0 = 0, has_v1 = 0;
            for (int k = 0; k < 4; k++) {
                if (face[k] == v0) has_v0 = 1;
                if (face[k] == v1) has_v1 = 1;
            }
            if (has_v0 && has_v1) shared++;
        }
        if (shared == 1) {
            draw_line(verts[v0], verts[v1], rgb(255,255,255));
        }
    }
}

static int handle_input(void) {
    char c;
    if (read(0, &c, 1) == 1) {
        if (c == 'q' || c == 'Q' || c == 27) return 0;
        if (c == '+' || c == '=') {
            double nz = zoom * 1.1;
            if (nz > 5.0) nz = 5.0;
            zoom = nz;
        } else if (c == '-' || c == '_') {
            double nz = zoom / 1.1;
            if (nz < 0.1) nz = 0.1;
            zoom = nz;
        }
    }
    return 1;
}

int main(void) {
    int w, h;
    get_term_size(&w, &h);
    buf_init(w, h);
    term_init();
    
    struct timeval last_time;
    gettimeofday(&last_time, NULL);
    
    while (1) {
        struct timeval now;
        gettimeofday(&now, NULL);
        
        double dt = (now.tv_sec - last_time.tv_sec) +
                   (now.tv_usec - last_time.tv_usec) / 1e6;
        last_time = now;
        
        if (dt > 0.1) dt = 0.1;
        time_global += dt;
        rot_x += 0.6 * dt;
        rot_y += 0.8 * dt;
        rot_z += 0.4 * dt;
        
        if (!handle_input()) break;
        buf_clear();
        render_cube();
        buf_render();
        usleep(16667);
    }
    
    term_cleanup();
    free(buf.top_color);
    free(buf.bot_color);
    free(buf.top_depth);
    free(buf.bot_depth);
    return 0;
}