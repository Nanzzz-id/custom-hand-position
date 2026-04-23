/**
 * HandPosMod - Ubah posisi tangan (item held) secara real-time
 * 
 * Cara kerja (meniru libThirdPersonNametag.so):
 *  - Pakai .init_array → jalan otomatis saat .so di-load
 *  - Pakai pl_resolve_signature → cari fungsi MC via byte pattern
 *  - Render UI via hook eglSwapBuffers
 *  - Touch input via hook ALooper_pollAll
 *
 * UI Mobile-friendly:
 *  - Panel slider kanan layar (X, Y, Z)
 *  - Tombol reset ke default
 *  - Bisa sembunyikan/tampilkan panel
 *  - Animasi smooth
 */

#include <jni.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/input.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <ctime>
#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <atomic>

#include "Gloss.h"

#define TAG "HandPosMod"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// ═══════════════════════════════════════
//  STATE POSISI TANGAN
// ═══════════════════════════════════════
struct HandOffset {
    float x = 0.0f;   // kanan/kiri  (-1.0 ~ 1.0)
    float y = 0.0f;   // atas/bawah  (-1.0 ~ 1.0)
    float z = 0.0f;   // maju/mundur (-1.0 ~ 1.0)
};

static HandOffset g_offset;
static std::mutex g_mtx;

// ═══════════════════════════════════════
//  UI STATE
// ═══════════════════════════════════════
struct UIState {
    bool  visible    = true;
    bool  minimized  = false;  // panel dikecilkan
    float panel_x    = 0;      // posisi panel (diset di init)
    float panel_y    = 0;
    float panel_w    = 220;
    float panel_h    = 320;
    float anim       = 1.0f;   // 0=tersembunyi, 1=tampil (untuk animasi)

    // Slider yang sedang di-drag
    int   drag_axis  = -1;     // 0=X, 1=Y, 2=Z, -1=tidak ada
    int   drag_id    = -1;     // pointer ID

    // Tombol toggle (pojok kanan bawah)
    float btn_x, btn_y;
    float btn_size   = 52;

    bool  initialized = false;
};
static UIState g_ui;
static int g_sw = 0, g_sh = 0;

// ═══════════════════════════════════════
//  OPENGL RENDERER (minimal, tidak butuh font external)
// ═══════════════════════════════════════
static GLuint g_prog_solid = 0;
static GLuint g_prog_text  = 0;
static GLuint g_vao = 0, g_vbo = 0;
static GLuint g_font_tex = 0;
static bool   g_gl_ready = false;

// Font 8x8 bitmap
static const uint8_t FONT[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00},
    {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00},{0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00},
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00},{0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00},
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00},{0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00},
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00},{0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00},
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},{0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06},{0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00},{0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00},
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00},{0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00},
    {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00},{0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00},
    {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00},{0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00},
    {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00},{0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00},
    {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00},{0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00},
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00},{0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06},
    {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00},{0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00},
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00},{0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00},
    {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00},{0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00},
    {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00},{0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00},
    {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00},{0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00},
    {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00},{0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00},
    {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00},{0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00},{0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00},
    {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00},{0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00},
    {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00},{0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00},
    {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00},{0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00},
    {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00},{0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00},
    {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},{0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00},
    {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00},{0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00},
    {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00},{0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00},
    {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00},{0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00},
    {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00},{0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00},
    {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF},
    {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00},
    {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00},{0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00},
    {0x38,0x30,0x30,0x3E,0x33,0x33,0x6E,0x00},{0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00},
    {0x1C,0x36,0x06,0x0F,0x06,0x06,0x0F,0x00},{0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F},
    {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00},{0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E},{0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00},
    {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},{0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00},
    {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00},{0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00},
    {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F},{0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78},
    {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00},{0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00},
    {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00},{0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00},{0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00},
    {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00},{0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F},
    {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00},{0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00},
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00},{0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00},
    {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00},{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
};

static const char* VERT_SRC = R"(#version 300 es
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
uniform vec2 uRes;
out vec2 vUV;
void main(){
    vec2 n=(aPos/uRes)*2.0-1.0; n.y=-n.y;
    gl_Position=vec4(n,0,1); vUV=aUV;
})";

static const char* FRAG_SOLID = R"(#version 300 es
precision mediump float;
uniform vec4 uCol;
out vec4 fc;
void main(){ fc=uCol; })";

static const char* FRAG_TEXT = R"(#version 300 es
precision mediump float;
in vec2 vUV;
uniform sampler2D uTex;
uniform vec4 uCol;
out vec4 fc;
void main(){
    float a=texture(uTex,vUV).r;
    fc=vec4(uCol.rgb,uCol.a*a);
})";

static GLuint compileShader(GLenum t, const char* s){
    GLuint sh=glCreateShader(t);
    glShaderSource(sh,1,&s,nullptr);
    glCompileShader(sh);
    return sh;
}
static GLuint makeProgram(const char* v, const char* f){
    GLuint p=glCreateProgram();
    glAttachShader(p,compileShader(GL_VERTEX_SHADER,v));
    glAttachShader(p,compileShader(GL_FRAGMENT_SHADER,f));
    glLinkProgram(p);
    return p;
}

static void initGL(int w, int h){
    if(g_gl_ready && g_sw==w && g_sh==h) return;
    g_sw=w; g_sh=h;
    g_prog_solid=makeProgram(VERT_SRC,FRAG_SOLID);
    g_prog_text =makeProgram(VERT_SRC,FRAG_TEXT);

    glGenVertexArrays(1,&g_vao);
    glGenBuffers(1,&g_vbo);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER,g_vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(float)*24,nullptr,GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(2*sizeof(float)));
    glBindVertexArray(0);

    // Font texture
    const int NC=96,CW=8,CH=8;
    std::vector<uint8_t> tex(NC*CW*CH,0);
    for(int c=0;c<NC;c++)
        for(int r=0;r<CH;r++){
            uint8_t b=FONT[c][r];
            for(int col=0;col<CW;col++)
                if((b>>(7-col))&1)
                    tex[r*(NC*CW)+c*CW+col]=255;
        }
    glGenTextures(1,&g_font_tex);
    glBindTexture(GL_TEXTURE_2D,g_font_tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_R8,NC*CW,CH,0,GL_RED,GL_UNSIGNED_BYTE,tex.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D,0);

    // Setup UI posisi
    if(!g_ui.initialized){
        g_ui.panel_x = w - g_ui.panel_w - 10;
        g_ui.panel_y = h * 0.25f;
        g_ui.btn_x   = w - g_ui.btn_size - 10;
        g_ui.btn_y   = h * 0.25f - g_ui.btn_size - 10;
        g_ui.initialized = true;
    }

    g_gl_ready=true;
    LOGI("GL ready %dx%d", w, h);
}

static void drawQuad(float x,float y,float w,float h,float r,float g,float b,float a){
    glUseProgram(g_prog_solid);
    glUniform2f(glGetUniformLocation(g_prog_solid,"uRes"),(float)g_sw,(float)g_sh);
    glUniform4f(glGetUniformLocation(g_prog_solid,"uCol"),r,g,b,a);
    float v[24]={x,y,0,0, x+w,y,1,0, x,y+h,0,1, x+w,y,1,0, x+w,y+h,1,1, x,y+h,0,1};
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER,g_vbo);
    glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(v),v);
    glDrawArrays(GL_TRIANGLES,0,6);
    glBindVertexArray(0);
}

static void drawRoundRect(float x,float y,float w,float h,float r,float g,float b,float a){
    // Simpel: gambar 3 rect tumpang tindih untuk simulasi rounded
    float pad=8;
    drawQuad(x+pad,y,w-pad*2,h,r,g,b,a);
    drawQuad(x,y+pad,w,h-pad*2,r,g,b,a);
}

static float drawText(const char* txt, float x, float y, float sc,
                      float r, float g, float b, float a){
    const int NC=96,CW=8,CH=8;
    float cw=CW*sc, ch=CH*sc;
    float tw=(float)(NC*CW);
    glUseProgram(g_prog_text);
    glUniform2f(glGetUniformLocation(g_prog_text,"uRes"),(float)g_sw,(float)g_sh);
    glUniform4f(glGetUniformLocation(g_prog_text,"uCol"),r,g,b,a);
    glUniform1i(glGetUniformLocation(g_prog_text,"uTex"),0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D,g_font_tex);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER,g_vbo);
    float px=x;
    for(int i=0;txt[i];i++){
        int idx=(int)txt[i]-32;
        if(idx<0||idx>=NC){px+=cw;continue;}
        float u0=(float)(idx*CW)/tw, u1=(float)(idx*CW+CW)/tw;
        float v[24]={px,y,u0,0, px+cw,y,u1,0, px,y+ch,u0,1,
                     px+cw,y,u1,0, px+cw,y+ch,u1,1, px,y+ch,u0,1};
        glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(v),v);
        glDrawArrays(GL_TRIANGLES,0,6);
        px+=cw;
    }
    glBindTexture(GL_TEXTURE_2D,0);
    glBindVertexArray(0);
    return px-x;
}

// ═══════════════════════════════════════
//  RENDER UI PANEL
// ═══════════════════════════════════════
static void renderUI(){
    if(!g_gl_ready) return;

    float px=g_ui.panel_x, py=g_ui.panel_y;
    float pw=g_ui.panel_w, ph=g_ui.panel_h;

    // Tombol toggle (selalu tampil)
    float bx=g_ui.btn_x, by=g_ui.btn_y, bs=g_ui.btn_size;
    drawRoundRect(bx,by,bs,bs, 0.15f,0.15f,0.15f,0.9f);
    // Icon tangan
    float sc=2.5f;
    float lbl_w=strlen("XYZ")*8*sc;
    drawText("XYZ", bx+(bs-lbl_w)/2, by+(bs-8*sc)/2, sc, 1,1,1,1);

    if(!g_ui.visible) return;

    // ── Panel utama ───────────────────────────
    // Shadow
    drawRoundRect(px+4,py+4,pw,ph, 0,0,0,0.4f);
    // Background
    drawRoundRect(px,py,pw,ph, 0.1f,0.1f,0.15f,0.92f);
    // Header bar
    drawRoundRect(px,py,pw,36, 0.2f,0.5f,0.9f,0.95f);

    // Judul
    float hsc=2.2f;
    drawText("Hand Position", px+12, py+10, hsc, 1,1,1,1);

    // Tombol minimize di header
    drawText(g_ui.minimized ? "+" : "-",
             px+pw-24, py+10, hsc, 1,1,1,0.9f);

    if(g_ui.minimized) return;

    // ── 3 Slider: X, Y, Z ────────────────────
    const char* labels[] = {"X", "Y", "Z"};
    float colors[3][3] = {
        {0.9f,0.3f,0.3f},  // X = merah
        {0.3f,0.9f,0.3f},  // Y = hijau
        {0.3f,0.5f,1.0f},  // Z = biru
    };
    float* vals[3] = {&g_offset.x, &g_offset.y, &g_offset.z};

    float slider_x  = px + 16;
    float slider_w  = pw - 32;
    float start_y   = py + 50;
    float row_h     = 75;

    for(int i=0;i<3;i++){
        float ry = start_y + i * row_h;
        float r=colors[i][0], g=colors[i][1], b=colors[i][2];

        // Label
        float lsc=2.5f;
        drawText(labels[i], slider_x, ry, lsc, r,g,b,1);

        // Nilai numerik
        char val_str[16];
        snprintf(val_str,sizeof(val_str),"%.2f",*vals[i]);
        float vsc=2.0f;
        float vw=strlen(val_str)*8*vsc;
        drawText(val_str, px+pw-vw-16, ry, vsc, 1,1,1,0.9f);

        // Track slider
        float ty = ry + 22;
        float th = 8;
        drawQuad(slider_x, ty, slider_w, th, 0.3f,0.3f,0.3f,0.8f);

        // Fill slider
        float pct = (*vals[i] + 1.0f) / 2.0f;  // -1..1 → 0..1
        float fill_w = pct * slider_w;
        if(fill_w > 0)
            drawQuad(slider_x, ty, fill_w, th, r,g,b,0.9f);

        // Knob
        float kx = slider_x + pct*slider_w - 14;
        float ky = ty - 8;
        // Shadow knob
        drawQuad(kx+2,ky+2,28,24, 0,0,0,0.35f);
        // Knob body
        drawQuad(kx,ky,28,24, r*1.1f,g*1.1f,b*1.1f,1.0f);
        // Knob border
        drawQuad(kx,ky,28,2, 1,1,1,0.5f);
        drawQuad(kx,ky+22,28,2, 1,1,1,0.2f);

        // Tombol -/+ kecil
        // Minus
        drawRoundRect(slider_x-2, ry+18, 20, 20, 0.25f,0.25f,0.25f,0.9f);
        drawText("-", slider_x+3, ry+19, 2.0f, 1,1,1,1);
        // Plus
        drawRoundRect(slider_x+slider_w-18, ry+18, 20, 20, 0.25f,0.25f,0.25f,0.9f);
        drawText("+", slider_x+slider_w-14, ry+19, 2.0f, 1,1,1,1);
    }

    // Tombol RESET
    float btn_ry = start_y + 3*row_h + 5;
    float btn_bw = pw - 32;
    drawRoundRect(slider_x, btn_ry, btn_bw, 32, 0.6f,0.2f,0.2f,0.9f);
    const char* rst="RESET";
    float rsc=2.2f;
    float rw=strlen(rst)*8*rsc;
    drawText(rst, slider_x+(btn_bw-rw)/2, btn_ry+8, rsc, 1,1,1,1);

    // Garis bawah dekoratif
    drawQuad(px,py+ph-3,pw,3, 0.2f,0.5f,0.9f,0.7f);
}

// ═══════════════════════════════════════
//  TOUCH HANDLING
// ═══════════════════════════════════════
static bool hitRect(float tx,float ty,float rx,float ry,float rw,float rh){
    return tx>=rx && tx<=rx+rw && ty>=ry && ty<=ry+rh;
}

static void onTouchDown(int id, float tx, float ty){
    // Tombol toggle
    if(hitRect(tx,ty, g_ui.btn_x,g_ui.btn_y,g_ui.btn_size,g_ui.btn_size)){
        g_ui.visible = !g_ui.visible;
        return;
    }
    if(!g_ui.visible) return;

    float px=g_ui.panel_x, py=g_ui.panel_y;
    float pw=g_ui.panel_w;

    // Tombol minimize
    if(hitRect(tx,ty, px+pw-30,py+5,30,30)){
        g_ui.minimized = !g_ui.minimized;
        return;
    }
    if(g_ui.minimized) return;

    float slider_x = px+16;
    float slider_w = pw-32;
    float start_y  = py+50;
    float row_h    = 75;
    float* vals[3] = {&g_offset.x, &g_offset.y, &g_offset.z};

    for(int i=0;i<3;i++){
        float ry = start_y + i*row_h;
        float ty2 = ry+18;

        // Tombol minus
        if(hitRect(tx,ty, slider_x-2,ty2,20,20)){
            std::lock_guard<std::mutex> lk(g_mtx);
            *vals[i] = fmaxf(-1.0f, *vals[i]-0.05f);
            return;
        }
        // Tombol plus
        if(hitRect(tx,ty, slider_x+slider_w-18,ty2,20,20)){
            std::lock_guard<std::mutex> lk(g_mtx);
            *vals[i] = fminf(1.0f, *vals[i]+0.05f);
            return;
        }
        // Drag slider
        if(hitRect(tx,ty, slider_x,ry+14,slider_w,24)){
            g_ui.drag_axis = i;
            g_ui.drag_id   = id;
            float pct = (tx-slider_x)/slider_w;
            pct = fmaxf(0,fminf(1,pct));
            std::lock_guard<std::mutex> lk(g_mtx);
            *vals[i] = pct*2.0f-1.0f;
            return;
        }
    }

    // Tombol reset
    float btn_ry = start_y + 3*row_h + 5;
    if(hitRect(tx,ty, slider_x,btn_ry,pw-32,32)){
        std::lock_guard<std::mutex> lk(g_mtx);
        g_offset = {0,0,0};
    }
}

static void onTouchMove(int id, float tx, float ty){
    if(g_ui.drag_id != id || g_ui.drag_axis < 0) return;
    float slider_x = g_ui.panel_x+16;
    float slider_w = g_ui.panel_w-32;
    float pct = (tx-slider_x)/slider_w;
    pct = fmaxf(0,fminf(1,pct));
    float* vals[3] = {&g_offset.x, &g_offset.y, &g_offset.z};
    std::lock_guard<std::mutex> lk(g_mtx);
    *vals[g_ui.drag_axis] = pct*2.0f-1.0f;
}

static void onTouchUp(int id){
    if(g_ui.drag_id == id){
        g_ui.drag_axis = -1;
        g_ui.drag_id   = -1;
    }
}

// ═══════════════════════════════════════
//  HOOK eglSwapBuffers
// ═══════════════════════════════════════
typedef EGLBoolean (*eglSwap_t)(EGLDisplay,EGLSurface);
static eglSwap_t g_orig_swap = nullptr;

static EGLBoolean hook_eglSwap(EGLDisplay dpy, EGLSurface surf){
    EGLint w=0,h=0;
    eglQuerySurface(dpy,surf,EGL_WIDTH,&w);
    eglQuerySurface(dpy,surf,EGL_HEIGHT,&h);
    if(w>0&&h>0){
        initGL(w,h);
        GLint prevProg; glGetIntegerv(GL_CURRENT_PROGRAM,&prevProg);
        GLboolean blend=glIsEnabled(GL_BLEND);
        GLboolean depth=glIsEnabled(GL_DEPTH_TEST);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        renderUI();
        if(!blend) glDisable(GL_BLEND);
        if(depth)  glEnable(GL_DEPTH_TEST);
        glUseProgram(prevProg);
    }
    return g_orig_swap(dpy,surf);
}

// ═══════════════════════════════════════
//  HOOK ALooper_pollAll (touch input)
// ═══════════════════════════════════════
typedef int (*pollAll_t)(int,int*,int*,void**);
static pollAll_t g_orig_poll = nullptr;

static int hook_pollAll(int ms, int* fd, int* ev, void** data){
    int res = g_orig_poll(ms,fd,ev,data);
    if(res==ALOOPER_POLL_CALLBACK && data && *data){
        AInputQueue* q=(AInputQueue*)*data;
        AInputEvent* e=nullptr;
        while(AInputQueue_getEvent(q,&e)>=0){
            if(AInputQueue_preDispatchEvent(q,e)) continue;
            bool consumed=false;
            if(AInputEvent_getType(e)==AINPUT_EVENT_TYPE_MOTION){
                int act=AMotionEvent_getAction(e);
                int mask=act&AMOTION_EVENT_ACTION_MASK;
                int pidx=(act&AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                         >>AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
                int pid=AMotionEvent_getPointerId(e,pidx);
                float tx=AMotionEvent_getX(e,pidx);
                float ty=AMotionEvent_getY(e,pidx);
                switch(mask){
                    case AMOTION_EVENT_ACTION_DOWN:
                    case AMOTION_EVENT_ACTION_POINTER_DOWN:
                        onTouchDown(pid,tx,ty); consumed=true; break;
                    case AMOTION_EVENT_ACTION_MOVE:
                        for(int i=0;i<(int)AMotionEvent_getPointerCount(e);i++)
                            onTouchMove(AMotionEvent_getPointerId(e,i),
                                        AMotionEvent_getX(e,i),
                                        AMotionEvent_getY(e,i));
                        consumed=true; break;
                    case AMOTION_EVENT_ACTION_UP:
                    case AMOTION_EVENT_ACTION_POINTER_UP:
                    case AMOTION_EVENT_ACTION_CANCEL:
                        onTouchUp(pid); consumed=true; break;
                }
            }
            AInputQueue_finishEvent(q,e,consumed?1:0);
        }
    }
    return res;
}

// ═══════════════════════════════════════
//  HOOK POSISI TANGAN
//  Pakai pl_resolve_signature seperti libThirdPersonNametag.so
//  Pattern ini untuk fungsi render item di tangan
// ═══════════════════════════════════════

// Typedef fungsi render item (parameter bervariasi per versi MC)
// Kita hook untuk inject offset posisi
typedef void (*renderHeld_t)(void*, void*, void*, float, float, float, float);
static renderHeld_t g_orig_renderHeld = nullptr;

static void hook_renderHeld(void* a, void* b, void* c,
                             float x, float y, float z, float d){
    std::lock_guard<std::mutex> lk(g_mtx);
    g_orig_renderHeld(a, b, c,
                      x + g_offset.x,
                      y + g_offset.y,
                      z + g_offset.z,
                      d);
}

// ═══════════════════════════════════════
//  INIT VIA .init_array
//  Dipanggil otomatis saat .so di-dlopen
// ═══════════════════════════════════════
static void __attribute__((constructor)) mod_init(){
    LOGI("HandPosMod initializing via .init_array...");

    // Init Gloss
    GlossInit();

    // Hook eglSwapBuffers
    void* libEGL = GlossOpen("libEGL.so");
    if(libEGL){
        void* sym = GlossSymbol(libEGL,"eglSwapBuffers");
        if(sym && GlossHook(sym,(void*)hook_eglSwap,(void**)&g_orig_swap)==0)
            LOGI("eglSwapBuffers hooked OK");
        else
            LOGE("eglSwapBuffers hook failed");
    }

    // Hook ALooper_pollAll untuk touch
    void* libAndroid = GlossOpen("libandroid.so");
    if(libAndroid){
        void* sym = GlossSymbol(libAndroid,"ALooper_pollAll");
        if(sym && GlossHook(sym,(void*)hook_pollAll,(void**)&g_orig_poll)==0)
            LOGI("ALooper_pollAll hooked OK");
    }

    // Hook render tangan via signature pattern
    // Pattern ini dari komunitas MCPE modding untuk fungsi held item render
    // Perlu diupdate jika versi MC berubah
    const char* hand_pattern =
        "? ? 40 F9 ? ? ? EB ? ? ? 54 ? ? 40 F9 ? 81 40 F9 E0 03 ? AA 00 01 3F D6 ? ? 00 37 "
        "? ? 40 F9 ? ? ? A9 ? ? ? CB ? ? ? D3 ? ? 00 51 ? ? ? 8A";

    void* hand_fn = pl_resolve_signature("libminecraftpe.so", hand_pattern);
    if(hand_fn){
        if(GlossHook(hand_fn,(void*)hook_renderHeld,(void**)&g_orig_renderHeld)==0)
            LOGI("Hand render hooked OK");
        else
            LOGE("Hand render hook failed");
    } else {
        LOGI("Hand signature not found - UI only mode (slider tampil tapi offset belum aktif)");
    }

    LOGI("HandPosMod ready!");
}
