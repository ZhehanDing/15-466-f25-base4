//ChatGPT used to create this file
//Credit: jialand
//reference: https://github.com/jialand/TheMuteLift/tree/main, https://github.com/arililoia-cmu/15-466-f23-base4


#include "PlayMode.hpp"

#include "LitColorTextureProgram.hpp"

#include "DrawLines.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <random>

GLuint hexapod_meshes_for_lit_color_texture_program = 0;
Load< MeshBuffer > hexapod_meshes(LoadTagDefault, []() -> MeshBuffer const * {
	MeshBuffer const *ret = new MeshBuffer(data_path("hexapod.pnct"));
	hexapod_meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret;
});

Load< Scene > hexapod_scene(LoadTagDefault, []() -> Scene const * {
	return new Scene(data_path("hexapod.scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name){
		Mesh const &mesh = hexapod_meshes->lookup(mesh_name);

		scene.drawables.emplace_back(transform);
		Scene::Drawable &drawable = scene.drawables.back();

		drawable.pipeline = lit_color_texture_program_pipeline;

		drawable.pipeline.vao = hexapod_meshes_for_lit_color_texture_program;
		drawable.pipeline.type = mesh.type;
		drawable.pipeline.start = mesh.start;
		drawable.pipeline.count = mesh.count;

	});
});

Load< Sound::Sample > dusty_floor_sample(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("dusty-floor.opus"));
});


Load< Sound::Sample > honk_sample(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("honk.wav"));
});

void PlayMode::move_selection(int delta) { //@With help of GPT
    if (finished) return;
    const DialogueNode* node = dialog.get(cur_state);
    if(!node || node->options.empty()) return;
    int n = (int)node->options.size();
    selected = (selected + (delta % n) + n) % n; // wrap
}

void PlayMode::confirm_selection() { //@With help of GPT
    if (finished) return;
    const DialogueNode* node = dialog.get(cur_state);
    if(!node) return;

    if(node->options.empty()){
        finished = true; // or stay
        return;
    }
    const auto& opt = node->options[selected];
    // prevent choosing locked option unless key is truex
    if (opt.label.find("[LOCKED]") == 0 && !key) {
        // do nothing, maybe play error sound later
        return;
    }
    if(opt.next == "END"){ finished = true; return; }

    // jump:
    cur_state = opt.next;
    selected = 0;
    //
    if (cur_state == "end") {
        key = true;  // set key true when reaching "end"
    }
    
}

PlayMode::PlayMode() : scene(*hexapod_scene) {
	//text @GPT
	text = std::make_unique<TextHB>();
    bool ok = text->init(data_path("PlayfairDisplay-VariableFont_wght.ttf"), 32);
    assert(ok && "Failed to init TextHB");

    std::string err;
    ok = dialog.load_from_file(data_path("dialogues.txt"), &err);
    assert(ok && "Failed to load dialogues.txt");
    if(!ok) SDL_Log("dialog load error: %s", err.c_str());

    cur_state = dialog.start_id;
    selected = 0;
    finished = false;


	//get pointer to camera for convenience:
	if (scene.cameras.size() != 1) throw std::runtime_error("Expecting scene to have exactly one camera, but it has " + std::to_string(scene.cameras.size()));
	camera = &scene.cameras.front();

}

PlayMode::~PlayMode() {
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {
    if (evt.type == SDL_EVENT_KEY_DOWN) {
        if (evt.key.key == SDLK_ESCAPE) {
            SDL_SetWindowRelativeMouseMode(Mode::window, false);
            return true;
        } else if (evt.key.key == SDLK_RETURN) {
            confirm_selection();
            return true;
        }
    }
    //GPT help debug
    else if (evt.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (evt.button.button == SDL_BUTTON_LEFT) {
            int my = evt.button.y;

            const DialogueNode* node = dialog.get(cur_state);
            if (node) {
                // skip body lines first
                std::vector<std::string> body_lines;
                text->wrap_text(node->text, float(window_size.x) - 128.0f, body_lines);
                float y = 100.0f + 42.0f * (float)body_lines.size() + 30.0f;

                for (int i = 0; i < (int)node->options.size(); ++i) {
                    std::vector<std::string> opt_lines;
                    text->wrap_text(node->options[i].label, float(window_size.x) - 156.0f, opt_lines);

                    float opt_height = 42.0f * (float)opt_lines.size();
					float top = y- (42.0f*0.5f);
                    if (my >= top && my <= y) {
                        selected = i;          // set selection
                        confirm_selection();   // confirm immediately
                        return true;
                    }
                    y += opt_height;
                }
            }
        }
    }
    else if (evt.type == SDL_EVENT_MOUSE_MOTION) {
        int my = evt.motion.y;

        const DialogueNode* node = dialog.get(cur_state);
        if (node) {
            std::vector<std::string> body_lines;
            text->wrap_text(node->text, float(window_size.x) - 128.0f, body_lines);
            //GPT help recheck range
            float y = 100.0f + 42.0f * (float)body_lines.size() + 30.0f;

            for (int i = 0; i < (int)node->options.size(); ++i) {
                std::vector<std::string> opt_lines;
                text->wrap_text(node->options[i].label, float(window_size.x) - 156.0f, opt_lines);

                float opt_height = 42.0f * (float)opt_lines.size();
				float top = y- (42.0f *0.5f);
				float bottom = y + opt_height-(42.0f*0.5f);
                if (my >= top && my <= bottom) {
                    selected = i; // just hover highlight
                    break;
                }
                y += opt_height;
            }
        }
    }

	return false;
}

void PlayMode::update(float elapsed) {

	left.downs = 0;
	right.downs = 0;
	up.downs = 0;
	down.downs = 0;
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	//update camera aspect ratio for drawable:
	camera->aspect = float(drawable_size.x) / float(drawable_size.y);

	// black screen
    glClearColor(0.96f, 0.87f, 0.70f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    text->begin(drawable_size);

    const float margin_l = 64.0f;
    const float margin_r = 64.0f;
    const float start_x  = margin_l;
    const float start_y  = 100.0f;   // baseline of first line
    const float line_h   = 42.0f;
    const float opt_gap  = 30.0f;

    float max_width = float(drawable_size.x) - margin_l - margin_r;

    const DialogueNode* node = dialog.get(cur_state);
    if (node) {
        // --- body with auto wrap ---
        std::vector<std::string> body_lines;
        text->wrap_text(node->text, max_width, body_lines);

        float y = start_y;
        for (auto const& ln : body_lines) {
            if (!ln.empty())
                text->draw_text(ln, start_x, y, glm::vec3(0.0f,0.0f,0.0f));
            y += line_h;
        }

        // --- options with auto wrap
        y += opt_gap;
        for (int i = 0; i < (int)node->options.size(); ++i) {
            std::string label = node->options[i].label;
            // if this is a locked option and key is false, display it as locked
            if (label.find("[LOCKED]") == 0 && !key) {
                label = "";
            } else if (label.find("[LOCKED]") == 0 && key) {
                // remove the [LOCKED] tag when key is true
                label = label.substr(8); // skip "[LOCKED]"
            }
            bool sel = (i == selected);
            glm::vec3 color = sel ? glm::vec3(0.8f, 0.1f, 0.1f) : glm::vec3(0.0f,0.0f,0.0f);

            std::vector<std::string> opt_lines;
            text->wrap_text(label, max_width - 28.0f, opt_lines); 

            bool first = true;
            for (auto const& ln : opt_lines) {
                if (first) {
                    if (sel) text->draw_text("  ", start_x, y, color);
                    else     text->draw_text("  ", start_x, y, color);
                    text->draw_text(ln, start_x + 28.0f, y, color);
                    first = false;
                } else {
                    text->draw_text("  ", start_x, y, color);
                    text->draw_text(ln, start_x + 28.0f, y, color);
                }
                y += line_h;
            }
        }
    } else {
        text->draw_text("Dialogue node not found.", start_x, start_y, glm::vec3(1.0f,0.4f,0.4f));
    }

    text->end();

	GL_ERRORS();
}