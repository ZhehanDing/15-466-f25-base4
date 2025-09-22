//ChatGPT used to create this file
//Credit: jialand
//reference: https://github.com/jialand/TheMuteLift/tree/main
#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include <string_view>


// Forward declarations:
struct hb_font_t;
typedef struct FT_LibraryRec_* FT_Library;
typedef struct FT_FaceRec_*    FT_Face;

struct GlyphTex {
    unsigned tex = 0;
    int w = 0, h = 0;
    int bearingX = 0, bearingY = 0;
    float advance = 0.0f; // in pixels
};

class TextHB {
public:
    // screen_px: used in draw() to convert to clip space
    bool init(const std::string& font_path, int pixel_size);
    void shutdown();

    // Set screen pixel size (pass drawable_size each frame)
    void begin(glm::uvec2 screen_px);
    void end();

    // Baseline position (x, y_baseline), color in [0,1]
    void draw_text(const std::string& utf8, float x, float y_baseline, const glm::vec3& rgb);
    void draw_text(std::string_view utf8, float x, float y_baseline, const glm::vec3& rgb);
    void draw_text(std::u8string_view utf8, float x, float y_baseline, const glm::vec3& rgb);
    void draw_text(const char* utf8, float x, float y_baseline, const glm::vec3& rgb);

    // --- Measuring & Wrapping ---
    float measure_text(std::string_view utf8) const;
    float measure_text(std::u8string_view utf8) const;

    void wrap_text(std::string_view utf8, float max_width_px, std::vector<std::string>& out_lines) const;
    void wrap_text(std::u8string_view utf8, float max_width_px, std::vector<std::string>& out_lines) const;


private:
    // GL program + VAO/VBO
    unsigned prog = 0;
    int uScreen_loc = -1, uColor_loc = -1, uTex_loc = -1;
    unsigned vao = 0, vbo = 0;

    // FreeType / HarfBuzz
    FT_Library ft = nullptr;
    FT_Face face = nullptr;
    hb_font_t* hb_font = nullptr;
    int px_size = 32;

    // glyph cache
    std::unordered_map<unsigned,int> glyph_used_; // only used for cleanup order (optional)
    std::unordered_map<unsigned, GlyphTex> cache;

    glm::uvec2 screen = glm::uvec2(1280,720);

    bool load_glyph(unsigned glyph_index, GlyphTex& out); // FT_Load + upload texture
    bool ensure_program(); // compile shader program
};