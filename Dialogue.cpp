//@ChatGPT used to create this file
//Credit: jialand
//reference: https://github.com/jialand/TheMuteLift/tree/main
#include "Dialogue.hpp"
#include <fstream>
#include <sstream>
#include <string>
#include <cctype>

static inline std::string trim(std::string s){
    size_t a=0,b=s.size();
    while(a<b && std::isspace((unsigned char)s[a])) ++a;
    while(b>a && std::isspace((unsigned char)s[b-1])) --b;
    return s.substr(a,b-a);
}

const DialogueNode* DialogueGraph::get(const std::string& id) const {
    auto it = nodes.find(id);
    if(it==nodes.end()) return nullptr;
    return &it->second;
}

bool DialogueGraph::load_from_file(const std::string& path, std::string* err){
    nodes.clear();
    start_id.clear();

    std::ifstream fin(path, std::ios::binary);
    if(!fin){
        if(err) *err = "Failed to open: " + path;
        return false;
    }

    std::string line;
    DialogueNode cur;
    bool in_state = false;
    int line_no = 0;
    auto flush_state = [&](){
        if(in_state){
            if(cur.id.empty()){
                // invalid; ignore
            }else{
                nodes[cur.id] = cur;
            }
            cur = DialogueNode{};
            in_state = false;
        }
    };

    while (std::getline(fin, line)) {
        ++line_no;

        // strip Windows-style line endings
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        std::string trimmed = trim(line);

        // skip empty and comment lines
        if (trimmed.empty()) continue;
        if (trimmed.rfind("//", 0) == 0 || trimmed.rfind("#", 0) == 0) continue;

        // starting point
        if (trimmed.rfind("start:", 0) == 0) {
            start_id = trim(trimmed.substr(6));
            continue;
        }

        // begin new state
        if (trimmed.rfind("state:", 0) == 0) {
            flush_state();
            cur = DialogueNode(); // reset
            cur.id = trim(trimmed.substr(6));
            in_state = true;
            continue;
        }

        // dialogue text block
        if (trimmed == "text:") {
            std::string marker;
            if (!std::getline(fin, marker)) {
                if (err) *err = "Unexpected EOF after 'text:' (line " + std::to_string(line_no) + ")";
                return false;
            }
            ++line_no;
            if (!marker.empty() && marker.back() == '\r') marker.pop_back();

            if (trim(marker) != "<<<") {
                if (err) *err = "Expected '<<<' after text: at line " + std::to_string(line_no);
                return false;
            }

            std::ostringstream buffer;
            std::string text_line;
            while (std::getline(fin, text_line)) {
                ++line_no;
                if (!text_line.empty() && text_line.back() == '\r') text_line.pop_back();
                if (trim(text_line) == ">>>") break;
                buffer << text_line << "\n";
            }

            cur.text = buffer.str();
            if (!cur.text.empty() && cur.text.back() == '\n') {
                cur.text.pop_back();
            }
            continue;
        }

        // option line
        if (trimmed.rfind("option:", 0) == 0) {
            std::string payload = trim(trimmed.substr(7));
            size_t sep = payload.find("->");
            if (sep == std::string::npos) {
                if (err) *err = "Malformed option, missing '->' at line " + std::to_string(line_no);
                return false;
            }

            std::string label = trim(payload.substr(0, sep));
            std::string next  = trim(payload.substr(sep + 2));

            if (label.empty() || next.empty()) {
                if (err) *err = "Option missing label or target at line " + std::to_string(line_no);
                return false;
            }

            cur.options.emplace_back(DialogueOption{label, next});
            continue;
        }

        // end of state
        if (trimmed == "endstate") {
            flush_state();
            continue;
        }
    }

    flush_state();

    if(start_id.empty()){
        // use first state if available
        if(!nodes.empty()) start_id = nodes.begin()->first;
    }
    if(start_id.empty()){
        if(err) *err = "No states loaded.";
        return false;
    }

    return true;
}
