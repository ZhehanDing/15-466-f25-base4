//ChatGPT used to create this file
//Credit: jialand
//reference: https://github.com/jialand/TheMuteLift/tree/main
#include "TextHB.hpp"

#include "gl_compile_program.hpp"
#include "data_path.hpp"
#include "GL.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

#include <hb.h>
#include <hb-ft.h>

#include <cassert>
#include <cstring>
#include <cstddef>

static const char* VS = R"GLSL(
#version 330
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
out vec2 vUV;
uniform vec2 uScreen; // in pixels
void main(){
    vUV = aUV;
    // pixel -> NDC. Note: NDC y goes up; here (0,0) = top-left corner
    float x = (aPos.x / uScreen.x) * 2.0 - 1.0;
    float y = 1.0 - (aPos.y / uScreen.y) * 2.0;
    gl_Position = vec4(x, y, 0.0, 1.0);
}
)GLSL";

static const char* FS = R"GLSL(
#version 330
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uTex; // R8, red channel as alpha
uniform vec3 uColor;
void main(){
    float a = texture(uTex, vUV).r;
    FragColor = vec4(uColor, a);
}
)GLSL";

bool TextHB::ensure_program(){
    if(prog) return true;
    prog = gl_compile_program(VS, FS);
    if(!prog) return false;
    uScreen_loc = glGetUniformLocation(prog, "uScreen");
    uColor_loc  = glGetUniformLocation(prog, "uColor");
    uTex_loc    = glGetUniformLocation(prog, "uTex");

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    // aPos(0), aUV(1)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float)*4, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float)*4, (void*)(sizeof(float)*2));
    glBindVertexArray(0);
    return true;
}

bool TextHB::init(const std::string& font_path, int pixel_size){
    px_size = pixel_size;

    if(!ensure_program()) return false;

    if(FT_Init_FreeType(&ft)) return false;
    if(FT_New_Face(ft, font_path.c_str(), 0, &face)) return false;
    if(FT_Set_Pixel_Sizes(face, 0, px_size)) return false;

    hb_font = hb_ft_font_create_referenced(face);
    hb_ft_font_set_funcs(hb_font); // use FT-provided metric functions
    return true;
}

void TextHB::shutdown(){
    for(auto &kv : cache){
        if(kv.second.tex) glDeleteTextures(1, &kv.second.tex);
    }
    cache.clear();
    if(hb_font){ hb_font_destroy(hb_font); hb_font = nullptr; }
    if(face){ FT_Done_Face(face); face = nullptr; }
    if(ft){ FT_Done_FreeType(ft); ft = nullptr; }
    if(vbo){ glDeleteBuffers(1, &vbo); vbo = 0; }
    if(vao){ glDeleteVertexArrays(1, &vao); vao = 0; }
    if(prog){ glDeleteProgram(prog); prog = 0; }
}

void TextHB::begin(glm::uvec2 screen_px){
    screen = screen_px;
    glUseProgram(prog);
    glUniform2f(uScreen_loc, float(screen.x), float(screen.y));
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(uTex_loc, 0);
    // Enable alpha blending for text rendering
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void TextHB::end(){
    glDisable(GL_BLEND);
    glUseProgram(0);
}

bool TextHB::load_glyph(unsigned glyph_index, GlyphTex& out){
    auto it = cache.find(glyph_index);
    if(it != cache.end()){ out = it->second; return true; }

    if(FT_Load_Glyph(face, glyph_index, FT_LOAD_RENDER)) return false;
    FT_GlyphSlot slot = face->glyph;
    FT_Bitmap& bm = slot->bitmap;

    GlyphTex gt;
    gt.w = int(bm.width);
    gt.h = int(bm.rows);
    gt.bearingX = slot->bitmap_left;
    gt.bearingY = slot->bitmap_top;
    gt.advance = slot->advance.x / 64.0f;

    glGenTextures(1, &gt.tex);
    glBindTexture(GL_TEXTURE_2D, gt.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, gt.w, gt.h, 0, GL_RED, GL_UNSIGNED_BYTE, bm.buffer);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    cache[glyph_index] = gt;
    out = gt;
    return true;
}

// --- helpers: shape and collect per-cluster advances ---
struct HBCluster {
    uint32_t byte_start = 0;  // start byte index in original UTF-8
    uint32_t byte_end   = 0;  // end byte index (exclusive), set later
    float    advance_px = 0.0f;
    bool     is_space   = false; // basic: spaces/tabs (ASCII); CJK no-space lines will be false
};

// Return clusters ordered by input order; advances already merged per cluster.
static void hb_shape_to_clusters(hb_font_t* font, std::string_view text,
                                 std::vector<HBCluster>& result) {
    result.clear();
    if (text.empty()) return;

    // create HarfBuzz buffer and feed input
    hb_buffer_t* buffer = hb_buffer_create();
    hb_buffer_add_utf8(buffer, text.data(), static_cast<int>(text.size()), 0, static_cast<int>(text.size()));
    hb_buffer_guess_segment_properties(buffer);

    // enable kerning + ligatures
    hb_feature_t active_features[] = {
        {HB_TAG('k','e','r','n'), 1, 0, ~0u},
        {HB_TAG('l','i','g','a'), 1, 0, ~0u}
    };
    hb_shape(font, buffer, active_features, sizeof(active_features) / sizeof(active_features[0]));

    // retrieve glyph info/positions
    unsigned int glyph_count = 0;
    const hb_glyph_info_t* ginfo = hb_buffer_get_glyph_infos(buffer, &glyph_count);
    const hb_glyph_position_t* gpos = hb_buffer_get_glyph_positions(buffer, &glyph_count);

    std::vector<HBCluster> temp_clusters;
    temp_clusters.reserve(glyph_count ? glyph_count : 1);

    // helper lambda to append a cluster
    auto emit_cluster = [&](uint32_t start_index, float advance_px) {
        HBCluster cluster{};
        cluster.byte_start = start_index;
        cluster.advance_px = advance_px;

        if (start_index < text.size()) {
            unsigned char c = static_cast<unsigned char>(text[start_index]);
            cluster.is_space = (c == ' ' || c == '\t');
        }
        temp_clusters.push_back(cluster);
    };

    // walk glyphs and merge by cluster id
    if (glyph_count > 0) {
        uint32_t current_id = ginfo[0].cluster;
        float accumulated = 0.0f;

        for (unsigned int i = 0; i < glyph_count; ++i) {
            uint32_t this_cluster = ginfo[i].cluster;
            if (this_cluster != current_id) {
                emit_cluster(current_id, accumulated);
                current_id = this_cluster;
                accumulated = 0.0f;
            }
            accumulated += gpos[i].x_advance / 64.0f; // convert from 26.6 fixed
        }
        emit_cluster(current_id, accumulated);
    }

    // assign byte_end by peeking at next cluster start
    for (size_t i = 0; i < temp_clusters.size(); ++i) {
        uint32_t end_pos = (i + 1 < temp_clusters.size())
            ? temp_clusters[i + 1].byte_start
            : static_cast<uint32_t>(text.size());
        temp_clusters[i].byte_end = end_pos;
    }

    result.swap(temp_clusters);
    hb_buffer_destroy(buffer);
}

float TextHB::measure_text(std::string_view s) const {
    std::vector<HBCluster> cl;
    hb_shape_to_clusters(hb_font, s, cl);
    float w = 0.0f;
    for (auto const& c : cl) w += c.advance_px;
    return w;
}

float TextHB::measure_text(std::u8string_view s8) const {
    auto p = reinterpret_cast<const char*>(s8.data());
    return measure_text(std::string_view(p, s8.size()));
}

// Greedy wrap by clusters; prefer breaking at spaces; if none, break at last fitting cluster.
void TextHB::wrap_text(std::string_view text, float max_width_px, std::vector<std::string>& lines) const {
    lines.clear();
    if (text.empty()) return;

    // process each paragraph separately (split by '\n')
    size_t start_pos = 0;
    while (start_pos <= text.size()) {
        size_t newline_pos = text.find('\n', start_pos);
        std::string_view paragraph = (newline_pos == std::string::npos)
            ? text.substr(start_pos)
            : text.substr(start_pos, newline_pos - start_pos);

        std::vector<HBCluster> clusters;
        hb_shape_to_clusters(hb_font, paragraph, clusters);

        size_t cluster_count = clusters.size();
        size_t seg_begin = 0;
        float seg_width = 0.0f;
        std::ptrdiff_t last_space = -1;

        // helper lambda to extract substring from clusters [a, b]
        auto extract_line = [&](size_t a, size_t b) -> std::string {
            if (a > b || b >= clusters.size()) return {};
            uint32_t start_idx = clusters[a].byte_start;
            uint32_t end_idx   = clusters[b].byte_end;

            // trim leading/trailing whitespace
            while (a <= b && clusters[a].is_space) {
                start_idx = clusters[a].byte_end;
                ++a;
            }
            while (b >= a && clusters[b].is_space) {
                end_idx = clusters[b].byte_start;
                --b;
            }
            if (start_idx > end_idx) start_idx = end_idx;
            return std::string(paragraph.substr(start_idx, end_idx - start_idx));
        };

        for (size_t i = 0; i < cluster_count; ++i) {
            if (clusters[i].is_space) {
                last_space = static_cast<std::ptrdiff_t>(i);
            }

            float tentative_width = seg_width + clusters[i].advance_px;
            if (tentative_width <= max_width_px) {
                seg_width = tentative_width;
                continue;
            }

            // decide where to break
            size_t break_point;
            if (last_space >= static_cast<std::ptrdiff_t>(seg_begin)) {
                break_point = static_cast<size_t>(last_space); // break at previous space
            } else {
                break_point = (i == seg_begin) ? i : (i - 1); // force break
            }

            // push current line
            lines.push_back(extract_line(seg_begin, break_point));

            // prepare next segment
            seg_begin = break_point + 1;
            while (seg_begin < cluster_count && clusters[seg_begin].is_space) {
                ++seg_begin;
            }

            // reset accumulators
            i = (seg_begin > 0) ? seg_begin - 1 : 0; // compensate loop increment
            seg_width = 0.0f;
            last_space = -1;
        }

        // handle last line in the paragraph
        if (seg_begin < cluster_count) {
            lines.push_back(extract_line(seg_begin, cluster_count - 1));
        } else if (cluster_count == 0) {
            lines.emplace_back(); // completely empty paragraph
        }

        if (newline_pos == std::string::npos) break;
        start_pos = newline_pos + 1;

        // preserve blank line separation
        if (start_pos < text.size() && text[start_pos] == '\n') {
            lines.emplace_back();
        }
    }
}

void TextHB::wrap_text(std::u8string_view s8, float max_width_px, std::vector<std::string>& out_lines) const {
    auto p = reinterpret_cast<const char*>(s8.data());
    wrap_text(std::string_view(p, s8.size()), max_width_px, out_lines);
}


void TextHB::draw_text(const std::string& utf8, float x, float y_baseline, const glm::vec3& rgb) {
    // --- HarfBuzz shaping stage ---
    hb_buffer_t* buffer = hb_buffer_create();
    hb_buffer_add_utf8(buffer, utf8.c_str(), static_cast<int>(utf8.size()), 0, static_cast<int>(utf8.size()));
    hb_buffer_guess_segment_properties(buffer); // auto-detect script, direction, language

    hb_feature_t opts[] = {
        {HB_TAG('k','e','r','n'), 1, 0, ~0u}, // kerning
        {HB_TAG('l','i','g','a'), 1, 0, ~0u}  // ligatures
    };
    hb_shape(hb_font, buffer, opts, sizeof(opts) / sizeof(opts[0]));

    unsigned int glyph_count = 0;
    const hb_glyph_info_t* glyphs = hb_buffer_get_glyph_infos(buffer, &glyph_count);
    const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &glyph_count);

    // --- OpenGL setup ---
    glUseProgram(prog);
    glUniform3f(uColor_loc, rgb.x, rgb.y, rgb.z);
    glBindVertexArray(vao);

    float cursor_x = x;
    float cursor_y = y_baseline;

    // --- draw each glyph ---
    for (unsigned int i = 0; i < glyph_count; ++i) {
        unsigned idx = glyphs[i].codepoint;
        float adv_x = positions[i].x_advance / 64.0f;
        float adv_y = positions[i].y_advance / 64.0f;
        float off_x = positions[i].x_offset  / 64.0f;
        float off_y = positions[i].y_offset  / 64.0f;

        GlyphTex glyph_tex;
        if (!load_glyph(idx, glyph_tex)) continue;

        float left   = cursor_x + off_x + static_cast<float>(glyph_tex.bearingX);
        float top    = cursor_y - off_y - static_cast<float>(glyph_tex.bearingY);
        float right  = left + static_cast<float>(glyph_tex.w);
        float bottom = top  + static_cast<float>(glyph_tex.h);

        // build quad (two triangles, six vertices)
        const float quad[24] = {
            left,  top,    0.0f, 0.0f,
            right, top,    1.0f, 0.0f,
            right, bottom, 1.0f, 1.0f,

            left,  top,    0.0f, 0.0f,
            right, bottom, 1.0f, 1.0f,
            left,  bottom, 0.0f, 1.0f
        };

        glBindTexture(GL_TEXTURE_2D, glyph_tex.tex);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        cursor_x += adv_x;
        cursor_y += adv_y;
    }

    // --- cleanup ---
    hb_buffer_destroy(buffer);
    glBindVertexArray(0);
}

void TextHB::draw_text(std::string_view s, float x, float y, const glm::vec3& rgb) {
    draw_text(std::string(s), x, y, rgb);
}

void TextHB::draw_text(std::u8string_view s8, float x, float y, const glm::vec3& rgb) {
    const char* p = reinterpret_cast<const char*>(s8.data());
    draw_text(std::string(p, p + s8.size()), x, y, rgb);
}

void TextHB::draw_text(const char* s, float x, float y, const glm::vec3& rgb) {
    draw_text(std::string_view(s), x, y, rgb);
}