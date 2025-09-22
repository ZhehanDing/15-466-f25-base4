//@ChatGPT used to create this file
//Credit: jialand
//reference: https://github.com/jialand/TheMuteLift/tree/main

#pragma once
#include <string>
#include <unordered_map>
#include <vector>

struct DialogueOption {
    std::string label;   // shown text
    std::string next;    // next state id; use "END" to finish
};

struct DialogueNode {
    std::string id;
    std::string text;                // may contain '\n'
    std::vector<DialogueOption> options;
};

struct DialogueGraph {
    std::unordered_map<std::string, DialogueNode> nodes;
    std::string start_id;

    bool load_from_file(const std::string& path, std::string* err); // path = data_path("dialogues.txt")
    const DialogueNode* get(const std::string& id) const;
};
